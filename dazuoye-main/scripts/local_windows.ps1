param([ValidateSet("start","stop","status")][string]$Action = "start")
$ErrorActionPreference = "Stop"
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$PgCtl = Join-Path $Project ".runtime\postgresql14\pgsql\bin\pg_ctl.exe"
$PgData = Join-Path $Project ".runtime\postgresql14\data"
$Python = Join-Path $Project ".venv\Scripts\python.exe"
$PidFile = Join-Path $Project ".runtime\web.pid"
$LogDir = Join-Path $Project "logs"
New-Item -ItemType Directory -Force $LogDir | Out-Null
function Test-Web {
  try { return (Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:4173/health" -TimeoutSec 2).StatusCode -eq 200 }
  catch { return $false }
}
function Test-OwnWeb {
  if (-not (Test-Path $PidFile)) { return $false }
  $webProcessId = [int](Get-Content $PidFile)
  return $null -ne (Get-Process -Id $webProcessId -ErrorAction SilentlyContinue)
}
function Test-Postgres {
  & $PgCtl status -D $PgData *> $null
  return $LASTEXITCODE -eq 0
}
if ($Action -eq "status") {
  Write-Host ("PostgreSQL: " + $(if(Test-Postgres){"running"}else{"stopped"}))
  Write-Host ("Backend: " + $(if((Test-OwnWeb) -and (Test-Web)){"running at http://127.0.0.1:4173"}elseif(Test-Web){"4173 is used by another project"}else{"stopped"}))
  exit 0
}
if ($Action -eq "stop") {
  if (Test-Path $PidFile) {
    $webPid=[int](Get-Content $PidFile)
    Stop-Process -Id $webPid -Force -ErrorAction SilentlyContinue
    Remove-Item $PidFile -Force -ErrorAction SilentlyContinue
  }
  if (Test-Postgres) { & $PgCtl stop -D $PgData -m fast }
  Write-Host "Latest local services stopped."
  exit 0
}
if (-not (Test-Path $PgCtl)) { throw "PostgreSQL runtime missing" }
if (-not (Test-Path $Python)) { throw "Python environment missing" }
if ((Test-Web) -and -not (Test-OwnWeb)) { throw "Port 4173 is occupied by another project. Stop its backend first." }
if (-not (Test-Postgres)) {
  & $PgCtl start -D $PgData -l (Join-Path $LogDir "postgresql.log") -w
  if ($LASTEXITCODE -ne 0) { throw "PostgreSQL failed to start" }
}
Push-Location $Project
try {
  & $Python -m charging_core.cli init-db
  if ($LASTEXITCODE -ne 0) { throw "Database initialization failed" }
  if (-not (Test-OwnWeb)) {
    $out=Join-Path $LogDir "web.stdout.log"
    $err=Join-Path $LogDir "web.stderr.log"
    $p=Start-Process -FilePath $Python -ArgumentList @("-m","charging_core.cli","serve","--host","127.0.0.1","--port","4173") -WorkingDirectory $Project -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
    Set-Content -LiteralPath $PidFile -Value $p.Id
    $ready=$false
    1..20 | ForEach-Object { if(-not $ready){Start-Sleep -Milliseconds 500; $ready=Test-Web} }
    if(-not $ready){throw "Backend failed; see logs\web.stderr.log"}
  }
} finally { Pop-Location }
Write-Host "Latest services ready: http://127.0.0.1:4173"

