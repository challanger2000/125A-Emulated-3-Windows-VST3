# Owner-local complete installation of BOTH real VST3 variants.
# Only the user's own existing original boot ROM/OS files are ever used.
# No ROM/OS bytes are checked into GitHub or transferred to Actions.
param([string]$SourceResources)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$vstRoot = Join-Path $env:CommonProgramFiles 'VST3'
$osName = 'EIIIOS2_42_HDImage_953MB.ISO'
$romNames = @('e3-lsboot_ip381a_emu_systems_4088.ic3',
              'e3-msboot_ip380a_emu_systems_4088.ic4')
$osExpectedBytes = [int64]998328320
$variants = @(
  @{ Name = 'E-MU Emulator III.vst3' },
  @{ Name = '125A Vintage Sampler III.vst3' }
)
function Check-Bundles {
  foreach($variant in $variants) {
    $dll = Join-Path (Join-Path $root $variant.Name) ('Contents\x86_64-win\' + $variant.Name)
    if(!(Test-Path -LiteralPath $dll -PathType Leaf)) {
      throw "Das komplette VST3-Buildpaket ist unvollstaendig: $dll"
    }
    if((Get-Item -LiteralPath $dll).Length -lt 100000) {
      throw "Zu kleine VST3-Datei, kein gueltiges Plugin: $dll"
    }
  }
}
function Get-ResourcePair([string]$candidate) {
  if(!$candidate -or !(Test-Path -LiteralPath $candidate -PathType Container)) { return $null }
  foreach($res in @($candidate,(Join-Path $candidate 'Contents\Resources'))) {
    $rom = Join-Path $res 'ROM'
    $os = Join-Path $res ('OS\' + $osName)
    if(!(Test-Path -LiteralPath $os -PathType Leaf)) { continue }
    if((Get-Item -LiteralPath $os).Length -ne $osExpectedBytes) { continue }
    $ok = $true
    foreach($name in $romNames) {
      $path = Join-Path $rom $name
      if(!(Test-Path -LiteralPath $path -PathType Leaf) -or
         (Get-Item -LiteralPath $path).Length -eq 0) { $ok=$false; break }
    }
    if($ok) {
      return @{ Resources=$res; Rom=$rom; Os=$os; SourceBundle=$candidate }
    }
  }
  return $null
}
function Find-ExistingResources {
  if($SourceResources) {
    $item=Get-ResourcePair $SourceResources
    if($item) { return $item }
    throw "Der angegebene ROM/OS-Ordner ist unvollstaendig: $SourceResources"
  }
  # Search installed bundles, including the user's old 'VST3\Eigene' path.
  $candidates = New-Object 'System.Collections.Generic.List[string]'
  if(Test-Path -LiteralPath $vstRoot -PathType Container) {
    foreach($item in @(Get-ChildItem -LiteralPath $vstRoot -Directory -ErrorAction SilentlyContinue)) {
      if($item.Name -like '*.vst3') { $candidates.Add($item.FullName) }
      else {
        foreach($child in @(Get-ChildItem -LiteralPath $item.FullName -Directory -ErrorAction SilentlyContinue)) {
          if($child.Name -like '*.vst3') { $candidates.Add($child.FullName) }
        }
      }
    }
  }
  # Check likely previous owners' bundle names first; do not assume these exist.
  $candidates = @($candidates | Sort-Object -Unique)
  foreach($bundle in $candidates) {
    $pair=Get-ResourcePair $bundle
    if($pair) { return $pair }
  }
  return $null
}
function Require-Source {
  $pair=Find-ExistingResources
  if($pair) { return $pair }
  Write-Host '[HINWEIS] Die Original-ROMs/OS sind im VST3-Verzeichnis nicht mehr auffindbar.' -ForegroundColor Yellow
  Write-Host 'Bitte den bereits vorhandenen alten VST3-Bundle-Ordner oder dessen Contents\Resources auswaehlen.'
  Write-Host 'Es wird nichts von einer fremden Webseite heruntergeladen.'
  $shell=New-Object -ComObject Shell.Application
  $folder=$shell.BrowseForFolder(0,'Ordner mit deinen vorhandenen EIII-ROMs und OS auswaehlen',0,0)
  if($folder) {
    $pair=Get-ResourcePair $folder.Self.Path
    if($pair) { return $pair }
  }
  throw 'ROM/OS fehlen. Keine unvollstaendigen VST3s installiert. Bitte vorhandene Originaldateien wieder bereitstellen.'
}
function Assert-Copy([string]$source,[string]$destination) {
  if(!(Test-Path -LiteralPath $destination -PathType Leaf)) {
    throw "Kopie fehlt: $destination"
  }
  if((Get-Item -LiteralPath $source).Length -ne (Get-Item -LiteralPath $destination).Length) {
    throw "Dateigroesse falsch nach Kopieren: $destination"
  }
  if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
     (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
    throw "SHA256-Abweichung: $destination"
  }
}
Check-Bundles
$pair=Require-Source
Write-Host "[QUELLE] Originaldateien gefunden: $($pair.Resources)"
Write-Host '[INFO] Beide Plugins erhalten jeweils eigene ROM/OS-Kopien.'
# Prefer the original per-user VST3 subfolder if a previously installed
# EIII bundle was located there. Avoid duplicate CIDs in root + Eigene.
$parent=$vstRoot
$sourceParent=Split-Path -Parent $pair.SourceBundle
if($sourceParent.StartsWith($vstRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) {
  $parent=$sourceParent
}
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$driveLetter=[System.IO.Path]::GetPathRoot($parent)
$free=(Get-PSDrive -Name $driveLetter.Substring(0,1)).Free
if($free -lt 2400000000) {
  throw 'Mindestens 2,4 GB freier Speicher fuer das sichere Staging beider VST3s erforderlich.'
}
$nonce=[guid]::NewGuid().ToString('N')
$stage=Join-Path $parent ('_125A_EIII_STAGE_'+$nonce)
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$done=New-Object 'System.Collections.Generic.List[object]'
try {
  foreach($variant in $variants) {
    $staged=Join-Path $stage $variant.Name
    $sourceBundle=Join-Path $root $variant.Name
    Copy-Item -LiteralPath $sourceBundle -Destination $staged -Recurse -Force
    $res=Join-Path $staged 'Contents\Resources'
    $destRom=Join-Path $res 'ROM'
    $destOs=Join-Path $res 'OS'
    New-Item -ItemType Directory -Force -Path $destRom,$destOs | Out-Null
    foreach($name in $romNames) {
      $from=Join-Path $pair.Rom $name
      $to=Join-Path $destRom $name
      Copy-Item -LiteralPath $from -Destination $to -Force
      Assert-Copy $from $to
    }
    $osTarget=Join-Path $destOs $osName
    Copy-Item -LiteralPath $pair.Os -Destination $osTarget -Force
    Assert-Copy $pair.Os $osTarget
    $builtDll=Join-Path $sourceBundle ('Contents\x86_64-win\' + $variant.Name)
    $newDll=Join-Path $staged ('Contents\x86_64-win\' + $variant.Name)
    Assert-Copy $builtDll $newDll
    Write-Host "[VOLLSTAENDIG] $($variant.Name): VST3 + 2 ROMs + 953MB OS, alle Hashes korrekt"
  }
  # Only now mutate live plugin paths. Full rollback on any failed move.
  foreach($variant in $variants) {
    $target=Join-Path $parent $variant.Name
    $backup=$target + '.125a-backup-' + $nonce
    $savedOld=$false
    if(Test-Path -LiteralPath $target) {
      Move-Item -LiteralPath $target -Destination $backup
      $savedOld=$true
    }
    $rec=@{ Target=$target; Backup=$backup; SavedOld=$savedOld }
    $done.Add($rec)
    Move-Item -LiteralPath (Join-Path $stage $variant.Name) -Destination $target
    $dll=Join-Path $target ('Contents\x86_64-win\' + $variant.Name)
    if(!(Test-Path -LiteralPath $dll -PathType Leaf) -or
       !(Test-Path -LiteralPath (Join-Path $target ('Contents\Resources\OS\'+$osName)))) {
      throw "Ziel nach Installieren unvollstaendig: $target"
    }
  }
  foreach($rec in $done) {
    if($rec.SavedOld -and (Test-Path -LiteralPath $rec.Backup)) {
      Remove-Item -LiteralPath $rec.Backup -Recurse -Force
    }
  }
  Write-Host '[SUCCESS] BEIDE VST3s vollstaendig mit Original-ROM und OS installiert.' -ForegroundColor Green
  Write-Host "VST3-Zielverzeichnis: $parent"
  Write-Host 'Studio One neu starten und die zwei Instrumente laden.'
}
catch {
  $reason=$_.Exception.Message
  for($i=$done.Count-1;$i -ge 0;$i--) {
    $rec=$done[$i]
    if(Test-Path -LiteralPath $rec.Target) {
      Remove-Item -LiteralPath $rec.Target -Recurse -Force
    }
    if($rec.SavedOld -and (Test-Path -LiteralPath $rec.Backup)) {
      Move-Item -LiteralPath $rec.Backup -Destination $rec.Target
    }
  }
  throw $reason
}
finally {
  if(Test-Path -LiteralPath $stage) {Remove-Item -LiteralPath $stage -Recurse -Force}
}
