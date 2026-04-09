# Spot Routed Message Spec

> 이 문서는 현재 개발 라운드에서 구현 기준으로 쓰는 작업 스펙이다.
> 구현과 테스트가 끝난 뒤 공개 API 기준은 `doc/api` 문서에 반영한다.
> **관련 문서**:
> [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) — 공통 ZMP 전송 형식
> [`ZMP_SPOT_ROUTED_PROTOCOL.md`](ZMP_SPOT_ROUTED_PROTOCOL.md) — SPOT routed protocol envelope
> [`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md) — request-reply protocol envelope
> [`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md) — socket request-reply API

## 목적

현재 SPOT 은 위치투명 토픽 발행/구독에 맞춰 설계되어 있다.
이 구조는 이벤트 전파에는 잘 맞지만,
특정 객체나 특정 노드로 메시지 한 건을 직접 보내는 용도에는 한계가 있다.

예를 들면 다음과 같은 경우다.

- 특정 객체에 명령 1건을 보낼 때
- 원격 노드의 특정 처리기에만 요청을 보낼 때
- 같은 서버 안에서는 매우 짧은 경로로 전달하고 싶을 때

이 문서는 `SpotNode` 위에
"지정한 목적지로 직접 보내는 메시지 기능"을 추가하는 방식을 정의한다.

이 기능의 목표는 다음과 같다.

- 기존 SPOT 발행/구독 계약을 그대로 유지한다
- 같은 프로세스 안에서는 `inproc` 으로 바로 전달한다
- 원격 노드로는 `ROUTER` 소켓끼리 직접 주고받는다
- `SpotNode` 안에 별도의 routed send/recv 기능을 추가한다
- 기존 discovery/registry 기반 연결 구조를 최대한 재사용한다
- 주소 의미 충돌과 재전파 루프를 막는다


## 한 줄 요약

- `SpotNode` 안에 기존 토픽 전달 경로와는 별도의 직접 전달 경로를 추가한다.
- 보내는 쪽은 목적지 노드를 지정해서 메시지를 보낸다.
- 목적지가 같은 프로세스 안에 있으면 `inproc` 으로 바로 넘긴다.
- 목적지가 원격이면 노드 간 `ROUTER` 소켓으로 직접 보낸다.
- 이 경로는 기존 subscribe 처리와는 별개의 routed send/recv 경로다.
- `spot -> spot`, `spot -> router`, `router -> spot` 을 모두 지원한다.
- 직접 전달 경로 위에서 `request/reply` 를 함께 지원한다.
- `SpotNode` 와 `Spot` 은 각각 자기 계층의 대표 `routing_id` 를 가진다.


## 범위

이 스펙이 다루는 범위:

- `SpotNode` 내부의 직접 전달 기능
- 노드 단위 직접 송신/수신
- 같은 프로세스 안에서의 `inproc` 전달
- 원격 노드와의 `ROUTER` 대 `ROUTER` 통신
- 목적지 구분
- routed send/recv 규칙
- discovery/registry 와의 연결 방식
- 공개 API
- 실패 처리, 모니터링, 순서 보장 기본 규칙

이 스펙이 다루지 않는 범위:

- 영속 큐
- 정확히 한 번 전달 보장
- 여러 중간 노드를 거치는 전달
- 범용 중계 서버 체인
- 세부 권한 정책
- 혼합 버전 간 협상 세부 설계


## 배경

현재 SPOT 의 핵심 동작은 다음과 같다.

- 로컬에서 publish 하면 node worker 가 받는다
- worker 는 로컬 subscriber 들에게 전달한다
- 동시에 원격 peer 들에게도 메시지를 보낸다
- 원격에서 받은 메시지는 다시 멀리 퍼뜨리지 않고 그 노드 안에서만 처리한다

이 구조는 "같은 토픽을 구독하는 여러 수신자에게 퍼뜨리는" 목적에는 적합하다.
하지만 아래와 같은 경우에는 과한 모델이다.

- 특정 객체 하나에게만 보내고 싶은 경우
- 특정 노드 하나에게만 보내고 싶은 경우
- 특정 `SpotNode` 의 routed endpoint 로 직접 보내고 싶은 경우

또 한 가지 정리가 필요하다.
`SpotNode` 와 그 안의 여러 `Spot` 은
서로 다른 주소 계층을 가져야 한다.

- `SpotNode` 는 node 수준 주소가 필요하다
- 각 `Spot` 은 node 안에서 자신을 식별하는 주소가 필요하다

이 둘은 서로 다른 계층이지만,
각 계층 안에서는 pub 과 routed router 가 같은 `routing_id` 를 공유하는 것이 자연스럽다.


## 기본 원칙

### 1. 토픽 전달과 직접 전달은 구분한다

토픽 발행/구독과 직접 전달은 같은 런타임 안에 들어갈 수 있다.
하지만 의미는 분리해야 한다.

- 토픽 전달: 발행 후 조건에 맞는 subscriber 들에게 퍼진다
- 직접 전달: 지정한 목적지 한 곳으로 간다

두 기능을 같은 API 이름이나 같은 주소 의미로 섞지 않는다.

### 2. 주소 계층은 둘로 나눈다

이 스펙에서는 주소 계층을 다음 둘로 나눈다.

- `SpotNode routing_id`: node 수준 주소
- `Spot routing_id`: 같은 node 안의 개별 spot 주소

중요한 점은 다음과 같다.

- `SpotNode` 내부 pub 과 routed router 는 같은 node `routing_id` 를 공유한다
- 같은 `Spot` 내부 pub 과 routed router 는 같은 spot `routing_id` 를 공유한다
- 하지만 node `routing_id` 와 spot `routing_id` 는 서로 다른 계층의 주소다

### 3. 같은 프로세스 안에서는 가장 짧은 경로를 사용한다

같은 프로세스 안의 목적지라면 네트워크 경로를 거치지 않는다.
바로 `inproc` 으로 전달한다.

### 4. 원격 전달은 한 번만 건넌다

보내는 노드는 목적지 노드를 직접 찾은 뒤 그 노드로 바로 보낸다.
중간 노드를 거쳐 다시 전달하지 않는다.

### 5. routed 경로는 pub/sub 경로와 분리한다

이 기능은 기존 subscribe 경로를 우회해서
별도의 routed send/recv 표면으로 동작해야 한다.

- routed 메시지는 기존 subscriber 에게 전달하지 않는다
- routed 메시지는 별도의 recv 표면과 callback 표면으로 받는다
- publish/subscribe 의미와 routed 의미를 같은 API 에 섞지 않는다
- request/reply 는 routed payload 안의 별도 상위 프로토콜로 처리한다


## 용어

| 용어 | 설명 |
|------|------|
| `node routing_id` | `SpotNode` 의 대표 주소 |
| `node router` | 다른 노드와 직접 메시지를 주고받는 내부 `ROUTER` 소켓 |
| `route ingress` | 로컬 애플리케이션이 node 런타임으로 직접 메시지를 넣는 내부 입력 지점 |
| `route receiver` | 직접 전달 메시지를 받는 로컬 수신자 |
| `spot routing_id` | 같은 `SpotNode` 안의 개별 `Spot` 주소 |
| `router rid` | 일반 `ROUTER` peer 식별자 |
| `destination class` | 목적지 종류. `spot` 또는 `router` |
| `event notification` | payload 를 직접 넘기지 않고 "지금 읽을 수 있음"만 알리는 이벤트 |


## 범위 밖 항목

이 스펙 범위 밖의 항목:

- 여러 중간 노드를 거치는 전달
- 와일드카드 목적지 지정
- 한 번 보내서 여러 노드에 동시에 직접 전달
- 직접 전달 경로에서의 패턴 매칭
- 직접 전달 메시지를 자동으로 클러스터 전체 publish 로 바꾸는 기능
- 직접 전달 메시지 재생
- 직접 전달 메시지 묶음 전송


## 전체 구조

`SpotNode` 는 기존 토픽 처리 경로 외에 routed 전달 경로를 하나 더 가진다.

