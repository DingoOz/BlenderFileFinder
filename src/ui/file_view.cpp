#include "file_view.hpp"
#include "../debug.hpp"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// Helper to convert OpenGL texture ID to ImTextureID (ImU64)
// Simple static_cast is safe since uint32_t -> uint64_t is well-defined
static inline ImTextureID toImTextureID(uint32_t id) {
    return static_cast<ImTextureID>(static_cast<uint64_t>(id));
}

namespace BlenderFileFinder {

FileView::FileView() = default;

bool FileView::matchesFilter(const std::string& filename, const std::string& filter) const {
    if (filter.empty()) return true;

    // Case-insensitive search
    std::string lowerFilename = filename;
    std::string lowerFilter = filter;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    return lowerFilename.find(lowerFilter) != std::string::npos;
}

bool FileView::matchesFilterWithTags(const BlendFileInfo& file, const std::string& filter) const {
    if (filter.empty()) return true;

    // First check filename
    if (matchesFilter(file.filename, filter)) {
        return true;
    }

    // Then check tags
    if (m_database) {
        const auto& tags = getCachedTags(file.path);
        std::string lowerFilter = filter;
        std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

        for (const auto& tag : tags) {
            std::string lowerTag = tag;
            std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
            if (lowerTag.find(lowerFilter) != std::string::npos) {
                return true;
            }
        }
    }

    return false;
}

std::string FileView::formatFileSize(uintmax_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        ++unitIndex;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(unitIndex > 0 ? 1 : 0) << size << " " << units[unitIndex];
    return oss.str();
}

std::string FileView::formatDate(const std::filesystem::file_time_type& time) const {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm* tm = std::localtime(&tt);

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

bool FileView::matchesTagFilter(const BlendFileInfo& file) const {
    if (m_tagFilter.empty() || !m_database) return true;
    // Use cached tags for filtering
    const auto& tags = getCachedTags(file.path);
    return std::find(tags.begin(), tags.end(), m_tagFilter) != tags.end();
}

const std::vector<std::string>& FileView::getCachedTags(const std::filesystem::path& path) const {
    static const std::vector<std::string> emptyTags;

    if (!m_database) return emptyTags;

    // Invalidate cache every 120 frames (~2 seconds) to pick up changes
    if (m_currentFrame - m_tagCacheFrame > 120) {
        m_tagCache.clear();
        m_tagCacheFrame = m_currentFrame;
        m_pendingTagLoads.clear();
        m_tagsLoadedThisFrame = 0;
    }

    auto it = m_tagCache.find(path);
    if (it != m_tagCache.end()) {
        return it->second;
    }

    // Not in cache - queue for loading but limit loads per frame
    // to keep UI responsive (load max 5 tag sets per frame)
    if (m_tagsLoadedThisFrame < 5) {
        auto queryStart = std::chrono::steady_clock::now();
        m_tagCache[path] = m_database->getTagsForFile(path);
        auto queryMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - queryStart).count();
        if (queryMs > 20) {
            DEBUG_LOG("Slow tag query during render: " << path.filename() << " took " << queryMs << "ms");
        }
        m_tagsLoadedThisFrame++;
        return m_tagCache[path];
    }

    // Queue for loading on next frame
    m_pendingTagLoads.insert(path);
    return emptyTags;
}

void FileView::loadPendingTags() {
    if (!m_database || m_pendingTagLoads.empty()) return;

    // Load up to 10 tag sets per call
    int loaded = 0;
    auto it = m_pendingTagLoads.begin();
    while (it != m_pendingTagLoads.end() && loaded < 10) {
        m_tagCache[*it] = m_database->getTagsForFile(*it);
        it = m_pendingTagLoads.erase(it);
        loaded++;
    }
}

void FileView::render(std::vector<FileGroup>& groups, ThumbnailCache& cache,
                      PreviewCache& previewCache, Database& database,
                      const std::string& filter, const std::string& tagFilter) {
    auto renderStart = std::chrono::steady_clock::now();

    m_database = &database;
    m_previewCache = &previewCache;
    m_tagFilter = tagFilter;
    m_currentFrame++;
    m_tagsLoadedThisFrame = 0;  // Reset per-frame limit

    // Log for first 10 frames
    if (m_currentFrame <= 10) {
        DEBUG_LOG("FileView::render() frame " << m_currentFrame << " starting with " << groups.size() << " groups");
    }

    // Load some pending tags from previous frames
    auto tagLoadStart = std::chrono::steady_clock::now();
    loadPendingTags();
    auto tagLoadMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tagLoadStart).count();
    if (tagLoadMs > 10 || m_currentFrame <= 10) {
        DEBUG_LOG("FileView frame " << m_currentFrame << " loadPendingTags took " << tagLoadMs << "ms (pending: " << m_pendingTagLoads.size() << ")");
    }

