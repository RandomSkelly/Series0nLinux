// xg_xs.dll replacement.  Xbox graphics helpers answer S_OK with dummy
// objects instead of dereferencing missing console resources.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

struct TextureComputer {
    void** vtable;
};

HRESULT WINAPI texture_computer_noop(void*, void*, void*, void*, void*, void*, void*, void*) {
    return (HRESULT)0;
}

}  // namespace

extern "C" {

HRESULT WINAPI XgCreateTextureComputer(void*, void** computer, void*) {
    if (!computer) return (HRESULT)0;

    static TextureComputer object = {};
    static void* vtable[0x100] = {};
    static bool initialized = false;
    if (!initialized) {
        for (size_t i = 0; i < 0x100; ++i) {
            vtable[i] = reinterpret_cast<void*>(&texture_computer_noop);
        }
        object.vtable = vtable;
        initialized = true;
    }

    *computer = &object;
    return (HRESULT)0;
}

HRESULT WINAPI XgSuggestSwizzleMode(void* suggestion) {
    if (suggestion) std::memset(suggestion, 0, 64);
    return (HRESULT)0;
}

HRESULT WINAPI XgOk(void) { return (HRESULT)0; }

}  // extern "C"
