#include "app.hpp"
#include "debug.hpp"
#include "scanner.hpp"
#include "thumbnail_cache.hpp"
#include "version_grouper.hpp"
#include "database.hpp"
#include "blend_parser.hpp"
#include "preview_cache.hpp"
#include "ui/file_browser.hpp"
#include "ui/file_view.hpp"
#include "ui/search_bar.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace BlenderFileFinder {

// Forward declarations for UI components
static FileBrowser* s_fileBrowser = nullptr;
static FileView* s_fileView = nullptr;
static SearchBar* s_searchBar = nullptr;

App::App() = default;

App::~App() = default;

bool App::init() {
    DEBUG_LOG("App::init() starting");

    // Initialize GLFW
    if (!glfwInit()) {
        DEBUG_LOG("glfwInit failed");
        return false;
    }
    DEBUG_LOG("GLFW initialized");

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create window
    m_window = glfwCreateWindow(1280, 720, "Blender File Finder", nullptr, nullptr);
    if (!m_window) {
        DEBUG_LOG("Window creation failed");
        glfwTerminate();
        return false;
    }
    DEBUG_LOG("Window created");

    // Set window icon
    setWindowIcon();

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    DEBUG_LOG("OpenGL context set");

    // Initialize Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    DEBUG_LOG("ImGui context created");

    // Load nicer font (Inter)
    const char* fontPaths[] = {
        "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/opentype/inter/Inter-Medium.otf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        nullptr
    };

    for (const char** path = fontPaths; *path != nullptr; ++path) {
        if (std::filesystem::exists(*path)) {
            io.Fonts->AddFontFromFileTTF(*path, 16.0f);
            DEBUG_LOG("Loaded font: " << *path);
            break;
        }
    }
    if (io.Fonts->Fonts.empty()) {
        io.Fonts->AddFontDefault();
        DEBUG_LOG("Using default font");
    }

    // Custom Blender-inspired dark theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.95f, 0.55f, 0.15f, 0.3f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.5f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.95f, 0.55f, 0.15f, 0.7f);
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.7f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.55f, 0.15f, 0.7f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.55f, 0.15f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.5f);
    colors[ImGuiCol_TabActive] = ImVec4(0.95f, 0.55f, 0.15f, 0.7f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.95f, 0.55f, 0.15f, 0.35f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.95f, 0.55f, 0.15f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.95f, 0.55f, 0.15f, 0.5f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.95f, 0.55f, 0.15f, 0.75f);

    DEBUG_LOG("Initializing ImGui backends");
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    DEBUG_LOG("ImGui backends initialized");

    // Initialize components
    DEBUG_LOG("Creating Scanner");
    m_scanner = std::make_unique<Scanner>();
    DEBUG_LOG("Creating ThumbnailCache");
    m_thumbnailCache = std::make_unique<ThumbnailCache>(2000);  // Increased to reduce eviction thrashing
    DEBUG_LOG("Creating VersionGrouper");
    m_versionGrouper = std::make_unique<VersionGrouper>();
    DEBUG_LOG("Creating Database");
    m_database = std::make_unique<Database>();
    DEBUG_LOG("Creating PreviewCache");
    m_previewCache = std::make_unique<PreviewCache>();

    // Open database
    const char* home = std::getenv("HOME");
    std::filesystem::path dbPath;
    if (home) {
        dbPath = std::filesystem::path(home) / ".local" / "share" / "BlenderFileFinder" / "database.db";
    } else {
        dbPath = "/tmp/BlenderFileFinder/database.db";
    }

    if (!m_database->open(dbPath)) {
        DEBUG_LOG("Failed to open database!");
        return false;
    }
    DEBUG_LOG("Database opened at: " << dbPath);

    DEBUG_LOG("Core components created");

    // Initialize UI components
    DEBUG_LOG("Creating FileBrowser");
    s_fileBrowser = new FileBrowser();
    DEBUG_LOG("Creating FileView");
    s_fileView = new FileView();
    DEBUG_LOG("Creating SearchBar");
    s_searchBar = new SearchBar();
    DEBUG_LOG("UI components created");

    // Set up callbacks
    s_fileBrowser->setScanCallback([this](const std::filesystem::path& path) {
        // Add as scan location and scan
        m_database->addScanLocation(path, true);
        startScan(path, false);
    });

    s_fileView->setOpenCallback([this](const BlendFileInfo& file) {
        openInBlender(file.path);
    });

    s_fileView->setOpenFolderCallback([this](const std::filesystem::path& path) {
        openContainingFolder(path);
    });

    s_fileView->setTagFilterCallback([this](const std::string& tag) {
        m_tagFilter = tag;
    });

    s_searchBar->setSearchCallback([this](const std::string& query) {
        m_searchQuery = query;
    });

    m_currentPath = std::filesystem::current_path();

    // Database loading is deferred to after first frame renders
    // to keep the window responsive during startup

    DEBUG_LOG("App::init() complete");
    return true;
}

