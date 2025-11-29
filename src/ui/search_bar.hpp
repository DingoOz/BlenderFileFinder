/**
 * @file search_bar.hpp
 * @brief Search input widget for filtering files.
 */

#pragma once

#include <string>
#include <functional>

namespace BlenderFileFinder {

/**
 * @brief Search bar widget for filtering the file list.
 *
 * Provides a text input field with a clear button for searching
 * files by name or tags. The search is performed as the user types.
 */
class SearchBar {
public:
    /**
     * @brief Callback type for search query changes.
     * @param query The new search query string
     */
    using SearchCallback = std::function<void(const std::string&)>;

    SearchBar() = default;

    /**
     * @brief Render the search bar widget.
     *
     * Displays the search input field and clear button.
     * Must be called within an ImGui context.
     */
    void render();

    /**
     * @brief Get the current search query.
     * @return Current query string
     */
    const std::string& getQuery() const { return m_query; }

    /**
     * @brief Set the search query programmatically.
     * @param query New query string
     */
    void setQuery(const std::string& query) { m_query = query; }

    /**
     * @brief Clear the search query.
     */
    void clear() { m_query.clear(); }

    /**
     * @brief Set callback for query changes.
     *
     * Called whenever the user modifies the search text.
     *
     * @param callback Function to call with new query
     */
    void setSearchCallback(SearchCallback callback) { m_searchCallback = std::move(callback); }

private:
    std::string m_query;                        ///< Current search query
    char m_inputBuffer[256] = {0};              ///< Input buffer
    SearchCallback m_searchCallback;            ///< Query change callback
};

} // namespace BlenderFileFinder
