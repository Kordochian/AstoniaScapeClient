@echo off
rem Build the Astonia client natively on Windows with Visual Studio (clang-cl) + vcpkg.
rem No MSYS2 needed. Run from anywhere; output goes to bin\moac.exe next to this file.
rem
rem   build-msvc.bat           Release build
rem   build-msvc.bat debug     Debug build (DEVELOPER on)
rem
rem Needs: Visual Studio 2022 or 2026 with "Desktop development with C++",
rem        "C++ Clang tools for Windows" and "vcpkg package manager"; git.

setlocal
cd /d "%~dp0"

set PRESET=ninja
if /i "%~1"=="debug" set PRESET=ninja-debug

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer not found. Install Visual Studio 2022 or 2026 first.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No Visual Studio with the C++ workload found. Add "Desktop development with C++".
    exit /b 1
)
echo Using Visual Studio in %VSINSTALL%

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
if errorlevel 1 exit /b 1

if not exist "%VSINSTALL%\VC\Tools\Llvm\x64\bin\clang-cl.exe" (
    echo clang-cl not found. In the Visual Studio Installer, add the component
    echo "C++ Clang tools for Windows" ^(Individual components^).
    exit /b 1
)
set "PATH=%VSINSTALL%\VC\Tools\Llvm\x64\bin;%PATH%"

if not defined VCPKG_ROOT if exist "%VSINSTALL%\VC\vcpkg\vcpkg.exe" set "VCPKG_ROOT=%VSINSTALL%\VC\vcpkg"
if not defined VCPKG_ROOT (
    echo vcpkg not found. Add the "vcpkg package manager" component in the Visual Studio
    echo Installer, or clone https://github.com/microsoft/vcpkg and set VCPKG_ROOT to it.
    exit /b 1
)

cmake --preset %PRESET%
if errorlevel 1 exit /b 1
cmake --build --preset %PRESET%
if errorlevel 1 exit /b 1

echo.
echo Done: %~dp0bin\moac.exe
echo Copy res\ from the official v1.1.3 client next to bin\ before running it.
endlocal
