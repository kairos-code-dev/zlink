# SPOT route channel bridge core 전환 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현·회귀 테스트·성능 검증이
> 끝난 뒤에만 정식 spec, guide, internals 문서에 나누어 반영한다.
>
> 구현 전 단계에서는 이 문서의 API 이름과 세부 시그니처를 `core/doc/spec/core/` 또는
> `framework/doc/`의 정식 spec에 계약처럼 쓰지 않는다.

## 목표

SPOT이 외부 channel을 통해 특정 `Spot`으로 메시지를 보내는 기능을 core 라이브러리의
공개 기능으로 정리한다.

핵심 목표는 세 가지다.

1. `SpotNode`가 client/server channel의 `DEALER`나 route mesh channel의 `ROUTER`를 직접
   가져오지 않는다.
2. channel socket 소유권은 channel runtime 또는 호출자가 만든 일반 socket에 남긴다.
3. core가 SPOT route relay packet, request/reply, error, lifecycle, monitoring 계약을
   한 번 정의하고 모든 bindings와 framework가 같은 의미를 투영한다.

이 계획에서 새로 둘 중심 개념은 **SPOT route channel bridge**다. bridge는 channel socket과
`SpotNode` 사이에 놓이는 core 객체다. `SpotNode`는 SPOT routed plane을 유지하고, channel
socket은 channel transport를 유지한다. bridge가 두 쪽의 메시지 형식과 lifecycle을 연결한다.

topic publish는 route bridge에 넣지 않는다. 외부 코드가 local `SpotNode`의 topic plane으로
publish해야 하면 raw `PUB` socket attach 대신 `SpotNode` publisher handle을 연다. 반대로
`Spot` 코드가 외부 pub/sub channel로 publish해야 하면 framework가 기존 channel publisher
client를 주입해서 사용한다.

## 비목표

- SPOT pub/sub topic plane의 내부 fan-out 의미를 다시 설계하지 않는다.
- 외부 pub/sub channel과 SPOT topic plane을 자동으로 연결하는 gateway를 만들지 않는다.
- `ROUTER`와 `DEALER` socket 자체의 일반 send/recv 계약을 바꾸지 않는다.
- framework serializer, DI, handler discovery 정책을 core로 내리지 않는다.
- route mesh channel을 물리적으로 client/server socket 쌍으로 바꾸지 않는다. `ROUTER`
  socket 특성은 그대로 둔다.
- 기존 언어별 framework의 typed API 이름을 이 문서에서 최종 확정하지 않는다. core 계약이
  먼저 확정된 뒤 각 언어 문서에서 idiom에 맞게 정한다.

## 관계 문서

이 문서는 아래 draft와 겹치거나 인접한다. 구현자는 먼저 관계를 확인하고 서로 모순되는
계약을 동시에 구현하지 않는다.

- `core/doc/spec/draft/spot-channel-router-attach.ko.md`
  - 관계: supersede.
  - 기존 draft는 `SpotNode`가 route mesh `ROUTER`를 직접 attach하는 안을 폐기했다. 폐기
    사유는 `ROUTER`가 request를 받을 수 있어 SPOT routed plane과 route mesh channel 의미가
    섞인다는 점이었다.
  - 이 문서는 그 결정을 되돌리지 않는다. `SpotNode` 직접 attach는 계속 제거 대상이다. 대신
    channel socket을 channel runtime이 소유하고, bridge가 caller-owned receive handoff와
    `SPOT_ROUTE` policy로 packet을 분류한다.
- `core/doc/spec/draft/spot-publish-data-plane-ingress.ko.md`
  - 관계: 통합.
  - local topic publish는 route bridge가 아니라 `SpotNode` publisher handle로 분리한다. 이
    문서의 `zlink_spot_node_publisher_*`는 해당 draft의 data-plane publish ingress 방향과
    충돌하지 않아야 한다.
- `core/doc/spec/draft/auto-connect-channel-types.ko.md`
  - 관계: 병행.
  - Discovery 자동 연결 타입과 socket role 검증은 이 문서의 bridge endpoint ownership 규칙과
    함께 유지한다. 이 문서는 Discovery auto-connect 모델을 다시 설계하지 않는다.
- `core/doc/spec/draft/framework-route-resolvers.ko.md`
  - 관계: 병행.
  - route resolver는 target `SpotNode`와 target `Spot`을 찾는 framework 책임이다. bridge는
    resolver가 넘긴 routing id를 사용한다.
- `core/doc/spec/draft/actor-spot-route-messaging.ko.md`
  - 관계: 병행.
  - Actor route messaging은 Actor 위치 조회와 Actor-to-Spot routing 의미를 다룬다. 이 문서는
    Actor 전용 C API를 추가하지 않는다.

## 출발점

현재 checkout에는 SPOT 외부 channel 경로가 여러 갈래로 흩어져 있다.

- client/server channel
  - 현재: `SpotNode`에 `DEALER`를 attach한다.
  - 문제: `SpotNode`가 외부 channel client socket을 들게 된다.
- router-capable channel
  - 현재: `SpotNode`의 `external_router`가 router-channel peer로 연결된다.
  - 문제: SPOT routed plane과 application route mesh channel 의미가 섞인다.
- framework route mesh egress
  - 현재: framework route mesh runtime의 `ROUTER`가 SPOT relay packet을 보낸다.
  - 문제: 언어별 framework가 relay 의미를 직접 구현한다.
- local topic publish ingress
  - 현재: `SpotNode`에 외부 `PUB`를 attach한다.
  - 문제: 외부 코드가 local `SpotNode` topic plane으로 publish하려고 raw socket을 넘긴다.

이 구조는 동작은 가능하지만 책임 경계가 흐리다. `SpotNode`는 SPOT routed/pubsub plane의
소유자여야 하고, client/server나 route mesh channel의 socket lifecycle까지 알 필요가 없다.
또한 SPOT route relay packet 의미가 framework마다 따로 구현되면 bindings와 framework의 동작이
서로 달라질 수 있다. topic publish ingress는 channel bridge가 아니라 `SpotNode` publisher
handle로 분리해서 다룬다.

## 설계 원칙

### 1. `SpotNode`는 SPOT plane만 소유한다

`SpotNode`는 SPOT node routing id, `Spot` lifecycle, SPOT routed request/reply, pub/sub
topic plane을 소유한다. 외부 channel socket은 소유하지 않는다. 외부 코드가 topic plane으로
publish해야 하면 raw socket이 아니라 publisher handle을 사용한다.

### 2. channel socket은 channel 쪽이 소유한다

client/server channel의 `DEALER`와 route mesh channel의 `ROUTER`는 channel runtime 또는
일반 socket 사용자가 소유한다. bridge는 빌려 쓴다. bridge destroy가 socket을 닫지 않는다.

### 3. core가 relay packet 계약을 정의한다

SPOT route relay packet의 frame 순서, metadata, request sequence, reply, error reply는
core C API와 core 테스트가 정의한다. framework는 이 packet을 직접 만들지 않고 core bridge
API를 호출한다.

### 4. inbound는 소켓 종류가 아니라 policy로 판단한다

`ROUTER`는 물리적으로 양방향이다. 따라서 "client socket에는 메시지가 들어오면 안 된다"를
socket이 막게 만들 수 없다. bridge는 inbound packet을 받은 뒤, 해당 bridge의 policy가 그
packet을 받을 수 있는지 판단한다.

