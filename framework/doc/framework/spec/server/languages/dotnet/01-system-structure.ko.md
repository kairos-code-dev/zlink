<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md)
<!-- framework-adapter-nav:end -->

[.NET spec 목차](README.ko.md)

# .NET 시스템 구조 — 등록과 부트스트랩

> 이 문서는 **ASP.NET Core 위에서 ZLink framework를 어떻게 구성하는가**를 소유한다. 어떤 등록
> 표면이 무엇을 켜고, 무엇이 무엇에 붙고, 어떤 순서를 지켜야 하는지다.
>
> **기능의 의미와 동작 규칙은 공통 스펙이 소유한다** — [channel-messaging](../../11-channel-messaging.ko.md),
> [spot-messaging](../../20-spot-messaging.ko.md), [spot-node](../../21-spot-node.ko.md),
> [stream-session](../../30-stream-session.ko.md), [actor-model](../../22-actor-model.ko.md),
> [session-actor-dispatch](../../31-session-actor-dispatch.ko.md),
> [runtime-monitoring](../../50-runtime-monitoring.ko.md),
> [location-runtime](../../40-location-runtime.ko.md),
> [channel-topology](../../10-channel-topology.ko.md).
>
> **public 타입과 시그니처는 [handler-interfaces](02-handler-interfaces.ko.md)가 소유한다.**
> **사용 예제와 튜토리얼은 [.NET 가이드](../../../../dotnet/guide/01-overview.ko.md)가 소유한다.**
> client connector는 [stream-connector](../../../stream-connector/languages/dotnet/03-stream-connector.ko.md)가 소유한다.

## 1. 계약 기준

`.NET` framework runtime은 바인딩의 public 표면만 사용한다.

- `SpotNode`의 attach/connect API를 **직접 호출하지 않는다.** public `CreateRouteBridge(...)` /
  `ISpotRouteBridge` 표면으로 channel socket을 bridge에 연결한다.
- **channel socket의 lifecycle은 channel runtime이 소유하고, bridge는 SPOT relay packet만
  분류한다.**
- local `SpotNode` topic plane으로 외부 publish가 필요하면 raw `PUB` attach가 아니라 **public
  publisher handle**을 사용한다.

## 2. 패키지 구조

**`.NET` 배포 산출물은 6개 NuGet package다.** 이 집합은 고정이며, 검증기가 **정확히 이 6개인지**
확인한다(§14).

| package | 역할 | 의존 |
|---|---|---|
| `Zlink.Framework` | framework core — contract, runtime, dispatcher | 없음(core 바인딩만) |
| `Zlink.Framework.AspNetCore` | ASP.NET Core host adapter — `AddZLinkFramework(...)` 등록 표면 | `Zlink.Framework` |
| `Zlink.Framework.Codecs.Protobuf` | Protobuf codec **extension** | `Zlink.Framework` |
| `Zlink.Framework.Codecs.MessagePack` | MessagePack codec **extension** | `Zlink.Framework` |
| `Zlink.Framework.Locations.Redis` | Redis location store **extension** | `Zlink.Framework` |
| `Systems.Zlink.Stream.Connector` | **client** connector — 서버 framework에 의존하지 않는다 | 없음 |

**분리 원칙:**

- **codec 구현을 core에 섞지 않는다.** JSON은 기본 codec이고, Protobuf·MessagePack은 **extension
  package**로 분리한다. 같은 extension을 framework codec registry, HTTP client, stream connector가
  **공유한다**([channel-messaging §6](../../11-channel-messaging.ko.md)).
- **location store 구현도 extension이다.** core는 `IZLinkLocationStore` 계약만 알고, Redis 구현은
  별도 package가 제공한다(§10).
- **connector는 서버 framework package를 참조하지 않는다.** 반대 방향도 같다
  ([stream-connector §1](../../../stream-connector/languages/dotnet/03-stream-connector.ko.md)).
- **host adapter(`AspNetCore`)와 core를 나눈다.** core는 ASP.NET Core에 의존하지 않는다.

## 3. 배포 계획

