#include "preview_cache.hpp"
#include "debug.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <unistd.h>

namespace BlenderFileFinder {

PreviewCache::PreviewCache() {
    // Set up cache directory
    const char* home = std::getenv("HOME");
    if (home) {
        m_cacheDir = std::filesystem::path(home) / ".cache" / "BlenderFileFinder" / "previews";
        std::filesystem::create_directories(m_cacheDir);
    }
    DEBUG_LOG("PreviewCache initialized, cache dir: " << m_cacheDir);
}

PreviewCache::~PreviewCache() {
    cancelGeneration();

    // Clean up textures
    for (auto& [path, preview] : m_previews) {
        for (GLuint texId : preview.textureIds) {
            if (texId != 0) {
                glDeleteTextures(1, &texId);
            }
        }
    }
}

std::string PreviewCache::getFileHash(const std::filesystem::path& blendFile) const {
    // Simple hash based on path and modification time
    auto modTime = std::filesystem::last_write_time(blendFile);
    auto modTimeT = modTime.time_since_epoch().count();

    std::stringstream ss;
    ss << std::hex << std::hash<std::string>{}(blendFile.string()) << "_" << modTimeT;
    return ss.str();
}

std::filesystem::path PreviewCache::getPreviewDir(const std::filesystem::path& blendFile) const {
    return m_cacheDir / getFileHash(blendFile);
}

std::filesystem::path PreviewCache::getBlenderScriptPath() const {
    // Look for the script in common locations
    std::vector<std::filesystem::path> searchPaths = {
        std::filesystem::path(__FILE__).parent_path().parent_path() / "resources" / "turntable_render.py",
        "/usr/share/BlenderFileFinder/turntable_render.py",
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".local" / "share" / "BlenderFileFinder" / "turntable_render.py"
    };

    // Also check relative to executable
    char exePath[4096];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
        exePath[len] = '\0';
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        searchPaths.insert(searchPaths.begin(), exeDir / "resources" / "turntable_render.py");
        searchPaths.insert(searchPaths.begin(), exeDir.parent_path() / "resources" / "turntable_render.py");
    }

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return {};
}

bool PreviewCache::hasPreview(const std::filesystem::path& blendFile) const {
    // Check cache first to avoid filesystem operations every frame
    auto cacheIt = m_previewExistsCache.find(blendFile);
    if (cacheIt != m_previewExistsCache.end()) {
        return cacheIt->second;
    }

    // Not in cache, check filesystem
    bool exists = false;
    if (std::filesystem::exists(blendFile)) {
        auto previewDir = getPreviewDir(blendFile);
        if (std::filesystem::exists(previewDir)) {
            exists = std::filesystem::exists(previewDir / "frame_000.png");
        }
    }

    // Cache the result (const_cast needed since this is a const method but we're caching)
    const_cast<PreviewCache*>(this)->m_previewExistsCache[blendFile] = exists;
    return exists;
}

PreviewFrames* PreviewCache::getPreview(const std::filesystem::path& blendFile) {
    auto it = m_previews.find(blendFile);
    if (it != m_previews.end() && it->second.loaded) {
        return &it->second;
    }
    return nullptr;
}

void PreviewCache::loadPreview(const std::filesystem::path& blendFile) {
    if (!hasPreview(blendFile)) return;

    // Check if already loaded or loading
    auto it = m_previews.find(blendFile);
    if (it != m_previews.end()) return;

    // Create placeholder entry
    m_previews[blendFile] = PreviewFrames{};

    // Load frames in background
    std::thread([this, blendFile]() {
        auto previewDir = getPreviewDir(blendFile);

        PendingLoad pending;
        pending.blendFile = blendFile;

        // Load all frame images
        for (int i = 0; i < m_frameCount; ++i) {
            std::stringstream ss;
            ss << "frame_" << std::setfill('0') << std::setw(3) << i << ".png";
            auto framePath = previewDir / ss.str();

            if (!std::filesystem::exists(framePath)) break;

            int width, height, channels;
            unsigned char* data = stbi_load(framePath.string().c_str(), &width, &height, &channels, 4);

            if (data) {
                pending.imageData.emplace_back(data, data + width * height * 4);
                pending.widths.push_back(width);
                pending.heights.push_back(height);
                stbi_image_free(data);
            }
        }

        if (!pending.imageData.empty()) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingLoads.push_back(std::move(pending));
        }
    }).detach();
}

