// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "DIME.h"
#include "CompositionProcessorEngine.h"
#include "TableDictionaryEngine.h"
#include "BinaryDictionaryEngine.h"
#include "DictionarySearch.h"
#include "TfInputProcessorProfile.h"
#include "Globals.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "DebugLog.h"
#include "RegKey.h"


//////////////////////////////////////////////////////////////////////
//
// CDIME implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _AddTextProcessorEngine
//
//----------------------------------------------------------------------------

BOOL CDIME::_AddTextProcessorEngine()
{
    LANGID langid = 0;
    CLSID clsid = GUID_NULL;
    GUID guidProfile = GUID_NULL;

    // Get default profile.
    CTfInputProcessorProfile profile;

    if (FAILED(profile.CreateInstance()))
    {
        return FALSE;
    }

    if (FAILED(profile.GetCurrentLanguage(&langid)))
    {
        return FALSE;
    }

    if (FAILED(profile.GetDefaultLanguageProfile(langid, GUID_TFCAT_TIP_KEYBOARD, &clsid, &guidProfile)))
    {
        return FALSE;
    }

    // Is this already added?
    if (_pCompositionProcessorEngine != nullptr)
    {
        LANGID langidProfile = 0;
        GUID guidLanguageProfile = GUID_NULL;

        guidLanguageProfile = _pCompositionProcessorEngine->GetLanguageProfile(&langidProfile);
        if ((langid == langidProfile) && IsEqualGUID(guidProfile, guidLanguageProfile))
        {
            return TRUE;
        }
    }

    // Create composition processor engine
    if (_pCompositionProcessorEngine == nullptr)
    {
        _pCompositionProcessorEngine = new (std::nothrow) CCompositionProcessorEngine();
    }
    if (!_pCompositionProcessorEngine)
    {
        return FALSE;
    }

    // setup composition processor engine
    if (FALSE == _pCompositionProcessorEngine->SetupLanguageProfile(langid, guidProfile, _GetThreadMgr(), _GetClientId(), _IsSecureMode(), _IsComLess()))
    {
        return FALSE;
    }

    _pCompositionProcessorEngine->SetTextService(this);

    return TRUE;
}

//////////////////////////////////////////////////////////////////////
//
// CompositionProcessorEngine implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::CCompositionProcessorEngine()
{
    _pTableDictionaryEngine = nullptr;
    _pPinyinDictionaryEngine = nullptr;
    _pDictionaryFile = nullptr;
    _pPinyinDictionaryFile = nullptr;
    _isPinyinInput = FALSE;
    _isEnglishInput = FALSE;
    _isOnlyCommon = FALSE;

    _langid = 0xffff;
    _guidProfile = GUID_NULL;
    _tfClientId = TF_CLIENTID_NULL;
    _pThreadMgr = nullptr;

    _pTextService = nullptr;

    _pLanguageBar_IMEMode = nullptr;
    _pLanguageBar_DoubleSingleByte = nullptr;
    _pLanguageBar_Punctuation = nullptr;

    _pCompartmentConversion = nullptr;
    _pCompartmentKeyboardOpenEventSink = nullptr;
    _pCompartmentConversionEventSink = nullptr;
    _pCompartmentDoubleSingleByteEventSink = nullptr;
    _pCompartmentPunctuationEventSink = nullptr;

    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    _isWildcard = FALSE;
    _isDisableWildcardAtFirst = FALSE;
    _hasMakePhraseFromText = FALSE;
    _isKeystrokeSort = FALSE;
    _candidatesTruncated = FALSE;
    _candidateListIncremental = FALSE;
    _candidateListWildcard = FALSE;

    _candidateListPhraseModifier = 0;

    _candidateWndWidth = CAND_WINDOW_WIDTH_PX;

    _imeModeSnapshotValid = FALSE;
    _imeModeSnapshotFullWidth = FALSE;
    _imeModeSnapshotChinesePunctuation = FALSE;

    InitKeyStrokeTable();
}

//+---------------------------------------------------------------------------
//
// dtor
//
//----------------------------------------------------------------------------

CCompositionProcessorEngine::~CCompositionProcessorEngine()
{
    if (_pTableDictionaryEngine)
    {
        delete _pTableDictionaryEngine;
        _pTableDictionaryEngine = nullptr;
    }

    if (_pLanguageBar_IMEMode)
    {
        _pLanguageBar_IMEMode->CleanUp();
        _pLanguageBar_IMEMode->Release();
        _pLanguageBar_IMEMode = nullptr;
    }
    if (_pLanguageBar_DoubleSingleByte)
    {
        _pLanguageBar_DoubleSingleByte->CleanUp();
        _pLanguageBar_DoubleSingleByte->Release();
        _pLanguageBar_DoubleSingleByte = nullptr;
    }
    if (_pLanguageBar_Punctuation)
    {
        _pLanguageBar_Punctuation->CleanUp();
        _pLanguageBar_Punctuation->Release();
        _pLanguageBar_Punctuation = nullptr;
    }

    if (_pCompartmentConversion)
    {
        delete _pCompartmentConversion;
        _pCompartmentConversion = nullptr;
    }
    if (_pCompartmentKeyboardOpenEventSink)
    {
        _pCompartmentKeyboardOpenEventSink->_Unadvise();
        delete _pCompartmentKeyboardOpenEventSink;
        _pCompartmentKeyboardOpenEventSink = nullptr;
    }
    if (_pCompartmentConversionEventSink)
    {
        _pCompartmentConversionEventSink->_Unadvise();
        delete _pCompartmentConversionEventSink;
        _pCompartmentConversionEventSink = nullptr;
    }
    if (_pCompartmentDoubleSingleByteEventSink)
    {
        _pCompartmentDoubleSingleByteEventSink->_Unadvise();
        delete _pCompartmentDoubleSingleByteEventSink;
        _pCompartmentDoubleSingleByteEventSink = nullptr;
    }
    if (_pCompartmentPunctuationEventSink)
    {
        _pCompartmentPunctuationEventSink->_Unadvise();
        delete _pCompartmentPunctuationEventSink;
        _pCompartmentPunctuationEventSink = nullptr;
    }

    if (_pThreadMgr)
    {
        _pThreadMgr->Release();
        _pThreadMgr = nullptr;
    }

    if (_pDictionaryFile)
    {
        delete _pDictionaryFile;
        _pDictionaryFile = nullptr;
    }

    if (_pPinyinDictionaryEngine)
    {
        delete _pPinyinDictionaryEngine;
        _pPinyinDictionaryEngine = nullptr;
    }
    if (_pPinyinDictionaryFile)
    {
        delete _pPinyinDictionaryFile;
        _pPinyinDictionaryFile = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// SetupLanguageProfile
//
// Setup language profile for Composition Processor Engine.
// param
//     [in] LANGID langid = Specify language ID
//     [in] GUID guidLanguageProfile - Specify GUID language profile which GUID is as same as Text Service Framework language profile.
//     [in] ITfThreadMgr - pointer ITfThreadMgr.
//     [in] tfClientId - TfClientId value.
//     [in] isSecureMode - secure mode
// returns
//     If setup succeeded, returns true. Otherwise returns false.
// N.B. For reverse conversion, ITfThreadMgr is NULL, TfClientId is 0 and isSecureMode is ignored.
//+---------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode, BOOL isComLessMode)
{
    BOOL ret = TRUE;
    if ((tfClientId == 0) && (pThreadMgr == nullptr))
    {
        ret = FALSE;
        goto Exit;
    }

    _isComLessMode = isComLessMode;
    _langid = langid;
    _guidProfile = guidLanguageProfile;
    _tfClientId = tfClientId;

    if (_pThreadMgr)
    {
        _pThreadMgr->Release();
        _pThreadMgr = nullptr;
    }
    _pThreadMgr = pThreadMgr;
    if (_pThreadMgr)
    {
        _pThreadMgr->AddRef();
    }

    SetupPreserved(pThreadMgr, tfClientId);	
	InitializeDIMECompartment(pThreadMgr, tfClientId);
    SetupPunctuationPair();
    SetupLanguageBar(pThreadMgr, tfClientId, isSecureMode);
    SetupKeystroke();
    SetupConfiguration();
    SetupDictionaryFile();

Exit:
    return ret;
}

//+---------------------------------------------------------------------------
//
// AddVirtualKey
// Add virtual key code to Composition Processor Engine for used to parse keystroke data.
// param
//     [in] uCode - Specify virtual key code.
// returns
//     State of Text Processor Engine.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::AddVirtualKey(WCHAR wch)
{
    if (!wch)
    {
        return FALSE;
    }

    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();
    DWORD_PTR maxKeystrokeLen = _isEnglishInput ? ENGLISH_MAX_CODE_LENGTH
        : (_isPinyinInput ? PINYIN_MAX_CODE_LENGTH : WUBI_MAX_CODE_LENGTH);
    if (srgKeystrokeBufLen >= maxKeystrokeLen)
    {
        return FALSE;
    }

    //
    // append one keystroke in buffer.
    //
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen + 1 ];
    if (!pwch)
    {
        return FALSE;
    }

    memcpy(pwch, _keystrokeBuffer.Get(), srgKeystrokeBufLen * sizeof(WCHAR));
    pwch[ srgKeystrokeBufLen ] = wch;

    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
    }

    _keystrokeBuffer.Set(pwch, srgKeystrokeBufLen + 1);

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// RemoveVirtualKey
// Remove stored virtual key code.
// param
//     [in] dwIndex   - Specified index.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::RemoveVirtualKey(DWORD_PTR dwIndex)
{
    DWORD_PTR srgKeystrokeBufLen = _keystrokeBuffer.GetLength();

    if (dwIndex + 1 < srgKeystrokeBufLen)
    {
        // shift following eles left
        memmove((BYTE*)_keystrokeBuffer.Get() + (dwIndex * sizeof(WCHAR)),
            (BYTE*)_keystrokeBuffer.Get() + ((dwIndex + 1) * sizeof(WCHAR)),
            (srgKeystrokeBufLen - dwIndex - 1) * sizeof(WCHAR));
    }

    _keystrokeBuffer.Set(_keystrokeBuffer.Get(), srgKeystrokeBufLen - 1);
}

//+---------------------------------------------------------------------------
//
// PurgeVirtualKey
// Purge stored virtual key code.
// param
//     none.
// returns
//     none.
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PurgeVirtualKey()
{
    if (_keystrokeBuffer.Get())
    {
        delete [] _keystrokeBuffer.Get();
        _keystrokeBuffer.Set(NULL, 0);
    }
    // NOTE: do NOT reset _isOnlyCommon here. It is a user preference, not a
    // transient composition state, and lives in a TSF compartment (the single
    // source of truth). Resetting it on every commit/cancel would clobber the
    // persisted "only common characters" mode and desync the candidate-window
    // indicator from the actual dictionary filter.
    _isPinyinInput = FALSE;
    _isEnglishInput = FALSE;
}

WCHAR CCompositionProcessorEngine::GetVirtualKey(DWORD_PTR dwIndex) 
{ 
    if (dwIndex < _keystrokeBuffer.GetLength())
    {
        return *(_keystrokeBuffer.Get() + dwIndex);
    }
    return 0;
}
//+---------------------------------------------------------------------------
//
// GetReadingStrings
// Retrieves string from Composition Processor Engine.
// param
//     [out] pReadingStrings - Specified returns pointer of CUnicodeString.
// returns
//     none
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetReadingStrings(_Inout_ CDIMEArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded)
{
    CStringRange oneKeystroke;

    _hasWildcardIncludedInKeystrokeBuffer = FALSE;

    if (pReadingStrings->Count() == 0 && _keystrokeBuffer.GetLength())
    {
        CStringRange* pNewString = nullptr;

        pNewString = pReadingStrings->Append();
        if (pNewString)
        {
            *pNewString = _keystrokeBuffer;
        }

        for (DWORD index = 0; index < _keystrokeBuffer.GetLength(); index++)
        {
            oneKeystroke.Set(_keystrokeBuffer.Get() + index, 1);

            if (IsWildcard() && !_isPinyinInput && IsWildcardChar(*oneKeystroke.Get()))
            {
                _hasWildcardIncludedInKeystrokeBuffer = TRUE;
            }
        }
    }

    *pIsWildcardIncluded = _hasWildcardIncludedInKeystrokeBuffer;
}

//+---------------------------------------------------------------------------
//
// GetCandidateList
//
//----------------------------------------------------------------------------

static BOOL _BuildWubiWildcardSearchKey(_In_ CStringRange *pKeystrokeBuffer, _Out_ CStringRange *pSearchKey, _Outptr_result_maybenull_ PWCHAR *ppAlloc)
{
    *ppAlloc = nullptr;

    DWORD_PTR keystrokeLen = pKeystrokeBuffer->GetLength();
    if (keystrokeLen == 0)
    {
        return FALSE;
    }

    PWCHAR pwch = new (std::nothrow) WCHAR[keystrokeLen + 1];
    if (!pwch)
    {
        return FALSE;
    }

    for (DWORD_PTR i = 0; i < keystrokeLen; i++)
    {
        WCHAR wch = *(pKeystrokeBuffer->Get() + i);
        if (towupper(wch) == towupper(WUBI_WILDCARD_CHAR))
        {
            pwch[i] = L'?';
        }
        else
        {
            pwch[i] = wch;
        }
    }
    pwch[keystrokeLen] = L'\0';

    *ppAlloc = pwch;
    pSearchKey->Set(pwch, keystrokeLen);
    return TRUE;
}

UINT CCompositionProcessorEngine::GetTruncatedCandidateMaxCount() const
{
    return WUBI_INITIAL_CANDIDATE_PAGES * _candidateListIndexRange.Count();
}

void CCompositionProcessorEngine::LoadFullCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    GetCandidateList(pCandidateList, _candidateListIncremental, _candidateListWildcard, TRUE);
}

