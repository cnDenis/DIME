// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT

// build_bindict: offline compiler that turns a DIME text dictionary
// ("code"="word", UTF-16LE) into the precompiled binary format (.bin).
//
//   build_bindict.exe <in.txt> <out.bin> [--max-code N] [--verify]
//   build_bindict.exe                      (no args: compile every Dictionary\*.txt)
//
// In no-argument mode the tool scans the source dictionary folder (Dictionary)
// for all *.txt files and writes a sibling <name>.bin next to each (pinyin
// dictionaries get --max-code 24 automatically).
//
// See doc/BinaryDictionaryFormat.md and BinaryDictFormat.h for the layout.
// Parsing intentionally mirrors CDictionaryParser::ParseLine and
// CDictionaryIndex::Build so the produced .bin matches the in-memory index.


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "BinaryDictFormat.h"

namespace
{

using std::wstring;

// Decode a multi-byte buffer into UTF-16LE. Returns false only when 'strict'
// is set and the input is not valid for the given codepage.
static bool DecodeMB(const char* src, size_t len, UINT cp, std::wstring& out, bool strict = false)
{
    if (len == 0)
    {
        out.clear();
        return true;
    }
    int needed = MultiByteToWideChar(cp, strict ? MB_ERR_INVALID_CHARS : 0,
                                     src, static_cast<int>(len), nullptr, 0);
    if (needed == 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(needed));
    int got = MultiByteToWideChar(cp, strict ? MB_ERR_INVALID_CHARS : 0,
                                  src, static_cast<int>(len), &out[0], needed);
    if (got == 0)
    {
        return false;
    }
    return true;
}

// Read the whole input file and decode it into a UTF-16LE std::wstring.
// Supported encodings: UTF-16LE/BE (BOM), UTF-8 (BOM or no BOM), and, as a
// last resort for legacy files without a BOM, the system ANSI codepage
// (GBK on Chinese locales). 'fileSize'/'lastWrite' are captured for the
// staleness fields in the binary header.
bool ReadSourceFile(const wchar_t* path,
                    std::wstring& text,
                    uint64_t& fileSize,
                    FILETIME& lastWrite)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"[ERROR] cannot open input: %s (err %lu)\n", path, GetLastError());
        return false;
    }

    LARGE_INTEGER li = {};
    if (!GetFileSizeEx(h, &li))
    {
        fwprintf(stderr, L"[ERROR] GetFileSizeEx failed (err %lu)\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    fileSize = static_cast<uint64_t>(li.QuadPart);

    FILETIME created = {}, accessed = {};
    if (!GetFileTime(h, &created, &accessed, &lastWrite))
    {
        fwprintf(stderr, L"[ERROR] GetFileTime failed (err %lu)\n", GetLastError());
        CloseHandle(h);
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    uint64_t total = 0;
    while (total < fileSize)
    {
        DWORD toRead = static_cast<DWORD>((fileSize - total > 0x10000000ull)
                                              ? 0x10000000ull
                                              : (fileSize - total));
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + total, toRead, &got, nullptr) || got == 0)
        {
            fwprintf(stderr, L"[ERROR] ReadFile failed (err %lu)\n", GetLastError());
            CloseHandle(h);
            return false;
        }
        total += got;
    }
    CloseHandle(h);

    const uint8_t* p = bytes.data();
    const size_t n = bytes.size();

    // --- Encoding detection: BOM first, then content sniffing. ---
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)            // UTF-16LE
    {
        text.assign(reinterpret_cast<const wchar_t*>(p + 2), (n - 2) / sizeof(wchar_t));
        return true;
    }
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)            // UTF-16BE -> byte-swap
    {
        std::wstring tmp(reinterpret_cast<const wchar_t*>(p + 2), (n - 2) / sizeof(wchar_t));
        for (wchar_t& c : tmp)
        {
            c = static_cast<wchar_t>((c >> 8) | (c << 8));
        }
        text = std::move(tmp);
        return true;
    }
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)   // UTF-8 (BOM)
    {
        DecodeMB(reinterpret_cast<const char*>(p + 3), n - 3, CP_UTF8, text);
        return true;
    }
    // No BOM: try UTF-8 (strict); on failure fall back to system ANSI/GBK.
    if (DecodeMB(reinterpret_cast<const char*>(p), n, CP_UTF8, text, true))
    {
        return true;
    }
    DecodeMB(reinterpret_cast<const char*>(p), n, CP_ACP, text);
    return true;
}

