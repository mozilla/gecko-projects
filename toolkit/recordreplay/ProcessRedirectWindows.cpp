/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This needs to be included before any other windows headers :(
#include <winsock2.h>

// Copied from nsOSHelperAppService.h, to make IApplicationAssociationRegistration visible.
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600

#include "ProcessRedirect.h"

#include "InfallibleVector.h"

#include <windows.h>

#include <aclapi.h>
#include <audiopolicy.h>
#include <evntrace.h>
#include <iphlpapi.h>
#include <mftransform.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <setupapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincrypt.h>
#include <wpcapi.h>

#include <mozilla/RefPtr.h>
#include "nsISupportsImpl.h"

#include "prrecordreplay.h"

#include <algorithm>

using std::min;

namespace mozilla {
namespace recordreplay {

#define MACROAW(aMacro, aName)                  \
  aMacro(aName ## A)                            \
  aMacro(aName ## W)

#define MACROEX(aMacro, aName)                  \
  aMacro(aName)                                 \
  aMacro(aName ## Ex)

#define MACROEXAW(aMacro, aName)                \
  aMacro(aName ## A)                            \
  aMacro(aName ## W)                            \
  aMacro(aName ## ExA)                          \
  aMacro(aName ## ExW)

#define FOR_EACH_KERNEL32_REDIRECTION(MACRO)    \
  MACRO(CancelIo)                               \
  MACRO(CloseHandle)                            \
  MACRO(ConnectNamedPipe)                       \
  MACROEXAW(MACRO, CopyFile)                    \
  MACROEXAW(MACRO, CreateDirectory)             \
  MACROAW(MACRO, CreateEvent)                   \
  MACROAW(MACRO, CreateFile)                    \
  MACROAW(MACRO, CreateFileMapping)             \
  MACROAW(MACRO, CreateHardLink)                \
  MACROAW(MACRO, CreateSemaphore)               \
  MACRO(CreateIoCompletionPort)                 \
  MACROAW(MACRO, CreateJobObject)               \
  MACROAW(MACRO, CreateNamedPipe)               \
  MACRO(CreatePipe)                             \
  MACRO(CreateThread)                           \
  MACRO(CreateTimerQueueTimer)                  \
  MACROAW(MACRO, CreateWaitableTimer)           \
  MACRO(DeleteCriticalSection)                  \
  MACROAW(MACRO, DeleteFile)                    \
  MACRO(DeleteTimerQueueTimer)                  \
  MACRO(DeviceIoControl)                        \
  MACRO(DuplicateHandle)                        \
  MACRO(EnterCriticalSection)                   \
  MACROAW(MACRO, ExpandEnvironmentStrings)      \
  MACRO(FindClose)                              \
  MACROEXAW(MACRO, FindFirstFile)               \
  MACROAW(MACRO, FindNextFile)                  \
  MACROEXAW(MACRO, GetComputerName)             \
  MACRO(GetCurrentProcess)                      \
  MACRO(GetCurrentProcessId)                    \
  MACROAW(MACRO, GetDateFormat)                 \
  MACROEXAW(MACRO, GetDiskFreeSpace)            \
  MACRO(GetEnvironmentStringsW)                 \
  MACROAW(MACRO, GetEnvironmentVariable)        \
  MACROEXAW(MACRO, GetFileAttributes)           \
  MACRO(GetFileInformationByHandle)             \
  MACROEX(MACRO, GetFileSize)                   \
  MACRO(GetFileTime)                            \
  MACROAW(MACRO, GetFullPathName)               \
  MACROAW(MACRO, GetGeoInfo)                    \
  MACRO(GetHandleInformation)                   \
  MACROAW(MACRO, GetLocaleInfo)                 \
  MACRO(GetLocalTime)                           \
  MACRO(GetLogicalProcessorInformation)         \
  MACROAW(MACRO, GetLongPathName)               \
  MACROAW(MACRO, GetModuleFileName)             \
  MACROEXAW(MACRO, GetModuleHandle)             \
  MACROAW(MACRO, GetNumberFormat)               \
  MACRO(GetOverlappedResult)                    \
  MACRO(GetProcessHeap)                         \
  MACRO(GetProcessHeaps)                        \
  MACROAW(MACRO, GetProfileString)              \
  MACRO(GetQueuedCompletionStatus)              \
  MACROAW(MACRO, GetShortPathName)              \
  MACRO(GetSystemTime)                          \
  MACRO(GetSystemTimeAdjustment)                \
  MACRO(GetSystemTimeAsFileTime)                \
  MACROAW(MACRO, GetSystemDirectory)            \
  MACRO(GetSystemInfo)                          \
  MACROAW(MACRO, GetTempFileName)               \
  MACROAW(MACRO, GetTempPath)                   \
  MACROAW(MACRO, GetTimeFormat)                 \
  MACRO(GetTimeFormatEx)                        \
  MACRO(GetTimeZoneInformation)                 \
  MACROAW(MACRO, GetWindowsDirectory)           \
  MACRO(InitializeCriticalSectionEx)            \
  MACRO(IsDebuggerPresent)                      \
  MACRO(LeaveCriticalSection)                   \
  MACROEX(MACRO, MapViewOfFile)                 \
  MACROEXAW(MACRO, MoveFile)                    \
  MACROAW(MACRO, OpenEvent)                     \
  MACRO(OpenFile)                               \
  MACROAW(MACRO, OpenFileMapping)               \
  MACROAW(MACRO, OpenJobObject)                 \
  MACRO(OutputDebugStringW)                     \
  MACRO(PostQueuedCompletionStatus)             \
  MACRO(QueryPerformanceCounter)                \
  MACRO(QueryPerformanceFrequency)              \
  MACROEX(MACRO, ReadFile)                      \
  MACRO(ReadFileScatter)                        \
  MACRO(ReadProcessMemory)                      \
  MACROAW(MACRO, RemoveDirectory)               \
  MACRO(ResetEvent)                             \
  MACROAW(MACRO, SearchPath)                    \
  MACROAW(MACRO, SetCurrentDirectory)           \
  MACRO(SetEndOfFile)                           \
  MACROAW(MACRO, SetEnvironmentStrings)         \
  MACROAW(MACRO, SetEnvironmentVariable)        \
  MACRO(SetEvent)                               \
  MACROAW(MACRO, SetFileAttributes)             \
  MACROEX(MACRO, SetFilePointer)                \
  MACRO(SetFileTime)                            \
  MACRO(TryEnterCriticalSection)                \
  MACRO(VirtualAlloc)                           \
  MACRO(VirtualAllocEx)                         \
  MACRO(VirtualFree)                            \
  MACRO(VirtualFreeEx)                          \
  MACRO(VirtualProtect)                         \
  MACRO(VirtualProtectEx)                       \
  MACROEX(MACRO, WaitForMultipleObjects)        \
  MACROEX(MACRO, WaitForSingleObject)           \
  MACRO(WriteFile)                              \
  MACRO(WriteFileEx)                            \
  MACRO(WriteFileGather)                        \
  MACRO(_hread)                                 \
  MACRO(_hwrite)                                \
  MACRO(_lclose)                                \
  MACRO(_lcreat)                                \
  MACRO(_llseek)                                \
  MACRO(_lopen)                                 \
  MACRO(_lread)                                 \
  MACRO(_lwrite)

#define FOR_EACH_SHELL32_REDIRECTION(MACRO)     \
  MACRO(CommandLineToArgvW)                     \
  MACRO(ILCreateFromPathA)                      \
  MACRO(ILCreateFromPathW)                      \
  MACRO(SetCurrentProcessExplicitAppUserModelID) \
  MACRO(SHGetKnownFolderPath)                   \
  MACRO(SHGetPathFromIDListW)                   \
  MACRO(SHGetSpecialFolderLocation)             \
  MACRO(SHGetSpecialFolderPathW)                \
  /*MACRO(SHLoadLibraryFromKnownFolder)*/       \
  MACRO(SHOpenFolderAndSelectItems)

#define FOR_EACH_USER32_REDIRECTION(MACRO)      \
  MACRO(ActivateKeyboardLayout)                 \
  MACROEX(MACRO, AdjustWindowRect)              \
  MACRO(AnimateWindow)                          \
  MACRO(BeginDeferWindowPos)                    \
  MACRO(BeginPaint)                             \
  MACRO(CallNextHookEx)                         \
  MACROAW(MACRO, CallWindowProc)                \
  MACROAW(MACRO, ChangeDisplaySettings)         \
  MACRO(ClientToScreen)                         \
  MACRO(CloseClipboard)                         \
  MACRO(CloseDesktop)                           \
  /*MACRO(CloseGestureInfoHandle)*/             \
  MACRO(CloseWindow)                            \
  MACRO(CreateCaret)                            \
  MACRO(CreateIconIndirect)                     \
  MACROAW(MACRO, CreateWindowEx)                \
  MACROAW(MACRO, DefWindowProc)                 \
  MACRO(DestroyCaret)                           \
  MACRO(DestroyIcon)                            \
  MACRO(DestroyWindow)                          \
  MACRO(DispatchMessageW)                       \
  MACRO(DrawEdge)                               \
  MACRO(DrawFocusRect)                          \
  MACRO(DrawFrameControl)                       \
  MACRO(EmptyClipboard)                         \
  MACRO(EnableWindow)                           \
  MACRO(EndPaint)                               \
  MACRO(EnumChildWindows)                       \
  MACROAW(MACRO, EnumDisplayDevices)            \
  MACRO(EnumDisplayMonitors)                    \
  MACROEXAW(MACRO, EnumDisplaySettings)         \
  MACRO(EnumThreadWindows)                      \
  MACROEXAW(MACRO, FindWindow)                  \
  MACROEX(MACRO, FlashWindow)                   \
  MACRO(GetActiveWindow)                        \
  MACRO(GetAncestor)                            \
  MACRO(GetClassInfoW)                          \
  MACRO(GetClipboardData)                       \
  MACRO(GetCursorPos)                           \
  MACROEX(MACRO, GetDC)                         \
  MACRO(GetDlgItem)                             \
  MACRO(GetDlgItemInt)                          \
  MACROAW(MACRO, GetDlgItemText)                \
  MACRO(GetDoubleClickTime)                     \
  MACRO(GetFocus)                               \
  MACRO(GetForegroundWindow)                    \
  MACRO(GetIconInfo)                            \
  MACRO(GetKeyState)                            \
  MACRO(GetKeyboardLayout)                      \
  MACRO(GetKeyboardLayoutList)                  \
  MACROAW(MACRO, GetKeyboardLayoutName)         \
  MACRO(GetKeyboardState)                       \
  MACRO(GetLastInputInfo)                       \
  MACRO(GetMessageExtraInfo)                    \
  MACRO(GetMessagePos)                          \
  MACRO(GetMessageTime)                         \
  MACROAW(MACRO, GetMonitorInfo)                \
  MACRO(GetQueueStatus)                         \
  MACRO(GetSysColor)                            \
  MACRO(GetSysColorBrush)                       \
  MACRO(GetSystemMenu)                          \
  MACRO(GetSystemMetrics)                       \
  /*MACRO(GetTouchInputInfo)*/                  \
  MACRO(GetUpdateRect)                          \
  MACRO(GetUpdateRgn)                           \
  MACRO(GetWindowThreadProcessId)               \
  MACROEX(MACRO, InSendMessage)                 \
  MACRO(InflateRect)                            \
  MACRO(IsClipboardFormatAvailable)             \
  MACRO(IsIconic)                               \
  MACRO(IsWindowEnabled)                        \
  MACRO(IsWindowVisible)                        \
  MACRO(KillTimer)                              \
  MACROAW(MACRO, LoadCursor)                    \
  MACROAW(MACRO, LoadKeyboardLayout)            \
  MACROAW(MACRO, MapVirtualKeyEx)               \
  MACRO(MapWindowPoints)                        \
  MACRO(MessageBeep)                            \
  MACRO(MonitorFromPoint)                       \
  MACRO(MonitorFromRect)                        \
  MACRO(MonitorFromWindow)                      \
  MACROEX(MACRO, MsgWaitForMultipleObjects)     \
  MACRO(OpenClipboard)                          \
  MACROAW(MACRO, PeekMessage)                   \
  MACROAW(MACRO, PostMessage)                   \
  MACRO(PostQuitMessage)                        \
  MACRO(RedrawWindow)                           \
  MACRO(RegisterClassW)                         \
  MACROAW(MACRO, RegisterClipboardFormat)       \
  MACROAW(MACRO, RegisterWindowMessage)         \
  MACRO(ReleaseCapture)                         \
  MACRO(ReleaseDC)                              \
  MACROAW(MACRO, SendMessage)                   \
  MACROAW(MACRO, SetMenuItemInfo)               \
  MACRO(SetWinEventHook)                        \
  MACROAW(MACRO, SetWindowLong)                 \
  MACRO(SetWindowPos)                           \
  MACRO(SetWindowRgn)                           \
  MACROAW(MACRO, SetWindowsHookEx)              \
  MACRO(ShowCaret)                              \
  MACRO(ShowCursor)                             \
  MACRO(ShowWindow)                             \
  MACROAW(MACRO, SystemParametersInfo)          \
  MACRO(TrackMouseEvent)                        \
  MACRO(TrackPopupMenu)                         \
  MACRO(TranslateMessage)                       \
  MACRO(UnloadKeyboardLayout)

#define FOR_EACH_DLL_REDIRECTION(MACRO)         \
  MACRO(advapi32, AddAccessAllowedAce)          \
  MACRO(advapi32, AdjustTokenPrivileges)        \
  MACRO(advapi32, AllocateAndInitializeSid)     \
  MACRO(advapi32, BuildExplicitAccessWithNameW) \
  MACRO(advapi32, ConvertSecurityDescriptorToStringSecurityDescriptorW) \
  MACRO(advapi32, ConvertSidToStringSidW)       \
  MACRO(advapi32, ConvertStringSecurityDescriptorToSecurityDescriptorW) \
  MACRO(advapi32, ConvertStringSidToSidW)       \
  MACRO(advapi32, CopySid)                      \
  MACRO(advapi32, CryptAcquireContextW)         \
  MACRO(advapi32, CryptCreateHash)              \
  MACRO(advapi32, CryptDecrypt)                 \
  MACRO(advapi32, CryptDestroyHash)             \
  MACRO(advapi32, CryptDestroyKey)              \
  MACRO(advapi32, CryptExportKey)               \
  MACRO(advapi32, CryptGenRandom)               \
  MACRO(advapi32, CryptGetDefaultProviderW)     \
  MACRO(advapi32, CryptGetHashParam)            \
  MACRO(advapi32, CryptGetUserKey)              \
  MACRO(advapi32, CryptHashData)                \
  MACRO(advapi32, CryptImportKey)               \
  MACRO(advapi32, CryptReleaseContext)          \
  MACRO(advapi32, CryptSetHashParam)            \
  MACRO(advapi32, CryptSignHashW)               \
  MACRO(advapi32, CryptVerifySignatureW)        \
  MACRO(advapi32, GetLengthSid)                 \
  MACRO(advapi32, GetNamedSecurityInfoW)        \
  MACRO(advapi32, GetTokenInformation)          \
  MACRO(advapi32, GetUserNameW)                 \
  MACRO(advapi32, InitializeAcl)                \
  MACRO(advapi32, InitializeSecurityDescriptor) \
  MACRO(advapi32, LookupAccountNameW)           \
  MACRO(advapi32, LookupAccountSidW)            \
  MACRO(advapi32, OpenProcessToken)             \
  MACRO(advapi32, RegisterTraceGuidsW)          \
  MACRO(advapi32, RegEnumValueW)                \
  MACRO(advapi32, RegCloseKey)                  \
  MACRO(advapi32, RegOpenKeyExW)                \
  MACRO(advapi32, RegQueryValueExW)             \
  MACRO(advapi32, SetEntriesInAclW)             \
  MACRO(advapi32, SetSecurityDescriptorDacl)    \
  MACRO(advapi32, SetSecurityDescriptorGroup)   \
  MACRO(advapi32, SetSecurityDescriptorOwner)   \
  MACRO(advapi32, SystemFunction036 /*RtlGenRandom*/) \
  MACRO(advapi32, UnregisterTraceGuids)         \
  MACRO(gdi32, AddFontResourceExW)              \
  MACRO(gdi32, CreateCompatibleDC)              \
  MACRO(gdi32, CreateDIBSection)                \
  MACRO(gdi32, CreateFontIndirectW)             \
  MACRO(gdi32, DeleteDC)                        \
  MACRO(gdi32, DeleteObject)                    \
  MACRO(gdi32, EnumFontFamiliesExW)             \
  MACRO(gdi32, GdiFlush)                        \
  MACRO(gdi32, GetCharWidthI)                   \
  MACRO(gdi32, GetClipBox)                      \
  MACRO(gdi32, GetClipRgn)                      \
  MACRO(gdi32, GetDeviceCaps)                   \
  MACRO(gdi32, GetFontData)                     \
  MACRO(gdi32, GetGlyphIndicesW)                \
  MACRO(gdi32, GetGlyphOutlineW)                \
  MACRO(gdi32, GetGraphicsMode)                 \
  MACRO(gdi32, GetICMProfileW)                  \
  MACRO(gdi32, GetObjectW)                      \
  MACRO(gdi32, GetOutlineTextMetricsW)          \
  MACRO(gdi32, GetTextExtentPoint32W)           \
  MACRO(gdi32, GetTextMetricsA)                 \
  MACRO(gdi32, GetTextMetricsW)                 \
  MACRO(gdi32, GetWorldTransform)               \
  MACRO(gdi32, IntersectClipRect)               \
  MACRO(gdi32, ModifyWorldTransform)            \
  MACRO(gdi32, RestoreDC)                       \
  MACRO(gdi32, SaveDC)                          \
  MACRO(gdi32, SelectClipRgn)                   \
  MACRO(gdi32, SelectObject)                    \
  MACRO(gdi32, SetGraphicsMode)                 \
  MACRO(gdi32, SetMapMode)                      \
  MACRO(gdi32, SetWorldTransform)               \
  MACRO(iphlpapi, GetAdaptersInfo)              \
  MACRO(kernelbase, FreeLibrary)                \
  /*MACRO(kernelbase, LoadLibraryA)*/           \
  /*MACRO(kernelbase, LoadLibraryW)*/           \
  /*MACRO(kernelbase, LoadLibraryExA)*/         \
  MACRO(kernelbase, LoadLibraryExW)             \
  MACRO(kernelbase, LocalFree)                  \
  MACRO(mfplat, MFShutdown)                     \
  MACRO(mfplat, MFStartup)                      \
  MACRO(ntdll, NtWaitForSingleObject)           \
  MACRO(ole32, CLSIDFromString)                 \
  MACRO(ole32, CoCreateGuid)                    \
  MACRO(ole32, CoCreateInstance)                \
  MACRO(ole32, CoInitialize)                    \
  MACRO(ole32, CoInitializeEx)                  \
  MACRO(ole32, CoInitializeSecurity)            \
  MACRO(ole32, CoSetProxyBlanket)               \
  MACRO(ole32, CoTaskMemAlloc)                  \
  MACRO(ole32, CoTaskMemFree)                   \
  MACRO(ole32, CoTaskMemRealloc)                \
  MACRO(ole32, CoWaitForMultipleHandles)        \
  MACRO(ole32, CoUninitialize)                  \
  MACRO(ole32, OleDuplicateData)                \
  MACRO(ole32, OleFlushClipboard)               \
  MACRO(ole32, OleGetClipboard)                 \
  MACRO(ole32, OleInitialize)                   \
  MACRO(ole32, OleQueryLinkFromData)            \
  MACRO(ole32, OleSetClipboard)                 \
  MACRO(ole32, OleUninitialize)                 \
  MACRO(rpcrt4, UuidToStringA)                  \
  MACRO(setupapi, SetupDiEnumDeviceInfo)        \
  MACRO(setupapi, SetupDiGetDeviceRegistryPropertyW) \
  MACRO(setupapi, SetupDiGetClassDevsW)         \
  MACRO(setupapi, SetupDiDestroyDeviceInfoList) \
  MACRO(shcore, GetProcessDpiAwareness)         \
  MACRO(shlwapi, PathRemoveFileSpecA)           \
  MACRO(shlwapi, PathRemoveFileSpecW)           \
  MACRO(ucrtbase, __stdio_common_vfprintf)      \
  MACRO(ucrtbase, __stdio_common_vfprintf_p)    \
  MACRO(ucrtbase, __stdio_common_vfprintf_s)    \
  MACRO(ucrtbase, _beginthreadex)               \
  MACRO(ucrtbase, _fdopen)                      \
  MACRO(ucrtbase, _time64)                      \
  MACRO(ucrtbase, _tzset)                       \
  MACRO(ucrtbase, getenv)                       \
  MACRO(ucrtbase, fclose)                       \
  MACRO(ucrtbase, fopen)                        \
  MACRO(ucrtbase, setlocale)                    \
  MACRO(uxtheme, IsAppThemed)                   \
  MACRO(uxtheme, CloseThemeData)                \
  MACRO(uxtheme, DrawThemeBackground)           \
  MACRO(uxtheme, DrawThemeBackgroundEx)         \
  MACRO(uxtheme, GetThemeBackgroundContentRect) \
  MACRO(uxtheme, GetThemePartSize)              \
  MACRO(uxtheme, OpenThemeData)                 \
  MACRO(version, GetFileVersionInfoSizeW)       \
  MACRO(version, GetFileVersionInfoW)           \
  MACRO(version, VerQueryValueW)                \
  MACRO(ws2_32, __WSAFDIsSet)                   \
  MACRO(ws2_32, accept)                         \
  MACRO(ws2_32, bind)                           \
  MACRO(ws2_32, closesocket)                    \
  MACRO(ws2_32, connect)                        \
  MACRO(ws2_32, gethostname)                    \
  MACRO(ws2_32, getsockname)                    \
  MACRO(ws2_32, getsockopt)                     \
  MACRO(ws2_32, listen)                         \
  MACRO(ws2_32, ioctlsocket)                    \
  MACRO(ws2_32, recv)                           \
  MACRO(ws2_32, select)                         \
  MACRO(ws2_32, send)                           \
  MACRO(ws2_32, setsockopt)                     \
  MACRO(ws2_32, shutdown)                       \
  MACRO(ws2_32, socket)                         \
  MACRO(ws2_32, WSACleanup)                     \
  MACRO(ws2_32, WSAGetOverlappedResult)         \
  MACRO(ws2_32, WSAIoctl)                       \
  MACRO(ws2_32, WSARecv)                        \
  MACRO(ws2_32, WSARecvFrom)                    \
  MACRO(ws2_32, WSASend)                        \
  MACRO(ws2_32, WSASendTo)                      \
  MACRO(ws2_32, WSAStartup)                     \
  MACRO(ws2_32, WSAStringToAddressA)            \
  MACRO(ws2_32, WSCEnumProtocols)               \
  MACRO(ws2_32, WSCGetProviderInfo)             \
  MACRO(ws2_32, WSCGetProviderPath)

#define MAKE_CALL_EVENT(aName)  CallEvent_ ##aName ,
#define MAKE_DLL_CALL_EVENT(_, aName) MAKE_CALL_EVENT(aName)

enum CallEvent {                                        \
  FOR_EACH_KERNEL32_REDIRECTION(MAKE_CALL_EVENT)        \
  FOR_EACH_SHELL32_REDIRECTION(MAKE_CALL_EVENT)         \
  FOR_EACH_USER32_REDIRECTION(MAKE_CALL_EVENT)          \
  FOR_EACH_DLL_REDIRECTION(MAKE_DLL_CALL_EVENT)         \
  CallEvent_Count                                       \
};

#undef MAKE_CALL_EVENT
#undef MAKE_DLL_CALL_EVENT

template <typename T>
static void
RecordOrReplayHandle(AutoRecordReplayFunction<T>& aRrf)
{
  aRrf.mThread->mEvents.RecordOrReplayValue(&aRrf.mRval);
  if ((!aRrf.mRval || aRrf.mRval == INVALID_HANDLE_VALUE)) {
    aRrf.mThread->mEvents.RecordOrReplayValue(&aRrf.mError);
  }
}

#define RRFunctionHandle0(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName ()                                                 \
  {                                                              \
    RecordReplayFunction(aName, HANDLE);                         \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle1(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0)                                         \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0);                     \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle2(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1)                               \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1);                 \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle3(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2)                     \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2);             \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle4(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3)           \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3);         \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle6(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3,           \
               DWORD a4, DWORD a5)                               \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3, a4, a5); \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle7(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3,           \
               DWORD a4, DWORD a5, DWORD a6)                     \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3, a4, a5, a6); \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle8(aName)                                 \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3,           \
               DWORD a4, DWORD a5, DWORD a6, DWORD a7)           \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3, a4, a5, a6, a7); \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle11(aName)                                \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3,           \
               DWORD a4, DWORD a5, DWORD a6, DWORD a7,           \
               DWORD a8, DWORD a9, DWORD a10)                    \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10); \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define RRFunctionHandle12(aName)                                \
  static HANDLE __stdcall                                        \
  RR_ ##aName (DWORD a0, DWORD a1, DWORD a2, DWORD a3,           \
               DWORD a4, DWORD a5, DWORD a6, DWORD a7,           \
               DWORD a8, DWORD a9, DWORD a10, DWORD a11)         \
  {                                                              \
    RecordReplayFunction(aName, HANDLE, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11); \
    RecordOrReplayHandle(rrf);                                   \
    return rval;                                                 \
  }

