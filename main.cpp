#include <iostream>
#include <thread>
#include <chrono>
#include <dlfcn.h>

#include "static_module.h"
#include "dlr/loader.h"
#include "rcp/patcher.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static void separator(const char* title) {
    std::cout << "\n══════════════════════════════════════\n"
              << "  " << title << "\n"
              << "══════════════════════════════════════\n";
}

// ── RCP demo: load patch plugin and redirect static_compute ──────────────────

static void demo_rcp(const char* patch_so_path) {
    separator("RCP Demo — Runtime Code Patching");

    std::cout << "[RCP] Before patch: static_compute(5) = "
              << static_compute(5) << "  (expected 11 = 5*2+1)\n";

    // Load the shared library that contains the replacement function.
    // The main binary was never designed to call this library — it is loaded
    // purely to obtain the address of patched_compute.
    void* patch_handle = dlopen(patch_so_path, RTLD_NOW | RTLD_LOCAL);
    if (!patch_handle) {
        std::cerr << "[RCP] dlopen patch plugin failed: " << dlerror() << "\n";
        return;
    }

    void* new_fn = dlsym(patch_handle, "patched_compute");
    if (!new_fn) {
        std::cerr << "[RCP] dlsym patched_compute failed: " << dlerror() << "\n";
        dlclose(patch_handle);
        return;
    }

    // Overwrite the first 12 bytes of static_compute with an absolute JMP.
    // After this point every call to static_compute() — including direct calls
    // already compiled into this binary — silently executes patched_compute().
    if (!rcp_patch(reinterpret_cast<void*>(static_compute), new_fn)) {
        std::cerr << "[RCP] Patching failed.\n";
        dlclose(patch_handle);
        return;
    }

    std::cout << "[RCP] After  patch: static_compute(5) = "
              << static_compute(5) << "  (expected 25 = 5*5)\n";

    // Keep patch_handle open — patched_compute must remain mapped in memory.
    // In a real system you would store this handle for later dlclose().
    (void)patch_handle;
}

// ── DLR demo: hot-reload loop ─────────────────────────────────────────────────

static void demo_dlr_loop(DLRLoader& loader) {
    separator("DLR Loop — edit dlr/plugin.cpp, then run: cmake --build build --target plugin");

    int i = 0;
    while (true) {
        bool reloaded = loader.reload_if_changed();
        if (reloaded)
            std::cout << "[DLR] New plugin loaded!\n";

        auto fn = loader.get_fn();

        std::cout << "  [DLR] plugin_compute(" << i << ") = ";
        if (fn) std::cout << fn(i);
        else    std::cout << "<not loaded>";

        std::cout << "   [RCP] static_compute(" << i << ") = "
                  << static_compute(i) << "\n";

        ++i;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

// ── entry point ───────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Paths can be overridden via argv for out-of-tree builds.
    const char* plugin_so     = (argc > 1) ? argv[1] : "./build/libplugin.so";
    const char* patch_so      = (argc > 2) ? argv[2] : "./build/libpatch_plugin.so";

    separator("Hot-Reload Proof of Concept  (DLR + RCP)");

    // ── Phase 1: DLR — load the plugin library ────────────────────────────
    separator("DLR Demo — Dynamic Library Reload");
    DLRLoader loader(plugin_so);
    if (!loader.load())
        return 1;

    auto fn = loader.get_fn();
    std::cout << "[DLR] Initial call: plugin_compute(5) = " << fn(5)
              << "  (expected 15 = 5+10)\n";

    // ── Phase 2: RCP — patch the statically linked function ───────────────
    demo_rcp(patch_so);

    // ── Phase 3: combined loop ─────────────────────────────────────────────
    demo_dlr_loop(loader);

    return 0;
}
