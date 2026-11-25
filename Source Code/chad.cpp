// chad.dll replacement.  Xbox DSP commands cannot run on PC; reporting S_OK
// keeps the audio thread's state machine moving.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI ChadOk(void) { return (HRESULT)0; }

HRESULT WINAPI ChadBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

}  // extern "C"
