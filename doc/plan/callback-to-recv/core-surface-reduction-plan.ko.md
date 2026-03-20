# `core` callback surface 축소 계획

> 범위:
> [`core/include/`](/home/hep7/project/kairos/zlink/core/include),
> [`core/src/`](/home/hep7/project/kairos/zlink/core/src),
> [`core/tests/`](/home/hep7/project/kairos/zlink/core/tests)

## 1. 목표

이번 작업의 목표는 callback receive를 전면 제거하는 것이 아니다.

- 사용자 가치가 높은 subject에는 callback을 유지한다.
- 가치 대비 관리 포인트만 큰 subject에서는 callback을 걷어낸다.
- public contract를 "모든 recv-capable subject가 dual-mode"라는 가정에서
  벗어나게 만든다.
- `core/tests/`의 회귀 범위도 축소된 support matrix에 맞게 다시 고정한다.

핵심 설계 원칙:

- `STREAM`, `SPOT`, monitor는 event-driven/object-level surface로 본다.
- raw socket과 `gateway`는 recv/poller 중심 low-level control surface로 본다.
- callback이 빠진 subject는 사용자가 상위 layer에서 직접 감싸도록 둔다.
- 라이브러리가 반복 래핑을 대신해 주는 가치가 큰 surface에만 callback을 남긴다.
- recv 모드의 writable readiness는 `poller POLLOUT`를 canonical로 본다.
- `send_ready` callback은 callback receive를 유지하는 family에만 남긴다.

## 2. 고정 결정

### 2.1 dual-mode 유지 대상

| family | recv | callback | 비고 |
|---|---|---|---|
| raw `STREAM` | 유지 | 유지 | direct dispatch 가치가 큰 data-plane |
| `SPOT` | 유지 | 유지 | object-level pub/sub facade |
| `SPOT node` | 유지 | 유지 | node-owned default sub receive |
| socket monitor | 유지 | 유지 | callback-primary event surface |
| service monitor | 유지 | 유지 | callback-primary event surface |

### 2.2 recv-only 축소 대상

| family | recv | callback | 조치 |
|---|---|---|---|
| raw `PAIR` | 유지 | 제거 | `zlink_recv_handler()` 지원 대상에서 제외 |
| raw `DEALER` | 유지 | 제거 | 동일 |
| raw `ROUTER` | 유지 | 제거 | 동일 |
| raw `SUB` | 유지 | 제거 | `zlink_recv_handler()`, `zlink_subscribe_handler()` 지원 대상에서 제외 |
| raw `XSUB` | 유지 | 제거 | `zlink_recv_handler()`, `zlink_subscribe_handler()` 지원 대상에서 제외 |
| raw `XPUB` | recv/event 유지 | callback 제거 | `zlink_subscription_event_handler()` 제거 대상 |
| `gateway` | 유지 | 제거 | recv model을 canonical로 고정 |

### 2.3 `send_ready` 정책

| family | recv mode | callback mode | 조치 |
|---|---|---|---|
| raw `STREAM` | `poller POLLOUT` | `zlink_send_ready_handler()` 유지 | callback receive retained family |
| `SPOT` | `poller POLLOUT` | `zlink_send_ready_handler()` 유지 | callback receive retained family |
| `SPOT node` | `poller POLLOUT` | `zlink_send_ready_handler()` 유지 | callback receive retained family |
| raw `PAIR` | `poller POLLOUT` | 제거 | low-level raw surface 정리 |
| raw `PUB` | `poller POLLOUT` | 제거 | 동일 |
| raw `XPUB` | `poller POLLOUT` | 제거 | 동일 |
| raw `DEALER` | `poller POLLOUT` | 제거 | 동일 |
| raw `ROUTER` | `poller POLLOUT` | 제거 | 동일 |
| `gateway` | `poller POLLOUT` | 제거 | recv/poller 중심 surface로 고정 |

정책:

- `send_ready`는 더 이상 "모든 send-capable subject에 공통으로 제공되는 callback"
  이 아니다.
- callback receive retained family(`STREAM`, `SPOT`, `SPOT node`)에서만
  callback mode의 readiness signal로 제공한다.
- recv mode 또는 recv-only subject의 writable readiness는 poller
  `ZLINK_POLLOUT`로만 문서화한다.