```text
+------------------------------------------------------------------+
|                            SpotNode                              |
+------------------------------------------------------------------+
| Topic Messaging                                                  |
|  spot pub -> pub_ingress -> worker -> local sub / mesh pub-xsub  |
+------------------------------------------------------------------+
| Routed Messaging                                                 |
|  route sender -> route_ingress -> worker -> route recv surface   |
|                                   -> node_router -> peer router  |
+------------------------------------------------------------------+
| Control                                                          |
|  discovery / registry / peer ctrl / readiness / topology         |
+------------------------------------------------------------------+
```

중요한 점은
직접 전달 기능이 기존 토픽 기능을 대체하는 것이 아니라,
같은 `SpotNode` 안에 별도 경로로 추가된다는 점이다.


## SPOT Request-Reply

### 목적

SPOT 직접 전달 위에서도 request-reply 를 사용할 수 있어야 한다.
이유는 SPOT 이 결국 `ROUTER` 기반 메시징 위에 올라가고,
특정 `Spot` 또는 특정 `ROUTER` 로 요청 1건을 보내고
그 응답 1건을 기다리는 패턴이 필요하기 때문이다.

이 기능은 기존 socket request-reply 와 별도 의미를 만들지 않는다.
같은 request-reply envelope 를 그대로 재사용하고,
그 바깥에 SPOT routed envelope 를 한 겹 더 두는 방식으로 정리한다.

### 계층 순서

SPOT request 메시지는 아래 순서로 해석한다.

```text
[router transport envelope if needed]
[spot routed protocol envelope]
[request-reply protocol envelope]
[payload]
```

즉 받는 쪽은 먼저 SPOT routed envelope 로 목적지를 해석하고,
그 다음 payload 앞부분의 request-reply envelope 로
request 인지 reply 인지와 request seq 를 해석한다.

### 지원 방향

아래 세 방향을 모두 지원한다.

- `spot -> spot request/reply`
- `spot -> router request/reply`
- `router -> spot request/reply`

이때 request/reply 의미는
[`ZMP_REQUEST_REPLY_PROTOCOL.md`](ZMP_REQUEST_REPLY_PROTOCOL.md)
를 그대로 따른다.

### 공개 표면 방향

SPOT 쪽에도 socket request-reply 와 비슷한 공개 표면이 필요하다.

최소한 아래 세 가지가 있어야 한다.

- `spot.request(...)`
- `spot.reply(...)`
- `spot.request_handler(...)`
- `spot.request_recv(...)`

C API 방향은 아래와 같다.

```c
typedef void (*zlink_spot_reply_handler_fn)(
    int errno,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

typedef void (*zlink_spot_request_handler_fn)(
    const zlink_routing_id_t *source_rid,
    const zlink_routing_id_t *spot_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

typedef enum zlink_spot_option_t {
    ...
    ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS = ...,
} zlink_spot_option_t;

int zlink_set_spot_option(
    void *spot,
    zlink_spot_option_t option,
    const void *optval,
    size_t optvallen);

int zlink_get_spot_option(
    void *spot,
    zlink_spot_option_t option,
    void *optval,
    size_t *optvallen);

int zlink_spot_request_spot(
    void *spot,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    uint32_t timeout_ms,
    zlink_spot_reply_handler_fn handler,
    void *userdata);

int zlink_spot_request_router(
    void *spot,
    const zlink_routing_id_t *peer_rid,
    zlink_msg_t *parts,
    size_t part_count,
    uint32_t timeout_ms,
    zlink_spot_reply_handler_fn handler,
    void *userdata);

int zlink_spot_reply_spot(
    void *spot,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count);

int zlink_spot_reply_router(
    void *spot,
    const zlink_routing_id_t *peer_rid,
    uint64_t request_seq,
    zlink_msg_t *parts,
    size_t part_count);

int zlink_spot_request_handler(
    void *spot,
    zlink_spot_request_handler_fn handler,
    void *userdata);

int zlink_spot_request_recv(
    void *spot,
    const zlink_routing_id_t **source_rid_out,
    const zlink_routing_id_t **spot_rid_out,
    uint64_t *request_seq_out,
    zlink_msg_t **parts_out,
    size_t *part_count_out);
```

설명:

- request timeout 기본값은 `Spot` 전용 option 으로 설정한다
- `Spot` 은 `zlink_set_spot_option()` 으로 `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS` 를 설정한다

예:

```c
uint32_t timeout_ms = 1000;

zlink_set_spot_option(
    spot,
    ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS,
    &timeout_ms,
    sizeof(timeout_ms));
```

handler 필드 규칙:

- 이 handler 의 두 address 필드는 "reply target 주소 슬롯"으로 해석한다
- `router` 에서 온 request 이면 `source_rid = router peer rid`, `spot_rid = empty` 다
- `spot` 에서 온 request 이면 `source_rid = source node rid`, `spot_rid = source spot rid` 다
- `request_seq` 는 두 경우 모두 항상 있어야 한다
- 두 address 필드가 모두 비어 있으면 잘못된 요청이다

`request_recv(...)` 도 같은 정보 모델을 쓴다.

- `source_rid_out`, `spot_rid_out`, `request_seq_out` 의 의미는
  `request_handler(...)` 의 같은 이름 파라미터와 같다
- callback 기반 수신과 pull 기반 수신이 서로 다른 주소 해석 규칙을 가지면 안 된다

핵심 규칙:

- 같은 `Spot` 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있어야 한다
- 각 request 는 request seq 로 구분한다
- reply 도착 순서는 request 송신 순서와 같을 필요가 없다
- high-level `request()` 완료는 첫 reply 1건으로 끝난다
- 같은 request seq 의 추가 reply 는 무시하고 카운터만 올릴 수 있다
- `spot.reply(...)` 는 `ctx` 가 아니라 목적지 주소와 `request_seq` 를 직접 받는다
- handler 에서 `spot_rid` 가 비어 있으면 `zlink_spot_reply_router(...)` 를 써야 한다
- handler 에서 `spot_rid` 가 있으면 `zlink_spot_reply_spot(...)` 를 써야 한다
- 위 규칙과 다른 reply 함수를 쓰면 `EINVAL` 로 즉시 실패해야 한다
- `spot` 에서 온 request 와 `router` 에서 온 request 는 서로 다른 recv 함수로 나누지 않는다
- `spot.request_handler(...)` 와 `spot.request_recv(...)` 는 같은 request 수신 plane 을 공유한다

### 구현 순서 메모

request-reply 는 먼저 `ROUTER/DEALER` core 작업을 끝낸 뒤
그 위에 SPOT request-reply 를 얹는 순서가 자연스럽다.

timeout 은
SPOT request/reply 의 마지막 단계에서 붙인다.
즉 처음에는 routing 과 request seq matching 을 먼저 닫고,
timeout 정책은 맨 마지막에 추가한다.


## 최종 방향

### 기능 형태

이 기능은 `SpotNode` 기반의 선택적 확장 기능이다.

- 기존 SPOT 기능은 그대로 유지한다
- 직접 전달 기능을 쓰지 않는 애플리케이션은 기존과 동일하게 동작한다
- 로컬 전달과 원격 전달은 같은 공개 API 체계로 표현한다
- 런타임이 같은 프로세스인지 원격인지 판정한다

### 배포 규칙

이 기능을 사용하는 노드들은 모두 같은 형식을 이해해야 한다.

- 직접 전달 기능을 쓰는 노드들은 모두 이 형식을 이해해야 한다
- 토픽 전용 SPOT 노드와 함께 존재할 수는 있다
- 하지만 직접 전달 목적지로는 이 기능을 지원하는 노드만 사용해야 한다

### 하위 호환

- 기존 `publish/subscribe/recv/callback` 계약은 바꾸지 않는다
- 기존 토픽 메시지 형식은 바꾸지 않는다
- 기존 `__zlink.spot.ctrl.*` 제어용 subject 와 직접 전달용 메시지는 분리한다


## 주소 모델

### 주소 계층

각 `SpotNode` 는 node 수준의 `routing_id` 를 가진다.
그리고 같은 node 안의 각 `Spot` 도 자기 `routing_id` 를 가진다.

