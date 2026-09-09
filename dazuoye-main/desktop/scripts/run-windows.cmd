@echo off
setlocal

set "ROLE=%~1"
if /I not "%ROLE%"=="user" if /I not "%ROLE%"=="admin" (
  echo Usage: %~nx0 user^|admin [application arguments]
  exit /b 2
)
shift

if defined ELECTRA_PROJECT_ROOT (
  set "PROJECT_ROOT=%ELECTRA_PROJECT_ROOT%"
) else (
  set "PROJECT_ROOT=%~dp0..\.."
)
set "SOURCE_DIR=%PROJECT_ROOT%\desktop"
set "BUILD_DIR=%PROJECT_ROOT%\.runtime\qt-build-windows"

if defined ELECTRA_QT_ROOT (
  set "QT_ROOT=%ELECTRA_QT_ROOT%"
) else (
  set "QT_ROOT=D:\SoftWare\Qt\6.5.3\msvc2019_64"
)
if defined ELECTRA_CMAKE (
  set "CMAKE=%ELECTRA_CMAKE%"
) else (
  set "CMAKE=D:\SoftWare\Qt\Tools\CMake_64\bin\cmake.exe"
)
if defined ELECTRA_NINJA (
  set "NINJA=%ELECTRA_NINJA%"
) else (
  set "NINJA=D:\SoftWare\Qt\Tools\Ninja\ninja.exe"
)
if defined ELECTRA_VCVARS64 (
  set "VCVARS64=%ELECTRA_VCVARS64%"
) else (
  set "VCVARS64=D:\SoftWare\VSdownload\VC\Auxiliary\Build\vcvars64.bat"
)
if defined ELECTRA_API_URL (
  set "API_URL=%ELECTRA_API_URL%"
) else (
  set "API_URL=https://lv-l40s-liuzihang.taild6df1c.ts.net:8443"
)

if not exist "%QT_ROOT%\bin\Qt6Core.dll" goto :missing_qt
if not exist "%CMAKE%" goto :missing_cmake
if not exist "%NINJA%" goto :missing_ninja
if not exist "%VCVARS64%" goto :missing_msvc

call "%VCVARS64%" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %errorlevel%
set "PATH=%QT_ROOT%\bin;%PATH%"

echo [Electra] Configuring latest source tree...
"%CMAKE%" -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" "-DCMAKE_PREFIX_PATH=%QT_ROOT%" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
if errorlevel 1 exit /b %errorlevel%

echo [Electra] Closing previous %ROLE% client...
rem Close old copies before linking; Windows does not allow replacing a running exe.
powershell.exe -NoProfile -NonInteractive -Command "Get-Process 'electra-%ROLE%' -ErrorAction SilentlyContinue | Stop-Process -Force"

echo [Electra] Building %ROLE% client...
"%CMAKE%" --build "%BUILD_DIR%" --target "electra-%ROLE%" --parallel 4
if errorlevel 1 exit /b %errorlevel%

if not exist "%PROJECT_ROOT%\.runtime" mkdir "%PROJECT_ROOT%\.runtime"
echo [Electra] Checking data service %API_URL%...
powershell.exe -NoProfile -NonInteractive -Command "try { $response = Invoke-WebRequest -UseBasicParsing -Uri '%API_URL%/health' -TimeoutSec 15; if ($response.StatusCode -ne 200) { Write-Host ('HTTP status: ' + $response.StatusCode); exit 1 }; Write-Host ('[Electra] Service ready: HTTP ' + $response.StatusCode) } catch { Write-Host ('[Electra] Service error: ' + $_.Exception.Message); exit 1 }"
if errorlevel 1 (
  echo [Electra] Data service is unavailable. Check the network or set ELECTRA_API_URL.
  exit /b 4
)
echo [Electra] Starting %ROLE% client...
start "Electra %ROLE%" /D "%BUILD_DIR%" "%BUILD_DIR%\electra-%ROLE%.exe" --api "%API_URL%" --cache "%PROJECT_ROOT%\.runtime\qt-%ROLE%.sqlite" %*
exit /b 0

:missing_qt
echo Qt 6.5.3 MSVC was not found at "%QT_ROOT%".
echo Set ELECTRA_QT_ROOT to the Qt kit directory and retry.
exit /b 3
:missing_cmake
echo CMake was not found at "%CMAKE%".
exit /b 3
:missing_ninja
echo Ninja was not found at "%NINJA%".
exit /b 3
:missing_msvc
echo Visual Studio x64 tools were not found at "%VCVARS64%".
exit /b 3
