// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "Globals.h"
#include "EditSession.h"
#include "DIME.h"
#include "CandidateListUIPresenter.h"
#include "CompositionProcessorEngine.h"
#include "DebugLog.h"

//////////////////////////////////////////////////////////////////////
//
// CDIME class
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _IsRangeCovered
//
// Returns TRUE if pRangeTest is entirely contained within pRangeCover.
//
//----------------------------------------------------------------------------

BOOL CDIME::_IsRangeCovered(TfEditCookie ec, _In_ ITfRange *pRangeTest, _In_ ITfRange *pRangeCover)
{
    LONG lResult = 0;;

    if (FAILED(pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult)) 
        || (lResult > 0))
    {
        return FALSE;
    }

    if (FAILED(pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult)) 
        || (lResult < 0))
    {
        return FALSE;
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// _DeleteCandidateList
//
//----------------------------------------------------------------------------

VOID CDIME::_DeleteCandidateList(BOOL isForce, _In_opt_ ITfContext *pContext)
{
    DIME_DEBUG_LOG(L"isForce=%d hasPresenter=%d keyLen=%llu",
        isForce ? 1 : 0,
        _pCandidateListUIPresenter ? 1 : 0,
        _pCompositionProcessorEngine ? _pCompositionProcessorEngine->GetVirtualKeyLength() : 0);

    isForce;pContext;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->PurgeVirtualKey();

    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }
}

//+---------------------------------------------------------------------------
//
// _HandleComplete
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleComplete(TfEditCookie ec, _In_ ITfContext *pContext)
{
    _DeleteCandidateList(FALSE, pContext);

    // just terminate the composition
    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCancel
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCancel(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // Canceling the composition also ends any temporary English input mode,
    // so "Esc cancels" leaves the IME in the normal state.
    if (_pCompositionProcessorEngine)
    {
        _pCompositionProcessorEngine->SetEnglishInput(FALSE);
    }

    _RemoveDummyCompositionForComposing(ec, _pComposition);

    _DeleteCandidateList(FALSE, pContext);

    _TerminateComposition(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInput
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionInput(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;

    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    if ((_pCandidateListUIPresenter != nullptr) && (_candidateMode != CANDIDATE_INCREMENTAL))
    {
        _HandleCompositionFinalize(ec, pContext, FALSE);
    }

    // Start composition in this edit session (Word fails on nested sync sessions).
    if (!_IsComposing())
    {
        _StartCompositionAt(ec, pContext);
    }

    // first, test where a keystroke would go in the document if we did an insert
    if (pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched) != S_OK || fetched != 1)
    {
        return S_FALSE;
    }

    // is the insertion point covered by a composition?
    if (_pComposition && SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
    {
        isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);

        pRangeComposition->Release();

        if (!isCovered)
        {
            goto Exit;
        }
    }

    // Add virtual key to composition processor engine
    //
    // Temporary pinyin mode: when the buffer is empty and the user types 'z',
    // switch to pinyin input. 'z' itself is consumed as the mode switch and is
    // NOT added to the keystroke buffer.
    if (wch == L'z' && pCompositionProcessorEngine->GetVirtualKeyLength() == 0 &&
        !pCompositionProcessorEngine->IsPinyinInput() && pCompositionProcessorEngine->IsPinyinDictionaryAvailable())
    {
        pCompositionProcessorEngine->SetPinyinInput(TRUE);
        DIME_DEBUG_LOG(L"pinyin mode ON (z triggered)");

        // Show the candidate window immediately with the "拼" indicator, even
        // though the keystroke buffer is still empty.
        CStringRange emptyKey;
        if (!_pCandidateListUIPresenter)
        {
            _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        }
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_UpdateEditCookie(ec);
            _pCandidateListUIPresenter->_RefreshCandidateContent(&emptyKey, nullptr);
        }
        return S_OK;
    }

    // Temporary English mode (triggered by ';' on an empty buffer):
    // switch into English mode and show the "英" indicator, then return.
    // The ';' itself is consumed as the mode switch and is NOT added to the buffer.
    if (wch == L';' && pCompositionProcessorEngine->GetVirtualKeyLength() == 0 &&
        !pCompositionProcessorEngine->IsEnglishInput())
    {
        pCompositionProcessorEngine->SetEnglishInput(TRUE);
        DIME_DEBUG_LOG(L"english mode ON (; triggered)");

        // Show the candidate window immediately with the "英" indicator, even
        // though the keystroke buffer is still empty.
        CStringRange emptyKey;
        if (!_pCandidateListUIPresenter)
        {
            _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        }
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_UpdateEditCookie(ec);
            _pCandidateListUIPresenter->_RefreshCandidateContent(&emptyKey, nullptr);
        }
        return S_OK;
    }

    // Enter temporary English mode by typing a capital letter on an empty buffer:
    // the capital seeds the English text with its case preserved (e.g. "A...").
    if (wch >= L'A' && wch <= L'Z' &&
        pCompositionProcessorEngine->GetVirtualKeyLength() == 0 &&
        !pCompositionProcessorEngine->IsEnglishInput())
    {
        pCompositionProcessorEngine->SetEnglishInput(TRUE);
        DIME_DEBUG_LOG(L"english mode ON (capital letter triggered)");
        // Fall through to the English-collection branch below, which appends the
        // typed capital to the buffer and refreshes the window.
    }

    // Temporary English mode: collect characters literally (no dictionary lookup).
    // Only the code area shows the input; Enter commits it, space does not.
    if (pCompositionProcessorEngine->IsEnglishInput())
    {
        DWORD_PTR engLen = pCompositionProcessorEngine->GetVirtualKeyLength();
        if (engLen < ENGLISH_MAX_CODE_LENGTH)
        {
            pCompositionProcessorEngine->AddVirtualKey(wch);
        }
        else
        {
            MessageBeep(MB_OK);
        }

        CStringRange keystroke;
        keystroke.Set(pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(),
            pCompositionProcessorEngine->GetVirtualKeyLength());

        if (!_pCandidateListUIPresenter)
        {
            _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        }
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_UpdateEditCookie(ec);
            _pCandidateListUIPresenter->_RefreshCandidateContent(&keystroke, nullptr);
        }
        return S_OK;
    }

    if (!pCompositionProcessorEngine->AddVirtualKey(wch))
    {
        if (pCompositionProcessorEngine->GetVirtualKeyLength() >= WUBI_MAX_CODE_LENGTH &&
            _pCandidateListUIPresenter)
        {
            MessageBeep(MB_OK);
            CStringRange keystroke;
            keystroke.Set(pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(),
                pCompositionProcessorEngine->GetKeystrokeBuffer()->GetLength());
            _pCandidateListUIPresenter->_UpdateEditCookie(ec);
            _pCandidateListUIPresenter->_RefreshCandidateContent(&keystroke, nullptr);
        }
        goto Exit;
    }

    _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);

