<#
.SYNOPSIS
Deploy AnimeBoot.efi and animation assets to the system ESP or dedicated FAT32 partition and register a firmware boot option.

.DESCRIPTION
This script copies the built AnimeBoot EFI application plus its animation payload into \EFI\AnimeBoot
on the EFI System Partition (ESP) or a dedicated FAT32 partition. It optionally creates or updates a firmware entry
that boots AnimeBoot before Windows Boot Manager, so the EFI app can play the splash and chainload bootmgfw.efi.

When using a dedicated partition, the script can automatically create and format a FAT32 partition for animation storage.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EfiBinary,

    [Parameter(Mandatory = $true)]
    [string]$AnimSource,

    [string]$EspMountPoint = "S:",

    [string]$BootEntryDescription = "AnimeBoot Splash",

    [switch]$SkipBcd,

    [switch]$UseDedicatedPartition,

    [string]$PartitionLabel = "ANIMDATA",

    [uint64]$PartitionSizeGB = 2,

    [switch]$AutoCreatePartition,

    [switch]$ListPartitions
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
        $normalized = $normalized + ":"
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

function Find-UnallocatedSpace {
    $disks = Get-Disk | Where-Object { $_.PartitionStyle -eq "GPT" }
    $results = @()

    foreach ($disk in $disks) {
        $partitions = $disk | Get-Partition
        $diskSize = $disk.Size

        # Sort partitions by offset
        $sortedPartitions = $partitions | Sort-Object -Property Offset

        $currentOffset = 0
        foreach ($partition in $sortedPartitions) {
            $gapSize = $partition.Offset - $currentOffset
            if ($gapSize -gt 100MB) {  # Minimum 100MB gap
                $results += [pscustomobject]@{
                    DiskNumber = $disk.Number
                    StartOffset = $currentOffset
                    Size = $gapSize
                    EndOffset = $partition.Offset
                }
            }
            $currentOffset = $partition.Offset + $partition.Size
        }

        # Check space after last partition
        $finalGap = $diskSize - $currentOffset
        if ($finalGap -gt 100MB) {
            $results += [pscustomobject]@{
                DiskNumber = $disk.Number
                StartOffset = $currentOffset
                Size = $finalGap
                EndOffset = $diskSize
            }
        }
    }

    return $results
}

function Create-AnimationPartition {
    param(
        [string]$Label,
        [uint64]$SizeGB
    )

    Write-Host "Searching for available disk space..."
    $freeSpaces = Find-UnallocatedSpace

    if ($freeSpaces.Count -eq 0) {
        throw "No suitable unallocated space found. Please manually create a FAT32 partition."
    }

    # Find the largest suitable space
    $selectedSpace = $freeSpaces | Where-Object { $_.Size -ge ($SizeGB * 1GB) } | Sort-Object -Property Size -Descending | Select-Object -First 1

    if (-not $selectedSpace) {
        $maxAvailable = ($freeSpaces | Measure-Object -Property Size -Maximum).Maximum / 1GB
        throw "No space large enough for ${SizeGB}GB partition. Maximum available: ${maxAvailable}GB"
    }

    $sizeBytes = [math]::Min($SizeGB * 1GB, $selectedSpace.Size - 100MB)  # Leave some margin

    Write-Host "Creating ${SizeGB}GB FAT32 partition on disk $($selectedSpace.DiskNumber)..."
    $partition = New-Partition -DiskNumber $selectedSpace.DiskNumber -Size $sizeBytes -Offset $selectedSpace.StartOffset

    Write-Host "Formatting partition as FAT32..."
    $formattedVolume = Format-Volume -Partition $partition -FileSystem FAT32 -NewFileSystemLabel $Label -Confirm:$false

    Write-Host "FAT32 partition created successfully: $($formattedVolume.DriveLetter):"
    return [pscustomobject]@{
        DriveLetter = $formattedVolume.DriveLetter + ":"
        Label = $Label
        SizeGB = $SizeGB
    }
}

