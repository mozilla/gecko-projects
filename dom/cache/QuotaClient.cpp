/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/QuotaClient.h"

#include "mozilla/dom/cache/Manager.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIFile.h"
#include "nsISimpleEnumerator.h"
#include "nsThreadUtils.h"

namespace {

using mozilla::Atomic;
using mozilla::dom::ContentParentId;
using mozilla::dom::cache::DirPaddingFile;
using mozilla::dom::cache::Manager;
using mozilla::dom::cache::QuotaInfo;
using mozilla::dom::quota::AssertIsOnIOThread;
using mozilla::dom::quota::Client;
using mozilla::dom::quota::PersistenceType;
using mozilla::dom::quota::QuotaManager;
using mozilla::dom::quota::UsageInfo;
using mozilla::ipc::AssertIsOnBackgroundThread;
using mozilla::MutexAutoLock;
using mozilla::Unused;

static nsresult
GetBodyUsage(nsIFile* aDir, const Atomic<bool>& aCanceled,
             UsageInfo* aUsageInfo)
{
  AssertIsOnIOThread();

  nsCOMPtr<nsISimpleEnumerator> entries;
  nsresult rv = aDir->GetDirectoryEntries(getter_AddRefs(entries));
  if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

  bool hasMore;
  while (NS_SUCCEEDED(rv = entries->HasMoreElements(&hasMore)) && hasMore &&
         !aCanceled) {
    nsCOMPtr<nsISupports> entry;
    rv = entries->GetNext(getter_AddRefs(entry));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    nsCOMPtr<nsIFile> file = do_QueryInterface(entry);

    bool isDir;
    rv = file->IsDirectory(&isDir);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    if (isDir) {
      rv = GetBodyUsage(file, aCanceled, aUsageInfo);
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
      continue;
    }

    int64_t fileSize;
    rv = file->GetFileSize(&fileSize);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
    MOZ_DIAGNOSTIC_ASSERT(fileSize >= 0);

    aUsageInfo->AppendToFileUsage(fileSize);
  }

  return NS_OK;
}

class CacheQuotaClient final : public Client
{
  static CacheQuotaClient* sInstance;

public:
  CacheQuotaClient()
  : mDirPaddingFileMutex("DOMCacheQuotaClient.mDirPaddingFileMutex")
  {
    AssertIsOnBackgroundThread();
    MOZ_DIAGNOSTIC_ASSERT(!sInstance);
    sInstance = this;
  }

  static CacheQuotaClient*
  Get()
  {
    MOZ_DIAGNOSTIC_ASSERT(sInstance);
    return sInstance;
  }

  virtual Type
  GetType() override
  {
    return DOMCACHE;
  }

  virtual nsresult
  InitOrigin(PersistenceType aPersistenceType, const nsACString& aGroup,
             const nsACString& aOrigin, const AtomicBool& aCanceled,
             UsageInfo* aUsageInfo) override
  {
    AssertIsOnIOThread();

    // The QuotaManager passes a nullptr UsageInfo if there is no quota being
    // enforced against the origin.
    if (!aUsageInfo) {
      return NS_OK;
    }

    return GetUsageForOrigin(aPersistenceType, aGroup, aOrigin, aCanceled,
                             aUsageInfo);
  }

