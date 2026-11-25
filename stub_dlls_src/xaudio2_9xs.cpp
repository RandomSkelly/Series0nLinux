// xaudio2_9xs.dll replacement.  The standard XAudio2 factory is forwarded to
// Wine's xaudio2_9.dll; Xbox-only XMA/hardware audio calls answer S_OK and hand
// back non-null dummy objects.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kFail = (HRESULT)0x80004005;
using CreateFn = HRESULT(WINAPI*)(void**, uint32_t, uint32_t);
using CreateVersionedFn = HRESULT(WINAPI*)(void**, uint32_t, uint32_t, uint32_t);

template <typename Fn>
Fn native(const char* name) {
    static Fn cached = stubutil::resolve_export<Fn>("xaudio2_9.dll", name);
    return cached;
}

HRESULT xbox_ok(void** out) {
    if (out) *out = stubutil::dummy_pointer();
    return (HRESULT)0;
}

}  // namespace

extern "C" {

HRESULT WINAPI XsXAudio2Create(void** audio, uint32_t flags, uint32_t processor) {
    CreateFn fn = native<CreateFn>("XAudio2Create");
    return fn ? fn(audio, flags, processor) : kFail;
}

HRESULT WINAPI XsXAudio2CreateWithVersionInfo(void** audio, uint32_t flags, uint32_t processor, uint32_t version) {
    CreateVersionedFn fn = native<CreateVersionedFn>("XAudio2CreateWithVersionInfo");
    return fn ? fn(audio, flags, processor, version) : XsXAudio2Create(audio, flags, processor);
}

HRESULT WINAPI XsXAudio2CreateProcessor(void** processor) { return xbox_ok(processor); }
HRESULT WINAPI XsXAudio2CreateXmaDecoder(void** decoder) { return xbox_ok(decoder); }
HRESULT WINAPI XsXAudio2CreateXmaDecoderAsync(void*, void** decoder) { return xbox_ok(decoder); }
HRESULT WINAPI XsXAudio2GetXmaDecoderRegistration(void*, void** registration) { return xbox_ok(registration); }
HRESULT WINAPI XsXAudio2SetXmaDecoderRegistration(void*, void*) { return (HRESULT)0; }
HRESULT WINAPI XsXAudio2CreateVolumeMeter(void** meter) { return xbox_ok(meter); }
HRESULT WINAPI XsXAudio2CreateReverb(void** reverb) { return xbox_ok(reverb); }
HRESULT WINAPI XsXAudio2CreateApm(void** apm) { return xbox_ok(apm); }
HRESULT WINAPI XsXAudio2GetDeviceDetails(void*, void* details) {
    if (details) std::memset(details, 0, 64);
    return (HRESULT)0;
}
HRESULT WINAPI XsXAudio2Initialize(void) { return (HRESULT)0; }
HRESULT WINAPI XsXAudio2Uninitialize(void) { return (HRESULT)0; }

}  // extern "C"
