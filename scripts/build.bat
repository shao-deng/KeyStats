@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build-msvc"
set "GENERATOR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

where cmake >nul 2>nul || (echo [ERROR] CMake not found in PATH.& exit /b 1)
if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if defined VSPATH echo %VSPATH%|findstr /C:"2022" >nul && set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if defined VSPATH echo %VSPATH%|findstr /C:"2019" >nul && set "GENERATOR=Visual Studio 16 2019"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" set "GENERATOR=Visual Studio 16 2019"
if not defined GENERATOR (echo [ERROR] Visual Studio with C++ workload not found.& exit /b 1)

if /I "%1"=="/clean" if exist "%BUILD%" rmdir /s /q "%BUILD%"
echo Using generator: %GENERATOR%
cmake -S "%ROOT%" -B "%BUILD%" -G "%GENERATOR%" -A x64 || exit /b 1
cmake --build "%BUILD%" --config Release --parallel || exit /b 1
if /I "%1"=="/test" goto test
if /I "%2"=="/test" goto test
goto done

:test
cmake --build "%BUILD%" --config Release --target RUN_TESTS || exit /b 1
:done
echo Build complete: "%BUILD%\Release\KeyStats.exe"
exit /b 0
