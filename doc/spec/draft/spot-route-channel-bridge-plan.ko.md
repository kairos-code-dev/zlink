# SPOT route channel bridge core 전환 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현·회귀 테스트·성능 검증이
> 끝난 뒤에만 정식 spec, guide, internals 문서에 나누어 반영한다.
>
> 구현 전 단계에서는 이 문서의 API 이름과 세부 시그니처를 `doc/site/docs/api/` 또는
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

## 비목표

- SPOT pub/sub topic plane을 다시 설계하지 않는다.
- `ROUTER`와 `DEALER` socket 자체의 일반 send/recv 계약을 바꾸지 않는다.
- framework serializer, DI, handler discovery 정책을 core로 내리지 않는다.
- route mesh channel을 물리적으로 client/server socket 쌍으로 바꾸지 않는다. `ROUTER`
  socket 특성은 그대로 둔다.
- 기존 언어별 framework의 typed API 이름을 이 문서에서 최종 확정하지 않는다. core 계약이
  먼저 확정된 뒤 각 언어 문서에서 idiom에 맞게 정한다.

## 출발점

현재 checkout에는 SPOT 외부 channel 경로가 두 갈래로 흩어져 있다.

| 경로 | 현재 소유권 | 문제 |
|------|-------------|------|
| client/server channel | `SpotNode`에 `DEALER`를 attach한다 | `SpotNode`가 외부 channel client socket을 들게 된다 |
| router-capable channel | `SpotNode`의 `external_router`가 router-channel peer로 연결된다 | SPOT routed plane과 application route mesh channel 의미가 섞인다 |
| framework route mesh egress | framework route mesh runtime의 `ROUTER`가 SPOT relay packet을 보낸다 | 언어별 framework가 relay 의미를 직접 구현한다 |

이 구조는 동작은 가능하지만 책임 경계가 흐리다. `SpotNode`는 SPOT routed/pubsub plane의
소유자여야 하고, client/server나 route mesh channel의 socket lifecycle까지 알 필요가 없다.
또한 SPOT route relay packet 의미가 framework마다 따로 구현되면 bindings와 framework 사이에
drift가 생긴다.

## 설계 원칙

### 1. `SpotNode`는 SPOT plane만 소유한다

`SpotNode`는 SPOT node routing id, `Spot` lifecycle, SPOT routed request/reply, pub/sub
topic plane을 소유한다. 외부 channel socket은 소유하지 않는다.

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
| `zlink_spot_route_bridge_policy_t` | inbound 허용 범위와 error 처리 방식을 정한다 | bridge 생성 시 복사한다 |

### channel endpoint 종류

bridge는 최소 두 channel endpoint를 지원한다.

| endpoint 종류 | socket | 방향 | 설명 |
|----------------|--------|------|------|
| client/server egress | `DEALER` | outbound 중심 | 외부 channel client가 target `Spot`으로 command/request를 보낸다 |
| route mesh egress | `ROUTER` | routed outbound | route mesh peer routing id를 지정해 target `SpotNode`로 relay한다 |
| route ingress | `ROUTER` | inbound | channel에서 들어온 SPOT relay packet을 local `SpotNode`로 넘긴다 |

하나의 bridge가 양방향을 모두 맡을지, egress bridge와 ingress bridge를 분리할지는 구현 전
세부 설계에서 비교한다. 기본 선택은 **하나의 bridge 객체에 endpoint와 policy를 여러 개
붙이는 모델**이다. 같은 channel socket에 대해 ingress와 egress 설정을 한 곳에서 검증할 수
있기 때문이다.

### C API 초안

아래 이름은 계획용 초안이다.

