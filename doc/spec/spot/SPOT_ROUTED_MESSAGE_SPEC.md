# Spot Routed Message Spec

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
- 공개 API 초안
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


## 용어

| 용어 | 설명 |
|------|------|
| `node routing_id` | `SpotNode` 의 대표 주소 |
| `node router` | 다른 노드와 직접 메시지를 주고받는 내부 `ROUTER` 소켓 |
| `route ingress` | 로컬 애플리케이션이 node 런타임으로 직접 메시지를 넣는 내부 입력 지점 |
| `route receiver` | 직접 전달 메시지를 받는 로컬 수신자 |
| `spot routing_id` | 같은 `SpotNode` 안의 개별 `Spot` 주소 |
| `router rid` | 일반 `ROUTER` peer 식별자 |
| `source kind` | 수신 메시지의 송신자 종류. `spot` 또는 `router` |
| `destination class` | 목적지 종류. `spot` 또는 `router` |
| `dispatcher event` | payload 를 직접 넘기지 않고 "지금 읽을 수 있음"만 알리는 이벤트 |


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


## 공개 API 초안

여기 적는 API 이름은 스펙 초안이다.
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
- `SpotRoute`: `Spot` 에 귀속된 직접 전달 인터페이스다

### 생성

```c
void *zlink_spot_route_of(void *spot);
```

해석 규칙:

- `zlink_spot_route_of(spot)` 는 해당 `Spot` 과 연결된 borrowed route handle 을 돌려준다
- `SpotNode` 는 내부적으로 routed router send/recv 를 수행하지만,
  node 수준의 public routed send/recv 표면은 노출하지 않는다
- routed public surface 는 `Spot` 에 연결된 handle 로만 연다
- dispatcher 모델에서는 `Spot` handle 만 받아도
  `zlink_spot_route_of(spot)` 로 route recv 표면에 접근할 수 있어야 한다
- 같은 `Spot` 에 대해 얻는 route handle 은 모두 같은 routed recv 경로를 함께 쓴다
- 즉 같은 `Spot` 에 연결된 routed recv queue, callback mode, dispatcher mode 는
  모든 route handle 사이에서 같이 공유된다
- 따라서 한 route handle 에서 `recv` 를 소비하면
  다른 route handle 에서 같은 메시지를 다시 볼 수 없다
- `EBUSY` 같은 수신 모델 충돌 규칙도
  같은 `Spot` 의 route handle 전체에 적용한다

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

다만 실제 공개 API 는
송신 방향이 드러나는 함수들로 나누는 편이 더 안전하다.

권장 형태:

```c
int zlink_spot_route_send_router(void *route,
                                 const zlink_routing_id_t *peer_rid,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 zlink_send_flags_t flags);

int zlink_spot_route_send_spot(void *route,
                               const zlink_routing_id_t *dest_node_rid,
                               const zlink_routing_id_t *dest_spot_rid,
                               zlink_msg_t *parts,
                               size_t part_count,
                               zlink_send_flags_t flags);

int zlink_router_send_spot(void *router,
                           const zlink_routing_id_t *dest_node_rid,
                           const zlink_routing_id_t *dest_spot_rid,
                           zlink_msg_t *parts,
                           size_t part_count,
                           zlink_send_flags_t flags);
```

이유:

- 사용자가 `spot -> router`, `spot -> spot`, `router -> spot` 을
  함수 이름만 보고 바로 구분할 수 있다
- 바인딩에서 더 읽기 쉬운 이름으로 그대로 노출하기 쉽다
- 잘못된 조합을 API 표면에서 줄일 수 있다

즉 이 문서의 `zlink_spot_route_send(... kind ...)` 는
실제 public API 는 위 세 함수로 분리하는 쪽을 우선한다

추가 규칙:

- `send_spot()` 는 `dest_node_rid` 와 `dest_spot_rid` 를 함께 받는다
- `send_router()` 는 일반 `ROUTER` peer `routing_id` 만 받는다
- `router_send_spot()` 는 일반 `ROUTER` 에서 `SpotNode` routed 경로로 보내는 함수다

### 직접 수신

