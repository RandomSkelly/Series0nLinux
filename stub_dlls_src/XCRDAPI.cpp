// XCRDAPI.dll replacement.  The title uses the Xbox DirectStorage ABI through
// DStorageGetFactory.  This supplies the factory IID from its April 2026 GDK
// kit, plus the older kit IID, and returns working dummy COM objects whose
// vtable slots match the console headers (including the newer slot 10).
#include "common.h"

#include <algorithm>
#include <string>
#include <vector>

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kOk = (HRESULT)0;
constexpr HRESULT kFalse = (HRESULT)0x80004005;       // E_FAIL
constexpr HRESULT kPointer = (HRESULT)0x80004003;     // E_POINTER
constexpr HRESULT kNoInterface = (HRESULT)0x80004002; // E_NOINTERFACE
constexpr HRESULT kPending = (HRESULT)0x80070001;     // E_PENDING

struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

constexpr Guid kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr Guid kIidFactoryOld = {0xFB3B54AC, 0xBF82, 0x463A, {0xB7, 0x77, 0x73, 0x19, 0x58, 0x37, 0x86, 0x4D}};
constexpr Guid kIidFactoryNew = {0x62959D11, 0xD45F, 0x4884, {0xA0, 0xA3, 0xF7, 0x86, 0x2E, 0xA2, 0x43, 0xAE}};
constexpr Guid kIidQueueOld = {0xA3F6649A, 0x8C2C, 0x4B3A, {0x8B, 0x6E, 0x10, 0x18, 0x6A, 0x04, 0xB9, 0xBC}};
constexpr Guid kIidQueueNew = {0x3C02CB0C, 0xC050, 0x4195, {0x99, 0xA1, 0xA1, 0xCE, 0x94, 0xBA, 0x33, 0xC5}};
constexpr Guid kIidFile = {0x43704F9F, 0x27C6, 0x4A9F, {0x88, 0x15, 0xD1, 0x65, 0x27, 0x89, 0x4D, 0xF4}};
constexpr Guid kIidStatus = {0xCD66D752, 0x2786, 0x44F7, {0x90, 0x70, 0xE2, 0xA6, 0xFD, 0xAD, 0x3F, 0x1B}};

bool same_iid(const Guid* a, const Guid& b) {
    return a && std::memcmp(a, &b, sizeof(Guid)) == 0;
}

struct ComObject {
    void** vtable;
};

ULONG STDMETHODCALLTYPE com_add_ref(void*) { return 1; }
ULONG STDMETHODCALLTYPE com_release(void*) { return 1; }
HRESULT STDMETHODCALLTYPE com_ok(void*) { return kOk; }
void STDMETHODCALLTYPE com_void(void*) {}
uint64_t STDMETHODCALLTYPE com_u64(void*) { return 0; }
bool STDMETHODCALLTYPE com_false(void*) { return false; }

template <typename... Guids>
HRESULT query_iid(void* self, const Guid* riid, void** out, Guids... iids) {
    if (!out) return kPointer;
    *out = nullptr;
    if (!riid) return kNoInterface;
    if (same_iid(riid, kIidUnknown) || ((same_iid(riid, iids)) || ...)) {
        *out = self;
        return kOk;
    }
    return kNoInterface;
}

// Console DirectStorage layouts, measured from the public GDK shims.
struct QueueDesc {
    uint64_t sourceType;
    uint16_t capacity;
    int8_t priority;
    const char* name;
};
struct NewerQueueDesc {
    QueueDesc desc;
    uint64_t unknown;
};
struct Request {
    uint64_t options;
    void* destination;
    uint64_t destinationPageOffset;
    uint32_t destinationSize;
    void* source;
    uint64_t fileOffset;
    uint32_t sourceSize;
    uint32_t intermediateSize;
    uint64_t tag;
    const char* name;
};
struct QueueInfo {
    QueueDesc desc;
    uint16_t emptySlots;
    uint16_t untilSubmit;
};
struct ErrorRecord {
    uint32_t failures;
    HRESULT first;
    unsigned char rest[1024];
};

ComObject g_factory;
ComObject g_queue;
ComObject g_file;
ComObject g_status;

HANDLE g_file_handle = INVALID_HANDLE_VALUE;
HANDLE g_error_event = nullptr;
bool g_status_done[256] = {0};
HRESULT g_status_result[256] = {0};

std::wstring game_directory() {
    wchar_t path[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(length ? path : L".");
    size_t slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos) result.resize(slash);
    return result;
}

