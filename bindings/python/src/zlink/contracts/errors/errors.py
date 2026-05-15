# SPDX-License-Identifier: MPL-2.0

from ..enums.enums import (
    BindResult,
    CloseResult,
    ConfigResult,
    ConnectResult,
    HandlerResult,
    RecvResult,
    RequestResult,
    SubmitResult,
)


class ZlinkError(RuntimeError):
    def __init__(self, code: int, internal_errno: int = 0):
        self._code = int(code)
        self._internal_errno = int(internal_errno)
        super().__init__(
            f"{self.__class__.__name__}(code={self._code}, internal_errno={self._internal_errno})"
        )

    @property
    def code(self):
        return self._code

    @property
    def internal_errno(self):
        return self._internal_errno


class _TypedZlinkError(ZlinkError):
    _result_type = None

    def __init__(self, result, internal_errno: int = 0):
        if self._result_type is None:
            raise TypeError("typed zlink error missing result type")
        self._result = self._result_type(int(result))
        super().__init__(int(self._result), internal_errno)

    @property
    def result(self):
        return self._result


class SubmitError(_TypedZlinkError):
    _result_type = SubmitResult


class RequestError(_TypedZlinkError):
    _result_type = RequestResult


class RecvError(_TypedZlinkError):
    _result_type = RecvResult


class HandlerError(_TypedZlinkError):
    _result_type = HandlerResult


class CloseError(_TypedZlinkError):
    _result_type = CloseResult


class BindError(_TypedZlinkError):
    _result_type = BindResult


class ConnectError(_TypedZlinkError):
    _result_type = ConnectResult


class ConfigError(_TypedZlinkError):
    _result_type = ConfigResult
