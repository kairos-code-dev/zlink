# Framework channel RoutingId 정책과 message flow tracing 확장 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤
> framework 공통 설명과 언어별 정식 spec/guide/internals 문서에 나누어 반영한다.

## 목적

framework channel 설정에서 `RoutingId` 를 일관되게 다룬다. 사용자는 노드나 채널을 추적하기 위해
의미 있는 RID 를 직접 주고 싶어 한다. 하지만 모든 socket 에 같은 RID 를 그대로 넣으면 routing key 와
디버깅용 식별자가 섞인다. 특히 client-server channel 에서 DEALER RID 는 ROUTER 에 붙는 순간 peer
key 가 되므로, server ROUTER RID 와 같은 값을 쓰면 self discovery, 중복 peer, handover 정책과 충돌할
수 있다.

이 계획은 `SetRoutingId(rid)` 를 channel 의 대표 identity 설정으로 제공하되, socket 역할별 의미를
분리한다. routing 의미가 있는 ROUTER 계열 socket 은 사용자가 준 RID 를 그대로 사용한다. ROUTER 가
아닌 내부 socket 은 같은 channel 에서 파생된 디버깅용 RID 를 사용한다. message flow tracing 에도 local
socket RID 를 출력해, 어떤 socket 이 메시지를 보내거나 받았는지 추적할 수 있게 한다.

## 현재 상태 요약

| 영역 | 현재 상태 | 문제 |
|------|-----------|------|
| core socket RID | 모든 socket 은 기본 RID 를 갖고 `set/get_routing_id` 를 지원한다. DEALER 도 RID 를 가질 수 있다. | DEALER RID 는 DEALER 내부 routing 용도가 아니라, ROUTER 가 보는 peer key 로 쓰인다. |
| ROUTER | peer RID 를 out-pipe key 로 사용한다. 중복 RID 는 reject/handover 정책을 따른다. | server ROUTER RID 와 client DEALER RID 를 같은 값으로 강제하면 peer key 충돌 가능성이 있다. |
| discovery | local RID 와 provider RID 가 같으면 self provider 로 보고 자동 연결 대상에서 제외한다. | client DEALER 에 server 와 같은 RID 를 넣으면 local server messaging 기대와 충돌할 수 있다. |
| .NET framework | route mesh 에는 `SetRoutingId(...)` 가 있다. client-server 는 `ConfigureServerRouting().RoutingId` 와 `ConfigureClientRouting().RoutingId` 가 노출되어 있다. | 역할별 RID 표면이 사용자를 헷갈리게 하고, client DEALER RID 설정은 일반 사용 패턴으로 위험하다. 기존 역할별 RID 설정 API 는 제거 대상이다. |
| actor factory 등록 | .NET, Java, Kotlin, Node 는 actor factory 를 framework 전역 옵션에 등록한다. C++ 는 SpotNode builder 아래에 등록한다. | actor 는 SpotNode/EntrySpot 에서 관리되는데 전역 등록으로 두면 여러 SpotNode 가 있을 때 actor type 의 소유 node 가 불명확하다. |
| C++/Java/Kotlin/Node framework | 언어별로 route mesh RID 설정은 존재하지만, client-server 대표 RID 정책은 명확하지 않다. | 언어별 표면과 문서가 같은 의미를 설명하지 못한다. 모든 언어에서 channel 별 RID 설정 API 를 하나로 맞춘다. |
| message flow tracing | `node=`, `src=`, `spot=`, `actor=` 등은 출력하지만 local socket RID 는 출력하지 않는다. | `node=` 는 무엇을 node 로 보는지 정의가 애매하고, 실제 socket identity 와 다를 수 있다. 메시지가 실제 어떤 socket 으로 나갔는지 확인하기 어렵다. |
| dispatch error observer | 미등록 메시지나 handler 실패를 별도 observer 로도 받을 수 있다. 샘플은 대부분 같은 필드를 다시 로그로 남긴다. | message flow tracing 과 역할이 겹치고, 사용자가 샘플을 보고 observer 등록을 필수 패턴으로 오해할 수 있다. |

## 목표 정책

이 작업은 호환성을 유지하지 않는 breaking change 로 진행한다. 기존 API 이름을 alias, deprecated wrapper,
extension helper 로 남기지 않는다. 샘플, e2e, 문서도 모두 새 표면만 사용한다.

### 1. channel 별 RID 설정 API 는 하나만 둔다

사용자는 channel builder 에서 RID 를 지정하지 않아도 된다. RID 를 지정하지 않으면 core/socket 기본 정책에
따라 socket 별 RID 를 자동 생성한다. 사용자가 `SetRoutingId(rid)` 를 호출하면 그 값은 해당 channel 의 대표
identity 다. framework 는 socket 역할에 따라 이 값을 그대로 쓰거나 파생한다. 같은 channel 에서 역할별 RID
설정 API 를 추가로 두지 않는다.

제거 대상:

