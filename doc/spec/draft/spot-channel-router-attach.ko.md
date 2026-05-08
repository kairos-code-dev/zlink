# SPOT Channel ROUTER Attach 폐기 초안

> **폐기됨.**
> 이 문서는 구현 전 검토 과정에서 작성된 초안이지만, 현재는 채택하지 않는다.
> 아래 API와 동작은 현재 공개 계약이 아니며 구현 대상도 아니다.
> 정식 공개 계약은 `core/include/zlink.h`와 정식 spec 문서를 기준으로 한다.

## 폐기 결정

`SpotNode`에 channel ROUTER를 attach하는 기능은 기존 channel DEALER attach의
단순한 대칭 기능이 아니다. DEALER는 명확한 client socket이라 `SpotNode` 전용
outbound channel로 붙여도 의미가 단순하다. 반면 ROUTER는 요청을 받을 수 있는
socket이므로, `SpotNode`에 전용 attach하는 순간 inbound request ownership을 함께
정의해야 한다.

검토 중 확인한 문제는 아래와 같다.

1. ROUTER attach에는 client router, server router, peer router 역할 구분이 필요하다.
2. attached ROUTER로 들어오는 request를 누가 처리할지 명확하지 않다.
3. 같은 channel의 ROUTER가 모두 같은 service contract를 제공해야 하는지 정의가 필요하다.
4. 이질적인 provider를 routing id로 골라 호출하는 요구는 ROUTER mesh보다
   DEALER -> ROUTER 구성이 더 적합하다.
5. 기존 `zlink_spot_request_router()` routed-plane API와 새 attached ROUTER channel
   API의 개념 경계가 쉽게 흐려진다.

따라서 이 문서에서 제안한 public C API는 추가하지 않는다. 현재 service 호출 모델은
기존 channel DEALER attach를 기준으로 유지한다.

## 남기는 이유

이 파일은 폐기된 설계 판단을 추적하기 위해 남긴다. 같은 기능을 다시 검토할 때는
이 문서의 API 목록을 구현 계획으로 사용하지 말고, 먼저 아래 질문에 답해야 한다.

- ROUTER는 client 역할인가, server 역할인가, peer 역할인가?
- inbound request를 `SpotNode`가 처리해야 하는가, 별도 gateway가 처리해야 하는가?
- channel name은 동일 service contract를 뜻하는가?
- target peer 선택은 응용이 직접 하는가, Discovery가 제공하는 metadata로 정하는가?
- 기존 DEALER channel attach로 충분하지 않은 실제 사용 사례가 있는가?

아래 내용은 폐기 전 검토 기록이다.

---

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 `SpotNode`에 ROUTER mesh용 ROUTER socket을 attach하고, `Spot`에서
대상 ROUTER routing id를 지정해 send/request를 수행하기 위한 설계안이었다.
정식 공개 계약에 반영하지 않는다.

## 목적

현재 `SpotNode`는 channel 호출을 위해 attached `DEALER`를 받을 수 있다.
이 경로는 `Spot`이 외부 channel의 `ROUTER` 또는 `DEALER` peer에게 메시지를 보낼 때
사용한다. `DEALER`는 연결된 peer 중 하나로 메시지를 내보낼 수 있으므로, 기존
`zlink_spot_send_channel()`과 `zlink_spot_request_channel()`은 대상 peer routing id를
받지 않는다.

ROUTER mesh를 `SpotNode`에 attach하는 경우는 다르다. attach되는 ROUTER socket은
자기 routing id를 가지지만, 그것은 송신자의 주소일 뿐이다. 실제 송신에는 어느
상대 ROUTER에게 보낼지 나타내는 대상 peer routing id가 필요하다. 따라서 기존
channel API에 ROUTER를 조용히 끼워 넣지 않고, ROUTER 대상 routing id를 명시적으로
받는 별도 `Spot` API를 추가한다.

이 초안의 목표는 아래와 같다.

1. `SpotNode`가 caller-owned ROUTER socket을 channel target으로 등록할 수 있게 한다.
2. `Spot`이 channel name과 대상 peer routing id를 함께 지정해 attached ROUTER를
   사용할 수 있게 한다.
