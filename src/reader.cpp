#include "reader.hpp"

#include <exception>

#include "ImageFile.hpp"
#include "impl/base.hpp"
#include <algorithm>
#include <cctype>
namespace fdb {

  std::unique_ptr<NormalFile> Reader::get(int index) const {
    const auto& fte = mFileTable[index];
    if (fte.offset == 0) {
      return nullptr;
    }
    std::unique_ptr<NormalFile> res;
    if (fte.type == FileType::normal) {
      res = std::make_unique<NormalFile>();
    } else {
      res = std::make_unique<ImageFile>();
    }
    res->name(mFileNames[index]);
    res->time(fte.time);
    std::vector<char> tmp;
    impl::NormalFileHeader nfh;
    {
      std::lock_guard<std::mutex> l(mCriticalSection);
      mPackage.seekg(fte.offset);

      mPackage.read((char*)&nfh, sizeof(nfh));
      if (nfh.size_uncompressed >> 31) {
        // check highest bit
        // if the file is bigger than 2GB than there is something horribly wrong
        return nullptr;
      }
      if (nfh.size_uncompressed == 0) {
        return nullptr;
      }
      if (nfh.size_compressed > 0x10000000) {
        return nullptr;
      }

      if (nfh.namelength > 0x200) {
        return nullptr;
      }

      mPackage.seekg(nfh.namelength, std::ios::cur);
      if (res->isImage()) {
        auto hdr = ((ImageFile*)res.get())->getHeader();

        impl::ImageFileHeader f;
        mPackage.read((char*)&f, sizeof(f));
        auto& _hdr = ((ImageFile*)res.get())->getHeader();
        _hdr.height = f.height;
        _hdr.width = f.width;
        _hdr.mipmap = f.mipmap;
        _hdr.type = f.type;
      }
      tmp.resize(nfh.compression == Compression::none ? nfh.size_uncompressed : nfh.size_compressed);
      if (!tmp.empty()) {
        mPackage.read((char*)&tmp.front(), tmp.size());
      }
    }
    res->data(std::move(tmp), nfh.compression, nfh.size_uncompressed);
    return res;
  }

  bool Reader::open(const char* file) {
    close();

    mPackage.open(file, std::ios::binary);
    if (!mPackage.is_open()) return *this;

    impl::FDBHeader hdr;
    mPackage.read((char*)&hdr, sizeof(hdr));

    if (hdr.magic != impl::MAGIC) return *this;
    if (hdr.filecount == 0) return *this;

    // read filetable
    mFileTable.resize(hdr.filecount);
    mPackage.read((char*)&mFileTable.front(), sizeof(decltype(mFileTable)::value_type) * hdr.filecount);

    // read filenames
    mFileNames.resize(hdr.filecount);

    std::vector<int> len;
    len.resize(hdr.filecount);
    mPackage.read((char*)&len.front(), hdr.filecount * sizeof(int));

    int namelen = 0;
    mPackage.read((char*)&namelen, sizeof(namelen));
    mNames = std::make_unique<char[]>(namelen);
    mPackage.read(mNames.get(), namelen);

    for (std::uint32_t i = 0, offset = 0; i < hdr.filecount; ++i) {
      mFileNames[i] = mNames.get() + offset;
      offset += len[i] + 1;
    }
    for (auto f : mFileNames) {
      std::string p = f;
      std::replace(p.begin(), p.end(), '\\', '/');
      p.erase(p.begin(), std::find_if(p.begin(), p.end(), [](auto c) { return !(c == '/' || c == '.'); }));
      std::transform(p.begin(), p.end(), p.begin(), ::tolower);
      memcpy(f, p.c_str(), p.size() + 1); // copy null terminator
    }
    return *this;
  }
  void Reader::close() {
    std::lock_guard<std::mutex> l(mCriticalSection);
    mPackage.close();
    mFileTable.clear();
    mFileNames.clear();
    mNames = nullptr;
  }
  FileInfo Reader::info(int index) const {
    FileInfo f;
    const auto& fte = mFileTable[index];
    f.name = mFileNames[index];
    f.time = fte.time;
    if (fte.offset == 0) {
      return f;
    } 

    impl::NormalFileHeader nfh;

    std::lock_guard<std::mutex> l(mCriticalSection);
    mPackage.seekg(fte.offset);
    mPackage.read((char*)&nfh, sizeof(nfh));

    f.compressedSize = nfh.size_compressed;
    f.expectedSize = nfh.size_uncompressed;
    f.compression = nfh.compression;
    return f;
  }
  int Reader::index(const char* name) const noexcept {
    if (name == nullptr) return -1;
    for (std::uint32_t i = 0; i < mFileNames.size(); ++i) {
      if (mFileNames[i] == name) {
        return i;
      }
    }
    return -1;
  }
}  // namespace fdb