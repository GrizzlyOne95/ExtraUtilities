@echo off
setlocal

if "%~1"=="" (
  echo Usage: %~nx0 ^<path-to-bzr.exe^>
  echo Example: %~nx0 "C:\Program Files (x86)\Steam\steamapps\common\Battlezone 98 Redux\bzr.exe"
  exit /b 2
)

python "%~dp0qualify_bzr_build.py" "%~1" --write-report
set EXITCODE=%ERRORLEVEL%

if %EXITCODE%==0 (
  echo.
  echo BZR executable matches the currently supported EXU build profile.
) else if %EXITCODE%==1 (
  echo.
  echo BZR executable does NOT fully match the supported EXU build profile.
  echo Review the generated EXU_BZR_Compatibility report before enabling native patches.
) else (
  echo.
  echo Qualification could not be completed.
)

exit /b %EXITCODE%
