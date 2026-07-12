// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "DIME.h"
#include "CandidateWindow.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "DIMEBaseStructure.h"
#include "Compartment.h"
#include "DebugLog.h"

//////////////////////////////////////////////////////////////////////
//
// CDIME candidate key handler methods
//
//////////////////////////////////////////////////////////////////////

const int MOVEUP_ONE = -1;
const int MOVEDOWN_ONE = 1;
const int MOVETO_TOP = 0;
const int MOVETO_BOTTOM = -1;

//+---------------------------------------------------------------------------
//
// _RefreshCandidateInputModeStatus
//
//----------------------------------------------------------------------------

void CDIME::_RefreshCandidateInputModeStatus()
{
    // When the input method is turned off (keyboard open == FALSE) while a
    // candidate window is still showing, we must hide the candidate window and
    // terminate any in-progress composition. Otherwise the candidate list stays
    // on screen even though IME input is disabled (e.g. pressing Ctrl+Space).
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) && !isOpen)
    {
        // _DeleteCandidateList ends the candidate window AND purges the engine's
        // keystroke buffer (e.g. "ab"), so a reopened IME does not resume the
        // previously typed reading.
        DIME_DEBUG_LOG(L"_RefreshCandidateInputModeStatus ime-off -> delete candidate list + purge buffer");
        _DeleteCandidateList(TRUE, _pContext);

        if (_pComposition && _pContext)
        {
            DIME_DEBUG_LOG(L"_RefreshCandidateInputModeStatus ime-off -> cancel composition");
            _CancelComposition(_pContext);
        }
        return;
    }

    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_RefreshInputModeStatus();
    }
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateFinalize
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;
    DWORD_PTR candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;
    CStringRange candidateString;

    if (nullptr == _pCandidateListUIPresenter)
    {
        goto NoPresenter;
    }

    candidateLen = _pCandidateListUIPresenter->_GetSelectedCandidateString(&pCandidateString);

    candidateString.Set(pCandidateString, candidateLen);

    if (candidateLen)
    {
        hr = _AddComposingAndChar(ec, pContext, &candidateString);

        if (FAILED(hr))
        {
            return hr;
        }
    }

NoPresenter:

    _HandleComplete(ec, pContext);

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateConvert
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateConvert(TfEditCookie ec, _In_ ITfContext *pContext)
{
    return _HandleCandidateWorker(ec, pContext);
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateWorker
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateWorker(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hrReturn = E_FAIL;
    DWORD_PTR candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;
    BSTR pbstr = nullptr;
    CStringRange candidateString;
    CDIMEArray<CCandidateListItem> candidatePhraseList;

    if (nullptr == _pCandidateListUIPresenter)
    {
        hrReturn = S_OK;
        goto Exit;
    }

    candidateLen = _pCandidateListUIPresenter->_GetSelectedCandidateString(&pCandidateString);
    if (0 == candidateLen)
    {
        hrReturn = S_FALSE;
        goto Exit;
    }

    candidateString.Set(pCandidateString, candidateLen);

    BOOL fMakePhraseFromText = _pCompositionProcessorEngine->IsMakePhraseFromText();
    if (fMakePhraseFromText)
    {
        _pCompositionProcessorEngine->GetCandidateStringInConverted(candidateString, &candidatePhraseList);
        LCID locale = _pCompositionProcessorEngine->GetLocale();

        _pCandidateListUIPresenter->RemoveSpecificCandidateFromList(locale, candidatePhraseList, candidateString);
    }

    // We have a candidate list if candidatePhraseList.Cnt is not 0
    // If we are showing reverse conversion, use CCandidateListUIPresenter
    CANDIDATE_MODE tempCandMode = CANDIDATE_NONE;
    CCandidateListUIPresenter* pTempCandListUIPresenter = nullptr;
    if (candidatePhraseList.Count())
    {
        tempCandMode = CANDIDATE_WITH_NEXT_COMPOSITION;

        pTempCandListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
            CATEGORY_CANDIDATE,
            _pCompositionProcessorEngine->GetCandidateListIndexRange(),
            FALSE);
        if (nullptr == pTempCandListUIPresenter)
        {
            hrReturn = E_OUTOFMEMORY;
            goto Exit;
        }
    }

    // call _Start*Line for CCandidateListUIPresenter or CReadingLine
    // we don't cache the document manager object so get it from pContext.
    ITfDocumentMgr* pDocumentMgr = nullptr;
    HRESULT hrStartCandidateList = E_FAIL;
    if (pContext->GetDocumentMgr(&pDocumentMgr) == S_OK)
    {
        ITfRange* pRange = nullptr;
        if (_pComposition->GetRange(&pRange) == S_OK)
        {
            if (pTempCandListUIPresenter)
            {
                hrStartCandidateList = pTempCandListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, _pCompositionProcessorEngine->GetCandidateWindowWidth());
            } 

            pRange->Release();
        }
        pDocumentMgr->Release();
    }

    // set up candidate list if it is being shown
    if (SUCCEEDED(hrStartCandidateList))
    {
        pTempCandListUIPresenter->_SetTextColor(RGB(0, 0x80, 0), GetSysColor(COLOR_WINDOW));    // Text color is green
        pTempCandListUIPresenter->_SetFillColor((HBRUSH)(COLOR_WINDOW+1));    // Background color is window
        pTempCandListUIPresenter->_SetText(&candidatePhraseList, FALSE);
        pTempCandListUIPresenter->_PresentCandidateWindow();

        // Add composing character
        hrReturn = _AddComposingAndChar(ec, pContext, &candidateString);

        // close candidate list
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_EndCandidateList();
            delete _pCandidateListUIPresenter;
            _pCandidateListUIPresenter = nullptr;

            _candidateMode = CANDIDATE_NONE;
            _isCandidateWithWildcard = FALSE;
        }

        if (hrReturn == S_OK)
        {
            // copy temp candidate
            _pCandidateListUIPresenter = pTempCandListUIPresenter;

            _candidateMode = tempCandMode;
            _isCandidateWithWildcard = FALSE;
        }
    }
    else
    {
        hrReturn = _HandleCandidateFinalize(ec, pContext);
    }

    if (pbstr)
    {
        SysFreeString(pbstr);
    }

