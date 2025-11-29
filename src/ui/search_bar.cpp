#include "search_bar.hpp"
#include "imgui.h"
#include <cstring>

namespace BlenderFileFinder {

void SearchBar::render() {
    ImGui::PushItemWidth(-1);

    // Copy current query to buffer if different
    if (m_query != m_inputBuffer) {
        strncpy(m_inputBuffer, m_query.c_str(), sizeof(m_inputBuffer) - 1);
    }

    bool changed = ImGui::InputTextWithHint(
        "##search",
        "Search files...",
        m_inputBuffer,
        sizeof(m_inputBuffer),
        ImGuiInputTextFlags_EscapeClearsAll
    );

    if (changed) {
        m_query = m_inputBuffer;
        if (m_searchCallback) {
            m_searchCallback(m_query);
        }
    }

    // Clear button
    ImGui::SameLine();
    if (ImGui::Button("X") && !m_query.empty()) {
        clear();
        m_inputBuffer[0] = '\0';
        if (m_searchCallback) {
            m_searchCallback(m_query);
        }
    }

    ImGui::PopItemWidth();
}

} // namespace BlenderFileFinder
