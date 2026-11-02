"""The handful of raven_web calls the user-guide pane needs.

The generated Python binding covers Raven.h; the web view lives in the same
shared library but has its own header, so this module declares the calls it
uses with ctypes. Nothing is vendored: the symbols are in libraven.
"""

import ctypes

import raven as _raven

_lib = _raven._lib
AVAILABLE = hasattr(_lib, "Rweb_view_create")


if AVAILABLE:
    _lib.Rweb_is_available.restype = ctypes.c_int
    _lib.Rweb_set_browser_path.argtypes = [ctypes.c_char_p]
    _lib.Rweb_set_browser_path.restype = None
    _lib.Rweb_view_create.restype = ctypes.c_uint32
    _lib.Rweb_view_load_url.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
    _lib.Rweb_view_load_url.restype = None
    _lib.Rweb_view_reload.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_reload.restype = None
    _lib.Rweb_view_go_back.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_go_back.restype = None
    _lib.Rweb_view_go_forward.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_go_forward.restype = None
    _lib.Rweb_view_can_go_back.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_can_go_back.restype = ctypes.c_int
    _lib.Rweb_view_can_go_forward.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_can_go_forward.restype = ctypes.c_int
    _lib.Rweb_view_get_title.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_get_title.restype = ctypes.c_char_p
    _lib.Rweb_view_is_loading.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_is_loading.restype = ctypes.c_int
    _lib.Rweb_view_get_error.argtypes = [ctypes.c_uint32]
    _lib.Rweb_view_get_error.restype = ctypes.c_char_p


def is_available():
    return bool(AVAILABLE and _lib.Rweb_is_available())


def set_browser_path(path):
    if AVAILABLE:
        _lib.Rweb_set_browser_path(path.encode())


def view_create():
    return _lib.Rweb_view_create()


def load_url(view, url):
    _lib.Rweb_view_load_url(view, url.encode())


def reload(view):
    _lib.Rweb_view_reload(view)


def go_back(view):
    _lib.Rweb_view_go_back(view)


def go_forward(view):
    _lib.Rweb_view_go_forward(view)


def can_go_back(view):
    return bool(_lib.Rweb_view_can_go_back(view))


def can_go_forward(view):
    return bool(_lib.Rweb_view_can_go_forward(view))


def get_title(view):
    value = _lib.Rweb_view_get_title(view)
    return value.decode() if value else ""


def is_loading(view):
    return bool(_lib.Rweb_view_is_loading(view))


def get_error(view):
    value = _lib.Rweb_view_get_error(view)
    return value.decode() if value else ""
