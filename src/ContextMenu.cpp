// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "ContextMenu.h"

// A tiny, hidden, no-activate owner window used solely to host the popup menu.
//
// TrackPopupMenu needs an owner whose foreground state we control. For a
// no-activate window SetForegroundWindow fails gracefully, and the
// PostMessage(WM_NULL) below then makes the menu release/dismiss reliably --
// even when the menu is launched from an activatable window such as the
// candidate window. Without this, calling SetForegroundWindow on the
// activatable candidate window would steal foreground from the host edit
// control (TSF then restores it), and the popup menu could get stuck on
// screen. Using a dedicated no-activate owner keeps the candidate window's own
// window style unchanged so input keeps working.
static HWND DimeGetMenuOwner()
{
    static HWND s_hwnd = nullptr;
    if (s_hwnd != nullptr && IsWindow(s_hwnd))
    {
        return s_hwnd;
    }

    WNDCLASS wc = {0};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DimeMenuOwnerWindow";
    ATOM atom = RegisterClass(&wc);
    if (atom == 0)
    {
        return nullptr;
    }

    s_hwnd = CreateWindowEx(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        MAKEINTRESOURCE(atom), nullptr, WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    return s_hwnd;
}

DIME_MENU_CMD DimeShowImeContextMenu(_In_ HWND hwnd, _In_ POINT screenPt, BOOL isStatusVisible, _In_opt_ const RECT* pAnchorRect)
{
    HMENU hMenu = CreatePopupMenu();
    if (hMenu == nullptr)
    {
        return DIME_CMD_NONE;
    }

    // The two items are identical for every DIME floating window by design.
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, DIME_CMD_SETTINGS, L"功能设置");
    // Label the toggle item from the current visibility of the status bar.
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING, DIME_CMD_TOGGLE_STATUSBAR,
        isStatusVisible ? L"隐藏状态栏" : L"显示状态栏");

    int x = screenPt.x;
    int y = screenPt.y;
    if (pAnchorRect != nullptr)
    {
        // Pop upward from the top edge of the anchor rectangle so the menu
        // never overlaps the bar it was launched from.
        int itemH = GetSystemMetrics(SM_CYMENU);
        int count = GetMenuItemCount(hMenu);
        int border = GetSystemMetrics(SM_CYEDGE);
        int menuH = count * itemH + 2 * border;
        x = pAnchorRect->left;
        y = pAnchorRect->top - menuH;
        if (y < 0)
        {
            y = pAnchorRect->top;   // not enough room above: fall back below
        }
    }

    // Host the menu on a no-activate owner window so it always dismisses
    // correctly regardless of which floating window launched it.
    HWND owner = DimeGetMenuOwner();
    if (owner == nullptr)
    {
        owner = hwnd;
    }

    // Recommended pattern for a no-activate floating window: bring the owner to
    // the foreground so the menu does not dismiss immediately, then post a NULL
    // message afterwards to restore the input state. For a no-activate owner
    // SetForegroundWindow no-ops, which is exactly what we want.
    SetForegroundWindow(owner);
    UINT cmd = TrackPopupMenu(hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        x, y, 0, owner, nullptr);
    PostMessage(owner, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
    return static_cast<DIME_MENU_CMD>(cmd);
}
