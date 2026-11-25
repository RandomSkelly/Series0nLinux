// xmem.dll replacement.  Title allocations must come from VirtualAlloc because
// the engine splits them with MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER; Xbox-only
// physical-page/title-memory calls answer offline.
#include "common.h"

// Wine lacks MEM_PRESERVE_PLACEHOLDER and rejects the engine's region splits
// with ERROR_INVALID_ADDRESS (487).  Patch the EXE's VirtualFree IAT slot to a
// shim that reports those splits as successful while keeping the region intact.
static BOOL WINAPI stub_virtual_free(LPVOID address, SIZE_T size, DWORD type) {
    constexpr DWORD kPreservePlaceholder = 0x0002;
    if (type & kPreservePlaceholder) return TRUE;
    return VirtualFree(address, size, type);
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        constexpr uintptr_t kVirtualFreeIatRva = 0x6793a70;
        void** slot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                               kVirtualFreeIatRva);
        DWORD previous = 0;
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) {
            *slot = reinterpret_cast<void*>(&stub_virtual_free);
            VirtualProtect(slot, sizeof(*slot), previous, &previous);
        }
    }
    return TRUE;
}

extern "C" {

static void* title_alloc(size_t size) {
    if (size == 0) size = 0x10000;
    size = (size + 0xffff) & ~static_cast<size_t>(0xffff);
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE);
}

void* WINAPI XMemAlloc(size_t size, DWORD flags) {
    (void)flags;
    return title_alloc(size);
}

void* WINAPI XMemAllocDefault(size_t size, DWORD flags) {
    (void)flags;
    return title_alloc(size);
}

void WINAPI XMemFree(void* pointer) {
    if (pointer) VirtualFree(pointer, 0, MEM_RELEASE);
}

void WINAPI XMemFreeDefault(void* pointer) {
    if (pointer) VirtualFree(pointer, 0, MEM_RELEASE);
}

void* WINAPI XMemVirtualAlloc(void* address, size_t size, DWORD type, DWORD protect) {
    (void)type;
    (void)protect;
    if (size == 0) size = 0x1000;

    // The engine commits placeholders inside an existing reservation.  Asking
    // VirtualAlloc to reserve those pages again makes Wine fail the call.
    DWORD flags = address ? MEM_COMMIT : (MEM_COMMIT | MEM_RESERVE);
    void* pointer = VirtualAlloc(address, size, flags, PAGE_READWRITE);
    if (!pointer) {
        pointer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size < 0x100 ? 0x100 : size);
    }
    return pointer;
}

BOOL WINAPI XMemVirtualFree(void* address, size_t size, DWORD type) { return VirtualFree(address, size, type); }

BOOL WINAPI XMemVirtualProtect(void* address, size_t size, DWORD protect, DWORD* previous) {
    return VirtualProtect(address, size, protect, previous);
}

SIZE_T WINAPI XMemVirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
    return VirtualQuery(address, info, length);
}

// Xbox physical pages are 64 KiB.  Host virtual reservations are already
// backed by VirtualAlloc, so these calls only need to report success and echo
// the requested page count/page array back to the title.
BOOL WINAPI XMemAllocatePhysicalPages(ULONG_PTR flags, ULONG_PTR* pages, ULONG_PTR* pageArray) {
    (void)flags;
    if (!pages) return FALSE;
    const ULONG_PTR wanted = *pages;
    if (pageArray) {
        for (ULONG_PTR i = 0; i < wanted; ++i) pageArray[i] = i + 1;
    }
    *pages = wanted;
    return TRUE;
}

BOOL WINAPI XMemFreePhysicalPages(ULONG_PTR pages, ULONG_PTR* pageArray) {
    (void)pages;
    (void)pageArray;
    return TRUE;
}

void* WINAPI XMemMapPhysicalPages(void* virtualAddress, ULONG_PTR pages, const ULONG_PTR* pageArray) {
    (void)pages;
    (void)pageArray;
    return virtualAddress;
}

BOOL WINAPI XMemMapPhysicalPagesScatter(void** virtualAddresses, ULONG_PTR pages, const ULONG_PTR* pageArray) {
    (void)virtualAddresses;
    (void)pages;
    (void)pageArray;
    return TRUE;
}

BOOL WINAPI XMemTransferMemory(void* destination, const void* source, size_t size, DWORD flags) {
    (void)flags;
    if (!destination || !source || !size) return FALSE;
    std::memcpy(destination, source, size);
    return TRUE;
}

HRESULT WINAPI XMemGetWorkingSetStatistics(void* statistics) {
    if (!statistics) return (HRESULT)0x80070057;
    std::memset(statistics, 0, 64);
    return (HRESULT)0;
}

BOOL WINAPI XMemIsToolsMemAvailable(void) { return TRUE; }

}  // extern "C"
