/**
 * @file scan_cache.hpp
 * @brief Disk cache for directory scan results.
 */

#pragma once

#include "blend_parser.hpp"
#include <filesystem>
#include <vector>
#include <string>

namespace BlenderFileFinder {

/**
 * @brief Disk cache for .blend file scan results.
 *
 * Caches the results of directory scans to avoid re-parsing all files
 * on application startup. Cache entries are invalidated when files
 * are modified (based on modification time).
 *
 * Cache files are stored in ~/.cache/BlenderFileFinder/ with filenames
 * based on a hash of the directory path.
 *
 * @note This is separate from the Database - the scan cache provides
 *       fast startup, while the Database provides persistent storage
 *       and tag management.
 */
class ScanCache {
public:
    ScanCache();

    /**
     * @brief Save scan results for a directory.
     *
     * Writes file information to a cache file. Includes paths, sizes,
     * modification times, and metadata (but not thumbnails).
     *
     * @param directory Directory that was scanned
     * @param files Scan results to cache
     */
    void save(const std::filesystem::path& directory, const std::vector<BlendFileInfo>& files);

    /**
     * @brief Load cached results for a directory.
     *
     * Reads cached file information and validates each entry against
     * the filesystem. Files that have been modified or deleted are
     * excluded from the results.
     *
     * @param directory Directory to load cache for
     * @return Cached file info, or empty vector if no valid cache
     */
    std::vector<BlendFileInfo> load(const std::filesystem::path& directory);

    /**
     * @brief Check if a valid cache exists for a directory.
     * @param directory Directory to check
     * @return true if cache file exists (validity not checked)
     */
    bool hasValidCache(const std::filesystem::path& directory);

    /**
     * @brief Delete all cache files.
     */
    void clearAll();

    /**
     * @brief Get the cache directory path.
     * @return Path to ~/.cache/BlenderFileFinder/
     */
    std::filesystem::path getCacheDir() const;

private:
    std::filesystem::path getCacheFilePath(const std::filesystem::path& directory) const;
    std::string hashPath(const std::filesystem::path& path) const;
    bool isEntryValid(const BlendFileInfo& info) const;

    std::filesystem::path m_cacheDir;   ///< Cache directory path
};

} // namespace BlenderFileFinder