| 기존 표면 | 처리 |
|-----------|------|
| client-server server/client routing config 의 `RoutingId` 속성 | public RID 설정 표면에서 제거 |
| client-server client DEALER RID 직접 설정 | 제거 |
| SPOT router/pubsub 개별 RID helper | channel 대표 `SetRoutingId` 로 통합 |
| 언어별로 이름이나 의미가 다른 RID 설정 helper | 하나의 channel RID API 로 통합 |

남길 표면:

| channel 종류 | RID 설정 API | 의미 |
|--------------|--------------|------|
| client-server | `SetRoutingId(rid)` | provider/server 대표 RID |
| route mesh | `SetRoutingId(rid)` | route node RID |
| fanout | `SetRoutingId(rid)` | fanout channel 대표 RID |
| SPOT mesh/node | `SetRoutingId(rid)` | SPOT node 대표 RID |

`SetRoutingId` 는 선택 API 다. 호출하지 않으면 framework 가 socket 생성 시 core 기본 RID 자동 생성 정책을
그대로 사용한다. 이 작업은 호환성을 유지하지 않는다. 기존 역할별 RID 설정 API 를 문서상 deprecated 로 남기지
않고 제거한다. 컴파일 오류가 발생하더라도 새 단일 API 로 고치도록 한다.

```csharp
options.AddClientServerChannel("api")
    .EnableServer(apiEndpoint)
    .EnableClient()
    .SetRoutingId(nodeRid);

options.AddRouteMesh("play")
    .EnableServer(playEndpoint)
    .EnableClient()
    .SetRoutingId(nodeRid);
```

산문에서는 API 이름을 길게 나열하지 않는다. 어떤 호출이 어떤 역할을 하는지는 예제의 호출 옆 주석으로
확인할 수 있게 한다.

```csharp
options.AddClientServerChannel("api")
    .EnableServer(apiEndpoint)   // 요청을 받는 provider ROUTER 를 bind 한다.
    .EnableClient()              // 요청을 보내는 DEALER client 를 켠다.
    .SetRoutingId(nodeRid);      // provider ROUTER 는 nodeRid, client DEALER 는 파생 RID 를 사용한다.
```

### 2. ROUTER 계열 socket 은 대표 RID 를 그대로 사용한다

ROUTER 는 routing key 를 소유한다. route mesh, client-server server, SPOT router 처럼 ROUTER 가 노드나
provider identity 를 대표하는 경우에는 사용자가 준 RID 를 그대로 설정한다.

| channel 종류 | socket 역할 | 적용 RID |
|--------------|-------------|----------|
| client-server | server ROUTER | `rid` |
| route mesh | ROUTER | `rid` |
| SPOT router | node/router | `rid` |

### 3. ROUTER 가 아닌 socket 은 역할 suffix 를 붙인 파생 RID 를 사용한다

DEALER, PUB, SUB 는 대표 RID 를 그대로 쓰지 않는다. 이 socket 들은 메시지 추적, 모니터링, peer 식별을
돕기 위해 파생 RID 를 갖는다.

| channel 종류 | socket 역할 | 적용 RID |
|--------------|-------------|----------|
| client-server | client DEALER | `rid.dealer` |
| fanout | publisher PUB | `rid.pub` |
| fanout | subscriber SUB | `rid.sub` |
| SPOT pub/sub | PUB 역할 | `rid.pub` |
| SPOT pub/sub | SUB 역할 | `rid.sub` |

이 파생 RID 는 application routing 대상 RID 로 쓰지 않는다. 예를 들어 route mesh target node RID 는
항상 대표 RID 이며, `rid.dealer` 같은 파생 RID 로 route call 을 보내는 사용 패턴은 공개 계약으로
지원하지 않는다.

### 4. binary RID 에도 안전한 suffix 규칙을 둔다

`RoutingId` 는 문자열만 받는 값이 아니다. 따라서 단순 문자열 결합만으로 규칙을 정의하면 안 된다.

규칙은 다음 중 하나로 고정한다.

| 대안 | 내용 | 장점 | 단점 |
|------|------|------|------|
| A | base bytes + `0x00` + UTF-8 suffix | binary RID 에도 구조가 명확하다. | base RID 에 `0x00` 이 있어도 사람이 읽기 어렵다. |
| B | base text 가 UTF-8 로 안전할 때만 `.dealer` 등 문자열 suffix, 아니면 실패 | 사람이 읽기 쉽다. | binary RID 사용자가 파생 RID 를 못 쓴다. |
| C | base bytes + fixed role byte | 짧고 빠르다. | 로그에서 역할을 바로 읽기 어렵다. |

선택은 A 로 한다. 이유는 모든 RID 입력 형태에 적용할 수 있고, suffix 경계가 명확하기 때문이다.
로그 출력은 기존 `RoutingId.ToString()` 또는 hex 표현 정책을 따른다. 파생 결과가 core RID 최대 길이를
넘으면 구성 오류로 실패한다.

역할 suffix 문자열은 아래로 고정한다.

| 역할 | suffix |
|------|--------|
| client DEALER | `dealer` |
| publisher PUB | `pub` |
| subscriber SUB | `sub` |

### 5. client-server client DEALER 에 대표 RID 를 그대로 넣지 않는다

