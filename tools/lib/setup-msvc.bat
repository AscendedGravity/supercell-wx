@echo off

:: Check if cl.exe is already in the path
where cl.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo MSVC environment already initialized.
    exit /b 0
)

echo Initializing MSVC environment...

:: Path to vswhere.exe
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%vswhere%" (
    echo Error: vswhere.exe not found at "%vswhere%"
    exit /b 1
)

:: Find the latest Visual Studio installation
set "vs_path="
for /f "usebackq tokens=*" %%i in (`"%vswhere%" -latest -property installationPath`) do (
    set "vs_path=%%i"
)

if "%vs_path%"=="" (
    echo Error: Could not find Visual Studio installation.
    exit /b 1
)

echo Found Visual Studio at: %vs_path%

:: Find vcvarsall.bat
set "vcvarsall=%vs_path%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist "%vcvarsall%" (
    echo Error: vcvarsall.bat not found at "%vcvarsall%"
    exit /b 1
)

:: Save original PATH (vcvarsall.bat will replace it entirely)
set "ORIGINAL_PATH=%PATH%"

:: Sanitize PATH before vcvarsall - remove entries with special batch characters
:: Entries containing ( ) & ^ | break parsing inside vcvarsall.bat
for /f "delims=" %%p in ('powershell -NoProfile -NonInteractive -Command "($env:PATH -split ';' | Where-Object { $_ -notmatch '[()^&^^^|]' }) -join ';'"') do set "PATH=%%p"

:: Call vcvarsall.bat
:: Use "call" so it affects the current environment
echo Calling: "%vcvarsall%" x64
call "%vcvarsall%" x64

if %ERRORLEVEL% neq 0 (
    echo Error: Failed to initialize MSVC environment.
    exit /b %ERRORLEVEL%
)

:: vcvarsall.bat replaces PATH entirely, losing critical tools.
:: Re-add essential system paths unconditionally (duplicates are harmless).
set "PATH=%PATH%;C:\Windows\System32;C:\Windows"
set "PATH=%PATH%;C:\Windows\System32\Wbem"
set "PATH=%PATH%;C:\Windows\System32\WindowsPowerShell\v1.0"
set "PATH=%PATH%;C:\Windows\System32\OpenSSH"

:: Re-add user tool paths
if exist "%USERPROFILE%\.local\bin" set "PATH=%PATH%;%USERPROFILE%\.local\bin"

:: Re-add Git
if exist "%ProgramFiles%\Git\cmd" set "PATH=%PATH%;%ProgramFiles%\Git\cmd"
if exist "%ProgramFiles(x86)%\Git\cmd" set "PATH=%PATH%;%ProgramFiles(x86)%\Git\cmd"

:: Add Windows SDK bin to PATH (for rc.exe, mt.exe, etc.)
set "PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
set "PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"
set "PATH=%PATH%;C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64"

:: Re-add Windows SDK paths (vcvarsall may not set them properly)
:: Add Include paths for all installed SDK versions
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\winrt"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\shared"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\um"
set "INCLUDE=%INCLUDE%;C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\winrt"
:: Add Lib paths for all installed SDK versions
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64"
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\ucrt\x64"
set "LIB=%LIB%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\um\x64"
:: Add LibPath for all installed SDK versions
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64"
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64"
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64"
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\ucrt\x64"
set "LIBPATH=%LIBPATH%;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\um\x64"

echo MSVC environment initialized successfully.
exit /b 0
