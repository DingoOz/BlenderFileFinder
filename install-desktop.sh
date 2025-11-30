#!/bin/bash
# Install Blender File Finder desktop entry and icons for Ubuntu/Linux

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESOURCES_DIR="$SCRIPT_DIR/resources"
ICONS_DIR="$RESOURCES_DIR/icons"
BUILD_DIR="$SCRIPT_DIR/build"

# Check if the binary exists
if [ ! -f "$BUILD_DIR/BlenderFileFinder" ]; then
    echo "Error: BlenderFileFinder binary not found in $BUILD_DIR"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

# Install binary to ~/.local/bin
echo "Installing binary..."
mkdir -p ~/.local/bin
cp "$BUILD_DIR/BlenderFileFinder" ~/.local/bin/
chmod +x ~/.local/bin/BlenderFileFinder

# Install icons at various sizes
echo "Installing icons..."
for size in 16 24 32 48 64 128 256 512; do
    ICON_DIR="$HOME/.local/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$ICON_DIR"
    cp "$ICONS_DIR/blender-file-finder-${size}.png" "$ICON_DIR/blender-file-finder.png"
done

# Also install the SVG for scalable icons
SCALABLE_DIR="$HOME/.local/share/icons/hicolor/scalable/apps"
mkdir -p "$SCALABLE_DIR"
cp "$ICONS_DIR/blender-file-finder.svg" "$SCALABLE_DIR/blender-file-finder.svg"

# Update icon cache
echo "Updating icon cache..."
gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor 2>/dev/null || true

# Install desktop file
echo "Installing desktop entry..."
mkdir -p ~/.local/share/applications
sed "s|Exec=BlenderFileFinder|Exec=$HOME/.local/bin/BlenderFileFinder|" \
    "$RESOURCES_DIR/blender-file-finder.desktop" > ~/.local/share/applications/blender-file-finder.desktop

# Update desktop database
update-desktop-database ~/.local/share/applications 2>/dev/null || true

echo ""
echo "Installation complete!"
echo ""
echo "You can now:"
echo "  - Find 'Blender File Finder' in your application menu"
echo "  - Run it from terminal: ~/.local/bin/BlenderFileFinder"
echo ""
echo "To uninstall, run: $SCRIPT_DIR/uninstall-desktop.sh"
