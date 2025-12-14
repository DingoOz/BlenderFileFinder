#include "database.hpp"
#include "debug.hpp"
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace BlenderFileFinder {

// Helper to safely get text from SQLite column (returns empty string if NULL)
static inline std::string safeColumnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

Database::Database() = default;

Database::~Database() {
    close();
}

bool Database::open(const std::filesystem::path& dbPath) {
    if (m_db) {
        close();
    }

    m_dbPath = dbPath;

    // Create parent directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);

    int rc = sqlite3_open(dbPath.string().c_str(), &m_db);
    if (rc != SQLITE_OK) {
        DEBUG_LOG("Failed to open database: " << sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Enable foreign keys
    execute("PRAGMA foreign_keys = ON;");

    // Create tables if they don't exist
    createTables();

    DEBUG_LOG("Database opened: " << dbPath);
    return true;
}

void Database::close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        DEBUG_LOG("Database closed");
    }
}

void Database::createTables() {
    // Scan locations table
    execute(R"(
        CREATE TABLE IF NOT EXISTS scan_locations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            recursive INTEGER DEFAULT 1,
            enabled INTEGER DEFAULT 1,
            name TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )");

    // Files table
    execute(R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path TEXT UNIQUE NOT NULL,
            filename TEXT NOT NULL,
            file_size INTEGER,
            modified_time INTEGER,
            blender_version TEXT,
            is_compressed INTEGER DEFAULT 0,
            object_count INTEGER DEFAULT 0,
            mesh_count INTEGER DEFAULT 0,
            material_count INTEGER DEFAULT 0,
            scan_location_id INTEGER,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (scan_location_id) REFERENCES scan_locations(id) ON DELETE SET NULL
        );
    )");

    // Tags table
    execute(R"(
        CREATE TABLE IF NOT EXISTS tags (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )");

    // File-Tag junction table
    execute(R"(
        CREATE TABLE IF NOT EXISTS file_tags (
            file_id INTEGER NOT NULL,
            tag_id INTEGER NOT NULL,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (file_id, tag_id),
            FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE,
            FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
        );
    )");

    // Create indexes for performance
    execute("CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);");
    execute("CREATE INDEX IF NOT EXISTS idx_files_scan_location ON files(scan_location_id);");
    execute("CREATE INDEX IF NOT EXISTS idx_tags_name ON tags(name);");
}

bool Database::execute(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        DEBUG_LOG("SQL error: " << errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

void Database::beginTransaction() {
    execute("BEGIN TRANSACTION;");
}

void Database::commitTransaction() {
    execute("COMMIT;");
}

void Database::rollbackTransaction() {
    execute("ROLLBACK;");
}

// === Scan Locations ===

int64_t Database::addScanLocation(const std::filesystem::path& path, bool recursive, const std::string& name) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO scan_locations (path, recursive, name) VALUES (?, ?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    std::string pathStr = path.string();
    sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, recursive ? 1 : 0);
    sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return -1;
    }

    return sqlite3_last_insert_rowid(m_db);
}

void Database::removeScanLocation(int64_t id) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM scan_locations WHERE id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::updateScanLocation(const ScanLocation& location) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE scan_locations SET path = ?, recursive = ?, enabled = ?, name = ? WHERE id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = location.path.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, location.recursive ? 1 : 0);
        sqlite3_bind_int(stmt, 3, location.enabled ? 1 : 0);
        sqlite3_bind_text(stmt, 4, location.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, location.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<ScanLocation> Database::getAllScanLocations() {
    auto startTime = std::chrono::steady_clock::now();
    std::vector<ScanLocation> result;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, path, recursive, enabled, name FROM scan_locations ORDER BY name, path;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ScanLocation loc;
            loc.id = sqlite3_column_int64(stmt, 0);
            loc.path = safeColumnText(stmt, 1);
            loc.recursive = sqlite3_column_int(stmt, 2) != 0;
            loc.enabled = sqlite3_column_int(stmt, 3) != 0;
            loc.name = safeColumnText(stmt, 4);
            result.push_back(loc);
        }
        sqlite3_finalize(stmt);
    }

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    if (totalMs > 10) {
        DEBUG_LOG("Database::getAllScanLocations() took " << totalMs << "ms, returned " << result.size() << " locations");
    }

    return result;
}

