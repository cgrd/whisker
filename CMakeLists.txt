cmake_minimum_required(VERSION 3.20)
project(whisker VERSION 1.1.1 LANGUAGES CXX C) # x-release-please-version

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(TRANSCRIBE_CPP_TAG 4d7c1ccefd39e5a9ea36cca5f8fefc6dcc14b99b CACHE STRING "transcribe.cpp commit or branch")
set(IMGUI_TAG v1.93.0 CACHE STRING "Dear ImGui tag")
FetchContent_Declare(transcribe_cpp
    GIT_REPOSITORY https://github.com/handy-computer/transcribe.cpp
    GIT_TAG ${TRANSCRIBE_CPP_TAG}
)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG ${IMGUI_TAG}
)

option(WHISKER_VULKAN "Enable Vulkan GPU acceleration" ON)
set(TRANSCRIBE_VULKAN ${WHISKER_VULKAN} CACHE BOOL "" FORCE)
set(TRANSCRIBE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TRANSCRIBE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TRANSCRIBE_USE_SYSTEM_BLAS OFF CACHE BOOL "" FORCE)

# ggml defaults to -march=native, which made CI builds depend on the runner's CPU and
# ship AVX-512 code that crashes elsewhere. Runtime dispatch (GGML_CPU_ALL_VARIANTS)
# needs shared ggml backends, which the static linking policy rules out.
option(WHISKER_NATIVE "Optimize for this machine's CPU instead of the x86-64-v3 baseline" OFF)
set(GGML_NATIVE ${WHISKER_NATIVE} CACHE BOOL "" FORCE)
if(NOT WHISKER_NATIVE)
    foreach(_flag GGML_SSE42 GGML_AVX GGML_AVX2 GGML_BMI2 GGML_FMA GGML_F16C)
        set(${_flag} ON CACHE BOOL "" FORCE)
    endforeach()
    foreach(_flag GGML_AVX_VNNI GGML_AVX512 GGML_AVX512_VBMI GGML_AVX512_VNNI GGML_AVX512_BF16
                  GGML_AMX_TILE GGML_AMX_INT8 GGML_AMX_BF16)
        set(${_flag} OFF CACHE BOOL "" FORCE)
    endforeach()
endif()

FetchContent_GetProperties(transcribe_cpp)
if(NOT transcribe_cpp_POPULATED)
    FetchContent_Populate(transcribe_cpp)
    add_subdirectory(${transcribe_cpp_SOURCE_DIR} ${transcribe_cpp_BINARY_DIR})
    target_include_directories(transcribe PUBLIC
        ${transcribe_cpp_SOURCE_DIR}/include
    )
endif()

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

find_package(PkgConfig REQUIRED)

set(STATIC_PREFIX "" CACHE PATH "Prefix for statically-linked dependencies")

if(STATIC_PREFIX)
    set(_SL ${STATIC_PREFIX}/lib)

    set(SDL2_INCLUDE_DIRS ${STATIC_PREFIX}/include/SDL2)
    set(SDL2_LIBRARIES ${_SL}/libSDL2.a wayland-client wayland-egl wayland-cursor xkbcommon dl pthread rt)

    set(LIBEVDEV_INCLUDE_DIRS ${STATIC_PREFIX}/include/libevdev-1.0)
    set(LIBEVDEV_LIBRARIES ${_SL}/libevdev.a)

    set(OPUSENC_INCLUDE_DIRS ${STATIC_PREFIX}/include/opus)
    set(OPUSENC_LIBRARIES ${_SL}/libopusenc.a ${_SL}/libopus.a)

    set(CAIRO_INCLUDE_DIRS ${STATIC_PREFIX}/include/cairo ${STATIC_PREFIX}/include/pixman-1)
    set(CAIRO_LIBRARIES ${_SL}/libcairo.a ${_SL}/libpixman-1.a ${_SL}/libpng16.a ${_SL}/libz.a pthread m)

    set(FT2_INCLUDE_DIRS ${STATIC_PREFIX}/include/freetype2)
    set(FT2_LIBRARIES ${_SL}/libfreetype.a ${_SL}/libpng16.a ${_SL}/libz.a)

    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
else()
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(LIBEVDEV REQUIRED libevdev)
    pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
    pkg_check_modules(OPUSENC REQUIRED libopusenc)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
    pkg_check_modules(CAIRO REQUIRED cairo)
    pkg_check_modules(FT2 REQUIRED freetype2)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
endif()

find_package(X11 REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC ${SDL2_LIBRARIES})

