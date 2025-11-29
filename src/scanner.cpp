#include "scanner.hpp"
#include "debug.hpp"
#include <algorithm>
#include <regex>

namespace BlenderFileFinder {

Scanner::Scanner() = default;

Scanner::~Scanner() {
    stopScan();
}

void Scanner::startScan(const std::filesystem::path& directory, bool recursive) {
    stopScan();

    m_isScanning = true;
    m_stopRequested = false;
    m_isComplete = false;
    m_filesScanned = 0;
    m_filesTotal = 0;

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.clear();
    }

    m_scanThread = std::jthread([this, directory, recursive]() {
        scanThread(directory, recursive);
    });
}

void Scanner::stopScan() {
    m_stopRequested = true;
    if (m_scanThread.joinable()) {
        m_scanThread.join();
    }
    m_isScanning = false;
}

std::pair<int, int> Scanner::getProgress() const {
    return {m_filesScanned.load(), m_filesTotal.load()};
}

std::vector<BlendFileInfo> Scanner::pollResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

bool Scanner::isBlendFile(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Match .blend or .blend1, .blend2, etc.
    if (ext == ".blend") return true;

    // Check for backup files (.blend1, .blend2, etc.)
    static const std::regex backupPattern(R"(\.blend\d+$)", std::regex::icase);
    return std::regex_match(ext, backupPattern);
}

void Scanner::scanThread(std::filesystem::path directory, bool recursive) {
    DEBUG_LOG("scanThread starting: " << directory.string() << " recursive=" << recursive);

    std::vector<std::filesystem::path> blendFiles;

    // First pass: collect all blend files
    try {
        auto options = std::filesystem::directory_options::skip_permission_denied;

        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, options)) {
                if (m_stopRequested) return;

                if (entry.is_regular_file() && isBlendFile(entry.path())) {
                    blendFiles.push_back(entry.path());
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(directory, options)) {
                if (m_stopRequested) return;

                if (entry.is_regular_file() && isBlendFile(entry.path())) {
                    blendFiles.push_back(entry.path());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        DEBUG_LOG("Filesystem error: " << e.what());
    }

    DEBUG_LOG("Found " << blendFiles.size() << " blend files");
    m_filesTotal = static_cast<int>(blendFiles.size());

    // Second pass: parse each file
    std::vector<BlendFileInfo> results;
    results.reserve(blendFiles.size());

    for (const auto& path : blendFiles) {
        if (m_stopRequested) break;

        auto info = BlendParser::parseQuick(path);
        if (info) {
            results.push_back(std::move(*info));
        } else {
            // Still add basic file info even if parsing failed
            BlendFileInfo basicInfo;
            basicInfo.path = path;
            basicInfo.filename = path.filename().string();
            std::error_code ec;
            basicInfo.fileSize = std::filesystem::file_size(path, ec);
            basicInfo.modifiedTime = std::filesystem::last_write_time(path, ec);
            results.push_back(std::move(basicInfo));
        }

        ++m_filesScanned;

        if (m_progressCallback) {
            m_progressCallback(m_filesScanned.load(), m_filesTotal.load());
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results = std::move(results);
    }

    m_isComplete = true;
    m_isScanning = false;

    if (m_completeCallback) {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_completeCallback(m_results);
    }
}

} // namespace BlenderFileFinder
