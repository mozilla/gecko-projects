/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * This file does not use many of the features Firefox provides such as
 * nsAString and nsIFile because code in this file will be included not only
 * in Firefox, but also in the Mozilla Maintenance Service, the Mozilla
 * Maintenance Service installer, and TestAUSHelper.
 */

#include <ctime>
#include <process.h>

#include <cinttypes>
#include <cwchar>
#include <string>
#include "city.h"
#include "commonupdatedir.h"
#include "updatedefines.h"

#ifdef XP_WIN
#  include <accctrl.h>
#  include <aclapi.h>
#  include <cstdarg>
#  include <errno.h>
#  include <objbase.h>
#  include <shellapi.h>
#  include <shlobj.h>
#  include <strsafe.h>
#  include <winerror.h>
#  include "nsWindowsHelpers.h"
#  include "updateutils_win.h"
#endif

#ifdef XP_WIN
// This is the name of the directory to be put in the application data directory
// if no vendor or application name is specified.
// (i.e. C:\ProgramData\<FALLBACK_VENDOR_NAME>)
#  define FALLBACK_VENDOR_NAME "Mozilla"
// This describes the directory between the "Mozilla" directory and the install
// path hash (i.e. C:\ProgramData\Mozilla\<UPDATE_PATH_MID_DIR_NAME>\<hash>)
#  define UPDATE_PATH_MID_DIR_NAME "updates"
// This describes the directory between the update directory and the patch
// directory.
// (i.e. C:\ProgramData\Mozilla\updates\<hash>\<UPDATE_SUBDIRECTORY>\0)
#  define UPDATE_SUBDIRECTORY "updates"
// This defines the leaf update directory, where the MAR file is downloaded to
// (i.e. C:\ProgramData\Mozilla\updates\<hash>\updates\<PATCH_DIRECTORY>)
#  define PATCH_DIRECTORY "0"
// This defines the prefix of files created to lock a directory
#  define LOCK_FILE_PREFIX "mozlock."

enum class WhichUpdateDir {
  CommonAppData,
  UserAppData,
};

/**
 * This is a very simple string class.
 *
 * This class has some substantial limitations for the sake of simplicity. It
 * has no support whatsoever for modifying a string that already has data. There
 * is, therefore, no append function and no support for automatically resizing
 * strings.
 *
 * Error handling is also done in a slightly unusual manner. If there is ever
 * a failure allocating or assigning to a string, it will do the simplest
 * possible recovery: truncate itself to 0-length.
 * This coupled with the fact that the length is cached means that an effective
 * method of error checking is to attempt assignment and then check the length
 * of the result.
 */
class SimpleAutoString {
 private:
  size_t mLength;
  // Unique pointer frees the buffer when the class is deleted or goes out of
  // scope.
  mozilla::UniquePtr<wchar_t[]> mString;

  /**
   * Allocates enough space to store a string of the specified length.
   */
  bool AllocLen(size_t len) {
    mString = mozilla::MakeUnique<wchar_t[]>(len + 1);
    return mString.get() != nullptr;
  }

  /**
   * Allocates a buffer of the size given.
   */
  bool AllocSize(size_t size) {
    mString = mozilla::MakeUnique<wchar_t[]>(size);
    return mString.get() != nullptr;
  }

 public:
  SimpleAutoString() : mLength(0) {}

  /*
   * Allocates enough space for a string of the given length and formats it as
   * an empty string.
   */
  bool AllocEmpty(size_t len) {
    bool success = AllocLen(len);
    Truncate();
    return success;
  }

  /**
   * These functions can potentially return null if no buffer has yet been
   * allocated. After changing a string retrieved with MutableString, the Check
   * method should be called to synchronize other members (ex: mLength) with the
   * new buffer.
   */
  wchar_t* MutableString() { return mString.get(); }
  const wchar_t* String() const { return mString.get(); }

  size_t Length() const { return mLength; }

  /**
   * This method should be called after manually changing the string's buffer
   * via MutableString to synchronize other members (ex: mLength) with the
   * new buffer.
   * Returns true if the string is now in a valid state.
   */
  bool Check() {
    mLength = wcslen(mString.get());
    return true;
  }

  void SwapBufferWith(mozilla::UniquePtr<wchar_t[]>& other) {
    mString.swap(other);
    if (mString) {
      mLength = wcslen(mString.get());
    } else {
      mLength = 0;
    }
  }

  void Swap(SimpleAutoString& other) {
    mString.swap(other.mString);
    size_t newLength = other.mLength;
    other.mLength = mLength;
    mLength = newLength;
  }

  /**
   * Truncates the string to the length specified. This must not be greater than
   * or equal to the size of the string's buffer.
   */
  void Truncate(size_t len = 0) {
    if (len > mLength) {
      return;
    }
    mLength = len;
    if (mString) {
      mString.get()[len] = L'\0';
    }
  }

  /**
   * Assigns a string and ensures that the resulting string is valid and has its
   * length set properly.
   *
   * Note that although other similar functions in this class take length, this
   * function takes buffer size instead because it is intended to be follow the
   * input convention of sprintf.
   *
   * Returns the new length, which will be 0 if there was any failure.
   *
   * This function does no allocation or reallocation. If the buffer is not
   * large enough to hold the new string, the call will fail.
   */
  size_t AssignSprintf(size_t bufferSize, const wchar_t* format, ...) {
    va_list ap;
    va_start(ap, format);
    size_t returnValue = AssignVsprintf(bufferSize, format, ap);
    va_end(ap);
    return returnValue;
  }
  /**
   * Same as the above, but takes a va_list like vsprintf does.
   */
  size_t AssignVsprintf(size_t bufferSize, const wchar_t* format, va_list ap) {
    if (!mString) {
      Truncate();
      return 0;
    }

    int charsWritten = vswprintf(mString.get(), bufferSize, format, ap);
    if (charsWritten < 0 || static_cast<size_t>(charsWritten) >= bufferSize) {
      // charsWritten does not include the null terminator. If charsWritten is
      // equal to the buffer size, we do not have a null terminator nor do we
      // have room for one.
      Truncate();
      return 0;
    }
    mString.get()[charsWritten] = L'\0';

    mLength = charsWritten;
    return mLength;
  }

  /**
   * Allocates enough space for the string and assigns a value to it with
   * sprintf. Takes, as an argument, the maximum length that the string is
   * expected to use (which, after adding 1 for the null byte, is the amount of
   * space that will be allocated).
   *
   * Returns the new length, which will be 0 on any failure.
   */
  size_t AllocAndAssignSprintf(size_t maxLength, const wchar_t* format, ...) {
    if (!AllocLen(maxLength)) {
      Truncate();
      return 0;
    }
    va_list ap;
    va_start(ap, format);
    size_t charsWritten = AssignVsprintf(maxLength + 1, format, ap);
    va_end(ap);
    return charsWritten;
  }

  /*
   * Allocates enough for the formatted text desired. Returns maximum storable
   * length of a string in the allocated buffer on success, or 0 on failure.
   */
  size_t AllocFromScprintf(const wchar_t* format, ...) {
    va_list ap;
    va_start(ap, format);
    size_t returnValue = AllocFromVscprintf(format, ap);
    va_end(ap);
    return returnValue;
  }
  /**
   * Same as the above, but takes a va_list like vscprintf does.
   */
  size_t AllocFromVscprintf(const wchar_t* format, va_list ap) {
    int len = _vscwprintf(format, ap);
    if (len < 0) {
      Truncate();
      return 0;
    }
    if (!AllocEmpty(len)) {
      // AllocEmpty will Truncate, no need to call it here.
      return 0;
    }
    return len;
  }

  /**
   * Automatically determines how much space is necessary, allocates that much
   * for the string, and assigns the data using swprintf. Returns the resulting
   * length of the string, which will be 0 if the function fails.
   */
  size_t AutoAllocAndAssignSprintf(const wchar_t* format, ...) {
    va_list ap;
    va_start(ap, format);
    size_t len = AllocFromVscprintf(format, ap);
    va_end(ap);
    if (len == 0) {
      // AllocFromVscprintf will Truncate, no need to call it here.
      return 0;
    }

    va_start(ap, format);
    size_t charsWritten = AssignVsprintf(len + 1, format, ap);
    va_end(ap);

    if (len != charsWritten) {
      Truncate();
      return 0;
    }
    return charsWritten;
  }

  /**
   * The following CopyFrom functions take various types of strings, allocate
   * enough space to hold them, and then copy them into that space.
   *
   * They return an HRESULT that should be interpreted with the SUCCEEDED or
   * FAILED macro.
   */
  HRESULT CopyFrom(const wchar_t* src) {
    mLength = wcslen(src);
    if (!AllocLen(mLength)) {
      Truncate();
      return E_OUTOFMEMORY;
    }
    HRESULT hrv = StringCchCopyW(mString.get(), mLength + 1, src);
    if (FAILED(hrv)) {
      Truncate();
    }
    return hrv;
  }
  HRESULT CopyFrom(const SimpleAutoString& src) {
    if (!src.mString) {
      Truncate();
      return S_OK;
    }
    mLength = src.mLength;
    if (!AllocLen(mLength)) {
      Truncate();
      return E_OUTOFMEMORY;
    }
    HRESULT hrv = StringCchCopyW(mString.get(), mLength + 1, src.mString.get());
    if (FAILED(hrv)) {
      Truncate();
    }
    return hrv;
  }
  HRESULT CopyFrom(const char* src) {
    int bufferSize =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, nullptr, 0);
    if (bufferSize == 0) {
      Truncate();
      return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!AllocSize(bufferSize)) {
      Truncate();
      return E_OUTOFMEMORY;
    }
    int charsWritten = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src,
                                           -1, mString.get(), bufferSize);
    if (charsWritten == 0) {
      Truncate();
      return HRESULT_FROM_WIN32(GetLastError());
    } else if (charsWritten != bufferSize) {
      Truncate();
      return E_FAIL;
    }
    mLength = charsWritten - 1;
    return S_OK;
  }

  bool StartsWith(const SimpleAutoString& prefix) const {
    if (!mString) {
      return (prefix.mLength == 0);
    }
    if (!prefix.mString) {
      return true;
    }
    if (prefix.mLength > mLength) {
      return false;
    }
    return (wcsncmp(mString.get(), prefix.mString.get(), prefix.mLength) == 0);
  }
};

