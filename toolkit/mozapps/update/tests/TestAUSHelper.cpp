/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

#include "updatedefines.h"

#ifdef XP_WIN
#  include <aclapi.h>
#  include <string>
#  include <winioctl.h>
#  include <winternl.h>
#  include "certificatecheck.h"
#  include "commonupdatedir.h"
#  include "nsWindowsHelpers.h"
#  include "updatehelper.h"
#  define NS_main wmain
#  define NS_tgetcwd _wgetcwd
#  define NS_ttoi _wtoi
#else
#  define NS_main main
#  define NS_tgetcwd getcwd
#  define NS_ttoi atoi
#endif

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

static void WriteMsg(const NS_tchar* path, const char* status) {
  FILE* outFP = NS_tfopen(path, NS_T("wb"));
  if (!outFP) {
    return;
  }

  fprintf(outFP, "%s\n", status);
  fclose(outFP);
  outFP = nullptr;
}

static bool CheckMsg(const NS_tchar* path, const char* expected) {
  FILE* inFP = NS_tfopen(path, NS_T("rb"));
  if (!inFP) {
    return false;
  }

  struct stat ms;
  if (fstat(fileno(inFP), &ms)) {
    fclose(inFP);
    inFP = nullptr;
    return false;
  }

  char* mbuf = (char*)malloc(ms.st_size + 1);
  if (!mbuf) {
    fclose(inFP);
    inFP = nullptr;
    return false;
  }

  size_t r = ms.st_size;
  char* rb = mbuf;
  size_t c = fread(rb, sizeof(char), 50, inFP);
  r -= c;
  if (c == 0 && r) {
    free(mbuf);
    fclose(inFP);
    inFP = nullptr;
    return false;
  }
  mbuf[ms.st_size] = '\0';
  rb = mbuf;

  bool isMatch = strcmp(rb, expected) == 0;
  free(mbuf);
  fclose(inFP);
  inFP = nullptr;
  return isMatch;
}

#ifdef XP_WIN
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

enum class PermissionType {
  CorrectPermissions,
  IncorrectPermissions
};
/**
 * Generates the permission set that we want to be applied to the update
 * directory and its contents. Returns the permissions data via the result
 * outparam.
 *
 * Depending on the value of whichPerms, this can generate correct or
 * incorrect permissions. Incorrect permissions deny access rather than granting
 * it. This is necessary for setting bad permissions, since the file probably
 * inherited permissions from its parent, so granting incomplete permissions
 * will be insufficient.
 *
 * Returns true on success or false on failure.
 */
