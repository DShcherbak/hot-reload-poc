#pragma once
#include <string>
#include <ctime>

// RCPReloader watches a shared library for changes and re-patches a statically
// linked target function on every change.
//
// Workflow:
//   1. load_and_patch()  — call once at startup
//   2. repatch_if_changed() — call in the main loop
//
// When the .so file is modified (e.g. after `cmake --build build --target patch_plugin`):
//   - the old handle is closed
//   - the new .so is opened via dlopen
//   - dlsym resolves the replacement function's (potentially new) address
//   - rcp_patch() overwrites the target's trampoline with the updated address
//
// Because the trampoline is re-written atomically (12-byte memcpy), all
// subsequent calls to `target_fn` land in the new implementation with no
// changes to the call site.
class RCPReloader {
public:
    // target_fn    — address of the statically linked function to redirect
    // so_path      — path to the shared library containing the replacement
    // replacement_sym — exported symbol name of the replacement function
    RCPReloader(void* target_fn,
                const std::string& so_path,
                const std::string& replacement_sym);
    ~RCPReloader();

    // Load the .so, resolve the replacement, and write the initial trampoline.
    bool load_and_patch();

    // Stat the .so; if mtime changed, reload and re-patch.
    // Returns true if a repatch actually happened.
    bool repatch_if_changed();

private:
    void*       target_fn_;
    std::string so_path_;
    std::string sym_name_;
    void*       handle_  = nullptr;
    time_t      mtime_   = 0;

    time_t query_mtime() const;
    void   unload();
};
