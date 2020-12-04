#pragma once
#include "NormalFile.hpp"
namespace fdb {
  class ImageFile : public NormalFile {
  public:
#pragma pack(push, 4)
    struct Header {
      std::uint32_t type;
      std::uint32_t width;
      std::uint32_t height;
      std::uint8_t mipmap;
      std::uint8_t unk[3];
    };
#pragma pack(pop)
  public:
    virtual bool decompress() override;
    virtual bool fromFile(const char*, const char* name) override;
    virtual bool toFile(const char* filename, bool decompress = true) override;
    virtual bool isImage() const override { return true; }
    Header& getHeader() { return mHeader; }

  private:
    Header mHeader;
  };
}  // namespace fdb