DEALER RID 는 ROUTER 가 보는 peer key 다. client-server 에서 같은 프로세스가 자기 server endpoint 로
요청을 보낼 수 있어야 하므로, client DEALER RID 와 server ROUTER RID 를 같은 값으로 묶지 않는다.

허용하는 동작:

- 같은 인스턴스의 client 가 같은 인스턴스의 server 로 메시지를 보낼 수 있다.
- discovery 가 같은 endpoint 를 self 로 거르는 동작은 유지한다.
- manual endpoint 로 local server 에 연결하는 경우에도 DEALER peer RID 는 파생 RID 이므로 server
  provider RID 와 충돌하지 않는다.

금지하는 동작:

- `ClientServer.SetRoutingId(rid)` 가 client DEALER 에 `rid` 를 그대로 설정하는 동작
- 일반 public API 에서 client DEALER RID 를 별도로 지정하도록 권장하는 동작

client DEALER RID 를 직접 지정하는 public API 는 만들지 않는다. 디버깅 목적의 RID 는 대표 RID 에서
파생하고 message flow tracing 에 출력한다.

### 6. actor factory 는 SpotMesh/SpotNode builder 아래에 둔다

actor 객체는 SpotNode 의 Entry Spot 과 actor runtime 에 붙는다. 따라서 actor factory 등록도 framework 전역
옵션이 아니라 actor 를 소유할 SpotMesh/SpotNode builder 아래에 둔다. 이 변경은 RID 정책과 별개의 기능이지만,
같은 framework 공개 설정 표면을 정리하는 breaking change 로 함께 처리한다.

전역 actor factory 등록을 유지하지 않는다.

| 기존 표면 | 처리 |
|-----------|------|
| `.NET` `options.AddActorFactory<TFactory>(actorType)` | 제거 |
| Java/Kotlin `options.addActorFactory(actorType, factoryType)` | 제거 |
| Node/NestJS 전역 `.actorFactory(actorType, factory)` | 제거 |
| actor factory 만 등록한 뒤 SpotNode 존재 여부를 따로 검증하는 정책 | SpotNode builder 안 등록으로 대체 |

남길 표면은 SpotNode 를 선언하는 builder 아래의 등록 API 다. C++ 의 현재 `add_actor_factory(...)` 표면을
기준으로 다른 언어를 맞춘다.

```csharp
options.AddSpotMesh("rooms")
    .SetRoutingId(nodeRid)                      // SPOT node 대표 RID 를 설정한다.
    .EnableRouter(spotRouterEndpoint)           // actor create/join 요청을 받을 router 를 켠다.
    .EnablePubSub(spotPubEndpoint)              // spot event pub/sub socket 을 켠다.
    .AddEntrySpot<BingoEntrySpot>()             // actor 생성 요청을 직렬 처리할 Entry Spot 을 등록한다.
    .AddActorFactory<PlayerActorFactory>("player") // 이 SpotNode 가 생성할 actor type 을 등록한다.
    .AddSpotFactory<BingoRoom>();               // actor 가 입장할 user spot 타입을 등록한다.
```

중복 actor type 검사는 SpotNode 단위로 수행한다. 하나의 process 에 actor factory 를 가진 SpotNode 가 여러 개
있을 수 있으므로, actor 생성 API 가 기본 대상 SpotNode 를 고를 수 없는 경우에는 설정 오류로 막는다. 기본
대상은 actor factory 를 가진 SpotNode 가 정확히 하나일 때만 자동으로 선택한다. 여러 actor-capable SpotNode 를
이 작업에서는 target SpotNode 를 명시하는 새 actor 생성 API 를 추가하지 않는다. actor factory 를 가진
SpotNode 가 둘 이상이면 startup validation 에서 실패한다. 임의의 첫 번째 SpotNode 로 보내는 동작은 만들지
않는다.

## message flow tracing 확장

### 현재 출력

현재 trace line 은 다음 필드를 중심으로 출력한다.

```text
message flow phase=Sent surface=Channel kind=Request node=api packet=...
```

현재 `node=` 는 사용자가 `TraceNodeId(...)` 로 설정한 framework 논리 label 이다. socket RID 가 아니고,
어떤 runtime 단위를 node 라고 부르는지도 분명하지 않다. `src=` 는 수신 source RID, envelope source, route
target RID 등 호출 위치별 의미가 섞여 있다.
따라서 `src=` 를 local socket RID 로 재사용하지 않는다.

`TraceNodeId(...)` 는 제거하고 `TraceLabel(...)` 로 바꾼다. 이 값은 routing identity 가 아니라 사람이
지정한 process/service label 이다. 출력 key 도 `node=` 가 아니라 `label=` 을 사용한다. `localRid=` 는 실제
socket identity 를 나타낸다. 두 값은 목적이 다르므로 trace 에 함께 둘 수 있지만, 문서와 예제에서는 이
차이를 분명히 설명한다.

### 추가할 필드

`ZLinkMessageFlowEvent` 에 아래 필드를 추가한다.

