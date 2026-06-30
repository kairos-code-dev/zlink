<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework NestJS Channel Messaging](nestjs-channel-messaging.ko.md) | [다음: Node.js Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../README.ko.md)

[Node.js 묶음](../README.ko.md) | [인터페이스](handler-interfaces.ko.md) | [Stage wrapper](stage-wrapper-on-spot.ko.md) | [channel](nestjs-channel-messaging.ko.md) | [STREAM](nestjs-stream.ko.md)

# ZLink Framework NestJS SPOT Integration

> 이 문서는 Node.js `ZLink Framework`(NestJS)의 SPOT **스펙**이다. 표면은 NestJS /
> TypeScript 모양이다. 번역 규칙은
> [dotnet-to-node-surface-mapping.ko.md](../internals/dotnet-to-node-surface-mapping.ko.md)
> 를 따른다. 표기가 어긋나면 `framework/languages/node` 코드가 기준이다.

## 현재 구현 기준

`outbound.sendToSpot(...)` / `outbound.requestToSpot(...)` 호출은 명시 route wiring 없이
동작한다. 내부 구현은 core legacy `SpotNode` attach/connect API를 호출하지 않고, `bindings/node`의 public
`createRouteBridge()` / `SpotRouteBridge` 표면으로 channel socket을 bridge에 연결한다.
channel socket은 channel runtime이 계속 소유하며, bridge는 SPOT relay packet만 분류한다.

## 1. 목표

이 절은 `ZLink Framework` 가 `SPOT` 을 `NestJS` 안에서 어떻게 다루려고 하는지,
그 방향을 한 문장으로 정리한다.

`SPOT` 은 zlink 쪽에서 이미 독립된 개념과 runtime 을 갖는다. 즉 `ZLink
Framework` 가 이 개념을 새로 만들거나 없애려는 것이 아니다. 대신 `NestJS`
사용자가 익숙한 모양(module options + decorator + provider lifecycle hook)으로
다룰 수 있도록 감싸는 것이 목적이다.

이 문서가 다루는 축은 다음과 같다.

- `SpotNode` lifecycle 관리
- `Spot` publish / subscribe facade 주입
- Entry Spot application registry 등록
- actor packet handler 등록과 Spot 멤버 lifecycle callback
- room, stage, zone 같은 논리 인스턴스 모델 설명
- 현재 channel publish / subscribe
- route bridge가 참조하는 다른 channel runtime socket을 통한 send / request
- `Discovery` 기반 peer 구성
- background subscriber handler

## 2. 기반이 되는 Node 바인딩

이 절은 framework 가 새로 만든 것이 아니라 기존 바인딩 위에 얹는 작업임을
밝히는 자리다.

현재 하부 토대는 `@zlink-systems/zlink`(Node 바인딩)의 다음 표면이다.

- `Discovery`
- `SpotNode`
- `Spot`
- `Spot` publish / subscribe
- channel route bridge 기반 channel send / request

이 문서의 핵심은 `SPOT` 기능 자체를 새로 만드는 일이 아니다. 이미 존재하는
바인딩 기능을 `NestJS` 안에 자연스럽게 녹여 넣는 방법을 정리하는 것이 목적이다.
새 transport / wire 의미를 만들지 않으므로, 같은 channel 로 붙으면 dotnet SPOT
노드와도 그대로 통신한다.

등록 코드부터 handler, channel send / request, topic publish 까지 한 흐름으로
보는 샘플은 [정본 샘플](../README.ko.md) 에 모아 두었다. 또한
`playhouse` 의 `Stage` 같은 상위 모델을 `SPOT` 위에 다시 감쌀 때 필요한 추가
조건은 [stage-wrapper-on-spot.ko.md](stage-wrapper-on-spot.ko.md) 에서 다룬다.

## 3. SPOT을 무엇으로 보는가

이 절은 `SPOT` 을 어떤 개념으로 읽어야 하는지부터 짚는다. 그 다음에 같은
관점에서 `Spot`, `SpotNode`, channel 의 관계를 한 줄씩 정리한다.

현재 스펙에서 `SPOT` 은 단순한 pub / sub helper 가 아니다. 오히려 **주소 가능한
논리 인스턴스** 로 이해하는 편이 더 정확하다. 대표적인 예는 다음과 같다.

- 게임 room
- playhouse stage
- 채팅 room
- MMORPG zone
- 필요하다면 Redis pub/sub 같은 fan-out 주제 공간

`SPOT` 은 "토픽 시스템" 이 아니라 먼저 "논리 대상 인스턴스" 로 설명되어야
한다. publish / subscribe 는 그 안에서 함께 사용할 수 있는 한 가지 활용 방식일
뿐이다.

이 관점에서 각 요소 사이의 관계를 더 정확히 정리하면 다음과 같다.

- `Spot` 은 특정 service 에 종속되지 않는다.
- `Spot` 은 `SpotNode` 에 종속된다.
- `SpotNode` 는 channel 이름을 직접 소유하지 않는다.
- module options 의 `discovery` 등록이 active channel view 를 공급한다.
- 같은 `SpotNode` 에는 active SPOT channel view 를 하나만 둔다.
- `SpotNode.router` 와 pub/sub mesh 는 같은 channel 에 속한 다른 `SpotNode` 와만
  연결된다.
- 다른 channel 호출은 `SpotNode.router` 가 아니라 channel runtime socket을 참조하는
  route bridge 경로로 처리한다.
- 따라서 `spotRid` 는 service 에서 부여되는 값이 아니라, `SpotNode` 가 spot
  인스턴스를 생성할 때 발급하는 식별자다.

이 관점에서 특히 중요한 점은 다음과 같다.

- 현재 SPOT channel 안에서는 topic publish / subscribe 를 사용한다.
- 다른 channel 호출은 route bridge channel socket을 통해 보낸다.
- `SpotNode.router` 는 peer topology 와 내부 routed delivery 를 위해 남겨 두되,
  framework core 의 public high-level API 에서는 `targetRid + spotRid` 를 직접
  받는 direct routed 호출 표면을 두지 않는다.
- spot rid 를 다른 노드의 user Spot 위치로 변환해야 하면
  `ZLinkSpotRemoteAddressResolver` 를 쓴다. resolver 구현체만 `RoutingId`(string)
  를 알고, application handler 는 spot rid 만 기준으로 호출한다.
- 외부 `PUB -> Spot` 입력은 generic pub/sub attach 가 아니라 별도의 ingress
  표면으로 분리한다.

여기서 경계를 분명히 짚어 두면 다음과 같다.

- `SPOT` 이 제공하는 것은 주소 가능한 논리 인스턴스와 그 인스턴스에 대한
  메시징, publish / subscribe, timer, lifecycle 까지다.
- 반면 room broadcast 정책과 도메인별 권한 모델은 여전히 응용 계층의 책임으로
  남는다.
- 현재 구현에서는 actor join, actor factory 등록, 그리고 stream callback 에서
  session context 로 actor packet / disconnect 를 같은 `SPOT` 실행 문맥에 올리는
  브리지까지를 framework core 범위에 포함한다.

## 4. NestJS 등록 모델

이 절은 실제 `ZLinkModule.forRoot(...)` 등록이 어떤 모양인지부터 한 덩어리로
보여 준 다음, 같은 코드를 한 줄씩 풀어서 설명한다.

dotnet 의 `options.AddSpotMesh("game.stage", mesh => ...)` 람다는 NestJS 의
`zlinkFramework()` builder 호출로 옮긴다. dotnet builder 메서드 한 개가 node builder
메서드 한 개에 1:1 로 대응한다.

Spot 관련 application 객체는 NestJS DI 가 소유한다. `entrySpotType` 과 Spot
factory type 은 module 의 `providers` 에 직접 등록한다. packet handler type,
actor handler type, timer handler type 은 각 handler class 에 decorator 를 붙이고
module 이 `zlinkDiscoverProviders(...)` 로 가져온다. framework 는 SpotNode 가
Entry Spot 을 만들거나 SpotManager 가 user Spot 을 만들 때 NestJS provider
resolver 를 통해 해당 타입을 resolve 한다. provider 로 등록되지 않은 경우에만
NestJS 밖에서 쓰는 저수준 framework 경로가 직접 생성 fallback 을 사용할 수 있다.

