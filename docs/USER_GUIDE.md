# Fucairserver

接收 Mac、iPhone 和 iPad 的 AirPlay 画面及声音。Mac 可以选择镜像或扩展桌面。

1. 在本项目 GitHub Releases 下载 `Fucairserver-版本-windows-x64-setup.exe`。
2. 双击安装，确认 Windows 的管理员授权。默认安装到 Program Files。
3. 保留“允许专用网络和域网络中的局域网设备连接”，完成后打开 Fucairserver。
4. 让发送设备与电脑连接同一局域网，在屏幕镜像列表选择 Fucairserver。

支持目标是 Windows 10 22H2（19045）x64 和 Windows 11 x64。
32 位 Windows 和 ARM64 尚不在本发行版的验收范围内。
Windows 10 的系统更新支持取决于微软的生命周期及设备的 ESU 状态。

Settings 中可保存设备名称、接收分辨率和录制目录。默认录制目录是当前用户的
“视频\Fucairserver”。关闭或最小化窗口后接收仍在托盘运行；托盘菜单可恢复或退出。

下载更新后直接运行新安装包。设备名称、窗口设置和配对身份保存在
`%APPDATA%\Fucairserver`，升级和卸载默认保留，录制的视频也会保留。
在 Windows“设置 → 应用 → 已安装的应用”中卸载 Fucairserver。
安装器只移除本产品文件、快捷方式及所属网络规则。

如果设备可见但无法连接，检查网络是否是你信任的专用网络，必要时使用应用里的
Allow local network 按钮。组织策略控制的设备需要管理员调整策略。
网络隔离、访客 Wi-Fi、VPN 路由和发送端的音频输出选择也可能影响连接。

本项目使用开源 AirPlay 接收实现，不隶属于 AirServer 或 Apple。
公开版本暂时使用 Windows 默认应用图标，本机私人图标不随公开包发布。
Miracast 组件仍为实验功能；Google Cast 和多发送端并排显示尚未完成。
不要把这些功能作为已支持功能承诺。

预览版本可能尚未获得发行者的代码签名。请仅从本仓库下载，并核对发布页的
SHA-256；数字签名和下载哈希是不同的验证手段。不要关闭 Windows 安全防护。