std::wstring console_path(const wchar_t* path) {
    if (!path) return L"";
    std::wstring direct(path);
    for (wchar_t& c : direct) {
        if (c == L'/') c = L'\\';
    }
    HANDLE probe = CreateFileW(direct.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return direct;
    }

    std::wstring relative(path);
    if (relative.size() >= 2 && relative[1] == L':') relative = relative.substr(2);
    while (!relative.empty() && (relative[0] == L'\\' || relative[0] == L'/')) relative.erase(0, 1);
    for (wchar_t& c : relative) {
        if (c == L'/') c = L'\\';
    }
    return game_directory() + L"\\" + relative;
}

// ---------------------------------------------------------------------------
// IDStorageFactoryX
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE factory_query(void* self, const Guid* riid, void** out) {
    return query_iid(self, riid, out, kIidFactoryOld, kIidFactoryNew);
}

HRESULT STDMETHODCALLTYPE factory_create_queue(void*, const QueueDesc*, const Guid* riid, void** queue) {
    if (!queue) return kPointer;
    return query_iid(&g_queue, riid, queue, kIidQueueOld, kIidQueueNew);
}

HRESULT STDMETHODCALLTYPE factory_open_file(void*, const wchar_t* path, const Guid* riid, void** file) {
    if (!file) return kPointer;
    std::wstring open_path = console_path(path);
    g_file_handle = CreateFileW(open_path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    return query_iid(&g_file, riid, file, kIidFile);
}

HRESULT STDMETHODCALLTYPE factory_status_array(void*, uint32_t, const char*, const Guid* riid, void** status) {
    if (!status) return kPointer;
    std::memset(g_status_done, 0, sizeof(g_status_done));
    std::memset(g_status_result, 0, sizeof(g_status_result));
    return query_iid(&g_status, riid, status, kIidStatus);
}

HRESULT STDMETHODCALLTYPE factory_new_queue(void*, const NewerQueueDesc* desc, const Guid* riid, void** queue) {
    if (!desc) return kPointer;
    return factory_create_queue(nullptr, &desc->desc, riid, queue);
}

// ---------------------------------------------------------------------------
// IDStorageQueueX
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE queue_query(void* self, const Guid* riid, void** out) {
    return query_iid(self, riid, out, kIidQueueOld, kIidQueueNew);
}

void STDMETHODCALLTYPE queue_enqueue_request(void*, const Request* request) {
    if (!request || !request->destination) return;
    std::memset(request->destination, 0, request->destinationSize);

    const uint64_t fromMemory = 1ull << 22;
    if (request->options & fromMemory) {
        if (request->source && request->sourceSize)
            std::memcpy(request->destination, request->source,
                        std::min<uint32_t>(request->sourceSize, request->destinationSize));
        return;
    }
    if (g_file_handle == INVALID_HANDLE_VALUE || !request->sourceSize) return;
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(request->fileOffset);
    at.OffsetHigh = static_cast<DWORD>(request->fileOffset >> 32);
    DWORD done = 0;
    ReadFile(g_file_handle, request->destination,
             std::min<uint32_t>(request->sourceSize, request->destinationSize), &done, &at);
}

void STDMETHODCALLTYPE queue_enqueue_status(void*, void* status, uint32_t index) {
    (void)status;
    if (index < 256) {
        g_status_done[index] = true;
        g_status_result[index] = kOk;
    }
}

void STDMETHODCALLTYPE queue_enqueue_signal(void*, void*, uint64_t) {}

void STDMETHODCALLTYPE queue_submit(void*) {}
void STDMETHODCALLTYPE queue_cancel(void*, uint64_t, uint64_t) {}
void STDMETHODCALLTYPE queue_close(void*) {}

HANDLE STDMETHODCALLTYPE queue_error_event(void*) {
    if (!g_error_event) g_error_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return g_error_event;
}

void STDMETHODCALLTYPE queue_error_record(void*, ErrorRecord* record) {
    if (!record) return;
    std::memset(record, 0, sizeof(*record));
    record->first = kOk;
    if (g_error_event) ResetEvent(g_error_event);
}

void STDMETHODCALLTYPE queue_query_info(void*, QueueInfo* info) {
    if (!info) return;
    std::memset(info, 0, sizeof(*info));
    info->desc.capacity = 64;
    info->emptySlots = 64;
}

// ---------------------------------------------------------------------------
// IDStorageFileX and IDStorageStatusArrayX
// ---------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE file_query(void* self, const Guid* riid, void** out) {
    return query_iid(self, riid, out, kIidFile);
}

void STDMETHODCALLTYPE file_close(void*) {}
HANDLE STDMETHODCALLTYPE file_handle(void*) { return g_file_handle; }

