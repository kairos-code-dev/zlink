# `core` recv-with-callback surface 복원 계획

> 범위:
> [`core/include/`](/home/hep7/project/kairos/zlink/core/include),
> [`core/src/`](/home/hep7/project/kairos/zlink/core/src),
> [`core/tests/`](/home/hep7/project/kairos/zlink/core/tests)

## 1. 목표

이번 작업은 callback surface를 예전처럼 무작정 되살리려는 게 아니다.
이미 남아 있는 dispatch 인프라를 활용해, 사용자에게 설명하기 쉬운 공통
규칙으로 다시 정렬하는 데 핵심이 있다.

고정 목표:

- recv-capable subject에서는 "recv 또는 callback 중 하나를 고른다"는 모델을
  다시 기본값으로 둔다.
- send-capable subject에서는 "poller `POLLOUT` 또는 send-ready callback 중
  하나를 고른다"는 모델을 다시 기본값으로 둔다.
- 소켓 family별 예외 나열보다 attach 이후의 공통 동작 규칙을 public contract의
  중심으로 둔다.
- `core/tests/` 회귀도 support matrix 자체보다 공통 규칙을 검증하게 재배치한다.

핵심 설계 원칙:

- callback attach는 "추가 기능"이 아니라 동일 subject의 대체 I/O 모델 전환으로
  본다.
- receive와 writable readiness는 서로 다른 축이다. receive callback이 없어도
  send-ready callback은 붙을 수 있고 그 반대도 가능하다.
- 구현 상태도 `receive_callback_active`와 `send_ready_active`를 별도 축으로
  다룬다. 하나가 켜졌다고 다른 하나가 자동으로 켜지거나 prerequisite가 되지
  않는다.
- callback attach 이후 sync API를 부분적으로 남겨 두는 hidden compat는 두지
  않는다.
- POSD 관점에서 shallow policy branch를 늘리는 대신 attach 후 invariant를 깊은
  모듈 계약으로 만든다.

## 2. support matrix

### 2.1 receive callback 복원 범위

| family | recv API | callback API | 목표 상태 |
|---|---|---|---|
| raw `PAIR` | `zlink_recv()` | `zlink_recv_handler()` | 복원 |
| raw `DEALER` | `zlink_recv()` | `zlink_recv_handler()` | 복원 |
| raw `ROUTER` | `zlink_recv()` | `zlink_recv_handler()` | 복원 |
| raw `STREAM` | `zlink_recv()` | `zlink_recv_handler()` | 유지 |
| raw `SUB` | `zlink_subscribe()` | `zlink_subscribe_handler()` | 복원 |
| raw `XSUB` | `zlink_subscribe()` | `zlink_subscribe_handler()` | 복원 |
| `SPOT` | `zlink_subscribe()` | `zlink_subscribe_handler()` | 유지 |
| `SPOT node` | `zlink_subscribe()` | `zlink_subscribe_handler()` | 유지 |
| `gateway` | `zlink_gateway_recv()` / `zlink_recv()` 경유 | `zlink_recv_handler()` | 복원 |
| socket monitor | `zlink_socket_monitor_recv()` | `zlink_socket_monitor_handler()` | 유지 |
| service monitor | `zlink_service_monitor_recv()` | `zlink_service_monitor_handler()` | 유지 |

명시적 비범위:

- raw `PUB`는 receive surface가 없다.
- raw `XPUB`는 `zlink_subscription_event()` recv surface는 유지하되, 이번 v1
  롤백에서는 삭제된 별도 callback ABI를 새로 복구 대상으로 삼지 않는다.

### 2.2 send-ready 공통화 범위

| family | send path | `zlink_send_ready_handler()` | 목표 상태 |
|---|---|---|---|
| raw `PAIR` | `zlink_send()` | 허용 | 복원 |
| raw `PUB` | `zlink_send()` / `zlink_publish()` | 허용 | 복원 |
| raw `XPUB` | `zlink_send()` | 허용 | 복원 |
| raw `DEALER` | `zlink_send()` | 허용 | 복원 |
| raw `ROUTER` | `zlink_send()` / `zlink_send_rid()` | 허용 | 복원 |
| raw `STREAM` | `zlink_send()` / `zlink_send_rid()` | 허용 | 유지 |
| `SPOT` | `zlink_publish()` | 허용 | 유지 후 일반 규칙에 편입 |
| `SPOT node` | `zlink_publish()` | 허용 | 유지 후 일반 규칙에 편입 |
| `gateway` | `zlink_gateway_send()` / `zlink_gateway_send_rid()` | 허용 | 복원 |

명시적 비범위:

- raw `SUB`, `XSUB`, monitor는 send path가 없으므로 send-ready 대상이 아니다.

## 3. public contract

### 3.0 상태 모델

각 subject는 아래 두 상태 축을 독립적으로 가질 수 있다.

- `receive_callback_active`
  - `zlink_recv_handler()`, `zlink_subscribe_handler()`, monitor handler attach로
    켜진다.
  - 이 축은 receive surface에만 영향을 준다.