- callback receive retained family의 callback mode에서는 writable readiness를
  `zlink_send_ready_handler()`로만 노출하고, data-plane `POLLOUT` poller 등록은
  허용하지 않는다.

## 3. public C API 영향

### 3.1 유지할 함수

| 함수 | 유지 방향 | 지원 subject |
|---|---|---|
| `zlink_recv()` | 유지 | 모든 recv-capable socket/service |
| `zlink_subscribe()` | 유지 | `SPOT` / `SPOT node`, raw `SUB` / `XSUB` recv |
| `zlink_socket_monitor_recv()` | 유지 | socket monitor |
| `zlink_service_monitor_recv()` | 유지 | service monitor |
| `zlink_recv_handler()` | 유지하되 지원 범위 축소 | raw `STREAM`만 |
| `zlink_subscribe_handler()` | 유지하되 지원 범위 축소 | `SPOT` / `SPOT node`만 |
| `zlink_send_ready_handler()` | 유지하되 지원 범위 축소 | raw `STREAM`, `SPOT`, `SPOT node`의 callback mode만 |
| `zlink_socket_monitor_handler()` | 유지 | socket monitor |
| `zlink_service_monitor_handler()` | 유지 | service monitor |

### 3.2 지원 제한 및 즉시 ABI 정리 대상

| 함수 / 타입 | 조치 | 이유 |
|---|---|---|
| `zlink_subscription_event_handler_fn` | 즉시 제거 | 남는 지원 subject가 없음 |
| `zlink_subscription_event_handler()` | 즉시 제거 | raw `XPUB` callback support 축소 |
| raw `PAIR/DEALER/ROUTER`에 대한 `zlink_recv_handler()` 지원 | 지원 대상에서 제외 | recv/poller가 canonical |
| raw `SUB/XSUB`에 대한 `zlink_recv_handler()` 지원 | 지원 대상에서 제외 | raw callback receive surface를 `STREAM`으로 축소 |
| raw `SUB/XSUB`에 대한 `zlink_subscribe_handler()` 지원 | 지원 대상에서 제외 | spot facade/node로 callback 책임 집중 |
| `gateway`에 대한 `zlink_recv_handler()` 지원 | 지원 대상에서 제외 | low-level request/reply control surface로 정렬 |
| raw `PAIR/PUB/XPUB/DEALER/ROUTER`에 대한 `zlink_send_ready_handler()` 지원 | 지원 대상에서 제외 | recv mode에서는 poller `POLLOUT`가 canonical |
| `gateway`에 대한 `zlink_send_ready_handler()` 지원 | 지원 대상에서 제외 | recv/poller 중심 control surface로 정렬 |

정책:

- 남겨 두는 handler 함수에서 "유효한 public handle이지만 지원 대상이 아닌
  경우"는 `ENOTSUP`를 즉시 반환한다.
- callback mode/poller mode 충돌, 중복 attach, 같은 family 내부에서의 모델 충돌은
  `EBUSY`를 반환한다.
- `NULL` handler와 잘못된 인자 조합은 `EINVAL`를 반환한다.
- 잘못된 handle 포인터 또는 tag mismatch는 기존 contract대로 `EFAULT`를 유지한다.
- "예전엔 되던 handle이라 hidden compat로 계속 동작"하는 경로는 남기지 않는다.
- callback이 빠진 handle은 recv/poller 예제로만 문서화한다.
- callback receive retained family에서도 recv mode에서는 `send_ready` 대신
  poller `ZLINK_POLLOUT`를 사용하도록 문서화한다.
- callback receive retained family의 callback mode에서는 `send_ready`와
  `POLLOUT` poller를 병행 가능한 dual-surface로 문서화하지 않는다.
- 호환성 유지용 deprecated 단계나 hidden compat 단계는 두지 않는다.

### 3.3 이번 작업에서 실제로 제거할 항목

이번 subsection은 "지원 제한"이 아니라, 이번 작업에서 코드/문서/회귀에서
실제로 삭제하거나 걷어내야 하는 항목만 따로 적는다.

#### public ABI

- `zlink_subscription_event_handler_fn`
- `zlink_subscription_event_handler()`

