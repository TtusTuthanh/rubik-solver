// Standalone self-test for the tables\ loader (hash/size check of
// tables\ then fallback message). NOT part of the rubik_tables
// library: build as a separate executable so its main() cannot
// collide with the application. CMake one-liner for integrator:
//   add_executable(tables_selftest src/tables/tables_selftest.cpp)
//   target_include_directories(tables_selftest PRIVATE src)
//   target_link_libraries(tables_selftest PRIVATE rubik_tables)
// Usage: tables_selftest [--tables-dir DIR] [--hash]
//   default dir = resolved default (.\tables / tables).
//   --hash = also verify SHA-256 of all 34 files (slow: ~4.3 GB).
//   Without --hash only sizes are checked (fast).
#include <filesystem>
#include <iostream>
#include <string>

#include "tables/adapter.hpp"
#include "tables/tables_loader.hpp"

int main(int argc, char* argv[]) {
  std::filesystem::path dir;
  bool hash = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--tables-dir") {
      if (i + 1 < argc) {
        dir = std::filesystem::path(argv[++i]);
      } else {
        std::cerr << "missing value for --tables-dir\n";
        return 2;
      }
    } else if (a == "--hash") {
      hash = true;
    } else if (a == "--help" || a == "-h") {
      std::cout << "Usage: tables_selftest [--tables-dir DIR] [--hash]\n";
      return 0;
    } else {
      std::cerr << "unknown argument: " << a << "\n";
      return 2;
    }
  }
  if (dir.empty()) dir = rubik::tables::resolve_tables_dir();
  return rubik::tables::run_self_test(dir, hash);
}