void CCompositionProcessorEngine::GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch, BOOL loadAllCandidates)
{
    // In temporary English mode characters are collected literally; no
    // dictionary lookup and no candidate list is produced.
    if (_isEnglishInput)
    {
        return;
    }

    if (!IsDictionaryAvailable())
    {
        return;
    }

    if (_isPinyinInput)
    {
        _GetPinyinCandidateList(pCandidateList, loadAllCandidates);
        return;
    }

    _candidateListIncremental = isIncrementalWordSearch;
    _candidateListWildcard = isWildcardSearch;
    _candidatesTruncated = FALSE;

    UINT maxCount = 0;
    if (!loadAllCandidates && isIncrementalWordSearch)
    {
        maxCount = GetTruncatedCandidateMaxCount();
    }

    PWCHAR pwchAlloc = nullptr;

    if (isIncrementalWordSearch)
    {
        DWORD_PTR keystrokeLen = _keystrokeBuffer.GetLength();
        DWORD wildcardIndex = static_cast<DWORD>(keystrokeLen);
        BOOL isFindWildcard = FALSE;
        BOOL hasMore = FALSE;

        if (IsWildcard())
        {
            for (DWORD i = 0; i < keystrokeLen; i++)
            {
                if (IsWildcardChar(*(_keystrokeBuffer.Get() + i)))
                {
                    isFindWildcard = TRUE;
                    wildcardIndex = i;
                    break;
                }
            }
        }

        if (isFindWildcard)
        {
            CStringRange wildcardSearch;
            if (!_BuildWubiWildcardSearchKey(&_keystrokeBuffer, &wildcardSearch, &pwchAlloc))
            {
                return;
            }
            _pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, pCandidateList, maxCount, &hasMore);
        }
        else
        {
            _pTableDictionaryEngine->CollectWordByPrefix(&_keystrokeBuffer, pCandidateList, maxCount, &hasMore);
        }

        if (hasMore)
        {
            _candidatesTruncated = TRUE;
        }

        if (0 >= pCandidateList->Count())
        {
            delete [] pwchAlloc;
            return;
        }

        if (IsKeystrokeSort())
        {
            _pTableDictionaryEngine->SortListItemByFindKeyCode(pCandidateList);
        }

        DWORD_PTR keystrokeBufferLen = isFindWildcard ? wildcardIndex : keystrokeLen;
        for (UINT index = 0; index < pCandidateList->Count(); index++)
        {
            CCandidateListItem *pLI = pCandidateList->GetAt(index);
            if (pLI->_FindKeyCode.GetLength() > keystrokeBufferLen)
            {
                CStringRange newFindKeyCode;
                newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keystrokeBufferLen, pLI->_FindKeyCode.GetLength() - keystrokeBufferLen);
                pLI->_FindKeyCode.Set(newFindKeyCode);
            }
            else
            {
                CStringRange emptyKey;
                emptyKey.Set(L"", 0);
                pLI->_FindKeyCode.Set(emptyKey);
            }
        }

        delete [] pwchAlloc;
    }
    else if (isWildcardSearch)
    {
        CStringRange wildcardSearch;
        if (_BuildWubiWildcardSearchKey(&_keystrokeBuffer, &wildcardSearch, &pwchAlloc))
        {
            _pTableDictionaryEngine->CollectWordForWildcard(&wildcardSearch, pCandidateList);
            delete [] pwchAlloc;
        }
    }
    else
    {
        _pTableDictionaryEngine->CollectWord(&_keystrokeBuffer, pCandidateList);
    }

    for (UINT index = 0; index < pCandidateList->Count();)
    {
        CCandidateListItem *pLI = pCandidateList->GetAt(index);
        CStringRange startItemString;
        CStringRange endItemString;

        startItemString.Set(pLI->_ItemString.Get(), 1);
        endItemString.Set(pLI->_ItemString.Get() + pLI->_ItemString.GetLength() - 1, 1);

        index++;
    }
}

//+---------------------------------------------------------------------------
//
// _GetPinyinCandidateList
//   Temporary pinyin input: search the pinyin dictionary with the keystroke
//   buffer treated as a pinyin string (uppercased to match the dictionary).
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::_GetPinyinCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL loadAllCandidates)
{
    if (!IsPinyinDictionaryAvailable())
    {
        return;
    }

    DWORD_PTR len = _keystrokeBuffer.GetLength();
    if (len == 0)
    {
        return;
    }

    _candidatesTruncated = FALSE;

    // pinyin dictionary keys are uppercase; build an uppercased search key.
    PWCHAR pwch = new (std::nothrow) WCHAR[len + 1];
    if (!pwch)
    {
        return;
    }
    for (DWORD_PTR i = 0; i < len; i++)
    {
        pwch[i] = (WCHAR)towupper(*(_keystrokeBuffer.Get() + i));
    }
    pwch[len] = L'\0';

    CStringRange pinyinKey;
    pinyinKey.Set(pwch, len);

    UINT maxCount = 0;
    if (!loadAllCandidates)
    {
        maxCount = GetTruncatedCandidateMaxCount();
    }

    BOOL hasMore = FALSE;
    _pPinyinDictionaryEngine->CollectWordByPrefix(&pinyinKey, pCandidateList, maxCount, &hasMore);
    if (hasMore)
    {
        _candidatesTruncated = TRUE;
    }

    // In temporary pinyin mode the right-side encoding of each candidate must
    // show its WUBI code (looked up from the wubi dictionary by the candidate
    // character), not the pinyin key.
    if (IsDictionaryAvailable())
    {
        for (UINT i = 0; i < pCandidateList->Count(); i++)
        {
            CCandidateListItem* pLI = pCandidateList->GetAt(i);
            if (pLI && pLI->_ItemString.GetLength() > 0)
            {
                CStringRange wubiCode;
                if (GetWubiCodeForWord(&pLI->_ItemString, &wubiCode))
                {
                    pLI->_FindKeyCode.Set(wubiCode.Get(), wubiCode.GetLength());
                }
            }
        }
    }

    delete [] pwch;
}

//+---------------------------------------------------------------------------
//
// GetWubiCodeForWord (reverse lookup)
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::GetWubiCodeForWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode)
{
    if (!_pTableDictionaryEngine || !pWord || !pCode)
    {
        return FALSE;
    }
    return _pTableDictionaryEngine->FindCodeByWord(pWord, pCode);
}

