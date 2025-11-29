/**
 * @file version_grouper.hpp
 * @brief Groups .blend files by version patterns and backup files.
 */

#pragma once

#include "blend_parser.hpp"
#include <string>
#include <vector>

namespace BlenderFileFinder {

/**
 * @brief A group of related .blend files (versions and backups).
 *
 * Files are grouped based on their base name, with version suffixes
 * and backup extensions stripped. For example, these files would be
 * grouped together:
 * - character.blend (primary)
 * - character_v01.blend (version)
 * - character_v02.blend (version)
 * - character.blend1 (backup)
 */
struct FileGroup {
    std::string baseName;                   ///< Common name without version suffix
    BlendFileInfo primaryFile;              ///< Main file (latest .blend without backup ext)
    std::vector<BlendFileInfo> versions;    ///< Older versions and backups
    bool isExpanded = false;                ///< UI state: group tree expanded
    bool isSelected = false;                ///< UI state: group selected
};

/**
 * @brief Groups .blend files by version patterns.
 *
 * Detects and groups files that are versions or backups of each other:
 *
 * **Supported version patterns:**
 * - `_v001`, `_v01`, `_v1` (version suffix)
 * - `_001`, `_01`, `_1` (numeric suffix)
 * - `-v001`, `-v1` (dash separator)
 *
 * **Backup file patterns:**
 * - `.blend1`, `.blend2`, etc. (Blender auto-backups)
 *
 * @par Example grouping:
 * @code
 * Input files:
 *   robot_v001.blend, robot_v002.blend, robot.blend,
 *   car.blend, car.blend1, car.blend2
 *
 * Output groups:
 *   Group "robot.blend": primary=robot.blend, versions=[v001, v002]
 *   Group "car.blend": primary=car.blend, versions=[.blend1, .blend2]
 * @endcode
 */
class VersionGrouper {
public:
    /**
     * @brief Group files by version patterns.
     *
     * Files with the same base name are grouped together. The primary
     * file is chosen as the main .blend file (not a backup), preferring
     * the highest version number.
     *
     * @param files List of files to group (will be moved from)
     * @return Vector of file groups, sorted alphabetically by base name
     */
    static std::vector<FileGroup> groupFiles(std::vector<BlendFileInfo>& files);

    /**
     * @brief Extract the base name from a versioned filename.
     *
     * Removes version suffixes and backup extensions.
     *
     * @param filename Filename to process (e.g., "robot_v002.blend")
     * @return Base name (e.g., "robot.blend")
     *
     * @par Examples:
     * - "model_v001.blend" → "model.blend"
     * - "scene_01.blend" → "scene.blend"
     * - "file.blend1" → "file.blend"
     */
    static std::string extractBaseName(const std::string& filename);

    /**
     * @brief Check if a filename contains a version pattern.
     * @param filename Filename to check
     * @return true if version pattern detected (e.g., "_v01", "_001")
     */
    static bool hasVersionPattern(const std::string& filename);

    /**
     * @brief Check if a file is a Blender auto-backup.
     * @param filename Filename to check
     * @return true if filename ends with .blend1, .blend2, etc.
     */
    static bool isBackupFile(const std::string& filename);

    /**
     * @brief Extract version number from a filename.
     * @param filename Filename to parse
     * @return Version number, or 0 if no version pattern found
     *
     * @par Examples:
     * - "model_v003.blend" → 3
     * - "scene_42.blend" → 42
     * - "file.blend2" → 2
     * - "noversion.blend" → 0
     */
    static int extractVersionNumber(const std::string& filename);

private:
    /**
     * @brief Sort files within a group.
     *
     * Sorts by version number (descending), then modification time.
     * Selects the primary file (non-backup with highest version).
     *
     * @param group Group to sort (modified in place)
     */
    static void sortGroup(FileGroup& group);
};

} // namespace BlenderFileFinder
