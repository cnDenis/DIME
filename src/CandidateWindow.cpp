// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "Globals.h"
#include "BaseWindow.h"
#include "CandidateWindow.h"
#include "StatusWindow.h"
#include "ContextMenu.h"
#include "ConfigDialog.h"

namespace
{
struct CStatusIconCacheSlot
{
    DWORD iconResId;
    COLORREF tintColor;
    int size;           // pixel size the cached bitmap was rendered at (DPI-scaled)
    HBITMAP hBitmap;
};

CStatusIconCacheSlot g_statusIconCache[4] = {};
const DWORD g_statusIconResourceIds[4] =
{
    IME_PUNCTUATION_ON_INDEX,
    IME_PUNCTUATION_OFF_INDEX,
    IME_DOUBLE_ON_INDEX,
    IME_DOUBLE_OFF_INDEX,
};
const COLORREF g_statusIconTintColors[4] =
{
    CANDWND_ACCENT_COLOR,
    CANDWND_ACCENT_COLOR,
    CANDWND_ACCENT_COLOR,
    CANDWND_ACCENT_COLOR,
};

HBITMAP CreateTintedStatusIconBitmap(int size, DWORD iconResId, COLORREF tintColor)
{
    if (!Global::dllInstanceHandle || size <= 0)
    {
        return nullptr;
    }

    HICON hIcon = reinterpret_cast<HICON>(LoadImage(
        Global::dllInstanceHandle,
        MAKEINTRESOURCE(iconResId),
        IMAGE_ICON,
        size,
        size,
        LR_DEFAULTCOLOR));
    if (!hIcon)
    {
        return nullptr;
    }

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen)
    {
        return nullptr;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    if (!hdcMem)
    {
        return nullptr;
    }

    BITMAPINFO bitmapInfo = {0};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;

    void* pvBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bitmapInfo, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hBitmap || !pvBits)
    {
        DeleteDC(hdcMem);
        return nullptr;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    PatBlt(hdcMem, 0, 0, size, size, WHITENESS);
    DrawIconEx(hdcMem, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);

    const BYTE fgR = GetRValue(tintColor);
    const BYTE fgG = GetGValue(tintColor);
    const BYTE fgB = GetBValue(tintColor);
    DWORD* pixels = static_cast<DWORD*>(pvBits);
    for (int i = 0; i < size * size; ++i)
    {
        const BYTE b = static_cast<BYTE>(pixels[i] & 0xFF);
        const BYTE g = static_cast<BYTE>((pixels[i] >> 8) & 0xFF);
        const BYTE r = static_cast<BYTE>((pixels[i] >> 16) & 0xFF);
        if (static_cast<int>(r) + g + b < 720)
        {
            // 32-bit DIB pixels are BGRA: low byte = Blue, then Green, Red, Alpha.
            // Pack the tint in that order so the on-screen color matches tintColor.
            pixels[i] = (0xFFu << 24) | (static_cast<DWORD>(fgR) << 16) | (static_cast<DWORD>(fgG) << 8) | fgB;
        }
        else
        {
            pixels[i] = 0;
        }
    }

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    return hBitmap;
}

HBITMAP GetCachedStatusIconBitmap(DWORD iconResId, COLORREF tintColor, int size)
{
    if (size <= 0)
    {
        return nullptr;
    }

    for (int i = 0; i < ARRAYSIZE(g_statusIconCache); ++i)
    {
        if (g_statusIconCache[i].iconResId == iconResId &&
            g_statusIconCache[i].tintColor == tintColor &&
            g_statusIconCache[i].size == size &&
            g_statusIconCache[i].hBitmap != nullptr)
        {
            return g_statusIconCache[i].hBitmap;
        }
    }

    for (int i = 0; i < ARRAYSIZE(g_statusIconResourceIds); ++i)
    {
        if (g_statusIconResourceIds[i] != iconResId || g_statusIconTintColors[i] != tintColor)
        {
            continue;
        }

        // Re-render if empty or the DPI-scaled size changed (e.g. moved to a
        // monitor with a different scaling).
        if (g_statusIconCache[i].hBitmap == nullptr || g_statusIconCache[i].size != size)
        {
            if (g_statusIconCache[i].hBitmap != nullptr)
            {
                DeleteObject(g_statusIconCache[i].hBitmap);
                g_statusIconCache[i].hBitmap = nullptr;
            }
            g_statusIconCache[i].iconResId = iconResId;
            g_statusIconCache[i].tintColor = tintColor;
            g_statusIconCache[i].size = size;
            g_statusIconCache[i].hBitmap = CreateTintedStatusIconBitmap(size, iconResId, tintColor);
        }

        return g_statusIconCache[i].hBitmap;
    }

    return nullptr;
}
} // namespace

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateWindow::CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode)
{
    _currentSelection = 0;

    _SetTextColor(CANDWND_ITEM_COLOR, GetSysColor(COLOR_WINDOW));    // text color is black
    _SetFillColor((HBRUSH)(COLOR_WINDOW+1));

    _pIndexRange = pIndexRange;

    _pfnCallback = pfnCallback;
    _pObj = pv;

    _pShadowWnd = nullptr;

    _cyRow = CANDWND_ROW_WIDTH;
    _iconSize = 16;   // 125% reference (DPI 120); re-scaled in _ResizeWindow()
    _cxTitle = 0;

    _wndWidth = 0;

    _dontAdjustOnEmptyItemPage = FALSE;

    _isStoreAppMode = isStoreAppMode;
    _keystrokeCodeLen = 0;
    _keystrokeCode[0] = L'\0';
    _isFullWidth = FALSE;
    _isChinesePunctuation = TRUE;
    _isPinyinMode = FALSE;
    _isEnglishMode = FALSE;
    _isOnlyCommon = FALSE;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCandidateWindow::~CCandidateWindow()
{
    _ClearList();
    _DeleteShadowWnd();
}

//+---------------------------------------------------------------------------
//
// _Create
//
// CandidateWinow is the top window
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_Create(ATOM atom, _In_ UINT wndWidth, _In_opt_ HWND parentWndHandle)
{
    BOOL ret = FALSE;
    _wndWidth = wndWidth;

    ret = _CreateMainWindow(atom, parentWndHandle);
    if (FALSE == ret)
    {
        goto Exit;
    }

    ret = _CreateBackGroundShadowWindow();
    if (FALSE == ret)
    {
        goto Exit;
    }

    _ResizeWindow();

Exit:
    return TRUE;
}

BOOL CCandidateWindow::_CreateMainWindow(ATOM atom, _In_opt_ HWND parentWndHandle)
{
    _SetUIWnd(this);

    if (!CBaseWindow::_Create(atom,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WS_POPUP,
        NULL, 0, 0, parentWndHandle))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL CCandidateWindow::_CreateBackGroundShadowWindow()
{
    _pShadowWnd = new (std::nothrow) CShadowWindow(this);
    if (_pShadowWnd == nullptr)
    {
        return FALSE;
    }

    if (!_pShadowWnd->_Create(Global::AtomShadowWindow,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        WS_DISABLED | WS_POPUP, this))
    {
        _DeleteShadowWnd();
        return FALSE;
    }

    return TRUE;
}

// Scale the row height to the monitor DPI, using 125% (DPI 120) as the
// reference so the current good-looking size is preserved and other scalings
// stay proportional. We read the same DPI the global font is built from
// (primary monitor via GetDC(NULL)/LOGPIXELSX) so row height and font never
// diverge.
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

void CCandidateWindow::_ResizeWindow()
{
    // Row height follows the DPI so text is never dwarfed by fixed padding.
    UINT dpi = DimeGetWindowDpi(_GetWnd());
    _cyRow = static_cast<int>(CANDWND_ROW_WIDTH * (static_cast<float>(dpi) / DIME_REFERENCE_DPI) + 0.5f);
    // Status icons pick a preferred pixel size per display-scaling tier (same
    // tiers as the candidate font), never below 16px so they stay legible.
    _iconSize = DimeSelectIconPixelHeight(dpi);

    // Keep text metrics / row height in sync with the live candidate font
    // (auto DPI tier or a user-chosen fixed size from the settings dialog).
    HWND hwnd = _GetWnd();
    if (hwnd != nullptr && Global::defaultlFontHandle != nullptr)
    {
        HDC dc = GetDC(hwnd);
        if (dc != nullptr)
        {
            HFONT hOld = (HFONT)SelectObject(dc, Global::defaultlFontHandle);
            GetTextMetrics(dc, &_TextMetric);
            SelectObject(dc, hOld);
            ReleaseDC(hwnd, dc);
            if (_TextMetric.tmHeight > _cyRow)
            {
                _cyRow = _TextMetric.tmHeight;
            }
        }
    }

    SIZE size = {0, 0};

    _cxTitle = max(_cxTitle, size.cx + 2 * GetSystemMetrics(SM_CXFRAME));

    // In temporary English mode the encoding area can hold up to 32 characters,
    // so widen the window to fit the typed text instead of clipping it.
    int minWidth = (_wndWidth > 0) ? static_cast<int>(_wndWidth) : CAND_WINDOW_WIDTH_PX;
    if (_isEnglishMode && _keystrokeCodeLen > 0)
    {
        int needed = _TextMetric.tmAveCharWidth * (static_cast<int>(_keystrokeCodeLen) + 2)
            + 2 * GetSystemMetrics(SM_CXFRAME);
        if (needed > minWidth)
        {
            minWidth = needed;
        }
    }
    _cxTitle = max(_cxTitle, minWidth);

    int candidateListPageCnt = _IsActiveInputLayout() ? static_cast<int>(_pIndexRange->Count()) : 0;
    int encodingRows = (_IsActiveInputLayout() ? 1 : 0);
    int totalRows = encodingRows + candidateListPageCnt;
    if (totalRows <= 0)
    {
        return;
    }

    int x = 0;
    int y = 0;
    RECT rcWnd = {0, 0, 0, 0};
    if (_GetWindowRect(&rcWnd))
    {
        x = rcWnd.left;
        y = rcWnd.top;
    }

    CBaseWindow::_Resize(x, y, _cxTitle, _cyRow * totalRows, FALSE);
}

void CCandidateWindow::_UpdateLayout()
{
    _ResizeWindow();
}

//+---------------------------------------------------------------------------
//
// _Move
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Move(int x, int y, BOOL fRedraw)
{
    CBaseWindow::_Move(x, y, fRedraw);
}

//+---------------------------------------------------------------------------
//
// _Show
//
//----------------------------------------------------------------------------

void CCandidateWindow::_Show(BOOL isShowWnd)
{
    if (_pShadowWnd)
    {
        _pShadowWnd->_Show(isShowWnd);
    }
    CBaseWindow::_Show(isShowWnd);
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------

VOID CCandidateWindow::_SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    _crTextColor = _AdjustTextColor(crColor, crBkColor);
    _crBkColor = crBkColor;
}

VOID CCandidateWindow::_SetFillColor(_In_ HBRUSH hBrush)
{
    _brshBkColor = hBrush;
}

//+---------------------------------------------------------------------------
//
// _WindowProcCallback
//
// Cand window proc.
//----------------------------------------------------------------------------

const int PageCountPosition = 1;

static int _CandidateStringLeft(_In_ HDC dcHandle, _In_ int cxLine, _In_ PCWSTR numberString)
{
    SIZE size = {0};
    int numberLen = (int)wcslen(numberString);
    if (numberLen > 0)
    {
        GetTextExtentPoint32(dcHandle, numberString, numberLen, &size);
    }
    return PageCountPosition * cxLine + size.cx;
}

static int _CandidateTextColumnLeft(_In_ HDC dcHandle, _In_ int cxLine)
{
    // 序号均为单字符 (1-9, 0), 格式为 "N. ".
    return _CandidateStringLeft(dcHandle, cxLine, L"8. ");
}

LRESULT CALLBACK CCandidateWindow::_WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        {
            HDC dcHandle = nullptr;

            dcHandle = GetDC(wndHandle);
            if (dcHandle)
            {
                HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);
                GetTextMetrics(dcHandle, &_TextMetric);

                _cxTitle = static_cast<int>(_wndWidth);
                SelectObject(dcHandle, hFontOld);
                ReleaseDC(wndHandle, dcHandle);
            }
        }
        return 0;

    case WM_DESTROY:
        _DeleteShadowWnd();
        return 0;

    case WM_WINDOWPOSCHANGED:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // move shadow
            if (_pShadowWnd)
            {
                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }

            _FireMessageToLightDismiss(wndHandle, pWndPos);
        }
        break;

    case WM_WINDOWPOSCHANGING:
        {
            WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;

            // show/hide shadow
            if (_pShadowWnd)
            {
                if ((pWndPos->flags & SWP_HIDEWINDOW) != 0)
                {
                    _pShadowWnd->_Show(FALSE);
                }

                // don't go behaind of shadow
                if (((pWndPos->flags & SWP_NOZORDER) == 0) && (pWndPos->hwndInsertAfter == _pShadowWnd->_GetWnd()))
                {
                    pWndPos->flags |= SWP_NOZORDER;
                }

                _pShadowWnd->_OnOwnerWndMoved((pWndPos->flags & SWP_NOSIZE) == 0);
            }
        }
        break;

    case WM_SHOWWINDOW:
        // show/hide shadow
        if (_pShadowWnd)
        {
            _pShadowWnd->_Show((BOOL)wParam);
        }
        break;

    case WM_ERASEBKGND:
        {
            HDC dcHandle = (HDC)wParam;
            RECT rc = {0, 0, 0, 0};
            GetClientRect(wndHandle, &rc);
            FillRect(dcHandle, &rc, _brshBkColor ? _brshBkColor : (HBRUSH)(COLOR_WINDOW + 1));
        }
        return 1;

    case WM_PAINT:
        {
            HDC dcHandle = nullptr;
            PAINTSTRUCT ps;

            dcHandle = BeginPaint(wndHandle, &ps);
            _OnPaint(dcHandle, &ps);
            _DrawBorder(wndHandle, 1);
            EndPaint(wndHandle, &ps);
        }
        return 0;

    case WM_SETCURSOR:
        {
            POINT cursorPoint;

            GetCursorPos(&cursorPoint);
            MapWindowPoints(NULL, wndHandle, &cursorPoint, 1);

            // handle mouse message
            _HandleMouseMsg(HIWORD(lParam), cursorPoint);
        }
        return 1;

    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        {
            POINT point;

            POINTSTOPOINT(point, MAKEPOINTS(lParam));

            // handle mouse message
            _HandleMouseMsg(uMsg, point);
        }
		// we processes this message, it should return zero. 
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

