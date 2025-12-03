/**
 * @file thumbnail_cache.hpp
 * @brief LRU cache for .blend file thumbnail textures with async loading.
 *
 * Thumbnails are cached to disk for fast loading on subsequent runs.
 */

#pragma once

#include "blend_parser.hpp"
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <optional>

namespace BlenderFileFinder {

/**
 * @brief LRU cache for .blend file thumbnails as OpenGL textures.
 *
 * Manages loading and caching of thumbnails from .blend files. Thumbnails
 * are loaded asynchronously in background threads and converted to OpenGL
 * textures on the main thread.
 *
 * Features:
 * - LRU (Least Recently Used) eviction policy
 * - Parallel background loading (4 threads by default)
 * - Thread-safe operations
 * - Automatic placeholder texture for loading/missing thumbnails
 *
 * @par Usage Pattern:
 * @code
 * ThumbnailCache cache;
 *
 * // In render loop:
 * uint32_t texId = cache.getTexture(blendFilePath);
 * // Returns placeholder if not loaded, triggers async load
 *
 * cache.processLoadedThumbnails(); // Must call each frame
 * @endcode
 *
 * @note processLoadedThumbnails() must be called from the main thread
 *       (with OpenGL context) each frame to upload textures to GPU.
 */
class ThumbnailCache {
public:
    /**
     * @brief Construct a thumbnail cache with specified capacity.
     * @param maxCacheSize Maximum number of textures to keep in cache
     */
    explicit ThumbnailCache(size_t maxCacheSize = 500);

    /**
     * @brief Destructor - stops threads and releases all textures.
     */
    ~ThumbnailCache();

    /**
     * @brief Get the OpenGL texture ID for a file's thumbnail.
     *
     * If the thumbnail is cached, returns it and marks as recently used.
     * If not cached, queues it for loading and returns the placeholder.
     *
     * @param path Path to the .blend file
     * @return OpenGL texture ID, or placeholder texture if not loaded
     */
    uint32_t getTexture(const std::filesystem::path& path);

    /**
     * @brief Request a thumbnail to be loaded (if not already queued).
     * @param path Path to the .blend file
     */
    void requestThumbnail(const std::filesystem::path& path);

    /**
     * @brief Get the placeholder texture ID.
     *
     * The placeholder is a checkerboard pattern shown while thumbnails load.
     *
     * @return OpenGL texture ID of the placeholder
     */
    uint32_t getPlaceholderTexture() const { return m_placeholderTexture; }

    /**
     * @brief Upload loaded thumbnails to GPU textures.
     *
     * Must be called from the main thread (with OpenGL context) each frame.
     * Processes all thumbnails that have finished loading in background threads.
     */
    void processLoadedThumbnails();

    /**
     * @brief Clear all cached textures and pending loads.
     */
    void clear();

    /**
     * @brief Check if a thumbnail is currently being loaded.
     * @param path Path to check
     * @return true if loading is in progress
     */
    bool isLoading(const std::filesystem::path& path) const;

    /**
     * @brief Get the number of thumbnails pending or being loaded.
     * @return Pair of (pending in queue, total requested)
     */
    std::pair<size_t, size_t> getLoadingProgress() const;

    /**
     * @brief Check if thumbnails are currently being loaded.
     * @return true if there are pending thumbnails
     */
    bool isLoadingThumbnails() const;

private:
    /**
     * @brief Cache entry storing a texture and its source path.
     */
    struct CacheEntry {
        uint32_t textureId = 0;         ///< OpenGL texture ID
        std::filesystem::path path;      ///< Source .blend file path
    };

    /**
     * @brief Request for a loaded thumbnail awaiting GPU upload.
     */
    struct LoadRequest {
        std::filesystem::path path;      ///< Source .blend file path
        BlendThumbnail thumbnail;        ///< Loaded thumbnail data
    };

    void loadThread();
    uint32_t createTexture(const BlendThumbnail& thumbnail);
    void createPlaceholderTexture();
    void evictOldest();

    // Disk cache methods
    void initDiskCache();
    std::filesystem::path getDiskCachePath(const std::filesystem::path& blendFile) const;
    std::optional<BlendThumbnail> loadFromDiskCache(const std::filesystem::path& blendFile);
    void saveToDiskCache(const std::filesystem::path& blendFile, const BlendThumbnail& thumbnail);

    size_t m_maxCacheSize;              ///< Maximum cache capacity

    /// @name LRU Cache
    /// List maintains access order (front = most recent)
    /// @{
    std::list<CacheEntry> m_cacheList;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> m_cacheMap;
    /// @}

    /// @name Loading Queue
    /// @{
    std::mutex m_queueMutex;                            ///< Protects queue access
    std::queue<std::filesystem::path> m_loadQueue;      ///< Files to load
    std::unordered_map<std::string, bool> m_loadingSet; ///< Files being loaded
    /// @}

    /// @name Loaded Results
    /// @{
    std::mutex m_loadedMutex;                   ///< Protects loaded queue
    std::queue<LoadRequest> m_loadedQueue;     ///< Thumbnails ready for GPU
    /// @}

    /// @name Background Threads
    /// @{
    std::vector<std::jthread> m_loadThreads;    ///< Loader threads
    std::atomic<bool> m_stopThread{false};      ///< Shutdown flag
    static constexpr int NUM_LOADER_THREADS = 4; ///< Number of loader threads
    /// @}

    /// @name Progress Tracking
    /// @{
    std::atomic<size_t> m_totalRequested{0};    ///< Total thumbnails requested this session
    std::atomic<size_t> m_totalLoaded{0};       ///< Total thumbnails loaded this session
    /// @}

    uint32_t m_placeholderTexture = 0;          ///< Placeholder texture ID

    /// @name Disk Cache
    /// @{
    std::filesystem::path m_diskCacheDir;       ///< Directory for cached thumbnails
    /// @}

    /// @name Anti-Thrashing
    /// Recently loaded items that shouldn't be re-requested immediately after eviction
    /// @{
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_recentlyLoaded;
    static constexpr int COOLDOWN_SECONDS = 5;  ///< Don't re-request evicted items for this long
    /// @}
};

} // namespace BlenderFileFinder
