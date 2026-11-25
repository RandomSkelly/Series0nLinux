#!/usr/bin/env python3
"""Build every offline replacement DLL in this directory.

The original seven stubs were minimal no-ops.  This version builds a complete
set: real xgameruntime COM vtable, kernel/xmem forwarding, D3D12/DXGI/XAudio
forwarding, GDK user/store async stubs, and offline network stubs.

Exports are controlled by the matching .def files, including link-level
forwarders for the untouched curl/Winsock ABIs.  Built DLLs are placed beside
game_scarlett_master.exe and mirrored into the parent DLLs/ folder.
"""

import os
import shutil
import subprocess
from pathlib import Path

GAME_DIR = Path(__file__).resolve().parent.parent
SRC_DIR = Path(__file__).resolve().parent
SYNC_DIR = GAME_DIR.parent / "DLLs"

MINGW_BIN = Path(os.environ.get("MINGW_BIN", "/tmp/opencode/mingw/usr/bin"))
MINGW_SYSROOT = Path(os.environ.get("MINGW_SYSROOT", str(MINGW_BIN.parent)))

# DLL name -> source files (the matching .def is added automatically).
BUILD = {
    "xmem.dll": ["xmem.cpp"],
    "kernelx.dll": ["kernelx.cpp"],
    "xgameruntime.dll": ["xgameruntime.cpp"],
    "XUser.dll": ["XUser.cpp"],
    "XStore.dll": ["XStore.cpp"],
    "d3d12_xs.dll": ["d3d12_xs.cpp"],
    "dxgi_xs.dll": ["dxgi_xs.cpp"],
    "xaudio2_9xs.dll": ["xaudio2_9xs.cpp"],
    "acpx.dll": ["acpx.cpp"],
    "XCurl.dll": ["XCurl.cpp"],
    "Party.dll": ["Party.cpp"],
    "winsockx.dll": ["winsockx.cpp"],
    "chad.dll": ["chad.cpp"],
    "xg_xs.dll": ["xg_xs.cpp"],
    "XCRDAPI.dll": ["XCRDAPI.cpp"],
    "xapu.dll": ["xapu.cpp"],
    "AcpHal.dll": ["AcpHal.cpp"],
    "GameInput.dll": ["GameInput.cpp"],
    "XboxBvhBuilder.dll": ["XboxBvhBuilder.cpp"],
}


def find_cxx():
    for name in ("x86_64-w64-mingw32-g++-posix", "x86_64-w64-mingw32-g++"):
        path = MINGW_BIN / name
        if path.exists():
            return path
        found = shutil.which(name)
        if found:
            return Path(found)
    raise SystemExit(
        "x86_64-w64-mingw32-g++ not found; set MINGW_BIN to the extracted "
        "toolchain directory."
    )


def build_command(cxx):
    cmd = [str(cxx)]
    if MINGW_SYSROOT.exists():
        cmd.append(f"--sysroot={MINGW_SYSROOT}")
    gcc_lib = None
    if MINGW_SYSROOT.exists():
        for version in ("13-posix", "12-posix"):
            candidate = MINGW_SYSROOT / "lib/gcc/x86_64-w64-mingw32" / version
            if candidate.is_dir():
                gcc_lib = candidate
                break
    if gcc_lib:
        cmd.append(f"-B{gcc_lib}")
    cmd += [
        "-shared",
        "-Os",
        "-static-libgcc",
        "-static-libstdc++",
        "-Wl,--exclude-libs,ALL",
        "-o",
    ]
    return cmd


STATIC_LIBS = ["-Wl,-Bstatic", "-lwinpthread", "-Wl,-Bdynamic"]


def preserve_curl_original():
    original = GAME_DIR / "XCurl.dll"
    preserved = GAME_DIR / "XCurlOrig.dll"
    if original.exists() and not preserved.exists():
        shutil.copy2(original, preserved)
        print(f"preserved {original.name} -> {preserved.name}")


def preserve_bvh_original():
    original = GAME_DIR / "XboxBvhBuilder.dll"
    preserved = GAME_DIR / "XboxBvhBuilderOrig.dll"
    if original.exists() and not preserved.exists():
        shutil.copy2(original, preserved)
        print(f"preserved {original.name} -> {preserved.name}")


def copy_runtime_dlls():
    candidates = [
        MINGW_SYSROOT / "x86_64-w64-mingw32/lib/libwinpthread-1.dll",
        MINGW_BIN / "libwinpthread-1.dll",
    ]
    for source in candidates:
        if source.exists():
            shutil.copy2(source, GAME_DIR / source.name)
            shutil.copy2(source, SYNC_DIR / source.name)
            print(f"copied {source.name} beside the game and to {SYNC_DIR}")
            return
    print("warning: libwinpthread-1.dll not found in toolchain")


def sync_sources():
    target = SYNC_DIR / "stub_dlls_src"
    target.mkdir(parents=True, exist_ok=True)
    patterns = ("*.cpp", "*.def", "*.h", "generate_stubs.py")
    for pattern in patterns:
        for source in SRC_DIR.glob(pattern):
            shutil.copy2(source, target / source.name)


def main():
    cxx = find_cxx()
    cmd = build_command(cxx)
    preserve_curl_original()
    preserve_bvh_original()
    SYNC_DIR.mkdir(parents=True, exist_ok=True)

    for dll, sources in BUILD.items():
        args = cmd + [str(GAME_DIR / dll)]
        args += [str(SRC_DIR / name) for name in sources]
        def_file = SRC_DIR / (Path(dll).stem + ".def")
        if def_file.exists():
            args.append(str(def_file))
        args += STATIC_LIBS
        subprocess.run(args, check=True)
        shutil.copy2(GAME_DIR / dll, SYNC_DIR / dll)
        print(f"built {dll} ({', '.join(sources + ([def_file.name] if def_file.exists() else []))})")

    for preserved_name in ("XCurlOrig.dll", "XboxBvhBuilderOrig.dll"):
        preserved = GAME_DIR / preserved_name
        if preserved.exists():
            shutil.copy2(preserved, SYNC_DIR / preserved_name)
    copy_runtime_dlls()
    sync_sources()
    print(f"synced sources and DLLs to {SYNC_DIR}")


if __name__ == "__main__":
    main()
