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
set "SRC_DIC=%ROOT%\Dictionary\wubi98.txt"

set "SRC_DLL_X64="
set "SRC_DLL_X86="

REM Precompile binary dictionaries (.bin) from the text sources so the IME
REM loads them with zero parsing. The runtime also compiles on the fly if a
REM .bin is missing, using the converter shipped below into dict\.
set "BUILD_BIN=%ROOT%\out\x64\Debug\build_bindict.exe"
if not exist "%ROOT%\out\x64\Debug\build_bindict.exe" if exist "%ROOT%\out\x64\Release\build_bindict.exe" set "BUILD_BIN=%ROOT%\out\x64\Release\build_bindict.exe"
if exist "%BUILD_BIN%" (
    echo.
    echo Compile binary dictionaries .bin ...
    "%BUILD_BIN%" "%SRC_DIC%" "%ROOT%\Dictionary\wubi98.bin"
    "%BUILD_BIN%" "%ROOT%\Dictionary\pinyin.txt" "%ROOT%\Dictionary\pinyin.bin" --max-code 24
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

if not exist "%SRC_DIC%" (
    echo [ERROR] Dictionary not found: %SRC_DIC%
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
REM <dllDir>\..\dict\ probe). .bin is used directly; .txt is kept so the runtime
REM converter can rebuild the .bin if it is ever missing or corrupt.
copy /Y "%SRC_DIC%" "%NEW_DICT%\wubi98.txt" >nul
copy /Y "%ROOT%\Dictionary\pinyin.txt" "%NEW_DICT%\pinyin.txt" >nul
if exist "%ROOT%\Dictionary\wubi98.bin" copy /Y "%ROOT%\Dictionary\wubi98.bin" "%NEW_DICT%\wubi98.bin" >nul
if exist "%ROOT%\Dictionary\pinyin.bin" copy /Y "%ROOT%\Dictionary\pinyin.bin" "%NEW_DICT%\pinyin.bin" >nul
if exist "%BUILD_BIN%" copy /Y "%BUILD_BIN%" "%NEW_DICT%\build_bindict.exe" >nul

echo.
echo x64 DLL : %SRC_DLL_X64% -> dime64.dll
echo x86 DLL : %SRC_DLL_X86% -> dime32.dll
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
