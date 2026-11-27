# Progress log

## Current milestone

- The game loads all stubs, creates a window titled `Americas`, and stays alive under a 20 s bounded headless Proton run.
- It still never leaves the persistent white screen; no main menu has been observed.
- Current working EXE SHA-256: `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`
- Current `d3d12_xs.dll` SHA-256: `23632f88e17261be36eabfd78de5f3623f85f766d96ef72ad0993c3c2e7a7d5e` (rebuilt after reverting the run186 bit-55 experiment)
- Current `stub_dlls_src/d3d12_xs.cpp` SHA-256: `40bb8eed6caf22debecd9aeb3afee47f49d95bdf865cb04aaa58a9c330736bea` (identical to the pre-run183 baseline source)
- Protected original SHA-256: `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`
- EXE size: `120181760` bytes (unchanged).

## Breakthroughs

1. Repaired the corrupted `[rcx+0x40]` descriptor-table inner pointer at `0x1413FDB53` by allocating a real `0x15000`-byte, 8-byte-aligned block array and installing it before re-entering the initializer.
2. Skipped the null callback trap at `0x142215C31` (`74 02` -> `eb 0e`), removing the final intentional `mov dword ptr ds:[0],0` fault.
3. With those patches, run151 survived 120 s with no unhandled page fault or unhandled exception; only the external `timeout` ended it.
4. Identified and removed a D3D12 fence-polling spin: `sub_wait_value` (`d3d12_xs.dll` slot `0x60`) returned a fixed counter, so the title loop at `0x144FB0BD0` could never satisfy `(counter - fence) & 0x80000000000000`. It now advances the counter by that stride on each call.
5. Patched unsupported AMD Zen wait instructions in the fence loop: `monitorx` at `0x144FB0BD7` and `mwaitx` at `0x144FB0C00` are NOPed, eliminating the `Unhandled illegal instruction` from run164.

## Experiments that did not help (reverted)

- Skipping `_Xlength_error` at `0x1455E364A` (run155) exposed invalid descriptor-table pointers and a repeating `ntdll+0x3969C` SEH/stack-overflow loop.
- Forcing all descriptor categories through the repair path and growing the array to `0x2D180` (run156) still faulted at `0x1413FCB0C`.
- Guarding the two bad descriptor-word writes (`0x1413FCB0C`, `0x1413FCBD6`) with canonical/upper-32 pointer checks (runs157/159/160) either kept faulting or produced a new fault at `0x1413FCBD6`.
- NOPing both descriptor-word writes (run161) moved the fault to `0x14540ABF5` (`rcx=0x20`).
- Redirecting `mov rcx,[rdi+0x18]` to `mov rcx,rdi` at `0x140018796` (run162) hung before window creation; reverted.
- Returning `-1` from `sub_wait_value` (run163) took the fatal-error path and hit an intentional `int3` at `0x142474606`; reverted to an advancing counter.

## Current hypothesis

The boot path is alive, but rendering never starts because the Xbox-only D3D12/DXGI device is still a dummy vtable. The trace tail is dominated by `d3d12_xs` device methods (`dummy_ok`, `device_out_arg6`, `device_out_r9`, `device_format_info`) called from title code around `0x1417F6D3B` and `0x141F75FB4`. `XsDXGIXboxCreateSwapChain` also returns a dummy object, so no real Present path exists. A faithful Series X -> PC D3D12 swapchain/resource shim is required for menus to appear.

## Rollback points

- EXE before all descriptor experiments: `/tmp/opencode/exe_before_run156.exe`
- EXE before wait-counter changes: `/tmp/opencode/exe_before_run163.exe`
- EXE before `monitorx`/`mwaitx` NOPs: `/tmp/opencode/exe_before_run165.exe`
- DLLs before instrumentation: `/tmp/opencode/dll_before_trace/`
- Pre-run155 EXE (run151 baseline): `/tmp/opencode/exe_before_run155.exe`

## Runs 170-179: white-screen stall localization

### Run170 - disable Xalia
- Hypothesis: Xalia/Mono UI layer was creating the `SetWaitableTimerEx` wait seen at the end of run169 and blocking the title.
- Test: `PROTON_USE_XALIA=0`, `WINEDEBUG='+timestamp,+pid,+tid,+seh'`, 15 s bound.
- Result: exit `124`; Proton log `64` lines, `0` Mono/Xalia lines, `0` `SetWaitableTimer` lines. Shim trace stayed at `4616` lines with the same tail as run168. Screenshot still white. Xalia ruled out.

### Run171 - window geometry/pixels
- Test: `WINEDEBUG='-all'`, captured all visible windows at 11 s.
- Result: game window is `Americas`, `1920x1048+1920+32`. Histogram: `1,947,742` pure-white pixels, `46,453` blue (`50,150,250`) title-bar pixels, no game imagery. `trace.log` again `4616` lines. Confirms the stall is before any rendered frame or logo.

### Run172 - syscall tracing
- Hypothesis: attach `strace` to the game PID to identify the blocked syscall.
- Result: `strace: attach: ptrace(PTRACE_SEIZE, 274352): Operation not permitted`. No syscall data; approach ruled out in this sandbox.

### Run173 - Wine sync/timer/thread-name tracing
- Test: `WINEDEBUG='+timestamp,+pid,+tid,+sync,+timer,+threadname,+loaddll'`.
- Result: exit `124`; `70138` Proton lines, `4616` trace lines. Last activity is two threads polling semaphores:
  - worker tid `017c` waits on `0x5b8`/`0x5bc`/`0x5c0`;
  - main tid `012c` waits on `0x5bc` with zero timeout;
  - tid `018c` waits on `0x83c`.
- A `NtReleaseSemaphore(0x5bc)` is consumed once by main (`WaitForSingleObject -> 0`), after which main releases `0x830` and `0x5b8` and re-enters the same wait. No further release arrives. This is a phase barrier, not a crash.

### Run174 - reverted fence-derivation experiment
- Hypothesis: the single `sub_wait_value` call could not satisfy the title's bit-55 fence test when the fence was large.
- Change: changed `sub_wait_value(void*)` to `sub_wait_value(void*, uint64_t fence, void*, uint32_t)` and set `count = fence + 0x80000000000000`.
- Result: exit `124`; `4616` trace lines with the exact same tail as runs 168/170/171/173. Proton log showed the semaphore spin starting before the shim call, so the shim was not the blocker.
- Decision: reverted to the advancing-counter implementation and rebuilt all stubs. Current `d3d12_xs.dll` hash is `566f54b4...`. Three identical trace tails with no forward movement satisfied the three-repeat rule.

### Run175 - handle trace channel
- Test: `WINEDEBUG='+timestamp,+pid,+tid,+sync,+handle'`.
- Result: Wine's `handle` channel produced `0` lines in this build; only the already-known semaphore/mutex waits were visible. Ruled out.

### Run176 - int3 at the wait helper
- Diagnostic: temporarily replaced the first byte of `0x145441BE0` with `int3`; EXE restored immediately afterward.
- Result: two breakpoint exceptions. Backtrace: `0x145441BE0` (zero-timeout `WaitForSingleObject`) <- `0x1423CEF43` <- `0x142241177` <- `0x14081B253` <- `0x1453EC04F` <- game thread trampoline.
- `0x1423CEED0` is a phase entry: it creates a worker, then loops with `Sleep(10)` until the semaphore at `[0x14A210CE8]` is released. This exactly matches the run173 spin.

### Run177 - int3 at worker semaphore call
- Diagnostic: temporary `int3` at `0x1423CF0D0` (start of `ReleaseSemaphore` setup in worker tail `0x1423CEFE0`).
- Result: reached by worker tid `017c`; backtrace `0x1423CF0D0` <- thunk `0x142215ECA` <- kernel thread start. Confirms the worker does execute the release path.

### Run178 - invalid probe byte
- Diagnostic attempt at `0x1423CF0D8` patched the last immediate byte of `mov edx,1`, not an instruction boundary. No breakpoint hit; run survived to timeout. EXE restored. No conclusion drawn.

### Run179 - int3 after semaphore handle load
- Diagnostic: temporary `int3` at `0x1423CF0D9`.
- Result: worker tid `017c` reaches the call with `rcx=0x5B4`, `rdx=1` — a valid `ReleaseSemaphore(handle, 1, NULL)`. EXE restored.
- Combined with run173, the first barrier is released and consumed; a later phase re-enters the same wait and never gets its matching release. The remaining blocker is upstream graphics/worker initialization, not Win32 semaphore delivery.

## Next pivot
- Graphics init is still the leading suspect: the phase worker waits on semaphore `0x5B8` (`[struct+0x28]`) while pumping messages, and only after that does it complete the phase. The dummy Xbox swapchain/device may never provide the completion event that main waits for.
- Media/Bink is deprioritized: no `BinkOpen`/`BinkWait` calls occur after startup, and no `.bik` files exist in the leak tree.
- GDK async remains secondary; no GDK callback stall has appeared in the trace since the allocator repair.

## Runs 180-182: forced phase-barrier bypass

### Run180 - always-signaled wait helper
- Hypothesis: the phase barrier at `0x1423CEED0` was the only blocker; forcing the shared zero-timeout wait helper at `0x145441BE0` to return `TRUE` should let both the main loop and its worker continue.
- Change (EXE only, restored afterward): `0x145441BE0` -> `mov al,1; ret`.
- Result: exit `3`; trace advanced to `2458` lines with new callers `0x142481E44`, `0x141E86853`, and repeated `0x1417F6D3B/4B` graphics calls. Crashed on an intentional `int3` at `0x140CC2834` after a null lookup. This proved the barrier is not the graphics blocker itself; bypassing it opens a previously unseen graphics path.

### Run181 - NOP the null-lookup trap
- Hypothesis: the `int3` at `0x140CC2834` was merely a debug assertion with a graceful null return.
- Change: NOPed only that trap byte while keeping the run180 barrier bypass.
- Result: exit `5`; trace `2468` lines; unhandled read of address `0x13` at `0x141EDB1FA`. Reverted the trap NOP.

### Run182 - force validation success
- Hypothesis: the failed validation at `0x14034BED6` caused the null object; skipping the branch would let graphics object creation continue.
- Change: NOPed the six-byte `je` at `0x14034BED6`, restored the `0x140CC2834` trap.
- Result: exit `5`; trace `3469` lines; unhandled read at `0x14048D7F2` (`r11=0`) through `0x14501F46B` -> `0x14034BFA2` -> `0x140CC27E9`. The forced path builds an invalid object; reverted.

### Decision
- All EXE bypass experiments were reverted to the run179 baseline (`d2ef3b3e...`). The next fix must be in C++ stubs. The leading C++ hypothesis is that returning the *same* shared dummy object for hundreds of device/view/shader outputs makes the title's graphics caches overwrite one entry and repeat work forever.

## Run183: rotating dummy-object pools (reverted)

- Hypothesis: hundreds of shader/view/sub-object creations reuse one shared dummy pointer, so the title's graphics cache overwrites a single entry and never converges.
- Change: `d3d12_xs.cpp` handed out distinct objects from 256-entry rotating pools for sub-object, shader, and view outputs.
- Result: exit `124`; trace grew from `4616` to `9105` lines, almost entirely because the new helper functions are instrumented. The real graphics calls only rose from `493` to `567` (`device_out_arg6` from `0x1417F6D3B`) and the final wait caller remained `0x144FB0AEF`. Screenshot still white (`mean=0.98862`). No fault, but no forward milestone.
- Conclusion: object identity is not the barrier. Reverted to the pre-run183 source (`d3d12_xs.dll` `566f54b4...`).

### New hypothesis
- The final `sub_wait_value` result decides whether boot enters the semaphore stall. The current counter advances by exactly `1<<55` on each call, which *toggles* bit 55. If the critical call lands on an even increment, `(counter - fence)` fails the `bt ...,0x37` test at `0x144FB0B30` and the title falls into the phase wait. Make bit 55 permanently set instead of toggling it.

## Runs 184-185: phase-worker object inspection

### Run184 - breakpoint at the worker release path
- Diagnostic: temporary `int3` at `0x1423CF0E9`; EXE restored immediately afterward.
- Result: exit `3`; `rax=1`, `rcx=0x1468BA6A0`. The worker reaches the release path with a live phase object, so the object itself is not null. No forward movement; no permanent change.

### Run185 - breakpoint after handle setup
- Diagnostic: temporary `int3` at `0x1423CF0EC`; EXE restored immediately afterward.
- Result: exit `3`; `rax=0x146202310`, `rcx=0x1468BA6A0`. Following `[rcx+0x28]` gives `0x145912BE0`, which jumps to `0x141C29030`; that routine calls `0x144EC8B60` and `0x145BBD7D0` then sets the byte at `0x1478AEEE8` to `1`. This confirms the worker does advance its internal completion state after releasing the semaphore. No permanent change.

## Run186: permanent bit-55 counter (reverted)

- Change: `sub_wait_value` kept bit 55 permanently set and advanced only the low counter bits, instead of adding `1<<55` per call.
- Result: exit `124`; Proton log `64` lines; `trace.log` stayed at `4616` lines with the exact baseline tail `188332668,380,d3d12_xs,E/X,264b,144fb0aef`. Screenshot mean `0.98862`, stddev `0.0735244`, `73` colors. No unhandled fault or exception.
- Conclusion: counter polarity is ruled out. The title never reaches a different graphics path because of bit 55; reverted to the pre-run183 source and rebuilt all stubs. Source hash `40bb8eed...`; rebuilt DLL hash `23632f88...`.
- The same `4616`-line tail has now repeated across runs 168/170/171/173/174/186 with no forward movement, satisfying the three-repeat rule. Pivot away from generic D3D12 dummy-object/counter churn to asset decompression/media, then GDK async.

## Runs 188-197: title-volume placeholders, tail repeat, and clean rollback

### Run187 - `+file` path diagnosis (recap)
- `WINEDEBUG=+file` showed Xbox absolute paths such as `platform:\...` being converted to `G:\platform:\...` and failing.
- This identified a concrete asset-availability blocker before the later white-screen tail.

### Run188 - manual placeholder title volumes
- Hypothesis: missing Xbox-volume asset files made the shader setup take a fatal path.
- Test: created placeholder files for the failing `platform:\` / `common:\` paths outside C++.
- Result: new `int3` at `0x14247B5D9` after reading `platform:\shaders\v2\sga_xbsx_final\im.fxc`; `rax=ffffffff`. Forward movement was achieved.
- Disassembly showed the shader container check expects magic `0x01066853` (`Sh\x06\x01`).

### Run189 - shader magic in placeholders
- Change: wrote `{53 68 06 01 00 00 00 00}` to placeholder `.fxc`/`.fxdb` files.
- Result: exit `124`; `3865` trace lines, no unhandled exception. White screen remained, but the earlier `int3` was gone.

### Run190 - permanent C++ placeholder fix
- Change: `xgameruntime.cpp::DllMain` now creates the whole placeholder tree itself:
  - `common:`, `common:\data`, `common:\data\version.txt` (`1.0.2.1`),
  - `platform:`, `platform:\data`, `platform:\data\effects`, `platform:\data\settings`,
  - `platform:\shaders\v2\fxdb`, `platform:\shaders\v2\sga_xbsx_final`,
  - shader placeholders with the `0x01066853` magic, `.zmt`/`.d3d12PipelineCache` zero-byte placeholders.
- Result: exit `124`; Proton log `18` lines, trace `3866` lines, no unhandled fault. This is the accepted fix and is retained.

### Run191 - `+file` verification
- Test: `WINEDEBUG=+file` after the C++ placeholder fix.
- Result: no remaining title-volume read misses except directory-collision noise and `NtQueryAttributesFile L"\??\G:\audio:\Core.Metadata.rpf" not found (c000003a)`. The unresolved `audio:` query is still open.
- Trace settled into a repeated D3D12 tail dominated by `0x14161D0A3`, `0x140344145`, `0x140344188`, `0x144ECC821`, `0x142479A8D`, `0x142481B31`, and `0x142481BD5`.

### Run192 - clean desktop capture
- Test: clean desktop screenshot after ensuring no stale game processes/windows.
- Result: game window `1920x1048+1920+32`; crop average `#FBFDFF`, `73` colors. Still no rendered frame or Rockstar logo.
- Conclusion: placeholder fix advances boot but does not reach presentation.

### Run193 - allocation-info value (reverted in run197)
- Hypothesis: device slot `0xc8` was returning a zeroed 16-byte allocation-info structure, causing repeated format/allocation queries.
- Change: `device_format_info` returned `{0x1000, 0x1000}` instead of zeros.
- Result: exit `124`; trace dropped from `3866` to `3778` lines. Tail became dominated by `device_out_r9` at `0x14161D0A3`; no new milestone, no fault. Screenshot remained white.
- Note: an initial suspicion about `device_out_r9` arity was checked against the source/symbol and ruled out; it already had four parameters.

### Run194 - dummy swapchain vtable (reverted in run196)
- Hypothesis: the Xbox swapchain was a zeroed pointer with a null vtable, so any method call would fault before the shim could observe it.
- Change: `dxgi_xs.cpp` returned a complete dummy swapchain object/vtable from both Xbox swapchain creators.
- Result: exit `124`; trace unchanged at `3778` lines; `grep -c dxgi_xs` was `0`. No swapchain method was called, so the change neither helped nor hurt.

### Run195 - synchronization diagnostic
- Test: `WINEDEBUG=+sync` on the unchanged run194 state.
- Result: Proton log `22919` lines, trace `3778` lines. A thread polls `NtWaitForSingleObject handle 0x5b4` with zero timeout forever; no matching `ReleaseSemaphore` arrives for that second phase. Confirms a phase barrier/idle state, not a crash.
- A pending EXE breakpoint at `0x1423CF0E9` was deliberately **not** run because run184 already covered the same release path. The EXE was restored to the run179/183 baseline (`d2ef3b3e...`) before run196.

### Runs 196-197 - three-repeat rollback
- Hypothesis: runs 193/194/195 had the same normalized D3D12 tail with no forward movement, so the most recent C++ changes should be reverted one at a time.
- Run196: reverted only the dummy swapchain vtable. Result: exit `124`, trace still `3778`, same normalized tail. Swapchain vtable ruled out.
- Run197: reverted only the allocation-info change. Result: exit `124`, trace back to `3866`, normalized tail hash equal to run191. D3D12 allocation-info value ruled out.
- Final state: both graphics experiments reverted; `d3d12_xs.cpp` source hash `40bb8eed6caf22debecd9aeb3afee47f49d95bdf865cb04aaa58a9c330736bea`; rebuilt `d3d12_xs.dll` hash `036642cfcf6f0f261d6ec210f9b9d5f67e8d53875a8673c8ce2f00ff3c23e0ce`. EXE baseline restored and verified at `120181760` bytes, SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`.
- Next pivot: media/decompression. Add a tracing/forwarding `Bink2Scarlett.dll` proxy beside the EXE while preserving the original as `Bink2ScarlettOrig.dll`; determine whether any Bink API is reached after the placeholder breakthrough.

## Runs 198-199: Bink proxy and GDK async ruled out

### Run198 - Bink tracing proxy
- Change: added a Bink proxy that preserved the original as `Bink2ScarlettOrig.dll` and wrapped all `74` exports with naked register-saving tail-jumps into the original, tracing through `WriteFile`.
- Result: exit `124`; Proton log `64` lines, trace `3866` lines. `grep -c Bink2Scarlett trace198.log` was `0`. No fault.
- The trace tail was unchanged from run197, so Bink playback is not active at the white-screen point.

### Run199 - `+loaddll` verification
- Test: `WINEDEBUG=+loaddll` with the proxy installed.
- Result: `Loaded L"G:\\Bink2Scarlett.dll" at 00006FFFFD2E0000: native`; still `0` Bink trace lines and `3866` total trace lines. The proxy loads correctly but no Bink export is called.
- Conclusion: media/Bink is ruled out as the current blocker. GDK async was also reviewed: `XUser.cpp`, `XStore.cpp`, and `xgameruntime.cpp` complete all async operations inline with `S_OK`, and trace198 contains no calls to `XUser.dll`, `XStore.dll`, or any `XAsync*` entry point. GDK async is not active before the stall.

### Revert
- Removed `Bink2Scarlett.dll` from `BUILD` in `generate_stubs.py`, restored `Bink2Scarlett.dll` from `Bink2ScarlettOrig.dll`, and rebuilt all stubs.
- `Bink2Scarlett.dll` and `Bink2ScarlettOrig.dll` both hash to `9eaf045d004cd7d6a8382aaff0c6ea23a29fec4ef8bfacd726b54f5bc26fafd0`, size `255488`.

### New concrete graphics hypothesis (run200)
- Disassembly of `0x142481ca0` shows the title calls device sub-object slot `0x38` twice to obtain adapter outputs, then slot `0xa8` twice to obtain display-mode lists. It scans 32-byte entries for `DWORD PTR [entry+8] == 0x78` (120 Hz refresh numerator) and sets a global flag accordingly.
- `d3d12_xs.cpp::slotA8` currently writes nothing, so the title always sees zero modes and selects the non-120 Hz path. The next single fix is to make slot `0xa8` return one 120 Hz display mode.

## Run 200: accepted 120 Hz display-mode shim

- Change: `d3d12_xs.cpp::slotA8` now writes one 32-byte `1920x1048@120` display mode instead of leaving the buffer untouched. This is the only retained C++/DLL change after run197.
- Result: exit `124`; no fault; white screen unchanged. The mode query itself is now answered, but boot still stalls at the second-phase barrier.
- Accepted as a correct placeholder and kept. Source hash `dc77afec8d25cbb48069c22ad7057e8b1930aa8908c51b0cf1c6bf8800280c3e`; `d3d12_xs.dll` hash `58b221df37b7e0707d2cd69408f97a86c812527553d2a1c546d0d26e1ad23bf4`.

## Runs 201-207: second-phase barrier bypass diagnostics (EXE patches only)

All patches in this section were diagnostic only. `game_scarlett_master.exe.bak` was never touched; the working EXE was restored to the baseline SHA-256 `d2ef3b3e...` and size `120181760` after every run.

### Run 201 - wait-helper identification
- `+sync` had shown a thread polling `NtWaitForSingleObject handle 0x5b4` with zero timeout forever while observed releases used `handle 0x5e0`.
- Disassembly identified `0x145441be0` as the wait helper: it calls `WaitForSingleObject(handle,0)` and returns `(result == 0)`.
- The active caller was `0x1423CEF43`, with `rcx=0x5b4`. This confirms a busy-spin phase barrier, not a deadlock on a different handle.

### Run 202 - bypassing the zero-timeout wait
- Diagnostic patch: `0x1423CEF3E` bypassed the wait-helper call.
- Result: new `int3` at `0x140CC2834`; backtrace `0x140CC2614`, `0x140CC250B`, `0x141EDB114`.
- Trap analysis: `0x142478140` rejects an object when `word[0]==0` or `word[2]==0`. `0x141EDB0D1` sets flag byte `+0xd` to `3`, so flags were not the failing condition. The failure came from zero width/height-style header fields.

### Run 203 - width/height forcing without wait bypass
- Diagnostic patches at `0x141EDB033` and `0x141EDB040` forced nonzero width/height values from `[0x1499fecf8]`/`[0x1499fecfc]`.
- Result: exit `124`; Proton log `64` lines, trace `3870` lines. No fault because the zero-timeout wait still gated the path. This ruled out width/height as an independent early fault.

### Run 204 - width/height plus wait bypass
- Same width/height patches plus the run202 wait bypass.
- Result: exit `5`; AV at `0x14048D7F2`; Proton log `417` lines, trace `3856` lines.
- Faulting instruction: `movzwl (%r11,%rax,2),%eax`, with `r11=[0x1475d59a8]` and index from `[0x1475d59b0] & 0xffff`. The table pointer/index object was null/invalid.
- Backtrace included `0x14501F46B`, `0x14034BFA2`, `0x140CC27E9`, `0x140CC2614`, `0x140CC250B`, `0x141EDB114`.
- Furthest milestone: past the wait barrier, into format/descriptor object construction.

### Run 205 - forced count
- Added a forced count `0x10` at `0x1419F4F3F` (`[0x14a943938]`, offset `0x180` in the `0x1e8` config struct copied at `0x1419F4DF0`).
- Result: exit `5`, log `416`, trace `3860`. Inspection confirmed the same `0x14048D7F2` AV and backtrace as run204.
- Conclusion: the count value is not the missing condition; the null table object is.

### Run 206 - zero-writer NOP
- Added a NOP of the zero writer at `0x142477F31` (`48 c7 05 6c da 15 05 00 00 00 00` → `90` × 11).
- First patch attempt aborted on a byte mismatch (`...6cda156c...` vs actual `...6cda1505...`); the EXE was unchanged. The corrected patch was applied and run.
- Result: exit `5`, log `416`, trace `3859`. Same `0x14048D7F2` AV. Zeroing that global is not the cause.

### Run 207 - bypassing the format-object routine
- Added `xor eax,eax; ret` (`31 c0 c3`) over the `0x14048D790` prologue at file offset `0x48cb90`, in addition to the run206 patches.
- Result: exit `5`; log `419`, trace `3857`; new AV at `0x140344172`.
- Faulting instruction: `mov (%rdx),%rax` with `rdx=0`, loaded from `[0x148759d00]`. Backtrace: `0x140344172`, `0x1451FBDB5`, `0x140164DEE`, `0x14034BFF6`, `0x140CC27E9`, `0x140CC2614`, `0x140CC250B`, `0x141EDB114`.
- Furthest milestone: past the format-object AV, but into a null D3D-device vtable dereference.

## Post-run207 table/IAT analysis

- `[0x148759d00]` has a single writer: `0x142478621: mov QWORD PTR [rip+0x62e16d8],rax`, where `rax` comes from `[0x1475df048]`.
- `[0x1475df048]` is the output pointer passed to `D3D12XboxCreateDevice` at `0x1424785b7` via IAT slot `0x145f3fd80`. The current `XsD3D12XboxCreateDevice` writes `*device = &g_device` and returns `0`.
- Trace search found zero hits for `XsD3D12XboxCreateDevice` (RVA `0x1f6e`) in runs 191, 204 and 207. The device-creation path at `0x142478440` is therefore not being reached, or exits before the IAT call.
- PE import-table parse identified IAT `0x146793c58` as `RegisterClassExA` in `ext-ms-win-rtcore-ntuser-window-ansi-l1-1-0.dll`. At `0x1424784d5` the title calls it and tests `ax`; if the returned ATOM is zero, it jumps to `0x1424787a7` and returns failure **before** `D3D12XboxCreateDevice`.
- `0x1424784d5` is a strong new candidate for the missing device: a zero `RegisterClassExA` return skips window creation, device creation, and the write to `[0x148759d00]`. The next diagnostic should confirm the returned ATOM with `+win`/`+class` tracing before any C++ change.

### Retained state after run207
- EXE restored and verified: SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`.
- Backup unchanged: SHA-256 `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
- No C++/DLL changes after accepted run200. Stub hashes remain: `d3d12_xs.dll` `58b221df...`, `dxgi_xs.dll` `e236dc...`; `common.h` still uses `WriteFile` plus named mutex `shim_trace_lock`.

## Runs 208-209: window path and device-call confirmation

### Run 208 - `+win/+class` diagnostic
- `WINEDEBUG='+timestamp,+pid,+tid,+seh,+win,+class'`: exit `124`; Proton log `1351` lines, trace `3870` lines.
- `sgaWindowClass` registered as atom `c03b`, wndproc `0x14247a660`; `CreateWindowExA` created hwnd `0x10072` titled `"Americas"` at `1920x1080`.
- No `XsD3D12XboxCreateDevice` (RVA `0x1f6e`) trace hit appeared before the stall.
- This rejected the earlier hypothesis that `RegisterClassExA` was failing and skipping the device-creation path.

### Run 209 - int3 at the device-creation call
- Diagnostic EXE patch only: `0x1424785b7` (file offset `0x24779b7`) changed from `e8c477ac03` to `cccccccccc`, restoring immediately after the run.
- Result: exit `3`; Proton log `350` lines, trace `365` lines; `EXCEPTION_BREAKPOINT` at `0x1424785b7`.
- Backtrace: `0x1424785b7`, `0x1419F4F54`, `0x14580437E`, `0x1443A4911`, `0x14224070B`, `0x1423CF00A`, `0x142215ECA`, `kernel32+0x11649`, `ntdll+0x10E0F`.
- `rdx=0x6f9efae0` (device parameter block) and `r9=0x1475df048` (output pointer) matched the expected call signature.
- Conclusion: the graphics-init function at `0x142478440` does reach the `D3D12XboxCreateDevice` IAT call. The "path not reached" hypothesis is rejected. The missing RVA `0x1f6e` trace in run208 means the export was not invoked in that stalled timeline, or the instrumented export was not reached before the stall.
- PE diff confirmed the call site, thunk `0x145f3fd80`, and IAT slot `0x146793c18` are identical to the protected original. No pre-existing patch redirects that import.
- EXE restored and verified after run209: SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`.

