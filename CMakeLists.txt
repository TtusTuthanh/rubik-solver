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

add_library(rubik_tables src/tables/tables_loader.cpp src/tables/adapter.cpp)
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

add_executable(rubik-visualiser src/visualiser.cpp)
target_include_directories(rubik-visualiser PUBLIC src tests)
target_link_libraries(rubik-visualiser PRIVATE rubik_core)
foreach(_t rubik_cfop rubik_roux rubik_zz rubik_heise rubik_twophase)
  if(TARGET ${_t})
    target_link_libraries(rubik-visualiser PRIVATE ${_t})
  endif()
endforeach()

if(BUILD_TESTING)
  add_executable(rubik_core_tests tests/core/test_cube.cpp tests/core/test_validation.cpp)
  target_include_directories(rubik_core_tests PUBLIC src)
  target_link_libraries(rubik_core_tests PRIVATE rubik_core gtest_main)
  add_test(NAME rubik_core_tests COMMAND rubik_core_tests)
  foreach(_m cfop roux zz heise)
    if(TARGET rubik_${_m})
      add_executable(rubik_${_m}_tests tests/${_m}/test_${_m}.cpp)
      target_include_directories(rubik_${_m}_tests PUBLIC src)
      target_link_libraries(rubik_${_m}_tests PRIVATE rubik_${_m} rubik_core gtest_main)
      add_test(NAME rubik_${_m}_tests COMMAND rubik_${_m}_tests)
    endif()
  endforeach()
endif()