static bool GeneratePermissions(AutoPerms& result, PermissionType whichPerms) {
  result.sidIdentifierAuthority = SECURITY_NT_AUTHORITY;
  ZeroMemory(&result.ea, sizeof(result.ea));

  // Make Users group SID and add it to the Explicit Access List.
  PSID usersSID = nullptr;
  BOOL success = AllocateAndInitializeSid(
      &result.sidIdentifierAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
      DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &usersSID);
  result.usersSID.reset(usersSID);
  if (!success) {
    return false;
  }
  if (whichPerms == PermissionType::CorrectPermissions) {
    result.ea[0].grfAccessPermissions = FILE_ALL_ACCESS;
    result.ea[0].grfAccessMode = SET_ACCESS;
  } else {
    // Deny writing the extended attributes, which we can detect, but that won't
    // interfere with the ability to read the file or change the permissions.
    result.ea[0].grfAccessPermissions = FILE_WRITE_EA;
    result.ea[0].grfAccessMode = DENY_ACCESS;
  }
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
    return false;
  }
  if (whichPerms == PermissionType::CorrectPermissions) {
    result.ea[1].grfAccessPermissions = FILE_ALL_ACCESS;
    result.ea[1].grfAccessMode = SET_ACCESS;
  } else {
    // Deny writing the extended attributes, which we can detect, but that won't
    // interfere with the ability to read the file or change the permissions.
    result.ea[1].grfAccessPermissions = FILE_WRITE_EA;
    result.ea[1].grfAccessMode = DENY_ACCESS;
  }
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
    return false;
  }
  if (whichPerms == PermissionType::CorrectPermissions) {
    result.ea[2].grfAccessPermissions = FILE_ALL_ACCESS;
    result.ea[2].grfAccessMode = SET_ACCESS;
  } else {
    // Deny writing the extended attributes, which we can detect, but that won't
    // interfere with the ability to read the file or change the permissions.
    result.ea[2].grfAccessPermissions = FILE_WRITE_EA;
    result.ea[2].grfAccessMode = DENY_ACCESS;
  }
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
    return false;
  }

  result.securityDescriptorBuffer =
      mozilla::MakeUnique<uint8_t[]>(SECURITY_DESCRIPTOR_MIN_LENGTH);
  if (!result.securityDescriptorBuffer) {
    return false;
  }
  result.securityDescriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(
      result.securityDescriptorBuffer.get());
  success = InitializeSecurityDescriptor(result.securityDescriptor,
                                         SECURITY_DESCRIPTOR_REVISION);
  if (!success) {
    return false;
  }

  success =
      SetSecurityDescriptorDacl(result.securityDescriptor, TRUE, acl, FALSE);
  if (!success) {
    return false;
  }

  result.securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  result.securityAttributes.lpSecurityDescriptor = result.securityDescriptor;
  result.securityAttributes.bInheritHandle = FALSE;
  return true;
}

/**
 * Sets the permissions of the file indicated by path to the permissions passed.
 * Unfortunately this does not take a const string because SetNamedSecurityInfoW
 * doesn't take one.
 *
 * Returns true on success and false on failure.
 */
static bool SetPathPerms(wchar_t* path, const AutoPerms& perms) {
  DWORD drv = SetNamedSecurityInfoW(path, SE_FILE_OBJECT,
                                    DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                    perms.acl.get(), nullptr);
  return drv == ERROR_SUCCESS;
}

/**
 * Helper function to normalize the access mask by converting generic access
 * flags to specific ones to make it easier to check if permissions match.
 */
static void NormalizeAccessMask(ACCESS_MASK& mask) {
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

/**
 * Checks that the permissions on the specified file match (or are a superset
 * of) the permissions passed.
 *
 * Returns true if the permissions match. Returns false on failure or if the
 * permissions do not match.
 */
static bool PermsOk(const wchar_t* path, const AutoPerms& perms) {
  PACL dacl = nullptr;
  SECURITY_DESCRIPTOR* securityDescriptor = nullptr;
  DWORD drv = GetNamedSecurityInfoW(path,
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                &dacl,
                nullptr,
                reinterpret_cast<PSECURITY_DESCRIPTOR*>(&securityDescriptor));
  // Store the security descriptor in a UniquePtr so that it automatically
  // gets freed properly. We don't need to worry about dacl, since it will
  // point within the security descriptor.
  mozilla::UniquePtr<SECURITY_DESCRIPTOR, LocalFreeDeleter>
    autoSecurityDescriptor(securityDescriptor);
  if (drv != ERROR_SUCCESS || dacl == nullptr) {
    return false;
  }

  size_t eaLen = sizeof(perms.ea) / sizeof(perms.ea[0]);
  for (size_t eaIndex = 0; eaIndex < eaLen; ++eaIndex) {
    PTRUSTEE_W trustee = const_cast<PTRUSTEE_W>(&perms.ea[eaIndex].Trustee);
    ACCESS_MASK expectedMask = perms.ea[eaIndex].grfAccessPermissions;
    ACCESS_MASK actualMask;
    drv = GetEffectiveRightsFromAclW(dacl, trustee, &actualMask);
    if (drv != ERROR_SUCCESS) {
      return false;
    }
    NormalizeAccessMask(expectedMask);
    NormalizeAccessMask(actualMask);
    if ((actualMask & expectedMask) != expectedMask) {
      return false;
    }
  }

  return true;
}

static void MakeFullNtPath(const wchar_t* input, std::wstring& output) {
  output.clear();

  wchar_t buffer[MAX_PATH + 1];
  DWORD chars_needed = GetFullPathNameW(input, MAX_PATH + 1, buffer, nullptr);
  if (chars_needed > 0 && chars_needed <= MAX_PATH + 1) {
    if (buffer[0] != L'\\') {
      output.assign(L"\\??\\");
    }
    output.append(buffer);
  } else {
    if (input[0] != L'\\') {
      output.assign(L"\\??\\");
    }
    output.append(input);
  }
}

typedef struct _REPARSE_DATA_BUFFER {
  ULONG  ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR  DataBuffer[1];
    } GenericReparseBuffer;
  } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