// FIXME: This should be merged to oak only, not central.
static HANDLE LOG_HANDLE = INVALID_HANDLE_VALUE;

// FIXME: This function should be merged to oak only, not central.
void LOG(const wchar_t* format, ...) {
  if (LOG_HANDLE == INVALID_HANDLE_VALUE) {
    return;
  }

  SimpleAutoString message;
  va_list ap;
  va_start(ap, format);
  size_t len = message.AllocFromVscprintf(format, ap);
  va_end(ap);
  if (len == 0) {
    return;
  }

  va_start(ap, format);
  size_t charsWritten = message.AssignVsprintf(len + 1, format, ap);
  va_end(ap);
  if (len != charsWritten) {
    return;
  }

  DWORD written;
  WriteFile(LOG_HANDLE,
            message.String(),
            static_cast<DWORD>(len) * sizeof(wchar_t),
            &written,
            nullptr);
}

// FIXME: This function should be merged to oak only, not central.
void INIT_LOG() {
  PWSTR homeDir;
  HRESULT hrv = SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_CREATE, nullptr,
                                     &homeDir);
  if (hrv != S_OK) {
    CoTaskMemFree(homeDir);
  }

  SimpleAutoString logPath;
  logPath.AutoAllocAndAssignSprintf(L"%s\\firefox_pid_%d.log", homeDir, _getpid());
  CoTaskMemFree(homeDir);
  if (logPath.Length() == 0) {
    return;
  }

  LOG_HANDLE = CreateFileW(logPath.String(),
                           GENERIC_WRITE,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);

  std::time_t t = std::time(0);
  std::tm* now = std::localtime(&t);
  LOG(L"Log Opened: %04d-%02d-%02d %02d:%02d:%02d\n",
      now->tm_year + 1900,
      now->tm_mon + 1,
      now->tm_mday,
      now->tm_hour,
      now->tm_min,
      now->tm_sec);
}

// FIXME: This function should be merged to oak only, not central.
void RELEASE_LOG() {
  if (LOG_HANDLE == INVALID_HANDLE_VALUE) {
    return;
  }
  LOG(L"End of Log\n");
  CloseHandle(LOG_HANDLE);
  LOG_HANDLE = INVALID_HANDLE_VALUE;
}

// Deleter for use with UniquePtr
struct CoTaskMemFreeDeleter {
  void operator()(void* aPtr) { ::CoTaskMemFree(aPtr); }
};

/**
 * A lot of data goes into constructing an ACL and security attributes, and the
 * Windows documentation does not make it very clear what can be safely freed
 * after these objects are constructed. This struct holds all of the
 * construction data in one place so that it can be passed around and freed
 * properly.
 */
struct AutoPerms {
  SID_IDENTIFIER_AUTHORITY sidIdentifierAuthority;
  UniqueSidPtr usersSID;
  UniqueSidPtr adminsSID;
  UniqueSidPtr systemSID;
  EXPLICIT_ACCESS_W ea[3];
  mozilla::UniquePtr<ACL, LocalFreeDeleter> acl;
  mozilla::UniquePtr<uint8_t[]> securityDescriptorBuffer;
  PSECURITY_DESCRIPTOR securityDescriptor;
  SECURITY_ATTRIBUTES securityAttributes;
};

static HRESULT GetFilename(SimpleAutoString& path, SimpleAutoString& filename);

enum class Tristate { False, True, Unknown };

const wchar_t TristateFalseName[] = L"False";
const wchar_t TristateTrueName[] = L"True";
const wchar_t TristateUnknownName[] = L"Unknown";
const wchar_t TristateInvalidName[] = L"Invalid";
static const wchar_t* TristateString(Tristate t) {
  if (t == Tristate::True) {
    return TristateTrueName;
  }
  if (t == Tristate::False) {
    return TristateFalseName;
  }
  if (t == Tristate::Unknown) {
    return TristateUnknownName;
  }
  return TristateInvalidName;
}

enum class Lockstate { Locked, Unlocked };

/**
 * This class will look up and store some data about the file or directory at
 * the path given.
 * The path can additionally be locked. For files, this is done by holding a
 * handle to that file. For directories, this is done by holding a handle to a
 * file within the directory.
 */
class FileOrDirectory {
 private:
  Tristate mIsHardLink;
  DWORD mAttributes;
  nsAutoHandle mLockHandle;
  // This stores the name of the lock file. We need to keep track of this for
  // directories, which are locked via a randomly named lock file inside. But
  // we do not store a value here for files, as they do not have a separate lock
  // file.
  SimpleAutoString mDirLockFilename;