### 5. request는 timeout보다 명시적 error를 우선한다

허용되지 않은 inbound request를 조용히 버리면 호출자는 timeout만 본다. bridge는 가능하면
error reply를 돌려준다. reply를 보낼 수 없는 command는 rate-limited warning과 drop으로
처리한다.

## 대상 구조

```text
+--------------------+        +--------------------+
| Channel runtime    |        | SpotNode           |
|                    |        |                    |
| DEALER or ROUTER   |<------>| Spot route bridge  |
| owned by channel   |        | borrowed endpoint  |
+--------------------+        +--------------------+
                                      |
                                      v
                               +-------------+
                               | Spot routed |
                               | plane       |
                               +-------------+
```

다이어그램의 `Spot route bridge`는 socket을 소유하지 않는다. bridge는 channel socket에서
받은 relay packet을 `SpotNode`의 routed plane으로 넘기고, `SpotNode`에서 나온 reply를 다시
channel socket으로 보낸다.

## core 공개 계약 초안

### 객체

새 core 객체 이름은 구현 단계에서 한 번 더 검토한다. 이 계획에서는 설명을 위해
`zlink_spot_route_bridge_t`라고 부른다.

| 객체 | 역할 | 소유권 |
|------|------|--------|
| `zlink_spot_route_bridge_t` | channel socket과 `SpotNode` 사이의 relay 계약을 실행한다 | bridge handle이 자기 상태만 소유한다 |
| `zlink_spot_route_bridge_endpoint_t` | bridge에 연결할 channel socket과 socket 역할을 설명한다 | caller가 socket을 계속 소유한다 |
| `zlink_spot_route_bridge_endpoint_options_t` | endpoint별 policy를 정한다 | endpoint attach 시 복사한다 |

### channel endpoint 종류

route bridge는 최소 두 channel endpoint를 지원한다.

| endpoint 종류 | socket | 방향 | 설명 |
|----------------|--------|------|------|
| client/server egress | `DEALER` | outbound 중심 | 외부 channel client가 target `Spot`으로 command/request를 보낸다 |
| route mesh egress | `ROUTER` | routed outbound | route mesh peer routing id를 지정해 target `SpotNode`로 relay한다 |
| route ingress | `ROUTER` | inbound | channel에서 들어온 SPOT relay packet을 local `SpotNode`로 넘긴다 |

하나의 bridge가 양방향을 모두 맡을지, egress bridge와 ingress bridge를 분리할지는 구현 전
세부 설계에서 비교한다. 기본 선택은 **하나의 bridge 객체에 endpoint와 policy를 여러 개
붙이는 모델**이다. 같은 channel socket에 대해 ingress와 egress 설정을 한 곳에서 검증할 수
있기 때문이다.

topic publish는 route bridge endpoint가 아니다. 외부 코드가 local `SpotNode`의 topic plane으로
publish해야 하면 별도 publisher handle을 연다. 이 handle은 raw `PUB` socket을 노출하지 않고
`publish(topic, parts, flags)` 같은 동작만 제공한다.

### C API 초안

아래 이름은 계획용 초안이다.

```c
typedef struct zlink_spot_route_bridge_options_t {
    uint32_t struct_size;
    int default_request_timeout_ms;
    int error_reply_policy;
    int receive_mode;
} zlink_spot_route_bridge_options_t;

typedef struct zlink_spot_route_bridge_endpoint_options_t {
    uint32_t struct_size;
    uint32_t capabilities;
    int inbound_relay_policy;
} zlink_spot_route_bridge_endpoint_options_t;

void *zlink_spot_route_bridge_new(
    void *ctx,
    void *spot_node,
    const zlink_spot_route_bridge_options_t *options);

int zlink_spot_route_bridge_attach_dealer_channel(
    void *bridge,
    const char *channel_name,
    void *dealer_socket,
    const zlink_spot_route_bridge_endpoint_options_t *options);

int zlink_spot_route_bridge_attach_router_channel(
    void *bridge,
    const char *channel_name,
    void *router_socket,
    const zlink_spot_route_bridge_endpoint_options_t *options);

int zlink_spot_route_bridge_set_target_node(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_node_rid);

int zlink_spot_route_bridge_send(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags);

int zlink_spot_route_bridge_request(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_spot_rid,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_reply_handler_fn callback,
    void *user_data,
    zlink_send_flags_t flags,
    uint32_t timeout_ms);

int zlink_spot_route_bridge_handle_router_received(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *source_node_rid,
    zlink_msg_t *parts,
    size_t part_count,
    bool *handled);

int zlink_spot_route_bridge_handle_dealer_received(
    void *bridge,
    const char *channel_name,
    zlink_msg_t *parts,
    size_t part_count,
    bool *handled);

int zlink_spot_route_bridge_close(void *bridge);

void *zlink_spot_node_publisher_new(void *spot_node);

int zlink_spot_node_publisher_publish(
    void *publisher,
    const char *topic,
    zlink_msg_t *parts,
    size_t part_count,
    zlink_send_flags_t flags);

int zlink_spot_node_publisher_close(void *publisher);
```

### C API 추가 목록

구현 시 추가할 C API는 아래로 고정한다. 이름은 구현 직전 한 번 더 검토할 수 있지만, 기능 수와
책임은 줄이거나 섞지 않는다.

| API | 역할 |
|-----|------|
| `zlink_spot_route_bridge_new` | `SpotNode`와 channel socket 사이의 route bridge handle 생성 |
| `zlink_spot_route_bridge_attach_dealer_channel` | client/server `DEALER` socket과 endpoint policy 등록 |
| `zlink_spot_route_bridge_attach_router_channel` | route mesh `ROUTER` socket과 endpoint policy 등록 |
| `zlink_spot_route_bridge_set_target_node` | channel별 target `SpotNode` routing id 설정 |
| `zlink_spot_route_bridge_send` | target `Spot`으로 command relay |
| `zlink_spot_route_bridge_request` | target `Spot`으로 request relay와 reply callback 등록 |
| `zlink_spot_route_bridge_handle_router_received` | caller가 받은 `ROUTER` multipart를 bridge 처리기로 넘김 |
| `zlink_spot_route_bridge_handle_dealer_received` | caller가 받은 `DEALER` multipart를 bridge 처리기로 넘김 |
| `zlink_spot_route_bridge_close` | bridge handle 종료 |
| `zlink_spot_node_publisher_new` | local `SpotNode` topic plane으로 publish하는 handle 생성 |
| `zlink_spot_node_publisher_publish` | raw `PUB` socket 없이 local `SpotNode` topic plane으로 publish |
| `zlink_spot_node_publisher_close` | publisher handle 종료 |

### C API 삭제 또는 legacy 전환 목록

아래 API는 새 코드에서 사용하지 않는다. public symbol 제거는 다음 major 변경에서 결정할 수
있지만, 이번 계획의 구현 완료 시점에는 내부 직접 attach 로직을 제거해야 한다.

- `zlink_spot_node_attach_channel_dealer`
  - 대체: `zlink_spot_route_bridge_attach_dealer_channel`
  - 처리: deprecated로 전환하고 직접 attach 구현을 제거한다.
- `zlink_spot_node_attach_channel_dealer_manual`
  - 대체: `zlink_spot_route_bridge_attach_dealer_channel`
  - 처리: deprecated로 전환하고 직접 attach 구현을 제거한다.
