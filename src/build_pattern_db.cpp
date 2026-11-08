// build_pattern_db: BFS-build the 4 optimal-solver PDBs into data/.
//
// Successor logic lives in rubik_optimal (solvers/optimal/solver.cpp);
// this executable is only argument parsing + orchestration so the
// builder and solver can never drift apart on index definitions.
//
// Expected runtime on a typical desktop (Release, -O2): ~5-10 minutes
// total for all 4 PDBs (corner dominates). If the build exceeds ~30
// minutes, stop and investigate rather than waiting indefinitely
// (see docs/SPEC.md Section 11). Run in background with a log file:
//   ./build_pattern_db --data-dir ./data > build_pdb.log 2>&1
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include "solvers/optimal/solver.hpp"

namespace {

void print_help() {
  std::cout
      << "Usage: build_pattern_db [--data-dir DIR]\n"
         "  --data-dir DIR   Directory for PDB files (default: ./data)\n"
         "  --help, -h       Show this help\n"
         "\n"
         "Builds all 4 pattern databases by BFS from the solved pattern:\n"
         "  corner.db  corner 8!*3^7 = 88,179,840 entries (~44 MB packed)\n"
         "  edge1.db   UR,UF,UL,UB,DR,DF = 42,577,920 entries (~21 MB)\n"
         "  edge2.db   DL,DB,FR,FL,BL,BR = 42,577,920 entries (~21 MB)\n"
         "  edge3.db   UR,UL,DR,DL,FR,BL = 42,577,920 entries (~21 MB)\n"
         "Entries are 4-bit packed with a version header; the solver\n"
         "rejects mismatched files (DatabaseMissing). Files in data/ are\n"
         "generated artifacts and are never committed (gitignored).\n"
         "\n"
         "Expected runtime: ~5-10 minutes total on a typical desktop in\n"
         "Release mode. Stop and investigate if the build exceeds ~30\n"
         "minutes. Run in background with a log file, e.g.:\n"
         "  ./build_pattern_db --data-dir ./data > build_pdb.log 2>&1\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  std::filesystem::path data_dir = "./data";
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    }
    if (arg == "--data-dir") {
      if (i + 1 >= argc) {
        std::cerr << "build_pattern_db: --data-dir requires a value\n";
        return 1;
      }
      data_dir = argv[++i];
      continue;
    }
    std::cerr << std::format("build_pattern_db: unknown argument '{}'\n", arg);
    print_help();
    return 1;
  }

  std::error_code ec;
  std::filesystem::create_directories(data_dir, ec);
  if (ec) {
    std::cerr << std::format("build_pattern_db: cannot create {}: {}\n",
                             data_dir.string(), ec.message());
    return 1;
  }
  std::cout << std::format("data-dir: {}\n", data_dir.string());

  struct Job {
    std::uint32_t id;
    const char* file;
  };
  const Job jobs[] = {{rubik::optimal::kPdbIdCorner,
                       rubik::optimal::kCornerFileName},
                      {rubik::optimal::kPdbIdEdge1,
                       rubik::optimal::kEdge1FileName},
                      {rubik::optimal::kPdbIdEdge2,
                       rubik::optimal::kEdge2FileName},
                      {rubik::optimal::kPdbIdEdge3,
                       rubik::optimal::kEdge3FileName}};
  for (const Job& job : jobs) {
    const std::filesystem::path out = data_dir / job.file;
    auto result = rubik::optimal::build_and_write_pdb(job.id, out);
    if (!result) {
      std::cerr << std::format("build_pattern_db: {} failed: {}\n", job.file,
                               result.error());
      return 1;
    }
    std::error_code sec;
    const auto size = std::filesystem::file_size(out, sec);
    if (!sec) {
      std::cout << std::format("{} written ({} bytes)\n", out.string(), size);
    }
  }
  std::cout << "build_pattern_db: all PDBs built.\n";
  return 0;
}
