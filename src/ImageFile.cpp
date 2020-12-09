#include "ImageFile.hpp"

#include <cstdint>
#include <exception>
#include <fstream>
#include <mutex>

#include "impl/base.hpp"

#define NOMINMAX
#include <windows.h>

// redux compression based on FDBExtractor by McBen
namespace {
  struct DLLWrapper {
    template <typename Signature>
    Signature get(const char* name) {
      return (Signature)GetProcAddress(mDLL, name);
    }
    explicit DLLWrapper(const char* file) { load(file); }
    bool load(const char* file) {
      mDLL = LoadLibraryA(file);
      return mDLL != nullptr;
    }
    void release() {
      if (mDLL) {
        FreeLibrary(mDLL);
      }
    }
    ~DLLWrapper() { release(); }
    operator bool() const { return mDLL != nullptr; }

    HMODULE mDLL;
  };
}  // namespace
namespace redux {
#pragma pack(push, 4)
  struct DATA2 {
    void* new_data;
    std::uint32_t unk;         // u2a
    std::uint16_t width;       // u2b
    std::uint16_t height;      // u2b
    std::uint8_t mipmapcount;  // u2c
    std::uint8_t pixelformat;
    std::uint16_t padding;  // 16 bytes padding?
  };
  struct DATA {
    DATA2 data;
    std::uint8_t* imageData;  // pointer containing image data
    std::uint8_t* unk;        // probably a pointer...
    std::uint8_t* tempData;   // temporary Pointer?

    std::uint32_t imageSize;  // sizeof imageData
    std::uint32_t page_size;
    std::uint32_t total_size;

    const char* filename;
    bool has_data;
  };
#pragma pack(pop)
  constexpr std::uint32_t TopBit(std::uint32_t size) {
    std::uint32_t power = (size > 0) ? 1 : 0;
    while (power < size) power *= 2;
    return power;
  }
  constexpr std::uint32_t getImageSize(std::int32_t format, std::int32_t mipmapcount, std::int32_t width,
                                       std::int32_t height) {
    int imagesize = 0;
    mipmapcount = std::max(1, mipmapcount);
    for (auto i = 0; i < mipmapcount; ++i, width >>= 1, height >>= 1) {
      switch (format) {
        case 1:
        case 2:
          imagesize += 2 * std::max(width, 1) * std::max(height, 1);
          break;
        case 3:
          imagesize += 3 * std::max(width, 1) * std::max(height, 1);
          break;
        case 4:
          imagesize += 4 * std::max(width, 1) * std::max(height, 1);
          break;
        case 5:
        case 6:  // DDS
          imagesize += std::max(width, 4) * std::max(height, 4) / 2;
          break;
        case 7:
        case 8:
          imagesize += std::max(width, 4) * std::max(height, 4);
          break;
      }
    }
    return imagesize;
  }
  constexpr std::uint8_t getPixelFormat(const DATA2& data) {
    switch (data.pixelformat) {
      case 0:
      case 4:
        return 4;
      case 1:
        return 5;
      case 8:
        return 3;
      case 9:
        return 7;
      case 16:
      case 20:
        return 2;
      case 17:
        return 8;
      case 21:
        return 6;
      case 24:
        return 1;
    }
    return 0;
  }

  struct redux_status {
    uint32_t k1;
    uint32_t k2;
    uint32_t k3;
    size_t size;
  };

  using t_HandleDecompress = int (*)(char* data, int src_size, DATA* dst);
  using t_CallbackSet = int (*)(unsigned nr, void* fct);
  using t_HandleGetOutputDesc = int (*)(int a1, int a2, void* a3);

  t_HandleDecompress handleDecompress{nullptr};
  t_CallbackSet callbackSet{nullptr};
  t_HandleGetOutputDesc handleGetOutputDesc{nullptr};