node/channel handler group 은 module 설정에서 고른다. 그러나 Spot packet,
subscribe, actor, timer handler 는 `main.ts` 에서 한꺼번에 나열하지 않는다. handler
class 가 decorator 로 자신의 역할을 드러내고, Entry Spot 또는 user Spot 이 자기
registry 에 필요한 handler 를 연결한다. Spot 이 어떤 메시지를 처리하는지는 Spot
자체의 책임이기 때문이다.

```ts
@Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .useDiscovery()
          .addRegistryEndpoint('tcp://registry1:5551')
        .addSpotMesh('stage-node')
          .enableRouter('tcp://0.0.0.0:9001')
          .enablePubSub('tcp://0.0.0.0:9000')
          .addEntrySpot(StageEntrySpot)
          .addSpotFactory(StageSpot)
        .options({ registrySpotRemoteAddresses: { namespace: 'game' } })
        .build()
    ),
  ],
  providers: [
    StageEntrySpot,
    StageSpot,
    ...zlinkDiscoverProviders(path.join(__dirname, 'Handlers')),
  ],
})
export class AppModule {}
```

이 등록 코드의 의미는 다음과 같다.

- 논리 `SpotNode` 이름은 `stage-node`
- 그에 대응하는 backing `SpotNode` 생성
- `discovery` 가 active channel view 공급
- 같은 channel 에 속한 다른 `SpotNode` 와만 mesh 구성
- `router` 로 local routed router 역할 활성화
- `pubSub` 로 local SPOT pub/sub 역할 활성화
- `channelClients` 로 다른 channel 호출용 client attach
- 필요하다면 `spotPublishers` 로 외부 노드용 spot publish client attach
- `entrySpotType` 으로 자동 Entry Spot 에 붙일 application registry 등록
- `spotFactories` 로 이 노드가 생성·소유할 user Spot 클래스 등록
- `registrySpotRemoteAddresses` 로 spot rid 기반 호출 또는 actor join 경로에서 사용할
  Registry 기반 spot remote address resolver 등록
- host shutdown 시 lifecycle 정리

`.addSpotMesh(name)` 은 실행할 `SpotNode` 를 이름으로 등록한다. 여러 `SpotNode` 가
필요하면 builder 에서 여러 번 호출한다. Discovery endpoint 가 없는 로컬
단일 노드도 같은 방식으로 표현한다. 이 경우 `discovery` 를 생략하고 필요한
`SpotNode` 만 등록한다.

각 역할 키의 역할은 다음과 같다.

- `.enableRouter(endpoint)` (dotnet `EnableRouter(endpoint)`)
  - local `SpotNode.router` 경로를 켜고 routed ingress endpoint 를 명시한다.
    같은 channel 에 속한 다른 `SpotNode` 와 routed packet 을 주고받는 축이다.
- `.enablePubSub(endpoint)` (dotnet `EnablePubSub(endpoint)`)
  - 현재 SPOT channel 안의 publish / subscribe 축을 켠다. local spot 안에서
    `context.outbound.publish(...)` 를 쓰려면 이 역할이 필요하다.
- `orders` channel 로 outbound send / request 를 보내려면 top-level
  `addClientServerChannel("orders").enableClient(...)`에서 client 역할을 켠다.
- Spot publisher client
  - `enablePubSub(...)`를 켠 SpotMesh 이름으로 외부 publish client가 노출된다.
- `.addEntrySpot(StageEntrySpot)` (dotnet `AddEntrySpot<StageEntrySpot>()`)
  - 이 노드의 자동 Entry Spot 에 붙일 application registry 를 등록한다.
  - Entry Spot 자체의 native 생성과 소멸은 framework 가 관리한다.
  - 등록하지 않으면 빈 Entry Spot registry 가 사용된다. 이 경우 actor 가 Entry
    Spot 에 머무는 동안 처리할 application actor packet handler 와 lifecycle
    handler 가 없다는 뜻이다.
  - 같은 노드에 Entry Spot 을 두 번 등록하면 startup validation 예외다.
- `.addSpotFactory(StageSpot)` (dotnet `AddSpotFactory<StageSpot>()`)
  - 이 노드가 생성하고 소유할 user Spot **클래스 참조**를 등록한다.
  - 같은 `SpotNode` 에 여러 spot factory 를 둘 수 있고, 생성 시점에는 **Spot
    클래스 자체**를 키로 어떤 factory 를 쓸지 선택한다(§4.5).
  - 이미 등록된 클래스를 다시 등록하면 조용히 덮어쓰지 않고 예외를 던진다.

> **코드 정합성 주의.** dotnet 코드는 factory 를 `spotName` 문자열이 아니라
> **Spot 타입(클래스)** 으로 식별한다(`AddSpotFactory<TSpot>()`,
> `CreateAsync<TSpot>()`, 내부 `GetNodeForSpotFactory(spotType)`). 따라서 node
> 표면도 `.addSpotFactory(StageSpot)` 처럼 클래스 참조를 쓰고, 생성은
> `manager.create(StageSpot)` 처럼 **클래스 참조**로 한다. 초기 node 드래프트가
> 쓰던 `{ spotName, spotType }` 객체 형태와 `spotName` 기준 생성은 코드에 근거가
> 없으므로 채택하지 않는다(§12 divergence 참고).

`SpotNode` 는 더 이상 여러 service surface 를 동시에 소유하는 hub 처럼
설명되지 않는다. 현재 방향에서 그 역할 분담은 다음과 같다.

- `.addSpotMesh(name)` 등록이 노드의 channel 정체성을 닫는다.
- 다른 channel 호출은 별도로 attach 된 client 경로를 통해 푼다.

이 모델에서 중요한 점은 다음과 같다.

- `.addSpotMesh('stage-node')` 가 이 노드의 런타임 범위를 정한다.
- 같은 `SpotNode` 에 active SPOT channel view 는 하나만 둔다.
- `router` 와 `pubSub` 는 별개의 역할이다.
- 다른 channel 에 대한 send / request 는 attach 된 client 가 담당한다.
- 외부 노드에서 SPOT channel 로 publish 하려면 별도의 spot publisher client 를
  쓴다.

### 4.1 Entry Spot과 actor handler 등록

이 소절은 Entry Spot 에서 어떤 handler 를 어디에 등록하는지, 그리고 그 등록을
application 이 직접 손대지 않는 raw 표면과 어떻게 구분하는지 정리한다.

Entry Spot 은 actor 가 생성된 직후 처음 머무르는 기본 실행 문맥이다. 따라서
application 은 raw Entry Spot handle 을 직접 만들거나 보관하지 않는다. 대신
`.addEntrySpot(StageEntrySpot)` 으로 Entry Spot 에서 실행할 actor packet handler 를
등록하고, Entry Spot lifecycle callback 은 Entry Spot 클래스의 멤버 메서드로
정의한다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .addSpotMesh('stage-node')
      .enablePubSub('tcp://0.0.0.0:9000')
      .configureEntrySpot({ routingId: 'entry' })
      .addEntrySpot(StageEntrySpot)
      .addSpotFactory(StageSpot)
    .build()
);
```

`entrySpot`(dotnet `ConfigureEntrySpot(...)`) 는 Entry Spot facade 의
routing id 같은 native 설정을 적용한다. 이 설정은 actor 생성과 route publish
전에 적용되며, `.addEntrySpot(...)` 은 Entry Spot 클래스 타입을 등록한다.

Entry Spot 클래스는 `ZLinkEntrySpot` 을 구현한다. `configure()` 안에서 Entry
단계의 handler 를 등록한다. Entry Spot 과 user Spot 은 등록할 수 있는 기능
표면이 같다. Entry Spot actor packet 은 대상 actor 의 mailbox 에서 처리되며,
같은 actor 의 packet 끼리만 순서가 보장된다. Entry Spot lifecycle callback 은
Entry Spot 자체 실행 문맥에서 처리한다.

```ts
@Injectable()
export class StageEntrySpot implements ZLinkEntrySpot {
  constructor(readonly context: ZLinkEntrySpotContext) {}