//+---------------------------------------------------------------------------

//
// GetCandidateStringInConverted
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::GetCandidateStringInConverted(CStringRange &searchString, _In_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!IsDictionaryAvailable())
    {
        return;
    }

    // Search phrase from SECTION_TEXT's converted string list
    CStringRange wildcardSearch;
    DWORD_PTR srgKeystrokeBufLen = searchString.GetLength() + 2;
    PWCHAR pwch = new (std::nothrow) WCHAR[ srgKeystrokeBufLen ];
    if (!pwch)
    {
        return;
    }

    StringCchCopyN(pwch, srgKeystrokeBufLen, searchString.Get(), searchString.GetLength());
    StringCchCat(pwch, srgKeystrokeBufLen, L"*");

    // add wildcard char
	size_t len = 0;
	if (StringCchLength(pwch, STRSAFE_MAX_CCH, &len) != S_OK)
    {
        return;
    }
    wildcardSearch.Set(pwch, len);

    _pTableDictionaryEngine->CollectWordFromConvertedStringForWildcard(&wildcardSearch, pCandidateList);

    if (IsKeystrokeSort())
    {
        _pTableDictionaryEngine->SortListItemByFindKeyCode(pCandidateList);
    }

    wildcardSearch.Clear();
    delete [] pwch;
}



//+---------------------------------------------------------------------------

//
// IsPunctuation
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsPunctuation(WCHAR wch)
{
    for (int i = 0; i < ARRAYSIZE(Global::PunctuationTable); i++)
    {
        if (Global::PunctuationTable[i]._Code == wch)
        {
            return TRUE;
        }
    }

    for (UINT j = 0; j < _PunctuationPair.Count(); j++)
    {
        CPunctuationPair* pPuncPair = _PunctuationPair.GetAt(j);

        if (pPuncPair->_punctuation._Code == wch)
        {
            return TRUE;
        }
    }

    for (UINT k = 0; k < _PunctuationNestPair.Count(); k++)
    {
        CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.GetAt(k);

        if (pPuncNestPair->_punctuation_begin._Code == wch)
        {
            return TRUE;
        }
        if (pPuncNestPair->_punctuation_end._Code == wch)
        {
            return TRUE;
        }
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// GetPunctuationPair
//
//----------------------------------------------------------------------------

WCHAR CCompositionProcessorEngine::GetPunctuation(WCHAR wch)
{
    for (int i = 0; i < ARRAYSIZE(Global::PunctuationTable); i++)
    {
        if (Global::PunctuationTable[i]._Code == wch)
        {
            return Global::PunctuationTable[i]._Punctuation;
        }
    }

    for (UINT j = 0; j < _PunctuationPair.Count(); j++)
    {
        CPunctuationPair* pPuncPair = _PunctuationPair.GetAt(j);

        if (pPuncPair->_punctuation._Code == wch)
        {
            if (! pPuncPair->_isPairToggle)
            {
                pPuncPair->_isPairToggle = TRUE;
                return pPuncPair->_punctuation._Punctuation;
            }
            else
            {
                pPuncPair->_isPairToggle = FALSE;
                return pPuncPair->_pairPunctuation;
            }
        }
    }

    for (UINT k = 0; k < _PunctuationNestPair.Count(); k++)
    {
        CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.GetAt(k);

        if (pPuncNestPair->_punctuation_begin._Code == wch)
        {
            if (pPuncNestPair->_nestCount++ == 0)
            {
                return pPuncNestPair->_punctuation_begin._Punctuation;
            }
            else
            {
                return pPuncNestPair->_pairPunctuation_begin;
            }
        }
        if (pPuncNestPair->_punctuation_end._Code == wch)
        {
            if (--pPuncNestPair->_nestCount == 0)
            {
                return pPuncNestPair->_punctuation_end._Punctuation;
            }
            else
            {
                return pPuncNestPair->_pairPunctuation_end;
            }
        }
    }
    return 0;
}

//+---------------------------------------------------------------------------
//
// IsDoubleSingleByte
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsDoubleSingleByte(WCHAR wch)
{
    if (L' ' <= wch && wch <= L'~')
    {
        return TRUE;
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupKeystroke
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupKeystroke()
{
    SetKeystrokeTable(&_KeystrokeComposition);
    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetKeystrokeTable(_Inout_ CDIMEArray<_KEYSTROKE> *pKeystroke)
{
    for (int i = 0; i < 26; i++)
    {
        _KEYSTROKE* pKS = nullptr;

        pKS = pKeystroke->Append();
        if (!pKS)
        {
            break;
        }
        *pKS = _keystrokeTable[i];
    }
}

//+---------------------------------------------------------------------------
//
// SetupPreserved
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    // NOTE: the Shift key is intentionally NOT bound to the IME on/off toggle.
    // Users switch Chinese/English via the language-bar IME button or the
    // system key (Ctrl+Space). Binding Shift here caused it to swallow plain
    // Shift presses.

    TF_PRESERVEDKEY preservedKeyDoubleSingleByte;
    preservedKeyDoubleSingleByte.uVKey = VK_SPACE;
    preservedKeyDoubleSingleByte.uModifiers = TF_MOD_SHIFT;
    SetPreservedKey(Global::DIMEGuidDoubleSingleBytePreserveKey, preservedKeyDoubleSingleByte, Global::DoubleSingleByteDescription, &_PreservedKey_DoubleSingleByte);

    TF_PRESERVEDKEY preservedKeyPunctuation;
    preservedKeyPunctuation.uVKey = VK_OEM_PERIOD;
    preservedKeyPunctuation.uModifiers = TF_MOD_CONTROL;
    SetPreservedKey(Global::DIMEGuidPunctuationPreserveKey, preservedKeyPunctuation, Global::PunctuationDescription, &_PreservedKey_Punctuation);

    TF_PRESERVEDKEY preservedKeyOnlyCommon;
    preservedKeyOnlyCommon.uVKey = 'M';
    preservedKeyOnlyCommon.uModifiers = TF_MOD_CONTROL;
    SetPreservedKey(Global::DIMEGuidOnlyCommonPreserveKey, preservedKeyOnlyCommon, L"OnlyCommon (Ctrl+M)", &_PreservedKey_OnlyCommon);

    InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_Punctuation, pThreadMgr, tfClientId);
    InitPreservedKey(&_PreservedKey_OnlyCommon, pThreadMgr, tfClientId);

    return;
}

//+---------------------------------------------------------------------------
//
// SetKeystrokeTable
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey)
{
    pXPreservedKey->Guid = clsid;

    TF_PRESERVEDKEY *ptfPsvKey1 = pXPreservedKey->TSFPreservedKeyTable.Append();
    if (!ptfPsvKey1)
    {
        return;
    }
    *ptfPsvKey1 = tfPreservedKey;

	size_t srgKeystrokeBufLen = 0;
	if (StringCchLength(pwszDescription, STRSAFE_MAX_CCH, &srgKeystrokeBufLen) != S_OK)
    {
        return;
    }
    pXPreservedKey->Description = new (std::nothrow) WCHAR[srgKeystrokeBufLen + 1];
    if (!pXPreservedKey->Description)
    {
        return;
    }

    StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen, pwszDescription);

    return;
}
//+---------------------------------------------------------------------------
//
// InitPreservedKey
//
// Register a hot key.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    ITfKeystrokeMgr *pKeystrokeMgr = nullptr;

    if (IsEqualGUID(pXPreservedKey->Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr) != S_OK)
    {
        return FALSE;
    }

    for (UINT i = 0; i < pXPreservedKey->TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY preservedKey = *pXPreservedKey->TSFPreservedKeyTable.GetAt(i);
        preservedKey.uModifiers &= 0xffff;

		size_t lenOfDesc = 0;
		if (StringCchLength(pXPreservedKey->Description, STRSAFE_MAX_CCH, &lenOfDesc) != S_OK)
        {
            return FALSE;
        }
        pKeystrokeMgr->PreserveKey(tfClientId, pXPreservedKey->Guid, &preservedKey, pXPreservedKey->Description, static_cast<ULONG>(lenOfDesc));
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// CheckShiftKeyOnly
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable)
{
    for (UINT i = 0; i < pTSFPreservedKeyTable->Count(); i++)
    {
        TF_PRESERVEDKEY *ptfPsvKey = pTSFPreservedKeyTable->GetAt(i);

        if (((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_SHIFT_ONLY & 0xffff0000)) && !Global::IsShiftKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_CONTROL_ONLY & 0xffff0000)) && !Global::IsControlKeyDownOnly) ||
            ((ptfPsvKey->uModifiers & (_TF_MOD_ON_KEYUP_ALT_ONLY & 0xffff0000)) && !Global::IsAltKeyDownOnly)         )
        {
            return FALSE;
        }
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// OnPreservedKey
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    if (IsEqualGUID(rguid, _PreservedKey_IMEMode.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_IMEMode.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        CompartmentKeyboardOpen._SetCompartmentBOOL(isOpen ? FALSE : TRUE);

        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_DoubleSingleByte.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        ToggleDoubleSingleByte(pThreadMgr, tfClientId);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_Punctuation.Guid))
    {
        if (!CheckShiftKeyOnly(&_PreservedKey_Punctuation.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        TogglePunctuation(pThreadMgr, tfClientId);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_OnlyCommon.Guid))
    {
        ToggleOnlyCommon(pThreadMgr, tfClientId);
        *pIsEaten = TRUE;
    }
    else
    {
        *pIsEaten = FALSE;
    }
}

//+---------------------------------------------------------------------------
//
// ToggleDoubleSingleByte / TogglePunctuation / ToggleOnlyCommon
//
//  Direct (programmatic) toggles of the three persistent input-mode
//  compartments. Used by the floating status bar's clickable segments.
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::ToggleDoubleSingleByte(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
    NotifyInputModeChanged(pThreadMgr);
}

