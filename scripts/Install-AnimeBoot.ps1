<#
.SYNOPSIS
Deploy AnimeBoot.efi and animation assets to the system ESP and register a firmware boot option.

.DESCRIPTION
This script copies the built AnimeBoot EFI application plus its animation payload into \EFI\AnimeBoot
on the EFI System Partition (ESP). It optionally creates or updates a firmware entry that boots
AnimeBoot before Windows Boot Manager, so the EFI app can play the splash and chainload bootmgfw.efi.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EfiBinary,

    [Parameter(Mandatory = $true)]
    [string]$AnimSource,

    [string]$EspMountPoint = "S:",

    [string]$BootEntryDescription = "AnimeBoot Splash",

    [switch]$SkipBcd
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Administrator privileges are required."
    }
}

function Resolve-DriveLetter([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return "S:"
    }
    $normalized = $Value.Trim()
    if ($normalized.Length -eq 1 -and $normalized -match "[A-Za-z]") {
        $normalized = "$normalized:"
    }
    if ($normalized.Length -eq 2 -and $normalized[1] -eq ':') {
        return $normalized.ToUpper()
    }
    throw "ESP mount point must be a drive letter such as 'S:' or 'S:\'."
}

function Mount-Esp([string]$DriveLetter) {
    $drive = Resolve-DriveLetter $DriveLetter
    $mountPath = "$drive\"
    $mountedByScript = $false
    if (-not (Test-Path $mountPath)) {
        Write-Verbose "Mounting ESP at $drive using mountvol."
        & mountvol $drive "/s" | Out-Null
        $mountedByScript = $true
    }
    return [pscustomobject]@{
        Drive = $drive
        Root = $mountPath
        MountedByScript = $mountedByScript
    }
}

function Dismount-Esp($info) {
    if ($info -and $info.MountedByScript) {
        Write-Verbose "Dismounting ESP from $($info.Drive)."
        & mountvol $info.Drive "/d" | Out-Null
    }
}

function Get-ExistingFirmwareEntry([string]$MatchPath) {
    $output = & bcdedit /enum firmware
    $current = $null
    foreach ($line in $output) {
        if ($line -match "identifier\s+(\{.+\})") {
            $current = $matches[1]
        }
        if ($line -match "path\s+(.+)$") {
            $pathValue = $matches[1].Trim()
            if ($pathValue -ieq $MatchPath) {
                return $current
            }
        }
    }
    return $null
}

function Ensure-FirmwareEntry {
    param(
        [string]$Description,
        [string]$DevicePartition,
        [string]$TargetPath
    )

    $entry = Get-ExistingFirmwareEntry -MatchPath $TargetPath
    if (-not $entry) {
        $copyOutput = & bcdedit /copy {bootmgr} /d $Description
        if ($LASTEXITCODE -ne 0) {
            throw "bcdedit /copy failed: $copyOutput"
        }
        if ($copyOutput -match "(\{[0-9a-fA-F\-]+\})") {
            $entry = $matches[1]
        } else {
            throw "Unable to parse identifier from bcdedit output: $copyOutput"
        }
    } else {
        Write-Verbose "Reusing existing firmware entry $entry for $TargetPath."
    }

    & bcdedit /set $entry device "partition=$DevicePartition" | Out-Null
    & bcdedit /set $entry path $TargetPath | Out-Null
    & bcdedit /set $entry description $Description | Out-Null
    & bcdedit /set {fwbootmgr} displayorder $entry /addfirst | Out-Null
    return $entry
}

Assert-Administrator
$resolvedEfi = (Resolve-Path $EfiBinary).ProviderPath
$resolvedAnim = (Resolve-Path $AnimSource).ProviderPath
$espInfo = Mount-Esp -DriveLetter $EspMountPoint

try {
    $targetDir = Join-Path $espInfo.Root "EFI\AnimeBoot"
    if (-not (Test-Path $targetDir)) {
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    }

    $efiTarget = Join-Path $targetDir "AnimeBoot.efi"
    Copy-Item -Path $resolvedEfi -Destination $efiTarget -Force
    Write-Host "Copied EFI binary to $efiTarget"

    if (Test-Path $resolvedAnim -PathType Container) {
        Write-Host "Copying loose frame assets to $targetDir"
        Copy-Item -Path (Join-Path $resolvedAnim "*") -Destination $targetDir -Recurse -Force
    } else {
        $animTarget = Join-Path $targetDir "splash.anim"
        Copy-Item -Path $resolvedAnim -Destination $animTarget -Force
        Write-Host "Copied animation package to $animTarget"
    }

    if (-not $SkipBcd.IsPresent) {
        $entry = Ensure-FirmwareEntry -Description $BootEntryDescription -DevicePartition $espInfo.Drive -TargetPath "\EFI\AnimeBoot\AnimeBoot.efi"
        Write-Host "Firmware entry ready: $entry"
    } else {
        Write-Host "SkipBcd set; remember to point firmware to \EFI\AnimeBoot\AnimeBoot.efi manually."
    }
}
finally {
    Dismount-Esp -info $espInfo
}

