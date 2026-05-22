[스펙 목차](../README.ko.md)

# Draft -- SpotNode Spot Route Channel Acceptance

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 각 binding 공개 API에
> 없는 동작을 보장하지 않는다.
> 구현과 테스트가 끝난 뒤 정식 spec, guide, internals 문서로 나누어 반영한다.

## 1. 목적

이 초안은 `SpotNode`의 routed plane을 router capability가 있는 channel에
연결하는 계약을 정의한다.

현재 framework에는 `ZLinkSpotRoute.RouterChannelId`가 있다. 이 값은
"어느 router channel을 통해 target SpotNode로 갈 것인가"를 표현한다. 하지만
현재 구현에는 `SpotNode` 내부 routed router가 그 router channel에 참여하는 공식
연결 계약이 없다. 이 때문에 route resolver가 target `SpotNode`의 `RoutingId`를
찾아도, router channel의 `ROUTER`가 실제 target `Spot`으로 메시지를 보낼 수 있는
transport 연결이 닫히지 않는다.

이 문서의 목표는 아래와 같다.

- router capability가 있는 channel에서 `Spot`으로 routed send/request를 보낼 수
  있게 한다.
- `SpotNode`가 어느 router channel에 참여할지 명시하는 framework 표면을 둔다.
- 수동 연결과 discovery 자동 연결을 모두 정의한다.
- 내부 routed router 연결과 routing id handshake는 core가 책임지게 한다.
- binding 라이브러리는 framework가 reflection 없이 사용할 수 있는 public API를
  제공한다.
- 구현 전에 회귀 테스트 항목과 정식 문서 반영 계획을 함께 고정한다.

## 2. 현재 문제

현재 channel 구성은 네 가지 concrete 모델로 정리하는 것이 맞다.

| 구성 | 소켓 패턴 | 의미 |
|------|-----------|------|
| `AddClientServerChannel` | `DEALER -> ROUTER` | 일반 client/server 요청 |
| `AddFanoutChannel` | `PUB -> SUB` | event fan-out |
| `AddDealerMeshChannel` | `DEALER mesh` | peer DEALER mesh |
| `AddRouteMeshChannel` | `ROUTER mesh` | routing id 기반 direct routed transport |

`AddChannel(...)`은 channel capability를 보고 client/server 또는 fanout을 추론하는
호환 표면이다. `AddRouteChannel(...)`은 현재 `AddRouteMeshChannel(...)`로 바로
위임하는 alias다. 이 초안은 두 API를 호환성을 깨는 변경으로 삭제하는 것을 정식
방향으로 둔다. 새 문서와 샘플은 concrete 네 가지 구성만 사용해야 한다.

SPOT으로 보내는 routed 메시지는 최종적으로 `ROUTER` socket capability가 필요하다.
따라서 target이 될 수 있는 channel은 "channel 종류"가 아니라 "router capability를
갖는가"로 판단한다.

포함되는 channel:

- `AddClientServerChannel(... EnableServer(...))`의 server `ROUTER`
- `AddRouteMeshChannel(...)`의 route mesh `ROUTER`

제외되는 channel:

- `AddFanoutChannel(...)`: PUB/SUB 경로이므로 SPOT routed send 대상이 아니다.
- `AddDealerMeshChannel(...)`: DEALER 경로이므로 router anchor가 아니다.

source 쪽 egress 설정은 target SpotNode ingress 설정과 별도다. framework 같은 상위
계층은 source process 가 보유한 local egress channel 을 명시하고, 그 channel 설정에
target SpotNode 가 accept 한 ingress channel 이름을 저장할 수 있다. 이때 target Spot 은
Spot rid 로 지정하며, target 정보만으로 local connection 을 역조회하지 않는다.

현재 빠진 부분은 아래 연결 관계다.

```text
+------------------+        +------------------+
| Router Channel   |        | SpotNode         |
|------------------|        |------------------|
| Router socket    |<------>| Routed router    |
| channel id       |        | node routing id  |
+------------------+        +------------------+
          |
          v
+------------------+
| Target Spot      |
|------------------|
| spot routing id  |
+------------------+
```

