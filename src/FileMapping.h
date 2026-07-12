// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#pragma once

#include "File.h"

class CFileMapping : public CFile
{
public:
    CFileMapping();
    virtual ~CFileMapping();

    BOOL CreateFile(_In_ PCWSTR pFileName, DWORD desiredAccess, DWORD creationDisposition,
        DWORD sharedMode = 0, _Inout_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr, DWORD flagsAndAttributes = 0, _Inout_opt_ HANDLE templateFileHandle = nullptr)

    {
        return CFile::CreateFile(pFileName, desiredAccess, creationDisposition,
            sharedMode, lpSecurityAttributes, flagsAndAttributes, templateFileHandle);
    }

    BOOL IsEndOfFile()
    {
        return CFile::IsEndOfFile();
    }
    VOID NextLine()
    {
        CFile::NextLine();
    }

    const WCHAR *GetReadBufferPointer() { return CFile::GetReadBufferPointer(); }
    DWORD_PTR GetFileSize() { return CFile::GetFileSize(); }

    LPCWSTR GetFileName() { return CFile::GetFileName(); }

    // When set, SetupReadBuffer maps the file as-is (binary) instead of
    // requiring a Unicode text BOM. Used for the precompiled .bin dictionary.
    void SetRawMode(BOOL raw) { _isRawMode = raw; }

protected:
    BOOL SetupReadBuffer();

private:
    HANDLE _fileMappingHandle;  // file handle for CreateFileMapping
    const VOID *_pMapBuffer;    // read buffer memory.
    BOOL _isRawMode;            // map file as binary (no Unicode/BOM handling)
};
