@echo off
setlocal EnableExtensions EnableDelayedExpansion

set ROOT_DIR=%~dp0..
set PHYSX_DIR=%ROOT_DIR%\KraftonEngine\ThirdParty\PhysX\physx
set PHYSX_SOLUTION=%PHYSX_DIR%\compiler\vc17win64\PhysXSDK.sln
set PM_python_PATH=%ROOT_DIR%\Scripts\python

set "_KEEP_PATH=%PATH%"
set "PATH="
set "Path=%_KEEP_PATH%"
set "_KEEP_PATH="

if not exist "%PHYSX_DIR%\generate_projects.bat" (
    echo PhysX SDK was not found at:
    echo   %PHYSX_DIR%
    pause
    exit /b 1
)

if not exist "%PM_python_PATH%\python.exe" (
    echo Bundled Python was not found at:
    echo   %PM_python_PATH%\python.exe
    pause
    exit /b 1
)

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -version [17.0^,18.0^) -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo Visual Studio 2022 with C++ build tools was not found.
    pause
    exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if %ERRORLEVEL% neq 0 (
    echo Failed to initialize Visual Studio build environment.
    pause
    exit /b %ERRORLEVEL%
)

set "_KEEP_PATH=%Path%"
set "PATH="
set "Path=%_KEEP_PATH%"
set "_KEEP_PATH="

set "CMAKE_BIN=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if not exist "%CMAKE_BIN%\cmake.exe" (
    for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -property installationPath`) do set "CMAKE_VS_PATH=%%i"
    if defined CMAKE_VS_PATH (
        set "CMAKE_BIN=!CMAKE_VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    )
)
if exist "%CMAKE_BIN%\cmake.exe" (
    set "Path=%CMAKE_BIN%;%Path%"
)

where /q cmake
if %ERRORLEVEL% neq 0 (
    if not exist "%PHYSX_DIR%\..\externals\cmake\x64\bin\cmake.exe" (
        echo CMake was not found.
        echo Install CMake and add it to PATH, or place cmake.exe at:
        echo   %PHYSX_DIR%\..\externals\cmake\x64\bin\cmake.exe
        pause
        exit /b 1
    )
)

cd /d "%PHYSX_DIR%"

call generate_projects.bat vc17win64
if %ERRORLEVEL% neq 0 (
    echo Failed to generate PhysX vc17win64 projects.
    pause
    exit /b %ERRORLEVEL%
)

msbuild "%PHYSX_SOLUTION%" /p:Configuration=debug /p:Platform=x64 /m /v:minimal
if %ERRORLEVEL% neq 0 (
    echo Failed to build PhysX debug x64.
    pause
    exit /b %ERRORLEVEL%
)

msbuild "%PHYSX_SOLUTION%" /p:Configuration=release /p:Platform=x64 /m /v:minimal
if %ERRORLEVEL% neq 0 (
    echo Failed to build PhysX release x64.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo PhysX build complete.
pause
