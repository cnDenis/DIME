// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


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
#include "StatusWindow.h"


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
    _mainDictionaryName[0] = L'\0';
    StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), TEXTSERVICE_DIC_STEM);
    _isPinyinInput = FALSE;
    _isEnglishInput = FALSE;
    _isOnlyCommon = FALSE;
    _emptyCodeSearchFull = TRUE;
    _englishCommaPeriodAfterDigit = TRUE;
    _lastKeyWasDigit = FALSE;
    _hotkeyOnlyCommonEnabled = TRUE;
    _hotkeyPunctuationEnabled = TRUE;
    _hotkeyDoubleSingleByteEnabled = TRUE;

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
    _candidatePageSize = 10;
    _candidateFontSize = 0;
    _settingsVersion = 0;
    _dictionaryVersion = 0;
    _cmdStorageUsed = 0;

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
    // Unregister preserved keys on the ThreadMgr used at InitPreservedKey.
    // XPreservedKey dtor must not CoCreate a different ThreadMgr.
    if (_pThreadMgr)
    {
        _PreservedKey_IMEMode.UninitPreservedKey(_pThreadMgr);
        _PreservedKey_DoubleSingleByte.UninitPreservedKey(_pThreadMgr);
        _PreservedKey_Punctuation.UninitPreservedKey(_pThreadMgr);
        _PreservedKey_OnlyCommon.UninitPreservedKey(_pThreadMgr);
    }

    if (_pTableDictionaryEngine)
    {
        delete _pTableDictionaryEngine;
        _pTableDictionaryEngine = nullptr;
    }

    if (_pLanguageBar_IMEMode)
    {
        _pLanguageBar_IMEMode->CleanUp(_pThreadMgr);
        _pLanguageBar_IMEMode->Release();
        _pLanguageBar_IMEMode = nullptr;
    }
    if (_pLanguageBar_DoubleSingleByte)
    {
        _pLanguageBar_DoubleSingleByte->CleanUp(_pThreadMgr);
        _pLanguageBar_DoubleSingleByte->Release();
        _pLanguageBar_DoubleSingleByte = nullptr;
    }
    if (_pLanguageBar_Punctuation)
    {
        _pLanguageBar_Punctuation->CleanUp(_pThreadMgr);
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
    AcknowledgeSettingsVersion();
    AcknowledgeDictionaryVersion();
    {
        WCHAR dictDir[MAX_PATH] = {L'\0'};
        if (ResolveDictionaryDirectory(dictDir, ARRAYSIZE(dictDir)))
        {
            TryCleanupStaleBinOldFiles(dictDir);
        }
    }

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

    _ExpandCmdCandidates(pCandidateList);

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

BOOL CCompositionProcessorEngine::IsCmdCandidate(_In_ const CStringRange *pItem, _In_z_ LPCWSTR cmdLiteral)
{
    if (!pItem || !cmdLiteral || !pItem->Get())
    {
        return FALSE;
    }
    const size_t litLen = wcslen(cmdLiteral);
    if (pItem->GetLength() != litLen)
    {
        return FALSE;
    }
    return wcsncmp(pItem->Get(), cmdLiteral, litLen) == 0;
}

BOOL CCompositionProcessorEngine::IsCmdEngCandidate(_In_ const CStringRange *pItem)
{
    return IsCmdCandidate(pItem, DIME_CMD_ENG);
}

BOOL CCompositionProcessorEngine::_StoreCmdCandidateString(_In_z_ LPCWSTR text, _Out_ CStringRange *pOut)
{
    if (!text || !pOut || _cmdStorageUsed >= _kCmdStorageMax)
    {
        return FALSE;
    }
    WCHAR* slot = _cmdStorage[_cmdStorageUsed];
    if (FAILED(StringCchCopyW(slot, _kCmdStorageCch, text)))
    {
        return FALSE;
    }
    pOut->Set(slot, wcslen(slot));
    _cmdStorageUsed++;
    return TRUE;
}

BOOL CCompositionProcessorEngine::_AppendStoredCandidate(_Inout_ CDIMEArray<CCandidateListItem> *pOut, _In_z_ LPCWSTR text)
{
    if (!pOut || !text)
    {
        return FALSE;
    }
    CCandidateListItem* pLI = pOut->Append();
    if (!pLI || !_StoreCmdCandidateString(text, &pLI->_ItemString))
    {
        return FALSE;
    }
    CStringRange emptyKey;
    emptyKey.Set(L"", 0);
    pLI->_FindKeyCode.Set(emptyKey);
    return TRUE;
}

namespace
{
// 年份用 〇; 时分秒读数里的 0 用 零.
static const WCHAR kCnYearDigit[] = L"〇一二三四五六七八九";
static const WCHAR kCnCardinal[]  = L"零一二三四五六七八九";

// 追加一位年份数字 (0-9 → 〇一二…).
static void AppendYearDigit(_Inout_updates_(cch) WCHAR* buf, size_t cch, _Inout_ size_t& used, UINT d)
{
    if (d > 9 || used + 1 >= cch)
    {
        return;
    }
    buf[used++] = kCnYearDigit[d];
    buf[used] = L'\0';
}

// 追加 0-99 的中文读数 (零/一/…/十/十一/…/二十/二十五/…).
static void AppendChineseCardinal(_Inout_updates_(cch) WCHAR* buf, size_t cch, _Inout_ size_t& used, UINT n)
{
    if (n > 99 || used + 4 >= cch)
    {
        return;
    }
    if (n < 10)
    {
        buf[used++] = kCnCardinal[n];
        buf[used] = L'\0';
        return;
    }
    if (n < 20)
    {
        buf[used++] = L'十';
        if (n > 10)
        {
            buf[used++] = kCnCardinal[n - 10];
        }
        buf[used] = L'\0';
        return;
    }
    buf[used++] = kCnCardinal[n / 10];
    buf[used++] = L'十';
    if (n % 10 != 0)
    {
        buf[used++] = kCnCardinal[n % 10];
    }
    buf[used] = L'\0';
}

static void AppendLiteral(_Inout_updates_(cch) WCHAR* buf, size_t cch, _Inout_ size_t& used, _In_z_ LPCWSTR lit)
{
    for (; *lit && used + 1 < cch; ++lit)
    {
        buf[used++] = *lit;
    }
    buf[used] = L'\0';
}

// 二零二六年七月二十五日 (年逐位 〇, 月日读数).
static void FormatChineseDate(_Out_writes_(cch) WCHAR* buf, size_t cch, const SYSTEMTIME& st)
{
    size_t used = 0;
    buf[0] = L'\0';
    UINT y = st.wYear;
    AppendYearDigit(buf, cch, used, (y / 1000) % 10);
    AppendYearDigit(buf, cch, used, (y / 100) % 10);
    AppendYearDigit(buf, cch, used, (y / 10) % 10);
    AppendYearDigit(buf, cch, used, y % 10);
    AppendLiteral(buf, cch, used, L"年");
    AppendChineseCardinal(buf, cch, used, st.wMonth);
    AppendLiteral(buf, cch, used, L"月");
    AppendChineseCardinal(buf, cch, used, st.wDay);
    AppendLiteral(buf, cch, used, L"日");
}

// 十一时零六分三十秒.
static void FormatChineseTime(_Out_writes_(cch) WCHAR* buf, size_t cch, const SYSTEMTIME& st, BOOL withSeconds)
{
    size_t used = 0;
    buf[0] = L'\0';
    AppendChineseCardinal(buf, cch, used, st.wHour);
    AppendLiteral(buf, cch, used, L"时");
    AppendChineseCardinal(buf, cch, used, st.wMinute);
    AppendLiteral(buf, cch, used, L"分");
    if (withSeconds)
    {
        AppendChineseCardinal(buf, cch, used, st.wSecond);
        AppendLiteral(buf, cch, used, L"秒");
    }
}

static void FormatChineseDateTime(_Out_writes_(cch) WCHAR* buf, size_t cch, const SYSTEMTIME& st, BOOL withSeconds)
{
    FormatChineseDate(buf, cch, st);
    size_t used = wcslen(buf);
    AppendLiteral(buf, cch, used, L" ");
    WCHAR timeBuf[64] = {L'\0'};
    FormatChineseTime(timeBuf, ARRAYSIZE(timeBuf), st, withSeconds);
    AppendLiteral(buf, cch, used, timeBuf);
}
} // namespace

void CCompositionProcessorEngine::_AppendCmdDateCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut, const SYSTEMTIME& st)
{
    WCHAR buf[_kCmdStorageCch] = {L'\0'};
    const UINT yy = static_cast<UINT>(st.wYear % 100);
    // 顺序: YYYY 阿拉伯 → 中文读数 → YY 阿拉伯; 一律 YMD.
    const WCHAR* yyyyFormats[] = {
        L"%04u年%02u月%02u日",
        L"%04u-%02u-%02u",
        L"%04u/%02u/%02u",
        L"%04u.%02u.%02u",
        L"%04u%02u%02u",
    };
    for (int i = 0; i < ARRAYSIZE(yyyyFormats); i++)
    {
        StringCchPrintfW(buf, ARRAYSIZE(buf), yyyyFormats[i], st.wYear, st.wMonth, st.wDay);
        _AppendStoredCandidate(pOut, buf);
    }

    FormatChineseDate(buf, ARRAYSIZE(buf), st);
    _AppendStoredCandidate(pOut, buf);

    const WCHAR* yyFormats[] = {
        L"%02u年%02u月%02u日",
        L"%02u-%02u-%02u",
        L"%02u/%02u/%02u",
        L"%02u.%02u.%02u",
        L"%02u%02u%02u",
    };
    for (int i = 0; i < ARRAYSIZE(yyFormats); i++)
    {
        StringCchPrintfW(buf, ARRAYSIZE(buf), yyFormats[i], yy, st.wMonth, st.wDay);
        _AppendStoredCandidate(pOut, buf);
    }
}

