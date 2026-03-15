#include "patcher.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>

// Size of the trampoline we write into the target function.
static constexpr size_t TRAMPOLINE_BYTES = 12;

// Build the 12-byte absolute-jump trampoline into `buf`.
static void build_trampoline(uint8_t* buf, void* replacement) {
    //  48 B8           — REX.W prefix + MOV RAX opcode (MOVABS RAX, imm64)
    buf[0] = 0x48;
    buf[1] = 0xB8;
    //  <8-byte address> — little-endian 64-bit immediate
    uint64_t addr = reinterpret_cast<uint64_t>(replacement);
    std::memcpy(buf + 2, &addr, sizeof(addr));
    //  FF E0           — JMP RAX
    buf[10] = 0xFF;
    buf[11] = 0xE0;
}

bool rcp_patch(void* target, void* replacement) {
    const long   page_size  = sysconf(_SC_PAGESIZE);
    auto         page_mask  = static_cast<uintptr_t>(page_size - 1);
    auto         target_int = reinterpret_cast<uintptr_t>(target);
    void*        page       = reinterpret_cast<void*>(target_int & ~page_mask);

    std::cout << "[RCP] Patching " << target << " -> " << replacement << "\n";

    // Step 1: add WRITE to the page while keeping EXEC.
    //
    // Important: mprotect@plt and static_compute may share the same 4K page.
    // If we removed EXEC here (pure PROT_READ|PROT_WRITE) the second mprotect
    // call below would fault because its PLT stub would no longer be executable.
    // Keeping PROT_EXEC throughout avoids this — Linux allows RWX pages by
    // default (unlike OpenBSD which enforces strict W^X).
    if (mprotect(page, static_cast<size_t>(page_size),
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("[RCP] mprotect(RWX) failed");
        return false;
    }

    // Step 2: write the trampoline.
    uint8_t trampoline[TRAMPOLINE_BYTES];
    build_trampoline(trampoline, replacement);
    std::memcpy(target, trampoline, TRAMPOLINE_BYTES);

    // Step 3: drop the write permission — leave read+execute.
    if (mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC) != 0) {
        perror("[RCP] mprotect(RX) failed");
        // Non-fatal: the trampoline is already written and the page is still
        // executable; we just failed to remove the write bit.
    }

    // Print the trampoline bytes for paper/debugging visibility.
    std::cout << "[RCP] Trampoline: ";
    for (size_t i = 0; i < TRAMPOLINE_BYTES; ++i)
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(trampoline[i]) << " ";
    std::cout << std::dec << "\n";

    return true;
}
