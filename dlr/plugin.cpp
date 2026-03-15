// DLR Plugin — compiled to libplugin.so.
//
// To demonstrate DLR hot-reload:
//   1. The main loop calls plugin_compute() through a dlsym pointer.
//   2. Edit the return expression below (e.g. change to x * 3).
//   3. Run:  cmake --build build --target plugin
//   4. The running program detects the .so timestamp change and reloads.

extern "C" int plugin_compute(int x) {
    return x * 0;   // [V1] — edit this line, then rebuild the plugin
}