#define InstantiateAW(aMacro)                   \
  aMacro(A)                                     \
  aMacro(W)

#define InstantiateStringsAW(aMacro)            \
  aMacro(A, LPSTR)                              \
  aMacro(W, LPWSTR)

///////////////////////////////////////////////////////////////////////////////
// Callbacks
///////////////////////////////////////////////////////////////////////////////

enum CallbackEvent {
  CallbackEvent_FONTENUMPROCW,
  CallbackEvent_WNDPROC
};

static int CALLBACK
FONTENUMPROCWWrapper(ENUMLOGFONTEXW* aElfe, NEWTEXTMETRICEXW* aNmetrics,
                     DWORD aFontType, LPARAM aData)
{
  RecordReplayCallback(FONTENUMPROCW, (void**)&aData);

  if (IsReplaying()) {
    aElfe = new ENUMLOGFONTEXW();
    aNmetrics = new NEWTEXTMETRICEXW();
  }
  RecordReplayBytes(aElfe, sizeof(ENUMLOGFONTEXW));
  RecordReplayBytes(aNmetrics, sizeof(NEWTEXTMETRICEXW));
  aFontType = RecordReplayValue(aFontType);

  int rv = rrc.mFunction((LOGFONTW*)aElfe, (TEXTMETRICW*)aNmetrics, aFontType, aData);

  if (IsReplaying()) {
    free(aElfe);
    free(aNmetrics);
  }
  return rv;
}

struct RegisteredClass {
  const wchar_t* mName;
  WNDPROC mRealProc;

  RegisteredClass(const wchar_t* aName, WNDPROC aRealProc)
    : mName(aName), mRealProc(aRealProc)
  {}
};
static StaticInfallibleVector<RegisteredClass> gRegisteredClasses;

static RegisteredClass*
GetRegisteredClass(LPCWSTR aName)
{
  for (RegisteredClass& cls : gRegisteredClasses) {
    if (!wcscmp(aName, cls.mName)) {
      return &cls;
    }
  }
  return nullptr;
}

static LRESULT CALLBACK
WNDPROCWrapper(HWND aHwnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam)
{
  MOZ_ASSERT(IsRecordingOrReplaying());
  WNDPROC function;
  if (IsRecording()) {
    WCHAR className[256];
    int rv = GetClassNameW(aHwnd, className, sizeof(className) / sizeof(className[0]));
    MOZ_RELEASE_ASSERT(rv);
    RegisteredClass* cls = GetRegisteredClass(className);
    MOZ_ASSERT(cls);
    function = cls->mRealProc;
    BeginCallback(CallbackEvent_WNDPROC);
  }

  SaveOrRestoreCallbackData((void**)&function);

  // nsAppShell::EventWindowProc looks for specific messages that were sent
  // from elsewhere in Gecko, and passes on other messages to DefWindowProc.
  SaveOrRestoreCallbackData((void**)&aLParam, /* aCanMiss = */ true);

  aHwnd = (HWND)RecordReplayValue((size_t)aHwnd);
  aMsg = RecordReplayValue(aMsg);
  aWParam = RecordReplayValue(aWParam);

  LRESULT rv = function(aHwnd, aMsg, aWParam, aLParam);

  if (IsRecording()) {
    EndCallback();
  }
  return rv;
}

static void
NoteRegisteredClass(WNDCLASSW* aClass)
{
  MOZ_ASSERT(Thread::CurrentIsMainThread());
  MOZ_ASSERT(!GetRegisteredClass(aClass->lpszClassName));

  RegisterCallbackData(aClass->lpfnWndProc);

  gRegisteredClasses.emplaceBack(aClass->lpszClassName, aClass->lpfnWndProc);
  aClass->lpfnWndProc = WNDPROCWrapper;
}

void
ReplayInvokeCallback(size_t aId)
{
  MOZ_ASSERT(IsReplaying());
  switch (aId) {
  case CallbackEvent_FONTENUMPROCW:
    FONTENUMPROCWWrapper(nullptr, nullptr, 0, 0);
    break;
  case CallbackEvent_WNDPROC:
    WNDPROCWrapper(nullptr, 0, 0, 0);
    break;
  default:
    MOZ_CRASH();
  }
}

///////////////////////////////////////////////////////////////////////////////
// advapi32 redirections
///////////////////////////////////////////////////////////////////////////////

static BOOL __stdcall
RR_AddAccessAllowedAce(PACL aAcl, DWORD aRev, DWORD aMask, PSID aId)
{
  RecordReplayFunction(AddAccessAllowedAce, BOOL, aAcl, aRev, aMask, aId);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aAcl, sizeof(*aAcl));
  return rval;
}

static BOOL __stdcall
RR_AdjustTokenPrivileges(HANDLE aHandle, BOOL aDisable,
                         PTOKEN_PRIVILEGES aNewState,
                         DWORD aBuflen,
                         PTOKEN_PRIVILEGES aPreviousState,
                         PDWORD aPreviouslen)
{
  RecordReplayFunction(AdjustTokenPrivileges, BOOL,
                       aHandle, aDisable, aNewState, aBuflen, aPreviousState, aPreviouslen);
  RecordOrReplayHadErrorZero(rrf);
  if (aPreviousState || aPreviouslen) {
    MOZ_CRASH();
  }
  return rval;
}

static BOOL __stdcall
RR_AllocateAndInitializeSid(PSID_IDENTIFIER_AUTHORITY aAuth,
                            BYTE aSubauthCount,
                            DWORD a0, DWORD a1, DWORD a2,
                            DWORD a3, DWORD a4, DWORD a5,
                            DWORD a6, DWORD a7,
                            PSID* aSid)
{
  RecordReplayFunction(AllocateAndInitializeSid, BOOL,
                       aAuth, aSubauthCount, a0, a1, a2, a3, a4, a5, a6, a7, aSid);
  RecordOrReplayHadErrorZero(rrf);
  if (IsRecording() && !*aSid) {
    MOZ_CRASH();
  }
  if (IsReplaying()) {
    *aSid = (PSID)0x1;
  }
  return rval;
}

static VOID __stdcall
RR_BuildExplicitAccessWithNameW(PEXPLICIT_ACCESS aAccess,
                                LPWSTR aName, DWORD aPerm,
                                ACCESS_MODE aMode, DWORD aInherit)
{
  RecordReplayFunctionVoid(BuildExplicitAccessWithNameW,
                           aAccess, aName, aPerm, aMode, aInherit);
  events.RecordOrReplayBytes(aAccess, sizeof(*aAccess));
}

static BOOL __stdcall
RR_ConvertSecurityDescriptorToStringSecurityDescriptorW
  (PSECURITY_DESCRIPTOR aDesc, DWORD aRev, SECURITY_INFORMATION aInfo,
   LPTSTR aNewDesc, PULONG aNewDesclen)
{
  RecordReplayFunction(ConvertSecurityDescriptorToStringSecurityDescriptorW, BOOL,
                       aDesc, aRev, aInfo, aNewDesc, aNewDesclen);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_ConvertSidToStringSidW(PSID aId, LPTSTR aStr)
{
  RecordReplayFunction(ConvertSidToStringSidW, BOOL, aId, aStr);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_ConvertStringSecurityDescriptorToSecurityDescriptorW
  (LPCWSTR aStr, DWORD aRev, PSECURITY_DESCRIPTOR* aDesc, PULONG aSize)
{
  RecordReplayFunction(ConvertStringSecurityDescriptorToSecurityDescriptorW, BOOL,
                       aStr, aRev, aDesc, aSize);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_ConvertStringSidToSidW(LPCWSTR aStr, PSID* aId)
{
  RecordReplayFunction(ConvertStringSidToSidW, BOOL, aStr, aId);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_CopySid(DWORD aDstlen, PSID aDst, PSID aSrc)
{
  RecordReplayFunction(CopySid, BOOL, aDstlen, aDst, aSrc);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aDst, aDstlen);
  return rval;
}

static BOOL __stdcall
RR_CryptAcquireContextW(HCRYPTPROV* aProv, LPCWSTR aContainer, LPCWSTR aProvider,
                        DWORD aProvtype, DWORD aFlags)
{
  RecordReplayFunction(CryptAcquireContextW, BOOL,
                       aProv, aContainer, aProvider, aProvtype, aFlags);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aProv);
  return rval;
}

static BOOL __stdcall
RR_CryptCreateHash(HCRYPTPROV aProv, ALG_ID aId, HCRYPTKEY aKey,
                   DWORD aFlags, HCRYPTHASH* aHash)
{
  RecordReplayFunction(CryptCreateHash, BOOL, aProv, aId, aKey, aFlags, aHash);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aHash);
  return rval;
}

static BOOL __stdcall
RR_CryptDecrypt(HCRYPTKEY aKey, HCRYPTHASH aHash,
                BOOL aFinal, DWORD aFlags, BYTE* aData, DWORD* aDatalen)
{
  RecordReplayFunction(CryptDecrypt, BOOL, aKey, aHash, aFinal, aFlags, aData, aDatalen);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aDatalen);
  if (aData) {
    events.RecordOrReplayBytes(aData, *aDatalen);
  }
  return rval;
}

RRFunctionZeroError1(CryptDestroyHash)
RRFunctionZeroError1(CryptDestroyKey)

static BOOL __stdcall
RR_CryptExportKey(HCRYPTKEY aKey, HCRYPTKEY aEKey,
                  DWORD aBlob, DWORD aFlags, BYTE* aData, DWORD* aDatalen)
{
  RecordReplayFunction(CryptExportKey, BOOL, aKey, aEKey, aBlob, aFlags, aData, aDatalen);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aDatalen);
  if (aData) {
    events.RecordOrReplayBytes(aData, *aDatalen);
  }
  return rval;
}

static BOOL __stdcall
RR_CryptGenRandom(HCRYPTPROV aProv, DWORD aLen, BYTE* aBuf)
{
  RecordReplayFunction(CryptGenRandom, BOOL, aProv, aLen, aBuf);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aBuf, aLen);
  return rval;
}

static BOOL __stdcall
RR_CryptGetDefaultProviderW(DWORD aType, DWORD* aReserved,
                            DWORD aFlags, LPWSTR aName, DWORD* aNameBytes)
{
  RecordReplayFunction(CryptGetDefaultProviderW, BOOL,
                       aType, aReserved, aFlags, aName, aNameBytes);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aNameBytes);
  if (aName) {
    events.RecordOrReplayBytes(aName, *aNameBytes);
  }
  return rval;
}

static BOOL __stdcall
RR_CryptGetHashParam(HCRYPTHASH aHash, DWORD aParam, BYTE* aData, DWORD* aDatalen, DWORD aFlags)
{
  RecordReplayFunction(CryptGetHashParam, BOOL,
                       aHash, aParam, aData, aDatalen, aFlags);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aDatalen);
  if (aData) {
    events.RecordOrReplayBytes(aData, *aDatalen);
  }
  return rval;
}

static BOOL __stdcall
RR_CryptGetUserKey(HCRYPTPROV aProv, DWORD aSpec, HCRYPTKEY* aKey)
{
  RecordReplayFunction(CryptGetUserKey, BOOL, aProv, aSpec, aKey);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aKey);
  return rval;
}

static BOOL __stdcall
RR_CryptHashData(HCRYPTHASH aHash, BYTE* aData, DWORD aDatalen, DWORD aFlags)
{
  RecordReplayFunction(CryptHashData, BOOL, aHash, aData, aDatalen, aFlags);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aData, aDatalen);
  return rval;
}

static BOOL __stdcall
RR_CryptImportKey(HCRYPTPROV aProv, BYTE* aData, DWORD aDatalen,
                  HCRYPTKEY aPubkey, DWORD aFlags, HCRYPTKEY* aKeyout)
{
  RecordReplayFunction(CryptImportKey, BOOL, aProv, aData, aDatalen, aPubkey, aFlags, aKeyout);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aKeyout);
  return rval;
}

RRFunctionZeroError2(CryptReleaseContext)
RRFunctionZeroError4(CryptSetHashParam)

static BOOL __stdcall
RR_CryptSignHashW(HCRYPTHASH aHash, DWORD aSpec, LPCWSTR aDesc,
                  DWORD aFlags, BYTE* aSig, DWORD* aSiglen)
{
  RecordReplayFunction(CryptSignHashW, BOOL, aHash, aSpec, aDesc, aFlags, aSig, aSiglen);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aSiglen);
  if (aSig) {
    events.RecordOrReplayBytes(aSig, *aSiglen);
  }
  return rval;
}

RRFunctionZeroError6(CryptVerifySignatureW)
RRFunctionZeroError1(GetLengthSid)

static DWORD __stdcall
RR_GetNamedSecurityInfoW(LPWSTR aName, SE_OBJECT_TYPE aType, SECURITY_INFORMATION aInfo,
                         PSID* aOwner, PSID* aGroup, PACL* aDacl, PACL* aSacl,
                         PSECURITY_DESCRIPTOR* aDesc)
{
  RecordReplayFunction(GetNamedSecurityInfoW, DWORD,
                       aName, aType, aInfo, aOwner, aGroup, aDacl, aSacl, aDesc);
  events.RecordOrReplayValue(&rval);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_GetTokenInformation(HANDLE aHandle, TOKEN_INFORMATION_CLASS aClass,
                       LPVOID aInfo, DWORD aInfolen, PDWORD aRetlen)
{
  RecordReplayFunction(GetTokenInformation, BOOL, aHandle, aClass, aInfo, aInfolen, aRetlen);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aRetlen);
  if (aInfo) {
    events.RecordOrReplayBytes(aInfo, *aRetlen);
  }
  return rval;
}

static BOOL __stdcall
RR_GetUserNameW(LPWSTR aBuf, LPDWORD aSize)
{
  DWORD sizeInit = *aSize;
  RecordReplayFunction(GetUserNameW, BOOL, aBuf, aSize);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aSize);
  if (aBuf) {
    events.RecordOrReplayBytes(aBuf, min(sizeInit, *aSize) * sizeof(*aBuf));
  }
  return rval;
}

static BOOL __stdcall
RR_InitializeAcl(PACL aAcl, DWORD aLen, DWORD aRev)
{
  RecordReplayFunction(InitializeAcl, BOOL, aAcl, aLen, aRev);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aAcl, aLen);
  return rval;
}

RRFunctionZeroError2(InitializeSecurityDescriptor)

static BOOL __stdcall
RR_LookupAccountNameW(LPCWSTR aSystemName, LPCWSTR aAccName, PSID aSid, LPDWORD aSidBytes,
                      LPWSTR aDomain, LPDWORD aDomainChars, PSID_NAME_USE aUse)
{
  RecordReplayFunction(LookupAccountNameW, BOOL,
                       aSystemName, aAccName, aSid, aSidBytes, aDomain, aDomainChars, aUse);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_LookupAccountSidW(LPCWSTR aSystemName, PSID aSid,
                     LPWSTR aName, LPDWORD aNameChars,
                     LPWSTR aDomain, LPDWORD aDomainChars,
                     PSID_NAME_USE aUse)
{
  RecordReplayFunction(LookupAccountSidW, BOOL,
                       aSystemName, aSid, aName, aNameChars, aDomain, aDomainChars, aUse);
  RecordOrReplayHadErrorZero(rrf);
  MOZ_CRASH();
  return rval;
}

static BOOL __stdcall
RR_OpenProcessToken(HANDLE aHandle, DWORD aAccess, PHANDLE aToken)
{
  RecordReplayFunction(OpenProcessToken, BOOL, aHandle, aAccess, aToken);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aToken);
  return rval;
}

static ULONG __stdcall
RR_RegisterTraceGuidsW(WMIDPREQUEST aAddress, PVOID aCx, LPCGUID aControl,
                       ULONG aGuidCount, PTRACE_GUID_REGISTRATION aGuids,
                       LPCWSTR a0, LPCWSTR a1, PTRACEHANDLE aReg)
{
  RecordReplayFunction(RegisterTraceGuidsW, ULONG,
                       aAddress, aCx, aControl, aGuidCount, aGuids, a0, a1, aReg);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aGuids, aGuidCount * sizeof(*aGuids));
  events.RecordOrReplayBytes(aReg, sizeof(*aReg)); // TRACEHANDLE is 64 bits.
  return rval;
}

static LONG __stdcall
RR_RegEnumValueW(HKEY aKey, DWORD aIndex, LPWSTR aValue, LPDWORD aValueChars,
                 LPDWORD aReserved, LPDWORD aType, LPBYTE aData, LPDWORD aDataBytes)
{
  DWORD valueCharsInit = *aValueChars;
  DWORD dataBytesInit = aDataBytes ? *aDataBytes : 0;
  RecordReplayFunction(RegEnumValueW, LONG,
                       aKey, aIndex, aValue, aValueChars, aReserved, aType, aData, aDataBytes);
  events.RecordOrReplayValue(&rval);
  events.CheckInput(valueCharsInit);
  events.CheckInput(dataBytesInit);
  events.RecordOrReplayValue(aValueChars);
  if (aValue) {
    size_t nelem = min(valueCharsInit, *aValueChars + 1);
    events.RecordOrReplayBytes(aValue, nelem * sizeof(*aValue));
  }
  if (aType) {
    events.RecordOrReplayValue(aType);
  }
  if (aDataBytes) {
    events.RecordOrReplayValue(aDataBytes);
    if (aData) {
      events.RecordOrReplayBytes(aData, min(dataBytesInit, *aDataBytes));
    }
  }
  return rval;
}

RRFunction1(RegCloseKey)

static LONG __stdcall
RR_RegOpenKeyExW(HKEY aKey, LPCTSTR aSubkey, DWORD aOptions, REGSAM aSam, PHKEY aResult)
{
  RecordReplayFunction(RegOpenKeyExW, LONG, aKey, aSubkey, aOptions, aSam, aResult);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aResult);
  return rval;
}

static LONG __stdcall
RR_RegQueryValueExW(HKEY aKey, LPCTSTR aName, LPDWORD aReserved,
                    LPDWORD aType, LPBYTE aData, LPDWORD aDataBytes)
{
  RecordReplayFunction(RegQueryValueExW, LONG, aKey, aName, aReserved, aType, aData, aDataBytes);
  events.RecordOrReplayValue(&rval);
  if (aType) {
    events.RecordOrReplayValue(aType);
  }
  if (aDataBytes) {
    events.RecordOrReplayValue(aDataBytes);
  }
  if (aData) {
    events.RecordOrReplayBytes(aData, *aDataBytes);
  }
  return rval;
}

static DWORD __stdcall
RR_SetEntriesInAclW(ULONG aCount, PEXPLICIT_ACCESS aList, PACL aOld, PACL* aNew)
{
  RecordReplayFunction(SetEntriesInAclW, DWORD, aCount, aList, aOld, aNew);
  events.RecordOrReplayValue(&rval);
  if (IsReplaying()) {
    *aNew = NewLeakyArray<ACL>(1);
  }
  events.RecordOrReplayBytes(*aNew, sizeof(**aNew));
  return rval;
}

RRFunctionZeroError4(SetSecurityDescriptorDacl)
RRFunctionZeroError3(SetSecurityDescriptorGroup)
RRFunctionZeroError3(SetSecurityDescriptorOwner)

// RtlGenRandom is #defined to SystemFunction036 for some reason.
static BOOLEAN __stdcall
RR_SystemFunction036(PVOID aBuf, ULONG aBufBytes)
{
  RecordReplayFunction(SystemFunction036, BOOLEAN, aBuf, aBufBytes);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aBuf, aBufBytes);
  return rval;
}

