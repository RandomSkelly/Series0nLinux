// Offline xgameruntime.dll replacement.
//
// The game does not import this module.  xgameruntime.lib-style thunks call
// LoadLibraryExW(L"xgameruntime.dll") and resolve the six internal API exports
// below.  QueryApiImpl hands out process-wide XThreadingImpl/XErrorImpl
// singletons with the measured vtable order, so the game's 144 X-prefixed
// thunks can run without the Microsoft Gaming Runtime.
#include "common.h"

#include <atomic>

namespace {

struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

constexpr Guid kIUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr Guid kThreadingClass = {0x073b7dcb, 0x1fcf, 0x4030, {0x94, 0xbe, 0xe3, 0xc9, 0xeb, 0x62, 0x34, 0x28}};
constexpr Guid kErrorClass = {0x8ca467f7, 0x22e8, 0x4096, {0x84, 0x56, 0xbb, 0x8a, 0xa1, 0x3f, 0x79, 0xd8}};

inline bool same_guid(const void* a, const Guid& b) {
    return a && std::memcmp(a, &b, sizeof(Guid)) == 0;
}

struct RuntimeObject {
    const void* const* vtable;
    const Guid* classId;
    std::atomic<long> refs;
};

HRESULT STDMETHODCALLTYPE object_query_interface(void* self, const void* iid, void** out) {
    if (!out) return (HRESULT)0x80004003;  // E_POINTER
    *out = nullptr;
    RuntimeObject* object = static_cast<RuntimeObject*>(self);
    if (!object) return (HRESULT)0x80004003;
    if (same_guid(iid, kIUnknown) || same_guid(iid, *object->classId)) {
        object->refs.fetch_add(1);
        *out = self;
        return (HRESULT)0;
    }
    return (HRESULT)0x80004002;  // E_NOINTERFACE
}

ULONG STDMETHODCALLTYPE object_add_ref(void* self) {
    RuntimeObject* object = static_cast<RuntimeObject*>(self);
    return object ? (ULONG)(object->refs.fetch_add(1) + 1) : 1;
}

ULONG STDMETHODCALLTYPE object_release(void* self) {
    RuntimeObject* object = static_cast<RuntimeObject*>(self);
    if (!object) return 1;
    long previous = object->refs.load();
    while (previous > 1 && !object->refs.compare_exchange_weak(previous, previous - 1)) {
    }
    return (ULONG)(previous > 1 ? previous - 1 : 1);
}

// ---------- task queue ----------

struct QueueObject {
    uint32_t magic;
};
constexpr uint32_t kQueueMagic = 0x58425155;  // 'XQBU'
static QueueObject g_queue = {kQueueMagic};
static QueueObject* g_processQueue = &g_queue;

struct PendingCallback {
    XTaskQueueCallback callback;
    void* context;
    PendingCallback* next;
};
static CRITICAL_SECTION g_queueLock;
static bool g_queueLockReady = false;
static PendingCallback* g_pendingHead = nullptr;
static PendingCallback* g_pendingTail = nullptr;

void queue_lock() {
    if (g_queueLockReady) EnterCriticalSection(&g_queueLock);
}
void queue_unlock() {
    if (g_queueLockReady) LeaveCriticalSection(&g_queueLock);
}

// ---------- async library ----------

static HRESULT run_provider_pass(XAsyncBlock* async, XAsyncProvider provider, void* context, uint32_t op) {
    if (!async || !provider) return (HRESULT)0;
    XAsyncProviderData data = {async, 0, nullptr, context};
    return provider(op, &data);
}

// The title opens Xbox absolute paths such as platform:\shaders\... .  Wine
// treats the volume name as a relative drive path (G:\platform:\...), so those
// opens fail until the corresponding directories and placeholder files exist.
// Create the small subset needed to get past shader/config initialization.
struct PlaceholderFile {
    const wchar_t* path;
    const unsigned char* data;
    DWORD size;
};

static void ensure_directory(const wchar_t* path) {
    CreateDirectoryW(path, nullptr);
}

// The audio metadata loader requires a real RPF image (magic 0x52504639 / "9FPR").
// Copy a small valid archive from the title root instead of creating an empty file.
static void copy_file(const wchar_t* source, const wchar_t* destination) {
    HANDLE input = CreateFileW(source, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) return;

    HANDLE output = CreateFileW(destination, GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output != INVALID_HANDLE_VALUE) {
        unsigned char buffer[16384];
        DWORD bytes_read = 0;
        if (ReadFile(input, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read) {
            DWORD bytes_written = 0;
            WriteFile(output, buffer, bytes_read, &bytes_written, nullptr);
        }
        CloseHandle(output);
    }
    CloseHandle(input);
}

static void create_placeholders() {
    static const wchar_t* directories[] = {
        L"audio:",
        L"common:",
        L"common:\\data",
        L"platform:",
        L"platform:\\data",
        L"platform:\\data\\effects",
        L"platform:\\data\\settings",
        L"platform:\\shaders",
        L"platform:\\shaders\\v2",
        L"platform:\\shaders\\v2\\fxdb",
        L"platform:\\shaders\\v2\\sga_xbsx_final",
    };
    for (const wchar_t* path : directories) ensure_directory(path);
    copy_file(L"appdata0.rpf", L"audio:\\Core.Metadata.rpf");

    static const unsigned char kVersion[] = {'1', '.', '0', '.', '2', '.', '1', '\n'};
    static const unsigned char kShaderHeader[] = {0x53, 0x68, 0x06, 0x01, 0, 0, 0, 0};
    static const PlaceholderFile files[] = {
        {L"common:\\data\\version.txt", kVersion, sizeof(kVersion)},
        {L"platform:\\data\\gameconfig.zmt", nullptr, 0},
        {L"platform:\\data\\language_sheets.zmt", nullptr, 0},
        {L"platform:\\data\\effects\\animimgfilters.zmt", nullptr, 0},
        {L"platform:\\data\\settings\\presets.zmt", nullptr, 0},
        {L"platform:\\shaders\\v2\\fxdb\\sga_xbsx_final.fxdb", kShaderHeader, sizeof(kShaderHeader)},
        {L"platform:\\shaders\\v2\\fxdb\\sga_xbsx_final_init.fxdb", kShaderHeader, sizeof(kShaderHeader)},
        {L"platform:\\shaders\\v2\\sga_xbsx_final_aco.d3d12PipelineCache", nullptr, 0},
        {L"platform:\\shaders\\v2\\sga_xbsx_final\\AnimImgFilter.fxc", kShaderHeader, sizeof(kShaderHeader)},
        {L"platform:\\shaders\\v2\\sga_xbsx_final\\im.fxc", kShaderHeader, sizeof(kShaderHeader)},
        {L"platform:\\shaders\\v2\\sga_xbsx_final\\postfx_footage.fxc", kShaderHeader, sizeof(kShaderHeader)},
    };
    for (const PlaceholderFile& file : files) {
        // Never replace real shader data placed here externally.  CREATE_NEW
        // leaves an existing file untouched and only writes the placeholder
        // when the path is absent.
        HANDLE handle = CreateFileW(file.path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        if (file.size) {
            DWORD written = 0;
            WriteFile(handle, file.data, file.size, &written, nullptr);
        }
        CloseHandle(handle);
    }
}

}  // namespace

extern "C" {

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH && !g_queueLockReady) {
        create_placeholders();
        InitializeCriticalSection(&g_queueLock);
        g_queueLockReady = true;
    } else if (reason == DLL_PROCESS_DETACH && g_queueLockReady) {
        DeleteCriticalSection(&g_queueLock);
        g_queueLockReady = false;
    }
    return TRUE;
}

HRESULT WINAPI DllCanUnloadNow(void) { return (HRESULT)0; }

// ---------- runtime initialization ----------

static std::atomic<int> g_apiRefs{0};
static std::atomic<int> g_runtimeRefs{0};

struct ApiImplOptions {
    uint32_t unknown;
    BOOL isInline;
    const char* gameConfig;
};

static HRESULT initialize_impl(const char*, ULONG, ULONG, int, const ApiImplOptions*) {
    g_apiRefs.fetch_add(1);
    return (HRESULT)0;
}

HRESULT WINAPI InitializeApiImpl(ULONG gdkVersion, ULONG gsVersion) {
    return initialize_impl("InitializeApiImpl", gdkVersion, gsVersion, -1, nullptr);
}

HRESULT WINAPI InitializeApiImplEx(ULONG gdkVersion, ULONG gsVersion, char mode) {
    return initialize_impl("InitializeApiImplEx", gdkVersion, gsVersion, (unsigned char)mode, nullptr);
}

HRESULT WINAPI InitializeApiImplEx2(ULONG gdkVersion, ULONG gsVersion, char mode, const ApiImplOptions* options) {
    return initialize_impl("InitializeApiImplEx2", gdkVersion, gsVersion, (unsigned char)mode, options);
}

HRESULT WINAPI UninitializeApiImpl(void) {
    int previous = g_apiRefs.load();
    while (previous > 0 && !g_apiRefs.compare_exchange_weak(previous, previous - 1)) {
    }
    return (HRESULT)0;
}

HRESULT WINAPI XGameRuntimeInitialize(void) {
    g_runtimeRefs.fetch_add(1);
    return (HRESULT)0;
}

void WINAPI XGameRuntimeUninitialize(void) {
    int previous = g_runtimeRefs.load();
    while (previous > 0 && !g_runtimeRefs.compare_exchange_weak(previous, previous - 1)) {
    }
}

BOOL WINAPI XGameRuntimeIsFeatureAvailable(uint32_t) { return TRUE; }

// ---------- error reporting ----------

typedef bool(CALLBACK* XErrorCallback)(HRESULT hr, const char* message, void* context);
static std::atomic<XErrorCallback> g_errorCallback{nullptr};
static std::atomic<void*> g_errorContext{nullptr};

void WINAPI XErrorReport(HRESULT hr, const char* message) {
    XErrorCallback callback = g_errorCallback.load();
    if (callback) callback(hr, message, g_errorContext.load());
}

void WINAPI XErrorSetCallback(XErrorCallback callback, void* context) {
    g_errorContext = context;
    g_errorCallback = callback;
}

HRESULT WINAPI XErrorSetOptions(uint32_t, uint32_t) { return (HRESULT)0; }

// ---------- async library ----------

HRESULT WINAPI XAsyncGetStatus(const XAsyncBlock* async, bool) {
    if (!async) return (HRESULT)0x80004003;
    HRESULT status = stubutil::completion_status(const_cast<XAsyncBlock*>(async));
    return status;  // E_PENDING while queued, otherwise the final result
}

HRESULT WINAPI XAsyncGetResultSize(const XAsyncBlock* async, size_t* resultSize) {
    if (resultSize) *resultSize = 0;
    return async ? (HRESULT)0 : (HRESULT)0x80004003;
}

HRESULT WINAPI XAsyncCancel(XAsyncBlock* async) {
    if (!async) return (HRESULT)0x80004003;
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XAsyncRun(XAsyncBlock* async, XAsyncWork work) {
    if (!async) return (HRESULT)0x80004003;
    stubutil::begin_async(async);
    if (work) work(async->context);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XAsyncBegin(XAsyncBlock* async, void* context, void*, const char*, XAsyncProvider provider) {
    if (!async) return (HRESULT)0x80004003;
    stubutil::begin_async(async);
    ((uintptr_t*)async->internal)[2] = (uintptr_t)provider;
    ((uintptr_t*)async->internal)[3] = (uintptr_t)context;

    HRESULT hr = run_provider_pass(async, provider, context, 0);  // Begin
    if (hr == (HRESULT)0x80070001) return (HRESULT)0;              // scheduled via DoWork
    if (hr >= 0) {
        run_provider_pass(async, provider, context, 2);            // GetResult
        stubutil::complete_async(async, (HRESULT)0, 0);
    } else {
        stubutil::complete_async(async, hr, 0);
    }
    return (HRESULT)0;
}

HRESULT WINAPI XAsyncSchedule(XAsyncBlock* async, uint32_t) {
    if (!async || !stubutil::async_pending(async)) return (HRESULT)0;
    XAsyncProvider provider = (XAsyncProvider)((uintptr_t*)async->internal)[2];
    void* context = (void*)((uintptr_t*)async->internal)[3];
    HRESULT hr = run_provider_pass(async, provider, context, 1);   // DoWork
    if (hr >= 0) {
        run_provider_pass(async, provider, context, 2);            // GetResult
        stubutil::complete_async(async, (HRESULT)0, 0);
    } else {
        stubutil::complete_async(async, hr, 0);
    }
    return (HRESULT)0;
}

void WINAPI XAsyncComplete(XAsyncBlock* async, HRESULT result, size_t requiredBufferSize) {
    stubutil::complete_async(async, result, requiredBufferSize);
}

HRESULT WINAPI XAsyncGetResult(const XAsyncBlock*, void*, size_t, void*, size_t* bufferUsed) {
    if (bufferUsed) *bufferUsed = 0;
    return (HRESULT)0;
}

// ---------- task queue ----------

HRESULT WINAPI XTaskQueueCreate(uint32_t, uint32_t, void** queue) {
    if (!queue) return (HRESULT)0x80004003;
    g_queue.magic = kQueueMagic;
    *queue = &g_queue;
    return (HRESULT)0;
}

HRESULT WINAPI XTaskQueueCreateComposite(void*, void*, void** queue) {
    return XTaskQueueCreate(0, 0, queue);
}

HRESULT WINAPI XTaskQueueGetPort(void*, uint32_t* port) {
    if (port) *port = 0;
    return (HRESULT)0;
}

HRESULT WINAPI XTaskQueueDuplicateHandle(void* queue, void** duplicate) {
    if (!duplicate) return (HRESULT)0x80004003;
    *duplicate = queue ? queue : &g_queue;
    return (HRESULT)0;
}

bool WINAPI XTaskQueueDispatch(void*, uint32_t, uint32_t timeoutInMs) {
    queue_lock();
    PendingCallback* item = g_pendingHead;
    if (item) {
        g_pendingHead = item->next;
        if (!g_pendingHead) g_pendingTail = nullptr;
    }
    queue_unlock();

    if (!item) {
        DWORD delay = timeoutInMs == 0 ? 1 : (timeoutInMs > 16 ? 16 : timeoutInMs);
        Sleep(delay);
        return false;
    }

    XTaskQueueCallback callback = item->callback;
    void* context = item->context;
    delete item;
    if (callback) callback(context);
    return true;
}

void WINAPI XTaskQueueCloseHandle(void*) {}

HRESULT WINAPI XTaskQueueSubmitCallback(void*, uint32_t, XTaskQueueCallback callback, void* context) {
    if (!callback) return (HRESULT)0x80004003;
    PendingCallback* item = new PendingCallback{callback, context, nullptr};
    queue_lock();
    if (g_pendingTail) {
        g_pendingTail->next = item;
    } else {
        g_pendingHead = item;
    }
    g_pendingTail = item;
    queue_unlock();
    return (HRESULT)0;
}

HRESULT WINAPI XTaskQueueSubmitDelayedCallback(void* queue, uint32_t port, uint32_t, XTaskQueueCallback callback,
                                               void* context) {
    return XTaskQueueSubmitCallback(queue, port, callback, context);
}

HRESULT WINAPI XTaskQueueRegisterWaiter(void*, void*, void** token) {
    if (token) *token = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI XTaskQueueUnregisterWaiter(void*, void*) { return (HRESULT)0; }

void WINAPI XTaskQueueTerminate(void*, bool, void*, void*) {}

HRESULT WINAPI XTaskQueueRegisterMonitor(void*, void*, void*, void** token) {
    if (token) *token = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI XTaskQueueUnregisterMonitor(void*, void*) { return (HRESULT)0; }

HRESULT WINAPI XTaskQueueGetCurrentProcessTaskQueue(void** queue) {
    if (!queue) return (HRESULT)0x80004003;
    *queue = g_processQueue;
    return (HRESULT)0;
}

void WINAPI XTaskQueueSetCurrentProcessTaskQueue(void* queue) {
    g_processQueue = queue ? static_cast<QueueObject*>(queue) : &g_queue;
}

// ---------- threading ----------

HRESULT WINAPI XThreadSetTimeSensitive(bool value) {
    static thread_local bool sensitive = false;
    sensitive = value;
    return (HRESULT)0;
}
bool WINAPI XThreadIsTimeSensitive(void) {
    static thread_local bool sensitive = false;
    return sensitive;
}
void WINAPI XThreadAssertNotTimeSensitive(void) {}

// ---------- system / package ----------

uint32_t WINAPI XSystemGetDeviceType(void) { return 7; }  // Xbox Series X

HRESULT WINAPI XSystemGetConsoleId(size_t size, char* consoleId, size_t* used) {
    static const char id[] = "00000000.00000000.00000000.00000000.00";
    if (!consoleId) return (HRESULT)0x80070057;
    if (size < sizeof(id)) return (HRESULT)0x8007007a;
    std::memcpy(consoleId, id, sizeof(id));
    if (used) *used = sizeof(id);
    return (HRESULT)0;
}

uint64_t WINAPI XSystemGetAppMemoryUsageLimit(void) {
    MEMORYSTATUSEX status = {sizeof(status)};
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : (12ULL * 1024 * 1024 * 1024);
}
uint64_t WINAPI XSystemGetAppMemoryUsage(void) { return 0; }

struct XVersion {
    uint16_t major, minor, build, revision;
};
struct XSystemAnalyticsInfo {
    XVersion osVersion;
    XVersion hostingOsVersion;
    char family[64];
    char form[64];
};

XSystemAnalyticsInfo WINAPI XSystemGetAnalyticsInfo(void) {
    XSystemAnalyticsInfo info = {};
    info.osVersion = {10, 0, 22000, 0};
    info.hostingOsVersion = {10, 0, 22000, 0};
    std::strncpy(info.family, "Xbox", sizeof(info.family) - 1);
    std::strncpy(info.form, "Xbox Series X", sizeof(info.form) - 1);
    return info;
}

HRESULT WINAPI XPackageGetCurrentProcessPackageIdentifier(size_t size, char* buffer) {
    static const char id[] = "GTA6ScarlettOffline00000000000";
    if (!buffer || size == 0) return (HRESULT)0x80070057;
    if (size < sizeof(id)) return (HRESULT)0x8007007a;
    std::memcpy(buffer, id, sizeof(id));
    return (HRESULT)0;
}

HRESULT WINAPI XPackageGetMountPath(void*, size_t size, char* path) {
    if (!path || size == 0) return (HRESULT)0x80070057;
    char modulePath[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath));
    if (length == 0 || length >= sizeof(modulePath)) return (HRESULT)0x80070002;
    char* slash = std::strrchr(modulePath, '\\');
    char* forward = std::strrchr(modulePath, '/');
    char* last = slash > forward ? slash : forward;
    if (last) *last = '\0';
    size_t needed = std::strlen(modulePath) + 1;
    if (size < needed) return (HRESULT)0x8007007a;
    std::memcpy(path, modulePath, needed);
    return (HRESULT)0;
}

bool WINAPI XPackageIsPackagedProcess(void) { return true; }

// ---------- GDK services that travel through xgameruntime ----------

HRESULT WINAPI XNetworkingQueryPreferredLocalUdpMultiplayerPortAsync(XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, sizeof(uint16_t));
    return (HRESULT)0;
}

HRESULT WINAPI XNetworkingQueryPreferredLocalUdpMultiplayerPortResult(XAsyncBlock*, uint16_t* port) {
    if (port) *port = 7777;
    return (HRESULT)0;
}

HRESULT WINAPI XNetworkingGetConnectivityHint(void* hint) {
    if (hint) std::memset(hint, 0, 32);
    return (HRESULT)0;
}

HRESULT WINAPI XNetworkingRegisterConnectivityHintChanged(void*, void*, void** token) {
    if (token) *token = stubutil::dummy_pointer();
    return (HRESULT)0;
}

HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlAsync(void*, const char*, XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XNetworkingQuerySecurityInformationForUrlUtf16Async(void*, const WCHAR*, XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XGameProtocolRegisterForActivation(const char*, XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XGameProtocolGetActivationDetails(size_t size, void* details, size_t* used) {
    if (details) std::memset(details, 0, size);
    if (used) *used = 0;
    return (HRESULT)0;
}

HRESULT WINAPI XGameInviteRegisterForEvent(void*, void** token) {
    if (token) *token = stubutil::dummy_pointer();
    return (HRESULT)0;
}

void WINAPI XGameInviteUnregisterForEvent(void*) {}

HRESULT WINAPI XGameUiShowPlayerProfileCardAsync(void*, XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

HRESULT WINAPI XGameUiShowTextEntryAsync(void*, void*, void*, XAsyncBlock* async) {
    stubutil::begin_async(async);
    stubutil::complete_async(async, (HRESULT)0, 0);
    return (HRESULT)0;
}

// ---------- QueryApiImpl and the two served COM classes ----------

static HRESULT STDMETHODCALLTYPE error_report_method(void*, HRESULT hr, const char* message) {
    XErrorReport(hr, message);
    return (HRESULT)0;
}
static void STDMETHODCALLTYPE error_set_callback_method(void*, XErrorCallback callback, void* context) {
    XErrorSetCallback(callback, context);
}
static HRESULT STDMETHODCALLTYPE error_set_options_method(void*, uint32_t, uint32_t) { return (HRESULT)0; }

static uint64_t STDMETHODCALLTYPE private_slot(void*) { return 0; }

static HRESULT STDMETHODCALLTYPE m_async_get_status(void*, XAsyncBlock* a, bool b) { return XAsyncGetStatus(a, b); }
static HRESULT STDMETHODCALLTYPE m_async_get_result_size(void*, XAsyncBlock* a, size_t* b) { return XAsyncGetResultSize(a, b); }
static HRESULT STDMETHODCALLTYPE m_async_cancel(void*, XAsyncBlock* a) { return XAsyncCancel(a); }
static HRESULT STDMETHODCALLTYPE m_async_run(void*, XAsyncBlock* a, XAsyncWork b) { return XAsyncRun(a, b); }
static HRESULT STDMETHODCALLTYPE m_async_begin(void*, XAsyncBlock* a, void* b, void* c, const char* d, XAsyncProvider e) {
    return XAsyncBegin(a, b, c, d, e);
}
static HRESULT STDMETHODCALLTYPE m_async_schedule(void*, XAsyncBlock* a, uint32_t b) { return XAsyncSchedule(a, b); }
static void STDMETHODCALLTYPE m_async_complete(void*, XAsyncBlock* a, HRESULT b, size_t c) { XAsyncComplete(a, b, c); }
static HRESULT STDMETHODCALLTYPE m_async_get_result(void*, XAsyncBlock* a, void* b, size_t c, void* d, size_t* e) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    if (e) *e = 0;
    return (HRESULT)0;
}
static HRESULT STDMETHODCALLTYPE m_queue_create(void*, uint32_t a, uint32_t b, void** c) { return XTaskQueueCreate(a, b, c); }
static HRESULT STDMETHODCALLTYPE m_queue_composite(void*, void* a, void* b, void** c) { return XTaskQueueCreateComposite(a, b, c); }
static HRESULT STDMETHODCALLTYPE m_queue_get_port(void*, void* a, uint32_t* b) { return XTaskQueueGetPort(a, b); }
static HRESULT STDMETHODCALLTYPE m_queue_duplicate(void*, void* a, void** b) { return XTaskQueueDuplicateHandle(a, b); }
static bool STDMETHODCALLTYPE m_queue_dispatch(void*, void* a, uint32_t b, uint32_t c) { return XTaskQueueDispatch(a, b, c); }
static void STDMETHODCALLTYPE m_queue_close(void*, void* a) { XTaskQueueCloseHandle(a); }
static HRESULT STDMETHODCALLTYPE m_queue_submit(void*, void* a, uint32_t b, XTaskQueueCallback c, void* d) {
    return XTaskQueueSubmitCallback(a, b, c, d);
}
static HRESULT STDMETHODCALLTYPE m_queue_submit_delayed(void*, void* a, uint32_t b, uint32_t c, XTaskQueueCallback d, void* e) {
    return XTaskQueueSubmitDelayedCallback(a, b, c, d, e);
}
static HRESULT STDMETHODCALLTYPE m_queue_register_waiter(void*, void* a, void* b, void** c) {
    return XTaskQueueRegisterWaiter(a, b, c);
}
static HRESULT STDMETHODCALLTYPE m_queue_unregister_waiter(void*, void* a, void* b) { return XTaskQueueUnregisterWaiter(a, b); }
static void STDMETHODCALLTYPE m_queue_terminate(void*, void* a, bool b, void* c, void* d) { XTaskQueueTerminate(a, b, c, d); }
static HRESULT STDMETHODCALLTYPE m_queue_register_monitor(void*, void* a, void* b, void* c, void** d) {
    return XTaskQueueRegisterMonitor(a, b, c, d);
}
static HRESULT STDMETHODCALLTYPE m_queue_unregister_monitor(void*, void* a, void* b) { return XTaskQueueUnregisterMonitor(a, b); }
static HRESULT STDMETHODCALLTYPE m_queue_get_process(void*, void** a) { return XTaskQueueGetCurrentProcessTaskQueue(a); }
static void STDMETHODCALLTYPE m_queue_set_process(void*, void* a) { XTaskQueueSetCurrentProcessTaskQueue(a); }
static HRESULT STDMETHODCALLTYPE m_thread_set(void*, bool a) { return XThreadSetTimeSensitive(a); }
static void STDMETHODCALLTYPE m_thread_assert(void*) { XThreadAssertNotTimeSensitive(); }
static bool STDMETHODCALLTYPE m_thread_is(void*) { return XThreadIsTimeSensitive(); }

static const void* const kThreadingVtable[] = {
    (const void*)&object_query_interface,
    (const void*)&object_add_ref,
    (const void*)&object_release,
    (const void*)&m_async_get_status,          // 3
    (const void*)&m_async_get_result_size,     // 4
    (const void*)&m_async_cancel,              // 5
    (const void*)&m_async_run,                 // 6
    (const void*)&m_async_begin,               // 7
    (const void*)&private_slot,                // 8
    (const void*)&m_async_schedule,            // 9
    (const void*)&m_async_complete,            // 10
    (const void*)&m_async_get_result,          // 11
    (const void*)&m_queue_create,              // 12
    (const void*)&m_queue_composite,           // 13
    (const void*)&m_queue_get_port,            // 14
    (const void*)&m_queue_duplicate,           // 15
    (const void*)&m_queue_dispatch,            // 16
    (const void*)&m_queue_close,               // 17
    (const void*)&m_queue_submit,              // 18
    (const void*)&m_queue_submit_delayed,      // 19
    (const void*)&m_queue_register_waiter,     // 20
    (const void*)&m_queue_unregister_waiter,   // 21
    (const void*)&m_queue_terminate,           // 22
    (const void*)&m_queue_register_monitor,    // 23
    (const void*)&m_queue_unregister_monitor,  // 24
    (const void*)&m_queue_get_process,         // 25
    (const void*)&m_queue_set_process,         // 26
    (const void*)&m_thread_set,                // 27
    (const void*)&private_slot,                // 28
    (const void*)&m_thread_assert,             // 29
    (const void*)&m_thread_is,                 // 30
};

static const void* const kErrorVtable[] = {
    (const void*)&object_query_interface,
    (const void*)&object_add_ref,
    (const void*)&object_release,
    (const void*)&error_report_method,
    (const void*)&error_set_callback_method,
    (const void*)&error_set_options_method,
};

static RuntimeObject g_threading = {kThreadingVtable, &kThreadingClass, 1};
static RuntimeObject g_error = {kErrorVtable, &kErrorClass, 1};

HRESULT WINAPI QueryApiImpl(const Guid* classId, const Guid* interfaceId, void** out) {
    if (out) *out = nullptr;
    if (classId && out) {
        RuntimeObject* objects[] = {&g_threading, &g_error};
        for (RuntimeObject* object : objects) {
            if (same_guid(classId, *object->classId)) {
                return object_query_interface(object, interfaceId, out);
            }
        }
    }
    return (HRESULT)0x80070032;  // HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
}

}  // extern "C"
