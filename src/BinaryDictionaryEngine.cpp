// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "BinaryDictionaryEngine.h"

#include <algorithm>
#include <map>
#include <string>

#include "define.h"

namespace
{

// Trim surrounding whitespace/CR/LF from a config token.
std::wstring _TrimConfigToken(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t' || s[e-1] == L'\r' || s[e-1] == L'\n')) --e;
    return s.substr(b, e - b);
}

// Parse the config block (UTF-16LE text at base+configOffset, configSize bytes)
// into out. Lines are "KEY:value"; the "#@" prefix is stripped by the builder.
// Unknown keys are preserved in out.raw; a parse failure is non-fatal.
void ParseConfig(const uint8_t* base, uint32_t configOffset, uint32_t configSize, DictConfig& out)
{
    if (configSize == 0 || (configSize % sizeof(wchar_t)) != 0)
    {
        return;
    }
    const wchar_t* p = reinterpret_cast<const wchar_t*>(base + configOffset);
    size_t chars = configSize / sizeof(wchar_t);

    auto flush = [&](const std::wstring& line)
    {
        if (line.empty())
        {
            return;
        }
        size_t colon = line.find(L':');
        if (colon == std::wstring::npos)
        {
            return;
        }
        std::wstring key = _TrimConfigToken(line.substr(0, colon));
        std::wstring val = _TrimConfigToken(line.substr(colon + 1));
        if (key.empty())
        {
            return;
        }
        out.raw[key] = val;
        if (key == L"NAME")
        {
            out.name = val;
        }
    };

    std::wstring line;
    for (size_t i = 0; i < chars; ++i)
    {
        wchar_t c = p[i];
        if (c == L'\r' || c == L'\n' || c == L'\0')   // NUL = alignment padding
        {
            flush(line);
            line.clear();
        }
        else
        {
            line.push_back(c);
        }
    }
    flush(line);   // trailing line without a terminator
}

BOOL ReadConfigFromFileImpl(_In_z_ LPCWSTR binPath, _Out_ DictConfig& out)
{
    out = DictConfig();
    if (!binPath || binPath[0] == L'\0')
    {
        return FALSE;
    }

    HANDLE hFile = CreateFileW(binPath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < (LONGLONG)DimeBinDict::kHeaderSize)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hMap)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    const BYTE* base = static_cast<const BYTE*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
    if (!base)
    {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return FALSE;
    }

    BOOL ok = FALSE;
    const DimeBinDict::Header* pHdr = reinterpret_cast<const DimeBinDict::Header*>(base);
    if (DimeBinDict::ValidateHeader(pHdr, static_cast<uint64_t>(fileSize.QuadPart)))
    {
        uint32_t configSize = pHdr->codeEntryOffset - DimeBinDict::kHeaderSize;
        if (configSize > 0)
        {
            ParseConfig(base, DimeBinDict::kHeaderSize, configSize, out);
        }
        ok = TRUE;
    }

    UnmapViewOfFile(base);
    CloseHandle(hMap);
    CloseHandle(hFile);
    return ok;
}

} // namespace

BOOL CBinaryDictionaryEngine::ReadConfigFromFile(_In_z_ LPCWSTR binPath, _Out_ DictConfig& out)
{
    return ReadConfigFromFileImpl(binPath, out);
}

//+---------------------------------------------------------------------------
//
// ctor / dtor
//
//----------------------------------------------------------------------------

CBinaryDictionaryEngine::CBinaryDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile)
    : CBaseDictionaryEngine(locale, pDictionaryFile),
      _pHdr(nullptr),
      _pCodeEntries(nullptr),
      _pWordRefs(nullptr),
      _pReverseEntries(nullptr),
      _pPool(nullptr),
      _isBuilt(FALSE),
      _onlyCommon(FALSE),
      _emptyCodeSearchFull(TRUE)
{
    if (!pDictionaryFile)
    {
        return;
    }

    const BYTE* base = reinterpret_cast<const BYTE*>(pDictionaryFile->GetReadBufferPointer());
    if (!base)
    {
        return;
    }

    const DimeBinDict::Header* pHdr = reinterpret_cast<const DimeBinDict::Header*>(base);
    if (!DimeBinDict::ValidateHeader(pHdr, pDictionaryFile->GetFileSize()))
    {
        return;
    }

    _pHdr = pHdr;
    _pCodeEntries = reinterpret_cast<const DimeBinDict::CodeEntry*>(base + _pHdr->codeEntryOffset);
    _pWordRefs = reinterpret_cast<const DimeBinDict::WordRef*>(base + _pHdr->wordRefOffset);
    _pReverseEntries = reinterpret_cast<const DimeBinDict::ReverseEntry*>(base + _pHdr->reverseEntryOffset);
    // String offsets stored in the file are absolute (from file start), so the
    // pool base must be the file base; _PoolStr(offset) = base + offset.
    _pPool = base;
    _isBuilt = TRUE;

    // Parse the config block (Header .. codeEntryOffset); absent in older bins.
    uint32_t configSize = _pHdr->codeEntryOffset - DimeBinDict::kHeaderSize;
    if (configSize > 0)
    {
        ParseConfig(base, DimeBinDict::kHeaderSize, configSize, _config);
    }
}

