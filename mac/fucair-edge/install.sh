#!/bin/bash
# Builds FucairEdge.app into ~/Applications, signs it with the stable local
# identity from signing.sh (so macOS keeps its Accessibility and Input
# Monitoring approval across updates) and starts it at login.
set -euo pipefail
cd "$(dirname "$0")"
source ./signing.sh

app="$HOME/Applications/FucairEdge.app"
label="com.ericmingle.fucairserver.edge"
agent="$HOME/Library/LaunchAgents/$label.plist"

mkdir -p "$app/Contents/MacOS"
swiftc -O FucairEdge.swift -o "$app/Contents/MacOS/FucairEdge"
cat > "$app/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>ai.mingle.fucair-edge</string>
  <key>CFBundleName</key><string>FucairEdge</string>
  <key>CFBundleExecutable</key><string>FucairEdge</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSUIElement</key><true/>
  <key>NSLocalNetworkUsageDescription</key><string>FucairEdge 需要在本地网络上找到并连接运行 Fucairserver 的电脑，用于键鼠跨屏和剪贴板共享。</string>
  <key>NSBonjourServices</key><array><string>_fucair-edge._tcp</string></array>
</dict>
</plist>
PL
codesign --force --keychain "$sign_keychain" --sign "$sign_name" "$app"

mkdir -p "$HOME/Library/LaunchAgents"
cat > "$agent" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$label</string>
  <key>ProgramArguments</key><array><string>$app/Contents/MacOS/FucairEdge</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>
  <key>ProcessType</key><string>Interactive</string>
</dict>
</plist>
PL
launchctl bootout "gui/$(id -u)/$label" 2>/dev/null || true
pkill -x FucairEdge 2>/dev/null || true
sleep 1
launchctl bootstrap "gui/$(id -u)" "$agent"
echo "installed $app ($(codesign -dr - "$app" 2>&1 | grep designated))"