  configure(): void {
    this.context.handlers.addPacket(StageAdmissionHandler);
    this.context.handlers.addSubscribe(StageAdmissionEventHandler, 'stage.admission');
    this.context.handlers.addHandler(AuthenticateStageActorHandler);
    this.context.handlers.addHandler(JoinStageHandler);
  }

  async onJoinedActor(actor: StageActor): Promise<void> {
    await this.recordEntryJoin(actor);
  }

  async onLeaveActor(actor: StageActor): Promise<void> {
    await this.recordEntryLeave(actor);
  }
}
```

user Spot 클래스도 같은 방식이다. `ZLinkSpot` 을 구현하고, user Spot 단계의
actor handler 를 등록한다. room, stage, zone 상태를 다루는 packet 은 Entry Spot
이 아니라 이쪽 registry 에 둔다.

Entry Spot 과 user Spot 모두 context 는 생성자에서 주입받아 `context` property
로 그대로 노출한다. framework 는 생성된 spot 이 주입된 context 를 노출하지
않으면(다른 context 면) activation 을 실패시킨다.

```ts
@Injectable()
export class StageSpot implements ZLinkSpot {
  constructor(readonly context: ZLinkSpotContext) {}

  configure(): void {
    this.context.handlers.addHandler(MoveOnStageHandler);
    this.context.handlers.addHandler(ReportStageStateHandler);
  }

  async onActorJoin(actor: StageActor, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const admission = await this.decodeAdmission(request.decode<StageAdmissionReq>());
    return admission.allowed
      ? { accepted: true, reply: admission.reply }
      : { accepted: false, reply: admission.reply };
  }

  async onJoinedActor(actor: StageActor): Promise<void> {
    await this.attachStageActor(actor);
  }

  async onLeaveActor(actor: StageActor): Promise<void> {
    await this.detachStageActor(actor);
  }
}
```

> **표면 주의.** dotnet 코드에서 handler registry 는 `Context.Handlers` property
> (`IZLinkSpotHandlerRegistry`) 에 있다. dotnet spec 예시가 쓰는
> `Context.AddPacket<T>()` 는 그 축약 표기다. node 기준 표면은
> `context.handlers.addPacket(...)` 로 둔다.

Entry Spot registry 와 user Spot registry 는 서로 다른 namespace 다. 따라서
같은 actor 타입과 packet 이름이라도, Entry 단계와 user Spot 단계에서 서로 다른
handler 로 매핑할 수 있다.

반대로 같은 registry 안에서 같은 `actor type + packet kind + packet name`
조합을 둘 이상 등록하면 startup validation 오류가 된다. `addHandler(...)` 로
등록한 actor disconnected callback handler 역시 같은 registry 안에서 같은 actor
타입에 대해 하나씩만 허용한다.

join / leave lifecycle 은 Spot 멤버 callback 으로 정의한다. user Spot 과
Entry Spot 은 `onActorJoin(actor, request)` 로 admission 을 결정하고, accept 된
뒤에만 `onJoinedActor(actor)` 를 실행한다. actor 생성 직후의 기본 Entry Spot
membership 은 join 요청이 아니므로 `onActorJoin` 을 실행하지 않는다.

handler registry 표면(`context.handlers`) 의 메서드는 다음과 같다. dotnet
`IZLinkSpotHandlerRegistry` 와 1:1 대응한다.

| node 메서드 | dotnet | 의미 |
| --- | --- | --- |
| `addPacket(Handler)` | `AddPacket<THandler>()` | spot packet / request handler |
| `addSubscribe(Handler, topic)` | `AddSubscribe<THandler>(topic)` | topic subscription handler |
| `addHandler(Handler)` | `AddHandler<THandler>()` | spot-local handler |
| `addHandler(Handler, packetName)` | `AddHandler<THandler>(packetName)` | 이름을 명시한 spot-local handler |
| `onDisconnectActor(actor)` | `onDisconnectActor(...)` | disconnect 후 callback |

### 4.2 SPOT 실행 queue와 actor mailbox

이 소절은 "같은 user Spot 안의 callback 은 왜 한 줄로 실행되는가" 와 "Entry Spot
callback 도 왜 같은 실행 줄을 쓰는가" 두 질문을 묶어서 정리한다.

user Spot 은 room, game, stage 같은 하나의 상태 객체로 본다. 따라서 user Spot
안에서 실행되는 callback 은 같은 Spot 실행 queue 에서 순서대로 처리한다.
dotnet 의 `ZLinkSpotSerialExecutor`(직렬 실행 큐) 에 대응한다. 여기에 포함되는
것은 다음과 같다.

- Spot packet, Spot request
- subscription, timer
- actor join
- user Spot 에 머무는 actor 에게 전달되는 packet

이 규칙 덕분에, 같은 user Spot 안의 `actor A` 와 `actor B` 가 모두 같은 게임판
상태를 바꾸더라도 두 handler 가 동시에 실행되지 않는다. 즉 application 은 user
Spot 인스턴스의 상태를 일일이 별도 lock 으로 보호하지 않아도 된다.

Entry Spot 은 특정 room 상태를 소유하는 곳이 아니라, 모든 actor 가 처음 거쳐 가는
공용 입구다. Entry Spot lifecycle callback 은 같은 Entry Spot 실행 줄에서 직렬로
실행된다. Entry Spot actor packet 은 Entry Spot 전체 실행 줄에 세우지 않고 대상
actor 의 mailbox 로 보낸다. 서로 다른 actor 의 Entry Spot actor packet 은 한 Entry
Spot 실행 줄 때문에 서로 기다리지 않는다.

정리하면 다음과 같다.

| 대상 | 실행 줄 |
| --- | --- |
| Entry Spot actor packet | 대상 actor mailbox |
| Entry Spot initialize / closing / lifecycle callback | Entry Spot 실행 queue |
| user Spot actor packet | user Spot 실행 queue |
| user Spot packet / timer / subscription | user Spot 실행 queue |

Entry Spot actor handler 는 entrySpot, actor, payload 를 함께 받는다. user Spot
actor handler 는 spot, actor, payload 를 함께 받는다. 두 표면을 따로 둔 이유는
간단하다. Entry Spot 에는 user Spot 객체가 없지만, 입장 처리를 맡는 Entry Spot
인스턴스의 상태나 helper 메서드는 handler 에서 사용할 수 있어야 한다. user Spot
에서는 room 같은 spot 상태와 actor 상태를 함께 다룬다.

자세한 시그니처는 [handler-interfaces.ko.md](handler-interfaces.ko.md) 의 SPOT
lifecycle callback 섹션을 기준으로 본다.

### 4.3 역할별 수동 연결

이 소절은 discovery 를 쓰지 않고 endpoint 를 직접 지정해 연결할 때, 그 설정을
어디에 어떻게 둬야 하는지를 정리한다.

SPOT 역시 일반 channel 과 마찬가지로 수동 연결은 역할 단위로 나눠서 다뤄야
한다. `router`, channel client, `pubSub`, spot publish client 는 각자 사용할
endpoint 집합을 따로 관리한다. 수동 endpoint 는 node 에서 각 역할 메서드의
endpoint 인자로 둔다.

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .useDiscovery()
      .addRegistryEndpoint('tcp://registry1:5551')
    .addSpotMesh('stage-node')
      .enableRouter(undefined, undefined, 'tcp://10.0.0.10:9000')
      .enablePubSub('tcp://0.0.0.0:9000', undefined, 'tcp://10.0.0.20:9100')
      .addEntrySpot(StageEntrySpot)
      .addSpotFactory(StageSpot)
    .build()
);
```

여기서 따라야 할 규칙은 다음과 같다.