#define REPARSE_DATA_BUFFER_HEADER_LENGTH \
  FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer.DataBuffer)
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)

/**
 * Creates a directory junction at link, pointing to target.
 * Both paths should point to existing directories. The directory pointed to by
 * link must be empty.
 *
 * Returns true on success, false on failure.
 */
static bool MakeDirJunction(const wchar_t* link, const wchar_t* target) {
  std::wstring targetNtPath;
  MakeFullNtPath(target, targetNtPath);

  const size_t target_size = targetNtPath.size() * sizeof(wchar_t);
  // The size of the union in _REPARSE_DATA_BUFFER, including the size needed
  // for the PathBuffer. One extra wchar is for the terminating null. The other
  // is for PrintName, which we are setting to 0 length, and will therefore
  // consist solely of a terminating null.
  const size_t union_size = sizeof(USHORT) * 4 +
                            target_size + 2 * sizeof(wchar_t);
  const size_t buffer_size = union_size + REPARSE_DATA_BUFFER_HEADER_LENGTH;

  mozilla::UniquePtr<uint8_t[]> autoBuffer =
    mozilla::MakeUnique<uint8_t[]>(buffer_size);
  REPARSE_DATA_BUFFER* buffer =
    reinterpret_cast<REPARSE_DATA_BUFFER*>(autoBuffer.get());

  buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
  buffer->ReparseDataLength = static_cast<USHORT>(union_size);
  buffer->Reserved = 0;

  buffer->MountPointReparseBuffer.SubstituteNameOffset = 0;
  buffer->MountPointReparseBuffer.SubstituteNameLength =
    static_cast<USHORT>(target_size);
  memcpy(buffer->MountPointReparseBuffer.PathBuffer, targetNtPath.c_str(),
         target_size + sizeof(wchar_t));

  // This will just point directly to the existing terminating null.
  buffer->MountPointReparseBuffer.PrintNameOffset =
    static_cast<USHORT>(target_size + sizeof(wchar_t));
  buffer->MountPointReparseBuffer.PrintNameLength = 0;
  *(buffer->MountPointReparseBuffer.PathBuffer + targetNtPath.size() + 1) =
    L'\0';

  nsAutoHandle reparsePointAutoHandle;
  HANDLE handle = CreateFile(link,
    GENERIC_READ | GENERIC_WRITE,
    0,
    0,
    OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
    0);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  reparsePointAutoHandle.own(handle);

  DWORD dummy;
  BOOL success = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer,
                                 buffer_size, nullptr, 0, &dummy, nullptr);
  return !!success;
}

typedef struct _FILE_LINK_INFORMATION {
  BOOLEAN ReplaceIfExists;
  HANDLE  RootDirectory;
  ULONG   FileNameLength;
  WCHAR   FileName[1];
} FILE_LINK_INFORMATION, *PFILE_LINK_INFORMATION;

const ULONG FileLinkInformation = 11;