## Run 210 hypothesis: seed the device globals from the shim

- Run207's second AV came from `mov (%rdx),%rax` with `rdx=0` loaded from `[0x148759d00]`.
- The only game writer for that global is the copy loop after `D3D12XboxCreateDevice` (`0x1424785d4`-`0x142478621`), sourced from `[0x1475df048]`.
- New targeted C++ change: during `d3d12_xs.dll` static initialization, write the address of `g_device` into all eleven `[0x1475deff8]`-`[0x1475df048]` slots and `[0x148759d00]` that the title's post-device copy loop would populate.
- Rationale: if the stalled timeline reaches device consumers without invoking the import thunk, the shim can still present a non-null dummy device. If the thunk does run, it writes the same pointer, so the change is idempotent.

### Run 210 result and rollback
- Rebuilt successfully; source hash `0fac1beb...`, `d3d12_xs.dll` hash `bccb3805...`.
- Result: exit `124`; Proton log `64` lines, trace `3870` matches/`3871` raw lines. No fault.
- Normalized trace tail was structurally identical to runs 195/198/199/200/203/208: `d3d12_xs dummy_ok` calls from callers `0x142481E56`, `0x142481E68`, `0x14247ABDE`, `0x14247AC02`, `0x1424824C7`, `0x14248260E`. Only the instrumented RVAs shifted by the added code.
- This is the fourth-plus repeat with no forward movement, so the three-repeat rule fired. The device-global seeding change was reverted and all stubs rebuilt.
- Restored source hash `dc77afec8d25cbb48069c22ad7057e8b1930aa8908c51b0cf1c6bf8800280c3e`; rebuilt `d3d12_xs.dll` hash `c2c266b75316fad2bb61377a0bf91094b86191a265e78441fb19fa6850c24398` (PE timestamp differs from the earlier build; source is byte-identical to the accepted baseline).
- EXE and backup remained at the verified baseline hashes and size `120181760`.

## Pivot after run210: audio metadata placeholder

- Graphics dummy-object/counter churn, Bink/media playback, and GDK async have all been ruled out or hit the repeat cap. The next concrete unresolved file miss from the earlier `+file` run191 was `NtQueryAttributesFile L"\\??\\G:\\audio:\\Core.Metadata.rpf" not found (c000003a)`.
- The accepted xgameruntime placeholder scheme already creates literal colon-named directories such as `G:\platform:\...`. The untested counterpart is `G:\audio:\Core.Metadata.rpf`.
- New targeted C++ change: add `audio:` to the placeholder directory list and an empty `audio:\Core.Metadata.rpf` placeholder file to `create_placeholders()`.

### Run 211 result: audio metadata file miss cleared, new RPF-loader breakpoint

- Built successfully; source hash `33d83a34747141643a0d96afae478a912c18e23e1eac883a7e65ee990be626c2`, `xgameruntime.dll` hash `766e7c1791950593a4437ba15c4eaf8d6301b9c7870b052882909264abb12524`. `d3d12_xs.cpp` source remained at the accepted baseline hash `dc77afec...` (the DLL hash changes with PE timestamps).
- Result: exit `3`; Proton log `41581` lines (`+file` enabled), trace `317` raw lines.
- New fault: `EXCEPTION_BREAKPOINT` at `0x141DF7BEA`, backtrace `0x141DF7BEA`, `0x141DF7B15`, `0x141DF7C00` (via inlined `0x141DF7B90`), `0x142236A88`, `0x1422368A7`, `0x142240EAB`, `0x14081B253`, `0x1453EC04F`, `0x1453EBF3A`, `0x140661F0`, `0x141D109C9`, `0x145A91C0D`, `0x1456A1E5A`.
- This is forward movement: the title now finds `G:\audio:\Core.Metadata.rpf`, opens it, reads 16 bytes, and reaches the audio-metadata loader instead of failing `NtQueryAttributesFile` with `c000003a`.
- Disassembly showed the trap is not a file-header check. `0x141DF7B90` calls `0x140FC20D0` to load `audio:\Core.Metadata.rpf` into global `0x147453500`, then calls `0x142236AB0`; if the global is still null after the load, `0x141DF7BEA` executes `int3`.
- `0x140FC21C0` requires the first DWORD of the RPF image to be `0x52504639` (`9FPR`); the empty placeholder fails that check, so the loader returns null and the global stays null.
- The workspace contains valid RPF files with the required magic, e.g. `appdata0.rpf` (8880 bytes) and `pack/story000_bvh4/story000_bvh4.rpf` (5632 bytes).
- EXE and backup remained at verified baseline hashes and size `120181760`.

### Next fix: valid RPF bytes for the audio metadata placeholder

- Hypothesis: replacing the empty `audio:\Core.Metadata.rpf` with the bytes of a small real RPF archive will let `0x140FC20D0` populate global `0x147453500`, clearing the `0x141DF7BEA` trap.
- Targeted change: during `create_placeholders()`, copy the existing root `appdata0.rpf` into `audio:\Core.Metadata.rpf` instead of creating a zero-length file. No EXE patch is needed.

### Run 212 result: valid RPF accepted, breakpoint cleared

- Built successfully; source hash `5478767004d464c30128c493776df7b31b5f715594b20412c494037118786f5e`, `xgameruntime.dll` hash `65b8b187c1ee79ee6d9eb9dc6b4c9239b6523fc4650989774f628d2aa2b206e2`.
- Result: exit `124`; Proton log `206884` lines (`+file`), trace `3870` raw lines / `3871` normalized matches.
- `audio:\Core.Metadata.rpf` was created at 8880 bytes with header `39 46 50 52 60 03 00 00` (`9FPR`), matching `appdata0.rpf`.
- No `EXCEPTION_BREAKPOINT` at `0x141DF7BEA`; no fault at all. The normalized trace tail is structurally identical to runs 195/198/199/200/203/208/210/212: the same `d3d12_xs dummy_ok` caller set ending in `0x14247AC02`, `0x1424824C7`, `0x14248260E`.
- Diffing all `G:` `NtCreateFile` names against run191 showed only two new paths: `\\??\\G:\\audio:` and `\\??\\G:\\audio:\\Core.Metadata.rpf`. No later asset opens were unlocked.
- The valid RPF placeholder is accepted and kept: it removes a real trap and returns the game to the known stall without regression.
- EXE and backup remained at baseline hashes and size `120181760`.

## Next pivot: worker semaphore starvation

- The persistent stall is a thread-pool completion wait: thread `012c` polls `NtWaitForSingleObject handle 0x5b4` with zero timeout forever, while worker threads wait on other semaphores and release `0x5e0`.
- Import parsing found `CreateSemaphoreW` at IAT `0x146793EE8` and `CreateSemaphoreExW` at `0x146793AC8`. The job-system initializer at `0x1455E887A`/`0x1455E8892` creates two semaphores with `xor edx,edx` (initial count 0), stores them at `[obj+0x118]` and `[obj+0x120]`, and starts worker `0x1455E8B00`.
- Worker `0x1455E8B00` waits forever on `[obj+0x118]`, processes one work item, then calls `ReleaseSemaphore([obj+0x120], 1, 0)` and loops. The main thread's polled handle is consistent with `[obj+0x120]`.
- Hypothesis: the worker is starved on the zero-count "work available" semaphore, so it never releases the completion semaphore. A diagnostic EXE patch changing only the two initial-count arguments from 0 to 1 will test whether allowing one immediate work iteration clears the stall.
- Patch plan: file offsets `0x55E7C72` and `0x55E7C83`, each `33 d2` (`xor edx,edx`) -> `b2 01` (`mov dl,1`). Diagnostic only; restore the EXE immediately after the run.

### Run 213 result: worker initial-count starvation rejected

- The first invocation omitted Proton's `run` verb and exited `1` before launching the game; no game process or `trace.log` was created. The corrected invocation used `proton run game_scarlett_master.exe` with `PROTON_LOG=1`, `PROTON_USE_XALIA=0`, `WINEDEBUG='+timestamp,+pid,+tid,+seh,+file'`, and the 15 s bound.
- Patched EXE hash during the run: `7c5e90c0757fd00e21ccd770303b99b69dd0c8a55250a1085943833e71e0c08b`. Only the two planned bytes changed at file offsets `0x55E7C72` and `0x55E7C83`.
- Result: exit `124`; Proton log `206164` lines, trace `3868` raw lines / `3870` normalized matches.
- No game fault: no `EXCEPTION_*`, unhandled exception, or backtrace. The only `code=6ba` RPC dispatch entries match run212 (2 lines each) and are unrelated startup noise.
- The last 60 normalized trace entries hash to `ef1d18744b5e1618b62eaf2b5068e63d1e3b4b272b7e62564c465f024943199a`, identical to run212, with the same caller set ending in `0x14247AC02`, `0x1424824C7`, `0x14248260E`.
- The `G:` `NtCreateFile` path set is identical to run212: 62 paths, zero additions or removals.
- Conclusion: the zero initial semaphore count is not the blocker; the worker is not merely missing its first permit. The next diagnostic must target the producer that posts work to `[obj+0x118]`, or pivot away from this job system if no producer is active.
- EXE restored from `/tmp/opencode/exe_before_run213.bin` and verified: SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`. Backup unchanged at `e95020b2fa4a721db3df59fdecf75621730ac559d8835252f6c3b658d5a08993`.
- No C++/DLL changes were made for run213: `xgameruntime.cpp` remained `5478767004d464c30128c493776df7b31b5f715594b20412c494037118786f5e`, `d3d12_xs.cpp` remained `dc77afec8d25cbb48069c22ad7057e8b1930aa8908c51b0cf1c6bf8800280c3e`, and the rebuilt DLLs remained at their run212 hashes.

## Pivot after run213: abandon the CRT semaphore job system

- A focused disassembly trace of `[obj+0x118]` showed its only `ReleaseSemaphore` producers are CRT exception / invalid-parameter / purecall / crash-dump dispatch paths (`0x1455E904F`, `0x1455E9349`, `0x1455E945D`). There is no normal game-boot producer for that object, so the run213 initial-count patch targeted the wrong subsystem. The worker-wait bypass at `0x1455E8B18` was also rejected as meaningless: it would spin completion releases without a work item.
- No files were modified by that analysis. Return to the phase-barrier/graphics tail: `0x1423CEED0` phase entry, `0x145441BE0` zero-timeout wait helper, `0x1423CEF43` active caller, `0x142478440` graphics init, and `0x1424785B7` device-creation call site.

### Run 214 hypothesis: longer phase-loop sleep

- In baseline runs the main phase loop polls the semaphore with `WaitForSingleObject(handle,0)` and then calls `Sleep(10)` after each failed poll. The `D3D12XboxCreateDevice` export is absent from the baseline trace, but a temporary `int3` at the wait helper (run201) and the forced-success bypass (run180) both allowed the worker to reach graphics initialization. This suggests the phase-loop timing/CPU handoff may be preventing the worker from reaching device creation.
- Diagnostic: change only the `Sleep` delay immediate at VA `0x1423CEF48` (file offset `0x23CE348`) from `0x0a` (10 ms) to `0x64` (100 ms), giving the worker a ten-times longer idle window per iteration. Single-byte EXE patch; restore immediately after the bounded run.



### Run 214 result: longer phase-loop sleep rejected
- Diagnostic patch: VA `0x1423CEF48` / file offset `0x23CE348`, single byte `0a` -> `64` (10 ms -> 100 ms Sleep in the phase poll loop). Patched hash `cd975f27e3c72eb6e323f6380935af34aa593d24b939f9c896a2638b3253b6b9`.
- First invocation failed before launch because `STEAM_COMPAT_CLIENT_INSTALL_PATH` was unset; the corrected invocation used `proton run game_scarlett_master.exe` with `PROTON_LOG=1`, `PROTON_USE_XALIA=0`, `SteamGameId=14721459835430240256`, `WINEDEBUG='+timestamp,+pid,+tid,+seh,+file'`, and the 15 s bound.
- Result: exit `124`; Proton log `206884` lines, trace `155319` raw bytes / `3871` normalized matches. No fault.
- Normalized last-60 trace hash was `ef1d18744b5e1618b62eaf2b5068e63d1e3b4b272b7e62564c465f024943199a`, identical to run212/213; d3d12_xs RVA histogram and the 62 unique `G:\` path set were also identical. Zero `0x1f6e` hits.
- Conclusion: worker scheduling delay is not the blocker. EXE restored to baseline hash `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.

### Run 215 diagnostic: graphics-tail worker not reached
- Disassembly located worker tail `0x1423CF5D0` (graphics/completion helper called from the phase worker at `0x1423CF00A`) and patched its first byte to `int3` at file offset `0x23CE9D0`. Patched hash `d242b9b15e585d3d11851e575c187d6634169873f4e982aa0a1ba81b31d2590a`.
- Result: exit `124`; no `EXCEPTION_BREAKPOINT`, no mention of `0x1423CF5D0` anywhere in the Proton log or trace. Trace tail hash and RVA histogram again identical to run212.
- Conclusion: this particular worker-tail helper is not executing in the stalled timeline, so it is not the completion producer to patch. EXE restored to baseline; backup unchanged.

### Pivot after run215: shader/resource callback loop
- The stable trace tail is produced by a registered callback at `0x142481E90` that iterates a 0x60-entry resource/shader work array at `[obj+0x10]`, calling device vtable slots `0x270`, `0x278`, `0x280`, `0x100`, and `0x80` through the dummy device. The only unresolved file churn is repeated opens of the 8-byte placeholder `platform:\shaders\v2\sga_xbsx_final\im.fxc`.
- The callback is registered from `0x145CABA60` via `0x141AD0EF0` with hash `0xEE295129`. Next hypothesis: the callback's per-frame work loop never completes because every shader operation returns the generic `dummy_ok`; making the callback a controlled no-op will show whether the boot can proceed to presentation without that loop.

### Run 216 hypothesis: no-op the shader/resource callback
- Plan: single EXE patch at VA `0x142481FAB` (file offset `0x24813AB`), replacing the 9-byte `cmp eax,0x60; je 0x142482627` sequence with a 5-byte `jmp` to the function epilogue `0x142482636` plus 4 NOPs. Diagnostic only; restore immediately after the bounded run.

### Run 216 result: callback no-op did not advance boot
- Patched hash `2b496e114cae13c9e26b985395c547fbfd85d05f73edac605beaf740edcaf879`; EXE restored afterward to baseline `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; Proton log `204920` lines, trace `155319` raw bytes / `3872` normalized matches. No `EXCEPTION_*`, unhandled fault, or new `G:\` paths; `0x1f6e` hits remained zero.
- The normalized last-60 hash changed from `ef1d18744b5e1618b62eaf2b5068e63d1e3b4b272b7e62564c465f024943199a` to `ee17d1672050124859089e6936ff41f18aaaa8e7a3206960d4f3128ef7979a89`, but only because two callback-originated calls disappeared. There was no forward movement: no device creation, no Present, no new asset opens, and the same white-screen stall.
- Callback-originated shim calls dropped from 10 to 8: only caller `0x14248244E` vanished; callers `0x1424821E3`, `0x1424824C7`, and `0x14248260E` remained. Since the patched branch is the only textual gate before the loop body, this anomaly needs confirmation before further callback surgery.

### Run 217 hypothesis: confirm the patched callback gate executes
- Plan: temporary `int3` at VA `0x142481FAB` / file offset `0x24813AB` (single byte `83` -> `cc`). If the patch site is live, this must produce `EXCEPTION_BREAKPOINT` at that address; if not, the remaining callback trace calls are coming from a different entry path or the patch site is not the gate. Restore immediately after the bounded run.

### Run 217 result: patch site is live; trace.log is contaminated across runs
- Patched hash `40a139c251d755d0971991bedd612e026885e0bad0ddacdd78296c10d8415fd7`; EXE restored to baseline `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `3`; Proton log line 203661: `EXCEPTION_BREAKPOINT` at `0x142481FAB`, backtrace `0x142481FAB`, `0x142215ECA`, `kernel32+0x11649`, `ntdll+0x10E0F`. The patch site is definitely executed.
- Critical test-hygiene discovery: `trace217.log` contains two distinct timestamp clusters, `194935*` (about 223 s before the run) and `195158*`/`195159*` (the actual run). Earlier trace files show the same pattern: each run inherits a small tail of the prior run instead of starting with a clean file. All cross-run tail hashes and callback caller counts from runs 212-216 are therefore suspect.
- Cause: the bounded run command did not delete `trace.log` before launch, and the shim opens it with `OPEN_ALWAYS`/append. From now on every run must start with `rm -f trace.log` immediately before `proton run`, and the copied log must be checked for a single contiguous timestamp cluster.
- The breakpoint backtrace also shows this callback is invoked directly from thread entry `0x142215ECA`, not through the phase worker. No conclusion about the callback no-op is valid until it is retested with a clean trace.

### Run 218 plan: clean baseline trace
- No EXE or C++ changes. Delete `trace.log` before launch, run the full 15 s baseline, and verify the trace has exactly one timestamp cluster. This will re-establish the authoritative tail hash, d3d12 RVA histogram, and callback caller set before the next targeted patch.

### Run 218 result: clean authoritative baseline
- Result: exit `124`; Proton log `206884` lines; trace `155319` raw bytes / `3870` lines / `3871` normalized matches. No fault and zero `0x1f6e` hits.
- Trace timestamps form one contiguous cluster (`195258`-`195260`), confirming the cleanup procedure works.
- Authoritative tail hash: `ef1d18744b5e1618b62eaf2b5068e63d1e3b4b272b7e62564c465f024943199a`.
- Authoritative d3d12 histogram: `2acd:1114`, `27d1:912`, `29a7:870`, `286e:184`, plus the same smaller counts as earlier accepted runs.
- Authoritative callback caller set: `0x14248244E:2`, `0x1424821E3:2`, `0x14248260E:4`, `0x1424824C7:2` (10 calls total).
- EXE and backup remained at their verified baseline hashes and size `120181760`.

### Run 219 plan: retest callback no-op with clean trace
- Reapply the run216 single 9-byte EXE patch at VA `0x142481FAB` / file offset `0x24813AB`: `83 f8 60 0f 84 73 06 00 00` -> `e9 86 06 00 00 90 90 90 90`, jumping to epilogue `0x142482636`.
- Delete `trace.log` before launch. Verify a single timestamp cluster, then compare callback caller counts and the tail hash against run218. Restore the EXE immediately afterward.

