@echo off
setlocal

set ROOT_DIR=%~dp0..
set PHYSX_DIR=%ROOT_DIR%\KraftonEngine\ThirdParty\PhysX\physx
set PHYSX_SOLUTION=%PHYSX_DIR%\compiler\vc17win64\PhysXSDK.sln

if not exist "%PHYSX_DIR%\generate_projects.bat" (
    echo PhysX SDK was not found at:
    echo   %PHYSX_DIR%
    pause
    exit /b 1
)

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS_PATH=%%i
if not defined VS_PATH (
    echo Visual Studio was not found.
    pause
    exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo
if %ERRORLEVEL% neq 0 (
    echo Failed to initialize Visual Studio build environment.
    pause
    exit /b %ERRORLEVEL%
)

cd /d "%PHYSX_DIR%"

call generate_projects.bat vc17win64
if %ERRORLEVEL% neq 0 (
    echo Failed to generate PhysX vc17win64 projects.
    pause
    exit /b %ERRORLEVEL%
)

msbuild "%PHYSX_SOLUTION%" /p:Configuration=checked /p:Platform=x64 /m /v:minimal
if %ERRORLEVEL% neq 0 (
    echo Failed to build PhysX checked x64.
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
