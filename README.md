# AnimeBoot - UEFI Boot Animation Player

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

AnimeBoot is a proof-of-concept project that plays animations (looking like animated GIFs) before Windows Boot Manager or custom UEFI bootloaders, making your PC startup process more personalized.

📖 [中文文档](README.zh-CN.md)

## System Requirements

### Development Environment
- **Operating System**: Windows 10/11 (x64)
- **Python**: 3.10+
- **EDK II**: 2022+
- **Visual Studio**: 2019/2022 (for EDK II builds)
- **PowerShell**: 5.1+

### Runtime Environment
- **UEFI Firmware**: Supports GOP (Graphics Output Protocol)
- **Architecture**: x86_64 (AMD64/Intel64)
- **Storage**: ESP (EFI System Partition) with at least 50MB available space
- **Secure Boot**: Optional support (complete signing solution provided)

## Quick Start

### 1. Build Host Tools

```bash
# Install abtool
cd host-tools/abtool
pip install -e .

# Verify installation
abtool --help
```

### 2. Build UEFI Application

```bash
# Set up EDK II environment (assuming EDK2_WORKSPACE is set)
cd AnimeBootPkg
edksetup.bat Rebuild

# Build Release version
build -p AnimeBootPkg\AnimeBootPkg.dsc -b RELEASE -a X64 -t VS2022
```

### 3. Convert Animation Resources

```bash
# Create animation package from GIF
abtool extract my_animation.gif build_frames --width 640 --height 360 --fps 24
abtool pack build_frames/sequence.anim.json build/splash.anim
```

### 4. Deploy to System

```powershell
# Run PowerShell as administrator
.\scripts\Install-AnimeBoot.ps1 `
    -EfiBinary "Build\AnimeBoot\RELEASE_X64\AnimeBootPkg\Application\AnimeBoot\AnimeBoot\AnimeBoot.efi" `
    -AnimSource "build\splash.anim" `
    -BootEntryDescription "AnimeBoot Splash"
```

## Usage

### Animation Resource Preparation

AnimeBoot supports multiple input formats:

- **Static Images**: JPG, PNG, BMP → Converted to single-frame animations
- **Animated GIF**: Automatically extracts all frames while preserving timeline
- **APNG**: Similar to GIF, but supports better compression
- **Video Files**: MP4, MOV, AVI, etc. → Sampled at specified FPS

#### Conversion Examples

```bash
# Basic conversion (default 640x360, 24fps)
abtool extract animation.gif output_frames

# Custom resolution and frame rate
abtool extract video.mp4 frames --width 800 --height 450 --fps 30

# Specify scaling mode and background color
abtool extract splash.png output --scaling letterbox --background "#001122"
```

#### Scaling Mode Description

- `letterbox` (default): Maintains aspect ratio, adds black bars for center display
- `fill`: Fills entire target area, may crop some content
- `center`: Displays original size centered (if original size is smaller than target)

### Pack as EFI Container

```bash
# Pack into .anim container
abtool pack frames/sequence.anim.json final/splash.anim

# Specify different root directory
abtool pack manifest.json output.anim --frames-root frames/
```

### PC-side Preview

Before deploying to EFI, you can preview animation effects on Windows:

```bash
abtool preview build/splash.anim
```

### UEFI Deployment

#### Automatic Deployment (Recommended)

```powershell
# Full deployment (files + firmware boot entry)
.\scripts\Install-AnimeBoot.ps1 `
    -EfiBinary "path\to\AnimeBoot.efi" `
    -AnimSource "path\to\splash.anim" `
    -EspMountPoint "S:" `
    -BootEntryDescription "My Custom Boot Animation"

# Copy files only, don't modify firmware
.\scripts\Install-AnimeBoot.ps1 -EfiBinary "..." -AnimSource "..." -SkipBcd
```

#### Manual Deployment

For more precise control, manual deployment is available:

1. **Mount ESP**
   ```powershell
   # Find ESP partition
   Get-Partition | Where-Object {$_.Type -eq "System"}

   # Mount to S: drive
   $esp = Get-Partition | Where-Object {$_.Type -eq "System"}
   $esp | Set-Partition -NewDriveLetter S
   ```

2. **Copy Files**
   ```powershell
   # Create directory
   New-Item -ItemType Directory -Path "S:\EFI\AnimeBoot" -Force

   # Copy EFI application and animation resources
   Copy-Item "AnimeBoot.efi" "S:\EFI\AnimeBoot\"
   Copy-Item "splash.anim" "S:\EFI\AnimeBoot\"
   ```

3. **Create Firmware Boot Entry**
   ```powershell
   # Create new firmware boot entry
   bcdedit /create /d "AnimeBoot Splash" /application firmware

   # Get newly created GUID
   $guid = (bcdedit /enum firmware | Select-String "AnimeBoot").Line.Split()[1]

   # Set path
   bcdedit /set $guid path "\EFI\AnimeBoot\AnimeBoot.efi"

   # Add to boot order first position
   bcdedit /set "{fwbootmgr}" displayorder $guid /addfirst
   ```

### Secure Boot Signing

If the target system has Secure Boot enabled, the EFI application must be signed:

```powershell
# Sign using Microsoft signing tools (certificate required)
.\scripts\Sign-AnimeBoot.ps1 `
    -EfiFile "AnimeBoot.efi" `
    -CertificatePath "my_cert.p12" `
    -CertificatePassword "password" `
    -TimestampUrl "http://timestamp.digicert.com"

