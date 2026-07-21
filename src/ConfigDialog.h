// Copyright (c) 2026 cnDenis
//
// SPDX-License-Identifier: MIT


#pragma once

#include <windows.h>

class CDIME;

// Launches the DIME settings dialog (left category list + right option panel).
// hwndOwner is disabled while the modal dialog is open and re-enabled on close.
// pTextService may be an activated live instance (status bar / candidate) or an
// unactivated CoCreate from ITfFnConfigure (Windows language settings Options).
void DimeShowConfigDialog(HWND hwndOwner, CDIME* pTextService);

// Exported entry for dime_config.exe: create an unactivated CDIME and show settings.
extern "C" HRESULT WINAPI DimeLaunchConfigDialog(HWND hwndParent);
