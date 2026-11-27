"""Verify a Release build and produce standalone runtime/source ZIPs."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.0.0"
DLL_NAME = "d2rl-repair-charge-replenishment.dll"


def metadata(dll: Path) -> dict:
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    size = version.GetFileVersionInfoSizeW(str(dll), None)
    if not size:
        raise ValueError("Windows version resource is missing.")
    resource = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(dll), 0, size, resource):
        raise ValueError("Windows version resource cannot be read.")

    def query(key):
        pointer = ctypes.c_void_p()
        length = wintypes.UINT()
        if not version.VerQueryValueW(resource, key, ctypes.byref(pointer), ctypes.byref(length)):
            raise ValueError(f"Version field is missing: {key}")
        return pointer, length.value

    windows = {}
    for field in ("Author", "CompanyName", "FileVersion", "ProductVersion"):
        pointer, _ = query("\\StringFileInfo\\040904B0\\" + field)
        windows[field] = ctypes.wstring_at(pointer)
    pointer, length = query("\\")
    if length < 52:
        raise ValueError("Fixed Windows version resource is incomplete.")
    fixed = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD * 13)).contents
    major, minor, patch = map(int, VERSION.split('.'))
    fixed_version = ((major << 16) | minor, patch << 16)
    if fixed[7] & 1 or (fixed[2], fixed[3], fixed[4], fixed[5]) != fixed_version * 2:
        raise ValueError("The DLL has a Debug flag or unexpected fixed version.")
    if windows != {"Author": "MadMike", "CompanyName": "MadMike", "FileVersion": VERSION, "ProductVersion": VERSION}:
        raise ValueError(f"Unexpected Windows metadata: {windows}")

    class PluginInfo(ctypes.Structure):
        _fields_ = [("size", ctypes.c_uint32), ("abi", ctypes.c_uint32), ("id", ctypes.c_char_p),
                    ("name", ctypes.c_char_p), ("version", ctypes.c_char_p), ("author", ctypes.c_char_p),
                    ("description", ctypes.c_char_p), ("flags", ctypes.c_uint32), ("reserved", ctypes.c_uint32 * 4)]
    # Only the plugin's static metadata export is called; no game or loader is launched.
    library = ctypes.CDLL(str(dll))
    get_info = library.D2RLoaderGetPluginInfo
    get_info.restype = ctypes.POINTER(PluginInfo)
    info = get_info().contents
    loader = {"id": info.id.decode(), "name": info.name.decode(), "version": info.version.decode(), "author": info.author.decode(), "abi": info.abi}
    if loader != {"id": "repair-charge-replenishment", "name": "Repair Charge Replenishment", "version": VERSION, "author": "MadMike", "abi": 4}:
        raise ValueError(f"Unexpected loader metadata: {loader}")
    if info.flags != 0x0E:  # Shared | NativeHooks, without ModScopedOnly.
        raise ValueError("The fork must allow global loading and use the shared gameplay role.")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    if not kernel.FindResourceW(library._handle, 0x03E9, 10):
        raise ValueError("The SDK manifest resource is missing.")
    if kernel.FindResourceW(library._handle, 0x03EA, 10):
        raise ValueError("This DLL-only fork must not embed or require a configuration file.")
    return {"windows": windows, "loader": loader, "configuration": "Release", "debug_flag": False,
            "global_supported": True, "configuration_resource_present": False}


def archive(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name, data in sorted(files.items()):
            record = zipfile.ZipInfo(name, (2026, 11, 27, 0, 0, 0))
            record.compress_type = zipfile.ZIP_DEFLATED
            output.writestr(record, data)
    with zipfile.ZipFile(path) as result:
        if result.testzip() is not None or set(result.namelist()) != set(files):
            raise ValueError(f"Archive verification failed: {path.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--in-game-result", help="Record the result of a completed manual in-game test")
    args = parser.parse_args()
    build = args.build_directory.resolve(strict=True)
    destination = args.output_directory.resolve()
    if destination.is_relative_to(ROOT):
        raise ValueError("Package output must be outside the plugin source tree.")
    destination.mkdir(parents=True, exist_ok=True)
    dll = build / DLL_NAME
    verified = metadata(dll)
    for name in ("policy_tests.exe", "runtime_tests.exe", "native_stats_tests.exe"):
        subprocess.run([str(build / name)], check=True)
    command = (build / "build-release.cmd").read_text(encoding="utf-8")
    if "/O2" not in command or "/DNDEBUG" not in command or "/MT " not in command:
        raise ValueError("The build record does not select the documented Release flags.")
    digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    verified.update({"version": VERSION, "sdk": "0.4.0", "sha256": digest,
                     "validation": {"policy": "passed", "mocked_sdk_including_88_byte_item_service": "passed", "native_stat_reader": "passed", "global_and_mod_scopes_without_config_or_mod_paths": "passed", "durability_and_quantity_preserved": "passed", "in_game": args.in_game_result or "pending"}})
    checksum = f"{digest}  {DLL_NAME}\n".encode()
    runtime = {DLL_NAME: dll.read_bytes(), DLL_NAME + ".sha256": checksum}
    for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "HOOK_INVENTORY.md", "NATIVE_CONTRACT.md", "CHANGELOG.md"):
        runtime[name] = (ROOT / name).read_bytes()
    runtime["BUILD_VERIFICATION.json"] = (json.dumps(verified, indent=2) + "\n").encode()
    sources = {}
    for path in ROOT.rglob("*"):
        if path.is_file() and not any(p in {".git", "__pycache__", "build", "dist"} for p in path.relative_to(ROOT).parts):
            sources[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    runtime_zip = destination / f"repair-charge-replenishment-{VERSION}-windows-x64.zip"
    source_zip = destination / f"repair-charge-replenishment-{VERSION}-source.zip"
    archive(runtime_zip, runtime)
    archive(source_zip, sources)
    (destination / "BUILD_VERIFICATION.json").write_text(json.dumps(verified, indent=2) + "\n", encoding="utf-8")
    (destination / (DLL_NAME + ".sha256")).write_bytes(checksum)
    print(json.dumps({"runtime": str(runtime_zip), "source": str(source_zip), **verified}, indent=2))


if __name__ == "__main__":
    main()