- `zlink_spot_node_connect_router_channel_peer`
  - 대체: `zlink_spot_route_bridge_attach_router_channel`과
    `zlink_spot_route_bridge_set_target_node`
  - 처리: deprecated로 전환하고 peer connect 구현을 제거한다.
- `zlink_spot_node_connect_router_channel_peer_rid`
  - 대체: `zlink_spot_route_bridge_attach_router_channel`과
    `zlink_spot_route_bridge_set_target_node`
  - 처리: deprecated로 전환하고 peer connect 구현을 제거한다.
- `zlink_spot_node_disconnect_router_channel_peer`
  - 대체: bridge endpoint lifecycle close 또는 channel socket lifecycle
  - 처리: deprecated로 전환하고 direct disconnect 구현을 제거한다.
- `zlink_spot_node_disconnect_router_channel_peer_rid`
  - 대체: bridge endpoint lifecycle close 또는 channel socket lifecycle
  - 처리: deprecated로 전환하고 direct disconnect 구현을 제거한다.
- `zlink_spot_node_attach_router_channel_discovery`
  - 대체: channel/discovery가 router socket을 소유하고 bridge가 endpoint를 받는 구조
  - 처리: deprecated로 전환하고 direct discovery attach 구현을 제거한다.
- `zlink_spot_node_attach_pub_ingress`
  - 대체: `zlink_spot_node_publisher_new`와 `zlink_spot_node_publisher_publish`
  - 처리: deprecated로 전환하고 raw `PUB` attach 구현을 제거한다.

legacy API가 기존 의미를 유지하려면 `SpotNode`가 bridge handle이나 외부 socket state를 숨겨서
소유해야 하는 경우가 있다. 그런 API는 wrapper로 유지하지 않는다. deprecated 상태로
`ENOTSUP` 계열 migration error를 돌리고, caller가 새 bridge 또는 publisher handle을 만들도록
한다. `EINVAL`은 잘못된 socket 종류나 인자 오류에 사용하고, legacy migration error에는
사용하지 않는다.

`default_request_timeout_ms`는 `zlink_spot_route_bridge_request()`의 `timeout_ms`가 0일 때
사용한다. `default_request_timeout_ms`도 0이면 core의 기존 request timeout 기본값을 따른다.
호출자가 timeout을 명시하면 endpoint 또는 bridge 기본값보다 우선한다.

세부 구현에서 `zlink_msg_t *parts` 대신 기존 multipart helper 계약을 그대로 따를 수 있다.
중요한 점은 bridge가 packet 조립과 reply decode를 담당하고 caller가 relay frame을 직접
만들지 않는다는 것이다.

초기 public API는 **caller-owned receive handoff** 모델을 기준으로 한다. channel runtime이나
socket poller가 먼저 multipart를 받고, bridge에 `parts`와 `part_count`를 넘겨 SPOT relay packet인지
확인한다. bridge가 처리한 packet이면 `handled=true`를 돌려준다. bridge 대상이 아닌
application packet이면 `handled=false`를 돌려주며, caller가 같은 multipart를 기존
application dispatcher로 넘긴다. 이 모델은 bridge가 socket에서 직접 recv하지 않으므로 같은
packet을 상위 dispatcher가 다시 읽을 수 없는 문제를 만들지 않는다.

`handle_*_received` 함수의 반환값은 API 호출 자체의 성공과 실패를 나타낸다. 해당 multipart를
bridge가 처리했는지는 `handled` out parameter로만 판단한다.

ownership은 명확해야 한다. `handled=false`이면 caller가 모든 part 소유권을 그대로 유지한다.
`handled=true`이면 bridge가 part를 consume하거나 close한다. 구현 전에는 이 규칙을 기존
multipart callback 계약과 맞춘다.

추가로 core가 socket receive pump를 직접 소유하는 모델을 도입할 수는 있다. 그 경우에는 별도
API로 분리하고, application packet을 caller callback으로 넘기는 계약을 함께 정의한다. 이
초안의 `handle_*_received` API와 socket-owner receive pump API를 같은 이름으로 섞지
않는다.

`zlink_spot_node_publisher_*`는 외부 pub/sub channel과 SPOT topic plane을 연결하지 않는다.
이 API는 같은 process 안의 framework runtime이나 application code가 local `SpotNode` topic
plane으로 publish할 수 있게 하는 얇은 handle이다. 기존 `attach_pub_ingress`처럼 caller가 raw
`PUB` socket을 만들고 `SpotNode`에 넘기는 방식은 새 코드에서 사용하지 않는다.

### 에러 계약

| 상황 | 반환 또는 reply | errno 후보 |
|------|----------------|------------|
| bridge가 닫힘 | 실패 | `ESHUTDOWN` |
| 등록되지 않은 channel | 실패 | `ENOENT` |
| socket 종류 불일치 | 실패 | `EINVAL` |
| target `Spot` route 없음 | error reply 또는 실패 | `EHOSTUNREACH` |
| target `SpotNode` route 없음 | error reply 또는 실패 | `ENETUNREACH` |
| receive capability 위반 | request는 error reply, command는 drop | `EPERM` |
| malformed relay packet | request는 error reply, command는 drop | `EPROTO` |
| backpressure | 실패 또는 retry 가능 상태 | `EAGAIN` |
| timeout | request callback 실패 | `ETIMEDOUT` |

request packet은 가능한 한 error reply를 돌려준다. command packet은 reply 경로가 없으므로
monitor event와 rate-limited log로 남긴다.

### receive capability policy

초기 policy는 두 capability만 둔다. 같은 `ROUTER` socket에는 application channel packet과
SPOT relay packet이 함께 들어올 수 있다. SPOT request를 보낼 수 있으면 reply도 받아야 하고,
ROUTER에서는 inbound SPOT relay packet 자체를 물리적으로 막을 수 없다. 따라서 SPOT routed
messaging의 inbound와 outbound를 capability로 나누지 않고 하나의 `SPOT_ROUTE`로 묶는다.
일반 channel packet을 application dispatcher로 넘길 수 있는지는 별도 capability로 둔다.

| capability | 의미 |
|------------|------|
| `ZLINK_SPOT_ROUTE_BRIDGE_CAP_SPOT_ROUTE` | bridge가 channel socket을 SPOT routed send/request/reply/relay 처리에 사용한다 |
| `ZLINK_SPOT_ROUTE_BRIDGE_CAP_CHANNEL_INBOUND` | SPOT relay가 아닌 application channel packet을 caller dispatcher로 계속 넘긴다 |

자주 쓰는 조합은 preset으로 제공할 수 있다.

- `ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_ONLY`
  - 포함 capability: `SPOT_ROUTE`
  - 의미: SPOT routed packet만 bridge가 처리한다.
- `ZLINK_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND`
  - 포함 capability: `SPOT_ROUTE`, `CHANNEL_INBOUND`
  - 의미: SPOT routed packet은 bridge가 처리하고 application channel packet은 dispatcher로 넘긴다.

`CHANNEL_INBOUND`가 꺼져 있고 SPOT relay가 아닌 application packet이 들어오면 caller-owned
receive handoff에서는 `handled=false`와 함께 policy violation counter를 증가시킨다. socket-owner
receive pump 모델에서는 같은 상황을 drop하거나 error callback으로 보고한다. 어떤 방식을 택하든
SPOT relay packet으로 보이는 malformed packet은 application dispatcher로 넘기지 않는다.

