// Bink2Scarlett tracing proxy.  The original library is preserved as
// Bink2ScarlettOrig.dll.  Each export is a naked tail-jump: it saves the
// incoming Win64 register arguments (and the original return address), writes
// a shim trace line, restores the registers, and jumps to the real export.
// No stdio is used; tracing goes through common.h's WriteFile/mutex path.
#include "common.h"

namespace {

constexpr char kOriginalDll[] = "Bink2ScarlettOrig.dll";

// Slot layout:
//  0 rcx, 1 rdx, 2 r8, 3 r9, 4 rax, 5 caller return address,
//  6..9 xmm0..xmm3.
constexpr size_t kSavedSlots = 10;

}  // namespace

#define BINK_PROXY_STUB(fn)                                                   \
    extern "C" void fn();                                                     \
    extern "C" void* bink_target_##fn;                                        \
    extern "C" uintptr_t bink_saved_##fn[kSavedSlots];                        \
    void* bink_target_##fn = nullptr;                                         \
    uintptr_t bink_saved_##fn[kSavedSlots] = {0};                             \
    extern "C" __attribute__((no_instrument_function)) void bink_trace_##fn() { \
        if (!bink_target_##fn) {                                               \
            bink_target_##fn = stubutil::resolve_export<void*>(kOriginalDll,  \
                                                              #fn);           \
        }                                                                     \
        void* caller = reinterpret_cast<void*>(bink_saved_##fn[5]);            \
        shim_trace_line('E', reinterpret_cast<void*>(&fn), caller);            \
        shim_trace_line('X', reinterpret_cast<void*>(&fn), caller);            \
    }                                                                         \
    extern "C" __attribute__((naked, no_instrument_function)) void fn() {      \
        __asm__(                                                              \
            "movq %rcx, bink_saved_" #fn "+0(%rip)\n\t"                       \
            "movq %rdx, bink_saved_" #fn "+8(%rip)\n\t"                       \
            "movq %r8,  bink_saved_" #fn "+16(%rip)\n\t"                      \
            "movq %r9,  bink_saved_" #fn "+24(%rip)\n\t"                      \
            "movq %rax, bink_saved_" #fn "+32(%rip)\n\t"                      \
            "movq (%rsp), %rax\n\t"                                           \
            "movq %rax, bink_saved_" #fn "+40(%rip)\n\t"                      \
            "movq %xmm0, bink_saved_" #fn "+48(%rip)\n\t"                     \
            "movq %xmm1, bink_saved_" #fn "+56(%rip)\n\t"                     \
            "movq %xmm2, bink_saved_" #fn "+64(%rip)\n\t"                     \
            "movq %xmm3, bink_saved_" #fn "+72(%rip)\n\t"                     \
            "subq $40, %rsp\n\t"                                              \
            "callq bink_trace_" #fn "\n\t"                                    \
            "addq $40, %rsp\n\t"                                              \
            "movq bink_saved_" #fn "+0(%rip), %rcx\n\t"                       \
            "movq bink_saved_" #fn "+8(%rip), %rdx\n\t"                       \
            "movq bink_saved_" #fn "+16(%rip), %r8\n\t"                       \
            "movq bink_saved_" #fn "+24(%rip), %r9\n\t"                       \
            "movq bink_saved_" #fn "+32(%rip), %rax\n\t"                      \
            "movq bink_saved_" #fn "+48(%rip), %xmm0\n\t"                     \
            "movq bink_saved_" #fn "+56(%rip), %xmm1\n\t"                     \
            "movq bink_saved_" #fn "+64(%rip), %xmm2\n\t"                     \
            "movq bink_saved_" #fn "+72(%rip), %xmm3\n\t"                     \
            "jmp *bink_target_" #fn "(%rip)\n\t"                              \
        );                                                                    \
    }