| 필드 | 의미 |
|------|------|
| `Outcome` | `received`, `dispatched`, `replied`, `sent`, `reply-received`, `dropped`, `error` 같은 결과 구분 |
| `LocalRid` | 이 trace event 를 만든 local socket 의 RID |
| `PeerRid` | 알고 있는 경우 remote peer RID 또는 route target RID |
| `SocketRole` | `router`, `dealer`, `pub`, `sub`, `spot-router`, `spot-pub`, `spot-sub` 등 framework 내부 역할명 |
| `ErrorReason` | 미등록 메시지, payload decode 실패, handler 예외 같은 오류 이유. 오류가 아니면 비운다. |
| `ErrorAction` | error reply 를 보냈는지 drop 했는지 나타낸다. 오류가 아니면 비운다. |
| `ErrorType` | handler 예외나 decode 예외의 type 이름. 오류가 아니면 비운다. |
| `ErrorMessage` | handler 예외나 decode 예외의 짧은 message. 오류가 아니면 비운다. |

기존 `Phase` 는 제거하고 `Outcome` 으로 대체한다. `Outcome` 은 성공 transition 과 오류 결과를 모두 담는다.
예를 들어 기존 `Sent`, `Received`, `Dropped` phase 는 각각 `sent`, `received`, `dropped` outcome 으로 옮긴다.
미등록 메시지나 handler 실패는 `outcome=error` 와 오류 필드로 남긴다.
message flow observer 에 실제 exception 객체를 전달하지 않는다. 언어별 exception type 이 다르고 observer 실패
격리 정책이 복잡해지기 때문이다. 오류 원인은 `ErrorReason`, `ErrorAction`, `ErrorType`, `ErrorMessage` 로
전달한다.

로그 출력은 key 이름을 짧게 둔다.

```text
message flow outcome=sent surface=Channel kind=Request label=api channel=api localRid=... peerRid=... socket=dealer
message flow outcome=received surface=Channel kind=Request label=play channel=api localRid=... src=... socket=router
```

`SourceRid` 는 remote source 의미가 분명한 receive path 에만 사용한다. send path 에 target RID 를
`SourceRid` 로 넣던 기존 사용은 제거하고 `PeerRid` 로 옮긴다. 호환성을 위해 의미가 섞인 필드를 유지하지
않는다.

| 상황 | `SourceRid` | `LocalRid` | `PeerRid` |
|------|-------------|------------|-----------|
| client-server send | 없음 또는 envelope source | client DEALER RID | 선택된 provider RID. 알 수 없으면 null |
| client-server receive | remote DEALER RID 또는 envelope source | server ROUTER RID | remote DEALER RID |
| route mesh send | 없음 | local ROUTER RID | target node RID |
| route mesh receive | source node RID | local ROUTER RID | source node RID |
| pub/sub publish | 없음 또는 publisher source | PUB RID | 없음 |
| pub/sub receive | publisher source | SUB RID | publisher RID. 알 수 없으면 null |

`PeerRid` 를 알 수 없는 send path 에서는 null 로 둔다. 추정을 위해 별도 lookup 을 수행하지 않는다.

### local RID 조회 방식

framework runtime 이 socket 을 만들 때 적용한 RID 를 registration/runtime state 에 저장한다. trace 마다
native `get_routing_id` 를 호출하지 않는다. 이유는 trace hot path 비용을 줄이고, off mode 에서는 기존처럼
거의 비용이 없어야 하기 때문이다.

권장 구조:

```text
ChannelRuntime
  channel name
  socket role
  local rid
  backend socket
```

trace event 를 만들기 전에는 반드시 `Enabled(outcome)` 을 먼저 확인한다. event 객체 생성과 RID
문자열 변환은 tracing 이 켜졌을 때만 수행한다.

### dispatch error observer 제거

미등록 메시지, payload decode 실패, handler 예외 같은 dispatch 실패는 별도 observer 로 분리하지 않는다.
framework 는 같은 message flow tracer 로 오류 결과를 남긴다. 사용자는 `MessageFlow(...)`, `TraceLogFile(...)`,
`TraceLabel(...)` 만 설정하면 성공 경로와 실패 경로를 같은 파일이나 logger 에서 correlation id 로 따라갈 수
있다.

제거 대상:

| 기존 표면 | 처리 |
|-----------|------|
| `.NET` `SetMessageDispatchErrorObserver(...)` | 제거 |
| Java/Kotlin `setMessageDispatchErrorObserver(...)` | 제거 |
| Node/NestJS `setMessageDispatchErrorObserver(...)` | 제거 |
| C++ `message_dispatch_error_observer_t` 계열 public 설정 | 제거 |
| 샘플의 `*DispatchErrorObserver` 클래스 | 제거 |

message flow observer 는 유지한다. 다만 이 observer 는 성공 transition 과 오류 transition 을 모두 받는다.
별도 error observer 를 두지 않으므로, 오류를 외부 metrics 나 audit sink 로 보내려는 사용자는 message flow
observer 에서 `Outcome=error` 또는 `ErrorReason` 이 있는 event 만 골라 처리한다.

