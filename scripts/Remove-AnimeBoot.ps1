<#
.SYNOPSIS
Remove the AnimeBoot firmware boot option and optionally delete ESP files.
#>
[CmdletBinding()]
param(
    [string]$EspMountPoint = "S:",

    [switch]$RemoveFiles
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
    throw "ESP mount point must be a drive letter."
}

function Mount-Esp([string]$DriveLetter) {
    $drive = Resolve-DriveLetter $DriveLetter
    $mountPath = "$drive\"
    $mountedByScript = $false
    if (-not (Test-Path $mountPath)) {
        Write-Verbose "Mounting ESP at $drive"
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
        & mountvol $info.Drive "/d" | Out-Null
    }
}

function Find-AnimeBootEntry {
    $output = & bcdedit /enum firmware
    $current = $null
    foreach ($line in $output) {
        if ($line -match "identifier\s+(\{.+\})") {
            $current = $matches[1]
        }
        if ($line -match "path\s+(.+)$") {
            $pathValue = $matches[1].Trim()
            if ($pathValue -ieq "\EFI\AnimeBoot\AnimeBoot.efi") {
                return $current
            }
        }
    }
    return $null
}

Assert-Administrator
$espInfo = Mount-Esp -DriveLetter $EspMountPoint

try {
    $entry = Find-AnimeBootEntry
    if ($entry) {
        Write-Host "Removing firmware entry $entry"
        & bcdedit /set {fwbootmgr} displayorder $entry /remove | Out-Null
        & bcdedit /delete $entry | Out-Null
    } else {
        Write-Host "No AnimeBoot firmware entry detected."
    }

    if ($RemoveFiles.IsPresent) {
        $targetDir = Join-Path $espInfo.Root "EFI\AnimeBoot"
        if (Test-Path $targetDir) {
            Remove-Item -Recurse -Force $targetDir
            Write-Host "Deleted $targetDir"
        }
    }
}
finally {
    Dismount-Esp -info $espInfo
}

