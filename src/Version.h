// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

// Single source of truth for the DIME product / DLL version.
// Used by DIME.rc (VERSIONINFO), the settings About panel, and packaging
// when HEAD has no exact v* tag (then installer is <this>-<yyyyMMdd>).
// Exact v* tags still win for package.bat / CI installer naming.
// Bump these three numbers when releasing.

#define DIME_VER_MAJOR 1
#define DIME_VER_MINOR 3
#define DIME_VER_PATCH 0

#define DIME_VER_STRINGIZE2(x) #x
#define DIME_VER_STRINGIZE(x)  DIME_VER_STRINGIZE2(x)

// Comma form for VERSIONINFO (Windows requires four fields; build is 0).
#define DIME_VERSION_COMMA \
    DIME_VER_MAJOR,DIME_VER_MINOR,DIME_VER_PATCH,0

// "1.0.0" — ANSI / RC string form.
#define DIME_VERSION_STRING \
    DIME_VER_STRINGIZE(DIME_VER_MAJOR) "." \
    DIME_VER_STRINGIZE(DIME_VER_MINOR) "." \
    DIME_VER_STRINGIZE(DIME_VER_PATCH)

// L"1.0.0" — wide form for UI text (MSVC adjacent-literal concat).
#define DIME_VERSION_STRING_W L"" DIME_VERSION_STRING
