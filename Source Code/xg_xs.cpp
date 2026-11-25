// xg_xs.dll replacement.  Xbox graphics helpers answer S_OK with dummy
// objects instead of dereferencing missing console resources.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI XgCreateTextureComputer(void** computer) {
    if (computer) *computer = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI XgSuggestSwizzleMode(void* suggestion) {
    if (suggestion) std::memset(suggestion, 0, 64);
    return (HRESULT)0;
}

HRESULT WINAPI XgOk(void) { return (HRESULT)0; }

}  // extern "C"
