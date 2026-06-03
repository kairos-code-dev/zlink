# SPDX-License-Identifier: MPL-2.0

from ...handles.native_support import SubmitError, SubmitResult


_NO_PAYLOAD = object()


class SendOp:
    """Fluent builder for Spot send operations."""
    __slots__ = ('_spot', '_op_fn', '_payload', '_parts', '_flags', '_submitted')

    def __init__(self, spot, op_fn):
        self._spot = spot
        self._op_fn = op_fn
        self._payload = _NO_PAYLOAD
        self._parts = None
        self._flags = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if self._parts is not None:
            self._parts.append(payload)
        elif self._payload is _NO_PAYLOAD:
            self._payload = payload
        else:
            self._parts = [self._payload, payload]
            self._payload = _NO_PAYLOAD
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not payloads:
            return self
        if self._parts is not None:
            self._parts.extend(payloads)
        elif self._payload is _NO_PAYLOAD:
            if len(payloads) == 1:
                self._payload = payloads[0]
            else:
                self._parts = list(payloads)
        else:
            self._parts = [self._payload, *payloads]
            self._payload = _NO_PAYLOAD
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self):
        """Returns True on success, False on DONTWAIT backpressure. Raises SubmitError on failure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if self._parts is None:
            if self._payload is _NO_PAYLOAD:
                raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
            payload = self._payload
        elif self._parts:
            payload = self._parts
        else:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_fn(payload, self._flags)


class RequestOp:
    """Fluent builder for Spot request operations (async or callback)."""
    __slots__ = ('_spot', '_op_async_fn', '_op_cb_fn', '_parts', '_timeout', '_submitted')

    def __init__(self, spot, op_async_fn, op_cb_fn):
        self._spot = spot
        self._op_async_fn = op_async_fn
        self._op_cb_fn = op_cb_fn
        self._parts = []
        self._timeout = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        """Calling flags() transitions to a RequestCallbackOp."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._submitted = True
        return RequestCallbackOp(self._spot, self._op_cb_fn, self._parts, self._timeout, int(flags))

    def submit_async(self):
        """Submit as async coroutine. Returns awaitable list[Message]."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_async_fn(self._parts, timeout=self._timeout)

    def submit(self, callback):
        """Submit with callback. Returns True/False for backpressure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_cb_fn(self._parts, callback, flags=0, timeout=self._timeout)


class RequestCallbackOp:
    """Request operation builder after flags() has been called - async submit is not available."""
    __slots__ = ('_spot', '_op_cb_fn', '_parts', '_timeout', '_flags', '_submitted')

    def __init__(self, spot, op_cb_fn, parts, timeout, flags):
        self._spot = spot
        self._op_cb_fn = op_cb_fn
        self._parts = parts
        self._timeout = timeout
        self._flags = flags
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def timeout(self, timeout):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._timeout = timeout
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self, callback):
        """Submit with callback. Returns True/False for backpressure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        return self._op_cb_fn(self._parts, callback, flags=self._flags, timeout=self._timeout)


class ReplyOp:
    """Fluent builder for Spot reply operations."""
    __slots__ = ('_op_fn', '_parts', '_flags', '_submitted')

    def __init__(self, op_fn):
        self._op_fn = op_fn
        self._parts = []
        self._flags = 0
        self._submitted = False

    def message(self, payload):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.append(payload)
        return self

    def messages(self, *payloads):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._parts.extend(payloads)
        return self

    def flags(self, flags):
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        self._flags = int(flags)
        return self

    def submit(self):
        """Submit the reply. Raises SubmitError on failure."""
        if self._submitted:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        if not self._parts:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        self._submitted = True
        self._op_fn(self._parts, self._flags)