요구사항:

- node `routing_id` 는 클러스터 안에서 겹치지 않아야 한다
- 같은 node 안의 spot `routing_id` 는 그 node 안에서 겹치지 않아야 한다
- 바이너리 값을 그대로 담을 수 있어야 한다
- 길이는 기존 `zlink_routing_id_t` 제한을 따른다
- bind/connect 이후에는 바꿀 수 없다

### 목적지 표현

직접 송신 시 목적지는
공개 API 기준으로 아래 두 종류 중 하나다.

- `spot` 목적지: `dest_node_rid + dest_spot_rid`
- `router` 목적지: `peer_rid`

### `routing_id` 소유 범위

이 스펙에서 주소는 두 계층으로 나뉜다.

기본 규칙:

- `SpotNode routing_id` 는 node 수준 주소다
- `Spot routing_id` 는 같은 node 안의 개별 `Spot` 주소다
- `SpotNode` 에 `zlink_set_routing_id()` 를 호출하면 node `routing_id` 를 설정한다
- `Spot` 에 `zlink_set_routing_id()` 를 호출하면 spot `routing_id` 를 설정한다
- 각 계층 안에서는 pub 과 routed router 가 같은 `routing_id` 를 공유한다
- bindings 는 `routing_id` 의 내부 포맷을 해석하지 않는다

### 주소 해석 규칙

- 목적지 node 는 반드시 node `routing_id` 로 해석한다
- 목적지 spot 은 반드시 spot `routing_id` 로 해석한다
- registry/discovery 는 node `routing_id` 와 spot `routing_id` 에 대응하는
  노드 endpoint 정보를 조회할 수 있어야 한다

### 로컬 노드 판정 규칙

같은 프로세스 안의 다른 `SpotNode` 로 보내는 경우까지
`inproc` 최적화를 적용하려면
런타임 안에 로컬 노드 디렉터리가 있어야 한다.

최소 요구사항:

- 같은 프로세스 안에서 살아 있는 `SpotNode` 들의
  `node routing_id + spot routing_id -> local route ingress` 매핑을 유지한다
- 송신 시 목적지 `node routing_id + spot routing_id` 가 이 로컬 디렉터리에서 발견되면
  네트워크 경로 대신 로컬 전달 경로를 선택한다
- 자기 자신의 `node routing_id + spot routing_id` 와 같은 경우도
  로컬 디렉터리 hit 의 특수한 경우로 취급한다
- 로컬 디렉터리에 없을 때만 discovery/registry 기반 원격 조회를 시도한다


## 내부 전달 경로

### 같은 프로세스 안의 직접 전달

보내는 쪽과 받는 쪽이 같은 프로세스 안에 있으면 네트워크를 거치지 않는다.

```text
route sender -> route_ingress(inproc) -> node worker -> local route recv
```

규칙:

- peer `node_router` 를 거치지 않는다
- 대상 노드가 살아 있는지만 확인하면 된다
- 도착한 메시지는 대상 `SpotNode` 안의 target `Spot` routed recv 경로로 넘긴다

### 원격 노드로의 직접 전달

목적지 노드가 원격이면 `node_router` 를 사용한다.

```text
route sender -> route_ingress -> local worker -> local node_router
            -> remote node_router -> target spot route receiver
```

규칙:

- 보내는 노드에서 목적지 노드로 한 번만 보낸다
- 목적지 노드를 찾지 못하면 송신 단계에서 실패한다
- 받은 노드는 다른 노드로 다시 넘기지 않는다

### 같은 프로세스 안의 `spot -> spot`

```text
route sender -> route_ingress -> worker -> target spot router recv
```

### 원격 `spot -> spot`

```text
route sender -> route_ingress -> local node_router
            -> remote node_router -> target spot router recv
```


## routed 메시지 의미

routed 메시지는 pub/sub 메시지와 다르다.

특징:

- 토픽 매칭이 없다
- subscriber 들에게 퍼뜨리지 않는다
- 기존 subscribe/callback 과 별도의 recv/callback 으로 받는다
- 수신 지점은 `SpotNode` 안의 target `Spot` routed recv 경로다


## 공개 API

중요한 점은 토픽 API 와 직접 전달 API 를 분리하는 것이다.

또 한 가지 중요한 기준이 있다.
기존 `zlink.h` 가 이미 제공하는 공용 개념은 최대한 그대로 재사용한다.

- `routing_id` 설정/조회는 기존 `zlink_set_routing_id()` 와
  `zlink_get_routing_id()` 를 그대로 사용한다
- recv ownership 규칙은 기존 `zlink_recv()` / `zlink_subscribe()` 와 같은 방식으로 맞춘다
- timer 는 기존 generic `zlink_timers_*` 와 역할이 겹치지 않도록
  spot 소유 helper 라는 점을 분명히 적는다

### 핸들 구조

- `SpotNode`: 토폴로지와 수명주기를 소유한다
- `Spot`: 기존 토픽 발행/구독용 통합 인터페이스다
- routed send/recv, request/reply, timer 는 모두 기존 `Spot` handle 위에 직접 얹는다

해석 규칙:

- `SpotNode` 는 내부적으로 routed router send/recv 를 수행하지만,
  node 수준의 public routed send/recv 표면은 노출하지 않는다
- routed public surface 는 별도 borrowed facade 가 아니라 기존 `Spot` handle 에 직접 연다
- 따라서 같은 `Spot` 에 대한 routed recv queue, callback mode, event mode 는
  모두 그 `Spot` handle 하나에 귀속된다
- `EBUSY` 같은 수신 모델 충돌 규칙도 같은 `Spot` handle 기준으로 적용한다

### 노드 주소 설정/조회

```c
int zlink_set_routing_id(void *handle,
                         const void *data,
                         size_t size);
int zlink_get_routing_id(void *handle,
                         zlink_routing_id_t *out);
```

해석 규칙:

- `handle=spot_node` 이면 node `routing_id` 를 다룬다
- `handle=spot` 이면 spot `routing_id` 를 다룬다
- 각 handle 내부 pub 과 routed router 는 같은 `routing_id` 를 공유한다

### 직접 송신

직접 송신 API 는
송신 방향이 드러나는 함수들로 나눈다.

```c
int zlink_spot_send_router(void *spot,
                           const zlink_routing_id_t *peer_rid,
                           zlink_msg_t *parts,
                           size_t part_count,
                           zlink_send_flags_t flags);

int zlink_spot_send_spot(void *spot,
                         const zlink_routing_id_t *dest_node_rid,
                         const zlink_routing_id_t *dest_spot_rid,
                         zlink_msg_t *parts,
                         size_t part_count,
                         zlink_send_flags_t flags);
```

이 표면을 쓰는 이유는 다음과 같다.

- 사용자가 `spot -> router`, `spot -> spot`, `router -> spot` 을
  함수 이름만 보고 바로 구분할 수 있다
- 바인딩에서 더 읽기 쉬운 이름으로 그대로 노출하기 쉽다
- 잘못된 조합을 API 표면에서 줄일 수 있다

추가 규칙:

- `send_spot()` 는 `dest_node_rid` 와 `dest_spot_rid` 를 함께 받는다
- `send_router()` 는 일반 `ROUTER` peer `routing_id` 만 받는다
- `router_send_spot()` 는 일반 `ROUTER` 에서 `SpotNode` routed 경로로 보내는 함수다

### 직접 수신

```c
int zlink_spot_recv(void *spot,
                    const zlink_routing_id_t **source_rid_out,
                    const zlink_routing_id_t **spot_rid_out,
                    zlink_msg_t **parts_out,
                    size_t *part_count_out,
                    int flags);
```

기본 규칙은 다음과 같다.

- routed 메시지만 이 recv 표면으로 돌려준다
- 기존 SPOT subscribe 메시지는 이 recv 표면으로 노출하지 않는다
- 두 address 출력 필드는 "reply target 주소 슬롯"으로 해석한다
- `router` 에서 온 메시지이면 `source_rid_out = router peer rid`,
  `spot_rid_out = empty` 다
