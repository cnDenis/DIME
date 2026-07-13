// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved


#pragma once

#include "private.h"
#include "BaseWindow.h"
#include "ShadowWindow.h"
#include "DIMEBaseStructure.h"
#include "define.h"

enum CANDWND_ACTION
{
    CAND_ITEM_SELECT,
    CAND_BEFORE_PAGE_DOWN
};

typedef HRESULT (*CANDWNDCALLBACK)(void *pv, enum CANDWND_ACTION action);

class CCandidateWindow : public CBaseWindow
{
public:
    CCandidateWindow(_In_ CANDWNDCALLBACK pfnCallback, _In_ void *pv, _In_ CCandidateRange *pIndexRange, _In_ BOOL isStoreAppMode);
    virtual ~CCandidateWindow();

    BOOL _Create(ATOM atom, _In_ UINT wndWidth, _In_opt_ HWND parentWndHandle);

    void _Move(int x, int y, BOOL fRedraw = TRUE);
    void _Show(BOOL isShowWnd);

    VOID _SetTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);
    VOID _SetFillColor(_In_ HBRUSH hBrush);

    LRESULT CALLBACK _WindowProcCallback(_In_ HWND wndHandle, UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
    void _OnPaint(_In_ HDC dcHandle, _In_ PAINTSTRUCT *pps);
    void _OnLButtonDown(POINT pt);
    void _OnLButtonUp(POINT pt);
    void _OnMouseMove(POINT pt);

    void _AddString(_Inout_ CCandidateListItem *pCandidateItem, _In_ BOOL isAddFindKeyCode);
    void _SetKeystrokeCode(_In_reads_(length) const WCHAR *pwch, DWORD_PTR length);
    void _SetInputModeStatus(BOOL isFullWidth, BOOL isChinesePunctuation);
    void _SetOnlyCommonMode(BOOL isOnlyCommon);
    void _SetPinyinMode(BOOL isPinyinMode);
    void _SetEnglishMode(BOOL isEnglishMode);
    void _ClearList();
    void _UpdateLayout();
    BOOL _HasEncodingLine() const { return _IsActiveInputLayout(); }
    static void ClearStatusIconCache();
    BOOL _IsActiveInputLayout() const { return _keystrokeCodeLen > 0 || _candidateList.Count() > 0 || _isPinyinMode || _isEnglishMode; }
    UINT _GetCount()
    {
        return _candidateList.Count();
    }
    UINT _GetSelection()
    {
        return _currentSelection;
    }
    void _SetScrollInfo(_In_ int nMax, _In_ int nPage);

    DWORD _GetCandidateString(_In_ int iIndex, _Outptr_result_maybenull_z_ const WCHAR **ppwchCandidateString);
    DWORD _GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString);

    BOOL _MoveSelection(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelection(_In_ int iPage, _In_ BOOL isNotify);
    void _SetSelection(_In_ int nIndex);
    BOOL _MovePage(_In_ int offSet, _In_ BOOL isNotify);
    BOOL _SetSelectionInPage(int nPos);

    HRESULT _GetPageIndex(UINT *pIndex, _In_ UINT uSize, _Inout_ UINT *puPageCnt);
    HRESULT _SetPageIndex(UINT *pIndex, _In_ UINT uPageCnt);
    HRESULT _GetCurrentPage(_Inout_ UINT *pCurrentPage);
    HRESULT _GetCurrentPage(_Inout_ int *pCurrentPage);

private:
    void _HandleMouseMsg(_In_ UINT mouseMsg, _In_ POINT point);
    void _DrawHeaderRow(_In_ HDC dcHandle, _In_ RECT *prc);
    void _DrawStatusIcons(_In_ HDC dcHandle, _In_ RECT *prc);
    static void _DrawResourceIcon(_In_ HDC dcHandle, int x, int y, int size, DWORD iconResId, COLORREF tintColor);
    void _DrawList(_In_ HDC dcHandle, _In_ UINT iIndex, _In_ RECT *prc, _In_ int yOffset);
    void _DrawEnglishHint(_In_ HDC dcHandle, _In_ RECT *prc);
    int _GetEncodingRowHeight() const;
    void _DrawBorder(_In_ HWND wndHandle, _In_ int cx);
    BOOL _SetSelectionOffset(_In_ int offSet);
    BOOL _AdjustPageIndexForSelection();
    HRESULT _CurrentPageHasEmptyItems(_Inout_ BOOL *pfHasEmptyItems);

	// LightDismiss feature support, it will fire messages lightdismiss-related to the light dismiss layout.
    void _FireMessageToLightDismiss(_In_ HWND wndHandle, _In_ WINDOWPOS *pWndPos);

    BOOL _CreateMainWindow(ATOM atom, _In_opt_ HWND parentWndHandle);
    BOOL _CreateBackGroundShadowWindow();

    HRESULT _AdjustPageIndex(_Inout_ UINT & currentPage, _Inout_ UINT & currentPageIndex);

    void _ResizeWindow();
    void _DeleteShadowWnd();

    friend COLORREF _AdjustTextColor(_In_ COLORREF crColor, _In_ COLORREF crBkColor);

private:
    UINT _currentSelection;
    CDIMEArray<CCandidateListItem> _candidateList;
    CDIMEArray<UINT> _PageIndex;

    COLORREF _crTextColor;
    COLORREF _crBkColor;
    HBRUSH _brshBkColor;

    TEXTMETRIC _TextMetric;
    int _cyRow;
    int _iconSize;   // DPI-scaled status icon size (16 at the 125% reference)
    int _cxTitle;
    UINT _wndWidth;

    CCandidateRange* _pIndexRange;

    CANDWNDCALLBACK _pfnCallback;
    void* _pObj;

    CShadowWindow* _pShadowWnd;

    BOOL _dontAdjustOnEmptyItemPage;
    BOOL _isStoreAppMode;

    WCHAR _keystrokeCode[ENGLISH_MAX_CODE_LENGTH + 1];
    DWORD_PTR _keystrokeCodeLen;

    BOOL _isFullWidth;
    BOOL _isChinesePunctuation;
    BOOL _isPinyinMode;
    BOOL _isEnglishMode;
    BOOL _isOnlyCommon;
};
