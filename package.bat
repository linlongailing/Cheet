@echo off
setlocal EnableExtensions
taskkill /F /IM cheetahClip.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul

cd /d "%~dp0"

REM ---- VS via vswhere ----
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

REM ---- Qt ----
if defined QTDIR (
  set "QT=%QTDIR%"
) else if defined CMAKE_PREFIX_PATH (
  for /f "tokens=1 delims=;" %%A in ("%CMAKE_PREFIX_PATH%") do set "QT=%%A"
) else (
  echo ERROR: Set QTDIR, e.g. set QTDIR=C:\Qt\6.8.3\msvc2022_64
  exit /b 1
)
if not exist "%QT%\bin\windeployqt.exe" (
  echo ERROR: windeployqt not found under QT="%QT%"
  exit /b 1
)
where cmake >nul 2>&1
if errorlevel 1 (
  echo ERROR: cmake not on PATH.
  exit /b 1
)
set "PATH=%QT%\bin;%PATH%"

if exist build_release rmdir /s /q build_release
if exist dist rmdir /s /q dist
if exist installer_output rmdir /s /q installer_output

cmake -S . -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%"
if errorlevel 1 (
  cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%"
  if errorlevel 1 exit /b 1
)
cmake --build build_release --config Release -j 8
if errorlevel 1 exit /b 1

mkdir dist
if exist build_release\cheetahClip.exe (
  copy /Y build_release\cheetahClip.exe dist\ >nul
) else if exist build_release\Release\cheetahClip.exe (
  copy /Y build_release\Release\cheetahClip.exe dist\ >nul
) else (
  echo ERROR: cheetahClip.exe not found after build
  exit /b 1
)
"%QT%\bin\windeployqt.exe" --qmldir qml --release dist\cheetahClip.exe
if errorlevel 1 exit /b 1

for /d %%D in ("%VSINSTALL%\VC\Redist\MSVC\*") do (
  if exist "%%D\x64\Microsoft.VC143.CRT\*.dll" (
    copy /Y "%%D\x64\Microsoft.VC143.CRT\*.dll" dist\ >nul
  )
)

REM ---- Inno Setup ----
set "ISCC="
if exist "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC (
  echo ERROR: Inno Setup 6 not found. Install: winget install JRSoftware.InnoSetup
  exit /b 1
)

"%ISCC%" "%~dp0installer\cheetahClip.iss"
if errorlevel 1 exit /b 1

echo.
echo OK: %~dp0installer_output\cheetahClip-Setup-1.0.0.exe
dir "%~dp0installer_output\cheetahClip-Setup-*.exe"
exit /b 0
