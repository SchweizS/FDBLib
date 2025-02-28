#pragma once
#include <vector>
#include <string>
#include "base.hpp"

namespace fdb {
  class NormalFile {
  public:
    virtual ~NormalFile() = default;
    // THIS IS NOT THREADSAFE!
    virtual bool decompress();
    virtual bool compress(Compression);

    const std::string& name() const { return mName; }
    const std::vector<char>& get() const { return mData; }
    const std::uint32_t size() const { return mData.size(); }
    const std::uint32_t compressed_size() const { return mCompressedSize; }
    const std::uint32_t uncompressed_size() const { return mSize; }
    Compression compression() const { return mCompression; }
    std::uint64_t time() const { return mTime; }

    virtual bool fromFile(const char* filename, const char* name);
    virtual bool toFile(const char* filename, bool decompress = true);
    virtual bool isImage() const { return false; }

  protected:
    friend class Reader;
    friend class Writer;
    void data(std::vector<char> _data) {
      mCompression = Compression::none;
      mCompressedSize = 0;
      mData = std::move(_data);
      mSize = mData.size();
    }
    void data(std::vector<char> _data, Compression c, std::uint32_t size) {
      if (c == Compression::none) return data(std::move(_data));
      mCompression = c;
      mSize = size;
      mData = std::move(_data);
      mCompressedSize = mData.size();
    }
    void time(std::uint64_t t) { mTime = t; }
    void name(std::string name) { mName = name; }

  protected:
    Compression mCompression{Compression::none};
    std::string mName;
    std::uint32_t mSize;
    std::uint32_t mCompressedSize;
    std::uint64_t mTime;
    std::vector<char> mData;
  };
}  // namespace fdb