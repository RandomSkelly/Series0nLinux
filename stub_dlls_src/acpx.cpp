// acpx.dll replacement.  Xbox audio-compression calls report success and hand
// back non-null dummy handles so title code never dereferences NULL.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI AcpxOk(void) { return (HRESULT)0; }

HRESULT WINAPI AcpxPointer(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI AcpxBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

void* WINAPI AcpxHandle(void) { return stubutil::dummy_pointer(); }

BOOL WINAPI AcpxTrue(void) { return TRUE; }

}  // extern "C"
