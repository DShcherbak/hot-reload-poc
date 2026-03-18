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
