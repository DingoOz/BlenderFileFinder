#include "blend_parser.hpp"
#include "debug.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <chrono>

namespace BlenderFileFinder {

namespace {

uint32_t swapBytes32(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

uint64_t swapBytes64(uint64_t val) {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8) |
           ((val & 0x00000000FF000000ULL) << 8) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

} // anonymous namespace

bool BlendParser::readHeader(std::ifstream& file, FileHeader& header) {
    file.read(header.magic, 7);
    if (std::strncmp(header.magic, "BLENDER", 7) != 0) {
        return false;
    }

    file.read(&header.pointerSize, 1);
    file.read(&header.endianness, 1);
    file.read(header.version, 3);

    return file.good();
}

bool BlendParser::readBlockHeader(std::ifstream& file, BlockHeader& block, bool is64bit, bool bigEndian) {
    file.read(block.code, 4);
    if (!file.good()) return false;

    file.read(reinterpret_cast<char*>(&block.size), 4);
    if (bigEndian) block.size = static_cast<int32_t>(swapBytes32(static_cast<uint32_t>(block.size)));

    if (is64bit) {
        file.read(reinterpret_cast<char*>(&block.oldAddress), 8);
        if (bigEndian) block.oldAddress = swapBytes64(block.oldAddress);
    } else {
        uint32_t addr32;
        file.read(reinterpret_cast<char*>(&addr32), 4);
        if (bigEndian) addr32 = swapBytes32(addr32);
        block.oldAddress = addr32;
    }

    file.read(reinterpret_cast<char*>(&block.sdnaIndex), 4);
    if (bigEndian) block.sdnaIndex = static_cast<int32_t>(swapBytes32(static_cast<uint32_t>(block.sdnaIndex)));

    file.read(reinterpret_cast<char*>(&block.count), 4);
    if (bigEndian) block.count = static_cast<int32_t>(swapBytes32(static_cast<uint32_t>(block.count)));

    return file.good();
}

std::optional<BlendThumbnail> BlendParser::extractThumbnail(std::ifstream& file, const BlockHeader& block) {
    if (block.size < 8) return std::nullopt;

    // Read thumbnail dimensions
    int32_t width, height;
    file.read(reinterpret_cast<char*>(&width), 4);
    file.read(reinterpret_cast<char*>(&height), 4);

    // Sanity check dimensions
    if (width <= 0 || width > 1024 || height <= 0 || height > 1024) {
        return std::nullopt;
    }

    size_t pixelDataSize = static_cast<size_t>(width * height * 4);
    if (block.size < static_cast<int32_t>(8 + pixelDataSize)) {
        return std::nullopt;
    }

    BlendThumbnail thumbnail;
    thumbnail.width = width;
    thumbnail.height = height;
    thumbnail.pixels.resize(pixelDataSize);

    file.read(reinterpret_cast<char*>(thumbnail.pixels.data()), pixelDataSize);

    if (!file.good()) return std::nullopt;

    // Blender stores thumbnails flipped vertically - flip them back
    std::vector<uint8_t> flipped(pixelDataSize);
    size_t rowSize = width * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(flipped.data() + y * rowSize,
                    thumbnail.pixels.data() + (height - 1 - y) * rowSize,
                    rowSize);
    }
    thumbnail.pixels = std::move(flipped);

    return thumbnail;
}

std::optional<BlendFileInfo> BlendParser::parse(const std::filesystem::path& path) {
    return parseQuick(path);
}

std::optional<BlendFileInfo> BlendParser::parseQuick(const std::filesystem::path& path) {
    auto startTime = std::chrono::steady_clock::now();
    DEBUG_LOG("parseQuick: " << path.string());

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DEBUG_LOG("parseQuick: failed to open file");
        return std::nullopt;
    }

    auto openTime = std::chrono::steady_clock::now();
    auto openMs = std::chrono::duration_cast<std::chrono::milliseconds>(openTime - startTime).count();
    if (openMs > 50) {
        DEBUG_LOG("parseQuick: file open took " << openMs << "ms");
    }

