# Framework handler 등록 방식 정렬 계획

> 상태: **초안**. 작성 2026-06-15.
> 목적: cpp, java, node, dotnet framework 의 Channel handler 와 SPOT handler 등록 방식을
> 언어 특성에 맞게 정렬한다. 특히 node 와 dotnet/java 는 **수동 등록**과 **자동 등록**을 모두
> 제공하고, cpp 는 언어 특성상 자동 등록을 제공하지 않는 방향을 명확히 한다.

## 1. 목표

handler 등록 방식은 두 가지를 모두 1급 기능으로 둔다.

- **수동 등록**
  - 구성 코드에서 어떤 handler 가 등록되는지 바로 보인다.
  - 샘플, 운영 서비스, 보안 검토, 테스트 격리에서 유리하다.
  - handler 개수가 많아져도 노출 범위를 명시적으로 통제할 수 있다.
- **자동 등록**
  - decorator, annotation, attribute, package/assembly scan 으로 반복 등록 코드를 줄인다.
  - handler 파일을 추가하는 앱 개발 흐름에서 편하다.
  - 단, 자동 등록은 내부적으로 명시 등록 목록을 만들어 넣는 편의 기능이어야 한다.

cpp 는 예외로 둔다. cpp 는 runtime reflection 과 annotation scan 이 언어 표준 기능이 아니므로
자동 등록을 억지로 맞추지 않는다. cpp 는 현재처럼 명시 registry 와 member pointer 기반 등록을
정식 방식으로 유지한다.

## 2. 현재 상태 요약

| 언어 | Channel 수동 | Channel 자동 | SPOT 수동 | SPOT 자동 | 판정 |
|------|--------------|--------------|-----------|-----------|------|
| cpp | 있음 | 없음 | 있음 | 없음 | 의도한 상태. 자동 등록 추가 대상 아님 |
| dotnet | 있음 | 있음 | 있음 | 부족 | SPOT 자동 등록을 node 수준으로 보강 필요 |
| java | 있음 | 있음 | 있음 | 부분 지원 | SPOT 자동 등록 범위를 node 수준으로 정리 필요 |
| node | core options 에 있음, Nest DSL 부족 | 있음 | core options 에 actor request 배열 있음, Nest DSL 부족 | 있음 | Nest 수동 등록 DSL 보강 필요 |

### 2.1 cpp

cpp 는 Channel handler registry 와 Spot/EntrySpot `configure()` 안의 handler registry 가 명시 등록 표면이다.

- Channel: `handlers().on_request(...)`, `on_send(...)`, `on_event(...)`
- SPOT: Spot/EntrySpot 이 생성자에서 context 를 받고, `configure()` 안에서 `context.handlers().add_handler<&T::method>()`,
  `add_actor_packet<&T::method>()`

이 상태를 유지한다. 자동 등록을 추가하면 빌드 시스템, 매크로, 정적 초기화 순서 같은 별도 규칙이
필요해져서 cpp 쪽 복잡성이 커진다.

### 2.2 dotnet

Channel 은 이미 두 방식이 있다.

- 수동: `AddRequestHandler<THandler>()`, `AddSendHandler<THandler>()`, `AddPublishHandler<THandler>()`
- 자동: `AddHandlersFromAssembly(...)` 와 handler attribute/interface scan

SPOT 은 수동 등록 중심이다.

- `Configure()` 안의 `Context.Handlers.AddHandler<THandler>()`
- `Configure()` 안의 `Context.Handlers.AddActorRequest<THandler>()`
- `Configure()` 안의 `Context.AddTimer<THandler>(...)`

SPOT handler 에 attribute/interface descriptor 는 있지만, assembly scan 결과를 Spot/EntrySpot 타입 기준으로
연결하는 public 규칙은 부족하다.

### 2.3 java

Channel 은 두 방식이 있다.

- 수동: `addRequestHandler(...)`, `addSendHandler(...)`, `addPublishHandler(...)`
- 자동: `addHandlersFromPackageOf(...)` 와 annotation/interface scan

SPOT 은 수동 등록이 있고, scanned actor handler catalog 도 존재한다. 다만 SPOT packet,
subscription, actor handler 자동 등록 범위를 사용자 관점에서 한 세트로 설명하기 어렵다.
node 수준으로 자동 등록 범위를 명확히 해야 한다.

### 2.4 node

Node 는 자동 등록이 강하다.

- Channel: `@zlinkRequestHandler`, `@zlinkSendHandler`, `@zlinkPublishHandler`
- SPOT actor: `@zlinkSpotActorRequestHandler`
- provider scan: `zlinkDiscoverProviders(...)`

반면 Nest DSL 의 수동 등록 표면은 부족하다.

- Channel options 에는 `requestHandlers`, `publishHandlers` 가 있지만
  `zlinkFramework().clientServerChannel(...).requestHandler(...)` 같은 fluent API 가 없다.
- SPOT options 에는 `entrySpotActorRequestHandlers`, `spotActorRequestHandlers` 가 있지만
  사용자가 Spot/EntrySpot 클래스 안의 `configure()` 에서 handler 를 수동 등록하는 표면이 아직 충분히
  정리되어 있지 않다. `configure()` hook 자체는 있지만, SPOT handler 등록과 dispatcher 연결을
  public API 로 완성해야 한다.

### 2.5 구현 적용 전 리뷰 결론

현재 checkout 기준으로 바로 구현에 들어가기 전에 정리해야 할 충돌은 아래처럼 해소한다.

- node Spot/EntrySpot 에는 이미 `configure()` hook 과 context 주입 흐름이 있다. 새 등록 전용 객체를 만들지
  않고, `configure()` 안에서 기존 context 의 handler 등록 함수를 사용한다.
- node 의 context handler registry 에 부족한 SPOT handler 수동 등록 메서드를 추가한다.
- node timer 는 timer 이름, 주기, option 이 필요하다. 자동 등록을 제공하려면 timer decorator 가 handler
  provider 표시만 하는 수준을 넘어 schedule metadata 도 함께 선언해야 한다.
- node 에서 `configure()` 로 등록한 SPOT handler 는 단순 목록 저장만으로는 호출되지 않는다. user spot 과
  Entry Spot 모두 실제 dispatcher registry 까지 연결해야 한다.
- SPOT 자동 등록은 Spot node builder 에 handler 를 붙이지 않는다. scan 으로 만든 descriptor 는 handler
  interface 의 Spot/EntrySpot 타입을 기준으로 해당 Spot/EntrySpot 에 연결한다.

### 2.6 언어별 필요 기능

cpp 는 수동 등록만 유지한다.

- 자동 등록 기능은 추가하지 않는다.
- SPOT 샘플은 context 를 생성자 주입받고 `configure()` 안에서 context handler registry 를 쓰는 형태로 정렬한다.
- 문서에는 자동 등록을 제공하지 않는 이유를 언어 특성으로 설명한다.
- 기존 수동 등록 registry 의 동작과 샘플만 유지한다.

node 는 Channel 과 SPOT 모두 수동/자동 등록을 제공해야 한다.