typedef NTSTATUS(__stdcall *_ZwSetInformationFile)(
  _In_   HANDLE            FileHandle,
  _Out_  PIO_STATUS_BLOCK  IoStatusBlock,
  _In_   PVOID             FileInformation,
  _In_   ULONG             Length,
  _In_   ULONG             FileInformationClass
);
typedef VOID(NTAPI *_RtlInitUnicodeString)(
  PUNICODE_STRING DestinationString,
  PCWSTR SourceString
);
typedef NTSTATUS(NTAPI* _NtOpenFile)(
  _Out_ PHANDLE            FileHandle,
  _In_  ACCESS_MASK        DesiredAccess,
  _In_  POBJECT_ATTRIBUTES ObjectAttributes,
  _Out_ PIO_STATUS_BLOCK   IoStatusBlock,
  _In_  ULONG              ShareAccess,
  _In_  ULONG              OpenOptions
);
FARPROC GetProcAddressNT(LPCSTR lpName)
{
  return GetProcAddress(GetModuleHandleW(L"ntdll"), lpName);
}

#define DEFINE_NTDLL(x) _ ## x f ## x = (_ ## x)GetProcAddressNT(#x)

/**
 * Creates a hard link at link, pointing to target.
 * Target must be an existing regular file (hard links cannot be made to
 * directories).
 *
 * Returns true on success, false on failure.
 */
static bool MakeHardLink(const wchar_t* link, const wchar_t* target) {
  DEFINE_NTDLL(ZwSetInformationFile);
  DEFINE_NTDLL(RtlInitUnicodeString);
  DEFINE_NTDLL(NtOpenFile);

  std::wstring linkNtPath, targetNtPath;
  MakeFullNtPath(link, linkNtPath);
  MakeFullNtPath(target, targetNtPath);

  size_t linkLength = linkNtPath.size() * sizeof(wchar_t);

  size_t linkInfoSize = sizeof(FILE_LINK_INFORMATION) + linkLength
                      - sizeof(wchar_t);
  mozilla::UniquePtr<uint8_t[]> autoLinkInfo =
    mozilla::MakeUnique<uint8_t[]>(linkInfoSize);
  FILE_LINK_INFORMATION* link_info =
    reinterpret_cast<FILE_LINK_INFORMATION*>(autoLinkInfo.get());

  memcpy(&link_info->FileName[0], linkNtPath.c_str(), linkLength);
  link_info->ReplaceIfExists = TRUE;
  link_info->FileNameLength = linkLength;

  UNICODE_STRING targetUnicode = { 0 };
  fRtlInitUnicodeString(&targetUnicode, targetNtPath.c_str());

  OBJECT_ATTRIBUTES obj_attr = { 0 };
  InitializeObjectAttributes(&obj_attr, &targetUnicode, OBJ_CASE_INSENSITIVE,
                             nullptr, nullptr);

  HANDLE handle = INVALID_HANDLE_VALUE;
  IO_STATUS_BLOCK io_status = { { 0 } };
  NTSTATUS status = fNtOpenFile(&handle, MAXIMUM_ALLOWED, &obj_attr, &io_status,
                                FILE_SHARE_READ, 0);
  nsAutoHandle autoHandle(handle);
  if (!NT_SUCCESS(status)) {
    return false;
  }

  io_status = { { 0 } };
  status = fZwSetInformationFile(handle, &io_status, link_info, linkInfoSize,
                                 FileLinkInformation);
  return NT_SUCCESS(status);
}
#endif

