// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "TableDictionaryEngine.h"
#include "DictionaryIndex.h"
#include "DictionarySearch.h"
#include "BinaryDictionaryEngine.h"

//+---------------------------------------------------------------------------
//
// ctor / dtor
//
//----------------------------------------------------------------------------

CTableDictionaryEngine::CTableDictionaryEngine(LCID locale, _In_ CFile *pDictionaryFile, _In_opt_ CBinaryDictionaryEngine *pBinary)
    : CBaseDictionaryEngine(locale, pDictionaryFile), _pIndex(nullptr), _pBinary(pBinary), _onlyCommon(FALSE)
{
    // When a precompiled binary reader is supplied, skip the in-memory index
    // build entirely. Otherwise build the text index as before.
    if (_pBinary)
    {
        return;
    }
    _pIndex = new (std::nothrow) CDictionaryIndex(locale);
    if (_pIndex)
    {
        _pIndex->Build(pDictionaryFile);
    }
}

CTableDictionaryEngine::~CTableDictionaryEngine()
{
    if (_pBinary)
    {
        delete _pBinary;
        _pBinary = nullptr;
    }
    if (_pIndex)
    {
        delete _pIndex;
        _pIndex = nullptr;
    }
}

void CTableDictionaryEngine::SetOnlyCommon(BOOL f)
{
    _onlyCommon = f;
    if (_pBinary)
    {
        _pBinary->SetOnlyCommon(f);
    }
}

void CTableDictionaryEngine::SetEmptyCodeSearchFull(BOOL f)
{
    if (_pBinary)
    {
        _pBinary->SetEmptyCodeSearchFull(f);
    }
}

BOOL CTableDictionaryEngine::IsEmptyCodeSearchFull() const
{
    return _pBinary ? _pBinary->IsEmptyCodeSearchFull() : FALSE;
}

//+---------------------------------------------------------------------------
//
// CollectWord
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CStringRange> *pWordStrings)
{
    if (_pBinary)
    {
        _pBinary->CollectWord(pKeyCode, pWordStrings);
        return;
    }

    if (_pIndex && _pIndex->IsBuilt())
    {
        _pIndex->LookupExact(pKeyCode, pWordStrings);
        return;
    }

    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode);

    while (dshSearch.FindPhrase(&pdret))
    {
        for (UINT index = 0; index < pdret->_FindPhraseList.Count(); index++)
        {
            CStringRange* pPhrase = nullptr;
            pPhrase = pWordStrings->Append();
            if (pPhrase)
            {
                *pPhrase = *pdret->_FindPhraseList.GetAt(index);
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// FindCodeByWord (reverse lookup)
//
//----------------------------------------------------------------------------

BOOL CTableDictionaryEngine::FindCodeByWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode)
{
    if (_pBinary)
    {
        return _pBinary->FindCodeByWord(pWord, pCode);
    }
    if (_pIndex && _pIndex->IsBuilt())
    {
        return _pIndex->LookupCodeByWord(pWord, pCode);
    }
    return FALSE;
}

VOID CTableDictionaryEngine::CollectWord(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    if (_pBinary)
    {
        _pBinary->CollectWord(pKeyCode, pItemList);
        return;
    }

    if (_pIndex && _pIndex->IsBuilt())
    {
        _pIndex->LookupExact(pKeyCode, pItemList);
        return;
    }

    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode);

    while (dshSearch.FindPhrase(&pdret))
    {
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordForWildcard
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordForWildcard(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (_pBinary)
    {
        _pBinary->CollectWordForWildcard(pKeyCode, pItemList, maxCount, pHasMore);
        return;
    }

    if (_pIndex && _pIndex->IsBuilt())
    {
        _pIndex->LookupWildcard(pKeyCode, pItemList, maxCount, pHasMore);
        return;
    }

    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode);

    while (dshSearch.FindPhraseForWildcard(&pdret))
    {
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordByPrefix
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordByPrefix(_In_ CStringRange *pKeyCode, _Inout_ CDIMEArray<CCandidateListItem> *pItemList, UINT maxCount, _Out_opt_ BOOL *pHasMore)
{
    if (_pBinary)
    {
        _pBinary->CollectWordByPrefix(pKeyCode, pItemList, maxCount, pHasMore);
        return;
    }

    if (_pIndex && _pIndex->IsBuilt())
    {
        _pIndex->LookupPrefix(pKeyCode, pItemList, maxCount, pHasMore);
        return;
    }

    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pKeyCode);

    while (dshSearch.FindPhraseForPrefix(&pdret))
    {
        for (UINT iIndex = 0; iIndex < pdret->_FindPhraseList.Count(); iIndex++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(iIndex));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}

//+---------------------------------------------------------------------------
//
// CollectWordFromConvertedStringForWildcard
//
//----------------------------------------------------------------------------

VOID CTableDictionaryEngine::CollectWordFromConvertedStringForWildcard(_In_ CStringRange *pString, _Inout_ CDIMEArray<CCandidateListItem> *pItemList)
{
    if (_pBinary)
    {
        _pBinary->CollectWordFromConvertedStringForWildcard(pString, pItemList);
        return;
    }

    CDictionaryResult* pdret = nullptr;
    CDictionarySearch dshSearch(_locale, _pDictionaryFile, pString);

    while (dshSearch.FindConvertedStringForWildcard(&pdret))
    {
        for (UINT index = 0; index < pdret->_FindPhraseList.Count(); index++)
        {
            CCandidateListItem* pLI = nullptr;
            pLI = pItemList->Append();
            if (pLI)
            {
                pLI->_ItemString.Set(*pdret->_FindPhraseList.GetAt(index));
                pLI->_FindKeyCode.Set(pdret->_FindKeyCode.Get(), pdret->_FindKeyCode.GetLength());
            }
        }

        delete pdret;
        pdret = nullptr;
    }
}


