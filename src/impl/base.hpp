#pragma once
#include "base.hpp"

namespace fdb {
  namespace impl {
    constexpr std::uint32_t MAGIC = 0x46444201;
#pragma pack(push, 4)
    struct FDBHeader {
      std::uint32_t magic{MAGIC};
      std::uint32_t filecount;
    };
    struct NormalFileHeader {
      std::uint32_t size;
      FileType type;
      Compression compression;
      std::uint32_t size_uncompressed;
      std::uint32_t size_compressed;
      std::uint64_t time;
      std::uint32_t namelength;
    };
    struct ImageFileHeader {
      std::uint32_t type;
      std::uint32_t width;
      std::uint32_t height;
      std::uint8_t mipmap;
      std::uint8_t unk[3];
    };
  }  // namespace impl
#pragma pack(pop)
}  // namespace fdb