| package | 배포 채널 | 소비자 |
|---|---|---|
| `Zlink.Framework` · `Zlink.Framework.AspNetCore` | NuGet | 서버 애플리케이션 |
| `Zlink.Framework.Codecs.*` | NuGet | codec이 필요한 서버·client |
| `Zlink.Framework.Locations.Redis` | NuGet | 다중 프로세스 배포 |
| `Systems.Zlink.Stream.Connector` | NuGet | 데스크톱·서버 client, **Unity(네이티브)**, **Godot C#** |

- **Unity(네이티브)와 Godot C#은 전용 package를 두지 않는다.** 위 connector package를 그대로
  사용한다. **웹(WASM) 빌드에는 쓸 수 없다** — TypeScript connector를 사용한다
  ([stream-connector 공통 스펙 §11](../../../stream-connector/32-stream-connector.ko.md)).
- **package 계약은 archive entry, metadata, dependency까지 snapshot으로 고정한다.** 갱신 절차와
  검증은 [handler-interfaces §17](02-handler-interfaces.ko.md)이 소유한다.

**미결정:** Unity 배포 채널(NuGet 직접 소비 vs UPM 패키지 제공).

## 4. 등록 루트

`AddZLinkFramework(...)` 한 번이 다음 셋을 함께 세운다.

| 대상 | 내용 |
|---|---|
| framework 전역 runtime | dispatcher, filter, DI 배선 |
| channel별 runtime | 등록된 역할마다 하나씩 |
| codec registry | framework · HTTP client · stream connector가 공유한다 |

**location store는 역할별 builder 아래에 중복으로 두지 않는다.** 등록 루트에서
`AddLocationStore(...)`로 **한 번만** 둔다(§10). 반대로 **manual 연결은 역할별 runtime 설정**이므로
역할을 켜는 builder의 endpoint 인자로 둔다.

## 5. Channel 등록

### 5.1 역할

channel 등록은 **소켓 한 쌍을 만드는 일이 아니라 역할을 켜는 일**이다.

| 역할 | 의미 | bind |
|---|---|---|
| `EnableServer()` | 이 channel로 들어오는 request/send를 local handler가 받는다 | **필요** |
| `EnableClient()` | 이 channel 쪽으로 request/send를 내보낸다 | 불필요 |
| `EnablePublisher()` | 이 channel로 event를 publish한다 | **필요** |
| `EnableSubscriber()` | 이 channel의 event를 받는다 | 불필요 |

**outbound만 하는 앱은 server 역할 없이 `EnableClient()`만 켠 channel로 시작할 수 있다.**

### 5.2 자동 연결과 수동 연결

규칙은 [channel-topology §5](../../10-channel-topology.ko.md)가 소유한다. `.NET` 표면에서의 귀결은
다음과 같다.

- **client·subscriber 역할은 location store가 등록되어 있으면 그것을 기본 연결 방식으로 쓴다.**
  역할을 켜는 것만으로 store 기반 자동 연결이 동작한다.
- **endpoint를 명시한 역할은 manual 연결을 사용한다.** 다른 역할의 자동 연결에는 영향을 주지
  않는다.
- **같은 channel의 같은 역할 안에서 두 방식을 섞지 않는다.** 서로 다른 channel끼리 다른 방식을
  고르는 것은 허용한다.
- **manual 연결은 remote routing id를 받지 않는다.** 하부 모델이 "이미 connect된 `DEALER`를
  attach"하는 방식이므로 표면도 **endpoint 집합만** 다룬다.
- **server 역할의 논리 routing id는 server 쪽에서 정한다.** endpoint가 바뀌어도 논리 routing id가
  같으면 store의 peer row 기준으로 **같은 제공자의 새 endpoint로 교체**된다.

### 5.3 handler group

**handler 발견과 channel 노출은 분리된 두 단계다.**

| 단계 | 표면 | 의미 |
|---|---|---|
| 발견 | `AddHandlersFromAssemblyOf(...)` | handler 타입을 DI에 등록하고 attribute scan으로 후보를 찾는다 |
| 노출 | `channel.AddHandlerGroup("...")` | 이 channel이 어느 논리 그룹의 handler를 쓸지 고정한다 |

- **`[ZLinkHandlerGroup("...")]`은 opt-in 표식이다.** 붙이지 않은 handler는 **어느 channel에도
  자동 매핑되지 않는다.**
