@echo off
setlocal
set "ELECTRA_PROJECT_ROOT=%~dp0"
call "%~dp0desktop\scripts\run-windows.cmd" user %*
set "RESULT=%errorlevel%"
if not "%RESULT%"=="0" (
  echo.
  echo [Electra] Startup failed with error code %RESULT%.
  echo Please review the message above. This window will remain open.
  pause
)
exit /b %RESULT%