void CCompositionProcessorEngine::_AppendCmdTimeCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut, const SYSTEMTIME& st)
{
    WCHAR buf[_kCmdStorageCch] = {L'\0'};
    struct { const WCHAR* fmt; BOOL withSec; } formats[] = {
        { L"%02u时%02u分%02u秒", TRUE },
        { L"%02u:%02u:%02u", TRUE },
        { L"%02u时%02u分", FALSE },
        { L"%02u:%02u", FALSE },
    };
    for (int i = 0; i < ARRAYSIZE(formats); i++)
    {
        if (formats[i].withSec)
        {
            StringCchPrintfW(buf, ARRAYSIZE(buf), formats[i].fmt, st.wHour, st.wMinute, st.wSecond);
        }
        else
        {
            StringCchPrintfW(buf, ARRAYSIZE(buf), formats[i].fmt, st.wHour, st.wMinute);
        }
        _AppendStoredCandidate(pOut, buf);
    }

    FormatChineseTime(buf, ARRAYSIZE(buf), st, TRUE);
    _AppendStoredCandidate(pOut, buf);
    FormatChineseTime(buf, ARRAYSIZE(buf), st, FALSE);
    _AppendStoredCandidate(pOut, buf);
}

void CCompositionProcessorEngine::_AppendCmdNowCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut, const SYSTEMTIME& st)
{
    WCHAR buf[_kCmdStorageCch] = {L'\0'};
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%04u年%02u月%02u日 %02u时%02u分%02u秒",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    _AppendStoredCandidate(pOut, buf);

    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    _AppendStoredCandidate(pOut, buf);

    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%04u年%02u月%02u日%02u时%02u分",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    _AppendStoredCandidate(pOut, buf);

    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%04u/%02u/%02u %02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    _AppendStoredCandidate(pOut, buf);

    // Unix 时间戳: 秒 / 毫秒 (UTC 纪元; 与上面本地显示为同一时刻).
    {
        FILETIME ft = {};
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli = {};
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        // FILETIME: 100ns since 1601-01-01; Unix epoch 1970-01-01 差 116444736000000000.
        const ULONGLONG kEpochDiff = 116444736000000000ull;
        if (uli.QuadPart >= kEpochDiff)
        {
            const ULONGLONG unix100ns = uli.QuadPart - kEpochDiff;
            const ULONGLONG unixSec = unix100ns / 10000000ull;
            const ULONGLONG unixMs = unix100ns / 10000ull;
            StringCchPrintfW(buf, ARRAYSIZE(buf), L"%llu", unixSec);
            _AppendStoredCandidate(pOut, buf);
            StringCchPrintfW(buf, ARRAYSIZE(buf), L"%llu", unixMs);
            _AppendStoredCandidate(pOut, buf);
        }
    }

    FormatChineseDateTime(buf, ARRAYSIZE(buf), st, TRUE);
    _AppendStoredCandidate(pOut, buf);
    FormatChineseDateTime(buf, ARRAYSIZE(buf), st, FALSE);
    _AppendStoredCandidate(pOut, buf);
}

void CCompositionProcessorEngine::_AppendCmdWeekCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut, const SYSTEMTIME& st)
{
    // SYSTEMTIME.wDayOfWeek: 0=Sunday .. 6=Saturday
    static const WCHAR* kXingqi[] = {
        L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"
    };
    static const WCHAR* kZhou[] = {
        L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六"
    };
    static const WCHAR* kLibai[] = {
        L"礼拜日", L"礼拜一", L"礼拜二", L"礼拜三", L"礼拜四", L"礼拜五", L"礼拜六"
    };
    static const WCHAR* kFullEn[] = {
        L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"
    };
    static const WCHAR* kShortEn[] = {
        L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"
    };

    const WORD dow = st.wDayOfWeek;
    if (dow > 6)
    {
        return;
    }

    _AppendStoredCandidate(pOut, kXingqi[dow]);
    _AppendStoredCandidate(pOut, kZhou[dow]);
    _AppendStoredCandidate(pOut, kLibai[dow]);
    if (dow == 0)
    {
        // 口语里周日也常说「星期天」
        _AppendStoredCandidate(pOut, L"星期天");
    }
    _AppendStoredCandidate(pOut, kFullEn[dow]);
    _AppendStoredCandidate(pOut, kShortEn[dow]);
}

void CCompositionProcessorEngine::_AppendCmdUuidCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut)
{
    GUID guid = {};
    if (FAILED(CoCreateGuid(&guid)))
    {
        return;
    }

    // StringFromGUID2: "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" (大写); 只用中间段.
    WCHAR bracedUpper[64] = {L'\0'};
    if (StringFromGUID2(guid, bracedUpper, ARRAYSIZE(bracedUpper)) <= 0)
    {
        return;
    }

    const size_t bracedLen = wcslen(bracedUpper);
    if (bracedLen < 3)
    {
        return;
    }

    WCHAR upperHyphen[40] = {L'\0'};
    if (FAILED(StringCchCopyN(upperHyphen, ARRAYSIZE(upperHyphen), bracedUpper + 1, bracedLen - 2)))
    {
        return;
    }

    WCHAR lowerHyphen[40] = {L'\0'};
    WCHAR upperBare[40] = {L'\0'};
    WCHAR lowerBare[40] = {L'\0'};
    size_t barePos = 0;
    for (size_t i = 0; upperHyphen[i] != L'\0'; i++)
    {
        const WCHAR ch = upperHyphen[i];
        const WCHAR lower = (ch >= L'A' && ch <= L'Z')
            ? static_cast<WCHAR>(ch - L'A' + L'a')
            : ch;
        lowerHyphen[i] = lower;
        if (ch != L'-' && barePos + 1 < ARRAYSIZE(upperBare))
        {
            upperBare[barePos] = ch;
            lowerBare[barePos] = lower;
            barePos++;
        }
    }

    // 小写/大写带连字符, 再无连字符; 不含花括号.
    _AppendStoredCandidate(pOut, lowerHyphen);
    _AppendStoredCandidate(pOut, upperHyphen);
    _AppendStoredCandidate(pOut, lowerBare);
    _AppendStoredCandidate(pOut, upperBare);
}

void CCompositionProcessorEngine::_AppendCmdRandCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut)
{
    // 3~12 位随机整数: 每位宽取 [10^(n-1), 10^n - 1], 无前导零.
    ULONGLONG pow10 = 100ull; // 10^(3-1)
    for (int digits = 3; digits <= 12; digits++)
    {
        const ULONGLONG minV = pow10;
        const ULONGLONG maxV = pow10 * 10ull - 1ull;
        const ULONGLONG span = maxV - minV + 1ull;

        GUID guid = {};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return;
        }
        // 取 GUID 前 8 字节作熵; IME 展示用途, 模偏差可忽略.
        ULONGLONG entropy = 0;
        memcpy(&entropy, &guid, sizeof(entropy));
        const ULONGLONG value = minV + (entropy % span);

        WCHAR buf[24] = {L'\0'};
        if (SUCCEEDED(StringCchPrintfW(buf, ARRAYSIZE(buf), L"%llu", value)))
        {
            _AppendStoredCandidate(pOut, buf);
        }

        if (digits < 12)
        {
            pow10 *= 10ull;
        }
    }
}

void CCompositionProcessorEngine::_AppendCmdAsdfCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut)
{
    _AppendCmdLetterCandidates(pOut, L'a');
}

