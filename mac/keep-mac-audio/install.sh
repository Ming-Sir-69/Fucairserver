#!/bin/bash
# Installs the per-user agent that keeps the Mac's sound output when screen
# mirroring to a Fucairserver receiver starts.
#   ./install.sh [receiver name]     install or update (default: Eric-Mingle-Legion)
#   ./install.sh --uninstall         remove the agent
set -euo pipefail
label=com.ericmingle.fucairserver.keep-mac-audio
plist="$HOME/Library/LaunchAgents/$label.plist"
dir="$HOME/.fucairserver"

if [[ "${1:-}" == "--uninstall" ]]; then
    launchctl bootout "gui/$(id -u)" "$plist" 2>/dev/null || true
    rm -f "$plist" "$dir/keep-mac-audio"
    echo "removed"
    exit 0
fi

receiver="${1:-Eric-Mingle-Legion}"
mkdir -p "$dir" "$HOME/Library/LaunchAgents"
swiftc -O "$(dirname "$0")/keep-mac-audio.swift" -o "$dir/keep-mac-audio"
cat > "$plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$label</string>
  <key>ProgramArguments</key>
  <array><string>$dir/keep-mac-audio</string><string>$receiver</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Background</string>
</dict>
</plist>
PL
launchctl bootout "gui/$(id -u)" "$plist" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$plist"
echo "installed; log: $dir/keep-mac-audio.log"
