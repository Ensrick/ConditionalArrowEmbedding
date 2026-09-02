@echo off
setlocal EnableExtensions
call :main > "%~dp0build.log" 2>&1
exit /b %errorlevel%

:main
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :fail
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CAE_VSROOT=%%I"
if not defined CAE_VSROOT goto :fail
call "%CAE_VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 goto :fail
if not defined CAE_VCPKG_ROOT set "CAE_VCPKG_ROOT=%USERPROFILE%\source\repos\vcpkg"
set "VCPKG_ROOT=%CAE_VCPKG_ROOT%"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" goto :fail
set "CAE_NINJA=%CAE_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "CAE_CMAKE=%CAE_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CAE_CTEST=%CAE_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "%CAE_NINJA%" goto :fail
if not exist "%CAE_CMAKE%" goto :fail
if not exist "%CAE_CTEST%" goto :fail
set "PATH=%CAE_VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if not defined CMAKE_BUILD_PARALLEL_LEVEL set "CMAKE_BUILD_PARALLEL_LEVEL=3"
if not defined VCPKG_MAX_CONCURRENCY set "VCPKG_MAX_CONCURRENCY=3"
cd /d "%~dp0.."
"%CAE_CMAKE%" --fresh --preset release -DCMAKE_MAKE_PROGRAM="%CAE_NINJA%" -DCOMMONLIB_PREBUILT=OFF -DENABLE_SKYRIM_VR=OFF
if errorlevel 1 goto :fail
"%CAE_CMAKE%" --build build/release
if errorlevel 1 goto :fail
"%CAE_CTEST%" --test-dir build/release --output-on-failure
if errorlevel 1 goto :fail
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File tools\audit-binary.ps1 > build\release\binary-audit.json
if errorlevel 1 goto :fail
exit /b 0

:fail
echo ***BUILD_FAILED*** errorlevel %errorlevel%
exit /b 1
