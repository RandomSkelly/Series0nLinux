// XboxBvhBuilder.dll replacement.  The original Xbox/DXR helper dereferences
// D3D12Device11 interfaces that do not exist under Wine/Proton.  The title only
// needs plausible prebuild sizes and no-op acceleration-structure commands for
// offline boot, so implement the imported C++ member ABI directly.  The .def
// maps the mangled export names to these simple C symbols.
#include "common.h"

#include <cstring>

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" {

void* bvh_constructor(void* self, void*, unsigned char) { return self; }
void bvh_destructor(void*) {}
void bvh_build(void*, void*, void*, uint64_t, void*) {}
void bvh_build_batched(void*, void*, uint32_t, void*) {}
void bvh_copy(void*, void*, uint64_t, uint32_t) {}
void bvh_cpu_validate(void*, void*, void*) {}

void bvh_prebuild_info(void*, void*, void*, void* out) {
    if (!out) return;
    std::memset(out, 0, 0x40);
    static_cast<uint64_t*>(out)[0] = 0x100000;  // result data size
    static_cast<uint64_t*>(out)[1] = 0x100000;  // scratch data size
    static_cast<uint64_t*>(out)[2] = 0x100000;  // update scratch size
}

long bvh_rehydrate_batched(void*, void*, void*, const uint64_t*) { return 0; }
long bvh_rehydrate_batched_one(void*, void*, void*) { return 0; }
long bvh_rehydrate(void*, void*, void*) { return 0; }
void bvh_set_morton(void*, unsigned char) {}
long bvh_validate(void*, void*) { return 0; }

}  // extern "C"
