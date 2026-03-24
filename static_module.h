#pragma once

// static_compute is compiled directly into the main binary.
// It is NOT designed for hot-reload — it is called via a plain function call,
// not through any indirection. RCP will patch it at runtime by overwriting
// the first 12 bytes of its body with an absolute JMP trampoline.
//
// __attribute__((noinline)) is critical: without it the compiler may inline
// the call site, making function-level patching impossible.
extern "C" __attribute__((noinline)) int  static_compute(int x);

// RCP target for the algorithm-substitution demo.
// V1 (compiled in): bubble sort O(n²).
// At runtime the first 12 bytes are overwritten with a trampoline to shell sort.
extern "C" __attribute__((noinline)) void static_sort(int* arr, int n);
