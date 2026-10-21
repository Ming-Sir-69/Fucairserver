# Fucairserver

Windows 上的 AirPlay 投屏与音频接收器，支持 Mac 镜像和扩展桌面、录制、设备名称设置及系统托盘。

## 下载和安装

在 [Releases](https://github.com/Ming-Sir-69/Fucairserver/releases) 下载 `Fucairserver-版本-windows-x64-setup.exe`，运行安装后，从开始菜单打开 Fucairserver。发送端和电脑需处于同一局域网。也提供 ZIP 便携包。

当前发行面向 Windows 10 22H2（19045）x64 和 Windows 11 x64。Windows ARM64 和 32 位系统不在本次发行范围内。预览版的实际测试范围见 [兼容性与验收](docs/COMPATIBILITY.md)。

安装器提供开始菜单、可选桌面快捷方式、Windows 应用卸载入口和局域网规则。更新时直接运行新安装包；设备名称、设置、配对身份和录制文件会保留。正常运行不需要管理员权限、原 AirServer、Bonjour、开发工具或在线激活。

详细操作见 [使用说明](docs/USER_GUIDE.md)。

## 功能

- Mac、iPhone、iPad 的 AirPlay 画面与声音接收；Mac 可使用扩展桌面。
- 自定义接收设备名称、分辨率、窗口和视频录制设置。
- 托盘驻留，关闭或最小化窗口后继续接收，托盘菜单可退出。
- 配置与配对身份存放于当前用户的 `%APPDATA%\Fucairserver`。
- 默认录制目录是当前用户的“视频\Fucairserver”。

Miracast 组件是实验功能，真实发送端收流仍待验收；Google Cast、多路并排显示尚未完成。公开版本暂用 Windows 默认应用图标，本机私人图标不随仓库及公开二进制分发。

## 构建

使用固定上游源码、带哈希锁的 MSYS2 UCRT64 依赖及 Inno Setup。参见 [构建与发布](docs/BUILDING.md)。CI 包含源码构建、回归测试和安装器生命周期验收。发行物包括安装包、便携 ZIP、产品源码、运行库对应源码、SHA-256 和 CycloneDX SBOM。

## 来源与许可证

本项目基于 [AirPlay-Windows](https://github.com/moieric11/AirPlay-Windows)，并使用 UxPlay 相关协议实现、Dear ImGui 等开源组件。界面沿用并适配 AirPlay-Windows。Eric Mingle 69 / REA 为产品定制署名。它不隶属于 AirServer 或 Apple，也不是恢复出的 AirServer 源码。

项目按 GPL-3.0-only 分发，第三方各自的版权和许可证继续适用。参见 [LICENSE](LICENSE)、[第三方通知](THIRD_PARTY_NOTICES.md) 和 [安全说明](SECURITY.md)。