void CCompositionProcessorEngine::TogglePunctuation(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    BOOL isPunctuation = FALSE;
    CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._GetCompartmentBOOL(isPunctuation);
    CompartmentPunctuation._SetCompartmentBOOL(isPunctuation ? FALSE : TRUE);
    NotifyInputModeChanged(pThreadMgr);
}

void CCompositionProcessorEngine::ToggleOnlyCommon(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    // Flip the "only common characters" state in its TSF compartment. The
    // compartment is the single source of truth (TSF-persisted, instance-
    // independent), so the filter and the candidate-window indicator stay
    // consistent even if the engine object is recreated between keystrokes.
    CCompartment CompartmentOnlyCommon(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentOnlyCommon);
    BOOL isOnlyCommon = FALSE;
    CompartmentOnlyCommon._GetCompartmentBOOL(isOnlyCommon);
    isOnlyCommon = isOnlyCommon ? FALSE : TRUE;
    CompartmentOnlyCommon._SetCompartmentBOOL(isOnlyCommon);

    _isOnlyCommon = isOnlyCommon;

    _WriteRegistryOnlyCommon(isOnlyCommon);

    if (_pTableDictionaryEngine)
    {
        _pTableDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
    }
    if (_pPinyinDictionaryEngine)
    {
        _pPinyinDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
    }
    DIME_DEBUG_LOG(L"OnlyCommon toggled -> %d", _isOnlyCommon ? 1 : 0);

    // Clear any in-progress reading so the new filter applies cleanly and
    // the stale candidate list is removed (simpler than re-querying).
    if (_pTextService)
    {
        _pTextService->_ResetInputForModeChange();
    }

    NotifyInputModeChanged(pThreadMgr);
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration()
{
    _isWildcard = TRUE;
    _isDisableWildcardAtFirst = TRUE;
    _hasMakePhraseFromText = FALSE;
    _isKeystrokeSort = TRUE;
    _candidateWndWidth = CAND_WINDOW_WIDTH_PX;

    SetInitialCandidateListRange();

    SetDefaultCandidateTextFont();

    return;
}

static void AdviseCompartmentEventSink(_Inout_ CCompartmentEventSink** ppSink, _In_ ITfThreadMgr* pThreadMgr, REFGUID guidCompartment)
{
    if (ppSink == nullptr || *ppSink == nullptr)
    {
        return;
    }

    HRESULT hr = (*ppSink)->_Advise(pThreadMgr, guidCompartment);
    if (FAILED(hr))
    {
        delete *ppSink;
        *ppSink = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// SetupLanguageBar
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode)
{
    DWORD dwEnable = 1;
    CreateLanguageBarButton(dwEnable, GUID_LBI_INPUTMODE, Global::LangbarImeModeDescription, Global::ImeModeDescription, Global::ImeModeOnIcoIndex, Global::ImeModeOffIcoIndex, &_pLanguageBar_IMEMode, isSecureMode);
    CreateLanguageBarButton(dwEnable, Global::DIMEGuidLangBarDoubleSingleByte, Global::LangbarDoubleSingleByteDescription, Global::DoubleSingleByteDescription, Global::DoubleSingleByteOnIcoIndex, Global::DoubleSingleByteOffIcoIndex, &_pLanguageBar_DoubleSingleByte, isSecureMode);
    CreateLanguageBarButton(dwEnable, Global::DIMEGuidLangBarPunctuation, Global::LangbarPunctuationDescription, Global::PunctuationDescription, Global::PunctuationOnIcoIndex, Global::PunctuationOffIcoIndex, &_pLanguageBar_Punctuation, isSecureMode);

    InitLanguageBar(_pLanguageBar_IMEMode, pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    InitLanguageBar(_pLanguageBar_DoubleSingleByte, pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    InitLanguageBar(_pLanguageBar_Punctuation, pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);

    _pCompartmentConversion = new (std::nothrow) CCompartment(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    _pCompartmentKeyboardOpenEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentConversionEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentDoubleSingleByteEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);
    _pCompartmentPunctuationEventSink = new (std::nothrow) CCompartmentEventSink(CompartmentCallback, this);

    AdviseCompartmentEventSink(&_pCompartmentKeyboardOpenEventSink, pThreadMgr, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    AdviseCompartmentEventSink(&_pCompartmentConversionEventSink, pThreadMgr, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION);
    AdviseCompartmentEventSink(&_pCompartmentDoubleSingleByteEventSink, pThreadMgr, Global::DIMEGuidCompartmentDoubleSingleByte);
    AdviseCompartmentEventSink(&_pCompartmentPunctuationEventSink, pThreadMgr, Global::DIMEGuidCompartmentPunctuation);

    return;
}

//+---------------------------------------------------------------------------
//
// CreateLanguageBarButton
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode)
{
	dwEnable;

    if (ppLangBarItemButton)
    {
        *ppLangBarItemButton = new (std::nothrow) CLangBarItemButton(guidLangBar, pwszDescriptionValue, pwszTooltipValue, dwOnIconIndex, dwOffIconIndex, isSecureMode);
    }

    return;
}

//+---------------------------------------------------------------------------
//
// InitLanguageBar
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::InitLanguageBar(_In_ CLangBarItemButton *pLangBarItemButton, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment)
{
    if (pLangBarItemButton)
    {
        if (pLangBarItemButton->_AddItem(pThreadMgr) == S_OK)
        {
            if (pLangBarItemButton->_RegisterCompartment(pThreadMgr, tfClientId, guidCompartment))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// SetupDictionaryFile
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::SetupDictionaryFile()
{
    WCHAR wszFileName[MAX_PATH] = {'\0'};
    DWORD cchA = GetModuleFileName(Global::dllInstanceHandle, wszFileName, ARRAYSIZE(wszFileName));

    // find the last separator to obtain the DLL directory
    size_t iDir = cchA;
    while (iDir--)
    {
        WCHAR wszChar = wszFileName[iDir];
        if (wszChar == L'\\' || wszChar == L'/')
        {
            break;
        }
    }
    size_t dllDirLen = iDir + 1;   // includes trailing separator

    // Locate a shared code table. Both architectures must load ONE dictionary,
    // so we probe candidate "dict" directories and use the first that exists:
    //   1. <dllDir>\dict\        -> flat layout: DIME\dime32.dll + DIME\dict\
    //   2. <dllDir>\..\dict\      -> subfolder layout: DIME\x64\dime.dll + DIME\dict\
    //   3. <dllDir> (no dict)     -> dev/test flat layout with dictionaries in DLL dir
    // The dictionary file name is derived by _LoadDictionary from TEXTSERVICE_DIC.
    const WCHAR* pDictDir = wszFileName;   // default: DLL directory
    size_t dictDirLen = dllDirLen;
    WCHAR wszDictDir[MAX_PATH] = {L'\0'};

    struct { const WCHAR* suffix; size_t baseLen; } candidates[] =
    {
        { L"dict\\", dllDirLen },          // relative to DLL directory
        { L"..\\dict\\", dllDirLen },      // relative to DLL directory (parent)
    };

    for (int c = 0; c < _countof(candidates); c++)
    {
        if (SUCCEEDED(StringCchCopyN(wszDictDir, ARRAYSIZE(wszDictDir), wszFileName, candidates[c].baseLen)) &&
            SUCCEEDED(StringCchCatW(wszDictDir, ARRAYSIZE(wszDictDir), candidates[c].suffix)))
        {
            DWORD attrs = GetFileAttributes(wszDictDir);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                pDictDir = wszDictDir;
                dictDirLen = wcslen(wszDictDir);
                break;
            }
        }
    }

    // wubi main dictionary (required for the IME to work)
    if (!_LoadDictionary(TEXTSERVICE_DIC, pDictDir, dictDirLen, &_pDictionaryFile, &_pTableDictionaryEngine))
    {
        DIME_ERROR_LOG(L"SetupDictionaryFile: main wubi dictionary load FAILED (dir=%s)", pDictDir);
        return FALSE;
    }

    // pinyin dictionary (optional, enables temporary pinyin input via 'z')
    if (!_LoadDictionary(TEXTSERVICE_PINYIN_DIC, pDictDir, dictDirLen, &_pPinyinDictionaryFile, &_pPinyinDictionaryEngine))
    {
        DIME_WARNING_LOG(L"pinyin dictionary not loaded (optional)");
    }

    return TRUE;
}

//+---------------------------------------------------------------------------
//
// GetDictionaryFile
//
//----------------------------------------------------------------------------

CFile* CCompositionProcessorEngine::GetDictionaryFile()
{
    return _pDictionaryFile;
}

//+---------------------------------------------------------------------------
// Dictionary conversion helpers
//
// When a precompiled .bin is missing (or invalid), the engine invokes the
// shipped build_bindict.exe to compile the text source on the fly, then loads
// the resulting .bin. The binary dictionary is the single runtime source of
// truth; there is no automatic fallback to the raw text index.
//----------------------------------------------------------------------------

// Returns the directory containing the DLL (with trailing backslash), or 0.
static DWORD _GetDllDirectory(_Out_writes_(cchBuf) WCHAR* pszBuf, DWORD cchBuf)
{
    WCHAR path[MAX_PATH] = {L'\0'};
    DWORD cch = GetModuleFileName(Global::dllInstanceHandle, path, ARRAYSIZE(path));
    if (cch == 0 || cch >= ARRAYSIZE(path))
    {
        return 0;
    }
    WCHAR* pSlash = wcsrchr(path, L'\\');
    if (!pSlash)
    {
        return 0;
    }
    *pSlash = L'\0';
    if (FAILED(StringCchCopy(pszBuf, cchBuf, path)) ||
        FAILED(StringCchCat(pszBuf, cchBuf, L"\\")))
    {
        return 0;
    }
    return static_cast<DWORD>(wcslen(pszBuf));
}

// Locates the shipped build_bindict.exe. Returns TRUE and fills outPath on success.
static BOOL _FindConverter(_Out_writes_(cchOut) WCHAR* outPath, DWORD cchOut)
{
    WCHAR dllDir[MAX_PATH] = {L'\0'};
    if (!_GetDllDirectory(dllDir, ARRAYSIZE(dllDir)))
    {
        return FALSE;
    }
    // Covers flat (DIME\dict\), subdir (DIME\x64\ + DIME\dict\) and a copy next to the DLL.
    const WCHAR* const candidates[] =
    {
        L"build_bindict.exe",
        L"dict\\build_bindict.exe",
        L"..\\dict\\build_bindict.exe",
    };
    for (int i = 0; i < _countof(candidates); ++i)
    {
        WCHAR candidate[MAX_PATH] = {L'\0'};
        if (SUCCEEDED(StringCchCopy(candidate, ARRAYSIZE(candidate), dllDir)) &&
            SUCCEEDED(StringCchCat(candidate, ARRAYSIZE(candidate), candidates[i])))
        {
            if (GetFileAttributes(candidate) != INVALID_FILE_ATTRIBUTES)
            {
                return SUCCEEDED(StringCchCopy(outPath, cchOut, candidate));
            }
        }
    }
    return FALSE;
}

// Runs build_bindict.exe <txtPath> <binPath> synchronously. Concurrent callers
// (x64/x86 sharing one dict) are serialized via a <binPath>.lock file.
// Returns TRUE if a usable .bin exists afterwards (caller re-validates via IsBuilt).
static BOOL _RunConverter(_In_ LPCWSTR txtPath, _In_ LPCWSTR binPath)
{
    WCHAR lockPath[MAX_PATH] = {L'\0'};
    if (FAILED(StringCchCopy(lockPath, ARRAYSIZE(lockPath), binPath)) ||
        FAILED(StringCchCat(lockPath, ARRAYSIZE(lockPath), L".lock")))
    {
        return FALSE;
    }

    HANDLE hLock = CreateFileW(lockPath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hLock == INVALID_HANDLE_VALUE)
    {
        DIME_ERROR_LOG(L"_RunConverter: cannot create lock %s (err %lu)", lockPath, GetLastError());
        return FALSE;
    }

    OVERLAPPED ov = {0};
    if (LockFileEx(hLock, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov))
    {
        // We only reach here for a missing or invalid .bin; regenerate unconditionally
        // after removing any stale/corrupt file.
        DeleteFileW(binPath);

        WCHAR converter[MAX_PATH] = {L'\0'};
        if (_FindConverter(converter, ARRAYSIZE(converter)))
        {
            WCHAR cmdLine[MAX_PATH * 3] = {L'\0'};
            if (SUCCEEDED(StringCchPrintf(cmdLine, ARRAYSIZE(cmdLine),
                                          L"\"%s\" \"%s\" \"%s\"", converter, txtPath, binPath)))
            {
                STARTUPINFOW si = {sizeof(si)};
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = {0};
                if (CreateProcessW(converter, cmdLine, nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                {
                    if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_TIMEOUT)
                    {
                        TerminateProcess(pi.hProcess, 1);
                        DIME_ERROR_LOG(L"_RunConverter: build_bindict timed out for %s", txtPath);
                    }
                    else
                    {
                        DWORD exitCode = 1;
                        GetExitCodeProcess(pi.hProcess, &exitCode);
                        if (exitCode != 0)
                        {
                            DIME_ERROR_LOG(L"_RunConverter: build_bindict exited %lu (txt=%s)", exitCode, txtPath);
                        }
                    }
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                }
                else
                {
                    DIME_ERROR_LOG(L"_RunConverter: CreateProcess failed (err %lu) for %s", GetLastError(), converter);
                }
            }
        }
        else
        {
            DIME_ERROR_LOG(L"_RunConverter: build_bindict.exe not found near DLL; cannot compile %s", txtPath);
        }
        UnlockFileEx(hLock, 0, 1, 0, &ov);
    }
    else
    {
        DIME_ERROR_LOG(L"_RunConverter: cannot acquire lock %s (err %lu)", lockPath, GetLastError());
    }
    CloseHandle(hLock);

    return (GetFileAttributes(binPath) != INVALID_FILE_ATTRIBUTES);
}

//+---------------------------------------------------------------------------
//
// _LoadDictionary
//   Opens a dictionary file relative to the DLL directory and builds a
//   CTableDictionaryEngine. Used for both the wubi and pinyin dictionaries.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::_LoadDictionary(_In_ LPCWSTR pwszDicName, _In_ LPCWSTR pwszDir, size_t dirLen, _Out_ CFileMapping** ppFile, _Out_ CTableDictionaryEngine** ppEngine)
{
    *ppFile = nullptr;
    *ppEngine = nullptr;

    size_t nameLen = wcslen(pwszDicName);
    size_t bufLen = dirLen + nameLen + 1;
    WCHAR* pwszPath = new (std::nothrow) WCHAR[bufLen];
    if (!pwszPath)
    {
        return FALSE;
    }

    StringCchCopyN(pwszPath, bufLen, pwszDir, dirLen);
    StringCchCatN(pwszPath, bufLen, pwszDicName, nameLen);

    // --- Prefer the precompiled binary dictionary (.bin) when present and valid.
    // It is zero-parse at load and zero-copy at lookup. A .bin alone is
    // sufficient; the text .txt is optional and only opened if it coexists. ---
    WCHAR binPath[MAX_PATH] = {L'\0'};
    BOOL canBin = SUCCEEDED(StringCchCopyW(binPath, ARRAYSIZE(binPath), pwszPath)) &&
                  _ReplaceExtensionWithBin(binPath);

    // Prefer the precompiled binary dictionary (.bin): zero-parse load,
    // zero-copy lookup. If present and valid, use it directly.
    if (canBin && _TryLoadBinary(binPath, pwszPath, ppFile, ppEngine))
    {
        delete [] pwszPath;
        return TRUE;
    }

    // No valid .bin: invoke the shipped converter (txt -> bin) and load the result.
    // The binary dictionary is the single runtime source of truth; we deliberately
    // do NOT fall back to the raw text index. If the text source is missing too,
    // the dictionary is simply unavailable this session.
    if (canBin && GetFileAttributes(pwszPath) != INVALID_FILE_ATTRIBUTES)
    {
        if (_RunConverter(pwszPath, binPath) &&
            _TryLoadBinary(binPath, pwszPath, ppFile, ppEngine))
        {
            delete [] pwszPath;
            return TRUE;
        }
        DIME_ERROR_LOG(L"_LoadDictionary: conversion failed to produce a usable .bin for %s", pwszPath);
        delete [] pwszPath;
        return FALSE;
    }

    DIME_ERROR_LOG(L"_LoadDictionary: no dictionary available (missing both .bin and .txt): %s", pwszPath);
    delete [] pwszPath;
    return FALSE;
}

// Replace the trailing ".txt" (or any extension) of a path with ".bin".
// Both are 3 characters, so the buffer length is unchanged.
BOOL CCompositionProcessorEngine::_ReplaceExtensionWithBin(_Inout_ WCHAR* pwszPath)
{
    WCHAR* dot = wcsrchr(pwszPath, L'.');
    if (!dot)
    {
        return FALSE;
    }
    if (wcslen(dot) < 4)   // need room for ".bin"
    {
        return FALSE;
    }
    dot[1] = L'b';
    dot[2] = L'i';
    dot[3] = L'n';
    dot[4] = L'\0';
    return TRUE;
}

// Opens <pwszBinPath> as a binary dictionary and, if valid, constructs the
// engine. The text <pwszTxtPath> is opened only when it coexists (its ownership
// is returned via *ppFile so the composition engine frees it at teardown).
// On success sets *ppFile/*ppEngine and returns TRUE.
BOOL CCompositionProcessorEngine::_TryLoadBinary(_In_ LPCWSTR pwszBinPath, _In_ LPCWSTR pwszTxtPath,
                                                 _Out_ CFileMapping** ppFile, _Out_ CTableDictionaryEngine** ppEngine)
{
    *ppFile = nullptr;
    *ppEngine = nullptr;

    CFileMapping* pBinFile = new (std::nothrow) CFileMapping();
    if (!pBinFile)
    {
        return FALSE;
    }
    pBinFile->SetRawMode(TRUE);
    if (!pBinFile->CreateFile(pwszBinPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
    {
        delete pBinFile;
        return FALSE;
    }

    CBinaryDictionaryEngine* pBin = new (std::nothrow) CBinaryDictionaryEngine(GetLocale(), pBinFile);
    if (!pBin)
    {
        delete pBinFile;   // not yet owned by pBin
        return FALSE;
    }
    if (!pBin->IsBuilt())
    {
        delete pBin;       // dtor deletes pBinFile
        return FALSE;
    }
    // pBinFile is now owned by pBin.

    // Text dictionary is optional at runtime; open it only if it coexists.
    // Ownership is returned to the caller (engine _pDictionaryFile member).
    CFileMapping* pFile = new (std::nothrow) CFileMapping();
    if (pFile && !pFile->CreateFile(pwszTxtPath, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))
    {
        delete pFile;
        pFile = nullptr;
    }

    CTableDictionaryEngine* pEngine = new (std::nothrow) CTableDictionaryEngine(GetLocale(), pFile, pBin);
    if (!pEngine)
    {
        delete pFile;      // pEngine never constructed -> free manually
        delete pBin;       // dtor deletes pBinFile
        return FALSE;
    }
    pEngine->SetOnlyCommon(_GetCompartmentOnlyCommon());

    *ppFile = pFile;       // may be nullptr when only .bin ships
    *ppEngine = pEngine;   // owns pBin -> pBinFile
    return TRUE;
}



//+---------------------------------------------------------------------------
//
// SetupPunctuationPair
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupPunctuationPair()
{
    // Punctuation pair
    const int pair_count = 2;
    CPunctuationPair punc_quotation_mark(L'"', 0x201C, 0x201D);
    CPunctuationPair punc_apostrophe(L'\'', 0x2018, 0x2019);

    CPunctuationPair puncPairs[pair_count] = {
        punc_quotation_mark,
        punc_apostrophe,
    };

    for (int i = 0; i < pair_count; ++i)
    {
        CPunctuationPair *pPuncPair = _PunctuationPair.Append();
        *pPuncPair = puncPairs[i];
    }

    // Punctuation nest pair
    CPunctuationNestPair punc_angle_bracket(L'<', 0x300A, 0x3008, L'>', 0x300B, 0x3009);

    CPunctuationNestPair* pPuncNestPair = _PunctuationNestPair.Append();
    *pPuncNestPair = punc_angle_bracket;
}

void CCompositionProcessorEngine::_LoadSettings(_Inout_ BOOL &isFullWidth, _Inout_ BOOL &isChinesePunctuation)
{
    CRegKey reg;
    LONG lr = reg.Open(HKEY_CURRENT_USER, L"Software\\DIME");
    if (lr == ERROR_SUCCESS)
    {
        DWORD dw = 0;
        if (reg.QueryDWORDValue(L"DoubleSingleByte", dw) == ERROR_SUCCESS)
        {
            isFullWidth = (BOOL)dw;
        }
        if (reg.QueryDWORDValue(L"Punctuation", dw) == ERROR_SUCCESS)
        {
            isChinesePunctuation = (BOOL)dw;
        }
        DIME_DEBUG_LOG(L"_LoadSettings full=%d punct=%d", isFullWidth ? 1 : 0, isChinesePunctuation ? 1 : 0);
    }
    else
    {
        DIME_WARNING_LOG(L"_LoadSettings open reg FAILED lr=0x%08X (use defaults)", lr);
    }
}

BOOL CCompositionProcessorEngine::_GetCompartmentOnlyCommon()
{
    if (!_pThreadMgr)
    {
        return FALSE;
    }
    CCompartment compartment(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentOnlyCommon);
    BOOL isOnlyCommon = FALSE;
    compartment._GetCompartmentBOOL(isOnlyCommon);
    return isOnlyCommon;
}

BOOL CCompositionProcessorEngine::_ReadRegistryOnlyCommon()
{
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        DWORD dw = 0;
        if (reg.QueryDWORDValue(L"OnlyCommon", dw) == ERROR_SUCCESS)
        {
            return (BOOL)dw;
        }
    }
    return FALSE;
}

void CCompositionProcessorEngine::_WriteRegistryOnlyCommon(BOOL isOnlyCommon)
{
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"OnlyCommon", isOnlyCommon ? 1 : 0);
    }
}

void CCompositionProcessorEngine::_SaveSettings(BOOL isFullWidth, BOOL isChinesePunctuation)
{
    CRegKey reg;
    LONG lr = reg.Create(HKEY_CURRENT_USER, L"Software\\DIME");
    if (lr == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"DoubleSingleByte", isFullWidth ? 1 : 0);
        reg.SetDWORDValue(L"Punctuation", isChinesePunctuation ? 1 : 0);
        DIME_DEBUG_LOG(L"_SaveSettings full=%d punct=%d", isFullWidth ? 1 : 0, isChinesePunctuation ? 1 : 0);
    }
    else
    {
        DIME_WARNING_LOG(L"_SaveSettings create reg FAILED lr=0x%08X", lr);
    }
}

void CCompositionProcessorEngine::InitializeDIMECompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
	// load persisted settings from the registry; fall back to defaults
    BOOL isFullWidth = FALSE;           // default: 半角
    BOOL isChinesePunctuation = TRUE;   // default: 中文标点
    _LoadSettings(isFullWidth, isChinesePunctuation);
    DIME_DEBUG_LOG(L"InitializeDIMECompartment load full=%d punct=%d", isFullWidth ? 1 : 0, isChinesePunctuation ? 1 : 0);

	// set initial mode
    CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);

    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(isFullWidth);

    CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._SetCompartmentBOOL(isChinesePunctuation);

    // Restore "only common characters" from the persisted registry value (restart
    // store) and push it into its TSF compartment. The compartment is the live
    // source of truth during the session and survives engine-instance recreation.
    CCompartment CompartmentOnlyCommon(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentOnlyCommon);
    BOOL isOnlyCommon = _ReadRegistryOnlyCommon();
    CompartmentOnlyCommon._SetCompartmentBOOL(isOnlyCommon);
    _isOnlyCommon = isOnlyCommon;

    NotifyInputModeChanged(pThreadMgr);
}
//+---------------------------------------------------------------------------
//
// NotifyInputModeChanged
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::NotifyInputModeChanged(_In_ ITfThreadMgr *pThreadMgr)
{
    // Persist the committed input-mode settings to the registry so they
    // survive input-method switching. Only save while in Chinese mode
    // (keyboard open) to avoid persisting the temporary English-mode override.
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) && isOpen)
    {
        BOOL isFullWidth = FALSE;
        BOOL isChinesePunctuation = FALSE;
        CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
        CCompartment CompartmentPunctuation(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
        CompartmentDoubleSingleByte._GetCompartmentBOOL(isFullWidth);
        CompartmentPunctuation._GetCompartmentBOOL(isChinesePunctuation);
        _SaveSettings(isFullWidth, isChinesePunctuation);
        DIME_DEBUG_LOG(L"NotifyInputModeChanged saving full=%d punct=%d", isFullWidth ? 1 : 0, isChinesePunctuation ? 1 : 0);
    }
    else
    {
        DIME_DEBUG_LOG(L"NotifyInputModeChanged skip save (open=%d)", isOpen ? 1 : 0);
    }

    PrivateCompartmentsUpdated(pThreadMgr);

    if (_pLanguageBar_DoubleSingleByte)
    {
        _pLanguageBar_DoubleSingleByte->UpdateDisplay();
    }
    if (_pLanguageBar_Punctuation)
    {
        _pLanguageBar_Punctuation->UpdateDisplay();
    }
    if (_pTextService)
    {
        _pTextService->_RefreshCandidateInputModeStatus();
        _pTextService->_RefreshStatusWindow();
    }
}

//+---------------------------------------------------------------------------
//
// CompartmentCallback
//
//----------------------------------------------------------------------------

// static
HRESULT CCompositionProcessorEngine::CompartmentCallback(_In_ void *pv, REFGUID guidCompartment)
{
    CCompositionProcessorEngine* fakeThis = (CCompositionProcessorEngine*)pv;
    if (nullptr == fakeThis)
    {
        return E_INVALIDARG;
    }

    ITfThreadMgr* pThreadMgr = fakeThis->_pThreadMgr;
    if (pThreadMgr)
    {
        pThreadMgr->AddRef();
    }
    else if (fakeThis->_pTextService)
    {
        pThreadMgr = fakeThis->_pTextService->_GetThreadMgr();
        if (pThreadMgr)
        {
            pThreadMgr->AddRef();
        }
    }
    if (!pThreadMgr)
    {
        return E_FAIL;
    }

    if (IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentDoubleSingleByte) ||
        IsEqualGUID(guidCompartment, Global::DIMEGuidCompartmentPunctuation))
    {
        fakeThis->NotifyInputModeChanged(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION) ||
        IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE))
    {
        fakeThis->ConversionModeCompartmentUpdated(pThreadMgr);
    }
    else if (IsEqualGUID(guidCompartment, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE))
    {
        fakeThis->KeyboardOpenCompartmentUpdated(pThreadMgr);
    }

    pThreadMgr->Release();
    pThreadMgr = nullptr;

    return S_OK;
}

