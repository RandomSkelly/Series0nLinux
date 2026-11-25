// Offline XUser.dll replacement.  Handles are non-null dummy pointers and
// every async operation completes immediately with S_OK.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

HRESULT complete(XAsyncBlock* async) {
    if (!async) return (HRESULT)0x80004003;
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

void* user_handle() { return stubutil::dummy_pointer(); }

}  // namespace

extern "C" {

HRESULT WINAPI XUserAddAsync(uint32_t, XAsyncBlock* async) { return complete(async); }

HRESULT WINAPI XUserAddResult(XAsyncBlock*, void** user) {
    if (!user) return (HRESULT)0x80004003;
    *user = user_handle();
    return (HRESULT)0;
}

HRESULT WINAPI XUserFindUserById(uint64_t, void** user) {
    if (!user) return (HRESULT)0x80004003;
    *user = user_handle();
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetUserHandle(void*, void** user) {
    if (!user) return (HRESULT)0x80004003;
    *user = user_handle();
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetId(void*, uint64_t* id) {
    if (id) *id = 0x0000000000000001ULL;
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetXuid(void*, uint64_t* xuid) {
    if (xuid) *xuid = 0x0000000000000001ULL;
    return (HRESULT)0;
}

HRESULT WINAPI UserGetGamertag(void*, size_t size, char* buffer, size_t* used) {
    static const char tag[] = "OfflineGamer";
    size_t needed = sizeof(tag);
    if (used) *used = needed;
    if (!buffer) return (HRESULT)0x80004003;
    if (size < needed) return (HRESULT)0x8007007a;
    std::memcpy(buffer, tag, needed);
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetAgeGroup(void*, uint32_t* group) {
    if (group) *group = 3;  // Adult
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetSignInState(void*, uint32_t* state) {
    if (state) *state = 1;  // SignedIn
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetFirstSignInState(uint32_t* state) {
    if (state) *state = 1;
    return (HRESULT)0;
}

uint32_t WINAPI XUserGetMaxUsers(void) { return 1; }

HRESULT WINAPI XUserCheckPrivilege(void*, uint32_t, bool* hasPrivilege) {
    if (hasPrivilege) *hasPrivilege = true;
    return (HRESULT)0;
}

HRESULT WINAPI XUserClearCachedPrivileges(void*) { return (HRESULT)0; }
HRESULT WINAPI XUserSetPrivilege(void*, uint32_t, bool) { return (HRESULT)0; }
HRESULT WINAPI XUserSignOut(void*) { return (HRESULT)0; }

HRESULT WINAPI XUserGetTokenAndSignatureAsync(void*, uint32_t, const char*, const char*, size_t, const void*, size_t,
                                              const void*, XAsyncBlock* async) {
    return complete(async);
}

HRESULT WINAPI XUserGetTokenAndSignatureResultSize(XAsyncBlock*, size_t* size) {
    if (size) *size = 128;
    return (HRESULT)0;
}

HRESULT WINAPI XUserGetTokenAndSignatureResult(XAsyncBlock*, size_t size, void* buffer, void** data, size_t* used) {
    if (data) *data = buffer;
    if (used) *used = 0;
    if (buffer && size) std::memset(buffer, 0, size);
    return (HRESULT)0;
}

HRESULT WINAPI XUserResolvePrivilegeWithUiAsync(void*, uint32_t, uint32_t, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XUserResolvePrivilegeWithUiResult(XAsyncBlock*) { return (HRESULT)0; }

HRESULT WINAPI XUserResolveIssueWithUiAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XUserResolveIssueWithUiResult(XAsyncBlock*) { return (HRESULT)0; }

HRESULT WINAPI XUserChangeEventRegister(void*, void*, void** token) {
    if (token) *token = stubutil::dummy_pointer();
    return (HRESULT)0;
}
void WINAPI XUserChangeEventUnregister(void*) {}

}  // extern "C"
