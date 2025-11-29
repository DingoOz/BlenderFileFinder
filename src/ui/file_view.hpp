/**
 * @file file_view.hpp
 * @brief Main file listing view with grid and list modes.
 */

#pragma once

#include "../version_grouper.hpp"
#include "../thumbnail_cache.hpp"
#include "../database.hpp"
#include "../preview_cache.hpp"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <chrono>

namespace BlenderFileFinder {

/**
 * @brief Main file listing view with grid and list display modes.
 *
 * Displays .blend files as a grid of thumbnails or a sortable list.
 * Supports:
 * - Thumbnail display from embedded .blend previews
 * - Animated turntable previews on hover
 * - File version grouping
 * - Tag display and filtering
 * - Sorting by name, date, or size
 * - Search filtering
 * - Context menus for file operations
 *
 * @par Grid View:
 * Displays files as cards with thumbnails, filenames, version badges,
 * and tag indicators. Thumbnails animate on hover if preview data exists.
 *
 * @par List View:
 * Displays files in a sortable table with columns for thumbnail, name,
 * tags, size, date, and Blender version. Supports tree-style expansion
 * of version groups.
 */
class FileView {
public:
    /**
     * @brief Callback type for file selection/opening actions.
     * @param file The file that was selected or opened
     */
    using FileCallback = std::function<void(const BlendFileInfo&)>;

    /**
     * @brief Callback type for path-related actions.
     * @param path The path (e.g., containing folder)
     */
    using PathCallback = std::function<void(const std::filesystem::path&)>;

    /**
     * @brief Callback type for tag filter changes.
     * @param tag The selected tag (empty for "All Tags")
     */
    using TagFilterCallback = std::function<void(const std::string&)>;

    FileView();

    /**
     * @brief Render the file view.
     *
     * Displays the file listing with current view mode and filtering.
     * Must be called within an ImGui context.
     *
     * @param groups File groups to display
     * @param cache Thumbnail cache for texture lookup
     * @param previewCache Preview cache for animated previews
     * @param database Database for tag lookup
     * @param filter Search filter string
     * @param tagFilter Tag to filter by (empty for no filter)
     */
    void render(std::vector<FileGroup>& groups, ThumbnailCache& cache,
                PreviewCache& previewCache, Database& database,
                const std::string& filter, const std::string& tagFilter = "");

    /// @name View Settings
    /// @{
    bool isGridView() const { return m_gridView; }
    void setGridView(bool grid) { m_gridView = grid; }
    void toggleView() { m_gridView = !m_gridView; }

    float getThumbnailSize() const { return m_thumbnailSize; }
    void setThumbnailSize(float size) { m_thumbnailSize = size; }
    /// @}

    /// @name Version Display
    /// @{
    bool isShowAllVersions() const { return m_showAllVersions; }
    void setShowAllVersions(bool show) { m_showAllVersions = show; }
    /// @}

    /// @name Sorting
    /// @{
    enum class SortMode { Name, Date, Size };
    SortMode getSortMode() const { return m_sortMode; }
    void setSortMode(SortMode mode) { m_sortMode = mode; }
    bool isSortAscending() const { return m_sortAscending; }
    void setSortAscending(bool asc) { m_sortAscending = asc; }
    /// @}

    /// @name Callbacks
    /// @{
    void setOpenCallback(FileCallback callback) { m_openCallback = std::move(callback); }
    void setSelectCallback(FileCallback callback) { m_selectCallback = std::move(callback); }
    void setOpenFolderCallback(PathCallback callback) { m_openFolderCallback = std::move(callback); }
    void setTagFilterCallback(TagFilterCallback callback) { m_tagFilterCallback = std::move(callback); }
    /// @}

    /// @name Tag Filter
    /// @{
    void setAvailableTags(const std::vector<std::string>& tags) { m_availableTags = tags; }
    /// @}

    /// @name Selection
    /// @{
    const std::filesystem::path& getSelectedPath() const { return m_selectedPath; }
    bool hasSelection() const { return !m_selectedPath.empty(); }
    void clearSelection() { m_selectedPath.clear(); }
    /// @}

private:
    void renderGridView(std::vector<FileGroup>& groups, ThumbnailCache& cache, PreviewCache& previewCache, const std::string& filter);
    void renderListView(std::vector<FileGroup>& groups, ThumbnailCache& cache, PreviewCache& previewCache, const std::string& filter);
    void renderFileItem(const BlendFileInfo& file, ThumbnailCache& cache, PreviewCache& previewCache, bool isPrimary = true);
    void renderFileContextMenu(const BlendFileInfo& file);
    void renderFileDetails(const BlendFileInfo& file);
    void renderTagMenu(const BlendFileInfo& file);
    void renderFileTags(const BlendFileInfo& file);

    bool matchesFilter(const std::string& filename, const std::string& filter) const;
    bool matchesFilterWithTags(const BlendFileInfo& file, const std::string& filter) const;
    bool matchesTagFilter(const BlendFileInfo& file) const;
    std::string formatFileSize(uintmax_t bytes) const;
    std::string formatDate(const std::filesystem::file_time_type& time) const;
    bool isSelected(const BlendFileInfo& file) const { return file.path == m_selectedPath; }

    bool m_gridView = true;                     ///< Grid vs list view
    float m_thumbnailSize = 128.0f;             ///< Thumbnail size in pixels
    SortMode m_sortMode = SortMode::Name;       ///< Current sort mode
    bool m_sortAscending = true;                ///< Sort direction
    bool m_showAllVersions = false;             ///< Show all versions vs grouped

    std::filesystem::path m_selectedPath;        ///< Currently selected file
    std::string m_tagFilter;                    ///< Active tag filter

    char m_newTagBuffer[64] = {0};              ///< New tag input buffer

    Database* m_database = nullptr;              ///< Database reference
    PreviewCache* m_previewCache = nullptr;      ///< Preview cache reference

    /// @name Hover Animation
    /// @{
    std::filesystem::path m_hoveredPath;
    std::chrono::steady_clock::time_point m_hoverStartTime;
    /// @}

    /// @name Tag Cache
    /// Avoids database queries every frame
    /// @{
    mutable std::map<std::filesystem::path, std::vector<std::string>> m_tagCache;
    mutable std::set<std::filesystem::path> m_pendingTagLoads;
    mutable int m_tagCacheFrame = 0;
    mutable int m_tagsLoadedThisFrame = 0;
    int m_currentFrame = 0;

    const std::vector<std::string>& getCachedTags(const std::filesystem::path& path) const;
    void loadPendingTags();
    void invalidateTagCache() { m_tagCache.clear(); m_pendingTagLoads.clear(); }
    /// @}

    FileCallback m_openCallback;                ///< File open callback
    FileCallback m_selectCallback;              ///< File select callback
    PathCallback m_openFolderCallback;          ///< Open folder callback
    TagFilterCallback m_tagFilterCallback;      ///< Tag filter change callback

    std::vector<std::string> m_availableTags;   ///< Available tags for filter dropdown
};

} // namespace BlenderFileFinder
