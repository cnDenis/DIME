// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "Globals.h"
#include "DebugLog.h"
#include <new>

static CRITICAL_SECTION s_logCS;
static BOOL s_logCSReady = FALSE;
static WCHAR s_logPath[MAX_PATH] = {0};
static BOOL s_logPathReady = FALSE;

static void EnsureLogInfrastructure()
{
    if (!s_logCSReady)
    {
        InitializeCriticalSection(&s_logCS);
        s_logCSReady = TRUE;
    }

    if (s_logPathReady || Global::dllInstanceHandle == nullptr)
    {
        return;
    }

    WCHAR dllPath[MAX_PATH] = {0};
    if (GetModuleFileNameW(Global::dllInstanceHandle, dllPath, ARRAYSIZE(dllPath)) == 0)
    {
        return;
    }

    WCHAR* lastSlash = wcsrchr(dllPath, L'\\');
    if (lastSlash != nullptr)
    {
        *(lastSlash + 1) = L'\0';
    }
    else
    {
        dllPath[0] = L'\0';
    }

    if (FAILED(StringCchPrintfW(s_logPath, ARRAYSIZE(s_logPath), L"%sdime_debug.log", dllPath)))
    {
        return;
    }

    s_logPathReady = TRUE;
}

// Minimum level actually emitted. Release builds raise the bar to WARNING so
// DEBUG/INFO are dropped even if such a call slips through; Debug logs all.
#ifdef NDEBUG
static const DIME_LOG_LEVEL s_minLogLevel = DIME_LOG_LEVEL_WARNING;
#else
static const DIME_LOG_LEVEL s_minLogLevel = DIME_LOG_LEVEL_DEBUG;
#endif

static PCWSTR LogLevelTag(_In_ DIME_LOG_LEVEL level)
{
    switch (level)
    {
    case DIME_LOG_LEVEL_INFO:    return L"INFO";
    case DIME_LOG_LEVEL_WARNING: return L"WARNING";
    case DIME_LOG_LEVEL_ERROR:   return L"ERROR";
    case DIME_LOG_LEVEL_DEBUG:
    default:                     return L"DEBUG";
    }
}

void DimeLog(_In_ DIME_LOG_LEVEL level, _In_ PCWSTR location, _In_ PCWSTR format, ...)
{
    if (location == nullptr || format == nullptr)
    {
        return;
    }

    if (level < s_minLogLevel)
    {
        return;
    }

    EnsureLogInfrastructure();
    if (!s_logPathReady)
    {
        return;
    }

    EnterCriticalSection(&s_logCS);

    WCHAR message[1024] = {0};
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(message, ARRAYSIZE(message), format, args);
    va_end(args);

    SYSTEMTIME st = {0};
    GetLocalTime(&st);

    WCHAR line[1400] = {0};
    StringCchPrintfW(
        line,
        ARRAYSIZE(line),
        L"[%s] %04u-%02u-%02u %02u:%02u:%02u.%03u | %s | %s\r\n",
        LogLevelTag(level),
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds,
        location,
        message);

    OutputDebugStringW(line);

    HANDLE hFile = CreateFileW(
        s_logPath,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 1)
        {
            char* utf8 = new (std::nothrow) char[utf8Len];
            if (utf8 != nullptr)
            {
                WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, utf8Len, nullptr, nullptr);
                DWORD written = 0;
                SetFilePointer(hFile, 0, nullptr, FILE_END);
                WriteFile(hFile, utf8, static_cast<DWORD>(utf8Len - 1), &written, nullptr);
                delete[] utf8;
            }
        }
        CloseHandle(hFile);
    }

    LeaveCriticalSection(&s_logCS);
}