  virtual nsresult
  GetUsageForOrigin(PersistenceType aPersistenceType, const nsACString& aGroup,
                    const nsACString& aOrigin, const AtomicBool& aCanceled,
                    UsageInfo* aUsageInfo) override
  {
    AssertIsOnIOThread();
    MOZ_DIAGNOSTIC_ASSERT(aUsageInfo);

    QuotaManager* qm = QuotaManager::Get();
    MOZ_DIAGNOSTIC_ASSERT(qm);

    nsCOMPtr<nsIFile> dir;
    nsresult rv = qm->GetDirectoryForOrigin(aPersistenceType, aOrigin,
                                            getter_AddRefs(dir));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = dir->Append(NS_LITERAL_STRING(DOMCACHE_DIRECTORY_NAME));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    int64_t paddingSize = 0;
    {
      // If the tempoary file still exists after locking, it means the previous
      // action fails, so restore the padding file.
      MutexAutoLock lock(mDirPaddingFileMutex);

      if (mozilla::dom::cache::
          DirectoryPaddingFileExists(dir, DirPaddingFile::TMP_FILE) ||
          NS_WARN_IF(NS_FAILED(mozilla::dom::cache::
                               LockedDirectoryPaddingGet(dir,
                                                         &paddingSize)))) {
        nsCOMPtr<mozIStorageConnection> conn;
        QuotaInfo quotaInfo;
        quotaInfo.mGroup = aGroup;
        quotaInfo.mOrigin = aOrigin;
        rv = mozilla::dom::cache::
             OpenDBConnection(quotaInfo, dir, getter_AddRefs(conn));
        if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

        rv = mozilla::dom::cache::LockedDirectoryPaddingRestore(dir, conn);
        if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

        rv = mozilla::dom::cache::LockedDirectoryPaddingGet(dir, &paddingSize);
        if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
      }
    }

    aUsageInfo->AppendToFileUsage(paddingSize);

    nsCOMPtr<nsISimpleEnumerator> entries;
    rv = dir->GetDirectoryEntries(getter_AddRefs(entries));
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    bool hasMore;
    while (NS_SUCCEEDED(rv = entries->HasMoreElements(&hasMore)) && hasMore &&
           !aCanceled) {
      nsCOMPtr<nsISupports> entry;
      rv = entries->GetNext(getter_AddRefs(entry));
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

      nsCOMPtr<nsIFile> file = do_QueryInterface(entry);

      nsAutoString leafName;
      rv = file->GetLeafName(leafName);
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

      bool isDir;
      rv = file->IsDirectory(&isDir);
      if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

      if (isDir) {
        if (leafName.EqualsLiteral("morgue")) {
          rv = GetBodyUsage(file, aCanceled, aUsageInfo);
          if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
        } else {
          NS_WARNING("Unknown Cache directory found!");
        }

        continue;
      }

      // Ignore transient sqlite files and marker files
      if (leafName.EqualsLiteral("caches.sqlite-journal") ||
          leafName.EqualsLiteral("caches.sqlite-shm") ||
          leafName.Find(NS_LITERAL_CSTRING("caches.sqlite-mj"), false, 0, 0) == 0 ||
          leafName.EqualsLiteral("context_open.marker")) {
        continue;
      }

      if (leafName.EqualsLiteral("caches.sqlite") ||
          leafName.EqualsLiteral("caches.sqlite-wal")) {
        int64_t fileSize;
        rv = file->GetFileSize(&fileSize);
        if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }
        MOZ_DIAGNOSTIC_ASSERT(fileSize >= 0);

        aUsageInfo->AppendToDatabaseUsage(fileSize);
        continue;
      }

      // Ignore directory padding file
      if (leafName.EqualsLiteral(PADDING_FILE_NAME) ||
          leafName.EqualsLiteral(PADDING_TMP_FILE_NAME)) {
        continue;
      }

      NS_WARNING("Unknown Cache file found!");
    }

    return NS_OK;
  }

  virtual void
  OnOriginClearCompleted(PersistenceType aPersistenceType,
                         const nsACString& aOrigin) override
  {
    // Nothing to do here.
  }

  virtual void
  ReleaseIOThreadObjects() override
  {
    // Nothing to do here as the Context handles cleaning everything up
    // automatically.
  }

  virtual void
  AbortOperations(const nsACString& aOrigin) override
  {
    AssertIsOnBackgroundThread();

    Manager::Abort(aOrigin);
  }

