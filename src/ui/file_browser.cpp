#include "file_browser.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <regex>

namespace BlenderFileFinder {

FileBrowser::FileBrowser() {
    m_currentPath = std::filesystem::current_path();
    refreshDirectoryList();
    refreshNetworkMounts();
}

void FileBrowser::setCurrentPath(const std::filesystem::path& path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        m_currentPath = std::filesystem::canonical(path);
        strncpy(m_pathBuffer, m_currentPath.string().c_str(), sizeof(m_pathBuffer) - 1);
        m_pathBuffer[sizeof(m_pathBuffer) - 1] = '\0';  // Ensure null-termination
        refreshDirectoryList();
    }
}

void FileBrowser::navigateUp() {
    if (m_currentPath.has_parent_path() && m_currentPath != m_currentPath.root_path()) {
        setCurrentPath(m_currentPath.parent_path());
    }
}

void FileBrowser::refreshDirectoryList() {
    m_directoryEntries.clear();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_currentPath)) {
            if (entry.is_directory()) {
                m_directoryEntries.push_back(entry);
            }
        }
        sortDirectoryList();
    } catch (const std::filesystem::filesystem_error&) {
        // Permission denied or other error
    }
}

void FileBrowser::sortDirectoryList() {
    if (m_sortMode == SortMode::Name) {
        std::sort(m_directoryEntries.begin(), m_directoryEntries.end(),
            [this](const auto& a, const auto& b) {
                std::string nameA = a.path().filename().string();
                std::string nameB = b.path().filename().string();
                // Case-insensitive comparison
                std::transform(nameA.begin(), nameA.end(), nameA.begin(), ::tolower);
                std::transform(nameB.begin(), nameB.end(), nameB.begin(), ::tolower);
                return m_sortAscending ? (nameA < nameB) : (nameA > nameB);
            });
    } else {
        // Sort by date (last modified time)
        std::sort(m_directoryEntries.begin(), m_directoryEntries.end(),
            [this](const auto& a, const auto& b) {
                try {
                    auto timeA = a.last_write_time();
                    auto timeB = b.last_write_time();
                    return m_sortAscending ? (timeA < timeB) : (timeA > timeB);
                } catch (...) {
                    return false;
                }
            });
    }
}

void FileBrowser::addRecentPath(const std::filesystem::path& path) {
    // Remove if already exists
    auto it = std::find(m_recentPaths.begin(), m_recentPaths.end(), path);
    if (it != m_recentPaths.end()) {
        m_recentPaths.erase(it);
    }

    // Add to front
    m_recentPaths.insert(m_recentPaths.begin(), path);

    // Keep only last 10
    if (m_recentPaths.size() > 10) {
        m_recentPaths.resize(10);
    }
}

void FileBrowser::render() {
    // Track frame count for periodic refresh
    static int frameCount = 0;
    frameCount++;

    // Refresh network mounts periodically (every ~5 seconds)
    if (frameCount - m_networkRefreshFrame > 300) {
        refreshNetworkMounts();
        m_networkRefreshFrame = frameCount;
    }

    // Navigation buttons
    if (ImGui::Button("^ Up")) {
        navigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        const char* home = std::getenv("HOME");
        if (home) {
            setCurrentPath(home);
        }
    }

    // Sort buttons
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "|");
    ImGui::SameLine();

    // Name sort button - shows arrow if active
    bool isNameSort = (m_sortMode == SortMode::Name);
    const char* nameLabel = isNameSort ? (m_sortAscending ? "Name ^" : "Name v") : "Name";
    if (isNameSort) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.4f, 0.5f, 1.0f));
    }
    if (ImGui::Button(nameLabel)) {
        if (m_sortMode == SortMode::Name) {
            m_sortAscending = !m_sortAscending;  // Toggle direction
        } else {
            m_sortMode = SortMode::Name;
            m_sortAscending = true;
        }
        sortDirectoryList();
    }
    if (isNameSort) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sort by name%s", isNameSort ? " (click to reverse)" : "");
    }

    ImGui::SameLine();

    // Date sort button - shows arrow if active
    bool isDateSort = (m_sortMode == SortMode::Date);
    const char* dateLabel = isDateSort ? (m_sortAscending ? "Date ^" : "Date v") : "Date";
    if (isDateSort) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.4f, 0.5f, 1.0f));
    }
    if (ImGui::Button(dateLabel)) {
        if (m_sortMode == SortMode::Date) {
            m_sortAscending = !m_sortAscending;  // Toggle direction
        } else {
            m_sortMode = SortMode::Date;
            m_sortAscending = false;  // Default to newest first for date
        }
        sortDirectoryList();
    }
    if (isDateSort) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sort by date modified%s", isDateSort ? " (click to reverse)" : "");
    }

    // Current path display (read-only, shows where we are)
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", m_currentPath.string().c_str());

    ImGui::Separator();

    // Calculate available height
    float availHeight = ImGui::GetContentRegionAvail().y - 60; // Reserve space for add button
    if (availHeight < 100) availHeight = 100;

    // Store path to navigate to (can't modify lists while iterating)
    std::filesystem::path pathToNavigate;

    ImGui::BeginChild("BrowseArea", ImVec2(0, availHeight), true);

    // Network Locations section (collapsible)
    if (!m_networkMounts.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.3f, 0.4f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.4f, 0.5f, 0.8f));

        bool networkOpen = ImGui::CollapsingHeader("Network Locations",
            ImGuiTreeNodeFlags_DefaultOpen);

        ImGui::PopStyleColor(2);

        if (networkOpen) {
            ImGui::Indent(8.0f);

            for (const auto& mount : m_networkMounts) {
                ImGui::PushID(mount.path.string().c_str());

                // Network icon style with colored text
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
                bool clicked = ImGui::Selectable(mount.displayName.c_str(), false,
                    ImGuiSelectableFlags_AllowDoubleClick);
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", mount.path.string().c_str());
                }

                if (clicked) {
                    pathToNavigate = mount.path;
                }

                ImGui::PopID();
            }

            ImGui::Unindent(8.0f);
            ImGui::Spacing();
        }
    }

    // Local Directories section
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.25f, 0.25f, 0.25f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.35f, 0.35f, 0.35f, 0.8f));

    bool localOpen = ImGui::CollapsingHeader("Local Directories",
        ImGuiTreeNodeFlags_DefaultOpen);

    ImGui::PopStyleColor(2);

    if (localOpen) {
        ImGui::Indent(8.0f);

        for (const auto& entry : m_directoryEntries) {
            std::string name = entry.path().filename().string();

            // Skip hidden directories
            if (!name.empty() && name[0] == '.') {
                continue;
            }

            // Show folder
            bool clicked = ImGui::Selectable(name.c_str(), false,
                ImGuiSelectableFlags_AllowDoubleClick);

            if (clicked) {
                pathToNavigate = entry.path();
            }
        }

        ImGui::Unindent(8.0f);
    }

    ImGui::EndChild();

    // Navigate after loop to avoid iterator invalidation
    if (!pathToNavigate.empty()) {
        setCurrentPath(pathToNavigate);
    }
}