int NS_main(int argc, NS_tchar** argv) {
  if (argc == 2) {
    if (!NS_tstrcmp(argv[1], NS_T("post-update-async")) ||
        !NS_tstrcmp(argv[1], NS_T("post-update-sync"))) {
      NS_tchar exePath[MAXPATHLEN];
#ifdef XP_WIN
      if (!::GetModuleFileNameW(0, exePath, MAXPATHLEN)) {
        return 1;
      }
#else
      if (!NS_tvsnprintf(exePath, sizeof(exePath) / sizeof(exePath[0]),
                         NS_T("%s"), argv[0])) {
        return 1;
      }
#endif
      NS_tchar runFilePath[MAXPATHLEN];
      if (!NS_tvsnprintf(runFilePath,
                         sizeof(runFilePath) / sizeof(runFilePath[0]),
                         NS_T("%s.running"), exePath)) {
        return 1;
      }
#ifdef XP_WIN
      if (!NS_taccess(runFilePath, F_OK)) {
        // This makes it possible to check if the post update process was
        // launched twice which happens when the service performs an update.
        NS_tchar runFilePathBak[MAXPATHLEN];
        if (!NS_tvsnprintf(runFilePathBak,
                           sizeof(runFilePathBak) / sizeof(runFilePathBak[0]),
                           NS_T("%s.bak"), runFilePath)) {
          return 1;
        }
        MoveFileExW(runFilePath, runFilePathBak, MOVEFILE_REPLACE_EXISTING);
      }
#endif
      WriteMsg(runFilePath, "running");

      if (!NS_tstrcmp(argv[1], NS_T("post-update-sync"))) {
#ifdef XP_WIN
        Sleep(2000);
#else
        sleep(2);
#endif
      }

      NS_tchar logFilePath[MAXPATHLEN];
      if (!NS_tvsnprintf(logFilePath,
                         sizeof(logFilePath) / sizeof(logFilePath[0]),
                         NS_T("%s.log"), exePath)) {
        return 1;
      }
      WriteMsg(logFilePath, "post-update");
      return 0;
    }
  }

  if (argc < 3) {
    fprintf(
        stderr,
        "\n"
        "Application Update Service Test Helper\n"
        "\n"
        "Usage: WORKINGDIR INFILE OUTFILE -s SECONDS [FILETOLOCK]\n"
        "   or: WORKINGDIR LOGFILE [ARG2 ARG3...]\n"
        "   or: signature-check filepath\n"
        "   or: setup-symlink dir1 dir2 file symlink\n"
        "   or: remove-symlink dir1 dir2 file symlink\n"
        "   or: check-symlink symlink\n"
        "   or: post-update\n"
        "   or: create-update-dir\n"
        "\n"
        "  WORKINGDIR  \tThe relative path to the working directory to use.\n"
        "  INFILE      \tThe relative path from the working directory for the "
        "file to\n"
        "              \tread actions to perform such as finish.\n"
        "  OUTFILE     \tThe relative path from the working directory for the "
        "file to\n"
        "              \twrite status information.\n"
        "  SECONDS     \tThe number of seconds to sleep.\n"
        "  FILETOLOCK  \tThe relative path from the working directory to an "
        "existing\n"
        "              \tfile to open exlusively.\n"
        "              \tOnly available on Windows platforms and silently "
        "ignored on\n"
        "              \tother platforms.\n"
        "  LOGFILE     \tThe relative path from the working directory to log "
        "the\n"
        "              \tcommand line arguments.\n"
        "  ARG2 ARG3...\tArguments to write to the LOGFILE after the preceding "
        "command\n"
        "              \tline arguments.\n"
        "\n"
        "Note: All paths must be relative.\n"
        "\n");
    return 1;
  }

  if (!NS_tstrcmp(argv[1], NS_T("check-signature"))) {
#if defined(XP_WIN) && defined(MOZ_MAINTENANCE_SERVICE)
    if (ERROR_SUCCESS == VerifyCertificateTrustForFile(argv[2])) {
      return 0;
    } else {
      return 1;
    }
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("setup-symlink"))) {
#ifdef XP_UNIX
    NS_tchar path[MAXPATHLEN];
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s"),
                       NS_T("/tmp"), argv[2])) {
      return 1;
    }
    if (mkdir(path, 0755)) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s/%s"),
                       NS_T("/tmp"), argv[2], argv[3])) {
      return 1;
    }
    if (mkdir(path, 0755)) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]),
                       NS_T("%s/%s/%s/%s"), NS_T("/tmp"), argv[2], argv[3],
                       argv[4])) {
      return 1;
    }
    FILE* file = NS_tfopen(path, NS_T("w"));
    if (file) {
      fputs(NS_T("test"), file);
      fclose(file);
    }
    if (symlink(path, argv[5]) != 0) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s"),
                       NS_T("/tmp"), argv[2])) {
      return 1;
    }
    if (argc > 6 && !NS_tstrcmp(argv[6], NS_T("change-perm"))) {
      if (chmod(path, 0644)) {
        return 1;
      }
    }
    return 0;