void CCompositionProcessorEngine::_AppendCmdQwerCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut)
{
    // 大小写+数字+符号 / 大小写+数字 / 大小写 / 纯大写; 各出 16 位与 8 位 (长的在前).
    static const WCHAR kUpper[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const WCHAR kMixed[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static const WCHAR kAlnum[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    // 常见密码符号, 避开引号/反斜杠/空格以免粘贴到脚本时难用.
    static const WCHAR kSymbol[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
        L"!@#$%^&*-_=+?";

    static const struct
    {
        const WCHAR* alphabet;
        size_t alphabetLen;
    } kSets[] = {
        { kSymbol, ARRAYSIZE(kSymbol) - 1 },
        { kAlnum,  ARRAYSIZE(kAlnum) - 1 },
        { kMixed,  ARRAYSIZE(kMixed) - 1 },
        { kUpper,  ARRAYSIZE(kUpper) - 1 },
    };
    static const int kLens[] = { 16, 8 };

    for (int li = 0; li < ARRAYSIZE(kLens); li++)
    {
        const int len = kLens[li];
        for (int si = 0; si < ARRAYSIZE(kSets); si++)
        {
            GUID guid = {};
            if (FAILED(CoCreateGuid(&guid)))
            {
                return;
            }

            const BYTE* entropy = reinterpret_cast<const BYTE*>(&guid);
            const WCHAR* alphabet = kSets[si].alphabet;
            const size_t alphabetLen = kSets[si].alphabetLen;
            WCHAR buf[17] = {L'\0'};
            for (int i = 0; i < len; i++)
            {
                buf[i] = alphabet[entropy[i] % alphabetLen];
            }
            _AppendStoredCandidate(pOut, buf);
        }
    }
}

void CCompositionProcessorEngine::_AppendCmdLetterCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pOut, WCHAR baseLetter)
{
    // ASDF: 16~4 位连续小写 a-z; 倒排, 长的在前.
    for (int len = 16; len >= 4; len--)
    {
        GUID guid = {};
        if (FAILED(CoCreateGuid(&guid)))
        {
            return;
        }

        // GUID 16 字节刚好覆盖最长 16 位.
        const BYTE* entropy = reinterpret_cast<const BYTE*>(&guid);
        WCHAR buf[17] = {L'\0'};
        for (int i = 0; i < len; i++)
        {
            buf[i] = static_cast<WCHAR>(baseLetter + (entropy[i] % 26));
        }
        _AppendStoredCandidate(pOut, buf);
    }
}

void CCompositionProcessorEngine::_ExpandCmdCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!pCandidateList || pCandidateList->Count() == 0)
    {
        return;
    }

    BOOL hasCmd = FALSE;
    for (UINT i = 0; i < pCandidateList->Count(); i++)
    {
        CCandidateListItem* pLI = pCandidateList->GetAt(i);
        if (pLI && pLI->_ItemString.GetLength() >= 4 &&
            wcsncmp(pLI->_ItemString.Get(), DIME_CMD_PREFIX, 4) == 0)
        {
            hasCmd = TRUE;
            break;
        }
    }
    if (!hasCmd)
    {
        return;
    }

    _cmdStorageUsed = 0;
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    CDIMEArray<CCandidateListItem> rebuilt;
    for (UINT i = 0; i < pCandidateList->Count(); i++)
    {
        CCandidateListItem* pLI = pCandidateList->GetAt(i);
        if (!pLI)
        {
            continue;
        }

        if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_DATE))
        {
            _AppendCmdDateCandidates(&rebuilt, st);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_TIME))
        {
            _AppendCmdTimeCandidates(&rebuilt, st);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_NOW))
        {
            _AppendCmdNowCandidates(&rebuilt, st);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_WEEK))
        {
            _AppendCmdWeekCandidates(&rebuilt, st);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_UUID))
        {
            _AppendCmdUuidCandidates(&rebuilt);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_RAND))
        {
            _AppendCmdRandCandidates(&rebuilt);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_ASDF))
        {
            _AppendCmdAsdfCandidates(&rebuilt);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_QWER))
        {
            _AppendCmdQwerCandidates(&rebuilt);
        }
        else if (IsCmdCandidate(&pLI->_ItemString, DIME_CMD_ENG))
        {
            // 保留 CMD:ENG 原文, 上屏路径拦截; FindKeyCode 提示用途.
            CCandidateListItem* pNew = rebuilt.Append();
            if (pNew && _StoreCmdCandidateString(DIME_CMD_ENG, &pNew->_ItemString))
            {
                CStringRange hint;
                if (_StoreCmdCandidateString(L"临时英文", &hint))
                {
                    pNew->_FindKeyCode.Set(hint);
                }
                else
                {
                    CStringRange emptyKey; emptyKey.Set(L"", 0); pNew->_FindKeyCode.Set(emptyKey);
                }
            }
        }
        else
        {
            CCandidateListItem* pNew = rebuilt.Append();
            if (pNew)
            {
                *pNew = *pLI;
            }
        }
    }

    pCandidateList->Clear();
    for (UINT i = 0; i < rebuilt.Count(); i++)
    {
        CCandidateListItem* pSrc = rebuilt.GetAt(i);
        CCandidateListItem* pDst = pCandidateList->Append();
        if (pSrc && pDst)
        {
            *pDst = *pSrc;
        }
    }
}

//+---------------------------------------------------------------------------
//
// _GetPinyinCandidateList
//   Temporary pinyin input: search the pinyin dictionary with the keystroke
//   buffer treated as a pinyin string (uppercased to match the dictionary).
//   Pure digits: show Arabic / 中文 / 大写 / 带圈等数字形式候选.
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::_IsKeystrokeBufferPureDigits() const
{
    const DWORD_PTR len = _keystrokeBuffer.GetLength();
    // 空缓冲也视为“可继续输数字” (z 后直接敲数字).
    if (len == 0)
    {
        return TRUE;
    }
    const WCHAR* p = _keystrokeBuffer.Get();
    if (!p)
    {
        return TRUE;
    }
    for (DWORD_PTR i = 0; i < len; i++)
    {
        if (p[i] < L'0' || p[i] > L'9')
        {
            return FALSE;
        }
    }
    return TRUE;
}

void CCompositionProcessorEngine::_AppendPinyinDigitFormCandidates(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList)
{
    if (!pCandidateList)
    {
        return;
    }
    const DWORD_PTR len = _keystrokeBuffer.GetLength();
    if (len == 0 || !_keystrokeBuffer.Get())
    {
        return;
    }

    _cmdStorageUsed = 0;
    const WCHAR* digits = _keystrokeBuffer.Get();

    // 0-9 映射表 (逐位替换). 缺字形的用 ASCII 数字回退.
    static const WCHAR kSimple[10]  = { L'零', L'一', L'二', L'三', L'四', L'五', L'六', L'七', L'八', L'九' };
    // 〇一二… (圆圈零 U+3007)
    static const WCHAR kSimpleCircleZero[10] = { L'〇', L'一', L'二', L'三', L'四', L'五', L'六', L'七', L'八', L'九' };
    static const WCHAR kFinance[10] = { L'零', L'壹', L'贰', L'叁', L'肆', L'伍', L'陆', L'柒', L'捌', L'玖' };
    // ⓪①②③④⑤⑥⑦⑧⑨
    static const WCHAR kCircled[10] = {
        0x24EA, 0x2460, 0x2461, 0x2462, 0x2463, 0x2464, 0x2465, 0x2466, 0x2467, 0x2468
    };
    // ⓿❶❷❸❹❺❻❼❽❾
    static const WCHAR kDingbat[10] = {
        0x24FF, 0x2776, 0x2777, 0x2778, 0x2779, 0x277A, 0x277B, 0x277C, 0x277D, 0x277E
    };
    // ⑴⑵…⑼; 0 无 '0'
    static const WCHAR kParen[10] = {
        L'0', 0x2474, 0x2475, 0x2476, 0x2477, 0x2478, 0x2479, 0x247A, 0x247B, 0x247C
    };
    // ⒈⒉…⒐; 0 用 '0'
    static const WCHAR kPeriod[10] = {
        L'0', 0x2488, 0x2489, 0x248A, 0x248B, 0x248C, 0x248D, 0x248E, 0x248F, 0x2490
    };

    auto mapDigits = [&](const WCHAR table[10]) -> BOOL {
        WCHAR buf[_kCmdStorageCch] = {L'\0'};
        if (len >= _kCmdStorageCch)
        {
            return FALSE;
        }
        for (DWORD_PTR i = 0; i < len; i++)
        {
            buf[i] = table[digits[i] - L'0'];
        }
        buf[len] = L'\0';
        return _AppendStoredCandidate(pCandidateList, buf);
    };

    // 1) 阿拉伯数字原文
    {
        WCHAR buf[_kCmdStorageCch] = {L'\0'};
        if (len < _kCmdStorageCch &&
            SUCCEEDED(StringCchCopyN(buf, ARRAYSIZE(buf), digits, len)))
        {
            _AppendStoredCandidate(pCandidateList, buf);
        }
    }

    // 2) 零一二…
    mapDigits(kSimple);
    // 3) 〇一二… (圆圈零)
    mapDigits(kSimpleCircleZero);
    // 4) 零壹贰…
    mapDigits(kFinance);
    // 5) ⓪①②…
    mapDigits(kCircled);
    // 6) ⓿❶❷…
    mapDigits(kDingbat);

    // 7) ⑴⑵…
    mapDigits(kParen);
    // 8) ⒈⒉…
    mapDigits(kPeriod);
}

