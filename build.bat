@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM Same discovery as package.bat (no machine-specific paths)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere not found. Install Visual Studio 2022 with C++ workload.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo ERROR: Visual Studio with MSVC not found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

if defined QTDIR (
  set "QT=%QTDIR%"
) else if defined CMAKE_PREFIX_PATH (
  for /f "tokens=1 delims=;" %%A in ("%CMAKE_PREFIX_PATH%") do set "QT=%%A"
) else (
  echo ERROR: Set QTDIR to your Qt MSVC kit, e.g.
  echo   set QTDIR=C:\Qt\6.8.3\msvc2022_64
  exit /b 1
)

set "PATH=%QT%\bin;%PATH%"
if exist build rmdir /s /q build

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%"
if errorlevel 1 (
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%"
  if errorlevel 1 exit /b 1
)
cmake --build build --config Release -j 8
exit /b %ERRORLEVEL%
