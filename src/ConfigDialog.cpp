// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"

#include "ConfigDialog.h"

#include "Define.h"     // candidate-window color scheme (CANDWND_*)
#include "Version.h"

#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "Globals.h"
#include "Compartment.h"
#include "StatusWindow.h"
#include "RegKey.h"

#include <shellapi.h>

// Control identifiers (local to this file; no .rc resource needed).
enum
{
    IDC_CAT_LIST        = 100,

    IDC_PANEL_INPUT     = 200,
    IDC_PANEL_CAND      = 201,
    IDC_PANEL_UI        = 202,
    IDC_PANEL_HOTKEY    = 203,
    IDC_PANEL_ABOUT     = 204,

    IDC_GRP_DSB         = 210,
    IDC_RADIO_HALF      = 211,
    IDC_RADIO_FULL      = 212,
    IDC_GRP_PUNCT       = 213,
    IDC_RADIO_PUNCT_CN  = 214,
    IDC_RADIO_PUNCT_EN  = 215,
    IDC_CHK_ONLYCOMMON  = 216,
    IDC_CHK_DIGIT_EN_PUNCT = 217,
    IDC_CHK_EMPTY_CODE_FULL = 218,

    IDC_ST_PAGECNT_LBL  = 220,
    IDC_CMB_PAGECNT     = 221,

    IDC_CHK_SHOWSTATUS  = 230,
    IDC_ST_FONTSIZE_LBL = 231,
    IDC_CMB_FONTSIZE    = 232,
    IDC_ST_PREVIEW_LBL  = 233,
    IDC_PREVIEW         = 234,

    IDC_CHK_HOTKEY_ONLYCOMMON = 240,
    IDC_CHK_HOTKEY_PUNCTUATION = 241,
    IDC_CHK_HOTKEY_FULLHALF = 242,

    IDC_ST_ABOUT        = 243,
    IDC_ST_ABOUT_LINK   = 244,
    IDC_ST_ABOUT_COPY   = 245,

    IDC_BTN_OK          = 250,
    IDC_BTN_CANCEL      = 251,
    IDC_BTN_APPLY       = 252
};

// Sample key used by the candidate-window preview on the Interface tab.
static const WCHAR kPreviewKey[] = L"aa";
static const int kPreviewCandMax = 5;

struct PreviewCand
{
    WCHAR word[32];
    WCHAR suffix[8];
};