- 수동 연결은 `SpotNode` 전체가 아니라 역할별로 관리한다.
- 같은 역할 안에서는 `discovery` 와 `connect`(manual) 를 섞지 않는다.
- 같은 `SpotNode` 에서 같은 Spot 클래스 factory 를 두 번 등록하면 기존 값을
  덮어쓰지 않고 예외를 던진다.
- `router` manual 연결도 endpoint 집합만 등록한다. `connect` 항목에 remote router
  id 를 별도로 받지 않는다.
- channel client manual 연결도 endpoint 집합만 등록한다. 하부 `DEALER` 가 이미
  connect 된 peer 집합을 대상으로 요청을 보내기 때문에, remote `RoutingId` 를
  별도 파라미터로 받지 않는다.
- `pubSub` manual 연결에서 등록하는 주소는 다른 `SpotNode` 의 mesh publish bind
  주소다. local `SUB/XSUB` 쪽이 그 주소로 붙는다.

### 4.4 역할별 소켓 옵션

소켓 옵션은 호출 단위 builder 옵션과 섞지 않는다. 대신 등록 시점의 runtime
기본값으로 정의한다. dotnet 의 `ConfigureSocket(...)` / `ConfigureRouting(...)`
계열은 역할 키 안의 옵션 객체로 옮긴다.

- `router.socket` (dotnet `router.ConfigureSocket(...)`)
  - Node 바인딩의 공통 socket 기본값을 정한다.
- `router.routing` (dotnet `router.ConfigureRouting(...)`)
  - routed peer 연결에만 적용되는 전용 옵션을 정한다.
- `pubSub.publisher` (dotnet `pubsub.ConfigurePublisher(...)`)
  - `SpotNode` 의 mesh publish 기본값을 정한다.
- `pubSub.subscriber` (dotnet `pubsub.ConfigureSubscriber(...)`)
  - `SpotNode` 의 mesh subscribe 기본값을 정한다.
- `channelClients[name].socket` / `channelClients[name].routing`
  - route bridge channel socket 의 공통 socket 설정과 routed outbound 설정을 나눠
    구성한다.
- `spotPublishers[name].socket`
  - SpotNode publisher handle 의 publish ingress 기본값을 정한다.

예시를 풀어 보면 다음처럼 읽힌다. 시간 값은 ms number 로 표현한다(dotnet
`TimeSpan` 대응).

```ts
ZLinkModule.forRoot(
  zlinkFramework()
    .options({ requestTimeoutMs: 5_000 })
    .useDiscovery()
      .addRegistryEndpoint('tcp://registry1:5551')
    .addSpotMesh('stage-node')
      .enableRouter('tcp://0.0.0.0:9001')
      .enablePubSub('tcp://0.0.0.0:9000')
      .addSpotFactory(StageSpot)
    .build()
);
```

이때 timeout 은 socket option 이 아니다. 하부 바인딩의 channel request 처럼
**호출 단위 인자**로 들어가는 값이다. 즉 `requestToChannel(...)` 의 호출 옵션
(`timeoutMs`) 은 특정 요청 하나에만 적용된다. 위 등록 설정은 그와 별개로 runtime
기본값으로 유지된다.

### 4.5 spot 실행 문맥과 timer

이 소절의 핵심은 "timer 를 어디서 만들고 어느 문맥에서 실행하는가" 를 분명히
적어 두는 데 있다.

현재 core spec 기준으로 이미 다음과 같은 점이 정해져 있다.

- 같은 user `Spot` 의 dispatch callback delivery 는 직렬화된다.
- Entry Spot 은 user Spot 과 같은 handler / callback 등록 표면을 갖고, Entry Spot
  callback 을 Entry Spot 실행 줄에서 직렬로 실행한다. 여러 actor 와 입장 요청이
  공유하는 입구이므로 admission 상태가 동시에 변경되지 않아야 한다.
- subscribe, routed, **channel reply** completion 은 모두 같은 spot execution
  context 안에서 처리된다.
- timer 는 native timer 를 직접 노출하지 않고, framework runtime 이 만든 managed
  timer 를 사용한다.
- managed timer tick 은 user Spot 에서는 routed, subscribe, channel reply 와
  동일한 직렬 실행 경로로 들어온다.
- Entry Spot timer callback 은 Entry Spot lifecycle callback, channel reply completion 과
  같은 Entry Spot 실행 문맥에서 실행된다. actor packet 은 대상 actor mailbox 로 들어간다.
  단일 timer instance 안에서도
  이전 callback 이 끝나기 전에 다음 callback 을 겹쳐 실행하지 않는다.

여기서 핵심은 channel reply completion 과 timer callback 이 모두 같은 spot 실행
계약 안에 포함된다는 점이다.

- `context.outbound.requestToChannel(...)` 이 반환하는 `Promise` 는 임의의
  콜백 컨텍스트가 아니라 **spot execution context 안에서** complete 된다.
- request completion callback 이 같은 spot executor 에서 실행되므로, continuation
  도 spot state 에 별도 lock 없이 접근할 수 있다.
- 바인딩이 channel socket마다 별도 progress pump 를 돌리지 않아도 된다. `Spot`
  progress loop 하나로 channel reply completion 까지 처리된다.
- actor 가 `Spot` 에 join 된 뒤에는 `context.handlers.addHandler(...)` 로 등록한
  actor packet handler 역시 같은 spot execution context 에서 실행된다. stream
  session 은 packet ingress 를 맡고, actor 가 room 또는 stage 상태를 다루는
  코드는 `Spot` 실행 문맥으로 들어간다.
- actor join 으로 현재 `Spot` 이 바뀌는 경우, join 이 완료된 뒤 들어오는 actor
  dispatch 는 새 `Spot` 실행 문맥에서 실행되어야 한다. framework 는 actor session
  state 갱신과 packet dispatch 선택 사이의 경합을 막는다.

dispatch event 종류와 drain 대상은 아래처럼 정리된다(하부 dispatch table).

| dispatch event | subject kind | drain 방법 |
|---------------|--------------|------------|
| `subscribeReadable` | `Spot` | `subscribe()` |
| `routeReadable` | `Spot` | `recvRoute()` |
| `channelReplyReadable` | `ChannelDealer` | `drainChannelReplyFrom(subject)` |

timer 는 이 low-level dispatch table 에 직접 기대지 않는다. 대신 framework
runtime 이 만든 managed timer tick 을 Spot 문맥에서는 같은 spot queue 로 enqueue
해서 처리한다. Entry Spot actor packet 은 이 timer 실행 줄에 합류하지 않고 대상
actor mailbox 에서 처리된다.

framework 문서에서 "같은 spot 문맥" 이라고 설명하는 부분은 새 semantics 를
정의하는 작업이 아니다. 기존 core 계약과 framework 가 소유한 timer dispatch 를
사용자 눈높이로 풀어 적는 일에 더 가깝다. channel reply 역시 이제 그 "같은 spot
문맥" 안에 포함된다.

### 4.6 Spot 생성과 lifecycle

이 소절은 `Spot` 인스턴스를 누가 만들고 누가 소유하는지를 정리한다. 그리고 그에
맞춰 manager 표면을 어떤 모양으로 두는 것이 자연스러운지 본다.

현재 방향에서는 handler 클래스가 spot 을 만들지 않는다. `Spot` 인스턴스는
`SpotNode` 가 생성하고 소유한다. handler 는 이미 존재하는 spot 으로 들어오는
request, publish, subscribe 를 처리할 뿐이다.

이 기준에서 manager 는 `channelName` 이 아니라 현재 앱의 `SpotNode` 를 대상으로
동작한다. `ZLinkSpotManager` 의 전체 정의는
[handler-interfaces.ko.md](handler-interfaces.ko.md) 를 기준으로 본다. 이
문서에서는 그 인터페이스를 어떻게 읽고 어떤 상황에 쓰는지만 다룬다.

이 표면은 다음 상황을 함께 설명한다.