위 연결이 있어야 router channel의 `ROUTER`가 `targetNodeRid + targetSpotRid`로
`Spot`에 메시지를 보낼 수 있다.

## 3. 설계 결정

### 3.1 연결 방향

이 초안은 **SpotNode가 router channel에 참여하는 방향**을 채택한다.

즉 router channel이 모든 `SpotNode`를 끌어오는 모델이 아니라, 각 `SpotNode`가
"나는 이 router channel을 통해 SPOT routed 메시지를 받겠다"라고 선언한다.

이 방향을 선택하는 이유는 아래와 같다.

- `SpotNode`가 자기 routed ingress 노출 여부를 소유한다.
- router channel runtime이 SPOT topology와 Spot lifecycle을 직접 알 필요가 없다.
- 수동 연결과 discovery 연결을 `SpotNode` 구성 안에서 같은 의미로 표현할 수 있다.
- framework는 channel id와 peer source를 관리하고, core는 실제 router 연결을
  처리한다.

### 3.2 router channel acceptance는 폐기된 channel ROUTER attach와 다르다

이 초안은 `doc/spec/draft/spot-channel-router-attach.ko.md`의 폐기된 설계를 되살리는
문서가 아니다.

폐기된 초안은 caller-owned `ROUTER` socket을 `SpotNode`에 outbound channel socket처럼
붙이는 모델이었다. 이 문서의 모델은 반대다. `SpotNode`의 내부 routed router가
이미 존재하는 router-capable channel에 peer로 참여한다.

핵심 차이는 아래와 같다.

| 구분 | 폐기된 channel ROUTER attach | 이 초안 |
|------|------------------------------|---------|
| 붙는 대상 | caller-owned ROUTER socket | router-capable channel |
| 소유자 | 외부 socket owner | `SpotNode` routed plane |
| 목적 | Spot에서 외부 ROUTER peer로 outbound 호출 | router channel에서 Spot으로 routed 호출 |
| 판단 | 폐기 | 새 초안 |

### 3.3 resolver 책임

`IZLinkSpotRouteResolver`는 계속 위치 결정만 담당한다.

```csharp
public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid);
```

- `RouterChannelId`: 어떤 router-capable channel을 타야 하는지 나타낸다.
  이 값은 등록된 `AddClientServerChannel` 또는 `AddRouteMeshChannel` 중 router
  capability가 있는 channel name을 가리킨다.
- `TargetNodeRid`: target `SpotNode`의 routing id다.
- `SpotRid`: target user `Spot`의 routing id다.

resolver는 연결을 만들지 않는다. 연결은 `SpotNode`가 특정 channel에서 오는
SPOT route를 받겠다고 선언하는 표면이 담당한다.

## 4. Framework 구성 초안

### 4.1 기본 형태

`SpotNode` builder에 channel에서 오는 SPOT route를 받는 표면을 추가한다.

```csharp
options.AddClientServerChannel("api", channel =>
{
    channel.EnableServer(server =>
    {
        server.Bind("tcp://0.0.0.0:7000");
    });
});

options.AddSpotMesh("game.stage", mesh =>
{
    mesh.AddNode("stage-node", node =>
    {
        node.Bind("tcp://0.0.0.0:9000");
        node.EnableRouter(router => router.Bind("tcp://0.0.0.0:9001"));
        node.AcceptSpotRoutesFromChannel("api");
        node.AddEntrySpot<StageEntrySpot>();
        node.AddSpotFactory<StageSpot>("stage");
    });
});
```

`AcceptSpotRoutesFromChannel("api")`의 의미는 아래와 같다.

- `api` channel이 router capability를 갖는지 검증한다.
- 현재 `SpotNode`가 `api` channel의 router endpoint 집합에서 오는 SPOT routed
  메시지를 받을 수 있게 연결한다.
- 연결된 router channel은 `targetNodeRid + targetSpotRid`로 target `Spot`에
  send/request를 보낼 수 있다.

### 4.2 Route mesh channel에도 같은 표면을 사용한다

