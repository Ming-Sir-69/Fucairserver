#!/bin/bash
# Builds FucairEdge.app (menu bar agent) into ~/Applications and starts it.
set -euo pipefail
cd "$(dirname "$0")"
app="$HOME/Applications/FucairEdge.app"
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
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>LSUIElement</key><true/>
</dict>
</plist>
PL
identity="${SIGN_IDENTITY:--}"
codesign --force --sign "$identity" "$app"
pkill -x FucairEdge 2>/dev/null || true
open "$app"
echo "started $app (signed with: $identity)"