  virtual void
  AbortOperationsForProcess(ContentParentId aContentParentId) override
  {
    // The Cache and Context can be shared by multiple client processes.  They
    // are not exclusively owned by a single process.
    //
    // As far as I can tell this is used by QuotaManager to abort operations
    // when a particular process goes away.  We definitely don't want this
    // since we are shared.  Also, the Cache actor code already properly
    // handles asynchronous actor destruction when the child process dies.
    //
    // Therefore, do nothing here.
  }

  virtual void
  StartIdleMaintenance() override
  { }

  virtual void
  StopIdleMaintenance() override
  { }

  virtual void
  ShutdownWorkThreads() override
  {
    AssertIsOnBackgroundThread();

    // spins the event loop and synchronously shuts down all Managers
    Manager::ShutdownAll();
  }

  nsresult
  UpgradeStorageFrom2_0To3_0(nsIFile* aDirectory) override
  {
    AssertIsOnIOThread();
    MOZ_DIAGNOSTIC_ASSERT(aDirectory);

    MutexAutoLock lock(mDirPaddingFileMutex);

    nsresult rv = mozilla::dom::cache::LockedDirectoryPaddingInit(aDirectory);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    return rv;
  }

  // static
  template<typename Callable>
  nsresult
  MaybeUpdatePaddingFileInternal(nsIFile* aBaseDir,
                                 mozIStorageConnection* aConn,
                                 const int64_t aIncreaseSize,
                                 const int64_t aDecreaseSize,
                                 Callable aCommitHook)
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
    MOZ_DIAGNOSTIC_ASSERT(aConn);
    MOZ_DIAGNOSTIC_ASSERT(aIncreaseSize >= 0);
    MOZ_DIAGNOSTIC_ASSERT(aDecreaseSize >= 0);

    nsresult rv;

    // Temporary should be removed at the end of each action. If not, it means
    // the failure happened.
    bool temporaryPaddingFileExist =
      mozilla::dom::cache::DirectoryPaddingFileExists(aBaseDir,
                                                      DirPaddingFile::TMP_FILE);

    if (aIncreaseSize == aDecreaseSize && !temporaryPaddingFileExist) {
      // Early return here, since most cache actions won't modify padding size.
      rv = aCommitHook();
      Unused << NS_WARN_IF(NS_FAILED(rv));
      return rv;
    }

    {
      MutexAutoLock lock(mDirPaddingFileMutex);
      rv =
        mozilla::dom::cache::
        LockedUpdateDirectoryPaddingFile(aBaseDir, aConn, aIncreaseSize,
                                         aDecreaseSize,
                                         temporaryPaddingFileExist);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        mozilla::dom::cache::
        LockedDirectoryPaddingDeleteFile(aBaseDir, DirPaddingFile::TMP_FILE);
        return rv;
      }

      rv = aCommitHook();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        mozilla::dom::cache::
        LockedDirectoryPaddingDeleteFile(aBaseDir, DirPaddingFile::TMP_FILE);
        return rv;
      }

      rv = mozilla::dom::cache::LockedDirectoryPaddingFinalizeWrite(aBaseDir);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // Force restore file next time.
        mozilla::dom::cache::
        LockedDirectoryPaddingDeleteFile(aBaseDir, DirPaddingFile::FILE);
      }
    }

    return rv;
  }

  // static
  nsresult
  RestorePaddingFileInternal(nsIFile* aBaseDir, mozIStorageConnection* aConn)
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
    MOZ_DIAGNOSTIC_ASSERT(aConn);

    MutexAutoLock lock(mDirPaddingFileMutex);

    nsresult rv =
      mozilla::dom::cache::LockedDirectoryPaddingRestore(aBaseDir, aConn);
    Unused << NS_WARN_IF(NS_FAILED(rv));

    return rv;
  }

  // static
  nsresult
  WipePaddingFileInternal(const QuotaInfo& aQuotaInfo, nsIFile* aBaseDir)
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_DIAGNOSTIC_ASSERT(aBaseDir);

    MutexAutoLock lock(mDirPaddingFileMutex);

    // Remove temporary file if we have one.
    nsresult rv =
      mozilla::dom::cache::
      LockedDirectoryPaddingDeleteFile(aBaseDir, DirPaddingFile::TMP_FILE);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    MOZ_DIAGNOSTIC_ASSERT(mozilla::dom::cache::
                          DirectoryPaddingFileExists(aBaseDir,
                                                     DirPaddingFile::FILE));

    int64_t paddingSize = 0;
    rv = mozilla::dom::cache::LockedDirectoryPaddingGet(aBaseDir, &paddingSize);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      // If read file fail, there is nothing we can do to recover the file.
      NS_WARNING("Cannnot read padding size from file!");
      paddingSize = 0;
    }

    if (paddingSize > 0) {
      mozilla::dom::cache::DecreaseUsageForQuotaInfo(aQuotaInfo, paddingSize);
    }

    rv = mozilla::dom::cache::
         LockedDirectoryPaddingDeleteFile(aBaseDir, DirPaddingFile::FILE);
    if (NS_WARN_IF(NS_FAILED(rv))) { return rv; }

    rv = mozilla::dom::cache::LockedDirectoryPaddingInit(aBaseDir);
    Unused << NS_WARN_IF(NS_FAILED(rv));

    return rv;
  }

