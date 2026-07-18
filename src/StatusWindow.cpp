// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "Globals.h"
#include "StatusWindow.h"
#include "RegKey.h"
#include "Define.h"
#include "ContextMenu.h"
#include "ConfigDialog.h"

//+---------------------------------------------------------------------------
//
// CStatusWindow::ctor
//
//----------------------------------------------------------------------------

CStatusWindow::CStatusWindow(_In_ STATUSWNDCALLBACK pfnCallback, _In_ void *pv)
    : _pfnCallback(pfnCallback),
      _pv(pv),
      _isFullWidth(FALSE),
      _isChinesePunctuation(FALSE),
      _isOnlyCommon(FALSE),
      _isGrayed(FALSE),
      _iconSize(16),
      _pad(4),
      _gap(3),
      _radius(5),
      _hoverItem(-1),
      _isDragging(FALSE),
      _hDimeIcon(nullptr),
      _hFullWidthOn(nullptr),
      _hFullWidthOff(nullptr),
      _hPunctOn(nullptr),
      _hPunctOff(nullptr)
{
    ZeroMemory(&_rcIcon, sizeof(_rcIcon));
    for (int i = 0; i < STATUS_ITEM_COUNT; ++i)
    {
        ZeroMemory(&_rcItems[i], sizeof(_rcItems[i]));
    }
    _dragOffset.x = 0;
    _dragOffset.y = 0;
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::dtor
//
//----------------------------------------------------------------------------

CStatusWindow::~CStatusWindow()
{
    if (_hDimeIcon != nullptr) { DestroyIcon(_hDimeIcon); _hDimeIcon = nullptr; }
    if (_hFullWidthOn != nullptr) { DestroyIcon(_hFullWidthOn); _hFullWidthOn = nullptr; }
    if (_hFullWidthOff != nullptr) { DestroyIcon(_hFullWidthOff); _hFullWidthOff = nullptr; }
    if (_hPunctOn != nullptr) { DestroyIcon(_hPunctOn); _hPunctOn = nullptr; }
    if (_hPunctOff != nullptr) { DestroyIcon(_hPunctOff); _hPunctOff = nullptr; }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_Create
//
//----------------------------------------------------------------------------

// Scale chrome to the monitor DPI, using 125% (DPI 120) as the reference so the
// current good-looking size is preserved and other scalings stay proportional.
static UINT DimeGetWindowDpi(HWND)
{
    HDC dc = GetDC(NULL);
    UINT dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return dpi ? dpi : 96;
}

// Icons pick a preferred pixel size per display-scaling tier (the same DPI
// tiers as the candidate font), never below 16px so they stay legible.
static int DimeSelectIconPixelHeight(UINT dpi)
{
    if (dpi <= 156) return 16;   // 100% / 125% / 150%
    if (dpi <= 180) return 20;   // 175%
    if (dpi <= 216) return 24;   // 200%
    return 32;                   // 250% and above
}

// Same segmented font-size selector as the candidate window, so the status
// bar's padding / radius scale in the same tiers as the text.
static int DimeSelectFontPixelHeight(UINT dpi)
{
    if (dpi <= 108) return 14;   // 100%
    if (dpi <= 156) return 16;   // 125% and 150%
    if (dpi <= 180) return 20;   // 175%
    if (dpi <= 216) return 24;   // 200%
    return 32;                   // 250% and above
}

// Build the status window's rounded-rectangle region (all four corners rounded).
static HRGN MakeStatusRgn(int w, int h, int r)
{
    return CreateRoundRectRgn(0, 0, w, h, 2 * r, 2 * r);
}

BOOL CStatusWindow::_Create(ATOM atom, _In_opt_ HWND parentWndHandle)
{
    if (!CBaseWindow::_Create(atom,
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            WS_POPUP,
            NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }

    // Icon keeps its own segmented size (min 16px). The surrounding chrome
    // (padding / gap / corner radius) is derived from the segmented font size
    // so the whole status bar scales in clean tiers with the text. At the 125%
    // reference (font 16px) this reproduces the original 4/3/5 metrics.
    UINT dpi = DimeGetWindowDpi(_GetWnd());
    _iconSize = DimeSelectIconPixelHeight(dpi);
    int fontPx = DimeSelectFontPixelHeight(dpi);
    _pad      = fontPx / 4;
    _gap      = fontPx / 5;
    _radius   = fontPx / 3;

    _hDimeIcon = (HICON)LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(IDIS_DIME),
        IMAGE_ICON,
        _iconSize, _iconSize,
        LR_DEFAULTCOLOR);

    _hFullWidthOn = (HICON)LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(IDI_DOUBLE_SINGLE_BYTE_ON),
        IMAGE_ICON, _iconSize, _iconSize, LR_DEFAULTCOLOR);
    _hFullWidthOff = (HICON)LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(IDI_DOUBLE_SINGLE_BYTE_OFF),
        IMAGE_ICON, _iconSize, _iconSize, LR_DEFAULTCOLOR);
    _hPunctOn = (HICON)LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(IDI_PUNCTUATION_ON),
        IMAGE_ICON, _iconSize, _iconSize, LR_DEFAULTCOLOR);
    _hPunctOff = (HICON)LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(IDI_PUNCTUATION_OFF),
        IMAGE_ICON, _iconSize, _iconSize, LR_DEFAULTCOLOR);

    HDC hdc = GetDC(_GetWnd());
    if (hdc != nullptr)
    {
        HFONT hOld = (HFONT)SelectObject(hdc,
            Global::defaultlFontHandle ? Global::defaultlFontHandle : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        _ComputeLayout(hdc);
        SelectObject(hdc, hOld);
        ReleaseDC(_GetWnd(), hdc);
    }

    int w = _rcItems[STATUS_ITEM_COUNT - 1].right + _pad;
    int h = _iconSize + 2 * _pad;
    _Resize(0, 0, w, h, FALSE);

    // Clip the window to a rounded rectangle so the corners are truly round
    // (the region excludes the corner pixels, letting the desktop show).
    _ApplyRoundedCorners();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_ComputeLayout
//
//----------------------------------------------------------------------------

void CStatusWindow::_ComputeLayout(_In_ HDC dcHandle)
{
    dcHandle;

    int x = _pad;
    int y = _pad;

    _rcIcon.left = x;
    _rcIcon.top = y;
    _rcIcon.right = x + _iconSize;
    _rcIcon.bottom = y + _iconSize;

    x = _rcIcon.right + _gap;
    for (int i = 0; i < STATUS_ITEM_COUNT; ++i)
    {
        _rcItems[i].left = x;
        _rcItems[i].top = y;
        _rcItems[i].right = x + _iconSize;
        _rcItems[i].bottom = y + _iconSize;
        x += _iconSize + _gap;
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_SetStates
//
//----------------------------------------------------------------------------

void CStatusWindow::_SetStates(BOOL isFullWidth, BOOL isChinesePunctuation, BOOL isOnlyCommon)
{
    BOOL changed = (_isFullWidth != isFullWidth) ||
                   (_isChinesePunctuation != isChinesePunctuation) ||
                   (_isOnlyCommon != isOnlyCommon);
    _isFullWidth = isFullWidth;
    _isChinesePunctuation = isChinesePunctuation;
    _isOnlyCommon = isOnlyCommon;
    if (changed)
    {
        _RepaintNow();
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_SetGrayed
//
//  When the IME is off (English mode, e.g. after Ctrl+Space), the bar stays
//  visible but every icon is drawn grayed out as a visual "inactive" hint.
//
//----------------------------------------------------------------------------

void CStatusWindow::_SetGrayed(BOOL isGrayed)
{
    if (_isGrayed != isGrayed)
    {
        _isGrayed = isGrayed;
        _RepaintNow();
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_IsOnAnyMonitor
//
//----------------------------------------------------------------------------

BOOL CStatusWindow::_IsOnAnyMonitor(int x, int y, int w, int h) const
{
    RECT rc = {x, y, x + w, y + h};
    return MonitorFromRect(&rc, MONITOR_DEFAULTTONULL) != nullptr;
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_LoadPosition / _SavePosition
//
//  Persist the bar's top-left position in HKCU\Software\DIME so it survives
//  restart / IME re-activation.
//
//----------------------------------------------------------------------------

void CStatusWindow::_LoadPosition(_Out_ int &x, _Out_ int &y)
{
    x = -1;
    y = -1;

    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        DWORD dw = 0;
        if (reg.QueryDWORDValue(L"StatusWindowX", dw) == ERROR_SUCCESS)
        {
            x = (int)dw;
        }
        if (reg.QueryDWORDValue(L"StatusWindowY", dw) == ERROR_SUCCESS)
        {
            y = (int)dw;
        }
    }
}

void CStatusWindow::_SavePosition()
{
    RECT rc = {0};
    _GetWindowRect(&rc);

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"StatusWindowX", (DWORD)rc.left);
        reg.SetDWORDValue(L"StatusWindowY", (DWORD)rc.top);
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_LoadHiddenState / _SetHiddenByUser
//
//  Persist the user's "hide status bar" choice in HKCU\Software\DIME so it
//  survives restart / IME re-activation.
//
//----------------------------------------------------------------------------

void CStatusWindow::_LoadHiddenState()
{
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        DWORD dw = 0;
        if (reg.QueryDWORDValue(L"StatusWindowHidden", dw) == ERROR_SUCCESS)
        {
            _isHiddenByUser = (dw != 0);
        }
    }
}

void CStatusWindow::_SetHiddenByUser(BOOL hidden)
{
    _isHiddenByUser = hidden;

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"StatusWindowHidden", hidden ? 1 : 0);
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_RestorePosition
//
//  Restore the saved position if present and still on a monitor; otherwise
//  fall back to the default bottom-right corner of the work area.
//
//----------------------------------------------------------------------------

void CStatusWindow::_RestorePosition()
{
    RECT rc = {0};
    if (!_GetClientRect(&rc) || rc.right <= rc.left)
    {
        return;
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int x = -1;
    int y = -1;
    _LoadPosition(x, y);
    _LoadHiddenState();

    if (x < 0 || y < 0 || !_IsOnAnyMonitor(x, y, w, h))
    {
        RECT wa = {0};
        SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right - w - 8;
        y = wa.bottom - h - 8;
    }

    // Never restore onto the taskbar.
    _ClampToWorkArea(x, y, w, h);

    _Move(x, y);
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_HitTest
//
//----------------------------------------------------------------------------

int CStatusWindow::_HitTest(POINT pt) const
{
    for (int i = 0; i < STATUS_ITEM_COUNT; ++i)
    {
        if (PtInRect(&_rcItems[i], pt))
        {
            return i;
        }
    }
    return -1;
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_DrawItem
//
//----------------------------------------------------------------------------

void CStatusWindow::_DrawItem(_In_ HDC dcHandle, int item, _In_opt_ HICON hIcon, _In_opt_ LPCWSTR pszText, _In_ RECT *prc)
{
    if (item == _hoverItem && !_isDragging)
    {
        HBRUSH hbr = CreateSolidBrush(RGB(0xDD, 0xE8, 0xF6));
        FillRect(dcHandle, prc, hbr);
        DeleteObject(hbr);

        HBRUSH hbrBorder = CreateSolidBrush(RGB(0x9C, 0xC3, 0xE6));
        FrameRect(dcHandle, prc, hbrBorder);
        DeleteObject(hbrBorder);
    }

    if (hIcon != nullptr)
    {
        int dx = (prc->right - prc->left - _iconSize) / 2;
        int dy = (prc->bottom - prc->top - _iconSize) / 2;
        if (_isGrayed)
        {
            DrawState(dcHandle, NULL, NULL, (LPARAM)hIcon, 0,
                prc->left + dx, prc->top + dy, _iconSize, _iconSize,
                DST_ICON | DSS_DISABLED);
        }
        else
        {
            DrawIconEx(dcHandle, prc->left + dx, prc->top + dy, hIcon,
                _iconSize, _iconSize, 0, NULL, DI_NORMAL);
        }
    }
    else if (pszText != nullptr)
    {
        RECT rcText = *prc;
        DrawText(dcHandle, pszText, 1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_OnPaint
//
//----------------------------------------------------------------------------

void CStatusWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps)
{
    pps;

    RECT rc = {0};
    _GetClientRect(&rc);

    HBRUSH hbrBk = CreateSolidBrush(RGB(0xF2, 0xF2, 0xF2));
    FillRect(dcHandle, &rc, hbrBk);
    DeleteObject(hbrBk);

    HFONT hOldFont = (HFONT)SelectObject(dcHandle,
        Global::defaultlFontHandle ? Global::defaultlFontHandle : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    int oldBkMode = SetBkMode(dcHandle, TRANSPARENT);
    COLORREF oldText = SetTextColor(dcHandle,
        _isGrayed ? RGB(0xA0, 0xA0, 0xA0) : RGB(0x20, 0x20, 0x20));

    // DIME icon (acts as a drag handle / brand mark).
    if (_hDimeIcon != nullptr)
    {
        if (_isGrayed)
        {
            DrawState(dcHandle, NULL, NULL, (LPARAM)_hDimeIcon, 0,
                _rcIcon.left, _rcIcon.top, _iconSize, _iconSize,
                DST_ICON | DSS_DISABLED);
        }
        else
        {
            DrawIconEx(dcHandle, _rcIcon.left, _rcIcon.top, _hDimeIcon,
                _iconSize, _iconSize, 0, NULL, DI_NORMAL);
        }
    }

    // Three clickable status segments:
    //  - 全/半角 uses the DoubleSingleByte on/off icons
    //  - 中/英标点 uses the Punctuation on/off icons
    //  - 常/全 (common-only filter) has no dedicated icon, drawn as text
    HICON hFullWidth = _isFullWidth ? _hFullWidthOn : _hFullWidthOff;
    HICON hPunct = _isChinesePunctuation ? _hPunctOn : _hPunctOff;

    _DrawItem(dcHandle, STATUS_ITEM_FULL_HALF, hFullWidth, nullptr, &_rcItems[STATUS_ITEM_FULL_HALF]);
    _DrawItem(dcHandle, STATUS_ITEM_PUNCTUATION, hPunct, nullptr, &_rcItems[STATUS_ITEM_PUNCTUATION]);
    _DrawItem(dcHandle, STATUS_ITEM_ONLY_COMMON, nullptr, _isOnlyCommon ? L"常" : L"全", &_rcItems[STATUS_ITEM_ONLY_COMMON]);

    SetTextColor(dcHandle, oldText);
    SetBkMode(dcHandle, oldBkMode);
    SelectObject(dcHandle, hOldFont);

    // Single 1px border following the window region: a gray ring between the
    // outer region and a 1px-inset inner region. All four corners are rounded.
    // Filling one ring (instead of an inset stroke) avoids a second "edge" line
    // inside the window border.
    int bw = rc.right - rc.left;
    int bh = rc.bottom - rc.top;
    HRGN hOuter = MakeStatusRgn(bw, bh, _radius);
    HRGN hInner = (bw > 2 && bh > 2) ? MakeStatusRgn(bw - 2, bh - 2, _radius) : nullptr;
    if (hInner)
    {
        OffsetRgn(hInner, 1, 1);
    }
    HRGN hRing = CreateRectRgn(0, 0, bw, bh);
    if (hOuter && hInner && hRing)
    {
        CombineRgn(hRing, hOuter, hInner, RGN_DIFF);
        HBRUSH hbrBorder = CreateSolidBrush(RGB(0xB0, 0xB0, 0xB0));
        FillRgn(dcHandle, hRing, hbrBorder);
        DeleteObject(hbrBorder);
    }
    if (hOuter) DeleteObject(hOuter);
    if (hInner) DeleteObject(hInner);
    if (hRing) DeleteObject(hRing);
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_OnLButtonDown
//
//----------------------------------------------------------------------------

void CStatusWindow::_OnLButtonDown(POINT pt)
{
    if (PtInRect(&_rcIcon, pt))
    {
        _isDragging = TRUE;
        // Capture the mouse on this window directly so the drag keeps tracking
        // even when the cursor outruns the (small) window during a fast drag.
        // (The base _StartCapture() relies on _pUIWnd which is null for this
        // standalone window, so it would no-op and drop messages mid-drag.)
        SetCapture(_GetWnd());

        RECT rc;
        _GetWindowRect(&rc);
        POINT screenPt = pt;
        ClientToScreen(_GetWnd(), &screenPt);
        _dragOffset.x = screenPt.x - rc.left;
        _dragOffset.y = screenPt.y - rc.top;
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_OnRButtonDown
//
//  Right-click context menu (shared with the candidate window via
//  DimeShowImeContextMenu). "功能设置" is a placeholder for now; "隐藏状态栏"
//  just hides this floating bar.
//
//----------------------------------------------------------------------------

void CStatusWindow::_OnRButtonDown(POINT pt)
{
    POINT screenPt = pt;
    ClientToScreen(_GetWnd(), &screenPt);

    RECT rc = {0};
    _GetWindowRect(&rc);

    BOOL isVisible = _IsWindowVisible();
    switch (DimeShowImeContextMenu(_GetWnd(), screenPt, isVisible, &rc))
    {
        case DIME_CMD_SETTINGS:
        DimeShowConfigDialog(_GetWnd(), reinterpret_cast<CDIME*>(_pv));
        break;
    case DIME_CMD_TOGGLE_STATUSBAR:
        _SetHiddenByUser(isVisible);
        _Show(!isVisible);
        break;
    default:
        break;
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_OnMouseMove
//
//----------------------------------------------------------------------------

void CStatusWindow::_OnMouseMove(POINT pt)
{
    if (_isDragging)
    {
        // Free movement while dragging: the window follows the cursor exactly.
        // Edge snapping is applied only on release (see _OnLButtonUp) so the
        // bar never feels "stuck" to an edge mid-drag.
        POINT screenPt = pt;
        ClientToScreen(_GetWnd(), &screenPt);
        int x = screenPt.x - _dragOffset.x;
        int y = screenPt.y - _dragOffset.y;

        // Keep the bar inside the work area (never covering the taskbar),
        // even mid-drag.
        RECT rc;
        _GetClientRect(&rc);
        _ClampToWorkArea(x, y, rc.right - rc.left, rc.bottom - rc.top);

        // Remember the rect we are about to vacate so we can force the OS to
        // repaint it. A topmost tool window does not reliably trigger a repaint
        // of the area it leaves behind, so without this the bar's pixels would
        // linger at the original spot and look like a second status bar.
        RECT rcOld = {0};
        _GetWindowRect(&rcOld);

        _Move(x, y);

        ::RedrawWindow(GetDesktopWindow(), &rcOld, nullptr,
            RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        _RepaintNow();
        return;
    }

    int hit = _HitTest(pt);
    if (hit != _hoverItem)
    {
        _hoverItem = hit;
        _RepaintNow();
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_SnapToEdges
//
//  Snap a candidate top-left (x, y) to the nearest work-area edge when it is
//  within the threshold. Used on drag release.
//
//----------------------------------------------------------------------------

void CStatusWindow::_SnapToEdges(_Inout_ int &x, _Inout_ int &y, int w, int h)
{
    RECT wa = {0};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
    const int snap = 16;

    if (abs((y + h) - wa.bottom) <= snap)
    {
        y = wa.bottom - h;           // attach to the bottom (taskbar) edge
    }
    else if (abs(y - wa.top) <= snap)
    {
        y = wa.top;                 // attach to the top edge
    }

    if (abs((x + w) - wa.right) <= snap)
    {
        x = wa.right - w;           // attach to the right edge
    }
    else if (abs(x - wa.left) <= snap)
    {
        x = wa.left;                // attach to the left edge
    }

    // Never let the bar overlap the Windows taskbar (which lives outside the
    // work area). This is the final safety net for a drop that lands on the
    // taskbar without triggering an edge snap.
    _ClampToWorkArea(x, y, w, h);
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_ClampToWorkArea
//
//  Force the top-left (x, y) to keep the whole window inside the work area,
//  i.e. never overlapping the Windows taskbar.
//
//----------------------------------------------------------------------------

void CStatusWindow::_ClampToWorkArea(_Inout_ int &x, _Inout_ int &y, int w, int h)
{
    RECT wa = {0};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);

    if (x + w > wa.right)
    {
        x = wa.right - w;
    }
    if (x < wa.left)
    {
        x = wa.left;
    }
    if (y + h > wa.bottom)
    {
        y = wa.bottom - h;
    }
    if (y < wa.top)
    {
        y = wa.top;
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_ApplyRoundedCorners
//
//  Clip the window to a rounded rectangle region so its corners are rounded.
//  Pixels outside the region are not painted and show the desktop behind.
//
//----------------------------------------------------------------------------

void CStatusWindow::_ApplyRoundedCorners()
{
    RECT rc = {0};
    if (!_GetClientRect(&rc) || rc.right <= rc.left || rc.bottom <= rc.top)
    {
        return;
    }

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HRGN hrgn = MakeStatusRgn(w, h, _radius);
    if (hrgn != nullptr)
    {
        SetWindowRgn(_GetWnd(), hrgn, TRUE);
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_OnLButtonUp
//
//----------------------------------------------------------------------------

void CStatusWindow::_OnLButtonUp(POINT pt)
{
    if (_isDragging)
    {
        _isDragging = FALSE;
        ReleaseCapture();

        // Snap to the nearest edge on release, then persist the position.
        RECT rc;
        _GetClientRect(&rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        RECT wr;
        _GetWindowRect(&wr);
        int x = wr.left;
        int y = wr.top;
        _SnapToEdges(x, y, w, h);

        // Force a repaint of the area we just left (same reason as in
        // _OnMouseMove) so the bar does not leave a ghost at the drop point.
        _Move(x, y);
        ::RedrawWindow(GetDesktopWindow(), &wr, nullptr,
            RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        _RepaintNow();

        _SavePosition();
        return;
    }

    int hit = _HitTest(pt);
    if (hit >= 0 && _pfnCallback != nullptr)
    {
        _pfnCallback(_pv, hit);
    }
}

//+---------------------------------------------------------------------------
//
// CStatusWindow::_WindowProcCallback
//
//----------------------------------------------------------------------------

LRESULT CALLBACK CStatusWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dcHandle = BeginPaint(wndHandle, &ps);
            _OnPaint(dcHandle, &ps);
            EndPaint(wndHandle, &ps);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        {
            POINT point;
            POINTSTOPOINT(point, MAKEPOINTS(lParam));
            if (uMsg == WM_MOUSEMOVE)
            {
                _OnMouseMove(point);
            }
            else if (uMsg == WM_LBUTTONDOWN)
            {
                _OnLButtonDown(point);
            }
            else
            {
                _OnLButtonUp(point);
            }
        }
        return 0;

    case WM_RBUTTONUP:
        {
            POINT point;
            POINTSTOPOINT(point, MAKEPOINTS(lParam));
            _OnRButtonDown(point);
        }
        return 0;

    case WM_MOUSEACTIVATE:
        {
            WORD mouseEvent = HIWORD(lParam);
            if (mouseEvent == WM_LBUTTONDOWN ||
                mouseEvent == WM_RBUTTONDOWN ||
                mouseEvent == WM_MBUTTONDOWN)
            {
                return MA_NOACTIVATE;
            }
        }
        break;

    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;
    }

    return DefWindowProc(wndHandle, uMsg, wParam, lParam);
}