SPOT relay inbound request를 application 의미상 받지 않으려면 capability를 나누지 않고
`SPOT_ROUTE` 내부 하위 policy로 거부한다. 거부된 request에는 error reply를 돌리고, command는
drop과 counter로 처리한다. 이렇게 해야 outbound request의 reply 수신과 inbound relay 거부가
서로 충돌하지 않는다.

application route handler packet은 core bridge가 처리하지 않는다. framework route handler
dispatcher가 맡는다. bridge는 자기 packet prefix 또는 message kind가 아닌 packet을
`handled=false`로 돌려준다. caller는 socket에서 이미 받은 같은 multipart를 application
dispatcher로 넘겨야 한다. decode 자체가 실패한 malformed SPOT relay packet은
`handled=true`와 error 상태로 처리해 application handler로 넘어가지 않게 한다.

### monitoring

bridge는 최소한 아래 진단값을 제공한다.

| 항목 | 설명 |
|------|------|
| bridge state | `idle`, `running`, `closing`, `closed`, `error` |
| attached channels | channel name, socket kind, policy |
| pending requests | channel별 pending request 수 |
| rejected inbound | policy 위반으로 거부한 packet 수 |
| malformed inbound | relay decode 실패 수 |
| routed send failures | target route 없음, backpressure, timeout 집계 |

monitoring은 기존 `zlink_monitor_snapshot_t`에 억지로 섞지 않고, 별도 bridge summary 또는
service summary 확장으로 설계한다. 어떤 방식을 택할지는 core monitoring 문서와 맞춰 결정한다.

### thread safety

bridge public API는 기존 socket/Spot API와 같은 thread safety 기준을 따른다.

- bridge 생성·attach·close는 control path다.
- `send`와 `request`는 bridge 내부 pending request table을 사용한다.
- 기본 모델에서 bridge는 socket recv를 직접 호출하지 않는다.
- caller-owned receive handoff에서는 channel runtime이나 socket poller 하나만 해당 socket의
  recv owner가 된다.
- bridge를 socket-owner receive pump로 확장하는 경우에는 attach 시점에 socket receive owner를
  등록하고, 기존 직접 recv API가 같은 socket에서 호출되면 `EBUSY` 또는 configuration error로
  막아야 한다.
- receive owner 등록과 해제는 core socket 계층의 명시적 계약으로 추가한다. 단순히 문서상
  "섞으면 안 된다"라고 적는 것만으로는 완료로 보지 않는다.

## core 구현 계획

### 1단계: relay packet codec을 core로 이동

현재 framework에 있는 SPOT relay packet 생성·decode 로직을 core 내부 codec으로 옮긴다.
codec은 다음을 보장한다.

- packet kind: command, request, reply, error reply
- target `Spot` routing id
- source channel name
- deadline 또는 timeout
- payload multipart 보존
- malformed frame 검출

### 2단계: bridge runtime 추가

`core/src/runtime/services/spot/bridge/` 아래에 bridge runtime을 둔다.

예상 파일:

| 파일 | 역할 |
|------|------|
| `spot_route_bridge.hpp/.cpp` | bridge handle과 lifecycle |
| `spot_route_bridge_codec.hpp/.cpp` | relay packet encode/decode |
| `spot_route_bridge_request_table.hpp/.cpp` | request sequence와 pending callback |
| `spot_route_bridge_dispatch.hpp/.cpp` | inbound packet 처리 |
| `spot_route_bridge_monitor.hpp/.cpp` | summary와 counters |

### 3단계: `SpotNode` external channel attach 제거

기존 `SpotNode` 외부 channel attach API는 public symbol 호환성 때문에 바로 제거하지 않을 수
있다. 그러나 내부 구현 경로는 새 core 객체로 옮긴다. 완료 시점에는 `SpotNode` runtime이 외부
channel socket이나 외부 publisher socket을 직접 보관하지 않아야 한다.

legacy public API:

- `zlink_spot_node_attach_channel_dealer`
- `zlink_spot_node_attach_channel_dealer_manual`
- `zlink_spot_node_connect_router_channel_peer`
- `zlink_spot_node_connect_router_channel_peer_rid`
- `zlink_spot_node_disconnect_router_channel_peer`
- `zlink_spot_node_disconnect_router_channel_peer_rid`
- `zlink_spot_node_attach_router_channel_discovery`
- `zlink_spot_node_attach_pub_ingress`

제거 방향은 세 단계로 나눈다.

1. 새 bridge와 publisher handle을 구현한다.
2. framework, bindings sample, core sample이 legacy attach API를 더 이상 호출하지 않게 바꾼다.
3. legacy API implementation은 직접 attach 코드를 갖지 않는다. 새 bridge handle 수명을 안전하게
   보존할 수 있는 API만 얇은 wrapper로 남기고, 그렇지 않은 API는 `ENOTSUP` 계열 migration
   error를 돌린다. 다음 major 변경에서는 wrapper와 public symbol 제거 여부를 결정한다.

이 단계가 끝난 뒤에는 아래 코드가 남아 있으면 안 된다.

- `SpotNode` attachment state가 외부 `DEALER`, `ROUTER`, `PUB` socket pointer를 소유하는 코드
- `SpotNode` lifecycle이 외부 channel socket endpoint를 직접 connect/disconnect하는 코드
- framework가 SPOT relay packet을 직접 encode/decode하는 코드
- framework나 sample이 `attach_pub_ingress`로 raw `PUB` socket을 `SpotNode`에 넘기는 코드

### 제거할 내부 로직 inventory

구현자는 아래 영역을 직접 확인하고 제거 또는 새 객체 호출로 교체한다.

- core `service_spot_node_api.cpp`
  - `attach_channel_dealer*`, router channel peer, `attach_router_channel_discovery`,
    `attach_pub_ingress`의 직접 attach/connect 구현
- core `spot_node_lifecycle.cpp`
  - `attach_channel_dealer*`, `attach_pub_ingress`가 외부 socket을 `SpotNode` attachment state에
    넣는 로직
- core `spot_node_access.*`
  - legacy attach/connect helper가 `SpotNode` 내부 state를 직접 만지는 경로
- core request/reply routed delivery
  - `runtime->external_router`를 직접 사용해 route channel peer로 send/dispatch하는 경로
- core data plane state
  - `external_router`, `external_router_ingress`, channel dealer/pub ingress 소유 필드
- core dispatch API
  - `zlink_spot_install_external_router_dispatch`, `zlink_spot_process_external_router`,
    `zlink_spot_try_process_external_router_parts`의 직접 socket 처리 경로
- framework .NET
  - `ZLinkRoutedSpotRelayPackets` 직접 encode/decode와
    `ZLinkSpotRouteRelayIngressTransport` 직접 처리
- framework Java/Kotlin
  - `ZLinkRoutedSpotRelayPackets` 직접 encode/decode와 fake backend relay packet 조립
- framework C++
  - `route_channel_runtime_t::submit_spot_send_parts`,
    `request_reply_to_spot_parts`의 직접 relay packet 조립
- framework Node
  - `acceptSpotRoutesFromChannel` 구현이 `SpotNode` attach/connect native method에 의존하는 경로
