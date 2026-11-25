// AcpHal.dll replacement.  Hardware audio contexts are dummy objects; shape
// and APU memory calls report success without touching real hardware.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI AcpHalCreate(void** hal) {
    if (hal) *hal = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI AcpHalOk(void) { return (HRESULT)0; }

void* WINAPI AcpHalPointer(void) { return stubutil::dummy_pointer(); }

BOOL WINAPI AcpHalTrue(void) { return TRUE; }

HRESULT WINAPI AcpHalBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

}  // extern "C"