```c
typedef enum zlink_spot_route_source_kind_t {
    ZLINK_SPOT_ROUTE_SOURCE_SPOT = 1,
    ZLINK_SPOT_ROUTE_SOURCE_ROUTER = 2
} zlink_spot_route_source_kind_t;

int zlink_spot_route_recv(void *route,
                          zlink_spot_route_source_kind_t *source_kind_out,
                          zlink_routing_id_t *source_primary_rid_out,
                          zlink_routing_id_t *source_secondary_rid_out,
                          zlink_msg_t **parts_out,
                          size_t *part_count_out,
                          int flags);
```

기본 규칙은 다음과 같다.

- routed 메시지만 이 recv 표면으로 돌려준다
- 기존 SPOT subscribe 메시지는 이 recv 표면으로 노출하지 않는다
- `source_kind=spot` 이면 `source_primary_rid_out=node_rid`,
  `source_secondary_rid_out=spot_rid` 로 해석한다
- `source_kind=router` 이면 `source_primary_rid_out=router_rid`,
  `source_secondary_rid_out` 는 비운다
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
typedef void (*zlink_spot_route_handler_fn)(
    zlink_spot_route_source_kind_t source_kind,
    const zlink_routing_id_t *source_primary_rid,
    const zlink_routing_id_t *source_secondary_rid,
    zlink_msg_t *parts,
    size_t part_count,
    void *userdata);

