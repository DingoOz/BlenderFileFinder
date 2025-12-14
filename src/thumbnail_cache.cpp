#include "thumbnail_cache.hpp"
#include "debug.hpp"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace BlenderFileFinder {

ThumbnailCache::ThumbnailCache(size_t maxCacheSize)
    : m_maxCacheSize(maxCacheSize) {
    DEBUG_LOG("ThumbnailCache constructor, maxSize=" << maxCacheSize);

    // Initialize disk cache directory
    initDiskCache();

    createPlaceholderTexture();
    DEBUG_LOG("Placeholder texture created: " << m_placeholderTexture);

    // Start multiple loader threads for parallel thumbnail loading
    for (int i = 0; i < NUM_LOADER_THREADS; ++i) {
        m_loadThreads.emplace_back([this]() {
            loadThread();
        });
    }
    DEBUG_LOG("Started " << NUM_LOADER_THREADS << " loader threads");
}

ThumbnailCache::~ThumbnailCache() {
    m_stopThread = true;
    for (auto& thread : m_loadThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    clear();

    if (m_placeholderTexture) {
        glDeleteTextures(1, &m_placeholderTexture);
    }
}

void ThumbnailCache::createPlaceholderTexture() {
    // Create a simple gray placeholder texture
    const int size = 128;
    std::vector<uint8_t> pixels(size * size * 4);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int idx = (y * size + x) * 4;
            // Checkerboard pattern
            bool light = ((x / 16) + (y / 16)) % 2 == 0;
            uint8_t gray = light ? 80 : 60;
            pixels[idx + 0] = gray;
            pixels[idx + 1] = gray;
            pixels[idx + 2] = gray;
            pixels[idx + 3] = 255;
        }
    }

    glGenTextures(1, &m_placeholderTexture);
    glBindTexture(GL_TEXTURE_2D, m_placeholderTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

uint32_t ThumbnailCache::createTexture(const BlendThumbnail& thumbnail) {
    uint32_t textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, thumbnail.width, thumbnail.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, thumbnail.pixels.data());
    return textureId;
}

uint32_t ThumbnailCache::getTexture(const std::filesystem::path& path) {
    static int callCount = 0;
    bool logThis = (callCount < 5);
    callCount++;

    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] checking empty...");
    if (path.empty()) {
        DEBUG_LOG("getTexture: empty path!");
        return m_placeholderTexture;
    }

    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] path.string()...");
    std::string key = path.string();
    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] key=" << path.filename());

    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] cacheMap.find...");
    auto it = m_cacheMap.find(key);
    if (it != m_cacheMap.end()) {
        if (logThis) DEBUG_LOG("getTexture[" << callCount << "] found in cache, splicing...");
        // Move to front (most recently used)
        if (it->second != m_cacheList.end()) {
            m_cacheList.splice(m_cacheList.begin(), m_cacheList, it->second);
            return it->second->textureId;
        } else {
            DEBUG_LOG("getTexture: invalid iterator for " << key);
            return m_placeholderTexture;
        }
    }

    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] not in cache, requesting...");
    // Request loading if not already loading
    requestThumbnail(path);
    if (logThis) DEBUG_LOG("getTexture[" << callCount << "] returning placeholder");
    return m_placeholderTexture;
}

void ThumbnailCache::requestThumbnail(const std::filesystem::path& path) {
    std::string key = path.string();

    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Skip if already in cache or loading
    if (m_cacheMap.count(key) || m_loadingSet.count(key)) {
        return;
    }

    // Anti-thrashing: don't re-request items that were recently loaded
    // This prevents the eviction→re-request→eviction loop
    auto recentIt = m_recentlyLoaded.find(key);
    if (recentIt != m_recentlyLoaded.end()) {
        auto elapsed = std::chrono::steady_clock::now() - recentIt->second;
        if (elapsed < std::chrono::seconds(COOLDOWN_SECONDS)) {
            // Still in cooldown, use placeholder instead of re-requesting
            return;
        }
        // Cooldown expired, remove from recently loaded
        m_recentlyLoaded.erase(recentIt);
    }

    m_loadingSet[key] = true;
    m_loadQueue.push(path);
    m_totalRequested++;
}