- `spot` 에서 온 메시지이면 `source_rid_out = source node rid`,
  `spot_rid_out = source spot rid` 다
- 사용하지 않는 출력 값은 비워서 돌려준다

ownership 규칙은 기존 `zlink_recv()` 와 같은 방식으로 맞춘다.

- 성공 시 각 `zlink_msg_t` payload ownership 은 호출자에게 넘어간다
- `parts_out` 배열 자체는 라이브러리 thread-local view 다
- 호출자는 각 part 를 `zlink_msg_close()` 또는
  `zlink_multipart_close()` 로 닫아야 한다
- 호출자는 `parts_out` 자체를 `free()` 하면 안 된다
- 같은 thread 에서 다음 recv 계열 호출 전까지만 유효하다고 본다

### 직접 수신 콜백

```c
typedef void (*zlink_spot_handler_fn)(
    const zlink_routing_id_t *source_rid,
    const zlink_routing_id_t *spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

int zlink_spot_handler(void *spot,
                       zlink_spot_handler_fn handler,
                       void *userdata);
```

직접 수신 콜백은 routed recv 표면에만 적용한다.
기존 subscribe callback 과는 별개다.
또 recv payload shape 와 callback payload shape 는
같은 routed recv 경로 안에서 동일하게 유지한다.

### 수신 모델 충돌 규칙

같은 수신 표면에서는
`recv` 모델과 `callback` 모델을 동시에 허용하지 않는다.

이 규칙은 기존 subscribe 표면과 새 routed 표면에 모두 동일하게 적용한다.

정리:

- subscribe callback 을 등록하면 기존 subscribe `recv` 는 막힌다
- subscribe `recv` 를 사용하는 동안 subscribe callback 등록은 막힌다
- routed callback 을 등록하면 `zlink_spot_recv()` 는 막힌다
- `zlink_spot_recv()` 를 사용하는 동안 routed callback 등록은 막힌다

이유:

- 같은 메시지가 `recv` 와 callback 양쪽으로 동시에 보이면 이중 소비 위험이 생긴다
- 어느 쪽이 먼저 메시지를 가져가는지 모호해진다
- 바인딩과 샘플에서 동작 설명이 복잡해진다

규칙:

- 충돌하는 수신 모델을 등록하거나 호출하면 `EBUSY` 로 실패시킨다
- 한 표면에서는 하나의 수신 모델만 활성화한다

여기서 말하는 `callback` 은
메시지 payload 를 직접 전달하는 delivery callback 을 뜻한다.

`event callback` 은 delivery callback 이 아니다.
따라서 event 모델은
같은 표면의 `recv` 와 함께 사용하는 것을 허용한다.


## Event 기반 단일 스레드 처리

### 왜 이 모델이 필요한가

기존 subscribe callback 과 routed callback 을
각각 별도 callback thread 에서 실행하면
같은 공용 객체를 접근할 때 동기화 부담이 커질 수 있다.

하지만 `SpotNode` 가 수천 개까지 늘어날 수 있으므로
`SpotNode` 마다 전용 스레드를 두는 방식은 적합하지 않다.

이 스펙에서 권장하는 모델은 다음과 같다.

- event handler 가 `Spot` 과 `SpotTimer` 의 readable 상태를 알려준다
- event callback 은 "이 spot 또는 timer 에 읽을 것이 있다"는 사실만 알려준다
- 사용자는 그 callback 안에서 `zlink_subscribe()`,
  `zlink_spot_recv()`, `zlink_spot_timer_recv()` 를 호출한다
- 공용 상태 변경은 그 event loop thread 안에서만 수행한다

즉 권장 수신 모델은
"callback 직접 처리"보다
"event callback + recv" 쪽이다.

### 권장 원칙

- event callback 은 메시지 payload 자체를 직접 넘기지 않는다
- event callback 은 "지금 읽을 것이 있다"는 사실만 알려준다
- 실제 메시지 소비는 사용자가 같은 event loop thread 에서
  `recv` 또는 `zlink_subscribe()` 로 수행한다
- event callback 은 하나의 thread 에서만 호출되도록 보장하는 것을 권장한다

이렇게 하면
공용 객체 접근을 단일 스레드로 몰아
동기화 문제를 단순하게 만들 수 있다.

### event 종류

최소한 아래 세 이벤트를 구분할 수 있어야 한다.

- subscribe 쪽에 읽을 메시지가 있음
- routed recv 쪽에 읽을 메시지가 있음
- timer 쪽에 읽을 만료 정보가 있음

예시:

```c
typedef enum zlink_spot_dispatch_event_t {
    ZLINK_SPOT_DISPATCH_EVENT_SUB_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTE_READABLE = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3
} zlink_spot_dispatch_event_t;
```

### event 표면 초안

이 스펙은 `recv`, 직접 delivery `callback`, `event` 세 public 모델을 모두 허용한다.
그중 공용 상태를 한 thread 에서 처리해야 하는 애플리케이션에는
`Spot` 귀속 event handler 모델을 권장한다.

다만 이 말은 generic poller 사용을 금지한다는 뜻은 아니다.

- 기존 recv 모델만 쓴다면 `Spot` 을 generic poller 에 직접 등록할 수 있다
- generic poller 는 ready 된 `Spot` handle 자체를 돌려주므로,
  ready 된 `Spot` 에 대해서만 `zlink_subscribe()` 를 호출하면 된다
- 즉 `Spot` 이 많아도 매 poll 마다 모든 `Spot` 에 recv 를 호출할 필요는 없다

하지만 generic poller 만으로는 아래를 한 번에 풀어 설명하기 어렵다.

- 같은 `Spot` 에서 subscribe recv 와 routed recv 를 함께 쓰는 경우
- `SpotTimer` 까지 같이 붙는 경우
- ready 된 handle 이 "무슨 종류의 readable 인가"를 쉽게 알고 싶은 경우

그래서 이 스펙은 generic poller 도 허용하지만,
사용자에게 권장하는 상위 표면으로는 `event callback` 모델을 둔다.

예시:

```c
typedef void (*zlink_spot_dispatch_event_handler_fn)(
    void *handle,
    zlink_spot_dispatch_event_t event,
    void *userdata);

int zlink_spot_dispatch_event_handler(
    void *spot,
    zlink_spot_dispatch_event_handler_fn handler,
    void *userdata);
```

기본 방향:

- event handler 는 `Spot` 하나에 귀속된다
- callback 의 `handle` 은 읽을 것이 생긴 `Spot` 또는 `SpotTimer` 다
- 사용자는 callback 안에서 해당 handle 에 맞는 recv 함수를 직접 호출한다
- 내부 구현은 필요하면 `zlink_poller_*` 를 사용할 수 있다
- 하지만 공개 계약은 `event callback + recv` 모델로 설명한다

`Spot` 에 대한 `ROUTE_READABLE` 이벤트는
같은 `Spot` 에 읽을 routed 메시지가 생겼다는 뜻이다.
이 경우 사용자는 아래처럼 처리한다.

- callback 의 `handle` 을 `Spot` 으로 해석한다
- 그 `Spot` 에 대해 routed recv 함수를 직접 호출한다

event 모델도
같은 수신 표면의 직접 callback delivery 와는 섞지 않는 것을 권장한다.

예:

- subscribe callback delivery 를 쓰는 `Spot` 에서는
  `SUB_READABLE` 기반 소비를 함께 쓰지 않는다
- routed callback delivery 를 쓰는 `Spot` 에서는
  `ROUTE_READABLE` 기반 `recv` 소비를 함께 쓰지 않는다
- timer callback delivery 를 쓰는 `SpotTimer` 에서는
  `TIMER_READABLE` 기반 `zlink_spot_timer_recv()` 소비를 함께 쓰지 않는다

즉 event 모델을 선택했다면
그 표면에서는 event 알림 + `recv` 조합으로 일관되게 사용하는 편이 가장 단순하다.

### event callback 안에서의 처리 방식

권장 처리 방식은 다음과 같다.