Exit:
    return hrReturn;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateArrowKey
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pCandidateListUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCandidateSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCandidateSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
    int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pCandidateListUIPresenter)
    {
        if (_pCandidateListUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandleCandidateConvert(ec, pContext);
        }
    }

    return S_FALSE;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseFinalize
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandlePhraseFinalize(TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DWORD phraseLen = 0;
    const WCHAR* pPhraseString = nullptr;

    phraseLen = (DWORD)_pCandidateListUIPresenter->_GetSelectedCandidateString(&pPhraseString);

    CStringRange phraseString;
    phraseString.Set(pPhraseString, phraseLen);

    if (phraseLen)
    {
        if ((hr = _AddCharAndFinalize(ec, pContext, &phraseString)) != S_OK)
        {
            return hr;
        }
    }

    _HandleComplete(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseArrowKey
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandlePhraseArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, _In_ KEYSTROKE_FUNCTION keyFunction)
{
    ec;
    pContext;

    _pCandidateListUIPresenter->AdviseUIChangedByArrowKey(keyFunction);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandlePhraseSelectByNumber
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandlePhraseSelectByNumber(TfEditCookie ec, _In_ ITfContext *pContext, _In_ UINT uCode)
{
    int iSelectAsNumber = _pCompositionProcessorEngine->GetCandidateListIndexRange()->GetIndex(uCode);
    if (iSelectAsNumber == -1)
    {
        return S_FALSE;
    }

    if (_pCandidateListUIPresenter)
    {
        if (_pCandidateListUIPresenter->_SetSelectionInPage(iSelectAsNumber))
        {
            return _HandlePhraseFinalize(ec, pContext);
        }
    }

    return S_FALSE;
}

//////////////////////////////////////////////////////////////////////
//
// CCandidateListUIPresenter class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCandidateListUIPresenter::CCandidateListUIPresenter(_In_ CDIME *pTextService, ATOM atom, KEYSTROKE_CATEGORY Category, _In_ CCandidateRange *pIndexRange, BOOL hideWindow) : CTfTextLayoutSink(pTextService)
{
    _atom = atom;

    _pIndexRange = pIndexRange;

    _parentWndHandle = nullptr;
    _pCandidateWnd = nullptr;

    _Category = Category;

    _updatedFlags = 0;

    _uiElementId = (DWORD)-1;
    _isShowMode = TRUE;   // store return value from BeginUIElement
    _hideWindow = hideWindow;     // Hide window flag from [Configuration] CandidateList.Phrase.HideWindow

    _pTextService = pTextService;
    _pTextService->AddRef();

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCandidateListUIPresenter::~CCandidateListUIPresenter()
{
    _EndCandidateList();
    _pTextService->Release();
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (CTfTextLayoutSink::QueryInterface(riid, ppvObj) == S_OK)
    {
        return S_OK;
    }

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_ITfUIElement) ||
        IsEqualIID(riid, IID_ITfCandidateListUIElement))
    {
        *ppvObj = (ITfCandidateListUIElement*)this;
    }
    else if (IsEqualIID(riid, IID_IUnknown) || 
        IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) 
    {
        *ppvObj = (ITfCandidateListUIElementBehavior*)this;
    }
    else if (IsEqualIID(riid, __uuidof(ITfIntegratableCandidateListUIElement))) 
    {
        *ppvObj = (ITfIntegratableCandidateListUIElement*)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCandidateListUIPresenter::AddRef()
{
    CTfTextLayoutSink::AddRef();
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::IUnknown::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CCandidateListUIPresenter::Release()
{
    CTfTextLayoutSink::Release();

    LONG cr = --_refCount;

    assert(_refCount >= 0);

    if (_refCount == 0)
    {
        delete this;
    }

    return cr;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::GetDescription
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetDescription(BSTR *pbstr)
{
    if (pbstr)
    {
        *pbstr = SysAllocString(L"Cand");
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::GetGUID
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetGUID(GUID *pguid)
{
    *pguid = Global::DIMEGuidCandUIElement;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::Show
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::Show(BOOL showCandidateWindow)
{
    if (!showCandidateWindow &&
        _pCandidateWnd &&
        _pCandidateWnd->_HasEncodingLine())
    {
        DIME_DEBUG_LOG(L"Show(FALSE) ignored encodingLine=1");
        return S_OK;
    }

    DIME_DEBUG_LOG(L"Show(%d) hasWnd=%d hideWindow=%d isShowMode=%d",
        showCandidateWindow ? 1 : 0,
        _pCandidateWnd ? 1 : 0,
        _hideWindow ? 1 : 0,
        _isShowMode ? 1 : 0);

    if (showCandidateWindow)
    {
        ToShowCandidateWindow();
    }
    else
    {
        ToHideCandidateWindow();
    }
    return S_OK;
}

HRESULT CCandidateListUIPresenter::ToShowCandidateWindow()
{
    _PresentCandidateWindow();
    return S_OK;
}

HRESULT CCandidateListUIPresenter::ToHideCandidateWindow()
{
    if (_pCandidateWnd && _pCandidateWnd->_HasEncodingLine())
    {
        DIME_DEBUG_LOG(L"ToHide ignored encodingLine=1");
        return S_OK;
    }

    DIME_DEBUG_LOG(L"ToHide hiding wnd");

	if (_pCandidateWnd)
	{
		_pCandidateWnd->_Show(FALSE);
	}

    _updatedFlags = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    _UpdateUIElement();

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::ITfUIElement::IsShown
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::IsShown(BOOL *pIsShow)
{
    *pIsShow = _pCandidateWnd->_IsWindowVisible();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetUpdatedFlags
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetUpdatedFlags(DWORD *pdwFlags)
{
    *pdwFlags = _updatedFlags;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetDocumentMgr
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetDocumentMgr(ITfDocumentMgr **ppdim)
{
    *ppdim = nullptr;

    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetCount
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetCount(UINT *pCandidateCount)
{
    if (_pCandidateWnd)
    {
        *pCandidateCount = _pCandidateWnd->_GetCount();
    }
    else
    {
        *pCandidateCount = 0;
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetSelection
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetSelection(UINT *pSelectedCandidateIndex)
{
    if (_pCandidateWnd)
    {
        *pSelectedCandidateIndex = _pCandidateWnd->_GetSelection();
    }
    else
    {
        *pSelectedCandidateIndex = 0;
    }
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetString
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetString(UINT uIndex, BSTR *pbstr)
{
    if (!_pCandidateWnd || (uIndex > _pCandidateWnd->_GetCount()))
    {
        return E_FAIL;
    }

    DWORD candidateLen = 0;
    const WCHAR* pCandidateString = nullptr;

    candidateLen = _pCandidateWnd->_GetCandidateString(uIndex, &pCandidateString);

    *pbstr = (candidateLen == 0) ? nullptr : SysAllocStringLen(pCandidateString, candidateLen);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetPageIndex(UINT *pIndex, UINT uSize, UINT *puPageCnt)
{
    if (!_pCandidateWnd)
    {
        if (pIndex)
        {
            *pIndex = 0;
        }
        *puPageCnt = 0;
        return S_OK;
    }
    return _pCandidateWnd->_GetPageIndex(pIndex, uSize, puPageCnt);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::SetPageIndex
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::SetPageIndex(UINT *pIndex, UINT uPageCnt)
{
    if (!_pCandidateWnd)
    {
        return E_FAIL;
    }
    return _pCandidateWnd->_SetPageIndex(pIndex, uPageCnt);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElement::GetCurrentPage
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetCurrentPage(UINT *puPage)
{
    if (!_pCandidateWnd)
    {
        *puPage = 0;
        return S_OK;
    }
    return _pCandidateWnd->_GetCurrentPage(puPage);
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::SetSelection
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::SetSelection(UINT nIndex)
{
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_SetSelection(nIndex);
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::Finalize
// It is related of the mouse clicking behavior upon the suggestion window
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::Finalize(void)
{
    _CandidateChangeNotification(CAND_ITEM_SELECT);
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfCandidateListUIElementBehavior::Abort
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::Abort(void)
{
    return E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::SetIntegrationStyle
// To show candidateNumbers on the suggestion window
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::SetIntegrationStyle(GUID guidIntegrationStyle)
{
    return (guidIntegrationStyle == GUID_INTEGRATIONSTYLE_SEARCHBOX) ? S_OK : E_NOTIMPL;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::GetSelectionStyle
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::GetSelectionStyle(_Out_ TfIntegratableCandidateListSelectionStyle *ptfSelectionStyle)
{
    *ptfSelectionStyle = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::OnKeyDown
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::OnKeyDown(_In_ WPARAM wParam, _In_ LPARAM lParam, _Out_ BOOL *pIsEaten)
{
    wParam;
    lParam;

    *pIsEaten = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::ShowCandidateNumbers
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::ShowCandidateNumbers(_Out_ BOOL *pIsShow)
{
    *pIsShow = TRUE;
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfIntegratableCandidateListUIElement::FinalizeExactCompositionString
//
//----------------------------------------------------------------------------

STDAPI CCandidateListUIPresenter::FinalizeExactCompositionString()
{
    return E_NOTIMPL;
}


//+---------------------------------------------------------------------------
//
// _StartCandidateList
//
//----------------------------------------------------------------------------

HRESULT CCandidateListUIPresenter::_StartCandidateList(TfClientId tfClientId, _In_ ITfDocumentMgr *pDocumentMgr, _In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition, UINT wndWidth)
{
	pDocumentMgr;tfClientId;

    HRESULT hr = E_FAIL;

    if (FAILED(_StartLayout(pContextDocument, ec, pRangeComposition)))
    {
        goto Exit;
    }

    BeginUIElement();

    hr = MakeCandidateWindow(pContextDocument, wndWidth);
    if (FAILED(hr))
    {
        goto Exit;
    }

    RECT rcTextExt;
    if (SUCCEEDED(_GetTextExt(&rcTextExt)))
    {
        _LayoutChangeNotification(&rcTextExt);
    }

Exit:
    if (SUCCEEDED(hr))
    {
        DIME_DEBUG_LOG(L"StartCandidateList OK hasWnd=%d", _pCandidateWnd ? 1 : 0);
    }
    else
    {
        BOOL keepLayout = (_pTextService && _pTextService->IsCandidateListActive());
        DIME_WARNING_LOG(L"StartCandidateList FAIL hr=0x%08X keepLayout=%d", hr, keepLayout ? 1 : 0);
        if (keepLayout)
        {
            _EndLayout();
        }
        else
        {
            _EndCandidateList();
        }
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// _EndCandidateList
//
//----------------------------------------------------------------------------

void CCandidateListUIPresenter::_EndCandidateList()
{
    DIME_DEBUG_LOG(L"EndCandidateList hasWnd=%d", _pCandidateWnd ? 1 : 0);

    EndUIElement();

    DisposeCandidateWindow();

    _EndLayout();
}

//+---------------------------------------------------------------------------
//
// _SetText
//
//----------------------------------------------------------------------------

void CCandidateListUIPresenter::_EnsureCandidateWindowVisible()
{
    if (_isShowMode && _pCandidateWnd && !_pCandidateWnd->_IsWindowVisible())
    {
        _PresentCandidateWindow();
    }
}

void CCandidateListUIPresenter::_PresentCandidateWindow()
{
    if (!_pCandidateWnd)
    {
        DIME_DEBUG_LOG(L"Present skip noWnd");
        return;
    }

    if (_hideWindow)
    {
        DIME_DEBUG_LOG(L"Present hideWindow=1 -> Show(FALSE)");
        _pCandidateWnd->_Show(FALSE);
        return;
    }

    BOOL wasVisible = _pCandidateWnd->_IsWindowVisible();

    _MoveWindowToTextExt();
    _pCandidateWnd->_RepaintNow();

    if (!wasVisible)
    {
        DIME_DEBUG_LOG(L"Present -> Show(TRUE) encodingLine=%d",
            _pCandidateWnd->_HasEncodingLine() ? 1 : 0);
        _pCandidateWnd->_Show(TRUE);
    }
}

void CCandidateListUIPresenter::_SyncInputModeStatus()
{
    if (!_pCandidateWnd || !_pTextService)
    {
        return;
    }

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (!pThreadMgr)
    {
        return;
    }

    BOOL isFullWidth = FALSE;
    BOOL isChinesePunctuation = FALSE;
    CCompartment compartmentFullWidth(pThreadMgr, _pTextService->_GetClientId(), Global::DIMEGuidCompartmentDoubleSingleByte);
    CCompartment compartmentPunctuation(pThreadMgr, _pTextService->_GetClientId(), Global::DIMEGuidCompartmentPunctuation);
    compartmentFullWidth._GetCompartmentBOOL(isFullWidth);
    compartmentPunctuation._GetCompartmentBOOL(isChinesePunctuation);
    DIME_DEBUG_LOG(L"SyncInputMode full=%d punct=%d", isFullWidth ? 1 : 0, isChinesePunctuation ? 1 : 0);
    _pCandidateWnd->_SetInputModeStatus(isFullWidth, isChinesePunctuation);

    CCompositionProcessorEngine* pEngine = _pTextService ? _pTextService->GetCompositionProcessorEngine() : nullptr;
    if (pEngine)
    {
        _pCandidateWnd->_SetOnlyCommonMode(pEngine->IsOnlyCommon());
    }
}

void CCandidateListUIPresenter::_RefreshInputModeStatus()
{
    if (!_pCandidateWnd)
    {
        return;
    }

    _SyncInputModeStatus();
    if (_isShowMode)
    {
        _pCandidateWnd->_RepaintNow();
    }
}

void CCandidateListUIPresenter::_RefreshCandidateContent(
    _In_opt_ CStringRange *pKeystrokeCode,
    _In_opt_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!_pCandidateWnd)
    {
        DIME_DEBUG_LOG(L"Refresh skip noWnd keystrokeLen=%llu candidates=%u",
            pKeystrokeCode ? pKeystrokeCode->GetLength() : 0,
            pCandidateList ? pCandidateList->Count() : 0);
        return;
    }

    HWND hwnd = _pCandidateWnd->_GetWnd();
    if (hwnd)
    {
        SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
    }

    // Reflect the temporary pinyin / English input modes on the candidate window
    // so the "拼" / "英" tag shows in the header row.
    BOOL isPinyinMode = FALSE;
    BOOL isEnglishMode = FALSE;
    CCompositionProcessorEngine* pEngine = _pTextService ? _pTextService->GetCompositionProcessorEngine() : nullptr;
    if (pEngine)
    {
        isPinyinMode = pEngine->IsPinyinInput();
        isEnglishMode = pEngine->IsEnglishInput();
    }
    if (_pCandidateWnd)
    {
        _pCandidateWnd->_SetPinyinMode(isPinyinMode);
        _pCandidateWnd->_SetEnglishMode(isEnglishMode);
    }

    _ClearList();

    if (pKeystrokeCode && pKeystrokeCode->GetLength() > 0)
    {
        _pCandidateWnd->_SetKeystrokeCode(pKeystrokeCode->Get(), pKeystrokeCode->GetLength());
    }
    else
    {
        _pCandidateWnd->_SetKeystrokeCode(nullptr, 0);
    }

    if (pCandidateList && pCandidateList->Count() > 0)
    {
        AddCandidateToCandidateListUI(pCandidateList, TRUE);
    }

    if ((pKeystrokeCode && pKeystrokeCode->GetLength() > 0) ||
        (pCandidateList && pCandidateList->Count() > 0))
    {
        CDIMEArray<CCandidateListItem> emptyCandidateList;
        SetPageIndexWithScrollInfo(
            (pCandidateList && pCandidateList->Count() > 0) ? pCandidateList : &emptyCandidateList);
    }

    _SyncInputModeStatus();
    _pCandidateWnd->_UpdateLayout();

    BOOL hasContent = (pKeystrokeCode && pKeystrokeCode->GetLength() > 0) ||
        (pCandidateList && pCandidateList->Count() > 0) ||
        isPinyinMode || isEnglishMode;

    DIME_DEBUG_LOG(L"Refresh keystrokeLen=%llu candidates=%u hasContent=%d isShowMode=%d",
        pKeystrokeCode ? pKeystrokeCode->GetLength() : 0,
        pCandidateList ? pCandidateList->Count() : 0,
        hasContent ? 1 : 0,
        _isShowMode ? 1 : 0);

    if (hwnd)
    {
        SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
    }

    if (hasContent)
    {
        _PresentCandidateWindow();
    }
    else if (_isShowMode)
    {
        DIME_DEBUG_LOG(L"Refresh hasContent=0 -> Show(FALSE)");
        _pCandidateWnd->_Show(FALSE);
    }
}

void CCandidateListUIPresenter::_SetKeystrokeCode(_In_ CStringRange *pKeystrokeCode)
{
    if (!_pCandidateWnd)
    {
        return;
    }

    if (pKeystrokeCode && pKeystrokeCode->GetLength() > 0)
    {
        _pCandidateWnd->_SetKeystrokeCode(pKeystrokeCode->Get(), pKeystrokeCode->GetLength());
    }
    else
    {
        _pCandidateWnd->_SetKeystrokeCode(nullptr, 0);
    }

    if (_isShowMode)
    {
        _pCandidateWnd->_InvalidateRect();
    }
}

void CCandidateListUIPresenter::_SetText(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{
    AddCandidateToCandidateListUI(pCandidateList, isAddFindKeyCode);

    SetPageIndexWithScrollInfo(pCandidateList);
    _pCandidateWnd->_UpdateLayout();

    if (_isShowMode)
    {
        _pCandidateWnd->_InvalidateRect();
    }
    else
    {
        _updatedFlags = TF_CLUIE_COUNT       |
            TF_CLUIE_SELECTION   |
            TF_CLUIE_STRING      |
            TF_CLUIE_PAGEINDEX   |
            TF_CLUIE_CURRENTPAGE;
        _UpdateUIElement();
    }
}

void CCandidateListUIPresenter::AddCandidateToCandidateListUI(_In_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isAddFindKeyCode)
{
    for (UINT index = 0; index < pCandidateList->Count(); index++)
    {
        _pCandidateWnd->_AddString(pCandidateList->GetAt(index), isAddFindKeyCode);
    }
}

void CCandidateListUIPresenter::SetPageIndexWithScrollInfo(_In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    UINT candCntInPage = _pIndexRange->Count();
    UINT bufferSize = pCandidateList->Count() / candCntInPage + 1;
    UINT* puPageIndex = new (std::nothrow) UINT[ bufferSize ];
    if (puPageIndex != nullptr)
    {
        for (UINT i = 0; i < bufferSize; i++)
        {
            puPageIndex[i] = i * candCntInPage;
        }

        _pCandidateWnd->_SetPageIndex(puPageIndex, bufferSize);
        delete [] puPageIndex;
    }
    _pCandidateWnd->_SetScrollInfo(pCandidateList->Count(), candCntInPage);  // nMax:range of max, nPage:number of items in page

}
//+---------------------------------------------------------------------------
//
// _ClearList
//
//----------------------------------------------------------------------------

void CCandidateListUIPresenter::_ClearList()
{
    _pCandidateWnd->_ClearList();
    _pCandidateWnd->_InvalidateRect();
}

//+---------------------------------------------------------------------------
//
// _SetTextColor
// _SetFillColor
//
//----------------------------------------------------------------------------

void CCandidateListUIPresenter::_SetTextColor(COLORREF crColor, COLORREF crBkColor)
{
    _pCandidateWnd->_SetTextColor(crColor, crBkColor);
}

void CCandidateListUIPresenter::_SetFillColor(HBRUSH hBrush)
{
    _pCandidateWnd->_SetFillColor(hBrush);
}

//+---------------------------------------------------------------------------
//
// _GetSelectedCandidateString
//
//----------------------------------------------------------------------------

DWORD_PTR CCandidateListUIPresenter::_GetSelectedCandidateString(_Outptr_result_maybenull_ const WCHAR **ppwchCandidateString)
{
    return _pCandidateWnd->_GetSelectedCandidateString(ppwchCandidateString);
}

//+---------------------------------------------------------------------------
//
// _MoveSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateListUIPresenter::_MoveSelection(_In_ int offSet)
{
    BOOL ret = _pCandidateWnd->_MoveSelection(offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION;
            _UpdateUIElement();
        }
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _SetSelection
//
//----------------------------------------------------------------------------

BOOL CCandidateListUIPresenter::_SetSelection(_In_ int selectedIndex)
{
    BOOL ret = _pCandidateWnd->_SetSelection(selectedIndex, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION |
                TF_CLUIE_CURRENTPAGE;
            _UpdateUIElement();
        }
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _MovePage
//
//----------------------------------------------------------------------------

BOOL CCandidateListUIPresenter::_NeedsExpandBeforePageDown(_In_ int pageOffset) const
{
    if (pageOffset <= 0 || !_pTextService || !_pCandidateWnd)
    {
        return FALSE;
    }

    CCompositionProcessorEngine *pEngine = _pTextService->GetCompositionProcessorEngine();
    if (!pEngine || !pEngine->AreCandidatesTruncated())
    {
        return FALSE;
    }

    int currentPage = 0;
    if (FAILED(_pCandidateWnd->_GetCurrentPage(&currentPage)))
    {
        return FALSE;
    }

    return (currentPage + pageOffset >= static_cast<int>(WUBI_INITIAL_CANDIDATE_PAGES));
}

BOOL CCandidateListUIPresenter::_ExpandCandidateListIfNeeded()
{
    if (!_pTextService || !_pCandidateWnd)
    {
        return FALSE;
    }

    CCompositionProcessorEngine *pEngine = _pTextService->GetCompositionProcessorEngine();
    if (!pEngine || !pEngine->AreCandidatesTruncated())
    {
        return TRUE;
    }

    UINT selection = _pCandidateWnd->_GetSelection();
    CDIMEArray<CCandidateListItem> candidateList;
    pEngine->LoadFullCandidateList(&candidateList);

    if (candidateList.Count() == 0)
    {
        return FALSE;
    }

    _RefreshCandidateContent(pEngine->GetKeystrokeBuffer(), &candidateList);

    if (selection < candidateList.Count())
    {
        _pCandidateWnd->_SetSelection(static_cast<int>(selection), FALSE);
    }

    return TRUE;
}

BOOL CCandidateListUIPresenter::_MovePage(_In_ int offSet)
{
    if (_NeedsExpandBeforePageDown(offSet))
    {
        if (!_ExpandCandidateListIfNeeded())
        {
            return FALSE;
        }
    }

    BOOL ret = _pCandidateWnd->_MovePage(offSet, TRUE);
    if (ret)
    {
        if (_isShowMode)
        {
            _pCandidateWnd->_InvalidateRect();
        }
        else
        {
            _updatedFlags = TF_CLUIE_SELECTION |
                TF_CLUIE_CURRENTPAGE;
            _UpdateUIElement();
        }
    }
    return ret;
}

//+---------------------------------------------------------------------------
//
// _MoveWindowToTextExt
//
//----------------------------------------------------------------------------

void CCandidateListUIPresenter::_MoveWindowToTextExt()
{
    RECT rc = {0, 0, 0, 0};

    if (FAILED(_GetTextExt(&rc)))
    {
        return;
    }

    _LayoutChangeNotification(&rc);
}
//+---------------------------------------------------------------------------
//
// _LayoutChangeNotification
//
//----------------------------------------------------------------------------

VOID CCandidateListUIPresenter::_LayoutChangeNotification(_In_ RECT *lpRect)
{
    if (lpRect->right <= lpRect->left && lpRect->bottom <= lpRect->top)
    {
        return;
    }

    RECT rectCandidate = {0, 0, 0, 0};
    POINT ptCandidate = {0, 0};

    _pCandidateWnd->_GetClientRect(&rectCandidate);
    _pCandidateWnd->_GetWindowExtent(lpRect, &rectCandidate, &ptCandidate);
    _pCandidateWnd->_Move(ptCandidate.x, ptCandidate.y, FALSE);
}

//+---------------------------------------------------------------------------
//
// _LayoutDestroyNotification
//
//----------------------------------------------------------------------------

VOID CCandidateListUIPresenter::_LayoutDestroyNotification()
{
    DIME_DEBUG_LOG(L"LayoutDestroy ignored hasWnd=%d encodingLine=%d",
        _pCandidateWnd ? 1 : 0,
        (_pCandidateWnd && _pCandidateWnd->_HasEncodingLine()) ? 1 : 0);
    // TF_LC_DESTROY may fire for empty composition anchors; do not tear down the candidate window.
}

//+---------------------------------------------------------------------------
//
// _CandidateChangeNotifiction
//
//----------------------------------------------------------------------------

HRESULT CCandidateListUIPresenter::_CandidateChangeNotification(_In_ enum CANDWND_ACTION action)
{
    HRESULT hr = E_FAIL;

    TfClientId tfClientId = _pTextService->_GetClientId();
    ITfThreadMgr* pThreadMgr = nullptr;
    ITfDocumentMgr* pDocumentMgr = nullptr;
    ITfContext* pContext = nullptr;

    _KEYSTROKE_STATE KeyState;
    KeyState.Category = _Category;
    KeyState.Function = FUNCTION_FINALIZE_CANDIDATELIST;

    if (CAND_BEFORE_PAGE_DOWN == action)
    {
        if (_NeedsExpandBeforePageDown(1))
        {
            _ExpandCandidateListIfNeeded();
        }
        hr = S_OK;
        goto Exit;
    }

    if (CAND_ITEM_SELECT != action)
    {
        goto Exit;
    }

    pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        goto Exit;
    }

    hr = pThreadMgr->GetFocus(&pDocumentMgr);
    if (FAILED(hr))
    {
        goto Exit;
    }

    hr = pDocumentMgr->GetTop(&pContext);
    if (FAILED(hr))
    {
        pDocumentMgr->Release();
        goto Exit;
    }

    CKeyHandlerEditSession *pEditSession = new (std::nothrow) CKeyHandlerEditSession(_pTextService, pContext, 0, 0, KeyState);
    if (nullptr != pEditSession)
    {
        HRESULT hrSession = S_OK;
        hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
        if (hrSession == TF_E_SYNCHRONOUS || hrSession == TS_E_READONLY)
        {
            hr = pContext->RequestEditSession(tfClientId, pEditSession, TF_ES_ASYNC | TF_ES_READWRITE, &hrSession);
        }
        pEditSession->Release();
    }

    pContext->Release();
    pDocumentMgr->Release();

Exit:
    return hr;
}

//+---------------------------------------------------------------------------
//
// _CandWndCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CCandidateListUIPresenter::_CandWndCallback(_In_ void *pv, _In_ enum CANDWND_ACTION action)
{
    CCandidateListUIPresenter* fakeThis = (CCandidateListUIPresenter*)pv;

    return fakeThis->_CandidateChangeNotification(action);
}

//+---------------------------------------------------------------------------
//
// _UpdateUIElement
//
//----------------------------------------------------------------------------

HRESULT CCandidateListUIPresenter::_UpdateUIElement()
{
    HRESULT hr = S_OK;

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr == pThreadMgr)
    {
        return S_OK;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;

    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
    {
        pUIElementMgr->UpdateUIElement(_uiElementId);
        pUIElementMgr->Release();
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnSetThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CCandidateListUIPresenter::OnSetThreadFocus()
{
    if (!_isShowMode || !_pTextService || !_pTextService->IsCandidateListActive())
    {
        return S_OK;
    }

    CCompositionProcessorEngine *pEngine = _pTextService->GetCompositionProcessorEngine();
    if (!pEngine)
    {
        return S_OK;
    }

    DWORD_PTR keyLen = pEngine->GetVirtualKeyLength();
    if (keyLen == 0)
    {
        return S_OK;
    }

    CDIMEArray<CCandidateListItem> candidateList;
    pEngine->GetCandidateList(&candidateList, TRUE, FALSE);

    CStringRange keystroke;
    keystroke.Set(pEngine->GetKeystrokeBuffer()->Get(), keyLen);

    _RefreshCandidateContent(
        &keystroke,
        candidateList.Count() > 0 ? &candidateList : nullptr);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// OnKillThreadFocus
//
//----------------------------------------------------------------------------

HRESULT CCandidateListUIPresenter::OnKillThreadFocus()
{
    BOOL active = (_pTextService && _pTextService->IsCandidateListActive());
    DIME_DEBUG_LOG(L"OnKillThreadFocus isShowMode=%d active=%d", _isShowMode ? 1 : 0, active ? 1 : 0);
    if (_pCandidateWnd)
    {
        // 切换程序时须隐藏窗口; 不走 Show(FALSE), 避免编码行保护拦截.
        _pCandidateWnd->_Show(FALSE);
    }
    return S_OK;
}

void CCandidateListUIPresenter::RemoveSpecificCandidateFromList(_In_ LCID Locale, _Inout_ CDIMEArray<CCandidateListItem> &candidateList, _In_ CStringRange &candidateString)
{
    for (UINT index = 0; index < candidateList.Count();)
    {
        CCandidateListItem* pLI = candidateList.GetAt(index);

        if (CStringRange::Compare(Locale, &candidateString, &pLI->_ItemString) == CSTR_EQUAL)
        {
            candidateList.RemoveAt(index);
            continue;
        }

        index++;
    }
}

void CCandidateListUIPresenter::AdviseUIChangedByArrowKey(_In_ KEYSTROKE_FUNCTION arrowKey)
{
    switch (arrowKey)
    {
    // Arrow-key (Up/Down) selection movement is disabled; candidates are
    // chosen by number keys. Paging via '-' / '=' is kept below.
    case FUNCTION_MOVE_PAGE_UP:
        {
            _MovePage(MOVEUP_ONE);
            break;
        }
    case FUNCTION_MOVE_PAGE_DOWN:
        {
            _MovePage(MOVEDOWN_ONE);
            break;
        }
    default:
        break;
    }
}

HRESULT CCandidateListUIPresenter::BeginUIElement()
{
    HRESULT hr = S_OK;

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if (nullptr ==pThreadMgr)
    {
        hr = E_FAIL;
        goto Exit;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;
    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
    {
        pUIElementMgr->BeginUIElement(this, &_isShowMode, &_uiElementId);
        pUIElementMgr->Release();
    }

Exit:
    return hr;
}

HRESULT CCandidateListUIPresenter::EndUIElement()
{
    HRESULT hr = S_OK;

    ITfThreadMgr* pThreadMgr = _pTextService->_GetThreadMgr();
    if ((nullptr == pThreadMgr) || (-1 == _uiElementId))
    {
        hr = E_FAIL;
        goto Exit;
    }

    ITfUIElementMgr* pUIElementMgr = nullptr;
    hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void **)&pUIElementMgr);
    if (hr == S_OK)
    {
        pUIElementMgr->EndUIElement(_uiElementId);
        pUIElementMgr->Release();
    }

Exit:
    return hr;
}

HRESULT CCandidateListUIPresenter::MakeCandidateWindow(_In_ ITfContext *pContextDocument, _In_ UINT wndWidth)
{
    HRESULT hr = S_OK;

    if (nullptr != _pCandidateWnd)
    {
        return hr;
    }

    _pCandidateWnd = new (std::nothrow) CCandidateWindow(_CandWndCallback, this, _pIndexRange, _pTextService->_IsStoreAppMode());
    if (_pCandidateWnd == nullptr)
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

    HWND parentWndHandle = nullptr;
    ITfContextView* pView = nullptr;
    if (SUCCEEDED(pContextDocument->GetActiveView(&pView)))
    {
        pView->GetWnd(&parentWndHandle);
    }

    if (!_pCandidateWnd->_Create(_atom, wndWidth, parentWndHandle))
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }

Exit:
    return hr;
}

void CCandidateListUIPresenter::DisposeCandidateWindow()
{
    if (nullptr == _pCandidateWnd)
    {
        return;
    }

    _pCandidateWnd->_Destroy();

    delete _pCandidateWnd;
    _pCandidateWnd = nullptr;
}