inline bool IsSpaceCh(wchar_t c)
{
    return c == L' ' || c == L'\t';
}

// Trim leading/trailing spaces, tabs, CR and LF (mirrors the parser's
// RemoveWhiteSpaceFromBegin/End behavior), then strip one matching pair of
// surrounding double quotes (RemoveStringDelimiter).
wstring TrimAndUnquote(const wchar_t* p, size_t len)
{
    size_t b = 0, e = len;
    while (b < e && (IsSpaceCh(p[b]) || p[b] == L'\r' || p[b] == L'\n'))
    {
        ++b;
    }
    while (e > b && (IsSpaceCh(p[e - 1]) || p[e - 1] == L'\r' || p[e - 1] == L'\n'))
    {
        --e;
    }
    if (e - b >= 2 && p[b] == L'"' && p[e - 1] == L'"')
    {
        ++b;
        --e;
    }
    return wstring(p + b, p + e);
}

// Trim leading/trailing ASCII spaces and tabs (used for config directives).
wstring TrimSpace(const wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && IsSpaceCh(s[b])) ++b;
    while (e > b && IsSpaceCh(s[e - 1])) --e;
    return s.substr(b, e - b);
}

wstring ToUpperOrdinal(const wstring& s)
{
    wstring r(s);
    for (auto& c : r)
    {
        c = static_cast<wchar_t>(towupper(c));
    }
    return r;
}

// GB2312 level of a single character, using codepage 20936 (GB2312-80) with
// WC_NO_BEST_FIT_CHARS so substitution never fakes a match.
//   0 = not encodable in GB2312 (rare/extended character),
//   1 = GB2312 level-2 (次常用, zone 56-87),
//   2 = GB2312 level-1 (最常用,  zone 16-55).
// Multi-character strings are not GB2312 characters; callers handle phrases by
// length, so this returns 0 for them (unused).
int GetGb2312Level(const wstring& w)
{
    if (w.size() != 1)
    {
        return 0;
    }
    char buf[4] = {0};
    BOOL usedDefault = FALSE;
    int n = WideCharToMultiByte(20936, WC_NO_BEST_FIT_CHARS, w.c_str(), 1,
                                buf, sizeof(buf), nullptr, &usedDefault);
    if (n != 2 || usedDefault)
    {
        return 0;
    }
    int zone = static_cast<unsigned char>(buf[0]) - 0xA0;
    if (zone >= 16 && zone <= 55)
    {
        return 2;
    }
    if (zone >= 56 && zone <= 87)
    {
        return 1;
    }
    return 0;
}

