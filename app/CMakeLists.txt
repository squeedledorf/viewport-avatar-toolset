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
# The Help window's pages: the tutorials, with their pictures and example projects. Every other page of docs/wiki is
# the online manual (the GitHub wiki, tools/publish-wiki.sh), listed in online-pages.tsv so links to them open there,
# or in the manual a user downloads (Help > Download the Manual).
set(VATS_HELP ${PROJECT_SOURCE_DIR}/docs/wiki)
file(GLOB VATS_HELP_PAGES RELATIVE ${VATS_HELP} CONFIGURE_DEPENDS ${VATS_HELP}/*.md)
set(VATS_ONLINE_INDEX "")
foreach(page IN LISTS VATS_HELP_PAGES)
    if(page MATCHES "^(tutorial.*|STYLE|README|hex.*-import)\\.md$")
        continue()
    endif()
    file(STRINGS ${VATS_HELP}/${page} title LIMIT_COUNT 1 REGEX "^# ")
    file(STRINGS ${VATS_HELP}/${page} category LIMIT_COUNT 1 REGEX "^Category: ")
    string(REGEX REPLACE "^# " "" title "${title}")
    string(REGEX REPLACE "^Category: " "" category "${category}")
    string(REGEX REPLACE "\\.md$" "" stem "${page}")
    string(APPEND VATS_ONLINE_INDEX "${stem}\t${title}\t${category}\n")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/online-pages.tsv "${VATS_ONLINE_INDEX}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/online-pages.tsv DESTINATION ${VATS_SHARE}/help)
file(GLOB VATS_TUTORIAL_PAGES CONFIGURE_DEPENDS ${VATS_HELP}/tutorial*.md)
file(GLOB VATS_TUTORIAL_IMAGES LIST_DIRECTORIES true CONFIGURE_DEPENDS ${VATS_HELP}/images/tutorial*)
install(FILES ${VATS_TUTORIAL_PAGES} DESTINATION ${VATS_SHARE}/help)
install(DIRECTORY ${VATS_TUTORIAL_IMAGES} DESTINATION ${VATS_SHARE}/help/images FILES_MATCHING PATTERN "*.png" PATTERN "*.gif")
install(DIRECTORY ${VATS_HELP}/examples DESTINATION ${VATS_SHARE}/help FILES_MATCHING PATTERN "*.vat" PATTERN "*.wav")
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