```csharp
options.AddRouteMeshChannel("game.route", route =>
{
    route.Bind("tcp://0.0.0.0:7100");
});

options.AddSpotMesh("game.stage", mesh =>
{
    mesh.AddNode("stage-node", node =>
    {
        node.Bind("tcp://0.0.0.0:9000");
        node.EnableRouter(router => router.Bind("tcp://0.0.0.0:9001"));
        node.AcceptSpotRoutesFromChannel("game.route");
        node.AddSpotFactory<StageSpot>("stage");
    });
});
```

framework는 `game.route`가 `AddRouteMeshChannel(...)`로 등록된 channel임을 보고
router-capable channel로 인정한다.

### 4.3 수동 연결

수동 연결은 route 수신 표면 아래에서 명시한다.

```csharp
node.AcceptSpotRoutesFromChannel("api", routes =>
{
    routes.UseManualConnections(peers =>
    {
        peers.Connect("tcp://10.0.0.20:7000");
        peers.Connect("tcp://10.0.0.21:7000");
    });
});
```

수동 연결의 의미는 아래와 같다.

- endpoint는 router channel의 public `ROUTER` endpoint다.
- framework는 각 endpoint를 binding public API로 넘긴다.
- core는 `SpotNode` 내부 routed router가 해당 endpoint로 connect하도록 처리한다.
- 호출자는 core 내부 routed router socket, socket option, routing id handshake를 알
  필요가 없다.

### 4.4 자동 연결

수동 endpoint가 없으면 discovery 기반 자동 연결을 사용한다.

```csharp
options.UseDiscovery(discovery =>
{
    discovery.Add("tcp://registry1:5551");
});

node.AcceptSpotRoutesFromChannel("api");
```

자동 연결의 의미는 아래와 같다.

- framework는 `api` channel의 router-capable discovery view를 만든다.
- binding public API를 통해 `SpotNode`에 해당 discovery를 attach한다.
- core는 discovery member 변화에 맞춰 router channel peer를 connect/disconnect한다.

동일 route 수신 관계 안에서는 manual과 discovery를 섞지 않는다. manual endpoint가
하나라도 있으면 discovery attach는 수행하지 않는다.

### 4.5 validation 규칙

framework startup validation은 아래 조건을 검사한다.

- `AcceptSpotRoutesFromChannel(channelName)`의 `channelName`은 등록된 channel이어야
  한다.
- `channelName`은 정확히 하나의 router-capable channel로 해석되어야 한다. 같은
  이름이 일반 channel registry와 route mesh registry 양쪽에 있으면 startup 실패다.
- 대상 channel은 `AddClientServerChannel` 또는 `AddRouteMeshChannel`이어야 한다.
- `AddClientServerChannel`은 server 쪽이 `ROUTER`인 channel 패턴이므로 허용한다.
  이 process가 반드시 server capability를 켜야 하는 것은 아니다.
- 실제 연결 peer는 manual endpoint 또는 framework discovery로 해석되어야 한다.
  둘 다 없으면 startup 실패다.
- `AddFanoutChannel`과 `AddDealerMeshChannel`을 지정하면 startup 실패다.
- 같은 `SpotNode`가 같은 channel에서 오는 SPOT route를 중복 수락하면 startup 실패다.
- `node.EnableRouter(router => router.Bind(endpoint))` 없이 route 수신 관계를
  등록하면 startup 실패다.
- manual과 discovery peer source를 같은 route 수신 관계에 동시에 쓰면 startup
  실패다.
- `AddChannel(...)`과 `AddRouteChannel(...)`은 새 public 구성 표면에서 삭제한다.

## 5. Core C API 초안

core는 `.NET` framework의 channel builder 개념을 알 필요가 없다. core에는
"SpotNode routed router를 router channel peer에 연결한다"는 낮은 수준의 API만 둔다.

### 5.1 manual peer 연결

```c
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_connect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);

ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_disconnect_router_channel_peer(
  void *node,
  const char *channel_name,
  const char *endpoint);
```

의미:

- `node`는 `SpotNode` handle이다.
- `channel_name`은 연결 관계를 구분하는 router channel id다.
- `endpoint`는 router channel의 public `ROUTER` endpoint다.
- core는 `SpotNode` 내부 routed router가 이 endpoint로 connect하도록 필요한 내부
  연결을 만든다.
