@echo off
REM Build Open-terminal-core with MSVC C++20 /W4 /WX

setlocal

set SRC_DIR=%~dp0
set BUILD_DIR=%SRC_DIR%build
set OBJ_DIR=%BUILD_DIR%\obj
set BIN_DIR=%BUILD_DIR%\bin

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

set CXX_FLAGS=/std:c++20 /W4 /WX /EHsc /nologo /Zi /MDd /I"%SRC_DIR%"
set LINK_FLAGS=/nologo /DEBUG

echo Building Open-terminal-core...
echo.

REM Platform
cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\com.obj" "%SRC_DIR%platform\com.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\files.obj" "%SRC_DIR%platform\files.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\foreground.obj" "%SRC_DIR%platform\foreground.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\paths.obj" "%SRC_DIR%platform\paths.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\process.obj" "%SRC_DIR%platform\process.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\startup.obj" "%SRC_DIR%platform\startup.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\strings.obj" "%SRC_DIR%platform\strings.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\wsl.obj" "%SRC_DIR%platform\wsl.cpp"
if errorlevel 1 goto :error

REM Storage
cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\json.obj" "%SRC_DIR%storage\json.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\settings.obj" "%SRC_DIR%storage\settings.cpp"
if errorlevel 1 goto :error

REM Features
cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\terminal_launch.obj" "%SRC_DIR%features\terminal_launch.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\claude_inject.obj" "%SRC_DIR%features\claude_inject.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\claude_settings_file.obj" "%SRC_DIR%features\claude_settings_file.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\chrome_profiles.obj" "%SRC_DIR%features\chrome_profiles.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\chrome_visible_set.obj" "%SRC_DIR%features\chrome_visible_set.cpp"
if errorlevel 1 goto :error

cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\app_settings.obj" "%SRC_DIR%features\app_settings.cpp"
if errorlevel 1 goto :error

REM Application
cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\core_application.obj" "%SRC_DIR%application\core_application.cpp"
if errorlevel 1 goto :error

REM Link static library
lib /nologo /out:"%BIN_DIR%\open_terminal_core.lib" ^
    "%OBJ_DIR%\com.obj" ^
    "%OBJ_DIR%\files.obj" ^
    "%OBJ_DIR%\foreground.obj" ^
    "%OBJ_DIR%\paths.obj" ^
    "%OBJ_DIR%\process.obj" ^
    "%OBJ_DIR%\startup.obj" ^
    "%OBJ_DIR%\strings.obj" ^
    "%OBJ_DIR%\wsl.obj" ^
    "%OBJ_DIR%\json.obj" ^
    "%OBJ_DIR%\settings.obj" ^
    "%OBJ_DIR%\terminal_launch.obj" ^
    "%OBJ_DIR%\claude_inject.obj" ^
    "%OBJ_DIR%\claude_settings_file.obj" ^
    "%OBJ_DIR%\chrome_profiles.obj" ^
    "%OBJ_DIR%\chrome_visible_set.obj" ^
    "%OBJ_DIR%\app_settings.obj" ^
    "%OBJ_DIR%\core_application.obj"

if errorlevel 1 goto :error

echo.
echo ✓ Library built: %BIN_DIR%\open_terminal_core.lib
echo.

REM Build tests
echo Building tests...
cl %CXX_FLAGS% /c /Fo"%OBJ_DIR%\core_application_test.obj" "%SRC_DIR%tests\core_application_test.cpp"
if errorlevel 1 goto :error

link %LINK_FLAGS% /out:"%BIN_DIR%\core_application_test.exe" ^
    "%OBJ_DIR%\core_application_test.obj" ^
    "%BIN_DIR%\open_terminal_core.lib" ^
    shell32.lib ole32.lib shlwapi.lib

if errorlevel 1 goto :error

echo.
echo ✓ Test built: %BIN_DIR%\core_application_test.exe
echo.
echo Running tests...
echo.

"%BIN_DIR%\core_application_test.exe"
if errorlevel 1 goto :error

echo.
echo ✅ Build and test successful
goto :end

:error
echo.
echo ❌ Build failed
exit /b 1

:end
endlocal
