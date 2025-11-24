<#
.SYNOPSIS
Sign AnimeBoot.efi with an Authenticode certificate using signtool.exe.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputEfi,

    [string]$OutputEfi,

    [Parameter(Mandatory = $true)]
    [string]$PfxPath,

    [string]$PfxPassword,

    [string]$TimestampServer = "http://timestamp.digicert.com",

    [string]$SigntoolPath = "signtool.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Executable([string]$Name) {
    if ([System.IO.Path]::IsPathRooted($Name)) {
        if (-not (Test-Path $Name)) {
            throw "Executable not found: $Name"
        }
        return $Name
    }
    $command = Get-Command $Name -ErrorAction Stop
    return $command.Source
}

$signTool = Resolve-Executable $SigntoolPath
$resolvedInput = (Resolve-Path $InputEfi).ProviderPath
$resolvedPfx = (Resolve-Path $PfxPath).ProviderPath

$targetPath = if ($OutputEfi) {
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputEfi)
    $parentDir = Split-Path -Path $resolvedOutput -Parent
    if ($parentDir -and -not (Test-Path $parentDir)) {
        New-Item -ItemType Directory -Path $parentDir -Force | Out-Null
    }
    Copy-Item -Path $resolvedInput -Destination $resolvedOutput -Force
    $resolvedOutput
} else {
    $resolvedInput
}

$arguments = @("sign", "/fd", "SHA256", "/f", $resolvedPfx)
if ($PfxPassword) {
    $arguments += @("/p", $PfxPassword)
}
$arguments += @("/tr", $TimestampServer, "/td", "SHA256", "/v", $targetPath)

& $signTool @arguments
if ($LASTEXITCODE -ne 0) {
    throw "signtool exited with $LASTEXITCODE"
}

Write-Host "Signed image saved to $targetPath"

