// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT

// DIME precompiled binary dictionary format (.bin).
// Shared by the offline builder (src/build_bindict.cpp) and the runtime
// reader (BinaryDictionaryEngine). This header is the single source of truth
// for the on-disk layout; see doc/BinaryDictionaryFormat.md for the design.
//
// The header is intentionally dependency-light (only <cstdint>) so the offline
// tool can include it without pulling in the IME/TSF runtime headers.


#pragma once

#include <cstdint>

namespace DimeBinDict
{

// 'D','I','C','B' in little-endian byte order.
const uint32_t kMagic       = 0x44494342u;
const uint32_t kVersion     = 2u;
const uint32_t kFlagLittleEndian = 0x1u;

// Fixed strides (bytes) for each fixed-size array element.
const uint32_t kHeaderSize        = 64u;
const uint32_t kCodeEntryStride   = 16u;
const uint32_t kWordRefStride     = 8u;
const uint32_t kReverseEntryStride = 16u;

// Config block: a variable-length UTF-16LE text region at [kHeaderSize, codeEntryOffset).
// It holds zero or more "KEY:value\n" lines (no BOM). Its byte length is
// (codeEntryOffset - kHeaderSize), padded with NULs up to kConfigAlign so the
// following CodeEntry[] stays 4-byte aligned. Old loaders ignore this gap.
const uint32_t kConfigAlign = 4u;

// WordRef.reserved low 2 bits hold the GB2312 level of a single-character word:
//   0 = not in GB2312 (rare character or a multi-char phrase),
//   1 = GB2312 level-2 (次常用),
//   2 = GB2312 level-1 (最常用).
// Phrases (wordLen > 1) are never filtered, so their level is irrelevant.
const uint16_t kWordLevelMask = 0x0003;

// CodeEntry.reserved bit0: this code has at least one word that passes the
// "only common characters" filter (a phrase, or a single char in GB2312).
// Lets wildcard/prefix scans skip whole codes when the filter is active.
const uint32_t kCodeHasCommonBit = 0x00000001;

#pragma pack(push, 1)

// File header, exactly 64 bytes. All multi-byte fields are little-endian.
// All *Offset fields are absolute byte offsets from the start of the file.
struct Header
{
    uint32_t magic;              // 0  : kMagic
    uint32_t version;            // 4  : kVersion
    uint32_t flags;              // 8  : bit0 = little-endian (always 1)
    uint32_t sourceSize;         // 12 : low 32 bits of source .txt size in bytes
    uint32_t codeCount;          // 16 : number of CodeEntry records
    uint32_t wordPairCount;      // 20 : number of WordRef records
    uint32_t reverseCount;       // 24 : number of ReverseEntry records
    uint32_t sourceMtimeLow;     // 28 : source .txt ftLastWriteTime low 32 bits
    uint32_t codeEntryOffset;    // 32 : offset of CodeEntry[]
    uint32_t codeEntryStride;    // 36 : = kCodeEntryStride (16)
    uint32_t wordRefOffset;      // 40 : offset of WordRef[]
    uint32_t wordRefStride;      // 44 : = kWordRefStride (8)
    uint32_t reverseEntryOffset; // 48 : offset of ReverseEntry[]
    uint32_t reverseEntryStride; // 52 : = kReverseEntryStride (16)
    uint32_t stringPoolOffset;   // 56 : offset of the string pool
    uint32_t sourceMtimeHigh;    // 60 : source .txt ftLastWriteTime high 32 bits
};

// One record per unique code (keystroke key). Sorted ascending by the code
// string (uppercased, ordinal comparison).
struct CodeEntry
{
    uint32_t codeOffset;    // 0 : offset to first WCHAR of the code string
    uint32_t firstWordRef;  // 4 : index into WordRef[] of this code's first word
    uint16_t codeLen;       // 8 : code length in UTF-16 units
    uint16_t wordCount;     // 10: number of words for this code
    uint32_t reserved;      // 12: 0
};

// One record per (code, word) pair. WordRef[firstWordRef .. +wordCount)
// enumerates a code's words in source order.
struct WordRef
{
    uint32_t wordOffset;    // 0 : offset to first WCHAR of the word string
    uint16_t wordLen;       // 4 : word length in UTF-16 units
    uint16_t reserved;      // 6 : 0
};

// One record per unique word (candidate). Sorted ascending by the word string
// (ordinal comparison). Maps word -> shortest code.
struct ReverseEntry
{
    uint32_t wordOffset;    // 0 : offset to first WCHAR of the word string
    uint32_t codeOffset;    // 4 : offset to first WCHAR of the shortest code
    uint16_t wordLen;       // 8 : word length in UTF-16 units
    uint16_t codeLen;       // 10: code length in UTF-16 units
    uint32_t reserved;      // 12: 0
};

#pragma pack(pop)

static_assert(sizeof(Header) == 64, "DimeBinDict::Header must be 64 bytes");
static_assert(sizeof(CodeEntry) == 16, "DimeBinDict::CodeEntry must be 16 bytes");
static_assert(sizeof(WordRef) == 8, "DimeBinDict::WordRef must be 8 bytes");
static_assert(sizeof(ReverseEntry) == 16, "DimeBinDict::ReverseEntry must be 16 bytes");

// Lightweight header sanity check shared by the runtime loader. Verifies magic,
// version, endianness flag, stride fields, and that every declared section lies
// within [0, fileSize]. Does NOT validate source size/mtime (that is the
// caller's staleness check) nor the string pool contents.
inline bool ValidateHeader(const Header* h, uint64_t fileSize)
{
    if (!h)
    {
        return false;
    }
    if (fileSize < kHeaderSize)
    {
        return false;
    }
    if (h->magic != kMagic || h->version != kVersion)
    {
        return false;
    }
    if ((h->flags & kFlagLittleEndian) == 0)
    {
        return false;
    }
    if (h->codeEntryStride != kCodeEntryStride ||
        h->wordRefStride != kWordRefStride ||
        h->reverseEntryStride != kReverseEntryStride)
    {
        return false;
    }

    // Each section must fit within the file. Use 64-bit math to avoid overflow.
    const uint64_t codeBytes    = static_cast<uint64_t>(h->codeCount) * kCodeEntryStride;
    const uint64_t wordBytes    = static_cast<uint64_t>(h->wordPairCount) * kWordRefStride;
    const uint64_t reverseBytes = static_cast<uint64_t>(h->reverseCount) * kReverseEntryStride;

    // The config block sits between the Header and CodeEntry[], so CodeEntry
    // must never start before the header ends (a corrupt/zeroed offset would
    // overlap the header and the string pool would be misbased).
    if (h->codeEntryOffset < kHeaderSize)
    {
        return false;
    }
    if (static_cast<uint64_t>(h->codeEntryOffset) + codeBytes > fileSize)
    {
        return false;
    }
    if (static_cast<uint64_t>(h->wordRefOffset) + wordBytes > fileSize)
    {
        return false;
    }
    if (static_cast<uint64_t>(h->reverseEntryOffset) + reverseBytes > fileSize)
    {
        return false;
    }
    if (h->stringPoolOffset > fileSize)
    {
        return false;
    }

    return true;
}

} // namespace DimeBinDict