- `send_ready_active`
  - `zlink_send_ready_handler()` attach로 켜진다.
  - 이 축은 writable readiness surface에만 영향을 준다.

고정 규칙:

- `receive_callback_active`는 sync recv 계열 호출과 data-plane `POLLIN`을 막는다.
- `send_ready_active`는 data-plane `POLLOUT`을 막는다.
- 두 축은 독립적이므로 receive callback 없이 send-ready만 켠 subject도 허용한다.

### 3.1 receive callback 규칙

receive callback attach가 성공한 subject는 아래 계약을 따른다.

- 동일 subject의 sync receive API는 `EBUSY`를 반환한다.
  - multipart family: `zlink_recv()`
  - topic-aware family: `zlink_subscribe()`
  - gateway: `zlink_gateway_recv()` 및 generic receive 경로
  - monitor: 대응 recv API
- data-plane `POLLIN` poller 등록과 data-plane readiness 관찰은 `EBUSY`를
  반환한다.
- 중복 attach 또는 다른 receive model과의 충돌은 `EBUSY`를 반환한다.
- 지원되지 않는 family는 `ENOTSUP`를 반환한다.
- callback payload shape는 대응 recv payload shape와 parity를 유지한다.

설명 의도:

- 사용자는 "이 family는 callback 되나 안 되나"보다 "callback 붙였으면 recv/pollin은
  더 이상 직접 만지지 않는다"만 알면 된다.
- raw `SUB`/`XSUB`와 `SPOT` 계열은 topic-aware shape를 유지해 payload 계약이
  흔들리지 않게 한다.

### 3.2 send-ready 규칙

`zlink_send_ready_handler()` attach가 성공한 subject는 아래 계약을 따른다.

- writable readiness는 callback으로만 노출한다.
- 동일 subject의 data-plane `ZLINK_POLLOUT` poller 등록은 `EBUSY`를 반환한다.
- receive model과 무관하게 send-ready attach 자체는 허용된다.
- `receive_callback_active`가 꺼져 있어도 `send_ready_active`는 켤 수 있다.
- handler replace는 기존 semantics를 유지하되, same-handle reentrant replace는
  `EDEADLK`를 유지한다.
- 지원되지 않는 family는 `ENOTSUP`를 반환한다.

설명 의도:

- recv callback이 붙지 않아도 writable readiness만 callback으로 받을 수 있다.
- send-ready는 "callback receive retained family만의 특수 기능"이 아니라,
  send-capable subject의 공통 writable signal 모델이 된다.

### 3.3 errno 고정

| 상황 | errno |
|---|---|
| 지원 대상 아님 | `ENOTSUP` |
| callback/recv 또는 callback/poller 충돌 | `EBUSY` |
| reentrant send-ready replace | `EDEADLK` |
| `NULL` handler, 잘못된 인자 | `EINVAL` |
| 잘못된 handle/tag | `EFAULT` |

추가 원칙:

- silent fallback은 두지 않는다.
- 특정 family에서만 hidden compat로 예전 동작을 계속 허용하지 않는다.
- family별 차이는 payload shape와 attach entrypoint 종류에만 남긴다.

## 4. `core/include/zlink.h` 변경 방향

### 4.1 함수 설명 정리

- `zlink_recv_handler()`
  - `STREAM` 전용 설명을 없애고 multipart receive family 공통 설명으로 다시 쓴다.
  - attach 후 `zlink_recv()`와 `POLLIN`이 `EBUSY`가 된다고 명시한다.
- `zlink_subscribe_handler()`
  - raw `SUB`/`XSUB`, `SPOT`, `SPOT node`의 topic-aware callback surface로 다시
    기술한다.
  - attach 후 `zlink_subscribe()`와 `POLLIN`이 `EBUSY`가 된다고 명시한다.
- `zlink_send_ready_handler()`
  - callback receive prerequisite 문구를 제거한다.
  - send-capable subject 공통 writable readiness callback으로 다시 기술한다.
  - attach 후 `ZLINK_POLLOUT` poller가 `EBUSY`라고 명시한다.

### 4.2 public matrix 표현 방식

header 주석은 family별 장황한 열거보다 아래 구조로 정리한다.

- multipart callback family
  - raw `PAIR`, `DEALER`, `ROUTER`, `STREAM`, `gateway`
- topic-aware callback family
  - raw `SUB`, `XSUB`, `SPOT`, `SPOT node`
- monitor dual-mode family
  - socket monitor, service monitor
- send-ready family
  - send-capable raw sockets + `SPOT`/`SPOT node` + `gateway`

## 5. `core/src/` 구현 작업

### 5.1 API gate 복원

[`core/src/api/zlink.cpp`](../../../core/src/api/core/zlink.cpp)
에서 우선 정리할 항목:

- `zlink_recv_handler()` type gate를 raw `STREAM` 전용에서 multipart callback
  family 전체로 다시 연다.
- `zlink_subscribe_handler()`의 raw `SUB`/`XSUB` fast-fail `ENOTSUP`를 제거한다.
- `gateway`의 callback receive fast-fail `ENOTSUP`를 제거한다.
- `zlink_send_ready_handler()`의 "callback mode여야만 허용" gate를 일반화한다.
- `STREAM` 전용 `POLLOUT` 차단 로직을 send-ready active general rule로 올린다.

