/**
 * @file preview_cache.hpp
 * @brief Animated turntable preview generation and caching for .blend files.
 */

#pragma once

#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <GL/gl.h>

namespace BlenderFileFinder {

/**
 * @brief Container for animated preview frame textures.
 *
 * Stores a sequence of OpenGL textures representing a turntable
 * animation of a .blend file's content.
 */
struct PreviewFrames {
    std::vector<GLuint> textureIds;  ///< OpenGL texture IDs for each frame
    int currentFrame = 0;             ///< Current animation frame (for playback)
    bool loaded = false;              ///< True when textures are uploaded to GPU
};

/**
 * @brief Cache for animated turntable previews of .blend files.
 *
 * Generates and caches animated previews by rendering .blend files
 * from multiple angles using Blender in background mode. Previews
 * are stored on disk and loaded into OpenGL textures on demand.
 *
 * The generation process:
 * 1. Runs Blender headless with a Python script (turntable_render.py)
 * 2. Renders N frames (default 24) rotating around the scene
 * 3. Saves frames as PNG files in ~/.cache/BlenderFileFinder/previews/
 * 4. Loads frames into OpenGL textures when requested
 *
 * @note Preview generation requires Blender to be installed and
 *       accessible via the 'blender' command.
 */
class PreviewCache {
public:
    /**
     * @brief Callback for batch generation progress.
     * @param current Current file index (0-based)
     * @param total Total number of files
     * @param filename Name of the file being processed
     */
    using ProgressCallback = std::function<void(int current, int total, const std::string& filename)>;

    PreviewCache();
    ~PreviewCache();

    /**
     * @brief Check if a preview exists on disk for a file.
     *
     * Results are cached to avoid repeated filesystem checks.
     *
     * @param blendFile Path to the .blend file
     * @return true if preview frames exist
     */
    bool hasPreview(const std::filesystem::path& blendFile) const;

    /**
     * @brief Get loaded preview frames for a file.
     * @param blendFile Path to the .blend file
     * @return Pointer to frames if loaded, nullptr otherwise
     */
    PreviewFrames* getPreview(const std::filesystem::path& blendFile);

    /**
     * @brief Start loading preview frames from disk.
     *
     * Loads frames asynchronously. Call processLoadedPreviews() each
     * frame to upload completed loads to GPU textures.
     *
     * @param blendFile Path to the .blend file
     */
    void loadPreview(const std::filesystem::path& blendFile);

    /**
     * @brief Generate a preview for a single file (blocking).
     *
     * Runs Blender to render the turntable animation. This can take
     * several seconds per file.
     *
     * @param blendFile Path to the .blend file
     * @return true if generation succeeded
     */
    bool generatePreview(const std::filesystem::path& blendFile);

    /**
     * @brief Start background generation for multiple files.
     * @param files List of .blend files to generate previews for
     * @param callback Optional progress callback
     */
    void startBatchGeneration(const std::vector<std::filesystem::path>& files,
                              ProgressCallback callback = nullptr);

    /**
     * @brief Check if batch generation is in progress.
     * @return true if generating
     */
    bool isGenerating() const { return m_isGenerating; }

    /**
     * @brief Get batch generation progress.
     * @return Pair of (current file index, total files)
     */
    std::pair<int, int> getProgress() const;

    /**
     * @brief Cancel ongoing batch generation.
     */
    void cancelGeneration();

    /**
     * @brief Upload loaded preview images to GPU textures.
     *
     * Must be called from main thread (with OpenGL context) each frame.
     */
    void processLoadedPreviews();

    /// @name Settings
    /// @{

    /**
     * @brief Set number of frames in generated previews.
     * @param count Number of frames (default: 24)
     */
    void setFrameCount(int count) { m_frameCount = count; }

    /**
     * @brief Get number of frames in generated previews.
     * @return Frame count
     */
    int getFrameCount() const { return m_frameCount; }

    /**
     * @brief Set resolution of generated preview frames.
     * @param res Resolution in pixels (square, default: 128)
     */
    void setResolution(int res) { m_resolution = res; }

    /**
     * @brief Get resolution of generated preview frames.
     * @return Resolution in pixels
     */
    int getResolution() const { return m_resolution; }

    /// @}

    /**
     * @brief Get the cache directory path.
     * @return Path to ~/.cache/BlenderFileFinder/previews/
     */
    std::filesystem::path getCacheDir() const { return m_cacheDir; }

    /**
     * @brief Clear all cached previews from disk and memory.
     */
    void clearCache();

private:
    std::filesystem::path getPreviewDir(const std::filesystem::path& blendFile) const;
    std::string getFileHash(const std::filesystem::path& blendFile) const;
    std::filesystem::path getBlenderScriptPath() const;
    void loadPreviewFrames(const std::filesystem::path& blendFile, PreviewFrames& preview);

    std::filesystem::path m_cacheDir;       ///< Preview cache directory
    int m_frameCount = 24;                  ///< Frames per preview animation
    int m_resolution = 128;                 ///< Frame resolution (pixels)

    std::map<std::filesystem::path, PreviewFrames> m_previews;  ///< Loaded previews
    mutable std::map<std::filesystem::path, bool> m_previewExistsCache; ///< hasPreview cache

    /// @name Background Generation
    /// @{
    std::atomic<bool> m_isGenerating{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<int> m_currentFile{0};
    std::atomic<int> m_totalFiles{0};
    std::jthread m_generationThread;
    /// @}

    /**
     * @brief Pending preview load awaiting GPU upload.
     */
    struct PendingLoad {
        std::filesystem::path blendFile;                    ///< Source file
        std::vector<std::vector<unsigned char>> imageData;  ///< Frame pixel data
        std::vector<int> widths;                            ///< Frame widths
        std::vector<int> heights;                           ///< Frame heights
    };

    std::mutex m_pendingMutex;              ///< Protects pending loads
    std::vector<PendingLoad> m_pendingLoads; ///< Frames awaiting GPU upload
};

} // namespace BlenderFileFinder
