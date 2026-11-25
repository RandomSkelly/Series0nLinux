// Party.dll replacement.  Party/telemetry calls answer S_OK and provide dummy
// handles so a missing online service cannot null-deref the title.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI PartyOk(void) { return (HRESULT)0; }

HRESULT WINAPI PartyPointer(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI PartyBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

void* WINAPI PartyHandle(void) { return stubutil::dummy_pointer(); }

BOOL WINAPI PartyTrue(void) { return TRUE; }

void WINAPI PartyVoid(void) {}

}  // extern "C"
