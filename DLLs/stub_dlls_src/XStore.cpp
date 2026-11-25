// Offline XStore.dll replacement.  All license/product queries report success
// immediately; async callbacks run inline with S_OK.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

HRESULT complete(XAsyncBlock* async) {
    if (!async) return (HRESULT)0x80004003;
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

}  // namespace

extern "C" {

HRESULT WINAPI XStoreQueryGameLicenseAsync(void*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreQueryGameLicenseResult(XAsyncBlock*, void* license) {
    if (license) std::memset(license, 0, 64);
    return (HRESULT)0;
}
HRESULT WINAPI XStoreQueryGameLicenseResultSize(XAsyncBlock*, size_t* size) {
    if (size) *size = 64;
    return (HRESULT)0;
}

HRESULT WINAPI XStoreAcquireLicenseForDurablesAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreAcquireLicenseForDurablesResult(XAsyncBlock*) { return (HRESULT)0; }

HRESULT WINAPI XStoreAcquireLicenseForPackageAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreAcquireLicenseForPackageResult(XAsyncBlock*) { return (HRESULT)0; }

HRESULT WINAPI XStoreQueryEntitledProductsAsync(void*, uint32_t, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreQueryEntitledProductsResult(XAsyncBlock*, void** products) {
    if (products) *products = stubutil::dummy_pointer();
    return (HRESULT)0;
}
HRESULT WINAPI XStoreQueryEntitledProductsResultSize(XAsyncBlock*, size_t* size) {
    if (size) *size = 0;
    return (HRESULT)0;
}

HRESULT WINAPI XStoreQueryAssociatedProductsAsync(void*, const char*, uint32_t, XAsyncBlock* async) {
    return complete(async);
}
HRESULT WINAPI XStoreQueryAssociatedProductsResult(XAsyncBlock*, void** products) {
    if (products) *products = stubutil::dummy_pointer();
    return (HRESULT)0;
}
HRESULT WINAPI XStoreQueryAssociatedProductsResultSize(XAsyncBlock*, size_t* size) {
    if (size) *size = 0;
    return (HRESULT)0;
}

HRESULT WINAPI XStoreQueryConsumableBalanceRemainingAsync(void*, const char*, XAsyncBlock* async) {
    return complete(async);
}
HRESULT WINAPI XStoreQueryConsumableBalanceRemainingResult(XAsyncBlock*, uint32_t* balance) {
    if (balance) *balance = 1;
    return (HRESULT)0;
}

HRESULT WINAPI XStoreReportConsumableFulfillmentAsync(void*, const char*, const char*, uint32_t, XAsyncBlock* async) {
    return complete(async);
}
HRESULT WINAPI XStoreReportConsumableFulfillmentResult(XAsyncBlock*) { return (HRESULT)0; }

HRESULT WINAPI XStoreShowProductPageUIAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreShowPurchaseUIAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreShowRedeemTokenUIAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }

HRESULT WINAPI XStoreGetUserCollectionsIdAsync(void*, const char*, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreGetUserCollectionsIdResult(XAsyncBlock*, char* buffer, size_t size, size_t* used) {
    static const char id[] = "00000000-0000-0000-0000-000000000000";
    size_t needed = sizeof(id);
    if (used) *used = needed;
    if (!buffer) return (HRESULT)0x80004003;
    if (size < needed) return (HRESULT)0x8007007a;
    std::memcpy(buffer, id, needed);
    return (HRESULT)0;
}

HRESULT WINAPI XStoreGetUserPurchaseIdAsync(void*, const char*, uint32_t, XAsyncBlock* async) { return complete(async); }
HRESULT WINAPI XStoreGetUserPurchaseIdResult(XAsyncBlock*, char* buffer, size_t size, size_t* used) {
    static const char id[] = "00000000-0000-0000-0000-000000000000";
    size_t needed = sizeof(id);
    if (used) *used = needed;
    if (!buffer) return (HRESULT)0x80004003;
    if (size < needed) return (HRESULT)0x8007007a;
    std::memcpy(buffer, id, needed);
    return (HRESULT)0;
}

}  // extern "C"
