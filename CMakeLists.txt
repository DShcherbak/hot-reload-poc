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

# ── DLR plugin v2: libplugin_v2.so ───────────────────────────────────────────
# V2 adds plugin_extra — demonstrates that DLR can expose brand-new symbols.
add_library(plugin_v2 SHARED
    dlr/plugin_v2.cpp
)
set_target_properties(plugin_v2 PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ── Large DLR benchmark plugin: libplugin_large.so ───────────────────────────
# Generated at configure time with 1000 exported dummy symbols so that dlopen's
# ELF relocation and symbol-table pass takes measurable time. This exposes the
# O(symbol count) cost of DLR that is invisible with a 1-symbol toy library.
set(_LARGE_SRC "// Auto-generated — do not edit\n")
string(APPEND _LARGE_SRC "extern \"C\" int plugin_compute(int x) { return x * 3; }\n")
foreach(_i RANGE 1 999)
    string(APPEND _LARGE_SRC "extern \"C\" int dummy_sym_${_i}(int x) { return x + ${_i}; }\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/plugin_large.cpp" "${_LARGE_SRC}")

add_library(plugin_large SHARED "${CMAKE_BINARY_DIR}/plugin_large.cpp")
set_target_properties(plugin_large PROPERTIES
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

# ── Raw machine code target: patch_snippet.bin ────────────────────────────────
# Compiles patch_plugin.cpp to an object file, then extracts the raw .text
# section bytes. RawInjector maps these bytes directly into executable memory
# without involving the dynamic linker at all.
add_custom_target(patch_raw
    COMMAND ${CMAKE_CXX_COMPILER}
            -O0 -fPIC -std=c++17
            -c ${CMAKE_SOURCE_DIR}/rcp/patch_plugin.cpp
            -o ${CMAKE_BINARY_DIR}/patch_snippet.o
    COMMAND objcopy -O binary --only-section=.text
            ${CMAKE_BINARY_DIR}/patch_snippet.o
            ${CMAKE_BINARY_DIR}/patch_snippet.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Extracting raw machine code from patch_plugin.cpp -> patch_snippet.bin"
    VERBATIM
)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(hot_reload
    main.cpp
    static_module.cpp
    dlr/loader.cpp
    rcp/patcher.cpp
    rcp/rcp_reloader.cpp
    rcp/raw_injector.cpp
    bench/bench.cpp
)
target_include_directories(hot_reload PRIVATE "${CMAKE_SOURCE_DIR}")
# Link libdl for dlopen/dlsym/dlclose.
target_link_libraries(hot_reload PRIVATE dl)
# Do NOT link plugin or patch_plugin here — they are loaded at runtime.