- `SUB_READABLE` 이면 해당 `Spot` 에 대해 `zlink_subscribe()` 를 호출한다
- `ROUTE_READABLE` 이면 해당 `Spot` 에 대해 `zlink_spot_recv()` 를 호출한다
- `TIMER_READABLE` 이면 해당 `SpotTimer` 에 대해
  `zlink_spot_timer_recv()` 를 호출한다
- 각 callback 안에서 가능한 만큼 recv 하거나,
  event loop 정책에 맞춰 한 개씩 읽는다

예시 흐름:

```text
event callback
-> single-thread event loop
-> user calls subscribe recv or route recv or timer recv
-> user updates shared object on the same thread
```

짧은 사용 예시는 아래와 같다.

```c
zlink_spot_dispatch_event_handler(spot, on_spot_event, userdata);

// on_spot_event(handle, event, userdata)
//   SUB_READABLE    -> zlink_subscribe(spot, ...)
//   ROUTE_READABLE  -> zlink_spot_recv(spot, ...)
//   TIMER_READABLE  -> zlink_spot_timer_recv(timer, ...)
```

### 이 모델의 장점

- `SpotNode` 수가 많아도 thread-per-spot 이 필요 없다
- subscribe, routed recv, timer recv 를 같은 이벤트 루프에서 처리할 수 있다
- 직접 delivery callback 을 쓰지 않으면
  실제 상태 변경을 한 스레드에서 수행하기 쉬워진다
- 사용자는 `Spot` 단위로 event handler 를 붙여서 편하게 사용할 수 있다
- 사용자는 "도착 알림"과 "실제 메시지 소비"를 분리해서 이해할 수 있다

### 요약

정리하면 수신 모델은 아래처럼 나뉜다.

- 기존 subscribe callback
- 기존 subscribe recv
- routed callback
- routed recv
- timer callback
- timer recv

하지만 공용 상태를 안전하게 다뤄야 하는 애플리케이션에는
아래 모델을 기본 선택으로 권장한다.

- event handler 는 `Spot` 과 그에 귀속된 `SpotTimer` 의 readable 상태를 알린다
- event callback 은 readable 이벤트만 알린다
- 실제 메시지는 `zlink_subscribe()`, `zlink_spot_recv()`,
  `zlink_spot_timer_recv()` 로 읽는다
- 상태 변경은 event loop 의 단일 스레드에서 처리한다

### 주의사항

- 이 event 기반 모델은
  기존 subscribe callback, routed callback, timer callback 을
  대체하는 강제 모델은 아니다
- 다만 공용 상태를 안전하게 다뤄야 하는 애플리케이션에는
  callback 직접 처리보다 이 event 기반 모델을 우선 권장한다
- sample 과 문서에서는
  `recv` 모델, `callback` 모델, `event callback + recv` 모델을
  서로 구분해서 설명하는 것이 좋다


## Timer 통합 방향

### 기본 입장

`Spot` 은 raw socket 보다 복합적인 서비스 라이브러리 성격이 강하므로,
event loop 와 함께 동작하는 timer 기능을 제공하는 것은 자연스럽다.

즉 timer 자체는 이 스펙의 방향과 어긋나지 않는다.

다만 timer 문제의 핵심은
timer 기능 그 자체보다
binding 경계에서 callback 을 얼마나 자주 넘느냐에 있다.

### timer 표면 초안

timer 는 `Spot` 에 연결되고,
event loop 에서는 `SpotTimer` 단위 이벤트로 노출한다.

여기서 말하는 timer 는 기존 generic `zlink_timers_*` 를 대체하는 것이 아니다.
역할이 다르다.

- `zlink_timers_*`: socket/spot 바깥에서도 쓸 수 있는 범용 timer set
- `SpotTimer`: `Spot` 수명주기와 event callback 모델에 맞춘 spot 소유 timer

즉 public 의미는 spot 소유 service timer 이고,
내부 구현은 필요하면 기존 `zlink_timers_*` 를 재사용할 수 있다.

예시:

```c
void *zlink_spot_timer_new(void *spot);
int zlink_spot_timer_destroy(void **timer_p);
int zlink_spot_timer_start(void *timer,
                           uint64_t interval_ns,
                           uint64_t repeat_count);
int zlink_spot_timer_stop(void *timer);
int zlink_spot_timer_recv(void *timer,
                          uint64_t *expired_count_out,
                          int flags);

typedef void (*zlink_spot_timer_handler_fn)(
    uint64_t expired_count,
    void *userdata);

int zlink_spot_timer_handler(void *timer,
                             zlink_spot_timer_handler_fn handler,
                             void *userdata);
```

의미:

- timer 생성과 수명주기는 연결된 `Spot` 런타임에 속한다
- 만료 판정은 spot 런타임 내부에서 수행한다
- `repeat_count = 0` 이면 사용자가 중지할 때까지 같은 간격으로 계속 반복한다
- `repeat_count = 1` 이면 한 번만 실행한다
- `repeat_count > 1` 이면 지정한 횟수만큼 반복한 뒤 멈춘다
- event callback 은 `TIMER_READABLE` 이벤트만 알려준다
- 실제 만료 횟수 소비는 `zlink_spot_timer_recv()` 로 수행한다
- timer callback delivery 를 쓰고 싶으면 `zlink_spot_timer_handler()` 를 쓸 수 있다

### C/C++ 와 managed bindings 의 차이

C/C++ 에서는 timer 사용에 특별한 제약을 두지 않는다.
물론 실제 비용은 timer 개수와 workload 에 따라 달라지므로
운영 전 측정은 필요하다.

반면 C# / Java 같은 managed bindings 에서는
아래 비용을 함께 고려해야 한다.

- native -> managed callback marshalling 비용
- runtime scheduling 지터
- GC 영향

그래서 managed bindings 에서는
timer 기능을 금지하기보다
권장 주기를 문서로 안내하는 쪽이 맞다.

### managed bindings 권장 주기

managed bindings 에서는
일반적인 timer 주기를 대략 `8ms ~ 33ms` 범위 이상으로 잡는 것을 권장한다.

이 범위는 다음과 같은 용도에 잘 맞는다.

- heartbeat
- timeout
- housekeeping
- event-loop scheduling
- gameplay 보조 timer

이보다 더 짧은 주기를 쓰는 것도 막지 않지만,
그 경우에는 아래를 충분히 검증해야 한다.

- callback 빈도
- timer 개수
- callback 안에서 수행하는 작업량
- runtime 별 지터와 GC 영향

timer 표면에서도 수신 모델 충돌 규칙은 동일하게 적용한다.

- timer callback delivery 를 등록하면 `zlink_spot_timer_recv()` 는 막힌다
- `zlink_spot_timer_recv()` 를 사용하는 동안 timer callback delivery 등록은 막힌다
- 충돌하는 timer 수신 모델을 등록하거나 호출하면 `EBUSY` 로 실패시킨다

### 권장 문구

정리하면 timer 지원 정책은 아래처럼 이해하면 된다.

- `Spot` 은 event loop 와 함께 쓰는 timer 기능을 제공할 수 있다
- C/C++ 에서는 상대적으로 자유롭게 사용할 수 있다
- managed bindings 에서는 `8ms ~ 33ms` 정도를 일반 권장 범위로 본다
- 그보다 더 짧은 주기는 비금지이지만, workload 기준으로 충분한 측정과 검증이 필요하다


## 왜 별도 routed facade 를 두지 않는가

현재 기준에서는 직접 전달 API 를 별도 facade 로 분리하지 않는다.
그 이유는 다음과 같다.

- `Spot` 하나로 topic, routed, request-reply 표면을 함께 설명하는 편이 수명주기와 ownership 이 단순하다
- borrowed facade 를 하나 더 만들면 recv plane, callback mode, event mode 설명이 오히려 복잡해진다
- `Spot` 에 이미 `routing_id` 가 있으므로 직접 전달 표면을 같은 handle 에 두는 편이 더 자연스럽다

정리하면:

- `Spot` 은 topic, routed, request-reply 표면을 함께 가진다
- `SpotTimer` 는 `Spot` 에 붙는 timer 용이다


## 공개 send API 의미

공개 routed send API 는 아래 두 개를 둔다.

