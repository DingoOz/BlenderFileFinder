#include "tag_manager.hpp"
#include "debug.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace BlenderFileFinder {

TagManager::TagManager() {
    const char* home = std::getenv("HOME");
    if (home) {
        m_dataDir = std::filesystem::path(home) / ".cache" / "BlenderFileFinder";
    } else {
        m_dataDir = "/tmp/BlenderFileFinder";
    }

    std::error_code ec;
    std::filesystem::create_directories(m_dataDir, ec);

    load();
}

TagManager::~TagManager() {
    if (m_dirty) {
        save();
    }
}

std::filesystem::path TagManager::getTagFilePath() const {
    return m_dataDir / "tags.dat";
}

void TagManager::addTag(const std::filesystem::path& file, const std::string& tag) {
    if (tag.empty()) return;

    std::string key = file.string();
    m_fileTags[key].insert(tag);
    m_allTags.insert(tag);
    m_dirty = true;
}

void TagManager::removeTag(const std::filesystem::path& file, const std::string& tag) {
    std::string key = file.string();
    auto it = m_fileTags.find(key);
    if (it != m_fileTags.end()) {
        it->second.erase(tag);
        if (it->second.empty()) {
            m_fileTags.erase(it);
        }
        m_dirty = true;
    }
}

std::vector<std::string> TagManager::getTags(const std::filesystem::path& file) const {
    std::string key = file.string();
    auto it = m_fileTags.find(key);
    if (it != m_fileTags.end()) {
        return std::vector<std::string>(it->second.begin(), it->second.end());
    }
    return {};
}

bool TagManager::hasTag(const std::filesystem::path& file, const std::string& tag) const {
    std::string key = file.string();
    auto it = m_fileTags.find(key);
    if (it != m_fileTags.end()) {
        return it->second.count(tag) > 0;
    }
    return false;
}

std::vector<std::string> TagManager::getAllTags() const {
    return std::vector<std::string>(m_allTags.begin(), m_allTags.end());
}

std::vector<std::filesystem::path> TagManager::getFilesWithTag(const std::string& tag) const {
    std::vector<std::filesystem::path> result;
    for (const auto& [path, tags] : m_fileTags) {
        if (tags.count(tag) > 0) {
            result.push_back(path);
        }
    }
    return result;
}

void TagManager::save() {
    std::filesystem::path tagFile = getTagFilePath();

    std::ofstream out(tagFile);
    if (!out) {
        DEBUG_LOG("Failed to save tags to " << tagFile);
        return;
    }

    // Write version header
    out << "TAGS1\n";

    // Write all known tags first
    out << m_allTags.size() << "\n";
    for (const auto& tag : m_allTags) {
        out << tag << "\n";
    }

    // Write file-tag mappings
    out << m_fileTags.size() << "\n";
    for (const auto& [path, tags] : m_fileTags) {
        out << path << "\n";
        out << tags.size() << "\n";
        for (const auto& tag : tags) {
            out << tag << "\n";
        }
    }

    m_dirty = false;
    DEBUG_LOG("Saved tags: " << m_allTags.size() << " tags, " << m_fileTags.size() << " files");
}

void TagManager::load() {
    std::filesystem::path tagFile = getTagFilePath();

    if (!std::filesystem::exists(tagFile)) {
        return;
    }

    std::ifstream in(tagFile);
    if (!in) {
        return;
    }

    std::string line;

    // Check version
    std::getline(in, line);
    if (line != "TAGS1") {
        DEBUG_LOG("Invalid tag file format");
        return;
    }

    // Read all known tags
    std::getline(in, line);
    size_t tagCount = std::stoull(line);
    for (size_t i = 0; i < tagCount; ++i) {
        std::getline(in, line);
        m_allTags.insert(line);
    }

    // Read file-tag mappings
    std::getline(in, line);
    size_t fileCount = std::stoull(line);
    for (size_t i = 0; i < fileCount; ++i) {
        std::string path;
        std::getline(in, path);

        std::getline(in, line);
        size_t numTags = std::stoull(line);

        std::set<std::string> tags;
        for (size_t j = 0; j < numTags; ++j) {
            std::getline(in, line);
            tags.insert(line);
        }

        if (!tags.empty()) {
            m_fileTags[path] = tags;
        }
    }

    DEBUG_LOG("Loaded tags: " << m_allTags.size() << " tags, " << m_fileTags.size() << " files");
}

} // namespace BlenderFileFinder
