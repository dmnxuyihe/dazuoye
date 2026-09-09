param([Parameter(Mandatory=$true)][string]$QtRoot, [string]$Output = "$PSScriptRoot\..\build-windows")
$ErrorActionPreference = 'Stop'
# Run from the x64 Native Tools Command Prompt / Developer PowerShell for VS 2022.
$Source = (Resolve-Path "$PSScriptRoot\..").Path
cmake -S $Source -B $Output -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $Output --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
$Bundle = Join-Path $Output 'Electra-Windows'
New-Item -ItemType Directory -Force $Bundle | Out-Null
Copy-Item "$Output\electra-user.exe", "$Output\electra-admin.exe" $Bundle
& "$QtRoot\bin\windeployqt.exe" --release --no-translations "$Bundle\electra-user.exe" "$Bundle\electra-admin.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed' }
Copy-Item "$Source\licenses" $Bundle -Recurse -Force
Write-Host "Output: $Bundle"
Write-Host 'Run electra-admin.exe --api https://lv-l40s-liuzihang.taild6df1c.ts.net:8443'
