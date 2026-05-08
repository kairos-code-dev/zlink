# Java Porting Issue Report

## Scope
This report covers issues found while porting core tests to `bindings/java` without skip/disable workarounds.

## Core Test Porting Coverage (default `core/tests/CMakeLists.txt` set)
- Core default test tokens: `55`
- Ported in Java: `52`
- Remaining not ported: `3`

## Remaining not ported (and why)
### 1) `test_ancillaries`
- Reason: C-level ancillary/FD control path is not exposed through current Java binding API surface.

### 2) `test_msg_ffn`
- Reason: `zlink_msg_init_data` free-function callback ownership path is not exposed in Java `Message` API.

### 3) `test_zmp_metadata`
- Reason: C API metadata accessor path (`zlink_msg_gets`) is not exposed in Java `Message`.

## Current failing ported test
### `TestSpotSendBlockingWakeupPortedTest`
- Source equivalent: `core/tests/spot/test_spot_send_blocking_wakeup.cpp`
- Command:
  - `cd bindings/java`
  - `./gradlew integrationTest --no-daemon --tests systems.zlink.integration.TestSpotSendBlockingWakeupPortedTest`
- Result: **2/2 fail**
- Failure detail:
  - `spot topology not ready: ReadyProbeResult[ready=false, sendOk=0, sendFail=..., recvOk=0, recvFail=0]`
  - `sendOk=0` means every `spotPub.publish(..., DONTWAIT)` call failed during readiness window.

### Interpretation
- Peer visibility API reports that peer connection exists (`pubPeers/subPeers` non-empty),
  but spot publish path never becomes writable and never delivers probe messages.
- This points to either:
  - native spot direct peer path issue (`zlink_spot_node_connect_peer_pub` + subscription propagation), or
  - Java spot binding behavior mismatch for this path.

## Resolved during this pass
### Newly ported from `core/tests`
- `test_connect_resolve` -> `TestConnectResolvePortedTest`
- `routing-id/test_connect_rid_string_alias` -> `TestConnectRidStringAliasPortedTest`
- `test_socket_null` -> `CoreSocketNullPortedTest`
- `test_pair_send_blocking_wakeup` -> `TestPairSendBlockingWakeupPortedTest`
- `test_bind_src_address` -> `TestBindSrcAddressPortedTest`
- `test_issue_566` -> `TestIssue566PortedTest`
- `test_shutdown_stress` -> `TestShutdownStressPortedTest`
- `test_spec_router` -> `TestSpecRouterPortedTest`
- `test_stream_fastpath` -> `TestStreamFastpathPortedTest`
- `test_stream_send_blocking_wakeup` -> `TestStreamSendBlockingWakeupPortedTest`
- `spot/test_spot_pubsub_scenario` -> `TestSpotPubsubScenarioPortedTest`
- `test_zmp_ws_wss` -> `TestZmpWsWssPortedTest`

## Validation status
- `./gradlew test --no-daemon`: pass
- Targeted new-port tests: pass (class-level runs)
- `./gradlew integrationTest --no-daemon`: process-abort observed in full-suite run:
  - Assertion: `!has_out_pipe (routing_id)` at `core/src/sockets/router.cpp:426`
  - This abort is reproducible in full-suite execution and is separate from the known spot blocking failure.