- Channel 수동 등록
  - Nest builder 에 `requestHandler(...)`, `sendHandler(...)`, `publishHandler(...)` 를 추가한다.
  - 수동 등록 handler type 은 자동 등록 handler 와 같은 provider resolver 경로로 실행한다.
- SPOT 수동 등록
  - Spot/EntrySpot 의 기존 `configure()` 를 유지한다.
  - Spot/EntrySpot 은 기존처럼 context 를 주입받고, `configure()` 안에서 context 의 handler 등록 함수를 사용한다.
  - timer 는 기존 context 의 timer 등록 함수를 사용한다.
- SPOT 자동 등록
  - Entry Spot actor request, user Spot actor request 는 기존 decorator 흐름을 유지하되 수동 등록과 같은
    descriptor 로 변환한다.
  - actor send, SPOT packet, subscription 도 decorator metadata 와 discovery 변환 경로를 추가한다.
  - timer 자동 등록은 decorator 에 `name`, `periodMs`, `options` 를 받는 형태를 추가해야 한다. 이름과 주기가
    없는 `@zlinkSpotTimerHandler()` 만으로는 timer schedule 을 만들지 않는다.
  - 자동 등록 결과는 handler interface 의 Spot/EntrySpot 타입이 node 에 등록된 타입과 맞을 때만 연결한다.
- 공통 runtime 연결
  - 수동/자동 등록 descriptor 는 같은 중복 검사와 같은 dispatcher registry 로 들어가야 한다.
  - handler instance 생성은 `ModuleRef` 또는 provider resolver 경로를 사용한다.

dotnet 은 Channel 과 SPOT 모두 수동/자동 등록을 제공해야 한다.

- Channel 은 현재 수동/자동 등록 표면을 유지한다.
- SPOT 수동 등록은 기존 `Configure()` 안의 `Context.Handlers.Add...` 와 `Context.AddTimer...` 흐름을 유지한다.
- SPOT 자동 등록
  - `AddHandlersFromAssembly*` scan 결과에 SPOT packet, subscription, actor send/request, timer handler
    descriptor 를 포함한다.
  - scan 된 descriptor 는 handler interface 의 Spot/EntrySpot 타입을 기준으로 등록된 Spot/EntrySpot 에
    연결한다.
  - 선택된 descriptor 는 `Configure()` 에서 context 로 수동 등록했을 때와 같은
    runtime registration 으로 변환한다.
  - timer 자동 등록은 attribute/interface descriptor 에 timer 이름, 주기, option 을 담을 수 있어야 한다.

java 는 Channel 과 SPOT 모두 수동/자동 등록을 제공해야 한다.

- Channel 은 현재 수동/자동 등록 표면을 유지한다.
- SPOT 수동 등록은 현재 Spot/EntrySpot context handler registry 흐름을 유지한다.
- SPOT 자동 등록
  - `addHandlersFromPackageOf(...)` scan 결과에 SPOT packet, subscription, actor send/request, timer handler
    descriptor 를 포함한다.
  - scan 된 descriptor 는 handler interface 의 Spot/EntrySpot 타입을 기준으로 등록된 Spot/EntrySpot 에
    연결한다.
  - 선택된 descriptor 는 현재 수동 registry 등록과 같은 runtime registration 으로 변환한다.
  - timer 자동 등록은 annotation 에 timer 이름, 주기, option 을 담을 수 있어야 한다.

## 3. 언어별 등록 표면 상세

이 섹션은 구현 누락을 막기 위한 체크리스트다. 각 언어는 아래 항목을 모두 제공해야 한다. cpp 는 예외로
수동 등록만 제공한다.
아래 예시에 나온 이름이 현재 코드에 없으면 해당 이름과 같은 의미의 public API 를 추가한다. 이미 같은
의미의 API 가 있으면 기존 이름을 우선 사용하고, 문서 예시는 그 이름에 맞춰 갱신한다.

등록 API 는 이미 알 수 있는 정보를 다시 요구하지 않는다.

- handler type 으로 알 수 있는 값은 다시 쓰지 않는다.
- handler interface 의 generic type 으로 알 수 있는 Spot/EntrySpot/Actor 타입은 annotation 에 다시 쓰지 않는다.
- handler group 은 자동 등록에서 scan/discovery 결과를 선택하는 이름이다. 수동 등록은 호출된
  Channel builder 또는 Spot/EntrySpot context 자체가 범위를 정하므로 group 을 따로 받지 않는다.
- 자동 등록에서 handler group 이 필요하면 scan 단계에서 한 번만 지정하는 간략 API 를 추가한다.
  기존 handler 별 group decorator/attribute/annotation 은 호환을 위해 유지하되, 새 샘플에서는 반복해서 쓰지 않는다.
- Spot node builder 에서는 어떤 언어에서도 SPOT handler 를 등록하지 않는다.
  Channel builder 의 handler 등록과 Spot node builder 의 Spot/EntrySpot factory 등록은 서로 다른 표면이다.
  SPOT handler 는 항상 Spot/EntrySpot 의 `configure()` context 또는 자동 등록 scan 결과로만 연결한다.
- packet name, topic, timer name, timer period 처럼 handler type 만으로 알 수 없는 값만 명시한다.

actor handler 이름은 아래처럼 정렬한다.

- actor send 는 응답을 만들지 않는 actor packet handler 다.
- actor request 는 응답을 만드는 actor packet handler 다.
- node, dotnet, java 의 새 public 등록 표면은 `actorSend` / `actorRequest` 계열 이름을 쓴다.
- node, dotnet, java 의 자동 등록도 send/request 를 decorator, attribute, annotation 이름으로 구분한다.
  actor send/request 자동 등록 인자는 packet name 만 받으며, Spot/EntrySpot/Actor 타입은 handler interface 에서 읽는다.
- cpp 의 기존 `add_actor_packet` 은 메서드 인자의 context 타입으로 send/request 를 구분하므로 유지한다.
  문서와 샘플에서는 이 함수가 actor send 와 actor request 를 모두 등록하는 통합 API 라고 설명한다.
  따라서 send/request 판별 기준은 cpp 는 handler 메서드의 context 타입이고, node/dotnet/java 는 등록 함수나
  decorator, attribute, annotation 이름이다.

EntrySpot 연결 기준은 등록 방식에 따라 다르다. 자동 등록에서는 handler interface 의 Spot/EntrySpot 타입으로
연결 대상을 고르고, 수동 등록에서는 호출된 `configure()` 의 context 가 EntrySpot 인지 user Spot 인지로
연결 대상을 정한다.

Channel handler 등록과 SPOT handler 등록은 같은 packet 이름을 쓰더라도 서로 다른 registry 에 들어간다.
Channel 은 Channel builder 에서 request/send/publish handler 를 등록하고, SPOT 은 Spot/EntrySpot context 에서
packet/subscription/actor/timer handler 를 등록한다.

timer 는 packet handler registry 에 넣지 않고 context 의 schedule 등록으로 취급한다. 수동 등록은
`context.add_timer` / `Context.AddTimer` / `context().addTimer` 를 쓰고, 자동 등록은 timer 이름, 주기,
필요한 option 을 metadata 로 선언한 뒤 같은 schedule registration 으로 변환한다.

