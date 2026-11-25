// GameInput.dll replacement.  Wine's built-in GameInputCreate is a stub that
// returns E_NOTIMPL, which the title traps on.  This module supplies the three
// public exports with the original ordinals and returns offline COM objects:
// input registration succeeds, no devices/callbacks are ever produced, and
// readings contain zeroed state.
#include "common.h"

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

namespace {

constexpr HRESULT kOk = (HRESULT)0;
constexpr HRESULT kNoInterface = (HRESULT)0x80004002;  // E_NOINTERFACE
constexpr HRESULT kPointer = (HRESULT)0x80004003;      // E_POINTER
constexpr HRESULT kClassNotAvail = (HRESULT)0x80040111;

struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

constexpr Guid kIidUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr Guid kIidInput = {0x11BE2A7E, 0x4254, 0x445A, {0x9C, 0x09, 0xFF, 0xC4, 0x0F, 0x00, 0x69, 0x18}};
constexpr Guid kIidReading = {0x2156947A, 0xE1FA, 0x4DE0, {0xA3, 0x0B, 0xD8, 0x12, 0x93, 0x1D, 0xBD, 0x8D}};
constexpr Guid kIidDevice = {0x31DD86FB, 0x4C1B, 0x408A, {0x86, 0x8F, 0x43, 0x9B, 0x3C, 0xD4, 0x71, 0x25}};
constexpr Guid kIidDispatcher = {0x415EED2E, 0x98CB, 0x42C2, {0x8F, 0x28, 0xB9, 0x46, 0x01, 0x07, 0x4E, 0x31}};

bool same_iid(const Guid* a, const Guid& b) {
    return a && std::memcmp(a, &b, sizeof(Guid)) == 0;
}

struct ComObject {
    void** vtable;
};

// IUnknown implementations shared by every dummy object.
HRESULT STDMETHODCALLTYPE query_interface(void* self, const Guid* riid, void** out) {
    if (!out) return kPointer;
    *out = nullptr;
    if (same_iid(riid, kIidUnknown)) {
        *out = self;
        return kOk;
    }
    return kNoInterface;
}

ULONG STDMETHODCALLTYPE add_ref(void*) { return 1; }
ULONG STDMETHODCALLTYPE release(void*) { return 1; }

HRESULT STDMETHODCALLTYPE no_result(void*) { return kOk; }
void STDMETHODCALLTYPE no_void(void*) {}
uint64_t STDMETHODCALLTYPE no_timestamp(void*) { return 0; }
uint32_t STDMETHODCALLTYPE no_count(void*) { return 0; }
bool STDMETHODCALLTYPE no_bool(void*) { return false; }

// ---------------------------------------------------------------------------
// IGameInput (v3/v4 layout: GetTemporalReading is slot 7)
// ---------------------------------------------------------------------------

ComObject g_input;
ComObject g_device;
ComObject g_reading;
ComObject g_dispatcher;

uint64_t STDMETHODCALLTYPE input_timestamp(void*) {
    return GetTickCount64() * 1000;
}

HRESULT STDMETHODCALLTYPE input_get_reading(void*, uint32_t, void*, void** reading) {
    if (reading) *reading = &g_reading;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_get_next(void*, void*, uint32_t, void*, void** reading) {
    if (reading) *reading = &g_reading;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_get_temporal(void*, uint64_t, void*, void** reading) {
    if (reading) *reading = &g_reading;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_register_reading(void*, void*, uint32_t, float, void*, void*, uint64_t* token) {
    if (token) *token = 1;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_register_device(void*, void*, uint32_t, uint32_t, uint32_t, void*, void*, uint64_t* token) {
    if (token) *token = 2;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_register_simple(void*, void*, void*, void*, uint64_t* token) {
    if (token) *token = 3;
    return kOk;
}

void STDMETHODCALLTYPE input_stop_callback(void*, uint64_t) {}

bool STDMETHODCALLTYPE input_unregister_callback(void*, uint64_t, uint64_t) { return true; }

HRESULT STDMETHODCALLTYPE input_create_dispatcher(void*, void** dispatcher) {
    if (dispatcher) *dispatcher = &g_dispatcher;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_create_device(void*, uint32_t, void** device) {
    if (device) *device = &g_device;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_find_device(void*, void* value, void** device) {
    (void)value;
    if (device) *device = &g_device;
    return kOk;
}

HRESULT STDMETHODCALLTYPE input_find_device_string(void*, const wchar_t*, void** device) {
    if (device) *device = &g_device;
    return kOk;
}

void STDMETHODCALLTYPE input_set_focus(void*, uint32_t) {}

// ---------------------------------------------------------------------------
// IGameInputReading: all state queries report zeroed input.
// ---------------------------------------------------------------------------

uint32_t STDMETHODCALLTYPE reading_kind(void*) { return 0; }
uint64_t STDMETHODCALLTYPE reading_sequence(void*, uint32_t) { return 0; }

void STDMETHODCALLTYPE reading_get_device(void*, void** device) {
    if (device) *device = &g_device;
}

bool STDMETHODCALLTYPE reading_get_raw_report(void*, void** report) {
    if (report) *report = nullptr;
    return false;
}

uint32_t STDMETHODCALLTYPE reading_get_state(void*, uint32_t count, void* state) {
    if (state) std::memset(state, 0, static_cast<size_t>(count) * 64);
    return 0;
}

bool STDMETHODCALLTYPE reading_get_bool_state(void*, void* state) {
    if (state) std::memset(state, 0, 256);
    return false;
}

// ---------------------------------------------------------------------------
// IGameInputDevice: report an offline, zero-capability device.
// ---------------------------------------------------------------------------

void* STDMETHODCALLTYPE device_info(void*) {
    static unsigned char info[512] = {0};
    return info;
}

uint32_t STDMETHODCALLTYPE device_status(void*) { return 1; }  // Connected

void STDMETHODCALLTYPE device_battery(void*, void* state) {
    if (state) std::memset(state, 0, 64);
}

bool STDMETHODCALLTYPE device_bool(void*, uint32_t) { return false; }

void STDMETHODCALLTYPE device_set_float(void*, uint32_t, float) {}
void STDMETHODCALLTYPE device_set_bool(void*, bool) {}

HRESULT STDMETHODCALLTYPE device_create_effect(void*, uint32_t, void*, void** effect) {
    if (effect) *effect = nullptr;
    return kOk;
}

void STDMETHODCALLTYPE device_set_haptic(void*, uint32_t, void*) {}
void STDMETHODCALLTYPE device_set_rumble(void*, void*) {}

HRESULT STDMETHODCALLTYPE device_raw(void*, uint32_t, void** report) {
    if (report) *report = nullptr;
    return kOk;
}

HRESULT STDMETHODCALLTYPE device_raw_with_report(void*, void* report, void** response) {
    (void)report;
    if (response) *response = nullptr;
    return kOk;
}

HRESULT STDMETHODCALLTYPE device_ioctl(void*, uint32_t, size_t, void*, size_t, void*, size_t*) {
    return kOk;
}

bool STDMETHODCALLTYPE device_exclusive(void*, uint64_t) { return false; }

// ---------------------------------------------------------------------------
// IGameInputDispatcher
// ---------------------------------------------------------------------------

bool STDMETHODCALLTYPE dispatcher_dispatch(void*, uint64_t) { return false; }

HRESULT STDMETHODCALLTYPE dispatcher_wait_handle(void*, void** handle) {
    if (handle) *handle = nullptr;
    return kOk;
}

// Fill every unused slot with a harmless no-op so a future method call cannot
// jump through a NULL vtable entry.
void* input_vtable_raw[64] = {0};
void* reading_vtable_raw[64] = {0};
void* device_vtable_raw[64] = {0};
void* dispatcher_vtable_raw[64] = {0};

template <size_t N>
void fill_vtable(void* (&table)[N], const Guid& iid, void** fallback) {
    for (size_t i = 0; i < N; ++i) table[i] = reinterpret_cast<void*>(fallback[i % 3]);
    table[0] = reinterpret_cast<void*>(&query_interface);
    table[1] = reinterpret_cast<void*>(&add_ref);
    table[2] = reinterpret_cast<void*>(&release);
    (void)iid;
}

struct Initializer {
    Initializer() {
        void* generic[3] = {
            reinterpret_cast<void*>(&no_result),
            reinterpret_cast<void*>(&no_void),
            reinterpret_cast<void*>(&no_timestamp),
        };
        fill_vtable(input_vtable_raw, kIidInput, generic);
        fill_vtable(reading_vtable_raw, kIidReading, generic);
        fill_vtable(device_vtable_raw, kIidDevice, generic);
        fill_vtable(dispatcher_vtable_raw, kIidDispatcher, generic);

        // IGameInput
        input_vtable_raw[3] = reinterpret_cast<void*>(&input_timestamp);
        input_vtable_raw[4] = reinterpret_cast<void*>(&input_get_reading);
        input_vtable_raw[5] = reinterpret_cast<void*>(&input_get_next);
        input_vtable_raw[6] = reinterpret_cast<void*>(&input_get_reading);
        input_vtable_raw[7] = reinterpret_cast<void*>(&input_get_temporal);
        input_vtable_raw[8] = reinterpret_cast<void*>(&input_register_reading);
        input_vtable_raw[9] = reinterpret_cast<void*>(&input_register_device);
        input_vtable_raw[10] = reinterpret_cast<void*>(&input_register_simple);
        input_vtable_raw[11] = reinterpret_cast<void*>(&input_register_simple);
        input_vtable_raw[12] = reinterpret_cast<void*>(&input_stop_callback);
        input_vtable_raw[13] = reinterpret_cast<void*>(&input_unregister_callback);
        input_vtable_raw[14] = reinterpret_cast<void*>(&input_create_dispatcher);
        input_vtable_raw[15] = reinterpret_cast<void*>(&input_create_device);
        input_vtable_raw[16] = reinterpret_cast<void*>(&input_find_device);
        input_vtable_raw[17] = reinterpret_cast<void*>(&input_find_device);
        input_vtable_raw[18] = reinterpret_cast<void*>(&input_find_device);
        input_vtable_raw[19] = reinterpret_cast<void*>(&input_find_device_string);
        input_vtable_raw[20] = reinterpret_cast<void*>(&no_result);
        input_vtable_raw[21] = reinterpret_cast<void*>(&input_set_focus);

        // IGameInputReading
        reading_vtable_raw[3] = reinterpret_cast<void*>(&reading_kind);
        reading_vtable_raw[4] = reinterpret_cast<void*>(&reading_sequence);
        reading_vtable_raw[5] = reinterpret_cast<void*>(&no_timestamp);
        reading_vtable_raw[6] = reinterpret_cast<void*>(&reading_get_device);
        reading_vtable_raw[7] = reinterpret_cast<void*>(&reading_get_raw_report);
        reading_vtable_raw[8] = reinterpret_cast<void*>(&no_count);
        reading_vtable_raw[9] = reinterpret_cast<void*>(&reading_get_state);
        reading_vtable_raw[10] = reinterpret_cast<void*>(&no_count);
        reading_vtable_raw[11] = reinterpret_cast<void*>(&reading_get_state);
        reading_vtable_raw[12] = reinterpret_cast<void*>(&no_count);
        reading_vtable_raw[13] = reinterpret_cast<void*>(&reading_get_state);
        reading_vtable_raw[14] = reinterpret_cast<void*>(&no_count);
        reading_vtable_raw[15] = reinterpret_cast<void*>(&reading_get_state);
        reading_vtable_raw[16] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[17] = reinterpret_cast<void*>(&no_count);
        reading_vtable_raw[18] = reinterpret_cast<void*>(&reading_get_state);
        reading_vtable_raw[19] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[20] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[21] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[22] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[23] = reinterpret_cast<void*>(&reading_get_bool_state);
        reading_vtable_raw[24] = reinterpret_cast<void*>(&reading_get_bool_state);

        // IGameInputDevice
        device_vtable_raw[3] = reinterpret_cast<void*>(&device_info);
        device_vtable_raw[4] = reinterpret_cast<void*>(&device_status);
        device_vtable_raw[5] = reinterpret_cast<void*>(&device_battery);
        device_vtable_raw[6] = reinterpret_cast<void*>(&device_create_effect);
        device_vtable_raw[7] = reinterpret_cast<void*>(&device_bool);
        device_vtable_raw[8] = reinterpret_cast<void*>(&device_set_float);
        device_vtable_raw[9] = reinterpret_cast<void*>(&device_set_haptic);
        device_vtable_raw[10] = reinterpret_cast<void*>(&device_set_rumble);
        device_vtable_raw[11] = reinterpret_cast<void*>(&device_set_bool);
        device_vtable_raw[12] = reinterpret_cast<void*>(&no_void);
        device_vtable_raw[13] = reinterpret_cast<void*>(&no_void);
        device_vtable_raw[14] = reinterpret_cast<void*>(&device_raw);
        device_vtable_raw[15] = reinterpret_cast<void*>(&device_raw);
        device_vtable_raw[16] = reinterpret_cast<void*>(&device_raw_with_report);
        device_vtable_raw[17] = reinterpret_cast<void*>(&device_raw_with_report);
        device_vtable_raw[18] = reinterpret_cast<void*>(&device_ioctl);
        device_vtable_raw[19] = reinterpret_cast<void*>(&device_exclusive);
        device_vtable_raw[20] = reinterpret_cast<void*>(&no_void);

        // IGameInputDispatcher
        dispatcher_vtable_raw[3] = reinterpret_cast<void*>(&dispatcher_dispatch);
        dispatcher_vtable_raw[4] = reinterpret_cast<void*>(&dispatcher_wait_handle);

        g_input.vtable = input_vtable_raw;
        g_reading.vtable = reading_vtable_raw;
        g_device.vtable = device_vtable_raw;
        g_dispatcher.vtable = dispatcher_vtable_raw;
    }
} initializer;

}  // namespace

extern "C" {

HRESULT WINAPI GameInputCreate(void** gameInput) {
    if (!gameInput) return kPointer;
    *gameInput = &g_input;
    return kOk;
}

HRESULT WINAPI DllCanUnloadNow(void) { return 1; }  // S_FALSE

HRESULT WINAPI DllGetClassObject(const Guid*, const Guid&, void** object) {
    if (object) *object = nullptr;
    return kClassNotAvail;
}

}  // extern "C"
