/**
 * @file database.hpp
 * @brief SQLite database for storing .blend file information and user tags.
 */

#pragma once

#include "blend_parser.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <set>
#include <optional>
#include <sqlite3.h>

namespace BlenderFileFinder {

/**
 * @brief Represents a folder location to scan for .blend files.
 *
 * Scan locations are directories that the application monitors for
 * Blender files. Each location can be scanned recursively or just
 * at the top level.
 */
struct ScanLocation {
    int64_t id = 0;                     ///< Database primary key
    std::filesystem::path path;          ///< Full path to the directory
    bool recursive = true;               ///< Whether to scan subdirectories
    bool enabled = true;                 ///< Whether this location is active
    std::string name;                    ///< Optional display name for UI
};

/**
 * @brief SQLite database manager for .blend file metadata and tags.
 *
 * Provides persistent storage for:
 * - Scan locations (directories to monitor)
 * - File information (path, size, metadata, thumbnail status)
 * - User-defined tags and file-tag associations
 *
 * The database is stored at ~/.local/share/BlenderFileFinder/database.db
 *
 * @note All database operations are synchronous. For large operations,
 *       consider running in a background thread.
 */
class Database {
public:
    Database();
    ~Database();

    /**
     * @brief Open or create the database at the specified path.
     * @param dbPath Path to the SQLite database file
     * @return true if successfully opened, false on error
     */
    bool open(const std::filesystem::path& dbPath);

    /**
     * @brief Close the database connection.
     */
    void close();

    /**
     * @brief Check if the database is currently open.
     * @return true if open, false otherwise
     */
    bool isOpen() const { return m_db != nullptr; }

    /// @name Scan Location Management
    /// @{

    /**
     * @brief Add a new scan location to the database.
     * @param path Directory path to add
     * @param recursive Whether to scan subdirectories
     * @param name Optional display name
     * @return ID of the new location, or -1 on failure
     */
    int64_t addScanLocation(const std::filesystem::path& path, bool recursive = true, const std::string& name = "");

    /**
     * @brief Remove a scan location by ID.
     * @param id Location ID to remove
     */
    void removeScanLocation(int64_t id);

    /**
     * @brief Update an existing scan location.
     * @param location Updated location data (must have valid id)
     */
    void updateScanLocation(const ScanLocation& location);

    /**
     * @brief Get all registered scan locations.
     * @return Vector of all scan locations
     */
    std::vector<ScanLocation> getAllScanLocations();

    /**
     * @brief Get a specific scan location by ID.
     * @param id Location ID to retrieve
     * @return ScanLocation if found, std::nullopt otherwise
     */
    std::optional<ScanLocation> getScanLocation(int64_t id);

    /// @}

    /// @name File Management
    /// @{

    /**
     * @brief Add a new file or update an existing one.
     * @param file File information to store
     * @param scanLocationId Optional ID of the containing scan location
     * @return ID of the file record, or -1 on failure
     */
    int64_t addOrUpdateFile(const BlendFileInfo& file, int64_t scanLocationId = 0);

    /**
     * @brief Remove a file by its database ID.
     * @param fileId File ID to remove
     */
    void removeFile(int64_t fileId);

    /**
     * @brief Remove a file by its filesystem path.
     * @param path Path of the file to remove
     */
    void removeFileByPath(const std::filesystem::path& path);

    /**
     * @brief Get file information by path.
     * @param path Path to look up
     * @return BlendFileInfo if found, std::nullopt otherwise
     */
    std::optional<BlendFileInfo> getFileByPath(const std::filesystem::path& path);

    /**
     * @brief Get all files in the database.
     * @return Vector of all stored file information
     */
    std::vector<BlendFileInfo> getAllFiles();

    /**
     * @brief Get files belonging to a specific scan location.
     * @param scanLocationId ID of the scan location
     * @return Vector of files in that location
     */
    std::vector<BlendFileInfo> getFilesByScanLocation(int64_t scanLocationId);