- **그룹 이름과 channel 이름은 서로 다른 namespace다.** 그룹은 코드 안의 논리 묶음이고, channel은
  실제 배포 식별자다. **같은 그룹을 여러 channel에 매핑할 수 있고, 한 channel에 여러 그룹을 붙일
  수 있다.**
- **handler 코드는 어느 물리 channel로 매핑될지 몰라도 된다.** 배포 시점에 topology가 바뀌어도
  handler는 그대로다.
- **framework는 handler를 직접 생성하지 않는다.** 그룹 매핑만 잡아 두고 실제 객체는
  `IServiceProvider`로 resolve한다. constructor injection이 그대로 동작한다.

**attribute의 책임 분리:**

| 위치 | 책임 |
|---|---|
| 메서드 attribute | packet kind와 packet name override **만** |
| 클래스 attribute(`[ZLinkHandlerGroup]`) | 논리 그룹 소속 **만** |
| channel 등록 | 어느 그룹을 이 channel에 노출할지 |

**channel 이름을 메서드 attribute의 속성으로 두지 않는다.**

### 5.4 dispatch key와 중복 검사 범위

**handler registry는 전역 packet table이 아니다.** 각 channel은 **자기에게 매핑된 그룹과 개별
typed registration 안에서만** packet을 찾는다.

dispatch key = **inbound channel 이름 + message kind + packet name**.
`response`는 client 측 reply correlation 전용이라 **dispatch key 어휘에 두지 않는다.**

- **subscriber channel의 typed event handler를 고르는 키는 topic이 아니라 packet name이다.**
  topic은 publish fan-out 라우팅에 쓰는 값이다.
- **중복 검사 범위는 channel 안으로 제한된다.** 같은 channel에서 같은 `kind + packet name`이 둘
  이상이면 **설정 오류**다(같은 그룹 안의 충돌, 서로 다른 그룹이 한 channel에 붙어 생긴 충돌 모두).
- **다른 channel에서 같은 packet name을 다시 쓰는 것은 허용한다.**

## 6. SPOT 등록

### 6.1 `AddSpotMesh`

**`AddSpotMesh(channelName)` 한 번이 SPOT channel 이름과 그 channel을 소유하는 `SpotNode` 하나를
함께 등록한다.**

- **channel 이름이 곧 local node 이름이다.** route·publish 소유 노드를 따로 고르는 설정이 없다.
- **SPOT mesh channel과 node 등록을 분리해 호출하는 public 경로를 제공하지 않는다.**
- 여러 번 호출하면 channel별 `SpotNode`가 각각 등록된다.
- **location store 없는 local-only 노드도 `AddSpotMesh` 안에서 표현한다.**
- **peer 획득은 이 등록이 소유하지 않는다.** store 자동 연결 또는 manual endpoint가 공급한다.

### 6.2 노드 builder

| 표면 | 켜는 것 |
|---|---|
| `EnableRouter(endpoint)` | local `SpotNode.router` 경로. 같은 channel의 다른 `SpotNode`와 routed packet을 주고받는 축 |
| `EnablePubSub(endpoint)` | 현재 SPOT channel의 publish/subscribe 축. local spot에서 publish하려면 필요하다 |
| `AddEntrySpot<TEntrySpot>()` | 자동 Entry Spot에 붙일 **application registry**. Entry Spot 자체의 native lifecycle은 framework가 관리한다 |
| `AddSpotFactory<TSpot>()` | 이 노드가 생성·소유할 spot factory. **generic spot 타입으로 등록한다**(이름 인자 없음) |

- **channel client는 SpotNode builder가 아니라 `IZLinkFrameworkOptions`에 등록한다.**
  `AddClientServerChannel(...).EnableClient(...)`는 framework 수준 표면이며, SpotNode는 별도
  channel client를 부착하지 않는다.
- **`AddEntrySpot`을 등록하지 않으면 빈 Entry Spot registry를 사용한다.** actor가 Entry Spot에
  머무는 동안 처리할 application handler와 lifecycle callback이 없다는 뜻이다.
- **같은 `SpotNode`에 여러 spot factory를 둘 수 있다.** 생성 시점에 **spot 타입으로** 어떤 factory를
  쓸지 고른다.