Exit:
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionInputWorker
//
// If the keystroke happens within a composition, eat the key and return S_OK.
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionInputWorker(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    //
    // Get candidate string from composition processor engine
    //
    CDIMEArray<CCandidateListItem> candidateList;

    pCompositionProcessorEngine->GetCandidateList(&candidateList, TRUE, FALSE);

    DWORD_PTR keyLen = pCompositionProcessorEngine->GetVirtualKeyLength();
    DIME_DEBUG_LOG(L"keyLen=%llu candidates=%u hasPresenter=%d",
        keyLen,
        candidateList.Count(),
        _pCandidateListUIPresenter ? 1 : 0);

    if (!pCompositionProcessorEngine->IsPinyinInput() && !pCompositionProcessorEngine->IsEnglishInput() && keyLen == WUBI_MAX_CODE_LENGTH)
    {
        if (candidateList.Count() == 0)
        {
            DIME_DEBUG_LOG(L"branch=empty4 code=%.*s",
                (int)keyLen,
                pCompositionProcessorEngine->GetKeystrokeBuffer()->Get());
            MessageBeep(MB_OK);
            CStringRange emptyCodeKeystroke;
            emptyCodeKeystroke.Set(pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(), pCompositionProcessorEngine->GetKeystrokeBuffer()->GetLength());
            _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
            if (_pCandidateListUIPresenter)
            {
                _pCandidateListUIPresenter->_UpdateEditCookie(ec);
                _pCandidateListUIPresenter->_RefreshCandidateContent(&emptyCodeKeystroke, nullptr);
            }
            return hr;
        }
        if (candidateList.Count() == 1)
        {
            CCandidateListItem *pLI = candidateList.GetAt(0);
            DIME_DEBUG_LOG(L"branch=autoCommit4 code=%.*s candidateLen=%llu",
                (int)keyLen,
                pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(),
                pLI->_ItemString.GetLength());
            hr = _AddCharAndFinalize(ec, pContext, &pLI->_ItemString);
            if (SUCCEEDED(hr))
            {
                // _HandleCancel clears composition range and erases the text just committed.
                _HandleComplete(ec, pContext);
            }
            else
            {
                DIME_WARNING_LOG(L"autoCommit4 AddCharAndFinalize failed hr=0x%08X", hr);
            }
            return hr;
        }
    }

    CStringRange keystroke;
    keystroke.Set(pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(), pCompositionProcessorEngine->GetKeystrokeBuffer()->GetLength());

    if (candidateList.Count() || keystroke.GetLength() > 0)
    {
        _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_UpdateEditCookie(ec);
            _pCandidateListUIPresenter->_RefreshCandidateContent(
                &keystroke,
                candidateList.Count() > 0 ? &candidateList : nullptr);
        }
    }
    else if (_pCandidateListUIPresenter)
    {
        DIME_DEBUG_LOG(L"branch=refreshEmpty noCandidates noKeystroke");
        _pCandidateListUIPresenter->_RefreshCandidateContent(nullptr, nullptr);
    }
    else
    {
        DIME_DEBUG_LOG(L"branch=noop noPresenter");
    }

    return hr;
}
//+---------------------------------------------------------------------------
//
// _CreateAndStartCandidate
//
//----------------------------------------------------------------------------