#else
    // Not implemented on non-Unix platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("remove-symlink"))) {
#ifdef XP_UNIX
    // The following can be called at the start of a test in case these symlinks
    // need to be removed if they already exist and at the end of a test to
    // remove the symlinks created by the test so ignore file doesn't exist
    // errors.
    NS_tchar path[MAXPATHLEN];
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s"),
                       NS_T("/tmp"), argv[2])) {
      return 1;
    }
    if (chmod(path, 0755) && errno != ENOENT) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]),
                       NS_T("%s/%s/%s/%s"), NS_T("/tmp"), argv[2], argv[3],
                       argv[4])) {
      return 1;
    }
    if (unlink(path) && errno != ENOENT) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s/%s"),
                       NS_T("/tmp"), argv[2], argv[3])) {
      return 1;
    }
    if (rmdir(path) && errno != ENOENT) {
      return 1;
    }
    if (!NS_tvsnprintf(path, sizeof(path) / sizeof(path[0]), NS_T("%s/%s"),
                       NS_T("/tmp"), argv[2])) {
      return 1;
    }
    if (rmdir(path) && errno != ENOENT) {
      return 1;
    }
    return 0;
#else
    // Not implemented on non-Unix platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("check-symlink"))) {
#ifdef XP_UNIX
    struct stat ss;
    if (lstat(argv[2], &ss)) {
      return 1;
    }
    return S_ISLNK(ss.st_mode) ? 0 : 1;
#else
    // Not implemented on non-Unix platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("wait-for-service-stop"))) {
