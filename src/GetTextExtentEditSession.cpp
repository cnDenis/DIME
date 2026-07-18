// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "EditSession.h"
#include "GetTextExtentEditSession.h"
#include "TfTextLayoutSink.h"

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CGetTextExtentEditSession::CGetTextExtentEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext, _In_ ITfContextView *pContextView, _In_ ITfRange *pRangeComposition, _In_ CTfTextLayoutSink *pTfTextLayoutSink) : CEditSessionBase(pTextService, pContext)
{
    _pContextView = pContextView;
    _pRangeComposition = pRangeComposition;
    _pTfTextLayoutSink = pTfTextLayoutSink;
}

//+---------------------------------------------------------------------------
//
// ITfEditSession::DoEditSession
//
//----------------------------------------------------------------------------

STDAPI CGetTextExtentEditSession::DoEditSession(TfEditCookie ec)
{
    RECT rc = {0, 0, 0, 0};

    if (SUCCEEDED(CTfTextLayoutSink::GetTextExtWithFallback(ec, _pContext, _pContextView, _pRangeComposition, &rc)))
    {
        _pTfTextLayoutSink->_LayoutChangeNotification(&rc);
    }

    return S_OK;
}