- **같은 spot 타입을 다시 등록하면 조용히 덮어쓰지 않고 예외를 던진다.**
- **actor factory를 소유하는 노드는 프로세스에서 하나만 허용한다.** 둘 이상이면 actor 생성 소유자를
  결정할 수 없으므로 설정 오류다. **actor factory가 없는 여러 노드는 허용한다.**

### 6.3 역할별 수동 연결

**수동 연결은 `SpotNode` 전체가 아니라 역할별로 관리한다** — `router`, channel client, `pub/sub`,
spot publisher client가 각자 endpoint 집합을 갖는다.

- **같은 역할 안에서 store 자동 연결과 manual을 섞지 않는다.**
- **`router` manual 연결은 두 형태다.** `ConnectRouter(endpoint)`는 endpoint만 등록한다.
  **location store가 없어 target `ROUTER`의 routing id를 해석할 곳이 없으면**
  `ConnectRouter(RoutingId peerRid, endpoint)`로 peer id를 함께 등록한다.
- **channel client manual 연결도 endpoint 집합만 등록한다.** 하부 `DEALER`가 이미 connect된 peer
  집합을 대상으로 보내기 때문이다.
- **`pub/sub` manual 연결에서 등록하는 주소는 다른 `SpotNode`의 mesh publish bind 주소다.** local
  `SUB/XSUB` 쪽이 그 주소로 연결된다.

### 6.4 Route ingress

규칙은 [spot-messaging §6](../../20-spot-messaging.ko.md)이 소유한다. `.NET`에서는:

- `node.EnableRouter(endpoint)`로 ingress를 켠다.
- 수동 endpoint는 `AddRouteMeshChannel(...)` builder의 `EnableServer(endpoint)`(ingress `ROUTER`)와
  `EnableClient(endpoint)`(outbound peer)로 지정한다.
- egress로 client-server channel의 client `DEALER`를 쓸 수도 있다.
- **target Spot은 string overload 없이 `RoutingId`로 지정한다.**

### 6.5 Entry Spot routing id의 적용 순서

**`ConfigureEntrySpot(...)`은 `AddEntrySpot<TEntrySpot>()`과 별개다.** 전자는 native Entry Spot
facade의 설정을, 후자는 구현 타입을 등록한다.

**Entry Spot routing id는 native SpotNode가 bind되기 전에 적용해야 한다.** core가 bind 이후 변경을
잠그기 때문이다([spot-node §2.1](../../21-spot-node.ko.md)).

1. `ApplyEntrySpotRoutingIdBeforeBind()` — `ConfigureEntrySpot(...)`의 `RoutingId`를 native
   facade(`entrySpot.SetRoutingId(...)`)에 적용한다.
2. `SetRouterBind(...)` / `SetPubBind(...)` — bind endpoint를 설정한다.
3. store 자동 연결, manual peer, accepted spot route channel, publisher를 붙인다.
4. `InitializeEntrySpotAsync()` — activation과 dispatch pump를 포함해 초기화한다.

**순서를 어기면 actor가 잘못된 Entry Spot rid를 갖는다.**

## 7. STREAM 등록

`AddStreamNode(name)` → `Bind(endpoint)` → `RegisterSession<TSession>()`.

- **attribute 기반 암시 등록으로 열지 않는다.** 명시 등록만 기본 표면이다.
- **한 stream node에는 session을 하나만 둔다.**
- **bind endpoint는 반드시 있어야 한다.**
- **header binary 형식은 framework와 connector가 공유하는 내부 프로토콜로 고정된다.** application이
  이 형식을 바꾸는 설정을 갖지 않는다([stream-session §2](../../30-stream-session.ko.md)).

## 8. Session actor dispatch 등록

계약은 [session-actor-dispatch](../../31-session-actor-dispatch.ko.md)가 소유한다. host 등록에서 필요한
것은 둘이다.

| 표면 | 역할 |
|---|---|
| STREAM session relay | **framework registration 안의 router 역할을 켠 SpotNode를 relay ingress로 자동 사용한다**(별도 지정 없음) |
| `IZLinkSpotHandleResolver` | spot rid를 user Spot routing id로 푼다. actor가 node 경계를 넘을 수 있으면 등록한다 |

