#pragma once
#include <string>
#include <ctime>

using ComputeFn = int (*)(int);

// DLRLoader manages a single shared library that exports `plugin_compute`.
// Call reload_if_changed() in your main loop; it does nothing if the .so
// file has not been modified since the last load.
class DLRLoader {
public:
    explicit DLRLoader(const std::string& path);
    ~DLRLoader();

    // Load (or reload) the library. Returns true on success.
    bool load();

    // Checks mtime; calls load() only if the file changed.
    // Returns true if a reload actually happened.
    bool reload_if_changed();

    ComputeFn get_fn() const { return fn_; }

private:
    std::string  path_;
    void*        handle_ = nullptr;
    ComputeFn    fn_     = nullptr;
    time_t       mtime_  = 0;

    time_t query_mtime() const;
    void   unload();
};
