# UEFI 安全启动签名指南 / UEFI Secure Boot Signing Guide

本文档详细介绍如何为 AnimeBoot EFI 应用程序进行数字签名，以确保在启用 Secure Boot 的系统上正常启动。

This guide provides detailed instructions on how to digitally sign the AnimeBoot EFI application to ensure it boots properly on systems with Secure Boot enabled.

## 目录 / Table of Contents

- [前提条件 / Prerequisites](#前提条件--prerequisites)
- [签名策略概述 / Signing Strategy Overview](#签名策略概述--signing-strategy-overview)
- [策略 A：自建证书链 / Strategy A: Custom Certificate Chain](#策略-a自建证书链--strategy-a-custom-certificate-chain)
- [策略 B：Shim + Microsoft CA / Strategy B: Shim + Microsoft CA](#策略-bshim--microsoft-ca--strategy-b-shim--microsoft-ca)
- [使用签名脚本 / Using the Signing Script](#使用签名脚本--using-the-signing-script)
- [验证签名 / Verifying Signatures](#验证签名--verifying-signatures)
- [故障排除 / Troubleshooting](#故障排除--troubleshooting)

## 前提条件 / Prerequisites

### 系统要求 / System Requirements

- Windows 10/11 (x64)
- Windows SDK (包含 signtool.exe) 或单独安装的 Windows SDK Signing Tools
- OpenSSL (用于自定义证书链)
- 已构建的 AnimeBoot.efi 文件

### 证书准备 / Certificate Preparation

在开始签名之前，您需要准备适当的代码签名证书：

**选项 1: 商业代码签名证书 (推荐用于生产环境)**
- 从受信任的证书颁发机构购买：DigiCert、GlobalSign、Sectigo 等
- 支持 EV (Extended Validation) 证书以获得最佳兼容性
- 证书格式：.pfx 或 .p12 文件

**选项 2: 测试证书 (仅用于开发和测试)**
- 使用 OpenSSL 生成自签名证书
- 仅在测试环境中有效，生产环境不可用

## 签名策略概述 / Signing Strategy Overview

### 为什么需要签名？ / Why Signing is Required?

Secure Boot 通过验证 EFI 应用程序的数字签名来确保系统安全。未签名的应用程序会被拒绝加载。

Secure Boot ensures system security by verifying digital signatures of EFI applications. Unsigned applications will be refused to load.

### 两种主要策略 / Two Main Strategies

| 策略 | 适用场景 | 复杂度 | 证书要求 |
|------|----------|--------|----------|
| Strategy | Use Case | Complexity | Certificate Requirements |
| 自建证书链<br/>Custom Chain | 完全控制环境<br/>Full Control | 高<br/>High | 自建 PK/KEK/DB<br/>Custom PK/KEK/DB |
| Shim + Microsoft CA<br/>Shim + MS CA | 保留 OEM 设置<br/>Keep OEM Settings | 中<br/>Medium | 代码签名证书<br/>Code Signing Cert |

## 策略 A：自建证书链 / Strategy A: Custom Certificate Chain

### 适用场景 / Use Cases

- 实验环境或个人硬件
- 需要完全控制 Secure Boot 链
- 不依赖第三方证书

### 详细步骤 / Detailed Steps

#### 1. 生成证书 / Generate Certificates

```bash
# 生成 Platform Key (PK)
openssl req -new -x509 -sha256 -days 3650 -nodes -out pk.crt -keyout pk.key -subj "/CN=AnimeBoot Platform Key/"

# 生成 Key Exchange Key (KEK)
openssl req -new -x509 -sha256 -days 3650 -nodes -out kek.crt -keyout kek.key -subj "/CN=AnimeBoot KEK/"

# 生成 Database Key (DB) - 用于签名 EFI 应用
openssl req -new -x509 -sha256 -days 3650 -nodes -out db.crt -keyout db.key -subj "/CN=AnimeBoot DB/"
```

#### 2. 转换为 EFI 签名列表 / Convert to EFI Signature Lists

```bash
# 在 Linux 上安装 efitools (Ubuntu/Debian)
# 此步骤需要在 Linux 环境（WSL 或原生 Linux）中进行
sudo apt install efitools

# 转换证书
cert-to-efi-sig-list pk.crt pk.esl
cert-to-efi-sig-list kek.crt kek.esl
cert-to-efi-sig-list db.crt db.esl

# 签名 EFI 变量
sign-efi-sig-list -k pk.key -c pk.crt PK pk.esl pk.auth
sign-efi-sig-list -k pk.key -c pk.crt KEK kek.esl kek.auth
sign-efi-sig-list -k kek.key -c kek.crt db db.esl db.auth
```

#### 3. 安装到固件 / Install to Firmware

1. 进入 BIOS/UEFI 设置
2. 临时关闭 Secure Boot
3. 使用 KeyTool.efi 或厂商工具依次导入：
   - `pk.auth` → Platform Key
   - `kek.auth` → Key Exchange Key
   - `db.auth` → Database
4. 重新启用 Secure Boot

#### 4. 签名 EFI 应用 / Sign EFI Application

```bash
# 使用 sbsigntool (Linux)
sudo apt install sbsigntool
sbsign --key db.key --cert db.crt --output AnimeBoot.signed.efi AnimeBoot.efi

# 或使用 Windows 签名工具
scripts\Sign-AnimeBoot.ps1 -InputEfi AnimeBoot.efi -PfxPath animeboot-db.pfx -PfxPassword yourpassword
```

## 策略 B：Shim + Microsoft CA / Strategy B: Shim + Microsoft CA

### 适用场景 / Use Cases

- 在保持 OEM Secure Boot 设置的机器上使用
- 使用 Microsoft 提供的证书链
- 适合大多数现代 Windows 系统

### 详细步骤 / Detailed Steps

#### 1. 获取 Shim Bootloader / Obtain Shim Bootloader

对于 Windows 用户，从可信来源下载预签名的 shim 二进制文件：

**方案 A：下载预签名 Shim**
- 从 Microsoft 或提供 Windows 兼容 shim 二进制文件的 Linux 发行版下载
- 确保 shim 已通过 Microsoft UEFI CA 签名
- 常见位置：Ubuntu 软件包、Fedora 软件包或官方 shim 发布

**方案 B：使用 WSL 进行编译（高级用户）**
```bash
# 如果使用 WSL，可以从 Ubuntu 仓库安装
sudo apt install shim-signed

# 将 shim 二进制文件复制到 Windows
cp /usr/lib/shim/shimx64.efi /mnt/c/path/to/shim/
```

**方案 C：从源码编译（高级用户）**
```bash
# 在 Linux/WSL 上
git clone https://github.com/rhboot/shim.git
cd shim
make VENDOR_CERT_FILE=your_cert.crt

# 将生成的 shimx64.efi 复制到 Windows
```

#### 2. 准备代码签名证书 / Prepare Code Signing Certificate

您需要一个通过 Microsoft UEFI CA 信任的代码签名证书：

```bash
# 转换证书为 PFX 格式 (如果需要)
openssl pkcs12 -export -out vendor.pfx -inkey vendor.key -in vendor.crt
```

#### 3. 签名 AnimeBoot.efi / Sign AnimeBoot.efi

```powershell
# 使用提供的签名脚本
.\scripts\Sign-AnimeBoot.ps1 `
    -EfiFile "Build\AnimeBoot\RELEASE_X64\AnimeBootPkg\Application\AnimeBoot\AnimeBoot\AnimeBoot.efi" `
    -PfxPath "vendor.pfx" `
    -PfxPassword "your_password" `
    -TimestampServer "http://timestamp.digicert.com"
```

#### 4. 部署 Shim 和 AnimeBoot / Deploy Shim and AnimeBoot

```powershell
# 挂载 ESP
$esp = Get-Partition | Where-Object {$_.Type -eq "System"}
$esp | Set-Partition -NewDriveLetter S

# 创建目录并复制文件
New-Item -ItemType Directory -Path "S:\EFI\AnimeBoot" -Force
Copy-Item "shimx64.efi" "S:\EFI\AnimeBoot\"
Copy-Item "AnimeBoot.signed.efi" "S:\EFI\AnimeBoot\"

# 创建固件启动项指向 shim
bcdedit /create /d "AnimeBoot with Shim" /application firmware
# 获取 GUID 并设置路径...
```

#### 5. 配置 Shim / Configure Shim

创建 `S:\EFI\AnimeBoot\grub.cfg`：

```
set timeout=0
menuentry "AnimeBoot" {
    chainloader /EFI/AnimeBoot/AnimeBoot.signed.efi
}
```

## 使用签名脚本 / Using the Signing Script

### 脚本参数说明 / Script Parameters

`Sign-AnimeBoot.ps1` 脚本的参数：

| 参数 | 必需 | 描述 |
|------|------|------|
| Parameter | Required | Description |
| `-InputEfi` | 是<br/>Yes | 输入的 EFI 文件路径<br/>Path to input EFI file |
| `-PfxPath` | 是<br/>Yes | PFX 证书文件路径<br/>Path to PFX certificate file |
| `-PfxPassword` | 否<br/>No | PFX 文件密码<br/>PFX file password |
| `-OutputEfi` | 否<br/>No | 输出文件路径 (默认覆盖输入文件)<br/>Output file path (overwrites input by default) |
| `-TimestampServer` | 否<br/>No | 时间戳服务器 URL<br/>Timestamp server URL |

### 使用示例 / Usage Examples

#### 基本签名 / Basic Signing

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -PfxPath "my_cert.pfx" `
    -PfxPassword "mypassword"
```

#### 指定输出文件 / Specify Output File

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -OutputEfi "AnimeBoot.signed.efi" `
    -PfxPath "my_cert.pfx" `
    -PfxPassword "mypassword"
```

#### 自定义时间戳服务器 / Custom Timestamp Server

```powershell
.\scripts\Sign-AnimeBoot.ps1 `
    -InputEfi "AnimeBoot.efi" `
    -PfxPath "my_cert.pfx" `
    -TimestampServer "http://timestamp.globalsign.com/tsa/r6advanced1"
```

## 验证签名 / Verifying Signatures

### 使用 signtool 验证 / Verify with signtool

```cmd
signtool verify /pa /v AnimeBoot.signed.efi
```

### 使用 sbverify (Linux) / Verify with sbverify

```bash
sbverify --list AnimeBoot.signed.efi
```

### 验证输出示例 / Verification Output Example

成功的验证应该显示：
- 证书链完整
- 时间戳有效
- 签名算法正确 (SHA256)

## 故障排除 / Troubleshooting

### 常见问题 / Common Issues

#### 签名失败：找不到 signtool.exe / Signing Failed: signtool.exe not found

**解决方案 / Solution:**
- 安装 Windows SDK 或单独安装 Windows SDK Signing Tools
- 或指定完整路径：`-SigntoolPath "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"`

#### Secure Boot 仍然拒绝加载 / Secure Boot Still Refuses to Load

**可能原因 / Possible Causes:**
- 证书不在信任链中
- 证书已过期
- 时间戳无效

**解决方案 / Solutions:**
- 验证证书链：`signtool verify /pa /kp AnimeBoot.efi`
- 检查证书过期时间
- 重新签名并添加有效时间戳

#### Shim 无法启动 / Shim Fails to Boot

**检查项目 / Check Items:**
- Shim 版本是否与您的固件兼容
- MokManager 是否正确导入了 vendor 证书
- grub.cfg 配置是否正确

### 调试技巧 / Debugging Tips

#### 启用详细日志 / Enable Verbose Logging

```powershell
# 在签名脚本中添加 -Verbose 参数
.\scripts\Sign-AnimeBoot.ps1 -InputEfi "AnimeBoot.efi" -PfxPath "cert.pfx" -Verbose
```

#### 测试签名链 / Test Signature Chain

```bash
# 使用 OVMF 测试环境
qemu-system-x86_64 -bios OVMF_CODE.secboot.fd -drive file=fat:rw:esp,format=raw
```

## 安全注意事项 / Security Considerations

### 证书管理 / Certificate Management

- **保护私钥**：私钥应该存储在安全位置，避免泄露
- **定期更新**：证书过期前及时续期
- **备份策略**：备份证书但不包含私钥

### 生产环境建议 / Production Recommendations

- 使用 HSM (Hardware Security Module) 存储私钥
- 实施定期审计和监控
- 准备证书吊销和更新计划

## 相关资源 / Related Resources

- [UEFI Secure Boot 规范](https://uefi.org/specifications)
- [Microsoft UEFI CA 政策](https://docs.microsoft.com/en-us/windows-hardware/drivers/dashboard/uefi-ca-validation)
- [Shim Bootloader 文档](https://github.com/rhboot/shim)
- [efitools 使用指南](https://git.kernel.org/pub/scm/linux/kernel/git/jejb/efitools.git/)

---

**免责声明 / Disclaimer**: 本文档仅供参考，使用前请确保您理解所有安全风险并拥有适当的法律授权。

This document is provided for reference only. Please ensure you understand all security risks and have proper legal authorization before proceeding.