- `zlink_spot_send_router()`
- `zlink_spot_send_spot()`

이 함수들은 이름이 비슷해 보여도 목적지가 다르다.

### `zlink_spot_send_router()`

이 함수는
이미 연결된 `ROUTER` peer 의 `routing_id` 를 대상으로 보내는 함수다.

특징:

- 목적지는 `peer_rid` 다
- discovery 나 registry 로 목적지를 해석하지 않는다
- 이미 알고 있는 `ROUTER` peer identity 로 바로 보낸다
- SPOT publish/subscribe 의미를 갖지 않는다
- 받는 쪽 generic `ROUTER` 는 기존 `zlink_recv()` 또는 그와 같은 raw ROUTER recv 표면으로 받는다
- 이 경우 routed-message 여부는 protocol envelope 의 식별자로 구분한다
- payload ownership 규칙은 기존 raw `ROUTER` recv 규칙을 그대로 따른다
- 즉 `spot -> router` 는 generic `ROUTER` 쪽에 새 전용 recv API 를 추가하지 않아도,
  기존 raw `ROUTER` recv 위에서 protocol envelope 를 읽어 구분할 수 있어야 한다

### `zlink_spot_send_spot()`

이 함수는
다른 `SpotNode` 의 routed router endpoint 로 보내는 함수다.

특징:

- 목적지는 `dest_node_rid` 와 `dest_spot_rid` 다
- 로컬 노드 디렉터리 또는 discovery/registry 로 목적지를 해석한다
- 도착한 메시지는 대상 `SpotNode` 안의 target `Spot` routed recv 표면으로 들어간다
- 기존 subscribe 경로로 들어가지 않는다

`router -> spot` 송신 공개 API 는 socket 계열 기능이므로
[`SOCKET_REQUEST_REPLY_API_SPEC.md`](SOCKET_REQUEST_REPLY_API_SPEC.md)
문서에서 함께 다룬다.


## recv 식별자 해석 규칙

`recv` 와 routed callback 은
송신자 종류를 `source_kind` 로 먼저 구분하고,
그 뒤 `primary` 와 `secondary` 식별자를 해석한다.

### `source_kind=spot`

- `source_primary_rid`: 보내는 `SpotNode` 의 `routing_id`
- `source_secondary_rid`: 보내는 `Spot` 의 `routing_id`

### `source_kind=router`

- `source_primary_rid`: 보내는 일반 `ROUTER` peer 의 `routing_id`
- `source_secondary_rid`: 사용하지 않음

이 구조를 쓰는 이유는
필드 수를 줄이면서도
송신자 종류에 따라 값의 의미를 명확히 설명할 수 있기 때문이다.


## 일반 ROUTER send 와의 구분

이 스펙의 `Spot` routed send 는
일반 `ROUTER` 소켓의 peer-directed send 와는 다른 기능이다.

차이:

- `send_spot` 은 `SpotNode routing_id + Spot routing_id` 를 목적지로 사용한다
- `send_router` 는 transport peer `routing_id` 를 사용한다
- `send_spot` 은 discovery/registry 또는 로컬 노드 디렉터리로 목적지를 해석한다
- `send_router` 는 이미 연결된 peer identity 를 직접 사용한다

예시:

```c
int zlink_spot_send_router(void *spot,
                           const zlink_routing_id_t *peer_rid,
                           zlink_msg_t *parts,
                           size_t part_count,
                           zlink_send_flags_t flags);

int zlink_spot_send_spot(void *spot,
                         const zlink_routing_id_t *dest_node_rid,
                         const zlink_routing_id_t *dest_spot_rid,
                         zlink_msg_t *parts,
                         size_t part_count,
                         zlink_send_flags_t flags);
```

중요한 점은
이 API 들이 모두 `zlink_routing_id_t` 비슷한 모양을 쓰더라도
주소의 의미가 같지 않다는 점이다.

- `dest_node_rid`: SPOT node address
- `dest_spot_rid`: destination spot address inside the SPOT node
- `peer_rid`: ROUTER peer identity

따라서 이 값들은 함수 이름과 인자 이름에서 명확히 구분해야 한다.
주소 바이트 안에
"이건 SPOT 용", "이건 ROUTER 용" 같은 구분 태그를 넣는 방식은 사용하지 않는다.


## 바인딩 표면 규칙

일부 바인딩에서는 주소가 구조체가 아니라
단순 `string` 으로 보일 수 있다.

그래도 주소 문자열 자체에 타입 태그를 넣어 구분하지 않는다.

예:

- `spot:node-a`
- `router:peer-1`

같은 문자열 prefix 규칙을 public contract 로 만들지 않는다.

이유:

- 사용자가 문자열 인코딩 규칙까지 외워야 한다
- 주소값과 주소 종류가 한 문자열 안에 섞인다
- C/C++ core 의미와 바인딩 표면 의미가 어긋나기 쉽다
- 잘못된 prefix 를 넣었을 때 오류가 늦게 드러난다

대신 바인딩에서는 함수 이름과 인자 이름으로 구분한다.

예시:

```text
spotRoute.sendRouter(peerRoutingId, parts)
spotRoute.sendSpot(destNodeId, destSpotRid, parts)
router.sendSpot(destNodeId, destSpotRid, parts)
```

즉 바인딩에서 둘 다 `string` 이어도
다음 기준을 유지한다.

- `destNodeId` 는 SPOT node address 로 취급한다
- `destSpotRid` 는 SPOT node 안의 routed endpoint 로 취급한다
- `peerRoutingId` 는 ROUTER peer identity 로 취급한다
- 같은 문자열 타입이라고 해서 같은 의미로 취급하지 않는다
- 의미 구분은 문자열 포맷이 아니라 API 표면에서 한다


## 전송 형식

원격 직접 전달은 `ROUTER` 소켓끼리 multipart 메시지로 주고받는다.
다만 source/destination 주소 정보는
메시지 header 가 아니라 직접 전달 프로토콜 envelope 로 싣는다.

전송 형식의 정확한 wire 정의는
[`ZMP_SPOT_ROUTED_PROTOCOL.md`](ZMP_SPOT_ROUTED_PROTOCOL.md)
를 기준으로 본다.

이 문서에서는 아래 사항만 상위 동작 관점에서 전제로 둔다.

- 실제 wire 앞에는 `ROUTER` 송신 대상 식별용 transport envelope 가 붙을 수 있다
- source/destination 주소는 transport envelope 뒤의 SPOT routed protocol envelope 에 실린다
- request-reply 를 함께 쓰면 SPOT routed envelope 뒤에 request-reply envelope 가 온다
- 일반 `ROUTER` 는 기존 `zlink_recv()` 로 받은 뒤 protocol envelope 를 읽어
  source 주소를 reply 또는 follow-up `router -> spot` 송신에 사용할 수 있어야 한다
- `spot -> spot` 에서는 source node/spot 정보가 peer metadata 와 함께 검증되어야 한다


## 로컬 처리 규칙

### 로컬 목적지 판정

송신 시 목적지 `node routing_id + spot routing_id` 조합이
로컬 노드 디렉터리에서 발견되면
반드시 로컬 전달 경로를 사용한다.

### 로컬 routed 전달

- 대상 `SpotNode` 와 `dest_spot_rid` 유효성을 먼저 검사한다
- 그 뒤 대상 `SpotNode` 의 routed recv 경로에 넣는다
- 기존 publish/subscribe 경로로 바꾸지 않는다
- 클러스터 전체 publish 처럼 원격 peer 에 다시 보내지 않는다

로컬 routed consumer 가 없더라도,
`send_spot()` 의 성공 의미는 원격 경로와 같은 수준으로 유지하는 것이 원칙이다.

즉 `send_spot()` 성공은
"대상 node 경로에 직접 전달 메시지를 enqueue 하거나,
그와 동등한 transport handoff 를 완료했다"는 뜻으로 통일한다.

따라서 local/remote topology 에 따라
"성공의 의미"가 달라지지 않게 해야 한다.

구현은 다음 둘 중 하나를 택할 수 있다.

