# The standalone editor: the shared UI (ui/, vats_ui) in an SDL3 window with OpenGL 3.3, plus the app's own
# 3D scene renderer. Everything here is the host side of ui::Host (sdl_host.*).

# System SDL3 first; otherwise build the pinned release as a shared library next to the app.
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found on the system: fetching release-3.4.16")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# ImGui's SDL3 platform glue: the app's only (the viewer has its own).
add_library(imgui_sdl3 STATIC ${VATS_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp)
target_link_libraries(imgui_sdl3 PUBLIC imgui SDL3::SDL3)

add_executable(vats
    file_types.cpp
    gl.cpp
    main.cpp
    renderer.cpp
    sdl_host.cpp)
target_link_libraries(vats PRIVATE vats::ui imgui_opengl3 imgui_sdl3 SDL3::SDL3)
target_compile_definitions(vats PRIVATE VATS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(WIN32)
    set_target_properties(vats PROPERTIES WIN32_EXECUTABLE ON)  # no console window
    target_sources(vats PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/vats.rc)  # exe icon
endif()

# Install layout: bin/vats (+ SDL3 beside it), share/viewport-avatar-toolset/{data,assets,packaging,help}.
# The app looks beside itself first, then in ../share/viewport-avatar-toolset (see main.cpp).
set_target_properties(vats PROPERTIES INSTALL_RPATH "$ORIGIN")
include(GNUInstallDirs)
install(TARGETS vats RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET SDL3-shared AND NOT SDL3_FOUND)
    install(TARGETS SDL3-shared RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} NAMELINK_SKIP)
endif()
set(VATS_SHARE ${CMAKE_INSTALL_DATADIR}/viewport-avatar-toolset)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/ DESTINATION ${VATS_SHARE}/data)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/app/assets/ DESTINATION ${VATS_SHARE}/assets)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/packaging/linux/ DESTINATION ${VATS_SHARE}/packaging)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/docs/wiki/ DESTINATION ${VATS_SHARE}/help  # the Help window's pages
        FILES_MATCHING PATTERN "*.md" PATTERN "*.png" PATTERN "*.gif" PATTERN "*.vat" PATTERN "*.wav" PATTERN "STYLE.md" EXCLUDE)  # + images/, examples/
foreach(size 16 32 48 64 128 256)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/viewport-avatar-toolset-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME viewport-avatar-toolset.png)
endforeach()
install(FILES ${PROJECT_SOURCE_DIR}/packaging/icons/viewport-avatar-toolset.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md ${PROJECT_SOURCE_DIR}/README.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vats PRIVATE -Wall -Wextra)
endif()