//+---------------------------------------------------------------------------
//
// UpdatePrivateCompartments
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    // The DIME punctuation / full-width compartments are the authoritative user
    // settings (persisted to the registry, and used directly for output in
    // KeyEventSink). We deliberately do NOT derive them from the global
    // conversion-mode compartment here: that compartment is shared across all
    // keyboard layouts and Windows resets it to a Chinese default
    // (SYMBOL / FULLSHAPE) when the input method is switched, which would
    // otherwise overwrite the user's choice on every activation. The
    // conversion-mode bits are kept in sync FROM these compartments by
    // PrivateCompartmentsUpdated() instead.

    BOOL fOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (SUCCEEDED(CompartmentKeyboardOpen._GetCompartmentBOOL(fOpen)))
    {
        if (fOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            CompartmentKeyboardOpen._SetCompartmentBOOL(FALSE);
        }
        else if (!fOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
        {
            CompartmentKeyboardOpen._SetCompartmentBOOL(TRUE);
        }
    }

    // Diagnostics: confirm punctuation / full-width are NOT overwritten by the
    // global conversion mode here.
    BOOL dbgPunct = FALSE, dbgFull = FALSE;
    CCompartment dbgPunc(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CCompartment dbgFw(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    dbgPunc._GetCompartmentBOOL(dbgPunct);
    dbgFw._GetCompartmentBOOL(dbgFull);
    DIME_DEBUG_LOG(L"ConversionModeUpdated conv=0x%08X -> punct kept=%d full kept=%d (not overwritten)", conversionMode, dbgPunct ? 1 : 0, dbgFull ? 1 : 0);
}

//+---------------------------------------------------------------------------
//
// PrivateCompartmentsUpdated()
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    if (SUCCEEDED(CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble)))
    {
        if (!isDouble && (conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode &= ~TF_CONVERSIONMODE_FULLSHAPE;
        }
        else if (isDouble && !(conversionMode & TF_CONVERSIONMODE_FULLSHAPE))
        {
            conversionMode |= TF_CONVERSIONMODE_FULLSHAPE;
        }
    }

    BOOL isPunctuation = FALSE;
    CCompartment CompartmentPunctuation(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
    if (SUCCEEDED(CompartmentPunctuation._GetCompartmentBOOL(isPunctuation)))
    {
        if (!isPunctuation && (conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            conversionMode &= ~TF_CONVERSIONMODE_SYMBOL;
        }
        else if (isPunctuation && !(conversionMode & TF_CONVERSIONMODE_SYMBOL))
        {
            conversionMode |= TF_CONVERSIONMODE_SYMBOL;
        }
    }

    if (conversionMode != conversionModePrev)
    {
        _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
    }
}

//+---------------------------------------------------------------------------
//
// SyncInputModeLayoutForKeyboardOpen
//
// 切英文: 保存当前全半角/标点, 强制半角+英文标点.
// 切回中文: 恢复切英文前的状态.
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SyncInputModeLayoutForKeyboardOpen(_In_ ITfThreadMgr *pThreadMgr, BOOL isOpen)
{
    CCompartment compartmentFullWidth(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CCompartment compartmentPunctuation(pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);

    if (!isOpen)
    {
        if (!_imeModeSnapshotValid)
        {
            BOOL isFullWidth = FALSE;
            BOOL isChinesePunctuation = FALSE;
            compartmentFullWidth._GetCompartmentBOOL(isFullWidth);
            compartmentPunctuation._GetCompartmentBOOL(isChinesePunctuation);
            _imeModeSnapshotFullWidth = isFullWidth;
            _imeModeSnapshotChinesePunctuation = isChinesePunctuation;
            _imeModeSnapshotValid = TRUE;
        }

        compartmentFullWidth._SetCompartmentBOOL(FALSE);
        compartmentPunctuation._SetCompartmentBOOL(FALSE);
    }
    else if (_imeModeSnapshotValid)
    {
        compartmentFullWidth._SetCompartmentBOOL(_imeModeSnapshotFullWidth);
        compartmentPunctuation._SetCompartmentBOOL(_imeModeSnapshotChinesePunctuation);
        _imeModeSnapshotValid = FALSE;
    }
}

//+---------------------------------------------------------------------------
//
// KeyboardOpenCompartmentUpdated
//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr)
{
    if (!_pCompartmentConversion)
    {
        return;
    }

    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, _tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)))
    {
        return;
    }

    SyncInputModeLayoutForKeyboardOpen(pThreadMgr, isOpen);

    DWORD conversionMode = 0;
    DWORD conversionModePrev = 0;
    if (FAILED(_pCompartmentConversion->_GetCompartmentDWORD(conversionMode)))
    {
        return;
    }

    conversionModePrev = conversionMode;

    if (isOpen && !(conversionMode & TF_CONVERSIONMODE_NATIVE))
    {
        conversionMode |= TF_CONVERSIONMODE_NATIVE;
    }
    else if (!isOpen && (conversionMode & TF_CONVERSIONMODE_NATIVE))
    {
        conversionMode &= ~TF_CONVERSIONMODE_NATIVE;
    }

    if (conversionMode != conversionModePrev)
    {
        _pCompartmentConversion->_SetCompartmentDWORD(conversionMode);
    }

    PrivateCompartmentsUpdated(pThreadMgr);

    if (_pLanguageBar_DoubleSingleByte)
    {
        _pLanguageBar_DoubleSingleByte->UpdateDisplay();
    }
    if (_pLanguageBar_Punctuation)
    {
        _pLanguageBar_Punctuation->UpdateDisplay();
    }
    if (_pTextService)
    {
        _pTextService->_RefreshCandidateInputModeStatus();
        _pTextService->_RefreshStatusWindow();
    }
}


//////////////////////////////////////////////////////////////////////
//
// XPreservedKey implementation.
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// UninitPreservedKey
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::XPreservedKey::UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr)
{
    ITfKeystrokeMgr* pKeystrokeMgr = nullptr;

    if (IsEqualGUID(Guid, GUID_NULL))
    {
        return FALSE;
    }

    if (FAILED(pThreadMgr->QueryInterface(IID_ITfKeystrokeMgr, (void **)&pKeystrokeMgr)))
    {
        return FALSE;
    }

    for (UINT i = 0; i < TSFPreservedKeyTable.Count(); i++)
    {
        TF_PRESERVEDKEY pPreservedKey = *TSFPreservedKeyTable.GetAt(i);
        pPreservedKey.uModifiers &= 0xffff;

        pKeystrokeMgr->UnpreserveKey(Guid, &pPreservedKey);
    }

    pKeystrokeMgr->Release();

    return TRUE;
}