- Spot 클래스로 factory 를 고르고 runtime 이 새 `spotRid` 를 발급하는 생성
- 생성 요청이 넘긴 DTO 또는 `ZLinkMessage` 를 `onCreate(...)` 로 전달하는 경우
- 이미 존재하는 `spotRid` 라면 그대로 얻어 오는 `get-or-create` 성격의 동작

`ZLinkSpotManager` 표면(dotnet `IZLinkSpotManager` 대응):

```ts
interface ZLinkSpotManager {
  create(spotType: Type<ZLinkSpot>, request?: unknown | ZLinkMessage): Promise<ZLinkSpotCreateResult>;
  getOrCreate(spotType: Type<ZLinkSpot>, spotRid: string, request?: unknown | ZLinkMessage): Promise<ZLinkSpotCreateResult>;
  find(spotRid: string): Promise<ZLinkSpotInfo | null>;
  list(): Promise<readonly ZLinkSpotInfo[]>;
  close(spotRid: string): Promise<boolean>;
}

interface ZLinkSpotCreateResult {
  spotRid: string;
  state: ZLinkSpotCreateState;
  reply?: unknown;
}
interface ZLinkSpotInfo { spotRid: string; }
```

> **코드 정합성 주의.** dotnet `ZLinkSpotInfo` 는 `SpotRid` 하나만 가진다. spot
> 이름 필드가 없다. 따라서 node 도 `find/list` 결과에 `spotRid` 만 담고, 초기
> 드래프트가 가정한 `spotRid -> spotName` 조회는 두지 않는다(§12 divergence).

여기서 중요한 점은 반환값이 장기적으로 들고 다닐 spot instance handle 이
아니라는 사실이다. 생성 결과는 `spotRid`, `Existing` / `Created` / `Rejected`
상태와 선택적 reply DTO 를 담는다. 이후 메시징은
현재 channel publish 또는 route bridge channel socket을 통한 send / request 로 푼다.

생성 요청 payload 는 DTO 로 넘기거나 `ZLinkMessage` 로 감싸서 넘긴다. framework 는
caller payload 를 `ZLinkMessage` 로 만들어 `spot.onCreate(request)` 에 한 번 전달한다.
이 payload 는 방 설정, seed, 접근 정책처럼 spot 이 처음 만들어질 때만 해석해야 하는
값에 사용한다. `create(StageSpot)` 처럼 payload 가 없는 편의 overload 는 빈
`ZLinkMessage` 를 넘긴 것과 같다. 새 spot 이 만들어지면 `onCreate(...)` 는 빈
`ZLinkMessage` 를 받아 한 번 실행된다. JSON, MessagePack, Protobuf, custom codec 은
기존처럼 module options 의 codec registry 에 등록하며, spot 구현은 `request.decode<T>()`
로 업무 DTO 를 읽는다.

생성 요청에는 어떤 spot factory 를 사용할지도 함께 들어가야 한다. framework
public 표면에서는 이 값을 **Spot 클래스 참조**로 표현하고, string spot rid 은
contract 에서 제거한다. remote 생성 relay 가 필요한 경우 factory 식별은
framework 내부 metadata 로 처리하며 application API 에 노출하지 않는다.

명시적 `spotRid` 가 필요한 경우 public surface 는 `create(spotType, spotRid)` 가
아니라 `getOrCreate(spotType, spotRid, request?)` 로 표현한다. 이미 같은
`spotRid` 의 framework spot 이 ready 상태면 `state = Existing` 을 반환하고, 새
요청의 `request` 는 `onCreate(...)` 로 전달하지 않는다. initializing 상태면 첫
생성 요청의 `onCreate(...)` 완료를 기다린다. 다만 기존 entry 의 Spot 타입이
요청의 spotType 과 다르면 같은 logical spot 을 다른 framework type 으로 해석하려는
시도이므로 `SpotTypeMismatch` 로 실패해야 한다.

remote framework node 에 생성 요청을 relay 하는 경우도 같은 구조를 유지한다.
metadata 에는 factory 식별자와 선택적인 `spotRid` 를 넣고, 생성 요청은 단일
`Message` 로 보낸다. 이 식별자는 framework 내부 값이며 public
`spotRid` API 로 노출하지 않는다.

`find(...)` 와 `list(...)` 는 운영 코드가 현재 존재하는 logical spot rid 를
확인할 수 있게 하는 조회 표면이다. 조회 결과에는 `spotRid` 만 포함한다.

등록 단계에서 타입 충돌은 조용히 덮어쓰지 않는다. `spotFactories` 에 이미 등록된
클래스를 다시 받으면 startup 시점에 예외를 던진다. 설정 실수를 바로 드러내는
쪽을 기본 규칙으로 본다.

#### lifecycle callback 순서

dotnet 코드(`ZLinkSpotActivation`)에서 확인한 user Spot lifecycle 호출 순서는
다음과 같다. node 도 동일하게 둔다.

1. `configure()` — registration 단계. handler / subscribe 등록만 허용된다(이
   창이 닫힌 뒤 등록을 시도하면 예외). dotnet `Configure()`.
2. `onCreate(request)` — spot 인스턴스가 처음 만들어질 때 한 번. dotnet
   `OnCreateAsync(request, ct)`. 빈 생성이면 빈 `ZLinkMessage`.
3. `onInitialize()` — `onCreate` 직후 같은 직렬 실행 op 안에서 한 번. dotnet
   `OnInitializeAsync(ct)`. timer 등록은 보통 여기서 한다.
4. `onClosing()` — spot 종료 시 spot 실행 문맥에서. dotnet `OnClosingAsync(ct)`.

`onCreate` 와 `onInitialize` 는 같은 serial executor op 안에서 연달아 실행된다
(`InitializeAsync` 가 둘을 한 람다로 묶는다). Entry Spot 은 `onCreate` 가 없고
`configure() -> onInitialize() -> onClosing()` 만 갖는다(dotnet `IZLinkEntrySpot`
에는 `OnCreateAsync` 없음).

따라서 사용자는 생성 직후 식별자만 얻고:

```ts
const stage = await spotManager.create(StageSpot);

await spotPublisherClient.publishSpot('game.stage', 'stage.state.updated', {
  stageRid: stage.spotRid,
}).submit();

const info = await spotManager.find(stage.spotRid);
```

처럼 사용하면 된다. 생성된 `Spot` 인스턴스를 응용이 직접 오래 관리하는 모델은
현재 방향에서는 다루지 않는다.

초기 payload 를 함께 넘기는 create 표면은 framework 기본 계약에 포함한다. 다만
core C API 는 payload 를 해석하지 않는다. core 는 logical spot 확보의 원자성만
보장하고, payload 전달과 typed 초기화는 framework lifecycle 의 책임이다. factory
resolve, activation, `onCreate(...)`, `onInitialize(...)` 실패는
`SpotCreateFailed` 계열로 분류한다.

## 5. SPOT outbound 모델

이 절은 SPOT 쪽 outbound 호출이 어떤 축으로 갈라지는지, 그리고 그 축마다 어느
표면을 쓰는지를 정리한다.

현재 방향에서는 다음 세 종류를 구분한다.

- 현재 SPOT channel 안의 topic publish
- route bridge가 참조하는 다른 channel runtime socket을 통한 channel send / request
- spot rid 기반 routed spot send / request

각 표면이 맡는 역할은 다음과 같다.

- `sendToChannel(...)` / `requestToChannel(...)` 는 route bridge channel socket을
  사용한다.
- `sendToSpot(...)` / `requestToSpot(...)` 는 spot remote address resolver 가 찾은
  target route 를 이용한다.
- `targetRid + spotRid` 를 직접 받는 raw 호출은 하부 바인딩에 남아 있더라도,
  application guide 의 기본 API 로는 문서화하지 않는다.

`ZLinkSpotOutbound` 인터페이스의 메서드는 다음과 같다(dotnet
`IZLinkSpotOutbound` 와 1:1). SPOT 구현 안에서는 `context.outbound` 로 노출된다.

