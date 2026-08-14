@echo off
setlocal

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Restore-Dependencies.ps1"
if errorlevel 1 exit /b %errorlevel%

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo vcvars64.bat not found. Install Visual Studio 2022 C++ Build Tools.
  exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 exit /b %errorlevel%

set "CONFIGS=%*"
if "%CONFIGS%"=="" set "CONFIGS=Debug Release"

for %%C in (%CONFIGS%) do (
  echo === %%C ^| x64 ===
  msbuild "%~dp0Terminal.sln" /m /nologo /v:minimal ^
    /p:Configuration=%%C /p:Platform=x64 || exit /b 1
)

endlocal