- bindings samples/perf
  - `attach_channel_dealer*`, router channel peer, `attach_pub_ingress`를 표준 사용 예로 보여 주는 코드

삭제 검증은 `rg`로 수행한다. 남아도 되는 것은 deprecated symbol 선언, migration error wrapper,
문서의 legacy 설명, major 제거 계획뿐이다. runtime hot path나 sample 표준 흐름에 legacy 직접
attach 구현이 남으면 완료로 보지 않는다.

### 4단계: core public C API 추가

`core/include/zlink/service/spot.h` 또는 별도 `core/include/zlink/service/spot_bridge.h`를 검토한다.
계약이 `SpotNode`가 아니라 bridge 중심이면 별도 header가 더 읽기 쉽다.

### 5단계: core examples와 smoke 추가

C sample은 최소 두 개를 둔다.

| sample | 검증 |
|--------|------|
| client/server bridge request | `DEALER` channel로 target `Spot` request/reply |
| route mesh bridge request | `ROUTER` channel로 target `SpotNode` peer를 지정해 request/reply |

## core 회귀 테스트 계획

### CTest 단위

| 테스트 | 목적 |
|--------|------|
| `test_spot_route_bridge_codec` | relay packet frame 순서, malformed 처리 |
| `test_spot_route_bridge_dealer_request` | client/server channel socket으로 request/reply |
| `test_spot_route_bridge_dealer_send` | command 전달과 target route 없음 |
| `test_spot_route_bridge_router_request` | route mesh `ROUTER` socket으로 target node/spot request |
| `test_spot_route_bridge_router_send` | route mesh command 전달 |
| `test_spot_route_bridge_policy` | `SPOT_ROUTE` 하위 policy의 inbound relay 거부와 error reply |
| `test_spot_route_bridge_pending_close` | pending request가 close에서 실패 완료 |
| `test_spot_route_bridge_backpressure` | `EAGAIN`과 retry 가능 상태 |
| `test_spot_route_bridge_received_handoff` | relay packet은 bridge가 처리하고 application packet은 caller dispatcher로 남김 |
| `test_spot_route_bridge_socket_owner_guard` | socket-owner receive pump를 추가할 경우 직접 recv 혼용 차단 |
| `test_spot_route_bridge_monitoring` | counters와 summary |
| `test_spot_node_publisher_handle` | raw `PUB` attach 없이 local `SpotNode` topic plane으로 publish |
| `test_spot_legacy_attach_wrappers` | legacy attach API가 새 core 객체를 호출하고 직접 attach state를 만들지 않음 |

### 기존 테스트 조정

기존 router-channel peer와 channel dealer attach 테스트는 두 부류로 나눈다.

| 기존 영역 | 조정 |
|-----------|------|
| attach API contract 테스트 | deprecated API가 남아 있는 동안 symbol 존재와 migration error 확인 |
| framework SPOT route egress 테스트 | core bridge 기반으로 재작성 |
| route mesh no-arg client/discovery 테스트 | route mesh discovery 정책 회귀로 유지 |
| SPOT dispatch/readable 테스트 | bridge inbound가 기존 SPOT routed dispatch를 깨지 않는지 추가 |
| pub ingress 테스트 | raw `PUB` attach 경로 대신 publisher handle 기반으로 재작성 |

기존 public symbol이 사라지는 것은 이 계획의 1차 목표가 아니다. 그러나 기존 동작을 모두
보존한다는 뜻은 아니다. `SpotNode`가 외부 socket state를 다시 소유해야만 동작하는 API는
migration error로 실패시킨다. 따라서 deprecated 후보 API는 아래 절차를 따라 관리한다.

1. core header에는 bridge/publisher handle 대체 API와 deprecated 경고를 함께 둔다.
2. C/C++/.NET/Java/Node/Python/Go/Rust 바인딩별 contract test에서 기존 symbol이 유지되는지와
   새 bridge API와 publisher handle API가 추가됐는지를 모두 확인한다.
3. 바인딩 문서에는 기존 API가 legacy 경로이며 새 코드에서는 bridge 또는 publisher handle을
   쓰라는 경고를 넣는다.
4. 기존 API 제거 여부는 별도 major 변경 계획에서만 결정한다.

### 실패 경로

아래 실패 경로는 반드시 별도 테스트로 둔다.

- inbound relay reject policy가 request를 받으면 error reply를 보낸다.
- inbound relay reject policy가 command를 받으면 drop하고 counter를 증가시킨다.
- target `Spot`이 없으면 request는 error reply, command는 failure counter를 남긴다.
- malformed relay packet은 application route handler로 넘어가지 않는다.
- bridge가 닫힌 뒤 callback이 한 번만 완료된다.

## bindings 적용 계획

bindings는 core C API를 얇게 투영한다. 언어별 framework가 core bridge를 직접 쓰려면 각
binding에 최소 public API가 필요하다.

### 언어별 public interface 요약

- C
  - 추가: `zlink_spot_route_bridge_*`, `zlink_spot_node_publisher_*`
  - legacy: `zlink_spot_node_attach*`와 router channel peer symbol은 deprecated 또는 migration
    error로 처리한다.
- C++
  - 추가: `spot_route_bridge_t`, `spot_node_publisher_t`
  - legacy: `spot_node_t::attach_channel_dealer*`, `connect_router_channel_peer*`,
    `attach_pub_ingress`는 deprecated로 처리한다.
- .NET
  - 추가: `ISpotRouteBridge` 또는 `ZLinkSpotRouteBridge`,
    `ISpotNodePublisher` 또는 `ZLinkSpotNodePublisher`
  - legacy: `ISpotNode`의 attach/connect 계열은 deprecated 또는 제거 후보로 처리한다.
- Java
  - 추가: `SpotRouteBridge`, `SpotNodePublisher`
  - legacy: `NativeSpotNode` attach/connect 계열은 deprecated 또는 migration error wrapper로
    처리한다.
- Node/TypeScript
  - 추가: `SpotRouteBridge`, `SpotNodePublisher`
  - legacy: `SpotNode` attach/connect native method는 deprecated로 처리한다. framework public
    API는 1차 적용에서 유지한다.
- Python
  - 추가: `SpotRouteBridge`, `SpotNodePublisher`
  - legacy: `SpotNode.attach_channel_dealer*`, router peer, `attach_pub_ingress`는 deprecated로
    처리한다.
- Go
  - 추가: `SpotRouteBridge`, `SpotNodePublisher`
  - legacy: `SpotNode` legacy attach/connect wrapper는 deprecated로 처리한다.
- Rust
  - 추가: `SpotRouteBridge`, `SpotNodePublisher`
  - legacy: `SpotNode` legacy attach/connect method는 deprecated로 처리한다.

언어별 이름은 idiom에 맞게 조정할 수 있지만, 같은 기능을 서로 다른 계층에 숨겨 넣지 않는다.
route request/send는 route bridge interface에, local topic publish는 publisher handle interface에
둔다.

### C

- `zlink_spot_route_bridge_*` C API가 기준이다.
- `zlink_spot_node_publisher_*` C API도 같은 단계에서 추가한다.
- C sample과 CTest가 기준 동작을 검증한다.
- errno 문서에 bridge error mapping을 추가한다.

### C++