CCompositionProcessorEngine::XPreservedKey::XPreservedKey()
{
    Guid = GUID_NULL;
    Description = nullptr;
}

CCompositionProcessorEngine::XPreservedKey::~XPreservedKey()
{
    ITfThreadMgr* pThreadMgr = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
    if (SUCCEEDED(hr))
    {
        UninitPreservedKey(pThreadMgr);
        pThreadMgr->Release();
        pThreadMgr = nullptr;
    }

    if (Description)
    {
        delete [] Description;
    }
}
//+---------------------------------------------------------------------------
//
// CDIME::CreateInstance 
//
//----------------------------------------------------------------------------

HRESULT CDIME::CreateInstance(REFCLSID rclsid, REFIID riid, _Outptr_result_maybenull_ LPVOID* ppv, _Out_opt_ HINSTANCE* phInst, BOOL isComLessMode)
{
    HRESULT hr = S_OK;
    if (phInst == nullptr)
    {
        return E_INVALIDARG;
    }

    *phInst = nullptr;

    if (!isComLessMode)
    {
        hr = ::CoCreateInstance(rclsid, 
            NULL, 
            CLSCTX_INPROC_SERVER,
            riid,
            ppv);
    }
    else
    {
        hr = CDIME::ComLessCreateInstance(rclsid, riid, ppv, phInst);
    }

    return hr;
}

