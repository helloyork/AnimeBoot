# AnimeBoot - UEFI 启动动画播放器

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

AnimeBoot 是一个概念验证项目，它能在 Windows Boot Manager 或自定义 UEFI bootloader 之前播放动画（看起来像动图），让你的 PC 启动过程更具个性化。

## 系统要求

### 开发环境
- **操作系统**：Windows 10/11 (x64)
- **Python**：3.10+
- **EDK II**：2022+
- **Visual Studio**：2019/2022 (用于 EDK II 构建)
- **PowerShell**：5.1+

### 运行环境
- **UEFI 固件**：支持 GOP (Graphics Output Protocol)
- **架构**：x86_64 (AMD64/Intel64)
- **存储**：ESP (EFI System Partition) 至少 50MB 可用空间
- **Secure Boot**：可选支持（提供完整签名方案）

## 快速开始

### 1. 构建 PC 端工具

```bash
# 安装 abtool
cd host-tools/abtool
pip install -e .

# 验证安装
abtool --help
```

### 2. 构建 UEFI 应用

```bash
# 设置 EDK II 环境 (假设 EDK2_WORKSPACE 已设置)
cd AnimeBootPkg
edksetup.bat Rebuild

# 构建 Release 版本
build -p AnimeBootPkg\AnimeBootPkg.dsc -b RELEASE -a X64 -t VS2022
```

### 3. 转换动画资源

```bash
# 从 GIF 创建动画包
abtool extract my_animation.gif build_frames --width 640 --height 360 --fps 24
abtool pack build_frames/sequence.anim.json build/splash.anim
```

### 4. 部署到系统

```powershell
# 以管理员身份运行 PowerShell
.\scripts\Install-AnimeBoot.ps1 `
    -EfiBinary "Build\AnimeBoot\RELEASE_X64\AnimeBootPkg\Application\AnimeBoot\AnimeBoot\AnimeBoot.efi" `
    -AnimSource "build\splash.anim" `
    -BootEntryDescription "AnimeBoot Splash"
```

## 使用指南

### 动画资源准备

AnimeBoot 支持多种输入格式：

- **静态图片**：JPG、PNG、BMP → 转换为单帧动画
- **动画 GIF**：自动提取所有帧并保持时间轴
- **APNG**：类似 GIF，但支持更好的压缩
- **视频文件**：MP4、MOV、AVI 等 → 按指定 FPS 采样

#### 转换示例

```bash
# 基本转换 (默认 640x360, 24fps)
abtool extract animation.gif output_frames

# 自定义分辨率和帧率
abtool extract video.mp4 frames --width 800 --height 450 --fps 30

# 指定缩放模式和背景色
abtool extract splash.png output --scaling letterbox --background "#001122"
```

#### 缩放模式说明

- `letterbox` (默认)：保持宽高比，添加黑边居中显示
- `fill`：填充整个目标区域，可能裁剪部分内容
- `center`：居中显示原始尺寸（如果原始尺寸小于目标）

### 打包为 EFI 容器

```bash
# 打包为 .anim 容器
abtool pack frames/sequence.anim.json final/splash.anim

# 指定不同根目录
abtool pack manifest.json output.anim --frames-root frames/
```

### PC 端预览

在部署到 EFI 之前，可以在 Windows 上预览动画效果：

```bash
abtool preview build/splash.anim
```

### UEFI 部署

#### 自动部署（推荐）

```powershell
# 完整部署（文件 + 固件启动项）
.\scripts\Install-AnimeBoot.ps1 `
    -EfiBinary "path\to\AnimeBoot.efi" `
    -AnimSource "path\to\splash.anim" `
    -EspMountPoint "S:" `
    -BootEntryDescription "My Custom Boot Animation"

# 只复制文件，不修改固件
.\scripts\Install-AnimeBoot.ps1 -EfiBinary "..." -AnimSource "..." -SkipBcd
```

#### 手动部署

如果需要更精细的控制，可以手动部署：

1. **挂载 ESP**
   ```powershell
   # 查找 ESP 分区
   Get-Partition | Where-Object {$_.Type -eq "System"}

   # 挂载到 S: 盘
   $esp = Get-Partition | Where-Object {$_.Type -eq "System"}
   $esp | Set-Partition -NewDriveLetter S
   ```

2. **复制文件**
   ```powershell
   # 创建目录
   New-Item -ItemType Directory -Path "S:\EFI\AnimeBoot" -Force

   # 复制 EFI 应用和动画资源
   Copy-Item "AnimeBoot.efi" "S:\EFI\AnimeBoot\"
   Copy-Item "splash.anim" "S:\EFI\AnimeBoot\"
   ```

3. **创建固件启动项**
   ```powershell
   # 创建新的固件启动项
   bcdedit /create /d "AnimeBoot Splash" /application firmware

   # 获取新创建的 GUID
   $guid = (bcdedit /enum firmware | Select-String "AnimeBoot").Line.Split()[1]

   # 设置路径
   bcdedit /set $guid path "\EFI\AnimeBoot\AnimeBoot.efi"

   # 添加到启动顺序首位
   bcdedit /set "{fwbootmgr}" displayorder $guid /addfirst
   ```

### Secure Boot 签名

如果目标系统启用了 Secure Boot，必须对 EFI 应用进行签名：

```powershell
# 使用微软签名工具签名 (需要证书)
.\scripts\Sign-AnimeBoot.ps1 `
    -EfiFile "AnimeBoot.efi" `
    -CertificatePath "my_cert.p12" `
    -CertificatePassword "password" `
    -TimestampUrl "http://timestamp.digicert.com"

