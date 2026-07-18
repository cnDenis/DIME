// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT
//
// DIMEStructureArray.h — CDIMEStructureArray declaration.

#pragma once

#include <vector>

template<class T>
class CDIMEStructureArray
{
    typedef typename    std::vector<T>         value_type;
    typedef const       T&                     CONST_REF;
    typedef typename    value_type             CDIMEArray;
    typedef typename    value_type::iterator   CDIMEIter;

public:
    CDIMEStructureArray(): _imeVector()
    {
    }

    explicit CDIMEStructureArray(size_t iCount): _imeVector(iCount)
    {
    }

    CDIMEStructureArray(size_t iCount, CONST_REF tVal): _imeVector(iCount, tVal)
    {
    }

    virtual ~CDIMEStructureArray() {}

    inline CONST_REF GetAt(size_t iIndex) const
    {
        assert(iIndex <= _imeVector.size());
        assert(_imeVector.size() > 0);

        return _imeVector[iIndex];
    }

    inline T& GetAt(size_t iIndex)
    {
        assert(iIndex <= _imeVector.size());
        assert(_imeVector.size() > 0);

        return _imeVector[iIndex];
    }

    void RemoveAt(size_t iIndex, size_t iElements)
    {
        assert(iIndex <= _imeVector.size());
        assert(_imeVector.size() > 0);

        CDIMEIter beginIter = _imeVector.begin() + iIndex;
        CDIMEIter lastIter = beginIter + iElements - 1;

        _imeVector.erase(beginIter, lastIter);
    }

    size_t Count() const { return _imeVector.size(); }

    void Append(const T& tVal)
    {
        _imeVector.push_back(tVal);
    }

    void Clear()
    {
        _imeVector.clear();
    }

private:
    CDIMEArray _imeVector;   // the actual array of data
    CDIMEIter  _imeIter;
};