## 모든 언어 공통 적용 계획

아래 정책은 `.NET`, C++, Java, Kotlin, Node 에 동일하게 적용한다. 언어별 문법만 다르고 공개 의미는
같아야 한다.

| 항목 | 공통 규칙 |
|------|-----------|
| RID 설정 API | channel builder 당 하나의 `SetRoutingId` 계열 API 만 제공 |
| 기존 역할별 RID API | 제거 |
| client-server server | 대표 RID 를 ROUTER 에 그대로 적용 |
| client-server client | 대표 RID 에서 파생한 DEALER RID 적용 |
| route mesh | 대표 RID 를 ROUTER 에 그대로 적용 |
| fanout | 대표 RID 에서 PUB/SUB 파생 RID 적용 |
| SPOT mesh/node | 대표 RID 를 router/node 에 그대로 적용하고 pub/sub 는 파생 RID 적용 |
| RID 미설정 | `SetRoutingId` 를 호출하지 않으면 socket 별 RID 를 자동 생성 |
| actor factory 등록 | SpotMesh/SpotNode builder 아래에서만 등록 |
| actor factory 전역 API | 제거 |
| 여러 actor-capable SpotNode | target API 를 새로 만들지 않는다. actor factory 를 가진 SpotNode 가 둘 이상이면 startup validation 실패 |
| dispatch error observer | 제거하고 message flow event 의 오류 결과로 통합 |
| tracing event | `Outcome`, `LocalRid`, `PeerRid`, `SocketRole`, 오류 필드 또는 언어별 동일 의미 필드 제공 |
| trace label | `TraceNodeId` API 는 제거하고 `TraceLabel` API 로 교체한다. `label=` 은 사람이 지정한 label 이다. |
| tracing formatter | `label=`, `localRid=`, `peerRid=`, `socket=` 출력 |
| old API migration | source compatibility 없이 새 API 로 수정 |

### .NET

1. `IZLinkClientServerChannelBuilder`, fanout builder, SPOT builder 에 `SetRoutingId(RoutingId rid)` 를
   둔다. route mesh 의 기존 `SetRoutingId` 는 같은 의미로 유지한다.
2. `IZLinkRouteConfig.RoutingId`, `IZLinkOutboundRouteConfig.RoutingId`, SPOT router/pubsub 개별 RID helper
   같은 역할별 public RID 설정 표면은 제거한다. routing config 에 다른 옵션이 필요하면 RID 없는 새
   config 모양으로 유지한다.
3. client-server registration 에 channel 대표 RID 를 optional 값으로 하나만 저장한다.
4. 대표 RID 가 있으면 server ROUTER 에는 대표 RID 를 그대로 적용하고 client DEALER 에는
   `DeriveRoutingId(rid, "dealer")` 를 적용한다. 대표 RID 가 없으면 socket 별 core 기본 RID 자동 생성을
   사용한다.
5. fanout PUB/SUB 와 SPOT pub/sub 에 파생 RID 를 적용할 수 있도록 backend socket abstraction 을 확장한다.
   framework 는 binding public API 만 사용한다.
6. `ZLinkMessageFlowEvent` 의 positional record 모양은 호환성을 고려하지 않고 새 필드가 포함된 새 모양으로
   바꾼다.
7. `IZLinkDispatchOptions.TraceNodeId(string)` 를 제거하고 `TraceLabel(string)` 을 둔다. `ZLinkTraceFormat` 과 logger message template 에는
   `label`, `localRid`, `peerRid`, `socket` 을 출력한다.
8. `IZLinkDispatchOptions.SetMessageDispatchErrorObserver(...)` 와
   `IZLinkMessageDispatchErrorObserver` public surface 를 제거한다.
9. 기존 `ZLinkMessageDispatchErrorEvent` 정보는 message flow error event 로 흡수한다.
10. `IZLinkFrameworkOptions.AddActorFactory<TFactory>(...)` 를 제거하고 `IZLinkSpotNodeBuilder` 또는
   `IZLinkSpotMeshBuilder` 아래에 `AddActorFactory<TFactory>(...)` 를 둔다.
11. `ZLinkFrameworkRegistration.ActorFactories` 전역 저장소를 제거하고 `ZLinkSpotNodeRegistration` 이
   actor factory map 을 소유하게 한다.
12. actor manager/runtime 은 actor factory 를 가진 SpotNode 가 정확히 하나일 때만 기본 actor target 으로
    사용한다. 둘 이상이면 startup validation 에서 설정 오류로 막는다.
13. `.NET` Bingo sample 과 unit/e2e test 를 새 단일 API, message flow error 통합, SpotNode 소유 actor
    factory 정책으로 수정한다.

### C++

1. 모든 channel builder 에 `set_routing_id(routing_id)` 를 하나만 둔다.
2. role 별 RID setter 나 config field 를 제거한다.
3. client-server server socket 에 대표 RID, client socket 에 파생 DEALER RID 를 적용한다.
4. fanout PUB/SUB 와 SPOT pub/sub 에 파생 RID 를 적용한다.
5. 기존 trace node id 설정 API 를 제거하고 `trace_label(std::string label)` 을 둔다.
6. trace event struct 와 logger field 에 `label`, `local_rid`, `peer_rid`, `socket_role` 을 포함한다. `label` 은
   사람이 지정한 label 이고 routing id 가 아니다.