// Re-map the just-written .bin and rebuild code->words / word->code maps by
// walking the on-disk structures, then compare against the in-memory source
// maps. This proves the writer and the (zero-parse) layout agree, i.e. the
// file a runtime reader would consume is faithful to the text dictionary.
bool VerifyBin(const wchar_t* path,
               const std::map<wstring, std::vector<wstring>>& codeToWords,
               const std::map<wstring, wstring>& wordToCode)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"[VERIFY] cannot reopen %s (err %lu)\n", path, GetLastError());
        return false;
    }

    LARGE_INTEGER li = {};
    GetFileSizeEx(h, &li);
    const uint64_t fileSize = static_cast<uint64_t>(li.QuadPart);

    HANDLE map = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map)
    {
        fwprintf(stderr, L"[VERIFY] CreateFileMapping failed (err %lu)\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    const uint8_t* base = static_cast<const uint8_t*>(MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0));
    if (!base)
    {
        fwprintf(stderr, L"[VERIFY] MapViewOfFile failed (err %lu)\n", GetLastError());
        CloseHandle(map);
        CloseHandle(h);
        return false;
    }

    const DimeBinDict::Header* hdr = reinterpret_cast<const DimeBinDict::Header*>(base);
    if (!DimeBinDict::ValidateHeader(hdr, fileSize))
    {
        fwprintf(stderr, L"[VERIFY] header validation failed\n");
        UnmapViewOfFile(base);
        CloseHandle(map);
        CloseHandle(h);
        return false;
    }

    const uint8_t* poolBase = base + hdr->stringPoolOffset;
    auto poolStr = [&](uint32_t off, uint16_t len) -> wstring {
        return wstring(reinterpret_cast<const wchar_t*>(poolBase + (off - hdr->stringPoolOffset)), len);
    };

    const DimeBinDict::CodeEntry* ce =
        reinterpret_cast<const DimeBinDict::CodeEntry*>(base + hdr->codeEntryOffset);
    const DimeBinDict::WordRef* wr =
        reinterpret_cast<const DimeBinDict::WordRef*>(base + hdr->wordRefOffset);
    const DimeBinDict::ReverseEntry* re =
        reinterpret_cast<const DimeBinDict::ReverseEntry*>(base + hdr->reverseEntryOffset);

    std::map<wstring, std::vector<wstring>> readCodeToWords;
    for (uint32_t i = 0; i < hdr->codeCount; ++i)
    {
        wstring code = poolStr(ce[i].codeOffset, ce[i].codeLen);
        std::vector<wstring> words;
        for (uint16_t j = 0; j < ce[i].wordCount; ++j)
        {
            const DimeBinDict::WordRef& r = wr[ce[i].firstWordRef + j];
            words.push_back(poolStr(r.wordOffset, r.wordLen));
        }
        readCodeToWords[code] = words;
    }

    std::map<wstring, wstring> readWordToCode;
    for (uint32_t i = 0; i < hdr->reverseCount; ++i)
    {
        wstring w = poolStr(re[i].wordOffset, re[i].wordLen);
        wstring c = poolStr(re[i].codeOffset, re[i].codeLen);
        readWordToCode[w] = c;
    }

    bool good = true;
    if (readCodeToWords != codeToWords)
    {
        fwprintf(stderr, L"[VERIFY] code->words mismatch (%zu vs %zu entries)\n",
                 readCodeToWords.size(), codeToWords.size());
        good = false;
    }
    if (readWordToCode != wordToCode)
    {
        fwprintf(stderr, L"[VERIFY] word->code mismatch (%zu vs %zu entries)\n",
                 readWordToCode.size(), wordToCode.size());
        good = false;
    }

    UnmapViewOfFile(base);
    CloseHandle(map);
    CloseHandle(h);
    return good;
}

} // namespace

int BuildAllFromDict();