void App::run() {
    DEBUG_LOG("App::run() entered, starting main loop");

    while (!glfwWindowShouldClose(m_window)) {
        auto frameStart = std::chrono::steady_clock::now();

        // Poll events with timing
        auto pollStart = std::chrono::steady_clock::now();
        glfwPollEvents();
        auto pollMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pollStart).count();
        if (m_frameCount <= 10 || pollMs > 10) {
            DEBUG_LOG("Frame " << m_frameCount << " glfwPollEvents: " << pollMs << "ms");
        }

        // Deferred initial load - wait a few frames for window to become responsive
        m_frameCount++;

        if (m_needsInitialLoad && m_frameCount > 3) {
            DEBUG_LOG("Frame " << m_frameCount << " triggering initial background load");
            m_needsInitialLoad = false;
            startBackgroundLoad();
        }

        // Check if background load completed (with timing)
        auto bgCheckStart = std::chrono::steady_clock::now();
        checkBackgroundLoadComplete();
        auto bgCheckMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bgCheckStart).count();
        if (m_frameCount <= 10 || bgCheckMs > 10) {
            DEBUG_LOG("Frame " << m_frameCount << " checkBackgroundLoadComplete: " << bgCheckMs << "ms");
        }

        // Process loaded thumbnails and previews (with timing)
        auto processStart = std::chrono::steady_clock::now();
        m_thumbnailCache->processLoadedThumbnails();
        auto thumbProcMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - processStart).count();

        m_previewCache->processLoadedPreviews();
        auto previewProcMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - processStart).count() - thumbProcMs;

        // Process preview preloading (a few per frame to keep UI responsive)
        if (m_isPreloadingPreviews && m_preloadCurrentIndex < m_preloadPaths.size() && !m_preloadCancelRequested) {
            // Load up to 3 previews per frame
            for (int i = 0; i < 3 && m_preloadCurrentIndex < m_preloadPaths.size(); ++i, ++m_preloadCurrentIndex) {
                m_preloadCurrentFile = m_preloadPaths[m_preloadCurrentIndex].filename().string();
                m_previewCache->loadPreview(m_preloadPaths[m_preloadCurrentIndex]);
            }

            // Check if done
            if (m_preloadCurrentIndex >= m_preloadPaths.size()) {
                m_isPreloadingPreviews = false;
                m_showPreloadDialog = false;
                DEBUG_LOG("Preview preload complete: " << m_preloadTotalCount << " files");
            }
        } else if (m_preloadCancelRequested && m_isPreloadingPreviews) {
            m_isPreloadingPreviews = false;
            m_showPreloadDialog = false;
            DEBUG_LOG("Preview preload cancelled at " << m_preloadCurrentIndex << "/" << m_preloadTotalCount);
        }

        if (m_frameCount <= 10 || thumbProcMs > 10 || previewProcMs > 10) {
            DEBUG_LOG("Frame " << m_frameCount << " process: thumbs=" << thumbProcMs << "ms previews=" << previewProcMs << "ms");
        }

        // Log frame progress for first 10 frames
        if (m_frameCount <= 10) {
            DEBUG_LOG("Frame " << m_frameCount << " starting render");
        }

        // Check for scan completion (with timing for early frames)
        auto scanCheckStart = std::chrono::steady_clock::now();
        if (m_isScanning && m_scanner->isComplete()) {
            auto results = m_scanner->pollResults();

            // Save files to database
            auto locations = m_database->getAllScanLocations();
            int64_t locationId = 0;
            for (const auto& loc : locations) {
                if (m_currentPath.string().find(loc.path.string()) == 0) {
                    locationId = loc.id;
                    break;
                }
            }

            for (const auto& file : results) {
                m_database->addOrUpdateFile(file, locationId);
            }

            // Check if we have more locations to scan
            m_scanLocationIndex++;
            if (m_scanLocationIndex < static_cast<int>(m_pendingScanLocations.size())) {
                const auto& nextLoc = m_pendingScanLocations[m_scanLocationIndex];
                m_currentPath = nextLoc.path;
                m_scanner->startScan(nextLoc.path, nextLoc.recursive);
            } else {
                m_isScanning = false;
                m_pendingScanLocations.clear();
                startBackgroundLoad();
            }
        }

        // Time the scan check
        auto scanCheckMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - scanCheckStart).count();
        if (m_frameCount <= 10 || scanCheckMs > 10) {
            DEBUG_LOG("Frame " << m_frameCount << " scan check: " << scanCheckMs << "ms");
        }

        // Start ImGui frame (with timing)
        auto imguiStartTime = std::chrono::steady_clock::now();
        ImGui_ImplOpenGL3_NewFrame();
        auto gl3Ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - imguiStartTime).count();

        ImGui_ImplGlfw_NewFrame();
        auto glfwMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - imguiStartTime).count() - gl3Ms;

        ImGui::NewFrame();
        auto newFrameMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - imguiStartTime).count() - gl3Ms - glfwMs;

        if (m_frameCount <= 10) {
            DEBUG_LOG("Frame " << m_frameCount << " ImGui init: GL3=" << gl3Ms << "ms GLFW=" << glfwMs << "ms NewFrame=" << newFrameMs << "ms");
        }

        // Clear screen BEFORE rendering
        int displayW, displayH;
        glfwGetFramebufferSize(m_window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto renderStart = std::chrono::steady_clock::now();
        renderUI();
        auto renderMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - renderStart).count();

        // Render ImGui draw data
        auto imguiStart = std::chrono::steady_clock::now();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        auto imguiMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - imguiStart).count();

        auto swapStart = std::chrono::steady_clock::now();
        glfwSwapBuffers(m_window);
        auto swapMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - swapStart).count();

        auto frameMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frameStart).count();

        // Always log for first 10 frames
        if (m_frameCount <= 10) {
            DEBUG_LOG("Frame " << m_frameCount << " COMPLETE: renderUI=" << renderMs << "ms imgui=" << imguiMs << "ms swap=" << swapMs << "ms TOTAL=" << frameMs << "ms");
        } else if (frameMs > 100) {
            DEBUG_LOG("SLOW FRAME " << m_frameCount << ": renderUI=" << renderMs << "ms imgui=" << imguiMs << "ms swap=" << swapMs << "ms TOTAL=" << frameMs << "ms");
        }

        // Periodic FPS/responsiveness check - log every 5 seconds if frames are slow
        static int slowFrameCount = 0;
        static auto lastFpsReport = std::chrono::steady_clock::now();
        if (frameMs > 50) slowFrameCount++;

        auto now = std::chrono::steady_clock::now();
        auto sinceFpsReport = std::chrono::duration_cast<std::chrono::seconds>(now - lastFpsReport).count();
        if (sinceFpsReport >= 5) {
            if (slowFrameCount > 0) {
                DEBUG_LOG("FPS WARNING: " << slowFrameCount << " slow frames (>50ms) in last 5 seconds");
            }
            slowFrameCount = 0;
            lastFpsReport = now;
        }
    }
}

