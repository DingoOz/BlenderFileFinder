#include "scan_cache.hpp"
#include "debug.hpp"
#include <fstream>
#include <sstream>
#include <functional>
#include <cstdlib>

namespace BlenderFileFinder {

ScanCache::ScanCache() {
    // Use ~/.cache/BlenderFileFinder/
    const char* home = std::getenv("HOME");
    if (home) {
        m_cacheDir = std::filesystem::path(home) / ".cache" / "BlenderFileFinder";
    } else {
        m_cacheDir = "/tmp/BlenderFileFinder";
    }

    // Create cache directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(m_cacheDir, ec);
}

std::filesystem::path ScanCache::getCacheDir() const {
    return m_cacheDir;
}

std::string ScanCache::hashPath(const std::filesystem::path& path) const {
    // Simple hash of the path string
    std::hash<std::string> hasher;
    size_t hash = hasher(path.string());

    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

std::filesystem::path ScanCache::getCacheFilePath(const std::filesystem::path& directory) const {
    return m_cacheDir / (hashPath(directory) + ".cache");
}

bool ScanCache::isEntryValid(const BlendFileInfo& info) const {
    if (!std::filesystem::exists(info.path)) {
        return false;
    }

    std::error_code ec;
    auto currentModTime = std::filesystem::last_write_time(info.path, ec);
    if (ec) {
        return false;
    }

    // Check if modification time matches
    return currentModTime == info.modifiedTime;
}

void ScanCache::save(const std::filesystem::path& directory, const std::vector<BlendFileInfo>& files) {
    std::filesystem::path cacheFile = getCacheFilePath(directory);

    std::ofstream out(cacheFile, std::ios::binary);
    if (!out) {
        DEBUG_LOG("Failed to open cache file for writing: " << cacheFile);
        return;
    }

    // Write header
    out << "BFMCACHE1\n";  // Version identifier
    out << directory.string() << "\n";
    out << files.size() << "\n";

    // Write each file entry
    for (const auto& file : files) {
        out << file.path.string() << "\n";
        out << file.filename << "\n";
        out << file.fileSize << "\n";

        // Store modification time as duration since epoch
        auto duration = file.modifiedTime.time_since_epoch();
        out << duration.count() << "\n";

        // Metadata
        out << file.metadata.blenderVersion << "\n";
        out << file.metadata.isCompressed << "\n";
        out << file.metadata.objectCount << "\n";
        out << file.metadata.meshCount << "\n";
        out << file.metadata.materialCount << "\n";

        // Has thumbnail flag (we don't cache actual thumbnails)
        out << (file.thumbnail.has_value() ? 1 : 0) << "\n";
    }

    DEBUG_LOG("Saved cache for " << directory << " (" << files.size() << " files)");
}

std::vector<BlendFileInfo> ScanCache::load(const std::filesystem::path& directory) {
    std::vector<BlendFileInfo> result;
    std::filesystem::path cacheFile = getCacheFilePath(directory);

    if (!std::filesystem::exists(cacheFile)) {
        return result;
    }

    std::ifstream in(cacheFile);
    if (!in) {
        return result;
    }

    std::string line;

    // Check header
    std::getline(in, line);
    if (line != "BFMCACHE1") {
        DEBUG_LOG("Invalid cache file format");
        return result;
    }

    // Read cached directory path
    std::getline(in, line);
    if (line != directory.string()) {
        DEBUG_LOG("Cache directory mismatch");
        return result;
    }

    // Read file count
    std::getline(in, line);
    size_t count = std::stoull(line);

    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        BlendFileInfo info;

        // Path
        std::getline(in, line);
        info.path = line;

        // Filename
        std::getline(in, line);
        info.filename = line;

        // File size
        std::getline(in, line);
        info.fileSize = std::stoull(line);

        // Modification time
        std::getline(in, line);
        auto duration = std::filesystem::file_time_type::duration(std::stoll(line));
        info.modifiedTime = std::filesystem::file_time_type(duration);

        // Metadata
        std::getline(in, line);
        info.metadata.blenderVersion = line;

        std::getline(in, line);
        info.metadata.isCompressed = (line == "1");

        std::getline(in, line);
        info.metadata.objectCount = std::stoi(line);

        std::getline(in, line);
        info.metadata.meshCount = std::stoi(line);

        std::getline(in, line);
        info.metadata.materialCount = std::stoi(line);

        // Has thumbnail (skip, we reload thumbnails separately)
        std::getline(in, line);

        // Validate entry is still current
        if (isEntryValid(info)) {
            result.push_back(std::move(info));
        }
    }

    DEBUG_LOG("Loaded cache for " << directory << " (" << result.size() << "/" << count << " valid files)");
    return result;
}

bool ScanCache::hasValidCache(const std::filesystem::path& directory) {
    std::filesystem::path cacheFile = getCacheFilePath(directory);
    return std::filesystem::exists(cacheFile);
}

void ScanCache::clearAll() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(m_cacheDir, ec)) {
        if (entry.path().extension() == ".cache") {
            std::filesystem::remove(entry.path(), ec);
        }
    }
    DEBUG_LOG("Cleared all cache files");
}

} // namespace BlenderFileFinder