//+---------------------------------------------------------------------------
//
// _HandleMouseMsg
//
//----------------------------------------------------------------------------

void CCandidateWindow::_HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point)
{
    switch (mouseMsg)
    {
    case WM_MOUSEMOVE:
        _OnMouseMove(point);
        break;
    case WM_LBUTTONDOWN:
        _OnLButtonDown(point);
        break;
    case WM_LBUTTONUP:
        _OnLButtonUp(point);
        break;
    case WM_RBUTTONUP:
        _OnRButtonDown(point);
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _OnPaint
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pPaintStruct)
{
    SetBkMode(dcHandle, TRANSPARENT);

    HFONT hFontOld = (HFONT)SelectObject(dcHandle, Global::defaultlFontHandle);

    FillRect(dcHandle, &pPaintStruct->rcPaint, _brshBkColor);

    int yOffset = _GetEncodingRowHeight();
    if (yOffset > 0)
    {
        _DrawHeaderRow(dcHandle, &pPaintStruct->rcPaint);
    }

    if (_IsActiveInputLayout())
    {
        UINT currentPageIndex = 0;
        UINT currentPage = 0;

        if (_PageIndex.Count() > 0 && SUCCEEDED(_GetCurrentPage(&currentPage)))
        {
            _AdjustPageIndex(currentPage, currentPageIndex);
        }

        RECT listPaint = pPaintStruct->rcPaint;
        listPaint.top += yOffset;
        _DrawList(dcHandle, currentPageIndex, &listPaint, yOffset);
    }

    SelectObject(dcHandle, hFontOld);
}

//+---------------------------------------------------------------------------
//
// _OnLButtonDown
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonDown(POINT pt)
{
    // Mouse interaction with the candidate window is disabled (no item
    // selection, no scrollbar).
    (void)pt;
}

//+---------------------------------------------------------------------------
//
// CCandidateWindow::_SetStatusWindow
//
//  Hands the candidate window a pointer to the floating status bar so its
//  context menu can show/hide that bar on request.
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetStatusWindow(_In_opt_ CStatusWindow* pStatusWnd)
{
    _pStatusWnd = pStatusWnd;
}

void CCandidateWindow::_SetTextService(_In_ CDIME* pTextService)
{
    _pTextService = pTextService;
}

//+---------------------------------------------------------------------------
//
// CCandidateWindow::_OnRButtonDown
//
//  Right-click context menu (shared with the status bar via
//  DimeShowImeContextMenu). "功能设置" is a placeholder; "隐藏状态栏" hides
//  the floating status bar through the pointer supplied by the presenter.
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnRButtonDown(POINT pt)
{
    POINT screenPt = pt;
    ClientToScreen(_GetWnd(), &screenPt);

    BOOL isVisible = (_pStatusWnd != nullptr) ? _pStatusWnd->_IsWindowVisible() : FALSE;
    switch (DimeShowImeContextMenu(_GetWnd(), screenPt, isVisible))
    {
        case DIME_CMD_SETTINGS:
        DimeShowConfigDialog(_GetWnd(), _pTextService);
        break;

    case DIME_CMD_TOGGLE_STATUSBAR:
        if (_pStatusWnd != nullptr)
        {
            _pStatusWnd->_SetHiddenByUser(isVisible);
            _pStatusWnd->_Show(!isVisible);
        }
        break;
    default:
        break;
    }
}

//+---------------------------------------------------------------------------
//
// _OnLButtonUp
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnLButtonUp(POINT pt)
{
    (void)pt;
}

//+---------------------------------------------------------------------------
//
// _OnMouseMove
//
//----------------------------------------------------------------------------

void CCandidateWindow::_OnMouseMove(POINT pt)
{
    (void)pt;
}

//+---------------------------------------------------------------------------
//
// _GetEncodingRowHeight
//
//----------------------------------------------------------------------------

int CCandidateWindow::_GetEncodingRowHeight() const
{
    return (_IsActiveInputLayout() ? _cyRow : 0);
}

//+---------------------------------------------------------------------------
//
// _SetInputModeStatus
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetInputModeStatus(BOOL isFullWidth, BOOL isChinesePunctuation)
{
    BOOL changed = (_isFullWidth != isFullWidth) || (_isChinesePunctuation != isChinesePunctuation);
    _isFullWidth = isFullWidth;
    _isChinesePunctuation = isChinesePunctuation;
    if (changed)
    {
        _RepaintNow();
    }
}

void CCandidateWindow::_SetPinyinMode(BOOL isPinyinMode)
{
    if (_isPinyinMode != isPinyinMode)
    {
        _isPinyinMode = isPinyinMode;
        _RepaintNow();
    }
}

void CCandidateWindow::_SetOnlyCommonMode(BOOL isOnlyCommon)
{
    if (_isOnlyCommon != isOnlyCommon)
    {
        _isOnlyCommon = isOnlyCommon;
        _RepaintNow();
    }
}

void CCandidateWindow::_SetEnglishMode(BOOL isEnglishMode)
{
    if (_isEnglishMode != isEnglishMode)
    {
        _isEnglishMode = isEnglishMode;
        _RepaintNow();
    }
}

//+---------------------------------------------------------------------------
//
// _DrawResourceIcon
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawResourceIcon(_In_ HDC dcHandle, int x, int y, int size, DWORD iconResId, COLORREF tintColor)
{
    if (size <= 0)
    {
        return;
    }

    HBITMAP hBitmap = GetCachedStatusIconBitmap(iconResId, tintColor, size);
    if (!hBitmap)
    {
        return;
    }

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen)
    {
        return;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    ReleaseDC(nullptr, hdcScreen);
    if (!hdcMem)
    {
        return;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dcHandle, x, y, size, size, hdcMem, 0, 0, size, size, blend);
    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
}

//+---------------------------------------------------------------------------
//
// ClearStatusIconCache
//
//----------------------------------------------------------------------------

void CCandidateWindow::ClearStatusIconCache()
{
    for (int i = 0; i < ARRAYSIZE(g_statusIconCache); ++i)
    {
        if (g_statusIconCache[i].hBitmap != nullptr)
        {
            DeleteObject(g_statusIconCache[i].hBitmap);
            g_statusIconCache[i].hBitmap = nullptr;
        }
        g_statusIconCache[i].iconResId = 0;
        g_statusIconCache[i].tintColor = 0;
    }
}

//+---------------------------------------------------------------------------
//
// _DrawStatusIcons
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawStatusIcons(_In_ HDC dcHandle, _In_ RECT *prc)
{
    // In temporary English mode the status icons are hidden; the "英" tag on
    // the left already signals the mode, so the three indicators are redundant.
    if (_isEnglishMode)
    {
        return;
    }

    // _iconSize is already DPI-scaled (16px at the 125% reference); the gap
    // scales proportionally so icons stay evenly spaced at any DPI.
    const int iconSize = _iconSize;
    const int iconGap = static_cast<int>(4.0f * static_cast<float>(iconSize) / 16.0f + 0.5f);
    const int rightMargin = CANDWND_BORDER_WIDTH + 2;
    const int cyLine = prc->bottom - prc->top;
    const int iconY = prc->top + (cyLine - iconSize) / 2;

    int x = prc->right - rightMargin;

    // "Only common characters" indicator: drawn at the far right, to the right
    // of the Chinese/English punctuation icon. "常" = common-only, "全" = all.
    {
        // The 常/全 tag sits right next to the status icons, so draw it at the
        // same pixel size as the icons. The global font may be smaller at low
        // DPI (e.g. 14px text vs 16px icon at 100%), which would look off.
        const WCHAR onlyCommonCh = _isOnlyCommon ? L'常' : L'全';
        LOGFONTW lf = {0};
        GetObjectW(Global::defaultlFontHandle, sizeof(lf), &lf);
        lf.lfHeight = -iconSize;
        HFONT hTagFont = CreateFontIndirectW(&lf);
        HFONT hOldFont = hTagFont ? (HFONT)SelectObject(dcHandle, hTagFont) : nullptr;
        SIZE charSize = {0};
        GetTextExtentPoint32(dcHandle, &onlyCommonCh, 1, &charSize);
        int textY = prc->top + (cyLine - charSize.cy) / 2;
        x -= charSize.cx;
        SetTextColor(dcHandle, CANDWND_ACCENT_COLOR);
        SetBkColor(dcHandle, GetSysColor(CANDWND_HEADER_BK_SYSCOLOR));
        ExtTextOut(dcHandle, x, textY, ETO_OPAQUE, nullptr, &onlyCommonCh, 1, NULL);
        if (hOldFont) SelectObject(dcHandle, hOldFont);
        if (hTagFont) DeleteObject(hTagFont);
        x -= iconGap;
    }

    DWORD punctIcon = _isChinesePunctuation ? IME_PUNCTUATION_ON_INDEX : IME_PUNCTUATION_OFF_INDEX;
    x -= iconSize;
    _DrawResourceIcon(dcHandle, x, iconY, iconSize, punctIcon, CANDWND_ACCENT_COLOR);

    x -= iconGap;
    DWORD widthIcon = _isFullWidth ? IME_DOUBLE_ON_INDEX : IME_DOUBLE_OFF_INDEX;
    x -= iconSize;
    _DrawResourceIcon(dcHandle, x, iconY, iconSize, widthIcon, CANDWND_ACCENT_COLOR);
}

//+---------------------------------------------------------------------------
//
// _DrawHeaderRow
//
//----------------------------------------------------------------------------

static WCHAR _ToLowerChar(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') ? (WCHAR)(ch - L'A' + L'a') : ch;
}

void CCandidateWindow::_DrawHeaderRow(_In_ HDC dcHandle, _In_ RECT *prc)
{
    int cxLine = _TextMetric.tmAveCharWidth;
    int cyLine = max(_cyRow, _TextMetric.tmHeight);
    int cyOffset = (cyLine == _cyRow ? (cyLine - _TextMetric.tmHeight) / 2 : 0);

    RECT rcRow = {prc->left, prc->top, prc->right, prc->top + cyLine};
    FillRect(dcHandle, &rcRow, GetSysColorBrush(CANDWND_HEADER_BK_SYSCOLOR));

    int textLeft = _CandidateTextColumnLeft(dcHandle, cxLine);

    // "拼" tag for the temporary pinyin input mode, drawn at the far-left corner.
    // It is laid out independently of the keystroke code, which keeps its normal
    // column and font so it does not overlap the tag.
    if (_isPinyinMode)
    {
        SetTextColor(dcHandle, CANDWND_NUM_COLOR);
        SetBkColor(dcHandle, GetSysColor(CANDWND_HEADER_BK_SYSCOLOR));
        int tagX = prc->left + 2;
        RECT rcTag = rcRow;
        rcTag.left = tagX;
        ExtTextOut(dcHandle, tagX, rcRow.top + cyOffset, ETO_OPAQUE, &rcTag, L"拼", 1, NULL);
    }

    // "英" tag for the temporary English input mode, drawn at the far-left corner.
    if (_isEnglishMode)
    {
        SetTextColor(dcHandle, CANDWND_NUM_COLOR);
        SetBkColor(dcHandle, GetSysColor(CANDWND_HEADER_BK_SYSCOLOR));
        int tagX = prc->left + 2;
        RECT rcTag = rcRow;
        rcTag.left = tagX;
        ExtTextOut(dcHandle, tagX, rcRow.top + cyOffset, ETO_OPAQUE, &rcTag, L"英", 1, NULL);
    }

    if (_keystrokeCodeLen > 0)
    {
        WCHAR drawKey[ENGLISH_MAX_CODE_LENGTH + 1] = {L'\0'};
        for (DWORD_PTR k = 0; k < _keystrokeCodeLen && k <= ENGLISH_MAX_CODE_LENGTH; k++)
        {
            // Preserve the user's casing for English input; lowercase the rest
            // (wubi/pinyin codes) for a consistent display.
            drawKey[k] = _isEnglishMode ? _keystrokeCode[k] : _ToLowerChar(_keystrokeCode[k]);
        }

        RECT rcText = rcRow;
        rcText.left = textLeft;
        rcText.right = prc->right;

        SetTextColor(dcHandle, CANDWND_NUM_COLOR);
        SetBkColor(dcHandle, GetSysColor(CANDWND_HEADER_BK_SYSCOLOR));
        ExtTextOut(dcHandle, textLeft, rcRow.top + cyOffset, ETO_OPAQUE, &rcText, drawKey, (DWORD)_keystrokeCodeLen, NULL);
    }

    _DrawStatusIcons(dcHandle, &rcRow);
}

//+---------------------------------------------------------------------------
//
// _DrawList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc, _In_ int yOffset)
{
    yOffset;

    // Temporary English mode has no candidates; show usage hints instead of
    // an empty box.
    if (_isEnglishMode && _candidateList.Count() == 0)
    {
        _DrawEnglishHint(dcHandle, prc);
        return;
    }

    int pageCount = 0;
    int candidateListPageCnt = _pIndexRange->Count();

    int cxLine = _TextMetric.tmAveCharWidth;
    int cyLine = max(_cyRow, _TextMetric.tmHeight);
    int cyOffset = (cyLine == _cyRow ? (cyLine-_TextMetric.tmHeight)/2 : 0);

    RECT rc;

	const size_t lenOfPageCount = 16;
    for (;
        (iIndex < _candidateList.Count()) && (pageCount < candidateListPageCnt);
        iIndex++, pageCount++)
    {
        WCHAR pageCountString[lenOfPageCount] = {'\0'};
        CCandidateListItem* pItemList = nullptr;

        rc.top = prc->top + pageCount * cyLine;
        rc.bottom = rc.top + cyLine;

        StringCchPrintf(pageCountString, ARRAYSIZE(pageCountString), L"%d. ", (LONG)*_pIndexRange->GetAt(pageCount));
        int textLeft = _CandidateStringLeft(dcHandle, cxLine, pageCountString);

        rc.left = prc->left + PageCountPosition * cxLine;
        rc.right = prc->left + textLeft;

        // Number Font Color And BK
        SetTextColor(dcHandle, CANDWND_NUM_COLOR);
        SetBkColor(dcHandle, GetSysColor(COLOR_3DHIGHLIGHT));

        ExtTextOut(dcHandle, PageCountPosition * cxLine, rc.top + cyOffset, ETO_OPAQUE, &rc, pageCountString, (UINT)wcslen(pageCountString), NULL);

        rc.left = prc->left + textLeft;
        rc.right = prc->right;

        pItemList = _candidateList.GetAt(iIndex);

        // Candidate Font Color And BK (no per-row selection highlight)
        SetTextColor(dcHandle, _crTextColor);
        SetBkColor(dcHandle, GetSysColor(COLOR_3DHIGHLIGHT));

        ExtTextOut(dcHandle, textLeft, rc.top + cyOffset, ETO_OPAQUE, &rc, pItemList->_ItemString.Get(), (DWORD)pItemList->_ItemString.GetLength(), NULL);

        DWORD_PTR suffixLen = pItemList->_FindKeyCode.GetLength();
        if (suffixLen > 0)
        {
            SIZE itemSize = {0};
            GetTextExtentPoint32(dcHandle, pItemList->_ItemString.Get(), (int)pItemList->_ItemString.GetLength(), &itemSize);

            int suffixLeft = textLeft + itemSize.cx;
            // Encoding code shown in the default num color (no selection tint).
            SetTextColor(dcHandle, CANDWND_NUM_COLOR);
            // 不用 ETO_OPAQUE, 避免背景矩形从 textLeft 起涂掉已绘制的汉字.

            // Lowercase the encoding for display.
            WCHAR* pSuffixLower = new (std::nothrow) WCHAR[suffixLen + 1];
            if (pSuffixLower)
            {
                for (DWORD_PTR k = 0; k < suffixLen; k++)
                {
                    pSuffixLower[k] = _ToLowerChar(pItemList->_FindKeyCode.Get()[k]);
                }
                pSuffixLower[suffixLen] = L'\0';
                ExtTextOut(dcHandle, suffixLeft, rc.top + cyOffset, 0, nullptr, pSuffixLower, (DWORD)suffixLen, NULL);
                delete [] pSuffixLower;
            }
            else
            {
                ExtTextOut(dcHandle, suffixLeft, rc.top + cyOffset, 0, nullptr, pItemList->_FindKeyCode.Get(), (DWORD)suffixLen, NULL);
            }
        }
    }
    for (; (pageCount < candidateListPageCnt); pageCount++)
    {
        rc.top    = prc->top + pageCount * cyLine;
        rc.bottom = rc.top + cyLine;

        rc.left   = prc->left + PageCountPosition * cxLine;
        rc.right  = prc->left + _CandidateTextColumnLeft(dcHandle, cxLine);

        FillRect(dcHandle, &rc, (HBRUSH)(COLOR_3DHIGHLIGHT+1));
    }
}

//+---------------------------------------------------------------------------
//
// _DrawEnglishHint
//
//  Temporary English mode has no candidate words, so the body of the box is
//  otherwise empty. Draw a short usage hint there instead.
//
//----------------------------------------------------------------------------

void CCandidateWindow::_DrawEnglishHint(_In_ HDC dcHandle, _In_ RECT *prc)
{
    int cyLine = _cyRow;
    int x = prc->left + 2 * _TextMetric.tmAveCharWidth;

    SetBkMode(dcHandle, TRANSPARENT);
    SetTextColor(dcHandle, GetSysColor(COLOR_GRAYTEXT));

    ExtTextOut(dcHandle, x, prc->top + 2, 0, nullptr, L"回车输出英文", 6, NULL);
    ExtTextOut(dcHandle, x, prc->top + cyLine + 2, 0, nullptr, L"Esc键取消", 6, NULL);
}

//+---------------------------------------------------------------------------
//
// _DrawBorder
//
//----------------------------------------------------------------------------
void CCandidateWindow::_DrawBorder(_In_ HWND wndHandle, _In_ int cx)
{
    RECT rcWnd;

    HDC dcHandle = GetWindowDC(wndHandle);

    GetWindowRect(wndHandle, &rcWnd);
    // zero based
    OffsetRect(&rcWnd, -rcWnd.left, -rcWnd.top); 

    HPEN hPen = CreatePen(PS_SOLID, cx, CANDWND_BORDER_COLOR);
    HPEN hPenOld = (HPEN)SelectObject(dcHandle, hPen);
    HBRUSH hBorderBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hBorderBrushOld = (HBRUSH)SelectObject(dcHandle, hBorderBrush);

    Rectangle(dcHandle, rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom);

    SelectObject(dcHandle, hPenOld);
    SelectObject(dcHandle, hBorderBrushOld);
    DeleteObject(hPen);
    DeleteObject(hBorderBrush);
    ReleaseDC(wndHandle, dcHandle);

}

//+---------------------------------------------------------------------------
//
// _AddString
//
//----------------------------------------------------------------------------

void CCandidateWindow::_AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode)
{
    DWORD_PTR dwItemString = pCandidateItem->_ItemString.GetLength();
    const WCHAR* pwchString = nullptr;
    if (dwItemString)
    {
        pwchString = new (std::nothrow) WCHAR[ dwItemString ];
        if (!pwchString)
        {
            return;
        }
        memcpy((void*)pwchString, pCandidateItem->_ItemString.Get(), dwItemString * sizeof(WCHAR));
    }

    DWORD_PTR itemWildcard = pCandidateItem->_FindKeyCode.GetLength();
    const WCHAR* pwchWildcard = nullptr;
    if (itemWildcard && isAddFindKeyCode)
    {
        pwchWildcard = new (std::nothrow) WCHAR[ itemWildcard ];
        if (!pwchWildcard)
        {
            if (pwchString)
            {
                delete [] pwchString;
            }
            return;
        }
        memcpy((void*)pwchWildcard, pCandidateItem->_FindKeyCode.Get(), itemWildcard * sizeof(WCHAR));
    }

    CCandidateListItem* pLI = nullptr;
    pLI = _candidateList.Append();
    if (!pLI)
    {
        if (pwchString)
        {
            delete [] pwchString;
            pwchString = nullptr;
        }
        if (pwchWildcard)
        {
            delete [] pwchWildcard;
            pwchWildcard = nullptr;
        }
        return;
    }

    if (pwchString)
    {
        pLI->_ItemString.Set(pwchString, dwItemString);
    }
    if (pwchWildcard)
    {
        pLI->_FindKeyCode.Set(pwchWildcard, itemWildcard);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// _SetKeystrokeCode
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetKeystrokeCode(_In_reads_(length) const WCHAR *pwch, DWORD_PTR length)
{
    _keystrokeCodeLen = 0;
    _keystrokeCode[0] = L'\0';

    if (pwch && length > 0)
    {
        DWORD_PTR copyLen = min(length, static_cast<DWORD_PTR>(ENGLISH_MAX_CODE_LENGTH));
        memcpy(_keystrokeCode, pwch, copyLen * sizeof(WCHAR));
        _keystrokeCode[copyLen] = L'\0';
        _keystrokeCodeLen = copyLen;
    }

    _ResizeWindow();
}

//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CCandidateWindow::_ClearList()
{
    for (UINT index = 0; index < _candidateList.Count(); index++)
    {
        CCandidateListItem* pItemList = nullptr;
        pItemList = _candidateList.GetAt(index);
        delete [] pItemList->_ItemString.Get();
        delete [] pItemList->_FindKeyCode.Get();
    }
    _currentSelection = 0;
    _candidateList.Clear();
    _PageIndex.Clear();
}

//+---------------------------------------------------------------------------
//
// _SetScrollInfo
//
//----------------------------------------------------------------------------

void CCandidateWindow::_SetScrollInfo(_In_ int nMax, _In_ int nPage)
{
    // The candidate window no longer has a scrollbar; this is now a no-op.
    (void)nMax;
    (void)nPage;
}

//+---------------------------------------------------------------------------
//
// _GetCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetCandidateString(_In_ int iIndex, _Outptr_result_maybenull_z_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (iIndex < 0 )
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    UINT index = static_cast<UINT>(iIndex);
	
	if (index >= _candidateList.Count())
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(iIndex);
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD CCandidateWindow::_GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString)
{
    CCandidateListItem* pItemList = nullptr;

    if (_currentSelection >= _candidateList.Count())
    {
        *ppwchCandidateString = nullptr;
        return 0;
    }

    pItemList = _candidateList.GetAt(_currentSelection);
    if (ppwchCandidateString)
    {
        *ppwchCandidateString = pItemList->_ItemString.Get();
    }
    return (DWORD)pItemList->_ItemString.GetLength();
}

//+---------------------------------------------------------------------------
//
// _SetSelectionInPage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionInPage(int nPos)
{	
    if (nPos < 0)
    {
        return FALSE;
    }

    UINT pos = static_cast<UINT>(nPos);

    if (pos >= _candidateList.Count())
    {
        return FALSE;
    }

    int currentPage = 0;
    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    _currentSelection = *_PageIndex.GetAt(currentPage) + nPos;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MoveSelection(_In_ int offSet, _In_ BOOL isNotify)
{
    if (_currentSelection + offSet >= _candidateList.Count())
    {
        return FALSE;
    }

    _currentSelection += offSet;

    _dontAdjustOnEmptyItemPage = TRUE;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelection(_In_ int selectedIndex, _In_ BOOL isNotify)
{
    if (selectedIndex == -1)
    {
        selectedIndex = _candidateList.Count() - 1;
    }

    if (selectedIndex < 0)
    {
        return FALSE;
    }

    int candCnt = static_cast<int>(_candidateList.Count());
    if (selectedIndex >= candCnt)
    {
        return FALSE;
    }

    _currentSelection = static_cast<UINT>(selectedIndex);

    BOOL ret = _AdjustPageIndexForSelection();

    return ret;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------
void CCandidateWindow::_SetSelection(_In_ int nIndex)
{
    _currentSelection = nIndex;
}

//+---------------------------------------------------------------------------
//
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_MovePage(_In_ int offSet, _In_ BOOL isNotify)
{
    if (offSet == 0)
    {
        return TRUE;
    }

    int currentPage = 0;
    int selectionOffset = 0;
    int newPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    newPage = currentPage + offSet;
    if ((newPage < 0) || (newPage >= static_cast<int>(_PageIndex.Count())))
    {
        return FALSE;
    }

    // If current selection is at the top of the page AND 
    // we are on the "default" page border, then we don't
    // want adjustment to eliminate empty entries.
    //
    // We do this for keeping behavior inline with downlevel.
    if (_currentSelection % _pIndexRange->Count() == 0 && 
        _currentSelection == *_PageIndex.GetAt(currentPage)) 
    {
        _dontAdjustOnEmptyItemPage = TRUE;
    }

    selectionOffset = _currentSelection - *_PageIndex.GetAt(currentPage);
    _currentSelection = *_PageIndex.GetAt(newPage) + selectionOffset;
    _currentSelection = _candidateList.Count() > _currentSelection ? _currentSelection : _candidateList.Count() - 1;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _SetSelectionOffset
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_SetSelectionOffset(_In_ int offSet)
{
	if (_currentSelection + offSet >= _candidateList.Count())
    {
        return FALSE;
    }

    BOOL fCurrentPageHasEmptyItems = FALSE;
    BOOL fAdjustPageIndex = TRUE;

    _CurrentPageHasEmptyItems(&fCurrentPageHasEmptyItems);

    int newOffset = _currentSelection + offSet;

    // For SB_LINEUP and SB_LINEDOWN, we need to special case if CurrentPageHasEmptyItems.
    // CurrentPageHasEmptyItems if we are on the last page.
    if ((offSet == 1 || offSet == -1) &&
        fCurrentPageHasEmptyItems && _PageIndex.Count() > 1)
    {
        int iPageIndex = *_PageIndex.GetAt(_PageIndex.Count() - 1);
        // Moving on the last page and last page has empty items.
        if (newOffset >= iPageIndex)
        {
            fAdjustPageIndex = FALSE;
        }
        // Moving across page border.
        else if (newOffset < iPageIndex)
        {
            fAdjustPageIndex = TRUE;
        }

        _dontAdjustOnEmptyItemPage = TRUE;
    }

    _currentSelection = newOffset;

    if (fAdjustPageIndex)
    {
        return _AdjustPageIndexForSelection();
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _GetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt)
{
    HRESULT hr = S_OK;

    if (uSize > _PageIndex.Count())
    {
        uSize = _PageIndex.Count();
    }
    else
    {
        hr = S_FALSE;
    }

    if (pIndex)
    {
        for (UINT i = 0; i < uSize; i++)
        {
            *pIndex = *_PageIndex.GetAt(i);
            pIndex++;
        }
    }

    *puPageCnt = _PageIndex.Count();

    return hr;
}

//+---------------------------------------------------------------------------
//
// _SetPageIndex
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt)
{
    uPageCnt;

    _PageIndex.Clear();

    for (UINT i = 0; i < uPageCnt; i++)
    {
        UINT *pLastNewPageIndex = _PageIndex.Append();
        if (pLastNewPageIndex != nullptr)
        {
            *pLastNewPageIndex = *pIndex;
            pIndex++;
        }
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ UINT *pCurrentPage)
{
    HRESULT hr = S_OK;

    if (pCurrentPage == nullptr)
    {
        hr = E_INVALIDARG;
        goto Exit;
    }

    *pCurrentPage = 0;

    if (_PageIndex.Count() == 0)
    {
        hr = E_UNEXPECTED;
        goto Exit;
    }

    if (_PageIndex.Count() == 1)
    {
        *pCurrentPage = 0;
         goto Exit;
    }

    UINT i = 0;
    for (i = 1; i < _PageIndex.Count(); i++)
    {
        UINT uPageIndex = *_PageIndex.GetAt(i);

        if (uPageIndex > _currentSelection)
        {
            break;
        }
    }

    *pCurrentPage = i - 1;

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _GetCurrentPage
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_GetCurrentPage(_Inout_ int *pCurrentPage)
{
    HRESULT hr = E_FAIL;
    UINT needCastCurrentPage = 0;
    
    if (nullptr == pCurrentPage)
    {
        goto Exit;
    }

    *pCurrentPage = 0;

    hr = _GetCurrentPage(&needCastCurrentPage);
    if (FAILED(hr))
    {
       goto Exit;
    }

    hr = UIntToInt(needCastCurrentPage, pCurrentPage);
    if (FAILED(hr))
    {
        goto Exit;
    }

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _AdjustPageIndexForSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateWindow::_AdjustPageIndexForSelection()
{
    UINT candidateListPageCnt = _pIndexRange->Count();
    UINT* pNewPageIndex = nullptr;
    UINT newPageCnt = 0;

    if (_candidateList.Count() < candidateListPageCnt)
    {
        // no needed to restruct page index
        return TRUE;
    }

    // B is number of pages before the current page
    // A is number of pages after the current page
    // uNewPageCount is A + B + 1;
    // A is (uItemsAfter - 1) / candidateListPageCnt + 1 -> 
    //      (_CandidateListCount - _currentSelection - CandidateListPageCount - 1) / candidateListPageCnt + 1->
    //      (_CandidateListCount - _currentSelection - 1) / candidateListPageCnt
    // B is (uItemsBefore - 1) / candidateListPageCnt + 1 ->
    //      (_currentSelection - 1) / candidateListPageCnt + 1
    // A + B is (_CandidateListCount - 2) / candidateListPageCnt + 1

    BOOL isBefore = _currentSelection;
    BOOL isAfter = _candidateList.Count() > _currentSelection + candidateListPageCnt;

    // only have current page
    if (!isBefore && !isAfter) 
    {
        newPageCnt = 1;
    }
    // only have after pages; just count the total number of pages
    else if (!isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 1) / candidateListPageCnt + 1;
    }
    // we are at the last page
    else if (isBefore && !isAfter)
    {
        newPageCnt = 2 + (_currentSelection - 1) / candidateListPageCnt;
    }
    else if (isBefore && isAfter)
    {
        newPageCnt = (_candidateList.Count() - 2) / candidateListPageCnt + 2;
    }

    pNewPageIndex = new (std::nothrow) UINT[ newPageCnt ];
    if (pNewPageIndex == nullptr)
    {
        return FALSE;
    }
    pNewPageIndex[0] = 0;
    UINT firstPage = _currentSelection % candidateListPageCnt;
    if (firstPage && newPageCnt > 1) 
    {
        pNewPageIndex[1] = firstPage;
    }

    for (UINT i = firstPage ? 2 : 1; i < newPageCnt; ++i)
    {
        pNewPageIndex[i] = pNewPageIndex[i - 1] + candidateListPageCnt;
    }

    _SetPageIndex(pNewPageIndex, newPageCnt);

    delete [] pNewPageIndex;

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _AdjustTextColor
//
//----------------------------------------------------------------------------

COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor)
{
    if (!Global::IsTooSimilar(crColor, crBkColor))
    {
        return crColor;
    }
    else
    {
        return crColor ^ RGB(255, 255, 255);
    }
}

//+---------------------------------------------------------------------------
//
// _CurrentPageHasEmptyItems
//
//----------------------------------------------------------------------------

HRESULT CCandidateWindow::_CurrentPageHasEmptyItems(_Inout_ BOOL *hasEmptyItems)
{
    int candidateListPageCnt = _pIndexRange->Count();
    UINT currentPage = 0;

    if (FAILED(_GetCurrentPage(&currentPage)))
    {
        return S_FALSE;
    }

    if ((currentPage == 0 || currentPage == _PageIndex.Count()-1) &&
        (_PageIndex.Count() > 0) &&
        (*_PageIndex.GetAt(currentPage) > (UINT)(_candidateList.Count() - candidateListPageCnt)))
    {
        *hasEmptyItems = TRUE;
    }
    else 
    {
        *hasEmptyItems = FALSE;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _FireMessageToLightDismiss
//      fire EVENT_OBJECT_IME_xxx to let LightDismiss know about IME window.
//----------------------------------------------------------------------------

void CCandidateWindow::_FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos)
{
    if (nullptr == pWndPos)
    {
        return;
    }

    BOOL isShowWnd = ((pWndPos->flags & SWP_SHOWWINDOW) != 0);
    BOOL isHide = ((pWndPos->flags & SWP_HIDEWINDOW) != 0);
    BOOL needResize = ((pWndPos->flags & SWP_NOSIZE) == 0);
    BOOL needMove = ((pWndPos->flags & SWP_NOMOVE) == 0);
    BOOL needRedraw = ((pWndPos->flags & SWP_NOREDRAW) == 0);

    if (isShowWnd)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_SHOW, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (isHide)
    {
        NotifyWinEvent(EVENT_OBJECT_IME_HIDE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
    }
    else if (needResize || needMove || needRedraw)
    {
        if (IsWindowVisible(wndHandle))
        {
            NotifyWinEvent(EVENT_OBJECT_IME_CHANGE, wndHandle, OBJID_CLIENT, CHILDID_SELF);
        }
    }

}

HRESULT CCandidateWindow::_AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex)
{
    HRESULT hr = E_FAIL;
    UINT candidateListPageCnt = _pIndexRange->Count();

    currentPageIndex = *_PageIndex.GetAt(currentPage);

    BOOL hasEmptyItems = FALSE;
    if (FAILED(_CurrentPageHasEmptyItems(&hasEmptyItems)))
    {
        goto Exit; 
    }

    if (FALSE == hasEmptyItems)
    {
        goto Exit;
    }

    if (TRUE == _dontAdjustOnEmptyItemPage)
    {
        goto Exit;
    }

    UINT tempSelection = _currentSelection;

    // Last page
    UINT candNum = _candidateList.Count();
    UINT pageNum = _PageIndex.Count();

    if ((currentPageIndex > candNum - candidateListPageCnt) && (pageNum > 0) && (currentPage == (pageNum - 1)))
    {
        _currentSelection = candNum - candidateListPageCnt;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;

        if (FAILED(_GetCurrentPage(&currentPage)))
        {
            goto Exit;
        }

        currentPageIndex = *_PageIndex.GetAt(currentPage);
    }
    // First page
    else if ((currentPageIndex < candidateListPageCnt) && (currentPage == 0))
    {
        _currentSelection = 0;

        _AdjustPageIndexForSelection();

        _currentSelection = tempSelection;
    }

    _dontAdjustOnEmptyItemPage = FALSE;
    hr = S_OK;

Exit:
    return hr;
}
void CCandidateWindow::_DeleteShadowWnd()
{
    if (nullptr != _pShadowWnd)
    {
        delete _pShadowWnd;
        _pShadowWnd = nullptr;
    }
}