std::optional<ScanLocation> Database::getScanLocation(int64_t id) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, path, recursive, enabled, name FROM scan_locations WHERE id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            ScanLocation loc;
            loc.id = sqlite3_column_int64(stmt, 0);
            loc.path = safeColumnText(stmt, 1);
            loc.recursive = sqlite3_column_int(stmt, 2) != 0;
            loc.enabled = sqlite3_column_int(stmt, 3) != 0;
            loc.name = safeColumnText(stmt, 4);
            sqlite3_finalize(stmt);
            return loc;
        }
        sqlite3_finalize(stmt);
    }

    return std::nullopt;
}

// === Files ===

int64_t Database::addOrUpdateFile(const BlendFileInfo& file, int64_t scanLocationId) {
    sqlite3_stmt* stmt;
    const char* sql = R"(
        INSERT INTO files (path, filename, file_size, modified_time, blender_version,
                          is_compressed, object_count, mesh_count, material_count, scan_location_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            filename = excluded.filename,
            file_size = excluded.file_size,
            modified_time = excluded.modified_time,
            blender_version = excluded.blender_version,
            is_compressed = excluded.is_compressed,
            object_count = excluded.object_count,
            mesh_count = excluded.mesh_count,
            material_count = excluded.material_count,
            scan_location_id = excluded.scan_location_id,
            updated_at = CURRENT_TIMESTAMP;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    std::string pathStr = file.path.string();
    sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, file.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(file.fileSize));
    sqlite3_bind_int64(stmt, 4, file.modifiedTime.time_since_epoch().count());
    sqlite3_bind_text(stmt, 5, file.metadata.blenderVersion.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, file.metadata.isCompressed ? 1 : 0);
    sqlite3_bind_int(stmt, 7, file.metadata.objectCount);
    sqlite3_bind_int(stmt, 8, file.metadata.meshCount);
    sqlite3_bind_int(stmt, 9, file.metadata.materialCount);
    if (scanLocationId > 0) {
        sqlite3_bind_int64(stmt, 10, scanLocationId);
    } else {
        sqlite3_bind_null(stmt, 10);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return -1;
    }

    return sqlite3_last_insert_rowid(m_db);
}

void Database::removeFile(int64_t fileId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM files WHERE id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, fileId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::removeFileByPath(const std::filesystem::path& path) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM files WHERE path = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = path.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int64_t Database::getFileId(const std::filesystem::path& path) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM files WHERE path = ?;";
    int64_t fileId = -1;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = path.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            fileId = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return fileId;
}

std::optional<BlendFileInfo> Database::getFileByPath(const std::filesystem::path& path) {
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT path, filename, file_size, modified_time, blender_version,
               is_compressed, object_count, mesh_count, material_count
        FROM files WHERE path = ?;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = path.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            BlendFileInfo file;
            file.path = safeColumnText(stmt, 0);
            file.filename = safeColumnText(stmt, 1);
            file.fileSize = static_cast<uintmax_t>(sqlite3_column_int64(stmt, 2));
            auto duration = std::filesystem::file_time_type::duration(sqlite3_column_int64(stmt, 3));
            file.modifiedTime = std::filesystem::file_time_type(duration);
            file.metadata.blenderVersion = safeColumnText(stmt, 4);
            file.metadata.isCompressed = sqlite3_column_int(stmt, 5) != 0;
            file.metadata.objectCount = sqlite3_column_int(stmt, 6);
            file.metadata.meshCount = sqlite3_column_int(stmt, 7);
            file.metadata.materialCount = sqlite3_column_int(stmt, 8);

            sqlite3_finalize(stmt);
            return file;
        }
        sqlite3_finalize(stmt);
    }

    return std::nullopt;
}

std::vector<BlendFileInfo> Database::getAllFiles() {
    DEBUG_LOG("Database::getAllFiles() starting");
    auto startTime = std::chrono::steady_clock::now();

    std::vector<BlendFileInfo> result;
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT path, filename, file_size, modified_time, blender_version,
               is_compressed, object_count, mesh_count, material_count
        FROM files ORDER BY filename;
    )";

    auto prepareStart = std::chrono::steady_clock::now();
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        auto prepareMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - prepareStart).count();
        DEBUG_LOG("Database::getAllFiles() prepare took " << prepareMs << "ms");

        auto fetchStart = std::chrono::steady_clock::now();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BlendFileInfo file;
            file.path = safeColumnText(stmt, 0);
            file.filename = safeColumnText(stmt, 1);
            file.fileSize = static_cast<uintmax_t>(sqlite3_column_int64(stmt, 2));
            auto duration = std::filesystem::file_time_type::duration(sqlite3_column_int64(stmt, 3));
            file.modifiedTime = std::filesystem::file_time_type(duration);
            file.metadata.blenderVersion = safeColumnText(stmt, 4);
            file.metadata.isCompressed = sqlite3_column_int(stmt, 5) != 0;
            file.metadata.objectCount = sqlite3_column_int(stmt, 6);
            file.metadata.meshCount = sqlite3_column_int(stmt, 7);
            file.metadata.materialCount = sqlite3_column_int(stmt, 8);
            result.push_back(file);
        }
        auto fetchMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - fetchStart).count();
        DEBUG_LOG("Database::getAllFiles() fetch loop took " << fetchMs << "ms for " << result.size() << " files");
        sqlite3_finalize(stmt);
    } else {
        DEBUG_LOG("Database::getAllFiles() prepare FAILED: " << sqlite3_errmsg(m_db));
    }

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    DEBUG_LOG("Database::getAllFiles() completed in " << totalMs << "ms, returned " << result.size() << " files");

    return result;
}