int wmain(int argc, wchar_t** argv)
{
    // No arguments: compile every .txt in the source dictionary folder.
    if (argc == 1)
    {
        return BuildAllFromDict();
    }

    const wchar_t* inPath = nullptr;
    const wchar_t* outPath = nullptr;
    int maxCode = 0; // 0 = no length limit
    bool verify = false;

    for (int i = 1; i < argc; ++i)
    {
        if (wcscmp(argv[i], L"--max-code") == 0 && i + 1 < argc)
        {
            maxCode = _wtoi(argv[++i]);
        }
        else if (wcscmp(argv[i], L"--verify") == 0)
        {
            verify = true;
        }
        else if (!inPath)
        {
            inPath = argv[i];
        }
        else if (!outPath)
        {
            outPath = argv[i];
        }
    }

    // Pinyin dictionaries get --max-code 24 by default (mirrors the no-arg batch
    // mode and deploy_test.bat), unless an explicit --max-code was supplied. This
    // keeps the runtime's generic "build_bindict <txt> <bin>" call pinyin-aware.
    if (maxCode == 0 && inPath)
    {
        for (const wchar_t* p = inPath; *p; ++p)
        {
            if ((*p == L'p' || *p == L'P') && _wcsnicmp(p, L"pinyin", 6) == 0)
            {
                maxCode = 24;
                break;
            }
        }
    }

    if (!inPath || !outPath)
    {
        fwprintf(stderr, L"usage: build_bindict <in.txt> <out.bin> [--max-code N] [--verify]\n");
        return 2;
    }

    std::wstring source;
    uint64_t srcSize = 0;
    FILETIME srcMtime = {};
    if (!ReadSourceFile(inPath, source, srcSize, srcMtime))
    {
        return 1;
    }

    const wchar_t* text = source.data();
    size_t textLen = source.size();

    // ---- Collect config directives from the dictionary header ----
    // Lines at the top of the file of the form "#@KEY:Value" (or "#@KEY=Value")
    // are config directives. Leading blank lines and "# comment" lines are
    // ignored. Scanning stops at the first real (non-comment) line. Each
    // collected directive becomes a "KEY:value\n" line in the binary config
    // block (UTF-16LE, no BOM), written at [kHeaderSize, codeEntryOffset).
    std::wstring configText;
    bool haveName = false;
    {
        size_t i = 0;
        while (i <= textLen)
        {
            size_t lineEnd = i;
            while (lineEnd < textLen && text[lineEnd] != L'\r' && text[lineEnd] != L'\n')
            {
                ++lineEnd;
            }
            size_t b = i;
            while (b < lineEnd && IsSpaceCh(text[b])) ++b;
            size_t e = lineEnd;
            while (e > b && IsSpaceCh(text[e - 1])) --e;

            bool consume = false;
            if (b >= e)
            {
                consume = true;                          // blank / whitespace-only line
            }
            else if (text[b] == L'#')
            {
                size_t c = b + 1;
                while (c < e && IsSpaceCh(text[c])) ++c;
                if (c < e && text[c] == L'@')
                {
                    size_t d = c + 1;
                    while (d < e && IsSpaceCh(text[d])) ++d;
                    wstring content(text + d, e - d);
                    size_t sep = content.find_first_of(L":=");
                    if (sep != wstring::npos)
                    {
                        wstring key = TrimSpace(content.substr(0, sep));
                        wstring val = TrimSpace(content.substr(sep + 1));
                        if (!key.empty())
                        {
                            configText += key;
                            configText += L':';
                            configText += val;
                            configText += L'\n';
                            if (key == L"NAME")
                            {
                                haveName = true;
                            }
                        }
                    }
                    consume = true;
                }
                else
                {
                    consume = true;                      // ordinary comment, ignore
                }
            }

            i = lineEnd;
            while (i < textLen && (text[i] == L'\r' || text[i] == L'\n')) ++i;

            if (!consume)
            {
                break;   // first real dictionary entry -> stop scanning header
            }
        }
    }

    // Default NAME to the source file stem when no "#@NAME:" directive was
    // given, so the binary always carries a human-readable dictionary name.
    if (!haveName)
    {
        wstring fname(inPath);
        size_t slash = fname.find_last_of(L"\\/");
        if (slash != wstring::npos)
        {
            fname = fname.substr(slash + 1);
        }
        size_t dot = fname.rfind(L'.');
        if (dot != wstring::npos)
        {
            fname = fname.substr(0, dot);
        }
        configText += L"NAME:";
        configText += fname;
        configText += L'\n';
    }

    // Forward map: code -> words (word order preserved as in the file).
    // std::map keeps codes sorted by ordinal (matches runtime std::sort on wstring).
    std::map<wstring, std::vector<wstring>> codeToWords;
    // Reverse map: word -> shortest code (first wins on ties), built in file order.
    std::unordered_map<wstring, wstring> wordToCodeTmp;

    size_t lineStart = 0;
    uint32_t lineCount = 0, skipped = 0;
    for (size_t i = 0; i <= textLen; ++i)
    {
        if (i == textLen || text[i] == L'\r' || text[i] == L'\n')
        {
            if (i > lineStart)
            {
                const wchar_t* line = text + lineStart;
                size_t len = i - lineStart;

                // Skip blank lines and "#" comments. Config directives ("#@")
                // were already collected by the header scan above; ordinary
                // comments and blank lines carry no dictionary entry.
                size_t sb = 0;
                while (sb < len && IsSpaceCh(line[sb])) ++sb;
                if (sb >= len || line[sb] == L'#')
                {
                    lineStart = i + 1;
                    continue;
                }

                // Split on the first '=' (codes are quoted and contain no '=').
                size_t eq = wstring::npos;
                for (size_t k = 0; k < len; ++k)
                {
                    if (line[k] == L'=')
                    {
                        eq = k;
                        break;
                    }
                }

                if (eq != wstring::npos)
                {
                    wstring key = ToUpperOrdinal(TrimAndUnquote(line, eq));
                    wstring val = TrimAndUnquote(line + eq + 1, len - eq - 1);
                    ++lineCount;

                    const bool okLen = (maxCode == 0) ||
                                       (static_cast<int>(key.size()) <= maxCode);
                    if (!key.empty() && !val.empty() && okLen)
                    {
                        codeToWords[key].push_back(val);

                        auto it = wordToCodeTmp.find(val);
                        if (it == wordToCodeTmp.end() || key.size() < it->second.size())
                        {
                            wordToCodeTmp[val] = key;
                        }
                    }
                    else
                    {
                        ++skipped;
                    }
                }
            }
            lineStart = i + 1;
        }
    }

    if (codeToWords.empty())
    {
        fwprintf(stderr, L"[ERROR] no valid entries parsed from %s\n", inPath);
        return 4;   // 4 = not a DIME dictionary (skipped by batch mode)
    }

    // Reverse entries sorted by word (ordinal).
    std::map<wstring, wstring> wordToCode(wordToCodeTmp.begin(), wordToCodeTmp.end());

    // ---- Counts ----
    uint32_t codeCount = static_cast<uint32_t>(codeToWords.size());
    uint32_t wordPairCount = 0;
    for (const auto& kv : codeToWords)
    {
        wordPairCount += static_cast<uint32_t>(kv.second.size());
    }
    uint32_t reverseCount = static_cast<uint32_t>(wordToCode.size());
    uint32_t statsCommonChars = 0;             // single chars inside GB2312
    uint32_t commonChars = 0;

    // ---- Section offsets (include the variable-length config block) ----
    const uint32_t configCharLen = static_cast<uint32_t>(configText.size());
    uint32_t configBytes = configCharLen * static_cast<uint32_t>(sizeof(wchar_t));
    configBytes += (DimeBinDict::kConfigAlign - (configBytes % DimeBinDict::kConfigAlign)) % DimeBinDict::kConfigAlign;

    const uint32_t codeEntryOffset = DimeBinDict::kHeaderSize + configBytes;
    const uint32_t wordRefOffset = codeEntryOffset + codeCount * DimeBinDict::kCodeEntryStride;
    const uint32_t reverseEntryOffset = wordRefOffset + wordPairCount * DimeBinDict::kWordRefStride;
    const uint32_t stringPoolOffset = reverseEntryOffset + reverseCount * DimeBinDict::kReverseEntryStride;

    // ---- String pool (dedup); offsets are absolute (from file start) ----
    std::vector<uint8_t> pool;
    std::unordered_map<wstring, uint32_t> poolOffset;
    auto internString = [&](const wstring& s) -> uint32_t {
        auto it = poolOffset.find(s);
        if (it != poolOffset.end())
        {
            return it->second;
        }
        uint32_t abs = stringPoolOffset + static_cast<uint32_t>(pool.size());
        const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
        pool.insert(pool.end(), p, p + s.size() * sizeof(wchar_t));
        poolOffset.emplace(s, abs);
        return abs;
    };

    // ---- Build CodeEntry[] + WordRef[] ----
    std::vector<DimeBinDict::CodeEntry> codeEntries;
    std::vector<DimeBinDict::WordRef> wordRefs;
    codeEntries.reserve(codeCount);
    wordRefs.reserve(wordPairCount);

    for (const auto& kv : codeToWords)
    {
        DimeBinDict::CodeEntry ce = {};
        ce.codeOffset = internString(kv.first);
        ce.firstWordRef = static_cast<uint32_t>(wordRefs.size());
        ce.codeLen = static_cast<uint16_t>(kv.first.size());
        ce.wordCount = static_cast<uint16_t>(kv.second.size());
        ce.reserved = 0;

        bool hasCommon = false;
        for (const auto& w : kv.second)
        {
            DimeBinDict::WordRef wr = {};
            wr.wordOffset = internString(w);
            wr.wordLen = static_cast<uint16_t>(w.size());

            int lvl = 0;
            bool kept;
            if (w.size() == 1)
            {
                lvl = GetGb2312Level(w);
                kept = (lvl >= 1);          // 单字需在 GB2312
                if (lvl >= 1)
                {
                    ++statsCommonChars;
                }
            }
            else
            {
                kept = true;                // 词组照常显示
            }
            wr.reserved = static_cast<uint16_t>(lvl & 0x3);
            wordRefs.push_back(wr);
            if (kept)
            {
                hasCommon = true;
            }
        }
        if (hasCommon)
        {
            ce.reserved |= DimeBinDict::kCodeHasCommonBit;
        }
        codeEntries.push_back(ce);
    }

    // ---- Build ReverseEntry[] ----
    std::vector<DimeBinDict::ReverseEntry> reverseEntries;
    reverseEntries.reserve(reverseCount);
    for (const auto& kv : wordToCode)
    {
        DimeBinDict::ReverseEntry re = {};
        re.wordOffset = internString(kv.first);
        re.codeOffset = internString(kv.second);
        re.wordLen = static_cast<uint16_t>(kv.first.size());
        re.codeLen = static_cast<uint16_t>(kv.second.size());
        re.reserved = 0;
        reverseEntries.push_back(re);
    }

    // ---- Header ----
    DimeBinDict::Header hdr = {};
    hdr.magic = DimeBinDict::kMagic;
    hdr.version = DimeBinDict::kVersion;
    hdr.flags = DimeBinDict::kFlagLittleEndian;
    hdr.sourceSize = static_cast<uint32_t>(srcSize & 0xFFFFFFFFull);
    hdr.codeCount = codeCount;
    hdr.wordPairCount = wordPairCount;
    hdr.reverseCount = reverseCount;
    hdr.sourceMtimeLow = srcMtime.dwLowDateTime;
    hdr.codeEntryOffset = codeEntryOffset;
    hdr.codeEntryStride = DimeBinDict::kCodeEntryStride;
    hdr.wordRefOffset = wordRefOffset;
    hdr.wordRefStride = DimeBinDict::kWordRefStride;
    hdr.reverseEntryOffset = reverseEntryOffset;
    hdr.reverseEntryStride = DimeBinDict::kReverseEntryStride;
    hdr.stringPoolOffset = stringPoolOffset;
    hdr.sourceMtimeHigh = srcMtime.dwHighDateTime;

    // ---- Write output ----
    HANDLE out = CreateFileW(outPath, GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"[ERROR] cannot create output: %s (err %lu)\n", outPath, GetLastError());
        return 1;
    }

    auto writeAll = [&](const void* data, size_t size) -> bool {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t total = 0;
        while (total < size)
        {
            DWORD chunk = static_cast<DWORD>((size - total > 0x10000000ull)
                                                 ? 0x10000000ull
                                                 : (size - total));
            DWORD wrote = 0;
            if (!WriteFile(out, p + total, chunk, &wrote, nullptr) || wrote == 0)
            {
                return false;
            }
            total += wrote;
        }
        return true;
    };

    bool ok = writeAll(&hdr, sizeof(hdr));
    if (ok && configBytes > 0)
    {
        // Config block: UTF-16LE "KEY:value\n" lines, NUL-padded to kConfigAlign.
        std::vector<uint8_t> cfg(configBytes, 0);
        if (configCharLen > 0)
        {
            memcpy(cfg.data(), configText.data(), configCharLen * sizeof(wchar_t));
        }
        ok = writeAll(cfg.data(), cfg.size());
    }
    if (ok && !codeEntries.empty())
        ok = writeAll(codeEntries.data(), codeEntries.size() * sizeof(DimeBinDict::CodeEntry));
    if (ok && !wordRefs.empty())
        ok = writeAll(wordRefs.data(), wordRefs.size() * sizeof(DimeBinDict::WordRef));
    if (ok && !reverseEntries.empty())
        ok = writeAll(reverseEntries.data(), reverseEntries.size() * sizeof(DimeBinDict::ReverseEntry));
    if (ok && !pool.empty())
        ok = writeAll(pool.data(), pool.size());

    CloseHandle(out);

    if (!ok)
    {
        fwprintf(stderr, L"[ERROR] write failed for %s (err %lu)\n", outPath, GetLastError());
        DeleteFileW(outPath);
        return 1;
    }

    const uint64_t totalBytes = static_cast<uint64_t>(stringPoolOffset) + pool.size();
    wprintf(L"[OK] %s -> %s\n", inPath, outPath);
    wprintf(L"     lines=%u skipped=%u codes=%u wordPairs=%u reverse=%u\n",
            lineCount, skipped, codeCount, wordPairCount, reverseCount);
    wprintf(L"     commonChars(GB2312)=%u poolBytes=%zu totalBytes=%llu\n",
            statsCommonChars, pool.size(), static_cast<unsigned long long>(totalBytes));

    if (verify)
    {
        if (VerifyBin(outPath, codeToWords, wordToCode))
        {
            wprintf(L"[VERIFY] PASS: .bin round-trips identically to the text dictionary\n");
        }
        else
        {
            fwprintf(stderr, L"[VERIFY] FAIL\n");
            return 3;
        }
    }
    return 0;
}

