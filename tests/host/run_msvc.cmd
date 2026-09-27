@echo off
rem Compiles and runs every tests\host program with 32-bit MSVC, the compiler
rem exu.dll ships with. tests\linux\run.sh builds the same sources with g++.
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found; install Visual Studio or the Build Tools. 1>&2
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No Visual Studio installation with the x86 C++ tools was found. 1>&2
    exit /b 1
)

rem vcvars32 prints noise on stderr on some installs; check for cl instead.
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
where cl >nul 2>&1 || (
    echo vcvars32.bat did not put cl.exe on PATH. 1>&2
    exit /b 1
)

set "ROOT=%~dp0..\.."
set "OUT=%TEMP%\exu-host-tests"
if not exist "%OUT%" mkdir "%OUT%" || exit /b 1

set FAILED=0
for %%f in ("%ROOT%\tests\host\*.cpp") do (
    cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /I "%ROOT%\src" /Fe"%OUT%\%%~nf.exe" /Fo"%OUT%\\" "%%f" >"%OUT%\%%~nf.log"
    if errorlevel 1 (
        type "%OUT%\%%~nf.log"
        echo FAIL: compile %%~nxf 1>&2
        set FAILED=1
    ) else (
        "%OUT%\%%~nf.exe"
        if errorlevel 1 (
            echo FAIL: %%~nf 1>&2
            set FAILED=1
        )
    )
)

exit /b %FAILED%