# Or use shim approach (requires pre-configured shim)
# Details in docs/secure_boot.txt
```

## Configuration and Format Specifications

### .anim Container Format

AnimeBoot uses a custom `.anim` container format:

```
File Structure:
┌─────────────────┐
│ Header (96 bytes) │
├─────────────────┤
│ Manifest JSON    │
├─────────────────┤
│ Alignment Padding│
├─────────────────┤
│ Frame Index Table│
├─────────────────┤
│ Frame Data       │
└─────────────────┘
```

#### Header Fields

- `Magic`: "ABANIM\x00"
- `Version`: Version number (currently 1.0)
- `LogicalWidth/Height`: Logical resolution
- `PixelFormat`: Pixel format (0=BGRA32, 1=BMP32)
- `TargetFps`: Target frame rate
- `LoopCount`: Loop count

#### Manifest Configuration

```json
{
  "logical_width": 640,
  "logical_height": 360,
  "scaling": "letterbox",
  "background": "#000000",
  "max_memory": 67108864,
  "loop_count": 1,
  "frame_duration_us": 41666,
  "allow_key_skip": true,
  "max_total_duration_ms": 8000,
  "notes": "Custom boot animation"
}
```

### Loose Files Mode

For debugging and development, loose files mode can be used:

```
animation/
├── sequence.anim.json (manifest)
├── frame0001.bmp
├── frame0002.bmp
└── ...
```

## Security Statement

### ⚠️ Important Security Warning

**AnimeBoot is a UEFI application that runs in a high-privilege environment before the operating system loads. Please read the following security statements carefully:**
- **Only use animation files you trust**: Maliciously constructed `.anim` files may contain content that could cause system instability
- **Verify sources**: Only obtain AnimeBoot EFI applications and toolchains from trusted sources
- **Signature verification**: On systems with Secure Boot enabled, ensure EFI applications are properly signed

The inherent complexity of the UEFI environment means absolute security cannot be guaranteed. Potential risks include:
- Malicious animation packages may cause graphics display anomalies
- In extreme cases may affect system boot
- Improper firmware modifications may require recovery operations

#### 5. Disclaimer
**This project is provided "as is" without any warranties. Users assume all risks associated with its use.**

## ⚠️ Important Notes

### Pre-deployment Checks

1. **Backup important data**: Backup current configuration before modifying firmware boot entries
   ```powershell
   bcdedit /export backup.bcd
   ```

2. **ESP space**: Ensure ESP partition has sufficient space (at least 50MB)

3. **Secure Boot status**:
   ```powershell
   Confirm-SecureBootUEFI  # Check Secure Boot status
   ```

### Compatibility Notes

- **Hardware requirements**: Requires graphics card with GOP support
- **Resolution**: Test the actual resolution of the target system
- **Firmware limitations**: Some OEM firmware may have special restrictions

### Performance Considerations

- **Animation size**: Oversized animations will increase boot time
- **Frame rate**: 24fps is the recommended balance point
- **Resolution**: 640x360 is the recommended default resolution

### Recovery Procedures

If issues occur, recovery can be performed in the following ways:

1. **Enter firmware settings**: Reset boot order
2. **Use recovery script**:
   ```powershell
   .\scripts\Remove-AnimeBoot.ps1 -EspMountPoint "S:" -RemoveFiles
   ```
3. **Manual cleanup**:
   ```powershell
   # Delete files
   Remove-Item "S:\EFI\AnimeBoot" -Recurse -Force

   # Reset boot order
   bcdedit /set "{fwbootmgr}" displayorder /remove $guid
   ```

## Troubleshooting

### Common Issues

#### Animation Not Playing
- Check if `.anim` file is corrupted
- Verify if resolution settings are appropriate
- Check if ESP space is sufficient

#### Secure Boot Refuses to Load
- Confirm EFI application is properly signed
- Check certificate chain completeness
- Verify timestamp validity

#### Graphics Display Anomalies
- Test GOP mode compatibility
- Check pixel format settings
- Try different resolutions

#### Keys Not Responding
- Confirm keyboard availability during UEFI phase
- Check allow_key_skip setting

### Debugging Tips

1. **Enable verbose logging**:
   ```bash
   abtool extract --verbose ...
   ```

2. **Preview validation**:
   ```bash
   abtool preview build/splash.anim
   ```

3. **Check firmware boot entries**:
   ```powershell
   bcdedit /enum firmware
   ```

## Development Information

### Build Requirements

- **EDK II**: 2022 or newer version
- **Python**: 3.10+ (for abtool)
- **Visual Studio**: 2019/2022 (for EDK II)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details

## Acknowledgments

- EDK II community for providing UEFI development framework
- Open source image processing libraries like Pillow, imageio
- Microsoft for Secure Boot signing infrastructure

---

**Disclaimer**: This tool may affect the system boot process. Please use it after fully understanding the risks, and always maintain system backups.