void CCompositionProcessorEngine::_GetPinyinCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL loadAllCandidates)
{
    DWORD_PTR len = _keystrokeBuffer.GetLength();
    if (len == 0)
    {
        return;
    }

    // 纯数字: 不查拼音码表, 直接给多形式候选.
    if (_IsKeystrokeBufferPureDigits())
    {
        _candidatesTruncated = FALSE;
        _AppendPinyinDigitFormCandidates(pCandidateList);
        return;
    }

    if (!IsPinyinDictionaryAvailable())
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

    // Hotkey enable flags are persisted; load before registering so a disabled
    // shortcut is not claimed from the system.
    {
        CRegKey reg;
        if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
        {
            DWORD dw = 1;
            if (reg.QueryDWORDValue(L"HotkeyOnlyCommon", dw) == ERROR_SUCCESS)
            {
                _hotkeyOnlyCommonEnabled = (dw != 0) ? TRUE : FALSE;
            }
            dw = 1;
            if (reg.QueryDWORDValue(L"HotkeyPunctuation", dw) == ERROR_SUCCESS)
            {
                _hotkeyPunctuationEnabled = (dw != 0) ? TRUE : FALSE;
            }
            dw = 1;
            if (reg.QueryDWORDValue(L"HotkeyDoubleSingleByte", dw) == ERROR_SUCCESS)
            {
                _hotkeyDoubleSingleByteEnabled = (dw != 0) ? TRUE : FALSE;
            }
        }
    }

    InitPreservedKey(&_PreservedKey_IMEMode, pThreadMgr, tfClientId);
    if (_hotkeyDoubleSingleByteEnabled)
    {
        InitPreservedKey(&_PreservedKey_DoubleSingleByte, pThreadMgr, tfClientId);
    }
    if (_hotkeyPunctuationEnabled)
    {
        InitPreservedKey(&_PreservedKey_Punctuation, pThreadMgr, tfClientId);
    }
    if (_hotkeyOnlyCommonEnabled)
    {
        InitPreservedKey(&_PreservedKey_OnlyCommon, pThreadMgr, tfClientId);
    }

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

    StringCchCopy((LPWSTR)pXPreservedKey->Description, srgKeystrokeBufLen + 1, pwszDescription);

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
        // 英文态 (IME 关闭) 不响应全/半角热键, 只输出半角.
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        if (!isOpen ||
            !_hotkeyDoubleSingleByteEnabled ||
            !CheckShiftKeyOnly(&_PreservedKey_DoubleSingleByte.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        ToggleDoubleSingleByte(pThreadMgr, tfClientId);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_Punctuation.Guid))
    {
        // 英文态不响应中英文标点热键.
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        if (!isOpen ||
            !_hotkeyPunctuationEnabled ||
            !CheckShiftKeyOnly(&_PreservedKey_Punctuation.TSFPreservedKeyTable))
        {
            *pIsEaten = FALSE;
            return;
        }
        TogglePunctuation(pThreadMgr, tfClientId);
        *pIsEaten = TRUE;
    }
    else if (IsEqualGUID(rguid, _PreservedKey_OnlyCommon.Guid))
    {
        // 英文态不响应常/全热键.
        BOOL isOpen = FALSE;
        CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
        CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen);
        if (!isOpen || !_hotkeyOnlyCommonEnabled)
        {
            *pIsEaten = FALSE;
            return;
        }
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
    // 英文态固定半角, 不允许切换.
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) || !isOpen)
    {
        return;
    }

    BOOL isDouble = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._GetCompartmentBOOL(isDouble);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(isDouble ? FALSE : TRUE);
    NotifyInputModeChanged(pThreadMgr);
    ULONGLONG ver = BumpSettingsVersionInRegistry();
    _settingsVersion = ver;
}

void CCompositionProcessorEngine::TogglePunctuation(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    // 英文态固定半角英文标点, 不允许切换.
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) || !isOpen)
    {
        return;
    }

    BOOL isPunctuation = FALSE;
    CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._GetCompartmentBOOL(isPunctuation);
    CompartmentPunctuation._SetCompartmentBOOL(isPunctuation ? FALSE : TRUE);
    NotifyInputModeChanged(pThreadMgr);
    ULONGLONG ver = BumpSettingsVersionInRegistry();
    _settingsVersion = ver;
}

void CCompositionProcessorEngine::ToggleOnlyCommon(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId)
{
    // 英文态不切换常/全.
    BOOL isOpen = FALSE;
    CCompartment CompartmentKeyboardOpen(pThreadMgr, tfClientId, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE);
    if (FAILED(CompartmentKeyboardOpen._GetCompartmentBOOL(isOpen)) || !isOpen)
    {
        return;
    }

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
    ULONGLONG ver = BumpSettingsVersionInRegistry();
    _settingsVersion = ver;
}

void CCompositionProcessorEngine::SetDoubleSingleByte(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isFullWidth)
{
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._SetCompartmentBOOL(isFullWidth);

    BOOL isChinesePunctuation = FALSE;
    CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._GetCompartmentBOOL(isChinesePunctuation);
    _SaveSettings(isFullWidth, isChinesePunctuation);
    NotifyInputModeChanged(pThreadMgr);
}

void CCompositionProcessorEngine::SetPunctuation(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isChinesePunctuation)
{
    CCompartment CompartmentPunctuation(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentPunctuation);
    CompartmentPunctuation._SetCompartmentBOOL(isChinesePunctuation);

    BOOL isFullWidth = FALSE;
    CCompartment CompartmentDoubleSingleByte(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
    CompartmentDoubleSingleByte._GetCompartmentBOOL(isFullWidth);
    _SaveSettings(isFullWidth, isChinesePunctuation);
    NotifyInputModeChanged(pThreadMgr);
}

void CCompositionProcessorEngine::SetOnlyCommon(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isOnlyCommon)
{
    CCompartment CompartmentOnlyCommon(pThreadMgr, tfClientId, Global::DIMEGuidCompartmentOnlyCommon);
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
    if (_pTextService)
    {
        _pTextService->_ResetInputForModeChange();
    }
    NotifyInputModeChanged(pThreadMgr);
}

void CCompositionProcessorEngine::SetEmptyCodeSearchFull(BOOL v)
{
    _emptyCodeSearchFull = v ? TRUE : FALSE;
    if (_pTableDictionaryEngine)
    {
        _pTableDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
    }
    if (_pPinyinDictionaryEngine)
    {
        _pPinyinDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
    }
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"EmptyCodeSearchFull", _emptyCodeSearchFull ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetEnglishCommaPeriodAfterDigit(BOOL v)
{
    _englishCommaPeriodAfterDigit = v ? TRUE : FALSE;
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"EnglishCommaPeriodAfterDigit", _englishCommaPeriodAfterDigit ? 1 : 0);
    }
}

void CCompositionProcessorEngine::UpdateLastKeyWasDigit(UINT uCode, WCHAR wch, BOOL isCandidateSelect)
{
    // 选字数字上屏的是汉字, 后续 ,/. 应走中文标点.
    if (isCandidateSelect)
    {
        _lastKeyWasDigit = FALSE;
        return;
    }

    if ((uCode >= L'0' && uCode <= L'9') ||
        (uCode >= VK_NUMPAD0 && uCode <= VK_NUMPAD9) ||
        (wch >= L'0' && wch <= L'9') ||
        (wch >= 0xFF10 && wch <= 0xFF19))
    {
        _lastKeyWasDigit = TRUE;
    }
    else
    {
        _lastKeyWasDigit = FALSE;
    }
}

BOOL CCompositionProcessorEngine::ShouldOutputEnglishCommaOrPeriod(WCHAR wch) const
{
    return _englishCommaPeriodAfterDigit && _lastKeyWasDigit &&
        (wch == L',' || wch == L'.');
}

