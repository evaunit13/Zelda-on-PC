@echo off
rem Builds Zelda on PC (a Cemu fork) with Visual Studio 2022, CMake and Ninja.
rem Usage: tools\build.cmd [configure|build|all] [release|relwithdebinfo|debug]
setlocal
set ROOT=%~dp0..
set MODE=%2
if "%MODE%"=="" set MODE=release
set BUILD=%ROOT%\build\%MODE%

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
  echo Visual Studio 2022 ^(or the Build Tools^) with the "Desktop development with C++" workload is required.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i
if not defined VSINSTALL (
  echo No Visual Studio installation with the C++ x64 tools was found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set STEP=%1
if "%STEP%"=="" set STEP=all

if not exist "%ROOT%\dependencies\vcpkg\vcpkg.exe" (
  call "%ROOT%\dependencies\vcpkg\bootstrap-vcpkg.bat" -disableMetrics || exit /b 1
)

if "%STEP%"=="build" goto build

cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%MODE% -DVCPKG_INSTALL_OPTIONS="--clean-after-build" -DENABLE_DISCORD_RPC=OFF || exit /b 1
if "%STEP%"=="configure" exit /b 0

:build
cmake --build "%BUILD%" -- -k 0 || exit /b 1
exit /b 0