7. dispatch error observer public 설정을 제거하고 message flow error event 로 통합한다.
8. C++ 의 기존 SpotNode builder `add_actor_factory(...)` 를 기준 표면으로 유지한다.
9. 전역 actor factory 등록 표면을 제거한다.
10. C++ sample, parity test, e2e test 를 새 단일 API 로 수정한다.

### Java

1. Spring Boot builder 에 `setRoutingId(RoutingId rid)` 를 하나만 둔다.
2. role 별 RID setter 나 config field 를 제거한다.
3. client-server server ROUTER 에 대표 RID, client DEALER 에 파생 RID 를 적용한다.
4. fanout PUB/SUB 와 SPOT pub/sub 에 파생 RID 를 적용한다.
5. 기존 trace node id 설정 API 를 제거하고 `traceLabel(String label)` 을 둔다.
6. message flow event record 는 호환성을 고려하지 않고 `label`, `outcome`, `localRid`, `peerRid`, `socketRole`,
   오류 필드를 포함하는 새 모양으로 바꾼다. `label` 은 사람이 지정한 label 이고 routing id 가 아니다.
7. `setMessageDispatchErrorObserver(...)` 를 제거하고 dispatch error 는 message flow error event 로 통합한다.
8. `ZLinkFrameworkOptions.addActorFactory(...)` 를 제거하고 `ZLinkSpotMeshBuilder` 또는 SpotNode builder
   아래에 `addActorFactory(...)` 를 둔다.
9. registration/runtime 은 actor factory 를 SpotNode 단위로 저장하고 기본 actor target 이 모호하면 실패한다.
10. Java sample 과 e2e test 를 새 단일 API 로 수정한다.

### Kotlin

1. Kotlin DSL 에도 `setRoutingId(...)` 단일 RID 설정 API 를 둔다.
2. Java builder 와 다른 의미의 RID API 를 만들지 않는다.
3. role 별 RID 설정 DSL 을 제거한다.
4. Kotlin sample 은 Java 와 같은 대표 RID/파생 RID 정책으로 수정한다.
5. Kotlin DSL 의 dispatch error observer 설정을 제거하고 message flow error event 로 통합한다.
6. Kotlin DSL 의 기존 trace node id 설정을 제거하고 `traceLabel(label: String)` 을 둔다.
7. Kotlin message flow event 는 Java 와 같은 `label`, `outcome`, `localRid`, `peerRid`, `socketRole`, 오류
   필드를 제공한다.
8. Kotlin 사용 예제의 actor factory 등록도 `addSpotMesh { ... }` 내부 DSL 로 옮긴다.
9. Kotlin guide/spec 문서는 Java 하위 문서가 아니라 Kotlin 사용자가 바로 읽을 수 있는 DSL 예제로 갱신한다.

### Node

1. framework builder 와 NestJS builder 에 단일 `setRoutingId(...)` API 를 둔다.
2. role 별 RID option field 를 public type 에서 제거한다.
3. internal option state 에 대표 RID 와 파생 RID 를 저장하되 public `.d.ts` 에 내부 필드를 노출하지 않는다.
4. client-server server ROUTER 와 client DEALER 파생 RID 적용을 runtime host 에 연결한다.
5. fanout PUB/SUB 와 SPOT pub/sub 에 파생 RID 를 적용한다.
6. 기존 trace node id 설정 API 를 제거하고 `traceLabel(label: string)` 을 둔다.
7. message flow event object 와 log formatter 에 `label`, `outcome`, `localRid`, `peerRid`, `socketRole`, 오류
   필드를 추가한다. `label` 은 사람이 지정한 label 이고 routing id 가 아니다.
8. dispatch error observer 설정을 제거하고 dispatch error 는 message flow error event 로 통합한다.
9. NestJS 전역 `.actorFactory(...)` 를 제거하고 SpotMesh builder 내부의 actor factory 등록으로 옮긴다.
10. actor factory provider 는 NestJS DI provider 로 유지하되, 해당 factory 를 소유하는 SpotNode registration 에
   연결한다.
11. Node/NestJS sample 과 contract test 를 새 단일 API 로 수정한다.

## 문서 반영 계획

구현 전에는 이 plan 문서만 유지한다. 구현 뒤 아래 문서에 나누어 반영한다.