void PreviewCache::processLoadedPreviews() {
    std::vector<PendingLoad> toProcess;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        std::swap(toProcess, m_pendingLoads);
    }

    for (auto& pending : toProcess) {
        auto& preview = m_previews[pending.blendFile];

        for (size_t i = 0; i < pending.imageData.size(); ++i) {
            GLuint texId;
            glGenTextures(1, &texId);
            glBindTexture(GL_TEXTURE_2D, texId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pending.widths[i], pending.heights[i],
                         0, GL_RGBA, GL_UNSIGNED_BYTE, pending.imageData[i].data());

            preview.textureIds.push_back(texId);
        }

        preview.loaded = true;
        DEBUG_LOG("Loaded " << preview.textureIds.size() << " preview frames for " << pending.blendFile.filename());
    }
}

bool PreviewCache::generatePreview(const std::filesystem::path& blendFile) {
    if (!std::filesystem::exists(blendFile)) {
        DEBUG_LOG("Cannot generate preview: file not found: " << blendFile);
        return false;
    }

    auto scriptPath = getBlenderScriptPath();
    if (scriptPath.empty() || !std::filesystem::exists(scriptPath)) {
        DEBUG_LOG("Cannot generate preview: turntable script not found");
        return false;
    }

    auto outputDir = getPreviewDir(blendFile);
    std::filesystem::create_directories(outputDir);

    // Build Blender command - log output for debugging
    auto logFile = outputDir / "render.log";
    std::stringstream cmd;
    cmd << "blender --background --python \"" << scriptPath.string() << "\" -- "
        << "\"" << blendFile.string() << "\" "
        << "\"" << outputDir.string() << "\" "
        << m_frameCount << " "
        << m_resolution
        << " > \"" << logFile.string() << "\" 2>&1";

    DEBUG_LOG("Generating preview: \"" << blendFile.filename() << "\"");

    int result = std::system(cmd.str().c_str());

    // Check for success
    bool success = (result == 0) && std::filesystem::exists(outputDir / "frame_000.png");

    if (!success) {
        // Read and log the error output
        if (std::filesystem::exists(logFile)) {
            std::ifstream log(logFile);
            std::string line;
            int lineCount = 0;
            while (std::getline(log, line) && lineCount < 20) {
                if (!line.empty() && line.find("Read prefs") == std::string::npos) {
                    DEBUG_LOG("  Blender: " << line);
                    lineCount++;
                }
            }
        }
    }

    if (success) {
        DEBUG_LOG("Preview generated successfully for " << blendFile.filename());
        // Update cache
        m_previewExistsCache[blendFile] = true;
        return true;
    } else {
        DEBUG_LOG("Preview generation failed for " << blendFile.filename());
        m_previewExistsCache[blendFile] = false;
        return false;
    }
}

void PreviewCache::startBatchGeneration(const std::vector<std::filesystem::path>& files,
                                        ProgressCallback callback,
                                        bool forceRegenerate) {
    if (m_isGenerating) {
        DEBUG_LOG("Generation already in progress");
        return;
    }

    m_isGenerating = true;
    m_cancelRequested = false;
    m_currentFile = 0;
    m_totalFiles = static_cast<int>(files.size());

    m_generationThread = std::jthread([this, files, callback, forceRegenerate]() {
        for (size_t i = 0; i < files.size() && !m_cancelRequested; ++i) {
            m_currentFile = static_cast<int>(i);

            if (callback) {
                callback(static_cast<int>(i), static_cast<int>(files.size()), files[i].filename().string());
            }

            // Skip if preview already exists (unless forcing regeneration)
            if (forceRegenerate || !hasPreview(files[i])) {
                generatePreview(files[i]);
            }
        }

        m_isGenerating = false;
        m_currentFile = m_totalFiles.load();
        DEBUG_LOG("Batch preview generation complete");
    });
}

std::pair<int, int> PreviewCache::getProgress() const {
    return {m_currentFile.load(), m_totalFiles.load()};
}

void PreviewCache::cancelGeneration() {
    m_cancelRequested = true;
    if (m_generationThread.joinable()) {
        m_generationThread.join();
    }
}

void PreviewCache::clearCache() {
    cancelGeneration();

    // Clear loaded textures
    for (auto& [path, preview] : m_previews) {
        for (GLuint texId : preview.textureIds) {
            if (texId != 0) {
                glDeleteTextures(1, &texId);
            }
        }
    }
    m_previews.clear();
    m_previewExistsCache.clear();

    // Remove cache directory contents
    if (std::filesystem::exists(m_cacheDir)) {
        std::filesystem::remove_all(m_cacheDir);
        std::filesystem::create_directories(m_cacheDir);
    }

    DEBUG_LOG("Preview cache cleared");
}

} // namespace BlenderFileFinder
