#include "static_module.h"

// Version 1 — compiled into the binary, statically linked.
// This function is the RCP target: at runtime, its first 12 bytes will be
// overwritten with a trampoline that redirects execution to patched_compute
// loaded from a shared library. The caller (main) never changes.
extern "C" __attribute__((noinline)) int static_compute(int x) {
    // Deliberately a multi-step computation so the function body is well
    // over 12 bytes even at -O2, giving the trampoline room to fit.
    volatile int a = x * 2;   // [V1] linear: 2x
    volatile int b = a + 1;
    return (int)b;
}

