cmake_minimum_required(VERSION 3.24)
project(rubik-solver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(CTest)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

if(BUILD_TESTING)
  enable_testing()
endif()

add_library(rubik_core
  src/core/cube.cpp
  src/core/moves.cpp
  src/core/validation.cpp
)
target_include_directories(rubik_core PUBLIC src)
target_compile_features(rubik_core PUBLIC cxx_std_23)

add_library(rubik_tables
  src/tables/tables_loader.cpp
  src/tables/adapter.cpp
  src/tables/stage_pdb.cpp
  src/tables/move_pruning.cpp
)
target_include_directories(rubik_tables PUBLIC src)
target_compile_features(rubik_tables PUBLIC cxx_std_23)
target_link_libraries(rubik_tables PUBLIC rubik_core)

# Solver backends are added in P2/P3 (dirs may not exist yet).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/solvers/twophase/solver.cpp)
  add_library(rubik_twophase src/solvers/twophase/solver.cpp)
  target_include_directories(rubik_twophase PUBLIC src)
  target_link_libraries(rubik_twophase PUBLIC rubik_core rubik_tables)
endif()
foreach(_m cfop roux zz heise)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/solvers/${_m}/solver.cpp)
    add_library(rubik_${_m} src/solvers/${_m}/solver.cpp)
    target_include_directories(rubik_${_m} PUBLIC src)
    target_link_libraries(rubik_${_m} PUBLIC rubik_core)
  endif()
endforeach()

add_executable(rubik-solver src/main.cpp)
target_include_directories(rubik-solver PUBLIC src tests)
target_link_libraries(rubik-solver PRIVATE rubik_core)
# CLI links solver libs when present (declared above).
foreach(_t rubik_cfop rubik_roux rubik_zz rubik_heise rubik_twophase)
  if(TARGET ${_t})
    target_link_libraries(rubik-solver PRIVATE ${_t})
  endif()
endforeach()

if(BUILD_TESTING)
  add_executable(rubik_core_tests_cube tests/core/test_cube.cpp)
  target_include_directories(rubik_core_tests_cube PUBLIC src)
  target_link_libraries(rubik_core_tests_cube PRIVATE rubik_core gtest_main)
  add_test(NAME rubik_core_tests_cube COMMAND rubik_core_tests_cube)

  add_executable(rubik_core_tests_validation tests/core/test_validation.cpp)
  target_include_directories(rubik_core_tests_validation PUBLIC src)
  target_link_libraries(rubik_core_tests_validation PRIVATE rubik_core gtest_main)
  add_test(NAME rubik_core_tests_validation COMMAND rubik_core_tests_validation)

  add_executable(rubik_core_tests_searcher tests/core/test_searcher.cpp)
  target_include_directories(rubik_core_tests_searcher PUBLIC src)
  target_link_libraries(rubik_core_tests_searcher PRIVATE rubik_core gtest_main)
  add_test(NAME rubik_core_tests_searcher COMMAND rubik_core_tests_searcher)

  add_executable(rubik_core_tests_heuristic tests/core/test_heuristic.cpp)
  target_include_directories(rubik_core_tests_heuristic PUBLIC src)
  target_link_libraries(rubik_core_tests_heuristic PRIVATE rubik_core rubik_tables gtest_main)
  add_test(NAME rubik_core_tests_heuristic COMMAND rubik_core_tests_heuristic)

  add_executable(rubik_solver_replay_tests tests/integration/test_solver_replay.cpp)
  target_include_directories(rubik_solver_replay_tests PUBLIC src)
  target_link_libraries(rubik_solver_replay_tests PRIVATE
    rubik_core rubik_tables rubik_cfop rubik_roux rubik_zz rubik_heise rubik_twophase gtest_main)
  add_test(NAME rubik_solver_replay_tests COMMAND rubik_solver_replay_tests)

  add_executable(rubik_tables_tests_move_pruning tests/tables/test_move_pruning.cpp)
  target_include_directories(rubik_tables_tests_move_pruning PUBLIC src)
  target_link_libraries(rubik_tables_tests_move_pruning PRIVATE rubik_tables rubik_core gtest_main)
  add_test(NAME rubik_tables_tests_move_pruning COMMAND rubik_tables_tests_move_pruning)

  add_executable(rubik_tables_tests_stage_pdb tests/tables/test_stage_pdb.cpp)
  target_include_directories(rubik_tables_tests_stage_pdb PUBLIC src)
  target_link_libraries(rubik_tables_tests_stage_pdb PRIVATE rubik_tables rubik_core gtest_main)
  add_test(NAME rubik_tables_tests_stage_pdb COMMAND rubik_tables_tests_stage_pdb)

  foreach(_m cfop roux zz heise)
    if(TARGET rubik_${_m})
      add_executable(rubik_${_m}_tests tests/${_m}/test_${_m}.cpp)
      target_include_directories(rubik_${_m}_tests PUBLIC src)
      target_link_libraries(rubik_${_m}_tests PRIVATE rubik_${_m} rubik_core gtest_main)
      add_test(NAME rubik_${_m}_tests COMMAND rubik_${_m}_tests)
    endif()
  endforeach()
endif()