### Run 219 result: callback no-op rejected on clean trace
- Patched hash `2b496e114cae13c9e26b985395c547fbfd85d05f73edac605beaf740edcaf879`; EXE restored to baseline `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; Proton log `204920` lines; trace has one timestamp cluster (`195298`-`195299`), `3842` normalized matches, no fault, no `0x1f6e` hits.
- All 10 callback-originated shim calls disappeared (caller count `{}`), and the d3d12 histogram dropped exactly in the callback's slots (`2acd 1114->1098`, `261d 8->4`, `26d6 4->2`, `27a3 6->4`, `29e9 4->2`, `2a8d 4->2`). `im.fxc` mentions dropped `32->24`.
- The tail hash changed to `31025f0912241e31a0a11f690ed5569798d15b0087a4e726ea4429eb6dc033ea`, but there was no forward movement: still no device creation, no Present, no new asset opens, and the same white-screen stall. The callback is not the blocker.
- The earlier run216 anomaly was entirely explained by stale trace lines; the patch itself works correctly.

### Run 220 plan: clean confirmation of the device-creation call site
- Reapply the temporary `int3` at VA `0x1424785B7` / file offset `0x24779B7` (5-byte call site -> `cc cc cc cc cc`), with `rm -f trace.log` before launch.
- If `EXCEPTION_BREAKPOINT` appears at that address with a single-timestamp-cluster trace, the graphics-init path does call `D3D12XboxCreateDevice` even though the baseline trace has zero `0x1f6e` hits. If no exception appears, the run209 conclusion is invalid.

### Run 220 result: device-creation call site is definitely reached
- Patched hash `ab0fff4ec51e70494cfd915440695c2a1683f7840a53e74b2f411f99d48fdc0c`; EXE restored to baseline `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `3`; Proton log `45600` lines with two `EXCEPTION_BREAKPOINT` reports at `0x1424785B7`, the same backtrace as run209: `0x1424785B7`, `0x1419F4F54`, `0x14580437E`, `0x1443A4911`, `0x14224070B`, `0x1423CF00A`, `0x142215ECA`, kernel32, ntdll.
- Trace has one timestamp cluster (`195371`-`195372`) and only `368` normalized entries; the only d3d12_xs RVA is `0x16B5` (DllMain), with zero `0x1F6E` hits. The call site is reached before the known stall, so the absence of `0x1F6E` in clean baseline traces means either the IAT resolves elsewhere or the instrumented export is not being invoked.

### Run 221 plan: patch the stub export itself
- Temporarily change the first byte of `XsD3D12XboxCreateDevice` in `d3d12_xs.dll` (export RVA `0x1F6E`) to `int3`, snapshot the DLL, run a clean baseline with `rm -f trace.log`, then restore the DLL and verify hashes.
- If this crashes at `d3d12_xs+0x1F6E`, the export is called and the profiler trace is unreliable for this function. If it does not crash, the IAT call resolves to a different implementation and the shim is bypassed entirely.

### Run 221 result: the stub export is definitely called
- Patched DLL hash `ad6be24e98e1b9cf3786748853cf47a6d6706558ed7562cef65a6140b7cf2ec7`; restored to `76212a4d8d81c5095f6b69e928c4b7a8a02b7e13c0a3ff47be463896bb497a5f`.
- Result: exit `3`; two `EXCEPTION_BREAKPOINT` reports at `d3d12_xs.dll+0x1F6E`, backtrace beginning `d3d12_xs+0x1F6E`, `game_scarlett_master+0x24785BC`, then the same phase-worker stack as run220.
- The IAT call therefore reaches our export. The missing `0x1F6E` profiler line in clean baseline traces is not a path issue; it is a tracing/entry-instrumentation issue that must be isolated before it is used as evidence again.
- EXE remained at baseline hash/size; backup unchanged.

### Run 222 plan: patch after the profiler call
- Temporarily patch `d3d12_xs.dll` at RVA `0x1F90` (file offset `0x1590`), immediately after the `__cyg_profile_func_enter` call at `0x1F86`, from `48` (`mov rax,...`) to `cc`.
- If this crashes at `d3d12_xs+0x1F90` and `trace.log` contains an `E,1F6E` line, the profiler works and only the first-byte breakpoint skipped it. If it crashes but there is still no `E,1F6E` line, the instrumentation entry is not being written despite execution. Restore the DLL immediately after the run.

### Run 222 result: profiler writes are being overwritten
- Patched DLL hash `2b8369843885cf36ea571891e239d0d4fa069f3869c1982f63f6e0cccf2e15d0`; restored to `76212a4d8d81c5095f6b69e928c4b7a8a02b7e13c0a3ff47be463896bb497a5f`.
- Result: exit `3`; `EXCEPTION_BREAKPOINT` at `d3d12_xs.dll+0x1F90`, backtrace again rooted at `game+0x24785BC`, so the function reached the point after the profiler call.
- Raw `trace222.log` contains exactly one `,E,1f6e,1424785bc` sequence, but its timestamp/module prefix was overwritten, leaving `d12_xs,E,1f6e,1424785bc`. The profiler entry is written; the trace file itself is corrupt.
- Root cause: each stub DLL keeps its own `trace_file` handle with `FILE_WRITE_DATA | FILE_APPEND_DATA`; Wine does not give separate handles a shared append pointer, so concurrent writes overwrite each other despite the named mutex. This also explains the earlier stale-tail/interleaving anomalies and the unreliable absence of `0x1F6E` in baseline traces.
- EXE remained at baseline hash/size; backup unchanged.

### Next C++ fix: force every shim write to EOF
- Change `common.h::shim_trace_line()` to call `SetFilePointer(trace_file, 0, nullptr, FILE_END)` once after acquiring `shim_trace_lock` and before writing. This keeps the required `WriteFile` + named-mutex design and removes stdio, but guarantees that writes from all module-local handles append rather than overwrite.
- Rebuild all stubs, then run a clean baseline with `rm -f trace.log` and verify that (a) lines are well-formed, (b) `E,1F6E` appears at the device call, and (c) the tail hash/RVA histogram can be trusted again.

### Run 223 plan: baseline with repaired tracer
- C++ change accepted: `common.h` hash `865fd4d51e32072c66bd6422e039c7b2f4667dfbf952e8b01b773e211a719e49`; rebuilt `d3d12_xs.dll` hash `60b46e369689feab2c3838f71bdf07126211085466c37e37e2cf642c335db89a`; `xgameruntime.dll` `af48351a706cbff86b5f31b91a90df7546c1740da0165ca8e326fa2a6948dca5`. Source files `d3d12_xs.cpp` and `xgameruntime.cpp` are unchanged.
- Run with `rm -f trace.log` before launch, then check for well-formed lines, a single timestamp cluster, `E,1F6E`, and the new authoritative tail/histogram.

### Run 223 result: tracer repaired; new static-init crash reached
- Result: exit `5`; Proton log `225456` lines; trace `232136` raw bytes / `5709` lines / `5706` normalized matches with one timestamp cluster (`195599`-`195600`).
- The tracer repair worked: `d3d12_xs,E,1f7b,1424785bc` is now present (the old `0x1F6E` export shifted to `0x1F7B` because of the added `SetFilePointer` code), proving `XsD3D12XboxCreateDevice` is called and logged. RVAs also shifted by +14 bytes for later d3d12 functions (`dummy_ok` is now `2adb`).
- New crash: `EXCEPTION_ACCESS_VIOLATION` at `game+0x1D029B4`, read from `0x0000008900000088`; backtrace `0x1D029B4`, `0x1D02454`, `0x1BB1CBD`, `0x58A1E46`, `0x589F0E9`, `0x21B8974`, `0x81B2E8`, `0x53EC05E`, `0x53EBF3A`, `0x661F0`, `0x1D109C9`, `0x5A91C0D`, `0x56A1E5A`, kernel32, ntdll. Registers: `rdx=0x8900000088`, `rcx=0x450`, `rdi=0x2de7be8`, `rbx=0x2de9b78`.
- Disassembly shows this is the static shader/resource registry table resize at `0x141D026D0`. For entries with `+0x30 != 0`, it treats `+0x28` as a pointer to a packed handle array and copies `count*8` bytes. The offending entry has `+0x28 = 0x8900000088` (a tagged handle, high word `0x89`, low word `0x88`) and `+0x30 = 0x450`, so the resize dereferences the handle instead of an array pointer.
- This is genuine forward movement from the white-screen stall to device creation plus a concrete fault. The tracer change is retained, but its own header writes still lack `SetFilePointer` and can corrupt the first line; defer that cleanup to a later single change.
- EXE and backup remained at baseline hash/size; no temporary EXE patch was used in run223.

### Run 224 hypothesis: skip the registry-resize handle copy
- Diagnostic-only EXE patch at VA `0x141D0299D` / file offset `0x1D01D9D`, replacing `0f b7 4b 30` (`movzwl 0x30(%rbx),%ecx`) with `31 c9 90 90` (`xor ecx,ecx` + NOPs). This makes the resize loop skip all packed-handle array copies. Patched hash `08ee43660c189cc2f1b250179ce06df8552d645d4e69ee6e974067b2ec77775d`.
- If the same static-init fault disappears and boot advances, the handle-copy path is the new blocker and the next fix should distinguish handle entries from pointer-array entries. Restore the EXE immediately after the bounded run.

### Run 224 result: resize AV eliminated; bogus callback call reached
- Result: exit `5`; Proton log `239976` lines; trace `247355` raw bytes / `6090` lines / `6087` normalized matches with one contiguous timestamp cluster (`195833`-`195835`); `1f7b` device-creation entries still present twice.
- The `0x141D029B4` resize AV is gone. New crash: `EXCEPTION_ACCESS_VIOLATION` at `0x000000000201A600` (unknown module, `info[0]=0x8`, likely execute). Registers: `rax=0x201A600`, `rbx=0xD0`, `rcx=0x2DE90E0`, `rdx=0`, `rsi=0x818CB900`, `rdi=0x2DE9070`, `r8=0`, `r9=0`.
- Backtrace: `0x201A600`, game `0x40418C9`, game `0x4041820`, game `0x1DC40A9`, game `0x5BBD7F1`, game `0x1C2903E`, game `0x23CF0EF`, game `0x2215ECA`, kernel32, ntdll.
- `0x1440418C9` is `call *%rax` in the cleanup loop at `0x144041810`: it iterates records of size `0x68` and calls the function pointer at record `+0x60` when nonzero. The bogus `0x201A600` is an uninitialized/garbage callback slot left by skipping the resize copy entirely. This is forward movement past shader/resource static initialization, but the blanket copy skip is unsafe as a permanent fix.
- EXE restored to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.

### Run 225 hypothesis: zero copied handle words instead of skipping the copy
- Single new diagnostic EXE patch at VA `0x141D029B4` / file offset `0x1D01DB4`, replacing `4e 8b 0c 02` (`mov r9,[rdx+r8]`) with `4d 31 c9 90` (`xor r9,r9` + NOP). This preserves the resize write loop and count but writes zero for every packed handle word instead of dereferencing the bogus source pointer.
- If the callback-table slots become zero, the cleanup loop's `test rax; je` will skip them and boot may advance. If the same bogus callback persists, the callback source is not solely the copied handle word and the next target should be the callback producer at registration time.

### Run 225 result: crash cleared; new texture-init stall reached
- Patched hash `a91c0c29c9be7ce3d9b34b727556c55f696f614c7ff5d158e61ac57e9776ce23`; EXE restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; Proton log `234776` lines with **zero** `EXCEPTION_*` and zero unhandled reports. Trace `241691` raw bytes / `5962` lines / `5959` normalized matches, one contiguous timestamp cluster (`195920`-`195922`), with `1f7b` device-creation entries present twice.
- Both the `0x141D029B4` resize read AV and the `0x201A600` bogus-callback AV are eliminated. Zeroing copied packed-handle words is therefore accepted as the working diagnostic baseline for subsequent runs.
- Furthest milestone so far: boot reaches an extended texture/shader initialization loop and loads native `d3d12.dll`, `d3d12core.dll`, and `dxgi.dll` from `C:\windows\system32`. Trace activity continues through repeated `platform:\shaders\v2\sga_xbsx_final\*.fxc` reads, then stops at `195922.489` with no further shim calls before the 15 s timeout.
- Tail is dominated by `d3d12_xs` `device_format_info` (`287c`), `device_resource_allocation_info` (`276a`), `device_create_view` (`2726`), `device_out_r9` (`29b5`), `device_out_arg6` (`27df`), and `dummy_ok` (`2adb`), plus `xg_xs` `texture_computer_noop` (`176b`), `XgSuggestSwizzleMode` (`1799`), and `XgCreateTextureComputer` (`16f3`). This is real forward movement past static init, but still no Rockstar logo, Present, or rendered frame.
- Next diagnostic: sample live thread states during the silent tail with the zero-copy patch active, to determine whether the stall is a user-space spin, a blocking wait, or simply slow resource work.

### Run 226 plan: live thread-state sample at the silent tail
- Reapply only the accepted run225 zero-copy patch (`0x141D029B4` -> `xor r9,r9; nop`) and launch the standard 15 s bounded run in the background.
- After roughly 6 s, sample `/proc` for `game_scarlett_m` threads (`stat`, `wchan`, `syscall`) twice. No EXE or C++ change beyond the accepted patch; the goal is only to classify the stall before choosing the next single fix.

### Run 226 result: one user-space spinner drives repeated dummy-resource calls
- Patched hash `a91c0c29c9be7ce3d9b34b727556c55f696f614c7ff5d158e61ac57e9776ce23`; EXE restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; Proton log `231964` lines with zero `EXCEPTION_*` and zero unhandled reports. Trace `238731` bytes / `5888` lines / `5885` normalized matches, one contiguous timestamp cluster (`196078`-`196080`), `1f7b` present twice.
- Two `/proc` samples at roughly 6 s and 9 s showed the same shape: 23 game threads, all but one sleeping in `futex_wait_multiple` (one in `poll_schedule_timeout`), while one thread remained state `R`, `wchan=0`, with ~11 s accumulated CPU and ~1130 context switches. The stall is therefore a live user-space loop, not a Win32 blocking wait or a lack of worker wakeups.
- Trace tail is a tight cycle from trace thread `300` (`0x12c`): `device_format_info` (`287c`) from `0x140344188`/`145`, `device_resource_allocation_info` (`276a`) from `0x140abfe0d`/`faf`, `device_create_view` (`2726`) from `0x14193e2db`, then generic `dummy_ok` (`2adb`) from `0x140164f01`, `0x140cc29ea`, `0x141647616`, and `0x1414a69e8`. The cycle continues to the last shim call at timestamp `196080286`, just before the timeout.
- Disassembly of `0x14193e2b0` shows the slot-`0x428` call stores the real output pointer at `[rsp+0x30]` (`lea 0x20(%rsi),%rax`) and a constant at `[rsp+0x28]`. Our `device_create_view` has seven parameters, so it writes its `out` to `[rsp+0x38]`, one stack slot too late; the title's expected output at `[rsp+0x30]` is never filled. The same six-argument shape is visible at `0x14193dfa0`.
- Next C++ fix attempted: reduce `device_create_view` from `this + 5 void* + out` to `this + 4 void* + out`, so the output pointer is the sixth argument at `[rsp+0x30]` and the shader/view object is actually returned to the caller.

### Run 227 result: slot-0x428 arity hypothesis was wrong; original signature restored
- Change tested: `device_create_view` reduced from seven to six parameters. Rebuilt `d3d12_xs.dll` hash `cee70e8a56ef1fac456d77712bf6b7d09a8c1491d6e2353af651c7d28fda87f5`; source hash `b3a9d41fcbdf84127540a433bf579ba00c0fec03ca0b43a18a4ad06309ed58bd`.
- Result: exit `5`; Proton log contained `EXCEPTION_ACCESS_VIOLATION` at `d3d12_xs+0x2751`, instruction `movq %rax,(%rsi)`, writing to `0x1463FA690`. Backtrace rooted at game `0x141E86853` (slot-`0x428` call site `0x141E8684C`). Trace `144590` bytes / `3519` lines; the previous long resource loop never developed.
- Root cause of the failed hypothesis: Windows x64 callers reserve `0x20` bytes of shadow space before the first stack argument, and `call` pushes an additional return address. With the callee prologue (`push rsi; push rbx; sub 0x28`) the compiler correctly reads the seventh argument from caller `[rsp+0x30]`. The original seven-parameter signature was right; the six-parameter version wrote into the constant at `[rsp+0x28]` and crashed. Reverted the source and rebuilt the DLL.
- Corrected reading of `0x14193E2D4`/`0x141E8684C`: caller `[rsp+0x28]` is the sixth argument (constant `0x1463FA690`) and caller `[rsp+0x30]` is the seventh argument, the real output pointer. The original `device_create_view` already filled the expected output.
- New hypothesis from `0x140344130`: slot `0xc8` is called with the title's own `+8 = 0x1000` already set. The helper copies the 16-byte result and requires `[out+8] == 0x1000`; our `device_format_info` clears all 16 bytes, forcing the fallback path at `0x14034415F` and a second identical call that also returns zero. Setting the returned qword at `+8` to `0x1000` while keeping `+4 == 0` should let the first call satisfy the assertion and may remove one repeating branch from the loop.

### Run 228 result: format-info fallback removed; silent spin unchanged
- C++ change accepted: `device_format_info` now returns `{qword0=0, qword1=0x1000}` instead of clearing all 16 bytes. Source hash `80ce7cddc5db1fdc9a2a204cbc798b483eca58e6343102aee988226fb4d3aa22`; rebuilt `d3d12_xs.dll` hash `85d635278f52736a7b703d14f6cbabc1835ef0b387af56a375ee7e4f728d452e`.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; Proton log had no `EXCEPTION_*` or unhandled reports. Trace `237611` bytes / `5860` lines / `5857` normalized matches, one contiguous timestamp cluster (`196480`-`196481`).
- The fallback call count from `0x140344188` dropped from `99` (run226) to `52`, while the first call count from `0x140344145` stayed at `51`. This confirms the slot-`0xc8` output change works as intended and is retained.
- However, no new shim calls or rendering milestone appeared; the trace still ends at the same semantic tail (`device_resource_allocation_info`, `device_create_view`, `dummy_ok`) after only ~1.4 s of shim activity, followed by ~12 s of silent spinning. The remaining blocker is inside game code after the last shim return, not another dummy-COM retry visible in `trace.log`.
- Next diagnostic: sample `/proc/<pid>/task/<tid>/stat` field 30 (`kstkeip`) for the runnable thread and translate it through `/proc/<pid>/maps` to a game VA. Earlier samples showed a nonzero user-space instruction pointer; this can locate the silent loop without ptrace.

### Run 229 result: nondeterministic allocation crash during IP sampling
- Same accepted EXE zero-copy patch and same rebuilt DLL/source as run228. EXE restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `5`, not timeout. Proton log: `EXCEPTION_ACCESS_VIOLATION` in `VCRUNTIME140.dll+0x1989`, a write to address `0`, on thread `017c` (Wine tid `380`). Backtrace: `VCRUNTIME140+0x1989` <- game `0x145861B54` <- `0x1443C38FD` <- `0x1423CF4F2` <- `0x1423CF2D3` <- `0x1423CF104` <- thunk `0x142215ECA`.
- `0x145861B40` allocates `0x47AC0` bytes with `0x40` alignment via `0x145861BA0`; that routine allocates through `0x144D9C160` and tail-calls `0x145F40580`, which faults filling a null destination. This looks like an allocator failure in a worker phase, not a shim callback fault.
- Trace `243995` bytes / `6006` lines / `6003` normalized matches, one cluster (`196538`-`196540`). Last shim activity before the crash is thread `380` repeatedly calling `device_format_info` from `0x140344188`; thread `308` remains in the `xgameruntime` loop. This is a different failure from run228's clean timeout with the identical code, so the crash is timing/heap-state dependent.
- The `/proc` IP sampler started at 6 s and found no process because the game had already exited; no IP translation was obtained.
- Next run: repeat the same code with sampling at 2 s and 4 s, before the early crash window, to try to capture the runnable thread's `kstkeip` and maps.

### Run 230 result: second nondeterministic null-write crash; format-info change reverted
- Same accepting code as runs228-229. Process was already gone when sampling began at 2 s, confirming the crash occurs less than ~2 s after launch.
- Result: exit `5`; `EXCEPTION_ACCESS_VIOLATION` at game `0x1411BCE31`, a `movq $0,-0x30(%rax)` with `rax=0`, on thread `017c`. Backtrace: `0x1411BCE31` <- `0x1411BC8C2` <- `0x1411BC630` <- `0x141D4B37E` <- `0x141D4AD2C` <- `0x141DC408C` <- `0x145BBD7F1` <- `0x1C2903E` <- `0x1423CF0EF` <- thunk `0x142215ECA`.
- `0x1411BCD80` allocates `rbx*16` bytes through `0x144D9C140`, adds `0x30`, and writes bitmap entries; the allocator returned null. This is a heap/allocator failure in a worker phase, not a shim callback fault. Last shim event on the crashing thread was `device_format_info` from `0x140344188` at the crash timestamp.
- Trace `5963` normalized events with the same semantic tail; two consecutive runs with the format-info change crashed in two different null-write paths, while the immediately preceding run with the same change timed out cleanly. Because the change produced no new rendering milestone and cannot be shown safe, reverted `device_format_info` to its original zero-fill. Source hash is back to `dc77afec8d25cbb48069c22ad7057e8b1930aa8908c51b0cf1c6bf8800280c3e`; rebuilt `d3d12_xs.dll` hash `8e4db7659876ffb5e127511660f1d528fcaf743b4417b1d20748cc85815f983e` (PE build timestamp differs from the earlier `60b46e...`, but the source is identical to the run226 baseline).
- EXE restored to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Next diagnostic: run the reverted baseline and sample `/proc` at 2 s, 4 s, and 6 s. With the known stable no-crash baseline, the process should remain alive long enough to capture `kstkeip` from the runnable thread and translate it through `/proc/<pid>/maps`.

### Run 231 result: baseline is stable; Linux `/proc` instruction pointers are unusable
- Restored DLL/source baseline (`d3d12_xs.dll` `60b46e369689feab2c3838f71bdf07126211085466c37e37e2cf642c335db89a`, source `dc77afec8d25cbb48069c22ad7057e8b1930aa8908c51b0cf1c6bf8800280c3e`) and reapplied only the accepted zero-copy EXE patch. EXE restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `241691` bytes / `5962` lines / `5959` normalized matches, one contiguous cluster (`196678`-`196680`), with the same semantic tail as run226.
- `/proc` found the process alive at 2 s, 4 s, and 6 s with one runnable thread (`tid 305466`), but `kstkeip` and `kstkesp` were both `0` in every sample. Linux reports zeroed user-mode instruction pointers for these Wine threads, so this diagnostic cannot locate the silent game loop.
- The reverted baseline is stable again. Next diagnostic must sample RIP from inside the game process with Win32 `GetThreadContext`, not from `/proc`.

### Run 232 result: in-process RIP sampler found a hash-table probe loop
- Temporary diagnostic only: `d3d12_xs.cpp` spawned one sampler thread after the 100th `device_format_info` entry; after a 2 s delay it enumerated process threads, suspended them, logged `CONTEXT.Rip` and the owning module, then resumed them. Source hash `adc7a251e85ea2742b82aa84afac88a775adc97fbd2675abd64464d7a8e2b749`; rebuilt `d3d12_xs.dll` hash `40605bf3fc27d7e5f80e79e9cdb1719b1f207783bc81e5717715260c40cc5974`. All other DLLs were restored to the run227 baseline and the source/DLL were restored after the run to `dc77afec...`/`60b46e...`.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `244818` bytes / `6025` lines / `5999` normalized profiler events plus 23 sampler lines, one timestamp cluster. Sampler added ~5 KB, still acceptable.
- The sampler could only obtain contexts for one non-sampler thread (`tid 404`). Twenty-one samples had `RIP=ntdll.dll+0xea94`, one had `ntdll.dll+0xf094`, and one had `RIP=0x14181934a` in `game_scarlett_master.exe` (base `0x140000000`). Other threads either were not returned by Wine's toolhelp snapshot or `GetThreadContext` failed for them.
- `0x14181934a` is inside the open-addressing probe loop of `0x141819290`: it repeatedly reads 16-byte hash-table entries from `0x1475a7ed8`, tests for null or a matching 64-bit key, and jumps back to `0x14181932e`. If the table has no free slot for the requested key, this loop never terminates. This is the first concrete user-space location for the silent tail.
- Next diagnostic: extend the same sampler to log a handful of stack words from the game-RIP thread so the active caller of `0x141819290` can be identified; then patch that caller's hash input or table growth path instead of blindly skipping the loop.