- 이 API의 `endpoint`는 이미 router channel의 public `ROUTER` endpoint다. 호출자는
  `+20000` 같은 내부 포트 파생 규칙을 알면 안 되고, 이 API에 파생 endpoint를 넘겨서도
  안 된다.

오류 규칙:

- `node == NULL`: invalid argument
- `channel_name == NULL` 또는 빈 문자열: invalid argument
- `endpoint == NULL` 또는 빈 문자열: invalid argument
- routed mode가 없는 `SpotNode`: invalid state
- 같은 `(channel_name, endpoint)` 중복 connect: 성공 no-op
- discovery-owned channel에 manual connect 시도: busy
- disconnect 대상이 없으면 not found

### 5.2 discovery attach

```c
ZLINK_EXPORT zlink_config_result_t
zlink_spot_node_attach_router_channel_discovery(
  void *node,
  const char *channel_name,
  void *discovery);
```

의미:

- `discovery`는 `channel_name`의 router-capable channel view를 제공한다.
- `channel_name`은 discovery가 가진 channel view 이름과 같아야 한다.
- core는 discovery member 변화에 맞춰 router channel peer set을 동기화한다.
- discovery destroy는 해당 자동 연결 관계의 종료를 의미한다.
- 같은 `channel_name`에 manual peer가 있으면 attach는 실패한다.
- 같은 `channel_name`에 discovery가 이미 attach되어 있으면 attach는 실패한다.

이 API는 route mesh와 client/server router channel을 모두 표현해야 한다. discovery
member가 route mesh 또는 client/server server 역할이 아니면 core는 그 peer를 연결
대상에서 제외한다.

### 5.3 status와 introspection

아래 조회 표면은 같은 변경 묶음에서 함께 추가한다.

```c
ZLINK_EXPORT zlink_connect_result_t
zlink_spot_node_disconnect_router_channel_peer_rid(
  void *node,
  const char *channel_name,
  const zlink_routing_id_t *peer_rid);
```

기존 `zlink_spot_node_peers_snapshot()`에는 router channel acceptance source와 channel
name을 노출한다. 운영 도구가 SPOT mesh peer와 router channel peer를 구분해야 하기
때문이다.

이 정보가 필요한 이유는 운영 중 아래 상태를 구분해야 하기 때문이다.

- SPOT mesh peer
- router channel manual peer
- router channel discovery peer
- disconnected target

## 6. Binding 라이브러리 초안

framework가 binding 내부 멤버를 reflection으로 호출하면 안 된다. 따라서 각 binding은
core API를 public surface로 노출해야 한다.

### 6.1 공통 binding 계약

각 binding의 `SpotNode` surface에 아래 의미의 API를 추가한다.

- `ConnectRouterChannelPeer(channelName, endpoint)`
- `DisconnectRouterChannelPeer(channelName, endpoint)`
- `AttachSpotRouteChannelDiscovery(channelName, discovery)`

언어별 이름은 각 binding naming convention을 따른다. 하지만 의미와 오류 매핑은
같아야 한다.

binding/core 계층에서 `Attach...Discovery`라는 이름을 쓰는 것은 discovery object를
`SpotNode`에 붙인다는 낮은 수준의 의미다. binding public API 이름은 SPOT route
수신 의미를 드러내기 위해 `AttachSpotRouteChannelDiscovery(...)`로 둔다. framework
사용자 표면은 `AcceptSpotRoutesFromChannel(...)`로 유지해서 channel client 연결과
혼동하지 않게 한다.

공통 규칙:

- channel name과 endpoint는 빈 값을 허용하지 않는다.
- discovery attach와 manual peer connect는 같은 channel에서 섞지 않는다.
- core가 반환한 invalid argument, busy, not found, connect failure를 각 언어의
  기존 예외 또는 result 타입으로 매핑한다.
- public API로 노출한다. framework가 internal/private member를 우회하지 않는다.

### 6.2 .NET binding

예상 surface:

