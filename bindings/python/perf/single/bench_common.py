#!/usr/bin/env python3
"""Compatibility shim for legacy fastpath benchmarks/tests."""

from __future__ import annotations

import time

try:
    from perf_common import wait_for_input as _wait_for_input
    from perf_common import wait_until as _wait_until
except Exception:  # pragma: no cover
    _wait_for_input = None
    _wait_until = None


FASTPATH_CEXT = None


def make_cext_send_many_const(*_args, **_kwargs):
    return None


def make_cext_recv_many_into(*_args, **_kwargs):
    return None


def make_cext_send_routed_many_const(*_args, **_kwargs):
    return None


def make_cext_recv_pair_many_into(*_args, **_kwargs):
    return None


def make_cext_recv_pair_drain_into(*_args, **_kwargs):
    return None


def make_cext_gateway_send_many_const(*_args, **_kwargs):
    return None


def make_cext_spot_publish_many_const(*_args, **_kwargs):
    return None


def make_cext_spot_recv_many(*_args, **_kwargs):
    return None


def wait_for_input(sock, timeout_ms, waiter=None):
    if _wait_for_input is None:
        return False
    try:
        if waiter is None:
            return bool(_wait_for_input(sock, timeout_ms))
        return bool(_wait_for_input(sock, timeout_ms, waiter))
    except TypeError:
        return bool(_wait_for_input(sock, timeout_ms))


def wait_until(fn, timeout_ms, interval_ms=10):
    if _wait_until is not None:
        try:
            return bool(_wait_until(fn, timeout_ms, interval_ms))
        except TypeError:
            return bool(_wait_until(fn, timeout_ms))

    deadline = time.time() + (timeout_ms / 1000.0)
    while time.time() < deadline:
        try:
            if fn():
                return True
        except Exception:
            pass
        time.sleep(interval_ms / 1000.0)
    return False
