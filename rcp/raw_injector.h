#pragma once
#include <string>
#include <cstddef>
#include <ctime>

// RawInjector — patches a target function by injecting machine code read
// directly from a flat binary file (.bin, raw .text section bytes).
//
// Compared to RCPReloader (which uses dlopen to get the replacement address),
// RawInjector requires NO shared library, NO dynamic linker, and NO ELF
// relocation infrastructure. It is the minimal possible hot-patch path:
//
//   compile → objcopy → binary blob → mmap(EXEC) → memcpy → rcp_patch
//
// Limitation: the binary blob must be position-independent and must not
// reference external symbols (no calls to printf, malloc, etc.). Pure
// arithmetic functions compile to naturally position-independent code on x86-64.
class RawInjector {
public:
    // target_fn  — address of the statically linked function to redirect
    // bin_path   — path to the raw machine-code binary (extracted .text section)
    RawInjector(void* target_fn, const std::string& bin_path);
    ~RawInjector();

    // Read bin_path, mmap executable memory, copy bytes, write trampoline.
    bool load_and_patch();

    // Stat bin_path; if mtime changed, reload and re-patch.
    bool repatch_if_changed();

private:
    void*       target_fn_;
    std::string bin_path_;
    void*       code_region_ = nullptr;
    size_t      code_size_   = 0;
    time_t      mtime_       = 0;

    time_t query_mtime() const;
    void   unmap();
};
