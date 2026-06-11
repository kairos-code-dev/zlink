# SPDX-License-Identifier: MPL-2.0

from ...handles.native_support import SubmitError, SubmitResult


_NO_PAYLOAD = object()


def _ensure_not_submitted(op):
    if op._submitted:
        raise SubmitError(SubmitResult.INVALID_STATE, 0)


def _append_payload(op, payload):
    _ensure_not_submitted(op)
    if op._parts is not None:
        op._parts.append(payload)
    elif op._payload is _NO_PAYLOAD:
        op._payload = payload
    else:
        op._parts = [op._payload, payload]
        op._payload = _NO_PAYLOAD


def _extend_payloads(op, payloads):
    _ensure_not_submitted(op)
    if not payloads:
        return
    if op._parts is not None:
        op._parts.extend(payloads)
    elif op._payload is _NO_PAYLOAD:
        if len(payloads) == 1:
            op._payload = payloads[0]
        else:
            op._parts = list(payloads)
    else:
        op._parts = [op._payload, *payloads]
        op._payload = _NO_PAYLOAD


def _submitted_payload(op):
    _ensure_not_submitted(op)
    if op._parts is None:
        if op._payload is _NO_PAYLOAD:
            raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
        payload = op._payload
    elif op._parts:
        payload = op._parts
    else:
        raise SubmitError(SubmitResult.INVALID_ARGUMENT, 0)
    op._submitted = True
    return payload


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
        _append_payload(self, payload)
        return self

    def messages(self, *payloads):
        _extend_payloads(self, payloads)
        return self

    def flags(self, flags):
        _ensure_not_submitted(self)
        self._flags = int(flags)
        return self

    def submit(self):
        """Returns True on success, False on DONTWAIT backpressure. Raises SubmitError on failure."""
        payload = _submitted_payload(self)
        return self._op_fn(payload, self._flags)


class PublishOp:
    """Fluent builder for Spot publish operations."""
    __slots__ = ("_spot", "_topic", "_payload", "_parts", "_flags", "_submitted")

    def __init__(self, spot, topic):
        self._spot = spot
        self._topic = spot._publish_topic_bytes(topic)
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
        return self._spot._publish_submit_prevalidated(
            self._topic, payload, self._flags
        )


class RequestOp:
    """Fluent builder for Spot request callback operations."""
    __slots__ = ('_spot', '_op_cb_fn', '_parts', '_timeout', '_submitted')

    def __init__(self, spot, op_cb_fn):
        self._spot = spot
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