HRESULT CDIME::_CreateAndStartCandidate(_In_ CCompositionProcessorEngine *pCompositionProcessorEngine, TfEditCookie ec, _In_ ITfContext *pContext)
{
    HRESULT hr = S_OK;

    DIME_DEBUG_LOG(L"enter hasPresenter=%d hasComposition=%d hasWindow=%d mode=%d",
        _pCandidateListUIPresenter ? 1 : 0,
        _pComposition ? 1 : 0,
        (_pCandidateListUIPresenter && _pCandidateListUIPresenter->HasCandidateWindow()) ? 1 : 0,
        _candidateMode);

    if (!_pComposition && pContext)
    {
        _StartCompositionAt(ec, pContext);
    }

    if (((_candidateMode == CANDIDATE_PHRASE) && (_pCandidateListUIPresenter))
        || ((_candidateMode == CANDIDATE_NONE) && (_pCandidateListUIPresenter)))
    {
        // Recreate candidate list
        _pCandidateListUIPresenter->_EndCandidateList();
        delete _pCandidateListUIPresenter;
        _pCandidateListUIPresenter = nullptr;

        _candidateMode = CANDIDATE_NONE;
        _isCandidateWithWildcard = FALSE;
    }

    if (_pCandidateListUIPresenter == nullptr)
    {
        _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
            CATEGORY_CANDIDATE,
            pCompositionProcessorEngine->GetCandidateListIndexRange(),
            FALSE);
        if (!_pCandidateListUIPresenter)
        {
            return E_OUTOFMEMORY;
        }

        _candidateMode = CANDIDATE_INCREMENTAL;
        _isCandidateWithWildcard = FALSE;

        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            ITfRange* pRange = nullptr;
            if (_pComposition && SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
                pRange->Release();
            }
            else
            {
                TF_SELECTION sel = {0};
                ULONG fetched = 0;
                if (SUCCEEDED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1)
                {
                    hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, sel.range, pCompositionProcessorEngine->GetCandidateWindowWidth());
                    sel.range->Release();
                }
            }
            pDocumentMgr->Release();
        }
    }
    else if (!_pCandidateListUIPresenter->HasCandidateWindow())
    {
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            ITfRange* pRange = nullptr;
            if (_pComposition && SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
                pRange->Release();
            }
            else
            {
                TF_SELECTION sel = {0};
                ULONG fetched = 0;
                if (SUCCEEDED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1)
                {
                    hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, sel.range, pCompositionProcessorEngine->GetCandidateWindowWidth());
                    sel.range->Release();
                }
            }
            pDocumentMgr->Release();
        }
    }

    DIME_DEBUG_LOG(L"exit hr=0x%08X hasPresenter=%d hasWindow=%d",
        hr,
        _pCandidateListUIPresenter ? 1 : 0,
        (_pCandidateListUIPresenter && _pCandidateListUIPresenter->HasCandidateWindow()) ? 1 : 0);

    return hr;
}

