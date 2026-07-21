// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT

// Standalone launcher for the DIME settings dialog. Loads the sibling IME DLL
// (dime64.dll / dime32.dll / dime.dll) and calls DimeLaunchConfigDialog so the
// dialog uses that DLL's install directory for dict\ and registry settings.

#include <windows.h>
#include <strsafe.h>

typedef HRESULT (WINAPI *PFN_DimeLaunchConfigDialog)(HWND);

static BOOL _AppendFileName(_Inout_updates_(cch) WCHAR* path, DWORD cch, _In_z_ LPCWSTR fileName)
{
    WCHAR* slash = wcsrchr(path, L'\\');
    if (slash == nullptr)
    {
        return FALSE;
    }
    slash[1] = L'\0';
    return SUCCEEDED(StringCchCatW(path, cch, fileName));
}

static HMODULE _LoadSiblingDimeDll(_Out_writes_(cchDll) WCHAR* dllPath, DWORD cchDll)
{
    if (!GetModuleFileNameW(nullptr, dllPath, cchDll))
    {
        return nullptr;
    }

#ifdef _WIN64
    const WCHAR* candidates[] = { L"dime64.dll", L"dime.dll" };
#else
    const WCHAR* candidates[] = { L"dime32.dll", L"dime.dll" };
#endif

    for (int i = 0; i < _countof(candidates); ++i)
    {
        WCHAR tryPath[MAX_PATH] = {L'\0'};
        if (FAILED(StringCchCopyW(tryPath, ARRAYSIZE(tryPath), dllPath)))
        {
            continue;
        }
        if (!_AppendFileName(tryPath, ARRAYSIZE(tryPath), candidates[i]))
        {
            continue;
        }
        HMODULE h = LoadLibraryW(tryPath);
        if (h != nullptr)
        {
            StringCchCopyW(dllPath, cchDll, tryPath);
            return h;
        }
    }
    return nullptr;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    hInstance;

    // Match host-process DPI behavior (status-bar / candidate right-click).
    // Without this, Windows bitmap-scales the whole UI on high-DPI displays
    // and the settings window looks larger and blurry.
    {
        HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
        if (hUser32 != nullptr)
        {
            typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
            PFN_SetProcessDpiAwarenessContext pfn =
                reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
                    GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
            if (pfn != nullptr)
            {
                // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
                pfn(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
            }
        }
    }

    WCHAR dllPath[MAX_PATH] = {L'\0'};
    HMODULE hDll = _LoadSiblingDimeDll(dllPath, ARRAYSIZE(dllPath));
    if (hDll == nullptr)
    {
        MessageBoxW(nullptr,
            L"找不到同目录下的 dime64.dll / dime32.dll / dime.dll。\n请从 DIME 安装目录运行本程序。",
            L"迪弥五笔设置",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    PFN_DimeLaunchConfigDialog pfn =
        reinterpret_cast<PFN_DimeLaunchConfigDialog>(GetProcAddress(hDll, "DimeLaunchConfigDialog"));
    if (pfn == nullptr)
    {
        MessageBoxW(nullptr,
            L"DLL 中缺少 DimeLaunchConfigDialog 导出。\n请确认已安装匹配版本的 DIME。",
            L"迪弥五笔设置",
            MB_OK | MB_ICONERROR);
        FreeLibrary(hDll);
        return 1;
    }

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT hr = pfn(nullptr);
    if (SUCCEEDED(hrCo))
    {
        CoUninitialize();
    }

    FreeLibrary(hDll);

    if (FAILED(hr))
    {
        WCHAR msg[256] = {L'\0'};
        StringCchPrintfW(msg, ARRAYSIZE(msg), L"无法打开设置面板。\n错误代码: 0x%08X", hr);
        MessageBoxW(nullptr, msg, L"迪弥五笔设置", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
