# Zlink ASIO + TLS Simplification Plan

This document captures the full work plan to simplify zlink into:
- ASIO-only I/O backend
- Protocol set: tcp, ipc (Unix only), inproc, ws, wss, tls
- Socket types: stream removed; keep pair, pub, sub, xpub, xsub, router, dealer
- TLS is available by default (OpenSSL required at build time)

The plan is written so work can be resumed without prior context.

---

## Decisions (Locked)

- Windows: no ipc support.
- `tls://` protocol will be added.
- `wss://` and `tls://` share the same TLS options.
- TLS defaults:
  - Client verify: ON by default.
  - Server mTLS: OFF by default.
- `ZMQ_STREAM` and `raw_socket` are removed.
- Build options remain but default to ON for ASIO/SSL/WS.

## Open Decisions (Need Explicit Confirmation)

- SOCKS proxy support: remove entirely.

---

## Supported Matrix (Target)

### Protocols
- `inproc://` (memory pipe, unchanged)
- `ipc://` (Unix only)
- `tcp://`
- `ws://`
- `wss://`
- `tls://` (new; TLS over TCP)

### Socket types
- keep: `ZMQ_PAIR`, `ZMQ_PUB`, `ZMQ_SUB`, `ZMQ_XPUB`, `ZMQ_XSUB`, `ZMQ_ROUTER`, `ZMQ_DEALER`
- remove: `ZMQ_STREAM`

---

## TLS Option API (Proposed)

Add stable socket options in `include/zmq.h`:

- `ZMQ_TLS_CERT` (string, server cert chain file)
- `ZMQ_TLS_KEY` (string, server private key file)
- `ZMQ_TLS_CA` (string, CA file for peer verification)
- `ZMQ_TLS_VERIFY` (int, default 1)
- `ZMQ_TLS_REQUIRE_CLIENT_CERT` (int, default 0)
- `ZMQ_TLS_HOSTNAME` (string, SNI + hostname verification)
- `ZMQ_TLS_TRUST_SYSTEM` (int, default 1)
- `ZMQ_TLS_PASSWORD` (string, optional key password)

Notes:
- Server requires `ZMQ_TLS_CERT` + `ZMQ_TLS_KEY` for `tls://`/`wss://`.
- Client verification uses `ZMQ_TLS_CA` or system store if enabled.
- `ZMQ_TLS_HOSTNAME` is used for SNI + hostname verification; when empty,
  use host extracted from the endpoint URI.
- `wss://` uses the same TLS options as `tls://`.
- Client default: `ZMQ_TLS_VERIFY=1`.
- Server default: `ZMQ_TLS_REQUIRE_CLIENT_CERT=0`.
- If `ZMQ_TLS_CA` is empty and `ZMQ_TLS_TRUST_SYSTEM=1`, use system CA store.

Legacy cleanup:
- Remove draft-only `ZMQ_WSS_*` options from `src/zmq_draft.h`.
- Remove `options.wss_hostname` and replace with `options.tls_hostname`.

---

## Work Plan (Execution Order)

### Phase 0: Baseline Snapshot
1) Record current build command(s) and tests.
2) Ensure `WITH_BOOST_ASIO`, `WITH_ASIO_SSL`, `WITH_ASIO_WS` build correctly.
3) Capture a list of tests that will be removed or updated.

### Phase 1: Build System Simplification
Files: `CMakeLists.txt`
1) Default these to ON:
   - `WITH_BOOST_ASIO`
   - `WITH_ASIO_SSL`
   - `WITH_ASIO_WS`
2) Keep options, but fail configure if OpenSSL is missing while SSL is ON.
3) Force `POLLER=asio` and remove non-ASIO poller selection logic.
4) Remove build options and discovery for:
   - OpenPGM (`WITH_OPENPGM`)
   - NORM (`WITH_NORM`)
   - VMCI (`WITH_VMCI`)
   - TIPC (auto-detect)
5) Remove GNUTLS/NSS paths and legacy `ENABLE_WS` path.
   - `ZMQ_HAVE_WS`/`ZMQ_HAVE_WSS` come only from ASIO WS + ASIO SSL.
6) Force `ZMQ_HAVE_IPC` OFF on Windows.

Acceptance:
- `cmake` fails fast if OpenSSL is missing.
- `POLLER` is always ASIO.

### Phase 2: Protocol/Transport Pruning
Files (examples, not exhaustive):
`src/address.hpp`, `src/address.cpp`, `src/socket_base.cpp`, `src/zmq.cpp`,
`src/ctx.*`, `src/ip.cpp`

1) Remove protocols and transports:
   - `tipc`, `vmci`, `pgm`, `epgm`, `norm`, `udp`
2) Remove `ZMQ_STREAM`:
   - constants in `include/zmq.h`
   - socket class `src/stream.*`
   - `stream_engine_base.*`, `raw_*`
   - `stream_listener_base.*`, `stream_connecter_base.*` (after ASIO IPC/TCP)
3) Remove raw-socket options:
   - `ZMQ_ROUTER_RAW`, `ZMQ_STREAM_NOTIFY`
   - `options.raw_socket`, `options.raw_notify`
4) Update `zmq_has` to only expose remaining protocols:
   - `inproc`, `ipc`, `tcp`, `ws`, `wss`, `tls`

Decision point:
- `socks_connecter` uses `stream_connecter_base`. Remove SOCKS support
  and delete these files with related options:
  - `src/socks_connecter.*`, `src/socks.cpp`
  - `ZMQ_SOCKS_PROXY`, `ZMQ_SOCKS_USERNAME`, `ZMQ_SOCKS_PASSWORD`