### Run 233 result: stack sampler did not reach its sample window
- Extended diagnostic: sampler now resolves module names for stack words via `ReadProcessMemory` and logs `T,<tid>,slot=<i>,addr=...,module=...` for a thread whose RIP is in the game module. Source hash `c975f54546d25c76876b0f22dee6f63a1f776a6046f32dd5b1469d16cdbc66c0`; `d3d12_xs.dll` `3d2f9e2a9ac79437bbe4d09a4697c6597192957566cdbc76150afbf33e01ac13`. All DLLs restored from the snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `5`; `EXCEPTION_ACCESS_VIOLATION` in `VCRUNTIME140.dll+0x1989`, write to address `0`, thread `017c`, with the same backtrace as run229 (`0x145861B54` <- `0x1443C38FD` <- `0x1423CF4F2` <- ...). Trace `248921` bytes / `6128` lines / `6125` normalized profiler events with one cluster (`197051`-`197052`).
- No `S,` or `T,` lines were written. The sampler's 2 s sleep had not elapsed before the early crash; the trace itself only spans about 1.4 s. This is another instance of the timing-dependent null-allocator crash, not evidence against the stack-sampling code.
- Next change: trigger the sampler at 60 `device_format_info` entries and sample after 500 ms so it runs inside the active texture/shader phase. Everything else stays the same.

### Run 234 result: stack identifies the exact hash-insert caller
- Diagnostic change accepted for this run: trigger at 60 `device_format_info` entries, 500 ms sample delay. Source hash `b65098b64142ac82f69c6b95f6705ed54ea9f05d1dc8d1cf3b6a8a62f8df0706`; `d3d12_xs.dll` `4e1bf229adab327bfdfca98c8ee6d16e5b625319a913ff27a25ca35e0892aee9`. DLLs restored from snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace contains 23 `S,` and 3 `T,` sampler lines. The sampled game RIP is `0x141819357`, still inside `0x141819290`'s hash-probe loop.
- Stack words from the game-RIP thread identify the live chain: `0x141D4B624` (return from `call 0x141819250`) -> `0x141819270` (return from `call 0x141819290`) -> `0x141819290` (hash insert) -> spin at `0x141819357`. A data stack word is the table address `0x1475B30F8`.
- `0x141D4B624` is in a 41-entry loop at `0x141D4B53B` that builds a 0x44-byte key on the stack and interns it via `0x141819250`, storing the returned 16-bit index at `[rdi + r14*2 + 0x9F12]`. `0x141819250` hashes the 0x44-byte key and calls `0x141819290`.
- The spin means `0x141819290` never finds a null slot or matching key in the table at `0x1475A7ED8`; growth logic at `0x1418194D5` doubles capacity only when `count*0x4B/100 >= capacity`.
- Next change: log the runtime values at `0x1475A7ED0` (capacity), `0x1475A7ED4` (count), and `0x1475A7ED8` (table pointer), plus a few table words. That will distinguish a full table, a zero/invalid table pointer, and a key that can never match.

### Run 235 result: global hash table is not full, but no game-thread context was sampled
- Diagnostic change accepted: sampler at 60 `device_format_info` entries plus one `G,` line with hash-table globals and the first eight qwords. Source hash `e95d0872ae35a9600313e0b0fe5fcc1e132eb506ea50b28a8d73f9149ccdc125`; `d3d12_xs.dll` `103dce9ac1fc33744c06a2e505dc4957033ab8f462a19748374ad1a61cf4d0a1`. DLLs restored from the run235 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `265707` bytes / `6531` lines / `6499` normalized profiler events with one contiguous cluster (`197208`-`197210`).
- `G,cap=512,count=291,table=2de2a70,w0=0,...,w7=0`. The table is only 57% full, below the 75% growth threshold, and the first four 16-byte slots are empty. A full-table explanation is therefore ruled out for the global snapshot.
- The sampler produced 28 `S,` lines, all in `ntdll.dll`; no game-module RIP and no `T,` lines were captured this run. The earlier caller chain from run234 remains the best evidence, but the live registers that hold the spinning thread's table base and probe index were not recorded.
- Next change: keep the same trigger/delay, but request integer registers in `CONTEXT`, log `RAX/RDI/R14/R8/RBX/RDX`, and dump 16-byte table entries around `RAX + R8*16` while the game thread is suspended. `RAX` matters because the spinning loop caches the table base in a register; the global may change during a concurrent rehash.

### Run 236 result: earlier execute-access crash prevented the register sample
- Diagnostic change accepted: `CONTEXT_FULL` plus one `R,` line with the spinning thread's integer registers and 13 table keys around `RAX + R8*16`; the game thread stays suspended while those words are read. Source hash `dcdcd209717a664312639229f205063c74fc5eae7731e4b70cd2d705598312d1`; `d3d12_xs.dll` `bd9d5daab4302d817d001c4f41817347930026a0864fe1bc6f42ff712eca0e0d`. DLLs restored from the run236 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `5`; `EXCEPTION_ACCESS_VIOLATION` attempting execution at `0x02012600`, thread `017c`. Backtrace: unknown `0x02012600` <- game `0x1440418C9` <- `0x144041820` <- `0x141DC40A9` <- `0x145BBD7F1` <- `0x141C2903E` <- `0x1423CF0EF`. Trace `248041` bytes / `6106` lines / `6103` normalized profiler events with one cluster (`197333`-`197335`).
- No `G,`, `R,`, `S,`, or `T,` lines: the 60th `device_format_info` entry happened too near the early crash, so the 500 ms sampler delay did not complete. This is another timing-dependent callback/allocator failure, not a result from the register diagnostic.
- Next change: trigger the same sampler at the 20th `device_format_info` entry and sample after 200 ms, so the register dump happens before the known early-crash window.

### Run 237 result: sampler ran but all thread contexts were rejected
- Diagnostic change accepted: trigger at the 20th `device_format_info` entry, 200 ms delay. Source hash `8affd5fbd408c50c8260c984406cc6aa0edc3482c43fe9c9bede61dde45f95af`; `d3d12_xs.dll` `8016f0d4560dca2ee6ace1270d8145feba544372dd2d3b32fc15a0dc96d7acca`. DLLs restored from the run237 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `242257` bytes / `5973` lines / `5969` normalized profiler events with one cluster (`197405`-`197406`).
- `G,cap=512,count=291,table=2de0430` was written, with a mix of nonzero and zero table keys (`w1=0`, for example). The table is not globally full.
- No `S,`, `R,`, or `T,` lines were produced. The likely cause is the switch to `CONTEXT_FULL`: Wine may reject that flag set in `GetThreadContext`, so every thread's `ok` was false and no sampler line was emitted. This also explains why run236 had no sampler lines before its early crash.
- Next change: request only `CONTEXT_CONTROL | CONTEXT_INTEGER` (enough for RIP/RSP/RAX/RDI/R14/R8/RBX/RDX) instead of `CONTEXT_FULL`; keep the 20th-call trigger and 200 ms delay.

### Run 238 result: context-flag hypothesis rejected
- Diagnostic change accepted: `CONTEXT_CONTROL | CONTEXT_INTEGER` in place of `CONTEXT_FULL`. Source hash `2847842b2fe0cfe5a1a4dc92f88d726ae59ee2c4903f219e536e179a00de86d7`; `d3d12_xs.dll` `11a93e2d58a70ea6037c3b28585afd3cad901f185263597ae1e4c3e8c25b5e1e`. DLLs restored from the run238 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `243841` bytes / `6013` lines / `6008` normalized profiler events with one cluster (`197481`-`197482`).
- `G,cap=512,count=291,table=2dec290` was written, but again no `S,`, `R,`, or `T,` lines. Wine accepts the narrower context flags, so the missing lines are not caused by an invalid `CONTEXT_FULL` request.
- The remaining possibilities are that the thread snapshot is invalid/empty at this earlier trigger point or that `GetThreadContext` is failing silently. Next change: add sampler self-diagnostic lines for snapshot handle/`GetLastError` and the count of threads that fail `GetThreadContext`; keep the 20th-call trigger, 200 ms delay, and `CONTEXT_CONTROL | CONTEXT_INTEGER`.

### Run 239 result: sampler is healthy but too early for the game spin
- Diagnostic change accepted: sampler self-reporting (`H,` line) for snapshot validity and context failures. Source hash `ef3925a62e6131192b0a5df28ff380da79c6f9565eb0be51573aba7cb2955689`; `d3d12_xs.dll` `d53d576d102a59bd960377ba949cc80b4d6eed1e6faeb2c2c85e31e7454e84ce`. DLLs restored from the run239 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `265318` bytes / `6523` lines / `6493` normalized profiler events with one cluster (`197589`-`197591`).
- `H,snap=89c,snap_err=0,seen=25,ok=25,failed=0,last_fail_err=0` proves the toolhelp snapshot and all 25 `GetThreadContext` calls succeeded. There were 25 `S,` lines, all in `ntdll.dll`; no game-module RIP, so no `R,` or `T,` lines.
- `G,cap=512,count=291,table=2de3e30` again showed free table slots. The 20th-call/200 ms sample is simply before the game thread enters the `0x141819290` spin.
- Next change: keep the early trigger, but take four rounds at cumulative 200 ms, 500 ms, 1000 ms, and 1500 ms. The later rounds should catch the spin that run234 captured at approximately the 60th call plus 500 ms, without depending on one fragile timing point.

### Run 240 result: the hash table is effectively full while `count` is stale
- Diagnostic change accepted: four sampler rounds at cumulative 200/500/1000/1500 ms after the 20th `device_format_info` entry. Source hash `f492333c839b20a552da9a72d930d9356c8451e41ea99b92db01142b091890b9`; `d3d12_xs.dll` `3540fb4fca92448464a2c3381d08e978f896717a3b04b0ac62fe18a05a84308a`. DLLs restored from the run240 snapshot after the run.
- EXE used the accepted zero-copy patch and was restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `260685` bytes / `6302` lines / `6183` normalized profiler events with one cluster (`197699`-`197702`). Sampler overhead was 4 `H,`, 4 `G,`, 4 `R,`, 92 `S,`, and 12 `T,` lines (~24 KB), still acceptable.
- Every sampler round reported `snap_err=0`, 23 threads seen, 23 contexts obtained, zero failures. Four game-thread samples had `RIP=0x141819351/0x14181932e/0x141819332/0x141819357`, all inside the `0x141819290` probe loop, with the same `rdi=3d7047ddc6e7073c` and `r14=5bf4d569`.
- `RAX=0x2de1730` matched the global table pointer, so there was no rehash race. The live probe index advanced around the table (`0x8d` -> `0x15e` -> `0x115` after accounting for the shifted R8 at `0x141819332` -> `0xcb`), but never found a null slot or the requested key.
- All 13 table keys logged around each of the four probe positions were nonzero and formed dense consecutive runs (for example `5da000005d9`, `5de000005dd`, ... `60a00000609`). With `count=291` of 512 slots, finding 52 sampled slots all occupied would be extremely unlikely if `count` were the true occupancy; the table is effectively full and the counter is stale.
- Root cause hypothesis: the fallback insertion path at `0x1418193aa` can insert via the free list without incrementing `0x1475A7ED4`, while the growth check at `0x141819443` only triggers when `count == capacity*75/100` (exact equality). Once occupancy outruns the stale counter, growth never happens and the probe loop at `0x14181932e` has no exit.
- Next change: temporary two-byte EXE patch at VA `0x14181935A` (file offset `0x181875A`), `75 d2` (`jne 0x14181932E`) -> `eb 00` (fall through to `add rdx,rax`). This makes an occupied non-matching slot reuse its existing 16-bit index instead of looping forever. Apply it together with the accepted zero-copy patch, with baseline DLL/source restored, then check for forward progress or a new crash.

### Run 241 result: hash-spin patch accepted; boot reaches new device setup paths
- Temporary EXE change accepted for this run: zero-copy patch plus `0x14181935A` `75 d2` -> `eb 00`. Patched EXE SHA-256 `18587ece5326ad307edd65847423b625ede989356717b7b3322de69a06036156`; restored afterward to baseline SHA-256 `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`, size `120181760`; backup unchanged. Baseline DLL/source restored (`d3d12_xs.dll` `60b46e...`, source `dc77afec...`).
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `259741` bytes / `6406` lines / `6403` normalized profiler events with one cluster (`197779`-`197780`), up from `5959` events in the run231 baseline.
- The old `0x141819290` hash-probe stall is gone. New shim entries appear from callers `0x141F75FB4`, `0x141F75FCE`, `0x141F75FE0`, and `0x144FB0AEF`, with `d3d12_xs` RVAs `2973`, `25d5`, `2526`, `2942`, and `26a1`. Disassembly of `0x141F75F60` shows these are device vtable slot `0x70` (descriptor heap), a returned object's slot `0x48`, device slot `0x78` (tile size), and setup of large pointer arrays. This is concrete forward movement past the previous resource-interning wall.
- No Present, swapchain creation, Rockstar logo, or rendered frame appeared; the trace again becomes silent after roughly 1.5 s of shim activity.
- Next diagnostic: reapply the run240 four-round in-process sampler (20th-call trigger, cumulative 200/500/1000/1500 ms) together with both accepted EXE patches, so the new silent tail can be localized without reintroducing any C++ behavior change beyond the existing sampler.

