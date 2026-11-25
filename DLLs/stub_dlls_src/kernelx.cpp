// kernelx.dll replacement.  Ordinary Win32 calls are forwarded to
// kernel32.dll/ntdll.dll; Xbox-only title-memory queries are answered offline.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

using NtAllocFn = long(WINAPI*)(HANDLE, void**, ULONG_PTR, SIZE_T*, ULONG, ULONG);
using NtFreeFn = long(WINAPI*)(HANDLE, void**, SIZE_T*, ULONG);
using NtQueryFn = long(WINAPI*)(HANDLE, void*, void*, void*, SIZE_T, SIZE_T*);

template <typename Fn>
Fn ntdll(const char* name) {
    static Fn cached = stubutil::resolve_export<Fn>("ntdll.dll", name);
    return cached;
}

void fill_title_memory(void* information, SIZE_T size) {
    if (!information) return;
    std::memset(information, 0, size ? size : 64);
    unsigned char* bytes = static_cast<unsigned char*>(information);
    const uint64_t values[4] = {
        12ULL * 1024 * 1024 * 1024,  // total title memory
        8ULL * 1024 * 1024 * 1024,   // title-owned memory
        4ULL * 1024 * 1024 * 1024,   // reserved
        64ULL * 1024 * 1024,         // allocation granularity hint
    };
    if (size >= sizeof(values)) std::memcpy(bytes, values, sizeof(values));
}

}  // namespace

extern "C" {

// ---- memory ----
LPVOID WINAPI KxVirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect) {
    return VirtualAlloc(address, size, type, protect);
}
BOOL WINAPI KxVirtualFree(LPVOID address, SIZE_T size, DWORD type) { return VirtualFree(address, size, type); }
BOOL WINAPI KxVirtualProtect(LPVOID address, SIZE_T size, DWORD protect, DWORD* previous) {
    return VirtualProtect(address, size, protect, previous);
}
SIZE_T WINAPI KxVirtualQuery(LPCVOID address, MEMORY_BASIC_INFORMATION* info, SIZE_T length) {
    return VirtualQuery(address, info, length);
}
BOOL WINAPI KxVirtualLock(LPVOID address, SIZE_T size) { return VirtualLock(address, size); }
BOOL WINAPI KxVirtualUnlock(LPVOID address, SIZE_T size) { return VirtualUnlock(address, size); }

HANDLE WINAPI KxGetProcessHeap(void) { return GetProcessHeap(); }
HANDLE WINAPI KxHeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum) {
    return HeapCreate(options, initial, maximum);
}
BOOL WINAPI KxHeapDestroy(HANDLE heap) { return HeapDestroy(heap); }
LPVOID WINAPI KxHeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) { return HeapAlloc(heap, flags, size); }
LPVOID WINAPI KxHeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T size) {
    return HeapReAlloc(heap, flags, memory, size);
}
BOOL WINAPI KxHeapFree(HANDLE heap, DWORD flags, LPVOID memory) { return HeapFree(heap, flags, memory); }
SIZE_T WINAPI KxHeapSize(HANDLE heap, DWORD flags, LPCVOID memory) { return HeapSize(heap, flags, memory); }

long WINAPI KxNtAllocateVirtualMemory(HANDLE process, void** address, ULONG_PTR zeroBits, SIZE_T* size,
                                      ULONG type, ULONG protect) {
    NtAllocFn fn = ntdll<NtAllocFn>("NtAllocateVirtualMemory");
    return fn ? fn(process, address, zeroBits, size, type, protect) : (long)0xC0000002L;
}
long WINAPI KxNtFreeVirtualMemory(HANDLE process, void** address, SIZE_T* size, ULONG type) {
    NtFreeFn fn = ntdll<NtFreeFn>("NtFreeVirtualMemory");
    return fn ? fn(process, address, size, type) : (long)0xC0000002L;
}
long WINAPI KxNtQueryVirtualMemory(HANDLE process, void* address, void* infoClass, void* info, SIZE_T length,
                                   SIZE_T* used) {
    NtQueryFn fn = ntdll<NtQueryFn>("NtQueryVirtualMemory");
    return fn ? fn(process, address, infoClass, info, length, used) : (long)0xC0000002L;
}

// ---- threading and synchronization ----
HANDLE WINAPI KxCreateThread(SECURITY_ATTRIBUTES* attributes, SIZE_T stackSize, LPTHREAD_START_ROUTINE start,
                             LPVOID parameter, DWORD flags, DWORD* threadId) {
    return CreateThread(attributes, stackSize, start, parameter, flags, threadId);
}
void WINAPI KxExitThread(DWORD code) { ExitThread(code); }
HANDLE WINAPI KxGetCurrentThread(void) { return GetCurrentThread(); }
DWORD WINAPI KxGetCurrentThreadId(void) { return GetCurrentThreadId(); }
void WINAPI KxSleep(DWORD milliseconds) { Sleep(milliseconds); }
DWORD WINAPI KxGetTickCount64(void) { return (DWORD)GetTickCount64(); }