// TRACEHANDLE is a 64 bit quantity, so we can't use RRFunction here.
static ULONG __stdcall
RR_UnregisterTraceGuids(TRACEHANDLE aHandle)
{
  RecordReplayFunction(UnregisterTraceGuids, ULONG, aHandle);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(&rrf.mError);
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// gdi32 redirections
///////////////////////////////////////////////////////////////////////////////

RRFunction3(AddFontResourceExW)
RRFunctionHandle1(CreateCompatibleDC)

static HBITMAP __stdcall
RR_CreateDIBSection(HDC aHdc, const BITMAPINFO* aBmi, UINT aUsage,
                    VOID** aBits, HANDLE aSection, DWORD aOffset)
{
  RecordReplayFunction(CreateDIBSection, HBITMAP, aHdc, aBmi, aUsage, aBits, aSection, aOffset);
  RecordOrReplayHandle(rrf);
  if (IsReplaying() && aBits) {
    *aBits = (VOID*)0x1;
  }
  return rval;
}

RRFunctionHandle1(CreateFontIndirectW)
RRFunctionZeroError1(DeleteDC)
RRFunctionZeroError1(DeleteObject)

static int __stdcall
RR_EnumFontFamiliesExW(HDC aHdc, LPLOGFONT aFont, FONTENUMPROCW aProc, LPARAM aParam, DWORD aFlags)
{
  if (AreThreadEventsPassedThrough()) {
    return OriginalCall(EnumFontFamiliesExW, int, aHdc, aFont, aProc, aParam, aFlags);
  }

  RegisterCallbackData(aProc);
  RegisterCallbackData((void*)aParam);
  int rv = 0;
  if (IsRecording()) {
    AutoPassThroughThreadEventsAllowCallbacks pt;
    CallbackWrapperData data(aProc, (void*)aParam);
    rv = OriginalCall(EnumFontFamiliesExW, int,
                      aHdc, aFont, FONTENUMPROCWWrapper, &data, aFlags);
  } else {
    ReplayCallbacks();
  }
  RemoveCallbackData((void*)aParam);
  return RecordReplayValue(rv);
}

RRFunctionZeroError0(GdiFlush)

static BOOL __stdcall
RR_GetCharWidthI(HDC aHdc, UINT aFirst, UINT aCount, LPWORD aGi, LPINT aBuffer)
{
  RecordReplayFunction(GetCharWidthI, BOOL, aHdc, aFirst, aCount, aGi, aBuffer);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aBuffer, aCount * sizeof(*aBuffer));
  return rval;
}

static int __stdcall
RR_GetClipBox(HDC aHdc, LPRECT aRect)
{
  RecordReplayFunction(GetClipBox, int, aHdc, aRect);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aRect, sizeof(*aRect));
  return rval;
}

RRFunctionZeroError2(GetClipRgn)
RRFunctionZeroError2(GetDeviceCaps)

static DWORD __stdcall
RR_GetFontData(HDC aHdc, DWORD aTable, DWORD aOffset, LPVOID aBuffer, DWORD aBufferBytes)
{
  RecordReplayFunction(GetFontData, DWORD, aHdc, aTable, aOffset, aBuffer, aBufferBytes);
  events.RecordOrReplayValue(&rval);
  if (aBuffer) {
    events.RecordOrReplayBytes(aBuffer, aBufferBytes);
  }
  return rval;
}

static DWORD __stdcall
RR_GetGlyphIndicesW(HDC aHdc, LPCWSTR aStr, int aBufferCount, LPWORD aBuffer, DWORD a0)
{
  RecordReplayFunction(GetGlyphIndicesW, DWORD, aHdc, aStr, aBufferCount, aBuffer, a0);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aBuffer, aBufferCount * sizeof(aBuffer[0]));
  return rval;
}

static DWORD __stdcall
RR_GetGlyphOutlineW(HDC aHdc, UINT aUchar, UINT aFormat, LPGLYPHMETRICS aMetrics,
                    DWORD aBufferBytes, LPVOID aBuffer, const MAT2* aMatrix)
{
  RecordReplayFunction(GetGlyphOutlineW, DWORD,
                       aHdc, aUchar, aFormat, aMetrics, aBufferBytes, aBuffer, aMatrix);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aMetrics, sizeof(*aMetrics));
  if (aBuffer) {
    events.RecordOrReplayBytes(aBuffer, aBufferBytes);
  }
  return rval;
}

RRFunctionZeroError1(GetGraphicsMode)

static BOOL __stdcall
RR_GetICMProfileW(HDC aHdc, LPDWORD aFilenameChars, LPTSTR aFilename)
{
  DWORD filenameCharsInit = *aFilenameChars;
  RecordReplayFunction(GetICMProfileW, BOOL, aHdc, aFilenameChars, aFilename);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aFilenameChars);
  if (aFilename) {
    size_t nchars = min(filenameCharsInit, *aFilenameChars);
    events.RecordOrReplayBytes(aFilename, nchars * sizeof(aFilename[0]));
  }
  return rval;
}

static int __stdcall
RR_GetObjectW(HGDIOBJ aObj, int aBufferBytes, LPVOID aBuffer)
{
  RecordReplayFunction(GetObjectW, int, aObj, aBufferBytes, aBuffer);
  RecordOrReplayHadErrorZero(rrf);
  // Only LOGFONTs should be fetched using this API by firefox. Not all other types
  // of objects will be recorded/replayed accurately by simply copying the buffer's bytes.
  if (aBufferBytes != sizeof(LOGFONTW)) {
    MOZ_CRASH();
  }
  if (!aBuffer) {
    MOZ_CRASH();
  }
  events.RecordOrReplayBytes(aBuffer, aBufferBytes);
  return rval;
}

static UINT __stdcall
RR_GetOutlineTextMetricsW(HDC aHdc, UINT aMetricsBytes, LPOUTLINETEXTMETRICW aMetrics)
{
  RecordReplayFunction(GetOutlineTextMetricsW, UINT, aHdc, aMetricsBytes, aMetrics);
  RecordOrReplayHadErrorZero(rrf);
  if (aMetrics) {
    events.RecordOrReplayBytes(aMetrics, aMetricsBytes);
  }
  return rval;
}

static BOOL __stdcall
RR_GetTextExtentPoint32W(HDC aHdc, LPCWSTR aStr, int a0, LPSIZE aSize)
{
  RecordReplayFunction(GetTextExtentPoint32W, BOOL, aHdc, aStr, a0, aSize);
  RecordOrReplayHadErrorZero(rrf);
  if (a0 != 1) {
    MOZ_CRASH();
  }
  events.RecordOrReplayBytes(aSize, sizeof(*aSize));
  return rval;
}

static BOOL __stdcall
RR_GetTextMetricsA(HDC aHdc, LPTEXTMETRICA aMetrics)
{
  RecordReplayFunction(GetTextMetricsA, BOOL, aHdc, aMetrics);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aMetrics, sizeof(*aMetrics));
  return rval;
}

static BOOL __stdcall
RR_GetTextMetricsW(HDC aHdc, LPTEXTMETRICW aMetrics)
{
  RecordReplayFunction(GetTextMetricsW, BOOL, aHdc, aMetrics);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aMetrics, sizeof(*aMetrics));
  return rval;
}

static BOOL __stdcall
RR_GetWorldTransform(HDC aHdc, LPXFORM aXform)
{
  RecordReplayFunction(GetWorldTransform, BOOL, aHdc, aXform);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aXform, sizeof(*aXform));
  return rval;
}

RRFunctionZeroError5(IntersectClipRect)
RRFunctionZeroError3(ModifyWorldTransform)
RRFunctionZeroError2(RestoreDC)
RRFunctionZeroError1(SaveDC)
RRFunctionZeroError2(SelectClipRgn)
RRFunctionHandle2(SelectObject)
RRFunctionZeroError2(SetGraphicsMode)
RRFunctionZeroError2(SetMapMode)
RRFunctionZeroError2(SetWorldTransform)

///////////////////////////////////////////////////////////////////////////////
// iphlpapi redirections
///////////////////////////////////////////////////////////////////////////////

static ULONG __stdcall
RR_GetAdaptersInfo(PIP_ADAPTER_INFO aInfo, PULONG aSize)
{
  RecordReplayFunction(GetAdaptersInfo, ULONG, aInfo, aSize);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aSize);
  if (aInfo) {
    events.RecordOrReplayBytes(aInfo, *aSize);
    if (aInfo->Next) {
      aInfo->Next = (PIP_ADAPTER_INFO) 0x1; // Poison to catch accesses.
    }
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// Semaphore tracking
///////////////////////////////////////////////////////////////////////////////

// Keep track of which handles are semaphores that were created when events
// were not passed through. NSPR uses per-thread semaphores for blocking when
// the thread is waiting on a condition variable, so we need to emulate the
// semaphore's behavior even if it is used at a time when events are passed
// through or are disallowed. We need to keep track of which handles refer to
// such semaphores because semaphores can be used in generic APIs like
// CloseHandle and WaitForSingleObject.
static StaticInfallibleVector<HANDLE> gSemaphores;

static void
AddSemaphore(HANDLE aSemaphore)
{
  PR_Lock(gGlobalLock);
  gSemaphores.emplaceBack(aSemaphore);
  PR_Unlock(gGlobalLock);
}

static void
RemoveSemaphore(HANDLE aSemaphore)
{
  PR_Lock(gGlobalLock);
  for (HANDLE& existing : gSemaphores) {
    if (existing == aSemaphore) {
      gSemaphores.erase(&existing);
      PR_Unlock(gGlobalLock);
      return;
    }
  }
  MOZ_CRASH();
}

static bool
IsSemaphore(HANDLE aHandle)
{
  // During replay it is possible for a live semaphore handle to have the same
  // value as an old handle value from the recording, so we have to determine
  // which is which while we are still recording.
  MOZ_ASSERT(IsRecording());

  PR_Lock(gGlobalLock);
  for (HANDLE existing : gSemaphores) {
    if (existing == aHandle) {
      PR_Unlock(gGlobalLock);
      return true;
    }
  }
  PR_Unlock(gGlobalLock);
  return false;
}

static bool
RecordOrReplayIsSemaphore(Stream& aStream, HANDLE aObject)
{
  bool isSemaphore = IsRecording() ? IsSemaphore(aObject) : false;
  aStream.RecordOrReplayValue(&isSemaphore);
  return isSemaphore;
}

///////////////////////////////////////////////////////////////////////////////
// kernel32 redirections
///////////////////////////////////////////////////////////////////////////////

RRFunctionZeroError1(CancelIo)

static BOOL __stdcall
RR_CloseHandle(HANDLE aObject)
{
  MOZ_ASSERT_IF(IsRecording() && AreThreadEventsPassedThrough(), !IsSemaphore(aObject));

  RecordReplayFunction(CloseHandle, BOOL, aObject);
  RecordOrReplayHadErrorZero(rrf);
  if (rval && RecordOrReplayIsSemaphore(events, aObject)) {
    if (IsReplaying()) {
      OriginalCall(CloseHandle, BOOL, aObject);
    }
    RemoveSemaphore(aObject);
  }
  return rval;
}

// Hack to allow accurately restoration of OVERLAPPED pointers and I/O
// completion port keys. The Windows API leaves it up to the user of the
// API to manage when and where the overlapped structures are destroyed, and
// we can't use Register/RestoreCallbackData without knowing when the work
// items are gone.
struct OverlappedThing {
  void* mThing;
  void* mBuffer;
  size_t mSize;

  OverlappedThing(void* aThing, void* aBuffer, size_t aSize)
    : mThing(aThing), mBuffer(aBuffer), mSize(aSize)
  {}
};
static StaticInfallibleVector<OverlappedThing> gOverlappedThings;
static StaticMutexNotRecorded gOverlappedThingLock;

static void
RegisterOverlappedThing(void* aThing, void* aBuffer, size_t aSize)
{
  if (!aThing) {
    return;
  }

  AutoOrderedAtomicAccess aa;
  StaticMutexAutoLock lock(gOverlappedThingLock);
  gOverlappedThings.emplaceBack(aThing, aBuffer, aSize);
}

static void
RestoreOverlappedThing(Stream& aStream, void** aThing)
{
  AutoOrderedAtomicAccess();

  size_t index = 0;
  if (IsRecording()) {
    if (*aThing) {
      StaticMutexAutoLock lock(gOverlappedThingLock);
      for (int i = gOverlappedThings.length() - 1; i >= 0; i--) {
        if (*aThing == gOverlappedThings[i].mThing) {
          index = i + 1;
          break;
        }
      }
      MOZ_RELEASE_ASSERT(index);
    }
  }
  aStream.RecordOrReplayValue(&index);
  if (IsRecordingOrReplaying()) {
    if (index) {
      StaticMutexAutoLock lock(gOverlappedThingLock);
      MOZ_ASSERT(index <= gOverlappedThings.length());
      if (IsReplaying()) {
        *aThing = gOverlappedThings[index - 1].mThing;
      }
      void* buffer = gOverlappedThings[index - 1].mBuffer;
      size_t size = gOverlappedThings[index - 1].mSize;
      if (buffer) {
        aStream.RecordOrReplayBytes(buffer, size);
      }
    } else {
      *aThing = nullptr;
    }
  }
}

static BOOL __stdcall
RR_ConnectNamedPipe(HANDLE aPipe, LPOVERLAPPED aOverlapped)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing(aOverlapped, nullptr, 0);
  }
  RecordReplayFunction(ConnectNamedPipe, BOOL, aPipe, aOverlapped);
  RecordOrReplayHadErrorZero(rrf);
  return rval;
}

RRFunctionZeroError3(CopyFileA)
RRFunctionZeroError3(CopyFileW)

#define RecordReplayCopyFileFunction(aName, aType)                      \
  static BOOL __stdcall                                                 \
  RR_ ##aName                                                           \
    (aType aExistingName, aType aNewName, LPPROGRESS_ROUTINE aProgress, LPVOID aData, \
     LPBOOL aCancel, DWORD aFlags)                                      \
  {                                                                     \
    RecordReplayFunction(aName, BOOL,                                   \
                         aExistingName, aNewName, aProgress, aData, aCancel, aFlags); \
    if (aProgress || aCancel) {                                         \
      MOZ_CRASH();                                                      \
    }                                                                   \
    RecordOrReplayHadErrorZero(rrf);                                    \
    return rval;                                                        \
  }

RecordReplayCopyFileFunction(CopyFileExA, LPCSTR)
RecordReplayCopyFileFunction(CopyFileExW, LPCWSTR)
RRFunctionZeroError2(CreateDirectoryA)
RRFunctionZeroError2(CreateDirectoryW)
RRFunctionZeroError3(CreateDirectoryExA)
RRFunctionZeroError3(CreateDirectoryExW)
RRFunctionHandle4(CreateEventA)
RRFunctionHandle4(CreateEventW)
RRFunctionHandle7(CreateFileA)
RRFunctionHandle7(CreateFileW)
RRFunctionHandle6(CreateFileMappingA)
RRFunctionHandle6(CreateFileMappingW)
RRFunctionZeroError3(CreateHardLinkA)
RRFunctionZeroError3(CreateHardLinkW)

#define RecordReplayCreateSemaphoreFunction(aSuffix)                    \
  static HANDLE __stdcall                                               \
  RR_CreateSemaphore ## aSuffix                                         \
    (LPSECURITY_ATTRIBUTES aSecurity, LONG aInitialCount, LONG aMaxCount, void* aName) \
  {                                                                     \
    HANDLE rval = OriginalCall(CreateSemaphore ## aSuffix, HANDLE,      \
                               aSecurity, aInitialCount, aMaxCount, aName); \
    if (!AreThreadEventsPassedThrough()) {                              \
      AddSemaphore(rval);                                               \
    }                                                                   \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayCreateSemaphoreFunction)

static HANDLE __stdcall
RR_CreateIoCompletionPort(HANDLE aHandle, HANDLE aExisting, ULONG_PTR aKey, DWORD aThreads)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing((void*)aKey, nullptr, 0);
  }
  RecordReplayFunction(CreateIoCompletionPort, HANDLE, aHandle, aExisting, aKey, aThreads);
  RecordOrReplayHandle(rrf);
  return rval;
}

RRFunctionHandle2(CreateJobObjectA)
RRFunctionHandle2(CreateJobObjectW)
RRFunctionHandle8(CreateNamedPipeA)
RRFunctionHandle8(CreateNamedPipeW)

static BOOL __stdcall
RR_CreatePipe(PHANDLE aReadPipe, PHANDLE aWritePipe,
              LPSECURITY_ATTRIBUTES aAttributes, DWORD aSize)
{
  RecordReplayFunction(CreatePipe, BOOL, aReadPipe, aWritePipe, aAttributes, aSize);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aReadPipe);
  events.RecordOrReplayValue(aWritePipe);
  return rval;
}

static DWORD __stdcall
ThreadMain(void* aArgument)
{
  Thread::ThreadMain(aArgument);
  return 0;
}

static HANDLE __stdcall
RR_CreateThread(LPSECURITY_ATTRIBUTES aAttributes, SIZE_T aStackSize,
                LPTHREAD_START_ROUTINE aStartFunction, LPVOID aStartArg,
                DWORD aFlags, LPDWORD aId)
{
  if (IsRecording() && AreThreadEventsPassedThrough()) {
    return OriginalCall(CreateThread, HANDLE, aAttributes, aStackSize,
                        aStartFunction, aStartArg, aFlags, aId);
  }

  size_t id = NewThreadId();

  if (IsReplaying() && !AreThreadEventsPassedThrough()) {
    HANDLE handle;
    size_t nativeId;
    ReplayStartThread(id, aStartFunction, aStartArg, &handle, &nativeId);
    if (aId) {
      *aId = nativeId;
    }
    return handle;
  }

  Thread* thread = IsRecording() ? Thread::OpenById(id) : Thread::GetById(id);
  thread->mStart = aStartFunction;
  thread->mStartArg = aStartArg;

  thread->mNativeHandle = OriginalCall(CreateThread, HANDLE, aAttributes, aStackSize,
                                       ThreadMain, thread, aFlags, &thread->mNativeId);
  if (aId) {
    *aId = thread->mNativeId;
  }
  return thread->mNativeHandle;
}

static BOOL __stdcall
RR_CreateTimerQueueTimer(PHANDLE aNewTimer, HANDLE aQueue,
                         WAITORTIMERCALLBACK aCallback, PVOID aData,
                         DWORD aTime, DWORD aPeriod, ULONG aFlags)
{
  MOZ_CRASH();
  return false;
}

RRFunctionHandle3(CreateWaitableTimerA)
RRFunctionHandle3(CreateWaitableTimerW)

static void __stdcall
RR_DeleteCriticalSection(LPCRITICAL_SECTION aSection)
{
  DestroyLock(aSection);
  OriginalCall(DeleteCriticalSection, void, aSection);
}

RRFunctionZeroError1(DeleteFileA)
RRFunctionZeroError1(DeleteFileW)
RRFunctionZeroError3(DeleteTimerQueueTimer)

static BOOL __stdcall
RR_DeviceIoControl(HANDLE aDevice, DWORD aControlCode,
                   LPVOID aInBuffer, DWORD aInBufferSize,
                   LPVOID aOutBuffer, DWORD aOutBufferSize,
                   LPDWORD aBytesReturned, LPOVERLAPPED aOverlapped)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing(aOverlapped, nullptr, 0);
  }
  RecordReplayFunction(DeviceIoControl, BOOL,
                       aDevice, aControlCode,
                       aInBuffer, aInBufferSize, aOutBuffer, aOutBufferSize,
                       aBytesReturned, aOverlapped);
  RecordOrReplayHadErrorZero(rrf);
  if (aBytesReturned) {
    events.RecordOrReplayValue(aBytesReturned);
  }
  size_t outBytes = aBytesReturned ? *aBytesReturned : aOutBufferSize;
  events.RecordOrReplayBytes(aOutBuffer, outBytes);
  return rval;
}

static BOOL __stdcall
RR_DuplicateHandle(HANDLE aSourceProcess, HANDLE aSource, HANDLE aTargetProcess,
                   LPHANDLE aTarget, DWORD aAccess, BOOL aInherit, DWORD aOptions)
{
  RecordReplayFunction(DuplicateHandle, BOOL,
                       aSourceProcess, aSource, aTargetProcess, aTarget, aAccess,
                       aInherit, aOptions);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aTarget);
  return rval;
}

static void __stdcall
RR_EnterCriticalSection(LPCRITICAL_SECTION aSection)
{
  Lock* lock = FindLock(aSection);
  if (lock) {
    BeginLock(lock);
  }
  if (IsReplaying() && lock && !AreThreadEventsPassedThrough()) {
    ReplayLock(lock);
  } else {
    OriginalCall(EnterCriticalSection, void, aSection);
    if (lock) {
      RecordLock(lock);
    }
  }
}

#define RecordReplayExpandEnvironmentStringsFunction(aSuffix, aDataType) \
  static DWORD __stdcall                                                \
  RR_ExpandEnvironmentStrings ## aSuffix                                \
    (void* aSrc, aDataType aDst, DWORD aDstChars)                       \
  {                                                                     \
    RecordReplayFunction(ExpandEnvironmentStrings ## aSuffix, DWORD,    \
                         aSrc, aDst, aDstChars);                        \
    RecordOrReplayHadErrorZero(rrf);                                    \
    if (aDst) {                                                         \
      events.RecordOrReplayBytes(aDst, min(aDstChars, rval) * sizeof(aDst[0])); \
    }                                                                   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayExpandEnvironmentStringsFunction)

RRFunctionZeroError1(FindClose)