### Run 242 result: toolhelp sampler stalls in the new execution path
- Diagnostic reapplied: run240 four-round sampler source hash `f492333c839b20a552da9a72d930d9356c8451e41ea99b92db01142b091890b9`; rebuilt `d3d12_xs.dll` hash `d603ce97bc4bdb73e9201c6d54990899a4057964535803999a62e13506fa1ea2`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*` or unhandled reports. Trace `240688` bytes / `5934` lines, ending mid-write at `197861060,300`. Profiler events `5929`, one cluster (`197859`-`197861`). Most frequent events are the same device/caller set as run241 but with shifted RVAs from the different DLL build.
- Only one `G,` line was produced (`cap=256,count=182,table=2dddb80`), and no `H,`, `S,`, `R,`, or `T,` lines. The sampler wrote `G` from its first round and then apparently blocked inside `CreateToolhelp32Snapshot`/thread enumeration instead of completing even one round.
- The new game path now holds a Wine/loader lock that the toolhelp-based sampler cannot take; enumerating all threads is no longer a safe diagnostic here.
- Next change: capture `GetCurrentThreadId()` when the 20th `device_format_info` entry triggers the sampler and have the sampler call `OpenThread` directly on that thread. This avoids toolhelp entirely while still suspending the active game thread and reading `CONTEXT` plus its stack/table window.

### Run 243 result: direct-thread sampler also stalls after the first `G` line
- Diagnostic change accepted: direct `OpenThread` on the captured triggering thread, no toolhelp thread enumeration. Source hash `912195b23e4c706b27dec2da7a3c89d18552adda6fd25108cd30920ba53cbb53`; DLL hash `41733be45ca0f08fce90ad7c7efd5f9928cbb37f8c7d4859185264c302c83569`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `246281` bytes / `6073` lines / `6069` normalized profiler events with one cluster (`198009`-`198011`).
- Same pattern as run242: one `G,` line (`cap=256,count=183,table=2de4780`) was written, then no `H,`, `R,`, or `T,` lines. Unlike run240, the table is not densely full and `count` is increasing with each run, so the old full-table hypothesis no longer explains the new silent tail.
- Next change: add fine-grained `H,` checkpoints around `OpenThread`, `SuspendThread`, `GetThreadContext`, register logging, stack logging, and `ResumeThread`, so the exact blocking call is identified. Keep direct-thread sampling and both EXE patches.

### Run 244 result: sampler is healthy; captured thread is asleep in Wine
- Diagnostic change accepted: stage checkpoints and resume-before-logging. Source hash `02176595618d3040e289068784c7c422827f1f5aab4dcb45a8731ab95ec69a86`; DLL hash `80afbf897c52288ae5d2991358ee5dcf4a7a2bfa8418dcaf60ed7dac684deb3f`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `268039` bytes / `6574` lines / `6443` normalized profiler events with one cluster (`198127`-`198130`).
- All four rounds completed `start`, `open`, `resume`, `regs`, `stack`, and `done` checkpoints for `tid=380`; no blocking. The earlier no-`H` runs were caused by logging while the suspended target thread still held `shim_trace_lock`.
- Sampled RIPs were `0x6ffffff3ea94` (`NtWaitForSingleObject+0x14`) and `0x6ffffff3f094` (`NtDelayExecution+0x14`) in `ntdll.dll` (base `0x6ffffff30000`). The trigger thread is sleeping/waiting, not executing the game's silent tail.
- Proton log tail shows another thread (`tid=0x188`/392) actively writing `trace.log` at `198130.638`, so the boot work continues on a different thread.
- Next change: enumerate threads with `NtQuerySystemInformation(SystemProcessInformation)` instead of toolhelp, log the discovered TIDs, and then sample each non-self thread with the same suspend/get-context/resume-before-log pattern.

### Run 245 result: native thread enumeration works; known null-write crash recurs
- Diagnostic change accepted: resolve `NtQuerySystemInformation` in `DllMain` and enumerate TIDs via `SystemProcessInformation`. Source hash `b48b20eef7f4036d7f2ae4ff820bd7c84187f7d8c4ea8d3ea2cc67ca32640844`; DLL hash `bbea1133391558fb87f4dc5023ff9b70a2a3abfefc3cc02ad0eb04573440c3dd`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `5`; `EXCEPTION_ACCESS_VIOLATION` in `VCRUNTIME140+0x1989` / `0x6ffffda61989`, write to null, thread `0x17c`. This is the recurring timing-dependent null-write crash, not a new diagnostic failure.
- Enumeration succeeded: one `G,` line and 23 `U,` lines listing TIDs `300,308,...,384,400,404`. Toolhelp was avoided and no enumeration stall occurred.
- The crash happened near the end of the trace, so the next sampling pass should still collect data from the earlier rounds.
- Next change: in each sampler round, iterate the enumerated TIDs and sample every non-self thread with `OpenThread`, brief suspend, `GetThreadContext`, resume, then log `V,tid=...,rip=...` after the target resumes. This should reveal which thread owns the silent tail.

### Run 246 result: all-thread sampling started too late
- Diagnostic change accepted: enumerate TIDs and sample every non-self thread, logging `V,tid=...,rip=...`. Source hash `10d6f58335cf8cd5d61d988f15471897e9aca230b6536ae4cbdd652b492dcadf`; DLL hash `0514092814e7e1df7d62e30cbe2a328eeac097445e62a9afeee78a6d2ff63329`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `5`; the same `VCRUNTIME140+0x1989` null-write crash on thread `0x17c`. Trace `248921` bytes / `6128` lines / `6125` normalized events with one cluster (`198386`-`198387`).
- No `G,`, `U,`, or `V,` lines were produced. The 20th `device_format_info` trigger occurs very late—in prior runs the `G,` line sat at line ~5920 of ~5930—so the 200 ms first delay placed the first sample at the same time as this timing-sensitive crash.
- Next change: reduce sampler delays from `{200,300,500,500}` to `{0,100,200,300}` so the first all-thread sample happens immediately at the 20th trigger, before the recurring null-write crash can preempt it.

### Run 247 result: all-thread sampling now captures the boot tail
- Diagnostic change accepted: first sample delay zero (`{0,100,200,300}`). Source hash `644f1a1efd09dc671039b88228e14eac71bc58f2920eea36f3ed10476a69ac2d`; DLL hash `1ffc20c9d4de2d76c1f87614ba6a599f5d3b98dc9b3b5b2191cd9a1b5dc1c549`. EXE used both accepted patches (`18587ece...` while running); restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `267157` bytes / `6650` lines / `6443` normalized events with one cluster (`198453`-`198455`).
- Sampler produced 4 `G,` and 100 `V,` lines. Nearly every thread sat at `NtWaitForSingleObject+0x14` (`0x6ffffff3ea94`) or `NtDelayExecution+0x14` (`0x6ffffff3f094`) in `ntdll.dll`. The trigger thread `380` briefly showed `0x6fffff4a5294` and `NtQueryInformationThread+0x14` (`0x6ffffff3eeb4`), both system calls.
- This changes the model: after the hash-spin patch, the process is not spinning in game code; it is idle/waiting on Wine synchronization.

### Run 248 result: every sampled thread is in an ntdll wait
- Diagnostic change accepted: module attribution on `V,` lines and full context/stack for the trigger thread. Source hash `ca7e76a1c843af94baa13af291e547e30bdc693f166a935ad0174bbb438ee16c`; DLL hash `fad174c299caa0be7707a92f98389b902bdf76d8e4bfeb492009858e99845716`. EXE used both accepted patches (`18587ece...` while running); restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `276391` bytes / `6754` lines / `6443` normalized events with one cluster (`198530`-`198532`).
- All 102 `V,` samples resolved to `ntdll.dll` at base `0x6ffffff30000`; no game-module RIP was observed in any round. Trigger thread `380` was at `NtWaitForSingleObject+0x14` in round 0 and `NtDelayExecution+0x14` in rounds 1-3.
- Raw stacks still contain game addresses (`0x14010A330`, `0x144FB0C27`, `0x144E4BAA9`, `0x1423CF446`, `0x1423CF4DC`) and stub addresses (`0x6FFFFD8135A9`, `0x6FFFFD827020`), so the next step is module-aware stack logging.
- Next change: restore module attribution in `sample_stack` so each raw stack frame is labeled with its containing module and base; keep all-thread sampling and the zero first delay.

### Run 249 result: the waits are on the shim's own trace mutex
- Diagnostic change accepted: module-aware stack frames. Source hash `3f0715fa50cc3760f44fc62425580d2a9a3812a716d8a4586077279defd8b7a8`; DLL hash `aad5a2865f0ea58604b85520b57a61ea7a292be848b95be811c13c5e6e9e5322`. EXE used both accepted patches (`18587ece...` while running); restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `5`; recurring `VCRUNTIME140+0x1989` null-write crash on thread `0x178`. Trace was captured before the crash, with 19 module-labeled stack lines for trigger thread `376`.
- Stack frame `slot=12` was `0x6ffffd841949` = `d3d12_xs.dll+0x1949`, which disassembles to `shim_trace_line+0x49`, the `WaitForSingleObject` on `shim_trace_lock`. Combined with RIP `NtWaitForSingleObject+0x14`, this proves the sampled thread is blocked acquiring our own trace mutex.
- Other stack frames map to `kernelbase.dll` base `0x6fffffbf0000` and `ntdll.dll` base `0x6ffffff30000`; `d3d12_xs.dll` base was `0x6ffffd840000`.
- Next change: stack-sample every enumerated thread in round 0 so we can identify which thread holds `shim_trace_lock` and is itself waiting inside a traced call.

### Run 250 result: the blocked wait is our own `sub_wait_value` counter
- Diagnostic change accepted: stack-sample every enumerated thread, not only the trigger thread. Source hash `16eb8e3f9a6c02e86e94598dc04363dcd15479e0835c35112a29cbd469534026`; DLL hash `1efa63313d9353f2f73d1eb4bba6ad0ffcbbc053e28d0396efab447e31a1b317`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace contained `2520` module-labeled `T,` lines across 28 TIDs. All sampled RIPs were still in `ntdll.dll` waits.
- Thread `380` repeated a four-round stack: `d3d12_xs.dll+0x366b` (`sub_wait_value+0x36`) -> game `0x14010A330` (bit-55 comparison) -> game `0x144FB0C27` (wait-loop body) -> game `0x144E4BAA9`. The second stub frame, `d3d12_xs+0x17020`, is the `g_wait_value` data address, not code.
- Disassembly shows `0x144FB0AD0` calls sub-object slot `0x60`, then tests `(counter - fence)` bit 55. If the bit is set it jumps to `0x144FB0CD4` and loops; if clear it returns. `0x14010A330` returns true when that bit is clear (`bt; setae`), so the source comment had the polarity reversed.
- `sub_wait_value()` currently adds `0x80000000000000` (bit 55) on every call, making the first comparison enter the slow wait loop. Other worker TIDs were in unrelated generic waits; thread `384` was the active trace writer with frames in `shim_write` and `slot278`.
- Next change: remove the `g_wait_value.count += 0x80000000000000ULL` increment and leave the initial `0x41200000` count unchanged, with both accepted EXE patches. This should make `(counter - fence)` bit 55 clear and let the title return from the wait.

### Run 251 result: fixed counter avoids the old stall but does not satisfy the title
- Behavior change accepted: `sub_wait_value()` now returns the unchanged `0x41200000` counter instead of adding bit 55. Source hash `e26b69e372ae00696559a9b6bec3c03f7b1d6e06c31f89ff0d119d5a345bfc78`; DLL hash `70b83be41914c8856abc5a120b4d09de75bf9263d074e51b392520e1df645a30`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; trace `260141` bytes / `6416` lines / `6413` normalized events with one cluster (`198908`-`198909`). No early exception. The tail now has six `sub_wait_value` calls from caller `0x144FB0AEF` instead of one, so the wait did not resolve permanently.
- A C++ exception and repeated `ntdll.dll+0x3969C` AV storm occurs on unrelated thread `0x12c` with `VCRUNTIME140`/`MSVCP140`/game `0x1455E8AF0` frames. This is the known timing-dependent worker failure, not the boot thread.
- Disassembly refined the model: `0x144FB0A50` indexes the table at `0x1468929D0` by the high byte of the handle, passes the object at `entry+0x10` to `0x144FB0A90`, and masks the original handle to 56 bits. That masked handle is the value compared with `*counter`. The low 32 bits of the second argument become only a flag.
- The fixed `0x41200000` counter is smaller than large handle IDs such as `0x1CE94657D09`, so `counter - handle` still has bit 55 set. Next change: keep the no-increment fix and add a one-line `W,` diagnostic that dumps the `this` object's first 64 bytes from `sub_wait_value`, so we can find where the object stores the handle/counter and return a value ahead of it.

### Run 252 result: `sub_wait_value` receives the bare dummy subobject
- Diagnostic change accepted: `W,` dump of the first 64 bytes of the `this` object in `sub_wait_value`. Source hash `0b34012d73e91f69b1edc972aca8307b05f07e4ace2d06b07ff6513baa381552`; DLL hash `7754217aa59a574547942947b68345c47ba2573c28040d6a69c69cba06d2cf55`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; six `W,` lines. Each was `this=6ffffd844280,w0=6ffffd840140,w1..w7=0`. Symbol mapping identifies `this` as `g_subobject` and `w0` as `g_sub_vtable`; the object has no handle or counter stored in it.
- `0x144DACE90` reads the counter from `table_entry+0x28`, and `0x144FB0A50` passes `table_entry+0x10` as the object pointer. Therefore the counter field aliases `g_subobject+0x18`, which is still zero in our dummy object.
- Next change: extend the `W,` line with eight qwords from `this-0x10`, the presumed table entry, to see whether the handle/counter is stored there.

### Run 253 result: no counter in the dummy subobject or its table entry
- Diagnostic change accepted: `W,` dump extended with eight qwords from `this-0x10`. Source hash `55ff0432a6a032bb61d729681b69c1fa7358e8ddf403cf6d8463e9324affa486`; DLL hash `a486c44268452c601b758dc8ede4ce4f8f5ed9856b34ccc6618af71d6de1d55e`. EXE used both accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; six `W,` lines. Each was `this=...4280` with `e2=6ffffd840140` (the object pointer at `entry+0x10`) and all other fields, including `entry+0x28`, zero. No handle or counter is available in the dummy object or the table entry.
- Because the handle is only in the caller's register and never stored where the stub can read it, a C++ counter value cannot be made generally correct. The robust next step is to neutralize the title's slow-wait branch for the common callback path.
- Next change: third temporary EXE patch at VA `0x144FB0B35` / file offset `0x4FAFF35`, replacing the six-byte `jb 0x144FB0CD4` (`0f 82 99 01 00 00`) with six `nop`s. With `r14==0` and `r12==0x14010A330`, execution then falls through to the success return at `0x144FB0B3B`. Use baseline DLL/source so the only new change is this branch bypass, with the two accepted EXE patches still applied.

### Run 254 result: direct wait-branch bypass rejected
- Temporary EXE change tried: zero-copy patch + hash-spin patch + `0x144FB0B35` `jb` -> six `nop`s. Patched EXE SHA-256 `723ddeecec20de2a4101dece6a60485bb4ab84bc8177330b3df46f369677eaca`; restored afterward to baseline `d2ef3b3e...`; backup unchanged. Baseline DLL/source used.
- Result: exit `124`; no `EXCEPTION_*`. Trace `259741` bytes / `6406` lines / `6403` normalized events, the same count as run241. One `sub_wait_value` call from caller `0x144FB0AEF` still ends the game-thread trace; no new device/render path appeared.
- The bypass did not unlock boot, so this branch is not the sole blocker. Third EXE patch rejected; do not retain it.
- Next change: reapply the run254 EXE state and use the run250 all-thread stack sampler to see whether the game thread is now waiting on a different operation after the bypassed branch.

### Run 255 result: the bypassed branch was not the active one
- Diagnostic reapplied: run250 all-thread/module-stack sampler (source `16eb8e...`, DLL `1efa63...`) with all three run254 EXE patches. EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. `2159` module-labeled `T,` lines across the same 28 TIDs; trigger thread `380` still had frames `0x14010A330` -> `0x144FB0C27` -> `0x144E4BAA9` and RIPs in `NtDelayExecution+0x14`.
- The stack proves the active wait enters through the callback path at `0x144FB0B4C`, not the direct-compare path at `0x144FB0B2D` whose `jb` was patched in run254. At `0x144FB0B6A`, a false comparison result falls through to the slow loop; making that `jne` unconditional would bypass this path.
- Next change: replace the run254 third patch with VA `0x144FB0B6A` / file offset `0x4FAFF6A`, `75 cf` (`jne 0x144FB0B3B`) -> `eb cf` (`jmp 0x144FB0B3B`). Keep the two accepted EXE patches and baseline DLL/source.

### Run 256 result: callback-branch bypass also rejected
- Temporary EXE change tried: zero-copy patch + hash-spin patch + `0x144FB0B6A` `75 cf` -> `eb cf`. Patched EXE SHA-256 `728ff8703c95791c8ab71472c68425e9206d19cb476c325cd604c0fd767f6bc3`; restored afterward to baseline `d2ef3b3e...`; backup unchanged. Baseline DLL/source used.
- Result: exit `124`; no new device/render path. Trace `260141` bytes / `6416` lines / `6413` normalized events, with six `sub_wait_value` calls from `0x144FB0AEF` and the same recurring `VCRUNTIME140` null-write storm on the unrelated worker thread.
- Neither wait-branch patch unlocks boot, so further EXE branch-patching here is not productive. Both third-patch experiments are rejected.
- New key observation: at `0x144FB0AEC` (`call *0x60(%rax)`), `RDX` still holds the original 64-bit handle set at `0x144FB0A56`; the COM slot receives `this` in `RCX` and can legally declare a second `RDX` argument. The compare uses `handle & ((1<<56)-1)`.
- Next change: change `sub_wait_value` to accept that second argument and set `g_wait_value.count` to `(handle & 0x00FFFFFFFFFFFFFFULL) + 0x1000ULL`, so `(counter - handle)` has bit 55 clear. Remove the diagnostic object dumps. Use only the two accepted EXE patches.

### Run 257 result: RDX-derived counter rejected
- Behavior change accepted: `sub_wait_value(void*, uint64_t handle)` set `g_wait_value.count = (handle & 0x00FFFFFFFFFFFFFFULL) + 0x1000ULL`. Source hash `59dbde1592c26721d63825eec75b763dd3374cd5b7f45d92dc8e88a4c11307b2`; DLL hash `d71487f4da3685d5a534551d01037ef5c09032902acdef83e2f8594449ac4996`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `259261` bytes / `6394` lines / `6391` normalized events with one cluster (`199526840`-`199528621`). Six `sub_wait_value` calls from `0x144FB0AEF` remain; no device/render/Present milestone appeared.
- The normalized 12-line trace tail is byte-identical to runs 254 and 256. Per the three-repeat rule this change is rejected and reverted; RDX is not being used as assumed, or the handle-aware counter is not the active fence.
- Next change: revert to baseline behavior and add one `W,` diagnostic that logs the second argument, the counter pointer, the counter value after assignment, and the caller return address from `sub_wait_value`, while leaving title behavior unchanged. This will confirm whether the COM slot receives the handle and which return address invokes it.

### Run 258 result: the COM slot's second argument is 2, not the handle
- Diagnostic change accepted: `W,arg=...,ptr=...,count=...,ret=...` logged from `sub_wait_value(void*, uint64_t second)` before the unchanged counter increment. Source hash `10bb0a0da92db9e4a86e675fe8a453acda5b251ebaf20ca488ca6ccd835367f7`; DLL hash `ca539a6fd0a0ad97955537a7c1b8e3b47ca6cb47701ba56aae65cf710ac9b738`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `259715` bytes / `6405` lines. One `W,` line: `arg=2`, `ptr=6ffffd836040`, `count=41200000`, `ret=144fb0aef`. The caller is the expected vtable call site, but `RDX` is a small flag (`2`), not the 64-bit handle.
- Conclusion: run257 failed because it generated a counter from the wrong register. The handle/fence must be recovered from another live register or a structure field before changing behavior again.
- Next change: keep baseline behavior and add one `W,` diagnostic in `sub_wait_value` that logs raw compile-time register values (`RBX`, `RDI`, `RSI`, `R12`, `R13`, `R14`, `R15`) via inline assembly, so we can find the register holding the fence at the COM call.

### Run 259 result: ordinary inline capture was too late
- Diagnostic change accepted: inline `asm volatile` register capture at the start of the C++ body of `sub_wait_value`, with unchanged counter behavior. Source hash `428ee54b97b582752a6855b0482f33dacae38f4293df296afe35e8fd895697b2`; DLL hash `e39e1d879d2c2c09399bcddeef95db6d585500d9f6b686d22cd242aa7c9ccf31`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. One `W,` line had `rsi=6ffffd844280`, equal to `self`; this was the compiler prologue copying `RCX` into `RSI` before the inline asm executed. Disassembly confirmed `push %rsi; push %rbx; sub $0xb8,%rsp; mov %rcx,%rsi` ahead of the capture.
- Next change: replace the body-level capture with a naked assembly entry point that stores all registers to globals before tail-jumping to the unchanged C++ body.

### Run 260 result: naked capture confirms the fence is 2
- Diagnostic change accepted: naked `sub_wait_value` stores all 16 GPRs to `g_wait_entry_regs` before tail-jumping to `sub_wait_value_impl`, which logs them with unchanged counter behavior. Source hash `ac9faad79fbcff0bbc5c776db6cc20bebf42a530cc799adaf028362f287cd81c`; DLL hash `6423273566045f64fd5dd6d4d96abeebc84c92da7caf4c8405c093aa252e7dcf`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. One `W,` line showed `arg=2`, `rsi=2`, `r8=6ffffd8401c0`, `r15=6f9efe88`. Thus the 56-bit masked fence in `RSI` is `2`, the COM argument in `RDX` is the same small flag, and `R15` points at the compare descriptor stack slot. The normalized trace tail still matches runs 254/256/257.
- With the baseline increment, `counter = 0x81200000`; `(counter - 2)` has bit 55 set, so both the direct `bt/jb` path and the callback `0x14010A330` path enter the slow loop. This explains the current stall, but run251's no-increment result must be rechecked because that counter should have satisfied the compare.
- Next change: keep the naked register capture and remove only the `g_wait_value.count += 0x80000000000000ULL` increment, with the initial `0x41200000` count. This will log the fence for every invocation on the path that previously failed and show whether a second, larger fence is involved.

### Run 261 result: all observed fences are satisfied, but the outer loop still waits
- Behavior change accepted: naked capture retained; `sub_wait_value_impl` returned the unchanged `0x41200000` counter. Source hash `1c004ca9575adbf602ccc6fa9b68d53fb22a2180c318dfcfd525a38f5fd9be4e`; DLL hash `d0041ecb41e1b54b505754b1b87e188111cdbbd0041338d07a67482d04d3aee5`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; trace `260765` bytes / `6410` lines. Six `W,` lines with fences `2,2,0,0,0,0`; for `count=0x41200000`, every `(counter - fence)` has bit 55 clear. The known `VCRUNTIME140` null-write storm returned (208 AV records).
- Conclusion: the low-level compare is satisfied, so the repeated calls are driven by a higher-level wait loop whose real condition is not changing. Further tuning of this counter cannot advance boot.
- Next change: inspect and disassemble the higher-level caller around game frames `0x144E4BAA9` and `0x144FB0A50` to identify what condition keeps re-entering the low-level wait, then choose one targeted stub response that changes that condition.

### Disassembly finding after run 261
- `0x144E4BAA9` is just a timestamp conversion, not the loop condition. `0x144FB0A50` is called from the multi-handle wait at `0x144FB0910` and from a queue flush at `0x142474740`.
- `0x144FB0910` reloads an aggregate counter, checks bit 55 of `(aggregate - fence)`, waits on one queued handle via `0x144FB0A50`, updates the aggregate with `lock cmpxchg`, and loops while bit 55 remains set. Its only back-edge is `0x144FB09D8` (`jne 0x144FB0979`).
- Next change: temporary third EXE patch at VA `0x144FB09D8` / file offset `0x4FAFDD8`, replacing `75 9f` (`jne`) with `90 90` (`nop`s). This bypasses the whole multi-handle wait, unlike the rejected low-level branch patches. Keep the two accepted EXE patches and baseline DLL/source.

### Run 262 result: outer multi-handle wait back-edge bypass rejected
- Temporary EXE change tried: zero-copy patch + hash-spin patch + `0x144FB09D8` `75 9f` -> `90 90`. Patched EXE SHA-256 `5a83fedc00e7464bb84498f91505bff43987f84331811e0964f8712a30c2f615`; restored afterward to baseline `d2ef3b3e...`; backup unchanged. Baseline DLL/source used.
- Result: exit `124`; no `EXCEPTION_*`. Trace `258861` bytes / `6384` lines / `6381` normalized events. Event counts are unchanged (`620` `device_out_arg6` at `0x1417F6D3B`, `620` `dummy_ok` at `0x1417F6D4B`, `432` at `0x14161D0A3`); the normalized tail still matches runs 254/256/257/261.
- Bypassing the multi-handle wait back-edge does not advance boot, so all three wait-branch/loop EXE patches are rejected. The repeated stub events are not themselves the blocker.
- Next change: re-run the run250 all-thread stack sampler with the run261 no-increment counter. This will show whether the satisfied low-level compare lets the game thread leave `0x144FB0C27`, or whether a different wait is now active.

### Run 263 result: no-increment moves the game thread to a different wait
- Behavior/diagnostic change accepted: run250 stack sampler retained; `sub_wait_value` returned the unchanged `0x41200000` counter. Source hash `c845676906d41fa4c3880a3a491b73e68154f67addfe3b221f33a38c8779519c`; DLL hash `978a12202385064aa0615c6fee0dba67dc799f34ea84950e3e3f757f49b9dd3f`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no boot milestone. Trace `9116` lines / `6453` normalized events. The sampler's trigger thread 380 changed stacks: rounds 0-1 no longer contain `0x14010A330`/`0x144FB0C27`; they show `0x1423CF274`, `0x1423CF104`, `0x1423CEFD2` and `ntdll+0xEB54` (`NtWaitForSingleObject`). Rounds 2-3 still contain `0x14010A330`, but reached from `0x1443C46AB` rather than the old `0x144E4BAA9`.
- Disassembly: `0x1423CEFE0` calls vtable slot `0x60` on its object, then loops on `WaitForSingleObject(handle=[obj+0x28], 0)` at `0x145441BE0`; the imported IAT slot `0x146793B20` is `WaitForSingleObject`. The thread argument is global `0x14A210CB8`.
- Conclusion: with the low-level compare satisfied, the active blocker becomes a second wait loop whose condition is a real Win32 handle, not the counter. Further counter edits are not useful.
- Next change: keep the no-increment counter and augment `sub_wait_value` with a `W,` diagnostic that logs `self`, caller return address, eight qwords at `self`, and eight qwords at global `0x14A210CB8`. This will confirm whether slot `0x60` at `0x1423CF007` reaches this stub and reveal the handle/layout of the second wait object.

### Run 264 result: the second wait is entirely game-side
- Diagnostic change accepted: run263 sampler/no-increment retained; `sub_wait_value` logged `self`, caller, eight object qwords, and eight qwords at global `0x14A210CB8`. Source hash `599d5436f11407086fddfd06cad57120de8653030b91a2077bed91a6eb4e8b67`; DLL hash `f322fc96f4958e25b4cbb4ac60f12886cacf83e22b2dc7ec86d86a02453d3cd5`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; six `W,` lines, all `tid=380`, `self=6ffffd8352a0`, `ret=144fb0aef`. No call from `0x1423CF00A`, so the second wait loop's `call *0x60(%rsi)` is a game-side vtable, not our stub.
- Object dump: `self` qwords are `{vtable=6ffffd831160, 0,...}`; global `0x14A210CB8` qwords are `{1461ce318, 1468ba6a0, 1461cfd40, 0, 0, 5b4, 5b8, 5bc}`. The loop at `0x1423CEFE0` polls `WaitForSingleObject(0x5B4, 0)`, matching the already-documented phase barrier. This is not a new stub fix target.
- Next change: pivot away from counter/wait tuning. Because run249 proved a thread can block inside `shim_trace_line` on `shim_trace_lock`, test whether removing profiler instrumentation from the hot `sub_wait_value` path removes tracer-induced serialization. Keep baseline counter behavior and both accepted EXE patches; add only `__attribute__((no_instrument_function))` to `sub_wait_value`.

### Run 265 result: tracer instrumentation on `sub_wait_value` is not the blocker
- Diagnostic change accepted: baseline source with only `__attribute__((no_instrument_function))` added to `sub_wait_value`; baseline counter behavior and both accepted EXE patches. Source hash `5621179d2081593a5d2bf50d97237d064d2aebcea49c276f466a313971939bef`; DLL hash `4b0462fef1d11cb72d69d909a3e01a8fb86a062608947fe19ba53a956e54ac1d`. EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_*`. Trace `6404` lines / `6401` normalized events, with the same top callers and the same normalized tail. The six `sub_wait_value` events disappeared (as intended), but no new render/Present path appeared.
- Conclusion: profiler mutex contention in this function is not the stall. The no-instrumentation change is rejected and reverted.
- Next change: keep the no-increment counter and add one targeted behavior change in `sub_wait_value`: read the handle at global `0x14A210CB8+0x28` (observed as `0x5B4`) and call `ReleaseSemaphore` once per invocation. This directly tests whether the phase barrier is waiting for a permit that the stub can supply.