HANDLE WINAPI KxCreateEventW(SECURITY_ATTRIBUTES* attributes, BOOL manualReset, BOOL initialState, const WCHAR* name) {
    return CreateEventW(attributes, manualReset, initialState, name);
}
HANDLE WINAPI KxCreateEventA(SECURITY_ATTRIBUTES* attributes, BOOL manualReset, BOOL initialState, const char* name) {
    return CreateEventA(attributes, manualReset, initialState, name);
}
BOOL WINAPI KxSetEvent(HANDLE event) { return SetEvent(event); }
BOOL WINAPI KxResetEvent(HANDLE event) { return ResetEvent(event); }
HANDLE WINAPI KxCreateMutexW(SECURITY_ATTRIBUTES* attributes, BOOL initialOwner, const WCHAR* name) {
    return CreateMutexW(attributes, initialOwner, name);
}
BOOL WINAPI KxReleaseMutex(HANDLE mutex) { return ReleaseMutex(mutex); }
void WINAPI KxInitializeCriticalSection(CRITICAL_SECTION* section) { InitializeCriticalSection(section); }
void WINAPI KxEnterCriticalSection(CRITICAL_SECTION* section) { EnterCriticalSection(section); }
void WINAPI KxLeaveCriticalSection(CRITICAL_SECTION* section) { LeaveCriticalSection(section); }
void WINAPI KxDeleteCriticalSection(CRITICAL_SECTION* section) { DeleteCriticalSection(section); }
DWORD WINAPI KxWaitForSingleObject(HANDLE handle, DWORD milliseconds) { return WaitForSingleObject(handle, milliseconds); }
DWORD WINAPI KxWaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds) {
    return WaitForMultipleObjects(count, handles, waitAll, milliseconds);
}

// ---- file I/O ----
HANDLE WINAPI KxCreateFileW(const WCHAR* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* attributes,
                            DWORD creation, DWORD flags, HANDLE templateFile) {
    return CreateFileW(path, access, share, attributes, creation, flags, templateFile);
}
HANDLE WINAPI KxCreateFileA(const char* path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* attributes,
                            DWORD creation, DWORD flags, HANDLE templateFile) {
    return CreateFileA(path, access, share, attributes, creation, flags, templateFile);
}
BOOL WINAPI KxReadFile(HANDLE file, LPVOID buffer, DWORD bytes, DWORD* bytesRead, OVERLAPPED* overlapped) {
    return ReadFile(file, buffer, bytes, bytesRead, overlapped);
}
BOOL WINAPI KxWriteFile(HANDLE file, const LPVOID buffer, DWORD bytes, DWORD* bytesWritten, OVERLAPPED* overlapped) {
    return WriteFile(file, buffer, bytes, bytesWritten, overlapped);
}
BOOL WINAPI KxCloseHandle(HANDLE handle) { return CloseHandle(handle); }
BOOL WINAPI KxFlushFileBuffers(HANDLE file) { return FlushFileBuffers(file); }
BOOL WINAPI KxSetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* newPosition, DWORD method) {
    return SetFilePointerEx(file, distance, newPosition, method);
}
BOOL WINAPI KxGetFileSizeEx(HANDLE file, LARGE_INTEGER* size) { return GetFileSizeEx(file, size); }
HANDLE WINAPI KxCreateFileMappingW(HANDLE file, SECURITY_ATTRIBUTES* attributes, DWORD protect, DWORD maxSizeHigh,
                                   DWORD maxSizeLow, const WCHAR* name) {
    return CreateFileMappingW(file, attributes, protect, maxSizeHigh, maxSizeLow, name);
}
LPVOID WINAPI KxMapViewOfFile(HANDLE mapping, DWORD access, DWORD offsetHigh, DWORD offsetLow, SIZE_T bytes) {
    return MapViewOfFile(mapping, access, offsetHigh, offsetLow, bytes);
}
BOOL WINAPI KxUnmapViewOfFile(LPCVOID address) { return UnmapViewOfFile(address); }
BOOL WINAPI KxDeleteFileW(const WCHAR* path) { return DeleteFileW(path); }
BOOL WINAPI KxCreateDirectoryW(const WCHAR* path, SECURITY_ATTRIBUTES* attributes) {
    return CreateDirectoryW(path, attributes);
}
DWORD WINAPI KxGetFileAttributesW(const WCHAR* path) { return GetFileAttributesW(path); }
BOOL WINAPI KxSetFileAttributesW(const WCHAR* path, DWORD attributes) { return SetFileAttributesW(path, attributes); }

DWORD WINAPI KxGetLastError(void) { return GetLastError(); }
void WINAPI KxSetLastError(DWORD error) { SetLastError(error); }
BOOL WINAPI KxQueryPerformanceCounter(LARGE_INTEGER* value) { return QueryPerformanceCounter(value); }
BOOL WINAPI KxQueryPerformanceFrequency(LARGE_INTEGER* value) { return QueryPerformanceFrequency(value); }

// ---- Xbox-only title memory ----
BOOL WINAPI KxTitleMemoryQuery(void* information, SIZE_T size) {
    fill_title_memory(information, size);
    return information ? TRUE : FALSE;
}
HRESULT WINAPI KxTitleMemoryStats(void* statistics) {
    if (!statistics) return (HRESULT)0x80070057;
    std::memset(statistics, 0, 64);
    return (HRESULT)0;
}

}  // extern "C"
