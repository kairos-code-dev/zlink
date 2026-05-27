# SPDX-License-Identifier: MPL-2.0

_MONITOR_NAMES = {
    "MonitorEvent",
    "MonitorSnapshot",
    "MonitorSocket",
    "SocketMonitorEvent",
}
_POLLER_NAMES = {"Poller", "PollEvent", "PollEvents"}
_TIMER_NAMES = {"Timer"}


def __getattr__(name):
    if name in _MONITOR_NAMES:
        from . import monitor

        value = getattr(monitor, name)
        globals()[name] = value
        return value
    if name in _POLLER_NAMES:
        from . import poller

        value = getattr(poller, name)
        globals()[name] = value
        return value
    if name in _TIMER_NAMES:
        from . import timer

        value = getattr(timer, name)
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = sorted(_MONITOR_NAMES | _POLLER_NAMES | _TIMER_NAMES)