# 或者使用 shim 方案 (需要预先设置 shim)
# 详情见 docs/secure_boot.txt
```

## 配置和格式说明

### .anim 容器格式

AnimeBoot 使用自定义的 `.anim` 容器格式：

```
文件结构：
┌─────────────────┐
│ 包头 (96 字节)  │
├─────────────────┤
│ Manifest JSON   │
├─────────────────┤
│ 对齐填充        │
├─────────────────┤
│ 帧索引表        │
├─────────────────┤
│ 帧数据          │
└─────────────────┘
```

#### 包头字段

- `Magic`: "ABANIM\x00"
- `Version`: 版本号 (当前 1.0)
- `LogicalWidth/Height`: 逻辑分辨率
- `PixelFormat`: 像素格式 (0=BGRA32, 1=BMP32)
- `TargetFps`: 目标帧率
- `LoopCount`: 循环次数

#### Manifest 配置

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

### Loose Files 模式

对于调试和开发，可以使用松散文件模式：

```
animation/
├── sequence.anim.json (manifest)
├── frame0001.bmp
├── frame0002.bmp
└── ...
```

## 安全声明

### ⚠️ 重要安全警告

**AnimeBoot 是一款 UEFI 应用程序，它运行在操作系统加载之前的高权限环境中。请仔细阅读以下安全声明：**
- **只使用您信任的动画文件**：恶意构造的 `.anim` 文件可能包含会导致系统不稳定的内容
- **验证来源**：只从可信来源获取 AnimeBoot EFI 应用和工具链
- **签名验证**：在启用 Secure Boot 的系统上，确保 EFI 应用已正确签名

UEFI 环境固有的复杂性意味着无法保证绝对安全，潜在风险包括：  
- 恶意动画包可能导致图形显示异常
- 极端情况下可能影响系统启动
- 不当的固件修改可能需要恢复操作

#### 5. 责任免除
**本项目按"原样"提供，不提供任何形式的担保。使用者需自行承担使用风险。**

## ⚠️ 注意事项

### 部署前检查

1. **备份重要数据**：修改固件启动项前备份当前配置
   ```powershell
   bcdedit /export backup.bcd
   ```

2. **ESP 空间**：确保 ESP 分区有足够空间（至少 50MB）

3. **Secure Boot 状态**：
   ```powershell
   Confirm-SecureBootUEFI  # 检查 Secure Boot 状态
   ```

### 兼容性注意

- **硬件要求**：需要 GOP 支持的显卡
- **分辨率**：测试目标系统的实际分辨率
- **固件限制**：某些 OEM 固件可能有特殊限制

### 性能考虑

- **动画大小**：过大的动画会增加启动时间
- **帧率**：24fps 是推荐的平衡点
- **分辨率**：640x360 是推荐的默认分辨率

### 故障恢复

如果遇到问题，可以通过以下方式恢复：

1. **进入固件设置**：重置启动顺序
2. **使用恢复脚本**：
   ```powershell
   .\scripts\Remove-AnimeBoot.ps1 -EspMountPoint "S:" -RemoveFiles
   ```
3. **手动清理**：
   ```powershell
   # 删除文件
   Remove-Item "S:\EFI\AnimeBoot" -Recurse -Force

   # 重置启动顺序
   bcdedit /set "{fwbootmgr}" displayorder /remove $guid
   ```

## 故障排除

### 常见问题

#### 动画不播放
- 检查 `.anim` 文件是否损坏
- 验证分辨率设置是否合适
- 查看 ESP 空间是否充足

#### Secure Boot 拒绝加载
- 确认 EFI 应用已正确签名
- 检查证书链是否完整
- 验证时间戳是否有效

#### 图形显示异常
- 测试 GOP 模式兼容性
- 检查像素格式设置
- 尝试不同分辨率

#### 按键不响应
- 确认键盘在 UEFI 阶段可用
- 检查 allow_key_skip 设置

### 调试技巧

1. **启用详细日志**：
   ```bash
   abtool extract --verbose ...
   ```

2. **预览验证**：
   ```bash
   abtool preview build/splash.anim
   ```

3. **检查固件启动项**：
   ```powershell
   bcdedit /enum firmware
   ```

## 开发信息

### 构建要求

- **EDK II**：2022 或更新版本
- **Python**：3.10+ (用于 abtool)
- **Visual Studio**：2019/2022 (用于 EDK II)

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 致谢

- EDK II 社区提供的 UEFI 开发框架
- Pillow、imageio 等开源图像处理库
- Microsoft 为 Secure Boot 提供的签名基础设施

---

**免责声明**：本工具可能会影响系统启动过程。请在充分理解风险的前提下使用，并始终保持系统备份。