void CCompositionProcessorEngine::SetHotkeyOnlyCommonEnabled(BOOL v)
{
    BOOL enabled = v ? TRUE : FALSE;
    if (_hotkeyOnlyCommonEnabled != enabled)
    {
        _hotkeyOnlyCommonEnabled = enabled;
        if (_pThreadMgr && _tfClientId != TF_CLIENTID_NULL)
        {
            if (_hotkeyOnlyCommonEnabled)
            {
                InitPreservedKey(&_PreservedKey_OnlyCommon, _pThreadMgr, _tfClientId);
            }
            else
            {
                _PreservedKey_OnlyCommon.UninitPreservedKey(_pThreadMgr);
            }
        }
    }

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"HotkeyOnlyCommon", _hotkeyOnlyCommonEnabled ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetHotkeyPunctuationEnabled(BOOL v)
{
    BOOL enabled = v ? TRUE : FALSE;
    if (_hotkeyPunctuationEnabled != enabled)
    {
        _hotkeyPunctuationEnabled = enabled;
        if (_pThreadMgr && _tfClientId != TF_CLIENTID_NULL)
        {
            if (_hotkeyPunctuationEnabled)
            {
                InitPreservedKey(&_PreservedKey_Punctuation, _pThreadMgr, _tfClientId);
            }
            else
            {
                _PreservedKey_Punctuation.UninitPreservedKey(_pThreadMgr);
            }
        }
    }

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"HotkeyPunctuation", _hotkeyPunctuationEnabled ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetHotkeyDoubleSingleByteEnabled(BOOL v)
{
    BOOL enabled = v ? TRUE : FALSE;
    if (_hotkeyDoubleSingleByteEnabled != enabled)
    {
        _hotkeyDoubleSingleByteEnabled = enabled;
        if (_pThreadMgr && _tfClientId != TF_CLIENTID_NULL)
        {
            if (_hotkeyDoubleSingleByteEnabled)
            {
                InitPreservedKey(&_PreservedKey_DoubleSingleByte, _pThreadMgr, _tfClientId);
            }
            else
            {
                _PreservedKey_DoubleSingleByte.UninitPreservedKey(_pThreadMgr);
            }
        }
    }

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"HotkeyDoubleSingleByte", _hotkeyDoubleSingleByteEnabled ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetWildcard(BOOL v)
{
    _isWildcard = v ? TRUE : FALSE;
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"Wildcard", _isWildcard ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetDisableWildcardAtFirst(BOOL v)
{
    _isDisableWildcardAtFirst = v ? TRUE : FALSE;
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"DisableWildcardAtFirst", _isDisableWildcardAtFirst ? 1 : 0);
    }
}

void CCompositionProcessorEngine::SetKeystrokeSort(BOOL v)
{
    _isKeystrokeSort = v ? TRUE : FALSE;
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"KeystrokeSort", _isKeystrokeSort ? 1 : 0);
    }
}

//+---------------------------------------------------------------------------
//
// SetupConfiguration

//
//----------------------------------------------------------------------------

void CCompositionProcessorEngine::SetupConfiguration()
{
    // Candidate-engine options keep their original defaults for now.
    // Wildcard / DisableWildcardAtFirst / KeystrokeSort are intentionally NOT
    // read from the registry yet — revisit when the UI comes back.
    _isWildcard = TRUE;
    _isDisableWildcardAtFirst = TRUE;
    _isKeystrokeSort = TRUE;

    // Per-page candidate count and candidate font size are user-configurable.
    CRegKey reg;
    DWORD dwPages = 10;
    DWORD dwFontSize = 0;
    DWORD dwDigitEnPunct = 1;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        if (reg.QueryDWORDValue(L"CandidatesPerPage", dwPages) == ERROR_SUCCESS &&
            dwPages >= 1 && dwPages <= 10)
        {
            _candidatePageSize = (int)dwPages;
        }
        else
        {
            _candidatePageSize = 10;
        }

        // 0 = auto; otherwise one of the fixed sizes offered in the UI.
        if (reg.QueryDWORDValue(L"CandidateFontSize", dwFontSize) == ERROR_SUCCESS &&
            (dwFontSize == 0 || dwFontSize == 12 || dwFontSize == 14 ||
             dwFontSize == 16 || dwFontSize == 18 || dwFontSize == 20 ||
             dwFontSize == 24 || dwFontSize == 28 || dwFontSize == 32))
        {
            _candidateFontSize = (int)dwFontSize;
        }
        else
        {
            _candidateFontSize = 0;
        }

        // Default ON: after a digit, ',' / '.' stay English.
        if (reg.QueryDWORDValue(L"EnglishCommaPeriodAfterDigit", dwDigitEnPunct) == ERROR_SUCCESS)
        {
            _englishCommaPeriodAfterDigit = (dwDigitEnPunct != 0) ? TRUE : FALSE;
        }
        else
        {
            _englishCommaPeriodAfterDigit = TRUE;
        }

        // Default ON: 常用字模式下空码时检索全码表.
        DWORD dwEmptyFull = 1;
        if (reg.QueryDWORDValue(L"EmptyCodeSearchFull", dwEmptyFull) == ERROR_SUCCESS)
        {
            _emptyCodeSearchFull = (dwEmptyFull != 0) ? TRUE : FALSE;
        }
        else
        {
            _emptyCodeSearchFull = TRUE;
        }
    }
    else
    {
        _candidatePageSize = 10;
        _candidateFontSize = 0;
        _englishCommaPeriodAfterDigit = TRUE;
        _emptyCodeSearchFull = TRUE;
    }

    _hasMakePhraseFromText = FALSE;
    _candidateWndWidth = CAND_WINDOW_WIDTH_PX;

    SetInitialCandidateListRange(_candidatePageSize);

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

BOOL CCompositionProcessorEngine::ResolveDictionaryDirectory(_Out_writes_(cch) WCHAR* buf, DWORD cch)
{
    if (!buf || cch == 0)
    {
        return FALSE;
    }
    buf[0] = L'\0';

    WCHAR wszFileName[MAX_PATH] = {L'\0'};
    DWORD cchA = GetModuleFileName(Global::dllInstanceHandle, wszFileName, ARRAYSIZE(wszFileName));
    if (cchA == 0 || cchA >= ARRAYSIZE(wszFileName))
    {
        return FALSE;
    }

    size_t iDir = cchA;
    while (iDir--)
    {
        WCHAR wszChar = wszFileName[iDir];
        if (wszChar == L'\\' || wszChar == L'/')
        {
            break;
        }
    }
    size_t dllDirLen = iDir + 1;

    // Prefer shared dict\: flat layout, then parent\dict\, else DLL directory.
    struct { const WCHAR* suffix; size_t baseLen; } candidates[] =
    {
        { L"dict\\", dllDirLen },
        { L"..\\dict\\", dllDirLen },
    };

    for (int c = 0; c < _countof(candidates); c++)
    {
        WCHAR wszDictDir[MAX_PATH] = {L'\0'};
        if (SUCCEEDED(StringCchCopyN(wszDictDir, ARRAYSIZE(wszDictDir), wszFileName, candidates[c].baseLen)) &&
            SUCCEEDED(StringCchCatW(wszDictDir, ARRAYSIZE(wszDictDir), candidates[c].suffix)))
        {
            DWORD attrs = GetFileAttributes(wszDictDir);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                return SUCCEEDED(StringCchCopy(buf, cch, wszDictDir));
            }
        }
    }

    return SUCCEEDED(StringCchCopyN(buf, cch, wszFileName, dllDirLen));
}

void CCompositionProcessorEngine::ReadDictionaryNameFromRegistry(_Out_writes_(cch) WCHAR* buf, DWORD cch)
{
    if (!buf || cch == 0)
    {
        return;
    }
    StringCchCopy(buf, cch, TEXTSERVICE_DIC_STEM);

    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") != ERROR_SUCCESS)
    {
        return;
    }
    WCHAR value[64] = {L'\0'};
    ULONG chars = ARRAYSIZE(value);
    if (reg.QueryStringValue(L"Dictionary", value, &chars) != ERROR_SUCCESS || value[0] == L'\0')
    {
        return;
    }
    if (_IsValidDictionaryStem(value))
    {
        StringCchCopy(buf, cch, value);
    }
}

void CCompositionProcessorEngine::WriteDictionaryNameToRegistry(_In_z_ LPCWSTR name)
{
    if (!_IsValidDictionaryStem(name))
    {
        return;
    }
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetStringValue(L"Dictionary", name);
    }
}

ULONGLONG CCompositionProcessorEngine::ReadSettingsVersionFromRegistry()
{
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") != ERROR_SUCCESS)
    {
        return 0;
    }
    ULONGLONG ver = 0;
    if (reg.QueryQWORDValue(L"SettingsVersion", ver) != ERROR_SUCCESS)
    {
        return 0;
    }
    return ver;
}

BOOL CCompositionProcessorEngine::IsSyncSettingsOnFocusEnabled()
{
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") != ERROR_SUCCESS)
    {
        return TRUE; // 默认开启
    }
    DWORD dw = 1;
    if (reg.QueryDWORDValue(L"SyncSettingsOnFocus", dw) != ERROR_SUCCESS)
    {
        return TRUE;
    }
    return (dw != 0) ? TRUE : FALSE;
}

void CCompositionProcessorEngine::SetSyncSettingsOnFocusEnabled(BOOL enabled)
{
    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"SyncSettingsOnFocus", enabled ? 1 : 0);
    }
}

ULONGLONG CCompositionProcessorEngine::BumpSettingsVersionInRegistry()
{
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG ver = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) |
        static_cast<ULONGLONG>(ft.dwLowDateTime);

    // 保证严格递增, 避免同一 FILETIME 分辨率内连续保存撞车.
    ULONGLONG prev = ReadSettingsVersionFromRegistry();
    if (ver <= prev)
    {
        ver = prev + 1;
    }

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetQWORDValue(L"SettingsVersion", ver);
    }
    DIME_DEBUG_LOG(L"BumpSettingsVersion -> %llu", ver);
    return ver;
}

void CCompositionProcessorEngine::AcknowledgeSettingsVersion()
{
    _settingsVersion = ReadSettingsVersionFromRegistry();
}