  void* __stdcall CallbackBuffer(std::int32_t a1, DATA** _data, std::int32_t index, std::int32_t mipmaplevel,
                                 redux_status* status) {
    // void* __stdcall redux_callback2(int a1, a1_struct* a2, int a3, int a4, redux_status* a5)
    auto data = *_data;
    data->has_data = true;
    if (mipmaplevel == 0) {
      DATA2 inner;
      handleGetOutputDesc(a1, index, &inner);
      inner.width = TopBit(inner.width);
      inner.height = TopBit(inner.height);

      data->data = inner;
      data->imageSize = getImageSize(getPixelFormat(inner), inner.mipmapcount, inner.width, inner.height);
      data->imageData = nullptr;
      data->tempData = nullptr;
      data->total_size = 0;
      if (data->imageSize > 0) {
        data->imageData = new std::uint8_t[data->imageSize];
      } else {
        data->tempData = new std::uint8_t[status->size];
      }
    } else {
      data->total_size += data->page_size;
      if (!data->tempData && data->total_size >= data->imageSize && data->page_size > 0) {
        data->tempData = new std::uint8_t[data->page_size];
      }
    }
    data->page_size = status->size;
    if (data->tempData) {
      return data->tempData;
    }
    return data->imageData + data->total_size;
  }
  void __stdcall CallbackOnComplete(std::int32_t, DATA** _data, std::int32_t) {
    // void* __stdcall redux_callback1(int a1, a1_struct* a2, int a3)
    auto data = *_data;
    data->has_data = true;
    if (data->tempData) {
      delete[] data->tempData;
      data->tempData = nullptr;
    }
  }

  bool init() {
    static bool initialized = false;
    static DLLWrapper dll("redux_runtime.dll");
    static std::mutex mutex;
    std::lock_guard<std::mutex> l(mutex);
    if (initialized || !dll) return dll;
    redux::handleDecompress = dll.get<redux::t_HandleDecompress>("reduxHandleDecompress");
    redux::callbackSet = dll.get<redux::t_CallbackSet>("reduxCallbackSet");
    redux::handleGetOutputDesc = dll.get<redux::t_HandleGetOutputDesc>("reduxHandleGetOutputDesc");
    if (!redux::handleDecompress || !redux::callbackSet || !redux::handleGetOutputDesc) {
      dll.release();
      return false;
    }
    redux::callbackSet(7, (void*)&redux::CallbackOnComplete);
    redux::callbackSet(6, (void*)&redux::CallbackBuffer);
    return true;
  }
  // "redux_runtime.dll"
  bool decompress(std::vector<char>& data, fdb::ImageFile* img) {
    if (!init()) {
      return false;
    }
    redux::DATA reduxData;
    memset(&reduxData, 0, sizeof(reduxData));
    reduxData.filename = "dummy";  // img->name().c_str();
    auto res = redux::handleDecompress(&data.front(), data.size(), &reduxData);
    if (res != 0 || reduxData.imageSize == 0) {
      return false;
    }
    data.resize(reduxData.imageSize);
    memcpy(&data.front(), reduxData.imageData, data.size());

    auto& hdr = img->getHeader();
    hdr.height = reduxData.data.height;
    hdr.width = reduxData.data.width;
    hdr.mipmap = reduxData.data.mipmapcount;
    hdr.type = redux::getPixelFormat(reduxData.data);

    delete[] reduxData.imageData;
    return true;
  }
}  // namespace redux
namespace dds {
  constexpr std::uint32_t FOURCC = 0x00000004;  // DDPF_FOURCC
  constexpr std::uint32_t RGB = 0x00000040;     // DDPF_RGB
  constexpr std::uint32_t RGBA = 0x00000041;    // DDPF_RGB | DDPF_ALPHAPIXELS
  namespace pf {
    struct Pf {
      uint32_t dwSize;
      uint32_t dwFlags;
      uint32_t dwFourCC;
      uint32_t dwRGBBitCount;
      uint32_t dwRBitMask;
      uint32_t dwGBitMask;
      uint32_t dwBBitMask;
      uint32_t dwABitMask;
    };
    constexpr Pf DXT1 = {sizeof(Pf), FOURCC, MAKEFOURCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 0};
    constexpr Pf DXT2 = {sizeof(Pf), FOURCC, MAKEFOURCC('D', 'X', 'T', '2'), 0, 0, 0, 0, 0};
    constexpr Pf DXT3 = {sizeof(Pf), FOURCC, MAKEFOURCC('D', 'X', 'T', '3'), 0, 0, 0, 0, 0};
    constexpr Pf DXT4 = {sizeof(Pf), FOURCC, MAKEFOURCC('D', 'X', 'T', '4'), 0, 0, 0, 0, 0};
    constexpr Pf DXT5 = {sizeof(Pf), FOURCC, MAKEFOURCC('D', 'X', 'T', '5'), 0, 0, 0, 0, 0};
    constexpr Pf A8R8G8B8 = {sizeof(Pf), RGBA, 0, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000};
    constexpr Pf A1R5G5B5 = {sizeof(Pf), RGBA, 0, 16, 0x00007c00, 0x000003e0, 0x0000001f, 0x00008000};
    constexpr Pf A4R4G4B4 = {sizeof(Pf), RGBA, 0, 16, 0x00000f00, 0x000000f0, 0x0000000f, 0x0000f000};
    constexpr Pf R8G8B8 = {sizeof(Pf), RGB, 0, 24, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000};
    constexpr Pf _R5G6B5 = {sizeof(Pf), RGB, 0, 16, 0x0000f800, 0x000007e0, 0x0000001f, 0x00000000};
  };  // namespace pf
  namespace headerflags {
    constexpr std::uint32_t TEXTURE = 0x00001007;     // DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    constexpr std::uint32_t MIPMAP = 0x00020000;      // DDSD_MIPMAPCOUNT
    constexpr std::uint32_t VOLUME = 0x00800000;      // DDSD_DEPTH
    constexpr std::uint32_t PITCH = 0x00000008;       // DDSD_PITCH
    constexpr std::uint32_t LINEARSIZE = 0x00080000;  // DDSD_LINEARSIZE
  }                                                   // namespace headerflags
  namespace surfaceflags {
    constexpr std::uint32_t TEXTURE = 0x00001000;  // DDSCAPS_TEXTURE
    constexpr std::uint32_t MIPMAP = 0x00400008;   // DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    constexpr std::uint32_t CUBEMAP = 0x00000008;  // DDSCAPS_COMPLEX
  }                                                // namespace surfaceflags
  namespace cubemap {
    constexpr std::uint32_t POSITIVEX = 0x00000600;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX
    constexpr std::uint32_t NEGATIVEX = 0x00000a00;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEX
    constexpr std::uint32_t POSITIVEY = 0x00001200;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY
    constexpr std::uint32_t NEGATIVEY = 0x00002200;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEY
    constexpr std::uint32_t POSITIVEZ = 0x00004200;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEZ
    constexpr std::uint32_t NEGATIVEZ = 0x00008200;  // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEZ
    constexpr std::uint32_t ALLFACES = (POSITIVEX | NEGATIVEX | POSITIVEY | NEGATIVEY | POSITIVEZ | NEGATIVEZ);
  }  // namespace cubemap
  namespace flags {
    constexpr std::uint32_t VOLUME = 0x00200000;  // DDSCAPS2_VOLUME
  }

