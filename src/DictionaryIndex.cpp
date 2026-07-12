// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include "Private.h"
#include "DictionaryIndex.h"
#include "define.h"

#include <algorithm>

//+---------------------------------------------------------------------------
//
// ctor
//
//----------------------------------------------------------------------------

CDictionaryIndex::CDictionaryIndex(LCID locale)
    : CDictionaryParser(locale), _isBuilt(FALSE)
{
}

//+---------------------------------------------------------------------------
//
// Build
//
//----------------------------------------------------------------------------

BOOL CDictionaryIndex::Build(_In_ CFile *pFile)
{
    _isBuilt = FALSE;
    _exactMap.clear();
    _sortedKeys.clear();

    if (!pFile)
    {
        return FALSE;
    }

    const WCHAR *pwch = pFile->GetReadBufferPointer();
    if (!pwch)
    {
        return FALSE;
    }

    DWORD_PTR dwTotalBufLen = pFile->GetFileSize() / sizeof(WCHAR);
    DWORD_PTR indexTrace = 0;

    while (dwTotalBufLen > 0)
    {
        DWORD_PTR bufLenOneLine = GetOneLine(&pwch[indexTrace], dwTotalBufLen);

        if (bufLenOneLine > 0)
        {
            CParserStringRange keyword;
            CDIMEArray<CParserStringRange> valueStrings;
            if (ParseLine(&pwch[indexTrace], bufLenOneLine, &keyword, &valueStrings))
            {
            if (keyword.GetLength() > 0 && keyword.GetLength() <= WUBI_MAX_CODE_LENGTH && valueStrings.Count() > 0)
            {
                std::wstring key = _ToUpperKey(keyword.Get(), keyword.GetLength());
                std::vector<std::wstring> &entries = _exactMap[key];
                for (UINT i = 0; i < valueStrings.Count(); i++)
                {
                    CParserStringRange *pValue = valueStrings.GetAt(i);
                    if (pValue && pValue->GetLength() > 0)
                    {
                        std::wstring value(pValue->Get(), pValue->Get() + pValue->GetLength());
                        entries.emplace_back(value);

                        // Build reverse map (word -> wubi code). A word may map
                        // to several codes, so keep the shortest (most basic)
                        // one as the displayed encoding.
                        auto it = _reverseMap.find(value);
                        if (it == _reverseMap.end() || key.length() < it->second.length())
                        {
                            _reverseMap[value] = key;
                        }
                    }
                }
            }
            // Malformed or comment lines are skipped here; they must not abort
            // the whole index, otherwise the reverse-lookup map stays empty.
            }
        }

        dwTotalBufLen -= bufLenOneLine;
        if (dwTotalBufLen == 0)
        {
            break;
        }

        indexTrace += bufLenOneLine;
        if (pwch[indexTrace] == L'\r' || pwch[indexTrace] == L'\n' || pwch[indexTrace] == L'\0')
        {
            bufLenOneLine = 1;
            dwTotalBufLen -= bufLenOneLine;
            indexTrace += bufLenOneLine;
        }
    }

    _sortedKeys.reserve(_exactMap.size());
    for (const auto &entry : _exactMap)
    {
        _sortedKeys.push_back(entry.first);
    }
    std::sort(_sortedKeys.begin(), _sortedKeys.end());

    _isBuilt = !_exactMap.empty();
    return _isBuilt;
}

//+---------------------------------------------------------------------------
//
// LookupExact
//
//----------------------------------------------------------------------------

VOID CDictionaryIndex::LookupExact(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    if (!_isBuilt || !pKeyCode || !pItemList || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring key = _ToUpperKey(pKeyCode->Get(), pKeyCode->GetLength());
    auto it = _exactMap.find(key);
    if (it != _exactMap.end())
    {
        _AppendEntries(it->first, pItemList);
    }
}

VOID CDictionaryIndex::LookupExact(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
    if (!_isBuilt || !pKeyCode || !pWordStrings || pKeyCode->GetLength() == 0)
    {
        return;
    }

    std::wstring key = _ToUpperKey(pKeyCode->Get(), pKeyCode->GetLength());
    auto it = _exactMap.find(key);
    if (it != _exactMap.end())
    {
        _AppendEntries(it->first, pWordStrings);
    }
}

//+---------------------------------------------------------------------------
//
// LookupPrefix
//
//----------------------------------------------------------------------------

VOID CDictionaryIndex::LookupPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (pHasMore)
    {
        *pHasMore = FALSE;
    }

    if (!_isBuilt || !pKeyCode || !pItemList || pKeyCode->GetLength() == 0)
    {
        return;
    }

    UINT collected = 0;
    std::wstring prefix = _ToUpperKey(pKeyCode->Get(), pKeyCode->GetLength());
    auto it = std::lower_bound(_sortedKeys.begin(), _sortedKeys.end(), prefix);
    for (; it != _sortedKeys.end() && _KeyStartsWith(*it, prefix); ++it)
    {
        if (_AppendEntriesWithLimit(*it, pItemList, maxCount, collected))
        {
            if (pHasMore)
            {
                *pHasMore = TRUE;
            }
            return;
        }
    }
}

