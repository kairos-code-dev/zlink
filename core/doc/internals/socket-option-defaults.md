[한국어](socket-option-defaults.ko.md)

# Socket option defaults

`options_t` stores common raw-socket and transport defaults. Typed socket
implementations validate pattern-specific options before applying them.

## Queue planning

Automatic HWM selects a policy from the raw socket role, profile, effective
message size, and observed connection count. The plan produces bounded send and
receive HWM values and optional kernel-buffer values. Hysteresis prevents rapid
bucket changes near a connection-count boundary.

## Application-visible state

`zlink_monitor_status()` exposes the applied plan, input values, selected
bucket, and deferred shrink targets. These fields are diagnostic snapshots;
applications should configure policy inputs through public options rather than
mutating internal values.

## Transport defaults

Reconnect, TCP keepalive, kernel buffers, TOS, handshake intervals, and TLS
fields are applied by the relevant transport. Unsupported combinations fail
through the typed configuration result.