- `zlink::service::spot_route_bridge_t` 또는 `zlink::spot_route_bridge_t`를 추가한다.
- `zlink::service::spot_node_publisher_t` 또는 같은 의미의 RAII wrapper를 추가한다.
- RAII로 bridge handle을 닫는다.
- `attach_dealer_channel(...)`, `attach_router_channel(...)`, `send(...)`, `request(...)` builder를 둔다.
- publisher handle은 `publish(topic, parts, flags)`를 제공한다.
- multipart ownership은 기존 `message_t`/`message_parts_t` 규칙을 따른다.
- contract header 테스트에 생성·attach·send/request surface를 추가한다.
- 기존 `spot_node_t` attach/connect surface contract test는 deprecated 기간 동안 유지하고,
  migration error 정책과 대체 bridge/publisher test를 같은 suite에서 확인한다.

### .NET

- `ISpotRouteBridge` 또는 `ZLinkSpotRouteBridge`를 binding layer에 추가한다.
- `ISpotNodePublisher` 또는 `ZLinkSpotNodePublisher`를 binding layer에 추가한다.
- `IAsyncDisposable`로 lifecycle을 정리한다.
- request callback은 `ValueTask<IReadOnlyList<Message>>` 또는 기존 submitter 패턴과 맞춘다.
- framework가 직접 relay packet을 만들지 않고 binding bridge를 호출하게 바꾼다.
- native wrapper는 internal reflection 없이 public binding API만 사용한다.

### Java

- `SpotRouteBridge implements AutoCloseable`를 추가한다.
- `SpotNodePublisher implements AutoCloseable`를 추가한다.
- request는 `CompletionStage<List<Message>>` 형태로 노출한다.
- FFM/native handle ownership은 기존 service handle 규칙을 따른다.
- fake backend testkit은 core bridge 의미를 흉내 내는 test double을 제공한다.

### Node/TypeScript

- low-level binding에는 `SpotRouteBridge` class를 추가한다.
- low-level binding에는 `SpotNodePublisher` class를 추가한다.
- `request(...)`는 `Promise<Message[]>`, `send(...)`는 boolean 또는 `Promise<void>` 중 기존
  binding 관례에 맞춘다.
- framework package는 기존 `acceptSpotRoutesFromChannel` 구현을 bridge 기반으로 바꾼다.
- Node framework의 1차 적용에서는 새 egress builder를 열지 않는다. 기존
  `outbound.sendToSpot(...)` / `outbound.requestToSpot(...)` public API를 유지하고,
  내부 구현만 core bridge 기반으로 교체한다.
- Node에 egress builder를 추가하는 일은 별도 public API 계획에서 다룬다.

### Python

- `SpotRouteBridge` context manager를 추가한다.
- `SpotNodePublisher` context manager를 추가한다.
- request는 callback보다 blocking/async helper를 우선 검토한다.
- pytest에 dealer/route bridge smoke를 둔다.

### Go

- `SpotRouteBridge` struct와 `Close()`를 추가한다.
- `SpotNodePublisher` struct와 `Close()`를 추가한다.
- callback handle 해제는 기존 callback lifecycle 규칙을 따른다.
- request context cancellation과 core timeout의 우선순위를 명확히 한다.

### Rust

- `SpotRouteBridge` RAII wrapper를 추가한다.
- `SpotNodePublisher` RAII wrapper를 추가한다.
- `send`는 `Result<()>`, `request`는 blocking 또는 async feature 범위에서 결정한다.
- `Message` ownership과 borrowed payload lifetime을 기존 binding 규칙과 맞춘다.

## framework 적용 계획

framework는 core bridge를 감싼다. typed serializer, handler registry, DI, route resolver만
framework 책임으로 남긴다.

### 공통 framework 의미

| 기능 | framework 책임 | core 책임 |
|------|----------------|-----------|
| typed request/send encode | message type을 `Message` parts로 바꿈 | 없음 |
| target `Spot` resolve | application id 또는 actor id를 `RoutingId`로 바꿈 | 없음 |
| relay packet 생성 | 없음 | bridge codec |
| request pending/reply decode | typed reply decode | bridge request/reply |
| receive capability | builder 설정을 core capability로 번역 | capability 실행 |
| dispatch error observer | framework event로 보고 | core error/counter 제공 |

### .NET framework

- `ZLinkRoutedSpotRelayPackets` 직접 encode/decode를 제거한다.
- `ZLinkFrameworkRuntimeChannels`의 client/server egress와 route mesh egress가 binding
  `SpotRouteBridge`를 호출하게 한다.
- `ZLinkSpotNodeBundleRegistry`가 `DEALER`를 `SpotNode`에 attach하지 않는다.
- `ZLinkSpotNodeInitializer`가 `ConnectRouterChannelPeer` 또는
  `AttachSpotRouteChannelDiscovery`를 호출하지 않는다.
- `AcceptSpotRoutesFromChannel`은 bridge ingress policy를 등록하는 builder 의미로 바꾼다.
- 기존 public builder 이름은 유지하되, spec에는 core bridge 기반 의미를 다시 쓴다.

### Java/Kotlin framework

- `ZLinkRoutedSpotRelayPackets`와 route runtime의 relay send/request 로직을 core bridge 호출로
  교체한다.
- fake backend는 bridge 호출을 관찰할 수 있게 확장한다.
- Kotlin guide는 Java spec을 공유하되 Kotlin sample은 새 path를 사용한다.

### Node framework

- 현재 Node 문서는 egress builder를 노출하지 않는다고 설명한다. 1차 적용에서는 이 방침을
  유지한다.
- 기존 `outbound.sendToSpot(...)` / `outbound.requestToSpot(...)`가 core bridge를 쓰게 한다.
- `acceptSpotRoutesFromChannel(...)`은 bridge ingress policy 등록으로 구현한다.

### C++ framework

- `route_channel_runtime_t::submit_spot_send_parts`와
  `request_reply_to_spot_parts`가 relay packet을 직접 다루지 않고 core bridge를 호출하게 한다.
- `spot_node_builder_t::attach_channel_client`와 `accept_routes_from_channel`은 bridge 설정으로
  변환한다.
- TicTacToe/Bingo samples는 public API 변화가 없도록 먼저 유지한다. 이름을 바꾸는 경우는
  별도 migration 단계에서 한다.

## samples 적용 계획

샘플은 public contract다. 샘플에 low-level packet 조립이나 framework 내부 helper를 넣지 않는다.

| sample | 변경 방향 |
|--------|-----------|
| C core bridge sample | bridge C API의 최소 사용법 |
| C++ TicTacToe/Bingo | route egress와 SPOT ingress가 bridge 기반으로 동작하는지 검증 |
| .NET Bingo/TicTacToe | 기존 public sample code를 최대한 유지하고 내부 구현만 교체 |
| Java/Kotlin TicTacToe | route egress sample을 bridge 기반으로 재검증 |
| Node Bingo | `acceptSpotRoutesFromChannel`과 `outbound.sendToSpot/requestToSpot` path를 재검증 |
| ShoppingMall channel samples | client/server channel messaging이 SPOT attach에 의존하지 않는지 확인 |

샘플 검증은 build만으로 끝내지 않는다. 각 sample은 실제 request/reply marker와 process exit code를
확인한다.

## perf 계획

### 측정 대상

| pattern | 목적 |
|---------|------|
| `SPOT_SENDSEND` local | bridge 도입이 SPOT routed hot path를 악화하지 않는지 확인 |
| `SPOT_REQREP` local | request table과 reply path 비용 확인 |
| client/server bridge request | 기존 attach dealer 경로 대비 비용 확인 |
| route mesh bridge request | framework relay packet 직접 구현 대비 비용 확인 |
| route mesh command | command-only overhead 확인 |