struct DlgState
{
    CDIME*   pTextService = nullptr;
    HWND     hwndOwner    = nullptr;
    HWND     hwndCatList  = nullptr;
    HWND     hwndClose    = nullptr;
    HWND     hwndPanels[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    HWND     hwndPreview  = nullptr;
    RECT     rcList       = {0,0,0,0};
    HFONT    hFont        = nullptr;
    HFONT    hPreviewFont = nullptr;
    int      previewFontPx = 0;
    PreviewCand previewCands[kPreviewCandMax] = {};
    int      previewCandCount = 0;
};

static WNDPROC s_prevPreviewWndProc = nullptr;
static WNDPROC s_prevAboutLinkWndProc = nullptr;

static const WCHAR kRepoUrl[] = L"https://github.com/cnDenis/DIME";

static const int kFontSizeChoices[] = {0, 12, 14, 16, 18, 20, 24, 28, 32};

static LRESULT CALLBACK _AboutLinkWndProc(_In_ HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_SETCURSOR)
    {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    if (s_prevAboutLinkWndProc != nullptr)
    {
        return CallWindowProc(s_prevAboutLinkWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static BOOL _GetCompartmentBool(_In_ ITfThreadMgr* tm, TfClientId id, REFGUID guid, _Out_ BOOL& out)
{
    CCompartment c(tm, id, guid);
    return SUCCEEDED(c._GetCompartmentBOOL(out));
}

static BOOL _RegGetDWORD(_In_z_ LPCWSTR name, DWORD defaultValue, _Out_ DWORD& out)
{
    out = defaultValue;
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") != ERROR_SUCCESS)
    {
        return FALSE;
    }
    DWORD dw = 0;
    if (reg.QueryDWORDValue(name, dw) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    out = dw;
    return TRUE;
}

static void _RegSetDWORD(_In_z_ LPCWSTR name, DWORD value)
{
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(name, value);
    }
}

static HWND _CreateCtrl(_In_ HWND parent, _In_ LPCWSTR cls, _In_opt_ LPCWSTR text,
                        DWORD style, int x, int y, int w, int h, int id, _In_opt_ HFONT hFont)
{
    HWND hw = CreateWindowEx(0, cls, text, style, x, y, w, h, parent,
                            (HMENU)(INT_PTR)id, Global::dllInstanceHandle, nullptr);
    if (hw != nullptr && hFont != nullptr)
    {
        SendMessage(hw, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    return hw;
}

static WCHAR _ToLowerPreviewChar(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') ? (WCHAR)(ch - L'A' + L'a') : ch;
}

static int _GetComboFontPx(_In_ DlgState* pState)
{
    HWND cmb = (pState->hwndPanels[2] != nullptr)
        ? GetDlgItem(pState->hwndPanels[2], IDC_CMB_FONTSIZE) : nullptr;
    int sel = 0;
    if (cmb != nullptr)
    {
        sel = (int)SendMessage(cmb, CB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= ARRAYSIZE(kFontSizeChoices))
        {
            sel = 0;
        }
    }
    int px = kFontSizeChoices[sel];
    if (px == 0)
    {
        px = CCompositionProcessorEngine::GetAutoCandidateFontSize();
    }
    return px;
}

static void _LoadPreviewCandidates(_In_ DlgState* pState)
{
    pState->previewCandCount = 0;
    CCompositionProcessorEngine* eng =
        (pState->pTextService != nullptr) ? pState->pTextService->GetCompositionProcessorEngine() : nullptr;

    if (eng != nullptr)
    {
        CDIMEArray<CCandidateListItem> list;
        eng->GetPreviewCandidateList(kPreviewKey, &list, kPreviewCandMax);
        UINT n = min(list.Count(), (UINT)kPreviewCandMax);
        for (UINT i = 0; i < n; ++i)
        {
            CCandidateListItem* pLI = list.GetAt(i);
            PreviewCand& c = pState->previewCands[pState->previewCandCount];
            c.word[0] = L'\0';
            c.suffix[0] = L'\0';
            if (pLI->_ItemString.GetLength() > 0)
            {
                size_t len = (size_t)pLI->_ItemString.GetLength();
                if (len >= ARRAYSIZE(c.word))
                {
                    len = ARRAYSIZE(c.word) - 1;
                }
                memcpy(c.word, pLI->_ItemString.Get(), len * sizeof(WCHAR));
                c.word[len] = L'\0';
            }
            if (pLI->_FindKeyCode.GetLength() > 0)
            {
                size_t len = (size_t)pLI->_FindKeyCode.GetLength();
                if (len >= ARRAYSIZE(c.suffix))
                {
                    len = ARRAYSIZE(c.suffix) - 1;
                }
                for (size_t k = 0; k < len; ++k)
                {
                    c.suffix[k] = _ToLowerPreviewChar(pLI->_FindKeyCode.Get()[k]);
                }
                c.suffix[len] = L'\0';
            }
            if (c.word[0] != L'\0')
            {
                pState->previewCandCount++;
            }
        }
    }

    // Fallback when the dictionary is not ready yet.
    if (pState->previewCandCount == 0)
    {
        static const PreviewCand kFallback[] = {
            { L"式", L"" },
            { L"藏", L"" },
            { L"戒", L"" },
            { L"工", L"a" },
            { L"藏匿", L"aa" },
        };
        for (int i = 0; i < ARRAYSIZE(kFallback) && i < kPreviewCandMax; ++i)
        {
            pState->previewCands[i] = kFallback[i];
        }
        pState->previewCandCount = min(ARRAYSIZE(kFallback), kPreviewCandMax);
    }
}

static void _PaintCandidatePreview(_In_ HWND hWnd, _In_opt_ DlgState* pState)
{
    PAINTSTRUCT ps = {0};
    HDC dc = BeginPaint(hWnd, &ps);
    RECT rc = {0};
    GetClientRect(hWnd, &rc);

    // Full clear first so a smaller font / shorter window never leaves
    // leftover border or glyph pixels from the previous paint.
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));

    if (pState == nullptr || pState->hPreviewFont == nullptr)
    {
        EndPaint(hWnd, &ps);
        return;
    }

    HFONT hOldFont = (HFONT)SelectObject(dc, pState->hPreviewFont);
    TEXTMETRIC tm = {0};
    GetTextMetrics(dc, &tm);
    int cyRow = max(tm.tmHeight, pState->previewFontPx);
    int cxLine = tm.tmAveCharWidth;
    int cyOffset = (cyRow - tm.tmHeight) / 2;
    int pad = CANDWND_BORDER_WIDTH;
    int y = pad;

    // Header row: encoding "aa".
    RECT rcHeader = { pad, y, rc.right - pad, y + cyRow };
    FillRect(dc, &rcHeader, GetSysColorBrush(CANDWND_HEADER_BK_SYSCOLOR));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, CANDWND_NUM_COLOR);
    SIZE numSize = {0};
    GetTextExtentPoint32(dc, L"8. ", 3, &numSize);
    int textLeft = pad + cxLine + numSize.cx;
    ExtTextOut(dc, textLeft, y + cyOffset, 0, nullptr, kPreviewKey, (int)wcslen(kPreviewKey), nullptr);
    y += cyRow;

    // Candidate rows.
    for (int i = 0; i < pState->previewCandCount; ++i)
    {
        RECT rcRow = { pad, y, rc.right - pad, y + cyRow };
        FillRect(dc, &rcRow, GetSysColorBrush(COLOR_3DHIGHLIGHT));

        WCHAR numBuf[16] = {0};
        int digit = (i == 9) ? 0 : (i + 1);
        StringCchPrintf(numBuf, ARRAYSIZE(numBuf), L"%d. ", digit);

        SetTextColor(dc, CANDWND_NUM_COLOR);
        ExtTextOut(dc, pad + cxLine, y + cyOffset, 0, nullptr, numBuf, (int)wcslen(numBuf), nullptr);

        SIZE nsz = {0};
        GetTextExtentPoint32(dc, numBuf, (int)wcslen(numBuf), &nsz);
        int candLeft = pad + cxLine + nsz.cx;

        SetTextColor(dc, CANDWND_ITEM_COLOR);
        const PreviewCand& c = pState->previewCands[i];
        ExtTextOut(dc, candLeft, y + cyOffset, 0, nullptr, c.word, (int)wcslen(c.word), nullptr);

        if (c.suffix[0] != L'\0')
        {
            SIZE wsz = {0};
            GetTextExtentPoint32(dc, c.word, (int)wcslen(c.word), &wsz);
            SetTextColor(dc, CANDWND_NUM_COLOR);
            ExtTextOut(dc, candLeft + wsz.cx, y + cyOffset, 0, nullptr, c.suffix, (int)wcslen(c.suffix), nullptr);
        }

        y += cyRow;
    }

    // Border last, fully inside the client (avoids thick-pen clipping residue).
    HBRUSH hbrBorder = CreateSolidBrush(CANDWND_BORDER_COLOR);
    if (hbrBorder != nullptr)
    {
        RECT rcBorder = rc;
        for (int i = 0; i < CANDWND_BORDER_WIDTH; ++i)
        {
            FrameRect(dc, &rcBorder, hbrBorder);
            InflateRect(&rcBorder, -1, -1);
        }
        DeleteObject(hbrBorder);
    }

    SelectObject(dc, hOldFont);
    EndPaint(hWnd, &ps);
}

static LRESULT CALLBACK _PreviewWndProc(_In_ HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DlgState* pState = (DlgState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (uMsg)
    {
    case WM_PAINT:
        _PaintCandidatePreview(hWnd, pState);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return CallWindowProc(s_prevPreviewWndProc, hWnd, uMsg, wParam, lParam);
}

static void _UpdatePreviewLayout(_In_ DlgState* pState)
{
    if (pState == nullptr || pState->hwndPreview == nullptr)
    {
        return;
    }

    int px = _GetComboFontPx(pState);
    if (pState->hPreviewFont == nullptr || pState->previewFontPx != px)
    {
        WCHAR fontName[50] = {L'\0'};
        LoadString(Global::dllInstanceHandle, IDS_DEFAULT_FONT, fontName, 50);
        HFONT hNew = CreateFont(-px, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, fontName);
        if (hNew == nullptr)
        {
            LOGFONT lf = {0};
            SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
            hNew = CreateFont(-px, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, lf.lfFaceName);
        }
        if (hNew != nullptr)
        {
            HFONT hOld = pState->hPreviewFont;
            pState->hPreviewFont = hNew;
            pState->previewFontPx = px;
            if (hOld != nullptr)
            {
                DeleteObject(hOld);
            }
        }
    }

    int cyRow = max(px, 16);
    // Measure real glyph height when possible.
    HDC dc = GetDC(pState->hwndPreview);
    if (dc != nullptr && pState->hPreviewFont != nullptr)
    {
        HFONT hOld = (HFONT)SelectObject(dc, pState->hPreviewFont);
        TEXTMETRIC tm = {0};
        GetTextMetrics(dc, &tm);
        SelectObject(dc, hOld);
        ReleaseDC(pState->hwndPreview, dc);
        cyRow = max(tm.tmHeight, px);
    }

    int rows = 1 + max(pState->previewCandCount, 1);
    int h = 2 * CANDWND_BORDER_WIDTH + rows * cyRow;
    int w = CAND_WINDOW_WIDTH_PX;

    // Invalidate the parent over the old+new preview rect so shrinking the
    // control does not leave the previous left/bottom border on the panel.
    HWND parent = GetParent(pState->hwndPreview);
    RECT rcOld = {0};
    GetWindowRect(pState->hwndPreview, &rcOld);
    if (parent != nullptr)
    {
        MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&rcOld, 2);
    }

    SetWindowPos(pState->hwndPreview, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);

    if (parent != nullptr)
    {
        RECT rcNew = {0};
        GetWindowRect(pState->hwndPreview, &rcNew);
        MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&rcNew, 2);
        RECT rcUnion = {0};
        UnionRect(&rcUnion, &rcOld, &rcNew);
        InvalidateRect(parent, &rcUnion, TRUE);
    }
    InvalidateRect(pState->hwndPreview, nullptr, TRUE);
}

// The config dialog reuses the candidate window's color scheme:
//   background  = COLOR_WINDOW (white)
//   text        = CANDWND_ITEM_COLOR (near-black)
//   border      = CANDWND_BORDER_COLOR (soft azure), 1px
//   selection   = CANDWND_SELECTED_BK_COLOR (azure) / CANDWND_SELECTED_ITEM_COLOR (white)
// Use a real system brush for WM_CTLCOLOR* / FillRect. The WNDCLASS form
// (COLOR_WINDOW+1) is only valid as hbrBackground and paints gray boxes if
// returned from CTLCOLOR for radios / checkboxes / group boxes.
static HBRUSH _DimeBkBrush() { return GetSysColorBrush(COLOR_WINDOW); }
static const int _DimeBorderWidth = 1;   // 1px borders for the config dialog

// Light-blue button palette (normal / hover / pressed), matched to the azure kit.
static const COLORREF _DimeBtnFace    = RGB(0xE4, 0xEF, 0xFB);
static const COLORREF _DimeBtnHover   = RGB(0xCF, 0xE4, 0xFA);
static const COLORREF _DimeBtnPressed = RGB(0xB8, 0xD6, 0xF5);

// Owner-draw a light-blue push button with an azure border.
static void _DrawDimeButton(_In_ const DRAWITEMSTRUCT* p, _In_opt_ HFONT hFont)
{
    if (p == nullptr)
    {
        return;
    }

    BOOL pressed = (p->itemState & ODS_SELECTED) != 0;
    BOOL hot     = (p->itemState & ODS_HOTLIGHT) != 0;
    COLORREF face = pressed ? _DimeBtnPressed : (hot ? _DimeBtnHover : _DimeBtnFace);

    RECT rc = p->rcItem;
    HBRUSH hbrFace = CreateSolidBrush(face);
    FillRect(p->hDC, &rc, hbrFace);
    DeleteObject(hbrFace);

    HPEN hPen = CreatePen(PS_SOLID, 1, CANDWND_BORDER_COLOR);
    HPEN hOldPen = (HPEN)SelectObject(p->hDC, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(p->hDC, (HBRUSH)GetStockObject(NULL_BRUSH));
    Rectangle(p->hDC, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(p->hDC, hOldPen);
    SelectObject(p->hDC, hOldBr);
    DeleteObject(hPen);

    WCHAR text[32] = {0};
    GetWindowText(p->hwndItem, text, ARRAYSIZE(text));
    SetBkMode(p->hDC, TRANSPARENT);
    SetTextColor(p->hDC, CANDWND_ITEM_COLOR);
    HFONT f = (hFont != nullptr) ? hFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT fOld = (HFONT)SelectObject(p->hDC, f);
    DrawText(p->hDC, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(p->hDC, fOld);

    if (p->itemState & ODS_FOCUS)
    {
        RECT rcFocus = rc;
        InflateRect(&rcFocus, -3, -3);
        DrawFocusRect(p->hDC, &rcFocus);
    }
}

// Panel window proc: white fill + azure border, and forwards control-color
// requests from its child controls (group boxes / radios / checkboxes / text).
static LRESULT CALLBACK _PanelWndProc(_In_ HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_COMMAND:
        {
            // Child controls (combobox / radios) send WM_COMMAND to their
            // parent panel; forward to the dialog so live preview can update.
            HWND parent = GetParent(hWnd);
            if (parent != nullptr)
            {
                return SendMessage(parent, WM_COMMAND, wParam, lParam);
            }
            break;
        }

    case WM_PAINT:
        {
            PAINTSTRUCT ps = {0};
            HDC dc = BeginPaint(hWnd, &ps);
            RECT rc = {0};
            GetClientRect(hWnd, &rc);
            FillRect(dc, &rc, _DimeBkBrush());
            HPEN hPen = CreatePen(PS_SOLID, _DimeBorderWidth, CANDWND_BORDER_COLOR);
            HPEN hOldPen = (HPEN)SelectObject(dc, hPen);
            HBRUSH hOldBr = (HBRUSH)SelectObject(dc, (HBRUSH)GetStockObject(NULL_BRUSH));
            Rectangle(dc, 0, 0, rc.right, rc.bottom);
            SelectObject(dc, hOldPen);
            SelectObject(dc, hOldBr);
            DeleteObject(hPen);
            EndPaint(hWnd, &ps);
            return 0;
        }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
        {
            // White background for child controls, including the combobox
            // (closed display + drop-down list).
            HDC dc = (HDC)wParam;
            HWND hwndCtrl = (HWND)lParam;
            SetBkMode(dc, TRANSPARENT);
            if (GetDlgCtrlID(hwndCtrl) == IDC_ST_ABOUT_LINK)
            {
                SetTextColor(dc, RGB(0x06, 0x6A, 0xCE));
            }
            else
            {
                SetTextColor(dc, CANDWND_ITEM_COLOR);
            }
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)_DimeBkBrush();
        }
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void _ShowCategory(_In_ DlgState* pState, int sel)
{
    for (int i = 0; i < 5; ++i)
    {
        if (pState->hwndPanels[i] != nullptr)
        {
            ShowWindow(pState->hwndPanels[i], (i == sel) ? SW_SHOW : SW_HIDE);
        }
    }
}

static void _InitStates(_In_ DlgState* pState)
{
    CDIME* p = pState->pTextService;
    if (p == nullptr)
    {
        return;
    }

    ITfThreadMgr* tm = p->_GetThreadMgr();
    TfClientId id = p->_GetClientId();
    CCompositionProcessorEngine* eng = p->GetCompositionProcessorEngine();

    // --- Input mode ---
    // Prefer live compartments when activated; otherwise read the registry
    // (ITfFnConfigure CoCreates an unactivated CDIME from Settings).
    BOOL fullWidth = FALSE, punctCN = TRUE, onlyCommon = FALSE;
    BOOL digitEnPunct = TRUE, emptyCodeFull = TRUE;
    if (tm != nullptr)
    {
        _GetCompartmentBool(tm, id, Global::DIMEGuidCompartmentDoubleSingleByte, fullWidth);
        _GetCompartmentBool(tm, id, Global::DIMEGuidCompartmentPunctuation, punctCN);
        _GetCompartmentBool(tm, id, Global::DIMEGuidCompartmentOnlyCommon, onlyCommon);
    }
    else
    {
        DWORD dw = 0;
        if (_RegGetDWORD(L"DoubleSingleByte", 0, dw)) fullWidth = (dw != 0);
        if (_RegGetDWORD(L"Punctuation", 1, dw)) punctCN = (dw != 0);
        if (_RegGetDWORD(L"OnlyCommon", 0, dw)) onlyCommon = (dw != 0);
    }

    if (eng != nullptr)
    {
        digitEnPunct = eng->IsEnglishCommaPeriodAfterDigit();
        emptyCodeFull = eng->IsEmptyCodeSearchFull();
    }
    else
    {
        DWORD dw = 1;
        if (_RegGetDWORD(L"EnglishCommaPeriodAfterDigit", 1, dw)) digitEnPunct = (dw != 0);
        if (_RegGetDWORD(L"EmptyCodeSearchFull", 1, dw)) emptyCodeFull = (dw != 0);
    }

    HWND panel = pState->hwndPanels[0];
    CheckRadioButton(panel, IDC_RADIO_HALF, IDC_RADIO_FULL, fullWidth ? IDC_RADIO_FULL : IDC_RADIO_HALF);
    CheckRadioButton(panel, IDC_RADIO_PUNCT_CN, IDC_RADIO_PUNCT_EN, punctCN ? IDC_RADIO_PUNCT_CN : IDC_RADIO_PUNCT_EN);
    CheckDlgButton(panel, IDC_CHK_ONLYCOMMON, onlyCommon ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(panel, IDC_CHK_DIGIT_EN_PUNCT, digitEnPunct ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(panel, IDC_CHK_EMPTY_CODE_FULL, emptyCodeFull ? BST_CHECKED : BST_UNCHECKED);

    // --- Candidate options (page size) ---
    {
        int ps = 10;
        if (eng != nullptr)
        {
            ps = eng->GetCandidatePageSize();
        }
        else
        {
            DWORD dw = 10;
            _RegGetDWORD(L"CandidatesPerPage", 10, dw);
            ps = (int)dw;
        }
        if (ps < 1) ps = 1;
        if (ps > 10) ps = 10;
        HWND cmb = GetDlgItem(pState->hwndPanels[1], IDC_CMB_PAGECNT);
        if (cmb != nullptr)
        {
            SendMessage(cmb, CB_SETCURSEL, ps - 1, 0);
        }
    }

    // --- Interface ---
    BOOL hidden = (p->_GetStatusWindow() != nullptr) ? p->_GetStatusWindow()->_IsHiddenByUser() : FALSE;
    CheckDlgButton(pState->hwndPanels[2], IDC_CHK_SHOWSTATUS, hidden ? BST_UNCHECKED : BST_CHECKED);

    {
        int px = 0;
        if (eng != nullptr)
        {
            px = eng->GetCandidateFontSize();
        }
        else
        {
            DWORD dw = 0;
            _RegGetDWORD(L"CandidateFontSize", 0, dw);
            px = (int)dw;
        }
        HWND cmbFont = GetDlgItem(pState->hwndPanels[2], IDC_CMB_FONTSIZE);
        if (cmbFont != nullptr)
        {
            int sel = 0;
            for (int i = 0; i < ARRAYSIZE(kFontSizeChoices); ++i)
            {
                if (kFontSizeChoices[i] == px)
                {
                    sel = i;
                    break;
                }
            }
            SendMessage(cmbFont, CB_SETCURSEL, sel, 0);
        }
    }

    // --- Hotkeys ---
    BOOL hkOnly = TRUE, hkPunct = TRUE, hkFull = TRUE;
    if (eng != nullptr)
    {
        hkOnly = eng->IsHotkeyOnlyCommonEnabled();
        hkPunct = eng->IsHotkeyPunctuationEnabled();
        hkFull = eng->IsHotkeyDoubleSingleByteEnabled();
    }
    else
    {
        DWORD dw = 1;
        if (_RegGetDWORD(L"HotkeyOnlyCommon", 1, dw)) hkOnly = (dw != 0);
        if (_RegGetDWORD(L"HotkeyPunctuation", 1, dw)) hkPunct = (dw != 0);
        if (_RegGetDWORD(L"HotkeyDoubleSingleByte", 1, dw)) hkFull = (dw != 0);
    }
    CheckDlgButton(pState->hwndPanels[3], IDC_CHK_HOTKEY_ONLYCOMMON, hkOnly ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(pState->hwndPanels[3], IDC_CHK_HOTKEY_PUNCTUATION, hkPunct ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(pState->hwndPanels[3], IDC_CHK_HOTKEY_FULLHALF, hkFull ? BST_CHECKED : BST_UNCHECKED);

    _UpdatePreviewLayout(pState);
}

static void _OnSettingChanged(_In_ DlgState* pState)
{
    CDIME* p = pState->pTextService;
    if (p == nullptr)
    {
        return;
    }

    ITfThreadMgr* tm = p->_GetThreadMgr();
    TfClientId id = p->_GetClientId();
    CCompositionProcessorEngine* eng = p->GetCompositionProcessorEngine();

    BOOL fullWidth = (IsDlgButtonChecked(pState->hwndPanels[0], IDC_RADIO_FULL) == BST_CHECKED);
    BOOL punctCN   = (IsDlgButtonChecked(pState->hwndPanels[0], IDC_RADIO_PUNCT_CN) == BST_CHECKED);
    BOOL onlyCommon = (IsDlgButtonChecked(pState->hwndPanels[0], IDC_CHK_ONLYCOMMON) == BST_CHECKED);
    BOOL digitEnPunct = (IsDlgButtonChecked(pState->hwndPanels[0], IDC_CHK_DIGIT_EN_PUNCT) == BST_CHECKED);
    BOOL emptyCodeFull = (IsDlgButtonChecked(pState->hwndPanels[0], IDC_CHK_EMPTY_CODE_FULL) == BST_CHECKED);

    int ps = 10;
    HWND cmb = GetDlgItem(pState->hwndPanels[1], IDC_CMB_PAGECNT);
    if (cmb != nullptr)
    {
        ps = (int)SendMessage(cmb, CB_GETCURSEL, 0, 0);
        if (ps < 0) ps = 9;
        ps += 1;
    }

    int fontPx = 0;
    HWND cmbFont = GetDlgItem(pState->hwndPanels[2], IDC_CMB_FONTSIZE);
    if (cmbFont != nullptr)
    {
        int sel = (int)SendMessage(cmbFont, CB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= ARRAYSIZE(kFontSizeChoices))
        {
            sel = 0;
        }
        fontPx = kFontSizeChoices[sel];
    }

    BOOL hkOnly = (IsDlgButtonChecked(pState->hwndPanels[3], IDC_CHK_HOTKEY_ONLYCOMMON) == BST_CHECKED);
    BOOL hkPunct = (IsDlgButtonChecked(pState->hwndPanels[3], IDC_CHK_HOTKEY_PUNCTUATION) == BST_CHECKED);
    BOOL hkFull = (IsDlgButtonChecked(pState->hwndPanels[3], IDC_CHK_HOTKEY_FULLHALF) == BST_CHECKED);

    if (eng != nullptr)
    {
        if (tm != nullptr)
        {
            eng->SetDoubleSingleByte(tm, id, fullWidth);
            eng->SetPunctuation(tm, id, punctCN);
            eng->SetOnlyCommon(tm, id, onlyCommon);
        }
        eng->SetEnglishCommaPeriodAfterDigit(digitEnPunct);
        eng->SetEmptyCodeSearchFull(emptyCodeFull);
        eng->SetCandidatePageSize(ps);
        eng->SetCandidateFontSize(fontPx);
        eng->SetHotkeyOnlyCommonEnabled(hkOnly);
        eng->SetHotkeyPunctuationEnabled(hkPunct);
        eng->SetHotkeyDoubleSingleByteEnabled(hkFull);
    }
    else
    {
        // Unactivated instance from Settings Options: persist to registry only.
        // Running IME processes pick these up on next Activate / restart.
        _RegSetDWORD(L"DoubleSingleByte", fullWidth ? 1 : 0);
        _RegSetDWORD(L"Punctuation", punctCN ? 1 : 0);
        _RegSetDWORD(L"OnlyCommon", onlyCommon ? 1 : 0);
        _RegSetDWORD(L"EnglishCommaPeriodAfterDigit", digitEnPunct ? 1 : 0);
        _RegSetDWORD(L"EmptyCodeSearchFull", emptyCodeFull ? 1 : 0);
        _RegSetDWORD(L"CandidatesPerPage", (DWORD)ps);
        _RegSetDWORD(L"CandidateFontSize", (DWORD)fontPx);
        _RegSetDWORD(L"HotkeyOnlyCommon", hkOnly ? 1 : 0);
        _RegSetDWORD(L"HotkeyPunctuation", hkPunct ? 1 : 0);
        _RegSetDWORD(L"HotkeyDoubleSingleByte", hkFull ? 1 : 0);
    }

    BOOL showStatus = (IsDlgButtonChecked(pState->hwndPanels[2], IDC_CHK_SHOWSTATUS) == BST_CHECKED);
    CStatusWindow* sw = p->_GetStatusWindow();
    if (sw != nullptr)
    {
        sw->_SetHiddenByUser(!showStatus);
        sw->_Show(showStatus ? TRUE : FALSE);
    }
}

static void _CreateControls(_In_ HWND hWnd, _In_ DlgState* pState)
{
    RECT rc = {0};
    GetClientRect(hWnd, &rc);
    int cx = rc.right - rc.left;
    int cy = rc.bottom - rc.top;

    int listW = 150;
    int listX = 12;
    int listY = 12;
    int listH = cy - 64;            // leave room for the close button at bottom
    int panelX = listX + listW + 14;
    int panelW = cx - panelX - 12;
    int panelH = listH;

    pState->hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // Left category list (acts as the tab selector). Standard listbox so the
    // category text always renders; the azure border is drawn by the parent.
    pState->rcList = { listX, listY, listX + listW, listY + listH };
    pState->hwndCatList = _CreateCtrl(hWnd, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS,
        listX + 1, listY + 1, listW - 2, listH - 2, IDC_CAT_LIST, pState->hFont);
    if (pState->hwndCatList != nullptr)
    {
        SendMessage(pState->hwndCatList, LB_ADDSTRING, 0, (LPARAM)L"输入模式");
        SendMessage(pState->hwndCatList, LB_ADDSTRING, 0, (LPARAM)L"候选设置");
        SendMessage(pState->hwndCatList, LB_ADDSTRING, 0, (LPARAM)L"界面");
        SendMessage(pState->hwndCatList, LB_ADDSTRING, 0, (LPARAM)L"快捷键");
        SendMessage(pState->hwndCatList, LB_ADDSTRING, 0, (LPARAM)L"关于");
        SendMessage(pState->hwndCatList, LB_SETCURSEL, 0, 0);
    }

    // Right panels. No system border — they draw their own azure border.
    DWORD panelStyle = WS_CHILD;
    pState->hwndPanels[0] = _CreateCtrl(hWnd, L"STATIC", nullptr, panelStyle | WS_VISIBLE, panelX, listY, panelW, panelH, IDC_PANEL_INPUT, pState->hFont);
    pState->hwndPanels[1] = _CreateCtrl(hWnd, L"STATIC", nullptr, panelStyle, panelX, listY, panelW, panelH, IDC_PANEL_CAND, pState->hFont);
    pState->hwndPanels[2] = _CreateCtrl(hWnd, L"STATIC", nullptr, panelStyle, panelX, listY, panelW, panelH, IDC_PANEL_UI, pState->hFont);
    pState->hwndPanels[3] = _CreateCtrl(hWnd, L"STATIC", nullptr, panelStyle, panelX, listY, panelW, panelH, IDC_PANEL_HOTKEY, pState->hFont);
    pState->hwndPanels[4] = _CreateCtrl(hWnd, L"STATIC", nullptr, panelStyle, panelX, listY, panelW, panelH, IDC_PANEL_ABOUT, pState->hFont);
    for (int i = 0; i < 5; ++i)
    {
        if (pState->hwndPanels[i] != nullptr)
        {
            SetWindowLongPtr(pState->hwndPanels[i], GWLP_WNDPROC, (LONG_PTR)_PanelWndProc);
        }
    }

    // --- Input mode panel ---
    HWND panel = pState->hwndPanels[0];
    int pw = panelW;
    _CreateCtrl(panel, L"BUTTON", L"全角 / 半角", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        12, 12, pw - 24, 96, IDC_GRP_DSB, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"半角", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        28, 40, 130, 24, IDC_RADIO_HALF, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"全角", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        28, 68, 130, 24, IDC_RADIO_FULL, pState->hFont);

    _CreateCtrl(panel, L"BUTTON", L"中 / 英文标点", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        12, 120, pw - 24, 96, IDC_GRP_PUNCT, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"中文标点", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON,
        28, 148, 140, 24, IDC_RADIO_PUNCT_CN, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"英文标点", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        28, 176, 140, 24, IDC_RADIO_PUNCT_EN, pState->hFont);

    _CreateCtrl(panel, L"BUTTON", L"仅输出常用字（GB2312 常用字）", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 236, pw - 32, 24, IDC_CHK_ONLYCOMMON, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"空码时检索全码表", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 268, pw - 32, 24, IDC_CHK_EMPTY_CODE_FULL, pState->hFont);
    _CreateCtrl(panel, L"BUTTON", L"数字后 , . 使用英文标点", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 300, pw - 32, 24, IDC_CHK_DIGIT_EN_PUNCT, pState->hFont);

    // --- Candidate panel ---
    HWND cpanel = pState->hwndPanels[1];
    _CreateCtrl(cpanel, L"STATIC", L"每页候选字数：", WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 20, pw - 32, 24, IDC_ST_PAGECNT_LBL, pState->hFont);
    HWND hwndCombo = _CreateCtrl(cpanel, L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        16, 48, 120, 200, IDC_CMB_PAGECNT, pState->hFont);
    if (hwndCombo != nullptr)
    {
        for (int i = 1; i <= 10; ++i)
        {
            WCHAR buf[8] = {0};
            StringCchPrintf(buf, ARRAYSIZE(buf), L"%d", i);
            SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)buf);
        }
    }

    // --- Interface panel ---
    _CreateCtrl(pState->hwndPanels[2], L"BUTTON", L"显示浮动状态栏", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 20, pw - 32, 24, IDC_CHK_SHOWSTATUS, pState->hFont);
    _CreateCtrl(pState->hwndPanels[2], L"STATIC", L"候选框字号：", WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 56, pw - 32, 24, IDC_ST_FONTSIZE_LBL, pState->hFont);
    HWND hwndFontCombo = _CreateCtrl(pState->hwndPanels[2], L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        16, 84, 120, 220, IDC_CMB_FONTSIZE, pState->hFont);
    if (hwndFontCombo != nullptr)
    {
        WCHAR autoLabel[32] = {0};
        StringCchPrintf(autoLabel, ARRAYSIZE(autoLabel), L"自动 (%d)",
            CCompositionProcessorEngine::GetAutoCandidateFontSize());
        SendMessage(hwndFontCombo, CB_ADDSTRING, 0, (LPARAM)autoLabel);
        for (int i = 1; i < ARRAYSIZE(kFontSizeChoices); ++i)
        {
            WCHAR buf[8] = {0};
            StringCchPrintf(buf, ARRAYSIZE(buf), L"%d", kFontSizeChoices[i]);
            SendMessage(hwndFontCombo, CB_ADDSTRING, 0, (LPARAM)buf);
        }
    }
    _CreateCtrl(pState->hwndPanels[2], L"STATIC", L"预览：", WS_CHILD | WS_VISIBLE | SS_LEFT,
        16, 120, pw - 32, 24, IDC_ST_PREVIEW_LBL, pState->hFont);
    pState->hwndPreview = _CreateCtrl(pState->hwndPanels[2], L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE, 16, 148, CAND_WINDOW_WIDTH_PX, 120, IDC_PREVIEW, nullptr);
    if (pState->hwndPreview != nullptr)
    {
        SetWindowLongPtr(pState->hwndPreview, GWLP_USERDATA, (LONG_PTR)pState);
        s_prevPreviewWndProc = (WNDPROC)SetWindowLongPtr(pState->hwndPreview, GWLP_WNDPROC, (LONG_PTR)_PreviewWndProc);
    }
    _LoadPreviewCandidates(pState);

    // --- Hotkey panel ---
    _CreateCtrl(pState->hwndPanels[3], L"BUTTON",
        L"Ctrl+M  切换常用字 / 全码表",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 20, pw - 32, 24, IDC_CHK_HOTKEY_ONLYCOMMON, pState->hFont);
    _CreateCtrl(pState->hwndPanels[3], L"BUTTON",
        L"Ctrl+.  切换中 / 英文标点",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 52, pw - 32, 24, IDC_CHK_HOTKEY_PUNCTUATION, pState->hFont);
    _CreateCtrl(pState->hwndPanels[3], L"BUTTON",
        L"Shift+空格  切换全 / 半角",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        16, 84, pw - 32, 24, IDC_CHK_HOTKEY_FULLHALF, pState->hFont);

    // --- About panel ---
    // Keep a comma before DIME_VERSION_STRING_W so it is a %s argument, not
    // concatenated into the format string.
    WCHAR aboutText[512] = {0};
    StringCchPrintf(aboutText, ARRAYSIZE(aboutText),
        L"迪铭五笔 (DIME)\r\n"
        L"版本 %s\r\n"
        L"Windows TSF 五笔输入法\r\n"
        L"\r\n"
        L"源码:\r\n",
        DIME_VERSION_STRING_W);
    _CreateCtrl(pState->hwndPanels[4], L"STATIC", aboutText,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 16, pw - 32, 100, IDC_ST_ABOUT, pState->hFont);

    HWND hwndLink = _CreateCtrl(pState->hwndPanels[4], L"STATIC", kRepoUrl,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_LEFT | SS_NOTIFY,
        16, 116, pw - 32, 24, IDC_ST_ABOUT_LINK, pState->hFont);
    if (hwndLink != nullptr)
    {
        s_prevAboutLinkWndProc = (WNDPROC)SetWindowLongPtr(hwndLink, GWLP_WNDPROC, (LONG_PTR)_AboutLinkWndProc);
    }

    _CreateCtrl(pState->hwndPanels[4], L"STATIC", L"版权所有 © 2026 cnDenis",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 156, pw - 32, 24, IDC_ST_ABOUT_COPY, pState->hFont);

    // --- Bottom-right buttons: 确定 (apply+close) / 取消 (cancel) / 应用 (apply) ---
    // Owner-drawn so they get the light-blue face.
    pState->hwndClose = _CreateCtrl(hWnd, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cx - 12 - 88, cy - 12 - 28, 88, 28, IDC_BTN_CANCEL, pState->hFont);
    _CreateCtrl(hWnd, L"BUTTON", L"应用", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cx - 12 - 88 - 8 - 88, cy - 12 - 28, 88, 28, IDC_BTN_APPLY, pState->hFont);
    _CreateCtrl(hWnd, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        cx - 12 - 88 - 8 - 88 - 8 - 88, cy - 12 - 28, 88, 28, IDC_BTN_OK, pState->hFont);

    _InitStates(pState);
}

static LRESULT CALLBACK ConfigDlgProc(_In_ HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DlgState* pState = (DlgState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (uMsg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps = {0};
            HDC dc = BeginPaint(hWnd, &ps);
            RECT rc = {0};
            GetClientRect(hWnd, &rc);
            HPEN hPen = CreatePen(PS_SOLID, _DimeBorderWidth, CANDWND_BORDER_COLOR);
            HPEN hOldPen = (HPEN)SelectObject(dc, hPen);
            HBRUSH hOldBr = (HBRUSH)SelectObject(dc, (HBRUSH)GetStockObject(NULL_BRUSH));
            Rectangle(dc, 0, 0, rc.right - 1, rc.bottom - 1);
            if (pState != nullptr)
            {
                Rectangle(dc, pState->rcList.left, pState->rcList.top,
                          pState->rcList.right, pState->rcList.bottom);
            }
            SelectObject(dc, hOldPen);
            SelectObject(dc, hOldBr);
            DeleteObject(hPen);
            EndPaint(hWnd, &ps);
            return 0;
        }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        {
            HDC dc = (HDC)wParam;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, CANDWND_ITEM_COLOR);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)_DimeBkBrush();
        }

    case WM_CTLCOLORLISTBOX:
        {
            HDC dc = (HDC)wParam;
            // Only set the background; let the listbox keep its own
            // selection-aware text color (black normally, white when selected).
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)_DimeBkBrush();
        }

    case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* p = (const DRAWITEMSTRUCT*)lParam;
            if (p != nullptr && (p->CtlID == IDC_BTN_OK ||
                                 p->CtlID == IDC_BTN_CANCEL ||
                                 p->CtlID == IDC_BTN_APPLY))
            {
                _DrawDimeButton(p, (pState != nullptr) ? pState->hFont : nullptr);
                return TRUE;
            }
            break;
        }

    case WM_CREATE:
        {
            CREATESTRUCT* pcs = (CREATESTRUCT*)lParam;
            pState = (DlgState*)pcs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pState);
            _CreateControls(hWnd, pState);
            return 0;
        }

    case WM_COMMAND:
        {
            int id = (int)LOWORD(wParam);
            int code = (int)HIWORD(wParam);
            if (id == IDC_CAT_LIST && code == LBN_SELCHANGE && pState != nullptr)
            {
                int sel = (int)SendMessage(pState->hwndCatList, LB_GETCURSEL, 0, 0);
                _ShowCategory(pState, sel);
            }
            else if (id == IDC_CMB_FONTSIZE && code == CBN_SELCHANGE && pState != nullptr)
            {
                // Live preview only; persisted on 确定 / 应用.
                _UpdatePreviewLayout(pState);
            }
            else if (id == IDC_BTN_OK && pState != nullptr)
            {
                // Apply and close.
                _OnSettingChanged(pState);
                DestroyWindow(hWnd);
            }
            else if (id == IDC_BTN_APPLY && pState != nullptr)
            {
                // Apply but keep the dialog open.
                _OnSettingChanged(pState);
            }
            else if (id == IDC_BTN_CANCEL)
            {
                DestroyWindow(hWnd);
            }
            else if (id == IDC_ST_ABOUT_LINK && code == STN_CLICKED)
            {
                ShellExecuteW(hWnd, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
            }
            // Any other control (radios / checkboxes / combobox) only updates
            // its own state here; changes take effect only when 保存 is clicked.
            return 0;
        }

    case WM_CLOSE:
        {
            DestroyWindow(hWnd);
            return 0;
        }

    case WM_DESTROY:
        {
            if (pState != nullptr)
            {
                if (pState->hPreviewFont != nullptr)
                {
                    DeleteObject(pState->hPreviewFont);
                    pState->hPreviewFont = nullptr;
                }
                if (pState->hwndOwner != nullptr && IsWindow(pState->hwndOwner))
                {
                    EnableWindow(pState->hwndOwner, TRUE);
                }
                delete pState;
            }
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void DimeShowConfigDialog(_In_ HWND hwndOwner, _In_ CDIME* pTextService)
{
    if (pTextService == nullptr)
    {
        return;
    }

    static ATOM atom = 0;
    if (atom == 0)
    {
        WNDCLASS wc = {0};
        wc.lpfnWndProc = ConfigDlgProc;
        wc.hInstance = Global::dllInstanceHandle;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"DimeConfigDialog";
        atom = RegisterClass(&wc);
        if (atom == 0)
        {
            return;
        }
    }

    DlgState* pState = new (std::nothrow) DlgState();
    if (pState == nullptr)
    {
        return;
    }
    pState->pTextService = pTextService;
    pState->hwndOwner = hwndOwner;

    HWND hWnd = CreateWindowEx(WS_EX_TOPMOST, L"DimeConfigDialog", L"DIME 设置",
        WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME),
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 480,
        nullptr, nullptr, Global::dllInstanceHandle, pState);
    if (hWnd == nullptr)
    {
        delete pState;
        return;
    }

    if (hwndOwner != nullptr && IsWindow(hwndOwner))
    {
        EnableWindow(hwndOwner, FALSE);
    }

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);

    // Modal loop on the host UI thread (mirrors DialogBox). Owner is disabled
    // for the duration; the loop still pumps all thread messages so the host
    // and other IME windows keep working.
    MSG msg = {0};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
