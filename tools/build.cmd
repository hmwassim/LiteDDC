@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "CONFIG="
set "PLATFORM="
set "MSBUILD="

if /i "%~1"=="Debug" set "CONFIG=Debug"
if /i "%~1"=="Release" set "CONFIG=Release"
if not defined CONFIG set "CONFIG=Release"

if /i "%~2"=="x64" set "PLATFORM=x64"
if /i "%~2"=="ARM64" set "PLATFORM=ARM64"
if not defined PLATFORM set "PLATFORM=x64"

rem Locate MSBuild via vswhere (VS 2022 Build Tools / VS installs).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" call :find_msbuild

if defined MSBUILD goto have_msbuild
echo [LiteDDC] MSBuild not found. Install VS 2022 Build Tools with the C++ workload.
exit /b 1
:have_msbuild
if exist "%MSBUILD%" goto build
echo [LiteDDC] MSBuild missing at: %MSBUILD%
exit /b 1

:build
echo [LiteDDC] Building %CONFIG%^|%PLATFORM% ...
"%MSBUILD%" "%ROOT%\LiteDDC.sln" -m -t:Build -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -v:m -nologo

set "EXITCODE=%ERRORLEVEL%"
if not "%EXITCODE%"=="0" goto failed
echo [LiteDDC] Build OK: %ROOT%\bin\%PLATFORM%\%CONFIG%\LiteDDC.exe
exit /b 0

:failed
echo [LiteDDC] Build FAILED with exit code %EXITCODE%
exit /b %EXITCODE%

:find_msbuild
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "MSBUILD=%%i\MSBuild\Current\Bin\MSBuild.exe"
exit /b 0
