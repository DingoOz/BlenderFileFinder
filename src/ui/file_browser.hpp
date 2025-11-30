/**
 * @file file_browser.hpp
 * @brief Directory browser UI component.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace BlenderFileFinder {

/**
 * @brief Information about a discovered network mount.
 */
struct NetworkMount {
    std::filesystem::path path;      ///< Full path to the mount
    std::string displayName;         ///< Human-readable name (e.g., "server/share")
    std::string server;              ///< Server hostname
    std::string share;               ///< Share name
    std::string protocol;            ///< Protocol (smb, nfs, sftp, etc.)
};

/**
 * @brief Directory browser widget for navigating the filesystem.
 *
 * Displays a list of subdirectories in the current path with
 * navigation controls (up, home) and sorting options.
 *
 * Also discovers and displays network mounts (GVFS, /mnt, /media)
 * for easy access to network shares.
 *
 * Used in the sidebar to allow users to browse to directories
 * they want to scan for .blend files.
 */
class FileBrowser {
public:
    /**
     * @brief Callback type for path-related actions.
     * @param path The selected directory path
     */
    using PathCallback = std::function<void(const std::filesystem::path&)>;

    /**
     * @brief Directory sorting modes.
     */
    enum class SortMode {
        Name,   ///< Sort alphabetically by name
        Date    ///< Sort by modification date
    };

    FileBrowser();

    /**
     * @brief Render the file browser widget.
     *
     * Displays navigation buttons, current path, and directory list.
     * Must be called within an ImGui context.
     */
    void render();

    /**
     * @brief Get the current directory path.
     * @return Current browsing path
     */
    const std::filesystem::path& getCurrentPath() const { return m_currentPath; }

    /**
     * @brief Navigate to a new directory.
     * @param path Directory to navigate to
     */
    void setCurrentPath(const std::filesystem::path& path);

    /**
     * @brief Set callback for scan requests.
     *
     * Called when the user wants to scan the current directory.
     *
     * @param callback Function to call with the directory path
     */
    void setScanCallback(PathCallback callback) { m_scanCallback = std::move(callback); }

    /**
     * @brief Get the list of recently visited paths.
     * @return Vector of recent paths (most recent first)
     */
    const std::vector<std::filesystem::path>& getRecentPaths() const { return m_recentPaths; }

    /**
     * @brief Add a path to the recent paths list.
     * @param path Path to add
     */
    void addRecentPath(const std::filesystem::path& path);

    /**
     * @brief Refresh the list of network mounts.
     *
     * Discovers GVFS mounts, /mnt, and /media directories.
     */
    void refreshNetworkMounts();

    /**
     * @brief Get discovered network mounts.
     * @return Vector of network mount information
     */
    const std::vector<NetworkMount>& getNetworkMounts() const { return m_networkMounts; }

private:
    void navigateUp();
    void refreshDirectoryList();
    void sortDirectoryList();
    NetworkMount parseGvfsMount(const std::filesystem::path& mountPath) const;

    std::filesystem::path m_currentPath;                    ///< Current directory
    std::vector<std::filesystem::directory_entry> m_directoryEntries; ///< Directory listing
    std::vector<std::filesystem::path> m_recentPaths;       ///< Recently visited paths
    std::vector<NetworkMount> m_networkMounts;              ///< Discovered network mounts

    PathCallback m_scanCallback;                            ///< Scan request callback
    char m_pathBuffer[512] = {0};                           ///< Path input buffer

    SortMode m_sortMode = SortMode::Name;                   ///< Current sort mode
    bool m_sortAscending = true;                            ///< Sort direction
    bool m_showNetworkLocations = true;                     ///< Show network section
    int m_networkRefreshFrame = -1000;                      ///< Frame counter for refresh
};

} // namespace BlenderFileFinder