| 문서 위치 | 반영 내용 |
|-----------|-----------|
| `framework/doc/framework/dotnet/spec/` | `.NET` builder 표면, RID 적용 규칙, tracing event 필드 |
| `framework/doc/framework/cpp/spec/` | C++ builder 표면, trace hook payload, logging field |
| `framework/doc/framework/java/spec/` | Java/Spring builder 표면, tracing event 필드 |
| `framework/doc/framework/kotlin/` | Kotlin DSL 예제와 단일 RID API 설명 |
| `framework/doc/framework/node/spec/` | Node/NestJS builder 표면, public type 노출 기준, tracing event 필드 |
| 각 언어 actor/session 문서 | actor factory 는 SpotMesh/SpotNode builder 아래에서 등록한다는 정책 |
| 각 언어 guide monitoring 문서 | `label` 과 `localRid`, `peerRid`, `socket` 로그 예시 |
| 각 언어 dispatch error observer 문서 | 별도 observer 설명을 제거하고 message flow error event 로 대체 |
| 각 언어 channel messaging 문서 | channel 종류별 대표 RID 와 파생 RID 설명 |
| internals behavior matrix | channel type 별 RID 적용 matrix 와 actor factory 소유 SpotNode 선택 규칙 |

정식 spec 문서는 구현과 테스트가 끝난 뒤에만 수정한다. 구현 전 상태에서 정식 spec 에 새 공개 계약처럼
쓰지 않는다.

## 테스트 계획

### core 회귀 테스트

core 동작은 이미 모든 socket RID 와 ROUTER peer key 모델을 제공한다. 1차 구현은 core 변경 없이
framework 정책으로 처리한다. 다만 아래 동작은 regression 으로 고정한다.

| 테스트 | 목적 |
|--------|------|
| DEALER RID 가 ROUTER receive source RID 로 보인다 | DEALER RID 가 debug only 가 아니라 peer key 로도 쓰임을 확인 |
| ROUTER own RID 와 DEALER peer RID 가 다를 때 request/reply 정상 | client-server 파생 RID 정책의 기본 동작 확인 |
| 동일 RID DEALER 두 개가 ROUTER 중복 정책을 탄다 | client DEALER 에 대표 RID 를 그대로 쓰면 위험하다는 근거 고정 |

### framework 단위 테스트

| 테스트 | 검증 |
|--------|------|
| client-server RID 미설정 | server ROUTER 와 client DEALER 는 socket 별 자동 생성 RID 사용 |
| client-server `SetRoutingId(rid)` | server ROUTER 는 `rid`, client DEALER 는 `rid + suffix` |
| route mesh `SetRoutingId(rid)` | ROUTER 는 `rid` 그대로 |
| fanout RID 미설정 | PUB/SUB 는 socket 별 자동 생성 RID 사용 |
| fanout RID 설정 | PUB 은 `rid.pub`, SUB 는 `rid.sub` |
| SPOT node RID 미설정 | router/pub/sub 는 socket 별 자동 생성 RID 사용 |
| SPOT node RID 설정 | router 는 `rid`, pub/sub 는 파생 RID |
| local server messaging | 같은 인스턴스 client 가 자기 server 로 request/reply 가능 |
| discovery self-filter | server provider self-filter 는 유지되고, client DEALER 파생 RID 로 인해 provider RID 와 충돌하지 않음 |
| tracing event | `label`, `localRid`, `peerRid`, `socketRole` 이 mode on 일 때 출력됨 |
| trace identity meaning | `label` 은 사람이 지정한 값이고 `localRid` 는 socket identity 임을 formatter 와 문서가 구분 |
| dispatch error tracing | 미등록 메시지와 handler 실패가 message flow error event 로 남음 |
| dispatch error observer removal | 별도 dispatch error observer public API 가 사라짐 |
| tracing off mode | event 생성과 RID 문자열 변환이 off mode 에서 발생하지 않음 |
| actor factory registration | SpotMesh/SpotNode builder 아래에서 등록되고 전역 등록 API 는 사라짐 |
| duplicate actor type | 같은 SpotNode 안의 중복 actor type 은 설정 오류 |
| multiple actor-capable SpotNodes | target 이 모호한 기본 create/get-or-create 호출은 설정 오류 |

### 샘플과 e2e

| 영역 | 수정 |
|------|------|
| Bingo .NET | API server channel, Play route mesh, SPOT node 에 대표 RID 설정을 적용 |
| Bingo C++ | API server channel, Play route mesh, SPOT node 에 대표 RID 설정을 적용 |
| Bingo Java | API server channel, Play route mesh, SPOT node 에 대표 RID 설정을 적용 |
| Bingo Kotlin | API server channel, Play route mesh, SPOT node 에 대표 RID 설정을 적용 |
| Bingo Node | API server channel, Play route mesh, SPOT node 에 대표 RID 설정을 적용 |
| trace label sample code | `TraceNodeId("api"|"play"|"session")` 를 `TraceLabel("api"|"play"|"session")` 호출로 교체 |
| actor sample code | actor factory 등록을 전역 옵션에서 SpotMesh/SpotNode builder 아래로 이동 |
| dispatch error sample code | `*DispatchErrorObserver` 클래스와 등록 호출 제거 |
| sample self-check | message flow log 에 `label` 과 `localRid` 가 함께 포함되는지 확인 |
| sample error self-check | 미등록 메시지 수신 시 message flow log 에 error outcome 과 reason 이 남는지 확인 |
| framework e2e monitoring | 새 trace 필드가 message flow observer 와 file logger 양쪽에 전달되는지 확인 |
| route/client-server e2e | 파생 RID 로 인해 request/reply, discovery, local messaging 이 깨지지 않는지 확인 |
| actor create e2e | actor 생성 요청이 factory 를 소유한 SpotNode/EntrySpot 에서 처리되는지 확인 |

