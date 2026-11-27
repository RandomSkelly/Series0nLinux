#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#ifndef TRACE_TAG
#define TRACE_TAG stub
#endif

#define SHIM_STR2(x) #x
#define SHIM_STR(x) SHIM_STR2(x)

extern "C" char __ImageBase;

namespace {

__attribute__((no_instrument_function))
inline DWORD shim_len(const char* s) {
    DWORD n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

__attribute__((no_instrument_function))
inline void shim_write(HANDLE h, const char* s, DWORD len) {
    DWORD written = 0;
    WriteFile(h, s, len, &written, nullptr);
}

__attribute__((no_instrument_function))
inline void shim_write_cstr(HANDLE h, const char* s) {
    shim_write(h, s, shim_len(s));
}

__attribute__((no_instrument_function))
inline void shim_write_dec(HANDLE h, unsigned long long v) {
    char buf[32];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    } else {
        char tmp[32];
        int m = 0;
        while (v != 0) {
            tmp[m++] = char('0' + (v % 10));
            v /= 10;
        }
        while (m != 0) buf[n++] = tmp[--m];
    }
    shim_write(h, buf, (DWORD)n);
}

__attribute__((no_instrument_function))
inline void shim_write_hex(HANDLE h, ULONG_PTR v) {
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    } else {
        char tmp[32];
        int m = 0;
        while (v != 0) {
            tmp[m++] = digits[v & 0xf];
            v >>= 4;
        }
        while (m != 0) buf[n++] = tmp[--m];
    }
    shim_write(h, buf, (DWORD)n);
}

__attribute__((no_instrument_function))
inline void shim_trace_line(char phase, void* func, void* caller) {
    static HANDLE trace_file = INVALID_HANDLE_VALUE;
    static HANDLE trace_mutex = nullptr;
    if (!trace_mutex) {
        trace_mutex = CreateMutexA(nullptr, FALSE, "shim_trace_lock");
    }
    if (trace_mutex) {
        WaitForSingleObject(trace_mutex, INFINITE);
    }
    if (trace_file == INVALID_HANDLE_VALUE) {
        HANDLE h = CreateFileA("trace.log",
                               FILE_WRITE_DATA | FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            trace_file = h;
            shim_write_cstr(h, "# module=");
            shim_write_cstr(h, SHIM_STR(TRACE_TAG));
            shim_write_cstr(h, " time_ms,tid,module,phase,rva,caller_addr\n");
        }
    }
    if (trace_file != INVALID_HANDLE_VALUE) {
        // Each stub module owns its own handle.  Wine does not give separate
        // handles a shared append pointer, so seek explicitly under the mutex
        // to prevent concurrent writes from overwriting earlier lines.
        SetFilePointer(trace_file, 0, nullptr, FILE_END);
        const ULONG_PTR base = reinterpret_cast<ULONG_PTR>(&__ImageBase);
        const ULONG_PTR rva = func ? (reinterpret_cast<ULONG_PTR>(func) - base) : 0;
        const ULONG_PTR caller_addr = caller ? reinterpret_cast<ULONG_PTR>(caller) : 0;
        shim_write_dec(trace_file, (unsigned long long)GetTickCount64());
        shim_write_cstr(trace_file, ",");
        shim_write_dec(trace_file, (unsigned long long)GetCurrentThreadId());
        shim_write_cstr(trace_file, ",");
        shim_write_cstr(trace_file, SHIM_STR(TRACE_TAG));
        shim_write_cstr(trace_file, ",");
        shim_write(trace_file, &phase, 1);
        shim_write_cstr(trace_file, ",");
        shim_write_hex(trace_file, rva);
        shim_write_cstr(trace_file, ",");
        shim_write_hex(trace_file, caller_addr);
        shim_write_cstr(trace_file, "\n");
    }
    if (trace_mutex) {
        ReleaseMutex(trace_mutex);
    }
}

}  // namespace

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void* func, void* caller) {
    shim_trace_line('E', func, caller);
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void* func, void* caller) {
    shim_trace_line('X', func, caller);
}

#ifdef __cplusplus
extern "C" {
#endif

struct XAsyncBlock;
typedef void(CALLBACK* XAsyncCompletionRoutine)(struct XAsyncBlock* asyncBlock);

typedef struct XAsyncBlock {
    void* queue;
    void* context;
    XAsyncCompletionRoutine callback;
    unsigned char internal[sizeof(void*) * 4];
} XAsyncBlock;

struct XAsyncProviderData {
    XAsyncBlock* async;
    size_t bufferSize;
    void* buffer;
    void* context;
};

typedef HRESULT(CALLBACK* XAsyncProvider)(uint32_t op, struct XAsyncProviderData* data);
typedef void(CALLBACK* XAsyncWork)(void* context);
typedef void(CALLBACK* XTaskQueueCallback)(void* context);

#ifdef __cplusplus
}
#endif

namespace stubutil {

inline void* dummy_pointer() {
    static char storage[64] = {0};
    return storage;
}

inline HRESULT completion_status(XAsyncBlock* async) {
    return async ? (HRESULT)(intptr_t)((uintptr_t*)async->internal)[0] : (HRESULT)0;
}

inline bool async_pending(XAsyncBlock* async) {
    return async && ((uintptr_t*)async->internal)[0] == (uintptr_t)(HRESULT)0x80070001;
}

inline void begin_async(XAsyncBlock* async) {
    if (!async) return;
    std::memset(async->internal, 0, sizeof(async->internal));
    ((uintptr_t*)async->internal)[0] = (uintptr_t)(HRESULT)0x80070001;
}

inline void complete_async(XAsyncBlock* async, HRESULT result = (HRESULT)0, size_t requiredBufferSize = 0) {
    if (!async) return;
    ((uintptr_t*)async->internal)[0] = (uintptr_t)result;
    ((uintptr_t*)async->internal)[1] = (uintptr_t)requiredBufferSize;
    if (async->callback) async->callback(async);
}

inline void run_provider(XAsyncBlock* async, XAsyncProvider provider, void* context, uint32_t op) {
    if (!async || !provider) return;
    XAsyncProviderData data = {async, 0, nullptr, context};
    provider(op, &data);
}

inline void finish_provider(XAsyncBlock* async, XAsyncProvider provider, void* context) {
    if (!async || !provider) return;
    XAsyncProviderData data = {async, 0, nullptr, context};
    provider(2, &data);  // XAsyncOp::GetResult
    complete_async(async, (HRESULT)0, 0);
}

template <typename Fn>
inline Fn resolve_export(const char* moduleName, const char* exportName) {
    HMODULE module = LoadLibraryA(moduleName);
    if (!module) return nullptr;
    FARPROC proc = GetProcAddress(module, exportName);
    return reinterpret_cast<Fn>(proc);
}

}  // namespace stubutil
