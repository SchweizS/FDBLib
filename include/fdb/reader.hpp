#pragma once
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

#include "NormalFile.hpp"
#include "base.hpp"

namespace fdb {
  class NormalFile;
  class Reader {
  protected:
    class BaseIterator {
    public:
      using iterator_category = std::random_access_iterator_tag;
      using value_type = Reader;
      using difference_type = int;
      using pointer = Reader*;
      using reference = Reader&;

    public:
      BaseIterator(const Reader* rd, bool end) : mReader(rd) { mIndex = end ? mReader->size() : 0; }

      bool operator==(const BaseIterator& other) const { return (mIndex == other.mIndex && mReader == other.mReader); }
      bool operator!=(const BaseIterator& other) const { return !(*this == other); }

      operator bool() const { return mReader != nullptr; }

      BaseIterator& operator+=(const difference_type& movement) {
        mIndex += movement;
        return (*this);
      }
      BaseIterator& operator-=(const difference_type& movement) {
        mIndex -= movement;
        return (*this);
      }
      BaseIterator& operator++() {
        ++mIndex;
        return (*this);
      }
      BaseIterator& operator--() {
        --mIndex;
        return (*this);
      }
      BaseIterator operator++(int) {
        auto temp(*this);
        ++mIndex;
        return temp;
      }
      BaseIterator operator--(int) {
        auto temp(*this);
        --mIndex;
        return temp;
      }
      BaseIterator operator+(const difference_type& movement) {
        auto oldPtr = mIndex;
        mIndex += movement;
        auto temp(*this);
        mIndex = oldPtr;
        return temp;
      }
      BaseIterator operator-(const difference_type& movement) {
        auto oldPtr = mIndex;
        mIndex -= movement;
        auto temp(*this);
        mIndex = oldPtr;
        return temp;
      }

    protected:
      const Reader* mReader;
      int mIndex;
    };
    template <typename T>
    class ItProxy {
    public:
      ItProxy(const Reader* p) : ptr(p) {}
      T begin() { return T(ptr); }
      T end() { return T(ptr, true); }

    private:
      const Reader* ptr;
    };

  public:
    class InfoIterator : public BaseIterator {
    public:
      InfoIterator(const Reader* rd, bool end = false) : BaseIterator(rd, end) {}
      FileInfo operator*() const { return mReader->info(mIndex); }
    };
    class FileIterator : public BaseIterator {
    public:
      FileIterator(const Reader* rd, bool end = false) : BaseIterator(rd, end) {}
      std::unique_ptr<NormalFile> operator*() const { return mReader->get(mIndex); }
    };

  public:
    Reader() = default;
    explicit Reader(const char* file) { open(file); }

    bool open(const char* file);
    void close();

    [[nodiscard]] operator bool() const { return !mFileTable.empty(); };

    [[nodiscard]] FileInfo info(int index) const;
    [[nodiscard]] std::unique_ptr<NormalFile> get(int index) const;
    [[nodiscard]] int index(const char* name) const noexcept;
    [[nodiscard]] std::uint32_t size() const noexcept { return mFileTable.size(); }

    [[nodiscard]] ItProxy<InfoIterator> InfoIt() { return ItProxy<InfoIterator>(this); }
    [[nodiscard]] ItProxy<FileIterator> FileIt() { return ItProxy<FileIterator>(this); }

  protected:
  private:
    mutable std::mutex mCriticalSection;  // multithreading safety
    mutable std::ifstream mPackage;
    std::vector<FileTableEntry> mFileTable;
    std::vector<char*> mFileNames;
    std::unique_ptr<char[]> mNames;
  };
}  // namespace fdb