**이 relay 연결이 있어야 session → actor 경로와 actor → bound session push가 같은 relay 상태를
사용한다.**

## 9. Monitoring 등록

계약은 [runtime-monitoring](../../50-runtime-monitoring.ko.md)이 소유한다. `.NET` 등록 표면은
`IZLinkMonitoringOptions`이며 source별로 나뉜다.

| source | 등록 조건 |
|---|---|
| socket | source 이름이 `<channel>.<capability>` 형식이고 **그 channel 역할이 실제로 등록되어 있어야 한다** |
| location | **polling 주기를 반드시 명시한다.** location runtime이 등록되어 있어야 한다 |
| spot | **등록된 `SpotNode` 이름**을 가리켜야 한다 |

**임의 source 자동 발견은 지원하지 않는다.** 자동 연결 상태는 location runtime source와 runtime
query로 관찰한다.

## 10. Location 등록

| 표면 | 의미 |
|---|---|
| `AddLocationStore(IZLinkLocationStore store)` | **물리 저장소 인스턴스 하나**를 등록한다. 이 인스턴스가 peer·spot·actor·route·owner lease 역할을 **모두** 맡는다. 같은 인스턴스가 `IZLinkLocationChangeStampStore`/`IZLinkLocationWatchStore`도 구현하면 자동 인식된다 |
| `UseInMemoryLocationStores()` | 단일 프로세스 개발·단위 테스트용. **여러 프로세스가 위치를 공유해야 하는 배포에서는 쓰지 않는다** |
| `ConfigureLocations()` → `ZLinkLocationOptions` | `HeartbeatInterval`, `OwnerLeaseTtl`, `PollingInterval`, `ListPageSize`, `StoreFailureGrace`, `MapSpotMeshToRouteChannel(...)` |

- **두 등록은 서로 대체 관계다. 둘 다 등록하면 설정 오류다.**
- **역할별 store를 따로 등록하는 public API는 없다.** owner lease와 위치 row가 **같은 물리
  저장소**에 있어야 오래된 소유자 판정과 위치 갱신을 같은 규칙으로 처리할 수 있다.
- **Spot 위치 row에는 Spot mesh 이름이 저장된다.** route channel 이름이 그와 다르면 시작 전에
  `MapSpotMeshToRouteChannel(...)`로 매핑한다. **매핑하지 않은 Spot mesh는 같은 이름의 route
  channel을 사용한다.** 등록하지 않은 mesh나 channel을 가리키는 매핑은 **시작 검증에서 거부한다.**

공식 Redis store(`ZLinkRedisLocationStore`)의 타입과 옵션은
[handler-interfaces §10.2.1](02-handler-interfaces.ko.md)이 소유한다.

## 11. Host lifecycle

**channel·spot·stream runtime은 host startup 단계에서 등록된 역할을 보고 만들고, host shutdown
단계에서 정리한다. lazy first-call 생성으로 숨기지 않는다** — 설정 오류가 startup에서 드러나도록
하기 위해서다([channel-messaging §2](../../11-channel-messaging.ko.md)).

`.NET`에서는 `IHostedService` 계열의 hosted lifecycle에 연동한다 — runtime 부팅 → store 자동 연결
수립 → handler dispatcher 시작 → 종료 시 graceful shutdown.

**host 종료 중 호출**의 계약은 [channel-messaging §5](../../11-channel-messaging.ko.md)가, 우아한
종료의 수명주기는 [graceful-drain-handoff](../../54-graceful-drain-handoff.ko.md)가 소유한다.

## 12. DI

DI 등록 조건과 public service 노출 기준은 [handler-interfaces §13](02-handler-interfaces.ko.md)이
소유한다.

## 13. Startup validation

검증 항목의 정본은 [channel-messaging §4](../../11-channel-messaging.ko.md)와
[spot-messaging §8](../../20-spot-messaging.ko.md)이 소유한다.

**`.NET`은 모든 위반을 `ZLinkConfigurationException`으로 host 시작 전에 던진다.**

## 14. 회귀 테스트

등록과 startup validation의 회귀 항목은
[regression-test-matrix](../../../../dotnet/internals/regression-test-matrix.ko.md)가 소유한다.