//+---------------------------------------------------------------------------
//
// LookupWildcard
//
//----------------------------------------------------------------------------

VOID CDictionaryIndex::LookupWildcard(_In_ CStringRange *pPattern, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (pHasMore)
    {
        *pHasMore = FALSE;
    }

    if (!_isBuilt || !pPattern || !pItemList || pPattern->GetLength() == 0)
    {
        return;
    }

    std::wstring pattern(pPattern->Get(), pPattern->Get() + pPattern->GetLength());
    for (auto &ch : pattern)
    {
        if (!_IsWildcardChar(ch))
        {
            ch = static_cast<WCHAR>(towupper(ch));
        }
    }

    size_t firstWildcard = std::wstring::npos;
    for (size_t i = 0; i < pattern.size(); i++)
    {
        if (_IsWildcardChar(pattern[i]))
        {
            firstWildcard = i;
            break;
        }
    }

    if (firstWildcard == std::wstring::npos)
    {
        LookupExact(pPattern, pItemList);
        return;
    }

    UINT collected = 0;
    std::wstring prefix = pattern.substr(0, firstWildcard);
    auto it = std::lower_bound(_sortedKeys.begin(), _sortedKeys.end(), prefix);
    for (; it != _sortedKeys.end() && _KeyStartsWith(*it, prefix); ++it)
    {
        if (_MatchesWildcardPattern(*it, pattern))
        {
            if (_AppendEntriesWithLimit(*it, pItemList, maxCount, collected))
            {
                if (pHasMore)
                {
                    *pHasMore = TRUE;
                }
                return;
            }
        }
    }
}

//+---------------------------------------------------------------------------
//
// helpers
//
//----------------------------------------------------------------------------

BOOL CDictionaryIndex::_IsWildcardChar(WCHAR wch)
{
    WCHAR upper = static_cast<WCHAR>(towupper(wch));
    return upper == L'?' || upper == towupper(WUBI_WILDCARD_CHAR);
}

std::wstring CDictionaryIndex::_ToUpperKey(_In_reads_(length) const WCHAR *pwch, DWORD_PTR length)
{
    std::wstring key(pwch, pwch + length);
    for (auto &ch : key)
    {
        ch = static_cast<WCHAR>(towupper(ch));
    }
    return key;
}

BOOL CDictionaryIndex::_KeyStartsWith(_In_ const std::wstring &key, _In_ const std::wstring &prefix)
{
    if (prefix.empty())
    {
        return TRUE;
    }
    if (key.size() < prefix.size())
    {
        return FALSE;
    }
    return key.compare(0, prefix.size(), prefix) == 0;
}

BOOL CDictionaryIndex::_MatchesWildcardPattern(_In_ const std::wstring &key, _In_ const std::wstring &pattern)
{
    if (key.size() != pattern.size())
    {
        return FALSE;
    }

    for (size_t i = 0; i < pattern.size(); i++)
    {
        if (!_IsWildcardChar(pattern[i]) && towupper(key[i]) != pattern[i])
        {
            return FALSE;
        }
    }

    return TRUE;
}

VOID CDictionaryIndex::_AppendEntries(_In_ const std::wstring &key, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    UINT collected = 0;
    _AppendEntriesWithLimit(key, pItemList, 0, collected);
}

BOOL CDictionaryIndex::_AppendEntriesWithLimit(_In_ const std::wstring &key, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Inout_ UINT &collected)
{
    auto it = _exactMap.find(key);
    if (it == _exactMap.end())
    {
        return FALSE;
    }

    for (const auto &word : it->second)
    {
        if (maxCount > 0 && collected >= maxCount)
        {
            return TRUE;
        }

        CCandidateListItem *pLI = pItemList->Append();
        if (pLI)
        {
            pLI->_ItemString.Set(word.c_str(), word.length());
            pLI->_FindKeyCode.Set(key.c_str(), key.length());
            collected++;
        }
    }

    return FALSE;
}

VOID CDictionaryIndex::_AppendEntries(_In_ const std::wstring &key, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
    auto it = _exactMap.find(key);
    if (it == _exactMap.end())
    {
        return;
    }

    for (const auto &word : it->second)
    {
        CStringRange *pPhrase = pWordStrings->Append();
        if (pPhrase)
        {
            pPhrase->Set(word.c_str(), word.length());
        }
    }
}

//+---------------------------------------------------------------------------
//
// LookupCodeByWord (reverse lookup)
//
//----------------------------------------------------------------------------

BOOL CDictionaryIndex::LookupCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode)
{
    if (!_isBuilt || !pWord || !pCode || pWord->GetLength() == 0)
    {
        return FALSE;
    }

    std::wstring value(pWord->Get(), pWord->Get() + pWord->GetLength());
    auto it = _reverseMap.find(value);
    if (it != _reverseMap.end())
    {
        pCode->Set(it->second.c_str(), it->second.length());
        return TRUE;
    }
    return FALSE;
}
