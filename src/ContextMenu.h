// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

#include <windows.h>

// Shared right-click context menu for the DIME floating UI. Both the status bar
// and the candidate window show the exact same menu (two items), so the menu
// construction lives in one place and each window only handles the result.
enum DIME_MENU_CMD
{
    DIME_CMD_NONE = 0,
    DIME_CMD_SETTINGS,          // 功能设置 (placeholder for now)
    DIME_CMD_TOGGLE_STATUSBAR   // 显示/隐藏状态栏 (label follows current state)
};

// Pops the shared context menu and returns the command the user picked.
// isStatusVisible tells the menu whether the floating status bar is currently
// shown, so the toggle item reads "隐藏状态栏" or "显示状态栏".
// When pAnchorRect is non-NULL the menu is placed just above that rectangle
// (growing upward) so it never overlaps the status bar; otherwise it pops at
// the given screen point.
DIME_MENU_CMD DimeShowImeContextMenu(_In_ HWND hwnd, _In_ POINT screenPt, BOOL isStatusVisible, _In_opt_ const RECT* pAnchorRect = nullptr);
