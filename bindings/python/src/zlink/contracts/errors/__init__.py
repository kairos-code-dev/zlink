# SPDX-License-Identifier: MPL-2.0

_ERROR_NAMES = {
    "BindError",
    "CloseError",
    "ConfigError",
    "ConnectError",
    "HandlerError",
    "RecvError",
    "RequestError",
    "SubmitError",
    "ZlinkError",
}


def __getattr__(name):
    if name in _ERROR_NAMES:
        from . import errors

        value = getattr(errors, name)
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = sorted(_ERROR_NAMES)
