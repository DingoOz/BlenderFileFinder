#!/bin/bash
# Build a .deb package for Blender File Finder
# Usage: ./build-deb.sh [version]
#   version: Optional version number (default: 1.0.0)

set -e

VERSION="${1:-1.0.0}"
PACKAGE_NAME="blender-file-finder"
ARCH=$(dpkg --print-architecture)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DEB_DIR="$BUILD_DIR/deb-package"
PACKAGE_DIR="$DEB_DIR/${PACKAGE_NAME}_${VERSION}_${ARCH}"

echo "Building Blender File Finder v${VERSION} .deb package for ${ARCH}..."

# Step 1: Build the project in Release mode
echo ""
echo "=== Building project in Release mode ==="
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

if [ ! -f "$BUILD_DIR/BlenderFileFinder" ]; then
    echo "Error: Build failed - BlenderFileFinder binary not found"
    exit 1
fi

echo "Build complete."

# Step 2: Create package directory structure
echo ""
echo "=== Creating package structure ==="
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/bin"
mkdir -p "$PACKAGE_DIR/usr/share/applications"
mkdir -p "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps"

# Create icon directories for each size
for size in 16 24 32 48 64 128 256 512; do
    mkdir -p "$PACKAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps"
done

# Step 3: Copy files
echo "Copying files..."

# Binary
cp "$BUILD_DIR/BlenderFileFinder" "$PACKAGE_DIR/usr/bin/blender-file-finder"
chmod 755 "$PACKAGE_DIR/usr/bin/blender-file-finder"

# Desktop file
cp "$SCRIPT_DIR/resources/blender-file-finder.desktop" "$PACKAGE_DIR/usr/share/applications/"
# Update Exec path to use the installed binary name
sed -i 's|Exec=BlenderFileFinder|Exec=blender-file-finder|' "$PACKAGE_DIR/usr/share/applications/blender-file-finder.desktop"

# Icons
for size in 16 24 32 48 64 128 256 512; do
    if [ -f "$SCRIPT_DIR/resources/icons/blender-file-finder-${size}.png" ]; then
        cp "$SCRIPT_DIR/resources/icons/blender-file-finder-${size}.png" \
           "$PACKAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/blender-file-finder.png"
    fi
done

# SVG icon
if [ -f "$SCRIPT_DIR/resources/icons/blender-file-finder.svg" ]; then
    cp "$SCRIPT_DIR/resources/icons/blender-file-finder.svg" \
       "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps/blender-file-finder.svg"
fi

# Step 4: Calculate installed size (in KB)
INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR" | cut -f1)

# Step 5: Create control file
echo "Creating control file..."
cat > "$PACKAGE_DIR/DEBIAN/control" << EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${INSTALLED_SIZE}
Depends: libc6 (>= 2.34), libstdc++6 (>= 11), libglfw3 (>= 3.3), libsqlite3-0, libgl1
Maintainer: Blender File Finder Team <noreply@example.com>
Homepage: https://github.com/yourusername/BlenderFileFinder
Description: Browse and manage Blender files with thumbnails
 Blender File Finder is a standalone application to browse, organize,
 and manage your .blend files with thumbnail previews and tagging support.
 .
 Features:
  - Thumbnail previews extracted from .blend files
  - Animated turntable previews on hover
  - Tag-based organization
  - Automatic version grouping
  - Search and filter by name or tags
  - Grid and list view modes
EOF

# Step 6: Create postinst script (update icon cache)
cat > "$PACKAGE_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Update icon cache
if command -v gtk-update-icon-cache &> /dev/null; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi

exit 0
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/postinst"

# Step 7: Create postrm script (cleanup on removal)
cat > "$PACKAGE_DIR/DEBIAN/postrm" << 'EOF'
#!/bin/bash
set -e

# Update icon cache after removal
if command -v gtk-update-icon-cache &> /dev/null; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi

exit 0
EOF
chmod 755 "$PACKAGE_DIR/DEBIAN/postrm"

# Step 8: Set proper ownership and permissions
echo "Setting permissions..."
find "$PACKAGE_DIR" -type d -exec chmod 755 {} \;
find "$PACKAGE_DIR/usr" -type f -exec chmod 644 {} \;
chmod 755 "$PACKAGE_DIR/usr/bin/blender-file-finder"

# Step 9: Build the .deb package
echo ""
echo "=== Building .deb package ==="
cd "$DEB_DIR"
dpkg-deb --build --root-owner-group "$PACKAGE_DIR"

# Move to build directory root
DEB_FILE="${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
mv "$DEB_DIR/$DEB_FILE" "$BUILD_DIR/"

echo ""
echo "=== Package built successfully ==="
echo "Location: $BUILD_DIR/$DEB_FILE"
echo ""
echo "To install:"
echo "  sudo dpkg -i $BUILD_DIR/$DEB_FILE"
echo ""
echo "To install with dependencies:"
echo "  sudo apt install ./$DEB_FILE"
echo ""
echo "To uninstall:"
echo "  sudo apt remove ${PACKAGE_NAME}"