```c
typedef struct zlink_spot_route_bridge_options_t {
    uint32_t struct_size;
    int default_request_timeout_ms;
    int inbound_policy;
    int error_reply_policy;
} zlink_spot_route_bridge_options_t;

void *zlink_spot_route_bridge_new(
    void *ctx,
    void *spot_node,
    const zlink_spot_route_bridge_options_t *options);

int zlink_spot_route_bridge_attach_dealer_channel(
    void *bridge,
    const char *channel_name,
    void *dealer_socket);

int zlink_spot_route_bridge_attach_router_channel(
    void *bridge,
    const char *channel_name,
    void *router_socket);

int zlink_spot_route_bridge_set_target_node(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_node_rid);

int zlink_spot_route_bridge_send(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_spot_rid,
    const zlink_msg_t *parts,
    size_t part_count,
    int flags);

int zlink_spot_route_bridge_request(
    void *bridge,
    const char *channel_name,
    const zlink_routing_id_t *target_spot_rid,
    const zlink_msg_t *parts,
    size_t part_count,
    zlink_spot_route_bridge_reply_fn callback,
    void *user_data,
    int flags,
    int timeout_ms);

int zlink_spot_route_bridge_process_router_readable(
    void *bridge,
    const char *channel_name);

int zlink_spot_route_bridge_process_dealer_readable(
    void *bridge,
    const char *channel_name);

int zlink_spot_route_bridge_close(void *bridge);
```

세부 구현에서 `zlink_msg_t *parts` 대신 기존 multipart helper 계약을 그대로 따를 수 있다.
중요한 점은 bridge가 packet 조립과 reply decode를 담당하고 caller가 relay frame을 직접
만들지 않는다는 것이다.

### 에러 계약

| 상황 | 반환 또는 reply | errno 후보 |
|------|----------------|------------|
| bridge가 닫힘 | 실패 | `ESHUTDOWN` |
| 등록되지 않은 channel | 실패 | `ENOENT` |
| socket 종류 불일치 | 실패 | `ENOTSUP` |
| target `Spot` route 없음 | error reply 또는 실패 | `EHOSTUNREACH` |
| target `SpotNode` route 없음 | error reply 또는 실패 | `ENETUNREACH` |
| inbound policy 위반 | request는 error reply, command는 drop | `EPERM` |
| malformed relay packet | request는 error reply, command는 drop | `EPROTO` |
| backpressure | 실패 또는 retry 가능 상태 | `EAGAIN` |
| timeout | request callback 실패 | `ETIMEDOUT` |

request packet은 가능한 한 error reply를 돌려준다. command packet은 reply 경로가 없으므로
monitor event와 rate-limited log로 남긴다.

### inbound policy

초기 policy는 세 가지면 충분하다.

| policy | 의미 |
|--------|------|
| `ZLINK_SPOT_ROUTE_BRIDGE_EGRESS_ONLY` | bridge가 outbound만 보낸다. inbound packet은 거부한다 |
| `ZLINK_SPOT_ROUTE_BRIDGE_SPOT_INGRESS` | SPOT relay packet만 받는다 |
| `ZLINK_SPOT_ROUTE_BRIDGE_FULL` | outbound와 SPOT relay ingress를 모두 허용한다 |

application route handler packet은 core bridge가 처리하지 않는다. framework route handler
dispatcher가 맡는다. bridge는 자기 packet prefix 또는 message kind가 아닌 packet을
`ENOTSUP`으로 돌려서 상위 dispatcher가 계속 처리할 수 있게 한다.

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
- `process_*_readable`은 해당 socket의 recv path와 섞이면 안 된다.
- 같은 socket을 bridge와 application code가 동시에 직접 recv하면 `EBUSY` 또는
  configuration error로 막는다.

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

### 3단계: `SpotNode` external channel attach 제거 준비

기존 `SpotNode` 외부 channel attach API는 바로 제거하지 않는다. 먼저 bridge 구현을 넣고,
framework와 bindings가 bridge를 쓰게 만든다.

deprecated 후보:

- `zlink_spot_node_attach_channel_dealer`
- `zlink_spot_node_attach_channel_dealer_manual`
- `zlink_spot_node_connect_router_channel_peer`
- `zlink_spot_node_connect_router_channel_peer_rid`
- `zlink_spot_node_disconnect_router_channel_peer`
- `zlink_spot_node_disconnect_router_channel_peer_rid`
- `zlink_spot_node_attach_router_channel_discovery`