bool ThumbnailCache::isLoading(const std::filesystem::path& path) const {
    std::string key = path.string();
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_queueMutex));
    return m_loadingSet.count(key) > 0;
}

void ThumbnailCache::loadThread() {
    while (!m_stopThread) {
        std::filesystem::path pathToLoad;

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_loadQueue.empty()) {
                // Queue is empty - release mutex BEFORE sleeping
                // to avoid blocking the main thread
            } else {
                pathToLoad = std::move(m_loadQueue.front());
                m_loadQueue.pop();
            }
        }

        // If no work, sleep outside the lock to avoid blocking other threads
        if (pathToLoad.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        LoadRequest request;
        request.path = pathToLoad;

        // First, try loading from disk cache (much faster than parsing .blend)
        auto cachedThumb = loadFromDiskCache(pathToLoad);

        if (cachedThumb) {
            // Cache hit - use cached thumbnail (may be empty marker for files without thumbnails)
            request.thumbnail = std::move(*cachedThumb);
        } else {
            // Cache miss - check if file is accessible first
            std::error_code ec;
            bool fileExists = std::filesystem::exists(pathToLoad, ec);

            if (!fileExists || ec) {
                // File doesn't exist or is inaccessible - don't keep retrying
                request.thumbnail.width = 0;
                request.thumbnail.height = 0;
                // Save empty marker so we don't retry this file
                saveToDiskCache(pathToLoad, request.thumbnail);
            } else {
                // Parse file and extract thumbnail
                auto parseStart = std::chrono::steady_clock::now();
                auto info = BlendParser::parseQuick(pathToLoad);
                auto parseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - parseStart).count();

                // Log all parses that take > 50ms (could indicate I/O issues)
                if (parseMs > 50) {
                    DEBUG_LOG("Slow parseQuick: " << pathToLoad.filename() << " took " << parseMs << "ms (thread)");
                }

                if (info && info->thumbnail) {
                    request.thumbnail = std::move(*info->thumbnail);
                } else {
                    // No thumbnail in file - create an empty thumbnail marker
                    request.thumbnail.width = 0;
                    request.thumbnail.height = 0;
                }
                // Save to disk cache (including empty markers for files without thumbnails)
                saveToDiskCache(pathToLoad, request.thumbnail);
            }
        }

        std::lock_guard<std::mutex> lock(m_loadedMutex);
        m_loadedQueue.push(std::move(request));
    }
}

