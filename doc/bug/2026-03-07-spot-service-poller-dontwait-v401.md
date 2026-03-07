# [Core Bug Report] `zlink_poller_add_spot_pub` 이후 `ZLINK_DONTWAIT` publish 불가 (`core/v4.0.1`)

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.1`
- Runtime sync: native libraries refreshed after `core/v4.0.1`
- Affected area: `Spot` service-instance poller publish path

## Summary

`Spot` service-instance poller 경로에서 publisher를 poller에 등록한 뒤
`zlink_spot_pub_publish(..., ZLINK_DONTWAIT)`를 호출하면 nonblocking send가
성공하지 않습니다.

현재 확인된 신호는 두 가지입니다.

1. C++ 최소 재현:
   - `add_spot_pub(...)` 이후 `publish(..., dontwait)`가 `-1 / errno=95(ENOTSUP)`
   - 같은 조건에서 blocking `publish(..., none)`는 성공
2. Java perf direct run:
   - `MULTI_SPOT` server의 첫 publish가 `zlink_spot_pub_publish failed`
   - stderr:
     `Operation cannot be accomplished in current state (errno=156384763)`

즉, service-instance poller publish 경로에서 문서상의 nonblocking send 정책을
그대로 적용할 수 없습니다.

## Why This Matters

`MULTI_SPOT`은 multi perf 정책상 아래를 만족해야 합니다.

- send는 shared event loop에서 blocking 금지
- `send(..., dontwait)` 1회 시도
- `EAGAIN`이면 pending만 남기고 `PollOut` readiness에서만 재개

그런데 현재 core `Spot` publish path는 `DONTWAIT` 자체가 성공 경로로 동작하지
않습니다. Java perf 쪽에서 blocking publish로 우회하면 정책 위반입니다.

## Expected

- `zlink_poller_add_spot_pub(...)`로 등록된 `SpotPub` service instance가
  `zlink_spot_pub_publish(..., ZLINK_DONTWAIT)`를 정상 지원해야 함
- writable이면 성공
- backpressure이면 `EAGAIN`
- unsupported/error state면 문서에 명시된 일관된 errno가 나와야 함

## Actual

### C++ minimal reproduction

조건:

- `SpotNode A`: `bind(pubEndpoint)`
- `SpotNode B`: `bind(subEndpoint)` 후 `connect_peer_pub(pubEndpoint)`
- `Spot B`: `subscribe("tcp:test")`
- `Spot A`: `poller.add_spot_pub(spotA, 0)`

관측:

```text
dontwait_rc=-1 errno=95
blocking_rc=0 errno=11
```

즉 `dontwait`는 `ENOTSUP`, blocking은 성공합니다.

### Java direct MULTI_SPOT reproduction

Server:

```bash
java --enable-native-access=ALL-UNNAMED \
  -cp bindings/java/perf/multi/Zlink.PerfBench/build/classes/java/main:bindings/java/build/classes/java/main:bindings/java/build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-server MULTI_SPOT tcp 64
```

Client:

```bash
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=0 \
PERF_CONNECT_READY_TIMEOUT_MS=5000 PERF_CLIENT_POLL_TIMEOUT_MS=10 \
java --enable-native-access=ALL-UNNAMED \
  -cp bindings/java/perf/multi/Zlink.PerfBench/build/classes/java/main:bindings/java/build/classes/java/main:bindings/java/build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client MULTI_SPOT tcp 64 --endpoint <READY endpoint>
```

Observed client stderr:

```text
ERROR,MULTI_SPOT,client,no_active_frames
```

Observed server stderr after interrupt:

```text
ERROR,MULTI_SPOT,server,ZlinkException,zlink_spot_pub_publish failed: Operation cannot be accomplished in current state (errno=156384763)
```

## Additional Evidence

기본 service-instance poller 자체는 완전히 깨진 것은 아닙니다.

Java integration tests added during this investigation:

- `TestSpotServicePollerPortedTest.testSpotSubCanBePolledViaServiceInstance`
- `TestSpotServicePollerPortedTest.testSpotPubCanBePolledViaServiceInstance`

이 두 테스트는 blocking/facade 경로에서는 통과합니다.

즉 문제는 "poller 등록 자체"보다는
`addSpotPub + publish(..., DONTWAIT)` 조합에 집중되어 있습니다.

## Reproduction Snippet (C++)

```cpp
zlink::service::spot_node_t nodeA(ctx), nodeB(ctx);
nodeA.bind(pub_endpoint);
nodeB.bind(sub_endpoint);
nodeB.connect_peer_pub(pub_endpoint);

zlink::service::spot_t spotB(nodeB);
spotB.subscribe("tcp:test");
std::this_thread::sleep_for(std::chrono::milliseconds(80));

zlink::service::spot_t spotA(nodeA);
zlink::poller_t poller;
poller.add_spot_pub(spotA, static_cast<zlink::poll_event>(0));

int rc = spotA.publish("tcp:test", "tcp-msg", 7, zlink::send_flag::dontwait);
```

Observed:

```text
rc = -1, errno = 95 (ENOTSUP)
```

## Likely Core Problem Area

관련 코드:

- `Spot` publish path: [spot_node.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node.cpp:1197)
- `SpotPub::poller_socket()`: [spot_pub.cpp](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_pub.cpp:38)
- poller service registration: [zlink.cpp](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp:2567)

현재 signal로 보면 다음 중 하나일 가능성이 큽니다.

1. `SpotPub` service-instance poller 등록 이후 `DONTWAIT` send contract가 완성되지 않음
2. poller-mode publisher readiness와 `spot_pub_publish(..., DONTWAIT)`의
   state machine이 연결되지 않음
3. service-instance poller는 열렸지만 publish path가 여전히 blocking/sync mode
   전제만 갖고 있어 `ENOTSUP` 또는 `EFSM`으로 튕김

## Suggested Checks For Core Owner

1. `zlink_poller_add_spot_pub(...)` 후 `zlink_spot_pub_publish(..., ZLINK_DONTWAIT)`
   targeted regression test 추가
2. 같은 시나리오에서 expected errno를 `EAGAIN`으로 수렴시키거나, 실제로
   unsupported면 public contract에 명시
3. `MULTI_SPOT` perf/server loop와 같은
   `pending + PollOut on-demand` 경로를 core/perf에서도 다시 확인

## Non-Workaround Policy

- Java perf에서는 blocking publish로 우회하지 않음
- retry budget/cap/fallback 추가하지 않음
- 실패는 그대로 stderr + non-zero exit로 보고

현재 `MULTI_SPOT`은 이 core 문제 때문에 policy-compliant 상태로 완료할 수 없습니다.