3. 기존 attached `DEALER` channel API의 의미와 오류 코드를 유지한다.
4. 기존 `zlink_spot_request_router()` routed-plane API와 새 attached ROUTER channel
   API를 이름과 의미에서 분리한다.

## 범위

이 초안은 `SpotNode`에 attach된 ROUTER socket을 **channel 호출용 outbound socket**으로
사용하는 기능만 다룬다.

포함 범위:

- `SpotNode`에 channel ROUTER attach API 추가
- `Spot`에 channel ROUTER send/request API 추가
- ROUTER mesh Discovery와 수동 연결 모두 지원
- 대상 peer routing id 검증
- 기존 channel reply dispatch 흐름과의 통합
- core C API와 C convenience wrapper 표면
- 회귀 테스트 요구사항

제외 범위:

- SPOT mesh routed plane 대체
- `SpotNode` 내부 `external-router`의 public 노출
- ROUTER peer 선택 정책 자동화
- attached ROUTER socket의 자동 생성
- attach API 안에서 `bind()` 또는 `connect()` 수행
- target peer discovery query를 send/request 호출 안에서 자동 수행

## 용어

| 용어 | 의미 |
|------|------|
| channel ROUTER | `SpotNode`에 attach되어 `Spot`의 channel ROUTER send/request가 사용하는 caller-owned ROUTER socket |
| channel DEALER | 기존 `zlink_spot_node_attach_channel_dealer()`로 붙이는 caller-owned DEALER socket |
| channel name | attached channel target 집합을 식별하는 논리 이름 |
| local ROUTER rid | attach된 ROUTER socket 자신의 routing id |
| target peer rid | channel ROUTER send/request가 도달해야 하는 상대 ROUTER의 routing id |
| ROUTER mesh Discovery | `ZLINK_AUTO_CONNECT_ROUTE_MESH` Discovery channel |
| routed-plane API | `zlink_spot_request_router()`처럼 SPOT routed protocol을 통해 ROUTER endpoint class로 보내는 기존 API |
| channel ROUTER API | 이 초안에서 추가하는 attached ROUTER socket 기반 API |

## 핵심 의미

channel ROUTER attach는 기존 channel DEALER attach와 같은 namespace를 공유한다.
즉 같은 `channel_name`에 channel DEALER와 channel ROUTER를 동시에 하나씩 붙이는
것은 허용하지 않는다. 사용자는 channel name 하나에 대해 하나의 outbound channel
방식을 선택해야 한다.

이 제한은 호출자가 `zlink_spot_send_channel()`과
`zlink_spot_send_channel_router()`를 섞어 쓸 때 어떤 socket이 선택되는지 추측하지
않게 하기 위한 것이다. 같은 channel name에 여러 종류의 target이 있으면 API 이름이
명시적이어도 장애 상황에서 상태 해석이 어려워진다.

channel ROUTER API는 기존 `zlink_spot_request_router()`와 다르다.

| 구분 | 기존 routed-plane API | 새 channel ROUTER API |
|------|----------------------|------------------------|
| 대표 함수 | `zlink_spot_request_router()` | `zlink_spot_request_channel_router()` |
| socket 선택 | `SpotNode` routed plane | attach된 channel ROUTER |
| 대상 지정 | peer rid | channel name + peer rid |
| 용도 | SPOT routed protocol의 ROUTER endpoint class | 외부 ROUTER mesh channel 호출 |
| attach 필요 | 필요 없음 | 필요 |

## Public C API 변경 요약

### Core C ABI: SpotNode attach

`core/include/zlink.h`에 아래 API를 추가한다.

```c
ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_router(
  void *node,
  void *discovery,
  void *router);

ZLINK_EXPORT zlink_config_result_t zlink_spot_node_attach_channel_router_manual(
  void *node,
  const char *channel_name,
  void *router);
```

`zlink_spot_node_attach_channel_router()`는 Discovery 기반 ROUTER mesh attach다.
`discovery`는 `ZLINK_AUTO_CONNECT_ROUTE_MESH` Discovery여야 한다. 이 함수는
Discovery를 observer로 등록하고, Discovery가 관리하는 peer endpoint 변화가 attached
ROUTER socket에 반영되도록 한다.