NetworkMount FileBrowser::parseGvfsMount(const std::filesystem::path& mountPath) const {
    NetworkMount mount;
    mount.path = mountPath;

    std::string name = mountPath.filename().string();

    // Parse GVFS mount name formats:
    // smb-share:server=hostname,share=sharename
    // sftp:host=hostname
    // nfs:server=hostname,share=path
    // dav:host=hostname,ssl=true

    // Detect protocol
    size_t colonPos = name.find(':');
    if (colonPos != std::string::npos) {
        mount.protocol = name.substr(0, colonPos);
    }

    // Parse key=value pairs
    std::regex kvRegex("([a-z]+)=([^,]+)");
    std::sregex_iterator it(name.begin(), name.end(), kvRegex);
    std::sregex_iterator end;

    for (; it != end; ++it) {
        std::smatch match = *it;
        std::string key = match[1].str();
        std::string value = match[2].str();

        if (key == "server" || key == "host") {
            mount.server = value;
        } else if (key == "share") {
            mount.share = value;
        }
    }

    // Build display name
    if (!mount.server.empty()) {
        // Remove .local suffix for cleaner display
        std::string serverDisplay = mount.server;
        if (serverDisplay.length() > 6 &&
            serverDisplay.substr(serverDisplay.length() - 6) == ".local") {
            serverDisplay = serverDisplay.substr(0, serverDisplay.length() - 6);
        }

        if (!mount.share.empty()) {
            mount.displayName = serverDisplay + "/" + mount.share;
        } else {
            mount.displayName = serverDisplay;
        }

        // Add protocol prefix for non-SMB
        if (!mount.protocol.empty() && mount.protocol != "smb-share") {
            mount.displayName = "[" + mount.protocol + "] " + mount.displayName;
        }
    } else {
        // Fallback to folder name
        mount.displayName = name;
    }

    return mount;
}

void FileBrowser::refreshNetworkMounts() {
    m_networkMounts.clear();

    // Get current user ID for GVFS path
    uid_t uid = getuid();
    std::filesystem::path gvfsPath = "/run/user/" + std::to_string(uid) + "/gvfs";

    // Check GVFS mounts (most common for desktop users)
    if (std::filesystem::exists(gvfsPath)) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(gvfsPath)) {
                if (entry.is_directory()) {
                    NetworkMount mount = parseGvfsMount(entry.path());
                    m_networkMounts.push_back(mount);
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Permission denied or other error
        }
    }

    // Check /mnt for manually mounted shares
    std::filesystem::path mntPath = "/mnt";
    if (std::filesystem::exists(mntPath)) {
        try {
            for (const auto& entry : std::filesystem::directory_iterator(mntPath)) {
                if (entry.is_directory()) {
                    NetworkMount mount;
                    mount.path = entry.path();
                    mount.displayName = "[mnt] " + entry.path().filename().string();
                    mount.protocol = "mount";
                    m_networkMounts.push_back(mount);
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Permission denied
        }
    }

    // Check /media/<user> for removable/network mounts
    const char* username = std::getenv("USER");
    if (username) {
        std::filesystem::path mediaPath = std::filesystem::path("/media") / username;
        if (std::filesystem::exists(mediaPath)) {
            try {
                for (const auto& entry : std::filesystem::directory_iterator(mediaPath)) {
                    if (entry.is_directory()) {
                        NetworkMount mount;
                        mount.path = entry.path();
                        mount.displayName = "[media] " + entry.path().filename().string();
                        mount.protocol = "media";
                        m_networkMounts.push_back(mount);
                    }
                }
            } catch (const std::filesystem::filesystem_error&) {
                // Permission denied
            }
        }
    }

    // Sort by display name
    std::sort(m_networkMounts.begin(), m_networkMounts.end(),
              [](const NetworkMount& a, const NetworkMount& b) {
                  return a.displayName < b.displayName;
              });
}

} // namespace BlenderFileFinder
