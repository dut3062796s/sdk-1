// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"
#if defined(TARGET_OS_WINDOWS)

#include "bin/platform.h"

#include <crtdbg.h>

#include "bin/file.h"
#include "bin/lockers.h"
#include "bin/log.h"
#if !defined(DART_IO_DISABLED) && !defined(PLATFORM_DISABLE_SOCKET)
#include "bin/socket.h"
#endif
#include "bin/thread.h"
#include "bin/utils.h"
#include "bin/utils_win.h"

// These are not always defined in the header files. See:
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms686033(v=vs.85).aspx
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace dart {

// Defined in vm/os_thread_win.cc
extern bool private_flag_windows_run_tls_destructors;

namespace bin {

const char* Platform::executable_name_ = NULL;
char* Platform::resolved_executable_name_ = NULL;
int Platform::script_index_ = 1;
char** Platform::argv_ = NULL;

class PlatformWin {
 public:
  static void InitOnce() {
    platform_win_mutex_ = new Mutex();
    saved_output_cp_ = -1;
    saved_input_cp_ = -1;
    // Set up a no-op handler so that CRT functions return an error instead of
    // hitting an assertion failure.
    // See: https://msdn.microsoft.com/en-us/library/a9yf33zb.aspx
    _set_invalid_parameter_handler(InvalidParameterHandler);
    // Disable the message box for assertions in the CRT in Debug builds.
    // See: https://msdn.microsoft.com/en-us/library/1y71x448.aspx
    _CrtSetReportMode(_CRT_ASSERT, 0);
    // Disable dialog boxes for "critical" errors or when OpenFile cannot find
    // the requested file. See:
    // See: https://msdn.microsoft.com/en-us/library/windows/desktop/ms680621(v=vs.85).aspx
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  }

  static void SaveAndConfigureConsole() {
    MutexLocker ml(platform_win_mutex_);
    // Set both the input and output code pages to UTF8.
    ASSERT(saved_output_cp_ == -1);
    ASSERT(saved_input_cp_ == -1);
    saved_output_cp_ = GetConsoleOutputCP();
    saved_input_cp_ = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    ansi_supported_ = true;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD out_mode;
    if ((out != INVALID_HANDLE_VALUE) && GetConsoleMode(out, &out_mode)) {
      const DWORD request = out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      ansi_supported_ = ansi_supported_ && SetConsoleMode(out, request);
    } else {
      ansi_supported_ = false;
    }

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD in_mode;
    if ((in != INVALID_HANDLE_VALUE) && GetConsoleMode(in, &in_mode)) {
      const DWORD request = in_mode | ENABLE_VIRTUAL_TERMINAL_INPUT;
      ansi_supported_ = ansi_supported_ && SetConsoleMode(in, request);
    } else {
      ansi_supported_ = false;
    }
  }

  static void RestoreConsole() {
    MutexLocker ml(platform_win_mutex_);

    // STD_OUTPUT_HANDLE and STD_INPUT_HANDLE may have been closed or
    // redirected. Therefore, we explicitly open the CONOUT$ and CONIN$
    // devices, so that we can be sure that we are really unsetting
    // ENABLE_VIRTUAL_TERMINAL_PROCESSING and ENABLE_VIRTUAL_TERMINAL_INPUT
    // respectively.
    HANDLE out;
    {
      Utf8ToWideScope conin("CONOUT$");
      out = CreateFileW(conin.wide(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
      if (out != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, out);
      }
    }
    DWORD out_mode;
    if ((out != INVALID_HANDLE_VALUE) && GetConsoleMode(out, &out_mode)) {
      DWORD request = out_mode & ~ENABLE_VIRTUAL_TERMINAL_INPUT;
      SetConsoleMode(out, request);
    }

    HANDLE in;
    {
      Utf8ToWideScope conin("CONIN$");
      in = CreateFileW(conin.wide(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
      if (in != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_INPUT_HANDLE, in);
      }
    }
    DWORD in_mode;
    if ((in != INVALID_HANDLE_VALUE) && GetConsoleMode(in, &in_mode)) {
      DWORD request = in_mode & ~ENABLE_VIRTUAL_TERMINAL_INPUT;
      SetConsoleMode(in, request);
    }

    if (saved_output_cp_ != -1) {
      SetConsoleOutputCP(saved_output_cp_);
      saved_output_cp_ = -1;
    }
    if (saved_input_cp_ != -1) {
      SetConsoleCP(saved_input_cp_);
      saved_input_cp_ = -1;
    }
  }

  static bool ansi_supported() { return ansi_supported_; }

 private:
  static Mutex* platform_win_mutex_;
  static int saved_output_cp_;
  static int saved_input_cp_;
  static bool ansi_supported_;

  static void InvalidParameterHandler(const wchar_t* expression,
                                      const wchar_t* function,
                                      const wchar_t* file,
                                      unsigned int line,
                                      uintptr_t reserved) {
    // Doing nothing here means that the CRT call that invoked it will
    // return an error code and/or set errno.
  }

  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(PlatformWin);
};

int PlatformWin::saved_output_cp_ = -1;
int PlatformWin::saved_input_cp_ = -1;
Mutex* PlatformWin::platform_win_mutex_ = NULL;
bool PlatformWin::ansi_supported_ = false;

bool Platform::Initialize() {
  PlatformWin::InitOnce();
  PlatformWin::SaveAndConfigureConsole();
  return true;
}


int Platform::NumberOfProcessors() {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors;
}


const char* Platform::OperatingSystem() {
  return "windows";
}


const char* Platform::LibraryPrefix() {
  return "";
}


const char* Platform::LibraryExtension() {
  return "dll";
}


bool Platform::LocalHostname(char* buffer, intptr_t buffer_length) {
#if defined(DART_IO_DISABLED) || defined(PLATFORM_DISABLE_SOCKET)
  return false;
#else
  if (!Socket::Initialize()) {
    return false;
  }
  return gethostname(buffer, buffer_length) == 0;
#endif
}


char** Platform::Environment(intptr_t* count) {
  wchar_t* strings = GetEnvironmentStringsW();
  if (strings == NULL) {
    return NULL;
  }
  wchar_t* tmp = strings;
  intptr_t i = 0;
  while (*tmp != '\0') {
    // Skip environment strings starting with "=".
    // These are synthetic variables corresponding to dynamic environment
    // variables like %=C:% and %=ExitCode%, and the Dart environment does
    // not include these.
    if (*tmp != '=') {
      i++;
    }
    tmp += (wcslen(tmp) + 1);
  }
  *count = i;
  char** result;
  result = reinterpret_cast<char**>(Dart_ScopeAllocate(i * sizeof(*result)));
  tmp = strings;
  for (intptr_t current = 0; current < i;) {
    // Skip the strings that were not counted above.
    if (*tmp != '=') {
      result[current++] = StringUtilsWin::WideToUtf8(tmp);
    }
    tmp += (wcslen(tmp) + 1);
  }
  FreeEnvironmentStringsW(strings);
  return result;
}


const char* Platform::ResolveExecutablePath() {
  // GetModuleFileNameW cannot directly provide information on the
  // required buffer size, so start out with a buffer large enough to
  // hold any Windows path.
  const int kTmpBufferSize = 32768;
  wchar_t* tmp_buffer =
      reinterpret_cast<wchar_t*>(Dart_ScopeAllocate(kTmpBufferSize));
  // Ensure no last error before calling GetModuleFileNameW.
  SetLastError(ERROR_SUCCESS);
  // Get the required length of the buffer.
  int path_length = GetModuleFileNameW(NULL, tmp_buffer, kTmpBufferSize);
  if (GetLastError() != ERROR_SUCCESS) {
    return NULL;
  }
  char* path = StringUtilsWin::WideToUtf8(tmp_buffer);
  // Return the canonical path as the returned path might contain symlinks.
  const char* canon_path = File::GetCanonicalPath(path);
  return canon_path;
}


bool Platform::AnsiSupported() {
  return PlatformWin::ansi_supported();
}


void Platform::Exit(int exit_code) {
  // TODO(zra): Remove once VM shuts down cleanly.
  ::dart::private_flag_windows_run_tls_destructors = false;
  // Restore the console's output code page
  PlatformWin::RestoreConsole();
  // On Windows we use ExitProcess so that threads can't clobber the exit_code.
  // See: https://code.google.com/p/nativeclient/issues/detail?id=2870
  ::ExitProcess(exit_code);
}

}  // namespace bin
}  // namespace dart

#endif  // defined(TARGET_OS_WINDOWS)
