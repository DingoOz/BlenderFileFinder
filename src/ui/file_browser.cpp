#include "file_browser.hpp"
#include "imgui.h"
#include <algorithm>
#include <cstring>

namespace BlenderFileFinder {

FileBrowser::FileBrowser() {
    m_currentPath = std::filesystem::current_path();
    refreshDirectoryList();
}

void FileBrowser::setCurrentPath(const std::filesystem::path& path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        m_currentPath = std::filesystem::canonical(path);
        strncpy(m_pathBuffer, m_currentPath.string().c_str(), sizeof(m_pathBuffer) - 1);
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

    // Directory list - show available height
    float availHeight = ImGui::GetContentRegionAvail().y - 60; // Reserve space for add button
    if (availHeight < 100) availHeight = 100;

    ImGui::BeginChild("DirList", ImVec2(0, availHeight), true);

    // Store path to navigate to (can't modify m_directoryEntries while iterating)
    std::filesystem::path pathToNavigate;

    for (const auto& entry : m_directoryEntries) {
        std::string name = entry.path().filename().string();

        // Skip hidden directories
        if (!name.empty() && name[0] == '.') {
            continue;
        }

        // Show folder icon style
        bool clicked = ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
        if (clicked && ImGui::IsMouseDoubleClicked(0)) {
            pathToNavigate = entry.path();
        } else if (clicked) {
            pathToNavigate = entry.path();
        }
    }

    ImGui::EndChild();

    // Navigate after loop to avoid iterator invalidation
    if (!pathToNavigate.empty()) {
        setCurrentPath(pathToNavigate);
    }
}

} // namespace BlenderFileFinder