### 기준

- `bindings/c/perf/run_benchmarks_multi.sh`는 `core/build` runtime을 사용한다.
- core 소스 변경 뒤에는 `cmake --build core/build`를 먼저 실행한다.
- runner가 실제 `libzlink.so` 경로와 source/build freshness를 출력해야 한다.
- 5% 미만 차이는 noise로 본다.
- 10% 이상 반복 차이가 나면 원인을 추적한다.
- perf 개선을 위해 bridge 계약을 우회하지 않는다.

### 통과 조건

첫 구현은 기존 attach 방식 대비 큰 개선을 목표로 삼지 않는다. 목표는 계약 정리다. 다만 아래 조건은
만족해야 한다.

- `SPOT_SENDSEND`와 `SPOT_REQREP` 핵심 패턴이 10% 이상 반복 악화되지 않는다.
- bridge request table이 pending request 증가에 따라 선형으로만 증가한다.
- malformed/error path가 success path HWM이나 queue를 공유해 hot path를 흔들지 않는다.
- framework sample E2E에서 p95 latency가 기존 대비 10% 이상 악화되면 원인을 기록한다.

## 문서 변경 계획

구현 전에는 정식 문서에 새 계약을 쓰지 않는다. 구현과 회귀 테스트가 끝난 뒤 아래 문서에 나누어
반영한다.

### core 문서

| 위치 | 변경 |
|------|------|
| `core/doc/spec/core/service/spot.ko.md` / `.md` | `SpotNode` ownership, bridge API, publisher handle |
| `core/doc/spec/core/socket/router.ko.md` / `.md` | bridge가 `ROUTER` received handoff를 사용할 때의 socket ownership 규칙 |
| `core/doc/spec/core/socket/dealer.ko.md` / `.md` | bridge가 `DEALER` endpoint를 사용할 때의 socket ownership 규칙 |
| `core/doc/spec/core/errno-map.ko.md` / `.md` | bridge errno mapping과 legacy migration error mapping |
| `core/doc/spec/core/service/README.ko.md` / `.md` | SPOT bridge와 publisher handle spec 링크 |
| `core/doc/spec/core/README.ko.md` / `.md` | service/socket spec index 갱신 |
| `core/doc/spec/README.ko.md` / `.md` | core spec index 갱신 |

`doc/site/docs/api/` 아래 문서는 정본이 아니다. core spec 반영 뒤 site 문서 생성 또는 동기화
절차가 있으면 그 결과로 갱신한다. 이 계획을 실행할 때 site 미러를 정본보다 먼저 직접 고치지
않는다.

### framework 문서

framework 문서는 언어별로 완결해야 한다. 각 언어 guide/spec는 자기 언어 사용자가 core 문서를
먼저 읽지 않아도 이해할 수 있게 쓴다. core 문서는 더 깊이 볼 때 링크한다.

| 위치 | 변경 |
|------|------|
| `framework/doc/framework/dotnet/spec/aspnet-core-spot.ko.md` | `AcceptSpotRoutesFromChannel`과 egress builder의 새 계약 |
| `framework/doc/framework/dotnet/spec/aspnet-core-channel-messaging.ko.md` | channel socket 소유권과 SPOT bridge 관계 |
| `framework/doc/framework/dotnet/guide/05-spot.ko.md` | .NET 사용 흐름과 오류 처리 |
| `framework/doc/framework/java/spec/spring-boot-spot.ko.md` | Java builder와 bridge 의미 |
| `framework/doc/framework/java/spec/spring-boot-channel-messaging.ko.md` | Java route/client channel 설명 |
| `framework/doc/framework/java/guide/05-spot.ko.md` | Java sample 흐름 |
| `framework/doc/framework/kotlin/guide/05-spot.ko.md` | Kotlin 사용 흐름 |
| `framework/doc/framework/node/spec/nestjs-spot.ko.md` | Node egress builder 유지/추가 결정 반영 |
| `framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md` | Node channel ownership 설명 |
| `framework/doc/framework/node/guide/05-spot.ko.md` | Node sample 흐름 |
| `framework/doc/framework/cpp/spec/cpp-spot.ko.md` | C++ builder와 bridge 의미 |
| `framework/doc/framework/cpp/guide/08-spot.ko.md` | C++ 사용 흐름 |
| `framework/doc/framework/common/e2e/config-2-spot-service.ko.md` | 공통 scenario marker와 검증 조건 |

### 문서 리뷰

새 정식 문서를 반영한 뒤에는 문서마다 Codex 2축 리뷰를 수행한다.

1. 원칙 준수: 이 문서와 `documentation-principles.ko.md`의 guide/spec/internals 구분,
   한글 산문, 현재 상태 원칙을 확인한다.
2. 코드 부합: core spec은 `core/include/`와 `core/src/`, framework 문서는 각 언어 runtime과
   sample을 기준으로 확인한다.

리뷰 finding은 live checkout에서 다시 검증한 뒤 반영한다.

## rollout 순서

### Stage 0: 계약 확정

- 이 계획 문서를 리뷰한다.
- bridge 객체 모델을 두 가지 이상 비교한다.
  - 대안 A: 하나의 bridge 객체에 여러 endpoint를 붙인다.
  - 대안 B: egress bridge와 ingress bridge를 별도 객체로 둔다.
- C API 이름, errno, ownership, thread safety를 확정한다.
- route mesh egress builder 이름을 유지할지 rename/deprecate할지 결정한다.

### Stage 1: core codec과 bridge MVP

- relay packet codec 추가
- bridge handle 추가
- `SpotNode` publisher handle 추가
- dealer egress request/send 구현
- caller-owned receive handoff API 추가
- router ingress request/send 처리 구현
- raw `PUB` attach 없이 publisher handle publish CTest 추가
- focused CTest 추가

### Stage 2: route mesh egress

- router egress request/send 구현
- target peer routing id resolver hook 추가
- `SPOT_ROUTE`와 `CHANNEL_INBOUND` capability 조합 추가
- monitoring counters 추가
- socket-owner receive pump가 필요하면 receive owner guard 계약을 별도 API로 추가
- `SpotNode` 내부 external router/channel dealer/pub ingress attach state 제거 준비

### Stage 3: bindings 최소 투영

- bindings 수정 전에 `bindings/dev_sync_local_core_libs.sh`로 core 산출물을 bindings에 동기화한다.
- C++/.NET/Java/Node 우선 적용
- Python/Go/Rust는 low-level bridge binding과 smoke test 추가
- 기존 attach/connect public API contract test 유지와 deprecated 경고 확인
- bridge public API contract test 추가
- publisher handle public API contract test 추가
- bindings contract 문서 초안 업데이트는 구현 후 진행

### Stage 4: framework 내부 교체

- .NET, Java, C++의 relay packet 직접 구현 제거
- Node는 현재 public API를 유지하고 내부 구현만 bridge 기반으로 교체
- framework의 raw `PUB` attach 사용 제거, local topic publish는 publisher handle 또는 기존
  `Spot` publish API로 교체
- `Spot`에서 외부 pub/sub channel로 publish하는 경로는 기존 channel publisher client를 사용
- handler/runtime fake backend와 scenario E2E 갱신

