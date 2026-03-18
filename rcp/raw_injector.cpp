#include "raw_injector.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>

// ── trampoline ────────────────────────────────────────────────────────────────
// 12-byte absolute-jump trampoline for x86-64:
//   48 B8 <addr:8>   MOVABS RAX, <replacement>
//   FF E0            JMP    RAX
//
// mprotect@plt and the target function may share the same 4K page, so we keep
// PROT_EXEC during the write window (PROT_RWX) to avoid a fault on the second
// mprotect call. Linux allows RWX pages; OpenBSD would require a different approach.

static constexpr size_t TRAMPOLINE_BYTES = 12;

static void build_trampoline(uint8_t* buf, void* replacement) {
    buf[0] = 0x48; buf[1] = 0xB8;
    uint64_t addr = reinterpret_cast<uint64_t>(replacement);
    std::memcpy(buf + 2, &addr, sizeof(addr));
    buf[10] = 0xFF; buf[11] = 0xE0;
}

static bool rcp_patch(void* target, void* replacement) {
    const long  page_size  = sysconf(_SC_PAGESIZE);
    auto        page_mask  = static_cast<uintptr_t>(page_size - 1);
    void*       page       = reinterpret_cast<void*>(
                                 reinterpret_cast<uintptr_t>(target) & ~page_mask);

    std::cout << "[RCP] Patching " << target << " -> " << replacement << "\n";

    if (mprotect(page, static_cast<size_t>(page_size),
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("[RCP] mprotect(RWX) failed");
        return false;
    }

    uint8_t trampoline[TRAMPOLINE_BYTES];
    build_trampoline(trampoline, replacement);
    std::memcpy(target, trampoline, TRAMPOLINE_BYTES);

    if (mprotect(page, static_cast<size_t>(page_size), PROT_READ | PROT_EXEC) != 0)
        perror("[RCP] mprotect(RX) failed");

    std::cout << "[RCP] Trampoline: ";
    for (size_t i = 0; i < TRAMPOLINE_BYTES; ++i)
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(trampoline[i]) << " ";
    std::cout << std::dec << "\n";

    return true;
}

// ── RawInjector ───────────────────────────────────────────────────────────────

RawInjector::RawInjector(void* target_fn, const std::string& bin_path)
    : target_fn_(target_fn), bin_path_(bin_path) {}

RawInjector::~RawInjector() { unmap(); }

time_t RawInjector::query_mtime() const {
    struct stat st;
    if (stat(bin_path_.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

void RawInjector::unmap() {
    if (code_region_) {
        munmap(code_region_, code_size_);
        code_region_ = nullptr;
        code_size_   = 0;
    }
}

bool RawInjector::load_and_patch() {
    unmap();

    std::ifstream f(bin_path_, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "[RawInject] Cannot open: " << bin_path_ << "\n";
        return false;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    if (size <= 0) {
        std::cerr << "[RawInject] Empty binary: " << bin_path_ << "\n";
        return false;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!f.read(reinterpret_cast<char*>(data.data()), size)) {
        std::cerr << "[RawInject] Read error: " << bin_path_ << "\n";
        return false;
    }

    code_size_   = static_cast<size_t>(size);
    code_region_ = mmap(nullptr, code_size_,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (code_region_ == MAP_FAILED) {
        std::cerr << "[RawInject] mmap failed\n";
        code_region_ = nullptr;
        code_size_   = 0;
        return false;
    }

    memcpy(code_region_, data.data(), code_size_);
    mprotect(code_region_, code_size_, PROT_READ | PROT_EXEC);

    mtime_ = query_mtime();
    std::cout << "[RawInject] Loaded " << code_size_ << " bytes from "
              << bin_path_ << " -> mapped at " << code_region_ << "\n";

    return rcp_patch(target_fn_, code_region_);
}

bool RawInjector::repatch_if_changed() {
    time_t new_mtime = query_mtime();
    if (new_mtime > mtime_) {
        std::cout << "[RawInject] bin changed — reinjecting...\n";
        return load_and_patch();
    }
    return false;
}