# Generate wlr-layer-shell protocol bindings
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/protocol)
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/protocol)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(LAYER_SHELL_XML ${PROTO_DIR}/wlr-layer-shell-unstable-v1.xml)
set(LAYER_SHELL_H ${PROTO_GEN_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h)
set(LAYER_SHELL_C ${PROTO_GEN_DIR}/wlr-layer-shell-unstable-v1-client-protocol.c)

set(XDG_SHELL_XML /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
set(XDG_SHELL_H ${PROTO_GEN_DIR}/xdg-shell-client-protocol.h)
set(XDG_SHELL_C ${PROTO_GEN_DIR}/xdg-shell-client-protocol.c)

add_custom_command(OUTPUT ${LAYER_SHELL_H}
    COMMAND ${WAYLAND_SCANNER} client-header ${LAYER_SHELL_XML} ${LAYER_SHELL_H}
    DEPENDS ${LAYER_SHELL_XML})
add_custom_command(OUTPUT ${LAYER_SHELL_C}
    COMMAND ${WAYLAND_SCANNER} private-code ${LAYER_SHELL_XML} ${LAYER_SHELL_C}
    DEPENDS ${LAYER_SHELL_XML})
add_custom_command(OUTPUT ${XDG_SHELL_H}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${XDG_SHELL_H}
    DEPENDS ${XDG_SHELL_XML})
add_custom_command(OUTPUT ${XDG_SHELL_C}
    COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${XDG_SHELL_C}
    DEPENDS ${XDG_SHELL_XML})

# Generate embedded font header
set(FONT_TTF ${CMAKE_SOURCE_DIR}/fonts/AtkinsonHyperlegible-Regular.ttf)
set(FONT_HEADER ${CMAKE_BINARY_DIR}/font_data.h)
add_custom_command(OUTPUT ${FONT_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env xxd -i AtkinsonHyperlegible-Regular.ttf > ${FONT_HEADER}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/fonts
    DEPENDS ${FONT_TTF}
    COMMENT "Embedding font as C array"
)

# Generate embedded icon header (raw RGBA, no decoder needed at runtime)
set(ICON_RGBA ${CMAKE_SOURCE_DIR}/assets/whisker-icon-256.rgba)
set(ICON_HEADER ${CMAKE_BINARY_DIR}/icon_data.h)
add_custom_command(OUTPUT ${ICON_HEADER}
    COMMAND ${CMAKE_COMMAND} -E env xxd -i whisker-icon-256.rgba > ${ICON_HEADER}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/assets
    DEPENDS ${ICON_RGBA}
    COMMENT "Embedding icon as C array"
)

add_library(whisker_assets STATIC src/font_embed.cpp src/icon_embed.cpp ${FONT_HEADER} ${ICON_HEADER})
target_include_directories(whisker_assets PUBLIC ${CMAKE_BINARY_DIR})

add_library(whisker_protocol OBJECT
    ${LAYER_SHELL_H}
    ${LAYER_SHELL_C}
    ${XDG_SHELL_H}
    ${XDG_SHELL_C}
)
target_include_directories(whisker_protocol PUBLIC ${PROTO_GEN_DIR} ${WAYLAND_CLIENT_INCLUDE_DIRS})

add_executable(whisker
    src/main.cpp
    src/config.cpp
    src/pedal.cpp
    src/keyboard.cpp
    src/hotkey.cpp
    src/buttons.cpp
    src/controllers.cpp
    src/command.cpp
    src/spawn.cpp
    src/audio.cpp
    src/audiocue.cpp
    src/dictation.cpp
    src/haptic.cpp
    src/stt.cpp
    src/overlay.cpp
    src/history.cpp
    src/clipboard.cpp
    src/tray.cpp
    src/settings.cpp
    src/trigger_capture.cpp
    src/overlay_gamescope.cpp
    src/overlay_layershell.cpp
)

target_include_directories(whisker PRIVATE
    ${LIBEVDEV_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${OPUSENC_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${FT2_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${SYSTEMD_INCLUDE_DIRS}
    ${PROTO_GEN_DIR}
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(whisker PRIVATE WHISKER_VERSION="${PROJECT_VERSION}")

target_link_libraries(whisker PRIVATE
    whisker_protocol
    transcribe
    ${LIBEVDEV_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${OPUSENC_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${CAIRO_LIBRARIES}
    ${FT2_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SYSTEMD_LIBRARIES}
    X11::X11
    imgui
    whisker_assets
)

install(TARGETS whisker RUNTIME DESTINATION bin)
install(FILES assets/whisker-icon-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME whisker.png)
install(FILES whisker.desktop DESTINATION share/applications)
install(FILES whisker.service DESTINATION share/systemd/user)
install(FILES 70-pedal-stt.rules DESTINATION lib/udev/rules.d RENAME 70-whisker.rules)

option(WHISKER_BUILD_TESTS "Build unit tests" ON)
if(WHISKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
