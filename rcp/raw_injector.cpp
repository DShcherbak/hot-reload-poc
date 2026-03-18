#include "raw_injector.h"
#include "patcher.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>

RawInjector::RawInjector(void* target_fn, const std::string& bin_path)
    : target_fn_(target_fn), bin_path_(bin_path) {}

RawInjector::~RawInjector() {
    unmap();
}

time_t RawInjector::query_mtime() const {
    struct stat st;
    if (stat(bin_path_.c_str(), &st) != 0)
        return 0;
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

    // Read the binary file.
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

    // Map anonymous executable memory.
    code_size_ = static_cast<size_t>(size);
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

    // Drop write permission.
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
