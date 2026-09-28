cmake_minimum_required(VERSION 3.20)
project(ViewportAvatarToolset VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VATS_BUILD_TESTS "Build the core tests" ON)
option(VATS_BUILD_UI "Build the shared editor UI, vats_ui (Dear ImGui, no SDL)" ON)
option(VATS_BUILD_APP "Build the standalone editor (needs SDL3 and vats_ui)" ON)

add_subdirectory(core)
if(VATS_BUILD_UI OR VATS_BUILD_APP)
    add_subdirectory(ui)
endif()
if(VATS_BUILD_APP)
    add_subdirectory(app)
endif()

if(VATS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_executable(vats_inworld_tests tools/inworld_tests.cpp)
target_link_libraries(vats_inworld_tests PRIVATE vats::core)

add_executable(vats_pose_preview tools/pose_preview.cpp)
target_link_libraries(vats_pose_preview PRIVATE vats::core)

add_executable(vats_make_test_body tools/make_test_body.cpp)
target_link_libraries(vats_make_test_body PRIVATE vats::core)

add_executable(vats_picker_dump tools/picker_dump.cpp)  # posed joints and body mesh as JSON, for picker design
target_link_libraries(vats_picker_dump PRIVATE vats::core)
add_executable(vats_example_review tools/example_review.cpp)  # frame strips and checks (docs/wiki/STYLE.md)
target_link_libraries(vats_example_review PRIVATE vats::core)
target_compile_definitions(vats_example_review PRIVATE VATS_ROOT="${PROJECT_SOURCE_DIR}")

add_executable(vats_make_wiki_examples tools/make_wiki_examples.cpp)  # docs/wiki/examples
target_link_libraries(vats_make_wiki_examples PRIVATE vats::core)
target_compile_definitions(vats_make_wiki_examples PRIVATE VATS_DATA_DIR="${PROJECT_SOURCE_DIR}/data/character"
                                                           VATS_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
