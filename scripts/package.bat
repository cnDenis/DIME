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
    rmdir /s /q "%DIST%" 2>nul
    if exist "%DIST%" echo [WARN] could not remove old dist; continuing (may include stale files)
)
mkdir "%DIST%\dict" 2>nul
copy /Y "%ROOT%\out\x64\Release\dime.dll" "%DIST%\dime64.dll" >nul
copy /Y "%ROOT%\out\x86\Release\dime.dll" "%DIST%\dime32.dll" >nul
copy /Y "%ROOT%\Dictionary\wubi98.txt"    "%DIST%\dict\" >nul
copy /Y "%ROOT%\Dictionary\pinyin.txt"    "%DIST%\dict\" >nul
copy /Y "%ROOT%\Dictionary\wubi98.bin"    "%DIST%\dict\" >nul
copy /Y "%ROOT%\Dictionary\pinyin.bin"    "%DIST%\dict\" >nul
REM build_bindict.exe ships next to the DLLs (same dir), where the runtime probes it first.
copy /Y "%ROOT%\out\x64\Release\build_bindict.exe" "%DIST%\" >nul
echo   dist -> %DIST%

REM 4) Compile the Inno Setup installer -> out\dist\DIME-<ver>_Setup.exe
echo.
echo === Step 4/4: Build DIME Setup ===

REM Version: when HEAD is exactly a git tag, use it (minus the "v" prefix);
REM otherwise fall back to <date>-<short sha> so every dev build is uniquely
REM identifiable. Passed to ISCC via /DMyAppVersion (keeps the .iss generic).
set "DIME_VERSION="
for /f "tokens=*" %%g in ('git describe --tags --exact-match 2^>nul') do set "DIME_VERSION=%%g"
if defined DIME_VERSION (
    set "DIME_VERSION=%DIME_VERSION:v=%"
) else (
    for /f "tokens=*" %%g in ('powershell -NoProfile -Command "(Get-Date).ToString('yyyyMMdd')"') do set "DATEPART=%%g"
    for /f "tokens=*" %%g in ('git rev-parse --short=6 HEAD') do set "SHAPART=%%g"
    set "DIME_VERSION=!DATEPART!-!SHAPART!"
)
echo   version: %DIME_VERSION%

"%ISCC%" /DMyAppVersion=%DIME_VERSION% "%ROOT%\installer\DIME.iss"
if errorlevel 1 exit /b 1

echo.
echo Done. Installer: %ROOT%\out\dist\DIME_%DIME_VERSION%_Setup.exe
exit /b 0
