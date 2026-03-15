#include "loader.h"
#include <dlfcn.h>
#include <sys/stat.h>
#include <iostream>

DLRLoader::DLRLoader(const std::string& path) : path_(path) {}

DLRLoader::~DLRLoader() { unload(); }

time_t DLRLoader::query_mtime() const {
    struct stat st{};
    return (stat(path_.c_str(), &st) == 0) ? st.st_mtime : 0;
}

void DLRLoader::unload() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
        fn_     = nullptr;
    }
}

bool DLRLoader::load() {
    unload();

    // RTLD_NOW  — resolve all symbols immediately (fail fast)
    // RTLD_LOCAL — do not expose symbols to subsequently loaded libraries
    handle_ = dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        std::cerr << "[DLR] dlopen failed: " << dlerror() << "\n";
        return false;
    }

    fn_ = reinterpret_cast<ComputeFn>(dlsym(handle_, "plugin_compute"));
    if (!fn_) {
        std::cerr << "[DLR] dlsym failed: " << dlerror() << "\n";
        unload();
        return false;
    }

    mtime_ = query_mtime();
    std::cout << "[DLR] Loaded " << path_ << "  (plugin_compute @ " << (void*)fn_ << ")\n";
    return true;
}

bool DLRLoader::reload_if_changed() {
    time_t t = query_mtime();
    if (t <= mtime_) return false;
    std::cout << "[DLR] .so changed — reloading...\n";
    return load();
}
