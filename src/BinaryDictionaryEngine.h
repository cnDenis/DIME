// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT

// CBinaryDictionaryEngine: mmap-based reader for the precompiled binary
// dictionary (.bin, see BinaryDictFormat.h / doc/BinaryDictionaryFormat.md).
// It mirrors the lookup API of CTableDictionaryEngine but performs zero
// parsing at load time and returns CStringRange views that point directly
// into the memory-mapped string pool (no per-entry string copies).


#pragma once

#include "BaseDictionaryEngine.h"
#include "BinaryDictFormat.h"

#include <map>
#include <string>

// Parsed view of the binary dictionary's config block (see BinaryDictFormat.h).
// Extend with typed fields as needed; `name` mirrors "#@NAME: <display name>"
// from the source dictionary header.
struct DictConfig
{
    std::wstring name;                          // dictionary display name
    std::map<std::wstring, std::wstring> raw;   // every KEY=value pair
};

class CBinaryDictionaryEngine : public CBaseDictionaryEngine
{
public:
    CBinaryDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile);
    virtual ~CBinaryDictionaryEngine();

    virtual VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings);
    virtual VOID CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);

    VOID CollectWordByPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);
    VOID CollectWordForWildcard(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount = 0, _Out_opt_ BOOL *pHasMore = nullptr);
    VOID CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList);
    BOOL FindCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode);

    BOOL IsBuilt() const { return _isBuilt; }

    // Config block accessors (the binary dictionary carries its own metadata).
    const DictConfig& GetConfig() const { return _config; }
    const std::wstring& GetName() const { return _config.name; }

    // Lightweight: map a .bin just long enough to read its config (NAME etc.).
    // Returns FALSE if the file is missing or the header is invalid.
    static BOOL ReadConfigFromFile(_In_z_ LPCWSTR binPath, _Out_ DictConfig& out);

    // "Only common characters" filter: when enabled, single characters outside
    // GB2312 (rare) are hidden; phrases (length > 1) are always shown.
    void SetOnlyCommon(BOOL f) { _onlyCommon = f; }
    BOOL IsOnlyCommon() const { return _onlyCommon; }

    // When only-common mode yields no candidates for a code, retry against the
    // full dictionary (空码检索全码表).
    void SetEmptyCodeSearchFull(BOOL f) { _emptyCodeSearchFull = f; }
    BOOL IsEmptyCodeSearchFull() const { return _emptyCodeSearchFull; }

private:
    const WCHAR* _PoolStr(uint32_t offset) const { return reinterpret_cast<const WCHAR*>(_pPool + offset); }
    const WCHAR* _CodeStr(const DimeBinDict::CodeEntry& ce) const { return _PoolStr(ce.codeOffset); }
    const WCHAR* _WordStr(const DimeBinDict::WordRef& wr) const { return _PoolStr(wr.wordOffset); }

    int _CompareCode(const DimeBinDict::CodeEntry& ce, const std::wstring& key) const;
    bool _StartsWith(const DimeBinDict::CodeEntry& ce, const std::wstring& prefix) const;
    bool _MatchesWildcard(const DimeBinDict::CodeEntry& ce, const std::wstring& pattern) const;
    bool _ReverseWordEqual(const DimeBinDict::ReverseEntry& re, _In_ const CStringRange *pWord) const;

    // True when a word should be shown under the current _onlyCommon setting.
    // Phrases (wordLen > 1) are always kept; single chars need a GB2312 level.
    bool _IsWordKept(const DimeBinDict::WordRef& wr) const;
    static bool _IsWildcardChar(WCHAR wch);
    std::wstring _ToUpperKey(_In_ const CStringRange *pKey) const;

    void _AppendCodeWords(_In_ const DimeBinDict::CodeEntry& ce,
                          _Inout_ CDIMEArray<CCandidateListItem> *pItemList,
                          UINT maxCount, _Inout_ UINT &collected, _Out_opt_ BOOL *pHasMore) const;

    const DimeBinDict::Header* _pHdr;
    const DimeBinDict::CodeEntry* _pCodeEntries;
    const DimeBinDict::WordRef* _pWordRefs;
    const DimeBinDict::ReverseEntry* _pReverseEntries;
    const BYTE* _pPool;
    BOOL _isBuilt;
    BOOL _onlyCommon;
    BOOL _emptyCodeSearchFull;
    DictConfig _config;
};
