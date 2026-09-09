@echo off
set "ELECTRA_PROJECT_ROOT=%~dp0"
if exist "%~dp0desktop\scripts\run-windows.local.cmd" (
  call "%~dp0desktop\scripts\run-windows.local.cmd" user %*
) else (
  call "%~dp0desktop\scripts\run-windows.cmd" user %*
)