void App::shutdown() {
    // Wait for any background loading to complete
    if (m_loadThread.joinable()) {
        m_loadThread.join();
    }

    delete s_fileBrowser;
    delete s_fileView;
    delete s_searchBar;

    m_database->close();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void App::loadFromDatabase() {
    auto files = m_database->getAllFiles();
    m_fileGroups = VersionGrouper::groupFiles(files);
    DEBUG_LOG("Loaded " << files.size() << " files from database, " << m_fileGroups.size() << " groups");
}

void App::startBackgroundLoad() {
    if (m_isLoading) return;  // Already loading

    m_isLoading = true;
    m_loadComplete = false;

    m_loadThread = std::jthread([this]() {
        DEBUG_LOG("Background load starting");
        auto startTime = std::chrono::steady_clock::now();

        auto files = m_database->getAllFiles();
        auto dbTime = std::chrono::steady_clock::now();
        DEBUG_LOG("Database query took: " << std::chrono::duration_cast<std::chrono::milliseconds>(dbTime - startTime).count() << "ms");

        auto groups = VersionGrouper::groupFiles(files);
        auto groupTime = std::chrono::steady_clock::now();
        DEBUG_LOG("Grouping took: " << std::chrono::duration_cast<std::chrono::milliseconds>(groupTime - dbTime).count() << "ms");

        {
            std::lock_guard<std::mutex> lock(m_loadMutex);
            m_loadedGroups = std::move(groups);
        }

        DEBUG_LOG("Background load complete: " << files.size() << " files, total: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() << "ms");
        m_loadComplete = true;
    });
}

void App::checkBackgroundLoadComplete() {
    if (m_loadComplete) {
        {
            std::lock_guard<std::mutex> lock(m_loadMutex);
            m_fileGroups = std::move(m_loadedGroups);
        }

        m_isLoading = false;
        m_loadComplete = false;

        // Wait for thread to finish
        if (m_loadThread.joinable()) {
            m_loadThread.join();
        }

        DEBUG_LOG("Transferred " << m_fileGroups.size() << " groups to main thread");
    }
}

void App::renderUI() {
    auto uiStart = std::chrono::steady_clock::now();

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    if (m_frameCount <= 10) {
        DEBUG_LOG("Viewport: pos=(" << viewport->WorkPos.x << "," << viewport->WorkPos.y
                  << ") size=" << viewport->WorkSize.x << "x" << viewport->WorkSize.y);
    }

    ImGui::Begin("MainWindow", nullptr, windowFlags);

    renderMenuBar();

    auto t1 = std::chrono::steady_clock::now();
    renderToolbar();
    auto toolbarMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();

    // Calculate available height, reserving space for status bar
    float statusBarHeight = ImGui::GetFrameHeightWithSpacing() + 4;
    float availableHeight = ImGui::GetContentRegionAvail().y - statusBarHeight;

    // Main content area with sidebar
    ImGui::BeginChild("Sidebar", ImVec2(m_sidebarWidth, availableHeight), true);
    auto t2 = std::chrono::steady_clock::now();
    renderSidebar();
    auto sidebarMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t2).count();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Content", ImVec2(0, availableHeight), true);
    if (m_frameCount <= 10) {
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        DEBUG_LOG("Content child: pos=(" << contentPos.x << "," << contentPos.y
                  << ") size=" << contentSize.x << "x" << contentSize.y
                  << " availH=" << availableHeight);
    }
    auto t3 = std::chrono::steady_clock::now();
    renderMainContent();
    auto contentMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t3).count();
    ImGui::EndChild();

    // Status bar at the bottom
    auto t4 = std::chrono::steady_clock::now();
    renderStatusBar();
    auto statusMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t4).count();

    ImGui::End();

    // Modal dialogs
    renderNewFilesDialog();
    renderPreviewGenerationDialog();
    renderUserGuide();
    renderStatisticsDialog();
    renderBulkTagDialog();
    renderPreloadDialog();

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - uiStart).count();

    // Always log timing for first 10 frames
    if (m_frameCount <= 10) {
        DEBUG_LOG("Frame " << m_frameCount << " UI: toolbar=" << toolbarMs << "ms sidebar=" << sidebarMs << "ms content=" << contentMs << "ms status=" << statusMs << "ms TOTAL=" << totalMs << "ms");
    } else if (totalMs > 100) {
        DEBUG_LOG("SLOW UI: toolbar=" << toolbarMs << "ms sidebar=" << sidebarMs << "ms content=" << contentMs << "ms status=" << statusMs << "ms TOTAL=" << totalMs << "ms");
    }
}