| node 메서드 | dotnet | 의미 |
| --- | --- | --- |
| `sendToSpot(spotRid, message)` | `SendToSpot<TMessage>(RoutingId, TMessage)` | spot-routed 단방향 send |
| `requestToSpot(spotRid, request)` | `RequestToSpot<TRequest>(RoutingId, TRequest)` | spot-routed request |
| `publish(topic, message)` | `Publish<TEvent>(string topic, TEvent)` | 현재 SPOT channel topic publish |
| `sendToChannel(channelName, message)` | `SendToChannel<TMessage>(string, TMessage)` | attach 된 channel 로 send |
| `requestToChannel(channelName, request)` | `RequestToChannel<TRequest>(string, TRequest)` | attach 된 channel 로 request |

handler 나 lifecycle callback 안에서는 별도 client 를 찾지 않고
`context.outbound.requestToSpot(...)` 처럼 호출한다. timer 는
`context.addTimer(name, periodMs, handlerType, options?)` 처럼 spot lifecycle
registration 표면으로 둔다.

현재 framework 표면은 channel 이름 기준 호출과 spot key 기반 호출을 구분한다.
`targetRid + spotRid` 를 직접 받는 raw route 함수가 하부 바인딩에 있어도,
framework application 문서에서는 backend / internal transport helper 로만 다룬다.
일반 application 은 `ZLinkSpotRemoteAddressResolver` 가 숨긴 위치값을 직접 보지
않는다.

예를 들면 다음과 같이 사용할 수 있다.

```ts
await spot.context.outbound.sendToChannel('orders', new RoomNoticeMessage());

const reply = await spot.context.outbound.requestToChannel<GetStageStateReply>(
  'orders',
  new GetStageStateRequest(),
  { timeoutMs: 200 },
);

await spot.context.outbound.sendToSpot(stage.spotRid, new StageNoticeMessage());
```

`Stage wrapper` 같은 상위 모델을 생각하면 timer 도 함께 필요하다. 다만 현재
기준은 이를 `ZLinkSpotOutbound` 의 callback scheduler 로 두지 않는다. 대신
`context.addTimer(...)` 로 등록하는 lifecycle timer 한 가지 모델로 정리한다.
그래야 stage state 를 별도 lock 없이 다루는 상위 모델을 설명하기 쉬워진다.

다만 이 관계를 `ZLinkChannelClient` 위에 `ZLinkSpotOutbound` 를 얹는 형태로
설명하면 안 된다. 두 인터페이스는 하부에서 서로 다른 C API 를 감싸기 때문이다.
현재 방향에서는 책임을 다음과 같이 나눈다.

- `ZLinkChannelClient` 는 일반 channel messaging 을 맡는다.
- `ZLinkSpotOutbound` 는 current SPOT channel publish, 다른 channel send /
  request, spot-routed send / request 를 맡는다.

local Spot callback 안에서는 `spot.context.outbound.publish(...)` 를 호출한다.
`ZLinkSpot` 외부에서 특정 SPOT channel 로 publish 하는 경우에는
`ZLinkSpotPublisherClient.publishSpot(channelName, topic, ...)` 를 사용한다.

## 6. publish 모델

이 절은 SPOT 쪽 publish 모델을 두 갈래로 나누어 정리한다. 먼저 local spot 안에서
의 topic publish 를 보고, 그 다음에 local spot 이 없는 외부 노드에서의 SPOT
channel publish 를 본다.

### 6.1 topic publish

`ZLinkSpotOutbound` 는 spot-to-spot routed call 과 publish 를 함께 가질 수 있다
([handler-interfaces.ko.md](handler-interfaces.ko.md) 참고). 이렇게 둔 이유는
`SPOT` 쪽에서 두 기능을 함께 쓰는 경우가 많기 때문이다.

여기서 `topic` 과 `spotRid` 는 역할이 서로 다르다.

- `spotRid`: 특정 room / stage / zone 인스턴스를 가리키는 논리 주소
- `topic`: 여러 subscriber 가 함께 듣는 fan-out 주제 이름

현재 topology 에서는 framework 기본 표면을 `targetRid + spotRid` direct 호출
중심으로 설명하지 않는다. 대신 high-level framework 문서는 다음 세 축을 먼저
보여 준다.

- 같은 channel 안의 publish / subscribe
- route bridge가 참조하는 다른 channel runtime socket을 통한 send / request
- spot rid 기반 routed send / request

이때 channel send / request, spot send / request, topic publish 는 일반 channel
messaging 과 비슷한 builder 감각으로 읽힌다.

```ts
const reply = await spot.context.outbound.requestToChannel<GetStageStateReply>(
  'orders',
  new GetStageStateRequest(),
  { timeoutMs: 200 },
);

await spot.context.outbound.sendToSpot(stage.spotRid, new StageNoticeMessage());

await spot.context.outbound.publish('stage.state.updated', new StageStateUpdatedEvent());
```

`Stage wrapper` 같은 상위 계층이 별도의 directory 나 lookup 을 얹는 것은
가능하다. 다만 그것을 framework 의 기본 표면으로 고정하는 모델은 현재 방향에서
채택하지 않는다.

### 6.2 외부 노드에서의 SPOT channel publish

local spot 인스턴스를 가지지 않는 외부 노드가 특정 SPOT channel 로 publish 해야
하는 경우도 있다. 이때는 `spot.context.outbound.publish(...)` 가 아니라
`ZLinkSpotPublisherClient.publishSpot(channelName, topic, ...)` 를 사용한다
([handler-interfaces.ko.md](handler-interfaces.ko.md) 참고).

```ts
@Controller('stage')
export class StagePublishController {
  constructor(private readonly spotPublisherClient: ZLinkSpotPublisherClient) {}

  @Post('publish')
  async publish(@Body() request: PublishStageStateHttpRequest): Promise<void> {
    await this.spotPublisherClient.publishSpot(
      'game.stage',
      'stage.state.updated',
      new StageStateUpdatedEvent(request.stageRid, request.userCount),
    ).submit();
  }
}
```

이 인터페이스는 local spot 문맥이 없는 외부 노드에서도 target SPOT channel
이름을 명시해 publish 할 수 있게 해 준다. 따라서 두 경우를 분리해 설명한다. 하나
는 local spot 안에서 현재 channel 로 publish 하는 경우이고, 다른 하나는 외부
노드에서 특정 SPOT channel 로 publish 하는 경우다.

또한 subscribe handler 는 router request handler 와 같은 종류의 매핑으로 보면 안
된다. 두 경우 모두 문자열을 키로 쓰지만, dispatch 의미는 서로 다르기 때문이다.

- packet 은 header 의 `msgId` 를 기준으로 targeted dispatch 된다.
- subscribe 는 `"stage.state.updated"` 같은 topic subscription 으로 consumer
  등록된다.

## 7. subscribe 모델

이 절은 `SPOT` 안에서 packet handler, subscribe handler, timer 가
어떤 모양으로 등록되는지를 정리한다. 핵심은 NestJS channel handler group 이 아니라
`configure()` 안에서 직접 등록하는 점이다. 실제 handler 인터페이스는
[handler-interfaces.ko.md](handler-interfaces.ko.md) 를 기준으로 본다.

현재 `SPOT` 모델은 spot 객체가 `configure()` 단계에서 직접 handler 를 등록하는
쪽을 기본으로 본다. actor packet handler 는 예외다. actor packet handler 는
`zlinkEntrySpotActorRequestHandler(...)` 또는 `zlinkSpotActorRequestHandler(...)`
decorator 로 등록한다.

```ts
@Injectable()
export class StageSpot implements ZLinkSpot {
  private heartbeat?: ZLinkTimer;

  constructor(readonly context: ZLinkSpotContext) {}

  configure(): void {
    this.context.handlers.addPacket(GetStageStateHandler);
    this.context.handlers.addPacket(ReportStageStateHandler);
    this.context.handlers.addSubscribe(StageStateUpdatedHandler, 'stage.state.updated');
  }

  async onInitialize(): Promise<void> {
    this.heartbeat = await this.context.addTimer(
      'heartbeat',
      1000,
      StageHeartbeatHandler,
      { overrunPolicy: ZLinkTimerOverrunPolicy.DelayNextTick },
    );
  }
}
```

