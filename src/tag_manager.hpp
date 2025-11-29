/**
 * @file tag_manager.hpp
 * @brief Simple file-based tag storage (legacy, prefer Database for tags).
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <map>

namespace BlenderFileFinder {

/**
 * @brief File-based tag manager for .blend files.
 *
 * Provides a simple tag storage system using a plain text file.
 * Tags are automatically saved when the TagManager is destroyed.
 *
 * @note This is a legacy implementation. The Database class provides
 *       the same functionality with SQLite-based storage and is
 *       preferred for new code.
 *
 * Storage location: ~/.cache/BlenderFileFinder/tags.dat
 */
class TagManager {
public:
    TagManager();

    /**
     * @brief Destructor - saves tags if modified.
     */
    ~TagManager();

    /// @name Tag Operations
    /// @{

    /**
     * @brief Add a tag to a file.
     * @param file Path to the file
     * @param tag Tag name to add
     */
    void addTag(const std::filesystem::path& file, const std::string& tag);

    /**
     * @brief Remove a tag from a file.
     * @param file Path to the file
     * @param tag Tag name to remove
     */
    void removeTag(const std::filesystem::path& file, const std::string& tag);

    /**
     * @brief Get all tags for a file.
     * @param file Path to the file
     * @return Vector of tag names
     */
    std::vector<std::string> getTags(const std::filesystem::path& file) const;

    /**
     * @brief Check if a file has a specific tag.
     * @param file Path to the file
     * @param tag Tag name to check
     * @return true if the file has the tag
     */
    bool hasTag(const std::filesystem::path& file, const std::string& tag) const;

    /// @}

    /**
     * @brief Get all known tags.
     *
     * Returns all tags that have ever been used, for autocomplete
     * and tag selection UI.
     *
     * @return Vector of all tag names
     */
    std::vector<std::string> getAllTags() const;

    /// @name Persistence
    /// @{

    /**
     * @brief Save tags to disk.
     */
    void save();

    /**
     * @brief Load tags from disk.
     */
    void load();

    /// @}

    /**
     * @brief Get all files with a specific tag.
     * @param tag Tag name to search for
     * @return Vector of file paths
     */
    std::vector<std::filesystem::path> getFilesWithTag(const std::string& tag) const;

private:
    std::filesystem::path getTagFilePath() const;

    std::map<std::string, std::set<std::string>> m_fileTags; ///< File path -> tags
    std::set<std::string> m_allTags;                         ///< All known tags
    std::filesystem::path m_dataDir;                         ///< Data directory
    bool m_dirty = false;                                    ///< Modified since load
};

} // namespace BlenderFileFinder