    // Toolbar
    if (ImGui::Button(m_gridView ? "List View" : "Grid View")) {
        toggleView();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char* sortModes[] = {"Name", "Date", "Size"};
    int currentSort = static_cast<int>(m_sortMode);
    if (ImGui::Combo("Sort", &currentSort, sortModes, 3)) {
        m_sortMode = static_cast<SortMode>(currentSort);
    }

    ImGui::SameLine();
    if (ImGui::Button(m_sortAscending ? "Asc" : "Desc")) {
        m_sortAscending = !m_sortAscending;
    }

    if (m_gridView) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Size", &m_thumbnailSize, 64.0f, 256.0f, "%.0f");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show All", &m_showAllVersions);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show all file versions instead of grouping");
    }

    // Tag filter dropdown
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char* currentTagLabel = m_tagFilter.empty() ? "All Tags" : m_tagFilter.c_str();
    if (ImGui::BeginCombo("##viewtagfilter", currentTagLabel)) {
        if (ImGui::Selectable("All Tags", m_tagFilter.empty())) {
            if (m_tagFilterCallback) {
                m_tagFilterCallback("");
            }
        }
        if (!m_availableTags.empty()) {
            ImGui::Separator();
        }
        for (const auto& tag : m_availableTags) {
            bool isSelected = (m_tagFilter == tag);
            if (ImGui::Selectable(tag.c_str(), isSelected)) {
                if (m_tagFilterCallback) {
                    m_tagFilterCallback(tag);
                }
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filter files by tag");
    }

    ImGui::Separator();

    // Content area
    ImVec2 contentRegion = ImGui::GetContentRegionAvail();
    if (m_currentFrame <= 10) {
        DEBUG_LOG("FileView content region available: " << contentRegion.x << "x" << contentRegion.y);
    }

    ImGui::BeginChild("FileContent", ImVec2(0, 0), false);

    ImVec2 childSize = ImGui::GetContentRegionAvail();
    ImVec2 childPos = ImGui::GetCursorScreenPos();
    if (m_currentFrame <= 10) {
        DEBUG_LOG("FileContent child: pos=(" << childPos.x << "," << childPos.y << ") size=" << childSize.x << "x" << childSize.y);
    }

    if (m_gridView) {
        renderGridView(groups, cache, previewCache, filter);
    } else {
        renderListView(groups, cache, previewCache, filter);
    }

    ImGui::EndChild();
}

void FileView::renderGridView(std::vector<FileGroup>& groups, ThumbnailCache& cache, PreviewCache& previewCache, const std::string& filter) {
    auto gridStart = std::chrono::steady_clock::now();

    if (m_currentFrame <= 10) {
        DEBUG_LOG("FileView::renderGridView frame " << m_currentFrame << " starting");
    }

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float itemWidth = m_thumbnailSize + 20.0f;
    int columns = std::max(1, static_cast<int>(windowWidth / itemWidth));

    int col = 0;

    // Build flat list of files to display
    auto buildListStart = std::chrono::steady_clock::now();
    std::vector<const BlendFileInfo*> filesToDisplay;
    int filterSkipped = 0;
    int tagSkipped = 0;
    int emptySkipped = 0;

    for (auto& group : groups) {
        // Check if primary file matches filter (filename OR tags)
        if (!matchesFilterWithTags(group.primaryFile, filter)) {
            filterSkipped++;
            continue;
        }
        if (!matchesTagFilter(group.primaryFile)) {
            tagSkipped++;
            continue;
        }
        if (group.primaryFile.path.empty()) {
            emptySkipped++;
            continue;
        }

        filesToDisplay.push_back(&group.primaryFile);

        // If showing all versions, add version files too
        if (m_showAllVersions) {
            for (const auto& version : group.versions) {
                if (matchesFilterWithTags(version, filter)) {
                    filesToDisplay.push_back(&version);
                }
            }
        }
    }
    auto buildListMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - buildListStart).count();
    if (buildListMs > 5 || m_currentFrame <= 10) {
        DEBUG_LOG("Build file list: " << buildListMs << "ms for " << filesToDisplay.size() << " files (filtered:" << filterSkipped << " tagSkip:" << tagSkipped << " empty:" << emptySkipped << ")");
    }

    // Count versions per file (for badge display when not showing all)
    auto versionCountStart = std::chrono::steady_clock::now();
    std::map<std::filesystem::path, size_t> versionCounts;
    if (!m_showAllVersions) {
        for (auto& group : groups) {
            versionCounts[group.primaryFile.path] = group.versions.size();
        }
    }
    auto versionCountMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - versionCountStart).count();
    if (versionCountMs > 10 || m_currentFrame <= 10) {
        DEBUG_LOG("Version count map: " << versionCountMs << "ms for " << versionCounts.size() << " entries");
    }

    if (m_currentFrame <= 10) {
        DEBUG_LOG("About to start file render loop...");
    }

    // Render each file
    int fileIndex = 0;
    auto loopStart = std::chrono::steady_clock::now();

    for (const BlendFileInfo* filePtr : filesToDisplay) {
        auto itemStart = std::chrono::steady_clock::now();
        const BlendFileInfo& file = *filePtr;

        // Log first few items in first 10 frames with detailed step tracking
        bool logThis = (m_currentFrame <= 10 && fileIndex < 3);
        if (logThis) {
            DEBUG_LOG("  [" << fileIndex << "] START: " << file.filename);
        }

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] PushID...");
        ImGui::PushID(file.path.string().c_str());

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] isSelected...");
        bool isItemSelected = isSelected(file);
        bool isHovered = false;

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] GetCursorScreenPos...");
        // Card-like container with padding
        ImVec2 cardStart = ImGui::GetCursorScreenPos();
        float cardWidth = m_thumbnailSize + 16.0f;
        float cardHeight = m_thumbnailSize + 50.0f;

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] InvisibleButton...");
        // Invisible button for the whole card area
        ImGui::InvisibleButton("##card", ImVec2(cardWidth, cardHeight));
        isHovered = ImGui::IsItemHovered();
        if (logThis) DEBUG_LOG("  [" << fileIndex << "] InvisibleButton done");

        if (ImGui::IsItemClicked()) {
            m_selectedPath = file.path;
            if (m_selectCallback) {
                m_selectCallback(file);
            }
        }
        if (isHovered && ImGui::IsMouseDoubleClicked(0)) {
            if (m_openCallback) {
                m_openCallback(file);
            }
        }

        // Context menu
        if (ImGui::BeginPopupContextItem("FileContext")) {
            renderFileContextMenu(file);
            ImGui::EndPopup();
        }

        // Draw card background
        if (logThis) DEBUG_LOG("  [" << fileIndex << "] GetWindowDrawList...");
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 bgColor = isItemSelected ? IM_COL32(230, 115, 25, 60) :
                        isHovered ? IM_COL32(80, 80, 80, 120) : IM_COL32(40, 40, 40, 100);
        ImU32 borderColor = isItemSelected ? IM_COL32(230, 115, 25, 255) :
                           isHovered ? IM_COL32(120, 120, 120, 200) : IM_COL32(0, 0, 0, 0);

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] AddRectFilled...");
        drawList->AddRectFilled(cardStart, ImVec2(cardStart.x + cardWidth, cardStart.y + cardHeight),
                                bgColor, 6.0f);
        if (isItemSelected || isHovered) {
            drawList->AddRect(cardStart, ImVec2(cardStart.x + cardWidth, cardStart.y + cardHeight),
                             borderColor, 6.0f, 0, 2.0f);
        }

        // Draw thumbnail centered in card (or animated preview if hovered and available)
        ImVec2 thumbPos = ImVec2(cardStart.x + 8.0f, cardStart.y + 8.0f);

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] Checking preview...");
        bool showingPreview = false;
        if (isHovered && previewCache.hasPreview(file.path)) {
            // Track hover state for animation timing
            if (m_hoveredPath != file.path) {
                m_hoveredPath = file.path;
                m_hoverStartTime = std::chrono::steady_clock::now();
                // Start loading preview if not already loaded
                previewCache.loadPreview(file.path);
            }

            PreviewFrames* preview = previewCache.getPreview(file.path);
            if (preview && preview->loaded && !preview->textureIds.empty()) {
                // Calculate which frame to show based on time (24fps animation)
                auto elapsed = std::chrono::steady_clock::now() - m_hoverStartTime;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
                int frameIndex = (ms / 42) % static_cast<int>(preview->textureIds.size()); // ~24fps

                drawList->AddImage(toImTextureID(preview->textureIds[frameIndex]), thumbPos,
                                  ImVec2(thumbPos.x + m_thumbnailSize, thumbPos.y + m_thumbnailSize));
                showingPreview = true;
            }
        } else if (m_hoveredPath == file.path) {
            // Clear hover state when no longer hovering
            m_hoveredPath.clear();
        }

        if (!showingPreview) {
            if (logThis) DEBUG_LOG("  [" << fileIndex << "] cache.getTexture...");
            uint32_t textureId = cache.getTexture(file.path);
            if (logThis) DEBUG_LOG("  [" << fileIndex << "] AddImage (texture=" << textureId << ")...");
            drawList->AddImage(toImTextureID(textureId), thumbPos,
                              ImVec2(thumbPos.x + m_thumbnailSize, thumbPos.y + m_thumbnailSize));
        }

        if (logThis) DEBUG_LOG("  [" << fileIndex << "] Drawing filename...");
        // Filename (truncated) - draw below thumbnail
        std::string displayName = file.filename;
        if (displayName.length() > 18) {
            displayName = displayName.substr(0, 15) + "...";
        }
        ImVec2 textPos = ImVec2(cardStart.x + 8.0f, cardStart.y + m_thumbnailSize + 12.0f);
        drawList->AddText(textPos, IM_COL32(230, 230, 230, 255), displayName.c_str());

        // Version indicator (only when not showing all versions)
        if (!m_showAllVersions) {
            auto countIt = versionCounts.find(file.path);
            if (countIt != versionCounts.end() && countIt->second > 0) {
                char versionText[32];
                snprintf(versionText, sizeof(versionText), "+%zu", countIt->second);
                ImVec2 badgePos = ImVec2(cardStart.x + cardWidth - 28.0f, cardStart.y + 4.0f);
                drawList->AddRectFilled(badgePos, ImVec2(badgePos.x + 24.0f, badgePos.y + 18.0f),
                                       IM_COL32(90, 90, 180, 220), 4.0f);
                drawList->AddText(ImVec2(badgePos.x + 4.0f, badgePos.y + 2.0f),
                                 IM_COL32(255, 255, 255, 255), versionText);
            }
        }

        // Tag indicators (small colored dots or first tag name)
        if (m_database) {
            const auto& tags = getCachedTags(file.path);
            if (!tags.empty()) {
                // Draw tag indicator at bottom-left of card
                ImVec2 tagPos = ImVec2(cardStart.x + 4.0f, cardStart.y + cardHeight - 16.0f);
                std::string tagText = tags[0];
                if (tagText.length() > 10) {
                    tagText = tagText.substr(0, 8) + "..";
                }
                if (tags.size() > 1) {
                    tagText += " +" + std::to_string(tags.size() - 1);
                }
                drawList->AddRectFilled(tagPos, ImVec2(tagPos.x + 8.0f * tagText.length(), tagPos.y + 14.0f),
                                       IM_COL32(70, 130, 180, 200), 3.0f);
                drawList->AddText(ImVec2(tagPos.x + 2.0f, tagPos.y + 1.0f),
                                 IM_COL32(255, 255, 255, 255), tagText.c_str());
            }
        }

        // Show tooltip on hover
        if (isHovered) {
            ImGui::BeginTooltip();
            renderFileDetails(file);
            ImGui::EndTooltip();
        }

        ImGui::PopID();

        // Log slow item renders
        auto itemMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - itemStart).count();
        if (itemMs > 50 || (m_currentFrame <= 10 && fileIndex < 3)) {
            DEBUG_LOG("  File " << fileIndex << " (" << file.filename << ") render took " << itemMs << "ms");
        }
        fileIndex++;

        // Layout
        ++col;
        if (col < columns) {
            ImGui::SameLine();
        } else {
            col = 0;
        }
    }

    // Log total loop time
    auto loopMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loopStart).count();
    if (loopMs > 100 || m_currentFrame <= 10) {
        DEBUG_LOG("File render loop: " << loopMs << "ms for " << fileIndex << " files");
    }

    // Legacy expanded versions code (for tree-style expansion, not used with Show All)
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (auto& group : groups) {
        bool hasVersions = !group.versions.empty();
        if (!m_showAllVersions && hasVersions && group.isExpanded) {
            col = 0; // Start new row
            ImGui::Dummy(ImVec2(20.0f, 0)); // Indent
            ImGui::SameLine();

            float smallThumbSize = m_thumbnailSize * 0.7f;
            float smallCardWidth = smallThumbSize + 12.0f;
            float smallCardHeight = smallThumbSize + 40.0f;

            for (const auto& version : group.versions) {
                ImGui::PushID(version.filename.c_str());

                bool versionSelected = isSelected(version);
                ImVec2 vCardStart = ImGui::GetCursorScreenPos();

                ImGui::InvisibleButton("##vcard", ImVec2(smallCardWidth, smallCardHeight));
                bool vHovered = ImGui::IsItemHovered();

                if (ImGui::IsItemClicked()) {
                    m_selectedPath = version.path;
                    if (m_selectCallback) {
                        m_selectCallback(version);
                    }
                }
                if (vHovered && ImGui::IsMouseDoubleClicked(0)) {
                    if (m_openCallback) {
                        m_openCallback(version);
                    }
                }

                // Draw version card with slightly muted colors
                ImU32 vBgColor = versionSelected ? IM_COL32(230, 115, 25, 50) :
                                vHovered ? IM_COL32(70, 70, 70, 100) : IM_COL32(35, 35, 35, 80);
                ImU32 vBorderColor = versionSelected ? IM_COL32(230, 115, 25, 200) :
                                    vHovered ? IM_COL32(100, 100, 100, 150) : IM_COL32(0, 0, 0, 0);

                drawList->AddRectFilled(vCardStart,
                    ImVec2(vCardStart.x + smallCardWidth, vCardStart.y + smallCardHeight),
                    vBgColor, 4.0f);
                if (versionSelected || vHovered) {
                    drawList->AddRect(vCardStart,
                        ImVec2(vCardStart.x + smallCardWidth, vCardStart.y + smallCardHeight),
                        vBorderColor, 4.0f, 0, 1.5f);
                }

                uint32_t versionTexture = cache.getTexture(version.path);
                ImVec2 vThumbPos = ImVec2(vCardStart.x + 6.0f, vCardStart.y + 6.0f);
                drawList->AddImage(toImTextureID(versionTexture), vThumbPos,
                    ImVec2(vThumbPos.x + smallThumbSize, vThumbPos.y + smallThumbSize));

                std::string versionName = version.filename;
                if (versionName.length() > 14) {
                    versionName = versionName.substr(0, 11) + "...";
                }
                ImVec2 vTextPos = ImVec2(vCardStart.x + 6.0f, vCardStart.y + smallThumbSize + 10.0f);
                drawList->AddText(vTextPos, IM_COL32(200, 200, 200, 255), versionName.c_str());

                if (vHovered) {
                    ImGui::BeginTooltip();
                    renderFileDetails(version);
                    ImGui::EndTooltip();
                }

                ImGui::PopID();

                ++col;
                if (col < columns) {
                    ImGui::SameLine();
                } else {
                    col = 0;
                    ImGui::Dummy(ImVec2(20.0f, 0)); // Indent for next row
                    ImGui::SameLine();
                }
            }
            col = 0;
        }
    }

    // Log grid view total time for first 10 frames
    auto gridTotalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gridStart).count();
    if (gridTotalMs > 50 || m_currentFrame <= 10) {
        DEBUG_LOG("FileView::renderGridView frame " << m_currentFrame << " complete: " << gridTotalMs << "ms (" << filesToDisplay.size() << " files displayed)");
    }
}

