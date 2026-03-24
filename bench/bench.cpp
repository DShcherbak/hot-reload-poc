#include "bench.h"

#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

// ── RCP arithmetic targets (small, ~40 bytes each) ───────────────────────────
// Used by benchmark_rcp to measure patch cost for a small function.

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

// ── RCP sort targets (large, ~300 bytes each) ─────────────────────────────────
// Used by benchmark_rcp_large to measure patch cost for a large function,
// demonstrating that patch cost is O(1) regardless of code size.

static __attribute__((noinline, used)) void bench_sort_v1(int* arr, int n) {
    // Bubble sort — O(n²)
    for (int i = 0; i < n - 1; i++)
        for (int j = 0; j < n - i - 1; j++)
            if (arr[j] > arr[j + 1]) {
                int t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t;
            }
}

static __attribute__((noinline, used)) void bench_sort_v2(int* arr, int n) {
    // Shell sort — O(n^(4/3)) average
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            int temp = arr[i], j = i;
            while (j >= gap && arr[j - gap] > temp) {
                arr[j] = arr[j - gap]; j -= gap;
            }
            arr[j] = temp;
        }
}

// ── bench_shell_sort_ref ──────────────────────────────────────────────────────
// Native reference: identical algorithm to sort_plugin.cpp's patched_sort.
// Calling this directly (no trampoline) gives the baseline for overhead comparison.

void bench_shell_sort_ref(int* arr, int n) {
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            int temp = arr[i], j = i;
            while (j >= gap && arr[j - gap] > temp) {
                arr[j] = arr[j - gap]; j -= gap;
            }
            arr[j] = temp;
        }
}

// ── benchmark_sort_fn ─────────────────────────────────────────────────────────

BenchResult benchmark_sort_fn(void (*fn)(int*, int), int n, int iters) {
    // Build a fixed pseudo-random array (LCG, seed=42) once; copy it each iteration.
    std::vector<int> source(static_cast<size_t>(n));
    unsigned s = 42;
    for (int i = 0; i < n; i++) {
        s = s * 1664525u + 1013904223u;
        source[static_cast<size_t>(i)] = static_cast<int>(s % static_cast<unsigned>(n));
    }

    std::vector<int> arr(static_cast<size_t>(n));
    double min_ns = 1e18, max_ns = 0, total = 0;

    for (int i = 0; i < iters; i++) {
        arr = source;
        auto t0 = std::chrono::high_resolution_clock::now();
        fn(arr.data(), n);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        total += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
    }
    return { total / iters, min_ns, max_ns, iters };
}

// ── benchmark_rcp_large ───────────────────────────────────────────────────────

BenchResult benchmark_rcp_large(int iterations) {
    const long   page_size  = sysconf(_SC_PAGESIZE);
    auto         page_mask  = static_cast<uintptr_t>(page_size - 1);
    void*        target     = reinterpret_cast<void*>(bench_sort_v1);
    void*        replace    = reinterpret_cast<void*>(bench_sort_v2);
    void*        page       = reinterpret_cast<void*>(
                                  reinterpret_cast<uintptr_t>(target) & ~page_mask);

    uint8_t trampoline[12];
    trampoline[0] = 0x48; trampoline[1] = 0xB8;
    uint64_t addr = reinterpret_cast<uint64_t>(replace);
    std::memcpy(trampoline + 2, &addr, sizeof(addr));
    trampoline[10] = 0xFF; trampoline[11] = 0xE0;

    for (int w = 0; w < 5; ++w) {
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_WRITE | PROT_EXEC);
        std::memcpy(target, trampoline, 12);
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC);
    }

    double min_ns = 1e18, max_ns = 0, total = 0;
    for (int i = 0; i < iterations; i++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_WRITE | PROT_EXEC);
        std::memcpy(target, trampoline, 12);
        mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        total += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
    }
    return { total / iterations, min_ns, max_ns, iterations };
}