// No-argument mode: scan the source dictionary folder for every *.txt and
// compile each into a sibling <name>.bin. Pinyin dictionaries are given
// --max-code 24 (mirrors deploy_test.bat). Reuses the single-file path by
// invoking wmain with synthesized arguments.
int BuildAllFromDict()
{
    const wchar_t* kFolder = L"Dictionary";
    wchar_t search[MAX_PATH] = {0};
    wcscpy_s(search, ARRAYSIZE(search), kFolder);
    wcscat_s(search, ARRAYSIZE(search), L"\\*.txt");

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        fwprintf(stderr, L"[ERROR] no .txt files found in %s (err %lu)\n", kFolder, GetLastError());
        return 1;
    }

    int converted = 0;
    int failures = 0;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            continue;
        }

        wchar_t inBuf[MAX_PATH] = {0};
        wcscpy_s(inBuf, ARRAYSIZE(inBuf), kFolder);
        wcscat_s(inBuf, ARRAYSIZE(inBuf), L"\\");
        wcscat_s(inBuf, ARRAYSIZE(inBuf), fd.cFileName);

        wchar_t outBuf[MAX_PATH] = {0};
        wcscpy_s(outBuf, ARRAYSIZE(outBuf), inBuf);
        size_t len = wcslen(outBuf);
        if (len < 4 || _wcsicmp(outBuf + len - 4, L".txt") != 0)
        {
            continue;
        }
        outBuf[len - 4] = L'\0';
        wcscat_s(outBuf, ARRAYSIZE(outBuf), L".bin");

        // Synthesize arguments for the single-file path.
        wchar_t maxCodeArg[16] = {0};
        int argc2 = 3;
        wchar_t* argv2[5];
        argv2[0] = const_cast<wchar_t*>(L"build_bindict");
        argv2[1] = inBuf;
        argv2[2] = outBuf;
        if (wcsstr(fd.cFileName, L"pinyin") != nullptr)
        {
            wcscpy_s(maxCodeArg, ARRAYSIZE(maxCodeArg), L"24");
            argv2[3] = const_cast<wchar_t*>(L"--max-code");
            argv2[4] = maxCodeArg;
            argc2 = 5;
        }

        wprintf(L"[scan] %s -> %s\n", inBuf, outBuf);
        int rc = wmain(argc2, argv2);
        if (rc == 0)
        {
            ++converted;
        }
        else if (rc == 4)
        {
            wprintf(L"[skip] %s (not a DIME code=word dictionary)\n", inBuf);
        }
        else
        {
            ++failures;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    wprintf(L"\n[SUMMARY] converted=%d failures=%d\n", converted, failures);
    return failures == 0 ? 0 : 1;
}
