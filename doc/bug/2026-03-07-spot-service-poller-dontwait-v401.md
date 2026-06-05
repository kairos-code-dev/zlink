# [Revised Investigation] `MULTI_SPOT`는 기존 문서의 원인 진술과 다르게 재현됨 (`core/v4.0.1`)

- Date: 2026-03-07
- Repo: `/home/hep7/project/kairos/zlink`
- Release tag: `core/v4.0.1`
- Runtime sync: native libraries refreshed after `core/v4.0.1`
- Scope: `bindings/java/perf` `MULTI_SPOT` 재검증 + cross-check 최소 재현

## Summary

기존 문서의 핵심 주장 두 가지는 현재 코드와 재현 결과에 맞지 않습니다.

1. `zlink_poller_add_spot_pub()` 이후 facade publish가 구조적으로 막힌다는 주장
2. `zlink_spot_pub_publish(..., ZLINK_DONTWAIT)`가 `ENOTSUP`라는 주장

소스 기준으로는:

- `zlink_poller_add_spot_pub()`는 `spot_pub_t::poller_socket()`을 거쳐
  `spot_node_t::pub_socket_for_poller()`를 사용합니다:
  [spot_pub.cpp](../../core/src/runtime/services/spot/pubsub/spot_pub.cpp#L40),
  [zlink.cpp](../../core/src/api/core/zlink.cpp#L2567)
- facade publish를 막는 `_pub_pollable_mode`는
  `pub_socket_unsafe()`에서만 켜집니다:
  [spot_node.cpp](../../core/src/services/spot/spot_node.cpp:905)
- `spot_node_t::publish()`는 `0`과 `ZLINK_DONTWAIT`를 둘 다 허용하고,
  그 외 flag만 `ENOTSUP`입니다:
  [spot_node.cpp](../../core/src/services/spot/spot_node.cpp:1180)

즉 기존 문서의 "add_spot_pub 이후 dontwait가 구조적으로 unsupported"라는
설명은 부정확합니다.

다만 `MULTI_SPOT` 자체의 실패는 여전히 재현됩니다. 현재 더 정확한 문제는
`Spot` service facade/service poller 조합이 separate-process 경로에서
정상 delivery를 만들지 못하거나 `EFSM`으로 실패한다는 점입니다.

## What Was Re-Verified

### 1. Java same-process one-shot publish는 정상

`addSpotPub(POLLOUT)` 이후 `publish(DONTWAIT)` 1회와 service subscriber 수신은
정상입니다.

Observed:

```text
writable=true
publish=dontwait_ok
received_parts=1
body=pong
```

이 결과는 "add_spot_pub 직후 첫 publish가 바로 깨진다"는 기존 문장을
지지하지 않습니다.

### 2. Java same-process 재사용 send도 정상

`PreparedTopic + PublishContext + Message` 재사용 100회 전송도 정상입니다.

Observed:

```text
sends=100 received=100
```

즉 `PreparedTopic`/`PublishContext` 재사용 자체가 문제는 아닙니다.

### 3. Java direct `MULTI_SPOT`는 여전히 실패

Command:

```bash
cd bindings/java
PERF_CLIENTS=1 PERF_WARMUP_SECONDS=0 PERF_DURATION_SECONDS=1 PERF_SETTLE_MS=0 \
PERF_CONNECT_READY_TIMEOUT_MS=5000 PERF_CLIENT_POLL_TIMEOUT_MS=10 \
java --enable-native-access=ALL-UNNAMED \
  -cp perf/multi/Zlink.PerfBench/build/classes/java/main:build/classes/java/main:build/resources/main \
  dev.kairoscode.zlink.integration.bench.PerfMultiMain \
  --multi-client MULTI_SPOT tcp 64 --endpoint <READY endpoint>
```

Observed:

```text
ERROR,MULTI_SPOT,client,no_active_frames
```

서버는 이번 재검증에서는 이전 문서처럼 첫 publish 예외를 내지 않았습니다.

### 4. Java separate-process minimal repro는 delivery 0

별도 프로세스로 나눈 단순 service `Spot` 재현에서도 메시지가 도착하지 않습니다.

#### One-shot facade publish

Server:

```text
READY,tcp://127.0.0.1:44788
SENT,1
```

Client:

```text
RECV,0
```

#### Perf-style publish loop

Server:

```text
READY,tcp://127.0.0.1:42672
SENT,1368886
```

Client:

```text
RECV,0
```

즉 sender는 성공으로 보고하지만 receiver는 0건입니다.

`pubPeers()` 이후 `sleep(100ms/500ms)`를 넣어도 결과는 동일했습니다.

### 5. C++ wrapper separate-process minimal repro도 실패

Java perf만의 문제인지 확인하기 위해 C++ wrapper로도 같은 시나리오를 나눠
실행했습니다.

Observed:

Server:

```text
READY,tcp://127.0.0.1:22429
SEND_ERR,156384763
```

Client:

```text
RECV_ERR,156384763
```

`156384763`은 `EFSM`입니다:
[CoreEnumPortedTest.java](../../bindings/java/src/test/java/dev/kairoscode/zlink/CoreEnumPortedTest.java:75)

이 결과는 문제를 Java perf 로직 하나로만 설명하기 어렵다는 뜻입니다.

## Current Assessment

현재 결론은 다음입니다.

1. 기존 문서의 원인 설명은 틀렸습니다.
   - `add_spot_pub => facade publish unsupported`
   - `DONTWAIT => ENOTSUP`
   둘 다 현재 코드/재현과 맞지 않습니다.
2. 하지만 `MULTI_SPOT` 실패 자체는 여전히 재현됩니다.
3. 재현 범위는 Java perf보다 넓습니다.
   - Java direct perf: `no_active_frames`
   - Java separate-process minimal: `SENT>0`, `RECV=0`
   - C++ wrapper separate-process minimal: `EFSM`
4. 따라서 현재 더 정확한 가설은:
   - separate-process `Spot` facade/service path 자체에 문제가 있거나
   - service facade와 peer/discovery runtime state machine 사이에
     cross-process 문제가 있다는 것입니다.

## Why This Matters For Java Perf

`bindings/java/perf` 쪽에서 retry/cap/fallback으로 이 현상을 숨기면 안 됩니다.

현재 `MULTI_SPOT`의 `client,no_active_frames`는 Java perf만의 집계 버그로 단정할
수 없습니다. public service path의 lower-level 문제 가능성이 남아 있습니다.

반대로, 기존 문서처럼 `add_spot_pub + DONTWAIT` 하나만을 원인으로 고정하는 것도
부정확합니다.

## Suggested Follow-Up

1. core owner 관점:
   - separate-process `spot_pub_new/spot_sub_new` publish/recv 최소 재현을
     targeted test로 추가
   - Java/C++ wrapper를 거치지 않는 C API 2-process repro로 한 번 더 확인
   - `EFSM`이 정확히 어디서 세팅되는지 tracing
2. bindings/java 관점:
   - `MULTI_SPOT`에는 fallback/retry 없이 현재 실패를 그대로 유지
   - service path 문제가 정리되기 전까지 raw-socket식 우회는 하지 않음

## Non-Workaround Policy

- Java perf에서 blocking/fallback/retry/cap으로 성공처럼 보이게 하지 않음
- 실패는 그대로 stderr + non-zero exit로 보고
- 이후 수정이 필요하면 원인 확인 후 contract에 맞는 방향으로만 변경