void FileView::renderListView(std::vector<FileGroup>& groups, ThumbnailCache& cache, PreviewCache& previewCache, const std::string& filter) {
    (void)previewCache; // Preview animation not implemented for list view yet
    ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("FileList", 6, flags)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Tags", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("Blender", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (auto& group : groups) {
            // Check if primary file matches filter (filename OR tags)
            if (!matchesFilterWithTags(group.primaryFile, filter)) {
                continue;
            }

            // Filter by tag dropdown if set
            if (!matchesTagFilter(group.primaryFile)) {
                continue;
            }

            bool hasVersions = !group.versions.empty();

            ImGui::TableNextRow();
            ImGui::PushID(group.baseName.c_str());

            // Thumbnail column
            ImGui::TableNextColumn();
            uint32_t textureId = cache.getTexture(group.primaryFile.path);
            ImGui::Image(toImTextureID(textureId), ImVec2(32, 32));

            // Name column
            ImGui::TableNextColumn();

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_SpanFullWidth;
            if (!hasVersions) {
                nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (isSelected(group.primaryFile)) {
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            }

            bool opened = ImGui::TreeNodeEx(group.primaryFile.filename.c_str(), nodeFlags);

            if (ImGui::IsItemClicked()) {
                m_selectedPath = group.primaryFile.path;
                if (m_selectCallback) {
                    m_selectCallback(group.primaryFile);
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                if (m_openCallback) {
                    m_openCallback(group.primaryFile);
                }
            }

            if (ImGui::BeginPopupContextItem()) {
                renderFileContextMenu(group.primaryFile);
                ImGui::EndPopup();
            }

            // Tags column
            ImGui::TableNextColumn();
            if (m_database) {
                const auto& tags = getCachedTags(group.primaryFile.path);
                for (size_t i = 0; i < tags.size() && i < 2; ++i) {
                    if (i > 0) ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "[%s]", tags[i].c_str());
                }
                if (tags.size() > 2) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "+%zu", tags.size() - 2);
                }
            }

            // Size column
            ImGui::TableNextColumn();
            ImGui::Text("%s", formatFileSize(group.primaryFile.fileSize).c_str());

            // Modified column
            ImGui::TableNextColumn();
            ImGui::Text("%s", formatDate(group.primaryFile.modifiedTime).c_str());

            // Blender version column
            ImGui::TableNextColumn();
            ImGui::Text("%s", group.primaryFile.metadata.blenderVersion.c_str());

            // Versions
            if (hasVersions && opened) {
                for (const auto& version : group.versions) {
                    ImGui::TableNextRow();
                    ImGui::PushID(version.filename.c_str());

                    ImGui::TableNextColumn();
                    uint32_t versionTexture = cache.getTexture(version.path);
                    ImGui::Image(toImTextureID(versionTexture), ImVec2(24, 24));

                    ImGui::TableNextColumn();
                    ImGuiTreeNodeFlags versionFlags = ImGuiTreeNodeFlags_Leaf |
                                                      ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                      ImGuiTreeNodeFlags_SpanFullWidth;
                    if (isSelected(version)) {
                        versionFlags |= ImGuiTreeNodeFlags_Selected;
                    }

                    ImGui::TreeNodeEx(version.filename.c_str(), versionFlags);

                    if (ImGui::IsItemClicked()) {
                        m_selectedPath = version.path;
                        if (m_selectCallback) {
                            m_selectCallback(version);
                        }
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        if (m_openCallback) {
                            m_openCallback(version);
                        }
                    }

                    // Tags column (for versions)
                    ImGui::TableNextColumn();
                    if (m_database) {
                        const auto& tags = getCachedTags(version.path);
                        for (size_t i = 0; i < tags.size() && i < 2; ++i) {
                            if (i > 0) ImGui::SameLine();
                            ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "[%s]", tags[i].c_str());
                        }
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", formatFileSize(version.fileSize).c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", formatDate(version.modifiedTime).c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", version.metadata.blenderVersion.c_str());

                    ImGui::PopID();
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void FileView::renderFileContextMenu(const BlendFileInfo& file) {
    if (ImGui::MenuItem("Open in Blender")) {
        if (m_openCallback) {
            m_openCallback(file);
        }
    }

    if (ImGui::MenuItem("Open Containing Folder")) {
        if (m_openFolderCallback) {
            m_openFolderCallback(file.path.parent_path());
        }
    }

    ImGui::Separator();

    // Tags submenu
    if (ImGui::BeginMenu("Tags")) {
        renderTagMenu(file);
        ImGui::EndMenu();
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Copy Path")) {
        ImGui::SetClipboardText(file.path.string().c_str());
    }
}

void FileView::renderTagMenu(const BlendFileInfo& file) {
    if (!m_database) return;

    // Show current tags with remove option
    const auto& currentTags = getCachedTags(file.path);
    if (!currentTags.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Current tags:");
        for (const auto& tag : currentTags) {
            ImGui::PushID(tag.c_str());
            if (ImGui::MenuItem(("  " + tag + " [x]").c_str())) {
                m_database->removeTagFromFile(file.path, tag);
                m_tagCache.erase(file.path);  // Invalidate cache for this file
            }
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    // Show existing tags to add
    auto allTags = m_database->getAllTags();
    if (!allTags.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Add existing tag:");
        for (const auto& tag : allTags) {
            // Skip if already has this tag (use cached tags)
            const auto& fileTags = getCachedTags(file.path);
            if (std::find(fileTags.begin(), fileTags.end(), tag) != fileTags.end()) continue;

            ImGui::PushID(("add_" + tag).c_str());
            if (ImGui::MenuItem(("  + " + tag).c_str())) {
                m_database->addTagToFile(file.path, tag);
                m_tagCache.erase(file.path);  // Invalidate cache for this file
            }
            ImGui::PopID();
        }
        ImGui::Separator();
    }

    // Add new tag input
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "New tag:");
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputText("##newtag", m_newTagBuffer, sizeof(m_newTagBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string newTag(m_newTagBuffer);
        if (!newTag.empty()) {
            m_database->addTagToFile(file.path, newTag);
            m_tagCache.erase(file.path);  // Invalidate cache
            m_newTagBuffer[0] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        std::string newTag(m_newTagBuffer);
        if (!newTag.empty()) {
            m_database->addTagToFile(file.path, newTag);
            m_tagCache.erase(file.path);  // Invalidate cache
            m_newTagBuffer[0] = '\0';
        }
    }
}

void FileView::renderFileTags(const BlendFileInfo& file) {
    if (!m_database) return;

    const auto& tags = getCachedTags(file.path);
    if (tags.empty()) return;

    ImGui::SameLine();
    for (size_t i = 0; i < tags.size() && i < 3; ++i) {  // Show max 3 tags
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "[%s]", tags[i].c_str());
    }
    if (tags.size() > 3) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "+%zu", tags.size() - 3);
    }
}

void FileView::renderFileDetails(const BlendFileInfo& file) {
    // Header with filename
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.15f, 1.0f));
    ImGui::Text("%s", file.filename.c_str());
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // File info section
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "PATH");
    ImGui::TextWrapped("%s", file.path.parent_path().string().c_str());

    ImGui::Spacing();

    // Two-column layout for file stats
    if (ImGui::BeginTable("##fileinfo", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "SIZE");
        ImGui::Text("%s", formatFileSize(file.fileSize).c_str());

        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "MODIFIED");
        ImGui::Text("%s", formatDate(file.modifiedTime).c_str());

        ImGui::EndTable();
    }

    // Blender info section
    if (!file.metadata.blenderVersion.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "BLENDER");

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "v%s", file.metadata.blenderVersion.c_str());

        if (file.metadata.isCompressed) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(compressed)");
        }

        // Show metadata if available and not compressed
        if (!file.metadata.isCompressed) {
            if (file.metadata.objectCount > 0 || file.metadata.meshCount > 0 ||
                file.metadata.materialCount > 0) {
                ImGui::Spacing();

                if (ImGui::BeginTable("##blendinfo", 3, ImGuiTableFlags_None)) {
                    if (file.metadata.objectCount > 0) {
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Objects");
                        ImGui::Text("%d", file.metadata.objectCount);
                    }
                    if (file.metadata.meshCount > 0) {
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Meshes");
                        ImGui::Text("%d", file.metadata.meshCount);
                    }
                    if (file.metadata.materialCount > 0) {
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Materials");
                        ImGui::Text("%d", file.metadata.materialCount);
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
}

} // namespace BlenderFileFinder
