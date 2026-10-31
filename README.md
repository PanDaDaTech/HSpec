# HSpec 轻谱

轻量、零依赖的桌面硬件信息面板。纯 Win32 C++，直接画在桌面层上：**不置顶、背景透明、不吃焦点**。

```
┌ 桌面（壁纸层）
│                                        [图标] Intel(R) Core(TM) i9-10850K CPU @ 3.60GHz
│                                               核心: 10    线程: 20
│                                        [图标] 主板: ROG STRIX Z490-E GAMING
│                                               厂商: ASUSTeK COMPUTER INC.
│                                               固件: 2801
│                                               启动: UEFI (Secure Boot)
│                                               证书: Pca 2011
│                                        [图标] 内存: 32 GB DDR4
│                                               型号: CMK32GX4M2B3200C16    3200 MHz
│                                        [图标] 硬盘: Samsung SSD 970 EVO Plus 1TB
│                                               容量: 932 GB    格式: GPT    类型: NVME
└ 图标区
```

## 特性

- **桌面层显示**：普通顶层分层窗口，z-order 锚定在 `Progman/WorkerW` 桌面栈之上、所有普通窗口之下；从不使用 `WS_EX_TOPMOST`，按 `Win+D` 后也会自动回到桌面。
- **逐像素透明**：GDI+ 32bpp 位图 + `UpdateLayeredWindow`，文字抗锯齿，无白底、无边框。
- **自动明暗**：读取壁纸并采样面板区域的亮度，深色壁纸自动切换为浅色文字，浅色壁纸用深色文字。
- **零第三方依赖**：不使用 WMI、PowerShell、`msinfo32`、`wmic`，不释放任何临时文件。
- **单文件源码**：`hspec.cpp` 一个翻译单元；资源只有图标、manifest 和版本信息。
- **多架构**：x64 / x86 / arm64，GitHub Actions 全量编译。
- **Win7+**：不依赖 XP；使用 `UpdateLayeredWindow` 实现逐像素透明。

## 硬件信息是怎么采集的

和 [WinosInfo](https://wuyou.net/forum.php?mod=viewthread&tid=427978) 这类工具的路径一致：**SMBIOS 原始表 + UEFI 变量 + 注册表 + 物理磁盘 IOCTL**。

| 项目 | 来源 | 关键 API / 数据 |
| :--- | :--- | :--- |
| CPU 名称 | 注册表 + CPUID 兜底 | `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString`，`__cpuid(0x80000002..4)` |
| 核心 / 线程 | 拓扑枚举 | `GetLogicalProcessorInformationEx(RelationProcessorCore)`、`GetActiveProcessorCount` |
| 主板 / BIOS / 内存条 | SMBIOS 原始表，注册表兜底 | `GetSystemFirmwareTable('RSMB', 0, ...)`，解析 Type 0/1/2/4/17；`HKLM\HARDWARE\DESCRIPTION\System\BIOS` |
| 内存总量 | 系统 | `GetPhysicallyInstalledSystemMemory`（已安装），`GlobalMemoryStatusEx`（可用量兜底） |
| 固件类型 | 内核 + 注册表兜底 | `GetFirmwareType()`（动态加载），`PEFirmwareType` |
| 安全启动状态 | 注册表 / UEFI 变量 | `...\Control\SecureBoot\State\UEFISecureBootEnabled`，`GetFirmwareEnvironmentVariableW` |
| 安全启动证书 | UEFI `db` / `dbx` | 解析 `EFI_SIGNATURE_LIST` → X.509 → `CertGetNameStringW` |
| 硬盘型号 / 总线 | 物理磁盘 | `\\.\PhysicalDriveN` + `IOCTL_STORAGE_QUERY_PROPERTY` |
| 容量 | 物理磁盘 | `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`（免管理员） |
| 分区格式 | 物理磁盘 | `IOCTL_DISK_GET_DRIVE_LAYOUT_EX` → MBR / GPT / RAW |
| 内存总量 | 系统 | `GlobalMemoryStatusEx` |

> 证书一项需要管理员权限（`SeSystemEnvironmentPrivilege` 才能读 UEFI `db`）。程序以 `asInvoker` 启动：普通权限下仍然显示“启动: UEFI (Secure Boot)”，只是省略证书行；在 WinPE / SYSTEM 下会完整显示。硬盘信息使用 `FILE_ANY_ACCESS` 的 geometry / property / layout IOCTL，**不需要管理员**。

## 使用

1. 运行 `HSpec.exe`。
2. 面板出现在主屏右侧。**右键**：刷新 / 退出。
3. 信息每 60 秒自动刷新一次；切换分辨率或 DPI 会重新排布。

单实例运行（互斥量 `HSpec_Singleton`）。不写注册表、不开机自启、不联网。

## 构建

本地（需要 Visual Studio 2019+ 的开发者命令提示符）：

```cmd
build.cmd
```

CI 每次 push 自动编译 x64 / x86 / arm64 并上传 artifact，见 `.github/workflows/build.yml`。

手动编译命令：

```cmd
rc /nologo /c65001 /fo build\hspec.res hspec.rc
cl /nologo /utf-8 /O2 /MT /EHsc /std:c++17 /D_WIN32_WINNT=0x0601 hspec.cpp build\hspec.res ^
   /link /OUT:dist\HSpec.exe /SUBSYSTEM:WINDOWS ^
   gdiplus.lib crypt32.lib advapi32.lib user32.lib gdi32.lib
```

## 与 HKeyboard / HRime 的关系

同一套做法：单文件纯 Win32 C++、MSVC 静态 CRT、GitHub Actions 三架构编译。命名沿用 `H` + 英文 / `轻` + 单字：HKeyboard / 轻键、HRime / 轻韵、**HSpec / 轻谱**。

## License

MIT
