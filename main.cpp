#include <iostream>
#include <thread>
#include <chrono>
#include <dlfcn.h>

#include "static_module.h"
#include "dlr/loader.h"
#include "rcp/patcher.h"
#include "rcp/rcp_reloader.h"
#include "rcp/raw_injector.h"
#include "bench/bench.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static void separator(const char* title) {
    std::cout << "\n══════════════════════════════════════\n"
              << "  " << title << "\n"
              << "══════════════════════════════════════\n";
}

// ── Raw injection demo ────────────────────────────────────────────────────────

static void demo_raw_inject(const char* patch_bin) {
    separator("RCP Raw Injection Demo — no .so, no dynamic linker");

    std::cout << "[RawInject] Before patch: static_compute_raw(5) = "
              << static_compute_raw(5)
              << "  (4*5+3 = 23)\n";

    RawInjector raw_inj(reinterpret_cast<void*>(static_compute_raw), patch_bin);
    if (!raw_inj.load_and_patch()) {
        std::cout << "[RawInject] NOTE: run './build.sh raw' first to generate "
                  << patch_bin << "\n";
        return;
    }

    std::cout << "[RawInject] After  patch: static_compute_raw(5) = "
              << static_compute_raw(5) << "\n";
    std::cout << "[RawInject] The replacement ran from mmap'd anonymous memory"
              << " — no .so was opened\n";
    std::cout << "[RawInject] Mechanism: compile -> objcopy (.text bytes)"
              << " -> mmap(EXEC) -> memcpy -> trampoline\n";
}

// ── Benchmark demo ────────────────────────────────────────────────────────────

static void demo_benchmark(const char* plugin_so, const char* plugin_large_so) {
    separator("Benchmark — RCP vs DLR mechanism latency");

    std::cout << "[Bench] RCP            — 1000 iterations...\n";
    BenchResult rcp       = benchmark_rcp(1000);

    std::cout << "[Bench] DLR  1 symbol  — 100  iterations...\n";
    BenchResult dlr_small = benchmark_dlr(plugin_so, 100);

    std::cout << "[Bench] DLR 1000 syms  — 50   iterations...\n";
    BenchResult dlr_large = benchmark_dlr(plugin_large_so, 50);

    print_bench_comparison(rcp, dlr_small, dlr_large);
}

// ── New-function demo ─────────────────────────────────────────────────────────

static void demo_new_functions(const char* plugin_so, const char* plugin_v2_so) {
    separator("New Functions Demo — DLR can add, RCP can only redirect");

    void* h1 = dlopen(plugin_so, RTLD_NOW | RTLD_LOCAL);
    if (!h1) { std::cerr << "[NewFn] dlopen V1 failed: " << dlerror() << "\n"; return; }
    void* extra_v1 = dlsym(h1, "plugin_extra");
    std::cout << "[NewFn] V1 plugin_extra symbol: "
              << (extra_v1 ? "FOUND" : "NOT FOUND") << "  (expected NOT FOUND)\n";
    dlclose(h1);

    void* h2 = dlopen(plugin_v2_so, RTLD_NOW | RTLD_LOCAL);
    if (!h2) { std::cerr << "[NewFn] dlopen V2 failed: " << dlerror() << "\n"; return; }

    auto compute_fn = reinterpret_cast<int(*)(int)>(dlsym(h2, "plugin_compute"));
    auto extra_fn   = reinterpret_cast<int(*)(int,int)>(dlsym(h2, "plugin_extra"));

    if (compute_fn)
        std::cout << "[NewFn] V2 plugin_compute(5)  = " << compute_fn(5)
                  << "  (expected 15 = 5*3)\n";
    if (extra_fn)
        std::cout << "[NewFn] V2 plugin_extra(3, 4) = " << extra_fn(3, 4)
                  << "  (expected 13 = 3*3+4)\n";

    dlclose(h2);

    std::cout << "\n[NewFn] RCP cannot add new functions — it can only redirect existing\n"
              << "        call sites. No `call plugin_extra` instruction exists in this binary.\n";
}

// ── Combined hot-reload loop ───────────────────────────────────────────────────

static void demo_loop(DLRLoader& dlr, RCPReloader& rcp_rel) {
    separator("Hot-Reload Loop\n"
              "  DLR: edit dlr/plugin.cpp       -> cmake --build build --target plugin\n"
              "  RCP: edit rcp/patch_plugin.cpp -> cmake --build build --target patch_plugin");

    int i = 0;
    while (true) {
        if (dlr.reload_if_changed())
            std::cout << "[DLR] New plugin loaded!\n";

        if (rcp_rel.repatch_if_changed())
            std::cout << "[RCP] Trampoline updated — static_compute now calls new implementation!\n";

        auto fn = dlr.get_fn();
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
    const char* plugin_so       = (argc > 1) ? argv[1] : "./build/libplugin.so";
    const char* patch_so        = (argc > 2) ? argv[2] : "./build/libpatch_plugin.so";
    const char* plugin_v2_so    = (argc > 3) ? argv[3] : "./build/libplugin_v2.so";
    const char* plugin_large_so = (argc > 4) ? argv[4] : "./build/libplugin_large.so";
    const char* patch_bin       = (argc > 5) ? argv[5] : "./build/patch_snippet.bin";

    separator("Hot-Reload Proof of Concept  (DLR + RCP)");

    // ── Phase 1: DLR initial load ─────────────────────────────────────────
    separator("DLR Demo — Dynamic Library Reload");
    DLRLoader loader(plugin_so);
    if (!loader.load()) return 1;
    std::cout << "[DLR] Initial call: plugin_compute(5) = " << loader.get_fn()(5) << "\n";

    // ── Phase 2: RCP initial patch ────────────────────────────────────────
    separator("RCP Demo — Runtime Code Patching");
    std::cout << "[RCP] Before patch: static_compute(5) = " << static_compute(5)
              << "  (2*5+1 = 11)\n";

    // RCPReloader owns the dlopen handle and re-patches whenever patch_plugin.so changes.
    RCPReloader rcp_reloader(reinterpret_cast<void*>(static_compute),
                             patch_so, "patched_compute");
    if (!rcp_reloader.load_and_patch()) return 1;

    std::cout << "[RCP] After  patch: static_compute(5) = " << static_compute(5)
              << "  (patched)\n";

    // ── Phase 2.5: Raw injection demo ─────────────────────────────────────
    demo_raw_inject(patch_bin);

    // ── Phase 3: Benchmark ────────────────────────────────────────────────
    demo_benchmark(plugin_so, plugin_large_so);

    // ── Phase 4: New-function demo ────────────────────────────────────────
    demo_new_functions(plugin_so, plugin_v2_so);

    // ── Phase 5: Live hot-reload loop (both DLR and RCP) ──────────────────
    demo_loop(loader, rcp_reloader);

    return 0;
}
