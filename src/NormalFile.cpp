#include "NormalFile.hpp"

#include <fstream>

#include "impl/base.hpp"
#include "zlib.h"

namespace {
  // from https://zlib.net/zpipe.c
  bool decompress_zlib(std::vector<char>& data) {
    if (data.empty()) return true;
    const size_t BUFSIZE = 10 * 1024;
    uint8_t temp_buffer[BUFSIZE];
    std::vector<char> buffer;

    /* allocate inflate state */
    z_stream strm;
    strm.zalloc = 0;
    strm.zfree = 0;
    strm.avail_in = data.size();
    strm.next_in = (Bytef*)&data.front();
    if (inflateInit(&strm) != Z_OK) return false;

    /* decompress until deflate stream ends or end of file */
    /* run inflate() on input until output buffer not full */
    int ret = 0;
    while (strm.avail_in != 0) {
      strm.avail_out = BUFSIZE;
      strm.next_out = temp_buffer;
      ret = inflate(&strm, Z_NO_FLUSH);
      switch (ret) {
        case Z_NEED_DICT:
        //	ret = Z_DATA_ERROR;     /* and fall through */
        case Z_STREAM_ERROR:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
          inflateEnd(&strm);
          return false;
      }
      buffer.insert(buffer.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
      if (strm.avail_out != 0) break;
      if (ret == Z_STREAM_END) break;
    };

    /* clean up and return */
    inflateEnd(&strm);
    if (strm.avail_in == 0) {
      buffer.swap(data);
      return true;
    }
    return false;
  }
  bool compress_zlib(std::vector<char>& data) {
    if (data.empty()) return true;
    const size_t BUFSIZE = 10 * 1024;
    uint8_t temp_buffer[BUFSIZE];
    std::vector<char> buffer;

    /* allocate deflate state */
    z_stream strm;
    strm.zalloc = 0;
    strm.zfree = 0;
    strm.avail_in = data.size();
    strm.next_in = (uint8_t*)&data.front();
    if (deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK) return false;
    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    int ret;
    do {
      strm.avail_out = BUFSIZE;
      strm.next_out = temp_buffer;
      ret = deflate(&strm, strm.avail_in == 0 ? Z_FINISH : Z_NO_FLUSH); /* no bad return value */
      if (ret == Z_STREAM_ERROR) {                                      /* state not clobbered */
        deflateEnd(&strm);
        return false;
      }
      buffer.insert(buffer.end(), temp_buffer, temp_buffer + BUFSIZE - strm.avail_out);
    } while (strm.avail_out == 0);

    if (strm.avail_in != 0 || ret != Z_STREAM_END) { /* all input will be used  && stream will be complete */
      deflateEnd(&strm);
      return false;
    }
    deflateEnd(&strm);
    data.swap(buffer);
    return true;
  }
}  // namespace
namespace fdb {
  bool NormalFile::decompress() {
    switch (mCompression) {
      case Compression::none:
        return true;
      case Compression::rle:
        return false;
      case Compression::lzo:
        return false;
      case Compression::zlib:
        if (decompress_zlib(mData)) {
          mCompression = Compression::none;
          return true;
        }
        break;
      case Compression::redux:
        // only for image files...
        return false;
    }
    return false;
  }
  bool NormalFile::compress(Compression compression) {
    if (!decompress()) return false;
    switch (mCompression) {
      case Compression::none:
        return true;
      case Compression::rle:
        return false;
      case Compression::lzo:
        return false;
      case Compression::zlib:
        if (compress_zlib(mData)) {
          mCompression = Compression::zlib;
          return true;
        }
        break;
      case Compression::redux:
        return false;
    }
    return false;
  }
  bool NormalFile::fromFile(const char* filename, const char* name) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) return false;
    mName = name ? name : filename;
    mCompression = Compression::none;
    mCompressedSize = 0;
    mTime = 0;
    f.seekg(0, std::ios::end);
    mSize = static_cast<std::uint32_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    mData.resize(mSize);
    if (mSize == 0) return true;
    f.read(&mData.front(), mData.size());
    return true;
  }
  bool NormalFile::toFile(const char* filename, bool _decompress) {
    if (_decompress) {
      decompress();
    }
    if (mCompression != Compression::none) {
      return false;
    }
    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open()) return false;
    if (mData.size() == 0) return true;
    f.write(&mData.front(), mData.size());
    return true;
  }
}  // namespace fdb