void App::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Scan All Locations", "Ctrl+Shift+R")) {
                scanAllLocations();
            }
            if (ImGui::MenuItem("Refresh from Database", "F5")) {
                loadFromDatabase();
            }
            if (ImGui::MenuItem("Check for New Files...", "Ctrl+N")) {
                checkForNewFiles();
            }
            if (ImGui::MenuItem("Generate New Previews...", nullptr, false, !m_previewCache->isGenerating())) {
                startPreviewGeneration(false);
            }
            if (ImGui::MenuItem("Regenerate All Previews...", nullptr, false, !m_previewCache->isGenerating())) {
                startPreviewGeneration(true);
            }
            if (ImGui::MenuItem("Load All Preview Thumbnails...", nullptr, false, !m_isPreloadingPreviews)) {
                // Build list of all files with existing previews
                m_preloadPaths.clear();
                for (const auto& group : m_fileGroups) {
                    if (m_previewCache->hasPreview(group.primaryFile.path)) {
                        m_preloadPaths.push_back(group.primaryFile.path);
                    }
                    for (const auto& version : group.versions) {
                        if (m_previewCache->hasPreview(version.path)) {
                            m_preloadPaths.push_back(version.path);
                        }
                    }
                }
                m_preloadCurrentIndex = 0;
                m_preloadTotalCount = m_preloadPaths.size();
                m_preloadCancelRequested = false;
                m_preloadCurrentFile.clear();

                if (m_preloadTotalCount > 0) {
                    m_isPreloadingPreviews = true;
                    m_showPreloadDialog = true;
                    DEBUG_LOG("Starting preview preload for " << m_preloadTotalCount << " files");
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cleanup Missing Files")) {
                int removed = m_database->cleanupMissingFiles();
                DEBUG_LOG("Removed " << removed << " missing files");
                loadFromDatabase();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Bulk Add Tags...", "Ctrl+T")) {
                m_showBulkTagDialog = true;
                m_bulkTagSelectedLocation = -1;
                m_bulkTagName[0] = '\0';
                m_bulkTagPreviewFiles.clear();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Grid View", nullptr, s_fileView->isGridView())) {
                s_fileView->setGridView(true);
            }
            if (ImGui::MenuItem("List View", nullptr, !s_fileView->isGridView())) {
                s_fileView->setGridView(false);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Thumbnail Cache")) {
                m_thumbnailCache->clear();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("User Guide", "F1")) {
                m_showUserGuide = true;
            }
            if (ImGui::MenuItem("Database Statistics...")) {
                m_showStatisticsDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About")) {
                // TODO: About dialog
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void App::renderToolbar() {
    // Search bar
    ImGui::SetNextItemWidth(250);
    s_searchBar->render();

    ImGui::SameLine();

    // Tag filter dropdown - use cached tags
    ImGui::SetNextItemWidth(120);
    if (m_frameCount - m_tagsUpdateFrame > 120) {  // Refresh every ~2 seconds
        m_cachedAllTags = m_database->getAllTags();
        m_tagsUpdateFrame = m_frameCount;
    }
    if (ImGui::BeginCombo("##tagfilter", m_tagFilter.empty() ? "All Tags" : m_tagFilter.c_str())) {
        if (ImGui::Selectable("All Tags", m_tagFilter.empty())) {
            m_tagFilter.clear();
        }
        if (!m_cachedAllTags.empty()) {
            ImGui::Separator();
        }
        for (const auto& tag : m_cachedAllTags) {
            bool isSelected = (m_tagFilter == tag);
            if (ImGui::Selectable(tag.c_str(), isSelected)) {
                m_tagFilter = tag;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    // Scan All button
    if (m_isScanning) {
        ImGui::BeginDisabled();
        ImGui::Button("Scanning...");
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Scan All")) {
            scanAllLocations();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(s_fileView->isGridView() ? "Grid" : "List")) {
        s_fileView->toggleView();
    }

    ImGui::Separator();
}

void App::renderSidebar() {
    // Header with action buttons
    if (ImGui::Button("Scan All", ImVec2(-1, 0))) {
        scanAllLocations();
    }

    ImGui::Spacing();

    // Tracked Locations section
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "TRACKED FOLDERS");
    ImGui::Separator();

    renderScanLocations();

    ImGui::Spacing();
    ImGui::Spacing();

    // Add New Location section
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ADD FOLDER");
    ImGui::Separator();

    renderAddLocation();
}

void App::renderScanLocations() {
    // Use cached scan locations - refresh every ~2 seconds
    if (m_frameCount - m_locationsUpdateFrame > 120) {
        m_cachedScanLocations = m_database->getAllScanLocations();
        // Also cache file counts and group counts per location
        m_locationFileCounts.clear();
        m_locationGroupCounts.clear();
        for (const auto& loc : m_cachedScanLocations) {
            auto files = m_database->getFilesByScanLocation(loc.id);
            m_locationFileCounts[loc.id] = files.size();
            // Count unique groups after version collapsing
            auto groups = VersionGrouper::groupFiles(files);
            m_locationGroupCounts[loc.id] = groups.size();
        }
        m_locationsUpdateFrame = m_frameCount;
    }

    if (m_cachedScanLocations.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No folders added yet.");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Browse below to add one.");
        return;
    }

    // Helper lambda to check if a path is inside another recursive location
    auto isSubfolderOfOther = [this](const ScanLocation& loc) -> std::string {
        std::string locPathStr = loc.path.string();
        for (const auto& other : m_cachedScanLocations) {
            if (other.id == loc.id) continue;  // Skip self
            if (!other.recursive) continue;     // Only check recursive parents

            std::string otherPathStr = other.path.string();
            // Check if loc.path starts with other.path (is a subfolder)
            if (locPathStr.length() > otherPathStr.length() &&
                locPathStr.substr(0, otherPathStr.length()) == otherPathStr &&
                locPathStr[otherPathStr.length()] == '/') {
                return other.name.empty() ? other.path.filename().string() : other.name;
            }
        }
        return "";  // Not a subfolder of any recursive location
    };

    // List locations in a clean format
    for (const auto& loc : m_cachedScanLocations) {
        ImGui::PushID(static_cast<int>(loc.id));

        // Get file count and group count
        size_t fileCount = 0;
        size_t groupCount = 0;
        auto it = m_locationFileCounts.find(loc.id);
        if (it != m_locationFileCounts.end()) {
            fileCount = it->second;
        }
        auto git = m_locationGroupCounts.find(loc.id);
        if (git != m_locationGroupCounts.end()) {
            groupCount = git->second;
        }

        // Check if this is a redundant subfolder
        std::string parentFolder = isSubfolderOfOther(loc);
        bool isRedundant = !parentFolder.empty();

        // Display name: custom name or folder name
        std::string displayName = loc.name.empty() ? loc.path.filename().string() : loc.name;

        // Show folder with file count
        ImGui::BeginGroup();

        // Use different color for redundant folders (orange/amber)
        if (isRedundant) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
        }

        // Folder icon/indicator and name
        bool expanded = ImGui::TreeNode("##folder", "%s", displayName.c_str());

        if (isRedundant) {
            ImGui::PopStyleColor();
        }

        // Unique group count (cyan) - only show if different from total
        ImGui::SameLine();
        if (groupCount != fileCount) {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.9f, 1.0f), "[%zu]", groupCount);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%zu unique project%s (versions collapsed)", groupCount, groupCount == 1 ? "" : "s");
            }
            ImGui::SameLine();
        }

        // Total file count (green)
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "(%zu)", fileCount);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%zu .blend file%s total", fileCount, fileCount == 1 ? "" : "s");
        }

        // Recursive indicator
        if (loc.recursive) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 0.8f, 1.0f), "[R]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Recursive: scans all subfolders");
            }
        }

        // Redundant/duplicate indicator
        if (isRedundant) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "[DUP]");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Redundant: already covered by '%s' (recursive)\nRight-click to remove", parentFolder.c_str());
            }
        }

        // Right-click context menu
        if (ImGui::BeginPopupContextItem("location_context")) {
            if (ImGui::MenuItem("Scan This Folder")) {
                startScan(loc.path, true);
            }
            ImGui::Separator();
            if (isRedundant) {
                // Emphasize removal for redundant entries
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 1.0f));
                if (ImGui::MenuItem("Remove Duplicate Entry")) {
                    m_database->removeScanLocation(loc.id);
                    m_locationsUpdateFrame = -1000;  // Force refresh
                }
                ImGui::PopStyleColor();
            } else {
                if (ImGui::MenuItem("Remove from List")) {
                    m_database->removeScanLocation(loc.id);
                    m_locationsUpdateFrame = -1000;  // Force refresh
                }
            }
            ImGui::EndPopup();
        }

        if (expanded) {
            // Show full path
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", loc.path.string().c_str());

            // Show redundancy warning
            if (isRedundant) {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Already covered by '%s'", parentFolder.c_str());
            }

            // Action buttons
            if (ImGui::SmallButton("Scan")) {
                startScan(loc.path, true);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, isRedundant ? ImVec4(0.7f, 0.5f, 0.2f, 1.0f) : ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
            if (ImGui::SmallButton(isRedundant ? "Remove Duplicate" : "Remove")) {
                m_database->removeScanLocation(loc.id);
                m_locationsUpdateFrame = -1000;  // Force refresh
            }
            ImGui::PopStyleColor();

            ImGui::TreePop();
        }

        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void App::renderAddLocation() {
    // Use the file browser for navigation
    s_fileBrowser->render();

    // Add current folder button
    ImGui::Spacing();
    std::filesystem::path currentPath = s_fileBrowser->getCurrentPath();

    // Check if this folder is already added
    bool alreadyAdded = false;
    for (const auto& loc : m_cachedScanLocations) {
        if (loc.path == currentPath) {
            alreadyAdded = true;
            break;
        }
    }

    if (alreadyAdded) {
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "This folder is already tracked");
    } else {
        // Options for adding
        ImGui::Checkbox("Include subfolders", &m_newLocationRecursive);

        if (ImGui::Button("+ Add This Folder", ImVec2(-1, 0))) {
            m_database->addScanLocation(currentPath.string(), m_newLocationRecursive, "");
            m_locationsUpdateFrame = -1000;  // Force refresh of locations list

            // Immediately start scanning the new location
            startScan(currentPath, true);
        }
    }
}

void App::renderMainContent() {
    if (m_needsInitialLoad || m_isLoading) {
        ImGui::Text("Loading database...");
        // Simple animated dots
        int dots = (m_frameCount / 30) % 4;
        ImGui::SameLine();
        ImGui::Text("%.*s", dots, "...");
        return;
    }

    if (m_isScanning) {
        auto [scanned, total] = m_scanner->getProgress();
        ImGui::Text("Scanning... %d / %d files", scanned, total);
        if (m_pendingScanLocations.size() > 1) {
            ImGui::Text("Location %d of %zu", m_scanLocationIndex + 1, m_pendingScanLocations.size());
        }
        ImGui::ProgressBar(total > 0 ? static_cast<float>(scanned) / total : 0.0f);
    } else if (m_fileGroups.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                          "No files in database. Add scan locations and click 'Scan All'.");
    } else {
        auto fileViewStart = std::chrono::steady_clock::now();
        s_fileView->setAvailableTags(m_cachedAllTags);
        s_fileView->render(m_fileGroups, *m_thumbnailCache, *m_previewCache, *m_database, m_searchQuery, m_tagFilter);
        auto fileViewMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - fileViewStart).count();
        if (m_frameCount <= 10 || fileViewMs > 50) {
            DEBUG_LOG("Frame " << m_frameCount << " file_view->render: " << fileViewMs << "ms (" << m_fileGroups.size() << " groups)");
        }
    }
}

