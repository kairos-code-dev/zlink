import ctypes
import ctypes.util
import os

_lib = None


def _load_lib():
    global _lib
    if _lib is not None:
        return _lib

    path = os.environ.get("ZLINK_LIBRARY_PATH")
    if not path:
        found = ctypes.util.find_library("zlink")
        if found:
            path = found
    if not path:
        raise OSError("zlink native library not found")

    _lib = ctypes.CDLL(path)

    _lib.zlink_version.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    _lib.zlink_version.restype = None

    return _lib


def version():
    lib = _load_lib()
    major = ctypes.c_int()
    minor = ctypes.c_int()
    patch = ctypes.c_int()
    lib.zlink_version(ctypes.byref(major), ctypes.byref(minor), ctypes.byref(patch))
    return major.value, minor.value, patch.value
