#!/bin/bash
set -e

APP_NAME="MidiMonitor"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$PROJECT_DIR/$APP_NAME.app/Contents"

echo "Building $APP_NAME..."
cd "$PROJECT_DIR"
swift build -c release --product "$APP_NAME"

rm -rf "$PROJECT_DIR/$APP_NAME.app"
mkdir -p "$APP_DIR/MacOS" "$APP_DIR/Resources"
cp "$PROJECT_DIR/.build/release/$APP_NAME" "$APP_DIR/MacOS/$APP_NAME"

cat > "$APP_DIR/Info.plist" << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>MidiMonitor</string>
    <key>CFBundleIdentifier</key>
    <string>com.collinpeterson.midi-monitor</string>
    <key>CFBundleName</key>
    <string>MidiMonitor</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>LSUIElement</key>
    <true/>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSAppTransportSecurity</key>
    <dict>
        <key>NSAllowsLocalNetworking</key>
        <true/>
    </dict>
</dict>
</plist>
PLIST

echo "✓ Built: $PROJECT_DIR/$APP_NAME.app"
echo ""
echo "Run:  open $PROJECT_DIR/$APP_NAME.app"
echo "Then: System Settings → General → Login Items → + → select MidiMonitor.app"
