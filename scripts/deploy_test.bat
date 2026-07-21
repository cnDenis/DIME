@echo off
setlocal enabledelayedexpansion

:: Deploy x64 + Win32 dime.dll to out\test\<N>\, register with matching regsvr32.
:: 32-bit apps (e.g. 32-bit Word) load only the Win32 DLL under SysWOW64 registration.
:: Run as Administrator.

cd /d "%~dp0.."
set "ROOT=%CD%"
set "TEST_ROOT=%ROOT%\out\test"
set "REGSVR64=%SystemRoot%\System32\regsvr32.exe"
set "REGSVR32=%SystemRoot%\SysWOW64\regsvr32.exe"
REM Precompile every Dictionary\*.txt into a sibling .bin (pinyin auto --max-code 24).
set "BUILD_BIN=%ROOT%\out\x64\Debug\build_bindict.exe"
if not exist "%ROOT%\out\x64\Debug\build_bindict.exe" if exist "%ROOT%\out\x64\Release\build_bindict.exe" set "BUILD_BIN=%ROOT%\out\x64\Release\build_bindict.exe"
if exist "%BUILD_BIN%" (
    echo.
    echo Compile binary dictionaries .bin ...
    pushd "%ROOT%"
    "%BUILD_BIN%"
    set "BIND_RC=!errorlevel!"
    popd
    if not "!BIND_RC!"=="0" (
        echo [ERROR] build_bindict failed, code !BIND_RC!
        exit /b 1
    )
) else (
    echo [WARN] build_bindict.exe not found; deploying text-only dictionaries.
)

if exist "%ROOT%\out\x64\Debug\dime.dll" set "SRC_DLL_X64=%ROOT%\out\x64\Debug\dime.dll"
if exist "%ROOT%\out\x64\Release\dime.dll" if not defined SRC_DLL_X64 set "SRC_DLL_X64=%ROOT%\out\x64\Release\dime.dll"

if exist "%ROOT%\out\x86\Debug\dime.dll" set "SRC_DLL_X86=%ROOT%\out\x86\Debug\dime.dll"
if exist "%ROOT%\out\x86\Release\dime.dll" if not defined SRC_DLL_X86 set "SRC_DLL_X86=%ROOT%\out\x86\Release\dime.dll"

net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

if not defined SRC_DLL_X64 (
    echo [ERROR] x64 dime.dll not found. Build with: cmake -B build -A x64 ^&^& cmake --build build --config Debug
    exit /b 1
)

if not defined SRC_DLL_X86 (
    echo [ERROR] Win32 dime.dll not found. Build with: cmake -B build-win32 -A Win32 ^&^& cmake --build build-win32 --config Debug
    exit /b 1
)

dir /b "%ROOT%\Dictionary\*.txt" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] no .txt dictionaries found in "%ROOT%\Dictionary"
    exit /b 1
)

taskkill /f /im ctfmon.exe 2>nul

if not exist "%TEST_ROOT%" mkdir "%TEST_ROOT%"

set "MAX=0"
for /d %%D in ("%TEST_ROOT%\*") do (
    set "N=%%~nxD"
    echo !N!| findstr /r "^[0-9][0-9]*$" >nul
    if !errorlevel! equ 0 (
        if !N! gtr !MAX! set "MAX=!N!"
    )
)

set /a NEXT=MAX+1
set "NEW_DIR=%TEST_ROOT%\!NEXT!"
set "PREV_DIR=%TEST_ROOT%\!MAX!"
set "NEW_X64=%NEW_DIR%\x64"
set "NEW_X86=%NEW_DIR%\x86"
set "NEW_DICT=%NEW_DIR%\dict"

mkdir "%NEW_X64%" 2>nul
mkdir "%NEW_X86%" 2>nul
mkdir "%NEW_DICT%" 2>nul
if errorlevel 1 (
    echo [ERROR] Cannot create: %NEW_DIR%
    exit /b 1
)

copy /Y "%SRC_DLL_X64%" "%NEW_X64%\dime64.dll" >nul
copy /Y "%SRC_DLL_X86%" "%NEW_X86%\dime32.dll" >nul

REM Shared code table in dict\ (loaded by both x64 and x86 via the engine's
REM <dllDir>\..\dict\ probe). Copy every Dictionary\*.txt and matching .bin.
for %%F in ("%ROOT%\Dictionary\*.txt") do (
    copy /Y "%%F" "%NEW_DICT%\%%~nxF" >nul
    if exist "%%~dpnF.bin" copy /Y "%%~dpnF.bin" "%NEW_DICT%\%%~nF.bin" >nul
)
if exist "%BUILD_BIN%" copy /Y "%BUILD_BIN%" "%NEW_DICT%\build_bindict.exe" >nul

REM Settings launcher next to x64 DLL (loads dime64.dll from the same folder).
set "SRC_CFG="
if exist "%ROOT%\out\x64\Debug\dime_config.exe" set "SRC_CFG=%ROOT%\out\x64\Debug\dime_config.exe"
if exist "%ROOT%\out\x64\Release\dime_config.exe" if not defined SRC_CFG set "SRC_CFG=%ROOT%\out\x64\Release\dime_config.exe"
if defined SRC_CFG copy /Y "%SRC_CFG%" "%NEW_X64%\dime_config.exe" >nul

echo.
echo x64 DLL : %SRC_DLL_X64% -^> dime64.dll
echo x86 DLL : %SRC_DLL_X86% -^> dime32.dll
echo Dict    : %NEW_DICT%
echo Deploy  : %NEW_DIR%

if !MAX! gtr 0 (
    echo.
    if exist "%PREV_DIR%\x64\dime64.dll" (
        echo Unregister x64: %PREV_DIR%\x64\dime64.dll
        "%REGSVR64%" /u /s "%PREV_DIR%\x64\dime64.dll"
    ) else if exist "%PREV_DIR%\x64\dime.dll" (
        echo Unregister x64 legacy: %PREV_DIR%\x64\dime.dll
        "%REGSVR64%" /u /s "%PREV_DIR%\x64\dime.dll"
    )
    if exist "%PREV_DIR%\x86\dime32.dll" (
        echo Unregister x86: %PREV_DIR%\x86\dime32.dll
        "%REGSVR32%" /u /s "%PREV_DIR%\x86\dime32.dll"
    ) else if exist "%PREV_DIR%\x86\dime.dll" (
        echo Unregister x86 legacy: %PREV_DIR%\x86\dime.dll
        "%REGSVR32%" /u /s "%PREV_DIR%\x86\dime.dll"
    )
    if exist "%PREV_DIR%\dime.dll" (
        echo Unregister legacy: %PREV_DIR%\dime.dll
        "%REGSVR64%" /u /s "%PREV_DIR%\dime.dll"
    )
)

echo.
echo Register x64: %NEW_X64%\dime64.dll
"%REGSVR64%" /s "%NEW_X64%\dime64.dll"
if errorlevel 1 (
    echo [ERROR] x64 register failed, code !errorlevel!
    exit /b 1
)

echo Register x86: %NEW_X86%\dime32.dll
"%REGSVR32%" /s "%NEW_X86%\dime32.dll"
if errorlevel 1 (
    echo [ERROR] x86 register failed, code !errorlevel!
    exit /b 1
)

echo.
echo Done. Active build: out\test\!NEXT!  (x64 + x86)
echo Restart 32-bit Word / Notepad after switching IME.
exit /b 0