여기서 기대하는 동작은 다음과 같다.

- `context.handlers.addPacket(Handler)` 는 request 와 send packet 을 함께
  등록한다.
- packet dispatch key 는 packet 타입의 header `msgId` 다.
- `protobuf` 를 쓰면 `msgId` 는 protobuf message 이름이 된다.
- `json` 을 쓰면 `msgId` 는 payload 클래스 생성자 이름이 된다(TS 는 런타임 타입
  소거가 있으므로 클래스 이름 또는 `@ZLinkPacket('name')` 에 의존한다).
- `context.handlers.addSubscribe(Handler, topic)` 는 topic consumer 등록이다.
- `context.addTimer(name, periodMs, Handler, options?)` 는 현재 spot lifecycle
  안에 timer 를 등록한다. 네 번째 인자 `ZLinkTimerOptions` 로 overrun 정책과
  handler 예외 정책을 정한다.
- handler 는 별도의 class 로 두고, `StageSpot` 안에는 코어 로직만 남길 수 있다.
- handler 가 다른 서버나 다른 spot 으로 outbound 호출을 해야 한다면
  `ZLinkChannelClient` 또는 `ZLinkSpotOutbound` 를 생성자 주입으로 받는 쪽이 더
  자연스럽다.
- framework 는 per-spot scope 를 만들고, 등록된 handler 타입을 그 scope 에서
  자동으로 resolve 하는 방식을 기본으로 본다(NestJS provider scope).

timer handler 는 아래처럼 tick metadata 를 받는다.

```ts
@zlinkSpotTimerHandler()
export class StageHeartbeatHandler implements ZLinkSpotTimerHandler<StageSpot> {
  async handle(spot: StageSpot, tick: ZLinkTimerTick): Promise<void> {
  }
}
```

`ZLinkTimerTick` 은 callback 번호, fixed-rate 시간표의 tick 번호, 예정 시각, 시작
시각, 지연, 건너뛴 tick 수를 포함한다. `SkipLateTicks` 와 `CatchUpBounded` 는
fixed-rate 기준 시각을 유지하고, `DelayNextTick` 은 handler 완료 뒤 period 를
다시 기다리는 fixed-delay 정책이다. timer handler 예외는 runtime monitoring 에
`TimerHandlerFailed` event 로 기록된다. `stopOnUnhandledException` 이 켜져 있으면
timer 를 중단하고 `TimerStoppedAfterUnhandledException` event 를 기록한다.

### 7.1 room 계열 사용과 핫패스 원칙

이 소절은 `SPOT` 을 FPS 같은 게임의 room 으로 쓸 때 어떤 성능 기준을 들어야
하는지, 그리고 실제 성능을 좌우하는 항목이 무엇인지 정리한다.

`SPOT` 이 FPS 같은 게임의 room 으로 쓰이더라도, 이 모델 자체가 곧바로 과한
오버헤드를 만든다고 보지는 않는다. 다만 `SPOT` 쪽 메시지 handler 호출은 room 의
핫패스가 될 수 있다. 따라서 일반 channel messaging 보다 더 강한 성능 기준을
적용한다.

- reflection / metadata 조회는 registration 단계까지만 허용한다.
- per-packet allocation, 과도한 DI 재구성, 불필요한 객체 래핑은 피해야 한다.

`context.handlers.addPacket(...)` 같은 등록 표면은 startup 과 spot
`configure()` 단계에서만 비용이 들도록 둔다. 실제 packet hot path 에서는 반복적인
reflection 이나 과도한 객체 생성이 남지 않게 해야 한다.

실제 room 성능에 더 큰 영향을 주는 것은 보통 registration 문법보다 다음
항목들이다.

- protobuf encode / decode 비용
- 같은 spot 안의 queue 적체
- broadcast fan-out
- allocator pressure
- event loop 점유 시간(긴 동기 핸들러)

따라서 framework 문서는 "class 기반 handler 라서 느리다" 가 아니라 "핫패스 구현을
어떻게 캐시하고 어떻게 줄일 것인가" 를 더 중요한 원칙으로 본다.

여기서 말하는 강한 최적화 기준은 `SPOT` packet 처리 쪽에 우선 적용된다. 일반
socket / service 메시지 handler 의 성능을 포기해도 된다는 뜻은 아니다. 일반
channel messaging 쪽은 `SPOT` room 의 핫패스에 비해 편의 기능을 조금 더 허용할
여지가 있다는 정도로 본다.

## 8. SPOT과 direct call의 관계

이 절은 framework 안에서 일반 channel messaging 과 `SPOT` 두 축을 어떻게 구분해
설명할지를 정리한다.

`ZLink Framework` 는 direct channel call 만 제공하는 계층처럼 비춰져서는 안 된다.
`SPOT` 역시 framework 안에서 동등한 축으로 다뤄야 한다.

다음 두 축이 함께 존재해야 한다.

- `channelName` 기반 일반 channel messaging
- `SPOT` 기반 current channel publish / subscribe 와 channel send / request

또한 현재 하부 topology 는 `SpotNode.router` peer 경로와 channel runtime socket을
참조하는 route bridge 경로를 함께 가진다. framework 문서에서는 다음 두 종류를
구분해서 설명한다.

- 같은 channel 안의 topic publish / subscribe
- route bridge가 참조하는 다른 channel runtime socket을 통한 send / request

이 점은 `playhouse` 시나리오에서 특히 중요하다.

- play -> api 는 direct call
- stage / state sync 는 `SPOT`

또한 `rid` 를 직접 넣는 routed 호출은 SPOT spot-to-spot 경로에만 남는다. 특정
channel 의 `ROUTER(server)` 를 `rid` 로 직접 지정해서 호출하는 모델은 현재
방향에서 채택하지 않는다.

`SPOT` 은 pub / sub 만으로 설명하면 부족하다. 다음 세 가지를 함께 설명해야
한다.

- room / stage / zone 같은 논리 인스턴스 모델
- channel publish / send / request
- `SpotNode` 가 spot 인스턴스를 생성하고 소유하는 lifecycle

## 9. discovery와 service name

이 절은 `SpotNode` 가 어떻게 channel 정체성을 닫는지, 그리고 그 결정이 discovery
와 어떻게 묶이는지를 짧게 정리한다.

최신 topology 에서는 `.addSpotMesh(name)` 이 실행할 `SpotNode` 를 직접 등록하고,
전역 `discovery` 가 active channel view 를 공급한다. SPOT network 를 구성하는
모든 node 는 `.addSpotMesh(...)` 로 등록한다. STREAM SessionRelay 는 별도 node
builder 가 아니라, stream 이 router
방식으로 연결한다(자세한 내용은 [nestjs-stream.ko.md](nestjs-stream.ko.md)).

## 10. 결정된 기준

- route bridge channel socket과 spot publisher client 설정은 역할별 옵션 하나
  로 묶는다. socket option 과 manual connection(`connect`)처럼 runtime 이 소유하는
  설정만 노출하고, 그보다 더 세밀한 하위 builder 트리는 기본 표면으로 확장하지
  않는다.
- spot rid 는 별도 wrapper 없이 `RoutingId`(string) 로 노출한다. framework
  문서에서는 node rid 와 spot rid 를 이름으로 구분한다.
- Entry Spot application registry 는 `SpotNode` 등록 안에서 `.addEntrySpot(...)` 으로
  붙인다. Entry Spot 자체의 native lifecycle 은 framework 가 관리한다.
- Entry Spot 과 user Spot 의 actor packet handler, actor joined handler, actor
  left handler 는 각 context 의 registry(`context.handlers`)에 등록한다.
  join / leave lifecycle 을 Spot 메서드 override 만으로 설명하지 않는다.
- Entry Spot 과 user Spot 은 packet, subscription, timer, channel outbound, actor
  handler 등록 표면과 Spot 단위 직렬 실행 정책을 맞춘다.
