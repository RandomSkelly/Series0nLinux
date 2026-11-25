#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

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
