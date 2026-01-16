# Phase 5 Changes (Speculative Read + Strand Rollback)

## Summary
- Removed strand serialization from `asio_engine` (io_context is single-threaded; strand caused head-of-line blocking).
- Added synchronous `read_some()` to `i_asio_transport` and implemented in TCP/IPC/SSL; WS/WSS returns EAGAIN unless frame buffer has data.
- Added `asio_engine_t::speculative_read()` and invoked it after backpressure clears in `restart_input_internal()` before re-arming async read.
- Disabled speculative synchronous writes for IPC by default (opt-in via `ZMQ_ASIO_IPC_SYNC_WRITE`) and gated speculative writes via `supports_speculative_write()`.
- IPC stats extended to include `read_some` counters.

## Files Modified
- `src/asio/i_asio_transport.hpp`
- `src/asio/asio_engine.hpp`
- `src/asio/asio_engine.cpp`
- `src/asio/asio_ws_engine.cpp`
- `src/asio/tcp_transport.hpp`
- `src/asio/tcp_transport.cpp`
- `src/asio/ipc_transport.hpp`
- `src/asio/ipc_transport.cpp`
- `src/asio/ssl_transport.hpp`
- `src/asio/ssl_transport.cpp`
- `src/asio/ws_transport.hpp`
- `src/asio/ws_transport.cpp`
- `src/asio/wss_transport.hpp`
- `src/asio/wss_transport.cpp`