```csharp
public interface ISpotNode
{
    void ConnectRouterChannelPeer(string channelName, string endpoint);
    void DisconnectRouterChannelPeer(string channelName, string endpoint);
    void AttachSpotRouteChannelDiscovery(string channelName, IDiscovery discovery);
}
```

`Zlink.Framework`는 위 public API만 호출한다.

### 6.3 C binding

C binding wrapper는 core helper와 같은 이름을 유지하되, binding layer가 별도
header를 가진다면 `bindings/c/include`에도 같은 의미를 반영한다.

multipart helper나 perf-only API로 확장하지 않는다. 이 기능은 성능 우회가 아니라
topology contract다.

### 6.4 다른 binding

Java, Node, Python, Go, Rust, C++ binding은 같은 순서로 반영한다.

- native declaration 추가
- `SpotNode` public method 추가
- error mapping 추가
- sample 또는 smoke test 추가
- binding spec 문서 갱신

## 7. Framework runtime 초안

### 7.1 registration model

`ZLinkSpotNodeRegistration`에 channel별 SPOT route 수신 목록을 추가한다.

개념 모델:

```csharp
internal sealed class ZLinkSpotRouteChannelAcceptanceRegistration
{
    public required string ChannelName { get; init; }
    public List<string> ManualConnections { get; } = [];
}
```

builder:

```csharp
public interface IZLinkSpotMeshNodeBuilder
{
    void AcceptSpotRoutesFromChannel(
        string channelName,
        Action<IZLinkSpotRouteChannelAcceptanceBuilder>? configure = null);
}

public interface IZLinkSpotRouteChannelAcceptanceBuilder
{
    void UseManualConnections(
        Action<ISpotRouterChannelConnections> configure);
}
```

### 7.2 initialization model

`ZLinkSpotNodeInitializer`는 `SpotNode` 생성, `SetRoutingId`, `Bind` 이후 channel별
SPOT route 수신 관계를 적용한다.

순서:

1. `SpotNode`를 만든다.
2. node routing id를 설정한다.
3. `SpotNode.Bind(...)`를 호출한다.
4. SPOT mesh discovery와 manual peer를 기존처럼 적용한다.
5. channel별 SPOT route 수신 관계를 적용한다.
6. Entry Spot을 초기화한다.

SPOT route 수신 관계 적용:

- 수신 관계에 manual endpoint가 있으면 `ConnectRouterChannelPeer(...)`를 호출한다.
- manual endpoint가 없으면 해당 channel의 discovery view를 만들고
  `AttachSpotRouteChannelDiscovery(...)`를 호출한다.
- manual endpoint가 없고 framework discovery도 없으면 startup validation에서 실패한다.
- 같은 process에 같은 channel의 server `ROUTER` bind endpoint가 있더라도 implicit
  local connect는 하지 않는다. local 연결도 discovery view 또는 manual endpoint로
  명시해야 한다.

### 7.3 SendSpot / RequestSpot 경로

framework routed spot call은 `ZLinkSpotRoute.RouterChannelId`를 실제 transport 선택에
사용해야 한다.

기본 경로:

```text
spot call
  -> resolve spot route
  -> get router-capable channel by RouterChannelId
  -> RouterSocket.SendToSpot or RequestToSpot
  -> target SpotNode routed ingress
  -> target Spot handler
```

현재처럼 `RouterChannelId`를 envelope metadata처럼만 쓰고 native `Spot.SendToSpot`
경로로 바로 내려가면 안 된다. route resolver가 반환한 router channel id가 실제
연결과 맞아야 한다.

## 8. 회귀 테스트 명세

구현 전에 아래 테스트를 먼저 추가한다. 초기에는 실패하는 테스트가 맞다.

### 8.1 Core

Core integration test:

- `test_spot_node_router_channel_manual_send_spot`
  - router channel `ROUTER`를 bind한다.
  - `SpotNode`가 `zlink_spot_node_connect_router_channel_peer()`로 해당 endpoint에
    connect한다.
  - router channel에서 `zlink_router_send_spot()`으로 user `Spot`에 메시지를 보낸다.
  - target `Spot` handler가 payload를 받는다.

