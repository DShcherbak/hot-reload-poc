cmake_minimum_required(VERSION 3.16)
project(hot_reload CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Debug so function bodies are large enough for the trampoline
# and symbols are easy to inspect in a disassembler.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ── shared compile options ────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra
    # Keep frame pointers — useful for stack unwinding through patched frames.
    -fno-omit-frame-pointer
)

# ── DLR plugin: libplugin.so ──────────────────────────────────────────────────
# Built as a separate target so it can be rebuilt independently:
#   cmake --build build --target plugin
add_library(plugin SHARED
    dlr/plugin.cpp
)
set_target_properties(plugin PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ── RCP patch plugin: libpatch_plugin.so ─────────────────────────────────────
# Contains the replacement for static_compute, loaded via dlopen at runtime.
add_library(patch_plugin SHARED
    rcp/patch_plugin.cpp
)
set_target_properties(patch_plugin PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(hot_reload
    main.cpp
    static_module.cpp
    dlr/loader.cpp
    rcp/patcher.cpp
)
target_include_directories(hot_reload PRIVATE "${CMAKE_SOURCE_DIR}")
# Link libdl for dlopen/dlsym/dlclose.
target_link_libraries(hot_reload PRIVATE dl)
# Do NOT link plugin or patch_plugin here — they are loaded at runtime.
