"""stackchan-mcp: Two-faced gateway for StackChan (xiaozhi-esp32).

MCP client side: stdio MCP server (mcp Python SDK)
ESP32 side: WebSocket server (MCP client over JSON-RPC 2.0)
"""

import os as _os
import platform as _platform
import sys as _sys
from importlib.metadata import PackageNotFoundError, version
from pathlib import Path as _Path

try:
    __version__ = version("stackchan-mcp")
except PackageNotFoundError:  # pragma: no cover - source checkout without install
    __version__ = "0.0.0+unknown"

# Windows: register the bundled native libs directory with the DLL
# search path before any submodule pulls in `opuslib` (or any other
# wrapper that calls `ctypes.util.find_library`). On Linux/macOS the
# system package manager typically already provides libopus, so we do
# nothing on those platforms.
#
# Why this is here and not in tts/__init__.py or stt/__init__.py:
# opuslib's libopus lookup happens at import time (the wrapper's
# top-level module unconditionally calls `find_library('opus')` and
# raises if it returns None). That means we need the DLL search path
# update to have run before *any* code imports opuslib, no matter
# which subpackage of stackchan_mcp loads first. The package
# `__init__.py` is the only place guaranteed to run before all
# sibling submodules.
#
# Why we update BOTH `os.add_dll_directory()` AND `os.environ["PATH"]`:
# - `os.add_dll_directory()` is the modern, isolated mechanism used by
#   `LoadLibraryEx(..., LOAD_LIBRARY_SEARCH_USER_DIRS)`. Importantly,
#   `ctypes.util.find_library()` on Windows uses the legacy
#   `LoadLibraryW()` path which does **not** consult the
#   `add_dll_directory()` list (see CPython issue #43603). Since
#   `opuslib/api/__init__.py` calls exactly that — `find_library('opus')`
#   — we also have to prepend the directory to PATH so the legacy
#   resolver picks it up.
# - We add to `add_dll_directory()` too because direct `ctypes.CDLL(...)`
#   / extension-module imports use the modern resolver, and we want
#   bundle discovery to work for both API styles future-proof.
#
# See `stackchan_mcp/_libs/SOURCES.md` for the bundled DLL provenance.
# Architecture gate: the bundled `opus.dll` is built for `win_amd64`
# (x86_64). On Windows ARM64 / Windows x86 (32-bit), loading the x64
# DLL would fail with a native-image mismatch — *exactly* the
# "looks installed but fails at runtime" footgun this bundling is
# meant to remove. Skip the DLL search-path setup on those
# architectures so the user falls back to the same
# "find_library returns None" failure mode they had before this
# fix, which at least produces a clean ImportError on
# `import opuslib` rather than a confusing crash inside the DLL
# loader. A platform-specific wheel build would have rejected those
# architectures at install time (no compatible wheel), so this
# guard mostly matters for users who bypass wheel selection (e.g.
# by installing from sdist on a non-x64 Windows host).
_machine = _platform.machine().upper() if _sys.platform == "win32" else ""
_dll_dir_handle = None  # kept alive at module scope; see comment below

if _sys.platform == "win32" and _machine in ("AMD64", "X86_64"):
    _libs_dir = _Path(__file__).resolve().parent / "_libs"
    if _libs_dir.is_dir():
        # Retain the directory handle at module scope. Per CPython docs
        # (`os.add_dll_directory`), the returned object is "an opaque
        # value that has a `close()` method ... the returned object
        # remains valid until close() is called". On garbage
        # collection, the directory de-registers itself, so direct
        # `ctypes.CDLL(...)` callers that rely on the modern resolver
        # path would lose access to the bundle. Holding the handle on
        # the module keeps the registration live for the process
        # lifetime — matching the intent documented above for both
        # `find_library` (legacy) and `LoadLibraryEx` (modern) lookup
        # paths.
        _dll_dir_handle = _os.add_dll_directory(str(_libs_dir))
        _libs_str = str(_libs_dir)
        _existing_path = _os.environ.get("PATH", "")
        if _libs_str not in _existing_path.split(_os.pathsep):
            _os.environ["PATH"] = _libs_str + _os.pathsep + _existing_path