즉 `zlink_recv_handler()`, `zlink_subscribe_handler()`,
`zlink_send_ready_handler()`는 남기되 지원 범위를 축소하고,
`zlink_subscription_event_handler[_fn]`는 이번 작업에서 public header/ABI에서
즉시 삭제한다.

#### `core/src/` 구현에서 실제 제거할 항목

- raw `PAIR/DEALER/ROUTER/SUB/XSUB`에 대한 callback attach 허용 경로
- raw `XPUB`의 subscription-event callback attach 허용 경로
- `gateway`의 callback receive 전환 경로
- raw `PAIR/PUB/XPUB/DEALER/ROUTER`에 대한 `send_ready` attach 허용 경로
- `gateway`의 `send_ready` attach 허용 경로
- retained family가 아닌 곳에 남아 있는 callback-specific dead branch
- 축소된 support matrix에 더 이상 필요 없는 callback-only state/분기

#### `core/tests/`에서 실제 제거할 항목

- raw `PAIR/DEALER/ROUTER` callback success를 전제로 한 회귀
- raw `SUB/XSUB` callback success를 전제로 한 회귀
- raw `XPUB` callback success를 전제로 한 회귀
- `gateway` callback receive / `send_ready` success를 전제로 한 회귀
- raw socket `send_ready` success를 전제로 한 회귀

위 항목들은 단순 수정이 아니라, 새 정책과 맞지 않으면 삭제 또는 실패 회귀로
교체한다.

#### 문서/가이드에서 실제 제거할 항목

- raw `PAIR/DEALER/ROUTER` callback 사용 예시
- raw `SUB/XSUB` callback 사용 예시
- raw `XPUB` callback 사용 예시
- `gateway` callback receive / `send_ready` 사용 예시
- raw socket `send_ready` 사용 예시
- recv mode에서 writable readiness를 callback처럼 설명하는 문구
- callback mode와 `POLLOUT` poller를 병행 가능한 것처럼 읽히는 문구

## 4. `core/include/zlink.h` 수정 항목

### 4.1 설명 문구 정리

- `zlink_recv_handler()` 설명에서 "Sockets start in recv mode"라는 일반화 문구를
  `STREAM` 전용 계약으로 좁힌다.
- `zlink_subscribe_handler()`는 raw `SUB/XSUB`가 아니라 `SPOT` / `SPOT node`
  callback receive용이라는 점을 명시한다.
- `zlink_send_ready_handler()`는 callback receive retained family의 callback mode
  에서만 지원된다고 명시한다.
- recv mode에서 writable readiness는 poller `ZLINK_POLLOUT`를 사용한다고
  명시한다.
- `gateway` 문서에서 callback model 설명을 제거하고 recv model만 canonical로
  서술한다.
- monitor는 dual-mode를 유지하되 `callback-primary, recv-secondary` 의미를
  문서에 명시한다.

### 4.2 시그니처/typedef 정리

- `zlink_subscription_event_handler_fn`
- `zlink_subscription_event_handler()`

위 두 항목은 이번 작업에서 public header/ABI에서 즉시 제거한다.
별도 deprecated 단계나 호환성 유지 단계는 두지 않는다.

## 5. `core/src/` 구현 작업

### 5.1 attach validation 축소

- raw socket type validation에서 callback attach 허용 범위를 다시 고정한다.
- `gateway` callback transition path는 제거한다.
- raw `SUB/XSUB/XPUB` callback dispatch 관련 분기는 제거한다.
- raw `SUB/XSUB`의 `zlink_recv_handler()` attach path는 `ENOTSUP`로 닫는다.
- `zlink_send_ready_handler()` 허용 범위를 `STREAM`, `SPOT`, `SPOT node`의
  callback mode로 다시 고정한다.
- raw socket과 `gateway`의 `send_ready` attach path는 `ENOTSUP`로 닫는다.
- retained family의 callback mode에서는 writable readiness용 `POLLOUT` poller
  등록을 `EBUSY`로 차단한다.

### 5.2 lifecycle 단순화

지원이 빠지는 family에서는 아래 callback-specific state를 걷어내는 방향으로
정리한다.

- callback inflight gate
- callback-only `EBUSY` branch
- same-handle callback reentry 특수 규칙
- callback mode에서의 poller 금지 분기

단, `STREAM`, `SPOT`, monitor는 계속 유지 대상이므로 해당 family 내부 계약은
보존한다.

