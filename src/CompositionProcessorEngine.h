// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT



#pragma once

#include "sal.h"
#include "TableDictionaryEngine.h"
#include "KeyHandlerEditSession.h"
#include "DIMEBaseStructure.h"
#include "FileMapping.h"
#include "Compartment.h"
#include "define.h"

class CDIME;

class CCompositionProcessorEngine
{
public:
    CCompositionProcessorEngine(void);
    ~CCompositionProcessorEngine(void);

    BOOL SetupLanguageProfile(LANGID langid, REFGUID guidLanguageProfile, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode, BOOL isComLessMode);

    // Get language profile.
    GUID GetLanguageProfile(LANGID *plangid)
    {
        *plangid = _langid;
        return _guidProfile;
    }
    // Get locale
    LCID GetLocale()
    {
        return MAKELCID(_langid, SORT_DEFAULT);
    }

    BOOL IsVirtualKeyNeed(UINT uCode, _In_reads_(1) WCHAR *pwch, BOOL fComposing, CANDIDATE_MODE candidateMode, BOOL hasCandidateWithWildcard, _Out_opt_ _KEYSTROKE_STATE *pKeyState);

    BOOL AddVirtualKey(WCHAR wch);
    void RemoveVirtualKey(DWORD_PTR dwIndex);
    void PurgeVirtualKey();

    DWORD_PTR GetVirtualKeyLength() { return _keystrokeBuffer.GetLength(); }
    WCHAR GetVirtualKey(DWORD_PTR dwIndex);
    CStringRange* GetKeystrokeBuffer() { return &_keystrokeBuffer; }

