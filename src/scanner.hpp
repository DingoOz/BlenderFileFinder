/**
 * @file scanner.hpp
 * @brief Background directory scanner for finding .blend files.
 */

#pragma once

#include "blend_parser.hpp"
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace BlenderFileFinder {

/**
 * @brief Asynchronous directory scanner for .blend files.
 *
 * Scans directories in a background thread to find Blender files,
 * parsing each one to extract thumbnails and metadata. Provides
 * progress reporting and thread-safe result polling.
 *
 * @par Usage Example:
 * @code
 * Scanner scanner;
 * scanner.setProgressCallback([](int done, int total) {
 *     std::cout << "Progress: " << done << "/" << total << std::endl;
 * });
 * scanner.startScan("/path/to/blender/projects", true);
 *
 * while (!scanner.isComplete()) {
 *     auto results = scanner.pollResults();
 *     // Process results incrementally...
 * }
 * @endcode
 *
 * @note Only one scan can run at a time. Starting a new scan while
 *       one is in progress will be ignored.
 */
class Scanner {
public:
    /**
     * @brief Callback type for progress updates.
     * @param scanned Number of files scanned so far
     * @param total Total number of .blend files found
     */
    using ProgressCallback = std::function<void(int scanned, int total)>;

    /**
     * @brief Callback type for scan completion.
     * @param results Vector of all parsed file information
     */
    using CompleteCallback = std::function<void(std::vector<BlendFileInfo>)>;

    Scanner();
    ~Scanner();

    /**
     * @brief Start scanning a directory for .blend files.
     *
     * Launches a background thread that recursively (if specified)
     * searches for .blend files and parses each one.
     *
     * @param directory Root directory to scan
     * @param recursive If true, scan subdirectories as well
     */
    void startScan(const std::filesystem::path& directory, bool recursive = true);

    /**
     * @brief Request the current scan to stop.
     *
     * The scan will stop at the next opportunity. Already parsed
     * results will still be available via pollResults().
     */
    void stopScan();

    /**
     * @brief Check if a scan is currently in progress.
     * @return true if scanning, false otherwise
     */
    bool isScanning() const { return m_isScanning.load(); }

    /**
     * @brief Get the current scan progress.
     * @return Pair of (files scanned, total files found)
     */
    std::pair<int, int> getProgress() const;

    /**
     * @brief Poll for newly parsed files (thread-safe).
     *
     * Returns and clears any results that have been parsed since
     * the last call. Can be called from the main thread while
     * scanning continues in the background.
     *
     * @return Vector of newly parsed file information
     */
    std::vector<BlendFileInfo> pollResults();

    /**
     * @brief Check if the scan has completed.
     * @return true if scan finished or was stopped
     */
    bool isComplete() const { return m_isComplete.load(); }

    /**
     * @brief Set callback for progress updates.
     * @param callback Function to call with progress updates
     */
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = std::move(callback); }

    /**
     * @brief Set callback for scan completion.
     * @param callback Function to call when scan completes
     */
    void setCompleteCallback(CompleteCallback callback) { m_completeCallback = std::move(callback); }

private:
    void scanThread(std::filesystem::path directory, bool recursive);
    bool isBlendFile(const std::filesystem::path& path) const;

    std::jthread m_scanThread;              ///< Background scanning thread
    std::atomic<bool> m_isScanning{false};  ///< Scan in progress flag
    std::atomic<bool> m_stopRequested{false}; ///< Stop request flag
    std::atomic<bool> m_isComplete{false};  ///< Scan complete flag

    std::atomic<int> m_filesScanned{0};     ///< Number of files parsed
    std::atomic<int> m_filesTotal{0};       ///< Total .blend files found

    std::mutex m_resultsMutex;              ///< Protects m_results
    std::vector<BlendFileInfo> m_results;   ///< Parsed file results

    ProgressCallback m_progressCallback;    ///< Progress callback
    CompleteCallback m_completeCallback;    ///< Completion callback
};

} // namespace BlenderFileFinder