// ── print_algorithm_comparison ────────────────────────────────────────────────

void print_algorithm_comparison(const BenchResult& bubble,
                                 const BenchResult& shell_rcp,
                                 const BenchResult& shell_native) {
    std::cout << std::fixed << std::setprecision(3) << std::setfill(' ');
    std::cout << "\n";
    std::cout << "  ┌──────────────────────────────────┬──────────────┬──────────────┬────────────┐\n";
    std::cout << "  │ Sort variant                     │  mean (ms)   │   min (ms)   │ iterations │\n";
    std::cout << "  ├──────────────────────────────────┼──────────────┼──────────────┼────────────┤\n";

    auto row = [](const char* lbl, const BenchResult& r) {
        std::cout << "  │ " << std::left  << std::setw(32) << lbl         << " │ "
                  << std::right << std::setw(12) << r.mean_ns / 1e6       << " │ "
                  << std::right << std::setw(12) << r.min_ns  / 1e6       << " │ "
                  << std::right << std::setw(10) << r.iterations           << " │\n";
    };

    row("Bubble sort  (V1, in binary)",  bubble);
    row("Shell sort   (V2, via RCP)",    shell_rcp);
    row("Shell sort   (native ref)",     shell_native);
    std::cout << "  └──────────────────────────────────┴──────────────┴──────────────┴────────────┘\n";

    double speedup      = bubble.mean_ns / shell_rcp.mean_ns;
    double min_overhead = (shell_rcp.min_ns - shell_native.min_ns) / shell_native.min_ns * 100.0;
    std::cout << std::setprecision(1);
    std::cout << "\n  Speedup after patch (shell vs bubble):       " << speedup << "x\n";
    std::cout << "  Trampoline overhead (min latency, warmed):   "
              << std::showpos << min_overhead << std::noshowpos << "%"
              << "  (one indirect JMP per call)\n";
    std::cout << "\n  Note: mean overhead may appear higher due to TLB/i-cache cold-start on the\n"
              << "  first iterations over the mmap'd region. Minimum latency is the fair comparison.\n";
    std::cout << "\n  Key insight:\n"
              << "    The patched function executes from anonymous mmap'd memory at full speed.\n"
              << "    The trampoline contributes exactly one additional indirect branch per call —\n"
              << "    negligible for any function body longer than a few instructions.\n";
}

// ── print_rcp_size_independence ───────────────────────────────────────────────

void print_rcp_size_independence(const BenchResult& rcp_small,
                                  const BenchResult& rcp_large) {
    std::cout << std::fixed << std::setprecision(1) << std::setfill(' ');
    std::cout << "\n";
    std::cout << "  ┌──────────────────────────────────┬──────────────┬──────────────┬────────────┐\n";
    std::cout << "  │ Patched function                 │  mean (ns)   │   min (ns)   │ iterations │\n";
    std::cout << "  ├──────────────────────────────────┼──────────────┼──────────────┼────────────┤\n";

    auto row = [](const char* lbl, const BenchResult& r) {
        std::cout << "  │ " << std::left  << std::setw(32) << lbl   << " │ "
                  << std::right << std::setw(12) << r.mean_ns        << " │ "
                  << std::right << std::setw(12) << r.min_ns         << " │ "
                  << std::right << std::setw(10) << r.iterations      << " │\n";
    };

    row("bench_fn   (arithmetic, ~40 B)",  rcp_small);
    row("bench_sort (bubble sort, ~300 B)", rcp_large);
    std::cout << "  └──────────────────────────────────┴──────────────┴──────────────┴────────────┘\n";
    std::cout << "\n  Key insight:\n"
              << "    RCP always writes exactly 12 bytes and calls mprotect twice.\n"
              << "    Patch cost is O(1) — independent of the size of the replaced function.\n"
              << "    A 300-byte sort function patches in the same time as a 40-byte arithmetic one.\n";
}