ULONGLONG CCompositionProcessorEngine::ReadDictionaryVersionFromRegistry()
{
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") != ERROR_SUCCESS)
    {
        return 0;
    }
    ULONGLONG ver = 0;
    if (reg.QueryQWORDValue(L"DictionaryVersion", ver) != ERROR_SUCCESS)
    {
        return 0;
    }
    return ver;
}

ULONGLONG CCompositionProcessorEngine::BumpDictionaryVersionInRegistry()
{
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULONGLONG ver = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) |
        static_cast<ULONGLONG>(ft.dwLowDateTime);

    ULONGLONG prev = ReadDictionaryVersionFromRegistry();
    if (ver <= prev)
    {
        ver = prev + 1;
    }

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetQWORDValue(L"DictionaryVersion", ver);
    }
    DIME_DEBUG_LOG(L"BumpDictionaryVersion -> %llu", ver);
    return ver;
}

void CCompositionProcessorEngine::AcknowledgeDictionaryVersion()
{
    _dictionaryVersion = ReadDictionaryVersionFromRegistry();
}

void CCompositionProcessorEngine::TryCleanupStaleBinOldFiles(_In_z_ LPCWSTR dictDir)
{
    if (!dictDir || dictDir[0] == L'\0')
    {
        return;
    }

    WCHAR search[MAX_PATH] = {L'\0'};
    if (FAILED(StringCchCopy(search, ARRAYSIZE(search), dictDir)) ||
        FAILED(StringCchCat(search, ARRAYSIZE(search), L"*.bin.old*")))
    {
        return;
    }

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            continue;
        }
        WCHAR path[MAX_PATH] = {L'\0'};
        if (SUCCEEDED(StringCchCopy(path, ARRAYSIZE(path), dictDir)) &&
            SUCCEEDED(StringCchCat(path, ARRAYSIZE(path), fd.cFileName)))
        {
            DeleteFileW(path); // 仍被 map 时失败, 忽略.
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void CCompositionProcessorEngine::ReloadDictionariesIfVersionChanged()
{
    ULONGLONG remote = ReadDictionaryVersionFromRegistry();
    if (remote == _dictionaryVersion)
    {
        return;
    }

    DIME_INFO_LOG(L"ReloadDictionariesIfVersionChanged local=%llu remote=%llu",
        _dictionaryVersion, remote);

    if (_pTextService)
    {
        _pTextService->_ResetInputForModeChange();
    }

    WCHAR dictDir[MAX_PATH] = {L'\0'};
    if (!ResolveDictionaryDirectory(dictDir, ARRAYSIZE(dictDir)))
    {
        DIME_ERROR_LOG(L"ReloadDictionariesIfVersionChanged: cannot resolve dict directory");
        // 仍对齐版本, 避免获焦时反复失败刷日志; 下次 bump 会再试.
        AcknowledgeDictionaryVersion();
        return;
    }
    size_t dictDirLen = wcslen(dictDir);

    WCHAR stem[64] = {L'\0'};
    StringCchCopy(stem, ARRAYSIZE(stem), _mainDictionaryName);

    _UnloadMainDictionary();
    if (!_LoadMainDictionaryFromStem(stem, dictDir, dictDirLen))
    {
        DIME_WARNING_LOG(L"ReloadDictionaries: load %s failed, fallback %s", stem, TEXTSERVICE_DIC_STEM);
        if (_wcsicmp(stem, TEXTSERVICE_DIC_STEM) != 0 &&
            _LoadMainDictionaryFromStem(TEXTSERVICE_DIC_STEM, dictDir, dictDirLen))
        {
            StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), TEXTSERVICE_DIC_STEM);
            WriteDictionaryNameToRegistry(TEXTSERVICE_DIC_STEM);
        }
        else
        {
            DIME_ERROR_LOG(L"ReloadDictionaries: main dictionary reload FAILED");
        }
    }
    else
    {
        StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), stem);
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
    if (!_LoadDictionary(TEXTSERVICE_PINYIN_DIC, dictDir, dictDirLen,
                         &_pPinyinDictionaryFile, &_pPinyinDictionaryEngine))
    {
        DIME_WARNING_LOG(L"ReloadDictionaries: pinyin dictionary not loaded (optional)");
    }

    if (_pTableDictionaryEngine)
    {
        _pTableDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
        _pTableDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
    }
    if (_pPinyinDictionaryEngine)
    {
        _pPinyinDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
        _pPinyinDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
    }

    TryCleanupStaleBinOldFiles(dictDir);
    AcknowledgeDictionaryVersion();

    if (_pTextService)
    {
        _pTextService->_RefreshStatusWindow();
    }
}

