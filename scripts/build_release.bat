@echo off
setlocal

cd /d "%~dp0.."
set "ROOT=%CD%"
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

if not exist "%CMAKE%" (
    echo [ERROR] cmake not found: %CMAKE%
    exit /b 1
)

if not exist "%ROOT%\out\x64\CMakeCache.txt" (
    "%CMAKE%" -S "%ROOT%" -B "%ROOT%\out\x64" -A x64 -G "Visual Studio 17 2022"
    if errorlevel 1 exit /b 1
)

if not exist "%ROOT%\out\x86\CMakeCache.txt" (
    "%CMAKE%" -S "%ROOT%" -B "%ROOT%\out\x86" -A Win32 -G "Visual Studio 17 2022"
    if errorlevel 1 exit /b 1
)

"%MSBUILD%" "%ROOT%\out\x64\dime.sln" /p:Configuration=Release /m /v:minimal
if errorlevel 1 exit /b 1

"%MSBUILD%" "%ROOT%\out\x86\dime.sln" /p:Configuration=Release /m /v:minimal
if errorlevel 1 exit /b 1

echo.
echo x64 : %ROOT%\out\x64\Release\dime.dll
echo x86 : %ROOT%\out\x86\Release\dime.dll
exit /b 0