- `ZLinkSpotManager` 는 생성과 조회를 함께 가진다. `find(...)`, `list(...)` 는
  별도 query 서비스로 분리하지 않고 manager 에 남긴다.
- subscriber concurrency 와 backpressure 는 per-handler 나 per-topic API 가 아니라,
  subscriber 역할 option 에서 노드 단위로 설정한다.

`Stage wrapper` 에서 필요한 metadata 전달, membership, 실행 문맥 규칙은 framework
의 기본 계약이 아니다. 이 항목들은
[stage-wrapper-on-spot.ko.md](stage-wrapper-on-spot.ko.md) 에서 다루는 상위
wrapper 축으로 본다.

### 10.1 dispatch 실패 정책

SPOT route request 에 handler 가 없거나 payload decode, handler 예외, invalid frame 이 발생하면 reply
path 가 있는 경우 error reply 를 반환한다. actor request 도 같은 원칙을 따른다. 같은 process 안의
local actor call 처럼 reply frame 이 없는 경로는 `Promise` 를 framework error 로 reject 한다.

SPOT route send, subscription, actor send 는 reply 를 만들 수 없으므로 실패한 메시지를 drop 한다.
route send 와 actor send 는 Warning 로그와 counter, subscription 은 Debug 로그 또는 counter 와 전역
`ZLinkMessageFlowObserver` 의 Error outcome 을 남긴다. observer 실패는 dispatch loop 나 shutdown 을
깨지 않는다.

## 11. Router channel route 수신

`ZLinkSpotRemoteAddress.routerChannelId` 는 resolver 가 반환한 위치 정보 중 하나다. 이 값은
metadata 로만 남으면 안 되고, 실제 transport 로 사용할 RouteMesh channel 을 가리켜야 한다.
같은 프로세스에 RouteMesh와 SpotMesh가 있으면 framework가 route bridge를 자동으로 붙인다.

```ts
zlinkFramework()
  .addRouteMesh('api')
    .enableRouter('tcp://0.0.0.0:7000')
  .addSpotMesh('stage-node')
    .enableRouter('tcp://0.0.0.0:9001')
```

fanout channel과 client-server channel은 SPOT route 수신 대상으로 쓰지 않는다. RouteMesh에
handler group을 매핑해도 일반 route packet과 SPOT relay packet은 bridge demux로 분리된다.

Spot callback 밖의 channel handler, HTTP handler, background service 에는 target
Spot 으로 직접 send / request 하는 별도 public client 를 두지 않는다. 이 경로에서
는 actor 생성 또는 entry spot join 같은 도메인 흐름으로 `ActorRef` 를 얻고,
session 이 필요하면 그 ref 로 session actor handle 을 bind 한다. current Spot
callback 안에서 다른 Spot 으로 보내야 할 때만 `spot.context.outbound.sendToSpot(...)`
또는 `spot.context.outbound.requestToSpot(...)` 을 사용한다.

local egress channel 은 RouteMesh channel 이다. route mesh channel 을 egress 로 쓸 때는 실제 target
ROUTER endpoint 에 연결되어 있어야 하고, target ROUTER 의 `RoutingId` 는 discovery / query metadata
또는 같은 process 안의 route channel 등록으로 확인할 수 있어야 한다. 주소만 알고 연결하지 않은
상태에서는 routed Spot 메시지를 보낼 수 없다.

```ts
zlinkFramework()
  .addRouteMesh('play.route')
    .connect('tcp://play-node-1:7201')
```

node 에는 별도 route egress 표면이 없다. 실제 전송은 `outbound.sendToSpot(spotRid, ...)` /
`outbound.requestToSpot(spotRid, ...)`으로 하며, target Spot 은 문자열 overload 없이 `RoutingId`로 지정한다.

## 12. 회귀 테스트

이 절은 SPOT 문서가 다룬 항목들을 어떤 테스트로 검증하는지 한꺼번에 본다.
node 테스트 이름은 dotnet 회귀 케이스를 그대로 옮긴다. 정식 매트릭스는
[regression-test-matrix](../internals/regression-test-matrix.ko.md) 가 소유한다.

SPOT 문서의 항목은 factory 등록, mesh / discovery 구성, lifecycle, publish, actor
join 문맥이 함께 검증되어야 한다. 또한 spot 클래스와 id 를 다루는 public 표면은,
호출자가 transport 위치를 알지 못해도 동작해야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `forRoot throws when spot factory class is duplicated across nodes` | 같은 Spot factory 클래스를 중복 등록하면 startup validation 예외가 난다. |
| `forRoot allows standalone local spot node` | Discovery 없이도 local-only SpotNode 구성은 시작할 수 있다. |
| `spotManager create/find/list/close work through framework runtime` | `create`, `find`, `list`, `close` 와 scope 정리가 일관된다. |
| `spot publish/timer and close stop callbacks work` | timer 와 publish callback 이 spot lifecycle 안에서 돌고, 종료 뒤에는 멈춘다. |
| `spot timer provides tick metadata` | timer handler 가 callback 번호, 예정/시작 시각, 지연, skip metadata 를 받는다. |
| `spot timer skips late ticks when configured` | `SkipLateTicks` 정책은 늦은 tick 을 무제한 전달하지 않고 `skippedTicks` 로 드러낸다. |
| `spot timer catches up within configured limit` | `CatchUpBounded` 정책은 `maxCatchUpTicks` 상한 안에서만 연속 실행한다. |
| `spot timer delayNextTick waits after handler completion` | `DelayNextTick` 정책은 handler 완료 뒤 period 를 다시 기다린다. |
| `spot timer rejects unknown overrun policy` | 알 수 없는 overrun 정책 값은 설정 오류다. |
| `spot timer reports handler exception to monitoring` | handler 예외가 runtime monitoring 의 timer failure event 로 기록된다. |
| `spot timer stopOnUnhandledException stops timer` | `stopOnUnhandledException` 이 켜진 timer 는 첫 handler 예외 뒤 중단된다. |
| `spot timer cancel stops managed timer loop` | `cancel()` 뒤 managed timer loop 가 추가 callback 을 실행하지 않는다. |
| `outbound-only spot publisher client publishes to target channel` | 외부 publisher client 가 target SPOT channel 로 publish 한다. |
| `spot actor join/move/submit run through spot execution context` | actor join, 이동, packet dispatch 가 현재 spot 실행 문맥에서 실행된다. |
| `entry spot actor packets use actor mailboxes without entry-wide serial dispatch` | Entry Spot actor packet 은 대상 actor mailbox 를 사용하고, 서로 다른 actor 는 Entry Spot 전체 실행 줄 때문에 서로 기다리지 않는다. |
| `entrySpot packet handlers use entrySpot serialization` | Entry Spot 일반 packet handler 가 user Spot 처럼 Spot 단위 직렬 실행 줄을 사용한다. |
| `entrySpot timer waits for entrySpot callbacks` | Entry Spot timer callback 이 같은 Entry Spot의 다른 callback 과 동시에 실행되지 않는다. |
| `entrySpot timer does not reenter same timer` | Entry Spot timer 는 같은 timer callback 을 겹쳐 실행하지 않는다. |

기본 `submit(...)` 경로는 user Spot handler completion까지 같은 실행 줄을 유지한다.
`yield(...)`은 request, Spot outbound request, actor `joinSpot` / `joinEntrySpot`,
bound session send completion, `runWorker` completion에서만 현재 Spot turn을 반납하고
completion 뒤 원래 mailbox에서 재개한다. Entry Spot actor handler에는 반납할 Entry Spot
전체 실행 turn이 없으므로 `yield(...)` 호출은 시간 초과가 아니라 즉시 계약 오류가 난다.
`yield(...)` 중에도 같은 actor와 같은 timer는
재진입하지 않는다. 다른 actor나 다른 timer 작업은 interleave될 수 있으므로, await 전후에 공용
가변 상태를 이어 판단하는 handler는 기본 `submit(...)`을 사용해야 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework NestJS Channel Messaging](nestjs-channel-messaging.ko.md) | [다음: Node.js Stage Wrapper On SPOT](stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
