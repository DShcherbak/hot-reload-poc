// RCP Patch Plugin — compiled to libpatch_plugin.so.
//
// Provides the *replacement* implementation for static_compute.
// It is loaded via dlopen at runtime; its address is then written into
// static_compute's body as an absolute-JMP trampoline by rcp_patch().
//
// This is the "hybrid" aspect: RCP handles the redirection of statically
// linked code, while DLR provides the new implementation at runtime.

extern "C" int patched_compute(int x) {
    return x * 11;   // [V2] quadratic — replaces the linear static_compute
}
