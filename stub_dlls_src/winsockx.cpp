// winsockx.dll replacement.  Standard Winsock exports are link-level
// forwarders to ws2_32.dll (see winsockx.def); Xbox-only helpers answer S_OK.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI WinsockxOk(void) { return (HRESULT)0; }

HRESULT WINAPI WinsockxPointer(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI WinsockxBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

BOOL WINAPI WinsockxTrue(void) { return TRUE; }

}  // extern "C"
