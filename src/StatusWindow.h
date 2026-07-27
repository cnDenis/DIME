// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "define.h"

// Callback fired when the user clicks one of the clickable status segments.
typedef void (*STATUSWNDCALLBACK)(void *pv, int item);

// The three clickable segments of the floating status bar.
enum STATUS_ITEM
{
    STATUS_ITEM_FULL_HALF = 0,   // 全/半角 (full / half width)
    STATUS_ITEM_PUNCTUATION,     // 中/英标点 (Chinese / English punctuation)
    STATUS_ITEM_ONLY_COMMON,     // 常/全 (common-only / all characters)
    STATUS_ITEM_COUNT
};

//+---------------------------------------------------------------------------
//
// CStatusWindow
//
//  A small, focus-less, always-on-top floating bar showing the DIME icon and
//  the three input-mode states. The latter three segments are clickable and
//  toggle their respective TSF compartments through the supplied callback.
//
//----------------------------------------------------------------------------

class CStatusWindow : public CBaseWindow
{
public:
    CStatusWindow(_In_ STATUSWNDCALLBACK pfnCallback, _In_ void *pv);
    virtual ~CStatusWindow();

    BOOL _Create(ATOM atom, _In_opt_ HWND parentWndHandle);
    void _SetStates(BOOL isFullWidth, BOOL isChinesePunctuation, BOOL isOnlyCommon);
    void _SetGrayed(BOOL isGrayed);
    void _RestorePosition();

    virtual LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    virtual void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    virtual void _OnLButtonDown(POINT pt);
    virtual void _OnLButtonUp(POINT pt);
    void _OnRButtonDown(POINT pt);
    void _SetHiddenByUser(BOOL hidden);
    BOOL _IsHiddenByUser() const { return _isHiddenByUser; }
    void _LoadHiddenState();
    virtual void _OnMouseMove(POINT pt);

private:
    void _ComputeLayout(_In_ HDC dcHandle);
    int  _HitTest(POINT pt) const;
    void _DrawItem(_In_ HDC dcHandle, int item, _In_opt_ HICON hIcon, _In_opt_ LPCWSTR pszText, _In_ RECT *prc);

    // Position persistence (HKCU\Software\DIME).
    void _LoadPosition(_Out_ int &x, _Out_ int &y);
    void _SavePosition();
    BOOL _IsOnAnyMonitor(int x, int y, int w, int h) const;
    void _SnapToEdges(_Inout_ int &x, _Inout_ int &y, int w, int h);
    void _ClampToWorkArea(_Inout_ int &x, _Inout_ int &y, int w, int h);
    void _ApplyRoundedCorners();

    STATUSWNDCALLBACK _pfnCallback;
    void *_pv;

    BOOL _isFullWidth;
    BOOL _isChinesePunctuation;
    BOOL _isOnlyCommon;
    BOOL _isGrayed;

    int _iconSize;
    int _pad;
    int _gap;
    int _radius;

    RECT _rcIcon;
    RECT _rcItems[STATUS_ITEM_COUNT];
    int  _hoverItem;

    BOOL _isHiddenByUser = FALSE;   // user chose to hide via the context menu

    BOOL _isDragging;
    POINT _dragOffset;

    HICON _hDimeIcon;
    HICON _hFullWidthOn;
    HICON _hFullWidthOff;
    HICON _hPunctOn;
    HICON _hPunctOff;
};