- 로컬 경로에서도 enqueue 성공까지만 send 성공으로 본다
- 더 강한 local 검증을 하더라도,
  public send 성공/실패 계약은 remote 경로와 같은 의미로 유지한다

로컬 target `Spot` 부재는 send call 의 동기 실패가 아니라
로컬 delivery failure 카운터, 상태 조회, 내부 진단 경로로 처리하는 편이 더 안전하다.


## Discovery / Registry 연동

### 광고해야 하는 정보

직접 전달 기능을 지원하는 `SpotNode` 는 다음 정보를 알릴 수 있어야 한다.

- 기존 토픽용 데이터 endpoint
- 직접 전달용 endpoint
- node `routing_id`
- 이 node 가 소유한 spot `routing_id` 목록
  또는 그 목록을 조회할 수 있는 정보
- 직접 전달 기능 버전
- peer 종류를 구분할 수 있는 정보

여기서 중요한 점은 `SpotNode` 하나에 `Spot` 여러 개가 있을 수 있다는 점이다.
따라서 discovery/registry 광고 단위는 단일 `spot routing_id` 값 하나가 아니라,
최소한 아래 둘 중 하나를 표현할 수 있어야 한다.

- `node endpoint + [spot_rid_1, spot_rid_2, ...]`
- `node endpoint + 별도 spot directory 조회 수단`

### 목적지 찾기

메시지를 보내기 전에 목적지 노드를 찾는 방법은 다음 둘 중 하나다.

- 프로세스 내부 로컬 노드 디렉터리 조회
- discovery 스냅샷을 이용한 원격 조회
- registry query client 를 통한 명시적 조회

이 스펙의 조회 방식:

- 먼저 `node routing_id + spot routing_id -> local route ingress` 로컬 매핑을 찾는다
- `SpotNode` 가 붙어 있는 discovery 정보들을 캐시한다
- 로컬에서 찾지 못한 경우에만
  `node routing_id + spot routing_id -> route endpoint` 원격 매핑을 찾는다

중요한 점은 연결 단위와 목적지 선택 단위를 구분하는 것이다.

- discovery/registry 가 광고하고 해석하는 직접 전달 endpoint 는
  개별 `Spot` endpoint 가 아니라 `SpotNode` 의 `node_router` endpoint 다
- 런타임이 자동으로 연결하는 대상도 원격 `SpotNode` 의 `node_router` 다
- `spot routing_id` 는 별도 원격 연결을 만들기 위한 값이 아니다
- `spot routing_id` 는 이미 연결되었거나 연결 가능한 원격 `SpotNode` 안에서
  어느 `Spot` 으로 메시지를 넣을지 고르는 식별자다

즉 직접 전달 경로는 아래처럼 이해하면 된다.

- 연결: `SpotNode node_router <-> SpotNode node_router`
- 목적지 선택: `node routing_id + spot routing_id`

또 한 가지 구분이 필요하다.
같은 discovery 서비스 안에는 generic `ROUTER` peer 와
`SpotNode node_router` peer 가 함께 존재할 수 있다.

규칙:

- discovery 는 routed-capable peer 들의 연결 정보를 공통 방식으로 관리할 수 있다
- 하지만 peer 정보에는 최소한 peer 종류 구분이 있어야 한다
- `SpotNode node_router` 는 `spot` 목적지를 해석할 수 있는 peer 로 본다
- generic `ROUTER` peer 는 `peer_rid` 기반 directed send 만 가능한 peer 로 본다
- `send_spot()` 는 `spot` 목적지를 해석할 수 있는 peer 에만 허용한다
- `send_router()` 는 generic `ROUTER` peer 전용이다
- 같은 서비스 안에 있다고 해서 generic `ROUTER` 를
  `spot` 목적지 해석 peer 로 취급하면 안 된다

### 토폴로지 변경

peer 구성이 바뀌면 런타임은 다음을 수행한다.

- 더 이상 유효하지 않은 route endpoint 제거
- 새 route endpoint 연결
- 대기 중이던 송신은 일반 송신 실패 규칙을 따른다


## 순서 규칙

### 같은 노드 쌍 사이 순서

같은 보내는 노드에서 같은 받는 노드로 보내는 직접 전달 메시지는
transport 가 허용하는 범위 안에서 보낸 순서를 유지하는 것을 목표로 한다.

### 토픽 메시지와의 상대 순서

직접 전달과 토픽 전달 사이의 상대 순서는 보장하지 않는다.

즉 다음은 정의하지 않는다.

- `publish(A)` 후 `route(B)` 를 보냈을 때 원격에서 어느 쪽을 먼저 보는지
- `route_spot(X)` 와 `publish(X)` 중 무엇이 먼저 보이는지

이 순서가 필요하면 애플리케이션이 한 종류의 전달 방식만 써야 한다.


## 실패 처리 규칙

### 송신 단계 실패

다음 경우 송신이 실패할 수 있다.

- 목적지 노드를 찾지 못한 경우
- peer router 가 연결되지 않은 경우
- HWM, timeout, backpressure 문제
- 잘못된 인자
- 런타임이 fault 상태인 경우

### 원격 routed 수신자가 없는 경우

`spot -> spot` 또는 `router -> spot` 메시지에서
도착한 노드 안에 matching `dest_spot_rid` 가 없으면
기본 계약은 최선 전달이다.

즉:

- 네트워크 전달 성공
- 원격 노드 안의 router recv 지점까지 도착
- 실제 routed consumer 가 없을 수 있다

이 규칙은 local/remote topology 에 따라 달라지지 않도록 해석해야 한다.
즉 `send_spot()` 의 public 성공 의미는
로컬과 원격 모두 "target node handoff 성공" 수준으로 맞춘다.

### 로컬 경로 실패

목적지가 로컬일 때도 아래와 같은 경우에는 송신 호출이 실패할 수 있다.

- 로컬 route ingress 자체가 닫혀 있거나 shutdown 상태인 경우
- enqueue/handoff 자체가 HWM, timeout, fault 상태로 실패한 경우
- 인자가 잘못된 경우

반면 "target `Spot` 가 실제로 없었다"는 이유만으로
로컬 send 만 즉시 실패시키는 계약은 두지 않는다.

### request 대상 `Spot` 이 없는 경우

`spot -> spot request` 또는 `router -> spot request` 가
대상 `SpotNode` 까지는 정상적으로 도착했지만
`dest_spot_rid` 와 일치하는 target `Spot` 이 없으면
이 경우는 timeout 이 아니라 명시적 request 실패로 처리한다.

규칙:

- 대상 `SpotNode` 는 request 를 drop 하지 않는다
- 대상 `SpotNode` 는 같은 reply 경로로 request-reply `error reply` 를 돌려보낸다
- 이때 `errno` 값은 `ENOENT` 다
- requester 쪽 high-level completion 은 `errno = ENOENT` 로 완료된다
- 이 규칙은 local delivery 와 remote delivery 에 동일하게 적용한다

즉 `spot not found` 는
"응답이 오지 않음"이 아니라
"대상 node 에는 도착했지만 target `Spot` 이 없음"을 뜻한다.
그래서 timeout 과 구분해야 한다.


## 큐와 혼잡 제어

직접 전달 기능은 토픽 전달과 별도 큐 한도를 가져야 한다.

이유:

- 직접 전달 폭주가 토픽 전달 지연을 망치면 안 된다
- 토픽 publish 폭주가 직접 전달 지연을 망치면 안 된다

권장 내부 항목:

- `route_ingress_rcvhwm`
- `route_receiver_sndhwm`
- `node_router_sndhwm`
- `node_router_rcvhwm`

이 값들은 별도 옵션 계열로 노출하거나,
`SpotNode` 내부 옵션으로 진단 가능하게 둘 수 있다.


## 모니터링과 상태 조회

직접 전달 기능이 추가되면 다음 상태를 확인할 수 있어야 한다.

- 로컬 직접 전달 기능 활성 상태
- 연결된 원격 직접 전달 peer 수
- 직접 전달용 endpoint
- 직접 송신 실패 횟수
- 로컬 직접 수신 큐 길이 또는 drop 횟수
- routed recv 로 넘긴 횟수

