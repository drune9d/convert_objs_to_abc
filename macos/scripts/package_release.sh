#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_NAME="OBJ Sequence to Alembic"
PACKAGE_NAME="OBJ-Sequence-to-Alembic-macOS"
DIST_DIR="$ROOT_DIR/dist"
STAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
APP_DIR="$STAGE_DIR/$APP_NAME.app"
APP_CONTENTS="$APP_DIR/Contents"
APP_MACOS="$APP_CONTENTS/MacOS"
APP_RESOURCES="$APP_CONTENTS/Resources"
TOOL_DIR="$APP_RESOURCES/tool"
ZIP_PATH="$DIST_DIR/$PACKAGE_NAME.zip"

echo "Building converter..."
"$ROOT_DIR/build.sh"

echo "Creating release package..."
rm -rf "$STAGE_DIR" "$ZIP_PATH"
mkdir -p "$APP_MACOS" "$TOOL_DIR/bin"

cp "$ROOT_DIR/README.md" "$STAGE_DIR/"
cp "$ROOT_DIR/LICENSE" "$STAGE_DIR/"

cp "$ROOT_DIR/CMakeLists.txt" "$TOOL_DIR/"
cp "$ROOT_DIR/build.sh" "$TOOL_DIR/"
cp "$ROOT_DIR/gui.py" "$TOOL_DIR/"
cp "$ROOT_DIR/launch_gui.sh" "$TOOL_DIR/"
cp -R "$ROOT_DIR/Objs2Abc" "$TOOL_DIR/"
cp -R "$ROOT_DIR/head-poses" "$TOOL_DIR/"
cp "$ROOT_DIR/bin/Objs2Abc" "$TOOL_DIR/bin/"
chmod +x "$TOOL_DIR/build.sh" "$TOOL_DIR/launch_gui.sh" "$TOOL_DIR/bin/Objs2Abc"

cat > "$APP_CONTENTS/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>OBJ Sequence to Alembic</string>
  <key>CFBundleIdentifier</key>
  <string>io.github.obj-sequence-to-alembic.app</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>OBJ Sequence to Alembic</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1.2.2</string>
  <key>CFBundleVersion</key>
  <string>4</string>
  <key>LSMinimumSystemVersion</key>
  <string>13.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
PLIST

cat > "$APP_MACOS/$APP_NAME" <<'LAUNCHER'
#!/bin/bash
set -euo pipefail

TOOL_DIR="$(cd "$(dirname "$0")/../Resources/tool" && pwd)"
cd "$TOOL_DIR"

exec "$TOOL_DIR/launch_gui.sh"
LAUNCHER
chmod +x "$APP_MACOS/$APP_NAME"

echo "Creating zip..."
mkdir -p "$DIST_DIR"
(cd "$DIST_DIR" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$PACKAGE_NAME" "$ZIP_PATH")

echo
echo "Release package ready:"
echo "$ZIP_PATH"
