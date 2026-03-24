#pragma once

struct BenchResult {
    double mean_ns;
    double min_ns;
    double max_ns;
    int    iterations;
};

// Measures mprotect(RWX) + memcpy(12B) + mprotect(RX) — the raw RCP mechanism
BenchResult benchmark_rcp(int iterations = 1000);

// Measures dlclose + dlopen + dlsym — the raw DLR reload mechanism.
// Call twice: once with a small .so and once with the large generated .so
// to show that DLR cost scales with symbol count while RCP does not.
BenchResult benchmark_dlr(const char* so_path, int iterations = 100);

// dlr_small  — tiny plugin (1 exported symbol)
// dlr_large  — generated plugin (1000 exported symbols)
void print_bench_comparison(const BenchResult& rcp,
                             const BenchResult& dlr_small,
                             const BenchResult& dlr_large);

// ── Algorithm substitution benchmark ─────────────────────────────────────────

// Measures wall time of fn(arr, n) over `iters` runs on a fixed pseudo-random array.
BenchResult benchmark_sort_fn(void (*fn)(int*, int), int n, int iters = 50);

// Native reference shell sort compiled directly into the benchmark TU.
// Used to verify that post-patch performance equals native performance.
void bench_shell_sort_ref(int* arr, int n);

// Measures mprotect+memcpy+mprotect patch cost on a large sort function (~300 B)
// to demonstrate that RCP cost is O(1) regardless of patched code size.
BenchResult benchmark_rcp_large(int iterations = 1000);

// Print results for the algorithm substitution section.
void print_algorithm_comparison(const BenchResult& bubble,
                                 const BenchResult& shell_rcp,
                                 const BenchResult& shell_native);

// Print results for the RCP-size-independence section.
void print_rcp_size_independence(const BenchResult& rcp_small,
                                  const BenchResult& rcp_large);