int zlink_spot_route_handler(void *route,
                             zlink_spot_route_handler_fn handler,
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
- routed callback 을 등록하면 `zlink_spot_route_recv()` 는 막힌다
- `zlink_spot_route_recv()` 를 사용하는 동안 routed callback 등록은 막힌다

이유:

- 같은 메시지가 `recv` 와 callback 양쪽으로 동시에 보이면 이중 소비 위험이 생긴다
- 어느 쪽이 먼저 메시지를 가져가는지 모호해진다
- 바인딩과 샘플에서 동작 설명이 복잡해진다

권장 동작:

- 충돌하는 수신 모델을 등록하거나 호출하면 `EBUSY` 또는 `EOPNOTSUPP` 계열로 실패시킨다
- 한 표면에서는 하나의 수신 모델만 활성화한다

여기서 말하는 `callback` 은
메시지 payload 를 직접 전달하는 delivery callback 을 뜻한다.

`dispatcher event callback` 은 delivery callback 이 아니다.
따라서 dispatcher event 모델은
같은 표면의 `recv` 와 함께 사용하는 것을 허용한다.


## Dispatcher 기반 단일 스레드 처리

### 왜 이 모델이 필요한가

기존 subscribe callback 과 routed callback 을
각각 별도 callback thread 에서 실행하면
같은 공용 객체를 접근할 때 동기화 부담이 커질 수 있다.

하지만 `SpotNode` 가 수천 개까지 늘어날 수 있으므로
`SpotNode` 마다 전용 스레드를 두는 방식은 적합하지 않다.

이 스펙에서 권장하는 모델은 다음과 같다.

- dispatcher 가 여러 `Spot` 과 `SpotTimer` 를 감시한다
- dispatcher event callback 은 "이 spot 또는 timer 에 읽을 것이 있다"는 사실만 알려준다
- 사용자는 그 callback 안에서 `zlink_subscribe()`,
  `zlink_spot_route_recv()`, `zlink_spot_timer_recv()` 를 호출한다
- 공용 상태 변경은 그 dispatcher thread 안에서만 수행한다

즉 권장 수신 모델은
"callback 직접 처리"보다
"dispatcher event + recv" 쪽이다.

### 권장 원칙

- dispatcher event callback 은 메시지 payload 자체를 직접 넘기지 않는다
- dispatcher event callback 은 "지금 읽을 것이 있다"는 사실만 알려준다
- 실제 메시지 소비는 사용자가 같은 dispatcher thread 에서
  `recv` 또는 `zlink_subscribe()` 로 수행한다
- dispatcher callback 은 하나의 thread 에서만 호출되도록 보장하는 것을 권장한다

이렇게 하면
공용 객체 접근을 단일 스레드로 몰아
동기화 문제를 단순하게 만들 수 있다.

### dispatcher 이벤트 종류

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

### dispatcher 표면 초안

이 스펙은 `recv`, 직접 delivery `callback`, `dispatcher` 세 public 모델을 모두 허용한다.
그중 공용 상태를 한 thread 에서 처리해야 하는 애플리케이션에는
`Spot` 단위 dispatcher helper 를 권장한다.

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
사용자에게 권장하는 상위 표면으로는 `dispatcher event callback` 모델을 둔다.

예시:

```c
void *zlink_spot_dispatcher_new(void);
int zlink_spot_dispatcher_destroy(void **dispatcher_p);

int zlink_spot_dispatcher_add_spot(void *dispatcher,
                                   void *spot,
                                   void *userdata);
int zlink_spot_dispatcher_remove_spot(void *dispatcher,
                                      void *spot);

int zlink_spot_dispatcher_add_timer(void *dispatcher,
                                    void *timer,
                                    void *userdata);
int zlink_spot_dispatcher_remove_timer(void *dispatcher,
                                       void *timer);

typedef void (*zlink_spot_dispatcher_handler_fn)(
    void *handle,
    zlink_spot_dispatch_event_t event,
    void *userdata);

int zlink_spot_dispatcher_handler(void *dispatcher,
                                  zlink_spot_dispatcher_handler_fn handler,
                                  void *userdata);
```

기본 방향:

- dispatcher 등록 단위는 `Spot` 과 `SpotTimer` 다
- callback 의 `handle` 은 읽을 것이 생긴 `Spot` 또는 `SpotTimer` 다
- 사용자는 callback 안에서 해당 handle 에 맞는 recv 함수를 직접 호출한다
- 내부 구현은 필요하면 `zlink_poller_*` 를 사용할 수 있다
- 하지만 공개 계약은 `dispatcher event callback + recv` 모델로 설명한다

`Spot` 에 대한 `ROUTE_READABLE` 이벤트는
같은 `Spot` 에 연결된 route handle 에 읽을 routed 메시지가 생겼다는 뜻이다.
이 경우 사용자는 아래처럼 처리한다.

- callback 의 `handle` 을 `Spot` 으로 해석한다
- `zlink_spot_route_of(spot)` 로 borrowed route handle 을 얻는다
- 그 route handle 에 대해 `zlink_spot_route_recv()` 를 호출한다

dispatcher event 모델도
같은 수신 표면의 직접 callback delivery 와는 섞지 않는 것을 권장한다.

예:

- subscribe callback delivery 를 쓰는 `Spot` 에서는
  `SUB_READABLE` 기반 소비를 함께 쓰지 않는다
- routed callback delivery 를 쓰는 `Spot` 에서는
  `ROUTE_READABLE` 기반 `recv` 소비를 함께 쓰지 않는다
- timer callback delivery 를 쓰는 `SpotTimer` 에서는
  `TIMER_READABLE` 기반 `zlink_spot_timer_recv()` 소비를 함께 쓰지 않는다

즉 dispatcher 모델을 선택했다면
그 표면에서는 event 알림 + `recv` 조합으로 일관되게 사용하는 편이 가장 단순하다.

### dispatcher callback 안에서의 처리 방식

권장 처리 방식은 다음과 같다.

- `SUB_READABLE` 이면 해당 `Spot` 에 대해 `zlink_subscribe()` 를 호출한다
- `ROUTE_READABLE` 이면 해당 `Spot` 에 연결된 route 표면에 대해
  `zlink_spot_route_recv()` 를 호출한다
- `TIMER_READABLE` 이면 해당 `SpotTimer` 에 대해
  `zlink_spot_timer_recv()` 를 호출한다
- 각 callback 안에서 가능한 만큼 recv 하거나,
  dispatcher 정책에 맞춰 한 개씩 읽는다

예시 흐름:

```text
dispatcher event callback
-> single-thread dispatcher loop
-> user calls subscribe recv or route recv or timer recv
-> user updates shared object on the same thread
```

짧은 사용 예시는 아래와 같다.

```c
zlink_spot_dispatcher_add_spot(dispatcher, spot, userdata);
zlink_spot_dispatcher_add_timer(dispatcher, timer, userdata);
zlink_spot_dispatcher_handler(dispatcher, on_spot_event, userdata);

// on_spot_event(handle, event, userdata)
//   SUB_READABLE    -> zlink_subscribe(spot, ...)
//   ROUTE_READABLE  -> zlink_spot_route_recv(zlink_spot_route_of(spot), ...)
//   TIMER_READABLE  -> zlink_spot_timer_recv(timer, ...)
```

### 이 모델의 장점

- `SpotNode` 수가 많아도 thread-per-spot 이 필요 없다
- subscribe, routed recv, timer recv 를 같은 이벤트 루프에서 처리할 수 있다
- 직접 delivery callback 을 쓰지 않으면
  실제 상태 변경을 한 스레드에서 수행하기 쉬워진다
- 사용자는 `Spot` 단위로 dispatcher 에 등록해서 편하게 사용할 수 있다
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

- dispatcher 가 여러 `Spot` 과 `SpotTimer` 를 감시한다
- dispatcher event callback 은 readable 이벤트만 알린다
- 실제 메시지는 `zlink_subscribe()`, `zlink_spot_route_recv()`,
  `zlink_spot_timer_recv()` 로 읽는다
- 상태 변경은 dispatcher 의 단일 스레드에서 처리한다

### 주의사항

- 이 dispatcher 기반 모델은
  기존 subscribe callback, routed callback, timer callback 을
  대체하는 강제 모델은 아니다
- 다만 공용 상태를 안전하게 다뤄야 하는 애플리케이션에는
  callback 직접 처리보다 이 dispatcher 기반 모델을 우선 권장한다
- sample 과 문서에서는
  `recv` 모델, `callback` 모델, `dispatcher event + recv` 모델을
  서로 구분해서 설명하는 것이 좋다


## Timer 통합 방향

### 기본 입장

`Spot` 은 raw socket 보다 복합적인 서비스 라이브러리 성격이 강하므로,
dispatcher 와 함께 동작하는 timer 기능을 제공하는 것은 자연스럽다.

즉 timer 자체는 이 스펙의 방향과 어긋나지 않는다.

다만 timer 문제의 핵심은
timer 기능 그 자체보다
binding 경계에서 callback 을 얼마나 자주 넘느냐에 있다.

### timer 표면 초안

timer 는 `Spot` 에 연결되고,
dispatcher 에서는 `SpotTimer` 단위 이벤트로 노출한다.

여기서 말하는 timer 는 기존 generic `zlink_timers_*` 를 대체하는 것이 아니다.
역할이 다르다.

- `zlink_timers_*`: socket/spot 바깥에서도 쓸 수 있는 범용 timer set
- `SpotTimer`: `Spot` 수명주기와 dispatcher event 모델에 맞춘 spot 소유 timer

즉 public 의미는 spot 소유 service timer 이고,
내부 구현은 필요하면 기존 `zlink_timers_*` 를 재사용할 수 있다.

예시:

```c
void *zlink_spot_timer_new(void *spot);
int zlink_spot_timer_destroy(void **timer_p);
int zlink_spot_timer_start(void *timer,
                           uint64_t interval_ns,
                           int repeat);
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
- dispatcher 는 `TIMER_READABLE` 이벤트만 알려준다
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

timer 표면에서도 수신 모델 충돌 규칙은 동일하게 적용하는 편이 좋다.

- timer callback delivery 를 쓰면 `zlink_spot_timer_recv()` 같은 pull 모델은 함께 쓰지 않는다
- `zlink_spot_timer_recv()` 기반으로 쓰면 timer callback delivery 는 등록하지 않는다

### 권장 문구

정리하면 timer 지원 정책은 아래처럼 이해하면 된다.

- `Spot` 은 dispatcher 와 함께 쓰는 timer 기능을 제공할 수 있다
- C/C++ 에서는 상대적으로 자유롭게 사용할 수 있다
- managed bindings 에서는 `8ms ~ 33ms` 정도를 일반 권장 범위로 본다
- 그보다 더 짧은 주기는 비금지이지만, workload 기준으로 충분한 측정과 검증이 필요하다


## 왜 `SpotRoute` 를 따로 두는가

직접 전달 API 를 기존 `Spot` 에 섞지 않는 이유는 다음과 같다.

- 기존 `publish/subscribe` 개념을 그대로 유지하기 쉽다
- 수신 모델이 헷갈리지 않는다
- 토픽 수신, 직접 수신, timer 수신을 dispatcher 표면에서 분리할 수 있다
- `routing_id` 의미 충돌을 막을 수 있다
- 문서와 바인딩 표면이 단순해진다

정리하면:

- `Spot` 은 토픽 메시지용
- `SpotRoute` 는 `Spot` 에 귀속된 직접 전달용
- `SpotTimer` 는 `Spot` 에 붙는 timer 용


## 공개 send API 의미

공개 routed send API 는 아래 세 개를 둔다.

- `zlink_spot_route_send_router()`
- `zlink_spot_route_send_spot()`
- `zlink_router_send_spot()`

이 함수들은 이름이 비슷해 보여도 목적지가 다르다.

### `zlink_spot_route_send_router()`

이 함수는
이미 연결된 `ROUTER` peer 의 `routing_id` 를 대상으로 보내는 함수다.

특징:

- 목적지는 `peer_rid` 다
- discovery 나 registry 로 목적지를 해석하지 않는다
- 이미 알고 있는 `ROUTER` peer identity 로 바로 보낸다
- SPOT publish/subscribe 의미를 갖지 않는다
- 받는 쪽 generic `ROUTER` 는 기존 `zlink_recv()` 또는 그와 같은 raw ROUTER recv 표면으로 받는다
- 이 경우 routed-message 여부는 body 의 `frame0` 프로토콜 식별자로 구분한다
- payload ownership 규칙은 기존 raw `ROUTER` recv 규칙을 그대로 따른다
- 즉 `spot -> router` 는 generic `ROUTER` 쪽에 새 전용 recv API 를 추가하지 않아도,
  기존 raw `ROUTER` recv 위에서 프로토콜 식별자로 구분할 수 있어야 한다

### `zlink_spot_route_send_spot()`

이 함수는
다른 `SpotNode` 의 routed router endpoint 로 보내는 함수다.

특징:

- 목적지는 `dest_node_rid` 와 `dest_spot_rid` 다
- 로컬 노드 디렉터리 또는 discovery/registry 로 목적지를 해석한다
- 도착한 메시지는 대상 `SpotNode` 안의 target `Spot` routed recv 표면으로 들어간다
- 기존 subscribe 경로로 들어가지 않는다

### `zlink_router_send_spot()`

이 함수는
일반 `ROUTER` 소켓에서 `SpotNode` 의 routed endpoint 로 보내는 함수다.

특징:

- 목적지는 `dest_node_rid` 와 `dest_spot_rid` 다
- 일반 `ROUTER` peer 가 `SpotNode` 안의 target `Spot` routed recv 경로로 메시지를 보낼 때 쓴다
- 수신한 `SpotNode` 는 target `Spot` routed recv 표면에서
  이 메시지를 `source_kind=router` 로 구분해서 돌려준다


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

이 스펙의 `SpotRoute` send 는
일반 `ROUTER` 소켓의 peer-directed send 와는 다른 기능이다.

차이:

- `send_spot` 은 `SpotNode routing_id + Spot routing_id` 를 목적지로 사용한다
- `send_router` 는 transport peer `routing_id` 를 사용한다
- `send_spot` 은 discovery/registry 또는 로컬 노드 디렉터리로 목적지를 해석한다
- `send_router` 는 이미 연결된 peer identity 를 직접 사용한다

예시:

```c
int zlink_spot_route_send_router(void *route,
                                 const zlink_routing_id_t *peer_rid,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 zlink_send_flags_t flags);

int zlink_spot_route_send_spot(void *route,
                               const zlink_routing_id_t *dest_node_rid,
                               const zlink_routing_id_t *dest_spot_rid,
                               zlink_msg_t *parts,
                               size_t part_count,
                               zlink_send_flags_t flags);

int zlink_router_send_spot(void *router,
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

### 기본 원칙

원격 직접 전달은 `ROUTER` 소켓끼리 multipart 메시지로 주고받는다.
다만 아래 프레임 정의는
`ROUTER` transport 가 내부적으로 사용하는 peer addressing envelope 를
벗긴 뒤의 "프로토콜 payload" 를 설명한다.

즉:

- 실제 wire 에는 `ROUTER` 송신 대상 식별용 envelope 가 앞에 붙을 수 있다
- 그 envelope 형식과 peer 선택은 `node_router` 구현이 내부적으로 관리한다
- 아래 `frame0..N` 규칙은 envelope 뒤에 오는 routed-message protocol body 에 적용한다

전송 형식:

```text
frame0 = 직접 전달 프로토콜 식별자
frame1 = 버전
frame2 = source class
frame3 = source node rid or empty
frame4 = source spot rid or source router rid
frame5 = destination class
frame6 = destination node rid or empty
frame7 = destination spot rid or destination router rid
frame8..N = 실제 payload part 들
```

방향별 해석은 아래와 같다.

### `spot -> spot`

```text
frame2 = "spot"
frame3 = source node rid
frame4 = source spot rid
frame5 = "spot"
frame6 = dest node rid
frame7 = dest spot rid
```

### `spot -> router`

```text
frame2 = "spot"
frame3 = source node rid
frame4 = source spot rid
frame5 = "router"
frame6 = empty
frame7 = dest router rid
```

### `router -> spot`

```text
frame2 = "router"
frame3 = empty
frame4 = empty or optional debug router rid
frame5 = "spot"
frame6 = dest node rid
frame7 = dest spot rid
```

### 프로토콜 식별자

토픽 메시지와 섞이지 않도록 별도의 식별자를 사용한다.

요구사항:

- `__zlink.spot.ctrl.*` 와 관계없어야 한다
- 토픽 payload 와 혼동되면 안 된다
- 일반 `ROUTER` 데이터와 구분할 수 있어야 한다

예시:

```text
\x00zlink.spot.route.v1\x00
```

### source class 와 destination class 값

`source class` 와 `destination class` 에는 다음 둘 중 하나를 넣는다.

- `"spot"`
- `"router"`

읽기 쉬운 문자열을 사용한다.

### routed endpoint 프레임 규칙

- `spot -> spot` 또는 `router -> spot` 메시지에는 `dest_spot_rid` 가 들어가야 한다
- 일반 `router` peer 대상 메시지에는 `dest_spot_rid` 가 없다
- 비어 있는 routed endpoint 식별자는 허용하지 않는다

### 보낸 쪽 주소 검증

받는 쪽은 transport 가 제공한 실제 peer identity 를
source of truth 로 사용해야 한다.

규칙:

- `spot -> spot` 에서는 source node/spot 정보가
  protocol body 와 peer metadata 규칙을 함께 만족해야 한다
- `router -> spot` 에서 `source_router_rid` 는 transport peer identity 에서 얻는다
- `router -> spot` body 안의 source router rid 값은
  보내는 쪽이 자기 자신이라고 주장한 값으로 보고 신뢰하지 않는다
- body 에 진단용 source rid 를 실었다면,
  실제 transport peer identity 와 다를 때 그 값은 버린다
- 최소한 잘못된 형식의 프레임은 반드시 버려야 한다

`spot -> spot` 에 대한 구체 규칙은 아래와 같다.

- transport peer 는 먼저 어느 원격 `SpotNode` 인지 식별할 수 있어야 한다
- body 의 `source node rid` 는 그 peer 가 광고한 node `routing_id` 와 같아야 한다
- body 의 `source spot rid` 는 그 node 가 광고한 spot 목록 안에 있어야 한다
- peer 종류가 `spot` 목적지를 해석할 수 없는 peer 로 보이면 메시지를 버린다
- body 의 `source node rid` 가 peer metadata 와 다르면 메시지를 버린다
- body 의 `source spot rid` 가 그 node 에 없는 값이면 메시지를 버린다
- 위 조건 중 하나라도 어기면 오류 카운터를 올리고 메시지를 버린다


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

### 수신 프레임 검사

받는 쪽은 다음과 같은 잘못된 메시지를 버려야 한다.

- frame 개수가 부족한 경우
- 프로토콜 식별자 또는 버전이 맞지 않는 경우
- source 또는 destination 식별자가 비어 있는 경우
- source kind 또는 destination class 가 맞지 않는 경우
- `spot` 목적지인데 `dest_spot_rid` 가 비어 있는 경우
- `router` 목적지인데 spot 전용 프레임 구성이 섞인 경우

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
- 잘못된 `dest_spot_rid` 거부
- 잘못된 직접 전달 프레임 버리기
- 로컬 target `Spot` 부재가 send 의미를 바꾸지 않는지 확인
- 원격 목적지 미해결 시 실패
- 토픽 전달과 직접 전달 사이 상대 순서 비보장 확인
- 직접 전달 큐 포화 시 토픽 전달과의 분리 확인


## 구현 전 확정할 항목

구현 전에 이름과 표면을 다음처럼 확정해야 한다.

- 공개 인터페이스 이름을 `SpotRoute` 로 할지 `SpotRouter` 로 할지
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