//+---------------------------------------------------------------------------
//
// CDIME::ComLessCreateInstance
//
//----------------------------------------------------------------------------

HRESULT CDIME::ComLessCreateInstance(REFGUID rclsid, REFIID riid, _Outptr_result_maybenull_ void **ppv, _Out_opt_ HINSTANCE *phInst)
{
    HRESULT hr = S_OK;
    HINSTANCE dimeDllHandle = nullptr;
    WCHAR wchPath[MAX_PATH] = {'\0'};
    WCHAR szExpandedPath[MAX_PATH] = {'\0'};
    DWORD dwCnt = 0;
    *ppv = nullptr;

    hr = phInst ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        *phInst = nullptr;
        hr = CDIME::GetComModuleName(rclsid, wchPath, ARRAYSIZE(wchPath));
        if (SUCCEEDED(hr))
        {
            dwCnt = ExpandEnvironmentStringsW(wchPath, szExpandedPath, ARRAYSIZE(szExpandedPath));
            hr = (0 < dwCnt && dwCnt <= ARRAYSIZE(szExpandedPath)) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                dimeDllHandle = LoadLibraryEx(szExpandedPath, NULL, 0);
                hr = dimeDllHandle ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    *phInst = dimeDllHandle;
                    FARPROC pfn = GetProcAddress(dimeDllHandle, "DllGetClassObject");
                    hr = pfn ? S_OK : E_FAIL;
                    if (SUCCEEDED(hr))
                    {
                        IClassFactory *pClassFactory = nullptr;
                        hr = ((HRESULT (STDAPICALLTYPE *)(REFCLSID rclsid, REFIID riid, LPVOID *ppv))(pfn))(rclsid, IID_IClassFactory, (void **)&pClassFactory);
                        if (SUCCEEDED(hr) && pClassFactory)
                        {
                            hr = pClassFactory->CreateInstance(NULL, riid, ppv);
                            pClassFactory->Release();
                        }
                    }
                }
            }
        }
    }

    if (!SUCCEEDED(hr) && phInst && *phInst)
    {
        FreeLibrary(*phInst);
        *phInst = 0;
    }
    return hr;
}

//+---------------------------------------------------------------------------
//
// CDIME::GetComModuleName
//
//----------------------------------------------------------------------------

HRESULT CDIME::GetComModuleName(REFGUID rclsid, _Out_writes_(cchPath)WCHAR* wchPath, DWORD cchPath)
{
    HRESULT hr = S_OK;

    CRegKey key;
    WCHAR wchClsid[CLSID_STRLEN + 1];
    hr = CLSIDToString(rclsid, wchClsid) ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        WCHAR wchKey[MAX_PATH];
        hr = StringCchPrintfW(wchKey, ARRAYSIZE(wchKey), L"CLSID\\%s\\InProcServer32", wchClsid);
        if (SUCCEEDED(hr))
        {
            hr = (key.Open(HKEY_CLASSES_ROOT, wchKey, KEY_READ) == ERROR_SUCCESS) ? S_OK : E_FAIL;
            if (SUCCEEDED(hr))
            {
                WCHAR wszModel[MAX_PATH];
                ULONG cch = ARRAYSIZE(wszModel);
                hr = (key.QueryStringValue(L"ThreadingModel", wszModel, &cch) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                if (SUCCEEDED(hr))
                {
                    if (CompareStringOrdinal(wszModel, 
                        -1, 
                        L"Apartment", 
                        -1,
                        TRUE) == CSTR_EQUAL)
                    {
                        hr = (key.QueryStringValue(NULL, wchPath, &cchPath) == ERROR_SUCCESS) ? S_OK : E_FAIL;
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
            }
        }
    }

    return hr;
}

void CCompositionProcessorEngine::InitKeyStrokeTable()
{
    for (int i = 0; i < 26; i++)
    {
        _keystrokeTable[i].VirtualKey = 'A' + i;
        _keystrokeTable[i].Modifiers = 0;
        _keystrokeTable[i].Function = FUNCTION_INPUT;
    }
}

void CCompositionProcessorEngine::ShowAllLanguageBarIcons()
{
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, FALSE);
}

void CCompositionProcessorEngine::HideAllLanguageBarIcons()
{
    SetLanguageBarStatus(TF_LBI_STATUS_HIDDEN, TRUE);
}

void CCompositionProcessorEngine::SetInitialCandidateListRange()
{
    for (DWORD i = 1; i <= 10; i++)
    {
        DWORD* pNewIndexRange = nullptr;

        pNewIndexRange = _candidateListIndexRange.Append();
        if (pNewIndexRange != nullptr)
        {
            if (i != 10)
            {
                *pNewIndexRange = i;
            }
            else
            {
                *pNewIndexRange = 0;
            }
        }
    }
}

void CCompositionProcessorEngine::SetDefaultCandidateTextFont()
{
    // Candidate Text Font
    if (Global::defaultlFontHandle == nullptr)
    {
		WCHAR fontName[50] = {'\0'}; 
		LoadString(Global::dllInstanceHandle, IDS_DEFAULT_FONT, fontName, 50);
        Global::defaultlFontHandle = CreateFont(-MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72), 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, fontName);
        if (!Global::defaultlFontHandle)
        {
			LOGFONT lf;
			SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
            // Fall back to the default GUI font on failure.
            Global::defaultlFontHandle = CreateFont(-MulDiv(10, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72), 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, lf.lfFaceName);
        }
    }
}

