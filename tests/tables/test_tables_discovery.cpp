// test_tables_discovery.cpp -- discovery-priority tests (mock dirs only).
// Not wired into CMakeLists.txt (owned-files constraint); build manually:
//   g++ -std=c++23 -I src tests/tables/test_tables_discovery.cpp
//       src/tables/tables_loader.cpp src/core/cube.cpp src/core/moves.cpp
//       -lgtest -lgtest_main -o /tmp/test_tables_discovery
// Same coverage runs in-process via:
//   tables_verifier discovery
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "tables/tables_loader.hpp"

namespace fs = std::filesystem;

class DiscoveryFixture : public ::testing::Test {
 protected:
  fs::path root_;
  fs::path cwd_;
  fs::path exe_dir_;
  fs::path local_;
  fs::path bld_;
  fs::path exe_t_;
  fs::path explicit_dir_;

  void SetUp() override {
    root_ = fs::temp_directory_path() / "rubik_discovery_gtest";
    std::error_code ec;
    fs::remove_all(root_, ec);
    cwd_ = root_ / "work";
    exe_dir_ = root_ / "exe";
    local_ = cwd_ / "tables";
    bld_ = cwd_ / "build" / "tables";
    exe_t_ = exe_dir_ / "tables";
    explicit_dir_ = root_ / "explicit";
    fs::create_directories(cwd_);
    fs::create_directories(exe_dir_);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  void make_tables(const fs::path& d) {
    std::error_code ec;
    fs::create_directories(d, ec);
    std::ofstream out(d / "cornerHeuristicHTM");
    out << "mock";
  }
};

TEST_F(DiscoveryFixture, ExplicitWins) {
  make_tables(local_);
  make_tables(bld_);
  make_tables(exe_t_);
  make_tables(explicit_dir_);
  EXPECT_EQ(rubik::tables::discover_tables_dir(explicit_dir_, cwd_, exe_dir_),
            explicit_dir_);
  EXPECT_EQ(rubik::tables::resolve_tables_dir(explicit_dir_), explicit_dir_);
  EXPECT_TRUE(rubik::tables::dir_contains_tables(explicit_dir_));
}

TEST_F(DiscoveryFixture, LocalTablesBeatsBuild) {
  make_tables(local_);
  make_tables(bld_);
  EXPECT_EQ(rubik::tables::discover_tables_dir(fs::path(), cwd_, exe_dir_),
            local_);
}

TEST_F(DiscoveryFixture, BuildWinsWhenLocalAbsent) {
  make_tables(bld_);
  EXPECT_EQ(rubik::tables::discover_tables_dir(fs::path(), cwd_, exe_dir_),
            bld_);
}

TEST_F(DiscoveryFixture, ExeTablesWhenCwdAbsent) {
  make_tables(exe_t_);
  EXPECT_EQ(rubik::tables::discover_tables_dir(fs::path(), cwd_, exe_dir_),
            exe_t_);
}

TEST_F(DiscoveryFixture, EmptyWhenNothingPresent) {
  EXPECT_TRUE(rubik::tables::discover_tables_dir(fs::path(), cwd_, exe_dir_)
                  .empty());
  EXPECT_TRUE(rubik::tables::first_tables_dir(
                  rubik::tables::tables_discovery_candidates(cwd_, exe_dir_))
                  .empty());
}
