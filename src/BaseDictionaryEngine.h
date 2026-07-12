// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved


#pragma once

#include "File.h"
#include "DIMEBaseStructure.h"

class CBaseDictionaryEngine
{
public:
    CBaseDictionaryEngine(LCID locale, _In_opt_ CFile *pDictionaryFile);
    virtual ~CBaseDictionaryEngine();

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CDIMEArray<CStringRange> *pasrgWordString)
    { 
        psrgKeyCode; 
        pasrgWordString = nullptr;
    }

    virtual VOID CollectWord(_In_ CStringRange *psrgKeyCode, _Out_ CDIMEArray<CCandidateListItem> *pItemList)
    { 
        psrgKeyCode;
        pItemList = nullptr;
    }

    virtual VOID SortListItemByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList);

protected:
    CFile* _pDictionaryFile;
    LCID _locale;

private:
    VOID MergeSortByFindKeyCode(_Inout_ CDIMEArray<CCandidateListItem> *pItemList, int leftRange, int rightRange);
    int CalculateCandidateCount(int leftRange,  int rightRange);
};