//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////

//+---------------------------------------------------------------------------
//
// _TryAssignCandidatePageKeyForMode
//
// 选字翻页: '-' 上一页, '=' 下一页.
//----------------------------------------------------------------------------

static BOOL _TryAssignCandidatePageKeyForMode(UINT uCode, WCHAR wch, BOOL fComposing, CANDIDATE_MODE candidateMode, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    KEYSTROKE_CATEGORY category = CATEGORY_NONE;

    if (candidateMode == CANDIDATE_INCREMENTAL && fComposing)
    {
        category = CATEGORY_CANDIDATE;
    }
    else if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        category = CATEGORY_CANDIDATE;
    }
    else if (candidateMode == CANDIDATE_PHRASE)
    {
        category = CATEGORY_PHRASE;
    }
    else if (fComposing && candidateMode != CANDIDATE_INCREMENTAL)
    {
        category = CATEGORY_COMPOSING;
    }

    if (category == CATEGORY_NONE)
    {
        return FALSE;
    }

    if (uCode == VK_OEM_MINUS || wch == L'-')
    {
        if (pKeyState)
        {
            pKeyState->Category = category;
            pKeyState->Function = FUNCTION_MOVE_PAGE_UP;
        }
        return TRUE;
    }

    if (uCode == VK_OEM_PLUS || wch == L'=')
    {
        if (pKeyState)
        {
            pKeyState->Category = category;
            pKeyState->Function = FUNCTION_MOVE_PAGE_DOWN;
        }
        return TRUE;
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    //
    // Enter temporary English mode with ';' on an empty buffer.
    //
    if (!_isEnglishInput && _keystrokeBuffer.GetLength() == 0 &&
        uCode == VK_OEM_1 && (fComposing || candidateMode == CANDIDATE_NONE))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_INPUT;
        }
        return TRUE;
    }

    //
    // Enter temporary English mode by typing a Shift+capital letter on an empty
    // buffer. A plain (CapsLock) capital must NOT trigger this, so require the
    // Shift modifier to be held. A Shift+letter is not a wubi code key, so we
    // eat it here and route it to the composing handler, which turns on English
    // mode and seeds the buffer with the typed capital.
    //
    if (!_isEnglishInput && _keystrokeBuffer.GetLength() == 0 &&
        pwch && *pwch >= L'A' && *pwch <= L'Z' &&
        (GetKeyState(VK_SHIFT) & 0x8000) &&
        (fComposing || candidateMode == CANDIDATE_NONE))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_INPUT;
        }
        return TRUE;
    }

    //
    // While in temporary English mode, eat the keys we handle and route them
    // to the composing handler. Everything typed is collected literally.
    //
    if (_isEnglishInput)
    {
        switch (uCode)
        {
        case VK_RETURN:
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE; }
            return TRUE;
        case VK_BACK:
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; }
            return TRUE;
        case VK_ESCAPE:
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; }
            return TRUE;
        case VK_SPACE:
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_INPUT; }
            return TRUE;
        default:
            {
                WCHAR wch = (pwch ? *pwch : L'\0');
                // Accept printable characters: letters, digits, punctuation, ';' and any
                // character above the ASCII range (e.g. full-width symbols).
                if (wch && (iswalnum(wch) || iswpunct(wch) || wch > 0x80))
                {
                    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_INPUT; }
                    return TRUE;
                }
                return FALSE;
            }
        }
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        fComposing = FALSE;
    }

    if (fComposing || candidateMode == CANDIDATE_INCREMENTAL || candidateMode == CANDIDATE_NONE)
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
        {
            return TRUE;
        }
        else if ((IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst()) ||
            (IsWildcard() && IsWildcardChar(*pwch) &&  IsDisableWildcardAtFirst() && _keystrokeBuffer.GetLength()))
        {
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = FUNCTION_INPUT;
            }
            return TRUE;
        }
        else if (_hasWildcardIncludedInKeystrokeBuffer && uCode == VK_SPACE)
        {
            if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT_WILDCARD; } return TRUE;
        }
    }

    if (candidateMode == CANDIDATE_ORIGINAL || candidateMode == CANDIDATE_PHRASE || candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
    {
        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }

        if (hasCandidateWithWildcard)
        {
            if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidateWildcard))
            {
                return isRetCode;
            }
        }

        // Candidate list could not handle key. We can try to restart the composition.
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            if (candidateMode != CANDIDATE_ORIGINAL)
            {
                return TRUE;
            }
            else
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT; } 
                return TRUE;
            }
        }
    } 

    // CANDIDATE_INCREMENTAL should process Keystroke.Candidate virtual keys.
    else if (candidateMode == CANDIDATE_INCREMENTAL)
    {
        BOOL isRetCode = TRUE;
        if (IsVirtualKeyKeystrokeCandidate(uCode, pKeyState, candidateMode, &isRetCode, &_KeystrokeCandidate))
        {
            return isRetCode;
        }
    }

    if (!fComposing && candidateMode != CANDIDATE_ORIGINAL && candidateMode != CANDIDATE_PHRASE && candidateMode != CANDIDATE_WITH_NEXT_COMPOSITION) 
    {
        if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
        {
            return TRUE;
        }
    }

    WCHAR wch = (pwch ? *pwch : L'\0');
    if (_TryAssignCandidatePageKeyForMode(uCode, wch, fComposing, candidateMode, pKeyState))
    {
        return TRUE;
    }

    // System pre-defined keystroke
    if (fComposing)
    {
        if ((candidateMode != CANDIDATE_INCREMENTAL))
        {
            switch (uCode)
            {
            case VK_LEFT:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_LEFT; } return TRUE;
            case VK_RIGHT:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_MOVE_RIGHT; } return TRUE;
            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
            }
        }
        else if ((candidateMode == CANDIDATE_INCREMENTAL))
        {
            switch (uCode)
            {
                // VK_LEFT, VK_RIGHT - set *pIsEaten = FALSE for application could move caret left or right.
                // and for CUAS, invoke _HandleCompositionCancel() edit session due to ignore CUAS default key handler for send out terminate composition
            case VK_LEFT:
            case VK_RIGHT:
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                }
                return FALSE;

            case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
            case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

                // VK_BACK - remove one char from reading string.
            case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_BACKSPACE; } return TRUE;

            case VK_SPACE:
                {
                    if (candidateMode == CANDIDATE_INCREMENTAL)
                    {
                        if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
                    }
                    else
                    {
                        if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
                    }
                }
            }
        }
    }

    if ((candidateMode == CANDIDATE_ORIGINAL) || (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION))
    {
        switch (uCode)
        {
        case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
        case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;

        case VK_ESCAPE:
            {
                if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
                        pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
                    }
                    return TRUE;
                }
                else
                {
                    if (pKeyState)
                    {
                        pKeyState->Category = CATEGORY_CANDIDATE;
                        pKeyState->Function = FUNCTION_CANCEL;
                    }
                    return TRUE;
                }
            }
        }

        if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            if (IsVirtualKeyKeystrokeComposition(uCode, NULL, FUNCTION_NONE))
            {
                if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT; } return TRUE;
            }
        }
    }

    if (candidateMode == CANDIDATE_PHRASE)
    {
        switch (uCode)
        {
        case VK_RETURN: if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_FINALIZE_CANDIDATELIST; } return TRUE;
        case VK_SPACE:  if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_CONVERT; } return TRUE;
        case VK_ESCAPE: if (pKeyState) { pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
        case VK_BACK:   if (pKeyState) { pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_CANCEL; } return TRUE;
        }
    }

    if (IsKeystrokeRange(uCode, pKeyState, candidateMode))
    {
        return TRUE;
    }
    else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
    {
        return FALSE;
    }

    if (*pwch && !IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_NONE))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION;
            pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE;
        }
        return FALSE;
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    for (UINT i = 0; i < _KeystrokeComposition.Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = _KeystrokeComposition.GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            if (function == FUNCTION_NONE)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
            else if (function == pKeystroke->Function)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CDIMEArray<_KEYSTROKE> *pKeystrokeMetric)
{
    if (pfRetCode == nullptr)
    {
        return FALSE;
    }
    *pfRetCode = FALSE;

    for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = pKeystrokeMetric->GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            *pfRetCode = TRUE;
            if (pKeyState)
            {
                pKeyState->Category = (candidateMode == CANDIDATE_ORIGINAL ? CATEGORY_CANDIDATE :
                    candidateMode == CANDIDATE_PHRASE ? CATEGORY_PHRASE : CATEGORY_CANDIDATE);

                pKeyState->Function = pKeystroke->Function;
            }
            return TRUE;
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    if (_candidateListIndexRange.IsRange(uCode))
    {
        if (candidateMode == CANDIDATE_PHRASE)
        {
            // Candidate phrase could specify modifier
            if ((GetCandidateListPhraseModifier() == 0 && Global::ModifiersValue == 0) ||
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_PHRASE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            else
            {
                pKeyState->Category = CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION; pKeyState->Function = FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT;
                return FALSE;
            }
        }
        else if (candidateMode == CANDIDATE_WITH_NEXT_COMPOSITION)
        {
            // Candidate phrase could specify modifier
            if ((GetCandidateListPhraseModifier() == 0 && Global::ModifiersValue == 0) ||
                (GetCandidateListPhraseModifier() != 0 && Global::CheckModifiers(Global::ModifiersValue, GetCandidateListPhraseModifier())))
            {
                pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
                return TRUE;
            }
            // else next composition
        }
        else if (candidateMode != CANDIDATE_NONE)
        {
            pKeyState->Category = CATEGORY_CANDIDATE; pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
            return TRUE;
        }
    }
    return FALSE;
}