#ifdef XP_WIN
    const int maxWaitSeconds = NS_ttoi(argv[3]);
    LPCWSTR serviceName = argv[2];
    DWORD serviceState = WaitForServiceStop(serviceName, maxWaitSeconds);
    if (SERVICE_STOPPED == serviceState) {
      return 0;
    } else {
      return serviceState;
    }
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("wait-for-application-exit"))) {
#ifdef XP_WIN
    const int maxWaitSeconds = NS_ttoi(argv[3]);
    LPCWSTR application = argv[2];
    DWORD ret = WaitForProcessExit(application, maxWaitSeconds);
    if (ERROR_SUCCESS == ret) {
      return 0;
    } else if (WAIT_TIMEOUT == ret) {
      return 1;
    } else {
      return 2;
    }
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("is-process-running"))) {
#ifdef XP_WIN
    LPCWSTR application = argv[2];
    return (ERROR_NOT_FOUND == IsProcessRunning(application)) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("launch-service"))) {
#ifdef XP_WIN
    DWORD ret =
        LaunchServiceSoftwareUpdateCommand(argc - 2, (LPCWSTR*)argv + 2);
    if (ret != ERROR_SUCCESS) {
      // 192 is used to avoid reusing a possible return value from the call to
      // WaitForServiceStop
      return 0x000000C0;
    }
    // Wait a maximum of 120 seconds.
    DWORD lastState = WaitForServiceStop(SVC_NAME, 120);
    if (SERVICE_STOPPED == lastState) {
      return 0;
    }
    return lastState;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("create-update-dir"))) {
#ifdef XP_WIN
    mozilla::UniquePtr<wchar_t[]> updateDir;
    HRESULT result = GetCommonUpdateDirectory(
        argv[2], SetPermissionsOf::BaseDirIfNotExists, updateDir);
    return SUCCEEDED(result) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("check-perms-correct"))) {
#ifdef XP_WIN
    if (argc != 3) {
      return 1;
    }
    AutoPerms perms;
    if (!GeneratePermissions(perms, PermissionType::CorrectPermissions)) {
      return 1;
    }
    return PermsOk(argv[2], perms) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("set-bad-perms"))) {
#ifdef XP_WIN
    if (argc != 3) {
      return 1;
    }
    wchar_t* path = const_cast<wchar_t*>(argv[2]);
    AutoPerms perms;
    if (!GeneratePermissions(perms, PermissionType::IncorrectPermissions)) {
      return 1;
    }
    return SetPathPerms(path, perms) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("make-dir-junction"))) {
#ifdef XP_WIN
    if (argc != 4) {
      return 1;
    }
    return MakeDirJunction(argv[2], argv[3]) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (!NS_tstrcmp(argv[1], NS_T("make-hard-link"))) {
#ifdef XP_WIN
    if (argc != 4) {
      return 1;
    }
    return MakeHardLink(argv[2], argv[3]) ? 0 : 1;
#else
    // Not implemented on non-Windows platforms
    return 1;
#endif
  }

  if (NS_tchdir(argv[1]) != 0) {
    return 1;
  }

  // File in use test helper section
  if (!NS_tstrcmp(argv[4], NS_T("-s"))) {
    // Note: glibc's getcwd() allocates the buffer dynamically using malloc(3)
    // if buf (the 1st param) is NULL so free cwd when it is no longer needed.
    NS_tchar* cwd = NS_tgetcwd(nullptr, 0);
    NS_tchar inFilePath[MAXPATHLEN];
    if (!NS_tvsnprintf(inFilePath, sizeof(inFilePath) / sizeof(inFilePath[0]),
                       NS_T("%s/%s"), cwd, argv[2])) {
      return 1;
    }
    NS_tchar outFilePath[MAXPATHLEN];
    if (!NS_tvsnprintf(outFilePath,
                       sizeof(outFilePath) / sizeof(outFilePath[0]),
                       NS_T("%s/%s"), cwd, argv[3])) {
      return 1;
    }
    free(cwd);

    int seconds = NS_ttoi(argv[5]);
#ifdef XP_WIN
    HANDLE hFile = INVALID_HANDLE_VALUE;
    if (argc == 7) {
      hFile = CreateFileW(argv[6], DELETE | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, 0, nullptr);
      if (hFile == INVALID_HANDLE_VALUE) {
        WriteMsg(outFilePath, "error_locking");
        return 1;
      }
    }

    WriteMsg(outFilePath, "sleeping");
    int i = 0;
    while (!CheckMsg(inFilePath, "finish\n") && i++ <= seconds) {
      Sleep(1000);
    }

    if (argc == 7) {
      CloseHandle(hFile);
    }
#else
    WriteMsg(outFilePath, "sleeping");
    int i = 0;
    while (!CheckMsg(inFilePath, "finish\n") && i++ <= seconds) {
      sleep(1);
    }
#endif
    WriteMsg(outFilePath, "finished");
    return 0;
  }

  {
    // Command line argument test helper section
    NS_tchar logFilePath[MAXPATHLEN];
    if (!NS_tvsnprintf(logFilePath,
                       sizeof(logFilePath) / sizeof(logFilePath[0]), NS_T("%s"),
                       argv[2])) {
      return 1;
    }

    FILE* logFP = NS_tfopen(logFilePath, NS_T("wb"));
    if (!logFP) {
      return 1;
    }
    for (int i = 1; i < argc; ++i) {
      fprintf(logFP, LOG_S "\n", argv[i]);
    }

    fclose(logFP);
    logFP = nullptr;
  }

  return 0;
}
