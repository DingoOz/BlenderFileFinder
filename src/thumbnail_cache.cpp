#include "thumbnail_cache.hpp"
#include "debug.hpp"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <cstring>
#include <chrono>

namespace BlenderFileFinder {

ThumbnailCache::ThumbnailCache(size_t maxCacheSize)
    : m_maxCacheSize(maxCacheSize) {
    DEBUG_LOG("ThumbnailCache constructor, maxSize=" << maxCacheSize);
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
    static int reqCallCount = 0;
    bool logThis = (reqCallCount < 5);
    reqCallCount++;

    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] start");
    std::string key = path.string();
    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] got key, acquiring mutex...");

    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] mutex acquired");

    // Skip if already in cache or loading
    if (m_cacheMap.count(key) || m_loadingSet.count(key)) {
        if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] already loading, skip");
        return;
    }

    // Log first few thumbnail requests
    static int requestCount = 0;
    if (requestCount < 3) {
        DEBUG_LOG("Thumbnail requested: " << path.filename());
        requestCount++;
    }

    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] adding to loadingSet...");
    m_loadingSet[key] = true;
    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] pushing to queue...");
    m_loadQueue.push(path);
    if (logThis) DEBUG_LOG("requestThumbnail[" << reqCallCount << "] done");
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

        // Parse file and extract thumbnail - this runs in background thread
        auto parseStart = std::chrono::steady_clock::now();
        auto info = BlendParser::parseQuick(pathToLoad);
        auto parseMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - parseStart).count();
        // Log all parses that take > 50ms (could indicate I/O issues)
        if (parseMs > 50) {
            DEBUG_LOG("Slow parseQuick: " << pathToLoad.filename() << " took " << parseMs << "ms (thread)");
        }
        if (info && info->thumbnail) {
            LoadRequest request;
            request.path = pathToLoad;
            request.thumbnail = std::move(*info->thumbnail);

            std::lock_guard<std::mutex> lock(m_loadedMutex);
            m_loadedQueue.push(std::move(request));
        } else {
            // Mark as done even if no thumbnail
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_loadingSet.erase(pathToLoad.string());
        }
    }
}

void ThumbnailCache::processLoadedThumbnails() {
    std::lock_guard<std::mutex> lock(m_loadedMutex);

    int processedCount = 0;
    auto processStart = std::chrono::steady_clock::now();

    while (!m_loadedQueue.empty()) {
        auto& request = m_loadedQueue.front();
        std::string key = request.path.string();

        // Create texture on main thread (OpenGL context)
        auto texStart = std::chrono::steady_clock::now();
        uint32_t textureId = createTexture(request.thumbnail);
        auto texMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - texStart).count();
        if (texMs > 10) {
            DEBUG_LOG("Slow texture creation: " << request.path.filename() << " took " << texMs << "ms");
        }
        processedCount++;

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

        // Remove from loading set
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            m_loadingSet.erase(key);
        }

        m_loadedQueue.pop();
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
    glDeleteTextures(1, &oldest.textureId);
    m_cacheMap.erase(oldest.path.string());
    m_cacheList.pop_back();
}

void ThumbnailCache::clear() {
    for (auto& entry : m_cacheList) {
        glDeleteTextures(1, &entry.textureId);
    }
    m_cacheList.clear();
    m_cacheMap.clear();

    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_loadQueue.empty()) {
        m_loadQueue.pop();
    }
    m_loadingSet.clear();
}

} // namespace BlenderFileFinder