### Phase 3: Add `tls://` Protocol + TLS Options
Files:
`include/zmq.h`, `src/options.hpp`, `src/options.cpp`,
`src/address.hpp`, `src/address.cpp`, `src/socket_base.cpp`

1) Add `protocol_name::tls` in `src/address.hpp`.
2) Treat `tls://` as TCP address resolution.
3) Update `address_t::to_string` so `tls://` uses the correct prefix
   (needed for `ZMQ_LAST_ENDPOINT` and events).
4) Add TLS options to `include/zmq.h`.
5) Add TLS fields to `options_t` and wire into `setsockopt/getsockopt`.
6) Replace `wss_hostname` with `tls_hostname` and share for `wss://`.
7) Add `zmq_has("tls")` gated by ASIO SSL availability.

Acceptance:
- `zmq_connect("tls://...")` parses and resolves like tcp.
- TLS options are visible via `zmq_setsockopt/zmq_getsockopt`.

### Phase 4: ASIO Engine Integration (tcp/ipc/tls/ws/wss)
Files:
`src/asio/asio_engine.*`, `src/asio/asio_zmtp_engine.*`,
`src/asio/tcp_transport.*`, `src/asio/ssl_transport.*`,
`src/asio/asio_ws_engine.*`, `src/asio/wss_transport.*`,
`src/asio/asio_tcp_*`, `src/asio/asio_ws_*`

1) Make `asio_engine_t` transport-agnostic:
   - Add `std::unique_ptr<i_asio_transport> _transport`.
   - Use `tcp_transport_t` for tcp/ipc.
   - Use `ssl_transport_t` for `tls://`.
   - Remove direct socket/stream_descriptor fields from `asio_engine_t`.
2) TLS handshake flow:
   - Transport handshake first (SSL).
   - Then ZMTP handshake.
3) `asio_zmtp_engine_t`:
   - Accept a transport factory or protocol flag.
4) IPC (Unix):
   - Implement `asio_ipc_connecter_t` and `asio_ipc_listener_t`
     using `boost::asio::local::stream_protocol` (Unix only).
   - Windows: IPC is disabled; no add_fd usage allowed.
5) WSS:
   - Update `asio_ws_engine_t` to use `i_asio_transport`.
   - Instantiate `ws_transport_t` for ws and `wss_transport_t` for wss.
   - Use `tls_hostname` for SNI/verification when set.
6) Windows note:
   - `asio_poller_t::add_fd` is not implemented on Windows.
   - Ensure no code path uses `add_fd` once ASIO-only is enforced.

Acceptance:
- `tcp://` and `ipc://` both use ASIO engines.
- `tls://` performs SSL handshake then ZMTP handshake.
- `wss://` works via `wss_transport_t`.

### Phase 5: Remove Legacy I/O
Files:
`src/tcp_*`, `src/ipc_*`, `src/stream_*`, `src/raw_*`,
`src/epoll.*`, `src/kqueue.*`, `src/poll.*`, `src/select.*`

1) Remove non-ASIO pollers and legacy TCP/IPC engines.
2) Remove `stream_*` and `raw_*` after all transports are ASIO.
3) Remove `udp`, `pgm`, `norm`, `tipc`, `vmci` code and options.

### Phase 6: TLS Context Helper Updates
Files:
`src/asio/ssl_context_helper.*`

1) Add hostname verification (`rfc2818_verification`).
2) Add server verification modes:
   - `verify_none` (default)
   - `verify_peer` + `verify_fail_if_no_peer_cert` when mTLS ON
3) Map TLS failures to existing handshake error events.

### Phase 7: Tests
Remove tests:
- All `test_stream*`
- `test_pair_tipc.cpp`, `test_shutdown_stress_tipc.cpp`, `test_sub_forward_tipc.cpp`
- `test_pair_vmci.cpp`
- `test_pair_udp` (if any)
- PGM/NORM related tests

Update/add tests:
- Update `test_capabilities.cpp` to include `tls`/`wss`.
- Add `test_tls_tcp.cpp` or repurpose `test_asio_ssl.cpp` to use `tls://`.
- Extend `test_asio_ws.cpp` to include `wss://`.
- Guard ipc tests with `#ifdef ZMQ_HAVE_IPC` and skip on Windows.
- Update transport matrix tests (`test_pubsub_transports.cpp`,
  `test_pair_transports.cpp`, `test_router_transports.cpp`).

### Phase 8: Documentation
Update README/docs:
- Supported protocols and TLS configuration.
- Build flags and OpenSSL requirement.
- Migration notes (removed protocols, removed `ZMQ_STREAM`).

---

## Acceptance Criteria

- Build passes with default options (ASIO + SSL + WS on).
- `tls://` and `wss://` connect/bind with TLS options.
- `zmq_has("tls")`, `zmq_has("ws")`, `zmq_has("wss")` return true.
- No references remain to removed transports or `ZMQ_STREAM`.
- Tests updated and passing on Linux + Windows (Windows: no IPC).

---

## Risks / Notes

- Transport handshake ordering: TLS must complete before ZMTP handshake.
- Hostname verification must be correct to avoid silent MITM.
- Removing `stream_*` will break raw socket usage; ensure it is intended.
- IPC on Windows is explicitly not supported.

---

## Suggested Implementation Order (Short Form)

1) CMake defaults + remove unused build flags.
2) Add `tls://` protocol + TLS option API.
3) ASIO transport refactor (`tcp`/`tls`/`ipc`).
4) WSS integration.
5) Remove legacy transports/pollers + stream/raw.
6) Update tests/docs.