### 3.1 cpp

cpp 는 자동 등록을 추가하지 않는다.

유지할 수동 등록 interface:

```cpp
class handler_registry_t {
public:
    template <class TRequest, class TReply, class THandler>
    handler_registry_t& on_request(std::string_view packet_name, THandler handler);

    template <class TMessage, class THandler>
    handler_registry_t& on_send(std::string_view packet_name, THandler handler);

    template <class TEvent, class THandler>
    handler_registry_t& on_event(std::string_view topic, THandler handler);
};

class spot_handler_registry_t {
public:
    template <auto Method>
    spot_handler_registry_t& add_handler(std::string_view packet_name);

    template <auto Method>
    spot_handler_registry_t& add_subscribe(std::string_view topic);

    template <auto Method>
    spot_handler_registry_t& add_actor_packet(std::string_view packet_name);
};

class spot_context_t {
public:
    spot_handler_registry_t& handlers();

    template <auto Method>
    spot_context_t& add_timer(
        std::string_view name,
        std::chrono::milliseconds period);
};
```

`add_actor_packet` 은 등록 대상 메서드가 `spot_actor_send_context_t` 를 받으면 actor send 로,
`spot_actor_request_context_t` 를 받으면 actor request 로 동작한다. cpp 는 이 정보를 member pointer
시그니처에서 얻을 수 있으므로 별도 `add_actor_send` / `add_actor_request` 함수를 추가하지 않는다.
Spot node builder 는 EntrySpot 과 Spot factory 만 등록하고, SPOT handler 는 항상 `configure()` 안에서
context 를 통해 등록한다.

Channel 수동 등록:

```cpp
framework.handlers().on_request<CreateGameReq, CreateGameRes>(
    "create-game",
    &create_game_handler::handle);
```

SPOT 수동 등록:

```cpp
class play_entry_spot {
public:
    explicit play_entry_spot(spot_context_t& context)
        : context_(context)
    {
    }

    void configure()
    {
        context_.handlers().add_actor_packet<&play_entry_spot::on_join_game>("join-game");
    }

private:
    spot_context_t& context_;
};

class room_spot {
public:
    explicit room_spot(spot_context_t& context)
        : context_(context)
    {
    }

    void configure()
    {
        context_.handlers().add_handler<&room_spot::on_room_packet>("room-command");
        context_.handlers().add_subscribe<&room_spot::on_room_event>("room-events");
        context_.handlers().add_actor_packet<&room_spot::on_actor_message>("actor-left");
        context_.add_timer<&room_spot::on_tick>(
            "room-tick",
            std::chrono::milliseconds(200));
    }

private:
    spot_context_t& context_;
};
```

자동 등록 표면:

- 제공하지 않는다.
- macro, static initializer, build-time scan 을 공식 API 로 추가하지 않는다.

### 3.2 node

등록할 interface:

```ts
interface ZLinkNestClientServerChannelBuilder {
  requestHandler(packetName: string, handlerType: Type): this;
  sendHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}

interface ZLinkNestFanoutChannelBuilder {
  publishHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}

interface ZLinkNestRouterMeshBuilder {
  sendHandler(packetName: string, handlerType: Type): this;
  requestHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}

interface ZLinkSpotHandlerRegistry {
  packet(packetName: string, handler: Type): this;
  subscribe(topic: string, handler: Type): this;
  actorSend(packetName: string, handler: Type): this;
  actorRequest(packetName: string, handler: Type): this;
}
```

Channel 수동 등록:

```ts
zlinkFramework()
  .clientServerChannel(SampleNames.playChannel, (channel) => channel
    .server(config.playEndpoint)
    .requestHandler(PacketNames.createGameReq, CreateGameHandler)
    .sendHandler(PacketNames.playerLeftNotify, PlayerLeftHandler))
  .fanoutChannel(SampleNames.eventsChannel, (channel) => channel
    .subscriber()
    .publishHandler(PacketNames.roomChangedNotify, RoomChangedHandler))
  .routerMesh(SampleNames.routeChannel, (route) => route
    .sendHandler(PacketNames.actorLeftNotify, ActorLeftRouteHandler)
    .requestHandler(PacketNames.actorLookupReq, ActorLookupRouteHandler));
```

Channel 자동 등록:

```ts
@zlinkRequestHandler(PacketNames.createGameReq)
class CreateGameHandler implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {}

@zlinkSendHandler(PacketNames.playerLeftNotify)
class PlayerLeftHandler implements ZLinkSendHandler<PlayerLeftNotify> {}

@zlinkPublishHandler(PacketNames.roomChangedNotify)
class RoomChangedHandler implements ZLinkPublishHandler<RoomChangedNotify> {}
```

Channel 자동 등록 scan 구성:

```ts
zlinkDiscoverProviders({
  group: 'play.channel',
  providers: [
    CreateGameHandler,
    PlayerLeftHandler
  ]
});

zlinkFramework()
  .clientServerChannel(SampleNames.playChannel, (channel) => channel
    .server(config.playEndpoint)
    .handlerGroup('play.channel'));
```

SPOT 수동 등록:

```ts
class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;

  configure(): void {
    this.context!.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler);
  }
}

class TicTacToeGameSpot implements ZLinkSpot {
  readonly context?: ZLinkSpotContext;

  async configure(): Promise<void> {
    this.context!.handlers.packet(PacketNames.roomCommand, RoomCommandHandler);
    this.context!.handlers.subscribe(SampleNames.roomEvents, RoomEventHandler);
    this.context!.handlers.actorSend(PacketNames.actorLeftNotify, ActorLeftHandler);
    this.context!.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler);
    await this.context!.addTimer(
      'game-tick',
      200,
      TicTacToeGameTimerHandler
    );
  }
}
```

SPOT 자동 등록:

```ts
@zlinkSpotPacketHandler(PacketNames.roomCommand)
class RoomCommandHandler implements ZLinkSpotPacketHandler<TicTacToeGameSpot, RoomCommand> {}

@zlinkSpotSubscriptionHandler(SampleNames.roomEvents)
class RoomEventHandler implements ZLinkSpotSubscriptionHandler<TicTacToeGameSpot, RoomChanged> {}

@zlinkSpotActorSendHandler(PacketNames.actorLeftNotify)
class ActorLeftHandler implements ZLinkSpotActorSendHandler<TicTacToeGameSpot, PlayActor, ActorLeftNotify> {}

@zlinkSpotActorRequestHandler(PacketNames.placeMarkReq)
class PlayActorPlaceMarkHandler
  implements ZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayActor, PlaceMarkReq, PlaceMarkRes> {}

@zlinkSpotActorRequestHandler(PacketNames.joinGameReq)
class PlayActorJoinGameHandler
  implements ZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayActor, JoinGameReq, JoinGameRes> {}

@zlinkSpotTimerHandler({
  name: 'game-tick',
  periodMs: 200
})
class TicTacToeGameTimerHandler implements ZLinkSpotTimerHandler<TicTacToeGameSpot> {}
```