//+---------------------------------------------------------------------------
//
// _RefreshActiveCandidateUI
//
//----------------------------------------------------------------------------

HRESULT CDIME::_RefreshActiveCandidateUI(TfEditCookie ec, _In_ ITfContext *pContext)
{
    if (!_pCompositionProcessorEngine)
    {
        return S_OK;
    }

    DWORD_PTR keyLen = _pCompositionProcessorEngine->GetVirtualKeyLength();
    if (keyLen == 0)
    {
        DIME_DEBUG_LOG(L"skip keyLen=0");
        return S_OK;
    }

    CStringRange keystroke;
    keystroke.Set(_pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(), keyLen);

    if (!_pCandidateListUIPresenter || !_pCandidateListUIPresenter->HasCandidateWindow())
    {
        DIME_DEBUG_LOG(L"recreate presenter=%d hasWindow=%d code=%.*s",
            _pCandidateListUIPresenter ? 1 : 0,
            (_pCandidateListUIPresenter && _pCandidateListUIPresenter->HasCandidateWindow()) ? 1 : 0,
            (int)keyLen,
            keystroke.Get());
        _CreateAndStartCandidate(_pCompositionProcessorEngine, ec, pContext);
    }

    if (_pCandidateListUIPresenter)
    {
        CDIMEArray<CCandidateListItem> candidateList;
        _pCompositionProcessorEngine->GetCandidateList(&candidateList, TRUE, FALSE);

        _pCandidateListUIPresenter->_UpdateEditCookie(ec);
        _pCandidateListUIPresenter->_RefreshCandidateContent(
            &keystroke,
            candidateList.Count() > 0 ? &candidateList : nullptr);
    }
    else
    {
        DIME_DEBUG_LOG(L"refreshFailed noPresenter code=%.*s", (int)keyLen, keystroke.Get());
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionFinalize
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionFinalizeRaw(TfEditCookie ec, _In_ ITfContext *pContext)
{
    // Enter during composition: commit the raw typed code (the English letters
    // entered so far) as literal text and clear, instead of the Chinese candidate.
    if (_pCompositionProcessorEngine)
    {
        DWORD_PTR len = _pCompositionProcessorEngine->GetVirtualKeyLength();
        if (len > 0 && _pCompositionProcessorEngine->GetKeystrokeBuffer())
        {
            CStringRange raw;
            raw.Set(_pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(), len);
            _AddCharAndFinalize(ec, pContext, &raw);
        }
    }
    _HandleComplete(ec, pContext);
    return S_OK;
}

HRESULT CDIME::_HandleCompositionFinalize(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isCandidateList)
{
    HRESULT hr = S_OK;

    // Temporary English mode: commit the collected English text on Enter, then
    // return to the normal state. Space never reaches here (it is an INPUT key).
    if (_pCompositionProcessorEngine && _pCompositionProcessorEngine->IsEnglishInput())
    {
        _pCompositionProcessorEngine->SetEnglishInput(FALSE);

        DWORD_PTR engLen = _pCompositionProcessorEngine->GetVirtualKeyLength();
        if (engLen > 0)
        {
            CStringRange engString;
            engString.Set(_pCompositionProcessorEngine->GetKeystrokeBuffer()->Get(), engLen);
            hr = _AddCharAndFinalize(ec, pContext, &engString);
        }

        _HandleComplete(ec, pContext);
        return hr;
    }

    if (isCandidateList && _pCandidateListUIPresenter)
    {
        // Finalize selected candidate string from CCandidateListUIPresenter
        DWORD_PTR candidateLen = 0;
        const WCHAR *pCandidateString = nullptr;

        candidateLen = _pCandidateListUIPresenter->_GetSelectedCandidateString(&pCandidateString);

        CStringRange candidateString;
        candidateString.Set(pCandidateString, candidateLen);

        if (candidateLen)
        {
            hr = _AddCharAndFinalize(ec, pContext, &candidateString);
            if (FAILED(hr))
            {
                return hr;
            }

            _HandleComplete(ec, pContext);
            return S_OK;
        }
    }
    else
    {
        // Finalize current text store strings
        if (_IsComposing())
        {
            ULONG fetched = 0;
            TF_SELECTION tfSelection;

            if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
            {
                return S_FALSE;
            }

            ITfRange* pRangeComposition = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
            {
                if (_IsRangeCovered(ec, tfSelection.range, pRangeComposition))
                {
                    _EndComposition(pContext);
                }

                pRangeComposition->Release();
            }

            tfSelection.range->Release();
        }
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionConvert
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionConvert(TfEditCookie ec, _In_ ITfContext *pContext, BOOL isWildcardSearch)
{
    HRESULT hr = S_OK;

    CDIMEArray<CCandidateListItem> candidateList;

    //
    // Get candidate string from composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;
    pCompositionProcessorEngine->GetCandidateList(&candidateList, FALSE, isWildcardSearch);

    int nCount = candidateList.Count();
    if (nCount == 0)
    {
        if (pCompositionProcessorEngine->GetVirtualKeyLength() == WUBI_MAX_CODE_LENGTH)
        {
            MessageBeep(MB_OK);
        }
        return S_OK;
    }

    if (nCount)
    {
        if (_pCandidateListUIPresenter)
        {
            _pCandidateListUIPresenter->_EndCandidateList();
            delete _pCandidateListUIPresenter;
            _pCandidateListUIPresenter = nullptr;

            _candidateMode = CANDIDATE_NONE;
            _isCandidateWithWildcard = FALSE;
        }

        // 
        // create an instance of the candidate list class.
        // 
        if (_pCandidateListUIPresenter == nullptr)
        {
            _pCandidateListUIPresenter = new (std::nothrow) CCandidateListUIPresenter(this, Global::AtomCandidateWindow,
                CATEGORY_CANDIDATE,
                pCompositionProcessorEngine->GetCandidateListIndexRange(),
                FALSE);
            if (!_pCandidateListUIPresenter)
            {
                return E_OUTOFMEMORY;
            }

            _candidateMode = CANDIDATE_ORIGINAL;
        }

        _isCandidateWithWildcard = isWildcardSearch;

        // we don't cache the document manager object. So get it from pContext.
        ITfDocumentMgr* pDocumentMgr = nullptr;
        if (SUCCEEDED(pContext->GetDocumentMgr(&pDocumentMgr)))
        {
            // get the composition range.
            ITfRange* pRange = nullptr;
            if (SUCCEEDED(_pComposition->GetRange(&pRange)))
            {
                hr = _pCandidateListUIPresenter->_StartCandidateList(_tfClientId, pDocumentMgr, pContext, ec, pRange, pCompositionProcessorEngine->GetCandidateWindowWidth());
                pRange->Release();
            }
            pDocumentMgr->Release();
        }
        if (SUCCEEDED(hr))
        {
            _pCandidateListUIPresenter->_SetText(&candidateList, FALSE);
            _pCandidateListUIPresenter->_PresentCandidateWindow();
        }
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionBackspace
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionBackspace(TfEditCookie ec, _In_ ITfContext *pContext)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;
    BOOL isCovered = TRUE;

    // Start the new (std::nothrow) compositon if there is no composition.
    if (!_IsComposing())
    {
        return S_OK;
    }

    // first, test where a keystroke would go in the document if we did an insert
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched)) || fetched != 1)
    {
        return S_FALSE;
    }

    // is the insertion point covered by a composition?
    if (_pComposition && SUCCEEDED(_pComposition->GetRange(&pRangeComposition)))
    {
        isCovered = _IsRangeCovered(ec, tfSelection.range, pRangeComposition);

        pRangeComposition->Release();

        if (!isCovered)
        {
            goto Exit;
        }
    }

    //
    // Add virtual key to composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    DWORD_PTR vKeyLen = pCompositionProcessorEngine->GetVirtualKeyLength();

    if (vKeyLen)
    {
        pCompositionProcessorEngine->RemoveVirtualKey(vKeyLen - 1);

        if (pCompositionProcessorEngine->GetVirtualKeyLength())
        {
            _HandleCompositionInputWorker(pCompositionProcessorEngine, ec, pContext);
        }
        else if (pCompositionProcessorEngine->IsEnglishInput())
        {
            // Buffer just became empty but keep the temporary English mode
            // alive; a further Backspace (while still empty) cancels it.
            if (!_pCandidateListUIPresenter)
            {
                _CreateAndStartCandidate(pCompositionProcessorEngine, ec, pContext);
            }
            if (_pCandidateListUIPresenter)
            {
                _pCandidateListUIPresenter->_UpdateEditCookie(ec);
                CStringRange emptyKey;
                _pCandidateListUIPresenter->_RefreshCandidateContent(&emptyKey, nullptr);
            }
        }
        else
        {
            _HandleCancel(ec, pContext);
        }
    }
    else if (pCompositionProcessorEngine->IsEnglishInput())
    {
        // Buffer is already empty: a further Backspace cancels the temporary
        // English mode.
        pCompositionProcessorEngine->SetEnglishInput(FALSE);
        _HandleCancel(ec, pContext);
    }

Exit:
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionArrowKey
//
// Update the selection within a composition.
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionArrowKey(TfEditCookie ec, _In_ ITfContext *pContext, KEYSTROKE_FUNCTION keyFunction)
{
    ITfRange* pRangeComposition = nullptr;
    TF_SELECTION tfSelection;
    ULONG fetched = 0;

    // get the selection
    if (FAILED(pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &tfSelection, &fetched))
        || fetched != 1)
    {
        // no selection, eat the keystroke
        return S_OK;
    }

    // get the composition range
    if (FAILED(_pComposition->GetRange(&pRangeComposition)))
    {
        goto Exit;
    }

    // For incremental candidate list
    if (_pCandidateListUIPresenter)
    {
        _pCandidateListUIPresenter->AdviseUIChangedByArrowKey(keyFunction);
    }

    pContext->SetSelection(ec, 1, &tfSelection);

    pRangeComposition->Release();

Exit:
    tfSelection.range->Release();
    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionPunctuation
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionPunctuation(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    HRESULT hr = S_OK;

    if (_candidateMode != CANDIDATE_NONE && _pCandidateListUIPresenter)
    {
        DWORD_PTR candidateLen = 0;
        const WCHAR* pCandidateString = nullptr;

        candidateLen = _pCandidateListUIPresenter->_GetSelectedCandidateString(&pCandidateString);

        CStringRange candidateString;
        candidateString.Set(pCandidateString, candidateLen);

        if (candidateLen)
        {
            _AddComposingAndChar(ec, pContext, &candidateString);
        }
    }
    //
    // Get punctuation char from composition processor engine
    //
    CCompositionProcessorEngine* pCompositionProcessorEngine = nullptr;
    pCompositionProcessorEngine = _pCompositionProcessorEngine;

    WCHAR punctuation = pCompositionProcessorEngine->GetPunctuation(wch);

    CStringRange punctuationString;
    punctuationString.Set(&punctuation, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &punctuationString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _HandleCompositionDoubleSingleByte
//
//----------------------------------------------------------------------------

HRESULT CDIME::_HandleCompositionDoubleSingleByte(TfEditCookie ec, _In_ ITfContext *pContext, WCHAR wch)
{
    HRESULT hr = S_OK;

    WCHAR fullWidth = Global::FullWidthCharTable[wch - 0x20];

    CStringRange fullWidthString;
    fullWidthString.Set(&fullWidth, 1);

    // Finalize character
    hr = _AddCharAndFinalize(ec, pContext, &fullWidthString);
    if (FAILED(hr))
    {
        return hr;
    }

    _HandleCancel(ec, pContext);

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// _InvokeKeyHandler
//
// This text service is interested in handling keystrokes to demonstrate the
// use the compositions. Some apps will cancel compositions if they receive
// keystrokes while a compositions is ongoing.
//
// param
//    [in] uCode - virtual key code of WM_KEYDOWN wParam
//    [in] dwFlags - WM_KEYDOWN lParam
//    [in] dwKeyFunction - Function regarding virtual key
//----------------------------------------------------------------------------

HRESULT CDIME::_InvokeKeyHandler(_In_ ITfContext *pContext, UINT code, WCHAR wch, DWORD flags, _KEYSTROKE_STATE keyState)
{
    flags;

    CKeyHandlerEditSession* pEditSession = nullptr;
    HRESULT hr = E_FAIL;

    // we'll insert a char ourselves in place of this keystroke
    pEditSession = new (std::nothrow) CKeyHandlerEditSession(this, pContext, code, wch, keyState);
    if (pEditSession == nullptr)
    {
        goto Exit;
    }

    //
    // Call CKeyHandlerEditSession::DoEditSession().
    //
    // Do not specify TF_ES_SYNC so edit session is not invoked on WinWord
    //
    hr = pContext->RequestEditSession(_tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);

    pEditSession->Release();

Exit:
    return hr;
}