초기에는 deprecated annotation과 문서 경고만 추가하고, 제거는 별도 major 변경에서 결정한다.

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
| `test_spot_route_bridge_policy` | egress-only inbound 거부, request error reply |
| `test_spot_route_bridge_pending_close` | pending request가 close에서 실패 완료 |
| `test_spot_route_bridge_backpressure` | `EAGAIN`과 retry 가능 상태 |
| `test_spot_route_bridge_socket_recv_exclusivity` | bridge와 직접 recv 혼용 차단 |
| `test_spot_route_bridge_monitoring` | counters와 summary |

### 기존 테스트 조정

기존 router-channel peer와 channel dealer attach 테스트는 두 부류로 나눈다.

| 기존 영역 | 조정 |
|-----------|------|
| attach API contract 테스트 | deprecated API가 남아 있는 동안 최소 유지 |
| framework SPOT route egress 테스트 | core bridge 기반으로 재작성 |
| route mesh no-arg client/discovery 테스트 | route mesh discovery 정책 회귀로 유지 |
| SPOT dispatch/readable 테스트 | bridge inbound가 기존 SPOT routed dispatch를 깨지 않는지 추가 |

### 실패 경로

아래 실패 경로는 반드시 별도 테스트로 둔다.

- egress-only bridge가 inbound request를 받으면 error reply를 보낸다.
- egress-only bridge가 inbound command를 받으면 drop하고 counter를 증가시킨다.
- target `Spot`이 없으면 request는 error reply, command는 failure counter를 남긴다.
- malformed relay packet은 application route handler로 넘어가지 않는다.
- bridge가 닫힌 뒤 callback이 한 번만 완료된다.

## bindings 적용 계획

bindings는 core C API를 얇게 투영한다. 언어별 framework가 core bridge를 직접 쓰려면 각
binding에 최소 public surface가 필요하다.

### C

- `zlink_spot_route_bridge_*` C API가 기준이다.
- C sample과 CTest가 canonical 동작을 검증한다.
- errno 문서에 bridge error mapping을 추가한다.

### C++

- `zlink::service::spot_route_bridge_t` 또는 `zlink::spot_route_bridge_t`를 추가한다.
- RAII로 bridge handle을 닫는다.
- `attach_dealer_channel(...)`, `attach_router_channel(...)`, `send(...)`, `request(...)` builder를 둔다.
- multipart ownership은 기존 `message_t`/`message_parts_t` 규칙을 따른다.
- contract header 테스트에 생성·attach·send/request surface를 추가한다.

### .NET

- `ISpotRouteBridge` 또는 `ZLinkSpotRouteBridge`를 binding layer에 추가한다.
- `IAsyncDisposable`로 lifecycle을 정리한다.
- request callback은 `ValueTask<IReadOnlyList<Message>>` 또는 기존 submitter 패턴과 맞춘다.
- framework가 직접 relay packet을 만들지 않고 binding bridge를 호출하게 바꾼다.
- native wrapper는 internal reflection 없이 public binding API만 사용한다.

### Java

- `SpotRouteBridge implements AutoCloseable`를 추가한다.
- request는 `CompletionStage<List<Message>>` 형태로 노출한다.
- FFM/native handle ownership은 기존 service handle 규칙을 따른다.
- fake backend testkit은 core bridge 의미를 흉내 내는 test double을 제공한다.

### Node/TypeScript

- low-level binding에는 `SpotRouteBridge` class를 추가한다.
- `request(...)`는 `Promise<Message[]>`, `send(...)`는 boolean 또는 `Promise<void>` 중 기존
  binding 관례에 맞춘다.
- framework package는 기존 `acceptSpotRoutesFromChannel` 구현을 bridge 기반으로 바꾼다.
- Node framework는 egress builder를 새로 열지 말지 별도 결정한다. 열 경우 core bridge
  계약과 같은 이름을 쓴다.

### Python

- `SpotRouteBridge` context manager를 추가한다.
- request는 callback보다 blocking/async helper를 우선 검토한다.
- pytest에 dealer/route bridge smoke를 둔다.