- `test_spot_node_router_channel_manual_request_spot`
  - manual connect 후 `zlink_router_request_spot()`을 호출한다.
  - target `Spot`이 reply한다.
  - router channel requester가 reply payload를 받는다.

- `test_spot_node_router_channel_disconnect_blocks_delivery`
  - disconnect 후 같은 target으로 send/request가 실패해야 한다.

- `test_spot_node_router_channel_discovery_connects_new_peer`
  - discovery member가 추가되면 `SpotNode` routed router가 자동으로 연결된다.

- `test_spot_node_router_channel_manual_and_discovery_conflict`
  - 같은 channel에 manual과 discovery를 섞으면 실패한다.

- `test_spot_node_router_channel_duplicate_manual_connect_is_idempotent`
  - 같은 `(channel_name, endpoint)`를 두 번 connect해도 두 번째 호출은 성공 no-op이다.
  - 중복 호출 때문에 peer 상태가 두 개 생기면 안 된다.

- `test_spot_node_router_channel_rejects_invalid_channel_name`
  - 빈 channel name과 NULL channel name을 거부한다.

### 8.2 Binding

각 binding 테스트:

- public `SpotNode.ConnectRouterChannelPeer(...)`가 core API를 호출한다.
- duplicate connect는 성공 no-op으로 처리된다.
- disconnect unknown endpoint는 not found 또는 false result로 매핑된다.
- discovery attach는 public discovery object만 받는다.
- framework adapter가 public binding API만 사용한다.

.NET binding은 `bindings/dotnet/tests/Zlink.Tests`에 public surface와 manual delivery
테스트를 둔다.

### 8.3 Framework

Framework 테스트:

- `AddSpotNode_AcceptSpotRoutesFromChannel_ClientServer_AllowsRouterSendToSpot`
- `AddSpotNode_AcceptSpotRoutesFromChannel_RouteMesh_AllowsRouterSendToSpot`
- `AcceptSpotRoutesFromChannel_RejectsFanoutChannel`
- `AcceptSpotRoutesFromChannel_RejectsDealerMeshChannel`
- `AcceptSpotRoutesFromChannel_RejectsAmbiguousChannelName`
- `AcceptSpotRoutesFromChannel_RequiresEnableRouter`
- `AcceptSpotRoutesFromChannel_ManualConnections_AreApplied`
- `AcceptSpotRoutesFromChannel_DiscoveryConnections_AreApplied`
- `SendSpot_UsesRouterChannelIdTransport`
- `RequestSpot_UsesRouterChannelIdTransport`
- `AddChannel_IsRemovedFromPublicConfigurationSurface`
- `AddRouteChannel_IsRemovedFromPublicConfigurationSurface`

Bingo sample은 API server 또는 session gateway router가 play `Spot`으로 routed request를
보내는 시나리오를 포함해야 한다.

### 8.4 문서 회귀 테스트

`RegressionTests`에는 아래 검색 규칙을 추가한다.

- 새 guide와 sample 문서에서 `AddChannel(` 사용 금지
- 새 guide와 sample 문서에서 `AddRouteChannel(` 사용 금지
- `AcceptSpotRoutesFromChannel` 설명이 `AddClientServerChannel`과
  `AddRouteMeshChannel` 양쪽을 모두 언급하는지 확인
- framework 문서가 binding internal/private 접근을 지시하지 않는지 확인

## 9. 정식 문서 반영 계획

구현이 끝난 뒤 아래 문서에 나누어 반영한다.

### 9.1 core internals

대상:

- `doc/internals/spot-internals.ko.md`
- `doc/internals/spot-internals.md`

반영 내용:

- `SpotNode` routed router와 router channel peer 연결 구조
- manual peer와 discovery peer의 상태 전이
- SPOT mesh peer와 router channel peer의 차이
- data plane에서 router channel ingress가 target `Spot`으로 전달되는 흐름

### 9.2 core spec

대상:

- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`

반영 내용:

- `zlink_spot_node_connect_router_channel_peer`
- `zlink_spot_node_disconnect_router_channel_peer`
- `zlink_spot_node_attach_router_channel_discovery`
- `zlink_router_send_spot` / `zlink_router_request_spot`이 router channel acceptance를 통해
  target `SpotNode`에 도달하는 조건
- 오류 코드와 lifecycle 규칙

### 9.3 binding spec

대상:

- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/rust/README.md`

반영 내용:

- 각 binding의 `SpotNode` public method
- error mapping
- public API만 사용하는 framework adapter 원칙
- sample smoke 요구사항

### 9.4 guide

대상:

- `doc/guide/03-4-router.ko.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/03-0-socket-patterns.ko.md`
- 영문 guide 대응 문서

반영 내용:

- router channel에서 `Spot`으로 보내는 사용 시나리오
- `ClientServerChannel` server router와 `RouteMeshChannel` router mesh의 차이
- `AcceptSpotRoutesFromChannel`을 언제 쓰는지
- 내부 routed endpoint나 포트 파생 규칙을 guide에 노출하지 않는다는 원칙

### 9.5 framework doc

대상:

- `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md`
- `framework/languages/dotnet/doc/spec/aspnet-core-channel-messaging.ko.md`
- `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md`
- `framework/languages/dotnet/doc/guide/samples/spot-samples.ko.md`
- `framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md`
- `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md`
- `framework/languages/dotnet/doc/README.ko.md`

반영 내용:

- 네 가지 concrete channel 구성만 기본 표면으로 설명
- `AddChannel(...)`과 `AddRouteChannel(...)` 삭제 표기
- `AcceptSpotRoutesFromChannel(...)` framework API
- route resolver와 router channel acceptance의 책임 분리
- Bingo sample의 router-to-spot 메시징 흐름

## 10. core 완료 후 binding 배포 계획

core API와 core regression test가 끝난 뒤 binding 작업 전에 반드시 core runtime을
다시 빌드하고 binding local core library를 갱신한다.

순서:

1. core public header와 implementation을 수정한다.
2. core regression test를 추가한다.
3. core build를 수행한다.

```bash
cmake --build core/build
```

4. core test를 수행한다.
5. core 변경이 통과하면 binding local runtime을 갱신한다.

```bash
/home/hep7/project/kairos/zlink/bindings/dev_sync_local_core_libs.sh
```

6. C binding부터 public wrapper와 test를 맞춘다.
7. C++/.NET/Java/Node/Python/Go/Rust binding을 순서대로 반영한다.
8. 각 binding test와 sample smoke를 수행한다.
9. framework adapter와 framework tests를 수행한다.
10. 정식 문서를 반영한다.

core 변경 뒤 `dev_sync_local_core_libs.sh`를 건너뛰면 binding test가 오래된 runtime을
보고 실패하거나, 더 나쁘게는 잘못 통과할 수 있다. 따라서 core API 구현 완료 후
binding 작업을 시작하기 전에 이 단계는 필수 gate로 둔다.

## 11. 구현 순서 체크리스트

아래 항목은 이 draft를 정식 구현과 문서로 옮길 때 빠뜨리면 안 되는 완료 조건이다.
현재 구현은 이 목록을 기준으로 반영됐다.

- [x] 이 draft의 API 이름이 구현 패치에 그대로 반영됐는지 확인한다.
- [x] `AddChannel(...)` / `AddRouteChannel(...)` 삭제 패치를 반영한다.
- [x] core regression test를 먼저 추가한다.
- [x] core C API를 구현한다.
- [x] core internals status snapshot에 router channel peer 구분을 반영한다.
- [x] `cmake --build core/build`를 통과시킨다.
- [x] core regression test를 통과시킨다.
- [x] `bindings/dev_sync_local_core_libs.sh`를 실행한다.
- [x] C binding public API와 test를 반영한다.
- [x] 나머지 binding public API와 test를 반영한다.
- [x] .NET framework binding adapter가 public API만 쓰는지 확인한다.
- [x] framework registration/runtime/test를 반영한다.
- [x] Bingo sample에 router-to-spot 흐름을 반영한다.
- [x] 정식 docs와 guide를 반영한다.
- [x] 문서 회귀 테스트를 통과시킨다.
