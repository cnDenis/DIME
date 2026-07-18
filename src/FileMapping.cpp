// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#include "Private.h"
#include "FileMapping.h"
#include "Globals.h"

//---------------------------------------------------------------------
//
// ctor
//
//---------------------------------------------------------------------

CFileMapping::CFileMapping() : CFile()
{
    _fileMappingHandle = nullptr;
    _pMapBuffer = nullptr;
    _isRawMode = FALSE;
}

//---------------------------------------------------------------------
//
// dtor
//
//---------------------------------------------------------------------

CFileMapping::~CFileMapping()
{
    if (_pMapBuffer)
    {
        UnmapViewOfFile(_pMapBuffer);
        _pMapBuffer = nullptr;
        _pReadBuffer = nullptr;
    }
    if (_fileMappingHandle)
    {
        CloseHandle(_fileMappingHandle);
        _fileMappingHandle = nullptr;
    }
}

//---------------------------------------------------------------------
//
// SetupReadBuffer
//
//---------------------------------------------------------------------

BOOL CFileMapping::SetupReadBuffer()
{
    if (_isRawMode)
    {
        // Binary precompiled dictionary: map as-is, no Unicode/BOM handling.
        if (_fileSize == 0)
        {
            return FALSE;
        }
        _fileMappingHandle = CreateFileMapping(_fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
        if (_fileMappingHandle)
        {
            _pMapBuffer = MapViewOfFile(_fileMappingHandle, FILE_MAP_READ, 0, 0, 0);
            if (_pMapBuffer)
            {
                // Treat the base as a WCHAR* view; callers cast to bytes as needed.
                _pReadBuffer = static_cast<const WCHAR*>(_pMapBuffer);
                return TRUE;
            }
            CloseHandle(_fileMappingHandle);
            _fileMappingHandle = nullptr;
        }
        return FALSE;
    }

    if (_fileSize > sizeof(WCHAR))
    {
        //
        // Read file in file mapping
        //
        _fileMappingHandle = CreateFileMapping(_fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
        if (_fileMappingHandle)
        {
            _pMapBuffer = (const WCHAR *)MapViewOfFile(_fileMappingHandle, FILE_MAP_READ, 0, 0, 0);
            if (_pMapBuffer)
            {
                if (IsTextUnicode(_pMapBuffer, (int)_fileSize, NULL))
                {
                    _pReadBuffer = (WCHAR*)_pMapBuffer;

                    // skip Unicode byte order mark
                    if (*((WCHAR*)_pMapBuffer) == Global::UnicodeByteOrderMark)
                    {
                        _pReadBuffer++;
                        _fileSize--;
                    }
                    return TRUE;
                }

                UnmapViewOfFile(_pReadBuffer);
                _pReadBuffer = nullptr;
            }

            CloseHandle(_fileMappingHandle);
            _fileMappingHandle = nullptr;
        }
    }

    return FALSE;
}