### Go

- `SpotRouteBridge` struct와 `Close()`를 추가한다.
- callback handle 해제는 기존 callback lifecycle 규칙을 따른다.
- request context cancellation과 core timeout의 우선순위를 명확히 한다.

### Rust

- `SpotRouteBridge` RAII wrapper를 추가한다.
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
| inbound policy | builder 설정을 core policy로 번역 | policy 실행 |
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

- 현재 Node 문서는 egress builder를 노출하지 않는다고 설명한다. 이 방침을 유지할지,
  core bridge 도입과 함께 egress builder를 열지 결정한다.
- 최소 변경안은 기존 `outbound.sendToSpot(...)` / `outbound.requestToSpot(...)`가 core bridge를
  쓰게 하는 것이다.
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
| `doc/site/docs/api/spot.ko.md` / `.md` | `SpotNode`가 외부 channel socket을 소유하지 않는 계약, bridge API 링크 |
| `doc/site/docs/api/socket.ko.md` / `.md` | bridge가 socket recv ownership을 가져갈 때 직접 recv와 섞을 수 없는 규칙 |
| `doc/site/docs/api/bindings.ko.md` / `.md` | bindings가 bridge를 어떻게 투영해야 하는지 언어별 계약 |
| `doc/site/docs/api/errno-map.ko.md` / `.md` | bridge errno mapping |
| `doc/site/docs/guide/07-3-spot.ko.md` / `.md` | 사용자 관점의 SPOT 외부 channel messaging 사용법 |
| `doc/site/docs/internals/spot-internals.ko.md` / `.md` | bridge가 `SpotNode`와 channel socket 사이에서 하는 일 |
| `doc/site/docs/internals/services-internals.ko.md` / `.md` | service ownership 경계 |

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
- dealer egress request/send 구현
- router ingress request/send 구현
- focused CTest 추가

### Stage 2: route mesh egress

- router egress request/send 구현
- target peer routing id resolver hook 추가
- egress-only inbound policy 추가
- monitoring counters 추가

### Stage 3: bindings 최소 투영

- C++/.NET/Java/Node 우선 적용
- Python/Go/Rust는 low-level bridge binding과 smoke test 추가
- bindings contract 문서 초안 업데이트는 구현 후 진행

### Stage 4: framework 내부 교체

- .NET, Java, C++의 relay packet 직접 구현 제거
- Node는 현재 public surface 유지 또는 egress builder 추가 결정을 반영
- handler/runtime fake backend와 scenario E2E 갱신

### Stage 5: samples와 perf

- C core bridge sample 추가
- framework sample 재검증
- perf baseline 측정과 10% 이상 regression 조사

### Stage 6: 정식 문서 반영

- core API/spec/guide/internals 반영
- framework 언어별 guide/spec 반영
- Codex 2축 리뷰와 수정 반복

### Stage 7: deprecated API 정리

- deprecated API warning과 문서 표기
- 바인딩에서 deprecated surface 노출 정책 결정
- 다음 major release에서 제거할지 별도 계획 작성

## 완료 조건

이 계획은 아래 조건을 모두 만족해야 완료로 본다.

1. core bridge C API가 구현되고 CTest가 통과한다.
2. 기존 `SpotNode` 외부 channel attach 경로를 framework가 더 이상 사용하지 않는다.
3. C++/.NET/Java/Node framework가 core bridge 기반으로 같은 의미를 제공한다.
4. Python/Go/Rust bindings에 bridge 최소 surface와 smoke test가 있다.
5. route mesh와 client/server channel 모두 같은 bridge 계약을 따른다.
6. sample E2E가 실제 request/reply marker와 exit code로 검증된다.
7. perf gate에서 10% 이상 반복 regression이 없거나 원인과 후속 계획이 문서화된다.
8. core 문서와 framework 언어별 문서가 구현 후 정식 문서에 반영된다.
9. Codex 2축 리뷰에서 의미 불일치와 문서 원칙 위반이 남지 않는다.
