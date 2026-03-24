// sort_plugin.cpp — Shell sort, injected via RawInjector to replace bubble sort.
//
// Compiled to sort_snippet.bin via:
//   g++ -O0 -fPIC -c sort_plugin.cpp -o sort_snippet.o
//   objcopy -O binary --only-section=.text sort_snippet.o sort_snippet.bin
//
// The raw .text bytes are mmap'd into anonymous executable memory; a 12-byte
// absolute-JMP trampoline in static_sort redirects all callers here without
// any involvement of the dynamic linker.
//
// IMPORTANT: Must be fully self-contained — no external calls, no recursion,
// no global variables — so that the extracted bytes are position-independent.
// Shell sort satisfies this: it uses only local variables and array indexing.

extern "C" void patched_sort(int* arr, int n) {
    for (int gap = n / 2; gap > 0; gap /= 2) {
        for (int i = gap; i < n; i++) {
            int temp = arr[i];
            int j = i;
            while (j >= gap && arr[j - gap] > temp) {
                arr[j] = arr[j - gap];
                j -= gap;
            }
            arr[j] = temp;
        }
    }
}