std::vector<BlendFileInfo> Database::getFilesByScanLocation(int64_t scanLocationId) {
    std::vector<BlendFileInfo> result;
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT path, filename, file_size, modified_time, blender_version,
               is_compressed, object_count, mesh_count, material_count
        FROM files WHERE scan_location_id = ? ORDER BY filename;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, scanLocationId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BlendFileInfo file;
            file.path = safeColumnText(stmt, 0);
            file.filename = safeColumnText(stmt, 1);
            file.fileSize = static_cast<uintmax_t>(sqlite3_column_int64(stmt, 2));
            auto duration = std::filesystem::file_time_type::duration(sqlite3_column_int64(stmt, 3));
            file.modifiedTime = std::filesystem::file_time_type(duration);
            file.metadata.blenderVersion = safeColumnText(stmt, 4);
            file.metadata.isCompressed = sqlite3_column_int(stmt, 5) != 0;
            file.metadata.objectCount = sqlite3_column_int(stmt, 6);
            file.metadata.meshCount = sqlite3_column_int(stmt, 7);
            file.metadata.materialCount = sqlite3_column_int(stmt, 8);
            result.push_back(file);
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

std::vector<BlendFileInfo> Database::searchFiles(const std::string& query) {
    std::vector<BlendFileInfo> result;
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT path, filename, file_size, modified_time, blender_version,
               is_compressed, object_count, mesh_count, material_count
        FROM files WHERE filename LIKE ? ORDER BY filename;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BlendFileInfo file;
            file.path = safeColumnText(stmt, 0);
            file.filename = safeColumnText(stmt, 1);
            file.fileSize = static_cast<uintmax_t>(sqlite3_column_int64(stmt, 2));
            auto duration = std::filesystem::file_time_type::duration(sqlite3_column_int64(stmt, 3));
            file.modifiedTime = std::filesystem::file_time_type(duration);
            file.metadata.blenderVersion = safeColumnText(stmt, 4);
            file.metadata.isCompressed = sqlite3_column_int(stmt, 5) != 0;
            file.metadata.objectCount = sqlite3_column_int(stmt, 6);
            file.metadata.meshCount = sqlite3_column_int(stmt, 7);
            file.metadata.materialCount = sqlite3_column_int(stmt, 8);
            result.push_back(file);
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

bool Database::isFileUpToDate(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return false;
    }

    sqlite3_stmt* stmt;
    const char* sql = "SELECT modified_time FROM files WHERE path = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = path.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t storedTime = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);

            std::error_code ec;
            auto currentTime = std::filesystem::last_write_time(path, ec);
            if (ec) return false;

            return storedTime == currentTime.time_since_epoch().count();
        }
        sqlite3_finalize(stmt);
    }

    return false;
}

int Database::cleanupMissingFiles() {
    std::vector<std::string> pathsToRemove;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT path FROM files;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string path = safeColumnText(stmt, 0);
            if (!path.empty() && !std::filesystem::exists(path)) {
                pathsToRemove.push_back(path);
            }
        }
        sqlite3_finalize(stmt);
    }

    for (const auto& path : pathsToRemove) {
        removeFileByPath(path);
    }

    return static_cast<int>(pathsToRemove.size());
}

// === Tags ===

int64_t Database::addTag(const std::string& tagName) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR IGNORE INTO tags (name) VALUES (?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return getTagId(tagName);
}