void App::renderStatusBar() {
    ImGui::Separator();

    // Determine if we're busy with any activity
    bool isLoadingThumbnails = m_thumbnailCache->isLoadingThumbnails();
    bool isBusy = m_isLoading || m_isScanning || m_previewCache->isGenerating() || m_needsInitialLoad || m_isPreloadingPreviews || isLoadingThumbnails;

    // Draw status indicator (colored circle)
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 5.0f;
    ImVec2 center = ImVec2(pos.x + radius + 2, pos.y + ImGui::GetTextLineHeight() / 2);

    if (isBusy) {
        // Red pulsing indicator when busy
        float pulse = (sinf(static_cast<float>(m_frameCount) * 0.15f) + 1.0f) * 0.5f;
        ImU32 color = IM_COL32(220, 60, 60, static_cast<int>(180 + 75 * pulse));
        drawList->AddCircleFilled(center, radius, color);
        drawList->AddCircle(center, radius, IM_COL32(180, 40, 40, 255), 0, 1.5f);
    } else {
        // Green indicator when idle
        drawList->AddCircleFilled(center, radius, IM_COL32(60, 180, 60, 200));
        drawList->AddCircle(center, radius, IM_COL32(40, 140, 40, 255), 0, 1.5f);
    }

    // Add spacing for the indicator
    ImGui::Dummy(ImVec2(radius * 2 + 8, 0));
    ImGui::SameLine();

    // Status text
    if (isBusy) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Busy");
        ImGui::SameLine();

        // Show what we're doing
        if (m_needsInitialLoad || m_isLoading) {
            ImGui::TextDisabled("(Loading database...)");
        } else if (m_isScanning) {
            auto [scanned, total] = m_scanner->getProgress();
            ImGui::TextDisabled("(Scanning %d/%d...)", scanned, total);
        } else if (m_previewCache->isGenerating()) {
            auto [current, total] = m_previewCache->getProgress();
            ImGui::TextDisabled("(Generating previews %d/%d...)", current + 1, total);
        } else if (m_isPreloadingPreviews) {
            ImGui::TextDisabled("(Loading preview thumbnails %zu/%zu...)",
                               m_preloadCurrentIndex, m_preloadTotalCount);
        } else if (isLoadingThumbnails) {
            auto [completed, total] = m_thumbnailCache->getLoadingProgress();
            size_t remaining = total - completed;
            ImGui::TextDisabled("(Loading %zu thumbnail%s...)", remaining, remaining == 1 ? "" : "s");
        }
        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Ready");
        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();
    }

    // Update cached stats every 60 frames (~1 second) to avoid querying database every frame
    if (m_frameCount - m_statsUpdateFrame > 60) {
        m_cachedFileCount = m_database->getTotalFileCount();
        m_cachedTagCount = m_database->getTotalTagCount();
        m_cachedLocationCount = m_database->getTotalScanLocationCount();
        m_statsUpdateFrame = m_frameCount;
    }

    // Use dimmed text for unobtrusive appearance
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

    ImGui::Text("%d files", m_cachedFileCount);
    ImGui::SameLine();
    ImGui::Text(" | %d tags", m_cachedTagCount);
    ImGui::SameLine();
    ImGui::Text(" | %d locations", m_cachedLocationCount);

    if (!m_fileGroups.empty()) {
        ImGui::SameLine();
        ImGui::Text(" | %zu groups", m_fileGroups.size());
    }

    if (s_fileView->hasSelection()) {
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        // Show selected file in normal color for visibility
        ImGui::PopStyleColor();
        ImGui::Text("Selected: %s", s_fileView->getSelectedPath().filename().string().c_str());
    } else {
        ImGui::PopStyleColor();
    }
}

void App::startScan(const std::filesystem::path& path, bool forceRescan) {
    if (m_isScanning) return;

    m_currentPath = path;
    m_pendingScanLocations.clear();
    m_scanLocationIndex = 0;

    // Get location info for recursive setting
    auto locations = m_database->getAllScanLocations();
    bool recursive = true;
    for (const auto& loc : locations) {
        if (loc.path == path) {
            recursive = loc.recursive;
            break;
        }
    }

    ScanLocation tempLoc;
    tempLoc.path = path;
    tempLoc.recursive = recursive;
    m_pendingScanLocations.push_back(tempLoc);

    m_isScanning = true;
    m_scanner->startScan(path, recursive);
}

void App::scanAllLocations() {
    if (m_isScanning) return;

    m_pendingScanLocations = m_database->getAllScanLocations();
    if (m_pendingScanLocations.empty()) {
        DEBUG_LOG("No scan locations configured");
        return;
    }

    m_scanLocationIndex = 0;
    m_isScanning = true;
    m_currentPath = m_pendingScanLocations[0].path;
    m_scanner->startScan(m_pendingScanLocations[0].path, m_pendingScanLocations[0].recursive);

    DEBUG_LOG("Starting scan of " << m_pendingScanLocations.size() << " locations");
}

void App::openInBlender(const std::filesystem::path& path) {
    std::string command = "blender \"" + path.string() + "\" &";
    std::system(command.c_str());
}

void App::openContainingFolder(const std::filesystem::path& path) {
    std::string command = "xdg-open \"" + path.parent_path().string() + "\" &";
    std::system(command.c_str());
}

