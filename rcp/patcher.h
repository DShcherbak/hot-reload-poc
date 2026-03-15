#pragma once
#include <cstdint>

// rcp_patch — overwrites the first 12 bytes of `target` with an
// x86-64 absolute-jump trampoline that redirects execution to `replacement`.
//
// Trampoline layout (12 bytes):
//   48 B8 <addr:8>   MOVABS RAX, <replacement>
//   FF E0            JMP    RAX
//
// The function uses mprotect to temporarily make the target page writable,
// writes the trampoline, then restores PROT_READ|PROT_EXEC.
// The target function must be at least 12 bytes long (guaranteed for any
// non-trivial function compiled without aggressive inlining).
//
// Thread safety: not safe to call while another thread may be executing
// target. For a single-threaded PoC this is irrelevant.
bool rcp_patch(void* target, void* replacement);