    /**
     * @brief Search files by filename pattern.
     * @param query Search string (uses SQL LIKE matching)
     * @return Vector of matching files
     */
    std::vector<BlendFileInfo> searchFiles(const std::string& query);

    /**
     * @brief Check if a file's stored modification time matches the filesystem.
     * @param path Path to check
     * @return true if up to date, false if modified or missing
     */
    bool isFileUpToDate(const std::filesystem::path& path);

    /**
     * @brief Remove database entries for files that no longer exist on disk.
     * @return Number of files removed
     */
    int cleanupMissingFiles();

    /// @}

    /// @name Tag Management
    /// @{

    /**
     * @brief Add a new tag to the database.
     * @param tagName Name of the tag
     * @return ID of the tag (existing or newly created)
     */
    int64_t addTag(const std::string& tagName);

    /**
     * @brief Remove a tag by ID.
     * @param tagId Tag ID to remove
     */
    void removeTag(int64_t tagId);

    /**
     * @brief Remove a tag by name.
     * @param tagName Tag name to remove
     */
    void removeTagByName(const std::string& tagName);

    /**
     * @brief Get all tags in the database.
     * @return Vector of tag names, sorted alphabetically
     */
    std::vector<std::string> getAllTags();

    /**
     * @brief Get the database ID for a tag name.
     * @param tagName Tag name to look up
     * @return Tag ID, or -1 if not found
     */
    int64_t getTagId(const std::string& tagName);

    /// @}

    /// @name File-Tag Associations
    /// @{

    /**
     * @brief Associate a tag with a file by IDs.
     * @param fileId File database ID
     * @param tagId Tag database ID
     */
    void addTagToFile(int64_t fileId, int64_t tagId);

    /**
     * @brief Associate a tag with a file by path and tag name.
     *
     * Creates the tag if it doesn't exist.
     *
     * @param filePath Path to the file
     * @param tagName Name of the tag
     */
    void addTagToFile(const std::filesystem::path& filePath, const std::string& tagName);

    /**
     * @brief Remove a tag association by IDs.
     * @param fileId File database ID
     * @param tagId Tag database ID
     */
    void removeTagFromFile(int64_t fileId, int64_t tagId);

    /**
     * @brief Remove a tag association by path and name.
     * @param filePath Path to the file
     * @param tagName Name of the tag
     */
    void removeTagFromFile(const std::filesystem::path& filePath, const std::string& tagName);

    /**
     * @brief Get all tags associated with a file.
     * @param filePath Path to the file
     * @return Vector of tag names
     */
    std::vector<std::string> getTagsForFile(const std::filesystem::path& filePath);

    /**
     * @brief Get all files that have a specific tag.
     * @param tagName Name of the tag
     * @return Vector of matching files
     */
    std::vector<BlendFileInfo> getFilesWithTag(const std::string& tagName);

    /**
     * @brief Check if a file has a specific tag.
     * @param filePath Path to the file
     * @param tagName Name of the tag
     * @return true if the file has the tag
     */
    bool fileHasTag(const std::filesystem::path& filePath, const std::string& tagName);

    /// @}

    /// @name Statistics
    /// @{

    /**
     * @brief Get the total number of files in the database.
     * @return File count
     */
    int getTotalFileCount();

    /**
     * @brief Get the total number of tags in the database.
     * @return Tag count
     */
    int getTotalTagCount();

    /**
     * @brief Get the total number of scan locations.
     * @return Location count
     */
    int getTotalScanLocationCount();

    /// @}

    /**
     * @brief Get the path to the database file.
     * @return Database file path
     */
    std::filesystem::path getDatabasePath() const { return m_dbPath; }

private:
    void createTables();
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

    int64_t getFileId(const std::filesystem::path& path);
    bool execute(const std::string& sql);

    sqlite3* m_db = nullptr;            ///< SQLite database handle
    std::filesystem::path m_dbPath;     ///< Path to database file
};

} // namespace BlenderFileFinder