`zlink_spot_node_attach_channel_router_manual()`은 수동 연결용 attach다. 호출자는 이
함수를 호출하기 전에 ROUTER socket의 `bind()` 또는 `connect()` 구성을 직접 끝내야
한다. 이 함수는 endpoint를 만들거나 연결하지 않는다.

두 함수 모두 `router`가 `ZLINK_SOCKET_ROUTER`가 아니면 실패한다.

### Core C ABI: Spot send/request part API

`core/include/zlink.h`에 아래 helper substrate API를 추가한다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_send_channel_router_part(
  void *spot,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  zlink_msg_t *part,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag);

ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_channel_router_part(
  void *spot,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  zlink_msg_t *part,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  zlink_part_flag_t part_flag,
  uint32_t timeout_ms);
```

이 API는 기존 `*_part` 계열과 같은 multipart builder 규칙을 따른다.
`part_flag == ZLINK_PART_MORE`이면 같은 send sequence가 열리고, 마지막 part에서
실제 ROUTER 송신이 완료된다.

`peer_rid`는 대상 ROUTER peer의 routing id다. 이 값이 비어 있거나 길이 제한을
넘으면 `EINVAL`로 실패한다.

### C convenience wrapper

`bindings/c/include/zlink_c.h`와 `bindings/c/src/zlink_c.c`에는 아래 wrapper를
추가한다.

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_channel_router(
  void *spot,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_channel_router(
  void *spot,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

wrapper는 기존 C wrapper처럼 `*_part` API를 반복 호출한다. 중간 part에서 실패하면
남은 part 소유권 처리도 기존 C wrapper 규칙을 따른다.

### 기존 API와의 관계

기존 API는 유지한다.

```c
zlink_config_result_t zlink_spot_node_attach_channel_dealer(
  void *node,
  void *discovery,
  void *dealer);

zlink_config_result_t zlink_spot_node_attach_channel_dealer_manual(
  void *node,
  const char *channel_name,
  void *dealer);

zlink_submit_result_t zlink_spot_send_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_send_flags_t flags);

zlink_submit_result_t zlink_spot_request_channel(
  void *spot,
  const char *channel_name,
  zlink_msg_t *parts,
  size_t part_count,
  zlink_reply_handler_fn handler,
  void *userdata,
  zlink_send_flags_t flags,
  uint32_t timeout_ms);