void CCompositionProcessorEngine::ApplySettingsFromRegistryIfNeeded()
{
    if (!IsSyncSettingsOnFocusEnabled())
    {
        return;
    }

    ULONGLONG remote = ReadSettingsVersionFromRegistry();
    if (remote == _settingsVersion)
    {
        return;
    }

    DIME_DEBUG_LOG(L"ApplySettingsFromRegistryIfNeeded local=%llu remote=%llu",
        _settingsVersion, remote);

    // --- Dictionary ---
    WCHAR stem[64] = {L'\0'};
    ReadDictionaryNameFromRegistry(stem, ARRAYSIZE(stem));
    if (stem[0] != L'\0' && _wcsicmp(stem, _mainDictionaryName) != 0)
    {
        SetMainDictionaryName(stem);
    }

    // --- Input mode compartments (only when TSF client is live) ---
    BOOL isFullWidth = FALSE;
    BOOL isChinesePunctuation = TRUE;
    _LoadSettings(isFullWidth, isChinesePunctuation);
    BOOL isOnlyCommon = _ReadRegistryOnlyCommon();

    if (_pThreadMgr != nullptr && _tfClientId != TF_CLIENTID_NULL)
    {
        CCompartment compartmentFull(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentDoubleSingleByte);
        CCompartment compartmentPunct(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentPunctuation);
        CCompartment compartmentOnly(_pThreadMgr, _tfClientId, Global::DIMEGuidCompartmentOnlyCommon);
        compartmentFull._SetCompartmentBOOL(isFullWidth);
        compartmentPunct._SetCompartmentBOOL(isChinesePunctuation);
        compartmentOnly._SetCompartmentBOOL(isOnlyCommon);
    }

    _isOnlyCommon = isOnlyCommon;
    if (_pTableDictionaryEngine)
    {
        _pTableDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
    }
    if (_pPinyinDictionaryEngine)
    {
        _pPinyinDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
    }

    // --- Engine options (re-read like SetupConfiguration) ---
    CRegKey reg;
    if (reg.Open(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        DWORD dw = 0;
        if (reg.QueryDWORDValue(L"EnglishCommaPeriodAfterDigit", dw) == ERROR_SUCCESS)
        {
            _englishCommaPeriodAfterDigit = (dw != 0) ? TRUE : FALSE;
        }
        if (reg.QueryDWORDValue(L"EmptyCodeSearchFull", dw) == ERROR_SUCCESS)
        {
            _emptyCodeSearchFull = (dw != 0) ? TRUE : FALSE;
            if (_pTableDictionaryEngine)
            {
                _pTableDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
            }
            if (_pPinyinDictionaryEngine)
            {
                _pPinyinDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
            }
        }
        if (reg.QueryDWORDValue(L"CandidatesPerPage", dw) == ERROR_SUCCESS &&
            dw >= 1 && dw <= 10)
        {
            _candidatePageSize = static_cast<int>(dw);
            SetInitialCandidateListRange(_candidatePageSize);
        }
        if (reg.QueryDWORDValue(L"CandidateFontSize", dw) == ERROR_SUCCESS &&
            (dw == 0 || dw == 12 || dw == 14 || dw == 16 || dw == 18 ||
             dw == 20 || dw == 24 || dw == 28 || dw == 32))
        {
            _candidateFontSize = static_cast<int>(dw);
            SetDefaultCandidateTextFont();
        }

        BOOL hkOnly = TRUE, hkPunct = TRUE, hkFull = TRUE;
        if (reg.QueryDWORDValue(L"HotkeyOnlyCommon", dw) == ERROR_SUCCESS)
        {
            hkOnly = (dw != 0) ? TRUE : FALSE;
        }
        if (reg.QueryDWORDValue(L"HotkeyPunctuation", dw) == ERROR_SUCCESS)
        {
            hkPunct = (dw != 0) ? TRUE : FALSE;
        }
        if (reg.QueryDWORDValue(L"HotkeyDoubleSingleByte", dw) == ERROR_SUCCESS)
        {
            hkFull = (dw != 0) ? TRUE : FALSE;
        }

        // 热键开关: 只改注册状态, 不再写回注册表 (避免 bump).
        if (_pThreadMgr && _tfClientId != TF_CLIENTID_NULL)
        {
            if (hkOnly != _hotkeyOnlyCommonEnabled)
            {
                _hotkeyOnlyCommonEnabled = hkOnly;
                if (_hotkeyOnlyCommonEnabled)
                {
                    InitPreservedKey(&_PreservedKey_OnlyCommon, _pThreadMgr, _tfClientId);
                }
                else
                {
                    _PreservedKey_OnlyCommon.UninitPreservedKey(_pThreadMgr);
                }
            }
            if (hkPunct != _hotkeyPunctuationEnabled)
            {
                _hotkeyPunctuationEnabled = hkPunct;
                if (_hotkeyPunctuationEnabled)
                {
                    InitPreservedKey(&_PreservedKey_Punctuation, _pThreadMgr, _tfClientId);
                }
                else
                {
                    _PreservedKey_Punctuation.UninitPreservedKey(_pThreadMgr);
                }
            }
            if (hkFull != _hotkeyDoubleSingleByteEnabled)
            {
                _hotkeyDoubleSingleByteEnabled = hkFull;
                if (_hotkeyDoubleSingleByteEnabled)
                {
                    InitPreservedKey(&_PreservedKey_DoubleSingleByte, _pThreadMgr, _tfClientId);
                }
                else
                {
                    _PreservedKey_DoubleSingleByte.UninitPreservedKey(_pThreadMgr);
                }
            }
        }
        else
        {
            _hotkeyOnlyCommonEnabled = hkOnly;
            _hotkeyPunctuationEnabled = hkPunct;
            _hotkeyDoubleSingleByteEnabled = hkFull;
        }

        // 状态栏显隐
        if (_pTextService)
        {
            CStatusWindow* sw = _pTextService->_GetStatusWindow();
            if (sw != nullptr)
            {
                sw->_LoadHiddenState();
            }
        }
    }

    if (_pTextService)
    {
        _pTextService->_ResetInputForModeChange();
        _pTextService->_RefreshStatusWindow();
        _pTextService->_RefreshCandidateInputModeStatus();
    }
    else if (_pThreadMgr)
    {
        NotifyInputModeChanged(_pThreadMgr);
    }

    _settingsVersion = remote;
}

BOOL CCompositionProcessorEngine::_IsValidDictionaryStem(_In_z_ LPCWSTR name)
{
    if (!name || name[0] == L'\0')
    {
        return FALSE;
    }
    // 禁止路径分隔与扩展名, 避免越出 dict 目录.
    for (const WCHAR* p = name; *p; ++p)
    {
        if (*p == L'\\' || *p == L'/' || *p == L':' || *p == L'.' ||
            *p == L'*' || *p == L'?' || *p == L'"' || *p == L'<' ||
            *p == L'>' || *p == L'|')
        {
            return FALSE;
        }
    }
    if (_wcsicmp(name, L"pinyin") == 0)
    {
        return FALSE;
    }
    return TRUE;
}

int CCompositionProcessorEngine::EnumerateMainDictionaries(_Out_writes_(maxCount) DictionaryListItem* items, int maxCount)
{
    if (!items || maxCount <= 0)
    {
        return 0;
    }

    WCHAR dictDir[MAX_PATH] = {L'\0'};
    if (!ResolveDictionaryDirectory(dictDir, ARRAYSIZE(dictDir)))
    {
        return 0;
    }

    WCHAR pattern[MAX_PATH] = {L'\0'};
    if (FAILED(StringCchCopy(pattern, ARRAYSIZE(pattern), dictDir)) ||
        FAILED(StringCchCat(pattern, ARRAYSIZE(pattern), L"*.bin")))
    {
        return 0;
    }

    int count = 0;
    WIN32_FIND_DATAW fd = {0};
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            continue;
        }
        if (_wcsicmp(fd.cFileName, TEXTSERVICE_PINYIN_BIN) == 0)
        {
            continue;
        }
        WCHAR stem[64] = {L'\0'};
        StringCchCopy(stem, ARRAYSIZE(stem), fd.cFileName);
        WCHAR* dot = wcsrchr(stem, L'.');
        if (dot)
        {
            *dot = L'\0';
        }
        if (!_IsValidDictionaryStem(stem))
        {
            continue;
        }
        if (count >= maxCount)
        {
            continue;
        }

        DictionaryListItem& item = items[count];
        StringCchCopy(item.stem, ARRAYSIZE(item.stem), stem);
        // 默认显示名为文件名; 有 NAME 属性时覆盖.
        StringCchCopy(item.displayName, ARRAYSIZE(item.displayName), stem);

        WCHAR binPath[MAX_PATH] = {L'\0'};
        if (SUCCEEDED(StringCchCopy(binPath, ARRAYSIZE(binPath), dictDir)) &&
            SUCCEEDED(StringCchCat(binPath, ARRAYSIZE(binPath), fd.cFileName)))
        {
            DictConfig cfg;
            if (CBinaryDictionaryEngine::ReadConfigFromFile(binPath, cfg) && !cfg.name.empty())
            {
                StringCchCopy(item.displayName, ARRAYSIZE(item.displayName), cfg.name.c_str());
            }
        }
        ++count;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // 按显示名排序.
    for (int i = 0; i + 1 < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            if (_wcsicmp(items[i].displayName, items[j].displayName) > 0)
            {
                DictionaryListItem tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
    return count;
}

void CCompositionProcessorEngine::GetMainDictionaryName(_Out_writes_(cch) WCHAR* buf, DWORD cch) const
{
    if (!buf || cch == 0)
    {
        return;
    }
    if (_mainDictionaryName[0] != L'\0')
    {
        StringCchCopy(buf, cch, _mainDictionaryName);
    }
    else
    {
        ReadDictionaryNameFromRegistry(buf, cch);
    }
}

void CCompositionProcessorEngine::_UnloadMainDictionary()
{
    if (_pTableDictionaryEngine)
    {
        delete _pTableDictionaryEngine;
        _pTableDictionaryEngine = nullptr;
    }
    if (_pDictionaryFile)
    {
        delete _pDictionaryFile;
        _pDictionaryFile = nullptr;
    }
}

BOOL CCompositionProcessorEngine::_LoadMainDictionaryFromStem(_In_z_ LPCWSTR stem, _In_ LPCWSTR pwszDir, size_t dirLen)
{
    if (!_IsValidDictionaryStem(stem))
    {
        return FALSE;
    }
    WCHAR dicFile[80] = {L'\0'};
    if (FAILED(StringCchPrintf(dicFile, ARRAYSIZE(dicFile), L"%s.txt", stem)))
    {
        return FALSE;
    }
    return _LoadDictionary(dicFile, pwszDir, dirLen, &_pDictionaryFile, &_pTableDictionaryEngine);
}

BOOL CCompositionProcessorEngine::SetMainDictionaryName(_In_z_ LPCWSTR name)
{
    if (!_IsValidDictionaryStem(name))
    {
        return FALSE;
    }

    WriteDictionaryNameToRegistry(name);

    // 未激活引擎时只写注册表; 下次 Activate 再加载.
    if (_pTableDictionaryEngine == nullptr && _pDictionaryFile == nullptr)
    {
        StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), name);
        return TRUE;
    }

    if (_wcsicmp(_mainDictionaryName, name) == 0 && _pTableDictionaryEngine != nullptr)
    {
        return TRUE;
    }

    WCHAR dictDir[MAX_PATH] = {L'\0'};
    if (!ResolveDictionaryDirectory(dictDir, ARRAYSIZE(dictDir)))
    {
        return FALSE;
    }

    _UnloadMainDictionary();
    if (!_LoadMainDictionaryFromStem(name, dictDir, wcslen(dictDir)))
    {
        // 回退到默认 wubi98.
        DIME_ERROR_LOG(L"SetMainDictionaryName: load %s failed, fallback to %s", name, TEXTSERVICE_DIC_STEM);
        if (!_LoadMainDictionaryFromStem(TEXTSERVICE_DIC_STEM, dictDir, wcslen(dictDir)))
        {
            return FALSE;
        }
        StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), TEXTSERVICE_DIC_STEM);
        WriteDictionaryNameToRegistry(TEXTSERVICE_DIC_STEM);
    }
    else
    {
        StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), name);
    }

    if (_pTableDictionaryEngine)
    {
        _pTableDictionaryEngine->SetOnlyCommon(_isOnlyCommon);
        _pTableDictionaryEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);
    }

    TryCleanupStaleBinOldFiles(dictDir);

    if (_pTextService)
    {
        _pTextService->_ResetInputForModeChange();
    }
    return TRUE;
}

