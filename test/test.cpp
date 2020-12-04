#include <filesystem>
#include <iostream>

#include "fdb/reader.hpp"

int main() {
  fdb::Reader rd("texture0.fdb");
  for (const auto& it : rd.FileIt()) {
    std::cout << it->name() << std::endl;
    std::filesystem::path p(it->name());
    std::filesystem::create_directories(p.parent_path());
    it->toFile(it->name().c_str(), true);
  }
}