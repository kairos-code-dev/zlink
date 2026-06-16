# ZLink Framework SpotNode 계약 (Node.js / NestJS)

[Node 묶음](../README.ko.md) | [SPOT](./nestjs-spot.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [표면 매핑 정책](../internals/dotnet-to-node-surface-mapping.ko.md)

이 문서는 `Node.js` framework 의 `SpotNode` 설정 중 core route 계약과 직접
맞물리는 부분, 그리고 spot manager 표면(create / get / list / close)을 정리한다.
의미·동작은 `framework/languages/dotnet` 의 정식 계약과 동일하며, 표면만 NestJS /
TypeScript 로 옮긴다. dotnet 표기와 어긋나면 dotnet **코드**가 최종 기준이다.
Spot handler 작성법과 packet dispatch 규칙은 [nestjs-spot.ko.md](./nestjs-spot.ko.md)
를 기준으로 한다.

이 문서대로 구현하면 .NET 버전과 동일한 SpotNode 동작이 나온다.

## SpotNode 등록

dotnet 의 `AddSpotNode(name)` / `AddSpotMesh(channel)` fluent builder 는
NestJS 의 `zlinkFramework()` builder 로 1:1 매핑한다. builder 메서드 한 개가
node builder 메서드 한 개에 대응한다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .addSpotNode('game.node')
      .enableRouter('tcp://0.0.0.0:7401', 'game-node')
      .enablePubSub('tcp://0.0.0.0:7402', 'game-pub')
      .attachChannelClient('price')
      .attachSpotPublisherClient('game.stage')
      .acceptSpotRoutesFromChannel('game.route', ['tcp://10.0.0.21:7501'])
      .configureEntrySpot({ routingId: 'entry' })
      .addEntrySpot(GameEntrySpot)
      .addSpotFactory(StageSpot)
      .addSpotFactory(ZoneSpot)
    .build()
)
```

dotnet builder 메서드와 node builder 메서드의 대응은 다음과 같다.

| dotnet `IZLinkSpotNodeBuilder` 메서드 | node builder 메서드 | 의미 |
|------|------|------|
| `EnableRouter(...)` | `enableRouter(endpoint, routingId?)` | spot router 역할 |
| `EnablePubSub(...)` | `enablePubSub(endpoint, routingId?)` | spot pub/sub 역할 |
| `AttachChannelClient(name)` | `attachChannelClient(name, endpoint?)` | client/server channel client 부착 |
| `AttachSpotPublisherClient(name)` | `attachSpotPublisherClient(name, endpoint?)` | spot publisher client 부착 |
| `AcceptSpotRoutesFromChannel(name)` | `acceptSpotRoutesFromChannel(name, endpoint?)` | router channel route 수신 |
| `ConfigureEntrySpot()` | `configureEntrySpot(...)` | Entry Spot facade 설정 |
| `AddEntrySpot<TEntrySpot>()` | `addEntrySpot(TEntrySpot)` | Entry Spot handler registry 타입 |
| `AddSpotFactory<TSpot>()` | `addSpotFactory(TSpot)` | 이 node 가 만들 수 있는 spot 타입 |

`router` / `pubSub` 키는 역할이 켜지는 것을 의미한다(dotnet `EnableRouter` /
`EnablePubSub` 호출에 해당). 역할 내부 옵션은 다음으로 매핑한다.

| dotnet builder | node builder 메서드 | 비고 |
|------|------|------|
| `EnableRouter(endpoint)` / `EnablePubSub(endpoint)` | `enableRouter(endpoint)` / `enablePubSub(endpoint)` | bind endpoint |
| `SetRouterRoutingId(rid)` / `SetPubSubRoutingId(rid)` | `routerRoutingId(rid)` / `pubSubRoutingId(rid)` 또는 `enable*(endpoint, rid)` | `RoutingId` → 문자열 |
| `ConfigureSocket(s => ...)` | `socket: {...}` | socket 옵션 |
| `ConfigureRouting(r => ...)` | `routing: {...}` | router routing 옵션 |
| `ConfigurePublisher(p => ...)` | `publisher: {...}` | pub/sub 전용 |
| `ConfigureSubscriber(s => ...)` | `subscriber: {...}` | pub/sub 전용 |
| `ConnectRouter(ep)` / `ConnectPubSub(ep)` | `connectRouter(ep)` / `connectPubSub(ep)` 또는 `enable*(..., ep)` | 수동 연결 endpoint |

`.acceptSpotRoutesFromChannel(name)` 은 dotnet `AcceptSpotRoutesFromChannel` 에
대응하며, 수동 endpoint 는 메서드의 endpoint 인자로 옮긴다. 같은
channel 이름을 중복 등록하면 dotnet 과 동일하게 startup 시점에 설정 예외를
던진다(`Duplicate accepted SPOT route channel`).

`.addSpotNode(name)` 은 dotnet `AddSpotMesh(channel).AddNode(name)` 에 대응한다.
Discovery 는 `.useDiscovery().addRegistryEndpoint(...)` 로 등록하고, 각 node 는 같은
SpotNode builder 메서드 집합을 쓴다.

```ts
zlinkFramework()
  .useDiscovery()
    .addRegistryEndpoint('tcp://registry1:5551')
  .addSpotNode('game.node')
    .enableRouter('tcp://0.0.0.0:7401')