void App::checkForNewFiles() {
    m_newFilesFound.clear();
    m_newFilesSelected.clear();

    // Get all scan locations
    auto locations = m_database->getAllScanLocations();

    // Get all files currently in database
    auto existingFiles = m_database->getAllFiles();
    std::set<std::filesystem::path> existingPaths;
    for (const auto& file : existingFiles) {
        existingPaths.insert(file.path);
    }

    // Scan each location for .blend files not in database
    for (const auto& location : locations) {
        if (!location.enabled) continue;
        if (!std::filesystem::exists(location.path)) continue;

        try {
            if (location.recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         location.path, std::filesystem::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file()) continue;

                    std::string ext = entry.path().extension().string();
                    // Check for .blend files (not backups like .blend1)
                    if (ext == ".blend") {
                        if (existingPaths.find(entry.path()) == existingPaths.end()) {
                            m_newFilesFound.push_back(entry.path());
                        }
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(
                         location.path, std::filesystem::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file()) continue;

                    std::string ext = entry.path().extension().string();
                    if (ext == ".blend") {
                        if (existingPaths.find(entry.path()) == existingPaths.end()) {
                            m_newFilesFound.push_back(entry.path());
                        }
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            DEBUG_LOG("Filesystem error scanning " << location.path << ": " << e.what());
        }
    }

    // Sort by filename
    std::sort(m_newFilesFound.begin(), m_newFilesFound.end(),
              [](const auto& a, const auto& b) { return a.filename() < b.filename(); });

    // Initialize all as selected (using 1 for true, 0 for false)
    m_newFilesSelected.resize(m_newFilesFound.size(), 1);

    m_showNewFilesDialog = true;
    DEBUG_LOG("Found " << m_newFilesFound.size() << " new files");
}

void App::renderNewFilesDialog() {
    if (!m_showNewFilesDialog) return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("New Files Found", &m_showNewFilesDialog, ImGuiWindowFlags_NoCollapse)) {
        if (m_newFilesFound.empty()) {
            ImGui::TextWrapped("No new .blend files found in your scan locations.");
            ImGui::TextDisabled("All files in your scan locations are already in the database.");
        } else {
            ImGui::Text("Found %zu new .blend file(s) in your scan locations:", m_newFilesFound.size());
            ImGui::Separator();

            // Select all / Deselect all buttons
            if (ImGui::Button("Select All")) {
                for (auto& sel : m_newFilesSelected) sel = 1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect All")) {
                for (auto& sel : m_newFilesSelected) sel = 0;
            }
            ImGui::SameLine();
            int selectedCount = std::count(m_newFilesSelected.begin(), m_newFilesSelected.end(), static_cast<char>(1));
            ImGui::Text("(%d selected)", selectedCount);

            ImGui::Separator();

            // Scrollable list of files
            ImGui::BeginChild("FileList", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), true);
            for (size_t i = 0; i < m_newFilesFound.size(); ++i) {
                const auto& path = m_newFilesFound[i];
                ImGui::PushID(static_cast<int>(i));

                bool selected = m_newFilesSelected[i] != 0;
                if (ImGui::Checkbox("##select", &selected)) {
                    m_newFilesSelected[i] = selected ? 1 : 0;
                }
                ImGui::SameLine();

                // Show filename in bold, path in dimmed
                ImGui::Text("%s", path.filename().string().c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", path.parent_path().string().c_str());

                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Separator();

            // Action buttons
            if (ImGui::Button("Scan Selected", ImVec2(120, 0))) {
                // Collect selected files and scan them
                std::vector<std::filesystem::path> toScan;
                for (size_t i = 0; i < m_newFilesFound.size(); ++i) {
                    if (m_newFilesSelected[i] != 0) {
                        toScan.push_back(m_newFilesFound[i]);
                    }
                }

                if (!toScan.empty()) {
                    // Parse and add each file to the database
                    BlendParser parser;
                    int addedCount = 0;
                    for (const auto& filePath : toScan) {
                        auto infoOpt = parser.parseQuick(filePath);
                        if (infoOpt) {
                            m_database->addOrUpdateFile(*infoOpt);
                            addedCount++;
                        }
                    }
                    loadFromDatabase();
                    DEBUG_LOG("Added " << addedCount << " new files to database");
                }

                m_showNewFilesDialog = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                m_showNewFilesDialog = false;
            }
        }
    }
    ImGui::End();
}

void App::startPreviewGeneration(bool forceRegenerate) {
    // Collect all primary files (not backups) from the database
    auto files = m_database->getAllFiles();
    std::vector<std::filesystem::path> primaryFiles;

    for (const auto& file : files) {
        // Skip backup files (.blend1, .blend2, etc.)
        std::string ext = file.path.extension().string();
        if (ext == ".blend") {
            primaryFiles.push_back(file.path);
        }
    }

    if (primaryFiles.empty()) {
        DEBUG_LOG("No files to generate previews for");
        return;
    }

    m_showPreviewGenerationDialog = true;

    // Start background generation with progress callback
    m_previewCache->startBatchGeneration(primaryFiles,
        [this](int current, int total, const std::string& filename) {
            m_currentPreviewFile = filename;
        },
        forceRegenerate);

    DEBUG_LOG("Started preview generation for " << primaryFiles.size() << " files");
}

void App::renderPreviewGenerationDialog() {
    if (!m_showPreviewGenerationDialog && !m_previewCache->isGenerating()) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(450, 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool isGenerating = m_previewCache->isGenerating();

    if (ImGui::Begin("Generating Rotation Previews", &m_showPreviewGenerationDialog,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

        if (isGenerating) {
            auto [current, total] = m_previewCache->getProgress();
            float progress = total > 0 ? static_cast<float>(current) / total : 0.0f;

            ImGui::Text("Generating preview %d of %d...", current + 1, total);
            ImGui::ProgressBar(progress, ImVec2(-1, 0));

            if (!m_currentPreviewFile.empty()) {
                ImGui::TextDisabled("Current: %s", m_currentPreviewFile.c_str());
            }

            ImGui::Spacing();
            ImGui::TextWrapped("This runs Blender in the background to render rotation frames. This may take a while.");

            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                m_previewCache->cancelGeneration();
            }
        } else {
            auto [current, total] = m_previewCache->getProgress();
            ImGui::Text("Preview generation complete!");
            ImGui::Text("Generated previews for %d files.", total);

            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showPreviewGenerationDialog = false;
            }
        }
    }
    ImGui::End();

    // Auto-close dialog if cancelled or closed while not generating
    if (!isGenerating && !m_showPreviewGenerationDialog) {
        m_showPreviewGenerationDialog = false;
    }
}

void App::renderUserGuide() {
    if (!m_showUserGuide) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("User Guide", &m_showUserGuide, ImGuiWindowFlags_NoCollapse)) {

        if (ImGui::CollapsingHeader("Getting Started", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped(
                "Blender File Finder helps you browse, organize, and manage your .blend files "
                "with thumbnail previews and tagging support."
            );
            ImGui::Spacing();
            ImGui::BulletText("Add scan locations in the sidebar to index your .blend files");
            ImGui::BulletText("Click 'Add Location' and enter a folder path");
            ImGui::BulletText("Enable 'Recursive' to include subfolders");
            ImGui::BulletText("Files are automatically scanned and thumbnails extracted");
        }

        if (ImGui::CollapsingHeader("Browsing Files")) {
            ImGui::BulletText("Use the search bar to filter files by name");
            ImGui::BulletText("Click on a file to select it");
            ImGui::BulletText("Double-click to open in Blender");
            ImGui::BulletText("Right-click for context menu options");
            ImGui::Spacing();
            ImGui::TextDisabled("View Options:");
            ImGui::BulletText("Toggle between Grid and List view (View menu)");
            ImGui::BulletText("Adjust thumbnail size with the slider in the toolbar");
        }

        if (ImGui::CollapsingHeader("Tags")) {
            ImGui::TextWrapped(
                "Tags help you organize files into custom categories."
            );
            ImGui::Spacing();
            ImGui::BulletText("Right-click a file and select 'Add Tag' to tag it");
            ImGui::BulletText("Click on a tag in the sidebar to filter by that tag");
            ImGui::BulletText("Tags are saved automatically and persist across sessions");
            ImGui::BulletText("Search also matches tag names");
        }

        if (ImGui::CollapsingHeader("Version Grouping")) {
            ImGui::TextWrapped(
                "Files with version patterns (e.g., model_v01.blend, model_v02.blend) "
                "are automatically grouped together."
            );
            ImGui::Spacing();
            ImGui::BulletText("The latest version is shown as the primary file");
            ImGui::BulletText("Older versions and backups (.blend1, .blend2) are grouped beneath");
            ImGui::BulletText("Click the expand arrow to see all versions");
        }

        if (ImGui::CollapsingHeader("Rotation Previews")) {
            ImGui::TextWrapped(
                "Generate animated turntable previews that play when you hover over a file."
            );
            ImGui::Spacing();
            ImGui::BulletText("Go to File > Generate New Previews (skips existing)");
            ImGui::BulletText("Features the 5 largest objects in each scene");
            ImGui::BulletText("Each object is shown individually, fit to frame");
            ImGui::BulletText("Camera rotates around each object in sequence");
            ImGui::BulletText("Requires Blender to be installed and in your PATH");
            ImGui::BulletText("Previews are cached in ~/.cache/BlenderFileFinder/");
            ImGui::Spacing();
            ImGui::TextDisabled("Note: Preview generation can take several seconds per file.");
        }

        if (ImGui::CollapsingHeader("Keyboard Shortcuts")) {
            ImGui::Columns(2, "shortcuts", false);
            ImGui::SetColumnWidth(0, 150);

            ImGui::Text("F1"); ImGui::NextColumn(); ImGui::Text("Open User Guide"); ImGui::NextColumn();
            ImGui::Text("F5"); ImGui::NextColumn(); ImGui::Text("Refresh from Database"); ImGui::NextColumn();
            ImGui::Text("Ctrl+N"); ImGui::NextColumn(); ImGui::Text("Check for New Files"); ImGui::NextColumn();
            ImGui::Text("Ctrl+Shift+R"); ImGui::NextColumn(); ImGui::Text("Scan All Locations"); ImGui::NextColumn();

            ImGui::Columns(1);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_showUserGuide = false;
        }
    }
    ImGui::End();
}

void App::renderStatisticsDialog() {
    if (!m_showStatisticsDialog) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Database Statistics", &m_showStatisticsDialog, ImGuiWindowFlags_NoCollapse)) {

        // Database info
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "DATABASE");
        ImGui::Separator();

        ImGui::Text("Location:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", m_database->getDatabasePath().string().c_str());

        // Get database file size
        if (std::filesystem::exists(m_database->getDatabasePath())) {
            auto fileSize = std::filesystem::file_size(m_database->getDatabasePath());
            std::string sizeStr;
            if (fileSize < 1024) {
                sizeStr = std::to_string(fileSize) + " B";
            } else if (fileSize < 1024 * 1024) {
                sizeStr = std::to_string(fileSize / 1024) + " KB";
            } else {
                sizeStr = std::to_string(fileSize / (1024 * 1024)) + " MB";
            }
            ImGui::Text("Size:");
            ImGui::SameLine();
            ImGui::Text("%s", sizeStr.c_str());
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Content statistics
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "CONTENT");
        ImGui::Separator();

        int fileCount = m_database->getTotalFileCount();
        int tagCount = m_database->getTotalTagCount();
        int locationCount = m_database->getTotalScanLocationCount();

        ImGui::Columns(2, "stats", false);
        ImGui::SetColumnWidth(0, 180);

        ImGui::Text("Scan Locations:");
        ImGui::NextColumn();
        ImGui::Text("%d", locationCount);
        ImGui::NextColumn();

        ImGui::Text("Total Files:");
        ImGui::NextColumn();
        ImGui::Text("%d", fileCount);
        ImGui::NextColumn();

        ImGui::Text("Unique Tags:");
        ImGui::NextColumn();
        ImGui::Text("%d", tagCount);
        ImGui::NextColumn();

        ImGui::Text("File Groups:");
        ImGui::NextColumn();
        ImGui::Text("%zu", m_fileGroups.size());
        ImGui::NextColumn();

        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Spacing();

        // File breakdown by location
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FILES BY LOCATION");
        ImGui::Separator();

        auto locations = m_database->getAllScanLocations();
        if (locations.empty()) {
            ImGui::TextDisabled("No scan locations configured.");
        } else {
            ImGui::BeginChild("LocationStats", ImVec2(0, 100), true);
            for (const auto& loc : locations) {
                auto files = m_database->getFilesByScanLocation(loc.id);
                std::string displayName = loc.name.empty() ? loc.path.filename().string() : loc.name;
                ImGui::Text("%s:", displayName.c_str());
                ImGui::SameLine(200);
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%zu files", files.size());
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Cache info
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "CACHE");
        ImGui::Separator();

        auto cacheDir = m_previewCache->getCacheDir();
        size_t previewCount = 0;
        size_t cacheSize = 0;
        if (std::filesystem::exists(cacheDir)) {
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(cacheDir)) {
                    if (entry.is_regular_file()) {
                        cacheSize += entry.file_size();
                        if (entry.path().extension() == ".png") {
                            previewCount++;
                        }
                    }
                }
            } catch (...) {}
        }

        ImGui::Columns(2, "cache_stats", false);
        ImGui::SetColumnWidth(0, 180);

        ImGui::Text("Preview Frames:");
        ImGui::NextColumn();
        ImGui::Text("%zu", previewCount);
        ImGui::NextColumn();

        ImGui::Text("Cache Size:");
        ImGui::NextColumn();
        std::string cacheSizeStr;
        if (cacheSize < 1024) {
            cacheSizeStr = std::to_string(cacheSize) + " B";
        } else if (cacheSize < 1024 * 1024) {
            cacheSizeStr = std::to_string(cacheSize / 1024) + " KB";
        } else {
            cacheSizeStr = std::to_string(cacheSize / (1024 * 1024)) + " MB";
        }
        ImGui::Text("%s", cacheSizeStr.c_str());
        ImGui::NextColumn();

        ImGui::Columns(1);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            m_showStatisticsDialog = false;
        }
    }
    ImGui::End();
}

