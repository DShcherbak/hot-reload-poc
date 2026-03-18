// V2 adds a brand-new function that V1 does not have
extern "C" int plugin_compute(int x) { return x * 3; }          // updated
extern "C" int plugin_extra(int x, int y) { return x * x + y; } // NEW
