/**
 * @file app.hpp
 * @brief Main application class for Blender File Finder.
 */

#pragma once

#include "database.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

struct GLFWwindow;

namespace BlenderFileFinder {

class Scanner;
class ThumbnailCache;
class VersionGrouper;
class PreviewCache;
struct FileGroup;

/**
 * @brief Main application controller for Blender File Finder.
 *
 * Manages the application lifecycle, UI rendering, and coordination
 * between all subsystems (database, scanner, thumbnail cache, etc.).
 *
 * The application provides:
 * - Grid/list view of .blend files with thumbnails
 * - Directory scanning with background loading
 * - Version grouping of related files
 * - Tag-based organization
 * - Animated turntable previews on hover
 * - Search and filtering
 *
 * @par Architecture:
 * - Uses GLFW for window management
 * - Uses ImGui for the user interface
 * - Uses OpenGL 3.3 Core for rendering
 * - SQLite database for persistent storage
 */
class App {
public:
    App();
    ~App();

    /**
     * @brief Initialize the application.
     *
     * Creates the window, initializes OpenGL and ImGui, opens the
     * database, and sets up all subsystems.
     *
     * @return true if initialization succeeded
     */
    bool init();

    /**
     * @brief Run the main application loop.
     *
     * Processes events, updates state, and renders the UI until
     * the window is closed.
     */
    void run();

    /**
     * @brief Clean up and shut down the application.
     *
     * Releases all resources, closes the database, and destroys
     * the window.
     */
    void shutdown();

private:
    /// @name UI Rendering
    /// @{
    void renderUI();
    void renderMenuBar();
    void renderToolbar();
    void renderSidebar();
    void renderScanLocations();
    void renderAddLocation();
    void renderMainContent();
    void renderStatusBar();
    void renderNewFilesDialog();
    void renderPreviewGenerationDialog();
    void renderUserGuide();
    void renderStatisticsDialog();
    void renderBulkTagDialog();
    void renderPreloadDialog();
    /// @}

    /// @name Actions
    /// @{
    void startScan(const std::filesystem::path& path, bool forceRescan = false);
    void scanAllLocations();
    void loadFromDatabase();
    void startBackgroundLoad();
    void checkBackgroundLoadComplete();
    void openInBlender(const std::filesystem::path& path);
    void openContainingFolder(const std::filesystem::path& path);
    void checkForNewFiles();
    void startPreviewGeneration(bool forceRegenerate = false);
    void setWindowIcon();
    /// @}

    GLFWwindow* m_window = nullptr;             ///< GLFW window handle

    /// @name Subsystems
    /// @{
    std::unique_ptr<Scanner> m_scanner;
    std::unique_ptr<ThumbnailCache> m_thumbnailCache;
    std::unique_ptr<VersionGrouper> m_versionGrouper;
    std::unique_ptr<Database> m_database;
    std::unique_ptr<PreviewCache> m_previewCache;
    /// @}

    /// @name File Data
    /// @{
    std::vector<FileGroup> m_fileGroups;        ///< Grouped files to display
    std::string m_searchQuery;                  ///< Current search filter
    std::string m_tagFilter;                    ///< Current tag filter
    std::filesystem::path m_currentPath;        ///< Current browsing path
    /// @}

    /// @name Scan State
    /// @{
    bool m_isScanning = false;
    int m_scanLocationIndex = 0;
    std::vector<ScanLocation> m_pendingScanLocations;
    /// @}

    /// @name View Settings
    /// @{
    bool m_showGridView = true;                 ///< Grid vs list view
    int m_sortMode = 0;                         ///< Sort mode (name/date/size)
    bool m_sortAscending = true;                ///< Sort direction
    float m_sidebarWidth = 280.0f;              ///< Sidebar width in pixels
    float m_thumbnailSize = 128.0f;             ///< Thumbnail size in pixels
    /// @}

    /// @name Add Location Dialog
    /// @{
    char m_newLocationPath[512] = {0};
    char m_newLocationName[128] = {0};
    bool m_newLocationRecursive = true;
    /// @}

    /// @name New Files Dialog
    /// @{
    bool m_showNewFilesDialog = false;
    std::vector<std::filesystem::path> m_newFilesFound;
    std::vector<char> m_newFilesSelected;       ///< Using char for addressability
    /// @}

    /// @name Preview Generation Dialog
    /// @{
    bool m_showPreviewGenerationDialog = false;
    std::string m_currentPreviewFile;
    /// @}

    /// @name User Guide Dialog
    /// @{
    bool m_showUserGuide = false;
    /// @}

    /// @name Statistics Dialog
    /// @{
    bool m_showStatisticsDialog = false;
    /// @}

    /// @name Bulk Tag Dialog
    /// @{
    bool m_showBulkTagDialog = false;
    int m_bulkTagSelectedLocation = -1;
    char m_bulkTagName[128] = {0};
    std::vector<BlendFileInfo> m_bulkTagPreviewFiles;
    /// @}

    /// @name Preview Preloading
    /// @{
    bool m_showPreloadDialog = false;
    bool m_isPreloadingPreviews = false;
    bool m_preloadCancelRequested = false;
    size_t m_preloadCurrentIndex = 0;
    size_t m_preloadTotalCount = 0;
    std::vector<std::filesystem::path> m_preloadPaths;
    std::string m_preloadCurrentFile;
    /// @}

    /// @name Background Loading
    /// @{
    std::atomic<bool> m_isLoading{false};
    std::atomic<bool> m_loadComplete{false};
    std::jthread m_loadThread;
    std::mutex m_loadMutex;
    std::vector<FileGroup> m_loadedGroups;      ///< Loaded in background thread
    bool m_needsInitialLoad = true;
    int m_frameCount = 0;
    /// @}

    /// @name Cached Statistics
    /// Avoid querying database every frame
    /// @{
    int m_cachedFileCount = 0;
    int m_cachedTagCount = 0;
    int m_cachedLocationCount = 0;
    int m_statsUpdateFrame = 0;
    /// @}

    /// @name Cached Data
    /// Avoid querying database every frame
    /// @{
    std::vector<std::string> m_cachedAllTags;
    std::vector<ScanLocation> m_cachedScanLocations;
    std::map<int64_t, size_t> m_locationFileCounts;
    std::map<int64_t, size_t> m_locationGroupCounts;
    int m_tagsUpdateFrame = -1000;              ///< Force initial load
    int m_locationsUpdateFrame = -1000;         ///< Force initial load
    /// @}
};

} // namespace BlenderFileFinder
