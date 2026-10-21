# Fucairserver Wireless Display

本组件通过 Windows Miracast、MediaPlayer 和 Windows Composition 接收无线显示，使用 Windows 系统 DLL。源码和引用组件的许可证见 THIRD_PARTY_NOTICES.md。

构建：

```powershell
pwsh -NoProfile -File scripts/Build-Miracast.ps1
```

Build-Miracast.ps1 使用第三方目录中已固定版本的 Microsoft C++/WinRT 生成器、生成头文件及现有 GCC 工具链，输出 build/miracast/Fucairserver.Miracast.exe。原图标来自 assets/Fucairserver.ico。

运行：无参数打开控制窗口；--start --name Fucairserver 启动接收。默认不写日志文件，需要文件诊断时显式传入 --log。

--probe 查询系统能力；--ui-smoke 检查窗口初始化与退出。两者不会启动接收；仅在显式 --report 路径时写检查文件。

组件处理一路发送端。Mac/iPhone 的 AirPlay 由主程序提供。Windows 实际无线显示收流应使用兼容发送端另行验证。
