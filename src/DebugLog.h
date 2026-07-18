// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

enum DIME_LOG_LEVEL
{
    DIME_LOG_LEVEL_DEBUG = 0,
    DIME_LOG_LEVEL_INFO,
    DIME_LOG_LEVEL_WARNING,
    DIME_LOG_LEVEL_ERROR,
};

void DimeLog(_In_ DIME_LOG_LEVEL level, _In_ PCWSTR location, _In_ PCWSTR format, ...);

// In Release builds (NDEBUG defined) DEBUG/INFO logs compile to nothing, so
// only WARNING and above are emitted. In Debug builds every level is logged.
#ifdef NDEBUG
#define DIME_DEBUG_LOG(...) ((void)0)
#define DIME_INFO_LOG(...)  ((void)0)
#else
#define DIME_DEBUG_LOG(...) DimeLog(DIME_LOG_LEVEL_DEBUG, __FUNCTIONW__, __VA_ARGS__)
#define DIME_INFO_LOG(...)  DimeLog(DIME_LOG_LEVEL_INFO, __FUNCTIONW__, __VA_ARGS__)
#endif

#define DIME_WARNING_LOG(...) DimeLog(DIME_LOG_LEVEL_WARNING, __FUNCTIONW__, __VA_ARGS__)
#define DIME_ERROR_LOG(...)   DimeLog(DIME_LOG_LEVEL_ERROR, __FUNCTIONW__, __VA_ARGS__)
