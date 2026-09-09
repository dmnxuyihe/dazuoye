@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\local_windows.ps1" status
if errorlevel 1 pause
