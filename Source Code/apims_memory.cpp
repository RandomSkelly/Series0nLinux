// App-local api-ms-win-core-memory-l1-1-0.dll shim.  Wine does not implement
// MEM_PRESERVE_PLACEHOLDER, which the engine uses to split a reserved region.
// Forward the real memory APIs and make only that split operation succeed.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" BOOL WINAPI StubVirtualFree(LPVOID address, SIZE_T size, DWORD type) {
    constexpr DWORD kPreservePlaceholder = 0x0002;
    if (type & kPreservePlaceholder) {
        return TRUE;
    }
    return VirtualFree(address, size, type);
}