최소 요구사항:

- `SpotNode status snapshot` 에 직접 전달 endpoint, 활성 여부, peer 수 추가
- peer snapshot 에 직접 전달 연결 상태 표시

이 항목은 기존 SPOT observability 재정의와 일치해야 한다.

- 새로운 public SPOT service monitor API 는 추가하지 않는다
- public surface 는 계속 `spot_node_status_snapshot()`,
  `spot_node_peers_snapshot()`, `spot_node_subjects_snapshot()` 중심으로 유지한다
- 필요하면 내부 raw socket monitor 를 구현에만 사용한다


## 유효성 검사

### routed 목적지 검사

`spot -> spot` 또는 `router -> spot` 메시지의 목적지는 다음을 만족해야 한다.

- `dest_node_rid` 가 비어 있지 않아야 한다
- `dest_spot_rid` 가 비어 있지 않아야 한다
- 길이 제한을 지켜야 한다
- node 주소와 spot 주소는 서로 다른 계층의 주소로 다뤄야 한다

`spot -> router` 메시지의 목적지는 다음을 만족해야 한다.

- `peer_rid` 가 비어 있지 않아야 한다
- 길이 제한을 지켜야 한다

### 수신 envelope 검사

받는 쪽은 다음과 같은 잘못된 메시지를 버려야 한다.

- protocol id 가 없거나 맞지 않는 경우
- protocol version 이 맞지 않는 경우
- routed envelope 조합이 허용된 규칙과 맞지 않는 경우
- source 또는 destination 식별자가 비어 있는 경우
- source class 또는 destination class 가 맞지 않는 경우
- `spot` 목적지인데 `dest_spot_rid` 가 비어 있는 경우
- `router` 목적지인데 `destination_node_rid` 가 비어 있지 않은 경우

이런 잘못된 메시지 하나 때문에
노드 전체를 fault 상태로 만들지 않는다.
기본 동작은 "버리고, 오류 카운터를 올리고, 필요하면 모니터링에 남기는 것"이다.


## 재전파 방지 규칙

이 스펙은 목적지 노드까지 한 번만 보내는 방식이므로 다음을 강제한다.

- 원격 노드는 받은 직접 전달 메시지를 다른 노드로 다시 보내지 않는다
- `spot` 대상 메시지도 도착한 노드의 routed recv 경로에서만 처리하고 끝낸다
- 직접 전달 메시지를 자동으로 다시 직접 전달 메시지로 포장해 넘기지 않는다


## 혼동을 막기 위한 금지 사항

### 1. node 주소와 spot 주소를 같은 값으로 취급하지 않는다

`SpotNode routing_id` 와 `Spot routing_id` 는 서로 다른 계층의 주소다.
둘 다 기존 `zlink_set_routing_id()` / `zlink_get_routing_id()` 로 다루더라도
같은 의미의 주소로 합쳐서 해석하면 안 된다.

### 2. routed 메시지를 클러스터 전체 publish 처럼 다시 퍼뜨리지 않는다

이렇게 하면 직접 전달과 publish 의미가 섞이고 루프 위험도 생긴다.

### 3. routed 메시지를 기존 subscribe API 에 섞지 않는다

같은 메시지가
routed recv 경로와 SPOT subscriber 양쪽에 동시에 보이면
이중 전달 문제가 생긴다.


## 구현 범위

이 스펙의 구현에는 다음 항목이 모두 포함된다.

- `SpotNode` 안의 직접 전달 런타임
- 로컬 `route_ingress` 와 로컬 직접 수신 경로
- `SpotNode routing_id` 설정/조회
- `Spot routing_id` 설정/조회
- 원격 `node_router` 연결과 주소 조회
- `spot -> spot`, `spot -> router`, `router -> spot` 송신/수신
- 유효성 검사
- 모니터링과 상태 스냅샷 확장
- 바인딩 표면 확장


## 테스트 요구사항

최소 테스트 항목:

- 로컬 `spot -> spot` 송신/수신
- 원격 `spot -> spot` 송신/수신
- `spot -> router` 송신/수신
- `router -> spot` 송신/수신
- `spot -> spot request/reply`
- `spot -> router request/reply`
- `router -> spot request/reply`
- 같은 `Spot` 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있는지 확인
- reply 순서가 바뀌어도 `request_seq` 로 올바르게 매칭되는지 확인
- 첫 reply 이후의 extra reply 가 high-level request 완료를 다시 일으키지 않는지 확인
- 잘못된 `dest_spot_rid` 거부
- 존재하지 않는 `dest_spot_rid` request 는 timeout 이 아니라 `ENOENT` error reply 로 완료되어야 한다
- 잘못된 직접 전달 envelope 버리기
- 로컬 target `Spot` 부재가 send 의미를 바꾸지 않는지 확인
- 원격 목적지 미해결 시 실패
- 토픽 전달과 직접 전달 사이 상대 순서 비보장 확인
- 직접 전달 큐 포화 시 토픽 전달과의 분리 확인


## 구현 전 확정할 항목

구현 전에 이름과 표면을 다음처럼 확정해야 한다.

- 직접 전달 표면을 별도 facade 없이 `Spot` handle 위에 직접 둘지
- 직접 수신 API 에 peer transport 정보를 함께 보여줄지
- registry topology API 가 node `routing_id` 와 spot `routing_id` 를 어떻게 노출할지
- status/peer snapshot 에 routed-message 상태를 어떤 필드 이름으로 노출할지


## 결론

SPOT 의 직접 전달 기능은
"기존 토픽 발행/구독에 이것저것 얹어서 하나로 합치는 기능"이 아니라,
`SpotNode` 위에 따로 두는 직접 전달 경로로 설계하는 것이 맞다.

이렇게 해야 다음이 동시에 성립한다.

- 기존 SPOT publish/subscribe 의미를 보존할 수 있다
- 같은 프로세스 안에서는 `inproc` 으로 빠르게 전달할 수 있다
- 원격 노드와는 `ROUTER` 소켓으로 직접 통신할 수 있다
- `spot -> spot`, `spot -> router`, `router -> spot` 을 모두 지원할 수 있다
- 주소 의미 충돌과 재전파 루프를 막을 수 있다

---

## 현재 변경 방향

위 본문은 SPOT 직접 전달 기능의 전체 설계 방향을 설명하는 기준 문서로 유지한다.

다만 현재 기준에서는 source/destination 주소나
request/reply 같은 상위 의미를
`zlink_msg_t` 내부 필드나 message header 확장으로 올리지 않는다.

정리:

- SPOT 직접 전달의 source/destination 정보는
  `ZMP` transport 위의 SPOT routed protocol envelope 에 둔다
- `spot -> router` 수신자는 기존 `ROUTER` recv 표면에서
  이 protocol envelope 를 읽어 해석한다
- transport `routing_id` 와 application-level source/destination 주소는
  서로 다른 계층으로 구분한다
- 공통 전송 형식은
  [`ZMP_PROTOCOL_OVERVIEW.md`](ZMP_PROTOCOL_OVERVIEW.md) 를 따른다

지원 방향:

- `spot -> spot`, `spot -> router`, `router -> spot` 은
  모두 같은 SPOT routed protocol envelope 체계 안에서 정의한다
- 이 envelope 는 최소한 protocol id, version,
  source class, source node rid, source spot rid,
  destination class, destination node rid,
  destination spot rid 또는 destination router rid 를 포함해야 한다
- generic `ROUTER` 는 기존 recv 표면으로 메시지를 받은 뒤
  routed protocol envelope 를 읽어 source node/spot return address 를 얻는다
- `SpotNode` 는 같은 envelope 규칙으로 local handoff 와 remote handoff 를
  일관되게 처리해야 한다

즉 이 문서는 계속 유효한 설계 문서로 두되,
구현 시에는 message-level 확장보다
protocol-level envelope 방향을 우선 기준으로 해석한다.

실제 envelope 형식 기준은
[`ZMP_SPOT_ROUTED_PROTOCOL.md`](ZMP_SPOT_ROUTED_PROTOCOL.md)
를 따른다.