  struct HEADER {
    HEADER() {
      memset(this, 0, sizeof(*this));
      dwSize = sizeof(*this);
    }
    std::uint32_t dwSize;
    std::uint32_t dwFlags;
    std::uint32_t dwHeight;
    std::uint32_t dwWidth;
    std::uint32_t dwLinearSize;
    std::uint32_t dwDepth;
    std::uint32_t dwMipMapCount;
    std::uint32_t dwReserved1[11];
    pf::Pf ddpf;
    std::uint32_t dwCaps;
    std::uint32_t dwCaps2;
    std::uint32_t dwCaps3;
    std::uint32_t dwCaps4;
    std::uint32_t dwReserved2;
  };
}  // namespace dds
namespace bitmap {
#pragma pack(1)
  struct FILEHEADER {
    std::uint16_t bfType{0x424d};
    std::uint32_t bfSize{0xffffffff};
    std::uint16_t bfReserved1{0};
    std::uint16_t bfReserved2{0};
    std::uint32_t bfOffBits{0xffffffff};
  };
  struct INFOHEADER {
    std::uint32_t biSize{0xffffffff};
    std::int32_t biWidth{-1};
    std::int32_t biHeight{-1};
    std::uint16_t biPlanes{1};
    std::uint16_t biBitCount{0xffff};
    std::uint32_t biCompression{0};
    std::uint32_t biSizeImage{0};
    std::int32_t biXPelsPerMeter{-1};
    std::int32_t biYPelsPerMeter{-1};
    std::uint32_t biClrUsed{0};
    std::uint32_t biClrImportant{0};
  };
  struct HEADER {
    HEADER() {
      fh.bfOffBits = sizeof(fh) + sizeof(ih);
      ih.biSize = sizeof(ih);
    }
    FILEHEADER fh;
    INFOHEADER ih;
  };
#pragma pack()
}  // namespace bitmap
namespace tga {
#pragma pack(1)
  struct HEADER {
    std::uint8_t identsize{0};      // size of ID field that follows 18 byte header (0 usually)
    std::uint8_t colourmaptype{0};  // type of colour map 0=none, 1=has palette
    std::uint8_t imagetype{2};      // type of image 0=none,1=indexed,2=rgb,3=grey,+8=rle packed

