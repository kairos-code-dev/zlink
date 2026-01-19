# Implementation Notes

**Date:** 2026-01-21  
**Owner:** 팀장님

---

## Stage A: ZMTP Lite

### Target Areas
- `src/asio/asio_zmtp_engine.cpp`
- `src/asio/asio_zmtp_engine.hpp`
- `src/asio/asio_engine.cpp`
- `src/null_mechanism.cpp`
- `src/v2_encoder.cpp`
- `src/v2_decoder.cpp`

### Changes (Done)
- Removed legacy ZMTP 1.0/2.0/3.0 handshake paths in `src/asio/asio_zmtp_engine.cpp`.
- Enforced ZMTP 3.1 greeting/minor version only.
- Restricted mechanism negotiation to NULL only.
- Updated `tests/test_mock_pub_sub.cpp` to drop legacy (non-command) mock paths.
- Kept routing-id flow unchanged.

---

## Stage B: ZMP Format

### Target Areas
- `src/asio/asio_zmp_engine.cpp`
- `src/zmp_encoder.cpp`
- `src/zmp_decoder.cpp`
- `src/zmp_protocol.hpp`

### Changes (Done)
- Added ZMP encoder/decoder and protocol header.
- Implemented `asio_zmp_engine_t` for ZMP framing.
- Wired TCP/IPC/TLS/WS engines to choose ZMP via `ZLINK_PROTOCOL=zmp`.
- Kept routing-id handling aligned with ZMTP paths.

---

## Testing

- `ctest --output-on-failure`
- Targeted protocol tests for ROUTER and PUB/SUB.