```

기존 `zlink_spot_send_channel()`과 `zlink_spot_request_channel()`은 channel DEALER
전용 의미를 유지한다. 같은 channel name에 channel ROUTER만 attach된 상태에서 이
기존 API를 호출하면 target type mismatch로 실패한다. 이 오류는 `ENOTSUP` 또는
`EINVAL` 중 하나로 고정해야 한다. 이 초안은 `ENOTSUP`을 선택한다. 이유는 핸들 자체가
잘못된 것이 아니라 해당 channel target이 API가 요구하는 socket family를 지원하지
않기 때문이다.

새 `zlink_spot_send_channel_router()`와
`zlink_spot_request_channel_router()`는 channel ROUTER 전용 의미를 가진다. 같은
channel name에 channel DEALER만 attach된 상태에서 호출하면 `ENOTSUP`으로 실패한다.

## Attach 계약

### 공통 조건

channel ROUTER attach는 아래 조건을 만족해야 성공한다.

| 조건 | 실패 errno |
|------|------------|
| `node == NULL` | `EFAULT` 또는 `EINVAL` |
| `router == NULL` | `EINVAL` |
| `router`가 `ZLINK_SOCKET_ROUTER`가 아님 | `EINVAL` |
| `channel_name`이 비어 있음 | `EINVAL` |
| 같은 channel name에 이미 channel target이 있음 | `EBUSY` |
| 같은 socket이 이미 다른 attachment에 등록됨 | `EBUSY` |
| node가 종료 중이거나 종료됨 | `ESHUTDOWN` |
| monitor open 실패 | monitor 실패 errno |
| Discovery 타입이 ROUTER mesh가 아님 | `ENOTSUP` |

`router`는 caller-owned handle이다. `SpotNode`는 handle 수명을 소유하지 않는다.
하지만 attach 이후 그 socket은 해당 `SpotNode`의 channel ROUTER 전용으로 사용해야
한다. 응용이 같은 ROUTER socket을 일반 raw socket으로 동시에 사용하면 message ordering,
request sequence, recv dispatch 의미가 섞이므로 지원하지 않는다.

### Discovery 기반 attach

```c
zlink_spot_node_attach_channel_router(node, discovery, router);
```

요구사항:

- `discovery->auto_connect_type()`은 `ZLINK_AUTO_CONNECT_ROUTE_MESH`여야 한다.
- `discovery->channel_name()`은 비어 있으면 안 된다.
- Discovery peer set은 ROUTER role만 포함해야 한다.
- attach는 Discovery observer를 등록한다.
- Discovery destroy 또는 shutdown 요청 시 해당 channel ROUTER attachment는 제거된다.
- attach는 socket을 생성하지 않는다.
- attach는 직접 `bind()` 또는 `connect()`를 호출하지 않는다. Discovery 자동 연결
  경로가 기존 socket discovery attach와 같은 방식으로 peer 연결을 관리한다.

Discovery 기반 attach에서는 ROUTER socket의 local routing id가 중요하다.
`zlink_socket_attach_discovery()`가 raw socket에 routing id를 준비하듯이,
channel ROUTER attach도 routing id가 없으면 Discovery runtime의 socket routing id
준비 경로를 사용해야 한다. 구현상 같은 helper를 재사용할 수 없다면 attach 전에
`zlink_set_routing_id(router, data, size)`가 필요하다는 계약으로 낮출 수 있다.
이 선택은 구현 중 하나로 고정하고, 정식 spec에는 한 가지만 남긴다.

이 초안의 우선 선택은 **attach가 routing id 준비를 내부에서 처리한다**이다.
호출자에게 별도의 사전 호출을 요구하지 않으면 channel DEALER attach와 사용성이
비슷해지고, routing id 초기화 지식이 호출자에게 새지 않는다.

### 수동 attach

```c
zlink_spot_node_attach_channel_router_manual(node, channel_name, router);
```

수동 attach는 Discovery를 받지 않으므로 자동 연결과 peer set 관리를 수행하지 않는다.
호출자는 `router`의 endpoint 구성과 peer 연결을 직접 준비한다.

수동 attach에서 local ROUTER routing id는 아래 중 하나로 정한다.

1. 호출자가 attach 전에 `zlink_set_routing_id(router, data, size)`로 고정한다.
2. routing id가 없으면 core가 기존 ROUTER socket auto id 규칙으로 생성한 값을 쓴다.

정식 구현에서는 위 둘 중 하나만 계약으로 남겨야 한다. 이 초안은 수동 attach에서도
기존 ROUTER socket auto id 규칙을 존중하는 쪽을 우선 선택한다. attach는 local rid가
비어 있지 않은지 확인하고, 비어 있으면 `EINVAL`로 실패한다.

## Send/Request 계약

### `zlink_spot_send_channel_router_part`

이 함수는 attached channel ROUTER를 사용해 `peer_rid`로 one-way routed send를 수행한다.
reply callback은 없고, request sequence도 만들지 않는다.

성공 조건:

- `spot`은 live `Spot` handle이어야 한다.
- `channel_name`은 유효한 channel name이어야 한다.
- `peer_rid`는 유효한 routing id여야 한다.
- `channel_name`에 channel ROUTER target이 attach되어 있어야 한다.
- selected ROUTER socket이 send 가능해야 한다.

실패 의미:

| 상황 | submit result | errno |
|------|---------------|-------|
| 잘못된 handle | `ZLINK_SUBMIT_INVALID_HANDLE` | `EFAULT` |
| 잘못된 channel name | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| 잘못된 peer rid | `ZLINK_SUBMIT_INVALID_ARGUMENT` | `EINVAL` |
| channel 없음 | `ZLINK_SUBMIT_NOT_FOUND` 계열 | `ENOENT` |
| channel은 있으나 ROUTER target 없음 | `ZLINK_SUBMIT_NOT_SUPPORTED` 계열 | `ENOTSUP` |
| ROUTER peer 연결 없음 | `ZLINK_SUBMIT_NOT_ADMITTED` 또는 failure 계열 | `EHOSTUNREACH`, `ENOTCONN`, `EAGAIN` |
| node 종료 중 | shutdown 계열 | `ESHUTDOWN` |

### `zlink_spot_request_channel_router_part`

이 함수는 attached channel ROUTER를 사용해 `peer_rid`로 request를 보내고, reply를
callback으로 완료한다.

성공 조건:

- `handler`는 `NULL`이 아니어야 한다.
- request가 accepted되면 handler는 정확히 한 번 호출된다.
- submit 결과가 성공이 아니면 handler는 등록되지 않는다.
- timeout은 기존 request/reply timeout 규칙을 따른다.

reply pair:

- 요청을 받은 peer ROUTER는 일반 ROUTER receive 경로로 요청을 읽는다.
- peer는 `zlink_router_reply()` 또는 동등한 binding API로 reply한다.
- reply는 attached channel ROUTER의 request completion queue를 거쳐 원래 `Spot`
  callback으로 전달된다.

dispatch:

- request completion은 기존 `CHANNEL_REPLY_READABLE` dispatch event를 재사용할 수 있다.
- subject kind 이름은 현재 `ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER`라서 ROUTER에는
  맞지 않는다. 구현 시 아래 두 선택지 중 하나를 고정해야 한다.

| 선택지 | 장점 | 단점 |
|--------|------|------|
| 새 enum `ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_SOCKET` 추가 | DEALER/ROUTER를 모두 표현한다 | enum과 바인딩 surface가 바뀐다 |
| 새 enum `ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_ROUTER` 추가 | ROUTER를 명시한다 | DEALER와 ROUTER subject 처리가 둘로 나뉜다 |

이 초안은 `CHANNEL_SOCKET` 추가를 우선 선택한다. 이유는 dispatch event가 "channel
reply source가 readable"이라는 의미이고, source가 DEALER인지 ROUTER인지는 attached
socket type으로 알 수 있기 때문이다. 기존 `CHANNEL_DEALER` 값은 호환을 위해 유지한다.

## 내부 모델 변경 방향

현재 attachment state는 이름은 `routers`지만 실제로는 attached DEALER socket만 담는다.
channel ROUTER를 지원하려면 socket pointer만 저장하는 구조를 아래처럼 바꾼다.

```c
typedef enum spot_channel_target_kind_t {
  spot_channel_target_dealer = 1,
  spot_channel_target_router = 2
} spot_channel_target_kind_t;