SPOT 자동 등록 scan 구성:

```ts
zlinkDiscoverProviders({
  group: 'play.spot',
  providers: [
    RoomCommandHandler,
    RoomEventHandler,
    ActorLeftHandler,
    PlayActorPlaceMarkHandler,
    PlayActorJoinGameHandler,
    TicTacToeGameTimerHandler
  ]
});

zlinkFramework()
  .spotNode(SampleNames.playSpotNode, (spot) => spot
    .entrySpot(PlayEntrySpot)
    .spotFactory(TicTacToeGameSpot));
```

### 3.3 dotnet

등록할 interface:

```csharp
public interface IZLinkClientServerChannelBuilder
{
    void AddRequestHandler<THandler>();
    void AddSendHandler<THandler>();
    void AddHandlerGroup(string groupName);
}

public interface IZLinkFanoutChannelBuilder
{
    void AddPublishHandler<THandler>();
    void AddHandlerGroup(string groupName);
}

public interface IZLinkRouteMeshChannelBuilder
{
    void AddSendHandler<THandler>();
    void AddRequestHandler<THandler>();
    void AddHandlerGroup(string groupName);
}

public interface IZLinkSpotHandlerRegistry
{
    void AddPacket<THandler>(string packetName);
    void AddSubscribe<THandler>(string topic);
    void AddActorSend<THandler>(string packetName);
    void AddActorRequest<THandler>(string packetName);
}

public interface IZLinkSpotContext
{
    IZLinkSpotHandlerRegistry Handlers { get; }
    void AddTimer<THandler>(string name, TimeSpan period, ZLinkTimerOptions? options = null);
}
```

Channel 수동 등록:

```csharp
options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
{
    channel.Server(topology.PlayEndpoint);
    channel.AddRequestHandler<CreateGameHandler>();
    channel.AddSendHandler<PlayerLeftHandler>();
});

options.AddFanoutChannel(SampleNames.EventsChannel, channel =>
{
    channel.Subscriber();
    channel.AddPublishHandler<RoomChangedHandler>();
});
```

Channel 자동 등록:

```csharp
[ZLinkRequest(PacketName = PacketNames.CreateGameReq)]
internal sealed class CreateGameHandler : IZLinkRequestHandler<CreateGameReq, CreateGameRes>
{
}

[ZLinkSend(PacketName = PacketNames.PlayerLeftNotify)]
internal sealed class PlayerLeftHandler : IZLinkSendHandler<PlayerLeftNotify>
{
}

[ZLinkPublish(PacketName = PacketNames.RoomChangedNotify)]
internal sealed class RoomChangedHandler : IZLinkPublishHandler<RoomChangedNotify>
{
}
```

Channel 자동 등록 scan 구성:

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddHandlersFromAssemblyOf<CreateGameHandler>("play.channel");
    options.AddClientServerChannel(SampleNames.PlayChannel, channel =>
    {
        channel.Server(topology.PlayEndpoint);
        channel.AddHandlerGroup("play.channel");
    });
});
```

SPOT 수동 등록:

```csharp
public sealed class PlayEntrySpot : IZLinkEntrySpot
{
    public void Configure()
    {
        Context.Handlers.AddActorRequest<PlayActorJoinGameHandler>(PacketNames.JoinGameReq);
    }
}

public sealed class TicTacToeGameSpot : IZLinkSpot
{
    public void Configure()
    {
        Context.Handlers.AddPacket<RoomCommandHandler>(PacketNames.RoomCommand);
        Context.Handlers.AddSubscribe<RoomEventHandler>(SampleNames.RoomEvents);
        Context.Handlers.AddActorSend<ActorLeftHandler>(PacketNames.ActorLeftNotify);
        Context.Handlers.AddActorRequest<PlayActorPlaceMarkHandler>(PacketNames.PlaceMarkReq);
        Context.AddTimer<TicTacToeGameTimerHandler>("game-tick", TimeSpan.FromMilliseconds(200));
    }
}
```

SPOT 자동 등록:

```csharp
[ZLinkSpotPacketHandler(PacketNames.RoomCommand)]
internal sealed class RoomCommandHandler : IZLinkSpotPacketHandler<TicTacToeGameSpot, RoomCommand>
{
}

[ZLinkSpotSubscriptionHandler(SampleNames.RoomEvents)]
internal sealed class RoomEventHandler : IZLinkSpotSubscriptionHandler<TicTacToeGameSpot, RoomChanged>
{
}

[ZLinkSpotActorSendHandler(PacketNames.ActorLeftNotify)]
internal sealed class ActorLeftHandler
    : IZLinkSpotActorSendHandler<TicTacToeGameSpot, PlayActor, ActorLeftNotify>
{
}

[ZLinkSpotActorRequestHandler(PacketNames.PlaceMarkReq)]
internal sealed class PlayActorPlaceMarkHandler
    : IZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayActor, PlaceMarkReq, PlaceMarkRes>
{
}

[ZLinkSpotActorRequestHandler(PacketNames.JoinGameReq)]
internal sealed class PlayActorJoinGameHandler
    : IZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayActor, JoinGameReq, JoinGameRes>
{
}

[ZLinkSpotTimerHandler("game-tick", 200)]
internal sealed class TicTacToeGameTimerHandler : IZLinkSpotTimerHandler<TicTacToeGameSpot>
{
}
```

SPOT 자동 등록 scan 구성:

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddHandlersFromAssemblyOf<TicTacToeGameSpot>("play.spot");
    options.AddSpotNode(SampleNames.PlaySpotNode, spot =>
    {
        spot.AddEntrySpot<PlayEntrySpot>();
        spot.AddSpotFactory<TicTacToeGameSpot>();
    });
});
```

### 3.4 java

등록할 interface:

```java
public interface ClientServerChannelBuilder {
    void addRequestHandler(Class<?> handlerType);
    void addSendHandler(Class<?> handlerType);
    void addHandlerGroup(String groupName);
}

public interface FanoutChannelBuilder {
    void addPublishHandler(Class<?> handlerType);
    void addHandlerGroup(String groupName);
}

public interface RouteMeshChannelBuilder {
    void addSendHandler(Class<?> handlerType);
    void addRequestHandler(Class<?> handlerType);
    void addHandlerGroup(String groupName);
}

public interface ZLinkSpotHandlerRegistry {
    void addPacket(Class<?> handlerType, String packetName);
    void addSubscribe(Class<?> handlerType, String topic);
    void addActorSend(Class<?> handlerType, String packetName);
    void addActorRequest(Class<?> handlerType, String packetName);
}

public interface ZLinkSpotContext {
    ZLinkSpotHandlerRegistry handlers();
    void addTimer(String name, Duration period, Class<?> handlerType, ZLinkTimerOptions options);
}
```

Channel 수동 등록:

