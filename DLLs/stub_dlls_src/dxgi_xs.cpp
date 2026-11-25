// dxgi_xs.dll replacement.  Standard DXGI factory/debug creation is forwarded
// to Wine's native dxgi.dll; Xbox-only extensions are harmless no-ops.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kFail = (HRESULT)0x80004005;

using FactoryFn = HRESULT(WINAPI*)(const void*, void**);
using Factory2Fn = HRESULT(WINAPI*)(uint32_t, const void*, void**);
using VoidFn = HRESULT(WINAPI*)(void);

template <typename Fn>
Fn native(const char* name) {
    static Fn cached = stubutil::resolve_export<Fn>("dxgi.dll", name);
    return cached;
}

HRESULT xbox_ok(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

}  // namespace

extern "C" {

HRESULT WINAPI XsCreateDXGIFactory(const void* riid, void** factory) {
    FactoryFn fn = native<FactoryFn>("CreateDXGIFactory");
    return fn ? fn(riid, factory) : kFail;
}

HRESULT WINAPI XsCreateDXGIFactory1(const void* riid, void** factory) {
    FactoryFn fn = native<FactoryFn>("CreateDXGIFactory1");
    return fn ? fn(riid, factory) : kFail;
}

HRESULT WINAPI XsCreateDXGIFactory2(uint32_t flags, const void* riid, void** factory) {
    Factory2Fn fn = native<Factory2Fn>("CreateDXGIFactory2");
    return fn ? fn(flags, riid, factory) : kFail;
}

HRESULT WINAPI XsDXGIGetDebugInterface(const void* riid, void** debug) {
    FactoryFn fn = native<FactoryFn>("DXGIGetDebugInterface");
    return fn ? fn(riid, debug) : kFail;
}

HRESULT WINAPI XsDXGIGetDebugInterface1(uint32_t flags, const void* riid, void** debug) {
    Factory2Fn fn = native<Factory2Fn>("DXGIGetDebugInterface1");
    return fn ? fn(flags, riid, debug) : kFail;
}

HRESULT WINAPI XsDXGIDeclareAdapterRemovalSupport(void) {
    VoidFn fn = native<VoidFn>("DXGIDeclareAdapterRemovalSupport");
    return fn ? fn() : (HRESULT)0;
}

HRESULT WINAPI XsDXGIXboxCreateSwapChain(void*, void*, void** swapChain) { return xbox_ok(swapChain); }
HRESULT WINAPI XsDXGIXboxGetSwapChainStats(void* stats) {
    if (stats) std::memset(stats, 0, 64);
    return (HRESULT)0;
}
HRESULT WINAPI XsDXGIXboxCreateFactory(const void* riid, void** factory) { return xbox_ok(factory); }
HRESULT WINAPI XsDXGIXboxCreateSwapChainForCoreWindow(void*, const void*, void** swapChain) {
    return xbox_ok(swapChain);
}

}  // extern "C"
