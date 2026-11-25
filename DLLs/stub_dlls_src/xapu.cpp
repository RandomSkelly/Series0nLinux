// xapu.dll replacement.  Xbox APU commands are accepted but do no work; result
// counts are zero and result buffers are zero-filled.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI XapuOk(void) { return (HRESULT)0; }

HRESULT WINAPI XapuOut(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI XapuCount(size_t* count) {
    if (count) *count = 0;
    return (HRESULT)0;
}

HRESULT WINAPI XapuResult(void* result, size_t size) {
    if (result) std::memset(result, 0, size);
    return (HRESULT)0;
}

}  // extern "C"