### 5.3 테스트 재정렬

`core/tests/`는 아래 support matrix로 다시 고정한다.

- raw `PAIR/DEALER/ROUTER` callback attach 실패
- raw `SUB/XSUB`의 `zlink_recv_handler()` attach 실패
- raw `SUB/XSUB/XPUB` callback attach 실패
- `gateway` callback attach 실패
- `STREAM` callback attach + recv parity 유지
- `SPOT` / `SPOT node` callback + recv parity 유지
- raw `PAIR/PUB/XPUB/DEALER/ROUTER`, `gateway`의 `send_ready` attach 실패
- `STREAM`, `SPOT`, `SPOT node` callback mode의 `send_ready` 허용 유지
- recv mode writable readiness는 poller `ZLINK_POLLOUT`로 회귀 고정
- `STREAM`, `SPOT`, `SPOT node` callback mode의 `POLLOUT` poller 등록 실패
- socket/service monitor callback + recv parity 유지

### 5.4 구현 파일 맵

이번 작업에서 바로 수정 대상으로 보는 핵심 파일은 아래와 같다.

- [`core/include/zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h)
- [`core/src/api/zlink.cpp`](/home/hep7/project/kairos/zlink/core/src/api/zlink.cpp)
- [`core/src/services/gateway/gateway.cpp`](/home/hep7/project/kairos/zlink/core/src/services/gateway/gateway.cpp)
- [`core/src/services/spot/spot_node.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_node.cpp)
- [`core/src/services/spot/spot_pub.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_pub.cpp)
- [`core/src/sockets/socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)
- [`core/src/sockets/stream.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/stream.cpp)

테스트 고정 대상의 시작점은 아래 파일로 본다.

- [`core/tests/unittest/unittest_service_mode_policy.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_service_mode_policy.cpp)
- [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
- [`core/tests/integration/test_monitor_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_monitor_with_handler.cpp)
- [`core/tests/integration/discovery/test_gateway_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/discovery/test_gateway_with_handler.cpp)
- [`core/tests/e2e/discovery/test_gateway.cpp`](/home/hep7/project/kairos/zlink/core/tests/e2e/discovery/test_gateway.cpp)
- [`core/tests/testutil_unity.cpp`](/home/hep7/project/kairos/zlink/core/tests/testutil_unity.cpp)

## 6. 문서 정리 대상

아래 문서는 이번 작업 안에서 같이 수정한다.

- `doc/api/socket.md`
- `doc/api/socket.ko.md`
- `doc/api/README.md`
- `doc/api/README.ko.md`
- `doc/api/gateway.md`
- `doc/api/gateway.ko.md`
- `doc/api/spot.md`
- `doc/api/spot.ko.md`
- `doc/api/monitoring.md`
- `doc/api/monitoring.ko.md`
- `doc/guide/02-core-api.md`
- `doc/guide/02-core-api.ko.md`
- `doc/guide/03-0-socket-patterns.*`
- `doc/guide/03-1-pair.*`
- `doc/guide/03-2-pubsub.*`
- `doc/guide/03-3-dealer.*`
- `doc/guide/03-4-router.*`
- `doc/guide/03-5-stream.*`
- `doc/guide/07-2-gateway.*`
- `doc/guide/07-3-spot.*`
- `doc/guide/07-4-registry.*`
- `doc/guide/08-routing-id.*`
- `doc/guide/10-performance.*`
- `doc/guide/11-thread-safety.*`

## 7. 패키지/바인딩 fallout

bindings/package 쪽은 이번 문서에서 상세 정책을 따로 들고 가지 않는다.

- 먼저 `core` public surface를 이번 계획대로 정리한다.
- 이후 bindings/package는 정리된 `core` contract와 정합성을 맞추는 방향으로
  후속 정리한다.

## 8. 순서

1. `core/include/zlink.h` support matrix와 설명을 먼저 고정한다.
2. `core/src/`의 callback attach validation을 축소한다.
3. `core/tests/`에서 남는/빠지는 matrix를 회귀로 고정한다.
4. `doc/api`, `doc/guide`, `doc/perf`, `core/perf`를 같은 작업에서 함께 정리한다.
5. bindings/package는 별도 후속 작업에서 `core` contract와만 정합성을 맞춘다.