void Database::removeTag(int64_t tagId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM tags WHERE id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, tagId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::removeTagByName(const std::string& tagName) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM tags WHERE name = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<std::string> Database::getAllTags() {
    std::vector<std::string> result;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT name FROM tags ORDER BY name;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string tag = safeColumnText(stmt, 0);
            if (!tag.empty()) {
                result.emplace_back(std::move(tag));
            }
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

int64_t Database::getTagId(const std::string& tagName) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM tags WHERE name = ?;";
    int64_t tagId = -1;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            tagId = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return tagId;
}

void Database::addTagToFile(int64_t fileId, int64_t tagId) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR IGNORE INTO file_tags (file_id, tag_id) VALUES (?, ?);";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, fileId);
        sqlite3_bind_int64(stmt, 2, tagId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::addTagToFile(const std::filesystem::path& filePath, const std::string& tagName) {
    int64_t fileId = getFileId(filePath);
    if (fileId < 0) return;

    int64_t tagId = addTag(tagName);  // Creates tag if it doesn't exist
    if (tagId < 0) return;

    addTagToFile(fileId, tagId);
}

void Database::removeTagFromFile(int64_t fileId, int64_t tagId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM file_tags WHERE file_id = ? AND tag_id = ?;";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, fileId);
        sqlite3_bind_int64(stmt, 2, tagId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::removeTagFromFile(const std::filesystem::path& filePath, const std::string& tagName) {
    int64_t fileId = getFileId(filePath);
    if (fileId < 0) return;

    int64_t tagId = getTagId(tagName);
    if (tagId < 0) return;

    removeTagFromFile(fileId, tagId);
}

std::vector<std::string> Database::getTagsForFile(const std::filesystem::path& filePath) {
    static int slowQueryCount = 0;
    auto startTime = std::chrono::steady_clock::now();

    std::vector<std::string> result;
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT t.name FROM tags t
        INNER JOIN file_tags ft ON t.id = ft.tag_id
        INNER JOIN files f ON f.id = ft.file_id
        WHERE f.path = ?
        ORDER BY t.name;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = filePath.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string tag = safeColumnText(stmt, 0);
            if (!tag.empty()) {
                result.emplace_back(std::move(tag));
            }
        }
        sqlite3_finalize(stmt);
    }

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    if (totalMs > 50 && slowQueryCount < 10) {
        DEBUG_LOG("SLOW getTagsForFile(" << filePath.filename() << ") took " << totalMs << "ms");
        slowQueryCount++;
    }

    return result;
}

std::vector<BlendFileInfo> Database::getFilesWithTag(const std::string& tagName) {
    std::vector<BlendFileInfo> result;
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT f.path, f.filename, f.file_size, f.modified_time, f.blender_version,
               f.is_compressed, f.object_count, f.mesh_count, f.material_count
        FROM files f
        INNER JOIN file_tags ft ON f.id = ft.file_id
        INNER JOIN tags t ON t.id = ft.tag_id
        WHERE t.name = ?
        ORDER BY f.filename;
    )";

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, tagName.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BlendFileInfo file;
            file.path = safeColumnText(stmt, 0);
            file.filename = safeColumnText(stmt, 1);
            file.fileSize = static_cast<uintmax_t>(sqlite3_column_int64(stmt, 2));
            auto duration = std::filesystem::file_time_type::duration(sqlite3_column_int64(stmt, 3));
            file.modifiedTime = std::filesystem::file_time_type(duration);
            file.metadata.blenderVersion = safeColumnText(stmt, 4);
            file.metadata.isCompressed = sqlite3_column_int(stmt, 5) != 0;
            file.metadata.objectCount = sqlite3_column_int(stmt, 6);
            file.metadata.meshCount = sqlite3_column_int(stmt, 7);
            file.metadata.materialCount = sqlite3_column_int(stmt, 8);
            result.push_back(file);
        }
        sqlite3_finalize(stmt);
    }

    return result;
}

bool Database::fileHasTag(const std::filesystem::path& filePath, const std::string& tagName) {
    sqlite3_stmt* stmt;
    const char* sql = R"(
        SELECT 1 FROM file_tags ft
        INNER JOIN files f ON f.id = ft.file_id
        INNER JOIN tags t ON t.id = ft.tag_id
        WHERE f.path = ? AND t.name = ?;
    )";

    bool hasTag = false;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pathStr = filePath.string();
        sqlite3_bind_text(stmt, 1, pathStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, tagName.c_str(), -1, SQLITE_TRANSIENT);

        hasTag = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }

    return hasTag;
}

// === Statistics ===

int Database::getTotalFileCount() {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM files;";
    int count = 0;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

int Database::getTotalTagCount() {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM tags;";
    int count = 0;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

int Database::getTotalScanLocationCount() {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM scan_locations;";
    int count = 0;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    return count;
}

} // namespace BlenderFileFinder
