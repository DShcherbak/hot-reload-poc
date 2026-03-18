#pragma once

// static_compute is compiled directly into the main binary.
// It is NOT designed for hot-reload — it is called via a plain function call,
// not through any indirection. RCP will patch it at runtime by overwriting
// the first 12 bytes of its body with an absolute JMP trampoline.
//
// __attribute__((noinline)) is critical: without it the compiler may inline
// the call site, making function-level patching impossible.
extern "C" __attribute__((noinline)) int static_compute(int x);

// Dedicated target for the raw-injection demo (no .so involved).
extern "C" __attribute__((noinline)) int static_compute_raw(int x);