void App::renderBulkTagDialog() {
    if (!m_showBulkTagDialog) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(550, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Bulk Add Tags", &m_showBulkTagDialog, ImGuiWindowFlags_NoCollapse)) {

        ImGui::TextWrapped("Add a tag to all .blend files in a scanned folder.");
        ImGui::Spacing();

        // Section 1: Select folder
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "1. SELECT FOLDER");
        ImGui::Separator();

        auto locations = m_database->getAllScanLocations();
        if (locations.empty()) {
            ImGui::TextDisabled("No scan locations configured. Add folders first.");
        } else {
            ImGui::BeginChild("FolderList", ImVec2(0, 120), true);

            for (size_t i = 0; i < locations.size(); ++i) {
                const auto& loc = locations[i];
                std::string displayName = loc.name.empty() ? loc.path.filename().string() : loc.name;

                // Get file count for this location
                auto files = m_database->getFilesByScanLocation(loc.id);
                size_t fileCount = files.size();

                // Format: "FolderName (123 files)"
                char label[256];
                snprintf(label, sizeof(label), "%s (%zu files)", displayName.c_str(), fileCount);

                bool isSelected = (m_bulkTagSelectedLocation == static_cast<int>(i));
                if (ImGui::Selectable(label, isSelected)) {
                    m_bulkTagSelectedLocation = static_cast<int>(i);
                    // Update preview files
                    m_bulkTagPreviewFiles = m_database->getFilesByScanLocation(loc.id);
                }

                // Show full path on hover
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", loc.path.string().c_str());
                }
            }

            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Section 2: Enter tag name
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "2. ENTER TAG NAME");
        ImGui::Separator();

        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##tagname", m_bulkTagName, sizeof(m_bulkTagName));

        // Show existing tags as suggestions
        if (strlen(m_bulkTagName) > 0) {
            auto allTags = m_database->getAllTags();
            std::vector<std::string> matchingTags;
            for (const auto& tag : allTags) {
                // Case-insensitive partial match
                std::string lowerTag = tag;
                std::string lowerInput = m_bulkTagName;
                std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
                std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);
                if (lowerTag.find(lowerInput) != std::string::npos) {
                    matchingTags.push_back(tag);
                }
            }

            if (!matchingTags.empty() && matchingTags.size() <= 5) {
                ImGui::SameLine();
                ImGui::TextDisabled("Existing:");
                for (const auto& tag : matchingTags) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tag.c_str())) {
                        strncpy(m_bulkTagName, tag.c_str(), sizeof(m_bulkTagName) - 1);
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // Section 3: Preview
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "3. PREVIEW");
        ImGui::Separator();

        if (m_bulkTagSelectedLocation >= 0 && m_bulkTagSelectedLocation < static_cast<int>(locations.size())) {
            const auto& selectedLoc = locations[m_bulkTagSelectedLocation];
            std::string displayName = selectedLoc.name.empty() ? selectedLoc.path.filename().string() : selectedLoc.name;

            ImGui::Text("Folder: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.9f, 1.0f), "%s", displayName.c_str());

            ImGui::Text("Files to tag: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%zu", m_bulkTagPreviewFiles.size());

            if (strlen(m_bulkTagName) > 0) {
                ImGui::Text("Tag to apply: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "\"%s\"", m_bulkTagName);
            }

            // Show file list preview
            if (!m_bulkTagPreviewFiles.empty()) {
                ImGui::BeginChild("FilePreview", ImVec2(0, 80), true);
                int shown = 0;
                for (const auto& file : m_bulkTagPreviewFiles) {
                    if (shown >= 10) {
                        ImGui::TextDisabled("... and %zu more files", m_bulkTagPreviewFiles.size() - 10);
                        break;
                    }
                    ImGui::TextDisabled("  %s", file.path.filename().string().c_str());
                    shown++;
                }
                ImGui::EndChild();
            }
        } else {
            ImGui::TextDisabled("Select a folder above to see preview.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action buttons
        bool canApply = (m_bulkTagSelectedLocation >= 0 &&
                        m_bulkTagSelectedLocation < static_cast<int>(locations.size()) &&
                        strlen(m_bulkTagName) > 0 &&
                        !m_bulkTagPreviewFiles.empty());

        if (!canApply) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Apply Tag", ImVec2(120, 0))) {
            // Apply the tag to all files
            std::string tagName = m_bulkTagName;
            int taggedCount = 0;

            for (const auto& file : m_bulkTagPreviewFiles) {
                // Only tag .blend files (not backups)
                if (file.path.extension() == ".blend") {
                    m_database->addTagToFile(file.path, tagName);
                    taggedCount++;
                }
            }

            DEBUG_LOG("Bulk tagged " << taggedCount << " files with '" << tagName << "'");

            // Reset and close
            m_showBulkTagDialog = false;
            m_bulkTagSelectedLocation = -1;
            m_bulkTagName[0] = '\0';
            m_bulkTagPreviewFiles.clear();

            // Force refresh of tag cache
            m_tagsUpdateFrame = -1000;
        }

        if (!canApply) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_showBulkTagDialog = false;
            m_bulkTagSelectedLocation = -1;
            m_bulkTagName[0] = '\0';
            m_bulkTagPreviewFiles.clear();
        }
    }
    ImGui::End();
}

