// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

#include "BaseDictionaryEngine.h"

class CDictionaryIndex;
class CBinaryDictionaryEngine;

class CTableDictionaryEngine : public CBaseDictionaryEngine
{
public:
    CTableDictionaryEngine(LCID locale, _In_opt_ CFile *pDictionaryFile, _In_opt_ CBinaryDictionaryEngine *pBinary = nullptr);
    virtual ~CTableDictionaryEngine();

    // Collect word from phrase string.
    // param
    //     [in] psrgKeyCode - Specified key code pointer
    //     [out] pasrgWordString - Specified returns pointer of word as CStringRange.
    // returns
    //     none.
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings);
    VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);

    VOID CollectWordForWildcard(_In_ CStringRange *psrgKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);

    VOID CollectWordByPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);

    VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);

    // Reverse lookup used by temporary pinyin mode: find the wubi code for a
    // given candidate word so it can be displayed on the right side.
    BOOL FindCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode);

    // "Only common characters" filter (see CBinaryDictionaryEngine). Forwarded
    // to the binary reader; the text fallback path does not filter.
    void SetOnlyCommon(BOOL f);
    // 空码检索全码表: forwarded to the binary reader.
    void SetEmptyCodeSearchFull(BOOL f);
    BOOL IsEmptyCodeSearchFull() const;

private:
    CDictionaryIndex* _pIndex;
    // Optional precompiled binary reader; when present, lookups are delegated
    // to it (zero-copy, no in-memory index build). Null in the text fallback.
    CBinaryDictionaryEngine* _pBinary;
    BOOL _onlyCommon;
};
