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

rem The material ownership test uses only this synthetic retail-ABI DLL.
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /MD /LD "%ROOT%\tests\fixtures\material_reference_fixture.cpp" /Fo"%OUT%\material_reference_fixture.obj" /Fe"%OUT%\OgreMain.dll" /link /DEF:"%ROOT%\tests\fixtures\material_reference_fixture.def" /IMPLIB:"%OUT%\material_reference_fixture.lib"
if errorlevel 1 exit /b 1

set FAILED=0
for %%f in ("%ROOT%\tests\host\*.cpp") do call :runtest "%%f"

exit /b %FAILED%

rem Tests named native_chunk_* exercise the vendored sources in
rem src\Gibs\NativeChunks, so those are compiled and linked in with them.
:runtest
set "TESTNAME=%~n1"
set "EXTRA="
echo %TESTNAME%| findstr /b /c:"native_chunk_" >nul && set EXTRA="%ROOT%\src\Gibs\NativeChunks\*.cpp"
if not exist "%OUT%\%TESTNAME%.objs" mkdir "%OUT%\%TESTNAME%.objs"
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /I "%ROOT%\src" /Fe"%OUT%\%TESTNAME%.exe" /Fo"%OUT%\%TESTNAME%.objs\\" %1 %EXTRA% >"%OUT%\%TESTNAME%.log"
if errorlevel 1 (
    type "%OUT%\%TESTNAME%.log"
    echo FAIL: compile %~nx1 1>&2
    set FAILED=1
    exit /b 0
)
"%OUT%\%TESTNAME%.exe"
if errorlevel 1 (
    echo FAIL: %TESTNAME% 1>&2
    set FAILED=1
)
exit /b 0
