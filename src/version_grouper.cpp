#include "version_grouper.hpp"
#include "debug.hpp"
#include <algorithm>
#include <map>
#include <regex>

namespace BlenderFileFinder {

bool VersionGrouper::isBackupFile(const std::string& filename) {
    static const std::regex backupPattern(R"(\.blend\d+$)", std::regex::icase);
    return std::regex_search(filename, backupPattern);
}

bool VersionGrouper::hasVersionPattern(const std::string& filename) {
    // Patterns: _v001, _v01, _v1, _001, _01, -v001, etc.
    static const std::regex versionPattern(
        R"([-_]v?\d+\.blend$)",
        std::regex::icase
    );
    return std::regex_search(filename, versionPattern);
}

std::string VersionGrouper::extractBaseName(const std::string& filename) {
    std::string result = filename;

    // Remove .blendN backup extension first
    static const std::regex backupExt(R"(\.blend\d+$)", std::regex::icase);
    result = std::regex_replace(result, backupExt, ".blend");

    // Remove version patterns: _v001, _v01, _v1, _001, -v1, etc.
    static const std::regex versionPattern(
        R"([-_]v?\d+(?=\.blend$))",
        std::regex::icase
    );
    result = std::regex_replace(result, versionPattern, "");

    return result;
}

int VersionGrouper::extractVersionNumber(const std::string& filename) {
    // Try backup pattern first (.blend1, .blend2)
    static const std::regex backupPattern(R"(\.blend(\d+)$)", std::regex::icase);
    std::smatch backupMatch;
    if (std::regex_search(filename, backupMatch, backupPattern)) {
        return std::stoi(backupMatch[1].str());
    }

    // Try version pattern (_v001, _001, etc.)
    static const std::regex versionPattern(R"([-_]v?(\d+)\.blend$)", std::regex::icase);
    std::smatch versionMatch;
    if (std::regex_search(filename, versionMatch, versionPattern)) {
        return std::stoi(versionMatch[1].str());
    }

    return 0; // No version found
}

void VersionGrouper::sortGroup(FileGroup& group) {
    if (group.versions.empty()) {
        return;
    }

    // Sort versions by version number (descending) then by modification time
    auto sortFunc = [](const BlendFileInfo& a, const BlendFileInfo& b) {
        int versionA = extractVersionNumber(a.filename);
        int versionB = extractVersionNumber(b.filename);

        if (versionA != versionB) {
            return versionA > versionB; // Higher version first
        }
        return a.modifiedTime > b.modifiedTime; // Newer first
    };

    std::sort(group.versions.begin(), group.versions.end(), sortFunc);

    // Find the main .blend file (not a backup) if it exists
    auto mainIt = std::find_if(group.versions.begin(), group.versions.end(),
        [](const BlendFileInfo& f) {
            return !isBackupFile(f.filename);
        });

    if (mainIt != group.versions.end()) {
        group.primaryFile = std::move(*mainIt);
        group.versions.erase(mainIt);
    } else {
        group.primaryFile = std::move(group.versions.front());
        group.versions.erase(group.versions.begin());
    }

}

std::vector<FileGroup> VersionGrouper::groupFiles(std::vector<BlendFileInfo>& files) {
    DEBUG_LOG("groupFiles: processing " << files.size() << " files");

    std::map<std::string, FileGroup> groupMap;

    for (auto& file : files) {
        if (file.filename.empty()) {
            continue;
        }

        std::string baseName = extractBaseName(file.filename);

        auto& group = groupMap[baseName];
        if (group.baseName.empty()) {
            group.baseName = baseName;
        }
        group.versions.push_back(std::move(file));
    }

    DEBUG_LOG("Created " << groupMap.size() << " groups");

    std::vector<FileGroup> result;
    result.reserve(groupMap.size());

    for (auto& [name, group] : groupMap) {
        sortGroup(group);
        result.push_back(std::move(group));
    }

    // Sort groups alphabetically by base name
    std::sort(result.begin(), result.end(),
        [](const FileGroup& a, const FileGroup& b) {
            return a.baseName < b.baseName;
        });

    return result;
}

} // namespace BlenderFileFinder