```java
options.addClientServerChannel(SampleNames.PLAY_CHANNEL, channel -> {
    channel.server(topology.playEndpoint());
    channel.addRequestHandler(CreateGameHandler.class);
    channel.addSendHandler(PlayerLeftHandler.class);
});

options.addFanoutChannel(SampleNames.EVENTS_CHANNEL, channel -> {
    channel.subscriber();
    channel.addPublishHandler(RoomChangedHandler.class);
});
```

Channel 자동 등록:

```java
@ZLinkRequest(packetName = PacketNames.CREATE_GAME_REQ)
public final class CreateGameHandler implements ZLinkRequestHandler<CreateGameReq, CreateGameRes> {
}

@ZLinkSend(packetName = PacketNames.PLAYER_LEFT_NOTIFY)
public final class PlayerLeftHandler implements ZLinkSendHandler<PlayerLeftNotify> {
}

@ZLinkPublish(packetName = PacketNames.ROOM_CHANGED_NOTIFY)
public final class RoomChangedHandler implements ZLinkPublishHandler<RoomChangedNotify> {
}
```

Channel 자동 등록 scan 구성:

```java
options.addHandlersFromPackageOf(CreateGameHandler.class, "play.channel");
options.addClientServerChannel(SampleNames.PLAY_CHANNEL, channel -> {
    channel.server(topology.playEndpoint());
    channel.addHandlerGroup("play.channel");
});
```

SPOT 수동 등록:

```java
public final class PlayEntrySpot implements ZLinkEntrySpot {
    @Override
    public void configure() {
        context().handlers().addActorRequest(
            PlayActorJoinGameHandler.class,
            PacketNames.JOIN_GAME_REQ);
    }
}

public final class TicTacToeGameSpot implements ZLinkSpot {
    @Override
    public void configure() {
        context().handlers().addPacket(RoomCommandHandler.class, PacketNames.ROOM_COMMAND);
        context().handlers().addSubscribe(RoomEventHandler.class, SampleNames.ROOM_EVENTS);
        context().handlers().addActorSend(
            ActorLeftHandler.class,
            PacketNames.ACTOR_LEFT_NOTIFY);
        context().handlers().addActorRequest(
            PlayActorPlaceMarkHandler.class,
            PacketNames.PLACE_MARK_REQ);
        context().addTimer(
            "game-tick",
            Duration.ofMillis(200),
            TicTacToeGameTimerHandler.class,
            ZLinkTimerOptions.defaults());
    }
}
```

SPOT 자동 등록:

```java
@ZLinkSpotPacketHandler(PacketNames.ROOM_COMMAND)
public final class RoomCommandHandler implements ZLinkSpotPacketHandler<TicTacToeGameSpot, RoomCommand> {
}

@ZLinkSpotSubscriptionHandler(SampleNames.ROOM_EVENTS)
public final class RoomEventHandler implements ZLinkSpotSubscriptionHandler<TicTacToeGameSpot, RoomChanged> {
}

@ZLinkSpotActorSendHandler(PacketNames.ACTOR_LEFT_NOTIFY)
public final class ActorLeftHandler
    implements ZLinkSpotActorSendHandler<TicTacToeGameSpot, PlayActor, ActorLeftNotify> {
}

@ZLinkSpotActorRequestHandler(PacketNames.PLACE_MARK_REQ)
public final class PlayActorPlaceMarkHandler
    implements ZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayActor, PlaceMarkReq, PlaceMarkRes> {
}

@ZLinkSpotActorRequestHandler(PacketNames.JOIN_GAME_REQ)
public final class PlayActorJoinGameHandler
    implements ZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayActor, JoinGameReq, JoinGameRes> {
}

@ZLinkSpotTimerHandler(name = "game-tick", periodMillis = 200)
public final class TicTacToeGameTimerHandler implements ZLinkSpotTimerHandler<TicTacToeGameSpot> {
}
```

SPOT 자동 등록 scan 구성:

```java
options.addHandlersFromPackageOf(TicTacToeGameSpot.class, "play.spot");
options.addSpotMesh(SampleNames.PLAY_SPOT, mesh -> {
    mesh.addNode(SampleNames.PLAY_SPOT_NODE, node -> {
        node.addEntrySpot(PlayEntrySpot.class);
        node.addSpotFactory(TicTacToeGameSpot.class);
    });
});
```

## 4. Target API

### 4.1 node Channel 수동 등록

Nest DSL 에 명시 등록 메서드를 추가한다.

```ts
zlinkFramework()
  .clientServerChannel(SampleNames.playChannel, (channel) => channel
    .server(config.playEndpoint)
    .requestHandler(PacketNames.ensurePlayerActorReq, EnsurePlayerActorHandler)
    .requestHandler(PacketNames.matchBingoReq, MatchBingoChannelHandler)
    .requestHandler(PacketNames.submitBingoCardReq, SubmitBingoCardChannelHandler))
  .fanoutChannel(SampleNames.eventsChannel, (channel) => channel
    .subscriber()
    .publishHandler(PacketNames.roomChangedNotify, RoomChangedHandler))
  .routerMesh(SampleNames.routeChannel, (route) => route
    .bind(config.routeEndpoint)
    .sendHandler(PacketNames.actorLeftNotify, ActorLeftRouteHandler)
    .requestHandler(PacketNames.actorLookupReq, ActorLookupRouteHandler));
```

제안 interface:

```ts
interface ZLinkNestClientServerChannelBuilder {
  requestHandler(packetName: string, handlerType: Type): this;
  sendHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}

interface ZLinkNestFanoutChannelBuilder {
  publishHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}

interface ZLinkNestRouterMeshBuilder {
  sendHandler(packetName: string, handlerType: Type): this;
  requestHandler(packetName: string, handlerType: Type): this;
  handlerGroup(groupName: string): this;
}
```

Node framework core 는 현재 handler object 를 받는다. Nest 수동 등록은 handler type 을 받아서
runtime 생성 시 `ModuleRef` 로 instance 를 찾아 호출하는 adapter 를 만든다. 자동 등록도 같은 adapter 를
사용하게 해서 수동/자동의 실행 경로를 맞춘다.

### 4.2 node SPOT 수동 등록

SPOT handler 는 module builder 가 아니라 Spot/EntrySpot 클래스 안의 `configure()` 에서 등록한다.
SPOT handler 는 SPOT 자체의 메시지 처리 계약에 가깝기 때문에, channel 구성처럼 module 에서 줄줄이
등록하지 않는다. Spot/EntrySpot 은 기존처럼 context 를 주입받고, `configure()` 안에서 context 의 handler
등록 함수를 사용한다.

```ts
class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;

  configure(): void {
    this.context!.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler);
  }
}

class TicTacToeGameSpot implements ZLinkSpot {
  readonly context?: ZLinkSpotContext;

  async configure(): Promise<void> {
    this.context!.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler);
    await this.context!.addTimer(
      'game-tick',
      200,
      TicTacToeGameTimerHandler
    );
  }
}
```

제안 interface:

```ts
interface ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;
  configure?(): void | Promise<void>;
}

interface ZLinkSpot {
  readonly context?: ZLinkSpotContext;
  configure?(): void | Promise<void>;
}

interface ZLinkSpotHandlerRegistry {
  packet(packetName: string, handler: Type): this;
  subscribe(topic: string, handler: Type): this;
  actorSend(packetName: string, handler: Type): this;
  actorRequest(packetName: string, handler: Type): this;
}
```

`configure()` 는 optional 이다. 자동 등록만 쓰는 Spot/EntrySpot 은 이 함수를 구현하지 않아도 된다.
runtime 은 `configure()` 가 없으면 빈 구현처럼 취급한다. 현재 node 계약에도 `configure()` hook 이
있으므로 새 함수 이름을 만들지 않는다.

현재 node 의 configuration options 는 SPOT actor request 중심이고, runtime 내부에는 handler registry 가 있다.
target 은 기존 context 기반 API 를 유지하면서, SPOT packet, subscription, actor send/request, timer 를 모두
수동/자동 등록 표면으로 제공하는 것이다.

구현 시 주의할 점:

- 현재 runtime 은 `spot.configure?.()` 와 `entrySpot.configure?.()` 를 동기 호출한다. target 은
  `await spot.configure?.()` 처럼 `Promise<void>` 를 허용해야 한다.
- `context.handlers.packet(...)`, `subscribe(...)`, `actorSend(...)`, `actorRequest(...)` 로 등록한 항목은
  기존 handler registry 에만 쌓이면 동작하지 않는다. 각 항목은 실제 packet/subscription/actor dispatcher
  registry 에도 같은 descriptor 로 반영해야 한다.
- 수동 등록 handler instance 는 bootstrap 때 미리 만들지 말고 현재 provider resolver 흐름과 맞춰 resolve 한다.
  그래야 request scope, Nest provider lifecycle, 테스트 mock 교체가 자동 등록과 같은 방식으로 동작한다.
- 수동 등록과 자동 등록은 bootstrap 또는 SPOT 활성화 시점에 같은 중복 검사 함수를 통과해야 한다.

### 4.3 node SPOT 자동 등록

자동 등록은 decorator metadata 를 scan 해서 수동 등록과 같은 descriptor 로 변환한다.
EntrySpot actor request 도 같은 decorator 를 쓴다. EntrySpot 여부는 handler interface 타입에서 읽을 수
있으므로 decorator 이름으로 다시 표현하지 않는다.

```ts
@zlinkSpotActorRequestHandler(PacketNames.matchBingoReq)
class MatchBingoActorHandler
  implements ZLinkEntrySpotActorRequestHandler<BingoEntrySpot, PlayerActor, MatchBingoReq, MatchBingoRes> {
  async handle(
    entrySpot: BingoEntrySpot,
    actor: PlayerActor,
    context: ZLinkSpotActorRequestContext,
    request: MatchBingoReq
  ): Promise<MatchBingoRes> {
    return await entrySpot.matchActor(actor, request);
  }
}
```

추가할 decorator 예시:

```ts
@zlinkSpotActorSendHandler(PacketNames.actorLeftNotify)
class ActorLeftHandler implements ZLinkSpotActorSendHandler<BingoRoom, PlayerActor, ActorLeftNotify> {}

@zlinkSpotPacketHandler(PacketNames.roomCommand)
class RoomCommandHandler implements ZLinkSpotPacketHandler<BingoRoom, RoomCommand> {}

@zlinkSpotSubscriptionHandler(SampleNames.roomEvents)
class RoomEventHandler implements ZLinkSpotSubscriptionHandler<BingoRoom, RoomChanged> {}

@zlinkSpotTimerHandler({
  name: 'room-tick',
  periodMs: 200
})
class BingoRoomTimerHandler implements ZLinkSpotTimerHandler<BingoRoom> {}
```

자동 등록은 다음 규칙을 유지한다.

- decorator 는 handler 의 metadata 만 선언한다.
- `zlinkDiscoverProviders(...)` 는 provider 등록 편의 기능이며 handler group 을 한 번만 받는다.
- `ZLinkModule` 은 discovery 결과를 명시 registration 구조로 변환한다.
- timer decorator 는 handler provider 표시뿐 아니라 timer 이름, 주기, 필요한 option 을 포함해야 한다.
- 수동 등록과 자동 등록이 같은 packet 을 등록하면 bootstrap 시점에 중복 오류를 낸다.

### 4.4 dotnet SPOT 자동 등록