BINK_PROXY_STUB(BinkAllocateFrameBuffers)
BINK_PROXY_STUB(BinkClose)
BINK_PROXY_STUB(BinkCloseTrack)
BINK_PROXY_STUB(BinkControlBackgroundIO)
BINK_PROXY_STUB(BinkCopyToBuffer)
BINK_PROXY_STUB(BinkCopyToBufferRect)
BINK_PROXY_STUB(BinkCurrentSubtitle)
BINK_PROXY_STUB(BinkDoFrame)
BINK_PROXY_STUB(BinkDoFrameAsync)
BINK_PROXY_STUB(BinkDoFrameAsyncMulti)
BINK_PROXY_STUB(BinkDoFrameAsyncWait)
BINK_PROXY_STUB(BinkDoFramePlane)
BINK_PROXY_STUB(BinkFindXAudio2WinDevice)
BINK_PROXY_STUB(BinkFreeGlobals)
BINK_PROXY_STUB(BinkGetError)
BINK_PROXY_STUB(BinkGetFrameBuffersInfo)
BINK_PROXY_STUB(BinkGetGPUDataBuffersInfo)
BINK_PROXY_STUB(BinkGetKeyFrame)
BINK_PROXY_STUB(BinkGetPlatformInfo)
BINK_PROXY_STUB(BinkGetRealtime)
BINK_PROXY_STUB(BinkGetRects)
BINK_PROXY_STUB(BinkGetSubtitleByIndex)
BINK_PROXY_STUB(BinkGetSummary)
BINK_PROXY_STUB(BinkGetTrackData)
BINK_PROXY_STUB(BinkGetTrackID)
BINK_PROXY_STUB(BinkGetTrackMaxSize)
BINK_PROXY_STUB(BinkGetTrackType)
BINK_PROXY_STUB(BinkGoto)
BINK_PROXY_STUB(BinkLoadSubtitles)
BINK_PROXY_STUB(BinkLogoAddress)
BINK_PROXY_STUB(BinkNextFrame)
BINK_PROXY_STUB(BinkOpen)
BINK_PROXY_STUB(BinkOpenTrack)
BINK_PROXY_STUB(BinkOpenWithOptions)
BINK_PROXY_STUB(BinkOpenXAudio2)
BINK_PROXY_STUB(BinkPause)
BINK_PROXY_STUB(BinkRegisterFrameBuffers)
BINK_PROXY_STUB(BinkRegisterGPUDataBuffers)
BINK_PROXY_STUB(BinkRequestStopAsyncThread)
BINK_PROXY_STUB(BinkRequestStopAsyncThreadsMulti)
BINK_PROXY_STUB(BinkService)
BINK_PROXY_STUB(BinkSetError)
BINK_PROXY_STUB(BinkSetFileOffset)
BINK_PROXY_STUB(BinkSetFrameRate)
BINK_PROXY_STUB(BinkSetIO)
BINK_PROXY_STUB(BinkSetIOSize)
BINK_PROXY_STUB(BinkSetMemory)
BINK_PROXY_STUB(BinkSetOSFileCallbacks)
BINK_PROXY_STUB(BinkSetPan)
BINK_PROXY_STUB(BinkSetSimulate)
BINK_PROXY_STUB(BinkSetSoundOnOff)
BINK_PROXY_STUB(BinkSetSoundSystem)
BINK_PROXY_STUB(BinkSetSoundSystem2)
BINK_PROXY_STUB(BinkSetSoundTrack)
BINK_PROXY_STUB(BinkSetSpeakerVolumes)
BINK_PROXY_STUB(BinkSetVideoOnOff)
BINK_PROXY_STUB(BinkSetVolume)
BINK_PROXY_STUB(BinkSetWillLoop)
BINK_PROXY_STUB(BinkShouldSkip)
BINK_PROXY_STUB(BinkStartAsyncThread)
BINK_PROXY_STUB(BinkUtilCPUs)
BINK_PROXY_STUB(BinkUtilFree)
BINK_PROXY_STUB(BinkUtilMalloc)
BINK_PROXY_STUB(BinkUtilMutexCreate)
BINK_PROXY_STUB(BinkUtilMutexDestroy)
BINK_PROXY_STUB(BinkUtilMutexLock)
BINK_PROXY_STUB(BinkUtilMutexLockTimeOut)
BINK_PROXY_STUB(BinkUtilMutexUnlock)
BINK_PROXY_STUB(BinkUtilSoundGlobalLock)
BINK_PROXY_STUB(BinkUtilSoundGlobalUnlock)
BINK_PROXY_STUB(BinkWait)
BINK_PROXY_STUB(BinkWaitStopAsyncThread)
BINK_PROXY_STUB(BinkWaitStopAsyncThreadsMulti)
BINK_PROXY_STUB(RADTimerRead)
