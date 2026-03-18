#include "bench.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>

// ── RCP target functions ──────────────────────────────────────────────────────
// Both functions must be large enough to hold the 12-byte trampoline.
// The volatile assignments prevent the compiler from optimising the bodies away.

static __attribute__((noinline, used)) int bench_fn_v1(int x) {
    volatile int a = x * 2;
    volatile int b = a + 3;
    volatile int c = b * b;
    volatile int d = c - a;
    return d;
}

static __attribute__((noinline, used)) int bench_fn_v2(int x) {
    volatile int a = x * 5;
    volatile int b = a - 7;
    volatile int c = b * 3;
    volatile int d = c + a;
    return d;
}

// ── benchmark_rcp ─────────────────────────────────────────────────────────────

BenchResult benchmark_rcp(int iterations) {
    const long   page_size = sysconf(_SC_PAGESIZE);
    auto         page_mask = static_cast<uintptr_t>(page_size - 1);
    void*        target    = reinterpret_cast<void*>(bench_fn_v1);
    void*        replace   = reinterpret_cast<void*>(bench_fn_v2);
    void*        page      = reinterpret_cast<void*>(
                                 reinterpret_cast<uintptr_t>(target) & ~page_mask);

    // Build the 12-byte absolute-JMP trampoline:  48 B8 <8-byte addr> FF E0
    uint8_t trampoline[12];
    trampoline[0] = 0x48;
    trampoline[1] = 0xB8;
    uint64_t addr = reinterpret_cast<uint64_t>(replace);
    std::memcpy(trampoline + 2, &addr, sizeof(addr));
    trampoline[10] = 0xFF;
    trampoline[11] = 0xE0;

    // Warm-up: 5 iterations (not timed)
    for (int w = 0; w < 5; ++w) {
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_WRITE | PROT_EXEC);
        std::memcpy(target, trampoline, 12);
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC);
    }

    double min_ns  = 1e18;
    double max_ns  = 0.0;
    double total   = 0.0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();

        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_WRITE | PROT_EXEC);
        std::memcpy(target, trampoline, 12);
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC);

        auto t1  = std::chrono::high_resolution_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        total += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
    }

    return { total / iterations, min_ns, max_ns, iterations };
}

// ── benchmark_dlr ─────────────────────────────────────────────────────────────

BenchResult benchmark_dlr(const char* so_path, int iterations) {
    // Warm-up: 3 iterations (not timed)
    for (int w = 0; w < 3; ++w) {
        void* h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
        if (h) {
            dlsym(h, "plugin_compute");
            dlclose(h);
        }
    }

    double min_ns = 1e18;
    double max_ns = 0.0;
    double total  = 0.0;

    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();

        void* h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
        dlsym(h, "plugin_compute");
        dlclose(h);

        auto t1  = std::chrono::high_resolution_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        total += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
    }

    return { total / iterations, min_ns, max_ns, iterations };
}

// ── print_bench_comparison ────────────────────────────────────────────────────

static void print_row(const char* label, const BenchResult& r) {
    std::cout << "  │ " << std::left  << std::setw(18) << label   << " │ "
              << std::right << std::setw(12) << r.mean_ns           << " │ "
              << std::right << std::setw(12) << r.min_ns            << " │ "
              << std::right << std::setw(12) << r.max_ns            << " │ "
              << std::right << std::setw(10) << r.iterations        << " │\n";
}

void print_bench_comparison(const BenchResult& rcp,
                             const BenchResult& dlr_small,
                             const BenchResult& dlr_large) {
    std::cout << std::fixed << std::setprecision(1) << std::setfill(' ');
    std::cout << "\n";
    std::cout << "  ┌────────────────────┬──────────────┬──────────────┬──────────────┬────────────┐\n";
    std::cout << "  │ Mechanism          │   mean (ns)  │    min (ns)  │    max (ns)  │ iterations │\n";
    std::cout << "  ├────────────────────┼──────────────┼──────────────┼──────────────┼────────────┤\n";
    print_row("RCP (12 B patch)",  rcp);
    print_row("DLR  1 symbol",     dlr_small);
    print_row("DLR  1000 symbols", dlr_large);
    std::cout << "  └────────────────────┴──────────────┴──────────────┴──────────────┴────────────┘\n";

    double factor_small = dlr_small.mean_ns / rcp.mean_ns;
    double factor_large = dlr_large.mean_ns / rcp.mean_ns;
    std::cout << "\n  RCP vs DLR-small:  " << std::setprecision(1) << factor_small << "x\n";
    std::cout << "  RCP vs DLR-large:  " << std::setprecision(1) << factor_large << "x\n";
    std::cout << "\n  Key insight:\n"
              << "    RCP cost is O(1) — always 2 mprotect syscalls + 12-byte memcpy,\n"
              << "    regardless of how much code changed.\n"
              << "    DLR cost is O(symbols) — the dynamic linker must parse the full ELF\n"
              << "    symbol table, process relocations, and remap pages for every reload.\n"
              << "    The DLR-small result looks competitive only because the 1-symbol .so\n"
              << "    is trivially cheap to parse. Real application libraries (thousands of\n"
              << "    symbols, cross-library dependencies) push DLR latency into the ms range.\n";
}
