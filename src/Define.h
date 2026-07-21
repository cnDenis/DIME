// Copyright (c) Microsoft Corporation.
// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once
#include "resource.h"

#define TEXTSERVICE_MODEL        L"Apartment"
#define TEXTSERVICE_LANGID       MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)
// RegisterProfile uIconIndex: 0-based icon order in dime.dll (see DIME.rc ICON list), not resource ID.
#define TEXTSERVICE_ICON_INDEX       0
#define TEXTSERVICE_DIC_STEM L"wubi98"
#define TEXTSERVICE_DIC L"wubi98.txt"
#define TEXTSERVICE_PINYIN_DIC L"pinyin.txt"
#define TEXTSERVICE_PINYIN_BIN L"pinyin.bin"

#define WUBI_MAX_CODE_LENGTH    4
#define PINYIN_MAX_CODE_LENGTH  24
#define ENGLISH_MAX_CODE_LENGTH 32
#define WUBI_WILDCARD_CHAR      L'Z'
#define WUBI_INITIAL_CANDIDATE_PAGES  5

#define IME_MODE_ON_ICON_INDEX      IDI_IME_MODE_ON
#define IME_MODE_OFF_ICON_INDEX     IDI_IME_MODE_OFF
#define IME_DOUBLE_ON_INDEX         IDI_DOUBLE_SINGLE_BYTE_ON
#define IME_DOUBLE_OFF_INDEX        IDI_DOUBLE_SINGLE_BYTE_OFF
#define IME_PUNCTUATION_ON_INDEX    IDI_PUNCTUATION_ON
#define IME_PUNCTUATION_OFF_INDEX   IDI_PUNCTUATION_OFF

#define SAMPLEIME_FONT_DEFAULT L"Microsoft YaHei UI"

// Layout metrics below are tuned at 125% display scaling (DPI 120 = 96 * 1.25).
// At runtime they are scaled by (current DPI / DIME_REFERENCE_DPI) so other
// scalings stay proportional to this reference look (125% is the baseline).
#define DIME_REFERENCE_DPI          (120)

//---------------------------------------------------------------------
// defined Candidated Window
//---------------------------------------------------------------------
#define CANDWND_ROW_WIDTH				(24)
// Azure accent palette, matched to the floating status bar (light-gray body +
// soft blue hover highlight) so the candidate box and status bar feel like one kit.
#define CANDWND_ACCENT_COLOR			(RGB(0x4C, 0x8B, 0xF5))   // azure blue: selection bg / header icons
#define CANDWND_BORDER_COLOR			(RGB(0x9C, 0xC3, 0xE6))   // soft azure border
#define CANDWND_BORDER_WIDTH			(2)
#define CANDWND_HEADER_BK_SYSCOLOR		(COLOR_BTNFACE)            // light gray, ~ status-bar body
#define CANDWND_NUM_COLOR				(RGB(0x6A, 0x9E, 0xDA))   // muted azure: index / encoding
#define CANDWND_SELECTED_ITEM_COLOR		(RGB(0xFF, 0xFF, 0xFF))   // white text on selection
#define CANDWND_SELECTED_BK_COLOR		(RGB(0x4C, 0x8B, 0xF5))   // azure blue selection
#define CANDWND_ITEM_COLOR				(RGB(0x20, 0x20, 0x20))   // near-black candidate text

//---------------------------------------------------------------------
// defined modifier
//---------------------------------------------------------------------
#define _TF_MOD_ON_KEYUP_SHIFT_ONLY    (0x00010000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_CONTROL_ONLY  (0x00020000 | TF_MOD_ON_KEYUP)
#define _TF_MOD_ON_KEYUP_ALT_ONLY      (0x00040000 | TF_MOD_ON_KEYUP)

#define CAND_WINDOW_WIDTH_PX    (200)

//---------------------------------------------------------------------
// string length of CLSID
//---------------------------------------------------------------------
#define CLSID_STRLEN    (38)  // strlen("{xxxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxx}")