샘플 적용 순서는 아래처럼 잡는다.

1. 각 언어 Bingo sample 의 dispatch 설정에서 dispatch error observer 등록을 제거한다.
2. 각 언어 Bingo sample 의 trace label 설정은 `TraceLabel` API 로 바꾼다.
3. 각 언어 Bingo sample 의 channel/spot RID 설정은 channel 별 단일 `SetRoutingId` 계열 API 만 사용한다.
4. 각 언어 Bingo sample 의 actor factory 등록은 SpotMesh/SpotNode builder 아래로 옮긴다.
5. sample self-check 는 trace 파일에서 `label=`, `localRid=`, `socket=`, 오류 `outcome=error` 를 검증한다.

## Breaking change 정책

이 작업은 호환성을 유지하지 않는다. 기존 RID 설정 API 를 보존하거나 deprecated wrapper 로 남기지 않는다.
사용자는 새 channel 별 단일 RID API 로 코드를 고쳐야 한다.

제거 원칙:

| 제거 대상 | 이유 |
|-----------|------|
| role 별 RID config 속성 | channel 대표 RID 와 client peer RID 의미가 섞인다. |
| client DEALER RID 직접 설정 API | ROUTER peer key 충돌을 유발할 수 있다. |
| SPOT router/pubsub 개별 RID helper | SPOT node 대표 RID 정책과 중복된다. |
| 전역 actor factory 등록 API | actor type 과 SpotNode 소유권이 분리되어 여러 SpotNode 구성에서 모호하다. |
| dispatch error observer API | message flow error event 와 중복되고 샘플 사용 패턴을 불필요하게 복잡하게 만든다. |
| `TraceNodeId` API 와 `node=` trace key | node 의 의미가 모호하다. 사람이 지정하는 진단용 값은 `TraceLabel` 과 `label=` 로 표현한다. |
| send path 에 target RID 를 `SourceRid` 로 넣는 trace | source 의미가 흐려진다. `PeerRid` 로 옮긴다. |

message flow event 는 source/binary break 를 허용하고 새 모양으로 정리한다. 모든 언어는 동일한 필드 의미를
제공해야 한다.

## 구현 전 확인 사항

아래 항목은 설계 선택지가 아니라 구현 시작 전에 확인해야 하는 작업 목록이다. 확인 결과 필요한 binding API 가
없으면 framework 에서 우회하지 말고 binding public API 를 먼저 추가한다.

1. 파생 RID suffix byte 규칙은 `base bytes + 0x00 + suffix` 로 구현한다.
2. fanout PUB/SUB, SPOT pub/sub 가 모든 binding 에서 RID set/get public API 를 지원하는지 확인한다.
3. 지원하지 않는 언어 binding 이 있으면 해당 binding public API 를 추가한 뒤 framework 에 연결한다.
4. `PeerRid` 를 send path 에서 알 수 없으면 null 로 둔다. Peer RID 를 만들기 위해 추가 lookup 을 수행하지 않는다.
5. `SocketRole` 문자열 집합은 `router`, `dealer`, `pub`, `sub`, `spot-router`, `spot-pub`, `spot-sub` 로 고정한다.
6. actor factory 를 가진 SpotNode 가 둘 이상이면 startup validation 에서 실패한다. 이 작업에서 target SpotNode
   를 명시하는 새 actor 생성 API 는 만들지 않는다.
7. message flow observer 의 error event 는 exception 객체를 전달하지 않는다. `ErrorReason`, `ErrorAction`,
   `ErrorType`, `ErrorMessage` 필드만 전달한다.

## 완료 기준

- 모든 언어 framework 에서 channel 대표 RID 정책이 동일하게 적용된다.
- `.NET`, C++, Java, Kotlin, Node 모두 기존 role 별 RID 설정 API 를 제거한다.
- `.NET`, Java, Kotlin, Node 의 전역 actor factory 등록 API 를 제거하고 C++ 와 같은 SpotMesh/SpotNode 하위
  등록 표면으로 맞춘다.
- channel 별 RID 설정 public API 는 하나만 남는다.
- RID 를 설정하지 않은 channel 은 socket 별 자동 생성 RID 를 사용한다.
- 별도 dispatch error observer public API 는 제거되고, dispatch 실패는 message flow error event 로 남는다.
- client-server client DEALER 는 대표 RID 를 그대로 쓰지 않는다.
- message flow trace 에 사람이 지정한 `label`, local socket RID, socket role 이 출력된다.
- tracing off mode 에서 기존 zero-cost 원칙이 유지된다.
- Bingo 와 framework e2e 가 새 RID 정책, tracing 필드, dispatch error 통합, SpotNode 소유 actor factory 등록을
  검증한다.
- 정식 spec/guide/internals 문서가 구현 결과와 맞게 갱신된다.
