// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"

//////////////////////////////////////////////////////////////////////
//
//    ITfEditSession
//        CEditSessionBase
// CEndCompositionEditSession class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// CEndCompositionEditSession
//
//----------------------------------------------------------------------------

class CEndCompositionEditSession : public CEditSessionBase
{
public:
    CEndCompositionEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        _pTextService->_TerminateComposition(ec, _pContext, TRUE);
        return S_OK;
    }

};

class CCancelCompositionEditSession : public CEditSessionBase
{
public:
    CCancelCompositionEditSession(_In_ CDIME *pTextService, _In_ ITfContext *pContext) : CEditSessionBase(pTextService, pContext)
    {
    }

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec)
    {
        _pTextService->_TerminateCompositionCancel(ec, _pContext);
        return S_OK;
    }

};

//////////////////////////////////////////////////////////////////////
//
// CDIME class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _TerminateComposition
//
//----------------------------------------------------------------------------

void CDIME::_TerminateComposition(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCalledFromDeactivate)
{
	isCalledFromDeactivate;

    if (_pComposition != nullptr)
    {
        // remove the display attribute from the composition range.
        _ClearCompositionDisplayAttributes(ec, pContext);

        if (FAILED(_pComposition->EndComposition(ec)))
        {
            // if we fail to EndComposition, then we need to close the reverse reading window.
            _DeleteCandidateList(TRUE, pContext);
        }

        _pComposition->Release();
        _pComposition = nullptr;

        if (_pContext)
        {
            _pContext->Release();
            _pContext = nullptr;
        }
    }

    if (_pCompositionProcessorEngine)
    {
        _pCompositionProcessorEngine->SetPinyinInput(FALSE);
    }
}
//
//----------------------------------------------------------------------------

void CDIME::_EndComposition(_In_opt_ ITfContext *pContext)
{
    CEndCompositionEditSession *pEditSession = new (std::nothrow) CEndCompositionEditSession(this, pContext);
    HRESULT hr = S_OK;

    if (nullptr != pEditSession)
    {
        pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        pEditSession->Release();
    }
}

//+---------------------------------------------------------------------------
//
// _TerminateCompositionCancel
//
// 取消组字: 结束前先清空组字范围内的文本, 使文档不留未提交的输入.
//
//----------------------------------------------------------------------------

void CDIME::_TerminateCompositionCancel(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (_pComposition != nullptr)
    {
        // 删除组字范围内的文本, 再结束 composition (即取消, 而非提交)
        ITfRange* pRange = nullptr;
        if (SUCCEEDED(_pComposition->GetRange(&pRange)) && pRange != nullptr)
        {
            pRange->SetText(ec, 0, L"", 0);
            pRange->Release();
            pRange = nullptr;
        }

        _ClearCompositionDisplayAttributes(ec, pContext);

        if (FAILED(_pComposition->EndComposition(ec)))
        {
            _DeleteCandidateList(TRUE, pContext);
        }

        _pComposition->Release();
        _pComposition = nullptr;

        if (_pContext)
        {
            _pContext->Release();
            _pContext = nullptr;
        }
    }

    if (_pCompositionProcessorEngine)
    {
        _pCompositionProcessorEngine->SetPinyinInput(FALSE);
    }
}

//+---------------------------------------------------------------------------
//
// _CancelComposition
//
//----------------------------------------------------------------------------

void CDIME::_CancelComposition(_In_opt_ ITfContext *pContext)
{
    CCancelCompositionEditSession *pEditSession = new (std::nothrow) CCancelCompositionEditSession(this, pContext);
    HRESULT hr = S_OK;

    if (nullptr != pEditSession)
    {
        pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        pEditSession->Release();
    }
}