void ThumbnailCache::processLoadedThumbnails() {
    // Extract all pending requests under the lock, then process without holding it
    // This avoids lock order issues with loadThread() which acquires m_queueMutex first
    std::queue<LoadRequest> toProcess;
    {
        std::lock_guard<std::mutex> lock(m_loadedMutex);
        std::swap(toProcess, m_loadedQueue);
    }

    if (toProcess.empty()) {
        return;
    }

    int processedCount = 0;
    auto processStart = std::chrono::steady_clock::now();
    std::vector<std::string> processedKeys;  // Track keys to update in m_loadingSet

    while (!toProcess.empty()) {
        auto& request = toProcess.front();
        std::string key = request.path.string();

        uint32_t textureId;

        // Check if this is a valid thumbnail or a "no thumbnail" marker
        if (request.thumbnail.width > 0 && request.thumbnail.height > 0) {
            // Create texture on main thread (OpenGL context)
            auto texStart = std::chrono::steady_clock::now();
            textureId = createTexture(request.thumbnail);
            auto texMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - texStart).count();
            if (texMs > 10) {
                DEBUG_LOG("Slow texture creation: " << request.path.filename() << " took " << texMs << "ms");
            }
        } else {
            // No thumbnail in file - use placeholder but cache it
            // so we don't keep trying to load this file
            textureId = m_placeholderTexture;
        }
        processedCount++;

        // Check if already in cache (shouldn't happen, but safeguard)
        auto existingIt = m_cacheMap.find(key);
        if (existingIt != m_cacheMap.end()) {
            bool existingIsReal = (existingIt->second->textureId != m_placeholderTexture);
            bool newIsReal = (textureId != m_placeholderTexture);

            if (existingIsReal && !newIsReal) {
                // Don't overwrite a real thumbnail with placeholder - skip
                processedKeys.push_back(key);
                toProcess.pop();
                continue;
            }
            if (existingIsReal && newIsReal) {
                // Both real - keep existing, skip new
                processedKeys.push_back(key);
                toProcess.pop();
                continue;
            }
            if (!existingIsReal && !newIsReal) {
                // Both placeholder - skip
                processedKeys.push_back(key);
                toProcess.pop();
                continue;
            }
            // Existing is placeholder, new is real - remove old entry to replace
            m_cacheList.erase(existingIt->second);
            m_cacheMap.erase(existingIt);
        }

        // Evict if cache is full
        while (m_cacheList.size() >= m_maxCacheSize) {
            evictOldest();
        }

        // Add to cache
        CacheEntry entry;
        entry.textureId = textureId;
        entry.path = request.path;

        m_cacheList.push_front(entry);
        m_cacheMap[key] = m_cacheList.begin();
        m_totalLoaded++;

        processedKeys.push_back(key);
        toProcess.pop();
    }

    // Update loading set and recently loaded map (separate lock acquisition)
    // This ensures consistent lock ordering: never hold m_loadedMutex while acquiring m_queueMutex
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        auto now = std::chrono::steady_clock::now();
        for (const auto& key : processedKeys) {
            m_loadingSet.erase(key);
            m_recentlyLoaded[key] = now;
        }

        // Periodically clean up old entries from recentlyLoaded
        if (m_recentlyLoaded.size() > 1000) {
            for (auto it = m_recentlyLoaded.begin(); it != m_recentlyLoaded.end(); ) {
                if (now - it->second > std::chrono::seconds(COOLDOWN_SECONDS * 2)) {
                    it = m_recentlyLoaded.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // Log if we processed thumbnails and it took significant time
    if (processedCount > 0) {
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - processStart).count();
        if (totalMs > 20 || processedCount > 5) {
            DEBUG_LOG("processLoadedThumbnails: created " << processedCount << " textures in " << totalMs << "ms");
        }
    }
}

void ThumbnailCache::evictOldest() {
    if (m_cacheList.empty()) return;

    auto& oldest = m_cacheList.back();
    // Don't delete the placeholder texture - it's shared
    if (oldest.textureId != m_placeholderTexture) {
        glDeleteTextures(1, &oldest.textureId);
    }
    m_cacheMap.erase(oldest.path.string());
    m_cacheList.pop_back();
}

void ThumbnailCache::clear() {
    for (auto& entry : m_cacheList) {
        // Don't delete the placeholder texture - it's shared
        if (entry.textureId != m_placeholderTexture) {
            glDeleteTextures(1, &entry.textureId);
        }
    }
    m_cacheList.clear();
    m_cacheMap.clear();

    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_loadQueue.empty()) {
        m_loadQueue.pop();
    }
    m_loadingSet.clear();
    m_recentlyLoaded.clear();

    // Reset progress counters
    m_totalRequested = 0;
    m_totalLoaded = 0;
}

std::pair<size_t, size_t> ThumbnailCache::getLoadingProgress() const {
    // Return current pending count, not cumulative session totals
    // This gives accurate progress when files are evicted and re-requested
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_queueMutex));
    size_t pending = m_loadingSet.size();  // Files from request until fully cached

    // m_loadingSet already includes files in m_loadedQueue (they're removed together)
    // So just return pending count as total, with 0 "completed" since we can't track batch progress
    return {0, pending};
}

bool ThumbnailCache::isLoadingThumbnails() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_queueMutex));
    return !m_loadingSet.empty();
}

// ============================================================================
// Disk Cache Implementation
// ============================================================================

void ThumbnailCache::initDiskCache() {
    const char* home = std::getenv("HOME");
    if (home) {
        m_diskCacheDir = std::filesystem::path(home) / ".cache" / "BlenderFileFinder" / "thumbnails";
    } else {
        m_diskCacheDir = "/tmp/BlenderFileFinder/thumbnails";
    }

    std::error_code ec;
    std::filesystem::create_directories(m_diskCacheDir, ec);
    if (ec) {
        DEBUG_LOG("Failed to create thumbnail cache directory: " << ec.message());
    } else {
        DEBUG_LOG("Thumbnail disk cache: " << m_diskCacheDir);
    }
}