typedef struct spot_channel_target_t {
  spot_channel_target_kind_t kind;
  socket_base_t *socket;
} spot_channel_target_t;
```

구현체는 C++ 내부 타입을 사용해도 된다. 중요한 것은 `select_service_router()`가
더 이상 `socket_base_t *`만 반환하지 않고, target kind와 socket을 함께 반환한다는 점이다.

권장 내부 함수:

```c
int spot_node_select_channel_target(
  spot_node_t *node,
  const char *channel_name,
  spot_channel_target_kind_t required_kind,
  spot_channel_target_t *out);
```

`zlink_spot_send_channel()`은 `required_kind == dealer`로 target을 찾고,
`zlink_spot_send_channel_router()`는 `required_kind == router`로 target을 찾는다.
이렇게 하면 기존 DEALER channel의 오류 의미가 ROUTER 추가로 흔들리지 않는다.

## POSD 관점의 설계 선택

### 대안 A: 기존 `attach_channel_dealer`에 ROUTER도 허용

장점:

- API 추가가 적다.
- binding 표면이 작다.

단점:

- 기존 함수 이름과 실제 동작이 어긋난다.
- 기존 `send_channel()`은 target peer rid를 받을 수 없다.
- ROUTER target 선택 실패가 호출자에게 늦게 드러난다.
- `SpotNode` 내부가 API 의미를 추측해야 한다.

### 대안 B: channel ROUTER attach와 channel ROUTER send/request를 별도 추가

장점:

- target peer rid 요구사항이 API에 드러난다.
- 기존 DEALER channel 의미를 깨지 않는다.
- routed-plane API와 attached ROUTER channel API를 분리할 수 있다.
- 호출자 복잡성이 함수 시그니처에 정확히 반영된다.

단점:

- C API와 바인딩 surface가 늘어난다.
- dispatch subject enum 정리가 필요하다.

선택: **대안 B**.

ROUTER는 대상 peer rid 없이는 의미 있는 send/request를 수행할 수 없다. 이 설계 결정을
숨기면 호출자가 실제 실패 조건을 이해하기 어렵다. 별도 API는 표면이 늘어나지만,
필수 정보를 호출 시점에 받기 때문에 모듈 내부가 호출자의 의도를 추측하지 않아도 된다.

## 정식 spec 반영 위치

구현 완료 뒤 아래 문서를 나누어 갱신한다.

| 문서 | 반영 내용 |
|------|----------|
| `doc/spec/core/service/spot.ko.md` | attach channel ROUTER 계약, Spot send/request 계약, 오류 의미 |
| `doc/spec/core/service/discovery.ko.md` | ROUTER mesh Discovery 기반 channel ROUTER attach 설명 |
| `doc/spec/core/errno-map.ko.md` | attach와 send/request errno mapping |
| `doc/spec/bindings/README.md` | 언어별 API surface alignment |
| `doc/spec/bindings/cpp/README.md` | C++ wrapper 이름과 예외 의미 |
| 각 binding spec | Go/Rust/Python/.NET/Node/Java 표면과 callback subject mapping |

정식 spec에는 구현된 API만 반영한다. 구현 전에는 이 draft만 링크하거나 별도 계획 문서에서
참조한다.

## 회귀 테스트 요구사항

구현 뒤에는 최소한 아래 회귀 테스트를 추가해야 한다. 테스트는 가능하면 core C API 기준으로
먼저 고정하고, 바인딩 테스트는 같은 의미를 각 언어 표면에 맞게 추가한다.

### Core unit test

권장 파일:

- `core/tests/unittest/unittest_service_mode_policy.cpp`
- 새 파일 `core/tests/unittest/unittest_spot_channel_router_attach.cpp`

필수 케이스:

| ID | 테스트 | 검증 내용 |
|----|--------|-----------|
| CHAN-ROUTER-ATTACH-01 | manual attach accepts ROUTER | `zlink_spot_node_attach_channel_router_manual()`이 ROUTER socket을 받는다 |
| CHAN-ROUTER-ATTACH-02 | manual attach rejects DEALER | ROUTER attach API에 DEALER를 넘기면 `EINVAL` |
| CHAN-ROUTER-ATTACH-03 | manual attach rejects empty channel | 빈 channel name은 `EINVAL` |
| CHAN-ROUTER-ATTACH-04 | duplicate channel rejected | 같은 channel name에 두 번째 target attach는 `EBUSY` |
| CHAN-ROUTER-ATTACH-05 | socket reuse rejected | 같은 ROUTER socket을 두 channel에 attach하면 `EBUSY` |
| CHAN-ROUTER-ATTACH-06 | dealer/router namespace conflict | 같은 channel name에 DEALER와 ROUTER를 섞으면 두 번째 attach는 `EBUSY` |
| CHAN-ROUTER-ATTACH-07 | discovery type accepts ROUTE_MESH | `ZLINK_AUTO_CONNECT_ROUTE_MESH` Discovery는 router attach 성공 |
| CHAN-ROUTER-ATTACH-08 | discovery type rejects CLIENT_SERVER | `CLIENT_SERVER` Discovery는 router attach에서 `ENOTSUP` |
| CHAN-ROUTER-ATTACH-09 | discovery type rejects DEALER_MESH | `DEALER_MESH` Discovery는 router attach에서 `ENOTSUP` |
| CHAN-ROUTER-ATTACH-10 | destroy discovery detaches channel router | Discovery destroy 뒤 해당 channel ROUTER target이 제거된다 |

### Core integration test

권장 파일:

- 새 파일 `core/tests/integration/test_spot_channel_router.cpp`
- 기존 request/reply helper를 재사용할 수 있으면 `test_helper_recv_part_basic.cpp`의
  ROUTER recv helper를 참조한다.

필수 케이스:

| ID | 테스트 | 검증 내용 |
|----|--------|-----------|
| CHAN-ROUTER-SEND-01 | one-way send through manual attached ROUTER | `Spot` -> attached ROUTER -> peer ROUTER 수신 |
| CHAN-ROUTER-SEND-02 | send requires target peer rid | `peer_rid == NULL` 또는 empty rid는 submit invalid argument |
| CHAN-ROUTER-SEND-03 | wrong channel name | attach되지 않은 channel name은 `ENOENT` 계열 |
| CHAN-ROUTER-SEND-04 | channel exists but wrong target kind | DEALER만 attach된 channel에 router send를 호출하면 `ENOTSUP` |
| CHAN-ROUTER-SEND-05 | multipart send preserves frames | 2개 이상 part가 peer ROUTER에서 같은 multipart delivery로 보인다 |
| CHAN-ROUTER-SEND-06 | DONTWAIT no peer | 대상 peer가 없으면 blocking 없이 실패하고 part 소유권이 정리된다 |
| CHAN-ROUTER-REQ-01 | request/reply through manual attached ROUTER | `Spot` request callback이 peer ROUTER reply를 받는다 |
| CHAN-ROUTER-REQ-02 | request timeout | peer가 reply하지 않으면 callback이 timeout으로 한 번만 호출된다 |
| CHAN-ROUTER-REQ-03 | late reply after timeout | timeout 뒤 늦은 reply가 double completion을 만들지 않는다 |
| CHAN-ROUTER-REQ-04 | close with pending request | pending request가 있는 상태에서 Spot/Node close가 use-after-free 없이 정리된다 |
| CHAN-ROUTER-REQ-05 | channel reply dispatch event | dispatch callback mode에서 channel reply readable event가 발생한다 |
| CHAN-ROUTER-REQ-06 | progress from subject | dispatch subject로 전달된 attached ROUTER에서만 progress drain이 된다 |

### Discovery integration test

권장 파일:

- 새 파일 `core/tests/integration/test_spot_channel_router_discovery.cpp`

필수 케이스:

| ID | 테스트 | 검증 내용 |
|----|--------|-----------|
| CHAN-ROUTER-DISC-01 | route mesh discovery connects routers | Discovery 기반 attach 뒤 ROUTER mesh peer에게 send 가능 |
| CHAN-ROUTER-DISC-02 | provider add/remove refresh | Registry provider 추가/제거가 attached ROUTER 연결 상태에 반영된다 |
| CHAN-ROUTER-DISC-03 | discovery destroy cleanup | Discovery destroy 뒤 send/request가 `ENOENT` 또는 `ENOTCONN` 계열로 실패한다 |
| CHAN-ROUTER-DISC-04 | routing id prepared | Discovery 기반 attach가 local ROUTER rid를 준비하거나, 준비되지 않은 상태를 명확히 실패시킨다 |
| CHAN-ROUTER-DISC-05 | route mesh role validation | ROUTER role이 아닌 provider는 ROUTER mesh channel에 들어가지 않는다 |

### 기존 기능 회귀

ROUTER channel 추가 뒤 기존 channel DEALER와 routed-plane 기능이 깨지지 않아야 한다.

필수 케이스:

| ID | 테스트 | 검증 내용 |
|----|--------|-----------|
| EXISTING-DEALER-01 | existing channel dealer request sample | 기존 `zlink_spot_request_channel()` 경로가 그대로 동작 |
| EXISTING-DEALER-02 | attached dealer dispatch subject | 기존 `CHANNEL_DEALER` dispatch subject가 유지된다 |
| EXISTING-DEALER-03 | shared dealer per request spot | 여러 Spot이 같은 DEALER channel reply source를 공유해도 completion이 분리된다 |
| EXISTING-ROUTED-01 | existing `zlink_spot_request_router()` | 기존 routed-plane ROUTER endpoint request가 그대로 동작 |
| EXISTING-ROUTED-02 | existing router-to-spot request | `zlink_router_request_spot()` / `zlink_router_reply_spot()` 회귀 없음 |
| EXISTING-SURFACE-01 | socket surface tests | Node/Go/Rust/Python/.NET/Java/C++ surface test에서 기존 함수가 제거되지 않음 |

### 오류와 소유권 테스트

필수 케이스:

| ID | 테스트 | 검증 내용 |
|----|--------|-----------|
| CHAN-ROUTER-OWN-01 | caller-owned router remains caller close responsibility | node destroy가 caller-owned ROUTER handle을 destroy하지 않는다 |
| CHAN-ROUTER-OWN-02 | node destroy detaches monitor | node destroy 뒤 attachment monitor handle이 닫힌다 |
| CHAN-ROUTER-OWN-03 | router close before node destroy | attached ROUTER가 먼저 close되어도 node destroy가 안전하다 |
| CHAN-ROUTER-OWN-04 | part ownership on failed send | send 실패 시 part close/reset 규칙이 기존 send API와 같게 유지된다 |
| CHAN-ROUTER-OWN-05 | pending callback after spot destroy | pending request 중 Spot destroy가 callback owner를 잘 정리한다 |
| CHAN-ROUTER-OWN-06 | pending callback after node destroy | pending request 중 Node destroy가 callback을 leak 없이 정리한다 |

### Binding 회귀

각 바인딩은 core API 구현 뒤 별도 PR 또는 같은 변경에서 아래 항목을 맞춘다.

| Binding | 필수 surface |
|---------|--------------|
| C++ | `spot_node_t::attach_channel_router`, `attach_channel_router_manual`, `spot_t::send_channel_router`, `request_channel_router` |
| C | `zlink_spot_send_channel_router`, `zlink_spot_request_channel_router` |
| Go | `SpotNode.AttachChannelRouter`, `AttachChannelRouterManual`, `Spot.SendChannelRouter`, `Spot.RequestChannelRouter` |
| Rust | `attach_channel_router`, `attach_channel_router_manual`, `send_channel_router`, `request_channel_router` |
| Python | `attach_channel_router`, `attach_channel_router_manual`, `send_channel_router`, `request_channel_router` |
| .NET | `AttachChannelRouter`, `AttachChannelRouterManual`, `SendChannelRouter`, `RequestChannelRouter` |
| Node | `attachChannelRouter`, `attachChannelRouterManual`, `sendChannelRouter`, `requestChannelRouter` |
| Java | `attachChannelRouter`, `attachChannelRouterManual`, channel router request builder |

바인딩 테스트는 최소한 surface test와 manual request/reply behavior test를 포함한다.
dispatch subject enum을 추가하면 enum value test도 함께 갱신한다.

## 구현 순서 제안

1. draft 기준 API 이름을 확정한다.
2. `core/include/zlink.h`에 attach API와 `*_part` API를 추가한다.
3. attachment state를 channel target 구조로 일반화한다.
4. 기존 channel DEALER 경로가 기존 target kind만 선택하도록 고정한다.
5. channel ROUTER send/request part API를 구현한다.
6. C wrapper를 추가한다.
7. core unit/integration 회귀 테스트를 추가한다.
8. dispatch subject enum 선택을 확정하고 테스트에 반영한다.
9. 바인딩 surface를 추가한다.
10. 정식 spec 문서를 구현된 API 기준으로 나누어 갱신한다.

## 열린 결정 사항

아래 항목은 구현 전에 결정해야 한다.

1. `zlink_spot_node_attach_channel_router()`가 local ROUTER routing id를 내부에서
   준비할지, 호출자가 attach 전에 반드시 설정해야 할지 정해야 한다.
2. dispatch subject enum을 `CHANNEL_SOCKET`으로 일반화할지,
   `CHANNEL_ROUTER`를 별도로 추가할지 정해야 한다.
3. 같은 channel name에 여러 ROUTER target을 허용할지 정해야 한다.
   이 초안은 첫 구현에서 하나만 허용한다.
4. `zlink_spot_request_router()` 기존 이름과 새 channel ROUTER API 이름을
   바인딩에서 어떻게 구분할지 정해야 한다.
5. ROUTER mesh Discovery attach가 기존 `zlink_socket_attach_discovery()`와 같은
   peer connection 코드를 재사용할 수 있는지 확인해야 한다.
