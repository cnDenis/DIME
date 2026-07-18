// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "DIME.h"

//+---------------------------------------------------------------------------
//
// CStartCompositinoEditSession
//
//----------------------------------------------------------------------------

class CStartCompositionEditSession : public CEditSessionBase
{
public:
    CStartCompositionEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec);
};

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CStartCompositionEditSession::DoEditSession(TfEditCookie ec)
{
    return _pTextService->_StartCompositionAt(ec, _pContext);
}

//////////////////////////////////////////////////////////////////////
//
// CDIME class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _StartCompositionAt
//
// Start composition in the current edit session. Word rejects nested sync
// RequestEditSession calls from inside an existing edit session.
//----------------------------------------------------------------------------

HRESULT CDIME::_StartCompositionAt(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (_IsComposing() || pContext == nullptr)
    {
        return S_OK;
    }

    HRESULT hr = S_OK;
    ITfInsertAtSelection* pInsertAtSelection = nullptr;
    ITfRange* pRangeInsert = nullptr;
    ITfContextComposition* pContextComposition = nullptr;
    ITfComposition* pComposition = nullptr;

    if (FAILED(pContext->QueryInterface(IID_ITfInsertAtSelection, (void **)&pInsertAtSelection)))
    {
        hr = E_FAIL;
        goto Exit;
    }

    hr = pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, nullptr, 0, &pRangeInsert);
    if (FAILED(hr))
    {
        hr = pInsertAtSelection->InsertTextAtSelection(ec, 0, L"", 0, &pRangeInsert);
    }
    if (FAILED(hr) || pRangeInsert == nullptr)
    {
        goto Exit;
    }

    if (FAILED(pContext->QueryInterface(IID_ITfContextComposition, (void **)&pContextComposition)))
    {
        hr = E_FAIL;
        goto Exit;
    }

    if (SUCCEEDED(pContextComposition->StartComposition(ec, pRangeInsert, this, &pComposition)) && (pComposition != nullptr))
    {
        _SetComposition(pComposition);

        TF_SELECTION tfSelection = {0};
        tfSelection.range = pRangeInsert;
        tfSelection.style.ase = TF_AE_NONE;
        tfSelection.style.fInterimChar = FALSE;

        pContext->SetSelection(ec, 1, &tfSelection);
        _SaveCompositionContext(pContext);
        _OnCompositionStarted(ec, pContext);
        hr = S_OK;
    }
    else
    {
        hr = E_FAIL;
    }

Exit:
    if (pContextComposition != nullptr)
    {
        pContextComposition->Release();
    }

    if (pRangeInsert != nullptr)
    {
        pRangeInsert->Release();
    }

    if (pInsertAtSelection != nullptr)
    {
        pInsertAtSelection->Release();
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _StartComposition
//
// this starts the new (std::nothrow) composition at the selection of the current
// focus context.
//----------------------------------------------------------------------------

void CDIME::_StartComposition(_In_ ITfContext *pContext)
{
    CStartCompositionEditSession* pStartCompositionEditSession = new (std::nothrow) CStartCompositionEditSession(this, pContext);

    if (nullptr != pStartCompositionEditSession)
    {
        HRESULT hr = S_OK;
        pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession, TF_ES_SYNC | TF_ES_READWRITE, &hr);

        pStartCompositionEditSession->Release();
    }
}

//+---------------------------------------------------------------------------
//
// _SaveCompositionContext
//
// this saves the context _pComposition belongs to; we need this to clear
// text attribute in case composition has not been terminated on
// deactivation
//----------------------------------------------------------------------------

void CDIME::_SaveCompositionContext(_In_ ITfContext *pContext)
{
    if (_pContext == pContext)
    {
        return;
    }

    if (_pContext)
    {
        _pContext->Release();
        _pContext = nullptr;
    }

    pContext->AddRef();
    _pContext = pContext;
}