std::filesystem::path ThumbnailCache::getDiskCachePath(const std::filesystem::path& blendFile) const {
    // Use ONLY the path hash for the filename - this ensures consistency
    // even when filesystem operations fail intermittently (network files)
    // Cache invalidation is handled by checking mod time when loading
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16)
       << std::hash<std::string>{}(blendFile.string()) << ".thumb";
    return m_diskCacheDir / ss.str();
}

std::optional<BlendThumbnail> ThumbnailCache::loadFromDiskCache(const std::filesystem::path& blendFile) {
    std::filesystem::path cachePath = getDiskCachePath(blendFile);

    std::ifstream file(cachePath, std::ios::binary);
    if (!file) {
        return std::nullopt;  // Cache miss
    }

    // Read and verify magic bytes
    char magic[4];
    file.read(magic, 4);
    if (!file || std::memcmp(magic, "BFFT", 4) != 0) {
        return std::nullopt;  // Invalid cache file
    }

    // Read version
    uint32_t version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!file || version != 2) {
        // Version 1 didn't have mod time - invalidate old cache
        return std::nullopt;
    }

    // Read stored modification time
    int64_t storedModTime;
    file.read(reinterpret_cast<char*>(&storedModTime), sizeof(storedModTime));
    if (!file) {
        return std::nullopt;
    }

    // Check if source file has been modified (cache invalidation)
    // Skip this check if stored time is 0 (file was inaccessible when cached)
    if (storedModTime != 0) {
        std::error_code ec;
        auto currentModTime = std::filesystem::last_write_time(blendFile, ec);
        if (!ec) {
            int64_t currentModTimeCount = currentModTime.time_since_epoch().count();
            if (currentModTimeCount != storedModTime) {
                return std::nullopt;  // Source file changed, cache invalid
            }
        }
        // If we can't get current mod time, use cached version anyway
    }

    // Read dimensions
    uint32_t width, height;
    file.read(reinterpret_cast<char*>(&width), sizeof(width));
    file.read(reinterpret_cast<char*>(&height), sizeof(height));
    if (!file || width > 4096 || height > 4096) {
        return std::nullopt;  // Invalid dimensions
    }

    // Handle "no thumbnail" marker (width=0, height=0)
    BlendThumbnail thumb;
    thumb.width = static_cast<int>(width);
    thumb.height = static_cast<int>(height);

    if (width > 0 && height > 0) {
        // Read pixel data
        thumb.pixels.resize(width * height * 4);
        file.read(reinterpret_cast<char*>(thumb.pixels.data()), thumb.pixels.size());

        if (!file) {
            return std::nullopt;  // Incomplete read
        }
    }

    return thumb;
}

void ThumbnailCache::saveToDiskCache(const std::filesystem::path& blendFile, const BlendThumbnail& thumbnail) {
    std::filesystem::path cachePath = getDiskCachePath(blendFile);

    std::ofstream file(cachePath, std::ios::binary);
    if (!file) {
        return;  // Can't write cache
    }

    // Write magic bytes
    file.write("BFFT", 4);

    // Write version (2 = includes mod time)
    uint32_t version = 2;
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // Write source file modification time for cache invalidation
    int64_t modTimeCount = 0;
    std::error_code ec;
    auto modTime = std::filesystem::last_write_time(blendFile, ec);
    if (!ec) {
        modTimeCount = modTime.time_since_epoch().count();
    }
    file.write(reinterpret_cast<const char*>(&modTimeCount), sizeof(modTimeCount));

    // Write dimensions (0x0 is valid as a "no thumbnail" marker)
    uint32_t width = static_cast<uint32_t>(std::max(0, thumbnail.width));
    uint32_t height = static_cast<uint32_t>(std::max(0, thumbnail.height));
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));

    // Write pixel data (only if we have actual pixels)
    if (width > 0 && height > 0 && !thumbnail.pixels.empty()) {
        file.write(reinterpret_cast<const char*>(thumbnail.pixels.data()), thumbnail.pixels.size());
    }
}

} // namespace BlenderFileFinder
