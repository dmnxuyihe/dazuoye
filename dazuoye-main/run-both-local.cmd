@echo off
setlocal
call "%~dp0start-local.cmd"
if errorlevel 1 exit /b 1
set "BUILD=%~dp0desktop\build-release"
set "PATH=D:\Qt\6.5.3\msvc2019_64\bin;%PATH%"
set "QT_OPENGL=software"
set "QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu"
start "ELECTRA User" /D "%BUILD%" "%BUILD%\electra-user.exe" --api http://127.0.0.1:4173
start "ELECTRA Admin" /D "%BUILD%" "%BUILD%\electra-admin.exe" --api http://127.0.0.1:4173
endlocal
