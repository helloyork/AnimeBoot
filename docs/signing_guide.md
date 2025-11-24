# UEFI Secure Boot Signing Guide

This guide provides detailed instructions on how to digitally sign the AnimeBoot EFI application to ensure it boots properly on systems with Secure Boot enabled.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Signing Strategy Overview](#signing-strategy-overview)
- [Strategy A: Custom Certificate Chain](#strategy-a-custom-certificate-chain)
- [Strategy B: Shim + Microsoft CA](#strategy-b-shim--microsoft-ca)
- [Using the Signing Script](#using-the-signing-script)
- [Verifying Signatures](#verifying-signatures)
- [Troubleshooting](#troubleshooting)

## Prerequisites

### System Requirements

- Windows 10/11 (x64)
- Windows SDK (containing signtool.exe) or separately install Windows SDK Signing Tools
- OpenSSL (for custom certificate chains)
- Built AnimeBoot.efi file

### Certificate Preparation

Before starting the signing process, you need to prepare appropriate code signing certificates:

**Option 1: Commercial Code Signing Certificates (Recommended for production)**
- Purchase from trusted certificate authorities: DigiCert, GlobalSign, Sectigo, etc.
- Support EV (Extended Validation) certificates for best compatibility
- Certificate format: .pfx or .p12 files

**Option 2: Test Certificates (Development and testing only)**
- Generate self-signed certificates using OpenSSL
- Valid only in test environments, not suitable for production

## Signing Strategy Overview

### Why Signing is Required?

Secure Boot ensures system security by verifying digital signatures of EFI applications. Unsigned applications will be refused to load.

### Two Main Strategies

| Strategy | Use Case | Complexity | Certificate Requirements |
|----------|----------|------------|--------------------------|
| Custom Certificate Chain | Full Control Environment | High | Custom PK/KEK/DB |
| Shim + Microsoft CA | Keep OEM Settings | Medium | Code Signing Certificate |

## Strategy A: Custom Certificate Chain

### Use Cases

- Experimental environments or personal hardware
- Need full control over Secure Boot chain
- No dependency on third-party certificates

### Detailed Steps

#### 1. Generate Certificates

```bash
# Generate Platform Key (PK)
openssl req -new -x509 -sha256 -days 3650 -nodes -out pk.crt -keyout pk.key -subj "/CN=AnimeBoot Platform Key/"

# Generate Key Exchange Key (KEK)
openssl req -new -x509 -sha256 -days 3650 -nodes -out kek.crt -keyout kek.key -subj "/CN=AnimeBoot KEK/"

# Generate Database Key (DB) - Used for signing EFI applications
openssl req -new -x509 -sha256 -days 3650 -nodes -out db.crt -keyout db.key -subj "/CN=AnimeBoot DB/"
```

#### 2. Convert to EFI Signature Lists

```bash
# Install efitools on Linux (Ubuntu/Debian)
# This step requires Linux environment (WSL or native Linux)
sudo apt install efitools

# Convert certificates
cert-to-efi-sig-list pk.crt pk.esl
cert-to-efi-sig-list kek.crt kek.esl
cert-to-efi-sig-list db.crt db.esl

# Sign EFI variables
sign-efi-sig-list -k pk.key -c pk.crt PK pk.esl pk.auth
sign-efi-sig-list -k pk.key -c pk.crt KEK kek.esl kek.auth
sign-efi-sig-list -k kek.key -c kek.crt db db.esl db.auth
```

#### 3. Install to Firmware

1. Enter BIOS/UEFI settings
2. Temporarily disable Secure Boot
3. Use KeyTool.efi or manufacturer tools to import sequentially:
   - `pk.auth` → Platform Key
   - `kek.auth` → Key Exchange Key
   - `db.auth` → Database
4. Re-enable Secure Boot

#### 4. Sign EFI Application

```bash
# Using sbsigntool (Linux)
sudo apt install sbsigntool
sbsign --key db.key --cert db.crt --output AnimeBoot.signed.efi AnimeBoot.efi

# Or use Windows signing tools
scripts\Sign-AnimeBoot.ps1 -InputEfi AnimeBoot.efi -PfxPath animeboot-db.pfx -PfxPassword yourpassword
```

## Strategy B: Shim + Microsoft CA

### Use Cases

- Use on machines while keeping OEM Secure Boot settings
- Utilize Microsoft's certificate chain
- Suitable for most modern Windows systems

### Detailed Steps

#### 1. Obtain Shim Bootloader

For Windows users, download pre-signed shim binaries from trusted sources:

**Option A: Download Pre-signed Shim**
- Download from Microsoft or Linux distributions that provide Windows-compatible shim binaries
- Ensure the shim is signed by Microsoft UEFI CA
- Common locations: Ubuntu packages, Fedora packages, or official shim releases

**Option B: Use WSL for Compilation (Advanced)**
```bash
# If using WSL, you can install from Ubuntu repository
sudo apt install shim-signed

# Copy the shim binaries to Windows
cp /usr/lib/shim/shimx64.efi /mnt/c/path/to/shim/
```

**Option C: Compile from Source (Advanced)**
```bash
# On Linux/WSL
git clone https://github.com/rhboot/shim.git
cd shim
make VENDOR_CERT_FILE=your_cert.crt

# Copy resulting shimx64.efi to Windows
```

#### 2. Prepare Code Signing Certificate

You need a code signing certificate trusted by Microsoft's UEFI CA:

```bash
# Convert certificate to PFX format (if needed)
openssl pkcs12 -export -out vendor.pfx -inkey vendor.key -in vendor.crt
```

#### 3. Sign AnimeBoot.efi

```powershell
# Using the provided signing script
.\scripts\Sign-AnimeBoot.ps1 `
    -EfiFile "Build\AnimeBoot\RELEASE_X64\AnimeBootPkg\Application\AnimeBoot\AnimeBoot\AnimeBoot.efi" `
    -PfxPath "vendor.pfx" `
    -PfxPassword "your_password" `
    -TimestampServer "http://timestamp.digicert.com"
```

#### 4. Deploy Shim and AnimeBoot

```powershell
# Mount ESP
$esp = Get-Partition | Where-Object {$_.Type -eq "System"}
$esp | Set-Partition -NewDriveLetter S

# Create directory and copy files
New-Item -ItemType Directory -Path "S:\EFI\AnimeBoot" -Force
Copy-Item "shimx64.efi" "S:\EFI\AnimeBoot\"
Copy-Item "AnimeBoot.signed.efi" "S:\EFI\AnimeBoot\"

# Create firmware boot entry pointing to shim
bcdedit /create /d "AnimeBoot with Shim" /application firmware
# Get GUID and set path...
```

#### 5. Configure Shim

Create `S:\EFI\AnimeBoot\grub.cfg`:

```
set timeout=0
menuentry "AnimeBoot" {
    chainloader /EFI\AnimeBoot\AnimeBoot.signed.efi
}
```

## Using the Signing Script

### Script Parameters

`Sign-AnimeBoot.ps1` script parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `-InputEfi` | Yes | Path to input EFI file |
| `-PfxPath` | Yes | Path to PFX certificate file |
| `-PfxPassword` | No | PFX file password |
| `-OutputEfi` | No | Output file path (overwrites input by default) |
| `-TimestampServer` | No | Timestamp server URL |

### Usage Examples

#### Basic Signing

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -PfxPath "my_cert.pfx" `
    -PfxPassword "mypassword"
```

#### Specify Output File

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -OutputEfi "AnimeBoot.signed.efi" `
    -PfxPath "my_cert.pfx" `
    -PfxPassword "mypassword"
```

#### Custom Timestamp Server

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -PfxPath "my_cert.pfx" `
    -TimestampServer "http://timestamp.globalsign.com/tsa/r6advanced1"
```

## Verifying Signatures

### Verify with signtool

```cmd
signtool verify /pa /v AnimeBoot.signed.efi
```

### Verify with sbverify (Linux)

```bash
sbverify --list AnimeBoot.signed.efi
```

### Verification Output Example

Successful verification should show:
- Complete certificate chain
- Valid timestamp
- Correct signature algorithm (SHA256)

## 故障排除 / Troubleshooting

### 常见问题 / Common Issues

#### Signing Failed: signtool.exe not found

**Solution:**
- Install Windows SDK or separately install Windows SDK Signing Tools
- Or specify full path: `-SigntoolPath "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"`

#### Secure Boot Still Refuses to Load

**Possible Causes:**
- Certificate not in trust chain
- Certificate has expired
- Invalid timestamp

**Solutions:**
- Verify certificate chain: `signtool verify /pa /kp AnimeBoot.efi`
- Check certificate expiration date
- Re-sign with valid timestamp

#### Shim Fails to Boot

**Check Items:**
- Shim version compatibility with your firmware
- MokManager correctly imported vendor certificate
- grub.cfg configuration is correct

### Debugging Tips

#### Enable Verbose Logging

```powershell
# Add -Verbose parameter to signing script
.\scripts\Sign-AnimeBoot.ps1 -InputEfi "AnimeBoot.efi" -PfxPath "cert.pfx" -Verbose
```

#### Test Signature Chain

```bash
# Use OVMF test environment
qemu-system-x86_64 -bios OVMF_CODE.secboot.fd -drive file=fat:rw:esp,format=raw
```

## Security Considerations

### Certificate Management

- **Protect Private Keys**: Private keys should be stored securely to prevent leakage
- **Regular Updates**: Renew certificates before expiration
- **Backup Strategy**: Backup certificates but exclude private keys

### Production Recommendations

- Use HSM (Hardware Security Module) for private key storage
- Implement regular auditing and monitoring
- Prepare certificate revocation and update plans

## Related Resources

- [UEFI Secure Boot Specification](https://uefi.org/specifications)
- [Microsoft UEFI CA Policy](https://docs.microsoft.com/en-us/windows-hardware/drivers/dashboard/uefi-ca-validation)
- [Shim Bootloader Documentation](https://github.com/rhboot/shim)
- [efitools Usage Guide](https://git.kernel.org/pub/scm/linux/kernel/git/jejb/efitools.git/)

---

**Disclaimer**: This document is provided for reference only. Please ensure you understand all security risks and have proper legal authorization before proceeding.
