// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT



#pragma once

class CDIME;

class CEditSessionBase : public ITfEditSession
{
public:
    CEditSessionBase(_In_ CDIME *pTextService, _In_ ITfContext *pContext);
    virtual ~CEditSessionBase();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // ITfEditSession
    virtual STDMETHODIMP DoEditSession(TfEditCookie ec) = 0;

protected:
    ITfContext *_pContext;
    CDIME *_pTextService;

private:
    LONG _refCount;     // COM ref count
};