void App::renderPreloadDialog() {
    if (!m_showPreloadDialog && !m_isPreloadingPreviews) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(450, 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Loading Preview Thumbnails", &m_showPreloadDialog,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

        if (m_isPreloadingPreviews) {
            float progress = m_preloadTotalCount > 0 ?
                static_cast<float>(m_preloadCurrentIndex) / m_preloadTotalCount : 0.0f;

            ImGui::Text("Loading preview %zu of %zu...",
                       m_preloadCurrentIndex + 1, m_preloadTotalCount);
            ImGui::ProgressBar(progress, ImVec2(-1, 0));

            if (!m_preloadCurrentFile.empty()) {
                ImGui::TextDisabled("Current: %s", m_preloadCurrentFile.c_str());
            }

            ImGui::Spacing();
            ImGui::TextWrapped("Loading animated preview thumbnails into memory for faster browsing.");

            ImGui::Spacing();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                m_preloadCancelRequested = true;
            }
        } else {
            ImGui::Text("Preview loading complete!");
            ImGui::Text("Loaded %zu preview thumbnails.", m_preloadTotalCount);

            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                m_showPreloadDialog = false;
            }
        }
    }
    ImGui::End();

    // Handle closing dialog while not loading
    if (!m_isPreloadingPreviews && !m_showPreloadDialog) {
        m_showPreloadDialog = false;
    }
}

void App::setWindowIcon() {
    // Search for icon in common locations
    std::vector<std::filesystem::path> searchPaths = {
        // Installed location
        std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") /
            ".local/share/icons/hicolor/256x256/apps/blender-file-finder.png",
        // Development location (relative to executable)
        "../resources/icons/blender-file-finder-256.png",
        "resources/icons/blender-file-finder-256.png",
        // Development location (relative to source)
        std::filesystem::path(__FILE__).parent_path().parent_path() /
            "resources/icons/blender-file-finder-256.png",
    };

    std::string iconPath;
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            iconPath = path.string();
            break;
        }
    }

    if (iconPath.empty()) {
        DEBUG_LOG("Window icon not found");
        return;
    }

    // Load icon using stb_image
    int width, height, channels;
    unsigned char* pixels = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        DEBUG_LOG("Failed to load window icon: " << iconPath);
        return;
    }

    // Set window icon
    GLFWimage icon;
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;
    glfwSetWindowIcon(m_window, 1, &icon);

    stbi_image_free(pixels);
    DEBUG_LOG("Window icon set: " << iconPath);
}

} // namespace BlenderFileFinder
