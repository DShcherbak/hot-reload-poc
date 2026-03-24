#include <iostream>
#include <thread>
#include <chrono>
#include <dlfcn.h>

#include "static_module.h"
#include "dlr/loader.h"
#include "rcp/raw_injector.h"
#include "bench/bench.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static void separator(const char* title) {
    std::cout << "\n══════════════════════════════════════\n"
              << "  " << title << "\n"
              << "══════════════════════════════════════\n";
}

// ── RCP demo ─────────────────────────────────────────────────────────────────

static void demo_rcp(const char* patch_bin, RawInjector& rcp_inj) {
    separator("RCP Demo — Raw Machine Code Injection");

    std::cout << "[RCP] Before patch: static_compute(5) = "
              << static_compute(5) << "  (2*5+1 = 11)\n";

    if (!rcp_inj.load_and_patch()) {
        std::cout << "[RCP] NOTE: run './build.sh raw' first to generate "
                  << patch_bin << "\n";
        return;
    }

    std::cout << "[RCP] After  patch: static_compute(5) = "
              << static_compute(5) << "\n";
    std::cout << "[RCP] Mechanism: compile -> objcopy (.text bytes)"
              << " -> mmap(EXEC) -> memcpy -> trampoline\n";
    std::cout << "[RCP] No .so opened — the replacement runs from anonymous mmap'd memory\n";
}

// ── Algorithm substitution demo ───────────────────────────────────────────────

static void demo_algorithm_patch(const char* sort_bin, RawInjector& sort_inj) {
    separator("Algorithm Substitution — Bubble Sort -> Shell Sort via RCP");

    // Show correctness with V1 (bubble sort, compiled into binary)
    int test[]  = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    int test2[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};

    std::cout << "[Algo] Input:          ";
    for (int x : test) std::cout << x << " ";
    std::cout << "\n";

    static_sort(test, 10);
    std::cout << "[Algo] Bubble sort:    ";
    for (int x : test) std::cout << x << " ";
    std::cout << "  (V1 correct)\n";

    // Use a fixed N for all three measurements so speedup and overhead are comparable.
    // N=3000: bubble sort ~12ms/iter (slow enough to show), shell sort ~0.3ms/iter (fast).
    constexpr int SORT_N = 3000;

    std::cout << "[Algo] Benchmarking bubble sort        (N=" << SORT_N << ", 5 iterations)...\n";
    BenchResult bubble = benchmark_sort_fn(static_sort, SORT_N, 5);

    // Patch static_sort to shell sort via raw injection
    if (!sort_inj.load_and_patch()) {
        std::cout << "[Algo] NOTE: run './build.sh sort' first to generate " << sort_bin << "\n";
        return;
    }

    // Verify correctness with V2 (shell sort, injected from .bin)
    static_sort(test2, 10);
    std::cout << "[Algo] Shell sort:     ";
    for (int x : test2) std::cout << x << " ";
    std::cout << "  (V2 correct — same call site, new algorithm)\n";

    // Benchmark post-patch execution — should match native speed
    std::cout << "[Algo] Benchmarking shell sort via RCP (N=" << SORT_N << ", 100 iter)...\n";
    BenchResult shell_rcp    = benchmark_sort_fn(static_sort, SORT_N, 100);

    std::cout << "[Algo] Benchmarking shell sort native  (N=" << SORT_N << ", 100 iter)...\n";
    BenchResult shell_native = benchmark_sort_fn(bench_shell_sort_ref, SORT_N, 100);

    print_algorithm_comparison(bubble, shell_rcp, shell_native);

    // RCP patch cost — O(1) regardless of function body size
    std::cout << "\n[Bench] RCP size independence: patching a small vs large function...\n";
    BenchResult rcp_small = benchmark_rcp(1000);
    BenchResult rcp_large = benchmark_rcp_large(1000);
    print_rcp_size_independence(rcp_small, rcp_large);
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

static void demo_loop(DLRLoader& dlr, RawInjector& rcp_inj) {
    separator("Hot-Reload Loop\n"
              "  DLR: edit dlr/plugin.cpp          -> cmake --build build --target plugin\n"
              "  RCP: edit rcp/patch_plugin.cpp     -> ./build.sh raw");

    int i = 0;
    while (true) {
        if (dlr.reload_if_changed())
            std::cout << "[DLR] New plugin loaded!\n";

        if (rcp_inj.repatch_if_changed())
            std::cout << "[RCP] Trampoline updated — static_compute now calls new code!\n";

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
    const char* plugin_v2_so    = (argc > 2) ? argv[2] : "./build/libplugin_v2.so";
    const char* plugin_large_so = (argc > 3) ? argv[3] : "./build/libplugin_large.so";
    const char* patch_bin       = (argc > 4) ? argv[4] : "./build/patch_snippet.bin";
    const char* sort_bin        = (argc > 5) ? argv[5] : "./build/sort_snippet.bin";

    separator("Hot-Reload Proof of Concept  (DLR + RCP)");

    // ── Phase 1: DLR initial load ─────────────────────────────────────────
    separator("DLR Demo — Dynamic Library Reload");
    DLRLoader loader(plugin_so);
    if (!loader.load()) return 1;
    std::cout << "[DLR] Initial call: plugin_compute(5) = " << loader.get_fn()(5) << "\n";

    // ── Phase 2: RCP — raw injection ──────────────────────────────────────
    RawInjector rcp_inj(reinterpret_cast<void*>(static_compute), patch_bin);
    demo_rcp(patch_bin, rcp_inj);

    // ── Phase 3: Algorithm substitution + size-independence benchmark ────
    RawInjector sort_inj(reinterpret_cast<void*>(static_sort), sort_bin);
    demo_algorithm_patch(sort_bin, sort_inj);

    // ── Phase 4: DLR vs RCP latency benchmark ────────────────────────────
    demo_benchmark(plugin_so, plugin_large_so);

    // ── Phase 5: New-function demo ────────────────────────────────────────
    demo_new_functions(plugin_so, plugin_v2_so);

    // ── Phase 6: Live hot-reload loop (both DLR and RCP) ──────────────────
    demo_loop(loader, rcp_inj);

    return 0;
}
