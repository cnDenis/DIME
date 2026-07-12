// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "TfTextLayoutSink.h"
#include "DIME.h"
#include "GetTextExtentEditSession.h"

CTfTextLayoutSink::CTfTextLayoutSink(_In_ CDIME *pTextService)
{
    _pTextService = pTextService;
    _pTextService->AddRef();

    _pRangeComposition = nullptr;
    _pContextDocument = nullptr;
    _tfEditCookie = TF_INVALID_EDIT_COOKIE;

    _dwCookieTextLayoutSink = TF_INVALID_COOKIE;

    _refCount = 1;

    DllAddRef();
}

CTfTextLayoutSink::~CTfTextLayoutSink()
{
    if (_pTextService)
    {
        _pTextService->Release();
    }

    DllRelease();
}

STDAPI CTfTextLayoutSink::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
    if (ppvObj == nullptr)
    {
        return E_INVALIDARG;
    }

    *ppvObj = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfTextLayoutSink))
    {
        *ppvObj = (ITfTextLayoutSink *)this;
    }

    if (*ppvObj)
    {
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDAPI_(ULONG) CTfTextLayoutSink::AddRef()
{
    return ++_refCount;
}

STDAPI_(ULONG) CTfTextLayoutSink::Release()
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
// ITfTextLayoutSink::OnLayoutChange
//
//----------------------------------------------------------------------------

STDAPI CTfTextLayoutSink::OnLayoutChange(_In_ ITfContext *pContext, TfLayoutCode lcode, _In_ ITfContextView *pContextView)
{
    // we're interested in only document context.
    if (pContext != _pContextDocument)
    {
        return S_OK;
    }

    switch (lcode)
    {
    case TF_LC_CHANGE:
        {
            CGetTextExtentEditSession* pEditSession = nullptr;
            pEditSession = new (std::nothrow) CGetTextExtentEditSession(_pTextService, pContext, pContextView, _pRangeComposition, this);
            if (nullptr != (pEditSession))
            {
                HRESULT hr = S_OK;
                pContext->RequestEditSession(_pTextService->_GetClientId(), pEditSession, TF_ES_SYNC | TF_ES_READ, &hr);

                pEditSession->Release();
            }
        }
        break;

    case TF_LC_DESTROY:
        _LayoutDestroyNotification();
        break;

    }
    return S_OK;
}

HRESULT CTfTextLayoutSink::_StartLayout(_In_ ITfContext *pContextDocument, TfEditCookie ec, _In_ ITfRange *pRangeComposition)
{
    if (_pContextDocument)
    {
        _EndLayout();
    }

    _pContextDocument = pContextDocument;
    _pContextDocument->AddRef();

    _pRangeComposition = pRangeComposition;
    _pRangeComposition->AddRef();

    _tfEditCookie = ec;

    return _AdviseTextLayoutSink();
}

VOID CTfTextLayoutSink::_EndLayout()
{
    if (_pRangeComposition)
    {
        _pRangeComposition->Release();
        _pRangeComposition = nullptr;
    }

    if (_pContextDocument)
    {
        _UnadviseTextLayoutSink();
        _pContextDocument->Release();
        _pContextDocument = nullptr;
    }
}

HRESULT CTfTextLayoutSink::_AdviseTextLayoutSink()
{
    HRESULT hr = S_OK;
    ITfSource* pSource = nullptr;

    hr = _pContextDocument->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink *)this, &_dwCookieTextLayoutSink);
    if (FAILED(hr))
    {
        pSource->Release();
        return hr;
    }

    pSource->Release();

    return hr;
}

HRESULT CTfTextLayoutSink::_UnadviseTextLayoutSink()
{
    HRESULT hr = S_OK;
    ITfSource* pSource = nullptr;

    if (nullptr == _pContextDocument)
    {
        return E_FAIL;
    }

    hr = _pContextDocument->QueryInterface(IID_ITfSource, (void **)&pSource);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pSource->UnadviseSink(_dwCookieTextLayoutSink);
    if (FAILED(hr))
    {
        pSource->Release();
        return hr;
    }

    pSource->Release();

    return hr;
}

static BOOL IsTextExtValid(_In_ const RECT *prc)
{
    return (prc->right > prc->left) || (prc->bottom > prc->top);
}

HRESULT CTfTextLayoutSink::GetTextExtWithFallback(
    TfEditCookie ec,
    _In_ ITfContext *pContext,
    _In_ ITfContextView *pContextView,
    _In_opt_ ITfRange *pRangeComposition,
    _Out_ RECT *lpRect)
{
    BOOL isClipped = TRUE;

    *lpRect = {0, 0, 0, 0};

    if (pRangeComposition)
    {
        if (SUCCEEDED(pContextView->GetTextExt(ec, pRangeComposition, lpRect, &isClipped)) &&
            IsTextExtValid(lpRect))
        {
            return S_OK;
        }
    }

    TF_SELECTION sel = {0};
    ULONG fetched = 0;
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) || fetched != 1)
    {
        return E_FAIL;
    }

    HRESULT hr = pContextView->GetTextExt(ec, sel.range, lpRect, &isClipped);
    sel.range->Release();

    if (FAILED(hr) || !IsTextExtValid(lpRect))
    {
        return E_FAIL;
    }

    return S_OK;
}

HRESULT CTfTextLayoutSink::_GetTextExt(_Out_ RECT *lpRect)
{
    HRESULT hr = S_OK;
    ITfContextView* pContextView = nullptr;

    hr = _pContextDocument->GetActiveView(&pContextView);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = GetTextExtWithFallback(_tfEditCookie, _pContextDocument, pContextView, _pRangeComposition, lpRect);

    pContextView->Release();

    return hr;
}