CBinaryDictionaryEngine::~CBinaryDictionaryEngine()
{
    // This engine owns the mapped .bin file passed in as its dictionary file.
    if (_pDictionaryFile)
    {
        delete _pDictionaryFile;
        _pDictionaryFile = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// helpers
//
//----------------------------------------------------------------------------

int CBinaryDictionaryEngine::_CompareCode(const DimeBinDict::CodeEntry& ce, const std::wstring& key) const
{
    const WCHAR* a = _CodeStr(ce);
    size_t n = (std::min)(static_cast<size_t>(ce.codeLen), key.size());
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != key[i])
        {
            return a[i] < key[i] ? -1 : 1;
        }
    }
    if (ce.codeLen != key.size())
    {
        return (ce.codeLen < key.size()) ? -1 : 1;
    }
    return 0;
}

bool CBinaryDictionaryEngine::_StartsWith(const DimeBinDict::CodeEntry& ce, const std::wstring& prefix) const
{
    if (ce.codeLen < prefix.size())
    {
        return false;
    }
    const WCHAR* a = _CodeStr(ce);
    for (size_t i = 0; i < prefix.size(); i++)
    {
        if (a[i] != prefix[i])
        {
            return false;
        }
    }
    return true;
}

bool CBinaryDictionaryEngine::_MatchesWildcard(const DimeBinDict::CodeEntry& ce, const std::wstring& pattern) const
{
    // Mirrors CDictionaryIndex::_MatchesWildcardPattern: fixed length, wildcard
    // positions (Z / ?) match any character.
    if (ce.codeLen != pattern.size())
    {
        return false;
    }
    const WCHAR* a = _CodeStr(ce);
    for (size_t i = 0; i < pattern.size(); i++)
    {
        if (!_IsWildcardChar(pattern[i]) && a[i] != pattern[i])
        {
            return false;
        }
    }
    return true;
}