dotnet 에는 SPOT 자동 등록을 Channel 자동 등록과 같은 관점으로 추가한다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddHandlersFromAssemblyOf<BingoRoom>("play.spot");

    options.AddSpotNode(SampleNames.RoomSpotType, spot =>
    {
        spot.EnableRouter(router => router.Bind(topology.PlaySpotRouterEndpoint));
        spot.AddEntrySpot<BingoEntrySpot>();
        spot.AddSpotFactory<BingoRoom>();
    });
});
```

handler 예시:

```csharp
[ZLinkSpotActorRequestHandler(PacketNames.MatchBingoReq)]
internal sealed class MatchBingoActorHandler
    : IZLinkEntrySpotActorRequestHandler<BingoEntrySpot, PlayerActor, MatchBingoReq, MatchBingoRes>
{
    public async ValueTask<MatchBingoRes> HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        MatchBingoReq request,
        CancellationToken cancellationToken)
    {
        return await entrySpot.MatchActorAsync(actor, request, cancellationToken);
    }
}
```

제안 API:

```csharp
public interface IZLinkSpotNodeBuilder
{
    void AddEntrySpot<TEntrySpot>();
    void AddSpotFactory<TSpot>();
}
```

동작:

- `AddHandlersFromAssembly*` 로 수집한 SPOT handler descriptor 를 catalog 에 포함한다.
- handler interface 의 Spot/EntrySpot 타입이 SPOT node 에 등록된 타입과 맞으면 descriptor 를 연결한다.
- 선택된 handler 는 `Configure()` 에서 context 로 수동 등록했을 때와 같은 descriptor 로 변환한다.
- 수동 등록과 자동 등록 중복 packet 은 bootstrap 시점에 오류를 낸다.

### 4.5 java SPOT 자동 등록 정리

java 는 package scan 이 이미 있으므로 node 수준의 사용성을 목표로 정리한다.

```java
options.addHandlersFromPackageOf(BingoRoom.class, "play.spot");
options.addSpotMesh(SampleNames.ROOM_SPOT, spot -> {
    spot.enableRouter(router -> router.bind(topology.playSpotRouterEndpoint()));
    spot.addEntrySpot(BingoEntrySpot.class);
    spot.addSpotFactory(BingoRoom.class);
});
```

handler 예시:

```java
@ZLinkSpotActorRequestHandler(PacketNames.MATCH_BINGO_REQ)
public final class MatchBingoActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        BingoEntrySpot,
        PlayerActor,
        MatchBingoReq,
        MatchBingoRes> {

    @Override
    public CompletionStage<MatchBingoRes> handle(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        MatchBingoReq request) {
        return entrySpot.matchActor(actor, request);
    }
}
```

점검할 범위:

- actor request 자동 등록이 entry spot 과 user spot 양쪽에서 같은 규칙으로 동작하는지 확인한다.
- SPOT packet/request/subscription 자동 등록을 node 수준으로 제공한다.
- Spot node builder 에 handler 등록 API 를 추가하지 않는다. 자동 등록 descriptor 는 handler interface 의
  Spot/EntrySpot 타입으로 연결한다.
- 수동 등록과 자동 등록 중복 packet 의 오류 메시지를 Channel 과 맞춘다.

## 5. 공식 샘플 고정 정책

공식 Node TS 샘플은 두 등록 방식을 모두 보여주도록 역할을 고정한다. 이 정책은
`Bingo.Ts` 와 `TicTacToe.Ts` 에 대한 정책이다. cpp 는 자동 등록을 제공하지 않으므로 cpp Bingo 샘플은
이 정책의 적용 대상이 아니며, cpp 샘플은 계속 수동 등록 예시로 유지한다.

### 5.1 Bingo.Ts: 자동 handler 등록 샘플

Bingo.Ts 는 자동 등록을 보여주는 샘플로 유지한다.

- `@zlinkRequestHandler`
- `@zlinkSpotActorRequestHandler`
- `@zlinkSpotActorSendHandler`
- `@zlinkSpotPacketHandler`
- `@zlinkSpotSubscriptionHandler`
- `@zlinkSpotTimerHandler`
- `zlinkDiscoverProviders(...)`

목표:

- handler 파일을 추가하면 provider discovery 와 decorator metadata 로 등록되는 흐름을 보여준다.
- `@zlinkSpotTimerHandler` 는 timer 이름과 주기를 함께 선언해서 timer schedule 까지 자동 등록한다.
- Registry/Discovery 기반 topology 샘플과 함께 “convention 기반 앱 구성”을 보여준다.
- README 에 “Bingo.Ts 는 자동 handler 등록 예시”라고 명시한다.

### 5.2 TicTacToe.Ts: 수동 handler 등록 샘플

TicTacToe.Ts 는 수동 등록을 보여주는 샘플로 고정한다.

수정 후 예시:

```ts
function createTicTacToePlayModule(config: {
  apiEndpoint: string;
  playEndpoint: string;
  playStreamEndpoint: string;
}) {
  class TicTacToePlayModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .server(config.playEndpoint)
            .requestHandler(PacketNames.createGameReq, CreateGameHandler))
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .client(config.apiEndpoint))
          .actorFactory(SampleNames.playerActorType, PlayActorFactory)
          .streamNode(SampleNames.playStream, (stream) => stream
            .bind(config.playStreamEndpoint)
            .registerSession(PlaySessionFactory))
          .spotNode(SampleNames.playSpotNode, (spot) => spot
            .entrySpot(PlayEntrySpot)
            .spotFactory(TicTacToeGameSpot))
          .build()
      })
    ],
    providers: [
      { provide: PLAY_STREAM_ENDPOINT, useValue: config.playStreamEndpoint },
      TicTacToeGameCreator,
      PlayActorFactory,
      PlayEntrySpot,
      PlaySessionFactory,
      CreateGameHandler,
      PlayActorJoinGameHandler,
      PlayActorPlaceMarkHandler,
      TicTacToeGameTimerHandler
    ]
  })(TicTacToePlayModule);

  return TicTacToePlayModule;
}
```

목표:

- `zlinkDiscoverProviders(...)` 를 TicTacToe.Ts 에서 제거한다.
- decorator 없이 provider 와 Channel builder 등록, 그리고 Spot/EntrySpot `configure()` 등록만으로
  동작하게 한다.
- README 에 “TicTacToe.Ts 는 수동 handler 등록 예시”라고 명시한다.

TicTacToe.Ts 의 SPOT handler 등록은 아래처럼 Spot/EntrySpot 안에 둔다.

```ts
class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context?: ZLinkEntrySpotContext;

  configure(): void {
    this.context!.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler);
  }
}

class TicTacToeGameSpot implements ZLinkSpot {
  readonly context?: ZLinkSpotContext;

