// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "globals.h"
#include "DIME.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "Compartment.h"
#include "StatusWindow.h"
#include "DebugLog.h"

//+---------------------------------------------------------------------------
//
// CreateInstance
//
//----------------------------------------------------------------------------

/* static */
HRESULT CDIME::CreateInstance(_In_ IUnknown *pUnkOuter, REFIID riid, _Outptr_ void **ppvObj)
{
    CDIME* pDIME = nullptr;
    HRESULT hr = S_OK;

    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (nullptr != pUnkOuter)
    {
        return CLASS_E_NOAGGREGATION;
    }

    pDIME = new (std::nothrow) CDIME();
    if (pDIME == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    hr = pDIME->QueryInterface(riid, ppvObj);

    pDIME->Release();

    return hr;
}

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDIME::CDIME()
{
    DllAddRef();

    _pThreadMgr = nullptr;

    _threadMgrEventSinkCookie = TF_INVALID_COOKIE;

    _pTextEditSinkContext = nullptr;
    _textEditSinkCookie = TF_INVALID_COOKIE;

    _activeLanguageProfileNotifySinkCookie = TF_INVALID_COOKIE;

    _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;

    _pComposition = nullptr;

    _pCompositionProcessorEngine = nullptr;

    _candidateMode = CANDIDATE_NONE;
    _pCandidateListUIPresenter = nullptr;
    _isCandidateWithWildcard = FALSE;

    _pStatusWindow = nullptr;
    _isThreadFocused = FALSE;

    _pDocMgrLastFocused = nullptr;

    _pSIPIMEOnOffCompartment = nullptr;
    _dwSIPIMEOnOffCompartmentSinkCookie = 0;
    _msgWndHandle = nullptr;

    _pContext = nullptr;

    _refCount = 1;
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CDIME::~CDIME()
{
    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;
    }
    DllRelease();
}

//+---------------------------------------------------------------------------
//
// _ResetInputForModeChange
//
//    Clears any in-progress reading / candidate list so a persistent-mode
//    toggle (Ctrl+M "only common characters") takes effect immediately and
//    does not leave a stale candidate list on screen. The keystroke buffer is
//    purged and the composition is cancelled (discarded), leaving a clean
//    slate for the next keystroke.
//
//----------------------------------------------------------------------------

void CDIME::_ResetInputForModeChange()
{
    BOOL hasInput = (_pCandidateListUIPresenter != nullptr) ||
                    (_pComposition != nullptr) ||
                    (_pCompositionProcessorEngine && _pCompositionProcessorEngine->GetVirtualKeyLength() > 0);
    if (!hasInput)
    {
        return;
    }

    DIME_DEBUG_LOG(L"_ResetInputForModeChange clearing input (presenter=%d composing=%d keyLen=%llu)",
        _pCandidateListUIPresenter ? 1 : 0,
        _pComposition ? 1 : 0,
        _pCompositionProcessorEngine ? _pCompositionProcessorEngine->GetVirtualKeyLength() : 0);

    // Purge the keystroke buffer AND tear down the candidate window.
    _DeleteCandidateList(TRUE, _pContext);

    // Cancel (discard) the in-progress composition so no half-typed reading
    // remains in the document.
    if (_pComposition && _pContext)
    {
        _CancelComposition(_pContext);
    }
}

//+---------------------------------------------------------------------------
//
// _InitStatusWindow
//
//  Create the floating status bar. Failure here is non-fatal: the IME still
//  works via the language bar / hotkeys; we simply skip the floating UI.
//
//----------------------------------------------------------------------------

BOOL CDIME::_InitStatusWindow()
{
    if (_pStatusWindow != nullptr)
    {
        return TRUE;
    }

    _pStatusWindow = new (std::nothrow) CStatusWindow(_StatusWndCallback, this);
    if (_pStatusWindow == nullptr)
    {
        return FALSE;
    }

    if (!_pStatusWindow->_Create(Global::AtomStatusWindow, nullptr))
    {
        delete _pStatusWindow;
        _pStatusWindow = nullptr;
        return FALSE;
    }

    _pStatusWindow->_RestorePosition();
    _RefreshStatusWindow();
    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _UninitStatusWindow
//
//----------------------------------------------------------------------------

void CDIME::_UninitStatusWindow()
{
    if (_pStatusWindow != nullptr)
    {
        _pStatusWindow->_Show(FALSE);
        _pStatusWindow->_Destroy();
        delete _pStatusWindow;
        _pStatusWindow = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// _RefreshStatusWindow
//
//  Show the bar (and sync its state) when the IME is on and the thread has
//  focus; hide it otherwise. The three segments are toggled by the user and
//  the resulting compartment change routes back through here to repaint.
//
//----------------------------------------------------------------------------

void CDIME::_RefreshStatusWindow()
{
    if (_pStatusWindow == nullptr)
    {
        return;
    }

    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
    {
        _pStatusWindow->_Show(FALSE);
        return;
    }

    // No thread focus: there is no active document, so hide the free-floating
    // bar entirely.
    if (!_isThreadFocused)
    {
        _pStatusWindow->_Show(FALSE);
        return;
    }

    // IME off (English mode, e.g. Ctrl+Space): keep the bar visible but gray
    // out every icon as an "inactive" hint rather than hiding it.
    if (!isOpen)
    {
        _pStatusWindow->_SetGrayed(TRUE);
        _pStatusWindow->_Show(TRUE);
        return;
    }

    BOOL isFullWidth = FALSE;
    BOOL isChinesePunctuation = FALSE;
    BOOL isOnlyCommon = FALSE;

    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CCompartment CompartmentPunctuation(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CCompartment CompartmentOnlyCommon(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentOnlyCommon);

    CompartmentDoubleSingleByte._GetCompartmentBOOL(isFullWidth);
    CompartmentPunctuation._GetCompartmentBOOL(isChinesePunctuation);
    CompartmentOnlyCommon._GetCompartmentBOOL(isOnlyCommon);

    _pStatusWindow->_SetStates(isFullWidth, isChinesePunctuation, isOnlyCommon);
    _pStatusWindow->_SetGrayed(FALSE);
    _pStatusWindow->_Show(TRUE);
}

//+---------------------------------------------------------------------------
//
// _StatusWndCallback
//
//  Click handler from the status bar: flip the compartment for the clicked
//  segment. The compartment change propagates back through CompartmentCallback
//  -> NotifyInputModeChanged -> _RefreshStatusWindow, which repaints the bar.
//
//----------------------------------------------------------------------------

/* static */
void CDIME::_StatusWndCallback(void *pv, int item)
{
    CDIME *self = reinterpret_cast<CDIME *>(pv);
    if (self == nullptr)
    {
        return;
    }

    CCompositionProcessorEngine *eng = self->GetCompositionProcessorEngine();
    if (eng == nullptr || self->_pThreadMgr == nullptr)
    {
        return;
    }

    switch (item)
    {
    case STATUS_ITEM_FULL_HALF:
        eng->ToggleDoubleSingleByte(self->_pThreadMgr, self->_tfClientId);
        break;
    case STATUS_ITEM_PUNCTUATION:
        eng->TogglePunctuation(self->_pThreadMgr, self->_tfClientId);
        break;
    case STATUS_ITEM_ONLY_COMMON:
        eng->ToggleOnlyCommon(self->_pThreadMgr, self->_tfClientId);
        break;
    default:
        break;
    }
}

//+---------------------------------------------------------------------------
//
// QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CDIME::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextInputProcessor))
    {
        *ppvObj = (ITfTextInputProcessor *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    {
        *ppvObj = (ITfTextInputProcessorEx *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    {
        *ppvObj = (ITfThreadMgrEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfTextEditSink))
    {
        *ppvObj = (ITfTextEditSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink))
    {
        *ppvObj = (ITfActiveLanguageProfileNotifySink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfCompositionSink))
    {
        *ppvObj = (ITfKeyEventSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    {
        *ppvObj = (ITfDisplayAttributeProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    {
        *ppvObj = (ITfThreadFocusSink *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunctionProvider))
    {
        *ppvObj = (ITfFunctionProvider *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFunction))
    {
        *ppvObj = (ITfFunction *)this;
    }
    else if (IsEqualIID(riid, IID_ITfFnGetPreferredTouchKeyboardLayout))
    {
        *ppvObj = (ITfFnGetPreferredTouchKeyboardLayout *)this;
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
// AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CDIME::AddRef()
{
    return ++_refCount;
}

//+---------------------------------------------------------------------------
//
// Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CDIME::Release()
{
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
// ITfTextInputProcessorEx::ActivateEx
//
//----------------------------------------------------------------------------

STDAPI CDIME::ActivateEx(ITfThreadMgr *pThreadMgr, TfClientId tfClientId, DWORD dwFlags)
{
    _pThreadMgr = pThreadMgr;
    _pThreadMgr->AddRef();

    _tfClientId = tfClientId;
    _dwActivateFlags = dwFlags;

    if (!_InitThreadMgrEventSink())
    {
        goto ExitError;
    }

    ITfDocumentMgr* pDocMgrFocus = nullptr;
    if (SUCCEEDED(_pThreadMgr->GetFocus(&pDocMgrFocus)) && (pDocMgrFocus != nullptr))
    {
        _InitTextEditSink(pDocMgrFocus);
        pDocMgrFocus->Release();
    }

    if (!_InitKeyEventSink())
    {
        goto ExitError;
    }

    if (!_InitActiveLanguageProfileNotifySink())
    {
        goto ExitError;
    }

    if (!_InitThreadFocusSink())
    {
        goto ExitError;
    }

    if (!_InitDisplayAttributeGuidAtom())
    {
        goto ExitError;
    }

    if (!_InitFunctionProviderSink())
    {
        goto ExitError;
    }

    if (!_AddTextProcessorEngine())
    {
        goto ExitError;
    }

    // Floating status bar (non-fatal if it fails to create).
    _isThreadFocused = TRUE;
    _InitStatusWindow();

    return S_OK;

ExitError:
    Deactivate();
    return E_FAIL;
}

//+---------------------------------------------------------------------------
//
// ITfTextInputProcessorEx::Deactivate
//
//----------------------------------------------------------------------------

STDAPI CDIME::Deactivate()
{
    _UninitStatusWindow();

    if (_pCompositionProcessorEngine)
    {
        _pCompositionProcessorEngine->SetTextService(nullptr);
        delete _pCompositionProcessorEngine;
        _pCompositionProcessorEngine = nullptr;
    }

    ITfContext* pContext = _pContext;
    if (_pContext)
    {   
        pContext->AddRef();
        _EndComposition(_pContext);
    }

    if (_pCandidateListUIPresenter)
    {
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        if (pContext)
        {
            pContext->Release();
        }

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    _UninitFunctionProviderSink();

    _UninitThreadFocusSink();

    _UninitActiveLanguageProfileNotifySink();

    _UninitKeyEventSink();

    _UninitThreadMgrEventSink();

    CCompartment CompartmentKeyboardOpen(_pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._ClearCompartment();

    CCompartment CompartmentDoubleSingleByte(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._ClearCompartment();

    CCompartment CompartmentPunctuation(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._ClearCompartment();

    if (_pThreadMgr != nullptr)
    {
        _pThreadMgr->Release();
    }

    _tfClientId = TF_CLIENTID_NULL;

    if (_pDocMgrLastFocused)
    {
        _pDocMgrLastFocused->Release();
		_pDocMgrLastFocused = nullptr;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::GetType
//
//----------------------------------------------------------------------------
HRESULT CDIME::GetType(__RPC__out GUID *pguid)
{
    HRESULT hr = E_INVALIDARG;
    if (pguid)
    {
        *pguid = Global::DIMECLSID;
        hr = S_OK;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetDescription
//
//----------------------------------------------------------------------------
HRESULT CDIME::GetDescription(__RPC__deref_out_opt BSTR *pbstrDesc)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDesc != nullptr)
    {
        *pbstrDesc = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunctionProvider::::GetFunction
//
//----------------------------------------------------------------------------
HRESULT CDIME::GetFunction(__RPC__in REFGUID rguid, __RPC__in REFIID riid, __RPC__deref_out_opt IUnknown **ppunk)
{
    HRESULT hr = E_NOINTERFACE;

    if ((IsEqualGUID(rguid, GUID_NULL)) 
        && (IsEqualGUID(riid, __uuidof(ITfFnSearchCandidateProvider))))
    {
        hr = _pITfFnSearchCandidateProvider->QueryInterface(riid, (void**)ppunk);
    }
    else if (IsEqualGUID(rguid, GUID_NULL))
    {
        hr = QueryInterface(riid, (void **)ppunk);
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFunction::GetDisplayName
//
//----------------------------------------------------------------------------
HRESULT CDIME::GetDisplayName(_Out_ BSTR *pbstrDisplayName)
{
    HRESULT hr = E_INVALIDARG;
    if (pbstrDisplayName != nullptr)
    {
        *pbstrDisplayName = nullptr;
        hr = E_NOTIMPL;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// ITfFnGetPreferredTouchKeyboardLayout::GetLayout
// The tkblayout will be Optimized layout.
//----------------------------------------------------------------------------
HRESULT CDIME::GetLayout(_Out_ TKBLayoutType *ptkblayoutType, _Out_ WORD *pwPreferredLayoutId)
{
    HRESULT hr = E_INVALIDARG;
    if ((ptkblayoutType != nullptr) && (pwPreferredLayoutId != nullptr))
    {
        *ptkblayoutType = TKBLT_UNDEFINED;
        *pwPreferredLayoutId = 0;
        hr = S_OK;
    }
    return hr;
}