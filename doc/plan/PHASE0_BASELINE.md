# Phase 0: Baseline Snapshot

**Date**: 2026-01-13
**Purpose**: Record current state before ASIO + TLS simplification work begins
**Branch**: feature/perf-improvements

## Build Configuration

### CMake Command
```bash
cmake -B /home/ulalax/project/ulalax/zlink/build \
  -DWITH_BOOST_ASIO=ON \
  -DWITH_ASIO_SSL=ON \
  -DWITH_ASIO_WS=ON \
  -DBUILD_TESTS=ON
```

### Configuration Output
- **ZMQ Version**: 4.3.5
- **Boost.Asio**: Enabled (headers found at `/home/ulalax/project/ulalax/zlink/external/boost`)
- **I/O Backend**: Boost.Asio (Phase 1-A: Reactor Mode)
- **OpenSSL**: Found (version 3.0.13)
- **Boost.Beast**: Found (WebSocket support)
- **WSS**: Enabled (Secure WebSocket)
- **WebSocket Transport**: Enabled (ZMQ_HAVE_WS via ASIO)
- **Secure WebSocket Transport**: Enabled (ZMQ_HAVE_WSS via ASIO)
- **Draft API**: Forced OFF (zlink policy)
- **Polling Method (I/O threads)**: asio
- **Polling Method (zmq_poll API)**: poll

### Build Result
**Status**: ✅ SUCCESS

```
[100%] Built target test_asio_ws
```

All targets built successfully:
- libzmq (static + shared)
- All test executables
- All unittest executables
- ASIO-specific tests: test_asio_poller, test_asio_connect, test_asio_tcp, test_asio_ssl, test_asio_ws

## Test Results

### Test Execution Command
```bash
cd /home/ulalax/project/ulalax/zlink/build && ctest --output-on-failure
```

### Summary
- **Total Tests**: 71
- **Passed**: 64 (100% pass rate for executed tests)
- **Skipped**: 7
- **Failed**: 0
- **Total Time**: 47.61 seconds

### Test Breakdown

#### Passed Tests (64)
1. test_ancillaries (0.00s)
2. test_system (0.01s)
3. test_pair_inproc (0.00s)
4. test_pair_tcp (0.00s)
5. test_hwm_pubsub (3.17s)
6. test_sub_forward (0.30s)
7. test_msg_flags (0.00s)
8. test_msg_ffn (1.20s)
9. test_connect_resolve (0.00s)
10. test_immediate (3.05s)
11. test_last_endpoint (0.00s)
12. test_router_mandatory (0.00s)
13. test_probe_router (0.00s)
14. test_stream (0.00s)
15. test_stream_empty (0.00s)
16. test_stream_disconnect (0.00s)
17. test_disconnect_inproc (0.60s)
18. test_ctx_options (0.00s)
19. test_ctx_destroy (0.30s)
20. test_spec_router (1.20s)
21. test_issue_566 (0.22s)
22. test_shutdown_stress (0.13s)
23. test_timeo (0.25s)
24. test_many_sockets (0.91s)
25. test_diffserv (0.00s)
26. test_connect_rid (0.61s)
27. test_xpub_nodrop (0.25s)
28. test_pub_invert_matching (1.20s)
29. test_heartbeats (0.10s)
30. test_bind_src_address (0.00s)
31. test_capabilities (0.00s)
32. test_router_handover (0.91s)
33. test_stream_timeout (0.20s)
34. test_xpub_manual (3.91s)
35. test_xpub_topic (0.00s)
36. test_xpub_welcome_msg (0.00s)
37. test_xpub_verbose (0.00s)
38. test_bind_after_connect_tcp (0.00s)
39. test_socket_null (0.00s)
40. test_reconnect_ivl (7.52s)
41. test_reconnect_options (3.73s)
42. test_mock_pub_sub (0.09s)
43. test_pair_transports (1.51s)
44. test_pubsub_transports (2.41s)
45. test_router_transports (2.41s)
46. test_ipc_wildcard (0.00s)
47. test_pair_ipc (0.00s)
48. test_proxy (1.50s)
49. test_getsockopt_memset (0.00s)
50. test_stream_exceeds_buffer (0.00s)
51. test_router_mandatory_hwm (1.00s)
52. test_abstract_ipc (0.00s)
53. test_router_mandatory_tipc (0.00s)
54. **test_asio_poller** (1.91s) ⭐
55. **test_asio_connect** (2.36s) ⭐
56. **test_asio_tcp** (3.33s) ⭐
57. **test_asio_ssl** (0.31s) ⭐
58. **test_asio_ws** (0.81s) ⭐
59. unittest_ypipe (0.00s)
60. unittest_poller (0.05s)
61. unittest_mtrie (0.00s)
62. unittest_ip_resolver (0.00s)
63. unittest_udp_address (0.00s)
64. unittest_radix_tree (0.00s)

⭐ = ASIO-specific tests

#### Skipped Tests (7)
1. test_connect_null_fuzzer (fuzzer test)
2. test_bind_null_fuzzer (fuzzer test)
3. test_connect_fuzzer (fuzzer test)
4. test_bind_fuzzer (fuzzer test)
5. test_pair_tipc (TIPC protocol - Linux kernel specific)
6. test_sub_forward_tipc (TIPC protocol)
7. test_shutdown_stress_tipc (TIPC protocol)

## Tests Identified for Removal

