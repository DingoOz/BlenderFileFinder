#!/bin/bash
# Uninstall Blender File Finder desktop entry and icons

echo "Removing Blender File Finder..."

# Remove binary
rm -f ~/.local/bin/BlenderFileFinder

# Remove icons
for size in 16 24 32 48 64 128 256 512; do
    rm -f ~/.local/share/icons/hicolor/${size}x${size}/apps/blender-file-finder.png
done
rm -f ~/.local/share/icons/hicolor/scalable/apps/blender-file-finder.svg

# Update icon cache
gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor 2>/dev/null || true

# Remove desktop file
rm -f ~/.local/share/applications/blender-file-finder.desktop

# Update desktop database
update-desktop-database ~/.local/share/applications 2>/dev/null || true

echo "Uninstall complete!"
echo ""
echo "Note: User data remains in:"
echo "  - ~/.local/share/BlenderFileFinder/ (database)"
echo "  - ~/.cache/BlenderFileFinder/ (thumbnail cache)"
echo "To remove data: rm -rf ~/.local/share/BlenderFileFinder ~/.cache/BlenderFileFinder"
