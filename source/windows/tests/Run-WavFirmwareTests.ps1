param(
    [Parameter(Mandatory=$true)][string]$RomDirectory,
    [Parameter(Mandatory=$true)][string]$OsImage,
    [Parameter(Mandatory=$true)][string]$Module,
    [Parameter(Mandatory=$true)][string]$Probe
)
$ErrorActionPreference = 'Stop'
# No images/ROMs are copied, uploaded, committed or passed to CI.
$env:EIII_ROM_DIR = (Resolve-Path $RomDirectory).Path
$env:EIII_HD_IMAGE = (Resolve-Path $OsImage).Path
$Module = (Resolve-Path $Module).Path
$Probe = (Resolve-Path $Probe).Path
$before = (Get-FileHash $env:EIII_HD_IMAGE -Algorithm SHA256).Hash
$failures = 0
$rates = @(22050,44100,48000,96000,192000,48000)
$case = 0
foreach ($channels in @(1,2)) {
    foreach ($bits in @(16,24,32)) {
        $env:EIII_WAV_TEST_CHANNELS = "$channels"
        $env:EIII_WAV_TEST_BITS = "$bits"
        $env:EIII_WAV_TEST_RATE = "$($rates[$case])"
        $env:EIII_WAV_TEST_QUIET = "$($case % 2)"
        $env:EIII_WAV_TEST_LEAD_SILENCE = "$($case % 2)"
        Write-Host "Case $case channels=$channels bits=$bits rate=$($rates[$case]) quiet/silence=$($case % 2)"
        & $Probe $Module
        if ($LASTEXITCODE -ne 0) { $failures++ }
        if ((Get-FileHash $env:EIII_HD_IMAGE -Algorithm SHA256).Hash -ne $before) {
            throw 'OS image changed: ORIGINAL PROTECTION FAILED'
        }
        $case++
    }
}
if ($failures -gt 0) { throw "$failures of $case firmware cases FAILED; preserve console logs and do not call WAV import verified" }
Write-Host "PASS $case actual packaged firmware cases; original image SHA256 unchanged"