HRESULT STDMETHODCALLTYPE status_query(void* self, const Guid* riid, void** out) {
    return query_iid(self, riid, out, kIidStatus);
}

bool STDMETHODCALLTYPE status_complete(void*, uint32_t index) {
    return index < 256 && g_status_done[index];
}

HRESULT STDMETHODCALLTYPE status_result(void*, uint32_t index) {
    if (index >= 256) return kFalse;
    return g_status_done[index] ? g_status_result[index] : kPending;
}

// ---------------------------------------------------------------------------
// vtables
// ---------------------------------------------------------------------------

void* factory_vtable[16] = {0};
void* queue_vtable[16] = {0};
void* file_vtable[8] = {0};
void* status_vtable[8] = {0};

struct Initializer {
    Initializer() {
        for (void*& entry : factory_vtable) entry = reinterpret_cast<void*>(&com_ok);
        for (void*& entry : queue_vtable) entry = reinterpret_cast<void*>(&com_void);
        for (void*& entry : file_vtable) entry = reinterpret_cast<void*>(&com_ok);
        for (void*& entry : status_vtable) entry = reinterpret_cast<void*>(&com_ok);

        factory_vtable[0] = reinterpret_cast<void*>(&factory_query);
        factory_vtable[1] = reinterpret_cast<void*>(&com_add_ref);
        factory_vtable[2] = reinterpret_cast<void*>(&com_release);
        factory_vtable[3] = reinterpret_cast<void*>(&factory_create_queue);
        factory_vtable[4] = reinterpret_cast<void*>(&factory_open_file);
        factory_vtable[5] = reinterpret_cast<void*>(&factory_status_array);
        factory_vtable[6] = reinterpret_cast<void*>(&com_ok);
        factory_vtable[7] = reinterpret_cast<void*>(&com_void);
        factory_vtable[8] = reinterpret_cast<void*>(&com_ok);
        factory_vtable[9] = reinterpret_cast<void*>(&com_ok);
        factory_vtable[10] = reinterpret_cast<void*>(&factory_new_queue);

        queue_vtable[0] = reinterpret_cast<void*>(&queue_query);
        queue_vtable[1] = reinterpret_cast<void*>(&com_add_ref);
        queue_vtable[2] = reinterpret_cast<void*>(&com_release);
        queue_vtable[3] = reinterpret_cast<void*>(&queue_enqueue_request);
        queue_vtable[4] = reinterpret_cast<void*>(&queue_enqueue_status);
        queue_vtable[5] = reinterpret_cast<void*>(&queue_enqueue_signal);
        queue_vtable[6] = reinterpret_cast<void*>(&queue_submit);
        queue_vtable[7] = reinterpret_cast<void*>(&queue_cancel);
        queue_vtable[8] = reinterpret_cast<void*>(&queue_close);
        queue_vtable[9] = reinterpret_cast<void*>(&queue_error_event);
        queue_vtable[10] = reinterpret_cast<void*>(&queue_error_record);
        queue_vtable[11] = reinterpret_cast<void*>(&queue_query_info);

        file_vtable[0] = reinterpret_cast<void*>(&file_query);
        file_vtable[1] = reinterpret_cast<void*>(&com_add_ref);
        file_vtable[2] = reinterpret_cast<void*>(&com_release);
        file_vtable[3] = reinterpret_cast<void*>(&file_close);
        file_vtable[4] = reinterpret_cast<void*>(&file_handle);

        status_vtable[0] = reinterpret_cast<void*>(&status_query);
        status_vtable[1] = reinterpret_cast<void*>(&com_add_ref);
        status_vtable[2] = reinterpret_cast<void*>(&com_release);
        status_vtable[3] = reinterpret_cast<void*>(&status_complete);
        status_vtable[4] = reinterpret_cast<void*>(&status_result);

        g_factory.vtable = factory_vtable;
        g_queue.vtable = queue_vtable;
        g_file.vtable = file_vtable;
        g_status.vtable = status_vtable;
    }
} initializer;

HRESULT WINAPI get_factory(const Guid* riid, void** factory) {
    return factory_query(&g_factory, riid, factory);
}

}  // namespace

extern "C" {

HRESULT WINAPI XcrdGetFactory(const void* riid, void** factory) {
    return get_factory(static_cast<const Guid*>(riid), factory);
}

HRESULT WINAPI XcrdOk(void) { return kOk; }

HRESULT WINAPI XcrdBuffer(void* buffer, size_t size) {
    if (buffer) std::memset(buffer, 0, size);
    return kOk;
}

}  // extern "C"