```

등록 단계의 타입 충돌은 조용히 덮어쓰지 않는다. 같은 `spotFactories` 타입을
두 번 넣거나 `entrySpotType` 을 두 번 지정하면(dotnet `AddSpotFactory` /
`AddEntrySpot` 중복) startup 시점에 설정 예외를 던진다. 설정 실수를 바로
드러내는 쪽을 기본 규칙으로 둔다.

## SpotManager 표면

dotnet `IZLinkSpotManager` 는 node 에서 `ZLinkSpotManager` provider 로 노출하고,
NestJS DI 로 생성자 주입한다(`ZLinkChannelClient` 등 다른 outbound client 와 동일
규칙, [표면 매핑 §3.3](../internals/dotnet-to-node-surface-mapping.ko.md)). spot
인스턴스는 `SpotNode` 가 생성·소유하고, application 은 manager 로 **생성·조회·
제거**만 한다.

```ts
@Injectable()
export class StageService {
  constructor(private readonly spotManager: ZLinkSpotManager) {}

  async openStage(): Promise<string> {
    const result = await this.spotManager.create(StageSpot);
    return result.spotRid;
  }
}
```

### 메서드

`ValueTask` → `Promise`, camelCase, `RoutingId` → 문자열로 매핑한다.
생성 요청 `Message` 는 Node framework 의 공통 `Message` 로 전달하고,
`CancellationToken` 은 선택 인자 `signal?: AbortSignal` 로 두거나 생략한다.

| dotnet (`IZLinkSpotManager`) | node (`ZLinkSpotManager`) | 반환 |
|------|------|------|
| `CreateAsync<TSpot>(ct)` | `create(spot, signal?)` | `Promise<ZLinkSpotCreateResult>` |
| `CreateAsync<TSpot>(request, ct)` | `create(spot, request, signal?)` | `Promise<ZLinkSpotCreateResult>` |
| `GetOrCreateAsync<TSpot>(spotRid, ct)` | `getOrCreate(spot, spotRid, signal?)` | `Promise<ZLinkSpotCreateResult>` |
| `GetOrCreateAsync<TSpot>(spotRid, request, ct)` | `getOrCreate(spot, spotRid, request, signal?)` | `Promise<ZLinkSpotCreateResult>` |
| `FindAsync(spotRid, ct)` | `find(spotRid, signal?)` | `Promise<ZLinkSpotInfo \| null>` |
| `ListAsync(ct)` | `list(signal?)` | `Promise<readonly ZLinkSpotInfo[]>` |
| `CloseAsync(spotRid, ct)` | `close(spotRid, signal?)` | `Promise<boolean>` |

TypeScript 는 런타임 타입 소거가 있으므로, dotnet 의 generic `TSpot` 은 spot
클래스 생성자를 첫 인자로 넘기는 형태(`create(StageSpot)`)로 표현한다. factory
식별은 등록된 클래스 생성자로 한다([표면 매핑 §4.3](../internals/dotnet-to-node-surface-mapping.ko.md)).

### 반환 타입

dotnet `readonly record struct` 는 불변 객체(interface / type)로 옮긴다.

```ts
interface ZLinkSpotCreateResult {
  readonly spotRid: string;   // RoutingId
  readonly state: ZLinkSpotCreateState;
  readonly reply?: Message;
}

interface ZLinkSpotInfo {
  readonly spotRid: string;   // RoutingId
}
```

### 동작 의미 (dotnet 과 동일)

- **create**: `spot`(=`TSpot`)으로 factory 를 고르고 runtime 이 새 `spotRid` 를
  발급한다. caller 가 넘긴 단일 `Message` 를 spot 의 `onCreate(request, ...)` 에
  한 번 전달한다. payload 없는 `create(spot)` 은 빈 `Message` 를 넘긴 것과 같고,
  `onCreate` 는 빈 `Message` 를 받아 한 번 실행된다.
- **getOrCreate**: 명시적 `spotRid` 가 필요할 때 쓴다. 같은 `spotRid` 의 spot 이
  이미 ready 면 `state: Existing` 을 반환하고, 이번 `request` 는 `onCreate` 로
  전달하지 않는다. initializing 상태면 첫 생성 요청의 `onCreate` 완료를
  기다린다. 기존 entry 의 spot 타입이 요청한 `spot` 과 다르면 같은 logical spot
  을 다른 framework type 으로 해석하려는 시도이므로 `SpotTypeMismatch` 로
  실패한다.
- **find / list**: 운영 코드가 현재 존재하는 logical spot rid 를 확인하는 조회
  표면이다. 결과(`ZLinkSpotInfo`)에는 `spotRid` 만 포함한다. `find` 는 없으면
  `null` 을 반환한다.
- **close**: 등록된 SpotNode 들을 훑어 해당 `spotRid` 를 정상 종료하고, 종료하면
  `true` 를 반환한다. actor 가 남은 user Spot 은 종료하지 않고 `false` 를 반환한다.

반환값은 장기 보관용 spot instance handle 이 아니다. 생성 결과는 `spotRid`, 상태,
선택적 reply `Message` 를 담고, 이후 메시징은 현재 channel publish 또는 attach 된 channel
client 를 통한 send / request 로 푼다. factory resolve, activation, `onCreate`,
`onInitialize` 실패는 `SpotCreateFailed` 계열로 분류한다.

## Entry Spot 설정

Entry Spot 은 Actor 가 생성 직후 머무르는 기본 Spot 이다. Actor 가 user Spot 에서
leave 하면 같은 node 의 Entry Spot 으로 돌아온다. 따라서 Entry Spot 의 routing id
는 Actor remote location 의 `currentSpotRid` 가 될 수 있다.

framework 는 Entry Spot routing id 설정을 `.configureEntrySpot(...)` 으로
제공한다(dotnet `ConfigureEntrySpot(...)`).

```ts
zlinkFramework()
  .addSpotNode('game.node')
    .configureEntrySpot({ routingId: 'entry' })
