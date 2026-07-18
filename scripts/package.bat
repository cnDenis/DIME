@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set "ROOT=%CD%"
set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
set "DIST=%ROOT%\out\dist\DIME"
set "BUILD_BIN=%ROOT%\out\x64\Release\build_bindict.exe"
set "REGSVR_KILL=ctfmon.exe"

if exist "%ISCC%" goto :have_iscc
echo [ERROR] Inno Setup not found: %ISCC%
exit /b 1
:have_iscc

REM 1) Build x64 + x86 Release (incremental; full build on first run)
echo.
echo === Step 1/4: Build x64 + x86 Release ===
call "%ROOT%\scripts\build_release.bat"
if errorlevel 1 exit /b 1

REM 2) Precompile binary dictionaries (.bin) from the text sources so the IME
REM    loads with zero parsing. The runtime also rebuilds on the fly if a .bin
REM    is missing, but shipping a fresh .bin is cleaner.
echo.
echo === Step 2/4: Compile binary dictionaries ===
if not exist "%BUILD_BIN%" (
    echo [ERROR] build_bindict.exe not found: %BUILD_BIN%
    exit /b 1
)
"%BUILD_BIN%" "%ROOT%\Dictionary\wubi98.txt" "%ROOT%\Dictionary\wubi98.bin"
"%BUILD_BIN%" "%ROOT%\Dictionary\pinyin.txt" "%ROOT%\Dictionary\pinyin.bin" --max-code 24

REM 3) Assemble the dist layout (flat: both DLLs + shared dict\)
echo.
echo === Step 3/4: Assemble dist layout ===
if exist "%DIST%" (
    rmdir /s /q "%DIST%" >nul 2>&1
)
if exist "%DIST%" (
    REM Folder locked (Explorer / AV / open handle). Clear files in place.
    del /f /q "%DIST%\*" >nul 2>&1
    if exist "%DIST%\dict" del /f /q "%DIST%\dict\*" >nul 2>&1
)
mkdir "%DIST%\dict" >nul 2>&1
call :copy1 "%ROOT%\out\x64\Release\dime.dll" "%DIST%\dime64.dll"
if errorlevel 1 exit /b 1
call :copy1 "%ROOT%\out\x86\Release\dime.dll" "%DIST%\dime32.dll"
if errorlevel 1 exit /b 1
call :copy1 "%ROOT%\Dictionary\wubi98.txt"    "%DIST%\dict\wubi98.txt"
if errorlevel 1 exit /b 1
call :copy1 "%ROOT%\Dictionary\pinyin.txt"    "%DIST%\dict\pinyin.txt"
if errorlevel 1 exit /b 1
call :copy1 "%ROOT%\Dictionary\wubi98.bin"    "%DIST%\dict\wubi98.bin"
if errorlevel 1 exit /b 1
call :copy1 "%ROOT%\Dictionary\pinyin.bin"    "%DIST%\dict\pinyin.bin"
if errorlevel 1 exit /b 1
REM build_bindict.exe ships next to the DLLs (same dir), where the runtime probes it first.
call :copy1 "%ROOT%\out\x64\Release\build_bindict.exe" "%DIST%\build_bindict.exe"
if errorlevel 1 exit /b 1
echo   dist -> %DIST%

REM 4) Compile the Inno Setup installer -> out\dist\DIME_<ver>_Setup.exe
echo.
echo === Step 4/4: Build DIME Setup ===

REM Version: prefer an exact v* git tag on HEAD; otherwise
REM <Version.h>-<yyyyMMdd> so untagged builds stay unique and traceable.
REM VER_H must be set outside the if/else: %% expansion inside () is parse-time.
set "VER_H=%ROOT%\src\Version.h"
set "DIME_VERSION="
for /f "tokens=*" %%g in ('git describe --tags --exact-match 2^>nul') do set "DIME_VERSION=%%g"
if defined DIME_VERSION (
    set "DIME_VERSION=!DIME_VERSION:v=!"
    echo   version: !DIME_VERSION! ^(from git tag^)
) else (
    set "DIME_VER_MAJOR="
    set "DIME_VER_MINOR="
    set "DIME_VER_PATCH="
    for /f "tokens=3" %%a in ('findstr /B /C:"#define DIME_VER_MAJOR " "%VER_H%"') do set "DIME_VER_MAJOR=%%a"
    for /f "tokens=3" %%a in ('findstr /B /C:"#define DIME_VER_MINOR " "%VER_H%"') do set "DIME_VER_MINOR=%%a"
    for /f "tokens=3" %%a in ('findstr /B /C:"#define DIME_VER_PATCH " "%VER_H%"') do set "DIME_VER_PATCH=%%a"
    if not defined DIME_VER_MAJOR (
        echo [ERROR] failed to read DIME_VER_* from %VER_H%
        exit /b 1
    )
    for /f "tokens=*" %%g in ('powershell -NoProfile -Command "(Get-Date).ToString('yyyyMMdd')"') do set "DATEPART=%%g"
    set "DIME_VERSION=!DIME_VER_MAJOR!.!DIME_VER_MINOR!.!DIME_VER_PATCH!-!DATEPART!"
    echo   version: !DIME_VERSION! ^(from Version.h + date^)
)

"%ISCC%" /DMyAppVersion=%DIME_VERSION% "%ROOT%\installer\DIME.iss"
if errorlevel 1 exit /b 1

echo.
echo Done. Installer: %ROOT%\out\dist\DIME_%DIME_VERSION%_Setup.exe
exit /b 0

:copy1
copy /Y "%~1" "%~2" >nul
if errorlevel 1 (
    echo [ERROR] copy failed: "%~1" -^> "%~2"
    echo        Close Explorer windows on out\dist, or retry after AV scan finishes.
    exit /b 1
)
exit /b 0