### Run 266 result: phase-semaphore release unlocks Xbox D3D12 device creation
- Behavior/diagnostic change accepted: no-increment counter retained; the all-thread sampler was retained; `sub_wait_value` additionally read the handle at `0x14A210CB8+0x28` and called `ReleaseSemaphore(handle, 1, nullptr)` once per invocation. Source hash `e8c40ad20774028baa41230c63c7955a60e48a5644c67a2baa9398fd5379f624`; DLL hash `6377aa6e5c3f0a6d0e4ab6980abd331d54de7ef4ddb1367a2854518751a33bdb`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION` or other `EXCEPTION_*` records. Trace `9092` lines / `6847` normalized events.
- New forward milestone: after the semaphore release, TID `380` called `XsD3D12XboxCreateDevice` at RVA `0x2f10` from return address `0x1424785BC`. This is the first run that reaches title-side D3D12 device creation.
- New activity includes repeated `device_out_rdx` (RVA `0x3821`) at caller `0x1417F6D3B`, `dummy_ok` (RVA `0x3B8B`) at caller `0x1417F6D4B`, `device_out_r9` (RVA `0x3A65`) at `0x14161D0A3`, plus device/subobject vtable calls from `0x142478713`-`0x142478CC5`. The dominant tail is a tight 620-iteration alternation between `0x1417F6D3B` and `0x1417F6D4B`; no `Present`, menu, or logo appeared before the timeout.
- The phase-semaphore release is retained as the first confirmed forward change. The remaining stall is a graphics-init response/loop, not another wait on the early phase barrier.
- Next change: repeat the confirmed behavior once with the all-thread sampler disabled, so the normalized tail is clean. Keep no-increment plus semaphore release and change only the sampler activation call in `device_format_info`.

### Run 267 result: device-creation path is stable without the sampler
- Single source change from run266: removed the `maybe_start_thread_sampler()` call from `device_format_info`. No other source behavior changed. Final source hash `a232711cb1f16c7cbcb32750d35bca230de07a3584f60c266865ab906188644d`; DLL hash `a7d62b316ce3f3be102e4e41f1a93a6525c46fe2b1b546e1bf17031a1b0b7756` (compiled with `-shared -std=c++17 -g -Os -finstrument-functions -static-libgcc -static-libstdc++` and `d3d12_xs.def`). EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Setup note: two initial launch attempts were invalid and are not behavioral results. The first DLL was compiled without static libgcc/libstdc++, so `d3d12_xs.dll` failed to load; the second omitted `d3d12_xs.def`, so `D3D12ConfigureVirtualMemoryX` was an unimplemented Wine stub. Both were rebuilt before the counted run.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION` and no `EXCEPTION_WINE_STUB`. Trace `6810` lines / `6807` normalized events, one contiguous timestamp cluster (`201528846`-`201531917`).
- The forward milestone reproduces: `XsD3D12XboxCreateDevice` is called at RVA `0x1FBA` from `0x1424785BC` (the RVA shifted from run266's `0x2F10` because of the corrected compile command). The same post-device loop is present: `625` `device_out_arg6` calls at `0x1417F6D3B`, `620` `dummy_ok` calls at `0x1417F6D4B`, and `452` `device_out_r9` calls at `0x14161D0A3`. No sampler lines obscure the tail, which ends in module/unload activity with no `Present`, menu, or logo.
- Conclusion: the no-increment counter plus phase-semaphore release is reproducible and stable. The remaining blocker is after D3D12 device creation, in the graphics-init path around the `0x1417F6BA0` resource-enumeration helper and its caller.
- Next change: move the all-thread sampler trigger from the 20th `device_format_info` call to the first `XsD3D12XboxCreateDevice` call, and sample at later rounds (1 s, 3 s, 6 s) so it captures the RIP and stacks after device creation rather than before it.

### Run 268 result: post-device blocker is the same phase wait, not a game-side spin
- Single source change from run267: sampler activation moved to the first `XsD3D12XboxCreateDevice` call, with cumulative sample rounds at 1 s, 3 s, and 6 s. Source hash `4a43fbe1d88cda768ab6e3c612e1e9f883593c6f3d9b0d24a9decc7dc57e930e`; DLL hash `41c59493580745cf4b4627c791787552a5c6425406a63f6d8b4f61d917763ef4`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `8481` lines. Device creation is reached at logged RVA `0x2F13` from `0x1424785BC`.
- Sampler: 65 `V,` lines across rounds 0-2. No game-module RIP was captured for the device thread `380`; every sampled RIP was `ntdll+0xEA94` (`NtWaitForSingleObject+0x14`) or `ntdll+0xF094` (`NtDelayExecution+0x14`). This rules out a user-space spin on the sampled threads.
- Stack words identify TID `300` as the relevant waiter: `slot=2` is `0x145441BEC` (the `WaitForSingleObject` return site) in rounds 0-2, with game frames including `0x1423CEF51`/`0x1423CAAA8`. This is the same `0x1423CEFE0` phase wait loop documented in run263/264, polling `[0x14A210CB8+0x28]`.
- Conclusion: one `ReleaseSemaphore` per `sub_wait_value` call is enough to reach device creation but does not keep the phase barrier satisfied afterward. The post-device stall is a real Win32 wait on that phase handle, not an unresolved stub response in the D3D12 loop.
- Next change: in `release_phase_semaphore`, create one manual-reset event in the signalled state and overwrite the global handle at `0x14A210CB8+0x28` with it. This makes the phase wait permanently satisfied without further EXE patching; keep no-increment counter and all other run268 behavior unchanged.

### Run 269 result: overwriting the phase handle is rejected
- Single source change from run268: after the existing one-shot release, `release_phase_semaphore` created a signalled manual-reset event and wrote its handle into global slot `0x14A210CB8+0x28`. Source hash `e0e1e620cdef4c65153611d575d144600041c42bcef35c4914581d72b489ff28`; DLL hash `e3e4d8d31c8dcb48cbc54080380aa7c8f90d4ff3fe87e4dc8a02aafbf4426f2f`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- First launch result: exit `5`, `EXCEPTION_ACCESS_VIOLATION` at `game_scarlett_master.exe+0x16401D` (null vtable dereference in `0x140164010`), backtrace `0x1443C3220` -> `0x1423CF135` -> `0x142215ECA`. Trace `6773` lines / `6711` normalized events. Only two sampler `V,` lines were emitted before the crash.
- A repeat launch was run to rule out timing: exit `124`, no `EXCEPTION_*`. Trace `9092` lines / `6847` normalized events, matching run266's event count. Device creation still occurred from `0x1424785BC`, but the same post-device loop and phase wait remain. The sampler emitted `87` `V,` lines and `2064` `T,` lines; TID `300` still has `0x145441BEC` (`WaitForSingleObject` return site) and frames through `0x1423CEF51`/`0x1423CAAA8`.
- Conclusion: replacing the handle slot does not keep the barrier satisfied (the game likely reloads/replaces the handle after the write), and it can destabilize vtable dispatch. This change is rejected.
- Next change: return to run268's source (no event overwrite) and add a heartbeat thread that re-reads `0x14A210CB8+0x28` every 10 ms and calls `ReleaseSemaphore(handle, 1, nullptr)` while boot is active. Keep no-increment counter, one-shot release, and post-device sampler unchanged.

### Run 270 result: semaphore heartbeat is rejected; null-vtable call exposed
- Single source change from run268: added a one-shot background thread that re-reads global slot `0x14A210CB8+0x28` every 10 ms and calls `ReleaseSemaphore(handle, 1, nullptr)` for up to 12 s. Source hash `008603cb340403b948c542f654f5f94f16016532e8fdcf2e7a97eaf26d157bd7`; DLL hash `5d7164c09212a614eb3994c49f474ceb15a6fb5f04041c6b26d0434fcb99835a`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Launch results: the first counted run exited `5` with the known `VCRUNTIME140+0x1989` null write (`6163` trace lines / `6125` normalized events). One repeat never started the game (exit `124`, `11`-line Proton log, no `trace.log`) and is invalid. The next valid run exited `5` at `game_scarlett_master.exe+0x16401D` (null vtable dereference, `6813` trace lines / `6731` events). The final valid run exited `124` but logged the same `+0x16401D` exception as caught/handled by the title (`6802` trace lines / `6731` events).
- All valid runs reached `XsD3D12XboxCreateDevice` from `0x1424785BC`, but none produced `Present`, a menu, or the Rockstar logo. The heartbeat did not release the post-device bottleneck; instead it reliably exposed a null-vtable virtual call at `0x140164010` when the title iterates its object array.
- Disassembly: `0x140163E40` iterates `[rsi+0x10]` with count `[rsi+0x18]`; at `0x140163ED8` it loads each object's vtable pointer into `RCX` and calls `0x140164010`, which dereferences `RCX` and jumps through `*RCX`. One entry has a null vtable pointer.
- Next change: temporary third EXE patch at VA `0x140164010` (file offset `0x163410`) to guard the vtable dereference: return immediately when `[object]` is null, otherwise preserve `EDX=1` and perform the original virtual jump. Keep run268 DLL/source (no heartbeat) and the two accepted EXE patches.

### Run 271 result: null-vtable guard is stable but does not advance
- Temporary EXE change tried: two accepted patches plus a guard at VA `0x140164010` / file offset `0x163410`, replacing the 32-byte vtable-dispatch stub so a null `[object]` returns immediately while valid objects keep `EDX=1` and jump through the vtable. Patched EXE SHA-256 `1a0a38ea2e033b1eb4f81291135407b882203f0cf8975cde7941a4b75c60cec4`; restored afterward to baseline `d2ef3b3e...`, backup unchanged. DLL/source were the exact run268 build (`41c59493580745cf4b4627c791787552a5c6425406a63f6d8b4f61d917763ef4` / `4a43fbe1d88cda768ab6e3c612e1e9f883593c6f3d9b0d24a9decc7dc57e930e`) and were restored to baseline `60b46e...`/`dc77afec...`.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `7927` lines / `6827` normalized events. `XsD3D12XboxCreateDevice` is reached from `0x1424785BC`; the same 873/625/452/163 device-loop event counts repeat.
- Sampler: `43` `V,` lines over rounds 0-1. TID `300` remains in the `0x1423CEFE0` wait loop with `0x145441BEC` at stack slot 2 and frames through `0x1423CEF51`/`0x1423CAAA8`. No `Present`, menu, or logo.
- Conclusion: the guard removes the crash but not the blocker. The phase wait is still the active gate after device creation.
- Next change: temporary EXE patch at VA `0x1423CF0F8` / file offset `0x23CE4F8`, replacing `test al,al; jne 0x1423CF106` (`84 c0 75 0a`) with `mov al,1; nop; nop` (`b0 01 90 90`). This forces the `WaitForSingleObject` wrapper result true so the title exits the phase loop, without further source or handle changes.

### Run 272 result: single wait-site bypass is not enough
- Temporary EXE change tried: two accepted patches plus VA `0x1423CF0F8` / file offset `0x23CE4F8` `84 c0 75 0a` -> `b0 01 90 90`, forcing the `0x1423CEFE0` phase loop to treat the wrapper result as signalled. Patched EXE SHA-256 `eda3b7765c37a127ae9575815659806fac827863cda2c442162106f44de6aa36`; restored afterward to baseline `d2ef3b3e...`, backup unchanged. DLL/source were the exact run268 build (`41c59493...` / `4a43fbe1...`) and were restored to baseline `60b46e...`/`dc77afec...`.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `7048` lines / `6445` normalized events. Device creation is reached from `0x1424785BC`, now on TID `384` instead of `380`, with the same device/subobject loop (841/625/452/163 events).
- Sampler: `23` `V,` lines in round 0 only. TID `384` is in `ntdll+0xEE14` (a wait syscall), and no game RIP or render/Present milestone was captured. The bypass moved the waiting thread but did not advance initialization.
- Disassembly finding: the wrapper at `0x145441BE0` is `WaitForSingleObject(handle, 0) == 0`, and there are nine callers in the EXE. Bypassing only the `0x1423CEFE0` site leaves other callers blocked.
- Next change: temporary EXE patch at VA `0x145441BE0` / file offset `0x5440FE0`, replacing the wrapper entry with `mov al,1; ret` (`b0 01 c3`). This makes every zero-timeout wrapper call report signalled, without altering the handle or source stubs.

### Run 273 result: global wait-wrapper patch rejected
- Temporary EXE change tried: two accepted patches plus VA `0x145441BE0` / file offset `0x5440FE0` `48 83 ec 28` -> `b0 01 c3`, making all nine zero-timeout `WaitForSingleObject` wrapper calls report signalled. Patched EXE SHA-256 `560f112f35b81c4ba89d9b79776ae0636de6a26fda0592451af70c5540dcfc04`; restored afterward to baseline `d2ef3b3e...`, backup unchanged. DLL/source were the exact run268 build and were restored to baseline `60b46e...`/`dc77afec...`.
- Result: exit `5`. The process faulted at `0x6FFFFFF7EE0B` in Wine's runtime with an all-zero unwind backtrace, before any `d3d12_xs.dll` load and before `trace.log` was created. This launch is invalid as a behavioral trace and the patch is rejected as destabilizing early startup.
- Conclusion: neither one-site nor global wait-result bypass is viable. The remaining stall must be addressed through the stub responses used after `XsD3D12XboxCreateDevice`.
- Next change: inspect the exact D3D12/Xbox stub exports and their current outputs, then change one graphics-init response that still returns null or an all-zero structure.

### Run 274 result: shared Xbox dummy object is not the null-vtable source
- Single source change from run268: `xbox_ok()` returned a dedicated `g_xbox_object` with a populated `0x400`-entry vtable instead of the shared zeroed `dummy_pointer()` buffer. Source hash `a5567f87c8fde3e7ee8136a861379f9d5d214fcd6e89b0f75ebd252adc430363`; DLL hash `a46b9ec56e51d4ca70694aaec498633002116aa1ebb342063ad04b529be3922a`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward from the true pre-run274 snapshots to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `5`; the same null-vtable fault at `game_scarlett_master.exe+0x16401D` (`0x1443C3220` -> `0x1423CF135` -> `0x142215ECA`). Trace `6800` lines. Device creation was reached, so the change did not affect the failing object.
- Conclusion: the null vtable belongs to a game-side or device-vtable object, not the shared Xbox create outputs.

### Run 275 result: phase handle is a semaphore
- Diagnostic source change from run268: the post-device sampler now emits one `Q,round,handle,status,type` line by calling `NtQueryObject(ObjectTypeInformation)` on global slot `0x14A210CB8+0x28`. Source hash `7b57e9888b0f78fdd137d76fe39c950b5f43ff3e188185f69cea37554a9976fa`; DLL hash `523ddad65659a0deb87f229e992aeeea544b368923447601c91aee6b7ab0da14`. EXE was carried over from run274 with only the two accepted patches (`18587ece...`; no new EXE change) and restored afterward to baseline `d2ef3b3e...`, backup unchanged. DLL/source restored from the true pre-run274 snapshots to baseline.
- Result: exit `5` at the same `game_scarlett_master.exe+0x16401D` null-vtable fault. Trace `6828` lines. The single `Q,` line is `round=0,handle=5b4,status=0,type=Semaphore`.
- Conclusion: the phase handle is a real semaphore. The one-shot permit from run266 is consumed, and run270's unconditional 10 ms releases overfilled it and likely caused the caught null-vtable storm. The correct behavior is to release only when the semaphore count is zero.
- Next change: revise the heartbeat from run270 so it calls `NtQuerySemaphore(handle, 1, ...)` first and calls `ReleaseSemaphore(handle, 1, nullptr)` only when `CurrentCount == 0`. Keep run268's no-increment counter, one-shot release, and sampler unchanged otherwise.

### Run 276 result: count-aware heartbeat is stable but still blocked
- Single source change from run268: added a background heartbeat that re-reads the phase semaphore every 10 ms and releases only when `NtQuerySemaphore(SemaphoreBasicInformation).CurrentCount == 0`. The original one-shot release on each `sub_wait_value` call and the post-device sampler were left unchanged. Source hash `ae42bbb730079cb027921a66c47244cf6231aeebc74af718659b396e46705a3d`; DLL hash `a535fd9478ffbabacdaf0d640413605c2145e23f6bbb24f23ee926e11f4f252d`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `7972` lines / `6847` normalized events. `XsD3D12XboxCreateDevice` is reached from `0x1424785BC`, with the same 873/625/452/163 device-loop event counts. No `Present`, menu, or logo.
- Sampler: `44` `V,` lines over rounds 0-1. TID `300` still has the `0x145441BEC` wait-return frame and `0x1423CEF51`/`0x1423CAAA8` callers. Its sampled RIP moved to `ntdll+0xF094` (`NtDelayExecution`), but this is only a sleep inside the blocked loop, not forward progress.
- Conclusion: a heartbeat that tops up an empty semaphore neither crashes nor advances the title. The remaining unconditional release inside `sub_wait_value` can still overfill the semaphore, so the next change makes every phase release count-aware.
- Next change: replace the unconditional `ReleaseSemaphore` in `release_phase_semaphore()` with the same `NtQuerySemaphore` guard used by the heartbeat, so no phase release can raise `CurrentCount` above 1. Keep the heartbeat and all other run268 behavior unchanged.

### Run 277 result: all releases count-aware is rejected
- Single source change from run276: factored phase release into `top_up_phase_semaphore()` and used it for both the heartbeat and each `sub_wait_value` call, so no release can raise the semaphore count above 1. Source hash `a6a7746a6f0fd6ccda2d9aa8af94efdd5fccb43e1282537e687812494ad6478c`; DLL hash `f65a557a4a150ddb4a7fc8fd1d645c2b782d1cabb7cc4d1bd6d3a647a4d69253`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Result: exit `5` with the known `VCRUNTIME140+0x1989` null write. Backtrace: `0x145861B54` -> `0x1443C38FD` -> `0x1423CF4F2` -> `0x1423CF2D3` -> `0x1423CF104`. Trace `6213` lines / `6125` normalized events; device creation was reached, but only three sampler `V,` lines were emitted before the fault. No forward milestone.
- Disassembly finding: `0x1423CEED0` starts the worker thread whose routine eventually allocates global object `0x149C3B740` via `0x1422406F0`; the main loop at `0x1423CEFE0` waits on that worker and then calls vtable slot 0 of that global. The null-vtable crash is therefore downstream of a real worker-ready barrier, not merely a semaphore count problem.
- Conclusion: changing semaphore counts alone is unstable. The next step is diagnostic: observe the global object pointer and its vtable while the title is stalled.
- Next change: add one sampler line per round from run268 that logs global `0x149C3B740`, its first vtable word, and the phase semaphore count. No behavioral change.

### Run 278 result: object-state diagnostic is timing-sensitive
- Diagnostic source change from run268: added one `G,round,ptr,vtable,phase,count` sampler line using `NtQuerySemaphore` and a read of global `0x149C3B740`. Source hash `4e92bac3bffc87bdc7e3749ff231002dbab46015fa56e10d231b70289d6e0317`; DLL hash `35ebf12188ba321e7ab3036abcc45fb9f2a101f78086a5ec6e8df266f351d991`. EXE used only the two accepted patches (`18587ece...` while running); EXE/DLL/source restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- First launch: exit `5` at `VCRUNTIME140+0x1989` with backtrace `0x145861B54` -> `0x1443C38FD` -> `0x1423CF4F2` -> `0x1423CF2D3` -> `0x1423CF104`; trace `6156` lines / `6125` events. The one `G,` line showed `ptr=34a4350,vtable=14641ce20,phase=5b4`, i.e. the worker object was constructed with the constructor's expected vtable (`0x145BFA544` sets exactly `0x14641CE20`).
- Repeat launch: exit `5` at the known `game_scarlett_master.exe+0x16401D` null-vtable fault; trace `6801` lines / `6711` events. Its `G,` line showed `ptr=0,vtable=0,phase=5b4`, so the worker object had not been constructed in that timing window.
- The `count` field is invalid (`4294967295`) because the diagnostic queried `NtQuerySemaphore` with information class 1; the correct class for `SEMAPHORE_BASIC_INFORMATION` is 0. No forward movement in either launch.
- Conclusion: the global object is not consistently ready when the title proceeds, and the crash path alternates between a failed large allocation/memset and null-vtable dispatch. The phase barrier is still the gate. Next change targets allocation reliability in `xmem.dll`: add a process-heap fallback when `VirtualAlloc` cannot satisfy `XMemAlloc`.
- Next change: in `stub_dlls_src/xmem.cpp::title_alloc`, fall back to `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)` if `VirtualAlloc` returns null. Keep the size rounding and all other stub behavior unchanged.

### Run 279 result: xmem allocation fallback does not advance
- Single source change from run268: `xmem.cpp::title_alloc` gained a `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)` fallback when `VirtualAlloc` returns null. Source hash `22f1eaa6cf671f46e2b7e4f639753f248246271207ffe003b7f96428cfc367df`; DLL hash `a7e5e47978fa2498a659b369f12b6fba6467ee345ea4ce455723ecd0ecd3fcd6`. The run used the exact run268 d3d12_xs build (`41c59493...` / `4a43fbe1...`) and only the two accepted EXE patches (`18587ece...`). All artifacts restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`/`670b1501...`/`19b2e996...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `8410` lines / `6827` normalized events. Device creation is reached from `0x1424785BC` with the usual 873/625/452/163 event counts; no `Present`, menu, or logo.
- Sampler: `66` `V,` lines over rounds 0-2. TID `300` still has `0x145441BEC` and `0x1423CAA54`/`0x1423CAAA8` frames, so the phase wait is unchanged. The allocation fallback did not affect the blocking path.
- Conclusion: title allocations were not the cause of the stall or the intermittent null-write crash. Next step is to identify which game code is supposed to release the phase semaphore by tracing the EXE's `ReleaseSemaphore` calls.
- Next change: temporary `xmem.cpp` diagnostic that patches the EXE IAT slot at RVA `0x67968B4` (`ReleaseSemaphore`) with a logging shim which records `S,tid,handle,count,caller` and forwards to the original function. Keep run268 d3d12_xs and the two accepted EXE patches.

### Run 280 result: ReleaseSemaphore diagnostic used the wrong address
- Single source change from run279 baseline: `xmem.cpp` added a `ReleaseSemaphore` logging shim (`S,tid,handle,count,caller`) and patched the EXE IAT slot at RVA `0x67968B4`. Source hash `a7893eddb6bf3a8893ac9673517e28ccea9a30585ec82d58ca12282ec6f1e5d2`; DLL hash `fcab867e48e8ab1a91c3acd0bcef9dfa0657bce6de1b1ee309a6837d11b75fa6`. EXE used only the two accepted patches (`18587ece...` while running); all artifacts restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`/`670b1501...`/`19b2e996...`, backup unchanged.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `9092` lines / `6847` normalized events. Device creation was reached from `0x1424785BC`; no `Present`, menu, or logo. Sampler emitted `87` `V,` lines over rounds 0-3; TID `300` still waits at `0x145441BEC` with `0x1423CAAA8` frames. No forward movement.
- The trace contains zero `S,` lines. PE import inspection shows `0x67968B4` is the hint/name string RVA for `ReleaseSemaphore`, not the IAT slot. The correct slot is `api-ms-win-core-synch-l1-1-0.dll` FirstThunk `0x6793AA8` plus index `12` * 8, i.e. RVA `0x6793B08`. The diagnostic was therefore inactive and the test is inconclusive.
- Conclusion: the phase-release path remains unobserved. Next change corrects the IAT RVA to `0x6793B08` and reruns the same diagnostic.
- Next change: change only `kReleaseSemaphoreIatRva` in the run280 `xmem.cpp` diagnostic from `0x67968B4` to `0x6793B08`, rebuild, and rerun.

### Run 281 result: corrected ReleaseSemaphore IAT still sees no EXE releases
- Single source change from run280: `kReleaseSemaphoreIatRva` corrected to `0x6793B08`, confirmed by full PE import parsing (`api-ms-win-core-synch-l1-1-0.dll`, FirstThunk `0x6793AA8`, index 12). Source hash `7350bc0e366a02a431f852cb1b8d0f8d7c55bb7264d5fcdbd7aee170a8072155`; DLL hash `36a1a244cd1f9a634cb28ff7db3fac0e03bede0db26607e4acc94ec4133fb093`. EXE used only the two accepted patches (`18587ece...` while running); all artifacts restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`/`670b1501...`/`19b2e996...`, backup unchanged.
- First launch produced no `trace.log` at all and only the Proton header in the Steam log; it was treated as a transient launcher failure and repeated.
- Repeat launch: exit `124` with `EXCEPTION_ACCESS_VIOLATION` at `game_scarlett_master.exe+0x16401D` (null-vtable dispatch). Trace `5143` lines / `5089` normalized events. Device creation was reached from `0x1424785BC`; sampler emitted only round 0 with TID `300` still waiting at `0x145441BEC`/`0x1423CEF51`. No forward movement.
- The trace still has zero `S,` lines, so the EXE does not call `ReleaseSemaphore` through its IAT before the worker-ready barrier. The phase release must originate in a stub DLL or through a direct ntdll call that the IAT hook cannot see.
- Conclusion: EXE IAT hooking is the wrong observation point. Next diagnostic uses Wine's `+sync` channel on the unmodified baseline build to observe semaphore waits/releases at the syscall layer.
- Next change: no code change; rerun the baseline build with `WINEDEBUG` extended by `+sync` and inspect `NtReleaseSemaphore`/`NtWaitForSingleObject` records for handle `0x5B4`.

### Run 282 result: +sync reveals the real post-device blocker
- No code change from run281. Run used the exact run268 d3d12_xs build (`41c59493...` / `4a43fbe1...`) and only the two accepted EXE patches (`18587ece...` while running); all artifacts restored afterward to baseline `d2ef3b3e...`/`60b46e...`/`dc77afec...`, backup unchanged.
- Several initial launches produced no `trace.log` because stale Wine device processes held the prefix; cleanup with `WINEPREFIX=/tmp/opencode/proton-stub-test/pfx wineserver -k` plus removal of leftover `winedevice.exe` processes restored normal startup. The valid run used `WINEDEBUG='+timestamp,+pid,+tid,+seh,+file,+sync'`.
- Result: exit `124`; no `EXCEPTION_ACCESS_VIOLATION`. Trace `8021` lines / `6805` normalized events. Device creation was reached from `0x1424785BC`; no `Present`, menu, or logo. No forward milestone.
- `+sync` shows `NtReleaseSemaphore handle 0x5c0` twice from TID `380` (`0x17c`) at `205028.832` and `205028.885`. TID `300` (`0x12c`) polls `0x5c0` with zero timeout and one of those waits returns `0` at `205028.885`, so the earlier phase barrier is satisfied. TID `300` then resumes and releases `0x848` and `0x5bc`, but continues polling `0x5c0` with `WAIT_TIMEOUT` afterward.
- New blocker: TID `400` (`0x190`) creates an auto-reset event (`NtCreateEvent type 1, state 0`) at `205028.591` and then spins forever on `NtWaitForSingleObject handle 0x854` with a negative timeout. No `NtSetEvent`, `NtReleaseSemaphore`, or any other signal for `0x854` appears in the log.
- The run268 sampler enumerated 21 threads but produced `V,` lines for only 2, indicating `SuspendThread` is deadlocking partway through the thread list.
- Conclusion: the phase semaphore is not the remaining blocker; an unsignaled auto-reset event on TID `400` is. Next change removes thread suspension from the sampler so every thread's RIP/stack is logged safely.
- Next change: in the run268 `d3d12_xs.cpp` sampler, replace the `SuspendThread`/`ResumeThread` pair with a direct `GetThreadContext` call, keeping logging strictly after the context read and never while a thread is suspended.

### Run 283 result: non-suspending sampler confirms TID 400 is blocked on an unsignaled event
- Single source change from the exact run268 build: in `d3d12_xs.cpp::sample_round`, the sampler's `SuspendThread`/`ResumeThread` pair was replaced with a direct `GetThreadContext` call. All `V/`, `U/`, `T/`, `R/` and `G/` logging occurs after the context read and never while a sampled thread is suspended. Source hash `ae074d042b8d23956dcf969a8fef7f3467e780b36b1eb1ed6af5d2b652892b2e`; DLL hash `b4b3927f4aa6b4d4ad955ca9b4e63827c8cb836268a0355c6da2e3db10e79bb5`. EXE used only the two accepted patches (`18587ece5326ad307edd65847423b625ede989356717b7b3322de69a06036156` while running).
- Preserved pre-run snapshots: EXE `/tmp/opencode/exe_before_run283.bin` (`d2ef3b3e...`), DLL `/tmp/opencode/d3d12_xs_before_run283.dll` (`60b46e36...`), source `/tmp/opencode/d3d12_xs_source_before_run283.cpp` (`dc77afec...`). Backup remained unchanged (`e95020b2...`).
- The first two launches were transient launcher failures: `run283.log` and `run283b.log` are only 730-byte Proton headers with no `trace.log` and are invalid as behavioral results. `run283c` was valid but unstable: exit `5`, `EXCEPTION_ACCESS_VIOLATION` at `game_scarlett_master.exe+0x16401D`, trace `6772` lines / `6712` normalized events. Device creation was still reached from `0x1424785BC`.
- Counted valid run `run283d`: exit `124`; no `EXCEPTION_*` records. Trace `7576` lines / `6827` normalized events in one contiguous timestamp cluster (`205390685`-`205392856`). `XsD3D12XboxCreateDevice` is reached from `0x1424785BC`; no `Present`, menu, or Rockstar logo.
- New sampler result: `29` `V,` lines, `45` `U,` lines and `670` `T,` lines; round 0 sampled `23` threads and round 1 sampled `6`. TID `400` (`0x190`) is sampled in `ntdll+0xEA94` (`NtWaitForSingleObject+0x14`). Its stack contains `slot=0` `0x6fffffc6234f` in `kernelbase.dll`, `slot=10` `0x14022511f` in the EXE (the `WaitForSingleObject` return site), and `slot=16` `0x142a8bc5c` in the EXE.
- Disassembly finding: `0x142a8bc48` loads global `0x147bc7f68`, sets `edx=1`, calls `0x140225110`, then tests `al`. `0x140225110` calls IAT slot `0x146793B20` and returns `WaitForSingleObject(handle,1) == 0`. The global's write site is `0x142a8b006` inside `0x142a8afe0`. The likely signal path is `0x142a8a990`, which calls `0x144daa220` with that handle and `edx=1`; `0x144daa220` jumps to `0x140007330`.
- Conclusion: removing sampler suspension did not yield forward movement; the blocker remains the auto-reset event wait on TID `400`. Next change is diagnostic only: log global `0x147bc7f68` during sampling and rerun with `+sync` to confirm it is Wine handle `0x854` and whether any `NtSetEvent`/`NtPulseEvent`/`NtReleaseSemaphore` signal for that handle executes.

### Run 284 result: the blocked event is the global at 0x147bc7f68, still unsignaled
- Single diagnostic source change from the exact run283 no-suspend build: `sample_round` now emits one `J,round,handle` line from global `0x147bc7f68` using `ReadProcessMemory` before thread sampling. No thread is suspended by the sampler and all logging still uses `WriteFile` plus `shim_trace_lock`. Source hash `825d6c83f01a601d871c052a9678b8af1583d64ac831d019e63f03b027a63296`; DLL hash `3543af7f2a62c7a3c31b2ea2e076e8da9ebbf34973e5deac2e457ea6bf28b73f`. EXE used only the two accepted patches (`18587ece...` while running). Snapshots: `/tmp/opencode/exe_before_run284.bin` (`d2ef3b3e...`), `/tmp/opencode/d3d12_xs_before_run284.dll` (`60b46e36...`), `/tmp/opencode/d3d12_xs_source_before_run284.cpp` (`dc77afec...`). Backup remained unchanged (`e95020b2...`).
- Run284 first launch: exit `5` at the known `game_scarlett_master.exe+0x16401D` null-vtable fault; trace `6830` lines / `6711` normalized events. Its only `J,` line was `round=0,handle=0`, i.e. the sampler fired before the global was initialized. No forward milestone.
- Run284b was invalid as a behavioral result: exit `124` but only `88` trace lines, no device creation, no sampler rounds, and only an early `xgameruntime` stall.
- Counted valid run `run284c`: exit `124`; no `EXCEPTION_*` records. Trace `8349` lines / `6827` normalized events in one contiguous timestamp cluster (`206070302`-`206072674`). `XsD3D12XboxCreateDevice` is reached from `0x1424785BC`; no `Present`, menu, or Rockstar logo.
- The three `J,` lines are `round=0,handle=0`, `round=1,handle=870`, `round=2,handle=870`. Wine `+sync` confirms TID `400` (`0x190`) repeatedly calls `NtWaitForSingleObject handle 0x870` with a negative timeout. There is no `NtSetEvent`, `NtPulseEvent`, or `NtReleaseSemaphore` for `0x870` anywhere in the log. The handle differs from run282's `0x854` only because handle allocation varies between runs.
- Conclusion: global `0x147bc7f68` is the exact event TID `400` waits on, and the expected signal path either never runs or fails to signal it. Next move for the next session: instrument or patch only the signal path around `0x142a8a990` / `0x144daa220` to determine why it does not signal this handle; do not touch semaphore counts or EXE `ReleaseSemaphore` IAT hooks.

### Run 285 result: int3 at 0x142a8a990 did not fire (inconclusive)
- Diagnostic EXE patch only: `0x142a8a990` (file offset `0x2a89d90`) first byte `41` -> `cc`. Patched hash `22f0d9992a5736c22f782fe3f4189793774d03cde08d5c3a8832e084be399c0b`; restored from `/tmp/opencode/exe_before_run285.bin` to baseline `d2ef3b3e...`.
- Result: exit `124`; no `trace.log` created in the game dir and no `EXCEPTION_BREAKPOINT` in `/tmp/opencode/steam-14721459835430240256.log`. The run was inconclusive as a signal-path probe (launch/tracing anomaly); no conclusion that `0x142a8a990` is not reached.
- Artifacts: `/tmp/opencode/run285.stdout`, `/tmp/opencode/steam-14721459835430240256.log`.

### Run 286 result: forcing WaitForSingleObject wrapper true causes early null-vtable AV
- Diagnostic EXE patch only: `0x142a8bc57` (file offset `0x2a8b057`) `e8 b4 94 79 fd` -> `b0 01 90 90 90` (force `0x140225110` return true at the `0x142a8bc48` phase wait). Patched hash `3b599dd3f79f43d6377bf462058ca388d0dbc163886d8e958f171733a1f66bd2`; restored from `/tmp/opencode/exe_before_run286.bin`.
- Result: exit `5`; `EXCEPTION_ACCESS_VIOLATION` in `VCRUNTIME140.dll+0x1989` called from `game_scarlett_master.exe+0x2A8B094` (`0x142a8b094`) -> `0x142215eca`. Trace `5668` lines copied to `/tmp/opencode/trace286.log`. This confirms the wait at `0x142a8bc48` gates object construction; blindly bypassing it produces invalid state earlier than the known post-device null-vtable crash.
- Not a forward milestone; patch reverted.

### Run 287 result: 144daa220 is hit, but from a different caller chain
- Diagnostic EXE patch only: `0x144daa220` (file offset `0x4da9620`) first byte `e9` -> `cc`. Patched hash `9fb8d89ec47781a6dd5466c584c51222d4f29cd9821fee9fa5a053dec95e804a`; restored from `/tmp/opencode/exe_before_run287.bin`.
- Result: exit `3`; `EXCEPTION_BREAKPOINT` at `0x144DAA220` with `rcx=0x5e4`, `rdx=1`. Backtrace: `0x144DAA220`, `0x140006380`, `0x1401F6549`, `0x142484528`, `0x142484318`, `0x14248412E`, `0x140CE03DF`, `0x140CE039D`, `0x140120CF6`, `0x142240772`, `0x14224071A`, `0x1423CF00A`, `0x142215ECA`. Trace `5516` lines copied to `/tmp/opencode/trace287.log`.
- Conclusion: `144daa220` (IAT-jmp to `ReleaseSemaphore`) is actively used, but this breakpoint did **not** catch the `147bc7f68` signal path. The hit came from the phase-worker/device-init chain (`1423CF00A`) with handle `0x5e4`, not the stalled TID 400 event global. Run285's `142a8a990` probe remains unresolved due missing trace/log; next diagnostic should re-run it with clean trace cleanup and confirm breakpoint/log capture, or instrument `142a8a9b2`/the `147bc7f68` call sites more directly.

### Run 291-294 result: clean baseline re-tests, no Rockstar logo, unstable early AVs
- Baseline verified before runs: `game_scarlett_master.exe d2ef3b3e...`, `d3d12_xs.dll 60b46e36...`, `xmem.dll 670b1501...`, `xgameruntime.dll af48351a...`; EXE unchanged throughout.
- Command used (25-30s timeout, Proton Experimental, `/tmp/opencode/proton-stub-test`, `WINEDEBUG=+timestamp,+pid,+tid,+seh,+sync`, `PROTON_LOG=1 PROTON_LOG_DIR=/tmp/opencode`): all exits `5`.
- Run 291: AV `VCRUNTIME140.dll+?` path reported in steam log as write fault `0000000141D0276B` (`trace 5668`, `/tmp/opencode/trace291.log`), no `J,round`/`147bc7f68` signal markers in trace.
- Run 292: AV read `0000008900000088` at `game+0x1D029B4`, backtrace through `1D02454,1BB1CBD,58A1E46,589F0E9,21B8974,081B2E8,53EC05E,...` (`trace 5778`, `/tmp/opencode/trace292.log`). No `XsD3D12XboxCreateDevice`/`147bc7f68`/`Present` markers in trace.
- Run 293: early VCRUNTIME AV `VCRUNTIME140.dll+0x1989` from `game+0x2A8B094` -> `0x142215ECA` (`trace 5668`, `/tmp/opencode/trace293.log`). No device/present markers.
- Run 294: same AV shape as run 292 at `game+0x1D029B4` (`trace 5778`, `/tmp/opencode/trace294.log`).
- Conclusion: clean baseline is currently unstable before visuals; failures vary across runs and do not reach the previously observed run284c `147bc7f68` wait stall. Need to re-establish a stable repro of the run284c blocker before patching signal path. Next diagnostic should capture full steam/proton logs per run, confirm trace file location/contents, and add lightweight DLL-side logging around device creation/wait-object creation and the `147bc7f68` global handle lifecycle.

### Runs 295-333: auto-run loop, no visuals, baseline unstable before 284c stall
- Environment: Proton Experimental and Proton 11.0, `STEAM_COMPAT_DATA_PATH=/tmp/opencode/proton-stub-test` and restored `/tmp/opencode/proton-stub-test.bak.1795705683`, `WINEDEBUG=+timestamp,+pid,+tid,+seh,+sync` (some with +file), `PROTON_LOG=1`, 35-90s timeouts. Baseline hashes verified each run.
- Results: 35+ runs, all exit 5 (except run308 timeout/trace 1034, runs 309-313 prefix-lock failures after moving prefix; fixed by recreating prefix dir). No `J,round`, no `XsD3D12XboxCreateDevice` marker, no `Present/CreateSwapChain` in trace.
- Common early AVs seen: `game+0x1D0276B` (write null+0x38), `game+0x1D029B4` read `[0000008900000088]` with stack `1D02454,1BB1CBD,58A1E46,589F0E9,21B8974,081B2E8,53EC05E...`, `VCRUNTIME140+...` via `game+0x016401D/56FFA3D`, and occasional `game+0x2A8B094 -> 0x2215ECA` (same shape as run286 wait-bypass AV but on unpatched EXE).
- Trace files in game dir are small/early (~222k-235k) compared to run284c (8349 lines, had J markers). Run284c repro not yet recovered.
- Artifacts: `/tmp/opencode/run295-333.stdout`, `/tmp/opencode/trace295-333.log`.
- Next: reduce tracing overhead/no PROTON_LOG, try clean prefix clone from known-good state, and disassemble/identify `0x141d029b4`/`0x141d0276b` to see what object/stub is null/malformed.

### Runs 334-367: continued autorun, still no visuals
- Autorun script `/tmp/opencode/autorun_gta6.py` cycled Proton 11.0/Experimental, old+fresh prefixes, `+seh,+file,+sync`/`+seh,+sync`, 120s timeout; then manual runs 358-367 with `WINEDEBUG=-all`, `PROTON_LOG=0`, old prefix.
- All runs exit 5; no `J,round`, no `XsD3D12XboxCreateDevice`, no `Present/CreateSwapChain`. Trace sizes ~222k-235k (early) vs run284c 8349 lines with J markers.
- Recurring AV signatures: `game+0x1D0276B` (null write +0x38), `game+0x1D029B4` reading `0000008900000088` via `1D02454,1BB1CBD,58A1E46,589F0E9,21B8974,081B2E8,53EC05E`, `VCRUNTIME` path `016401D/56FFA3D`, occasional `game+0x2A8B094 -> 2215ECA` (same site as run286 wait-bypass invalid-state AV, but on unpatched EXE).
- Run284c `147bc7f68` TID 400 wait-stall not reproduced. Baseline EXE/DLL hashes remained unchanged.
- Artifacts: `/tmp/opencode/run334-367.stdout`, `/tmp/opencode/trace334-367.log`, `/tmp/opencode/autorun_gta6.log`.

### Run 368-430: resumed autonomous loop, baseline verified, 284c stall not reproduced
- Baseline verified before resuming: EXE `d2ef3b3e...`, backup `e95020b2...` unchanged, `d3d12_xs.dll 60b46e36...`, `xmem.dll 670b1501...`, `xgameruntime.dll af48351a...`, sources `dc77afec.../19b2e996.../54787670...`. No temporary EXE patches active.
- Run 368 (15s, Proton Experimental, old prefix, `+timestamp,+pid,+tid,+seh,+sync`): exit 5, trace 5668 (`/tmp/opencode/trace368.log`), early AV `EXCEPTION_ACCESS_VIOLATION` at `game+0x1D029B4` reading `0000008900000088`, same unstable family as 292/294/315. No `J,round`, no `XsD3D12XboxCreateDevice`, no Present.
- Runs 369-430 (autorun, Proton 11.0/Experimental, old+fresh prefixes, mostly `WINEDEBUG=-all`, 120s timeout): all exit 5, traces ~219k-230k, no `J,round`, no device-create/present markers, no 284c `147bc7f68` wait-stall repro. Common AV signatures remain `1D0276B`, `1D029B4/1D02454/1BB1CBD...`, `016401D/56FFA3D`, occasional `2A8B094->2215ECA`.
- Planned +sync diagnostic on handle `0x5B4` from 284/283 context cannot be executed yet because the run284c post-device stall is not currently reproducing; runs fail before device creation. The handle is run-specific anyway; diagnostic must key on `147bc7f68`/post-device stall once reproduced.
- Autorun restarted after logging; will continue testing and log any run that reaches device creation, J markers, Present/CreateSwapChain, or a stable new stall.

### Runs 369-389: +sync batch, instability persists, no 284c repro
- 15s/20s bounded +sync runs on old prefix with Proton Experimental/11.0. Results mixed: exits 5 with early AVs (`016401D/1D0276B/56FFA3D`, `1D029B4/1D02454/1BB1CBD`) and exits 124 with missing/partial traces (0-473k) and no `J,round`/device-create/present markers. No repeated wait on `147bc7f68` handle observed; no 284c post-device stall reproduced, so 0x5B4/147bc7f68 +sync signal-path diagnostic remains blocked pending repro.
- Baseline EXE remained `d2ef3b3e...`. Autonomous watcher `/tmp/opencode/autorun_gta6_watch.py` continues cycling envs and will stop/log on device-create, J markers, present/swapchain, or stable stall.

### Post-pause state cleanup before run 390
- On resume, an autonomous loop had continued after the pause. No watcher process was still running, but it left `game_scarlett_master.exe` with extra temporary patches beyond the two accepted patches: a null-vtable guard around `0x140164010` and a `0x145441BE0` wait-wrapper rewrite. The patched EXE hash was `57ba56b4e91650199387ee0c6e97fff4c5f2c90118f7629c29764271e4259439`; it was saved as `/tmp/opencode/exe_autorun_current_before_restore.exe`, then replaced from `/tmp/opencode/exe_before_it04.exe` and verified at baseline `d2ef3b3e3eb3ffa5af167b474fe90d05a474a2611a7a4173da26a71141cb75fe`. Backup remained `e95020b2...` and EXE size remained `120181760`.
- The same loop had rebuilt every stub DLL with `generate_stubs.py` flags. All source files still match their known baseline hashes (`d3d12_xs.cpp dc77afec...`, `common.h 865fd4d5...`, `xmem.cpp 19b2e996...`, `xgameruntime.cpp 54787670...`), so all stub DLLs were rebuilt from those unchanged sources with the documented minimal compile command and synced to `DLLs/`. Current rebuilt hashes: `d3d12_xs.dll a9329296d49342aba4ca748d242ca22f0fa125a40641cbb919a2d25d776cd9c3`, `xmem.dll 915b34e214db410761889864c5522d1936924feab5e7870419af85344f77ffaa`, `xgameruntime.dll 277e2c9afafbafcd59bfe0bc3814bac60a658baa890a9d261321a49bcd38a11b`.
- The historical DLL hashes (`60b46e36...`, `670b1501...`, `af48351a...`) could not be restored byte-for-byte because the original `/tmp/opencode/mingw` toolchain snapshot was removed and PE timestamps/flags differ; timestamp-only reconstruction over 2026-11-23 through 2026-11-27 did not match. This cleanup is source-identical, not a behavioral change. No `PROGRESS.md` history was removed.
- New disassembly finding: the global at `0x147bc7f68` is created by `CreateSemaphoreExW(initial=0, max=0x7fff)` at `0x141662430`/`0x142a8afe0`. The real producer signal site is `0x1402b8285`, which loads that global and calls `ReleaseSemaphore(handle,1,NULL)` through `0x144daa220`. `0x142a8a990` is only the cleanup path that releases the same semaphore when a refcount reaches zero. `SetEvent` is IAT slot `0x146793B18`; `ReleaseSemaphore` is `0x146793B08`.
- Next change: one targeted behavior test from the run284 no-suspend diagnostic base: release the `0x147bc7f68` semaphore once when `XsD3D12XboxCreateDevice` is called, to see whether TID 400 leaves its wait and boot advances. No heartbeat and no EXE IAT hook.

### Run 431/432 result: device-time semaphore global is still null; one-shot release too early
- Run 431 did not launch: `/tmp/opencode/proton-stub-test` had been deleted, so Proton failed on missing `pfx.lock`. Artifacts saved as `/tmp/opencode/run431-prefix.log` and `/tmp/opencode/run431-prefix.stdout`. Directory was recreated before run 432.
- Run 432 used accepted two-patch EXE hash `18587ece...` and diagnostic DLL/source hashes `4a62e0ee...`/`e8297b5e...`. Command: Proton 11.0, `WINEDEBUG=+timestamp,+pid,+tid,+seh,+file,+sync`, `PROTON_LOG=1`, 15s timeout. Exit `124`; logs `/tmp/opencode/run432.log` (281546 lines) and `/tmp/opencode/trace432.log` (6191 lines, 6187 normalized events, one timestamp cluster).
- `XsD3D12XboxCreateDevice` was reached (`E,1f7b,1424785bc`), but the diagnostic line was `D,semrelease,380,0,0`: global `0x147bc7f68` was still `0`, so no release was attempted. No `EXCEPTION_*`, no `Present`/`CreateSwapChain` markers.
- `+sync` shows many `NtCreateSemaphore(initial=0,max=32767)` calls shortly after device creation (`run432.log` lines 11833+), matching the documented worker-ready semaphore construction. Therefore the global is populated later than `XsD3D12XboxCreateDevice`; the release cannot be done at device-creation time.
- Artifacts restored after the run: EXE `d2ef3b3e...`, diagnostic DLL/source replaced from `/tmp/opencode/d3d12_xs_before_run431.dll` and `/tmp/opencode/d3d12_xs_source_before_run431.cpp`, backup `e95020b2...` unchanged, EXE size `120181760`.
- Next change: replace the device-time direct release with one bounded watchdog thread started at device creation. It waits until global `0x147bc7f68` becomes non-null, then calls `ReleaseSemaphore(handle,1,NULL)` exactly once (interlocked guard) and logs the result. No heartbeat and no EXE IAT hook.

### Run 433 result: one-shot semaphore release unblocks TID 400 once, then it re-waits
- Diagnostic build: EXE `18587ece...`, source `106488d5...`, DLL `715babee...`. Run used Proton 11.0, `WINEDEBUG=+timestamp,+pid,+tid,+seh,+file,+sync`, `PROTON_LOG=1`, 15s timeout. Exit `124`; logs `/tmp/opencode/run433.log` (314313 lines) and `/tmp/opencode/trace433.log` (6253 lines).
- `XsD3D12XboxCreateDevice` reached (`E,2114,1424785bc`; RVA shifted because of the diagnostic code). The watcher thread logged `D,semrelease,392,838,1`; `+sync` confirms `NtReleaseSemaphore handle 0x838, count 1, prev_count (nil)` from TID `0x188` at `27841.790`.
- After that release, TID `0x190` (400) has exactly one successful wait on `0x838` (`-> 0` at `27841.809`), then resumes the same wait and times out 11846 more times through the end of the 15s window. Trace advanced about 60 events past the release, so this is forward movement but no menu/present.
- No `EXCEPTION_*` and no `Present`/`CreateSwapChain` markers. The global really is the handle the stalled thread waits on; one token is consumed, but the producer does not supply subsequent tokens.
- Artifacts restored after the run: EXE `d2ef3b3e...`, DLL `a9329296...`, source `dc77afec...`, backup unchanged, size `120181760`.
- Next change: keep the same one-shot watcher, but release a bounded burst of 8 tokens in a single `ReleaseSemaphore` call instead of 1, and log the requested count. This tests whether the stall needs multiple queued work tokens, without a repeating heartbeat.

### Run 434 result: 8-token burst only scales consumed waits, no new milestone
- Diagnostic build: EXE `18587ece...`, source `7ded40af...`, DLL `3a227333...`. Same command as run433. Exit `124`; logs `/tmp/opencode/run434.log` (314303 lines), `/tmp/opencode/trace434.log` (6253 lines).
- `D,semrelease,392,838,1,8` and `+sync` `NtReleaseSemaphore handle 0x838, count 8`. TID `0x190` now has 8 successful waits on `0x838` instead of 1, then returns to the same 1 ms polling wait for the remaining 11s. Trace length and stall point match run433; no menu/present, no exception.
- Disassembly finding: consumer `0x142a8bc60` pops from a ring buffer at `[rsi+0x11e330/334/338]` after a successful semaphore wait; when head equals tail it jumps to `0x142a8bdf5` and re-waits. Producer `0x1402b7ec0` pushes to `[global 0x147bc7f80 + same offsets]` and calls `ReleaseSemaphore(global 0x147bc7f68,1)` at `0x1402b8285`. Its sole caller is `0x145181f14`, gated by `0x1402b7e80` and global flag `0x147bc7f90` bit 0 (set during init at `0x142a8b6df`).
- Conclusion: releasing more tokens cannot help while the producer is not pushing work. Next diagnostic should observe whether `0x145181f14` is reached after the first token; do not keep increasing the burst.
- Artifacts restored: EXE `d2ef3b3e...`, DLL `a9329296...`, source `dc77afec...`, backup unchanged, size `120181760`.
- Next change: temporary EXE `int3` at `0x145181f14` (file offset `0x5181314`) plus the run434 one-shot 8-token DLL, to see whether the producer call site is reached after the consumer drains the injected tokens. Snapshot/restore EXE around the run.

### Run 435 result: producer call site 0x145181f14 is not reached
- Diagnostic build: EXE `6d45e753...` (accepted patches + first byte of `0x145181f14` changed `e8` -> `cc`, file offset `0x5181314`), source `da295a5f...`, DLL `2de867c2...`. Same command as run434. Exit `124`; logs `/tmp/opencode/run435.log`, `/tmp/opencode/trace435.log` (6253 lines).
- `D,semrelease,392,838,1,8` again confirms the one-shot burst. No `EXCEPTION_BREAKPOINT` and no other exception: the `int3` at the producer call never fired, even after the consumer drained all 8 injected tokens.
- Conclusion: the stall is upstream of `0x145181f14`. The producer function `0x1402b7ec0` is not being invoked from its only known caller after device creation; injecting more semaphore tokens cannot create work. Next diagnostic moves the breakpoint to the caller function entry `0x145181e90` to determine whether that function runs at all.
- Artifacts restored: EXE `d2ef3b3e...`, DLL `a9329296...`, source `dc77afec...`, backup unchanged.
- Next change: temporary EXE `int3` at `0x145181e90` (file offset `0x5181290`, first byte `41` -> `cc`) with the same 8-token DLL. Snapshot/restore EXE around the run.

### Run 436 result: caller function 0x145181e90 is also not reached
- EXE `26ef1780...` (accepted patches + `int3` at `0x145181e90`, file offset `0x5181290`), DLL/source `364ebf25...`/`85d5e275...` (same 8-token burst behavior). Same command as run434. Exit `124`; logs `/tmp/opencode/run436.log`, `/tmp/opencode/trace436.log` (6253 lines).
- `D,semrelease,392,838,1,8` present; no `EXCEPTION_*`. The breakpoint at the function entry never fired, so `0x145181e90` is not called after device creation even though the consumer is starving.
- Fresh disassembly supersedes this stale call graph: the callers of `0x145181e90` are `0x140b48a60` and the wrapper self-call at `0x145181e78`; `0x14259351c` calls the wrapper `0x145181e60`, not `0x145181e90` directly. The run437/438 `int3` test at `0x14259351c` is still valid as a negative result, but its “known caller” label was wrong.
- After restoring EXE to baseline, the 8-token diagnostic DLL/source are retained as the working diagnostic base and saved at `/tmp/opencode/d3d12_xs_burst8.dll` and `/tmp/opencode/d3d12_xs_burst8.cpp`; baseline copies remain `/tmp/opencode/d3d12_xs_before_run436.dll` and `/tmp/opencode/d3d12_xs_source_before_run436.cpp`.

### Run 437 result: call-site breakpoint inconclusive; timing-dependent AV preempted
- EXE `db70b605...` (accepted patches + `int3` at `0x14259351c`, file offset `0x259291c`), DLL/source `364ebf25...`/`85d5e275...`. Exit `5`; logs `/tmp/opencode/run437.log`, `/tmp/opencode/trace437.log` (5955 lines).
- `D,semrelease,392,838,1,8` present. No `EXCEPTION_BREAKPOINT`; instead the known timing-dependent `VCRUNTIME140+0x1989` null write occurred at `28195.475`, backtrace `0x145861B54 -> 0x1443C38FD -> 0x1423CF4F2 -> 0x1423CF2D3 -> 0x1423CF104 -> 0x142215ECA`. This AV preempted the stalled state before the breakpoint could be observed.
- Artifacts restored: EXE `d2ef3b3e...`; 8-token diagnostic DLL/source retained as the working diagnostic base.
- Next change: repeat the identical `int3` probe once (run438) to try to get a clean timeout without the early AV. If it still does not fire, move the probe to the other caller `0x140b48a60`.

### Run 438 result: clean repeat confirms 0x14259351c not reached
- Same EXE patch `db70b605...` and 8-token DLL/source as run437. Exit `124`; logs `/tmp/opencode/run438.log`, `/tmp/opencode/trace438.log` (6253 lines). No `EXCEPTION_*`, `D,semrelease,392,838,1,8` present, but `int3` at `0x14259351c` still did not fire.
- Conclusion: neither `0x145181f14` nor its caller at `0x14259351c` is reached in the stalled state. Next diagnostic targets the other known caller of `0x145181e90`, `0x140b48a60` (file offset `0xb47e60`, first byte `e8` -> `cc`).
- EXE restored to baseline; 8-token DLL/source retained.

### Run 439 result: other 0x145181e90 call site 0x140b48a60 also not reached
- EXE `cc652398...` (accepted patches + `int3` at `0x140b48a60`, file offset `0xb47e60`, first byte `e8` -> `cc`), DLL/source `364ebf25...`/`85d5e275...`. Same command as run434. Exit `124`; logs `/tmp/opencode/run439.log`, `/tmp/opencode/trace439.log` (6253 lines).
- `D,semrelease,392,838,1,8` present, no `EXCEPTION_*`. The breakpoint at `0x140b48a60` did not fire.
- Conclusion: neither known direct caller of `0x145181e90` is active in the stalled state, so the call graph derived from the static disassembly is incomplete or the producer is reached indirectly. Next diagnostic should put the breakpoint directly on the producer function entry `0x1402b7ec0` (file offset `0x2b72c0`, first byte `41` -> `cc`) to determine whether that function runs at all after device creation.
- EXE restored to baseline `d2ef3b3e...`; 8-token DLL/source retained.

### Run 440 result: indirect call probe also silent; pivot to shader/file state
- EXE `61158a10...` (accepted patches + `int3` at the indirect call `0x14247480c`, file offset `0x2473c0c`, first byte `ff` -> `cc`), DLL/source `364ebf25...`/`85d5e275...`. Same command as run434. Exit `124`; logs `/tmp/opencode/run440.log` and `/tmp/opencode/trace440.log`. No `EXCEPTION_*`; `D,semrelease,392,838,1,8` at `trace440.log:5328`; one contiguous timestamp cluster `28778074-28779765`.
- The probe never fired, so this indirect call site is not reached in the stalled state either. This is the fifth consecutive run with the same normalized tail and no forward movement, triggering the pivot rule: stop blind EXE `int3` probes and pivot to shader/file state plus one-shot global diagnostics.
- Trace detail: after the 8-token burst, TID `0x17c` repeats `0x1417f6d3b` (device slot `0x248`/`device_out_arg6`) and `0x1417f6d4b` (subslot `0x48`/`dummy_ok`) 1240 times each, while TID `0x190` keeps polling handle `0x838`. `platform:\shaders\v2\sga_xbsx_final\im.fxc` is repeatedly opened for read but is only an 8-byte placeholder (`53 68 06 01 00 00 00 00`); `shaders.rpf` exists but no extraction/mount is configured.
- Artifacts restored and verified: EXE `d2ef3b3e...`, DLL `364ebf25...`, source `85d5e275...`, backup `e95020b2...` unchanged, EXE size `120181760`. Snapshots: `/tmp/opencode/exe_before_run440.bin`, `/tmp/opencode/d3d12_xs_before_run440.dll`, `/tmp/opencode/d3d12_xs_source_before_run440.cpp`.
- Next move: use the published RPF9 extractor as a separate prep step to obtain real shader containers into the literal `platform:` tree; if that is not immediately feasible, add one source-only one-shot global diagnostic (`0x147bc7f90`, `0x147bc7f80+0x11e330/334/338`, handle) at `diag_sem_release`.

### Run 441 result: placeholder-preserving change accepted; known early AV preempted the stall
- Prep finding: `shaders.rpf` contains a real SGD5 database at file offset `0x9000`. SparkIV's `Sgd5Database` layout parses it: 33 shared blocks, stage shader counts `[11018, 10944, 195, 0, 0, 2647]`, 910 effects. The game's `platform:` `.fxc` loader at `0x14247b530` expects magic `0x01066853` and a similar six-stage structure, but no direct SGD5-to-`.fxc` conversion is confirmed yet; those cache files may be Xbox system assets absent from the leak.
- One source change: `xgameruntime.cpp::create_placeholders()` now uses `CREATE_NEW` instead of `CREATE_ALWAYS` for the placeholder file list, so externally supplied real shader data is not overwritten at `DllMain`. Source hash `0c702813...`; rebuilt `xgameruntime.dll` hash `75409a7b...`. `d3d12_xs.cpp`/DLL unchanged (`85d5e275...`/`364ebf25...`, 8-token diagnostic).
- EXE temporarily patched to the accepted two-patch hash `18587ece...`; same Proton 11.0 command with `PROTON_LOG=1`, `WINEDEBUG=+timestamp,+pid,+tid,+seh,+file,+sync`, 15s timeout. Exit `5` (not `124`); logs `/tmp/opencode/run441.log` (256345 lines), `/tmp/opencode/trace441.log` (5935 lines, 5929 normalized events, one contiguous timestamp cluster `29918532-29920490`).
- New failure: `EXCEPTION_ACCESS_VIOLATION` at `29920.474`, `VCRUNTIME140.dll+0x1989`, null write (`info[1]=0`), backtrace `game+0x5861B54 -> +0x43C38FD -> +0x23CF4F2 -> +0x23CF2D3 -> +0x23CF104 -> +0x2215ECA`. This is the previously observed timing-dependent AV from run437, not a new milestone. `D,semrelease,392,838,1,8` was present; no `Present`/`CreateSwapChain` and no Rockstar logo. Crash-log/minidump writes to `t:` failed because that DOS device is absent.
- The early AV likely reflects the slightly faster `CREATE_NEW` path (existing placeholders are no longer truncated/rewritten), but the source change itself is benign and preserves real assets. No forward movement past shader/resource initialization.
- Artifacts restored and verified: EXE `d2ef3b3e...`, backup `e95020b2...` unchanged, EXE size `120181760`; `d3d12_xs.dll` `364ebf25...`, `d3d12_xs.cpp` `85d5e275...`, `xgameruntime.dll` `75409a7b...`, `xgameruntime.cpp` `0c702813...`. Snapshots: `/tmp/opencode/exe_before_run441.bin`, `/tmp/opencode/d3d12_xs_before_run441.dll`, `/tmp/opencode/d3d12_xs_source_before_run441.cpp`, `/tmp/opencode/xgameruntime_before_run441.dll`, `/tmp/opencode/xgameruntime_source_before_run441.cpp`.
- Next move: repeat the identical run once to distinguish the timing-dependent AV from a deterministic effect of `CREATE_NEW`; if the AV repeats, treat `VCRUNTIME140+0x1989` as the next crash to fix. If the run times out again, proceed with the one-shot global diagnostic at `diag_sem_release` or the SGD5-to-`.fxc` reconstruction using the database at `shaders.rpf+0x9000`.

### Run 442 result: AV is timing-dependent; same stall returns
- Identical repeat of run441 (accepted two-patch EXE `18587ece…`, `CREATE_NEW` xgameruntime, 8-token d3d12 diagnostic). Clean trace deleted before launch. Exit `124`; logs `/tmp/opencode/run442.log` (317398 lines) and `/tmp/opencode/trace442.log` (6233 lines, 6227 normalized events, one contiguous timestamp cluster `32264259-32266263`).
- No `EXCEPTION_ACCESS_VIOLATION`; only an unrelated `RPC_S_SERVER_UNAVAILABLE` exception line. `D,semrelease,392,838,1,8` present. The normalized tail (module/RVA/phase only) again matches run440's stalled state, so run441's `VCRUNTIME140+0x1989` AV is confirmed timing-dependent and not caused by `CREATE_NEW`.
- Artifacts restored: EXE `d2ef3b3e…`, backup `e95020b2…`, size `120181760`, DLL/source hashes unchanged from run441.
- Next change: one-shot global diagnostic in `d3d12_xs.cpp::diag_sem_release` to log the producer gate `0x147bc7f90` and the three ring-buffer words at `0x147bc7f80+0x11e330/334/338` when the 8 tokens are released.

### Run 443 result: gate is still zero when the 8 tokens are released
- One source change: `diag_sem_release` now logs `0x147bc7f90` and three words from the ring global. Compile initially failed because the range-for used a braced initializer without `<initializer_list>`; replaced it with a static array, then rebuilt successfully. Source `79ecfae7...`, DLL `a225227b...`. First compile error was fixed before any run.
- Same accepted two-patch EXE `18587ece...`, `CREATE_NEW` xgameruntime `0c702813...`/`75409a7b...`. Exit `124`; logs `/tmp/opencode/run443.log` (315314 lines) and `/tmp/opencode/trace443.log` (6255 lines, 6249 normalized events, one contiguous timestamp cluster `32348742-32350298`).
- Diagnostic line: `D,semrelease,392,838,1,8,gate,0,r11e330,0,r11e334,0,r11e338,0`. No `EXCEPTION_ACCESS_VIOLATION` or `EXCEPTION_BREAKPOINT`; no `Present`/`CreateSwapChain`.
- Fresh disassembly corrects the diagnosis: `0x142a8b6df` writes byte `1` to `0x147bc7f90` only after `call 0x1457d7640`, while the semaphore global `0x147bc7f68` is available earlier. The watcher therefore releases the 8 tokens before the producer gate is opened; the consumer drains them before any producer can run. This explains why runs 433/434 only scaled consumed waits.
- The ring words are currently read incorrectly: `0x147bc7f80` holds a pointer (`mov r9,[0x147bc7f80]` then `[r9+0x11e30c]`), so `*reinterpret_cast<ULONG_PTR*>(kRing + offset)` reads the wrong addresses. This will be corrected separately if needed.
- Artifacts restored and verified: EXE `d2ef3b3e...`, backup `e95020b2...`, size `120181760`; DLL/source `a225227b...`/`79ecfae7...`.
- Next change: make the watcher wait until the gate byte at `0x147bc7f90` is set before releasing the one-shot 8-token burst, then observe whether the producer call path finally runs.

### Run 444 result: gate=1 at token release, but producer is still not reached
- One source change: watcher now polls the gate byte at `0x147bc7f90` and only releases the 8 tokens after it is set. Namespace-scope `kProducerGate` shared with the diagnostic. Source `4b1c463c...`, DLL `9f4a66c1...`; compiled cleanly before the run.
- Same accepted two-patch EXE `18587ece...`, `CREATE_NEW` xgameruntime. Exit `124`; logs `/tmp/opencode/run444.log` (319318 lines) and `/tmp/opencode/trace444.log` (6255 lines, 6249 normalized events, one contiguous timestamp cluster `32497920-32499715`).
- Diagnostic line: `D,semrelease,392,838,1,8,gate,1,...` confirms the burst now occurs after `0x142a8b6df`. No `EXCEPTION_*`, no `Present`/`CreateSwapChain`, no Rockstar logo.
- Trace tail and event counts remain the same stall as runs 442/443 (only RVAs shift from the changed DLL layout). Waiting for the gate did not make `0x145181e90` or `0x1402b7ec0` reachable; the consumer drains the injected tokens and returns to its single-object wait.
- Fresh disassembly: `0x1402b7e80` returns true only when the high 16 bits of its argument are below a limit and `0x144efca30` returns non-null; this gate is separate from `0x147bc7f90`. More importantly, `int3` at both known callers and at the producer call site already showed the wrapper is not called at all, so fixing its internal gates cannot help without an active caller.
- Conclusion: semaphore timing is not the blocker. The producer is not scheduled because the shader/resource work callback is not completing; continue with the `.fxc`/SGD5 reconstruction path rather than more semaphore changes.
- Artifacts restored and verified: EXE `d2ef3b3e...`, backup `e95020b2...`, size `120181760`; DLL/source `9f4a66c1...`/`4b1c463c...`; xgameruntime `75409a7b...`/`0c702813...`.
- Next change: decode the `.fxc` container parsers and construct a minimal nonzero-count `0x01066853` file for `platform:\shaders\v2\sga_xbsx_final\im.fxc`, preserving the existing `CREATE_NEW` behavior.

### Run 445 result: repeated device slot 0x248 interface query is uniform
- One source change: `device_out_arg6` logs its arguments for the first 20 calls and every 100th call using the shared `shim_trace_lock`/`WriteFile` tracer. Source `f73d3471...`, DLL `814ed59c...`; compiled cleanly before the run.
- Same accepted two-patch EXE `18587ece...`, `CREATE_NEW` xgameruntime. Exit `124`; logs `/tmp/opencode/run445.log` (311648 lines) and `/tmp/opencode/trace445.log` (6281 lines, 6249 normalized events, one contiguous timestamp cluster `32671851-32674078`).
- `D,arg6` shows ~600 calls from the same device object `0x6ffffd844320` with the same output slot `0x6ffffd844280`; the type GUID changes once from `0x1461e0668` (calls 1-5) to `0x1461ecfa0` (calls 6+). Returning `&g_subobject` keeps the following subobject slot `0x48` call returning success, so this query loop never terminates naturally.
- `D,semrelease,392,840,1,8,gate,1,...` still present; no `EXCEPTION_*`, no `Present`/`CreateSwapChain`, no Rockstar logo. Trace tail unchanged. Artifacts restored and verified: EXE `d2ef3b3e...`, backup `e95020b2...`, size `120181760`; DLL/source `814ed59c...`/`f73d3471...`.
- Next change: temporarily replace the first byte of the spinning callback `0x142481e90` with `C3` (ret) to see whether skipping the entire resource/shader work loop lets boot advance. This is a one-byte EXE probe, not a permanent fix.

### Run 446 result: skipping callback 0x142481e90 does not stop the interface-query loop
- One EXE probe: accepted two patches plus first byte of `0x142481e90` changed `41 -> C3` (ret). Patched EXE `2cbaf7d9...`; only temporary. The run used the run445 diagnostic DLL/source `814ed59c...`/`f73d3471...` unchanged.
- Exit `124`; logs `/tmp/opencode/run446.log` (311386 lines) and `/tmp/opencode/trace446.log` (6227 lines, 6195 normalized events, one contiguous timestamp cluster `34730715-34732394`).
- `D,arg6` still shows ~600 calls with the same device `0x6ffffd844320`, output slot `0x6ffffd844280`, and GUID `0x1461ecfa0` after call 6. Trace tail is the same stub/XCRDAPI/xapu/AcpHal spin as run445.
- No `EXCEPTION_*`, no `Present`/`CreateSwapChain`, no Rockstar logo. The callback at `0x142481e90` is therefore not the source of the repeated slot-`0x248` query; the caller is elsewhere.
- Artifacts restored and verified: EXE `d2ef3b3e...`, backup `e95020b2...`, size `120181760`; DLL/source `814ed59c...`/`f73d3471...`; xgameruntime `75409a7b...`/`0c702813...`.
- Next change: add the caller return address from `device_out_arg6` to the `D,arg6` diagnostic so the active spin call site can be identified from the disassembly.

### Run 447 result: hot slot-0x248 caller identified as 0x1417f6d3b
- One source change: `diag_arg6` now appends `__builtin_return_address(0)` as the final `ret` field of each `D,arg6` line, still using only `WriteFile` and `shim_trace_lock`. Source hash `cf8398786d43e818140cf38e76247a61d9da4b04cab8b9a94fc452283ae50c4f`; DLL hash `0d9b288c6e5485f39409d77ba03b20ce606cd5352f40ffd4a3b924bab3a43e38`; compiled cleanly before the run.
- Same accepted two-patch EXE `18587ece...` and `CREATE_NEW` xgameruntime `75409a7b...`. Exit `124`; logs `/tmp/opencode/run447.log` (312343 lines) and `/tmp/opencode/trace447.log` (6281 lines / 6249 normalized events, one contiguous timestamp cluster `34807987-34810192`).
- 26 sampled `D,arg6` calls: 21 from `0x1417f6d3b`, 4 from `0x142478c76`, 1 from `0x142478b77`. The first five use IID pointer `0x1461e0668`; calls 6+ use `0x1461ecfa0`, which is `IID_ID3D12GraphicsCommandList`. The hot device is `0x6ffffd844320` and the output slot stays `0x6ffffd844280`; returning `&g_subobject` keeps the following slot-`0x48` call at `0x1417f6d4b` successful, so the CreateCommandList loop never terminates naturally.
- Fresh disassembly: `0x1417f6cf0` builds a command-list descriptor at `rsp+0x38` (`edx`, `0x100000006`, IID at `+0x20`), calls `*0x248(%r10)`, stores the returned pointer, then calls `*0x48` on it. Its caller `0x1417f6c90` repeatedly invokes it to fill 16-byte entries; the trace shows roughly 600 iterations. This is a CreateCommandList-like path, not a generic QueryInterface loop.
- `D,semrelease,392,840,1,8,gate,1,...` is still present. No `EXCEPTION_*`, `Present`, `CreateSwapChain`, or Rockstar logo; no forward milestone.
- Conclusion: the remaining stub artifact in this loop is that every created command list is the same `g_subobject`, so the title builds a table of duplicate command lists. Next change should make command-list objects distinct (while keeping slot-`0x48` Close successful) or add one caller-context diagnostic to `sub_wait_value`; do not change semaphore timing.

### Run 448 hypothesis: distinct command-list objects may let the table-building loop finish
- `0x1417f6c90` fills a 16-byte-per-entry table by calling `0x1417f6cf0`; `0x141f75d3d` allocates four command lists per item. Every `device_out_arg6` currently returns the same `&g_subobject`, so the title receives duplicate identities for all ~600 lists.
- Planned single change: for the confirmed `IID_ID3D12GraphicsCommandList` stack IID, hand out rotating distinct `DummyObject` entries with `g_sub_vtable` (slot `0x48`/Close stays `dummy_ok`). Keep the earlier unknown-IID calls on `g_subobject`. No semaphore, EXE, or xgameruntime behavior changes.

### Run 448 result: distinct command-list objects do not change the stall
- Single source change implemented as planned: 4096 rotating `DummyObject` command lists with `g_sub_vtable` are returned only when the stack IID pointer equals `0x1461ecfa0` (`IID_ID3D12GraphicsCommandList`); earlier `0x1461e0668` calls still return `g_subobject`. Source hash `d67ae6fe...`; DLL hash `191c34e3...`; compiled cleanly before the run.
- Accepted two-patch EXE `18587ece...`; run command unchanged. Exit `124`; logs `/tmp/opencode/run448.log` and `/tmp/opencode/trace448.log` (6259 lines / 6227 normalized events, one contiguous timestamp cluster `35156659-35158409`).
- Sampled `D,arg6` calls still number 26 with the same caller split (21 from `0x1417f6d3b`, 4 from `0x142478c76`, 1 from `0x142478b77`) and the same IID transition. Normalized event counts are identical to run447 at the hot sites: 620 `0x1417f6d3b` entries and 620 `0x1417f6d4b` entries. Tail and stall point are unchanged.
- No `EXCEPTION_*`, no `Present`/`CreateSwapChain`, no Rockstar logo. `D,semrelease,392,840,1,8,gate,1,...` remains present. Distinct command-list identity is therefore not the blocker.
- Because the normalized tail has now repeated well beyond three runs with no forward movement, the last edit was reverted: source restored to `cf839878...` and DLL to `0d9b288c...`. EXE restored to baseline `d2ef3b3e...`, backup unchanged, size `120181760`.
- Pivot: stop changing command-list object identity and decode the other repeated sites in the stall (`0x14161d0a3`, `0x140344188`, and the `0x141f75fb4/fce/fe0` sequence) before the next DLL edit.
