# SPDX-License-Identifier: MPL-2.0

from .codes import BindResult, CloseResult, ConfigResult, ConnectResult
from ..sockets.codes import HandlerResult, RecvResult, RequestResult, SubmitResult


class ZlinkError(RuntimeError):
    """Base class for all errors raised by the zlink bindings."""

    def __init__(self, code: int, internal_errno: int = 0):
        self._code = int(code)
        self._internal_errno = int(internal_errno)
        super().__init__(
            f"{self.__class__.__name__}(code={self._code}, internal_errno={self._internal_errno})"
        )

    @property
    def code(self):
        """The zlink result code that classifies this failure."""
        return self._code

    @property
    def internal_errno(self):
        """The native errno that produced this failure, or 0 when none."""
        return self._internal_errno


class _TypedZlinkError(ZlinkError):
    """Base for errors that expose their result as a typed enum via
    :attr:`result`."""

    _result_type = None

    def __init__(self, result, internal_errno: int = 0):
        if self._result_type is None:
            raise TypeError("typed zlink error missing result type")
        self._result = self._result_type(int(result))
        super().__init__(int(self._result), internal_errno)

    @property
    def result(self):
        """The typed result enum classifying this failure."""
        return self._result


class SubmitError(_TypedZlinkError):
    """Raised when submitting a send or publish fails."""

    _result_type = SubmitResult


class RequestError(_TypedZlinkError):
    """Raised when a request fails or its reply reports an error."""

    _result_type = RequestResult


class RecvError(_TypedZlinkError):
    """Raised when receiving a message fails."""

    _result_type = RecvResult


class HandlerError(_TypedZlinkError):
    """Raised when registering or running a callback handler fails."""

    _result_type = HandlerResult


class CloseError(_TypedZlinkError):
    """Raised when closing a socket or resource fails."""

    _result_type = CloseResult


class BindError(_TypedZlinkError):
    """Raised when binding a socket to an endpoint fails."""

    _result_type = BindResult


class ConnectError(_TypedZlinkError):
    """Raised when connecting a socket to an endpoint fails."""

    _result_type = ConnectResult


class ConfigError(_TypedZlinkError):
    """Raised when reading or applying a configuration option fails."""

    _result_type = ConfigResult
