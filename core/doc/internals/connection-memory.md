[한국어](connection-memory.ko.md)

# Per-connection memory

Each transport connection allocates a session, engine state, pipe endpoints,
handshake buffers, and kernel socket buffers. Queued message storage grows with
effective message size and HWM rather than with a single fixed connection cost.

## Stable components

- session and engine objects;
- pipe metadata and queue chunks;
- routing-id and endpoint metadata;
- protocol handshake state;
- operating-system socket structures.

## Variable components

Send and receive queues retain message objects and referenced payload storage.
Kernel buffers may grow according to platform autotuning. TLS adds record and
handshake storage. Monitor snapshots report the applied HWM plan but do not
measure all allocator and kernel overhead.

Capacity planning must measure idle, post-traffic residual, and burst peak
memory with the production transport and message-size distribution.
