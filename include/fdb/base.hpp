#pragma once
#include <cstdint>
#include <string_view>

namespace fdb {
  enum class FileType : std::uint32_t { unk, normal, image };
  enum class Compression : std::uint32_t { none, rle, lzo, zlib, redux };

#pragma pack(push, 4)
  struct FileTableEntry {
    FileType type;
    std::uint64_t time;
    std::uint32_t offset;
  };

  struct FileInfo {
    std::string_view name;
    std::uint64_t time;
    Compression compression;
    std::uint32_t compressedSize;
    std::uint32_t expectedSize;
  };
#pragma pack(pop)
}  // namespace fdb