private:
  ~CacheQuotaClient()
  {
    AssertIsOnBackgroundThread();
    MOZ_DIAGNOSTIC_ASSERT(sInstance == this);

    sInstance = nullptr;
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CacheQuotaClient, override)

  // Mutex lock to protect directroy padding files. It should only be acquired
  // in DOM Cache IO threads and Quota IO thread.
  mozilla::Mutex mDirPaddingFileMutex;
};

// static
CacheQuotaClient* CacheQuotaClient::sInstance = nullptr;

} // namespace

namespace mozilla {
namespace dom {
namespace cache {

// static
already_AddRefed<quota::Client> CreateQuotaClient()
{
  AssertIsOnBackgroundThread();

  RefPtr<CacheQuotaClient> ref = new CacheQuotaClient();
  return ref.forget();
}

// static
template<typename Callable>
nsresult
MaybeUpdatePaddingFile(nsIFile* aBaseDir,
                       mozIStorageConnection* aConn,
                       const int64_t aIncreaseSize,
                       const int64_t aDecreaseSize,
                       Callable aCommitHook)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
  MOZ_DIAGNOSTIC_ASSERT(aConn);
  MOZ_DIAGNOSTIC_ASSERT(aIncreaseSize >= 0);
  MOZ_DIAGNOSTIC_ASSERT(aDecreaseSize >= 0);

  RefPtr<CacheQuotaClient> cacheQuotaClient = CacheQuotaClient::Get();
  MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

  nsresult rv =
    cacheQuotaClient->MaybeUpdatePaddingFileInternal(aBaseDir, aConn,
                                                     aIncreaseSize,
                                                     aDecreaseSize,
                                                     aCommitHook);
  Unused << NS_WARN_IF(NS_FAILED(rv));

  return rv;
}

// static
nsresult
RestorePaddingFile(nsIFile* aBaseDir, mozIStorageConnection* aConn)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
  MOZ_DIAGNOSTIC_ASSERT(aConn);

  RefPtr<CacheQuotaClient> cacheQuotaClient = CacheQuotaClient::Get();
  MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

  nsresult rv =
    cacheQuotaClient->RestorePaddingFileInternal(aBaseDir, aConn);
  Unused << NS_WARN_IF(NS_FAILED(rv));

  return rv;
}

// static
nsresult
WipePaddingFile(const QuotaInfo& aQuotaInfo, nsIFile* aBaseDir)
{
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);

  RefPtr<CacheQuotaClient> cacheQuotaClient = CacheQuotaClient::Get();
  MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

  nsresult rv =
    cacheQuotaClient->WipePaddingFileInternal(aQuotaInfo, aBaseDir);
  Unused << NS_WARN_IF(NS_FAILED(rv));

  return rv;
}
} // namespace cache
} // namespace dom
} // namespace mozilla
