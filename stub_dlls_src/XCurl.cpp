// XCurl.dll replacement.  The original curl ABI is preserved as link-level
// forwarders to XCurlOrig.dll (see XCurl.def).  Only the Xbox-specific global
// helpers are replaced so startup can continue in an offline state.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

HRESULT WINAPI XcurlGlobalInitMem(void) { return (HRESULT)0; }
HRESULT WINAPI XcurlGlobalResume(void) { return (HRESULT)0; }
HRESULT WINAPI XcurlGlobalSuspend(void) { return (HRESULT)0; }
HRESULT WINAPI XcurlGlobalSetRequestLimit(void) { return (HRESULT)0; }

}  // extern "C"