  /**
   * Locks the path. For directories, this is done by opening a file in the
   * directory and storing its handle in mLockHandle. For files, we just open
   * the file itself and store the handle.
   * Returns true on success and false on failure.
   *
   * Calling this function will result in mAttributes being updated.
   *
   * This function is private to prevent callers from locking the directory
   * after its attributes have been read. Part of the purpose of locking a
   * directory is to ensure that its attributes are what we think they are and
   * that they don't change while we hold the lock. If we get the lock after
   * attributes are looked up, we can no longer provide that guarantee.
   * If you think you want to call Lock(), you probably actually want to call
   * Reset().
   */
  bool Lock(const wchar_t* path) {
    mAttributes = GetFileAttributesW(path);
    Tristate isDir = IsDirectory();
    if (isDir == Tristate::Unknown) {
      return false;
    }

    if (isDir == Tristate::True) {
      SimpleAutoString lockPath;
      if (!lockPath.AllocEmpty(MAX_PATH)) {
        return false;
      }
      BOOL success = GetUUIDTempFilePath(path, NS_T(LOCK_FILE_PREFIX),
                                         lockPath.MutableString());
      if (!success || !lockPath.Check()) {
        return false;
      }

      HRESULT hrv = GetFilename(lockPath, mDirLockFilename);
      if (FAILED(hrv) || mDirLockFilename.Length() == 0) {
        return false;
      }

      mLockHandle.own(CreateFileW(lockPath.String(), 0, 0, nullptr, OPEN_ALWAYS,
                                  FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    } else {  // If path is not a directory
      // The usual reason for us to lock a file is to read and change the
      // permissions so, unlike the directory lock file, make sure we request
      // the access necessary to read and write permissions.
      mLockHandle.own(CreateFileW(path, WRITE_DAC | READ_CONTROL, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr));
    }
    if (!IsLocked()) {
      return false;
    }
    mAttributes = GetFileAttributesW(path);
    // Directories and files are locked in different ways. If we think that we
    // just locked one but we actually locked the other, our lock will be
    // ineffective and we should not tell callers that this is locked.
    // (This should fail earlier, since files cannot have children and
    // directories cannot be opened with FILE_ATTRIBUTE_NORMAL. But just to be
    // safe...)
    if (isDir != IsDirectory()) {
      Unlock();
      return false;
    }
    return true;
  }

  /**
   * Helper function to normalize the access mask by converting generic access
   * flags to specific ones to make it easier to check if permissions match.
   */
  void NormalizeAccessMask(ACCESS_MASK& mask) {
    if ((mask & GENERIC_ALL) == GENERIC_ALL) {
      mask &= ~GENERIC_ALL;
      mask |= FILE_ALL_ACCESS;
    }
    if ((mask & GENERIC_READ) == GENERIC_READ) {
      mask &= ~GENERIC_READ;
      mask |= FILE_GENERIC_READ;
    }
    if ((mask & GENERIC_WRITE) == GENERIC_WRITE) {
      mask &= ~GENERIC_WRITE;
      mask |= FILE_GENERIC_WRITE;
    }
    if ((mask & GENERIC_EXECUTE) == GENERIC_EXECUTE) {
      mask &= ~GENERIC_EXECUTE;
      mask |= FILE_GENERIC_EXECUTE;
    }
  }

 public:
  FileOrDirectory()
      : mIsHardLink(Tristate::Unknown),
        mAttributes(INVALID_FILE_ATTRIBUTES),
        mLockHandle(INVALID_HANDLE_VALUE) {}

  /**
   * If shouldLock is Locked:Locked, the file or directory will be locked.
   * Note that locking is fallible and success should be checked via the
   * IsLocked method.
   */
  FileOrDirectory(const SimpleAutoString& path, Lockstate shouldLock)
      : FileOrDirectory() {
    Reset(path, shouldLock);
  }

  /**
   * Initializes the FileOrDirectory to the file with the path given.
   *
   * If shouldLock is Locked:Locked, the file or directory will be locked.
   * Note that locking is fallible and success should be checked via the
   * IsLocked method.
   */
  void Reset(const SimpleAutoString& path, Lockstate shouldLock) {
    Unlock();
    mDirLockFilename.Truncate();
    if (shouldLock == Lockstate::Locked) {
      // This will also update mAttributes.
      Lock(path.String());
    } else {
      mAttributes = GetFileAttributesW(path.String());
    }

    mIsHardLink = Tristate::Unknown;
    nsAutoHandle autoHandle;
    HANDLE handle;
    if (IsLocked() && IsDirectory() == Tristate::False) {
      // If the path is a file and we locked it, we already have a handle to it.
      // No need to open it again.
      handle = mLockHandle.get();
    } else {
      handle = CreateFileW(path.String(), 0, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      // Make sure this handle gets freed automatically.
      autoHandle.own(handle);
    }

    Tristate isLink = Tristate::Unknown;
    if (handle != INVALID_HANDLE_VALUE) {
      BY_HANDLE_FILE_INFORMATION info;
      BOOL success = GetFileInformationByHandle(handle, &info);
      if (success) {
        if (info.nNumberOfLinks > 1) {
          isLink = Tristate::True;
        } else {
          isLink = Tristate::False;
        }
      }
    }

    mIsHardLink = Tristate::Unknown;
    Tristate isSymLink = IsSymLink();
    if (isLink == Tristate::False || isSymLink == Tristate::True) {
      mIsHardLink = Tristate::False;
    } else if (isLink == Tristate::True && isSymLink == Tristate::False) {
      mIsHardLink = Tristate::True;
    }
  }

  void Unlock() { mLockHandle.own(INVALID_HANDLE_VALUE); }

  bool IsLocked() const { return mLockHandle.get() != INVALID_HANDLE_VALUE; }

  Tristate IsSymLink() const {
    if (mAttributes == INVALID_FILE_ATTRIBUTES) {
      return Tristate::Unknown;
    }
    if (mAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
      return Tristate::True;
    }
    return Tristate::False;
  }

  Tristate IsHardLink() const { return mIsHardLink; }

  Tristate IsLink() const {
    Tristate isSymLink = IsSymLink();
    if (mIsHardLink == Tristate::True || isSymLink == Tristate::True) {
      return Tristate::True;
    }
    if (mIsHardLink == Tristate::Unknown || isSymLink == Tristate::Unknown) {
      return Tristate::Unknown;
    }
    return Tristate::False;
  }

  Tristate IsDirectory() const {
    if (mAttributes == INVALID_FILE_ATTRIBUTES) {
      return Tristate::Unknown;
    }
    if (mAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      return Tristate::True;
    }
    return Tristate::False;
  }

  Tristate IsReadonly() const {
    if (mAttributes == INVALID_FILE_ATTRIBUTES) {
      return Tristate::Unknown;
    }
    if (mAttributes & FILE_ATTRIBUTE_READONLY) {
      return Tristate::True;
    }
    return Tristate::False;
  }

  DWORD Attributes() const { return mAttributes; }

  /**
   * Sets the permissions to those passed. For this to be done safely, the file
   * must be locked and must not be a directory or a link. If these conditions
   * are not met, the function will fail.
   * Without locking, we can't guarantee that the file is the one we think it
   * is. Someone might have replaced a component of the path with a symlink.
   * With directories, setting the permissions can have the effect of setting
   * the permissions of a malicious hardlink within.
   */
  HRESULT SetPerms(const AutoPerms& perms) {
    if (IsDirectory() != Tristate::False || !IsLocked() ||
        IsHardLink() != Tristate::False) {
      return E_FAIL;
    }

    DWORD drv = SetSecurityInfo(mLockHandle.get(), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                perms.acl.get(), nullptr);
    return HRESULT_FROM_WIN32(drv);
  }

  /**
   * Checks the permissions of a file to make sure that they match the expected
   * permissions.
   */
  Tristate PermsOk(const SimpleAutoString& path, const AutoPerms& perms) {
    nsAutoHandle autoHandle;
    HANDLE handle;
    if (IsDirectory() == Tristate::False && IsLocked()) {
      handle = mLockHandle.get();
    } else {
      handle =
          CreateFileW(path.String(), READ_CONTROL, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      // Make sure this handle gets freed automatically.
      autoHandle.own(handle);
    }

    PACL dacl = nullptr;
    SECURITY_DESCRIPTOR* securityDescriptor = nullptr;
    DWORD drv = GetSecurityInfo(
        handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
        &dacl, nullptr,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&securityDescriptor));
    // Store the security descriptor in a UniquePtr so that it automatically
    // gets freed properly. We don't need to worry about dacl, since it will
    // point within the security descriptor.
    mozilla::UniquePtr<SECURITY_DESCRIPTOR, LocalFreeDeleter>
        autoSecurityDescriptor(securityDescriptor);
    if (drv != ERROR_SUCCESS || dacl == nullptr) {
      LOG(L"FileOrDirectory::PermsOk - Unable to get perms for \"%s\". Error: %#X\n", path.String(), drv);
      return Tristate::Unknown;
    }

    size_t eaLen = sizeof(perms.ea) / sizeof(perms.ea[0]);
    for (size_t eaIndex = 0; eaIndex < eaLen; ++eaIndex) {
      PTRUSTEE_W trustee = const_cast<PTRUSTEE_W>(&perms.ea[eaIndex].Trustee);
      ACCESS_MASK expectedMask = perms.ea[eaIndex].grfAccessPermissions;
      ACCESS_MASK actualMask;
      drv = GetEffectiveRightsFromAclW(dacl, trustee, &actualMask);
      if (drv != ERROR_SUCCESS) {
        LOG(L"FileOrDirectory::PermsOk - [eaIndex = %u] Unable to get effective rights for \"%s\". Error: %#X\n", eaIndex, path.String(), drv);
        return Tristate::Unknown;
      }
      LOG(L"FileOrDirectory::PermsOk - [eaIndex = %u] Pre-normalization masks: expected: %#X, actual: %#X\n", eaIndex, expectedMask, actualMask);
      NormalizeAccessMask(expectedMask);
      NormalizeAccessMask(actualMask);
      LOG(L"FileOrDirectory::PermsOk - [eaIndex = %u] Post-normalization masks: expected: %#X, actual: %#X\n", eaIndex, expectedMask, actualMask);
      if ((actualMask & expectedMask) != expectedMask) {
        LOG(L"FileOrDirectory::PermsOk - [eaIndex = %u] Returning False\n", eaIndex);
        return Tristate::False;
      }
    }

    LOG(L"FileOrDirectory::PermsOk - Returning True\n");
    return Tristate::True;
  }

  /**
   * Valid only if IsDirectory() == True.
   * Checks to see if the string given matches the filename of the lock file.
   */
  bool LockFilenameMatches(const wchar_t* filename) {
    if (mDirLockFilename.Length() == 0) {
      return false;
    }
    return wcscmp(filename, mDirLockFilename.String()) == 0;
  }
};

static bool GetCachedHash(const char16_t* installPath, HKEY rootKey,
                          const SimpleAutoString& regPath,
                          mozilla::UniquePtr<NS_tchar[]>& result);
static HRESULT GetUpdateDirectory(const wchar_t* installPath,
                                  const char* vendor, const char* appName,
                                  WhichUpdateDir whichDir,
                                  SetPermissionsOf permsToSet,
                                  mozilla::UniquePtr<wchar_t[]>& result);
static HRESULT EnsureUpdateDirectoryPermissions(
    const SimpleAutoString& basePath, const SimpleAutoString& updatePath,
    bool fullUpdatePath, SetPermissionsOf permsToSet);
static HRESULT GeneratePermissions(AutoPerms& result);
static HRESULT MakeDir(const SimpleAutoString& path, const AutoPerms& perms);
static HRESULT RemoveRecursive(const SimpleAutoString& path,
                               FileOrDirectory& file);
static HRESULT MoveConflicting(const SimpleAutoString& path,
                               FileOrDirectory& file,
                               SimpleAutoString* outPath);
static HRESULT EnsureCorrectPermissions(SimpleAutoString& path,
                                        FileOrDirectory& file,
                                        const SimpleAutoString& leafUpdateDir,
                                        const AutoPerms& perms);
static HRESULT FixDirectoryPermissions(const SimpleAutoString& path,
                                       FileOrDirectory& directory,
                                       const AutoPerms& perms,
                                       bool& permissionsFixed);
static HRESULT SplitPath(const SimpleAutoString& path,
                         SimpleAutoString& parentPath,
                         SimpleAutoString& filename);
static bool PathConflictsWithLeaf(const SimpleAutoString& path,
                                  const SimpleAutoString& leafPath);
#endif  // XP_WIN

/**
 * Returns a hash of the install path, suitable for uniquely identifying the
 * particular Firefox installation that is running.
 *
 * This function includes a compatibility mode that should NOT be used except by
 * GetUserUpdateDirectory. Previous implementations of this function could
 * return a value inconsistent with what our installer would generate. When the
 * update directory was migrated, this function was re-implemented to return
 * values consistent with those generated by the installer. The compatibility
 * mode is retained only so that we can properly get the old update directory
 * when migrating it.
 *
 * @param   installPath
 *          The null-terminated path to the installation directory (i.e. the
 *          directory that contains the binary). Must not be null. The path must
 *          not include a trailing slash.
 * @param   vendor
 *          A pointer to a null-terminated string containing the vendor name, or
 *          null. This is only used to look up a registry key on Windows. On
 *          other platforms, the value has no effect. If null is passed on
 *          Windows, "Mozilla" will be used.
 * @param   result
 *          The out parameter that will be set to contain the resulting hash.
 *          The value is wrapped in a UniquePtr to make cleanup easier on the
 *          caller.
 * @param   useCompatibilityMode
 *          Enables compatibility mode. Defaults to false.
 * @return  NS_OK, if successful.
 */
nsresult GetInstallHash(const char16_t* installPath, const char* vendor,
                        mozilla::UniquePtr<NS_tchar[]>& result,
                        bool useCompatibilityMode /* = false */) {
  MOZ_ASSERT(installPath != nullptr,
             "Install path must not be null in GetInstallHash");

  // Unable to get the cached hash, so compute it.
  size_t pathSize =
      std::char_traits<char16_t>::length(installPath) * sizeof(*installPath);
  uint64_t hash =
      CityHash64(reinterpret_cast<const char*>(installPath), pathSize);

  size_t hashStrSize = sizeof(hash) * 2 + 1;  // 2 hex digits per byte + null
  result = mozilla::MakeUnique<NS_tchar[]>(hashStrSize);
  int charsWritten;
  if (useCompatibilityMode) {
    // This behavior differs slightly from the default behavior.
    // When the default output would be "1234567800000009", this instead
    // produces "123456789".
    charsWritten = NS_tsnprintf(result.get(), hashStrSize,
                                NS_T("%") NS_T(PRIX32) NS_T("%") NS_T(PRIX32),
                                static_cast<uint32_t>(hash >> 32),
                                static_cast<uint32_t>(hash));
  } else {
    charsWritten =
        NS_tsnprintf(result.get(), hashStrSize, NS_T("%") NS_T(PRIX64), hash);
  }
  if (charsWritten < 1 || static_cast<size_t>(charsWritten) > hashStrSize - 1) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

#ifdef XP_WIN
/**
 * Returns true if the registry key was successfully found and read into result.
 */
static bool GetCachedHash(const char16_t* installPath, HKEY rootKey,
                          const SimpleAutoString& regPath,
                          mozilla::UniquePtr<NS_tchar[]>& result) {
  // Find the size of the string we are reading before we read it so we can
  // allocate space.
  unsigned long bufferSize;
  LSTATUS lrv = RegGetValueW(rootKey, regPath.String(),
                             reinterpret_cast<const wchar_t*>(installPath),
                             RRF_RT_REG_SZ, nullptr, nullptr, &bufferSize);
  if (lrv != ERROR_SUCCESS) {
    return false;
  }
  result = mozilla::MakeUnique<NS_tchar[]>(bufferSize);
  if (!result) {
    return false;
  }
  // Now read the actual value from the registry.
  lrv = RegGetValueW(rootKey, regPath.String(),
                     reinterpret_cast<const wchar_t*>(installPath),
                     RRF_RT_REG_SZ, nullptr, result.get(), &bufferSize);
  return (lrv == ERROR_SUCCESS);
}

/**
 * Returns the update directory path. The update directory needs to have
 * different permissions from the default, so we don't really want anyone using
 * the path without the directory already being created with the correct
 * permissions. Therefore, this function also ensures that the base directory
 * that needs permissions set already exists. If it does not exist, it is
 * created with the needed permissions.
 * The desired permissions give Full Control to SYSTEM, Administrators, and
 * Users.
 *
 * vendor and appName are passed as char*, not because we want that (we don't,
 * we want wchar_t), but because of the expected origin of the data. If this
 * data is available, it is probably available via XREAppData::vendor and
 * XREAppData::name.
 *
 * @param   installPath
 *          The null-terminated path to the installation directory (i.e. the
 *          directory that contains the binary). The path must not include a
 *          trailing slash. If null is passed for this value, the entire update
 *          directory path cannot be retrieved, so the function will return the
 *          update directory without the installation-specific leaf directory.
 *          This feature exists for when the caller wants to use this function
 *          to set directory permissions and does not need the full update
 *          directory path.
 * @param   vendor
 *          A pointer to a null-terminated string containing the vendor name.
 *          Will default to "Mozilla" if null is passed.
 * @param   appName
 *          A pointer to a null-terminated string containing the application
 *          name, or null.
 * @param   permsToSet
 *          Determines how aggressive to be when setting permissions.
 *          This is the behavior by value:
 *          BaseDir - Sets the permissions on the base directory
 *                    (Most likely C:\ProgramData\Mozilla)
 *          BaseDirIfNotExists - Sets the permissions on the base
 *                               directory, but only if it does not
 *                               already exist.
 *          AllFilesAndDirs - Recurses through the base directory,
 *                            setting the permissions on all files
 *                            and directories contained. Symlinks
 *                            are removed. Files with names
 *                            conflicting with the creation of the
 *                            update directory are moved or removed.
 * @param   result
 *          The out parameter that will be set to contain the resulting path.
 *          The value is wrapped in a UniquePtr to make cleanup easier on the
 *          caller.
 *
 * @return  An HRESULT that should be tested with SUCCEEDED or FAILED.
 */
HRESULT
GetCommonUpdateDirectory(const wchar_t* installPath,
                         SetPermissionsOf permsToSet,
                         mozilla::UniquePtr<wchar_t[]>& result) {
  INIT_LOG();
  HRESULT hrv = GetUpdateDirectory(installPath, nullptr, nullptr,
                            WhichUpdateDir::CommonAppData, permsToSet, result);
  RELEASE_LOG();
  return hrv;
}

/**
 * This function is identical to the function above except that it gets the
 * "old" (pre-migration) update directory that is located in the user's app data
 * directory, rather than the new one in the common app data directory.
 *
 * The other difference is that this function does not create or change the
 * permissions of the update directory since the default permissions on this
 * directory are acceptable as they are.
 */
HRESULT
GetUserUpdateDirectory(const wchar_t* installPath, const char* vendor,
                       const char* appName,
                       mozilla::UniquePtr<wchar_t[]>& result) {
  INIT_LOG();
  HRESULT hrv = GetUpdateDirectory(
      installPath, vendor, appName, WhichUpdateDir::UserAppData,
      SetPermissionsOf::BaseDirIfNotExists,  // Arbitrary value
      result);
  RELEASE_LOG();
  return hrv;
}

/**
 * This is a helper function that does all of the work for
 * GetCommonUpdateDirectory and GetUserUpdateDirectory. It partially exists to
 * prevent callers of GetUserUpdateDirectory from having to pass a useless
 * SetPermissionsOf argument, which will be ignored if whichDir is UserAppData.
 *
 * For information on the parameters and return value, see
 * GetCommonUpdateDirectory.
 */
static HRESULT GetUpdateDirectory(const wchar_t* installPath,
                                  const char* vendor, const char* appName,
                                  WhichUpdateDir whichDir,
                                  SetPermissionsOf permsToSet,
                                  mozilla::UniquePtr<wchar_t[]>& result) {
  PWSTR baseDirParentPath;
  REFKNOWNFOLDERID folderID = (whichDir == WhichUpdateDir::CommonAppData)
                                  ? FOLDERID_ProgramData
                                  : FOLDERID_LocalAppData;
  HRESULT hrv = SHGetKnownFolderPath(folderID, KF_FLAG_CREATE, nullptr,
                                     &baseDirParentPath);
  // Free baseDirParentPath when it goes out of scope.
  mozilla::UniquePtr<wchar_t, CoTaskMemFreeDeleter> baseDirParentPathUnique(
      baseDirParentPath);
  if (FAILED(hrv)) {
    return hrv;
  }

  SimpleAutoString baseDir;
  if (whichDir == WhichUpdateDir::UserAppData && (vendor || appName)) {
    const char* rawBaseDir = vendor ? vendor : appName;
    hrv = baseDir.CopyFrom(rawBaseDir);
  } else {
    const wchar_t baseDirLiteral[] = NS_T(FALLBACK_VENDOR_NAME);
    hrv = baseDir.CopyFrom(baseDirLiteral);
  }
  if (FAILED(hrv)) {
    return hrv;
  }

  // Generate the base path (C:\ProgramData\Mozilla)
  SimpleAutoString basePath;
  size_t basePathLen =
      wcslen(baseDirParentPath) + 1 /* path separator */ + baseDir.Length();
  basePath.AllocAndAssignSprintf(basePathLen, L"%s\\%s", baseDirParentPath,
                                 baseDir.String());
  if (basePath.Length() != basePathLen) {
    return E_FAIL;
  }

  // Generate the update directory path. This is the value to be returned by
  // this function.
  SimpleAutoString updatePath;
  if (installPath) {
    mozilla::UniquePtr<NS_tchar[]> hash;

    // The Windows installer caches this hash value in the registry
    bool gotHash = false;
    SimpleAutoString regPath;
    regPath.AutoAllocAndAssignSprintf(L"SOFTWARE\\%S\\%S\\TaskBarIDs",
                                      vendor ? vendor : "Mozilla",
                                      MOZ_APP_BASENAME);
    if (regPath.Length() != 0) {
      gotHash = GetCachedHash(reinterpret_cast<const char16_t*>(installPath),
                              HKEY_LOCAL_MACHINE, regPath, hash);
      if (!gotHash) {
        gotHash = GetCachedHash(reinterpret_cast<const char16_t*>(installPath),
                                HKEY_CURRENT_USER, regPath, hash);
      }
    }
    nsresult rv = NS_OK;
    if (!gotHash) {
      bool useCompatibilityMode = (whichDir == WhichUpdateDir::UserAppData);
      rv = GetInstallHash(reinterpret_cast<const char16_t*>(installPath),
                          vendor, hash, useCompatibilityMode);
    }
    if (NS_SUCCEEDED(rv)) {
      const wchar_t midPathDirName[] = NS_T(UPDATE_PATH_MID_DIR_NAME);
      size_t updatePathLen = basePath.Length() + 1 /* path separator */ +
                             wcslen(midPathDirName) + 1 /* path separator */ +
                             wcslen(hash.get());
      updatePath.AllocAndAssignSprintf(updatePathLen, L"%s\\%s\\%s",
                                       basePath.String(), midPathDirName,
                                       hash.get());
      // Permissions can still be set without this string, so wait until after
      // setting permissions to return failure if the string assignment failed.
    }
  }

  if (whichDir == WhichUpdateDir::CommonAppData) {
    if (updatePath.Length() > 0) {
      LOG(L"Update path: \"%s\"\n", updatePath.String());
      hrv = EnsureUpdateDirectoryPermissions(basePath, updatePath, true,
                                             permsToSet);
    } else {
      LOG(L"Unable to get update path. Base path is: \"%s\"\n", basePath.String());
      hrv = EnsureUpdateDirectoryPermissions(basePath, basePath, false,
                                             permsToSet);
    }
    if (FAILED(hrv)) {
      return hrv;
    }
  } else {
    LOG(L"Getting user update directory, not the common one.\n");
  }

  if (!installPath) {
    basePath.SwapBufferWith(result);
    return S_OK;
  }

  if (updatePath.Length() == 0) {
    return E_FAIL;
  }
  updatePath.SwapBufferWith(result);
  return S_OK;
}

/**
 * If the basePath does not exist, it is created with the expected permissions.
 *
 * It used to be that if basePath exists and SetPermissionsOf::AllFilesAndDirs
 * was passed in, this function would aggressively set the permissions of
 * the directory and everything in it. But that caused a problem: There does not
 * seem to be a good way to ensure that, when setting permissions on a
 * directory, a malicious process does not sneak a hard link into that directory
 * (causing it to inherit the permissions set on the directory).
 *
 * To address that issue, this function now takes a different approach.
 * To prevent abuse, permissions of directories will not be changed.
 * Instead, directories with bad permissions are deleted and re-created with the
 * correct permissions.
 *
 * @param   basePath
 *          The top directory within the application data directory.
 *          Typically "C:\ProgramData\Mozilla".
 * @param   updatePath
 *          The update directory to be checked for conflicts. If files
 *          conflicting with this directory structure exist, they may be moved
 *          or deleted depending on the value of permsToSet.
 * @param   fullUpdatePath
 *          Set to true if updatePath is the full update path. If set to false,
 *          it means that we don't have the installation-specific path
 *          component.
 * @param   permsToSet
 *          See the documentation for GetCommonUpdateDirectory for the
 *          descriptions of the effects of each SetPermissionsOf value.
 */
static HRESULT EnsureUpdateDirectoryPermissions(
    const SimpleAutoString& basePath, const SimpleAutoString& updatePath,
    bool fullUpdatePath, SetPermissionsOf permsToSet) {
  LOG(L"EnsureUpdateDirectoryPermissions(basePath = \"%s\", updatePath = \"%s\", fullUpdatePath = %s, permsToSet = %s)\n",
      basePath.String(),
      updatePath.String(),
      fullUpdatePath ? L"true" : L"false",
      (permsToSet == SetPermissionsOf::AllFilesAndDirs) ? L"AllFilesAndDirs" : L"BaseDirIfNotExists");

  HRESULT returnValue = S_OK;  // Stores the value that will eventually be
                               // returned. If errors occur, this is set to the
                               // first error encountered.

  Lockstate shouldLock = permsToSet == SetPermissionsOf::AllFilesAndDirs
                             ? Lockstate::Locked
                             : Lockstate::Unlocked;
  FileOrDirectory baseDir(basePath, shouldLock);
  // validBaseDir will be true if the basePath exists, and is a non-symlinked
  // directory.
  bool validBaseDir = baseDir.IsDirectory() == Tristate::True &&
                      baseDir.IsLink() == Tristate::False;
  LOG(L"EnsureUpdateDirectoryPermissions - baseDir is directory: %s\n", TristateString(baseDir.IsDirectory()));
  LOG(L"EnsureUpdateDirectoryPermissions - baseDir is link: %s\n", TristateString(baseDir.IsLink()));
  LOG(L"EnsureUpdateDirectoryPermissions - validBaseDir: %s\n", validBaseDir ? L"true" : L"false");

  // The most common case when calling this function is when the caller of
  // GetCommonUpdateDirectory just wants the update directory path, and passes
  // in the least aggressive option for setting permissions.
  // The most common environment is that the update directory already exists.
  // Optimize for this case.
  if (permsToSet == SetPermissionsOf::BaseDirIfNotExists && validBaseDir) {
    LOG(L"EnsureUpdateDirectoryPermissions end - not setting permissions and base dir looks ok.\n");
    return S_OK;
  }

  AutoPerms perms;
  HRESULT hrv = GeneratePermissions(perms);
  if (FAILED(hrv)) {
    LOG(L"EnsureUpdateDirectoryPermissions end - unable to generate permissions (Error: %#X)\n", hrv);
    // Fatal error. There is no real way to recover from this.
    return hrv;
  }

  if (permsToSet == SetPermissionsOf::BaseDirIfNotExists) {
    LOG(L"EnsureUpdateDirectoryPermissions - Base dir is invalid, but we aren't doing a full perm check. "
        L"Moving conflicting file and recreating directory.\n");
    // We know that the base directory is invalid, because otherwise we would
    // have exited already.
    // Ignore errors here. It could be that the directory doesn't exist at all.
    // And ultimately, we are only interested in whether or not we successfully
    // create the new directory.
    MoveConflicting(basePath, baseDir, nullptr);

    hrv = MakeDir(basePath, perms);
    returnValue = FAILED(returnValue) ? returnValue : hrv;
    LOG(L"EnsureUpdateDirectoryPermissions end - (conflicting dir recreated) "
        L"dir creation code: %#X exit code: %#X\n", hrv, returnValue);
    return returnValue;
  }

  // We need to pass a mutable basePath to EnsureCorrectPermissions, so copy it.
  SimpleAutoString mutBasePath;
  hrv = mutBasePath.CopyFrom(basePath);
  if (FAILED(hrv) || mutBasePath.Length() == 0) {
    returnValue = FAILED(returnValue) ? returnValue : hrv;
    LOG(L"EnsureUpdateDirectoryPermissions end - Unable to make mutable copy of basePath"
        L" Error Code: %#X\n", hrv);
    return returnValue;
  }

  if (fullUpdatePath) {
    // When we are doing a full permissions reset, we are also ensuring that no
    // files are in the way of our required directory structure. Generate the
    // path of the furthest leaf in our directory structure so that we can check
    // for conflicting files.
    SimpleAutoString leafDirPath;
    wchar_t updateSubdirectoryName[] = NS_T(UPDATE_SUBDIRECTORY);
    wchar_t patchDirectoryName[] = NS_T(PATCH_DIRECTORY);
    size_t leafDirLen = updatePath.Length() + wcslen(updateSubdirectoryName) +
                        wcslen(patchDirectoryName) + 2; /* 2 path separators */
    leafDirPath.AllocAndAssignSprintf(
        leafDirLen, L"%s\\%s\\%s", updatePath.String(), updateSubdirectoryName,
        patchDirectoryName);
    if (leafDirPath.Length() == leafDirLen) {
      LOG(L"EnsureUpdateDirectoryPermissions - Calling EnsureCorrectPermissions with leafDirPath = \"%s\"\n", leafDirPath.String());
      hrv = EnsureCorrectPermissions(mutBasePath, baseDir, leafDirPath, perms);
    } else {
      LOG(L"EnsureUpdateDirectoryPermissions - Calling EnsureCorrectPermissions with updatePath (despite fullUpdatePath)\n");
      // If we cannot generate the leaf path, just do the best we can by using
      // the updatePath.
      returnValue = FAILED(returnValue) ? returnValue : E_FAIL;
      hrv = EnsureCorrectPermissions(mutBasePath, baseDir, updatePath, perms);
    }
  } else {
    LOG(L"EnsureUpdateDirectoryPermissions - Calling EnsureCorrectPermissions with leafDirPath (no fullUpdatePath)\n");
    hrv = EnsureCorrectPermissions(mutBasePath, baseDir, updatePath, perms);
  }
  LOG(L"EnsureUpdateDirectoryPermissions - EnsureCorrectPermissions returned %#X\n", hrv);
  returnValue = FAILED(returnValue) ? returnValue : hrv;

  // EnsureCorrectPermissions does its best to remove links and conflicting
  // files but, in doing so, it may leave us without a base update directory.
  // Rather than checking whether it exists first, just try to create it. If
  // successful, the directory now exists with the right permissions and no
  // contents, which this function considers a success. If unsuccessful,
  // most likely the directory just already exists. But we need to verify that
  // before we can return success.
  BOOL success = CreateDirectoryW(
      basePath.String(),
      const_cast<LPSECURITY_ATTRIBUTES>(&perms.securityAttributes));
  if (success) {
    LOG(L"EnsureUpdateDirectoryPermissions - Created update directory!\n");
    return S_OK;
  }
  if (SUCCEEDED(returnValue)) {
    baseDir.Reset(basePath, Lockstate::Unlocked);
    if (baseDir.IsDirectory() != Tristate::True ||
        baseDir.IsLink() != Tristate::False ||
        baseDir.PermsOk(basePath, perms) != Tristate::True) {
      LOG(L"EnsureUpdateDirectoryPermissions - Succeeded, but update directory doesn't look right!"
          L"IsDirectory = %s, IsLink = %s, PermsOk = %s\n",
          TristateString(baseDir.IsDirectory()),
          TristateString(baseDir.IsLink()),
          TristateString(baseDir.PermsOk(basePath, perms)));
      return E_FAIL;
    }
  }

  return returnValue;
}

/**
 * Generates the permission set that we want to be applied to the update
 * directory and its contents. Returns the permissions data via the result
 * outparam.
 *
 * These are also the permissions that will be used to check that file
 * permissions are correct.
 */
static HRESULT GeneratePermissions(AutoPerms& result) {
  result.sidIdentifierAuthority = SECURITY_NT_AUTHORITY;
  ZeroMemory(&result.ea, sizeof(result.ea));

  // Make Users group SID and add it to the Explicit Access List.
  PSID usersSID = nullptr;
  BOOL success = AllocateAndInitializeSid(
      &result.sidIdentifierAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &usersSID);
  result.usersSID.reset(usersSID);
  if (!success) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  result.ea[0].grfAccessPermissions = FILE_ALL_ACCESS;
  result.ea[0].grfAccessMode = SET_ACCESS;
  result.ea[0].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  result.ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  result.ea[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  result.ea[0].Trustee.ptstrName = static_cast<LPWSTR>(usersSID);

  // Make Administrators group SID and add it to the Explicit Access List.
  PSID adminsSID = nullptr;
  success = AllocateAndInitializeSid(
      &result.sidIdentifierAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminsSID);
  result.adminsSID.reset(adminsSID);
  if (!success) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  result.ea[1].grfAccessPermissions = FILE_ALL_ACCESS;
  result.ea[1].grfAccessMode = SET_ACCESS;
  result.ea[1].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  result.ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  result.ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
  result.ea[1].Trustee.ptstrName = static_cast<LPWSTR>(adminsSID);

  // Make SYSTEM user SID and add it to the Explicit Access List.
  PSID systemSID = nullptr;
  success = AllocateAndInitializeSid(&result.sidIdentifierAuthority, 1,
                                     SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0,
                                     0, 0, &systemSID);
  result.systemSID.reset(systemSID);
  if (!success) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  result.ea[2].grfAccessPermissions = FILE_ALL_ACCESS;
  result.ea[2].grfAccessMode = SET_ACCESS;
  result.ea[2].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
  result.ea[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
  result.ea[2].Trustee.TrusteeType = TRUSTEE_IS_USER;
  result.ea[2].Trustee.ptstrName = static_cast<LPWSTR>(systemSID);

  PACL acl = nullptr;
  DWORD drv = SetEntriesInAclW(3, result.ea, nullptr, &acl);
  // Put the ACL in a unique pointer so that LocalFree is called when it goes
  // out of scope
  result.acl.reset(acl);
  if (drv != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(drv);
  }

  result.securityDescriptorBuffer =
      mozilla::MakeUnique<uint8_t[]>(SECURITY_DESCRIPTOR_MIN_LENGTH);
  if (!result.securityDescriptorBuffer) {
    return E_OUTOFMEMORY;
  }
  result.securityDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(
      result.securityDescriptorBuffer.get());
  success = InitializeSecurityDescriptor(result.securityDescriptor,
                                         SECURITY_DESCRIPTOR_REVISION);
  if (!success) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  success =
      SetSecurityDescriptorDacl(result.securityDescriptor, TRUE, acl, FALSE);
  if (!success) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  result.securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  result.securityAttributes.lpSecurityDescriptor = result.securityDescriptor;
  result.securityAttributes.bInheritHandle = FALSE;
  return S_OK;
}

/**
 * Creates a directory with the permissions specified. If the directory already
 * exists, this function will return success as long as it is a non-link
 * directory.
 */
static HRESULT MakeDir(const SimpleAutoString& path, const AutoPerms& perms) {
  LOG(L"MakeDir(path = %s)\n", path.String());
  BOOL success = CreateDirectoryW(
      path.String(),
      const_cast<LPSECURITY_ATTRIBUTES>(&perms.securityAttributes));
  if (success) {
    LOG(L"Successfully created path\n");
    return S_OK;
  }
  DWORD error = GetLastError();
  if (error != ERROR_ALREADY_EXISTS) {
    LOG(L"Got Error %#X\n", error);
    return HRESULT_FROM_WIN32(error);
  }
  LOG(L"Got Error ERROR_ALREADY_EXISTS\n");
  FileOrDirectory dir(path, Lockstate::Unlocked);
  if (dir.IsDirectory() == Tristate::True && dir.IsLink() == Tristate::False) {
    LOG(L"Directory looks ok\n");
    return S_OK;
  }
  LOG(L"Directory does not look ok\n");
  return HRESULT_FROM_WIN32(error);
}

/**
 * Attempts to move the file or directory to the Windows Recycle Bin.
 * If removal fails with an ERROR_FILE_NOT_FOUND, the file must not exist, so
 * this will return success in that case.
 *
 * The file will be unlocked in order to remove it.
 *
 * Whether this function succeeds or fails, the file parameter should no longer
 * be considered accurate. If it succeeds, it will be inaccurate because the
 * file no longer exists. If it fails, it may be inaccurate due to this function
 * potentially setting file attributes.
 */
static HRESULT RemoveRecursive(const SimpleAutoString& path,
                               FileOrDirectory& file) {
  LOG(L"RemoveRecursive(path = \"%s\")\n", path.String());
  file.Unlock();
  if (file.IsReadonly() != Tristate::False) {
    LOG(L"RemoveRecursive - Removing readonly attribute\n");
    // Ignore errors setting attributes. We only care if it was successfully
    // deleted.
    DWORD attributes = file.Attributes();
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      LOG(L"RemoveRecursive - Unable to read attributes. Setting normal ones\n");
      SetFileAttributesW(path.String(), FILE_ATTRIBUTE_NORMAL);
    } else {
      SetFileAttributesW(path.String(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
  }

  // The SHFILEOPSTRUCTW expects a list of paths. The list is simply one long
  // string separated by null characters. The end of the list is designated by
  // two null characters.
  SimpleAutoString pathList;
  pathList.AllocAndAssignSprintf(path.Length() + 1, L"%s\0", path.String());

  SHFILEOPSTRUCTW fileOperation;
  fileOperation.hwnd = nullptr;
  fileOperation.wFunc = FO_DELETE;
  fileOperation.pFrom = pathList.String();
  fileOperation.pTo = nullptr;
  fileOperation.fFlags = FOF_ALLOWUNDO | FOF_NO_UI;
  fileOperation.lpszProgressTitle = nullptr;

  int rv = SHFileOperationW(&fileOperation);
  if (rv == 0 || rv == ERROR_FILE_NOT_FOUND) {
    LOG(L"RemoveRecursive - Successfully moved file to the recycle bin\n");
    return S_OK;
  }
  LOG(L"RemoveRecursive - Failed to move file to the recycle bin\n");

  // Some files such as hard links can't be deleted properly with
  // SHFileOperation, so additionally try DeleteFile.
  BOOL success = DeleteFileW(path.String());
  if (success) {
    LOG(L"RemoveRecursive - Successfully removed with DeleteFileW\n");
  } else {
    LOG(L"RemoveRecursive - Unable to remove with DeleteFileW. Error: %#X\n", GetLastError());
  }
  return success ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

/**
 * Attempts to move the file or directory to a path that will not conflict with
 * our directory structure. If this fails, the path will instead be deleted.
 *
 * If an attempt results in the error ERROR_FILE_NOT_FOUND, this function
 * considers the file to no longer be a conflict and returns success.
 *
 * The file will be unlocked in order to move it. Strictly speaking, it may be
 * possible to move non-directories without unlocking them, but this function
 * will unconditionally unlock the file.
 *
 * If a non-null pointer is passed for outPath, the path that the file was moved
 * to will be stored there. If the file was removed, an empty string will be
 * stored. Note that if outPath is set to an empty string, it may not have a
 * buffer allocated, so outPath.Length() should be checked before using
 * outPath.String().
 * It is ok for outPath to point to the path parameter.
 * This function guarantees that if failure is returned, outPath will not be
 * modified.
 */
static HRESULT MoveConflicting(const SimpleAutoString& path,
                               FileOrDirectory& file,
                               SimpleAutoString* outPath) {
  LOG(L"MoveConflicting(path = \"%s\")\n", path.String());
  file.Unlock();
  // Try to move the file to a backup location
  SimpleAutoString newPath;
  unsigned int maxTries = 9;
  const wchar_t newPathFormat[] = L"%s.bak%u";
  size_t newPathMaxLength =
      newPath.AllocFromScprintf(newPathFormat, path.String(), maxTries);
  if (newPathMaxLength > 0) {
    for (unsigned int suffix = 0; suffix <= maxTries; ++suffix) {
      newPath.AssignSprintf(newPathMaxLength + 1, newPathFormat, path.String(),
                            suffix);
      if (newPath.Length() == 0) {
        // If we failed to make this string, we probably aren't going to
        // succeed on the next one.
        break;
      }
      BOOL success;
      if (suffix < maxTries) {
        success = MoveFileW(path.String(), newPath.String());
      } else {
        // Moving a file can sometimes work when deleting a file does not. If
        // there are already the maximum number of backed up files, try
        // overwriting the last backup before we fall back to deleting the
        // original.
        success = MoveFileExW(path.String(), newPath.String(),
                              MOVEFILE_REPLACE_EXISTING);
      }
      if (success) {
        LOG(L"MoveConflicting - successfully moved \"%s\" to \"%s\"\n", path.String(), newPath.String());
        if (outPath) {
          outPath->Swap(newPath);
        }
        return S_OK;
      }
      DWORD drv = GetLastError();
      if (drv == ERROR_FILE_NOT_FOUND) {
        if (outPath) {
          outPath->Truncate();
        }
        LOG(L"MoveConflicting - File is mysteriously gone. Success?\n");
        return S_OK;
      }
      LOG(L"MoveConflicting - Failed to move \"%s\" to \"%s\"\n", path.String(), newPath.String());
      // If the move failed because newPath already exists, loop to try a new
      // suffix. If the move failed for any other reason, a new suffix will
      // probably not help.
      // Sometimes, however, if we cannot read the existing file due to lack of
      // permissions, we may get an "Access Denied" error. So retry in that case
      // too.
      if (drv != ERROR_ALREADY_EXISTS && drv != ERROR_ACCESS_DENIED) {
        LOG(L"MoveConflicting - Error was not ERROR_ALREADY_EXISTS or ERROR_ACCESS_DENIED. Giving up.\n");
        break;
      }
    }
  }

  LOG(L"MoveConflicting - Unable to move. Attempting to remove.\n");
  // Moving failed. Try to delete.
  HRESULT hrv = RemoveRecursive(path, file);
  if (SUCCEEDED(hrv)) {
    LOG(L"MoveConflicting - Remove successful\n");
    if (outPath) {
      outPath->Truncate();
    }
  } else {
    LOG(L"MoveConflicting - Remove failed\n");
  }
  return hrv;
}

/**
 * This function will ensure that the specified path and all contained files and
 * subdirectories have the correct permissions.
 * Files will have their permissions set to match those specified.
 * Unfortunately, setting the permissions on directories is prone to abuse,
 * since it can potentially result in a hard link within the directory
 * inheriting those permissions. To get around this issue, directories will not
 * have their permissions changed. Instead, the directory will be moved
 * elsewhere so that it can be recreated with the correct permissions and its
 * contents moved back in.
 *
 * Symlinks and hard links are removed from the checked directories.
 *
 * This function also ensures that nothing is in the way of leafUpdateDir.
 * Non-directory files that conflict with this are moved or deleted.
 *
 * This function's second argument must receive a locked FileOrDirectory to
 * ensure that it is not tampered with while fixing the permissions of the
 * file/directory and any contents.
 *
 * If we cannot successfully determine if the path is a file or directory, we
 * simply attempt to delete it.
 *
 * Note that the path parameter is not constant. Its contents may be changed by
 * this function.
 */
static HRESULT EnsureCorrectPermissions(SimpleAutoString& path,
                                        FileOrDirectory& file,
                                        const SimpleAutoString& leafUpdateDir,
                                        const AutoPerms& perms) {
  LOG(L"EnsureCorrectPermissions(path = \"%s\") [IsDirectory: %s, IsLink: %s, IsHardLink: %s, IsSymLink: %s]\n",
      path.String(), TristateString(file.IsDirectory()), TristateString(file.IsLink()),
      TristateString(file.IsHardLink()), TristateString(file.IsSymLink()));
  HRESULT returnValue = S_OK;  // Stores the value that will eventually be
                               // returned. If errors occur, this is set to the
                               // first error encountered.
  HRESULT hrv;
  bool conflictsWithLeaf = PathConflictsWithLeaf(path, leafUpdateDir);
  if (file.IsDirectory() != Tristate::True ||
      file.IsLink() != Tristate::False) {
    // We want to keep track of the result of trying to set the permissions
    // separately from returnValue. If we later remove the file, we should not
    // report an error to set permissions.
    // SetPerms will automatically abort and return failure if it is unsafe to
    // set the permissions on the file (for example, if it is a hard link).
    HRESULT permSetResult = file.SetPerms(perms);

    bool removed = false;
    if (file.IsLink() != Tristate::False) {
      hrv = RemoveRecursive(path, file);
      returnValue = FAILED(returnValue) ? returnValue : hrv;
      if (SUCCEEDED(hrv)) {
        LOG(L"EnsureCorrectPermissions(%s) - Removed file\n", path.String());
        removed = true;
      }
    }

    if (FAILED(permSetResult) && !removed) {
      LOG(L"EnsureCorrectPermissions(%s) - Failed to set permissions (Error: %#X)\n", path.String(), permSetResult);
      returnValue = FAILED(returnValue) ? returnValue : permSetResult;
    }

    if (conflictsWithLeaf && !removed) {
      hrv = MoveConflicting(path, file, nullptr);
      returnValue = FAILED(returnValue) ? returnValue : hrv;
      if (SUCCEEDED(hrv)) {
        LOG(L"EnsureCorrectPermissions(%s) - Moved conflicting file\n", path.String());
      } else {
        LOG(L"EnsureCorrectPermissions(%s) - Unable to move conflicting file (error: %#X)\n", path.String(), hrv);
      }
    }
    LOG(L"EnsureCorrectPermissions(%s) - Returning %#X (done with non-directory)\n", path.String(), returnValue);
    return returnValue;
  }

  if (file.PermsOk(path, perms) != Tristate::True) {
    LOG(L"EnsureCorrectPermissions(%s) - Bad permissions detected\n", path.String());
    bool permissionsFixed;
    hrv = FixDirectoryPermissions(path, file, perms, permissionsFixed);
    returnValue = FAILED(returnValue) ? returnValue : hrv;
    // We only need to move conflicting directories if they have bad permissions
    // that we are unable to fix. If its permissions are correct, it isn't
    // conflicting with the leaf path, it is a component of the leaf path.
    if (!permissionsFixed && conflictsWithLeaf) {
      LOG(L"EnsureCorrectPermissions(%s) - Unable to fix permissions on conflicting directory\n", path.String());
      // No need to check for error here. returnValue is already a failure code
      // because FixDirectoryPermissions failed. MoveConflicting will ensure
      // that path is correct (or empty, on deletion) whether it succeeds or
      // fails.
      MoveConflicting(path, file, &path);
      LOG(L"EnsureCorrectPermissions(%s) - Path move attempted\n", path.String());
      if (path.Length() == 0) {
        LOG(L"EnsureCorrectPermissions(%s) - Path has been deleted. Returning %#X\n", path.String(), returnValue);
        // Path has been deleted.
        return returnValue;
      }
    }
    if (!file.IsLocked()) {
      // FixDirectoryPermissions or MoveConflicting may have left the directory
      // unlocked, but we still want to recurse into it, so re-lock it.
      file.Reset(path, Lockstate::Locked);
    }
  }

  // We MUST not recurse into unlocked directories or links.
  if (!file.IsLocked() || file.IsLink() != Tristate::False ||
      file.IsDirectory() != Tristate::True) {
    returnValue = FAILED(returnValue) ? returnValue : E_FAIL;
    LOG(L"EnsureCorrectPermissions(%s) - Want to recurse into unsafe path. Returning %#X instead\n", path.String(), returnValue);
    return returnValue;
  }

  SimpleAutoString childBuffer;
  if (!childBuffer.AllocEmpty(MAX_PATH)) {
    LOG(L"EnsureCorrectPermissions(%s) - Failed to allocate childBuffer\n", path.String());
    // Fatal error. We need a buffer to put the path in.
    return FAILED(returnValue) ? returnValue : E_OUTOFMEMORY;
  }

  // Recurse into the directory.
  DIR directoryHandle(path.String());
  errno = 0;
  for (dirent* entry = readdir(&directoryHandle); entry;
       entry = readdir(&directoryHandle)) {
    if (wcscmp(entry->d_name, L".") == 0 || wcscmp(entry->d_name, L"..") == 0 ||
        file.LockFilenameMatches(entry->d_name)) {
      continue;
    }

    childBuffer.AssignSprintf(MAX_PATH + 1, L"%s\\%s", path.String(),
                              entry->d_name);
    if (childBuffer.Length() == 0) {
      returnValue = FAILED(returnValue)
                        ? returnValue
                        : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
      LOG(L"EnsureCorrectPermissions(%s) - Skipping due to assignment failure: %s\n", path.String(), entry->d_name);
      continue;
    }

    FileOrDirectory child(childBuffer, Lockstate::Locked);
    LOG(L"EnsureCorrectPermissions(%s) - Recursing into child: %s\n", path.String(), childBuffer.String());
    hrv = EnsureCorrectPermissions(childBuffer, child, leafUpdateDir, perms);
    returnValue = FAILED(returnValue) ? returnValue : hrv;

    // Before looping, clear any errors that might have been encountered so we
    // can correctly get errors from readdir.
    errno = 0;
  }
  if (errno != 0) {
    LOG(L"EnsureCorrectPermissions(%s) - Directory listing failure\n", path.String());
    returnValue = FAILED(returnValue) ? returnValue : E_FAIL;
  }

  LOG(L"EnsureCorrectPermissions(%s) - Done with recursion. Returning %#X\n", path.String(), returnValue);
  return returnValue;
}

/**
 * This function fixes directory permissions without setting them directly.
 * The reasoning behind this is that if someone puts a hardlink in the
 * directory before we set the permissions, the permissions of the linked file
 * will be changed too. To prevent this, we will instead move the directory,
 * recreate it with the correct permissions, and move the contents back in.
 *
 * The new directory will be locked with the directory parameter so that the
 * caller can safely use the new directory. If the function fails, the directory
 * parameter may be left locked or unlocked. However, the function will never
 * leave the directory parameter locking something invalid. In other words, if
 * the directory parameter is locked after this function exits, it is safe to
 * assume that it is a locked non-link directory at the same location as the
 * original path.
 *
 * The permissionsFixed outparam serves as sort of a supplement to the return
 * value. The return value will be an error code if any part of this function
 * fails. But the function can fail at some parts while still completing its
 * main goal of fixing the directory permissions. To distinguish between these,
 * this value will be set to true if the directory permissions were successfully
 * fixed.
 */
static HRESULT FixDirectoryPermissions(const SimpleAutoString& path,
                                       FileOrDirectory& directory,
                                       const AutoPerms& perms,
                                       bool& permissionsFixed) {
  LOG(L"FixDirectoryPermissions(path = \"%s\")\n", path.String());
  permissionsFixed = false;

  SimpleAutoString parent;
  SimpleAutoString dirName;
  HRESULT hrv = SplitPath(path, parent, dirName);
  if (FAILED(hrv)) {
    LOG(L"FixDirectoryPermissions - Failed to split path (error = %#X)\n", hrv);
    return E_FAIL;
  }

  SimpleAutoString tempPath;
  if (!tempPath.AllocEmpty(MAX_PATH)) {
    LOG(L"FixDirectoryPermissions - Failed to allocate tempPath\n");
    return E_FAIL;
  }
  BOOL success = GetUUIDTempFilePath(parent.String(), dirName.String(),
                                     tempPath.MutableString());
  if (!success || !tempPath.Check() || tempPath.Length() == 0) {
    LOG(L"FixDirectoryPermissions - Failed to get UUID temp path\n");
    return E_FAIL;
  }

  directory.Unlock();
  success = MoveFileW(path.String(), tempPath.String());
  if (!success) {
    LOG(L"FixDirectoryPermissions - Failed to move directory (error = %#X)\n", GetLastError());
    return HRESULT_FROM_WIN32(GetLastError());
  }

  success = CreateDirectoryW(path.String(), const_cast<LPSECURITY_ATTRIBUTES>(
                                                &perms.securityAttributes));
  if (!success) {
    LOG(L"FixDirectoryPermissions - Failed to create replacement directory (error = %#X)\n", GetLastError());
    return E_FAIL;
  }
  directory.Reset(path, Lockstate::Locked);
  if (!directory.IsLocked() || directory.IsLink() != Tristate::False ||
      directory.IsDirectory() != Tristate::True ||
      directory.PermsOk(path, perms) != Tristate::True) {
    // Don't leave an invalid file locked when we return.
    directory.Unlock();
    LOG(L"FixDirectoryPermissions - The directory that we created does not appear to be the one we wanted: "
        L"IsLocked: %s, IsLink: %s, IsDirectory: %s, PermsOk: %s\n", directory.IsLocked() ? L"true" : L"false",
        TristateString(directory.IsLink()), TristateString(directory.IsDirectory()),
        TristateString(directory.PermsOk(path, perms)));
    return E_FAIL;
  }
  permissionsFixed = true;

  FileOrDirectory tempDir(tempPath, Lockstate::Locked);
  if (!tempDir.IsLocked() || tempDir.IsLink() != Tristate::False ||
      tempDir.IsDirectory() != Tristate::True) {
    LOG(L"FixDirectoryPermissions - Unable to lock temp directory\n");
    return E_FAIL;
  }

  SimpleAutoString moveFrom;
  SimpleAutoString moveTo;
  if (!moveFrom.AllocEmpty(MAX_PATH) || !moveTo.AllocEmpty(MAX_PATH)) {
    LOG(L"FixDirectoryPermissions - Unable to allocate moveTo/moveFrom\n");
    return E_OUTOFMEMORY;
  }

  // If we fail to copy one file, we still want to try for the others. This will
  // store the first error we encounter so it can be returned.
  HRESULT returnValue = S_OK;

  // Copy the contents of tempDir back to the original directory.
  DIR directoryHandle(tempPath.String());
  errno = 0;
  for (dirent* entry = readdir(&directoryHandle); entry;
       entry = readdir(&directoryHandle)) {
    if (wcscmp(entry->d_name, L".") == 0 || wcscmp(entry->d_name, L"..") == 0 ||
        tempDir.LockFilenameMatches(entry->d_name)) {
      continue;
    }

    moveFrom.AssignSprintf(MAX_PATH + 1, L"%s\\%s", tempPath.String(),
                           entry->d_name);
    if (moveFrom.Length() == 0) {
      returnValue = FAILED(returnValue)
                        ? returnValue
                        : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
      LOG(L"FixDirectoryPermissions - Unable assign to moveFrom: \"%s\"\n", entry->d_name);
      continue;
    }

    moveTo.AssignSprintf(MAX_PATH + 1, L"%s\\%s", path.String(), entry->d_name);
    if (moveTo.Length() == 0) {
      returnValue = FAILED(returnValue)
                        ? returnValue
                        : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
      LOG(L"FixDirectoryPermissions - Unable assign to moveTo: \"%s\"\n", entry->d_name);
      continue;
    }

    success = MoveFileW(moveFrom.String(), moveTo.String());
    if (!success) {
      LOG(L"FixDirectoryPermissions - Unable move \"%s\" to \"%s\"\n", moveFrom.String(), moveTo.String());
      returnValue = FAILED(returnValue) ? returnValue
                                        : HRESULT_FROM_WIN32(GetLastError());
    } else {
      LOG(L"FixDirectoryPermissions - Moved \"%s\" to \"%s\"\n", moveFrom.String(), moveTo.String());
    }

    // Before looping, clear any errors that might have been encountered so we
    // can correctly get errors from readdir.
    errno = 0;
  }
  if (errno != 0) {
    LOG(L"FixDirectoryPermissions - Directory listing failure\n");
    returnValue = FAILED(returnValue) ? returnValue : E_FAIL;
  }

  hrv = RemoveRecursive(tempPath, tempDir);
  returnValue = FAILED(returnValue) ? returnValue : hrv;
  if (SUCCEEDED(hrv)) {
    LOG(L"FixDirectoryPermissions - Successfully removed temp dir\n");
  } else {
    LOG(L"FixDirectoryPermissions - Failed to remove temp dir (error = %#X)\n", tempPath.String());
  }

  LOG(L"FixDirectoryPermissions - Returning %#X\n", returnValue);
  return returnValue;
}

/**
 * Splits an absolute path into its parent directory and filename.
 * For example, splits path="C:\foo\bar" into parentPath="C:\foo" and
 * filename="bar".
 */
static HRESULT SplitPath(const SimpleAutoString& path,
                         SimpleAutoString& parentPath,
                         SimpleAutoString& filename) {
  HRESULT hrv = parentPath.CopyFrom(path);
  if (FAILED(hrv) || parentPath.Length() == 0) {
    LOG(L"SplitPath failed to copy parent path (error = %#X)\n", hrv);
    return hrv;
  }

  hrv = GetFilename(parentPath, filename);
  if (FAILED(hrv)) {
    LOG(L"SplitPath - GetFilename failed (error = %#X)\n", hrv);
    return hrv;
  }

  size_t parentPathLen = parentPath.Length();
  if (parentPathLen < filename.Length() + 1) {
    LOG(L"SplitPath - parentPathLen is too short to truncate off the filename\n");
    return E_FAIL;
  }
  parentPathLen -= filename.Length() + 1;
  parentPath.Truncate(parentPathLen);
  if (parentPath.Length() == 0) {
    LOG(L"SplitPath - Failed to truncate the parent path\n");
    return E_FAIL;
  }
  LOG(L"SplitPath - \"%s\" split into \"%s\" and \"%s\"\n", path.String(), parentPath.String(), filename.String());

  return S_OK;
}

/**
 * Gets the filename of the given path. Also removes trailing path separators
 * from the input path.
 * Ex: If path="C:\foo\bar", filename="bar"
 */
static HRESULT GetFilename(SimpleAutoString& path, SimpleAutoString& filename) {
  // Remove trailing path separators.
  size_t pathLen = path.Length();
  if (pathLen == 0) {
    LOG(L"GetFilename called on empty path\n");
    return E_FAIL;
  }
  wchar_t lastChar = path.String()[pathLen - 1];
  while (lastChar == '/' || lastChar == '\\') {
    --pathLen;
    path.Truncate(pathLen);
    if (pathLen == 0) {
      LOG(L"GetFilename called on path containing only directory separators\n");
      return E_FAIL;
    }
    lastChar = path.String()[pathLen - 1];
  }

  const wchar_t* separator1 = wcsrchr(path.String(), '/');
  const wchar_t* separator2 = wcsrchr(path.String(), '\\');
  const wchar_t* separator =
      (separator1 > separator2) ? separator1 : separator2;
  if (separator == nullptr) {
    LOG(L"GetFilename unable to find directory separator\n");
    return E_FAIL;
  }

  HRESULT hrv = filename.CopyFrom(separator + 1);
  if (FAILED(hrv) || filename.Length() == 0) {
    LOG(L"GetFilename CopyFrom failed %#X\n", hrv);
    return E_FAIL;
  }
  LOG(L"GetFilename(%s) = \"%s\"\n", path.String(), filename.String());
  return S_OK;
}

/**
 * Returns true if the path conflicts with the leaf path.
 */
static bool PathConflictsWithLeaf(const SimpleAutoString& path,
                                  const SimpleAutoString& leafPath) {
  if (!leafPath.StartsWith(path)) {
    return false;
  }
  // Make sure that the next character after the path ends is a path separator
  // or the end of the string. We don't want to say that "C:\f" conflicts with
  // "C:\foo\bar".
  wchar_t charAfterPath = leafPath.String()[path.Length()];
  return (charAfterPath == L'\\' || charAfterPath == L'\0');
}
#endif  // XP_WIN
