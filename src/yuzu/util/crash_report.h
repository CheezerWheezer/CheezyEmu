// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Windows crash report: when CheezyEmu crashes, eden_log.txt gets the exception and the module and
// offset it happened at (match the offset with the release's cheezyemu-*-map.zip), and a minidump
// goes to <data>/crash_dumps. Without it a crash just ended the log mid-line.

#pragma once

#ifdef _WIN32

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include <fmt/format.h>

#include "common/fs/path_util.h"
#include "common/logging.h"

namespace CrashReport {

namespace detail {

inline std::wstring& DumpDir() {
    static std::wstring dir;
    return dir;
}

inline std::string ModuleAt(const void* address, uintptr_t& offset) {
    HMODULE module{};
    offset = reinterpret_cast<uintptr_t>(address);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        module == nullptr) {
        return "?";
    }
    offset -= reinterpret_cast<uintptr_t>(module);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).filename().string();
}

inline std::wstring WriteDump(EXCEPTION_POINTERS* info) {
    using WriteFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                  PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                  PMINIDUMP_CALLBACK_INFORMATION);
    const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    const auto write =
        dbghelp ? reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write || DumpDir().empty()) {
        return {};
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96];
    swprintf_s(name, L"\\cheezyemu_%04u%02u%02u_%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond);
    const std::wstring path = DumpDir() + name;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo |
                                                     MiniDumpWithIndirectlyReferencedMemory |
                                                     MiniDumpWithUnloadedModules),
                          &exception, nullptr, nullptr);
    CloseHandle(file);
    return ok ? path : std::wstring{};
}

inline LONG WINAPI Filter(EXCEPTION_POINTERS* info) {
    static std::atomic_bool handled{};
    if (handled.exchange(true) || info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    // The dump first: logging can't be trusted if the crash was inside the logger.
    const std::wstring dump = WriteDump(info);
    uintptr_t offset{};
    const std::string module = ModuleAt(record.ExceptionAddress, offset);
    std::string access;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        const auto kind = record.ExceptionInformation[0];
        access = fmt::format(" ({} of {:#x})",
                             kind == 0 ? "read" : kind == 1 ? "write" : "execute",
                             record.ExceptionInformation[1]);
    }
    LOG_CRITICAL(Frontend,
                 "CheezyEmu crashed: exception {:#010x}{} at {}+{:#x} (thread {}). Crash dump: {}",
                 static_cast<u32>(record.ExceptionCode), access, module, offset,
                 GetCurrentThreadId(),
                 dump.empty() ? std::string{"not written"}
                              : std::filesystem::path(dump).string());
    Common::Log::Stop(); // flush
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace detail

/// Call once at startup.
inline void Install() {
    std::error_code ec;
    const auto dir = Common::FS::GetEdenPath(Common::FS::EdenPath::CrashDumpsDir);
    std::filesystem::create_directories(dir, ec);
    detail::DumpDir() = dir.wstring();
    SetUnhandledExceptionFilter(detail::Filter);
}

} // namespace CrashReport

#else

namespace CrashReport {
inline void Install() {}
} // namespace CrashReport

#endif