function Find-ExistingAnimationPartition {
    param([string]$Label)

    $volumes = Get-Volume | Where-Object { $_.FileSystem -eq "FAT32" -and $_.FileSystemLabel -eq $Label }
    if ($volumes) {
        $volume = $volumes[0]

        # Check if volume already has a drive letter
        if ($volume.DriveLetter) {
            $partition = Get-Partition | Where-Object { $_.AccessPaths -contains $volume.DriveLetter + ":\" }
            return [pscustomobject]@{
                DriveLetter = $volume.DriveLetter + ":"
                Label = $Label
                SizeGB = [math]::Round($volume.Size / 1GB, 2)
                Volume = $volume
            }
        } else {
            # Volume exists but no drive letter - find the partition
            $partition = Get-Partition | Where-Object { $_.Guid -eq $volume.UniqueId.Trim('{}') }
            if ($partition) {
                return [pscustomobject]@{
                    DriveLetter = $null
                    Label = $Label
                    SizeGB = [math]::Round($volume.Size / 1GB, 2)
                    Volume = $volume
                    Partition = $partition
                }
            }
        }
    }
    return $null
}

function Mount-PartitionByLabel {
    param([string]$Label)

    $partition = Find-ExistingAnimationPartition -Label $Label
    if ($partition) {
        # If no drive letter, assign one temporarily
        if (-not $partition.DriveLetter) {
            Write-Host "Found partition '$Label' but no drive letter assigned. Assigning temporary drive letter..."
            $driveLetter = Get-AvailableDriveLetter
            if (-not $driveLetter) {
                throw "No available drive letters to assign to partition '$Label'."
            }

            try {
                # Use Add-PartitionAccessPath to assign drive letter
                Add-PartitionAccessPath -DiskNumber $partition.Partition.DiskNumber -PartitionNumber $partition.Partition.PartitionNumber -AccessPath "${driveLetter}:"
                Write-Host "Assigned drive letter ${driveLetter}: to partition '$Label'"

                # Update the partition object and set flag
                $partition.DriveLetter = "${driveLetter}:"
                $tempDriveAssigned = $true
            }
            catch {
                throw "Failed to assign drive letter to partition '$Label': $($_.Exception.Message)"
            }
        }
        return $partition
    }

    if ($AutoCreatePartition) {
        return Create-AnimationPartition -Label $Label -SizeGB $PartitionSizeGB
    }

    throw "Animation partition '$Label' not found. Use -AutoCreatePartition to create one automatically."
}

function Get-AvailableDriveLetter {
    # Get all currently used drive letters
    $usedLetters = (Get-Volume | Where-Object { $_.DriveLetter }).DriveLetter

    # Find first available letter from Z to A (reverse order to avoid common letters)
    for ($i = 90; $i -ge 65; $i--) {
        $letter = [char]$i
        if ($usedLetters -notcontains $letter) {
            return $letter
        }
    }

    return $null
}

function Show-AvailablePartitions {
    Write-Host "Available FAT32 partitions:"
    Write-Host "------------------------"

    $volumes = Get-Volume | Where-Object { $_.FileSystem -eq "FAT32" } | Sort-Object -Property Size -Descending

    if ($volumes.Count -eq 0) {
        Write-Host "No FAT32 partitions found."
        return
    }

    foreach ($volume in $volumes) {
        $driveLetter = if ($volume.DriveLetter) { "$($volume.DriveLetter):" } else { "(no letter)" }
        $sizeGB = [math]::Round($volume.Size / 1GB, 2)
        $label = if ($volume.FileSystemLabel) { $volume.FileSystemLabel } else { "(no label)" }

        Write-Host ("{0,-12} {1,-12} {2,-8}GB {3}" -f "Drive:", "Label:", "Size:", "Status:")
        Write-Host ("{0,-12} {1,-12} {2,-8}GB {3}" -f $driveLetter, $label, $sizeGB,
            $(if ($volume.DriveLetter) { "Accessible" } else { "No drive letter" }))
        Write-Host ""
    }

    Write-Host "To use a partition without drive letter, specify its label with -PartitionLabel"
    Write-Host "Example: -PartitionLabel `"$($volumes[0].FileSystemLabel)`""
}

function Create-ConfigFile {
    param(
        [string]$EspRoot,
        [string]$AnimPath
    )

    $configDir = Join-Path $EspRoot "EFI\AnimeBoot"
    if (-not (Test-Path $configDir)) {
        New-Item -ItemType Directory -Force -Path $configDir | Out-Null
    }
    $configPath = Join-Path $configDir "config.json"

    # Determine if animation is in package or loose files format
    $isPackage = $AnimPath -like "*.anim"

    if ($isPackage) {
        $config = @{
            animation_path = "$($PartitionLabel):\\animations\\splash.anim"
            manifest_path = "$($PartitionLabel):\\animations\\sequence.anim.json"
        }
    } else {
        $config = @{
            animation_path = "$($PartitionLabel):\\animations\\sequence.anim.json"
        }
    }

    $configJson = $config | ConvertTo-Json
    $configJson | Out-File -FilePath $configPath -Encoding UTF8 -Force

    Write-Host "Created configuration file: $configPath"
    Write-Host "Configuration content:"
    Write-Host $configJson
}

Assert-Administrator

# Handle list partitions request
if ($ListPartitions) {
    Show-AvailablePartitions
    exit 0
}

$resolvedEfi = (Resolve-Path $EfiBinary).ProviderPath
$resolvedAnim = (Resolve-Path $AnimSource).ProviderPath
$espInfo = Mount-Esp -DriveLetter $EspMountPoint
    $animPartition = $null
    $tempDriveAssigned = $false

try {
    # Handle dedicated partition setup
    if ($UseDedicatedPartition) {
        Write-Host "Setting up dedicated FAT32 partition for animations..."
        $animPartition = Mount-PartitionByLabel -Label $PartitionLabel

        Write-Host "Animation partition ready: $($animPartition.DriveLetter) ($($animPartition.Label))"

        # Create animations directory on the partition
        $animDir = Join-Path $animPartition.DriveLetter "animations"
        if (-not (Test-Path $animDir)) {
            New-Item -ItemType Directory -Force -Path $animDir | Out-Null
        }

        # Copy animation files to dedicated partition
        if (Test-Path $resolvedAnim -PathType Container) {
            Write-Host "Copying loose frame assets to $animDir"
            Copy-Item -Path (Join-Path $resolvedAnim "*") -Destination $animDir -Recurse -Force
        } else {
            $animTarget = Join-Path $animDir "splash.anim"
            Copy-Item -Path $resolvedAnim -Destination $animTarget -Force
            Write-Host "Copied animation package to $animTarget"
        }
    }

    # Setup EFI directory
    $targetDir = Join-Path $espInfo.Root "EFI\AnimeBoot"
    if (-not (Test-Path $targetDir)) {
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    }

    # Copy EFI binary to ESP
    $efiTarget = Join-Path $targetDir "AnimeBoot.efi"
    Copy-Item -Path $resolvedEfi -Destination $efiTarget -Force
    Write-Host "Copied EFI binary to $efiTarget"

    # Copy animation files to ESP if not using dedicated partition
    if (-not $UseDedicatedPartition) {
        if (Test-Path $resolvedAnim -PathType Container) {
            Write-Host "Copying loose frame assets to $targetDir"
            Copy-Item -Path (Join-Path $resolvedAnim "*") -Destination $targetDir -Recurse -Force
        } else {
            $animTarget = Join-Path $targetDir "splash.anim"
            Copy-Item -Path $resolvedAnim -Destination $animTarget -Force
            Write-Host "Copied animation package to $animTarget"
        }
    }

    # Create configuration file if using dedicated partition
    if ($UseDedicatedPartition) {
        Create-ConfigFile -EspRoot $espInfo.Root -AnimPath $resolvedAnim
    }

    # Handle firmware boot entry
    if (-not $SkipBcd.IsPresent) {
        $entry = Ensure-FirmwareEntry -Description $BootEntryDescription -DevicePartition $espInfo.Drive -TargetPath "\EFI\AnimeBoot\AnimeBoot.efi"
        Write-Host "Firmware entry ready: $entry"
    } else {
        Write-Host "SkipBcd set; remember to point firmware to \EFI\AnimeBoot\AnimeBoot.efi manually."
    }

    # Summary
    Write-Host ""
    Write-Host "Deployment Summary:"
    Write-Host "- EFI binary: $efiTarget"
    if ($UseDedicatedPartition) {
        Write-Host "- Animation partition: $($animPartition.DriveLetter) ($($animPartition.Label))"
        Write-Host "- Configuration: $(Join-Path $espInfo.Root "EFI\AnimeBoot\config.json")"
    } else {
        Write-Host "- Animation location: ESP ($($espInfo.Drive))"
    }
}
finally {
    # Clean up temporary drive letter assignment
    if ($tempDriveAssigned -and $animPartition -and $animPartition.DriveLetter) {
        try {
            Write-Verbose "Removing temporary drive letter assignment: $($animPartition.DriveLetter)"
            Remove-PartitionAccessPath -DiskNumber $animPartition.Partition.DiskNumber -PartitionNumber $animPartition.Partition.PartitionNumber -AccessPath $animPartition.DriveLetter
        }
        catch {
            Write-Warning "Failed to remove temporary drive letter $($animPartition.DriveLetter): $($_.Exception.Message)"
        }
    }

    Dismount-Esp -info $espInfo
}