  async configure(): Promise<void> {
    this.context!.handlers.actorRequest(PacketNames.placeMarkReq, PlayActorPlaceMarkHandler);
    await this.context!.addTimer(
      'game-tick',
      200,
      TicTacToeGameTimerHandler
    );
  }
}
```

## 6. 구현 단계

### Stage 1. 현재 계약 고정

1. node regression test 에 현재 정책을 명시한다.
   - Bingo.Ts 는 자동 등록을 사용한다.
   - TicTacToe.Ts 는 수동 등록으로 전환될 예정이다.
   - cpp 는 자동 등록 비대상이다.
2. dotnet/java/cpp/node 문서 또는 spec 에 현재 등록 방식 표를 추가한다.
3. 기존 sample regression 에 “자동 등록만 가능한 상태”를 실패로 잡는 테스트는 아직 넣지 않는다.

### Stage 2. node Channel 수동 등록 DSL 및 SPOT configure 보강

1. `ZLinkNestClientServerChannelBuilder`
   - `sendHandler(...)`
   - `requestHandler(...)`
2. `ZLinkNestFanoutChannelBuilder`
   - `publishHandler(...)`
3. `ZLinkNestRouterMeshBuilder`
   - `sendHandler(...)`
   - `requestHandler(...)`
4. `ZLinkEntrySpot` / `ZLinkSpot`
   - 기존 optional `configure()` hook 을 `void | Promise<void>` 로 확장한다.
   - hook 이 없으면 빈 구현처럼 처리한다.
   - runtime 의 `spot.configure?.()` 와 `entrySpot.configure?.()` 호출을 await 한다.
5. 기존 `ZLinkSpotHandlerRegistry` 에 SPOT handler 수동 등록 표면을 추가한다.
   - `packet(...)`
   - `subscribe(...)`
   - `actorSend(...)`
   - `actorRequest(...)`
6. timer 수동 등록은 기존 context timer 등록 함수로 유지한다.
7. `configure()` 에서 등록된 SPOT handler 를 실제 packet/subscription/actor dispatch registry 에 반영한다.
8. 수동 등록 handler 는 `ModuleRef` 를 통해 provider instance 를 얻어 호출한다.
9. 자동 등록과 수동 등록의 중복 packet/topic/timer name 검사를 같은 함수로 모은다.

### Stage 2-1. node SPOT 자동 등록 확장

1. SPOT actor send decorator 를 추가한다.
2. SPOT packet decorator 를 추가한다.
3. SPOT subscription decorator 를 추가한다.
4. timer decorator 가 `name`, `periodMs`, `options` 를 받을 수 있게 확장한다.
5. decorator 는 packet name, topic, timer metadata 만 받는다.
6. Spot/EntrySpot/Actor 타입은 handler interface 의 generic type 에서 읽는다.
7. `zlinkDiscoverProviders({ group, providers })` 간략 API 를 추가한다.
8. handler group 은 discovery 단계에서 한 번만 지정한다.
9. discovery 결과를 수동 등록과 같은 SPOT descriptor 로 변환한다.
10. 변환된 descriptor 는 handler interface 의 Spot/EntrySpot 타입이 등록된 타입과 맞을 때만 연결한다.

### Stage 3. TicTacToe.Ts 수동 등록 전환

1. Play/API/Spot handler 에서 decorator 의존을 제거한다.
2. `zlinkDiscoverProviders(...)` 사용을 제거한다.
3. module providers 에 handler class 를 직접 나열한다.
4. Channel handler 는 `zlinkFramework()` builder 에 직접 등록한다.
5. SPOT handler 는 Spot/EntrySpot 의 `configure()` 에 직접 등록한다.
6. sample regression 에 다음 규칙을 추가한다.
   - TicTacToe.Ts 는 `zlinkDiscoverProviders` 를 쓰지 않는다.
   - TicTacToe.Ts handler 파일은 `@zlinkRequestHandler` / `@zlinkSpot...Handler` 를 쓰지 않는다.
   - TicTacToe.Ts module 은 `.requestHandler(...)` 를 포함한다.
   - TicTacToe.Ts Spot/EntrySpot 은 `configure()` 를 포함한다.

### Stage 4. Bingo.Ts 자동 등록 고정

1. Bingo.Ts 는 decorator 와 discovery provider 사용을 유지한다.
2. sample regression 에 다음 규칙을 추가한다.
   - Bingo.Ts 는 `zlinkDiscoverProviders(...)` 를 사용한다.
   - Bingo.Ts 는 Channel decorator 와 SPOT packet/subscription/actor/timer decorator 를 사용한다.
   - Bingo.Ts handler 등록은 decorator 와 provider discovery 기반을 유지한다.

### Stage 5. dotnet SPOT 자동 등록 추가

1. `AddHandlersFromAssembly*` scan 결과에 SPOT handler descriptor 를 포함한다.
2. `AddHandlersFromAssemblyOf<TMarker>(string groupName)` 간략 overload 를 추가한다.
3. attribute 는 packet name, topic, timer metadata 만 받는다.
4. Spot/EntrySpot/Actor 타입은 handler interface 의 generic type 에서 읽는다.
5. handler group 은 assembly scan 단계에서 한 번만 지정한다.
6. SPOT node build 단계에서는 handler interface 의 Spot/EntrySpot 타입을 기준으로 descriptor 를 선택한다.
7. 선택한 descriptor 를 `Configure()` 에서 context 로 수동 등록했을 때와 같은 runtime registration 으로 변환한다.
8. 중복 packet, surface mismatch, spot type mismatch 오류를 추가한다.

### Stage 6. java SPOT 자동 등록 정리

1. 현재 package scan 이 SPOT packet, subscription, actor send/request, timer 를 entry/user spot 에 정확히
   연결하는지 테스트한다.
2. `addHandlersFromPackageOf(Class<?> markerType, String groupName)` 간략 overload 를 추가한다.
3. annotation 은 packet name, topic, timer metadata 만 받는다.
4. Spot/EntrySpot/Actor 타입은 handler interface 의 generic type 에서 읽는다.
5. handler group 은 package scan 단계에서 한 번만 지정한다.
6. 부족한 SPOT handler annotation 과 descriptor 변환을 추가한다.
7. node/dotnet 과 같은 중복 packet 오류 정책을 적용한다.

### Stage 7. 문서와 샘플 검증

1. cpp SPOT 샘플이 context 생성자 주입과 `configure()` 안의 context handler 등록을 쓰는지 확인한다.
2. `doc/spec/bindings/node/README` 또는 framework spec 에 등록 방식 표를 반영한다.
3. 각 언어 guide 에 다음 정책을 명시한다.
   - cpp: 수동 등록만 제공
   - node/dotnet/java: 수동 등록과 자동 등록 제공
4. 공식 샘플 설명을 고정한다.
   - Bingo.Ts: 자동 등록
   - TicTacToe.Ts: 수동 등록

## 7. 테스트 계획

### node

- builder unit test
  - Channel 수동 등록이 registration 의 `requestHandlers`, `publishHandlers`, route handlers 로 변환되는지 확인
- SPOT unit test
  - EntrySpot `configure()` 수동 등록이 SPOT handler dispatch 경로에 연결되는지 확인
  - Spot `configure()` 수동 등록이 SPOT handler dispatch 경로에 연결되는지 확인
  - async `configure()` 가 await 되는지 확인
  - `configure()` 가 없으면 빈 구현처럼 처리되는지 확인
  - timer 는 context timer 등록 함수로 등록되고 tick handler provider 가 호출되는지 확인
- Nest integration test
  - 수동 등록 handler 가 `ModuleRef` 로 resolve 되어 호출되는지 확인
  - SPOT packet, subscription, actor send/request 자동 등록 handler 가 호출되는지 확인
  - timer 자동 등록 handler 가 선언한 주기로 등록되는지 확인
  - 자동 등록 handler 와 수동 등록 handler 가 같은 packet 을 등록하면 실패하는지 확인
- sample regression
  - Bingo.Ts 자동 등록 고정
  - TicTacToe.Ts 수동 등록 고정
  - `run_samples.sh` 통과

### dotnet

- unit test
  - `AddHandlersFromAssembly*` 가 SPOT handler descriptor 를 수집하는지 확인
  - handler interface 의 Spot/EntrySpot 타입으로 descriptor 가 SPOT node 에 연결되는지 확인
- integration test
  - 자동 등록된 Entry Spot actor handler 호출
  - 자동 등록된 user spot packet/subscription/actor handler 호출
  - 자동 등록된 timer handler 호출
  - 수동 등록과 자동 등록 중복 packet 실패

### java

- unit test
  - `addHandlersFromPackageOf(...)` scan 결과가 SPOT handler catalog 에 포함되는지 확인
  - handler interface 의 Spot/EntrySpot 타입으로 descriptor 가 SPOT node 에 연결되는지 확인
- integration test
  - entry spot actor handler 자동 등록
  - user spot packet/subscription/actor handler 자동 등록
  - timer handler 자동 등록
  - 수동 등록과 자동 등록 중복 packet 실패

### cpp

- 새 자동 등록 테스트는 추가하지 않는다.
- 기존 수동 등록 테스트가 유지되는지만 확인한다.
- 문서 regression 에 “cpp 는 자동 등록 비대상”을 명시한다.

## 8. 완료 기준

1. node 는 Channel handler 와 SPOT handler 에 대해 수동 등록과 자동 등록을 제공한다.
2. dotnet 은 Channel handler 와 SPOT handler 에 대해 수동 등록과 자동 등록을 제공한다.
3. java 는 Channel handler 와 SPOT handler 에 대해 수동 등록과 자동 등록을 제공한다.
4. cpp 는 수동 등록만 제공하고, 자동 등록 비대상임을 문서에 명시한다.
5. Bingo.Ts 는 자동 등록 샘플로 고정된다.
6. TicTacToe.Ts 는 수동 등록 샘플로 고정된다.
7. 네 언어 문서가 같은 정책을 설명하되, cpp 의 예외를 언어 특성 차이로 분명히 적는다.