    BlendFileInfo info;
    info.path = path;
    info.filename = path.filename().string();

    std::error_code ec;
    info.fileSize = std::filesystem::file_size(path, ec);
    info.modifiedTime = std::filesystem::last_write_time(path, ec);

    FileHeader header;
    if (!readHeader(file, header)) {
        // Try to read as compressed file (gzip)
        file.seekg(0);
        unsigned char gzMagic[2];
        file.read(reinterpret_cast<char*>(gzMagic), 2);
        if (gzMagic[0] == 0x1f && gzMagic[1] == 0x8b) {
            info.metadata.isCompressed = true;
            // For compressed files, we can't easily extract metadata
            // Return basic file info only
            return info;
        }
        return std::nullopt;
    }

    info.metadata.blenderVersion = std::string(header.version, 3);
    info.metadata.blenderVersion.insert(1, ".");

    bool is64bit = (header.pointerSize == '-');
    bool bigEndian = (header.endianness == 'V');

    // Parse blocks to find thumbnail (TEST block)
    BlockHeader block;
    int blockCount = 0;
    auto blockStartTime = std::chrono::steady_clock::now();

    while (readBlockHeader(file, block, is64bit, bigEndian)) {
        blockCount++;

        // Check for end block
        if (std::strncmp(block.code, "ENDB", 4) == 0) {
            break;
        }

        // TEST block contains the thumbnail
        if (std::strncmp(block.code, "TEST", 4) == 0) {
            info.thumbnail = extractThumbnail(file, block);
            // Once we have the thumbnail, we can stop for quick parse
            break;
        }

        // Skip block data
        file.seekg(block.size, std::ios::cur);
    }

    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count();
    if (totalMs > 100) {
        DEBUG_LOG("parseQuick: " << path.filename() << " took " << totalMs << "ms, " << blockCount << " blocks scanned, thumbnail=" << (info.thumbnail.has_value() ? "yes" : "no"));
    }

    return info;
}

std::optional<BlendFileInfo> BlendParser::parseFull(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;

    BlendFileInfo info;
    info.path = path;
    info.filename = path.filename().string();

    std::error_code ec;
    info.fileSize = std::filesystem::file_size(path, ec);
    info.modifiedTime = std::filesystem::last_write_time(path, ec);

    FileHeader header;
    if (!readHeader(file, header)) {
        file.seekg(0);
        unsigned char gzMagic[2];
        file.read(reinterpret_cast<char*>(gzMagic), 2);
        if (gzMagic[0] == 0x1f && gzMagic[1] == 0x8b) {
            info.metadata.isCompressed = true;
            return info;
        }
        return std::nullopt;
    }

    info.metadata.blenderVersion = std::string(header.version, 3);
    info.metadata.blenderVersion.insert(1, ".");

    bool is64bit = (header.pointerSize == '-');
    bool bigEndian = (header.endianness == 'V');

    // Parse all blocks to count objects
    BlockHeader block;
    while (readBlockHeader(file, block, is64bit, bigEndian)) {
        if (std::strncmp(block.code, "ENDB", 4) == 0) {
            break;
        }

        // TEST block contains thumbnail
        if (std::strncmp(block.code, "TEST", 4) == 0) {
            info.thumbnail = extractThumbnail(file, block);
        }
        // OB block = Object
        else if (std::strncmp(block.code, "OB", 2) == 0) {
            info.metadata.objectCount += block.count;
        }
        // ME block = Mesh
        else if (std::strncmp(block.code, "ME", 2) == 0) {
            info.metadata.meshCount += block.count;
        }
        // MA block = Material
        else if (std::strncmp(block.code, "MA", 2) == 0) {
            info.metadata.materialCount += block.count;
        }
        // TE or TX block = Texture
        else if (std::strncmp(block.code, "TE", 2) == 0 ||
                 std::strncmp(block.code, "TX", 2) == 0) {
            info.metadata.textureCount += block.count;
        }

        // Skip block data
        file.seekg(block.size, std::ios::cur);
    }

    return info;
}

} // namespace BlenderFileFinder
