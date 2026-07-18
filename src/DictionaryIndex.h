// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "DictionaryParser.h"
#include "DIMEBaseStructure.h"
#include "File.h"

class CDictionaryIndex : public CDictionaryParser
{
public:
    explicit CDictionaryIndex(LCID locale);
    ~CDictionaryIndex() override = default;

    BOOL Build(_In_ CFile *pFile);
    BOOL IsBuilt() const { return _isBuilt; }

    VOID LookupExact(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
    VOID LookupPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);
    VOID LookupWildcard(_In_ CStringRange *pPattern, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);

    VOID LookupExact(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings);

    // Reverse lookup: given a word (the candidate character), returns its
    // wubi code (the dictionary key). Used by temporary pinyin mode to show
    // the wubi code on the right side of each candidate.
    BOOL LookupCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode);

private:
    static BOOL _IsWildcardChar(WCHAR wch);
    static std::wstring _ToUpperKey(_In_reads_(length) const WCHAR *pwch, DWORD_PTR length);
    static BOOL _KeyStartsWith(_In_ const std::wstring &key, _In_ const std::wstring &prefix);
    static BOOL _MatchesWildcardPattern(_In_ const std::wstring &key, _In_ const std::wstring &pattern);

    VOID _AppendEntries(_In_ const std::wstring &key, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
    VOID _AppendEntries(_In_ const std::wstring &key, _Inout_ CDIMEArray<CStringRange> *pWordStrings);
    BOOL _AppendEntriesWithLimit(_In_ const std::wstring &key, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Inout_ UINT &collected);

    BOOL _isBuilt;
    std::unordered_map<std::wstring, std::vector<std::wstring>> _exactMap;
    std::unordered_map<std::wstring, std::wstring> _reverseMap;
    std::vector<std::wstring> _sortedKeys;
};
