/**
 * @file blend_parser.hpp
 * @brief Parser for Blender .blend files to extract thumbnails and metadata.
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace BlenderFileFinder {

/**
 * @brief Thumbnail image extracted from a .blend file.
 *
 * Blender stores preview thumbnails in the TEST block of .blend files.
 * The pixels are stored in RGBA format (4 bytes per pixel).
 */
struct BlendThumbnail {
    int width = 0;                  ///< Width of the thumbnail in pixels
    int height = 0;                 ///< Height of the thumbnail in pixels
    std::vector<uint8_t> pixels;    ///< Pixel data in RGBA format
};

/**
 * @brief Metadata extracted from a .blend file.
 *
 * Contains information about the Blender version used to create the file
 * and counts of various data blocks within the file.
 */
struct BlendMetadata {
    std::string blenderVersion;     ///< Blender version string (e.g., "4.0")
    int meshCount = 0;              ///< Number of mesh objects
    int objectCount = 0;            ///< Total number of objects
    int materialCount = 0;          ///< Number of materials
    int textureCount = 0;           ///< Number of textures
    int64_t totalVertices = 0;      ///< Total vertex count across all meshes
    int64_t totalFaces = 0;         ///< Total face count across all meshes
    int64_t totalEdges = 0;         ///< Total edge count across all meshes
    bool isCompressed = false;      ///< True if the file is gzip compressed
};

/**
 * @brief Complete information about a .blend file.
 *
 * Combines file system information with parsed metadata and thumbnail.
 */
struct BlendFileInfo {
    std::filesystem::path path;                 ///< Full path to the file
    std::string filename;                       ///< Filename without path
    uintmax_t fileSize = 0;                     ///< File size in bytes
    std::filesystem::file_time_type modifiedTime; ///< Last modification time

    std::optional<BlendThumbnail> thumbnail;    ///< Embedded thumbnail (if present)
    BlendMetadata metadata;                     ///< Parsed metadata
};

/**
 * @brief Parser for Blender .blend files.
 *
 * Provides static methods to parse .blend files and extract thumbnails
 * and metadata. Supports both compressed and uncompressed files.
 *
 * The parser understands the Blender file format including:
 * - File headers (magic number, pointer size, endianness, version)
 * - Block headers (code, size, SDNA index)
 * - TEST blocks containing thumbnails
 * - Object counting blocks (OB, ME, MA, TE)
 *
 * @note Compressed .blend files (gzip) can only have basic file info extracted.
 */
class BlendParser {
public:
    /**
     * @brief Parse a .blend file (alias for parseQuick).
     * @param path Path to the .blend file
     * @return BlendFileInfo if successful, std::nullopt on failure
     */
    static std::optional<BlendFileInfo> parse(const std::filesystem::path& path);

    /**
     * @brief Quick parse - extracts basic info and thumbnail only.
     *
     * Stops parsing after finding the thumbnail block, making it faster
     * than parseFull for use cases that only need the preview.
     *
     * @param path Path to the .blend file
     * @return BlendFileInfo with thumbnail if successful, std::nullopt on failure
     */
    static std::optional<BlendFileInfo> parseQuick(const std::filesystem::path& path);

    /**
     * @brief Full parse - extracts all metadata including object counts.
     *
     * Parses the entire file to count objects, meshes, materials, and textures.
     * Slower than parseQuick but provides complete metadata.
     *
     * @param path Path to the .blend file
     * @return BlendFileInfo with full metadata if successful, std::nullopt on failure
     */
    static std::optional<BlendFileInfo> parseFull(const std::filesystem::path& path);

private:
    /**
     * @brief Internal structure for the .blend file header.
     */
    struct FileHeader {
        char magic[7];      ///< "BLENDER" magic identifier
        char pointerSize;   ///< '_' = 32-bit, '-' = 64-bit
        char endianness;    ///< 'v' = little endian, 'V' = big endian
        char version[3];    ///< Version digits (e.g., "400" for 4.0)
    };

    /**
     * @brief Internal structure for block headers in .blend files.
     */
    struct BlockHeader {
        char code[4];           ///< Block type code (e.g., "TEST", "OB", "ME")
        int32_t size;           ///< Size of block data in bytes
        uint64_t oldAddress;    ///< Original memory address (for relocation)
        int32_t sdnaIndex;      ///< SDNA structure index
        int32_t count;          ///< Number of structures in block
    };

    static bool readHeader(std::ifstream& file, FileHeader& header);
    static bool readBlockHeader(std::ifstream& file, BlockHeader& block, bool is64bit, bool bigEndian);
    static std::optional<BlendThumbnail> extractThumbnail(std::ifstream& file, const BlockHeader& block);
    static void extractMetadata(std::ifstream& file, BlendMetadata& metadata, bool is64bit, bool bigEndian);
};

} // namespace BlenderFileFinder