### 5.2 socket/service 내부 구현

직접 점검할 핵심 구현은 아래다.

- [`core/src/sockets/socket_base.cpp`](../../../core/src/runtime/sockets/common/socket_base.cpp)
  - generic socket message handler attach/stop 재사용
  - data-plane `POLLIN`/`POLLOUT` 충돌 validation 공통화
- [`core/src/sockets/pair.cpp`](../../../core/src/sockets/pair.cpp)
- [`core/src/sockets/dealer.cpp`](../../../core/src/sockets/dealer.cpp)
- [`core/src/sockets/router.cpp`](../../../core/src/sockets/router.cpp)
- [`core/src/sockets/xsub.cpp`](../../../core/src/runtime/sockets/pubsub/xsub.cpp)
  - 이미 존재하는 dispatch 경로가 public gate와 맞물리도록 정리
- [`core/src/services/gateway/gateway.cpp`](../../../core/src/services/gateway/gateway.cpp)
  - callback receive/send-ready attach 허용 경로 복원
- [`core/src/services/spot/spot_node.cpp`](../../../core/src/services/spot/spot_node.cpp)
- [`core/src/services/spot/spot_pub.cpp`](../../../core/src/runtime/services/spot/pubsub/spot_pub.cpp)
- [`core/src/services/spot/spot_sub.cpp`](../../../core/src/runtime/services/spot/pubsub/spot_sub.cpp)
  - 기존 callback contract 유지, 새 공통 규칙 표현에 맞게만 재정렬

### 5.3 구현 기본 원칙

- 새로운 family별 전용 상태를 추가하지 말고 기존 dispatch state를 재사용한다.
- callback attach 후 sync recv 차단은 receive API 쪽에서 일관되게 처리한다.
- poller 충돌 차단은 하나의 callback mode가 아니라 `receive_callback_active`와
  `send_ready_active` 두 상태를 기준으로 공통 처리한다.
  - `receive_callback_active` -> `POLLIN` 차단
  - `send_ready_active` -> `POLLOUT` 차단
- `gateway`는 recv-only special service가 아니라 raw multipart family와 같은
  규칙을 따르는 object facade로 정렬한다.

## 6. 테스트 재정렬

### 6.1 unit / integration에서 success로 바뀌는 회귀

- raw `PAIR` callback attach 성공
- raw `DEALER` callback attach 성공
- raw `ROUTER` callback attach 성공
- raw `SUB` / `XSUB` subscribe callback attach 성공
- `gateway` recv callback attach 성공
- raw `PAIR` / `PUB` / `XPUB` / `DEALER` / `ROUTER` / `STREAM` / `gateway`
  send-ready attach 성공

### 6.2 공통 규칙 회귀

- receive callback attach 후 대응 recv API가 `EBUSY`
- receive callback attach 후 data-plane `POLLIN` poller add가 `EBUSY`
- send-ready attach 후 `POLLOUT` poller add가 `EBUSY`
- receive callback 없이 send-ready만 attach한 상태에서도 `recv`는 계속 동작
- send-ready 없이 receive callback만 attach한 상태에서도 send path는 기존대로 유지
- receive callback 미사용 상태에서는 recv와 `POLLIN`이 정상 동작
- send-ready 미사용 상태에서는 `POLLOUT`이 정상 동작
- callback payload shape와 recv payload shape parity 유지

### 6.3 시작점으로 보는 테스트 파일

- [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)
  - raw socket support matrix를 failure 중심에서 success 중심으로 뒤집는다.
- [`core/tests/integration/discovery/test_gateway_with_handler.cpp`](../../../core/tests/integration/discovery/test_gateway_with_handler.cpp)
  - gateway callback attach `ENOTSUP` 회귀를 성공 계약 회귀로 교체한다.
- [`core/tests/unittest/unittest_service_mode_policy.cpp`](/home/hep7/project/kairos/zlink/core/tests/unittest/unittest_service_mode_policy.cpp)
  - `gateway recv-only`, `stream send_ready requires callback mode` 같은 축소 전제
    회귀를 새 공통 규칙 회귀로 교체한다.
- monitor 관련 회귀는 기존 parity 테스트를 유지하되 새 rule과 충돌하지 않는지
  확인한다.

## 7. 완료 기준

아래가 모두 만족되면 `core` surface 복원 완료로 본다.

1. public header가 family별 축소 문구 대신 attach 후 공통 규칙을 기준으로 설명된다.
2. raw `PAIR`/`DEALER`/`ROUTER`/`SUB`/`XSUB`, `gateway` callback attach가 success로
   바뀐다.
3. send-capable subject의 `zlink_send_ready_handler()`가 callback receive
   prerequisite 없이 동작한다.
4. receive callback attach 후 recv와 `POLLIN`이 `EBUSY`를 반환한다.
5. send-ready attach 후 `POLLOUT`이 `EBUSY`를 반환한다.
6. monitor dual-mode parity와 `SPOT`/`STREAM` 기존 callback parity가 깨지지
   않는다.