### Stage 5: legacy 내부 구현 제거

- `SpotNode` attachment state에서 외부 `DEALER`, `ROUTER`, `PUB` socket 소유 필드 제거
- `SpotNode` lifecycle에서 외부 channel socket connect/disconnect 구현 제거
- legacy attach/connect API implementation을 bridge 또는 publisher handle wrapper로 축소
- wrapper가 `SpotNode` 내부에 외부 socket state를 다시 만들게 되는 API는 deprecated 상태로
  실패시키고 migration error를 명확히 돌린다.
- `rg` 기반 제거 검증으로 framework/sample/test가 legacy attach implementation을 직접 호출하지
  않는지 확인

### Stage 6: samples와 perf

- C core bridge sample 추가
- framework sample 재검증
- perf baseline 측정과 10% 이상 regression 조사

### Stage 7: 정식 문서 반영

- core API/spec/guide/internals 반영
- framework 언어별 guide/spec 반영
- Codex 2축 리뷰와 수정 반복

### Stage 8: public symbol 제거 판단

- deprecated API warning과 문서 표기 유지
- 바인딩에서 deprecated API 노출 정책 확정
- 다음 major release에서 public symbol을 제거할지 별도 계획 작성

## goal 실행 체크리스트

이 문서를 goal로 실행할 때는 아래 순서를 지킨다. 앞 단계의 검증이 끝나지 않으면 다음 단계로
넘어가지 않는다. 시간이 부족하면 완료로 표시하지 말고 마지막으로 통과한 gate와 남은 gate를
기록한다.

### 공통 실행 규칙

1. 작업 시작 전에 `git status --short`로 기존 변경을 확인한다.
2. 사용자 변경으로 보이는 파일은 되돌리지 않는다.
3. core C API를 먼저 구현하고, bindings는 core build와 동기화가 끝난 뒤 수정한다.
4. framework는 bindings public API만 사용한다. .NET framework에서 binding internal member를
   reflection으로 호출하지 않는다.
5. legacy API를 남기는 경우에도 `SpotNode` 내부에 외부 socket state를 다시 만들지 않는다.
6. 새 샘플은 build 성공만으로 완료하지 않고 실제 marker와 exit code를 확인한다.

### Stage별 gate

| stage | 진입 조건 | 필수 산출물 | 통과 gate |
|-------|-----------|-------------|-----------|
| 0 계약 확정 | 이 draft가 최신 checkout에 있음 | C API 이름, legacy 처리, owner 규칙 확정 | 문서 리뷰에서 미해결 finding 없음 |
| 1 core MVP | Stage 0 통과 | bridge handle, publisher handle, dealer path, received handoff | focused CTest 통과 |
| 2 route mesh | Stage 1 통과 | router endpoint, target node resolver, capability policy | router request/send CTest 통과 |
| 3 bindings | Stage 2 통과와 core build 완료 | 언어별 binding | core lib sync 뒤 binding contract test 통과 |
| 4 framework | Stage 3 통과 | framework relay packet 직접 구현 제거 | framework unit/scenario test 통과 |
| 5 legacy 제거 | Stage 4 통과 | `SpotNode` 외부 socket 소유 state 제거 | `rg` 제거 검증 통과 |
| 6 samples/perf | Stage 5 통과 | C/framework sample, perf report | sample marker/exit code와 perf gate 통과 |
| 7 문서 반영 | Stage 6 통과 | core/framework 정식 문서 반영 | Codex 2축 리뷰 통과 |
| 8 symbol 판단 | Stage 7 통과 | major 제거 여부 계획 | deprecated policy 문서화 |

### 필수 검증 명령

실제 명령은 build system 상태에 맞게 조정할 수 있지만, 같은 의미의 검증을 반드시 수행한다.

```bash
git status --short
cmake --build core/build
ctest --test-dir core/build --output-on-failure
bindings/dev_sync_local_core_libs.sh
```

bindings와 framework 검증은 변경한 언어를 기준으로 실행한다. 한 언어만 수정했더라도 C API
inventory와 generated/include 복사 상태는 모든 bindings 기준으로 확인한다.

legacy 제거 검증은 최소한 아래 패턴을 포함한다.

```bash
rg -n "attach_channel_dealer|attach_pub_ingress|connect_router_channel_peer|external_router" core/src framework bindings
rg -n "ZLinkRoutedSpotRelayPackets|createRequestRelayParts|SubmitSpotSendParts|request_reply_to_spot_parts" framework
```

남아도 되는 항목은 deprecated symbol 선언, migration error wrapper, legacy 설명 문서,
major 제거 계획뿐이다. runtime hot path, framework runtime, sample 표준 흐름, perf runner에
legacy 직접 attach 경로가 남으면 gate 실패다.

### 산출물 기록

각 stage를 끝낼 때 아래를 기록한다.

- 변경한 파일 범위
- 실행한 build/test/perf 명령
- 통과한 marker 또는 실패 로그 위치
- 남긴 deprecated symbol과 그 이유
- 다음 stage에서 반드시 이어서 확인할 항목

## 완료 조건

이 계획은 아래 조건을 모두 만족해야 완료로 본다.

1. core bridge C API가 구현되고 CTest가 통과한다.
2. `SpotNode` publisher handle C API가 구현되고 raw `PUB` attach 없이 local topic publish가
   검증된다.
3. 추가 C API, legacy 전환 C API, 언어별 interface가 이 문서의 inventory와 일치한다.
4. receive handoff 테스트가 relay packet 처리와 application packet 전달을 모두 검증한다.
5. socket-owner receive pump를 추가한 경우 receive owner guard가 직접 recv 혼용을 차단한다.
6. 기존 `SpotNode` 외부 channel attach 경로를 framework가 더 이상 사용하지 않는다.
7. framework와 sample이 `attach_pub_ingress`로 raw `PUB` socket을 `SpotNode`에 넘기지 않는다.
8. `Spot`에서 외부 pub/sub channel로 publish하는 경로는 기존 channel publisher client로만
   설명되고 검증된다.
9. `SpotNode` 내부 runtime에 외부 `DEALER`, `ROUTER`, `PUB` socket을 직접 소유하거나
   connect/disconnect하는 legacy implementation code가 남지 않는다.
10. 제거할 내부 로직 inventory에 적은 runtime/framework/bindings 직접 attach 경로가 `rg` 검증에서
    남지 않는다.
11. 기존 attach/connect public API는 deprecated 기간 동안 contract test로 유지되고,
   새 bridge API contract test가 각 바인딩에 추가된다.
12. C++/.NET/Java/Node framework가 core bridge 기반으로 같은 의미를 제공한다. Node는 1차
   적용에서 기존 `outbound.sendToSpot(...)` / `outbound.requestToSpot(...)` API를 유지한다.
13. Python/Go/Rust bindings에 bridge 최소 API와 smoke test가 있다.
14. route mesh와 client/server channel 모두 같은 bridge 계약을 따른다.
15. sample E2E가 실제 request/reply marker와 exit code로 검증된다.
16. perf gate에서 10% 이상 반복 regression이 없거나 원인과 후속 계획이 문서화된다.
17. core 문서와 framework 언어별 문서가 구현 후 정식 문서에 반영된다.
18. Codex 2축 리뷰에서 의미 불일치와 문서 원칙 위반이 남지 않는다.
