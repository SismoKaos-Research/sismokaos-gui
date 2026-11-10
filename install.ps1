# Installs or updates sismokaos-gui on Windows (x86_64) from the GitHub
# releases. No other software is needed.
#
#   irm https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.ps1 | iex
#
#   .\install.ps1                    the latest release (a no-op when already current)
#   .\install.ps1 -Version 0.2.0     that release
#   .\install.ps1 -Force             reinstall the same version
#   .\install.ps1 -Uninstall         remove everything the script installed
#   .\install.ps1 -WaitPid N -Relaunch   used by the application's own updater
#   $env:SISMOKAOS_VERSION           the same as -Version, for the one-liner
#
# Installs under $env:SISMOKAOS_HOME (default %LOCALAPPDATA%\Programs\sismokaos-gui),
# a Start Menu shortcut, and a copy of this script as update.ps1 beside the
# application. User data (settings, campaigns, results) is never touched.
[CmdletBinding()]
param(
  [string]$Version = $env:SISMOKAOS_VERSION,
  [switch]$Force,
  [switch]$Uninstall,
  [int]$WaitPid = 0,
  [int]$WaitSeconds = 60,
  [switch]$Relaunch,
  # Stops before anything is downloaded or changed (used by the tests).
  [switch]$DryRun
)
$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = "SismoKaos-Research/sismokaos-gui"
$Raw = "https://raw.githubusercontent.com/$Repo/main/install.ps1"
$HomeDir = if ($env:SISMOKAOS_HOME) { $env:SISMOKAOS_HOME } else { Join-Path $env:LOCALAPPDATA "Programs\sismokaos-gui" }
$Exe = Join-Path $HomeDir "sismokaos-gui.exe"
$Updater = Join-Path $HomeDir "update.ps1"
$VersionFile = Join-Path $HomeDir "VERSION"
$Shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\sismokaos-gui.lnk"

if ($WaitPid -gt 0) {
  # Called from the running application: let it exit first.
  for ($i = 0; $i -lt $WaitSeconds -and (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
  # Still there (it was asked to stay open, or it is finishing its jobs):
  # replacing an application that runs would break it, so nothing is touched.
  if (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue) {
    Write-Host "error: sismokaos-gui (process $WaitPid) is still running after $WaitSeconds s; nothing was changed. Close it and update again."
    exit 3
  }
}

if ($DryRun) {
  Write-Host "dry run: nothing downloaded, nothing changed (installation: $HomeDir)."
  exit 0
}

if ($Uninstall) {
  if (Test-Path $HomeDir) { Remove-Item -Recurse -Force $HomeDir }
  if (Test-Path $Shortcut) { Remove-Item -Force $Shortcut }
  Write-Host "sismokaos-gui removed (user data kept)."
  return
}

if (-not $Version) {
  $latest = Invoke-RestMethod -Headers @{ Accept = "application/vnd.github+json"; "User-Agent" = "sismokaos-gui-installer" } `
    -Uri "https://api.github.com/repos/$Repo/releases/latest"
  $Version = $latest.tag_name -replace '^v', ''
  if (-not $Version) { throw "could not read the latest release of $Repo" }
}
$Version = $Version -replace '^v', ''
$Have = if (Test-Path $VersionFile) { (Get-Content $VersionFile -Raw).Trim() } else { "" }
if ($Have -eq $Version -and -not $Force) {
  Write-Host "sismokaos-gui $Version is already installed in $HomeDir."
  if ($Relaunch) { Start-Process $Exe }
  return
}

$Asset = "sismokaos-gui-$Version-windows-x86_64.zip"
$Base = "https://github.com/$Repo/releases/download/v$Version"
$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("sismokaos-gui-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $Tmp | Out-Null
try {
  Write-Host "Downloading $Asset ..."
  Invoke-WebRequest -Uri "$Base/$Asset" -OutFile (Join-Path $Tmp $Asset) -UseBasicParsing
  Invoke-WebRequest -Uri "$Base/SHA256SUMS" -OutFile (Join-Path $Tmp "SHA256SUMS") -UseBasicParsing
  $line = Get-Content (Join-Path $Tmp "SHA256SUMS") | Where-Object { $_ -match "\s$([regex]::Escape($Asset))$" }
  if (-not $line) { throw "$Asset is not listed in SHA256SUMS" }
  $expected = ($line -split '\s+')[0].ToLower()
  $actual = (Get-FileHash -Algorithm SHA256 (Join-Path $Tmp $Asset)).Hash.ToLower()
  if ($actual -ne $expected) { throw "checksum mismatch for $Asset; not installed" }

  Expand-Archive -Path (Join-Path $Tmp $Asset) -DestinationPath (Join-Path $Tmp "unpacked") -Force
  $inner = Get-ChildItem (Join-Path $Tmp "unpacked") -Directory | Select-Object -First 1
  $source = if ($inner) { $inner.FullName } else { Join-Path $Tmp "unpacked" }

  # The new version goes beside the old one first, then the two are swapped:
  # the old one is only removed once the new one is in place. A file of the
  # old one that is still in use makes the rename fail as a whole, with
  # nothing deleted, and the old version stays as it was.
  # This script may itself live in the installation that is about to move.
  $Self = $null
  if ($PSCommandPath -and (Test-Path $PSCommandPath)) {
    $Self = Join-Path $Tmp "self.ps1"
    Copy-Item $PSCommandPath $Self -Force
  }
  $New = $HomeDir + ".new"
  $Old = $HomeDir + ".old"
  if (Test-Path $New) { Remove-Item -Recurse -Force $New }
  if (Test-Path $Old) { Remove-Item -Recurse -Force $Old }
  New-Item -ItemType Directory -Path (Split-Path $HomeDir) -Force | Out-Null
  Move-Item -Path $source -Destination $New
  if (Test-Path $HomeDir) {
    try { Move-Item -Path $HomeDir -Destination $Old }
    catch {
      Remove-Item -Recurse -Force $New -ErrorAction SilentlyContinue
      throw "the installed version is in use and was left as it is ($($_.Exception.Message)); close sismokaos-gui and update again"
    }
  }
  try { Move-Item -Path $New -Destination $HomeDir }
  catch {
    if (Test-Path $Old) { Move-Item -Path $Old -Destination $HomeDir }
    Remove-Item -Recurse -Force $New -ErrorAction SilentlyContinue
    throw "could not put the new version in place; the installed version was put back ($($_.Exception.Message))"
  }
  if (Test-Path $Old) { Remove-Item -Recurse -Force $Old -ErrorAction SilentlyContinue }

  # Keep this script beside the application as the updater.
  if ($Self -and (Test-Path $Self)) { Copy-Item $Self $Updater -Force }
  elseif (-not (Test-Path $Updater)) { try { Invoke-WebRequest -Uri $Raw -OutFile $Updater -UseBasicParsing } catch {} }
  Set-Content -Path $VersionFile -Value $Version

  $shell = New-Object -ComObject WScript.Shell
  New-Item -ItemType Directory -Path (Split-Path $Shortcut) -Force | Out-Null
  $lnk = $shell.CreateShortcut($Shortcut)
  $lnk.TargetPath = $Exe
  $lnk.WorkingDirectory = $HomeDir
  $lnk.Description = "Seismic feature extraction, classification and AFAD/TDVMS downloads"
  $lnk.Save()
} finally {
  Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}

Write-Host "sismokaos-gui $Version installed in $HomeDir (Start Menu: sismokaos-gui)."
Write-Host "Update later with: powershell -ExecutionPolicy Bypass -File `"$Updater`""
if ($Relaunch) { Start-Process $Exe }
