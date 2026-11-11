cmake_minimum_required(VERSION 3.28)
project(Helix.Font LANGUAGES CXX)

if(NOT COMMAND helix_add_library)
    include("${CMAKE_CURRENT_SOURCE_DIR}/External/Helix.CMake/Helix.cmake")
endif()

helix_require(Core)
helix_require(Platform)
helix_require(Profiler)
helix_require(Jobs)
helix_require(Image)
helix_add_library(helix_font ALIAS Font PUBLIC_DEPS Helix::Core PRIVATE_DEPS Helix::Image)
# Copied public domain code, see THIRD-PARTY.txt.
target_include_directories(helix_font PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty)

helix_add_test_suite(helix_font_tests DEPS Helix::Font Helix::Image)
helix_add_benchmark_suite(helix_font_bench DEPS Helix::Font Helix::Image)
if(TARGET helix_font_bench)
    # The benchmarks build their fonts with the test font writer.
    target_include_directories(helix_font_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests)
endif()
helix_add_fuzz_suite(helix_font_fuzz DEPS Helix::Font Helix::Image ROUNDS 1000)