    void GetReadingStrings(_Inout_ CDIMEArray<CStringRange> *pReadingStrings, _Out_ BOOL *pIsWildcardIncluded);
    void GetCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL isIncrementalWordSearch, BOOL isWildcardSearch, BOOL loadAllCandidates = FALSE);
    void LoadFullCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList);
    BOOL AreCandidatesTruncated() { return _candidatesTruncated; }
    UINT GetTruncatedCandidateMaxCount() const;
    void GetCandidateStringInConverted(CStringRange &searchString, _In_ CDIMEArray<CCandidateListItem> *pCandidateList);

    // Preserved key handler
    void OnPreservedKey(REFGUID rguid, _Out_ BOOL *pIsEaten, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    // Toggle helpers used by the floating status bar (clickable segments).
    // They mirror the preserved-key behaviour but are fired directly, without
    // requiring a physical hotkey. The compartment change then propagates back
    // through CompartmentCallback -> NotifyInputModeChanged so the UI stays
    // consistent.
    void ToggleDoubleSingleByte(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void TogglePunctuation(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void ToggleOnlyCommon(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);

    // Explicit setters used by the configuration dialog (set a concrete value
    // instead of toggling, and persist the choice so it survives restart).
    void SetDoubleSingleByte(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isFullWidth);
    void SetPunctuation(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isChinesePunctuation);
    void SetOnlyCommon(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isOnlyCommon);

    // Persisted candidate-engine options surfaced in the config dialog.
    void SetWildcard(BOOL v);
    void SetDisableWildcardAtFirst(BOOL v);
    void SetKeystrokeSort(BOOL v);
    void SetCandidatePageSize(int n);
    int  GetCandidatePageSize() const { return _candidatePageSize; }
    // Candidate font size in pixels; 0 = auto (DPI tier), else fixed size.
    void SetCandidateFontSize(int px);
    int  GetCandidateFontSize() const { return _candidateFontSize; }
    // Current auto-tier pixel height (same DPI buckets as SetDefaultCandidateTextFont).
    static int GetAutoCandidateFontSize();
    // Preview lookup for the settings dialog (does not touch the live keystroke buffer).
    void GetPreviewCandidateList(_In_z_ LPCWSTR key, _Inout_ CDIMEArray<CCandidateListItem> *pList, UINT maxCount);

    // Punctuation
    BOOL IsPunctuation(WCHAR wch);
    WCHAR GetPunctuation(WCHAR wch);

    BOOL IsDoubleSingleByte(WCHAR wch);
    BOOL IsWildcard() { return _isWildcard; }
    BOOL IsDisableWildcardAtFirst() { return _isDisableWildcardAtFirst; }
    BOOL IsWildcardChar(WCHAR wch) { return ((IsWildcardOneChar(wch) || IsWildcardAllChar(wch)) ? TRUE : FALSE); }
    BOOL IsWildcardOneChar(WCHAR wch) { return (towupper(wch) == towupper(WUBI_WILDCARD_CHAR) ? TRUE : FALSE); }
    BOOL IsWildcardAllChar(WCHAR wch) { return FALSE; }
    BOOL IsMakePhraseFromText() { return _hasMakePhraseFromText; }
    BOOL IsKeystrokeSort() { return _isKeystrokeSort; }

    // Dictionary engine
    BOOL IsDictionaryAvailable() { return (_pTableDictionaryEngine ? TRUE : FALSE); }
    BOOL IsPinyinDictionaryAvailable() { return (_pPinyinDictionaryEngine ? TRUE : FALSE); }

    // 主词库选择 (注册表 Software\DIME\Dictionary, 默认 wubi98; 不含扩展名).
    void GetMainDictionaryName(_Out_writes_(cch) WCHAR* buf, DWORD cch) const;
    BOOL SetMainDictionaryName(_In_z_ LPCWSTR name);
    static void ReadDictionaryNameFromRegistry(_Out_writes_(cch) WCHAR* buf, DWORD cch);
    static void WriteDictionaryNameToRegistry(_In_z_ LPCWSTR name);
    static BOOL ResolveDictionaryDirectory(_Out_writes_(cch) WCHAR* buf, DWORD cch);
    // 扫描 dict 下 *.bin (排除 pinyin.bin); stem 为文件名, displayName 为 NAME 属性.
    struct DictionaryListItem
    {
        WCHAR stem[64];
        WCHAR displayName[128];
    };
    static int EnumerateMainDictionaries(_Out_writes_(maxCount) DictionaryListItem* items, int maxCount);

    // 跨进程设置同步: 注册表 SettingsVersion (FILETIME QWORD) + 获焦时对比.
    // SyncSettingsOnFocus (DWORD, 默认 1): 关闭则获焦时不重载.
    static BOOL IsSyncSettingsOnFocusEnabled();
    static void SetSyncSettingsOnFocusEnabled(BOOL enabled);
    static ULONGLONG ReadSettingsVersionFromRegistry();
    static ULONGLONG BumpSettingsVersionInRegistry();
    void AcknowledgeSettingsVersion();
    void ApplySettingsFromRegistryIfNeeded();

    // 码表内容热更新: build_bindict 替换 .bin 后 bump DictionaryVersion;
    // 获焦时对比并 unload/reload (不依赖 SyncSettingsOnFocus).
    static ULONGLONG ReadDictionaryVersionFromRegistry();
    static ULONGLONG BumpDictionaryVersionInRegistry();
    void AcknowledgeDictionaryVersion();
    void ReloadDictionariesIfVersionChanged();
    static void TryCleanupStaleBinOldFiles(_In_z_ LPCWSTR dictDir);

    // Temporary pinyin mode: reverse-lookup the wubi code for a candidate word
    // so it can be displayed on the right side of each candidate.
    BOOL GetWubiCodeForWord(_In_ const CStringRange *pWord, _Inout_ CStringRange *pCode);

    // Temporary pinyin input mode (triggered by 'z' on an empty buffer)
    BOOL IsPinyinInput() { return _isPinyinInput; }
    void SetPinyinInput(BOOL v) { _isPinyinInput = v; }

    // Temporary English input mode (triggered by ';' on an empty buffer).
    // Characters are collected literally (no dictionary lookup) and committed
    // only when Enter is pressed.
    BOOL IsEnglishInput() { return _isEnglishInput; }
    void SetEnglishInput(BOOL v) { _isEnglishInput = v; }

    // "Only common characters" filter (GB2312 level >= 1 single chars only).
    BOOL IsOnlyCommon() { return _isOnlyCommon; }

    // 常用字模式下空码时回退检索全码表.
    BOOL IsEmptyCodeSearchFull() const { return _emptyCodeSearchFull; }
    void SetEmptyCodeSearchFull(BOOL v);

    // After a digit key, output ',' / '.' as English (half-width) even in Chinese
    // punctuation or full-width mode. Persisted in the settings dialog.
    BOOL IsEnglishCommaPeriodAfterDigit() const { return _englishCommaPeriodAfterDigit; }
    void SetEnglishCommaPeriodAfterDigit(BOOL v);
    // Update / query follow-digit state from the key event sink.
    // isCandidateSelect: digit used to pick a candidate (上屏汉字), not a digit input.
    void UpdateLastKeyWasDigit(UINT uCode, WCHAR wch, BOOL isCandidateSelect = FALSE);
    BOOL ShouldOutputEnglishCommaOrPeriod(WCHAR wch) const;

    // Hotkey enable/disable (settings dialog "快捷键").
    BOOL IsHotkeyOnlyCommonEnabled() const { return _hotkeyOnlyCommonEnabled; }
    void SetHotkeyOnlyCommonEnabled(BOOL v);
    BOOL IsHotkeyPunctuationEnabled() const { return _hotkeyPunctuationEnabled; }
    void SetHotkeyPunctuationEnabled(BOOL v);
    BOOL IsHotkeyDoubleSingleByteEnabled() const { return _hotkeyDoubleSingleByteEnabled; }
    void SetHotkeyDoubleSingleByteEnabled(BOOL v);

    // Language bar control
    void SetLanguageBarStatus(DWORD status, BOOL isSet);

    void ConversionModeCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr);

    void ShowAllLanguageBarIcons();
    void HideAllLanguageBarIcons();

    void SetTextService(_In_opt_ CDIME *pTextService) { _pTextService = pTextService; }

    void NotifyInputModeChanged(_In_ ITfThreadMgr *pThreadMgr);

    inline CCandidateRange *GetCandidateListIndexRange() { return &_candidateListIndexRange; }
    inline UINT GetCandidateListPhraseModifier() { return _candidateListPhraseModifier; }
    inline UINT GetCandidateWindowWidth() { return _candidateWndWidth; }

private:
    void InitKeyStrokeTable();
    BOOL InitLanguageBar(_In_ CLangBarItemButton *pLanguageBar, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, REFGUID guidCompartment);

    struct _KEYSTROKE;
    BOOL IsVirtualKeyKeystrokeComposition(UINT uCode, _Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_FUNCTION function);
    BOOL IsVirtualKeyKeystrokeCandidate(UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode, _Out_ BOOL *pfRetCode, _In_ CDIMEArray<_KEYSTROKE> *pKeystrokeMetric);
    BOOL IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE candidateMode);

    void SetupKeystroke();
    void SetupPreserved(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    void SetupConfiguration();
    void SetupLanguageBar(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId, BOOL isSecureMode);
    void SetKeystrokeTable(_Inout_ CDIMEArray<_KEYSTROKE> *pKeystroke);
    void SetupPunctuationPair();
    void CreateLanguageBarButton(DWORD dwEnable, GUID guidLangBar, _In_z_ LPCWSTR pwszDescriptionValue, _In_z_ LPCWSTR pwszTooltipValue, DWORD dwOnIconIndex, DWORD dwOffIconIndex, _Outptr_result_maybenull_ CLangBarItemButton **ppLangBarItemButton, BOOL isSecureMode);
    void SetInitialCandidateListRange(int pageSize = 10);
    void SetDefaultCandidateTextFont();
	void InitializeDIMECompartment(_In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
	void _LoadSettings(_Inout_ BOOL &isFullWidth, _Inout_ BOOL &isChinesePunctuation);
	void _SaveSettings(BOOL isFullWidth, BOOL isChinesePunctuation);
	BOOL _GetCompartmentOnlyCommon();
	BOOL _ReadRegistryOnlyCommon();
	void _WriteRegistryOnlyCommon(BOOL isOnlyCommon);

    class XPreservedKey;
    void SetPreservedKey(const CLSID clsid, TF_PRESERVEDKEY & tfPreservedKey, _In_z_ LPCWSTR pwszDescription, _Out_ XPreservedKey *pXPreservedKey);
    BOOL InitPreservedKey(_In_ XPreservedKey *pXPreservedKey, _In_ ITfThreadMgr *pThreadMgr, TfClientId tfClientId);
    BOOL CheckShiftKeyOnly(_In_ CDIMEArray<TF_PRESERVEDKEY> *pTSFPreservedKeyTable);

    static HRESULT CompartmentCallback(_In_ void *pv, REFGUID guidCompartment);
    void PrivateCompartmentsUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void KeyboardOpenCompartmentUpdated(_In_ ITfThreadMgr *pThreadMgr);
    void SyncInputModeLayoutForKeyboardOpen(_In_ ITfThreadMgr *pThreadMgr, BOOL isOpen);

    
    BOOL SetupDictionaryFile();
    CFile* GetDictionaryFile();
    BOOL _LoadDictionary(_In_ LPCWSTR pwszDicName, _In_ LPCWSTR pwszDir, size_t dirLen, _Out_ CFileMapping** ppFile, _Out_ CTableDictionaryEngine** ppEngine);
    void _UnloadMainDictionary();
    BOOL _LoadMainDictionaryFromStem(_In_z_ LPCWSTR stem, _In_ LPCWSTR pwszDir, size_t dirLen);
    static BOOL _IsValidDictionaryStem(_In_z_ LPCWSTR name);
    static BOOL _ReplaceExtensionWithBin(_Inout_ WCHAR* pwszPath);
    BOOL _TryLoadBinary(_In_ LPCWSTR pwszBinPath, _Out_ CFileMapping** ppFile, _Out_ CTableDictionaryEngine** ppEngine);
    void _GetPinyinCandidateList(_Inout_ CDIMEArray<CCandidateListItem> *pCandidateList, BOOL loadAllCandidates);

private:
    struct _KEYSTROKE
    {
        UINT VirtualKey;
        UINT Modifiers;
        KEYSTROKE_FUNCTION Function;

        _KEYSTROKE()
        {
            VirtualKey = 0;
            Modifiers = 0;
            Function = FUNCTION_NONE;
        }
    };
    _KEYSTROKE _keystrokeTable[26];

    CTableDictionaryEngine* _pTableDictionaryEngine;
    CTableDictionaryEngine* _pPinyinDictionaryEngine;
    WCHAR _mainDictionaryName[64];
    CStringRange _keystrokeBuffer;

    BOOL _hasWildcardIncludedInKeystrokeBuffer;

    LANGID _langid;
    GUID _guidProfile;
    TfClientId  _tfClientId;
    ITfThreadMgr* _pThreadMgr;

    CDIMEArray<_KEYSTROKE> _KeystrokeComposition;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidate;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidateWildcard;
    CDIMEArray<_KEYSTROKE> _KeystrokeCandidateSymbol;
    CDIMEArray<_KEYSTROKE> _KeystrokeSymbol;

    // Preserved key data
    class XPreservedKey
    {
    public:
        XPreservedKey();
        ~XPreservedKey();
        BOOL UninitPreservedKey(_In_ ITfThreadMgr *pThreadMgr);

    public:
        CDIMEArray<TF_PRESERVEDKEY> TSFPreservedKeyTable;
        GUID Guid;
        LPCWSTR Description;
    };

    XPreservedKey _PreservedKey_IMEMode;
    XPreservedKey _PreservedKey_DoubleSingleByte;
    XPreservedKey _PreservedKey_Punctuation;
    XPreservedKey _PreservedKey_OnlyCommon;   // Ctrl+M: toggle "only common characters"

    // Punctuation data
    CDIMEArray<CPunctuationPair> _PunctuationPair;
    CDIMEArray<CPunctuationNestPair> _PunctuationNestPair;

    // Language bar data
    CLangBarItemButton* _pLanguageBar_IMEMode;
    CLangBarItemButton* _pLanguageBar_DoubleSingleByte;
    CLangBarItemButton* _pLanguageBar_Punctuation;

    // Compartment
    CCompartment* _pCompartmentConversion;
    CCompartmentEventSink* _pCompartmentConversionEventSink;
    CCompartmentEventSink* _pCompartmentKeyboardOpenEventSink;
    CCompartmentEventSink* _pCompartmentDoubleSingleByteEventSink;
    CCompartmentEventSink* _pCompartmentPunctuationEventSink;

    // Configuration data
    BOOL _isWildcard : 1;
    BOOL _isDisableWildcardAtFirst : 1;
    BOOL _hasMakePhraseFromText : 1;
    BOOL _isKeystrokeSort : 1;
    BOOL _isComLessMode : 1;
    BOOL _candidatesTruncated : 1;
    BOOL _isPinyinInput : 1;
    BOOL _isEnglishInput : 1;
    BOOL _isOnlyCommon : 1;     // "only common characters" filter (GB2312)
    BOOL _emptyCodeSearchFull : 1; // 空码时回退全码表
    BOOL _englishCommaPeriodAfterDigit : 1; // digit then ,/. -> English
    BOOL _lastKeyWasDigit : 1;  // runtime: previous keystroke was a digit
    BOOL _hotkeyOnlyCommonEnabled : 1; // Ctrl+M toggle commonly/full dict
    BOOL _hotkeyPunctuationEnabled : 1; // Ctrl+. toggle CN/EN punctuation
    BOOL _hotkeyDoubleSingleByteEnabled : 1; // Shift+Space full/half width
    BOOL _candidateListIncremental : 1;
    BOOL _candidateListWildcard : 1;
    CCandidateRange _candidateListIndexRange;
    UINT _candidateListPhraseModifier;
    UINT _candidateWndWidth;
    int  _candidatePageSize;   // candidates selectable per page (1-10)
    int  _candidateFontSize;   // 0 = auto (DPI tiers), else fixed px
    ULONGLONG _settingsVersion; // 与注册表 SettingsVersion 对齐的本地副本
    ULONGLONG _dictionaryVersion; // 与注册表 DictionaryVersion 对齐的本地副本

    BOOL _imeModeSnapshotValid;
    BOOL _imeModeSnapshotFullWidth;
    BOOL _imeModeSnapshotChinesePunctuation;

    CFileMapping* _pDictionaryFile;
    CFileMapping* _pPinyinDictionaryFile;

    CDIME* _pTextService;

    static const int OUT_OF_FILE_INDEX = -1;
};

