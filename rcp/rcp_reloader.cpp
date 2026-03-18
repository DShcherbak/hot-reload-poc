#include "rcp_reloader.h"
#include "patcher.h"

#include <dlfcn.h>
#include <sys/stat.h>
#include <iostream>

RCPReloader::RCPReloader(void* target_fn,
                         const std::string& so_path,
                         const std::string& replacement_sym)
    : target_fn_(target_fn), so_path_(so_path), sym_name_(replacement_sym) {}

RCPReloader::~RCPReloader() { unload(); }

time_t RCPReloader::query_mtime() const {
    struct stat st{};
    return (stat(so_path_.c_str(), &st) == 0) ? st.st_mtime : 0;
}

void RCPReloader::unload() {
    if (handle_) {
        // NOTE: between dlclose and the next rcp_patch call, the trampoline
        // still points into the now-unmapped .so. This is safe in a
        // single-threaded program because nothing calls static_compute during
        // this window. A multi-threaded runtime would need to patch to a safe
        // stub first, then swap the .so, then patch to the new function.
        dlclose(handle_);
        handle_ = nullptr;
    }
}

bool RCPReloader::load_and_patch() {
    unload();

    handle_ = dlopen(so_path_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        std::cerr << "[RCP-reload] dlopen failed: " << dlerror() << "\n";
        return false;
    }

    void* replacement = dlsym(handle_, sym_name_.c_str());
    if (!replacement) {
        std::cerr << "[RCP-reload] dlsym('" << sym_name_ << "') failed: "
                  << dlerror() << "\n";
        unload();
        return false;
    }

    mtime_ = query_mtime();
    std::cout << "[RCP-reload] Loaded " << so_path_
              << "  (" << sym_name_ << " @ " << replacement << ")\n";

    return rcp_patch(target_fn_, replacement);
}

bool RCPReloader::repatch_if_changed() {
    time_t t = query_mtime();
    if (t <= mtime_) return false;

    std::cout << "[RCP-reload] .so changed — reloading and repatching...\n";
    return load_and_patch();
}
