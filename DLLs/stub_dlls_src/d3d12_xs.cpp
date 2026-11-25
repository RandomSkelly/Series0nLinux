// d3d12_xs.dll replacement.  Standard D3D12 entry points are forwarded to the
// native d3d12.dll supplied by Proton.  Xbox device creation is answered with a
// dummy COM object whose vtable covers the Xbox-only methods the title calls.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kFail = (HRESULT)0x80004005;

using CreateDeviceFn = HRESULT(WINAPI*)(void*, uint32_t, const void*, void**);
using GetInterfaceFn = HRESULT(WINAPI*)(const void*, void**);
using GetInterface2Fn = HRESULT(WINAPI*)(const void*, const void*, void**);
using SerializeFn = HRESULT(WINAPI*)(const void*, uint32_t, void**, void**);
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
void* g_device_vtable[0x400] = {0};
void* g_sub_vtable[0x400] = {0};

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

HRESULT STDMETHODCALLTYPE slotA8(void*, void*, void*, void*, void*, void*) {
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

HRESULT STDMETHODCALLTYPE device_out_arg6(void*, void*, void*, void*, void*, void** out) {
    if (out) *out = &g_subobject;
    return (HRESULT)0;
}

// Xbox devices report a non-zero tile/element size here; the title multiplies
// resource dimensions by it and fails when it is zero.
uint32_t STDMETHODCALLTYPE device_tile_size(void*, void*, void*, void*, void*, void*) {
    return 0x20;
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
        g_device_vtable[0x88 / 8] = reinterpret_cast<void*>(&device_out_rdx);
        g_device_vtable[0x120 / 8] = reinterpret_cast<void*>(&device_out_arg5);
        g_device_vtable[0xd0 / 8] = reinterpret_cast<void*>(&dummy_void);
        g_device_vtable[0x148 / 8] = reinterpret_cast<void*>(&device_out_arg5);
        g_device_vtable[0x180 / 8] = reinterpret_cast<void*>(&slot180);
        g_device_vtable[0x188 / 8] = reinterpret_cast<void*>(&dummy_void);
        g_device_vtable[0x248 / 8] = reinterpret_cast<void*>(&device_out_arg6);
        g_device_vtable[0x278 / 8] = reinterpret_cast<void*>(&slot278);

        g_sub_vtable[0] = reinterpret_cast<void*>(&dummy_query);
        g_sub_vtable[1] = reinterpret_cast<void*>(&dummy_add_ref);
        g_sub_vtable[2] = reinterpret_cast<void*>(&dummy_release);
        g_sub_vtable[0x38 / 8] = reinterpret_cast<void*>(&device_out_r8);
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

        g_device.vtable = g_device_vtable;
        g_subobject.vtable = g_sub_vtable;
        g_heap_object.vtable = g_heap_vtable;
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

HRESULT WINAPI XsD3D12SerializeVersionedRootSignature(const void* desc, uint32_t version, void** blob,
                                                       void** errorBlob) {
    SerializeFn fn = native<SerializeFn>("D3D12SerializeVersionedRootSignature");
    return fn ? fn(desc, version, blob, errorBlob) : kFail;
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