bool CBinaryDictionaryEngine::_ReverseWordEqual(const DimeBinDict::ReverseEntry& re, _In_ const CStringRange *pWord) const
{
    if (re.wordLen != pWord->GetLength())
    {
        return false;
    }
    const WCHAR* a = _PoolStr(re.wordOffset);
    const WCHAR* b = pWord->Get();
    for (DWORD_PTR i = 0; i < pWord->GetLength(); i++)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

// Wildcard char set matches CDictionaryIndex::LookupWildcard (uppercase '?' or 'Z').
bool CBinaryDictionaryEngine::_IsWildcardChar(WCHAR wch)
{
    WCHAR upper = static_cast<WCHAR>(towupper(wch));
    return upper == L'?' || upper == towupper(WUBI_WILDCARD_CHAR);
}

bool CBinaryDictionaryEngine::_IsWordKept(const DimeBinDict::WordRef& wr) const
{
    if (!_onlyCommon)
    {
        return true;
    }
    if (wr.wordLen > 1)
    {
        return true;            // 词组照常显示
    }
    int level = wr.reserved & DimeBinDict::kWordLevelMask;
    return level >= 1;          // 单字需属于 GB2312
}

std::wstring CBinaryDictionaryEngine::_ToUpperKey(_In_ const CStringRange *pKey) const
{
    std::wstring key(pKey->Get(), pKey->Get() + pKey->GetLength());
    for (auto& c : key)
    {
        c = static_cast<WCHAR>(towupper(c));
    }
    return key;
}

void CBinaryDictionaryEngine::_AppendCodeWords(_In_ const DimeBinDict::CodeEntry& ce,
                                               _Inout_ CDIMEArray<CCandidateListItem> *pItemList,
                                               UINT maxCount, _Inout_ UINT &collected, _Out_opt_ BOOL *pHasMore) const
{
    for (UINT i = 0; i < ce.wordCount; i++)
    {
        const DimeBinDict::WordRef& wr = _pWordRefs[ce.firstWordRef + i];
        if (_onlyCommon && !_IsWordKept(wr))
        {
            continue;
        }
        if (maxCount > 0 && collected >= maxCount)
        {
            if (pHasMore)
            {
                *pHasMore = TRUE;
            }
            return;
        }
        CCandidateListItem* pLI = pItemList->Append();
        if (pLI)
        {
            pLI->_ItemString.Set(_WordStr(wr), wr.wordLen);
            pLI->_FindKeyCode.Set(_CodeStr(ce), ce.codeLen);
            collected++;
        }
    }
}

//+---------------------------------------------------------------------------
//
// CollectWord (exact) -> CCandidateListItem
//
//----------------------------------------------------------------------------

VOID CBinaryDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    if (!_isBuilt || !pItemList || !pKeyCode || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring key = _ToUpperKey(pKeyCode);
    auto first = _pCodeEntries;
    auto last = _pCodeEntries + _pHdr->codeCount;
    auto it = std::lower_bound(first, last, key,
        [this](const DimeBinDict::CodeEntry& ce, const std::wstring& k) { return _CompareCode(ce, k) < 0; });
    if (it != last && _CompareCode(*it, key) == 0)
    {
        UINT collected = 0;
        _AppendCodeWords(*it, pItemList, 0, collected, nullptr);
    }

    // 常用字模式下空码: 回退检索全码表.
    if (pItemList->Count() == 0 && _onlyCommon && _emptyCodeSearchFull)
    {
        _onlyCommon = FALSE;
        CollectWord(pKeyCode, pItemList);
        _onlyCommon = TRUE;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWord (exact) -> CStringRange (word strings only)
//
//----------------------------------------------------------------------------

VOID CBinaryDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
    if (!_isBuilt || !pWordStrings || !pKeyCode || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring key = _ToUpperKey(pKeyCode);
    auto first = _pCodeEntries;
    auto last = _pCodeEntries + _pHdr->codeCount;
    auto it = std::lower_bound(first, last, key,
        [this](const DimeBinDict::CodeEntry& ce, const std::wstring& k) { return _CompareCode(ce, k) < 0; });
    if (it != last && _CompareCode(*it, key) == 0)
    {
        for (UINT i = 0; i < it->wordCount; i++)
        {
            const DimeBinDict::WordRef& wr = _pWordRefs[it->firstWordRef + i];
            CStringRange* p = pWordStrings->Append();
            if (p)
            {
                p->Set(_WordStr(wr), wr.wordLen);
            }
        }
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordByPrefix
//
//----------------------------------------------------------------------------

VOID CBinaryDictionaryEngine::CollectWordByPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (pHasMore)
    {
        *pHasMore = FALSE;
    }
    if (!_isBuilt || !pItemList || !pKeyCode || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring prefix = _ToUpperKey(pKeyCode);
    auto first = _pCodeEntries;
    auto last = _pCodeEntries + _pHdr->codeCount;
    auto it = std::lower_bound(first, last, prefix,
        [this](const DimeBinDict::CodeEntry& ce, const std::wstring& k) { return _CompareCode(ce, k) < 0; });

    UINT collected = 0;
    for (; it != last; ++it)
    {
        if (!_StartsWith(*it, prefix))
        {
            break;
        }
        if (_onlyCommon && !(it->reserved & DimeBinDict::kCodeHasCommonBit))
        {
            continue;
        }
        _AppendCodeWords(*it, pItemList, maxCount, collected, pHasMore);
        if (pHasMore && *pHasMore)
        {
            return;
        }
    }

    // 常用字模式下空码: 回退检索全码表.
    if (pItemList->Count() == 0 && _onlyCommon && _emptyCodeSearchFull)
    {
        _onlyCommon = FALSE;
        CollectWordByPrefix(pKeyCode, pItemList, maxCount, pHasMore);
        _onlyCommon = TRUE;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordForWildcard
//
//----------------------------------------------------------------------------

VOID CBinaryDictionaryEngine::CollectWordForWildcard(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (pHasMore)
    {
        *pHasMore = FALSE;
    }
    if (!_isBuilt || !pItemList || !pKeyCode || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring pattern = _ToUpperKey(pKeyCode);

    size_t firstWildcard = std::wstring::npos;
    for (size_t i = 0; i < pattern.size(); i++)
    {
        if (_IsWildcardChar(pattern[i]))
        {
            firstWildcard = i;
            break;
        }
    }

    // No wildcard char -> exact lookup.
    if (firstWildcard == std::wstring::npos)
    {
        CollectWord(pKeyCode, pItemList);
        return;
    }

    std::wstring prefix = pattern.substr(0, firstWildcard);
    auto first = _pCodeEntries;
    auto last = _pCodeEntries + _pHdr->codeCount;
    auto it = std::lower_bound(first, last, prefix,
        [this](const DimeBinDict::CodeEntry& ce, const std::wstring& k) { return _CompareCode(ce, k) < 0; });

    UINT collected = 0;
    for (; it != last; ++it)
    {
        if (!_StartsWith(*it, prefix))
        {
            break;
        }
        if (_onlyCommon && !(it->reserved & DimeBinDict::kCodeHasCommonBit))
        {
            continue;
        }
        if (_MatchesWildcard(*it, pattern))
        {
            _AppendCodeWords(*it, pItemList, maxCount, collected, pHasMore);
            if (pHasMore && *pHasMore)
            {
                return;
            }
        }
    }

    // 常用字模式下空码: 回退检索全码表.
    if (pItemList->Count() == 0 && _onlyCommon && _emptyCodeSearchFull)
    {
        _onlyCommon = FALSE;
        CollectWordForWildcard(pKeyCode, pItemList, maxCount, pHasMore);
        _onlyCommon = TRUE;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordFromConvertedStringForWildcard
//
//   The caller appends a single '*' wildcard; strip it and treat the rest as
//   a prefix lookup (matching CDictionarySearch::FindConvertedStringForWildcard
//   where '*' matches any suffix).
//
//----------------------------------------------------------------------------

VOID CBinaryDictionaryEngine::CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    if (!_isBuilt || !pItemList || !pString || pString->GetLength() == 0)
    {
        return;
    }

    std::wstring prefix = _ToUpperKey(pString);
    while (!prefix.empty() && prefix.back() == L'*')
    {
        prefix.pop_back();
    }
    if (prefix.empty())
    {
        return;
    }

    auto first = _pCodeEntries;
    auto last = _pCodeEntries + _pHdr->codeCount;
    auto it = std::lower_bound(first, last, prefix,
        [this](const DimeBinDict::CodeEntry& ce, const std::wstring& k) { return _CompareCode(ce, k) < 0; });

    for (; it != last; ++it)
    {
        if (!_StartsWith(*it, prefix))
        {
            break;
        }
        if (_onlyCommon && !(it->reserved & DimeBinDict::kCodeHasCommonBit))
        {
            continue;
        }
        UINT collected = 0;
        BOOL hasMore = FALSE;
        _AppendCodeWords(*it, pItemList, 0, collected, &hasMore);
    }

    // 常用字模式下空码: 回退检索全码表.
    if (pItemList->Count() == 0 && _onlyCommon && _emptyCodeSearchFull)
    {
        _onlyCommon = FALSE;
        CollectWordFromConvertedStringForWildcard(pString, pItemList);
        _onlyCommon = TRUE;
    }
}

//+---------------------------------------------------------------------------
//
// FindCodeByWord (reverse lookup)
//
//----------------------------------------------------------------------------

BOOL CBinaryDictionaryEngine::FindCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode)
{
    if (!_isBuilt || !pWord || !pCode || pWord->GetLength() == 0)
    {
        return FALSE;
    }

    // ReverseEntry[] is sorted by word string (ordinal). Binary search.
    size_t lo = 0, hi = _pHdr->reverseCount;
    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        const DimeBinDict::ReverseEntry& re = _pReverseEntries[mid];
        int cmp = 0;
        const WCHAR* a = _PoolStr(re.wordOffset);
        DWORD_PTR n = (std::min)(static_cast<DWORD_PTR>(re.wordLen), pWord->GetLength());
        for (DWORD_PTR i = 0; i < n; i++)
        {
            if (a[i] != pWord->Get()[i])
            {
                cmp = a[i] < pWord->Get()[i] ? -1 : 1;
                break;
            }
        }
        if (cmp == 0 && re.wordLen != pWord->GetLength())
        {
            cmp = (re.wordLen < pWord->GetLength()) ? -1 : 1;
        }
        if (cmp < 0)
        {
            lo = mid + 1;
        }
        else if (cmp > 0)
        {
            hi = mid;
        }
        else
        {
            pCode->Set(_PoolStr(re.codeOffset), re.codeLen);
            return TRUE;
        }
    }
    return FALSE;
}