BOOL CCompositionProcessorEngine::SetupDictionaryFile()
{
    WCHAR wszDictDir[MAX_PATH] = {L'\0'};
    if (!ResolveDictionaryDirectory(wszDictDir, ARRAYSIZE(wszDictDir)))
    {
        DIME_ERROR_LOG(L"SetupDictionaryFile: cannot resolve dict directory");
        return FALSE;
    }
    size_t dictDirLen = wcslen(wszDictDir);
    const WCHAR* pDictDir = wszDictDir;

    WCHAR stem[64] = {L'\0'};
    ReadDictionaryNameFromRegistry(stem, ARRAYSIZE(stem));

    // 主词库 (必需); 所选词库失败时回退 wubi98.
    if (!_LoadMainDictionaryFromStem(stem, pDictDir, dictDirLen))
    {
        DIME_WARNING_LOG(L"SetupDictionaryFile: load %s failed, try default %s", stem, TEXTSERVICE_DIC_STEM);
        if (_wcsicmp(stem, TEXTSERVICE_DIC_STEM) != 0 &&
            _LoadMainDictionaryFromStem(TEXTSERVICE_DIC_STEM, pDictDir, dictDirLen))
        {
            StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), TEXTSERVICE_DIC_STEM);
        }
        else
        {
            DIME_ERROR_LOG(L"SetupDictionaryFile: main wubi dictionary load FAILED (dir=%s)", pDictDir);
            return FALSE;
        }
    }
    else
    {
        StringCchCopy(_mainDictionaryName, ARRAYSIZE(_mainDictionaryName), stem);
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

    // Prefer the precompiled binary dictionary (.bin): zero-parse load,
    // zero-copy lookup. Runtime never opens the companion .txt.
    WCHAR binPath[MAX_PATH] = {L'\0'};
    BOOL canBin = SUCCEEDED(StringCchCopyW(binPath, ARRAYSIZE(binPath), pwszPath)) &&
                  _ReplaceExtensionWithBin(binPath);

    if (canBin && _TryLoadBinary(binPath, ppFile, ppEngine))
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
            _TryLoadBinary(binPath, ppFile, ppEngine))
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
// engine. Does not open any companion .txt. On success *ppFile is nullptr
// (text mapping unused) and *ppEngine owns the binary reader.
BOOL CCompositionProcessorEngine::_TryLoadBinary(_In_ LPCWSTR pwszBinPath,
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
    // FILE_SHARE_DELETE: 允许 updater 在 IME 仍 map 时 rename/删除旧 .bin (方案 2).
    if (!pBinFile->CreateFile(pwszBinPath, GENERIC_READ, OPEN_EXISTING,
                              FILE_SHARE_READ | FILE_SHARE_DELETE))
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

    CTableDictionaryEngine* pEngine = new (std::nothrow) CTableDictionaryEngine(GetLocale(), nullptr, pBin);
    if (!pEngine)
    {
        delete pBin;       // dtor deletes pBinFile
        return FALSE;
    }
    pEngine->SetOnlyCommon(_GetCompartmentOnlyCommon());
    pEngine->SetEmptyCodeSearchFull(_emptyCodeSearchFull);

    *ppFile = nullptr;
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
    // UnpreserveKey must run against the ThreadMgr used at PreserveKey.
    // ~CCompositionProcessorEngine calls UninitPreservedKey before members die.
    if (Description)
    {
        delete [] Description;
        Description = nullptr;
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

void CCompositionProcessorEngine::SetInitialCandidateListRange(int pageSize)
{
    if (pageSize < 1) pageSize = 1;
    if (pageSize > 10) pageSize = 10;

    _candidateListIndexRange.Clear();
    for (int i = 1; i <= pageSize; i++)
    {
        DWORD* pNewIndexRange = nullptr;

        pNewIndexRange = _candidateListIndexRange.Append();
        if (pNewIndexRange != nullptr)
        {
            // The 10th selectable slot maps to the digit '0' (value 0);
            // slots 1-9 map to their own digit value.
            *pNewIndexRange = (i == 10) ? 0 : (DWORD)i;
        }
    }
}

void CCompositionProcessorEngine::SetCandidatePageSize(int n)
{
    if (n < 1) n = 1;
    if (n > 10) n = 10;

    _candidatePageSize = n;
    SetInitialCandidateListRange(n);

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"CandidatesPerPage", (DWORD)n);
    }
}

void CCompositionProcessorEngine::SetCandidateFontSize(int px)
{
    // 0 = auto; otherwise only the fixed sizes offered in the UI.
    if (px != 0 && px != 12 && px != 14 && px != 16 && px != 18 &&
        px != 20 && px != 24 && px != 28 && px != 32)
    {
        px = 0;
    }

    _candidateFontSize = px;

    CRegKey reg;
    if (reg.Create(HKEY_CURRENT_USER, L"Software\\DIME") == ERROR_SUCCESS)
    {
        reg.SetDWORDValue(L"CandidateFontSize", (DWORD)px);
    }

    // Rebuild the shared candidate font so the new size takes effect immediately.
    SetDefaultCandidateTextFont();
}

// Pick a preferred candidate/font pixel height from a fixed set of
// well-proportioned sizes (14/16/18/20/24/32), one tier per common display
// scaling, instead of a continuous DPI formula. This keeps the rendered text
// size on clean values at every scaling.
static int DimeSelectFontPixelHeight(UINT dpi)
{
    if (dpi <= 108) return 14;   // 100%
    if (dpi <= 156) return 16;   // 125% and 150%
    if (dpi <= 180) return 20;   // 175%
    if (dpi <= 216) return 24;   // 200%
    return 32;                   // 250% and above
}

int CCompositionProcessorEngine::GetAutoCandidateFontSize()
{
    HDC hdc = GetDC(NULL);
    UINT dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc)
    {
        ReleaseDC(NULL, hdc);
    }
    if (dpi == 0)
    {
        dpi = 96;
    }
    return DimeSelectFontPixelHeight(dpi);
}

void CCompositionProcessorEngine::GetPreviewCandidateList(_In_z_ LPCWSTR key, _Inout_ CDIMEArray<CCandidateListItem> *pList, UINT maxCount)
{
    if (pList == nullptr || key == nullptr || key[0] == L'\0')
    {
        return;
    }
    if (!IsDictionaryAvailable() || _pTableDictionaryEngine == nullptr)
    {
        return;
    }

    CStringRange keyRange;
    keyRange.Set(key, wcslen(key));

    BOOL hasMore = FALSE;
    _pTableDictionaryEngine->CollectWordByPrefix(&keyRange, pList, maxCount, &hasMore);

    if (IsKeystrokeSort())
    {
        _pTableDictionaryEngine->SortListItemByFindKeyCode(pList);
    }

    // Trim the typed prefix from each FindKeyCode, matching GetCandidateList.
    const DWORD_PTR keyLen = keyRange.GetLength();
    for (UINT index = 0; index < pList->Count(); index++)
    {
        CCandidateListItem *pLI = pList->GetAt(index);
        if (pLI->_FindKeyCode.GetLength() > keyLen)
        {
            CStringRange newFindKeyCode;
            newFindKeyCode.Set(pLI->_FindKeyCode.Get() + keyLen, pLI->_FindKeyCode.GetLength() - keyLen);
            pLI->_FindKeyCode.Set(newFindKeyCode);
        }
        else
        {
            CStringRange emptyKey;
            emptyKey.Set(L"", 0);
            pLI->_FindKeyCode.Set(emptyKey);
        }
    }

    _ExpandCmdCandidates(pList);
}

void CCompositionProcessorEngine::SetDefaultCandidateTextFont()
{
    HDC hdc = GetDC(NULL);
    UINT dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc)
    {
        ReleaseDC(NULL, hdc);
    }
    if (dpi == 0)
    {
        dpi = 96;
    }

    // Manual size wins; 0 falls back to the DPI-tier auto height.
    int fontPx = (_candidateFontSize > 0) ? _candidateFontSize : DimeSelectFontPixelHeight(dpi);

    WCHAR fontName[50] = {L'\0'};
    LoadString(Global::dllInstanceHandle, IDS_DEFAULT_FONT, fontName, 50);

    HFONT hNew = CreateFont(-fontPx, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, fontName);
    if (!hNew)
    {
        LOGFONT lf = {0};
        SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &lf, 0);
        // Fall back to the default GUI face on failure.
        hNew = CreateFont(-fontPx, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, lf.lfFaceName);
    }

    if (hNew)
    {
        HFONT hOld = Global::defaultlFontHandle;
        Global::defaultlFontHandle = hNew;
        if (hOld != nullptr)
        {
            DeleteObject(hOld);
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

    if (uCode == VK_OEM_MINUS)
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
    // Shift+; produces ':' and must NOT enter English mode: let it fall through
    // to punctuation / full-half-width handling so the colon is committed directly.
    //
    if (!_isEnglishInput && _keystrokeBuffer.GetLength() == 0 &&
        uCode == VK_OEM_1 && pwch && *pwch == L';' &&
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

    // Temporary English mode: collect literally; digits/letters go to composing.
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
                WCHAR wchEng = (pwch ? *pwch : L'\0');
                if (wchEng && (iswalnum(wchEng) || iswpunct(wchEng) || wchEng > 0x80))
                {
                    if (pKeyState) { pKeyState->Category = CATEGORY_COMPOSING; pKeyState->Function = FUNCTION_INPUT; }
                    return TRUE;
                }
                return FALSE;
            }
        }
    }

    // z 模式纯数字: 数字键继续输入 (不用于选词), 候选为各种数字形式.
    if (_isPinyinInput && pwch && *pwch >= L'0' && *pwch <= L'9' && _IsKeystrokeBufferPureDigits())
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_INPUT;
        }
        return TRUE;
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