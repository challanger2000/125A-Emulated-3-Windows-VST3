$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$exe = Join-Path $here 'eiii_d0_controller_test.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) {
  Write-Host '[FAIL] Die zum Paket gehoerende Test-EXE fehlt.' -ForegroundColor Red
  exit 2
}
$log = Join-Path $here 'D0-Console.txt'
Push-Location -LiteralPath $here
try {
  "125A EIII D0 synthetic test at $(Get-Date -Format o)" | Set-Content -LiteralPath $log
  "EXE SHA256 $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)" | Add-Content -LiteralPath $log
  Write-Host '[INFO] Test erzeugt temporaere IMG- und HFE-Dateien selbst.'
  Write-Host '[INFO] Keine Original-ROMs, Samples oder echte Disketten erforderlich.'
  $old = $ErrorActionPreference
  try {
    $ErrorActionPreference = 'Continue'
    & $exe 2>&1 | ForEach-Object {
      if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message }
      else { [string]$_ }
    } | Tee-Object -FilePath $log -Append | Out-Host
    $code = $LASTEXITCODE
  } finally { $ErrorActionPreference = $old }
  if ($null -eq $code) { $code = 1 }
  exit ([int]$code)
} finally {
  Pop-Location
}