```

`entrySpot`(=`ConfigureEntrySpot`)은 `entrySpotType`(=`AddEntrySpot<TEntrySpot>()`)
과 별개다. `entrySpotType` 은 Entry Spot 에서 실행할 handler registry 타입을
등록한다. `entrySpot` 은 handler 타입 등록 여부와 관계없이 native Entry Spot
facade 의 설정(`routingId`)을 적용한다.

## 적용 순서

framework 는 Entry Spot routing id 를 native SpotNode 가 bind 되기 전에 적용한다.
core 는 SpotNode bind 이후 Entry Spot rid 변경을 잠그기 때문에 이 순서가
필요하다.

1. backend 어댑터의 `node.entrySpot()` 으로 native Entry Spot facade 를 얻는다.
2. `entrySpot.routingId` 가 설정되어 있으면 `entrySpot.setRoutingId(...)` 를
   호출한다.
3. SpotNode 를 bind 한다.
4. discovery, route channel, publisher 같은 node 역할을 붙인다.
5. Entry Spot activation 을 만든다.
6. Entry Spot dispatch pump 를 붙인다.
7. 이후 Actor 생성과 Actor remote address publish 는 설정된 Entry Spot rid 를
   사용한다.

이 순서는 Actor 가 생성되기 전에 Entry Spot rid 가 정해지도록 하기 위한 것이다.
Actor remote address sync 가 켜져 있으면 Entry Spot 에 있는 Actor 의
`currentSpotRid` 는 설정된 Entry Spot rid 와 같아야 한다.

native facade 호출(`node.entrySpot()`, `setRoutingId(...)`)은 backend 어댑터
내부에서만 일어난다. public surface 에는 바인딩 객체(`SpotNode`)를 노출하지
않는다([backend-dependency-policy](../internals/backend-dependency-policy.ko.md)).

## Route 의미

framework 가 core discovery route 를 노출할 때 Entry Spot 과 user Spot 을
구분한다.

- Actor remote location 의 `currentSpotKind` 가 `Entry` 면 `currentSpotRid` 는
  Entry Spot rid 다.
- Actor remote location 의 `currentSpotKind` 가 `User` 면 `currentSpotRid` 는 user
  Spot rid 다.
- Spot remote address resolver 의 `ZLinkSpotRemoteAddress.spotKind` 도 core
  `resolveSpot()` 결과를 보존한다.

Spot RID route 는 framework 가 관리하는 이름 색인이다. 이 색인은 Spot rid 를
찾는 용도로만 사용한다. owner node rid 와 Spot kind 는 core `resolveSpot(spotRid)`
결과를 source of truth 로 사용한다.

## 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `EntryRoutingTests.EntrySpotRoutingId_IsApplied_ToNativeEntrySpot` | `entrySpot.routingId` 로 지정한 routing id 가 native Entry Spot facade 에 적용되고 Entry Spot activation 의 `spotRid` 로 노출된다. |
| `RegistryRemoteAddressesTests.RegistrySpotRemoteAddresses_Resolves_Created_Spot_By_Rid_And_Removes_Route` | Spot RID route 는 Spot rid 만 찾는 색인으로 쓰고, resolver 가 core `resolveSpot()` 결과의 owner node rid 와 `SpotKind.User` 를 보존한다. |
| `ManagerTests.SpotManager_Create_List_Close_And_Publish_Work_Through_FrameworkRuntime` | `create`, `find`, `list`, `close` 와 scope 정리가 일관되게 동작한다(dotnet `CreateAsync` / `GetAsync`·`FindAsync` / `ListAsync` / `CloseAsync` 동등). |

이름은 dotnet 회귀 테스트와 1:1 로 대응한다. node 구현은 같은 시나리오를 NestJS
test 로 재현한다([regression-test-matrix](../internals/regression-test-matrix.ko.md)).