#define RecordReplayFindFirstFileFunction(aSuffix, aDataType)           \
  static HANDLE __stdcall                                               \
  RR_FindFirstFile ## aSuffix (void* aName, aDataType aData)            \
  {                                                                     \
    RecordReplayFunction(FindFirstFile ## aSuffix, HANDLE, aName, aData); \
    RecordOrReplayHandle(rrf);                                          \
    events.RecordOrReplayBytes(aData, sizeof(*aData));                  \
    return rval;                                                        \
  }
RecordReplayFindFirstFileFunction(A, LPWIN32_FIND_DATAA)
RecordReplayFindFirstFileFunction(W, LPWIN32_FIND_DATAW)

#define RecordReplayFindFirstFileExFunction(aSuffix, aDataType)         \
  static HANDLE __stdcall                                               \
  RR_FindFirstFileEx ## aSuffix                                         \
    (void* aName, FINDEX_INFO_LEVELS aLevel, aDataType aData,           \
     FINDEX_SEARCH_OPS aSearchOp, LPVOID aFilter, DWORD aFlags)         \
  {                                                                     \
    RecordReplayFunction(FindFirstFileEx ## aSuffix, HANDLE,            \
                         aName, aLevel, aData, aSearchOp, aFilter, aFlags); \
    RecordOrReplayHandle(rrf);                                          \
    events.RecordOrReplayBytes(aData, sizeof(*aData));                  \
    return rval;                                                        \
  }
RecordReplayFindFirstFileExFunction(A, LPWIN32_FIND_DATAA)
RecordReplayFindFirstFileExFunction(W, LPWIN32_FIND_DATAW)

#define RecordReplayFindNextFileFunction(aSuffix, aDataType)            \
  static BOOL __stdcall                                                 \
  RR_FindNextFile ## aSuffix (HANDLE aFindFile, aDataType aData)        \
  {                                                                     \
    RecordReplayFunction(FindNextFile ## aSuffix, BOOL, aFindFile, aData); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, sizeof(*aData));                  \
    return rval;                                                        \
  }
RecordReplayFindNextFileFunction(A, LPWIN32_FIND_DATAA)
RecordReplayFindNextFileFunction(W, LPWIN32_FIND_DATAW)

#define RecordReplayComputerNameFunction(aSuffix, aDataType)            \
  static BOOL __stdcall                                                 \
  RR_GetComputerName ## aSuffix (aDataType aData, LPDWORD aDataChars)   \
  {                                                                     \
    DWORD dataCharsInit = *aDataChars;                                  \
    RecordReplayFunction(GetComputerName ## aSuffix, BOOL, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.CheckInput(dataCharsInit);                                   \
    events.RecordOrReplayValue(aDataChars);                             \
    size_t nelem = min(dataCharsInit, *aDataChars + 1);                 \
    events.RecordOrReplayBytes(aData, nelem * sizeof(aData[0]));        \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayComputerNameFunction)

#define RecordReplayComputerNameExFunction(aSuffix, aDataType)          \
  static BOOL __stdcall                                                 \
  RR_GetComputerNameEx ## aSuffix                                       \
    (COMPUTER_NAME_FORMAT aFormat, aDataType aData, LPDWORD aDataChars) \
  {                                                                     \
    DWORD dataCharsInit = *aDataChars;                                  \
    RecordReplayFunction(GetComputerNameEx ## aSuffix, BOOL, aFormat, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.CheckInput(dataCharsInit);                                   \
    events.RecordOrReplayValue(aDataChars);                             \
    size_t nelem = min(dataCharsInit, *aDataChars + 1);                 \
    events.RecordOrReplayBytes(aData, nelem * sizeof(aData[0]));        \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayComputerNameExFunction)

RRFunctionHandle0(GetCurrentProcess)
RRFunctionZeroError0(GetCurrentProcessId)

#define RecordReplayDateFormatFunction(aSuffix, aDataType)              \
  static int __stdcall                                                  \
  RR_GetDateFormat ## aSuffix                                           \
    (LCID aLocale, DWORD aFlags, CONST SYSTEMTIME* aDate, void* aFormat, \
     aDataType aData, LPDWORD aDataChars)                               \
  {                                                                     \
    DWORD dataCharsInit = *aDataChars;                                  \
    RecordReplayFunction(GetDateFormat ## aSuffix, int,                 \
                         aLocale, aFlags, aDate, aFormat, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    if (aData) {                                                        \
      size_t nelem = min(dataCharsInit, *aDataChars);                   \
      events.RecordOrReplayBytes(aData, nelem * sizeof(aData[0]));      \
    }                                                                   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayDateFormatFunction)

#define RecordReplayDiskFreeSpaceFunction(aSuffix)                      \
  static BOOL __stdcall                                                 \
  RR_GetDiskFreeSpace ## aSuffix                                        \
    (void* aRoot, LPDWORD a0, LPDWORD a1, LPDWORD a2, LPDWORD a3)       \
  {                                                                     \
    RecordReplayFunction(GetDiskFreeSpace ## aSuffix, BOOL, aRoot, a0, a1, a2, a3); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayValue(a0);                                     \
    events.RecordOrReplayValue(a1);                                     \
    events.RecordOrReplayValue(a2);                                     \
    events.RecordOrReplayValue(a3);                                     \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayDiskFreeSpaceFunction)

#define RecordReplayDiskFreeSpaceExFunction(aSuffix)                    \
  static BOOL __stdcall                                                 \
  RR_GetDiskFreeSpaceEx ## aSuffix                                      \
    (void* aRoot, PULARGE_INTEGER a0, PULARGE_INTEGER a1, PULARGE_INTEGER a2) \
  {                                                                     \
    RecordReplayFunction(GetDiskFreeSpaceEx ## aSuffix, BOOL, aRoot, a0, a1, a2); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(a0, sizeof(*a0));                        \
    events.RecordOrReplayBytes(a1, sizeof(*a1));                        \
    events.RecordOrReplayBytes(a2, sizeof(*a2));                        \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayDiskFreeSpaceExFunction)

static LPWCH __stdcall
RR_GetEnvironmentStringsW()
{
  MOZ_ASSERT(AreThreadEventsPassedThrough());
  return OriginalCall(GetEnvironmentStringsW, LPWCH);
}

#define RecordReplayEnvironmentVariableFunction(aSuffix, aDataType)     \
  static DWORD __stdcall                                                \
  RR_GetEnvironmentVariable ## aSuffix (void* aName, aDataType aData, DWORD aDataChars) \
  {                                                                     \
    RecordReplayFunction(GetEnvironmentVariable ## aSuffix, DWORD, aName, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayEnvironmentVariableFunction)

RRFunctionNegError1(GetFileAttributesA)
RRFunctionNegError1(GetFileAttributesW)

#define RecordReplayFileAttributesExFunction(aSuffix)                   \
  static BOOL __stdcall                                                 \
  RR_GetFileAttributesEx ## aSuffix (void* aName, GET_FILEEX_INFO_LEVELS aLevel, LPVOID aInfo) \
  {                                                                     \
    RecordReplayFunction(GetFileAttributesEx ## aSuffix, BOOL, aName, aLevel, aInfo); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aInfo, sizeof(WIN32_FILE_ATTRIBUTE_DATA)); \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayFileAttributesExFunction)

static BOOL __stdcall
RR_GetFileInformationByHandle(HANDLE aFile, LPBY_HANDLE_FILE_INFORMATION aData)
{
  RecordReplayFunction(GetFileInformationByHandle, BOOL, aFile, aData);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aData, sizeof(*aData));
  return rval;
}

static DWORD __stdcall
RR_GetFileSize(HANDLE aFile, LPDWORD aSize)
{
  RecordReplayFunction(GetFileSize, DWORD, aFile, aSize);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aSize);
  return rval;
}

static BOOL __stdcall
RR_GetFileSizeEx(HANDLE aFile, PLARGE_INTEGER aSize)
{
  RecordReplayFunction(GetFileSizeEx, BOOL, aFile, aSize);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aSize, sizeof(*aSize));
  return rval;
}

static BOOL __stdcall
RR_GetFileTime(HANDLE aFile, LPFILETIME a0, LPFILETIME a1, LPFILETIME a2)
{
  RecordReplayFunction(GetFileTime, BOOL, aFile, a0, a1, a2);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(a0, sizeof(*a0));
  events.RecordOrReplayBytes(a1, sizeof(*a1));
  events.RecordOrReplayBytes(a2, sizeof(*a2));
  return rval;
}

#define RecordReplayGetFullPathNameFunction(aSuffix, aDataType)         \
  static DWORD __stdcall                                                \
  RR_GetFullPathName ## aSuffix                                         \
    (aDataType aName, DWORD aSize, aDataType aBuffer, aDataType* aFilePart) \
  {                                                                     \
    RecordReplayFunction(GetFullPathName ## aSuffix, DWORD, aName, aSize, aBuffer, aFilePart); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aBuffer, min(rval + 1, aSize) * sizeof(aBuffer[0])); \
    if (aFilePart) {                                                    \
      size_t offset = IsRecording() ? (*aFilePart - aBuffer) : 0;       \
      events.RecordOrReplayValue(&offset);                              \
      if (IsReplaying()) {                                              \
        *aFilePart = (offset < aSize) ? &aBuffer[offset] : nullptr;     \
      }                                                                 \
    }                                                                   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayGetFullPathNameFunction)

#define RecordReplayGeoInfoFunction(aSuffix, aDataType)                 \
  static BOOL __stdcall                                                 \
  RR_GetGeoInfo ## aSuffix                                              \
    (GEOID aLocation, GEOTYPE aType, aDataType aData, int aDataChars, LANGID aLang) \
  {                                                                     \
    RecordReplayFunction(GetGeoInfo ## aSuffix, BOOL,                   \
                         aLocation, aType, aData, aDataChars, aLang);   \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayGeoInfoFunction)

static BOOL __stdcall
RR_GetHandleInformation(HANDLE aHandle, LPDWORD aFlags)
{
  RecordReplayFunction(GetHandleInformation, BOOL, aHandle, aFlags);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aFlags);
  return rval;
}

#define RecordReplayLocaleInfoFunction(aSuffix, aDataType)              \
  static int __stdcall                                                  \
  RR_GetLocaleInfo ## aSuffix                                           \
    (LCID aLocale, LCTYPE aType, aDataType aData, int aDataChars)       \
  {                                                                     \
    RecordReplayFunction(GetLocaleInfo ## aSuffix, int, aLocale, aType, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayLocaleInfoFunction)

static VOID __stdcall
RR_GetLocalTime(LPSYSTEMTIME aTime)
{
  RecordReplayFunctionVoid(GetLocalTime, aTime);
  events.RecordOrReplayBytes(aTime, sizeof(*aTime));
}

static BOOL __stdcall
RR_GetLogicalProcessorInformation(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION aBuf,
                                  PDWORD aBufBytes)
{
  RecordReplayFunction(GetLogicalProcessorInformation, BOOL, aBuf, aBufBytes);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aBufBytes);
  if (rval) {
    events.RecordOrReplayBytes(aBuf, *aBufBytes);
  }
  return rval;
}

#define RecordReplayLongPathNameFunction(aSuffix, aDataType)            \
  static DWORD __stdcall                                                \
  RR_GetLongPathName ## aSuffix                                         \
    (void* aShortPath, aDataType aData, DWORD aDataChars)               \
  {                                                                     \
    RecordReplayFunction(GetLongPathName ## aSuffix, BOOL, aShortPath, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayLongPathNameFunction)

#define RecordReplayModuleFileNameFunction(aSuffix, aDataType)          \
  static DWORD __stdcall                                                \
  RR_GetModuleFileName ## aSuffix                                       \
    (HMODULE aModule, aDataType aData, DWORD aDataChars)                \
  {                                                                     \
    RecordReplayFunction(GetModuleFileName ## aSuffix, DWORD, aModule, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayModuleFileNameFunction)

RRFunctionHandle1(GetModuleHandleA)
RRFunctionHandle1(GetModuleHandleW)

#define RecordReplayModuleHandleExFunction(aSuffix)                     \
  static BOOL __stdcall                                                 \
  RR_GetModuleHandleEx ## aSuffix                                       \
      (DWORD aFlags, void* aName, HMODULE* aModule)                     \
  {                                                                     \
    RecordReplayFunction(GetModuleHandleEx ## aSuffix, BOOL, aFlags, aName, aModule); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayValue(aModule);                                \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayModuleHandleExFunction)

#define RecordReplayNumberFormatFunction(aSuffix, aDataType)            \
  static int __stdcall                                                  \
  RR_GetNumberFormat ## aSuffix                                         \
      (LCID aLocale, DWORD aFlags, void* aValue, void* aFormat,         \
       aDataType aData, int aDataChars)                                 \
  {                                                                     \
    RecordReplayFunction(GetNumberFormat ## aSuffix, int,               \
                         aLocale, aFlags, aValue, aFormat, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayNumberFormatFunction)

static BOOL __stdcall
RR_GetOverlappedResult(HANDLE aFile, LPOVERLAPPED aOverlapped, LPDWORD aBytes, BOOL aWait)
{
  /*
  RecordReplayFunction(GetOverlappedResult, BOOL, aFile, aOverlapped, aBytes, aWait);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aBytes);
  return rval;
  */

  MOZ_CRASH();
  return false;
}

RRFunctionHandle0(GetProcessHeap)

static DWORD __stdcall
RR_GetProcessHeaps(DWORD aCount, PHANDLE aHeaps)
{
  MOZ_CRASH();
  return 0;
}

#define RecordReplayProfileStringFunction(aSuffix, aDataType)           \
  static DWORD __stdcall                                                \
  RR_GetProfileString ## aSuffix                                        \
      (void* aAppName, void* aKeyName, void* aDeflt, aDataType aData, DWORD aDataChars) \
  {                                                                     \
    RecordReplayFunction(GetProfileString ## aSuffix, DWORD,            \
                         aAppName, aKeyName, aDeflt, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayProfileStringFunction)

static BOOL __stdcall
RR_GetQueuedCompletionStatus(HANDLE port, LPDWORD aBytes, PULONG_PTR aKey,
                             LPOVERLAPPED* aOverlapped, DWORD aMillis)
{
  RecordReplayFunction(GetQueuedCompletionStatus, BOOL,
                       port, aBytes, aKey, aOverlapped, aMillis);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aBytes);
  RestoreOverlappedThing(events, (void**)aKey);
  RestoreOverlappedThing(events, (void**)aOverlapped);
  return rval;
}

#define RecordReplayShortPathNameFunction(aSuffix, aDataType)           \
  static DWORD __stdcall                                                \
  RR_GetShortPathName ## aSuffix                                        \
      (void* aLongPath, aDataType aData, DWORD aDataChars)              \
  {                                                                     \
    RecordReplayFunction(GetShortPathName ## aSuffix, DWORD, aLongPath, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayShortPathNameFunction)

static VOID __stdcall
RR_GetSystemTime(LPSYSTEMTIME aTime)
{
  RecordReplayFunctionVoid(GetSystemTime, (aTime));
  events.RecordOrReplayBytes(aTime, sizeof(*aTime));
}

static BOOL __stdcall
RR_GetSystemTimeAdjustment(PDWORD aAdjust, PDWORD aIncrement, PBOOL aDisabled)
{
  RecordReplayFunction(GetSystemTimeAdjustment, BOOL, aAdjust, aIncrement, aDisabled);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aAdjust);
  events.RecordOrReplayValue(aIncrement);
  events.RecordOrReplayValue(aDisabled);
  return rval;
}

static VOID __stdcall
RR_GetSystemTimeAsFileTime(LPFILETIME aTime)
{
  RecordReplayFunctionVoid(GetSystemTimeAsFileTime, aTime);
  events.RecordOrReplayBytes(aTime, sizeof(*aTime));
}

#define RecordReplayBufferFunction(aName, aDataType)                    \
  static UINT __stdcall                                                 \
  RR_ ## aName(aDataType aData, DWORD aDataChars)                       \
  {                                                                     \
    RecordReplayFunction(aName, UINT, aData, aDataChars);               \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }

#define RecordReplaySystemDirectoryFunction(aSuffix, aDataType)         \
  RecordReplayBufferFunction(GetSystemDirectory ## aSuffix, aDataType)
InstantiateStringsAW(RecordReplaySystemDirectoryFunction)

static VOID __stdcall
RR_GetSystemInfo(LPSYSTEM_INFO aInfo)
{
  RecordReplayFunctionVoid(GetSystemInfo, aInfo);
  events.RecordOrReplayBytes(aInfo, sizeof(*aInfo));
}

#define RecordReplayTempFileNameFunction(aSuffix, aDataType)            \
  static UINT __stdcall                                                 \
  RR_GetTempFileName ## aSuffix                                         \
      (void* aPath, void* aPrefix, UINT aUnique, aDataType aData)       \
  {                                                                     \
    RecordReplayFunction(GetTempFileName ## aSuffix, UINT, aPath, aPrefix, aUnique, aData); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, MAX_PATH * sizeof(aData[0]));     \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayTempFileNameFunction)

#define RecordReplayTempPathFunction(aSuffix, aDataType)                \
  static DWORD __stdcall                                                \
  RR_GetTempPath ## aSuffix(DWORD aDataChars, aDataType aData)          \
  {                                                                     \
    RecordReplayFunction(GetTempPath ## aSuffix, DWORD, aDataChars, aData); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayTempPathFunction)

#define RecordReplayTimeFormatFunction(aSuffix, aDataType)              \
  static int __stdcall                                                  \
  RR_GetTimeFormat ## aSuffix                                           \
      (LCID aLocale, DWORD aFlags, void* aTime, void* aFormat,          \
       aDataType aData, int aDataChars)                                 \
  {                                                                     \
    RecordReplayFunction(GetTempPath ## aSuffix, int,                   \
                         aLocale, aFlags, aTime, aFormat, aData, aDataChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayTimeFormatFunction)

static int __stdcall
RR_GetTimeFormatEx(LPCWSTR aLocale, DWORD aFlags, void* aTime, LPCWSTR aFormat,
                   LPWSTR aData, int aDataChars)
{
  RecordReplayFunction(GetTimeFormatEx, int, aLocale, aFlags, aTime, aFormat, aData, aDataChars);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));
  return rval;
}

static DWORD __stdcall
RR_GetTimeZoneInformation(LPTIME_ZONE_INFORMATION aTimeZone)
{
  RecordReplayFunction(GetTimeZoneInformation, DWORD, aTimeZone);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aTimeZone, sizeof(*aTimeZone));
  return rval;
}

#define RecordReplayWindowsDirectoryFunction(aSuffix, aDataType)        \
  RecordReplayBufferFunction(GetWindowsDirectory ## aSuffix, aDataType)
InstantiateStringsAW(RecordReplayWindowsDirectoryFunction)

static BOOL __stdcall
RR_InitializeCriticalSectionEx(LPCRITICAL_SECTION aSection, DWORD aSpinCount, DWORD aFlags)
{
  NewLock(aSection);
  if (!OriginalCall(InitializeCriticalSectionEx, BOOL, aSection, aSpinCount, aFlags)) {
    MOZ_CRASH();
  }
  return true;
}

RRFunctionZeroError0(IsDebuggerPresent)

static void __stdcall
RR_LeaveCriticalSection(LPCRITICAL_SECTION aSection)
{
  Lock* lock = FindLock(aSection);
  if (IsReplaying() && lock && !AreThreadEventsPassedThrough()) {
    ReplayUnlock(lock);
  } else {
    OriginalCall(LeaveCriticalSection, void, aSection);
  }
}

static LPVOID __stdcall
RR_MapViewOfFile(HANDLE aMapping, DWORD aAccess,
                 DWORD aOffsetHigh, DWORD aOffsetLow, SIZE_T aBytes)
{
  RecordReplayFunction(MapViewOfFile, LPVOID,
                       aMapping, aAccess, aOffsetHigh, aOffsetLow, aBytes);
  if (!aBytes) {
    MOZ_CRASH();
  }
  if (IsRecording() && !rval) {
    MOZ_CRASH();
  }
  if (IsReplaying()) {
    rval = DirectAllocateMemory(aBytes, AllocatedMemoryKind::Tracked);
  }
  events.RecordOrReplayBytes(rval, aBytes);
  return rval;
}

static LPVOID __stdcall
RR_MapViewOfFileEx(HANDLE aMapping, DWORD aAccess,
                   DWORD aOffsetHigh, DWORD aOffsetLow, SIZE_T aBytes, LPVOID aBase)
{
  if (aBase) {
    MOZ_CRASH();
  }
  return RR_MapViewOfFile(aMapping, aAccess, aOffsetHigh, aOffsetLow, aBytes);
}

RRFunctionZeroError2(MoveFileA)
RRFunctionZeroError2(MoveFileW)
RRFunctionZeroError3(MoveFileExA)
RRFunctionZeroError3(MoveFileExW)
RRFunctionHandle3(OpenEventA)
RRFunctionHandle3(OpenEventW)
RRFunctionHandle3(OpenFile)
RRFunctionHandle3(OpenFileMappingA)
RRFunctionHandle3(OpenFileMappingW)
RRFunctionHandle3(OpenJobObjectA)
RRFunctionHandle3(OpenJobObjectW)

static void __stdcall
RR_OutputDebugStringW(LPCWSTR aString)
{
  // Make sure events are passed through when sending strings to any debugger.
  AutoEnsurePassThroughThreadEvents pt;
  OriginalCall(OutputDebugStringW, void, aString);
}

static BOOL __stdcall
RR_PostQueuedCompletionStatus(HANDLE aPort, DWORD aBytes, ULONG_PTR aKey, LPOVERLAPPED aOverlapped)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing((void*)aKey, nullptr, 0);
    RegisterOverlappedThing(aOverlapped, nullptr, 0);
  }
  RecordReplayFunction(PostQueuedCompletionStatus, BOOL, aPort, aBytes, aKey, aOverlapped);
  RecordOrReplayHadErrorZero(rrf);
  return rval;
}

#define RecordReplayLargeIntegerFunction(aName)                 \
  static BOOL __stdcall                                         \
  RR_ ## aName (LARGE_INTEGER* aNum)                            \
  {                                                             \
    RecordReplayFunction(aName, BOOL, aNum);                    \
    RecordOrReplayHadErrorZero(rrf);                            \
    events.RecordOrReplayBytes(aNum, sizeof(*aNum));            \
    return rval;                                                \
  }

RecordReplayLargeIntegerFunction(QueryPerformanceCounter);
RecordReplayLargeIntegerFunction(QueryPerformanceFrequency);

static BOOL __stdcall
RR_ReadFile(HANDLE aFile, LPVOID aBuffer, DWORD aBytes,
            LPDWORD aReadBytes, LPOVERLAPPED aOverlapped)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing(aOverlapped, aBuffer, aBytes);
  }
  RecordReplayFunction(ReadFile, BOOL, aFile, aBuffer, aBytes, aReadBytes, aOverlapped);
  if (!aReadBytes) {
    MOZ_CRASH();
  }
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aReadBytes);
  events.RecordOrReplayBytes(aBuffer, *aReadBytes);
  return rval;
}

static BOOL __stdcall
RR_ReadFileEx(HANDLE aFile, LPVOID aBuffer, DWORD aBytes, LPOVERLAPPED aOverlapped,
              LPOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_ReadFileScatter(HANDLE aFile, FILE_SEGMENT_ELEMENT* aSegments, DWORD aBytes,
                   LPDWORD aReserved, LPOVERLAPPED aOverlapped)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_ReadProcessMemory(HANDLE aProcess, LPCVOID aBase, LPVOID aBuffer, SIZE_T aBytes,
                     SIZE_T* aReadBytes)
{
  MOZ_CRASH();
  return false;
}

RRFunctionZeroError1(RemoveDirectoryA)
RRFunctionZeroError1(RemoveDirectoryW)
RecordReplayOrderedFunction(ResetEvent, BOOL, false, (HANDLE aEvent), aEvent)

#define RecordReplaySearchPathFunction(aSuffix, aDataType)              \
  static DWORD __stdcall                                                \
  RR_SearchPath ## aSuffix                                              \
      (void* aPath, void* aFile, void* aExtension,                      \
       DWORD aDataChars, aDataType aData, aDataType* aFilePart)         \
  {                                                                     \
    RecordReplayFunction(SearchPath ## aSuffix, DWORD,                  \
                         aPath, aFile, aExtension, aDataChars, aData, aFilePart); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aData, aDataChars * sizeof(aData[0]));   \
    if (aFilePart) {                                                    \
      MOZ_CRASH();                                                      \
    }                                                                   \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplaySearchPathFunction)

RRFunctionZeroError1(SetCurrentDirectoryA)
RRFunctionZeroError1(SetCurrentDirectoryW)
RRFunctionZeroError1(SetEndOfFile)
RRFunctionZeroError1(SetEnvironmentStringsA)
RRFunctionZeroError1(SetEnvironmentStringsW)
RRFunctionZeroError2(SetEnvironmentVariableA)
RRFunctionZeroError2(SetEnvironmentVariableW)
RecordReplayOrderedFunction(SetEvent, BOOL, false, (HANDLE aEvent), aEvent)
RRFunctionZeroError2(SetFileAttributesA)
RRFunctionZeroError2(SetFileAttributesW)

static DWORD __stdcall
RR_SetFilePointer(HANDLE aFile, LONG aDistance, PLONG aDistanceHigh, DWORD aMethod)
{
  RecordReplayFunction(SetFilePointer, DWORD, aFile, aDistance, aDistanceHigh, aMethod);
  RecordOrReplayHadErrorZero(rrf);
  if (aDistanceHigh) {
    events.RecordOrReplayValue(aDistanceHigh);
  }
  return rval;
}

static BOOL __stdcall
RR_SetFilePointerEx(HANDLE aFile, LARGE_INTEGER aDistance, PLARGE_INTEGER aNewPointer,
                    DWORD aMethod)
{
  RecordReplayFunction(SetFilePointerEx, BOOL, aFile, aDistance, aNewPointer, aMethod);
  RecordOrReplayHadErrorZero(rrf);
  if (aNewPointer) {
    events.RecordOrReplayBytes(aNewPointer, sizeof(*aNewPointer));
  }
  return rval;
}

RRFunctionZeroError4(SetFileTime)

static BOOL __stdcall
RR_TryEnterCriticalSection(LPCRITICAL_SECTION aSection)
{
  Lock* lock = FindLock(aSection);
  if (IsReplaying() && lock && !AreThreadEventsPassedThrough()) {
    if (RecordReplayValue(0)) {
      ReplayLock(lock);
      return true;
    }
  } else {
    BOOL rv = OriginalCall(TryEnterCriticalSection, BOOL, aSection);
    if (lock) {
      RecordReplayValue(!!rv);
    }
    if (rv) {
      if (lock) {
        BeginLock(lock);
        RecordLock(lock);
      }
      return true;
    }
  }
  return false;
}

static LPVOID __stdcall
RR_VirtualAlloc(LPVOID aAddress, SIZE_T aSize, DWORD aType, DWORD aProtect)
{
  // Ignore MEM_RESET / MEM_RESET_UNDO.
  if (aType & (MEM_RESET | MEM_RESET_UNDO)) {
    MOZ_ASSERT(aAddress);
    return aAddress;
  }

  if (IsReplaying() && (aType & MEM_COMMIT)) {
    LPVOID res = ReplayTryAllocateMemory(aAddress, aSize);
    if (res) {
      return res;
    }
  }

  LPVOID res = OriginalCall(VirtualAlloc, LPVOID,
                            aAddress, aSize, aType, PAGE_EXECUTE_READWRITE);
  if (!res) {
    // Allow allocations to fail if the caller specified a particular address.
    // This does not indicate an OOM, only that this particular address is
    // unavailable.
    if (!aAddress) {
      InvalidateRecording("Out of memory");
    }
    return nullptr;
  }
  if (IsReplaying() && (aType & MEM_COMMIT)) {
    ReplayRegisterAllocatedMemory(res, aSize, AllocatedMemoryKind::Tracked);
  }
  return res;
}

static LPVOID __stdcall
RR_VirtualAllocEx(HANDLE aProcess, LPVOID aAddress, SIZE_T aSize, DWORD aType, DWORD aProtect)
{
  MOZ_RELEASE_ASSERT(aProcess == OriginalCall(GetCurrentProcess, HANDLE));
  return RR_VirtualAlloc(aAddress, aSize, aType, aProtect);
}

static BOOL __stdcall
RR_VirtualFree(LPVOID aAddress, SIZE_T aSize, DWORD aType)
{
  MOZ_ASSERT(aType == MEM_DECOMMIT || aType == MEM_RELEASE);
  if (IsReplaying()) {
    MOZ_CRASH();
    if (aType == MEM_RELEASE) {
      ReplayDeallocateMemory(aAddress, aSize, false);
    }
  } else {
    if (!OriginalCall(VirtualFree, BOOL, aAddress, aSize, aType)) {
      MOZ_CRASH();
    }
  }
  return true;
}

static BOOL __stdcall
RR_VirtualFreeEx(HANDLE aProcess, LPVOID aAddress, SIZE_T aSize, DWORD aType)
{
  MOZ_RELEASE_ASSERT(aProcess == OriginalCall(GetCurrentProcess, HANDLE));
  return RR_VirtualFree(aAddress, aSize, aType);
}

static BOOL __stdcall
RR_VirtualProtect(LPVOID aAddress, SIZE_T aSize, DWORD aNewProtect, PDWORD aOldProtect)
{
  // After a snapshot has been taken, disallow further memory protection calls
  // that were not triggered from the snapshot mechanism. Protect calls before
  // the first snapshot may still go through if they are giving write access,
  // in case the program is trying to unprotect memory for writing (e.g. for
  // DLL executable code patching).
  if (HasTakenSnapshot()) {
    *aOldProtect = PAGE_NOACCESS;
    return true;
  }
  switch (aNewProtect) {
  case PAGE_EXECUTE:
  case PAGE_EXECUTE_READ:
    aNewProtect = PAGE_EXECUTE_READWRITE;
    break;
  case PAGE_NOACCESS:
  case PAGE_READONLY:
    aNewProtect = PAGE_READWRITE;
    break;
  default:
    break;
  }
  if (!OriginalCall(VirtualProtect, BOOL, aAddress, aSize, aNewProtect, aOldProtect)) {
    MOZ_CRASH();
  }
  return true;
}

static BOOL __stdcall
RR_VirtualProtectEx(HANDLE aProcess, LPVOID aAddress, SIZE_T aSize,
                    DWORD aNewProtect, PDWORD aOldProtect)
{
  MOZ_RELEASE_ASSERT(aProcess == OriginalCall(GetCurrentProcess, HANDLE));
  return RR_VirtualProtect(aAddress, aSize, aNewProtect, aOldProtect);
}

RRFunctionZeroError4(WaitForMultipleObjects)
RRFunctionZeroError5(WaitForMultipleObjectsEx)

static DWORD __stdcall
RR_WaitForSingleObject(HANDLE aHandle, DWORD aMillis)
{
  if (AreThreadEventsPassedThrough()) {
    return OriginalCall(WaitForSingleObject, DWORD, aHandle, aMillis);
  }

  BeginOrderedEvent();
  DWORD rval;
  if (IsRecording()) {
    AutoPassThroughThreadEvents pt;
    rval = OriginalCall(WaitForSingleObject, DWORD, aHandle, aMillis);
    MOZ_RELEASE_ASSERT(rval != WAIT_ABANDONED);
  }
  EndOrderedEvent();

  Thread* thread = Thread::CurrentMaybePassedThrough(false);
  thread->mEvents.RecordOrReplayValue(&rval);

  if (rval != WAIT_FAILED && RecordOrReplayIsSemaphore(thread->mEvents, aHandle)) {
    if (IsReplaying() && rval == WAIT_OBJECT_0) {
      AutoPassThroughThreadEvents pt;
      DWORD newRval = OriginalCall(WaitForSingleObject, DWORD, aHandle, INFINITE);
      MOZ_RELEASE_ASSERT(newRval == WAIT_OBJECT_0);
    }
  }

  return rval;
}

RRFunctionZeroError3(WaitForSingleObjectEx)

static BOOL __stdcall
RR_WriteFile(HANDLE aFile, LPCVOID aBuffer, DWORD aSize, LPDWORD aBytesWritten,
             LPOVERLAPPED aOverlapped)
{
  if (!AreThreadEventsPassedThrough()) {
    RegisterOverlappedThing(aOverlapped, nullptr, 0);
  }
  RecordReplayFunction(WriteFile, BOOL, aFile, aBuffer, aSize, aBytesWritten, aOverlapped);
  RecordOrReplayHadErrorZero(rrf);
  if (aBytesWritten) {
    events.RecordOrReplayValue(aBytesWritten);
  }
  return rval;
}

static BOOL __stdcall
RR_WriteFileEx(HANDLE aFile, LPCVOID aBuffer, DWORD aSize,
               LPOVERLAPPED aOverlapped, LPOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_WriteFileGather(HANDLE aFile, FILE_SEGMENT_ELEMENT* aSegments,
                   DWORD aBytes, LPDWORD aReserved, LPOVERLAPPED aOverlapped)
{
  MOZ_CRASH();
  return false;
}

RecordReplayReadFunction(_hread)
RRFunctionNegError3(_hwrite)
RRFunctionNegError1(_lclose)
RRFunctionNegError2(_lcreat)
RRFunctionNegError3(_llseek)
RRFunctionNegError2(_lopen)
RecordReplayReadFunction(_lread)
RRFunctionNegError3(_lwrite)

///////////////////////////////////////////////////////////////////////////////
// kernelbase redirections
///////////////////////////////////////////////////////////////////////////////

// Information about all libraries that have been loaded.
struct LibraryInfo {
  LPCWSTR mName;
  DWORD mFlags;
  HMODULE mLibrary;
  LibraryInfo(LPCWSTR aName, DWORD aFlags, HMODULE aLibrary)
    : mName(aName), mFlags(aFlags), mLibrary(aLibrary)
  {}
  bool Matches(LPCWSTR aName, DWORD aFlags) {
    return !wcscmp(mName, aName) && mFlags == aFlags;
  }
};
static StaticInfallibleVector<LibraryInfo> gLibraries;

static BOOL __stdcall
RR_FreeLibrary(HMODULE aModule)
{
  // Don't free libraries which were loaded from mozilla code.
  if (AreThreadEventsPassedThrough()) {
    return OriginalCall(FreeLibrary, BOOL, aModule);
  }
  return true;
}

// Note: LoadLibraryA, LoadLibraryW, and LoadLibraryExA are wrappers for this function.
static HMODULE __stdcall
RR_LoadLibraryExW(LPCWSTR aName, HANDLE aFile, DWORD aFlags)
{
  if (AreThreadEventsPassedThrough() || !IsRecordingOrReplaying()) {
    return OriginalCall(LoadLibraryExW, HMODULE, aName, aFile, aFlags);
  }

  MOZ_ASSERT(!aFile);

  HMODULE rval = nullptr;
  if (IsRecording()) {
    AutoPassThroughThreadEvents pt;
    rval = OriginalCall(LoadLibraryExW, HMODULE, aName, aFile, aFlags);
    if (!rval) {
      return nullptr;
    }
  }

  PR_Lock(gGlobalLock);
  if (IsRecording()) {
    bool found = false;
    for (const LibraryInfo& info : gLibraries) {
      if (info.Matches(aName, aFlags)) {
        MOZ_ASSERT(rval == info.mLibrary);
        found = true;
        break;
      }
    }
    if (!found) {
      gLibraries.emplaceBack(wcsdup(aName), aFlags, rval);
    }
  } else {
    for (const LibraryInfo& info : gLibraries) {
      if (info.Matches(aName, aFlags)) {
        rval = info.mLibrary;
        break;
      }
    }
  }
  PR_Unlock(gGlobalLock);
  return rval;
}

void
WriteLoadedLibraries(Stream& aStream)
{
  aStream.WriteScalar(gLibraries.length());
  for (const LibraryInfo& info : gLibraries) {
    size_t len = wcslen(info.mName);
    aStream.WriteScalar(len);
    aStream.WriteBytes(info.mName, (len + 1) * sizeof(wchar_t));
    aStream.WriteScalar(info.mFlags);
  }
}

void
ReadLoadedLibraries(Stream& aStream)
{
  size_t count = aStream.ReadScalar();
  for (size_t i = 0; i < count; i++) {
    size_t len = aStream.ReadScalar();
    LPWSTR name = new wchar_t[len + 1];
    aStream.ReadBytes(name, (len + 1) * sizeof(wchar_t));
    DWORD flags = aStream.ReadScalar();
    HMODULE library = LoadLibraryExW(name, nullptr, flags);
    if (!library) {
      MOZ_CRASH();
    }
    gLibraries.emplaceBack(name, flags, library);
  }
}

static HLOCAL __stdcall
RR_LocalFree(HLOCAL aMem)
{
  RecordReplayFunction(LocalFree, HLOCAL, aMem);
  events.RecordOrReplayValue(&rval);
  if (rval != 0) {
    events.RecordOrReplayValue(&rrf.mError);
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// user32 redirections
///////////////////////////////////////////////////////////////////////////////

RRFunctionHandle2(ActivateKeyboardLayout)

static BOOL __stdcall
RR_AdjustWindowRect(LPRECT aRect, DWORD aStyle, BOOL aMenu)
{
  RecordReplayFunction(AdjustWindowRect, BOOL, aRect, aStyle, aMenu);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aRect, sizeof(*aRect));
  return rval;
}

static BOOL __stdcall
RR_AdjustWindowRectEx(LPRECT aRect, DWORD aStyle, BOOL aMenu, DWORD aExStyle)
{
  RecordReplayFunction(AdjustWindowRectEx, BOOL, aRect, aStyle, aMenu, aExStyle);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aRect, sizeof(*aRect));
  return rval;
}

RRFunctionZeroError3(AnimateWindow)
RRFunctionHandle1(BeginDeferWindowPos)

static HDC __stdcall
RR_BeginPaint(HWND aHwnd, LPPAINTSTRUCT aPaint)
{
  MOZ_CRASH();
  return false;
}

static LRESULT __stdcall
RR_CallNextHookEx(HHOOK aHook, int aCode, WPARAM aWParam, LPARAM aLParam)
{
  MOZ_CRASH();
  return false;
}

RRFunctionZeroError5(CallWindowProcA)
RRFunctionZeroError5(CallWindowProcW)

static LONG __stdcall
RR_ChangeDisplaySettingsA(DEVMODE* aDevmode, DWORD aFlags)
{
  MOZ_CRASH();
  return false;
}

static LONG __stdcall
RR_ChangeDisplaySettingsW(DEVMODE* aDevmode, DWORD aFlags)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_ClientToScreen(HWND aHwnd, LPPOINT aPoint)
{
  RecordReplayFunction(ClientToScreen, BOOL, aHwnd, aPoint);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aPoint, sizeof(*aPoint));
  return rval;
}

RRFunctionZeroError0(CloseClipboard)
RRFunctionZeroError1(CloseDesktop)
//RRFunctionZeroError1(CloseGestureInfoHandle)
RRFunctionZeroError1(CloseWindow)
RRFunctionZeroError4(CreateCaret)
RRFunctionHandle1(CreateIconIndirect)
RRFunctionHandle12(CreateWindowExA)

static HWND __stdcall
RR_CreateWindowExW(DWORD aExStyle, LPCWSTR aClass, LPCWSTR aWindow,
                   DWORD aStyle, int aX, int aY, int aWidth, int aHeight,
                   HWND aParent, HMENU aMenu, HINSTANCE aInstance, LPVOID aParam)
{
  if (AreThreadEventsPassedThrough()) {
    return OriginalCall(CreateWindowExW, HWND,
                        aExStyle, aClass, aWindow, aStyle,
                        aX, aY, aWidth, aHeight, aParent, aMenu, aInstance, aParam);
  }
  HWND rv = nullptr;
  if (IsRecording()) {
    AutoPassThroughThreadEventsAllowCallbacks pt;
    rv = OriginalCall(CreateWindowExW, HWND,
                      aExStyle, aClass, aWindow, aStyle,
                      aX, aY, aWidth, aHeight, aParent, aMenu, aInstance, aParam);
  } else {
    ReplayCallbacks();
  }
  return (HWND)RecordReplayValue((size_t)rv);
}

#define RecordReplayWindowProcFunction(aName)                           \
  static LRESULT __stdcall RR_ ## aName                                 \
    (HWND aWnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam)              \
  {                                                                     \
    if (AreThreadEventsPassedThrough()) {                               \
      return OriginalCall(aName, LRESULT, aWnd, aMsg, aWParam, aLParam); \
    }                                                                   \
    LRESULT rv = 0;                                                     \
    if (IsRecording()) {                                                \
      AutoPassThroughThreadEventsAllowCallbacks pt;                     \
      rv = OriginalCall(aName, LRESULT, aWnd, aMsg, aWParam, aLParam);  \
    } else {                                                            \
      ReplayCallbacks();                                                \
    }                                                                   \
    return RecordReplayValue(rv);                                       \
  }

RecordReplayWindowProcFunction(DefWindowProcA)
RecordReplayWindowProcFunction(DefWindowProcW)
RRFunctionZeroError0(DestroyCaret)
RRFunctionZeroError1(DestroyIcon)
RRFunctionZeroError1(DestroyWindow)

static LRESULT __stdcall
RR_DispatchMessageW(const MSG* aMsg)
{
  if (AreThreadEventsPassedThrough()) {
    return OriginalCall(DispatchMessageW, LRESULT, aMsg);
  }
  LRESULT rv = 0;
  if (IsRecording()) {
    AutoPassThroughThreadEventsAllowCallbacks pt;
    rv = OriginalCall(DispatchMessageW, LRESULT, aMsg);
  } else {
    ReplayCallbacks();
  }
  return RecordReplayValue(rv);
}

RRFunctionZeroError4(DrawEdge)
RRFunctionZeroError2(DrawFocusRect)
RRFunctionZeroError4(DrawFrameControl)
RRFunctionZeroError0(EmptyClipboard)
RRFunctionZeroError2(EnableWindow)
RRFunctionZeroError2(EndPaint)

static BOOL __stdcall
RR_EnumChildWindows(HWND aParent, WNDENUMPROC aFunc, LPARAM aParam)
{
  MOZ_CRASH();
  return false;
}

#define RecordReplayEnumDisplayDevicesFunction(aSuffix)                 \
  static BOOL __stdcall                                                 \
  RR_EnumDisplayDevices ## aSuffix                                      \
    (DWORD aDevice, DWORD aNum, PDISPLAY_DEVICE ## aSuffix aOut, DWORD aFlags) \
  {                                                                     \
    RecordReplayFunction(EnumDisplayDevices ## aSuffix, BOOL, aDevice, aNum, aOut, aFlags); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aOut, sizeof(*aOut));                    \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayEnumDisplayDevicesFunction)

static BOOL __stdcall
RR_EnumDisplayMonitors(HDC aHdc, LPCRECT aClip, MONITORENUMPROC aCallback, LPARAM aData)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_EnumDisplaySettingsA(LPCSTR aName, DWORD aMode, DEVMODE* aDevmode)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_EnumDisplaySettingsW(LPCWSTR aName, DWORD aMode, DEVMODE* aDevmode)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_EnumDisplaySettingsExA(LPCSTR aName, DWORD aMode, DEVMODE* aDevmode, DWORD aFlags)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_EnumDisplaySettingsExW(LPCWSTR aName, DWORD aMode, DEVMODE* aDevmode, DWORD aFlags)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_EnumThreadWindows(DWORD aThread, WNDENUMPROC aCallback, LPARAM aData)
{
  MOZ_CRASH();
  return false;
}

RRFunctionHandle2(FindWindowA)
RRFunctionHandle2(FindWindowW)
RRFunctionHandle4(FindWindowExA)
RRFunctionHandle4(FindWindowExW)
RRFunctionZeroError2(FlashWindow)
RRFunctionZeroError1(FlashWindowEx)
RRFunctionHandle0(GetActiveWindow)
RRFunctionHandle2(GetAncestor)

static BOOL __stdcall
RR_GetClassInfoW(HINSTANCE aInstance, LPCTSTR aName, LPWNDCLASS aClass)
{
  RecordReplayFunction(GetClassInfoW, BOOL, aInstance, aName, aClass);
  RecordOrReplayHadErrorZero(rrf);

  // Mozilla callers use GetClassInfo to test for existence.
  memset(aClass, 0x5E, sizeof(*aClass));

  return rval;
}

RRFunctionHandle1(GetClipboardData)

static BOOL __stdcall
RR_GetCursorPos(LPPOINT aPoint)
{
  RecordReplayFunction(GetCursorPos, BOOL, aPoint);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aPoint, sizeof(*aPoint));
  return rval;
}

RRFunctionHandle1(GetDC)
RRFunctionHandle3(GetDCEx)
RRFunctionHandle2(GetDlgItem)

static UINT __stdcall
RR_GetDlgItemInt(HWND aDlg, int aItem, BOOL* aTranslated, BOOL aSign)
{
  MOZ_CRASH();
  return false;
}

#define RecordReplayGetDlgItemTextFunction(aSuffix, aDataType)          \
  static UINT __stdcall                                                 \
  RR_GetDlgItemText ## aSuffix (HWND aDlg, int aItem, aDataType aString, int aStringChars) \
  {                                                                     \
    RecordReplayFunction(GetDlgItemText ## aSuffix, UINT, aDlg, aItem, aString, aStringChars); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aString, aStringChars * sizeof(aString[0])); \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayGetDlgItemTextFunction)

RRFunctionZeroError0(GetDoubleClickTime)
RRFunctionHandle0(GetFocus)
RRFunctionHandle0(GetForegroundWindow)

static BOOL __stdcall
RR_GetIconInfo(HICON aIcon, PICONINFO aInfo)
{
  RecordReplayFunction(GetIconInfo, BOOL, aIcon, aInfo);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aInfo, sizeof(*aInfo));
  return rval;
}

RRFunctionZeroError1(GetKeyState)
RRFunctionHandle1(GetKeyboardLayout)

static int __stdcall
RR_GetKeyboardLayoutList(int aBufferCount, HKL* aBuffer)
{
  RecordReplayFunction(GetKeyboardLayoutList, int, aBufferCount, aBuffer);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aBuffer, rval * sizeof(HKL));
  return rval;
}

#define RecordReplayGetKeyboardLayoutNameFunction(aSuffix, aDataType)   \
  static BOOL __stdcall                                                 \
  RR_GetKeyboardLayoutName ## aSuffix (aDataType aName)                 \
  {                                                                     \
    RecordReplayFunction(GetKeyboardLayoutName ## aSuffix, BOOL, aName); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    events.RecordOrReplayBytes(aName, KL_NAMELENGTH * sizeof(aName[0])); \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayGetKeyboardLayoutNameFunction)

static BOOL __stdcall
RR_GetKeyboardState(PBYTE aKeyState)
{
  RecordReplayFunction(GetKeyboardState, BOOL, aKeyState);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aKeyState, 256);
  return rval;
}

static BOOL __stdcall
RR_GetLastInputInfo(PLASTINPUTINFO aLii)
{
  RecordReplayFunction(GetLastInputInfo, BOOL, aLii);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aLii, sizeof(*aLii));
  return rval;
}

RRFunctionZeroError0(GetMessageExtraInfo)
RRFunctionZeroError0(GetMessagePos)
RRFunctionZeroError0(GetMessageTime)

static BOOL __stdcall
RR_GetMonitorInfoA(HMONITOR aMonitor, LPMONITORINFO aMi)
{
  MOZ_CRASH();
  return false;
}

static BOOL __stdcall
RR_GetMonitorInfoW(HMONITOR aMonitor, LPMONITORINFO aMi)
{
  MOZ_CRASH();
  return false;
}

RRFunctionZeroError1(GetQueueStatus)
RRFunctionZeroError1(GetSysColor)
RRFunctionHandle1(GetSysColorBrush)
RRFunctionHandle2(GetSystemMenu)
RRFunctionZeroError1(GetSystemMetrics)

/*
static BOOL __stdcall
RR_GetTouchInputInfo(HANDLE aInput, UINT aCount, void* aInputs, int a0)
{
  MOZ_CRASH();
  return false;
}
*/

static BOOL __stdcall
RR_GetUpdateRect(HWND aWnd, LPRECT aRect, BOOL aErase)
{
  RecordReplayFunction(GetUpdateRect, BOOL, aWnd, aRect, aErase);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aRect, sizeof(*aRect));
  return rval;
}

RRFunctionZeroError3(GetUpdateRgn)

static DWORD __stdcall
RR_GetWindowThreadProcessId(HWND aWnd, LPDWORD aProcid)
{
  RecordReplayFunction(GetWindowThreadProcessId, DWORD, aWnd, aProcid);
  RecordOrReplayHadErrorZero(rrf);
  if (aProcid) {
    events.RecordOrReplayValue(aProcid);
  }
  return rval;
}

RRFunctionZeroError0(InSendMessage)
RRFunctionZeroError1(InSendMessageEx)

static BOOL __stdcall
RR_InflateRect(LPRECT aRect, int aDx, int aDy)
{
  RecordReplayFunction(InflateRect, BOOL, aRect, aDx, aDy);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aRect, sizeof(*aRect));
  return rval;
}

RRFunctionZeroError1(IsClipboardFormatAvailable)
RRFunctionZeroError1(IsIconic)
RRFunctionZeroError1(IsWindowEnabled)
RRFunctionZeroError1(IsWindowVisible)
RRFunctionZeroError2(KillTimer)
RRFunctionHandle2(LoadCursorA)
RRFunctionHandle2(LoadCursorW)
RRFunctionHandle2(LoadKeyboardLayoutA)
RRFunctionHandle2(LoadKeyboardLayoutW)
RRFunctionZeroError3(MapVirtualKeyExA)
RRFunctionZeroError3(MapVirtualKeyExW)

static int __stdcall
RR_MapWindowPoints(HWND aFrom, HWND aTo, LPPOINT aPoints, UINT aCount)
{
  MOZ_ASSERT(AreThreadEventsPassedThrough());
  return OriginalCall(MapWindowPoints, int, aFrom, aTo, aPoints, aCount);
}

RRFunctionZeroError1(MessageBeep)
RRFunctionHandle2(MonitorFromPoint)
RRFunctionHandle2(MonitorFromRect)
RRFunctionHandle2(MonitorFromWindow)
RRFunctionZeroError5(MsgWaitForMultipleObjects)
RRFunctionZeroError5(MsgWaitForMultipleObjectsEx)
RRFunctionZeroError1(OpenClipboard)

#define RecordReplayPeekMessageFunction(aSuffix)                        \
  static BOOL __stdcall RR_PeekMessage ## aSuffix                       \
    (LPMSG aMsg, HWND aWnd, UINT aFilterMin, UINT aFilterMax, UINT aRemoveMsg) \
  {                                                                     \
    if (AreThreadEventsPassedThrough()) {                               \
      return OriginalCall(PeekMessage ## aSuffix, BOOL,                 \
                          aMsg, aWnd, aFilterMin, aFilterMax, aRemoveMsg); \
    }                                                                   \
    BOOL rv = false;                                                    \
    if (IsRecording()) {                                                \
      AutoPassThroughThreadEventsAllowCallbacks pt;                     \
      rv = OriginalCall(PeekMessage ## aSuffix, BOOL,                   \
                          aMsg, aWnd, aFilterMin, aFilterMax, aRemoveMsg); \
    } else {                                                            \
      ReplayCallbacks();                                                \
    }                                                                   \
    RecordReplayBytes(aMsg, sizeof(*aMsg));                             \
    return RecordReplayValue(rv);                                       \
  }
InstantiateAW(RecordReplayPeekMessageFunction)

#define RecordReplayPostMessageFunction(aSuffix)                        \
  static BOOL __stdcall RR_PostMessage ## aSuffix                       \
    (HWND aWnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam)              \
  {                                                                     \
    if (!AreThreadEventsPassedThrough()) {                              \
      RegisterCallbackData((void*)aLParam);                             \
    }                                                                   \
    RecordReplayFunction(PostMessage ## aSuffix, BOOL, aWnd, aMsg, aWParam, aLParam); \
    RecordOrReplayHadErrorZero(rrf);                                    \
    return rval;                                                        \
  }
InstantiateAW(RecordReplayPostMessageFunction)

RRFunctionVoid1(PostQuitMessage)
RRFunctionZeroError4(RedrawWindow)

static ATOM __stdcall
RR_RegisterClassW(WNDCLASSW* aClass)
{
  if (!AreThreadEventsPassedThrough()) {
    NoteRegisteredClass(aClass);
  }
  RecordReplayFunction(RegisterClassW, ATOM, aClass);
  RecordOrReplayHadErrorZero(rrf);
  return rval;
}

RRFunctionZeroError1(RegisterClipboardFormatA)
RRFunctionZeroError1(RegisterClipboardFormatW)
RRFunctionZeroError1(RegisterWindowMessageA)
RRFunctionZeroError1(RegisterWindowMessageW)
RRFunctionZeroError0(ReleaseCapture)
RRFunctionZeroError2(ReleaseDC)
RecordReplayWindowProcFunction(SendMessageA)
RecordReplayWindowProcFunction(SendMessageW)
RRFunctionZeroError4(SetMenuItemInfoA)
RRFunctionZeroError4(SetMenuItemInfoW)
RRFunctionHandle7(SetWinEventHook)
RRFunctionZeroError3(SetWindowLongA)
RRFunctionZeroError3(SetWindowLongW)
RRFunctionZeroError7(SetWindowPos)
RRFunctionZeroError3(SetWindowRgn)
RRFunctionHandle4(SetWindowsHookExA)
RRFunctionHandle4(SetWindowsHookExW)
RRFunctionZeroError1(ShowCaret)
RRFunctionZeroError1(ShowCursor)
RRFunctionZeroError2(ShowWindow)

// Some WinUser.h headers don't define this.
#ifndef SPI_GETWHEELSCROLLCHARS
#define SPI_GETWHEELSCROLLCHARS 0x6C
#endif

static void
EncodeSystemParametersInfo(Stream& aStream, UINT aAction, PVOID aValue)
{
  switch (aAction) {
  case SPI_SETDESKWALLPAPER:
  case SPI_SETSCREENSAVETIMEOUT:
    break;
  case SPI_GETFLATMENU:
  case SPI_GETFONTSMOOTHING:
  case SPI_GETSNAPTODEFBUTTON:
    aStream.RecordOrReplayValue((BOOL*) aValue);
    break;
  case SPI_GETFONTSMOOTHINGCONTRAST:
  case SPI_GETFONTSMOOTHINGTYPE:
  case SPI_GETGRADIENTCAPTIONS:
  case SPI_GETSCREENSAVETIMEOUT:
  case SPI_GETWHEELSCROLLCHARS:
  case SPI_GETWHEELSCROLLLINES:
    aStream.RecordOrReplayValue((UINT*) aValue);
    break;
  case SPI_GETFOREGROUNDFLASHCOUNT:
  case SPI_GETMENUSHOWDELAY:
    aStream.RecordOrReplayValue((DWORD*) aValue);
    break;
  case SPI_GETHIGHCONTRAST:
    aStream.RecordOrReplayBytes(aValue, sizeof(HIGHCONTRAST));
    ((HIGHCONTRAST*)aValue)->lpszDefaultScheme = (LPTSTR)0x1;
    break;
  case SPI_GETICONTITLELOGFONT:
    aStream.RecordOrReplayBytes(aValue, sizeof(LOGFONT));
    break;
  case SPI_GETNONCLIENTMETRICS:
    aStream.RecordOrReplayBytes(aValue, sizeof(NONCLIENTMETRICS));
    break;
  case SPI_GETWORKAREA:
    aStream.RecordOrReplayBytes(aValue, sizeof(RECT));
    break;
  default:
    MOZ_CRASH();
  }
}

static BOOL __stdcall
RR_SystemParametersInfoA(UINT aAction, UINT aParam, PVOID aValue, UINT aIni)
{
  RecordReplayFunction(SystemParametersInfoA, BOOL, aAction, aParam, aValue, aIni);
  RecordOrReplayHadErrorZero(rrf);
  EncodeSystemParametersInfo(events, aAction, aValue);
  return rval;
}

static BOOL __stdcall
RR_SystemParametersInfoW(UINT aAction, UINT aParam, PVOID aValue, UINT aIni)
{
  RecordReplayFunction(SystemParametersInfoW, BOOL, aAction, aParam, aValue, aIni);
  RecordOrReplayHadErrorZero(rrf);
  EncodeSystemParametersInfo(events, aAction, aValue);
  return rval;
}

static BOOL __stdcall
RR_TrackMouseEvent(LPTRACKMOUSEEVENT aTrack)
{
  MOZ_CRASH();
  return false;
}

RRFunctionZeroError7(TrackPopupMenu)
RRFunctionZeroError1(TranslateMessage)
RRFunctionZeroError1(UnloadKeyboardLayout)

///////////////////////////////////////////////////////////////////////////////
// mfplat redirections
///////////////////////////////////////////////////////////////////////////////

RRFunction2(MFStartup)
RRFunction0(MFShutdown)

///////////////////////////////////////////////////////////////////////////////
// ntdll redirections
///////////////////////////////////////////////////////////////////////////////

// This is only here so I can break on it in Visual Studio :(
static NTSTATUS __stdcall
RR_NtWaitForSingleObject(HANDLE aHandle, BOOLEAN aAlertable, PLARGE_INTEGER aTimeout)
{
  return OriginalCall(NtWaitForSingleObject, NTSTATUS, aHandle, aAlertable, aTimeout);
}

///////////////////////////////////////////////////////////////////////////////
// ole32 redirections
///////////////////////////////////////////////////////////////////////////////

static HRESULT __stdcall
RR_CLSIDFromString(LPCOLESTR aStr, LPCLSID aClassid)
{
  RecordReplayFunction(CLSIDFromString, HRESULT, aStr, aClassid);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aClassid, sizeof(*aClassid));
  return rval;
}

static HRESULT __stdcall
RR_CoCreateGuid(GUID* aId)
{
  RecordReplayFunction(CoCreateGuid, HRESULT, aId);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aId, sizeof(*aId));
  return rval;
}

static void
CreateCOMObject(REFIID aId, LPVOID* aThing);

static HRESULT __stdcall
RR_CoCreateInstance(/*REFCLSID*/void* aClassid, LPUNKNOWN aOuter,
                    DWORD aContext, /*REFIID*/void* aId, LPVOID* aThing)
{
  RecordReplayFunction(CoCreateInstance, HRESULT, aClassid, aOuter, aContext, aId, aThing);
  events.RecordOrReplayValue(&rval);
  bool hasObject = IsRecording() ? !!*aThing : false;
  events.RecordOrReplayValue(&hasObject);
  if (hasObject) {
    // aId is a void* to avoid confusing template type inference in RecordReplayFunction.
    CreateCOMObject(*reinterpret_cast<_GUID*>(aId), aThing);
  } else {
    *aThing = nullptr;
  }
  return rval;
}

RRFunction1(CoInitialize)
RRFunction2(CoInitializeEx)
RRFunction9(CoInitializeSecurity)
RRFunctionZeroError8(CoSetProxyBlanket)

static LPVOID __stdcall
RR_CoTaskMemAlloc(SIZE_T aSize)
{
  if (IsRecording()) {
    return OriginalCall(CoTaskMemAlloc, LPVOID, aSize);
  }
  return (LPVOID)0x1;
}

static void __stdcall
RR_CoTaskMemFree(LPVOID aData)
{
  if (IsRecording()) {
    OriginalCall(CoTaskMemFree, void, aData);
  }
}

static LPVOID __stdcall
RR_CoTaskMemRealloc(LPVOID aData, SIZE_T aSize)
{
  if (IsRecording()) {
    return OriginalCall(CoTaskMemRealloc, LPVOID, aData, aSize);
  }
  return (LPVOID)0x1;
}

static HRESULT __stdcall
RR_CoWaitForMultipleHandles(DWORD aFlags, DWORD aTimeout, ULONG aCount, LPHANDLE aHandles,
                            LPDWORD aIndex)
{
  RecordReplayFunction(CoWaitForMultipleHandles, HRESULT,
                       aFlags, aTimeout, aCount, aHandles, aIndex);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aIndex);
  return rval;
}

RRFunctionVoid0(CoUninitialize)
RRFunctionHandle3(OleDuplicateData)
RRFunctionZeroError0(OleFlushClipboard)

static HRESULT __stdcall
RR_OleGetClipboard(LPDATAOBJECT* aObj)
{
  RecordReplayFunction(OleGetClipboard, HRESULT, aObj);
  events.RecordOrReplayValue(&rval);
  MOZ_CRASH();
  return rval;
}

RRFunctionZeroError1(OleInitialize)
RRFunctionZeroError1(OleQueryLinkFromData)
RRFunctionZeroError1(OleSetClipboard)
RRFunctionVoid0(OleUninitialize)

///////////////////////////////////////////////////////////////////////////////
// rpcrt4 redirections
///////////////////////////////////////////////////////////////////////////////

static RPC_STATUS __stdcall
RR_UuidToStringA(const UUID* aId, char** aString)
{
  RecordReplayFunction(UuidToStringA, RPC_STATUS, aId, aString);
  events.RecordOrReplayValue(&rval);
  size_t len = IsRecording() ? strlen(*aString) + 1 : 0;
  events.RecordOrReplayValue(&len);
  if (IsReplaying()) {
    *aString = NewLeakyArray<char>(len);
  }
  events.RecordOrReplayBytes(*aString, len);
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// shell32 redirections
///////////////////////////////////////////////////////////////////////////////

static LPWSTR* __stdcall
RR_CommandLineToArgvW(LPCWSTR aCmdLine, int* aNumArgs)
{
  RecordReplayFunction(CommandLineToArgvW, LPWSTR*, aCmdLine, aNumArgs);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aNumArgs);
  if (rval) {
    if (IsReplaying()) {
      rval = NewLeakyArray<LPWSTR>(*aNumArgs);
    }
    for (int i = 0; i < *aNumArgs; i++) {
      size_t len = IsRecording() ? wcslen(rval[i]) : 0;
      events.RecordOrReplayValue(&len);
      if (IsReplaying()) {
        rval[i] = NewLeakyArray<wchar_t>(len + 1);
      }
      events.RecordOrReplayBytes(rval[i], (len + 1) * sizeof(wchar_t));
    }
  }
  return rval;
}

RRFunctionHandle1(ILCreateFromPathA)
RRFunctionHandle1(ILCreateFromPathW)
RRFunction1(SetCurrentProcessExplicitAppUserModelID)

static HRESULT __stdcall
RR_SHGetKnownFolderPath(/*REFKNOWNFOLDERID*/void* aId, DWORD aFlags, HANDLE aToken, PWSTR* aPath)
{
  RecordReplayFunction(SHGetKnownFolderPath, HRESULT, aId, aFlags, aToken, aPath);
  events.RecordOrReplayValue(&rval);
  size_t len = (IsRecording() && *aPath) ? wcslen(*aPath) : 0;
  events.RecordOrReplayValue(&len);
  if (IsReplaying()) {
    *aPath = len ? NewLeakyArray<WCHAR>(len + 1) : nullptr;
  }
  if (len) {
    events.RecordOrReplayBytes(*aPath, (len + 1) * sizeof(aPath[0]));
  }
  return rval;
}

static BOOL __stdcall
RR_SHGetPathFromIDListW(PCIDLIST_ABSOLUTE aIdl, LPWSTR aPath)
{
  RecordReplayFunction(SHGetPathFromIDListW, BOOL, aIdl, aPath);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aPath, MAX_PATH * sizeof(aPath[0]));
  return rval;
}

static HRESULT __stdcall
RR_SHGetSpecialFolderLocation(HWND aWnd, int aFolder, PIDLIST_ABSOLUTE* aIdl)
{
  RecordReplayFunction(SHGetSpecialFolderLocation, HRESULT, aWnd, aFolder, aIdl);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aIdl);
  return rval;
}

static BOOL __stdcall
RR_SHGetSpecialFolderPathW(HWND aWnd, LPWSTR aPath, int aId, BOOL aCreate)
{
  RecordReplayFunction(SHGetSpecialFolderPathW, BOOL, aWnd, aPath, aId, aCreate);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aPath, MAX_PATH * sizeof(aPath[0]));
  return rval;
}

/*
static HRESULT __stdcall
RR_SHLoadLibraryFromKnownFolder(REFKNOWNFOLDERID aLibrary, DWORD aMode,
                                REFIID aId, LPVOID* aThing)
{
  RecordReplayFunction(SHLoadLibraryFromKnownFolder, HRESULT, aLibrary, aMode, aId, aThing);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aThing);
  return rval;
}
*/

RRFunction4(SHOpenFolderAndSelectItems)

///////////////////////////////////////////////////////////////////////////////
// setupapi redirections
///////////////////////////////////////////////////////////////////////////////

static BOOL __stdcall
RR_SetupDiEnumDeviceInfo(HDEVINFO aInfo, DWORD aIndex, PSP_DEVINFO_DATA aData)
{
  RecordReplayFunction(SetupDiEnumDeviceInfo, BOOL, aInfo, aIndex, aData);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aData, sizeof(*aData));
  return rval;
}

static BOOL __stdcall
RR_SetupDiGetDeviceRegistryPropertyW(HDEVINFO aInfo, PSP_DEVINFO_DATA aData, DWORD aProperty,
                                     PDWORD aDataType, PBYTE aBuffer, DWORD aBufferSize,
                                     PDWORD aRequiredSize)
{
  RecordReplayFunction(SetupDiGetDeviceRegistryPropertyW, BOOL,
                       aInfo, aData, aProperty, aDataType, aBuffer, aBufferSize, aRequiredSize);
  RecordOrReplayHadErrorZero(rrf);
  if (aDataType) {
    events.RecordOrReplayValue(aDataType);
  }
  if (aBuffer) {
    events.RecordOrReplayBytes(aBuffer, aBufferSize);
  }
  if (aRequiredSize) {
    events.RecordOrReplayValue(aRequiredSize);
  }
  return rval;
}

RRFunctionHandle4(SetupDiGetClassDevsW)
RRFunctionZeroError1(SetupDiDestroyDeviceInfoList)

///////////////////////////////////////////////////////////////////////////////
// shcore redirections
///////////////////////////////////////////////////////////////////////////////

static HRESULT __stdcall
RR_GetProcessDpiAwareness(HANDLE aProcess, PROCESS_DPI_AWARENESS* aValue)
{
  RecordReplayFunction(GetProcessDpiAwareness, HRESULT, aProcess, aValue);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aValue);
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// shlwapi redirections
///////////////////////////////////////////////////////////////////////////////

#define RecordReplayPathRemoveFileSpecFunction(aSuffix, aDataType)      \
  static BOOL __stdcall                                                 \
  RR_PathRemoveFileSpec ## aSuffix (aDataType aPath)                    \
  {                                                                     \
    RecordReplayFunction(PathRemoveFileSpec ## aSuffix, BOOL, aPath);   \
    events.RecordOrReplayValue(&rval);                                  \
    events.RecordOrReplayBytes(aPath, 256 * sizeof(aPath[0]));          \
    return rval;                                                        \
  }
InstantiateStringsAW(RecordReplayPathRemoveFileSpecFunction)

///////////////////////////////////////////////////////////////////////////////
// ucrtbase redirections
///////////////////////////////////////////////////////////////////////////////

// The vfprintf library functions seem to call WriteFile an inconsistent number
// of times. Unfortunately, hooking this function means that other functions
// which operate on FILE* pointers need to be hooked as well.
#define RecordReplayVfprintf(aName)                                     \
  static int __cdecl                                                    \
  RR_ ## aName                                                          \
    (unsigned __int64 a0, FILE* a1, const char* a2, _locale_t a3, va_list a4) \
  {                                                                     \
    RecordReplayFunctionABI(aName, int, __cdecl, a0, a1, a2, a3, a4);   \
    events.RecordOrReplayValue(&rval);                                  \
    return rval;                                                        \
  }

RecordReplayVfprintf(__stdio_common_vfprintf)
RecordReplayVfprintf(__stdio_common_vfprintf_p)
RecordReplayVfprintf(__stdio_common_vfprintf_s)

static uintptr_t __cdecl
RR__beginthreadex(void* aSecurity, unsigned aStackSize,
                  LPTHREAD_START_ROUTINE aStart, void* aStartArg, unsigned aFlags,
                  unsigned* aThreadId)
{
  // _beginthreadex is a wrapper around CreateThread, except the created thread
  // does some CRT specific initialization/teardown. Skip this stuff, since it
  // there are some strange calls to LoadLibraryExW and this is apparently only
  // necessary to avoid a small memory leak.
  return (uintptr_t) RR_CreateThread((LPSECURITY_ATTRIBUTES) aSecurity, aStackSize,
                                     aStart, aStartArg, aFlags, (LPDWORD) aThreadId);
}

RRFunctionZeroErrorABI2(_fdopen, __cdecl)

// Calls LoadLibrary in a weird way.
static __time64_t __cdecl
RR__time64(__time64_t* aTimer)
{
  RecordReplayFunctionABI(_time64, __time64_t, __cdecl, aTimer);
  events.RecordOrReplayBytes(&rval, sizeof(__time64_t));
  return rval;
}

RRFunctionVoidABI0(_tzset, __cdecl)

static char* __cdecl
RR_getenv(char* aName)
{
  RecordReplayFunctionABI(getenv, char*, __cdecl, aName);
  size_t len = (IsRecording() && rval) ? strlen(rval) + 1 : 0;
  events.RecordOrReplayValue(&rval);
  if (IsReplaying()) {
    rval = len ? NewLeakyArray<char>(len) : nullptr;
  }
  if (len) {
    events.RecordOrReplayBytes(rval, len);
  }
  return rval;
}

RRFunctionNegErrorABI1(fclose, __cdecl)
RRFunctionZeroErrorABI2(fopen, __cdecl)

// Calls LoadLibrary in a weird way.
static char* __cdecl
RR_setlocale(int aCategory, const char* aLocale)
{
  RecordReplayFunctionABI(setlocale, char*, __cdecl, aCategory, aLocale);
  size_t len = IsRecording() ? strlen(rval) : 0;
  events.RecordOrReplayValue(&len);
  if (IsReplaying()) {
    rval = NewLeakyArray<char>(len + 1);
  }
  events.RecordOrReplayBytes(rval, len + 1);
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// uxtheme redirections
///////////////////////////////////////////////////////////////////////////////

static HRESULT __stdcall
RR_GetThemeBackgroundContentRect(HTHEME aTheme, HDC aHdc, int aPart, int aState,
                                 LPCRECT aBound, LPRECT aContent)
{
  RecordReplayFunction(GetThemeBackgroundContentRect, HRESULT,
                       aTheme, aHdc, aPart, aState, aBound, aContent);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aContent, sizeof(*aContent));
  return rval;
}

static HRESULT __stdcall
RR_GetThemePartSize(HTHEME aTheme, HDC aHdc, int aPart, int aState, LPCRECT aRect,
                    enum THEMESIZE aSize, SIZE* aResult)
{
  RecordReplayFunction(GetThemePartSize, HRESULT,
                       aTheme, aHdc, aPart, aState, aRect, aSize, aResult);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aResult, sizeof(*aResult));
  return rval;
}

RRFunction1(CloseThemeData)
RRFunctionZeroError6(DrawThemeBackground)
RRFunctionZeroError6(DrawThemeBackgroundEx)
RRFunctionZeroError0(IsAppThemed)
RRFunctionHandle2(OpenThemeData)

///////////////////////////////////////////////////////////////////////////////
// version redirections
///////////////////////////////////////////////////////////////////////////////

static DWORD __stdcall
RR_GetFileVersionInfoSizeW(LPCWSTR aName, LPDWORD aHandle)
{
  RecordReplayFunction(GetFileVersionInfoSizeW, DWORD, aName, aHandle);
  RecordOrReplayHadErrorZero(rrf);
  if (aHandle) {
    events.RecordOrReplayValue(aHandle);
  }
  return rval;
}

static BOOL __stdcall
RR_GetFileVersionInfoW(LPCWSTR aName, DWORD aHandle, DWORD aDataBytes, LPVOID aData)
{
  RecordReplayFunction(GetFileVersionInfoW, BOOL, aName, aHandle, aDataBytes, aData);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayBytes(aData, aDataBytes);
  return rval;
}

static BOOL __stdcall
RR_VerQueryValueW(LPCVOID aBlock, LPCWSTR aSub, LPVOID* aBuf, PUINT aLen)
{
  RecordReplayFunction(VerQueryValueW, BOOL, aBlock, aSub, aBuf, aLen);
  RecordOrReplayHadErrorZero(rrf);
  if (rval) {
    size_t offset = IsRecording() ? ((char*)*aBuf - (char*)aBlock) : 0;
    events.RecordOrReplayValue(&offset);
    if (IsReplaying()) {
      *aBuf = (char*)aBlock + offset;
    }
    events.RecordOrReplayValue(aLen);
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// ws2_32 redirections
///////////////////////////////////////////////////////////////////////////////

// Used by the FD_ISSET macro.
RRFunctionZeroError2(__WSAFDIsSet)

static SOCKET __stdcall
RR_accept(SOCKET aSocket, struct sockaddr* aAddr, int* aLen)
{
  RecordReplayFunction(accept, SOCKET, aSocket, aAddr, aLen);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aLen);
  events.RecordOrReplayBytes(aAddr, *aLen);
  return rval;
}

RRFunctionNegError3(bind)
RRFunctionNegError1(closesocket)
RRFunctionNegError3(connect)

static int __stdcall
RR_gethostname(char* aName, int aNamelen)
{
  RecordReplayFunction(gethostname, int, aName, aNamelen);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayBytes(aName, aNamelen);
  return rval;
}

static int __stdcall
RR_getsockname(SOCKET aSocket, struct sockaddr* aName, int* aNamelen)
{
  RecordReplayFunction(getsockname, int, aSocket, aName, aNamelen);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aNamelen);
  events.RecordOrReplayBytes(aName, *aNamelen);
  return rval;
}

static int __stdcall
RR_getsockopt(SOCKET aSocket, int aLevel, int aName, char* aData, int* aDataBytes)
{
  RecordReplayFunction(getsockopt, int, aSocket, aLevel, aName, aData, aDataBytes);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aDataBytes);
  events.RecordOrReplayBytes(aData, *aDataBytes);
  return rval;
}

RRFunctionNegError2(listen)

static int __stdcall
RR_ioctlsocket(SOCKET aSocket, long aCommand, unsigned long* aArgument)
{
  RecordReplayFunction(ioctlsocket, int, aSocket, aCommand, aArgument);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aArgument);
  return rval;
}

static int __stdcall
RR_recv(SOCKET aSocket, char* aBuf, int aLen, int aFlags)
{
  RecordReplayFunction(recv, int, aSocket, aBuf, aLen, aFlags);
  RecordOrReplayHadErrorNegative(rrf);
  if (rval > 0) {
    events.RecordOrReplayBytes(aBuf, rval);
  }
  return rval;
}

static int __stdcall
RR_select(int aNumFds, fd_set* aReadFds, fd_set* aWriteFds, fd_set* aExceptFds, DWORD aTimeout)
{
  RecordReplayFunction(select, int, aNumFds, aReadFds, aWriteFds, aExceptFds, aTimeout);
  RecordOrReplayHadErrorNegative(rrf);
  if (aReadFds) {
    events.RecordOrReplayBytes(aReadFds, sizeof(*aReadFds));
  }
  if (aWriteFds) {
    events.RecordOrReplayBytes(aWriteFds, sizeof(*aWriteFds));
  }
  if (aExceptFds) {
    events.RecordOrReplayBytes(aExceptFds, sizeof(*aExceptFds));
  }
  return rval;
}

RRFunctionNegError4(send)
RRFunctionNegError5(setsockopt)
RRFunctionNegError2(shutdown)
RRFunctionNegError3(socket)
RRFunction0(WSACleanup)

static BOOL __stdcall
RR_WSAGetOverlappedResult(SOCKET aSocket, LPOVERLAPPED aOverlapped, LPDWORD aBytes,
                          BOOL aWait, LPDWORD aFlags)
{
  /*
  RecordReplayFunction(WSAGetOverlappedResult, BOOL,
                       aSocket, aOverlapped, aBytes, aWait, aFlags);
  RecordOrReplayHadErrorZero(rrf);
  events.RecordOrReplayValue(aBytes);
  events.RecordOrReplayValue(aFlags);
  return rval;
  */

  MOZ_CRASH();
  return false;
}

static int __stdcall
RR_WSAIoctl(SOCKET aSocket, DWORD aCode, LPVOID aBuf, DWORD aBufSize,
            LPVOID aOutBuf, DWORD aOutBufSize,
            LPDWORD aBytesReturned, LPOVERLAPPED aOverlapped,
            LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  RecordReplayFunction(WSAIoctl, int,
                       aSocket, aCode, aBuf, aBufSize, aOutBuf, aOutBufSize, aBytesReturned,
                       aOverlapped, aRoutine);
  MOZ_CRASH();
  return rval;
}

static void
RecvGuts(Stream& aStream, LPWSABUF aBuffers, DWORD aBufferCount,
         LPDWORD aBytesReceived, LPDWORD aFlags,
         LPOVERLAPPED aOverlapped,
         LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  aStream.RecordOrReplayValue(aBytesReceived);
  aStream.RecordOrReplayValue(aFlags);
  size_t nbytes = *aBytesReceived;
  for (size_t i = 0; i < aBufferCount; i++) {
    size_t bufbytes = nbytes < aBuffers[i].len ? nbytes : aBuffers[i].len;
    aStream.RecordOrReplayBytes(aBuffers[i].buf, bufbytes);
    nbytes -= bufbytes;
  }
  if (aOverlapped || aRoutine) {
    MOZ_CRASH();
  }
}

static int __stdcall
RR_WSARecv(SOCKET aSocket, LPWSABUF aBuffers, DWORD aBufferCount, LPDWORD aBytesReceived,
           LPDWORD aFlags, LPOVERLAPPED aOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  RecordReplayFunction(WSARecv, int,
                       aSocket, aBuffers, aBufferCount, aBytesReceived, aFlags,
                       aOverlapped, aRoutine);
  RecordOrReplayHadErrorNegative(rrf);
  RecvGuts(events, aBuffers, aBufferCount, aBytesReceived, aFlags, aOverlapped, aRoutine);
  return rval;
}

static int __stdcall
RR_WSARecvFrom(SOCKET aSocket, LPWSABUF aBuffers, DWORD aBufferCount,
               LPDWORD aBytesReceived, LPDWORD aFlags, struct sockaddr* aFrom, LPINT aFromLen,
               LPOVERLAPPED aOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  RecordReplayFunction(WSARecvFrom, int,
                       aSocket, aBuffers, aBufferCount, aBytesReceived, aFlags,
                       aFrom, aFromLen, aOverlapped, aRoutine);
  RecordOrReplayHadErrorNegative(rrf);
  RecvGuts(events, aBuffers, aBufferCount, aBytesReceived, aFlags, aOverlapped, aRoutine);
  events.RecordOrReplayValue(aFromLen);
  events.RecordOrReplayBytes(aFrom, *aFromLen);
  return rval;
}

static int __stdcall
RR_WSASend(SOCKET aSocket, LPWSABUF aBuffers, DWORD aBufferCount, LPDWORD aBytesSent, DWORD aFlags,
           LPOVERLAPPED aOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  RecordReplayFunction(WSASend, int,
                       aSocket, aBuffers, aBufferCount, aBytesSent, aFlags,
                       aOverlapped, aRoutine);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aBytesSent);
  if (aOverlapped || aRoutine) {
    MOZ_CRASH();
  }
  return rval;
}

static int __stdcall
RR_WSASendTo(SOCKET aSocket, LPWSABUF aBuffers, DWORD aBufferCount,
             LPDWORD aBytesSent, DWORD aFlags, const struct sockaddr* aTo, int aTolen,
             LPOVERLAPPED aOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE aRoutine)
{
  RecordReplayFunction(WSASendTo, int,
                       aSocket, aBuffers, aBufferCount, aBytesSent, aFlags,
                       aTo, aTolen, aOverlapped, aRoutine);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aBytesSent);
  if (aOverlapped || aRoutine) {
    MOZ_CRASH();
  }
  return rval;
}

static int __stdcall
RR_WSAStartup(WORD aVersion, LPWSADATA aData)
{
  RecordReplayFunction(WSAStartup, int, aVersion, aData);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayBytes(aData, sizeof(*aData));
  aData->lpVendorInfo = (char*) 0x1;
  return rval;
}

static int __stdcall
RR_WSAStringToAddressA(LPCSTR aAddr, int aFamily, LPWSAPROTOCOL_INFO aInfo,
                       LPSOCKADDR aSock, LPINT aSocklen)
{
  RecordReplayFunction(WSAStringToAddressA, int,
                       aAddr, aFamily, aInfo, aSock, aSocklen);
  RecordOrReplayHadErrorNegative(rrf);
  events.RecordOrReplayValue(aSocklen);
  events.RecordOrReplayBytes(aSock, *aSocklen);
  return rval;
}

static int __stdcall
RR_WSCEnumProtocols(LPINT aProtocols, LPWSAPROTOCOL_INFOW aBuf, LPDWORD aBufBytes, LPINT aError)
{
  RecordReplayFunction(WSCEnumProtocols, int, aProtocols, aBuf, aBufBytes, aError);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aBufBytes);
  events.RecordOrReplayValue(aError);
  if (aBuf) {
    events.RecordOrReplayBytes(aBuf, *aBufBytes);
  }
  return rval;
}

static int __stdcall
RR_WSCGetProviderInfo(LPGUID aId, int aType, PBYTE aInfo, size_t* aInfosize, DWORD aFlags,
                      LPINT aError)
{
  RecordReplayFunction(WSCGetProviderInfo, int,
                       aId, aType, aInfo, aInfosize, aFlags, aError);
  events.RecordOrReplayValue(&rval);
  events.RecordOrReplayValue(aInfosize);
  events.RecordOrReplayValue(aError);
  if (aInfo) {
    events.RecordOrReplayBytes(aInfo, *aInfosize);
  }
  return rval;
}

static int __stdcall
RR_WSCGetProviderPath(LPGUID aId, LPWSTR aPath, LPINT aPathlen, LPINT aError)
{
  int pathlenInit = *aPathlen;
  RecordReplayFunction(WSCGetProviderPath, int, aId, aPath, aPathlen, aError);
  events.RecordOrReplayValue(&rval);
  events.CheckInput(pathlenInit);
  events.RecordOrReplayValue(aPathlen);
  events.RecordOrReplayValue(aError);
  if (aPath) {
    events.RecordOrReplayBytes(aPath, min(pathlenInit, *aPathlen + 1) * sizeof(aPath[0]));
  }
  return rval;
}

///////////////////////////////////////////////////////////////////////////////
// COM Object Infrastructure Declarations
///////////////////////////////////////////////////////////////////////////////

static void
MaybeCreateCOMObject(REFIID aId, LPVOID* aThing)
{
  bool hasResult = RecordReplayValue(IsRecording() ? !!*aThing : 0);
  if (hasResult) {
    CreateCOMObject(aId, aThing);
  }
}

#define DECLARE_IUNKNOWN(aInterface)              \
  public:                                         \
    STDMETHODIMP_(ULONG) AddRef();                \
    STDMETHODIMP QueryInterface(REFIID, LPVOID*); \
    STDMETHODIMP_(ULONG) Release();               \
                                                  \
    RecordReplay ## aInterface (LPVOID aThing)    \
      : mRefCnt(1)                                \
    {                                             \
      MOZ_ASSERT(IsRecordingOrReplaying());       \
      MOZ_ASSERT(!!aThing == IsRecording());      \
      mThing = already_AddRefed<aInterface>((aInterface*) aThing); \
    }                                             \
                                                  \
    ~RecordReplay ## aInterface ()                \
    {                                             \
      if (IsRecording()) {                        \
        AutoPassThroughThreadEvents pt;           \
        mThing = nullptr;                         \
      }                                           \
    }                                             \
  private:                                        \
    RefPtr<aInterface> mThing;                    \
    ThreadSafeAutoRefCnt mRefCnt;                 \
    NS_DECL_OWNINGTHREAD

#define IMPLEMENT_IUNKNOWN(aInterface)            \
  NS_IMPL_ADDREF(RecordReplay ## aInterface)      \
  NS_IMPL_RELEASE(RecordReplay ## aInterface)     \
                                                  \
  STDMETHODIMP                                    \
  RecordReplay ## aInterface ::QueryInterface(REFIID aId, LPVOID* aThing) \
  {                                               \
    HRESULT rv;                                   \
    if (IsRecording()) {                          \
      rv = mThing->QueryInterface(aId, aThing);   \
    }                                             \
    MaybeCreateCOMObject(aId, aThing);            \
    return RecordReplayValue(rv);                 \
  }

#define COMBegin(aMethod, aActuals)                     \
  HRESULT rv = (HRESULT)0;                              \
  if (IsRecording()) {                                  \
    AutoPassThroughThreadEvents pt;                     \
    rv = mThing->aMethod aActuals;                      \
  }                                                     \
  rv = RecordReplayValue(rv)

#define COMFunction(aMethod, aFormals, aActuals)        \
  STDMETHODIMP aMethod aFormals                         \
  {                                                     \
    COMBegin(aMethod, aActuals);                        \
    return rv;                                          \
  }

#define COMFunctionCreate(aMethod, aFormals, aActuals, aInterface, aArg) \
  STDMETHODIMP aMethod aFormals                         \
  {                                                     \
    COMBegin(aMethod, aActuals);                        \
    MaybeCreateCOMObject(__uuidof(aInterface), (LPVOID*) aArg); \
    return rv;                                          \
  }

#define COMFunctionCrash(aMethod, aFormals)             \
  STDMETHODIMP aMethod aFormals                         \
  {                                                     \
    MOZ_CRASH();                                        \
    return (HRESULT)0;                                  \
  }

///////////////////////////////////////////////////////////////////////////////
// IApplicationAssociationRegistration
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIApplicationAssociationRegistration : public IApplicationAssociationRegistration
{
  DECLARE_IUNKNOWN(IApplicationAssociationRegistration)

public:
  STDMETHODIMP QueryCurrentDefault(LPCWSTR aQuery, ASSOCIATIONTYPE aType, ASSOCIATIONLEVEL aLevel,
                                   LPWSTR* aResult)
  {
    COMBegin(QueryCurrentDefault, (aQuery, aType, aLevel, aResult));
    size_t len = RecordReplayValue(IsRecording() ? wcslen(*aResult) : 0);
    if (IsReplaying()) {
      *aResult = NewLeakyArray<WCHAR>(len + 1);
    }
    RecordReplayBytes(*aResult, (len + 1) * sizeof(WCHAR));
    return rv;
  }

  COMFunctionCrash(QueryAppIsDefault,
                   (LPCWSTR, ASSOCIATIONTYPE, ASSOCIATIONLEVEL, LPCWSTR, BOOL*))
  COMFunctionCrash(QueryAppIsDefaultAll, (ASSOCIATIONLEVEL, LPCWSTR, BOOL*))
  COMFunctionCrash(SetAppAsDefault, (LPCWSTR, LPCWSTR, ASSOCIATIONTYPE))
  COMFunctionCrash(SetAppAsDefaultAll, (LPCWSTR))
  COMFunctionCrash(ClearUserAssociations, ())
};

IMPLEMENT_IUNKNOWN(IApplicationAssociationRegistration)

///////////////////////////////////////////////////////////////////////////////
// IAudioSessionControl
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIAudioSessionControl : public IAudioSessionControl
{
  DECLARE_IUNKNOWN(IAudioSessionControl)

public:
  COMFunctionCrash(GetState, (AudioSessionState*))
  COMFunctionCrash(GetDisplayName, (LPWSTR*))
  COMFunction(SetDisplayName, (LPCWSTR aValue, LPCGUID aCx), (aValue, aCx))
  COMFunctionCrash(GetIconPath, (LPWSTR*))
  COMFunction(SetIconPath, (LPCWSTR aValue, LPCGUID aCx), (aValue, aCx))
  COMFunctionCrash(GetGroupingParam, (GUID*))
  COMFunction(SetGroupingParam, (LPCGUID aOverride, LPCGUID aCx), (aOverride, aCx))
  COMFunction(RegisterAudioSessionNotification, (IAudioSessionEvents* aNotes), (aNotes))
  COMFunction(UnregisterAudioSessionNotification, (IAudioSessionEvents* aNotes), (aNotes))
};

IMPLEMENT_IUNKNOWN(IAudioSessionControl)

///////////////////////////////////////////////////////////////////////////////
// IAudioSessionManager
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIAudioSessionManager : public IAudioSessionManager
{
  DECLARE_IUNKNOWN(IAudioSessionManager)

public:
  COMFunctionCreate(GetAudioSessionControl,
                    (LPCGUID aGuid, DWORD aFlags, IAudioSessionControl** aControl),
                    (aGuid, aFlags, aControl), IAudioSessionControl, aControl)
  COMFunctionCrash(GetSimpleAudioVolume, (LPCGUID, DWORD, ISimpleAudioVolume**))
};

IMPLEMENT_IUNKNOWN(IAudioSessionManager)

///////////////////////////////////////////////////////////////////////////////
// IGlobalOptions
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIGlobalOptions : public IGlobalOptions
{
  DECLARE_IUNKNOWN(IGlobalOptions)

public:
  COMFunction(Set, (GLOBALOPT_PROPERTIES aProperty, ULONG_PTR aValue), (aProperty, aValue))
  COMFunctionCrash(Query, (GLOBALOPT_PROPERTIES, ULONG_PTR*))
};

IMPLEMENT_IUNKNOWN(IGlobalOptions)

///////////////////////////////////////////////////////////////////////////////
// IMFTransform
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIMFTransform : public IMFTransform
{
  DECLARE_IUNKNOWN(IMFTransform)

public:
  COMFunctionCrash(AddInputStreams, (DWORD, DWORD*))
  COMFunctionCrash(DeleteInputStream, (DWORD))
  COMFunctionCreate(GetAttributes, (IMFAttributes** aAttributes), (aAttributes),
                    IMFAttributes, aAttributes)
  COMFunctionCrash(GetInputAvailableType, (DWORD, DWORD, IMFMediaType**))
  COMFunctionCrash(GetInputCurrentType, (DWORD, IMFMediaType**))
  COMFunctionCrash(GetInputStatus, (DWORD, DWORD*))
  COMFunctionCrash(GetInputStreamAttributes, (DWORD, IMFAttributes**))

  STDMETHODIMP GetInputStreamInfo(DWORD aId, MFT_INPUT_STREAM_INFO* aInfo)
  {
    COMBegin(GetInputStreamInfo, (aId, aInfo));
    RecordReplayBytes(aInfo, sizeof(*aInfo));
    return rv;
  }

  COMFunctionCreate(GetOutputAvailableType, (DWORD aId, DWORD aIndex, IMFMediaType** aType),
                    (aId, aIndex, aType), IMFMediaType, aType)
  COMFunctionCreate(GetOutputCurrentType, (DWORD aId, IMFMediaType** aType),
                    (aId, aType), IMFMediaType, aType)
  COMFunctionCrash(GetOutputStatus, (DWORD*))
  COMFunctionCrash(GetOutputStreamAttributes, (DWORD, IMFAttributes**))

  STDMETHODIMP GetOutputStreamInfo(DWORD aId, MFT_OUTPUT_STREAM_INFO* aInfo)
  {
    COMBegin(GetOutputStreamInfo, (aId, aInfo));
    RecordReplayBytes(aInfo, sizeof(*aInfo));
    return rv;
  }

  COMFunctionCrash(GetStreamCount, (DWORD*, DWORD*))
  COMFunctionCrash(GetStreamIDs, (DWORD, DWORD*, DWORD, DWORD*))
  COMFunctionCrash(GetStreamLimits, (DWORD*, DWORD*, DWORD*, DWORD*))
  COMFunctionCrash(ProcessEvent, (DWORD, IMFMediaEvent*))
  COMFunction(ProcessInput, (DWORD aId, IMFSample* aSample, DWORD aFlags), (aId, aSample, aFlags))
  COMFunction(ProcessMessage, (MFT_MESSAGE_TYPE aMessage, ULONG_PTR aParam), (aMessage, aParam))
  COMFunctionCrash(ProcessOutput, (DWORD, DWORD, MFT_OUTPUT_DATA_BUFFER*, DWORD*))
  COMFunction(SetInputType, (DWORD aId, IMFMediaType* aType, DWORD aFlags), (aId, aType, aFlags))
  COMFunctionCrash(SetOutputBounds, (LONGLONG, LONGLONG))
  COMFunction(SetOutputType, (DWORD aId, IMFMediaType* aType, DWORD aFlags), (aId, aType, aFlags))
};

IMPLEMENT_IUNKNOWN(IMFTransform)

///////////////////////////////////////////////////////////////////////////////
// IMMDevice
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIMMDevice : public IMMDevice
{
  DECLARE_IUNKNOWN(IMMDevice)

public:
  STDMETHODIMP Activate(REFIID aId, DWORD aCx, PROPVARIANT* aParams, LPVOID* aThing)
  {
    COMBegin(Activate, (aId, aCx, aParams, aThing));
    MaybeCreateCOMObject(aId, aThing);
    return rv;
  }

  COMFunctionCrash(OpenPropertyStore, (DWORD, IPropertyStore**))
  COMFunctionCrash(GetId, (LPWSTR*))
  COMFunctionCrash(GetState, (DWORD*))
};

IMPLEMENT_IUNKNOWN(IMMDevice)

///////////////////////////////////////////////////////////////////////////////
// IMMDeviceEnumerator
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIMMDeviceEnumerator : public IMMDeviceEnumerator
{
  DECLARE_IUNKNOWN(IMMDeviceEnumerator)

public:
  COMFunctionCrash(EnumAudioEndpoints, (EDataFlow, DWORD, IMMDeviceCollection**))
  COMFunctionCreate(GetDefaultAudioEndpoint, (EDataFlow aFlow, ERole aRole, IMMDevice** aThing),
                    (aFlow, aRole, aThing), IMMDevice, aThing)
  COMFunctionCrash(GetDevice, (LPCWSTR, IMMDevice**))
  COMFunctionCrash(RegisterEndpointNotificationCallback, (IMMNotificationClient*))
  COMFunctionCrash(UnregisterEndpointNotificationCallback, (IMMNotificationClient*))
};

IMPLEMENT_IUNKNOWN(IMMDeviceEnumerator)

///////////////////////////////////////////////////////////////////////////////
// IPersistFile
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIPersistFile : public IPersistFile
{
  DECLARE_IUNKNOWN(IPersistFile)

public:
  COMFunctionCrash(GetClassID, (CLSID*))
  COMFunctionCrash(IsDirty, ())
  COMFunction(Load, (LPCOLESTR aName, DWORD aMode), (aName, aMode))
  COMFunction(Save, (LPCOLESTR aName, BOOL aRemember), (aName, aRemember))
  COMFunctionCrash(SaveCompleted, (LPCOLESTR))
  COMFunctionCrash(GetCurFile, (LPOLESTR*))
};

IMPLEMENT_IUNKNOWN(IPersistFile)

///////////////////////////////////////////////////////////////////////////////
// IShellLinkW
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIShellLinkW : public IShellLinkW
{
  DECLARE_IUNKNOWN(IShellLinkW)

public:
  STDMETHODIMP GetPath(LPWSTR aFile, int aFileChars, WIN32_FIND_DATAW* aFd, DWORD aFlags)
  {
    COMBegin(GetPath, (aFile, aFileChars, aFd, aFlags));
    RecordReplayBytes(aFile, aFileChars * sizeof(aFile[0]));
    RecordReplayBytes(aFd, sizeof(*aFd));
    return rv;
  }

  COMFunctionCrash(GetIDList, (PIDLIST_ABSOLUTE*))
  COMFunctionCrash(SetIDList, (PCIDLIST_ABSOLUTE))
  COMFunctionCrash(GetDescription, (LPWSTR, int))
  COMFunction(SetDescription, (LPCWSTR aName), (aName))
  COMFunctionCrash(GetWorkingDirectory, (LPWSTR, int))
  COMFunction(SetWorkingDirectory, (LPCWSTR aDir), (aDir))
  COMFunctionCrash(GetArguments, (LPWSTR, int))
  COMFunction(SetArguments, (LPCWSTR aArguments), (aArguments))
  COMFunctionCrash(GetHotkey, (WORD*))
  COMFunctionCrash(SetHotkey, (WORD))
  COMFunctionCrash(GetShowCmd, (int*))
  COMFunctionCrash(SetShowCmd, (int))
  COMFunctionCrash(GetIconLocation, (LPWSTR, int, int*))
  COMFunction(SetIconLocation, (LPCWSTR aPath, int aIcon), (aPath, aIcon))
  COMFunctionCrash(SetRelativePath, (LPCWSTR, DWORD))
  COMFunction(Resolve, (HWND aWnd, DWORD aFlags), (aWnd, aFlags))
  COMFunction(SetPath, (LPCWSTR aPath), (aPath))
};

IMPLEMENT_IUNKNOWN(IShellLinkW)

///////////////////////////////////////////////////////////////////////////////
// IWindowsParentalControls
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIWindowsParentalControls : public IWindowsParentalControls
{
  DECLARE_IUNKNOWN(IWindowsParentalControls)

public:
  COMFunctionCrash(GetVisibility, (WPCFLAG_VISIBILITY*))
  COMFunctionCreate(GetUserSettings, (LPCWSTR aId, IWPCSettings** aThing),
                    (aId, aThing), IWPCSettings, aThing)
  COMFunctionCreate(GetWebSettings, (LPCWSTR aId, IWPCWebSettings** aThing),
                    (aId, aThing), IWPCWebSettings, aThing)
  COMFunctionCrash(GetWebFilterInfo, (GUID*, LPWSTR*))
  COMFunctionCrash(GetGamesSettings, (LPCWSTR, IWPCGamesSettings**))
};

IMPLEMENT_IUNKNOWN(IWindowsParentalControls)

///////////////////////////////////////////////////////////////////////////////
// IWPCSettings
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIWPCSettings : public IWPCSettings
{
  DECLARE_IUNKNOWN(IWPCSettings)

public:
  STDMETHODIMP IsLoggingRequired(BOOL* aResult)
  {
    COMBegin(IsLoggingRequired, (aResult));
    *aResult = RecordReplayValue(*aResult);
    return rv;
  }

  COMFunctionCrash(GetLastSettingsChangeTime, (SYSTEMTIME*))

  STDMETHODIMP GetRestrictions(DWORD* aResult)
  {
    COMBegin(GetRestrictions, (aResult));
    *aResult = RecordReplayValue(*aResult);
    return rv;
  }
};

IMPLEMENT_IUNKNOWN(IWPCSettings)

///////////////////////////////////////////////////////////////////////////////
// IWPCWebSettings
///////////////////////////////////////////////////////////////////////////////

class RecordReplayIWPCWebSettings : public IWPCWebSettings
{
  DECLARE_IUNKNOWN(IWPCWebSettings)

public:
  COMFunctionCrash(IsLoggingRequired, (BOOL*))
  COMFunctionCrash(GetLastSettingsChangeTime, (SYSTEMTIME*))
  COMFunctionCrash(GetRestrictions, (DWORD*))

  STDMETHODIMP GetSettings(DWORD* aResult)
  {
    COMBegin(GetSettings, (aResult));
    *aResult = RecordReplayValue(*aResult);
    return rv;
  }

  STDMETHODIMP RequestURLOverride(HWND aWnd, LPCWSTR aUrl, DWORD aUrlCount,
                                  LPCWSTR* aSubURLs, BOOL* aChanged)
  {
    COMBegin(RequestURLOverride, (aWnd, aUrl, aUrlCount, aSubURLs, aChanged));
    *aChanged = RecordReplayValue(*aChanged);
    return rv;
  }
};

IMPLEMENT_IUNKNOWN(IWPCWebSettings)

///////////////////////////////////////////////////////////////////////////////
// COM Object Infrastructure Implementation
///////////////////////////////////////////////////////////////////////////////

static void
CreateCOMObject(REFIID aId, LPVOID* aThing)
{
  MOZ_ASSERT(IsRecordingOrReplaying());

  LPVOID existing = IsRecording() ? *aThing : nullptr;

#define HandleInterface(aInterface)                   \
  if (aId == __uuidof(aInterface)) {                  \
    *aThing = new RecordReplay ## aInterface (existing); \
    return;                                          \
  }

  HandleInterface(IApplicationAssociationRegistration)
  HandleInterface(IAudioSessionControl)
  HandleInterface(IAudioSessionManager)
  HandleInterface(IGlobalOptions)
  HandleInterface(IMFTransform)
  HandleInterface(IMMDevice)
  HandleInterface(IMMDeviceEnumerator)
  HandleInterface(IPersistFile)
  HandleInterface(IShellLinkW)
  HandleInterface(IWindowsParentalControls)
  HandleInterface(IWPCSettings)
  HandleInterface(IWPCWebSettings)

#undef HandleInterface

  MOZ_CRASH();
}

///////////////////////////////////////////////////////////////////////////////
// DLL Redirections
///////////////////////////////////////////////////////////////////////////////

static void
GetNamesInDLL(const char* aDllName,
              HMODULE* aModule,
              size_t** aNameOffsets,
              size_t* aNameCount)
{
  // Dig around in the DLL's file mapping to find the list of exported function names.
  PIMAGE_DOS_HEADER module = (PIMAGE_DOS_HEADER) LoadLibraryA(aDllName);
  MOZ_RELEASE_ASSERT(module);
  MOZ_ASSERT(module->e_magic == IMAGE_DOS_SIGNATURE);

  PIMAGE_NT_HEADERS header = (PIMAGE_NT_HEADERS)((uint8_t*)module + module->e_lfanew);
  MOZ_ASSERT(header->Signature == IMAGE_NT_SIGNATURE);
  MOZ_ASSERT(header->OptionalHeader.NumberOfRvaAndSizes > 0);

  PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)
    ((uint8_t*)module + header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
  MOZ_ASSERT(exports->AddressOfNames != 0);

  *aModule = (HMODULE) module;
  *aNameOffsets = (size_t*)((uint8_t*)module + exports->AddressOfNames);
  *aNameCount = exports->NumberOfNames;
}

static const char*
GetDLLName(HMODULE aModule, size_t aNameOffset)
{
  return (const char*)((uint8_t*)aModule + aNameOffset);
}

void
GetAllocatedRegionInfo(void* aAddress, uint8_t** aBase, size_t* aSize)
{
  MEMORY_BASIC_INFORMATION buffer;
  size_t nbytes = VirtualQuery(aAddress, &buffer, sizeof(buffer));
  MOZ_RELEASE_ASSERT(nbytes == sizeof(buffer));

  MOZ_ASSERT(buffer.AllocationBase <= buffer.BaseAddress);
  *aBase = (uint8_t*)buffer.AllocationBase;
  *aSize = (uint8_t*)buffer.BaseAddress -
           (uint8_t*)buffer.AllocationBase +
           buffer.RegionSize;
}

void
GetExecutableCodeRegionInDLL(const char* aDllName, uint8_t** aBase, size_t* aSize)
{
  HMODULE module;
  size_t* nameOffsets;
  size_t nameCount;
  GetNamesInDLL(aDllName, &module, &nameOffsets, &nameCount);

  MOZ_RELEASE_ASSERT(nameCount);
  const char* name = GetDLLName(module, nameOffsets[0]);
  uint8_t* address = (uint8_t*)GetProcAddress(module, name);
  MOZ_RELEASE_ASSERT(address);

  GetAllocatedRegionInfo(address, aBase, aSize);
}

#ifdef DEBUG

// On Windows, all exported functions in certain DLLs which are not otherwise
// redirected are hooked with CheckPassThroughTrampoline below to ensure that
// the function is only called when events are passed through. This gives a
// good assurance that all entry points into the DLL which are used anywhere by
// the browser have been redirected.

struct CheckPassThroughInfo {
  uint8_t* mStackBase;
  size_t mStackSize;
  size_t mPassThrough;
};
#define MAX_PASS_THROUGH_THREADS 50
CheckPassThroughInfo gPassThroughThreads[MAX_PASS_THROUGH_THREADS];

/* static */ void
Thread::SetPassThroughInArray(size_t aId, bool aValue)
{
  if (aId >= MAX_PASS_THROUGH_THREADS - 1) { // -1 so the last entry is always clear
    MOZ_CRASH();
  }
  if (!gPassThroughThreads[aId].mStackBase) {
    GetAllocatedRegionInfo(&aId, &gPassThroughThreads[aId].mStackBase,
                           &gPassThroughThreads[aId].mStackSize);
  }
  gPassThroughThreads[aId].mPassThrough = aValue;
}

struct CheckPassThroughFunction {
  void* mAddress;
  const char* mDllName;
  const char* mName;
  CheckPassThroughFunction(void* aAddress, const char* aDllName, const char* aName)
    : mAddress(aAddress), mDllName(aDllName), mName(aName)
  {}
};
static StaticInfallibleVector<CheckPassThroughFunction> gPassThroughFunctions;

static void
AddPassThroughFunction(void* aAddress, const char* aDllName, const char* aName)
{
  gPassThroughFunctions.emplaceBack(aAddress, aDllName, aName);
}

static void
CheckPassThroughFailed(void* aAddress)
{
  AutoEnsurePassThroughThreadEvents pt;
  fprintf(stderr, "CheckPassThrough failed:\n");
  for (const CheckPassThroughFunction& fn : gPassThroughFunctions) {
    if (fn.mAddress == aAddress) {
      fprintf(stderr, "Alias: %s %s\n", fn.mDllName, fn.mName);
    }
  }
  MOZ_CRASH();
}

void __declspec(naked)
CheckPassThroughTrampoline()
{
  // Look for a thread whose stack contains esp. If mPassThrough on that thread
  // is set, or if gPRIsRecordingOrReplaying is false, then this call is OK.
  // Otherwise call CheckPassThroughFailed.
  //
  // On entry, eax contains the function to jump to after finishing. Only touch
  // ecx and edx (the only other volatile registers), as for some reason some
  // windows internal functions crash later if we touch the stack.
  __asm {
    mov edx, OFFSET gPassThroughThreads
    add edx, 12
  restart:
    mov ecx, esp
    sub ecx, [edx]
    cmp ecx, [edx+4]
    jae miss
    mov ecx, [edx+8]
    cmp ecx, 0
    jne done
    mov ecx, gPRIsRecordingOrReplaying
    cmp [ecx], 0
    je done
    push eax
    call CheckPassThroughFailed
    jmp done
  miss:
    mov ecx, 0
    cmp ecx, [edx+4]
    je done
    add edx, 12
    jmp restart
  done:
    jmp eax
  }
}

// Exported symbols in DLLs which are not redirected but should not be hooked
// with CheckPassThroughTrampoline.
static bool
SkipUnredirectedSymbol(const char* aName)
{
  // Global variables.
  if (aName[0] == 'g' && strncmp(aName, "get", 3)) {
    return true;
  }

  // Some ole32 internals
  if (!strncmp(aName, "NdrProxy", 8) || !strncmp(aName, "ObjectStubless", 14)) {
    return true;
  }

  // Internal interface used by ntdll.dll
  if (!strcmp(aName, "IsThreadDesktopComposited")) {
    return true;
  }

  // Called during thread initialization/teardown
  if (!strcmp(aName, "ClientThreadSetup") || !strcmp(aName, "WahCloseThread")) {
    return true;
  }

  // Appears to be a variable.
  if (!strcmp(aName, "WEP")) {
    return true;
  }

  // Alias for GetLastError/SetLastError.
  if (!strcmp(aName, "WSAGetLastError") || !strcmp(aName, "WSASetLastError")) {
    return true;
  }

  // Alias for RtlEnterCriticalSection/RtlLeaveCriticalSection
  if (!strcmp(aName, "EngAcquireSemaphore") || !strcmp(aName, "EngReleaseSemaphore")) {
    return true;
  }

  // Alias for LocalFree
  if (!strcmp(aName, "MIDL_user_free_Ext") || !strcmp(aName, "AuditFree")) {
    return true;
  }

  // Used by MozStackWalk.
  if (!strcmp(aName, "PostThreadMessageA")) {
    return true;
  }

  // Used by LoadLibraryA, which is not itself redirected.
  if (!strcmp(aName, "EngMultiByteToUnicodeN")) {
    return true;
  }

  // Trivial functions in ws2_32
  if (!strcmp(aName, "htonl") ||
      !strcmp(aName, "htons") ||
      !strcmp(aName, "ntohl") ||
      !strcmp(aName, "ntohs")) {
    return true;
  }

  return false;
}

// Filter used to make sure we don't insert a use of the pass through
// trampoline for functions that have already been redirected.
static bool
FilterRedirectedFunction(void* aBase)
{
  for (size_t j = 0; j < CallEvent_Count; j++) {
    Redirection* redirection = &gRedirections[j];
    if (redirection->mBaseFunction == aBase) {
      return false;
    }
  }
  return true;
}

static void
RedirectDLLExports(const char* aDllName, Assembler& aAssembler)
{
  HMODULE module;
  size_t* nameOffsets;
  size_t nameCount;
  GetNamesInDLL(aDllName, &module, &nameOffsets, &nameCount);

  for (size_t i = 0; i < nameCount; i++) {
    const char* name = GetDLLName(module, nameOffsets[i]);
    if (SkipUnredirectedSymbol(name)) {
      continue;
    }
    RedirectFunctionForTrampoline(aDllName, name, FilterRedirectedFunction,
                                  (uint8_t*) CheckPassThroughTrampoline, aAssembler);
  }
}

void
RedirectAllDLLExports(Assembler& aAssembler)
{
  if (!IsRecording()) {
    return;
  }

  RedirectDLLExports("advapi32.dll", aAssembler);
  RedirectDLLExports("audioses.dll", aAssembler);
  RedirectDLLExports("gdi32.dll", aAssembler);
  RedirectDLLExports("iphlpapi.dll", aAssembler);
  //RedirectDLLExports("kernel32.dll", aAssembler);
  RedirectDLLExports("mfplat.dll", aAssembler);
  RedirectDLLExports("mmdevapi.dll", aAssembler);
  //RedirectDLLExports("ntdll.dll", aAssembler);
  RedirectDLLExports("ole32.dll", aAssembler);
  RedirectDLLExports("setupapi.dll", aAssembler);
  RedirectDLLExports("shcore.dll", aAssembler);
  RedirectDLLExports("shell32.dll", aAssembler);
  RedirectDLLExports("user32.dll", aAssembler);
  RedirectDLLExports("uxtheme.dll", aAssembler);
  RedirectDLLExports("version.dll", aAssembler);
  RedirectDLLExports("ws2_32.dll", aAssembler);
}

#endif // DEBUG

///////////////////////////////////////////////////////////////////////////////
// Direct Function Calls
///////////////////////////////////////////////////////////////////////////////

void*
DirectAllocateMemory(size_t aSize, AllocatedMemoryKind aKind)
{
  void* res = OriginalCall(VirtualAllocEx, LPVOID,
                           OriginalCall(GetCurrentProcess, HANDLE),
                           nullptr, aSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  MOZ_RELEASE_ASSERT(res);
  if (IsReplaying() && aKind != AllocatedMemoryKind::Untracked) {
    ReplayRegisterAllocatedMemory(res, aSize,
                                  aKind == AllocatedMemoryKind::TrackedMemoryLockHeld);
  }
  return res;
}

void
DirectWriteProtectMemory(void* aAddress, size_t aSize)
{
  DWORD oldProtect;
  BOOL res = OriginalCall(VirtualProtectEx, BOOL,
                          OriginalCall(GetCurrentProcess, HANDLE),
                          aAddress, aSize, PAGE_EXECUTE_READ, &oldProtect);
  MOZ_RELEASE_ASSERT(res);
}

void
DirectUnprotectMemory(void* aAddress, size_t aSize)
{
  DWORD oldProtect;
  BOOL res = OriginalCall(VirtualProtectEx, BOOL,
                          OriginalCall(GetCurrentProcess, HANDLE),
                          aAddress, aSize, PAGE_EXECUTE_READWRITE, &oldProtect);
  MOZ_RELEASE_ASSERT(res);
}

void
DirectDeallocateMemory(void* aAddress, size_t aSize, AllocatedMemoryKind aKind)
{
  if (IsReplaying() && aKind != AllocatedMemoryKind::Untracked) {
    ReplayDeallocateMemory(aAddress, aSize,
                           aKind == AllocatedMemoryKind::TrackedMemoryLockHeld);
  } else {
    // The size parameter must be zero when using MEM_RELEASE.
    BOOL res = OriginalCall(VirtualFreeEx, BOOL,
                            OriginalCall(GetCurrentProcess, HANDLE),
                            aAddress, 0, MEM_RELEASE);
    MOZ_RELEASE_ASSERT(res);
  }
}

FileHandle
DirectOpenFile(const char* aFilename, bool aWriting)
{
  // CreateFileA calls into CreateFileW, so call that function directly so that
  // we don't need to make sure events are passed through here.
  wchar_t buf[256];
  size_t len = strlen(aFilename);
  if (len >= sizeof(buf) / sizeof(buf[0])) {
    MOZ_CRASH();
  }
  for (size_t i = 0; i <= len; i++) {
    buf[i] = aFilename[i];
  }

  HANDLE res = OriginalCall(CreateFileW, HANDLE,
                            buf, aWriting ? GENERIC_WRITE : GENERIC_READ, 0, nullptr,
                            aWriting ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  MOZ_RELEASE_ASSERT(res && res != INVALID_HANDLE_VALUE);
  return (FileHandle) res;
}

void
DirectSeekFile(FileHandle aFd, ptrdiff_t aOffset)
{
  BOOL res = OriginalCall(SetFilePointerEx, BOOL,
                          (HANDLE) aFd, aOffset, nullptr, FILE_BEGIN);
  MOZ_RELEASE_ASSERT(res);
}

void
DirectCloseFile(FileHandle aFd)
{
  BOOL res = OriginalCall(CloseHandle, BOOL, (HANDLE) aFd);
  MOZ_RELEASE_ASSERT(res);
}

void
DirectWrite(FileHandle aFd, const void* aData, size_t aSize)
{
  DWORD nwritten;
  BOOL res = OriginalCall(WriteFile, BOOL,
                          (HANDLE) aFd, aData, aSize, &nwritten, nullptr);
  MOZ_RELEASE_ASSERT(res);
  MOZ_RELEASE_ASSERT(nwritten == aSize);
}

size_t
DirectRead(FileHandle aFd, void* aData, size_t aSize)
{
  DWORD nread;
  BOOL res = OriginalCall(ReadFile, BOOL,
                          (HANDLE) aFd, aData, aSize, &nread, nullptr);
  MOZ_RELEASE_ASSERT(res);
  MOZ_RELEASE_ASSERT(nread <= aSize);
  return nread;
}

void
DirectCreatePipe(FileHandle* aWriteFd, FileHandle* aReadFd)
{
  HANDLE readHandle, writeHandle;
  BOOL res = OriginalCall(CreatePipe, BOOL,
                          &readHandle, &writeHandle, nullptr, 0);
  MOZ_RELEASE_ASSERT(res);
  *aWriteFd = (FileHandle) writeHandle;
  *aReadFd = (FileHandle) readHandle;
}

#define MAKE_DLL_REDIRECTION_ENTRY(aDll, aName) \
  { #aName, #aDll ".dll", nullptr, (PRUint8*) RR_ ##aName },
#define MAKE_KERNEL32_REDIRECTION_ENTRY(aName) \
  MAKE_DLL_REDIRECTION_ENTRY(kernel32, aName)
#define MAKE_SHELL32_REDIRECTION_ENTRY(aName) \
  MAKE_DLL_REDIRECTION_ENTRY(shell32, aName)
#define MAKE_USER32_REDIRECTION_ENTRY(aName) \
  MAKE_DLL_REDIRECTION_ENTRY(user32, aName)

Redirection gRedirections[CallEvent_Count] = {
  FOR_EACH_KERNEL32_REDIRECTION(MAKE_KERNEL32_REDIRECTION_ENTRY)
  FOR_EACH_SHELL32_REDIRECTION(MAKE_SHELL32_REDIRECTION_ENTRY)
  FOR_EACH_USER32_REDIRECTION(MAKE_USER32_REDIRECTION_ENTRY)
  FOR_EACH_DLL_REDIRECTION(MAKE_DLL_REDIRECTION_ENTRY)
};

#undef MAKE_DLL_REDIRECTION_ENTRY
#undef MAKE_KERNEL32_REDIRECTION_ENTRY
#undef MAKE_SHELL32_REDIRECTION_ENTRY
#undef MAKE_USER32_REDIRECTION_ENTRY

} // namespace recordreplay
} // namespace mozilla