### ZMQ_STREAM Socket Type Tests (5 files)
These tests will be removed when ZMQ_STREAM socket type is eliminated:

1. `/home/ulalax/project/ulalax/zlink/tests/test_stream.cpp`
2. `/home/ulalax/project/ulalax/zlink/tests/test_stream_disconnect.cpp`
3. `/home/ulalax/project/ulalax/zlink/tests/test_stream_empty.cpp`
4. `/home/ulalax/project/ulalax/zlink/tests/test_stream_exceeds_buffer.cpp`
5. `/home/ulalax/project/ulalax/zlink/tests/test_stream_timeout.cpp`

**Current Status**: All 5 tests currently PASS

### TIPC Protocol Tests (4 files)
TIPC (Transparent Inter-Process Communication) is a Linux kernel protocol not needed:

1. `/home/ulalax/project/ulalax/zlink/tests/test_pair_tipc.cpp`
2. `/home/ulalax/project/ulalax/zlink/tests/test_router_mandatory_tipc.cpp`
3. `/home/ulalax/project/ulalax/zlink/tests/test_shutdown_stress_tipc.cpp`
4. `/home/ulalax/project/ulalax/zlink/tests/test_sub_forward_tipc.cpp`

**Current Status**:
- 3 skipped (pair_tipc, sub_forward_tipc, shutdown_stress_tipc)
- 1 passed (router_mandatory_tipc)

### VMCI Protocol Tests (1 file)
VMCI (Virtual Machine Communication Interface) is VMware-specific:

1. `/home/ulalax/project/ulalax/zlink/tests/test_pair_vmci.cpp`

**Current Status**: Not executed (likely skipped or not built)

### PGM/NORM References
Files containing PGM (Pragmatic General Multicast) or NORM references:

1. `/home/ulalax/project/ulalax/zlink/tests/test_capabilities.cpp` - Contains capability checks for PGM/NORM
2. `/home/ulalax/project/ulalax/zlink/tests/test_timeo.cpp` - Contains "normal" word (not PGM-related)
3. `/home/ulalax/project/ulalax/zlink/tests/test_pubsub.cpp` - May contain PGM references

**Note**: test_capabilities.cpp will need PGM/NORM capability checks removed, but the test itself should remain.

## Source Code Structure

### ASIO Implementation Files (Built)
The following ASIO-related source files were compiled:

#### Core ASIO Infrastructure
- `src/asio/asio_poller.cpp`
- `src/asio/asio_engine.cpp`
- `src/asio/asio_zmtp_engine.cpp`

#### TCP with ASIO
- `src/asio/asio_tcp_listener.cpp`
- `src/asio/asio_tcp_connecter.cpp`
- `src/asio/tcp_transport.cpp`

#### SSL/TLS with ASIO
- `src/asio/ssl_transport.cpp`
- `src/asio/ssl_context_helper.cpp`

#### WebSocket with ASIO
- `src/asio/asio_ws_engine.cpp`
- `src/asio/asio_ws_listener.cpp`
- `src/asio/asio_ws_connecter.cpp`
- `src/asio/ws_transport.cpp`
- `src/ws_address.cpp`

#### Secure WebSocket (WSS) with ASIO
- `src/asio/wss_transport.cpp`
- `src/wss_address.cpp`

#### Legacy Transport (Built alongside ASIO)
- `src/tcp_connecter.cpp`
- `src/tcp_listener.cpp`
- `src/ipc_connecter.cpp`
- `src/ipc_listener.cpp`
- `src/tipc_connecter.cpp`
- `src/tipc_listener.cpp`

## Next Steps (Post-Baseline)

Based on this baseline, the simplification plan should:

1. **Remove ZMQ_STREAM socket type**
   - Delete 5 test files
   - Remove socket implementation code
   - Update CMakeLists.txt

2. **Remove TIPC, VMCI protocols**
   - Delete 5 test files
   - Remove protocol implementation code
   - Update build configuration

3. **Clean up PGM/NORM references**
   - Update test_capabilities.cpp to remove PGM/NORM checks

4. **Verify ASIO-only build works**
   - Ensure 5 ASIO tests continue to pass
   - Verify no regressions in remaining 59 tests

## Build Artifacts

### Binary Locations
- Static library: `build/lib/libzmq.a`
- Shared library: `build/lib/libzmq.so`
- Test binaries: `build/bin/test_*`
- Unit test binaries: `build/bin/unittest_*`

### Expected Test Count After Cleanup
- Current: 71 tests (64 passed, 7 skipped)
- After removing STREAM tests: 66 tests (-5)
- After removing TIPC tests: 62 tests (-4)
- After removing VMCI tests: 61 tests (-1)
- **Final Expected**: ~61 tests

## Baseline Verification

### Checksum/Version Info
```
ZMQ Version: 4.3.5
OpenSSL Version: 3.0.13
Platform: Linux 6.6.87.2-microsoft-standard-WSL2
Build Date: 2026-01-13
```

### Key Build Flags
- `WITH_BOOST_ASIO=ON`
- `WITH_ASIO_SSL=ON`
- `WITH_ASIO_WS=ON`
- `BUILD_TESTS=ON`
- Draft API: OFF (forced by zlink)

---

**Conclusion**: The baseline build is healthy with 100% test pass rate for executed tests. All ASIO features (TCP, SSL, WebSocket, WSS) are functional and tested. Ready to proceed with simplification phases.
