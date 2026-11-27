// d3d12_xs.dll replacement.  Standard D3D12 entry points are forwarded to the
// native d3d12.dll supplied by Proton.  Xbox device creation is answered with a
// dummy COM object whose vtable covers the Xbox-only methods the title calls.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kFail = (HRESULT)0x80004005;

// Diagnostic for run436: the EXE publishes its worker-ready semaphore handle
// in this global shortly after XsD3D12XboxCreateDevice.  Log with the same
// WriteFile + named-mutex tracer used by common.h.
constexpr ULONG_PTR kWorkerSemaphoreGlobal = 0x147bc7f68ULL;
constexpr ULONG_PTR kProducerGate = 0x147bc7f90ULL;
constexpr LONG kWorkerSemaphoreBurst = 8;

__attribute__((no_instrument_function))
void diag_sem_release(ULONG_PTR handle, BOOL ok, LONG count) {
    static HANDLE diag_file = INVALID_HANDLE_VALUE;
    static HANDLE diag_mutex = nullptr;
    if (!diag_mutex) {
        diag_mutex = CreateMutexA(nullptr, FALSE, "shim_trace_lock");
    }
    if (diag_mutex) {
        WaitForSingleObject(diag_mutex, INFINITE);
    }
    if (diag_file == INVALID_HANDLE_VALUE) {
        diag_file = CreateFileA("trace.log",
                                FILE_WRITE_DATA | FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    }
    if (diag_file != INVALID_HANDLE_VALUE) {
        SetFilePointer(diag_file, 0, nullptr, FILE_END);
        shim_write_cstr(diag_file, "D,semrelease,");
        shim_write_dec(diag_file, (unsigned long long)GetCurrentThreadId());
        shim_write_cstr(diag_file, ",");
        shim_write_hex(diag_file, handle);
        shim_write_cstr(diag_file, ",");
        shim_write_dec(diag_file, (unsigned long long)(ok ? 1 : 0));
        shim_write_cstr(diag_file, ",");
        shim_write_dec(diag_file, (unsigned long long)count);
        // One-shot view of the producer gate and ring-buffer state at the moment
        // the injected tokens are issued.  This is read-only and runs on the
        // watcher thread, never while the sampled thread is suspended.
        constexpr ULONG_PTR kRing = 0x147bc7f80ULL;
        shim_write_cstr(diag_file, ",gate,");
        shim_write_hex(diag_file, *reinterpret_cast<ULONG_PTR*>(kProducerGate));
        static const ULONG_PTR ring_offsets[] = {0x11e330ULL, 0x11e334ULL, 0x11e338ULL};
        for (ULONG_PTR offset : ring_offsets) {
            shim_write_cstr(diag_file, ",r");
            shim_write_hex(diag_file, offset);
            shim_write_cstr(diag_file, ",");
            shim_write_hex(diag_file, *reinterpret_cast<ULONG_PTR*>(kRing + offset));
        }
        shim_write_cstr(diag_file, "\n");
    }
    if (diag_mutex) {
        ReleaseMutex(diag_mutex);
    }
}

static LONG g_worker_sem_released = 0;

__attribute__((no_instrument_function))
DWORD WINAPI worker_sem_watch_thread(LPVOID) {
    for (;;) {
        HANDLE sem = *reinterpret_cast<HANDLE*>(kWorkerSemaphoreGlobal);
        // The producer gate is opened at 0x142a8b6df, after the semaphore is
        // published.  Releasing tokens before that only lets the consumer drain
        // them with no work behind them, so wait for the initialized gate too.
        unsigned char gate = *reinterpret_cast<volatile unsigned char*>(kProducerGate);
        if (sem && sem != INVALID_HANDLE_VALUE && (gate & 1)) {
            if (InterlockedCompareExchange(&g_worker_sem_released, 1, 0) == 0) {
                BOOL ok = ReleaseSemaphore(sem, kWorkerSemaphoreBurst, nullptr);
                if (ok) {
                    diag_sem_release(reinterpret_cast<ULONG_PTR>(sem), ok,
                                     kWorkerSemaphoreBurst);
                    return 0;
                }
                InterlockedExchange(&g_worker_sem_released, 0);
            }
        }
        Sleep(5);
    }
}

static LONG g_arg6_calls = 0;

__attribute__((no_instrument_function))
void diag_arg6(void* a2, void* a3, void* a4, void* a5, void* a6, void** out, void* ret) {
    LONG n = InterlockedIncrement(&g_arg6_calls);
    if (n > 20 && (n % 100) != 0) return;
    static HANDLE file = INVALID_HANDLE_VALUE;
    static HANDLE mutex = nullptr;
    if (!mutex) mutex = CreateMutexA(nullptr, FALSE, "shim_trace_lock");
    if (mutex) WaitForSingleObject(mutex, INFINITE);
    if (file == INVALID_HANDLE_VALUE) {
        file = CreateFileA("trace.log", FILE_WRITE_DATA | FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, nullptr, FILE_END);
        shim_write_cstr(file, "D,arg6,");
        shim_write_dec(file, (unsigned long long)n);
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(a2));
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(a3));
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(a4));
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(a5));
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(a6));
        shim_write_cstr(file, ",");
        shim_write_hex(file, out ? reinterpret_cast<ULONG_PTR>(*out) : 0);
        shim_write_cstr(file, ",");
        shim_write_hex(file, reinterpret_cast<ULONG_PTR>(ret));
        shim_write_cstr(file, "\n");
    }
    if (mutex) ReleaseMutex(mutex);
}