    std::uint16_t colourmapstart{0};   // first colour map entry in palette
    std::uint16_t colourmaplength{0};  // number of colours in palette
    std::uint8_t colourmapbits{0};     // number of bits per palette entry 15,16,24,32

    std::uint16_t xstart{0};  // image x origin
    std::uint16_t ystart{0};  // image y origin

    std::uint16_t width{0xffff};    // image width in pixels
    std::uint16_t height{0xffff};   // image height in pixels
    std::uint8_t bits{0x20};        // image bits per pixel 8,16,24,32
    std::uint8_t descriptor{0x20};  // image descriptor bits (vh flip bits)
  };
#pragma pack()
}  // namespace tga
namespace helper {
  struct TGA : public ::tga::HEADER {
    TGA(const fdb::ImageFile::Header& hdr) {
      width = hdr.width;
      height = hdr.height;
    };
  };
  struct BMP : public ::bitmap::HEADER {
    BMP(const fdb::ImageFile::Header& hdr) {
      fh.bfSize = sizeof(fh) + sizeof(ih) + hdr.width * hdr.height * hdr.mipmap;
      ih.biWidth = hdr.width;
      ih.biHeight = -(int32_t)hdr.height;
      ih.biBitCount = static_cast<std::uint16_t>(hdr.mipmap * 8);
      ih.biXPelsPerMeter = hdr.width;
      ih.biYPelsPerMeter = hdr.height;
    }
  };
  struct DDS {
    DDS(const fdb::ImageFile::Header& hdr) {
      header.dwHeight = hdr.height;
      header.dwWidth = hdr.width;
      header.dwMipMapCount = hdr.mipmap;
      header.dwFlags = dds::headerflags::TEXTURE | dds::headerflags::LINEARSIZE;
      header.dwCaps = dds::surfaceflags::TEXTURE;
      switch (hdr.type) {
        case 4:
          header.ddpf = dds::pf::A8R8G8B8;
          break;
        case 5:
          header.ddpf = dds::pf::DXT1;
          break;
        case 6:
          header.ddpf = dds::pf::DXT1;
          header.ddpf.dwFlags |= 1;
          break;  // + Alpha
        case 8:
          header.ddpf = dds::pf::DXT5;
          break;

        default:
          throw;
      }

      if (hdr.mipmap > 1) {
        header.dwFlags |= dds::headerflags::MIPMAP;
        header.dwCaps |= dds::surfaceflags::MIPMAP;
      }

      if (hdr.type == 8) {
        header.dwLinearSize = hdr.width * 64 * 8;
      } else {
        header.dwLinearSize = hdr.width * 32;
      }
    }
    std::uint32_t magic{0x20534444};
    dds::HEADER header;
  };
}  // namespace helper

namespace fdb {
  bool ImageFile::decompress() {
    if (mCompression == Compression::redux) {
      if (!redux::decompress(mData, this)) return false;
      mCompression = Compression::none;
      return true;
    }
    return NormalFile::decompress();
  }
  bool ImageFile::fromFile(const char*, const char* name) { throw std::exception("not implemented..."); }
  bool ImageFile::toFile(const char* filename, bool _decompress) {
    if (_decompress) {
      decompress();
    }
    if (mCompression != Compression::none) {
      return false;
    }
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    if (mName.rfind(".tga") == mName.size() - 4) {
      helper::TGA hdr(mHeader);
      file.write((char*)&hdr, sizeof(hdr));
    } else if (mName.rfind(".bmp") == mName.size() - 4) {
      helper::BMP hdr(mHeader);
      file.write((char*)&hdr, sizeof(hdr));
    } else if (mName.rfind(".dds") == mName.size() - 4) {
      helper::DDS hdr(mHeader);
      file.write((char*)&hdr, sizeof(hdr));
    }
    file.write(&mData.front(), mData.size());
    return true;
  }
}  // namespace fdb