using CreateDeviceFn = HRESULT(WINAPI*)(void*, uint32_t, const void*, void**);
using GetInterfaceFn = HRESULT(WINAPI*)(const void*, void**);
using GetInterface2Fn = HRESULT(WINAPI*)(const void*, const void*, void**);
using SerializeFn = HRESULT(WINAPI*)(const void*, uint32_t, void**, void**);
using SerializeVersionedFn = HRESULT(WINAPI*)(const void*, void**, void**);
using DeserializeFn = HRESULT(WINAPI*)(const void*, size_t, const void*, void**);
using ExperimentalFn = HRESULT(WINAPI*)(uint32_t, const void*, void*, uint32_t*);

template <typename Fn>
Fn native(const char* name) {
    static Fn cached = stubutil::resolve_export<Fn>("d3d12.dll", name);
    return cached;
}

HRESULT xbox_ok(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

// ---------------------------------------------------------------------------
// Dummy Xbox device objects
// ---------------------------------------------------------------------------

struct DummyObject {
    void** vtable;
    unsigned char reserved[128];
};

DummyObject g_device;
DummyObject g_subobject;
DummyObject g_shader_object;
DummyObject g_view_object;
void* g_device_vtable[0x400] = {0};
void* g_sub_vtable[0x400] = {0};
void* g_shader_vtable[0x100] = {0};
void* g_view_vtable[0x100] = {0};

HRESULT STDMETHODCALLTYPE dummy_ok(void*, void*, void*, void*, void*, void*) {
    return (HRESULT)0;
}
void STDMETHODCALLTYPE dummy_void(void*, void*, void*, void*, void*, void*) {
}

ULONG STDMETHODCALLTYPE dummy_add_ref(void*) { return 1; }
ULONG STDMETHODCALLTYPE dummy_release(void*) { return 1; }

HRESULT STDMETHODCALLTYPE dummy_query(void* self, const void*, void** out) {
    if (out) *out = self;
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE slot180(void*, void*, void*, void*, void*, void*) {
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE slot278(void*, void*, void*, void*, void*, void*) {
    return (HRESULT)0;
}

// Sub-object slot 0xa8 is GetDisplayModeList(self, format, flags, UINT* count,
// DXGI_MODE_DESC* modes).  Returning no modes makes the title's 120 Hz scan at
// 0x142481DFD fail and select the non-120 Hz presentation path.  Return one
// 1920x1048@120 mode; +8 in each 32-byte entry is the refresh numerator.
HRESULT STDMETHODCALLTYPE slotA8(void*, void*, void*, void* countPtr, void* modes, void*) {
    constexpr uint32_t kModeBytes = 32;
    uint32_t count = 1;
    if (countPtr) *static_cast<uint32_t*>(countPtr) = count;
    if (modes) {
        std::memset(modes, 0, static_cast<size_t>(count) * kModeBytes);
        uint32_t* mode = static_cast<uint32_t*>(modes);
        mode[0] = 1920;  // Width
        mode[1] = 1048;  // Height
        mode[2] = 120;   // RefreshRate.Numerator
        mode[3] = 1;     // RefreshRate.Denominator
    }
    return (HRESULT)0;
}

// Device slot 0xc8 fills a 16-byte format/resource-info structure.  The title
// asserts that field +4 is zero, so always clear the whole output.
HRESULT STDMETHODCALLTYPE device_format_info(void* out, void*, void*, void*) {
    if (out) std::memset(out, 0, 16);
    return (HRESULT)0;
}

// Descriptor heaps live in the large title-memory reservation.  The title
// writes descriptors into that region, so the COM object needs its own header
// rather than sharing the first bytes of the backing store.
DummyObject g_heap_object;
void* g_heap_vtable[0x200] = {0};

void* title_memory_base() {
    void** slot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                           0x75df188);
    return *slot;
}

void* STDMETHODCALLTYPE heap_cpu_handle(void*) { return title_memory_base(); }
void* STDMETHODCALLTYPE heap_gpu_handle(void*) {
    return static_cast<unsigned char*>(title_memory_base()) + 0x2600000;
}
uint32_t STDMETHODCALLTYPE heap_descriptor_size(void*, void*, void*, void*, void*, void*) { return 0x20; }

// Device slot 14 (byte offset 0x70) is CreateDescriptorHeap.  The title first
// calls it with a zero existing-heap argument; that output is reused as the
// heap object on later calls.
HRESULT STDMETHODCALLTYPE device_create_descriptor_heap(void*, void*, void*, void** out) {
    if (out) *out = &g_heap_object;
    return (HRESULT)0;
}

// Device slot 0x80 creates a shader from PRIV/DXBC data.  The title passes the
// output shader object as the sixth argument (stack); leaving it null creates a
// zero-filled shader-cache entry that faults on the next lookup.
HRESULT STDMETHODCALLTYPE device_create_shader(void*, uint32_t, void*, void*, void*, void** out) {
    if (out) *out = &g_shader_object;
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE device_out_r9(void*, void*, void*, void** out) {
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE device_out_rdx(void*, void** out) {
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

// The adapter-side object receives the same vtable offset but reports its
// output through r8 rather than rdx.
HRESULT STDMETHODCALLTYPE device_out_r8(void*, void*, void** out) {
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE device_out_arg5(void*, void*, void*, void*, void** out) {
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

HRESULT STDMETHODCALLTYPE device_out_arg6(void* a1, void* a2, void* a3, void* a4, void* a5, void** out) {
    diag_arg6(a1, a2, a3, a4, a5, out, __builtin_return_address(0));
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

// Xbox devices report a non-zero tile/element size here; the title multiplies
// resource dimensions by it and fails when it is zero.
uint32_t STDMETHODCALLTYPE device_tile_size(void*, void*, void*, void*, void*, void*) {
    return 0x20;
}

// Device slot 0x360 returns resource allocation info.  The title copies the
// 16-byte result and requires alignment (+8) to be non-zero and the high half
// of the size (+4) to be zero; leaving this as the generic dummy returns an
// all-zero structure, which makes the texture-creation path return null.
uint64_t STDMETHODCALLTYPE device_resource_allocation_info(void* out, void*, void*, void*, void*, void*) {
    if (out) {
        uint64_t* info = static_cast<uint64_t*>(out);
        info[0] = 0x1000;
        info[1] = 0x1000;
    }
    return 0;
}

// Sub-object slot 0x60 returns a pointer to a monotonic counter/fence value.
// The title calls 0x14010a330(rcx=*counter, rdx=fence) and waits forever when
// this slot returns null.  A fixed value matching the title's own default is
// safely ahead of any fence the boot path creates.
struct WaitValue {
    uint64_t count;
    unsigned char pad[64];
};
WaitValue g_wait_value{0x41200000};
void* STDMETHODCALLTYPE sub_wait_value(void*) {
    // The title polls this counter until (counter - fence) has bit 55 set.
    // Advance it aggressively so the wait loop completes instead of spinning.
    g_wait_value.count += 0x80000000000000ULL;
    return &g_wait_value;
}

// Device slot 0x428 creates a texture/computer object.  The title stores the
// result at [format+0x20] and later calls vtable slot 0x50 on it to build a
// resource description, so the generic dummy (which leaves the output null)
// causes a null dereference in the shader-resource path.
void STDMETHODCALLTYPE view_resource_description(void*, void* out) {
    if (out) std::memset(out, 0, 0x40);
}

HRESULT STDMETHODCALLTYPE device_create_view(void*, void*, void*, void*, void*, void*, void** out) {
    if (out) *out = &g_view_object;
    return (HRESULT)0;
}

struct VtableInitializer {
    VtableInitializer() {
        for (size_t i = 0; i < 0x400; ++i) {
            g_device_vtable[i] = reinterpret_cast<void*>(&dummy_ok);
            g_sub_vtable[i] = reinterpret_cast<void*>(&dummy_ok);
        }
        for (size_t i = 0; i < 0x200; ++i) {
            g_heap_vtable[i] = reinterpret_cast<void*>(&dummy_ok);
        }
        for (size_t i = 0; i < 0x100; ++i) {
            g_shader_vtable[i] = reinterpret_cast<void*>(&dummy_ok);
            g_view_vtable[i] = reinterpret_cast<void*>(&dummy_ok);
        }
        // Vtable calls use byte offsets, so divide each console slot offset by
        // sizeof(void*) to get the array index.
        g_device_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_device_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_device_vtable[2] = reinterpret_cast<void*>(&dummy_release);
        g_device_vtable[0x38 / 8] = reinterpret_cast<void*>(&device_out_rdx);
        g_device_vtable[0x40 / 8] = reinterpret_cast<void*>(&device_out_r9);
        g_device_vtable[0x48 / 8] = reinterpret_cast<void*>(&device_out_r9);
        g_device_vtable[0x70 / 8] = reinterpret_cast<void*>(&device_create_descriptor_heap);
        g_device_vtable[0x78 / 8] = reinterpret_cast<void*>(&device_tile_size);
        g_device_vtable[0x80 / 8] = reinterpret_cast<void*>(&device_create_shader);
        g_device_vtable[0x88 / 8] = reinterpret_cast<void*>(&device_out_rdx);
        g_device_vtable[0x120 / 8] = reinterpret_cast<void*>(&device_out_arg5);
        g_device_vtable[0xc8 / 8] = reinterpret_cast<void*>(&device_format_info);
        g_device_vtable[0xd0 / 8] = reinterpret_cast<void*>(&dummy_void);
        g_device_vtable[0x148 / 8] = reinterpret_cast<void*>(&device_out_arg5);
        g_device_vtable[0x180 / 8] = reinterpret_cast<void*>(&slot180);
        g_device_vtable[0x188 / 8] = reinterpret_cast<void*>(&dummy_void);
        g_device_vtable[0x248 / 8] = reinterpret_cast<void*>(&device_out_arg6);
        g_device_vtable[0x278 / 8] = reinterpret_cast<void*>(&slot278);
        g_device_vtable[0x360 / 8] = reinterpret_cast<void*>(&device_resource_allocation_info);
        g_device_vtable[0x428 / 8] = reinterpret_cast<void*>(&device_create_view);

        g_sub_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_sub_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_sub_vtable[2] = reinterpret_cast<void*>(&dummy_release);
        g_sub_vtable[0x38 / 8] = reinterpret_cast<void*>(&device_out_r8);
        g_sub_vtable[0x60 / 8] = reinterpret_cast<void*>(&sub_wait_value);
        g_sub_vtable[0xa8 / 8] = reinterpret_cast<void*>(&slotA8);
        // The title tests this slot as an HRESULT on sub-objects, so keep the
        // default dummy_ok here rather than returning an object pointer.
        g_sub_vtable[0x418 / 8] = reinterpret_cast<void*>(&dummy_void);
        g_sub_vtable[0x420 / 8] = reinterpret_cast<void*>(&dummy_void);

        g_heap_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_heap_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_heap_vtable[2] = reinterpret_cast<void*>(&dummy_release);
        g_heap_vtable[0x48 / 8] = reinterpret_cast<void*>(&heap_cpu_handle);
        g_heap_vtable[0x50 / 8] = reinterpret_cast<void*>(&heap_gpu_handle);
        g_heap_vtable[0x78 / 8] = reinterpret_cast<void*>(&heap_descriptor_size);

        g_shader_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_shader_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_shader_vtable[2] = reinterpret_cast<void*>(&dummy_release);

        g_view_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_view_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_view_vtable[2] = reinterpret_cast<void*>(&dummy_release);
        g_view_vtable[0x50 / 8] = reinterpret_cast<void*>(&view_resource_description);

        g_device.vtable = g_device_vtable;
        g_subobject.vtable = g_sub_vtable;
        g_heap_object.vtable = g_heap_vtable;
        g_shader_object.vtable = g_shader_vtable;
        g_view_object.vtable = g_view_vtable;
    }
} vtable_initializer;

}  // namespace

extern "C" {

HRESULT WINAPI XsD3D12CreateDevice(void* adapter, uint32_t minimumFeatureLevel, const void* riid, void** device) {
    CreateDeviceFn fn = native<CreateDeviceFn>("D3D12CreateDevice");
    return fn ? fn(adapter, minimumFeatureLevel, riid, device) : kFail;
}

HRESULT WINAPI XsD3D12GetDebugInterface(const void* riid, void** debug) {
    GetInterfaceFn fn = native<GetInterfaceFn>("D3D12GetDebugInterface");
    return fn ? fn(riid, debug) : kFail;
}

HRESULT WINAPI XsD3D12GetInterface(const void* clsid, const void* riid, void** output) {
    GetInterface2Fn fn = native<GetInterface2Fn>("D3D12GetInterface");
    return fn ? fn(clsid, riid, output) : kFail;
}

HRESULT WINAPI XsD3D12SerializeRootSignature(const void* desc, uint32_t version, void** blob, void** errorBlob) {
    SerializeFn fn = native<SerializeFn>("D3D12SerializeRootSignature");
    return fn ? fn(desc, version, blob, errorBlob) : kFail;
}

HRESULT WINAPI XsD3D12SerializeVersionedRootSignature(const void* desc, void** blob, void** errorBlob) {
    SerializeVersionedFn fn = native<SerializeVersionedFn>("D3D12SerializeVersionedRootSignature");
    return fn ? fn(desc, blob, errorBlob) : kFail;
}

HRESULT WINAPI XsD3D12CreateRootSignatureDeserializer(const void* data, size_t size, const void* riid,
                                                       void** deserializer) {
    DeserializeFn fn = native<DeserializeFn>("D3D12CreateRootSignatureDeserializer");
    return fn ? fn(data, size, riid, deserializer) : kFail;
}

HRESULT WINAPI XsD3D12CreateVersionedRootSignatureDeserializer(const void* data, size_t size, const void* riid,
                                                                void** deserializer) {
    DeserializeFn fn = native<DeserializeFn>("D3D12CreateVersionedRootSignatureDeserializer");
    return fn ? fn(data, size, riid, deserializer) : kFail;
}

HRESULT WINAPI XsD3D12EnableExperimentalFeatures(uint32_t count, const void* features, void* unknown,
                                                   uint32_t* layout) {
    ExperimentalFn fn = native<ExperimentalFn>("D3D12EnableExperimentalFeatures");
    return fn ? fn(count, features, unknown, layout) : kFail;
}

HRESULT WINAPI XsD3D12ConfigureVirtualMemoryX(void*, size_t, size_t) { return (HRESULT)0; }

// The retail EXE passes (adapter, params, riid*, device**, extra).  Earlier
// stubs treated the output as the fifth argument and left the real output null.
HRESULT WINAPI XsD3D12XboxCreateDevice(void* adapter, const void* params, const void* riid, void** device,
                                        void* extra) {
    (void)adapter;
    (void)params;
    (void)riid;
    (void)extra;
    HANDLE watcher = CreateThread(nullptr, 0, worker_sem_watch_thread, nullptr, 0, nullptr);
    if (watcher) CloseHandle(watcher);
    if (device) *device = &g_device;
    return (HRESULT)0;
}

HRESULT WINAPI XsD3D12XboxCreateRootSignature(void*, const void*, void** signature) { return xbox_ok(signature); }
HRESULT WINAPI XsD3D12XboxCreateCommandQueue(void*, const void*, void** queue) { return xbox_ok(queue); }
HRESULT WINAPI XsD3D12XboxCreateSwapChain(void*, const void*, void** swapChain) { return xbox_ok(swapChain); }
HRESULT WINAPI XsD3D12XboxGetGpuHardwareCapabilities(void*, void* capabilities) {
    if (capabilities) std::memset(capabilities, 0, 64);
    return (HRESULT)0;
}
HRESULT WINAPI XsD3D12XboxCloseAllocationObject(void*) { return (HRESULT)0; }
HRESULT WINAPI XsD3D12XboxCreatePlacedResource(void*, const void*, const void*, const void*, void** resource) {
    return xbox_ok(resource);
}
HRESULT WINAPI XsD3D12XboxCreateHeap(void*, const void*, void** heap) { return xbox_ok(heap); }
HRESULT WINAPI XsD3D12XboxGetProgramBinary(void*, const void*, size_t, void* buffer, size_t* used) {
    if (used) *used = 0;
    if (buffer) std::memset(buffer, 0, size_t(8));
    return (HRESULT)0;
}

}  // extern "C"
