<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework Spring Boot SPOT](./spring-boot-spot.ko.md) | [다음: Draft -- ZLink Framework Spring Boot STREAM](./spring-boot-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Java 묶음](./README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [STREAM](./spring-boot-stream.ko.md)

# Draft -- ZLink Framework Spring Boot Actor/Session

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework의 actor/session 기능을 Java와
> Kotlin에서 어떤 표면으로 포팅할지 정리한다.

## 1. 방향

Actor는 stateful application object다. 일반 handler처럼 메시지마다 새 객체로
처리하지 않고, actor id로 식별되는 같은 인스턴스가 여러 메시지를 받는다.

Actor 상태는 두 축으로 나눈다.

| 축 | 의미 |
|----|------|
| 위치 | Entry Spot 또는 user Spot 중 어디에서 실행되는가 |
| binding | STREAM session에 묶였는가 |

session binding은 client relay 경로일 뿐이다. actor가 실제로 어느 Spot에 있는지는
Entry Spot/user Spot 위치 축이 결정한다. session close가 actor를 자동으로 user Spot
밖으로 이동시키지 않는다.

## 2. 등록

```java
@Configuration
@EnableZLinkFramework
public class ActorConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions framework) {
        options.addActorFactory("player", PlayerActorFactory.class);

        options.addSpotMesh("game.stage", mesh -> {
            mesh.useDiscovery(discovery -> discovery.addRegistryEndpoint("tcp://registry1:5551"));
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addEntrySpot(GameEntrySpot.class);
                node.addSpotFactory(GameRoomSpot.class);
            });
        });

        options.addStreamNode("gateway", stream -> {
            stream.bind("tcp://0.0.0.0:7201");
            stream.attachActorGateway("play");
            stream.registerSession(ClientSession.class);
        });
    }
}
```

`attachActorGateway("play")`는 stream node가 session bind 전에 해당 SpotNode의
ActorGateway에 연결되어야 한다는 뜻이다. Java framework는 이 의미를 route mesh
channel packet으로 대신 구현하지 않는다.

## 3. Actor 계약

```java
public interface ZLinkActor {
    String actorId();
    ZLinkActorContext context();
    default void configure() {
    }
}

public interface ZLinkActorFactory {
    CompletionStage<ZLinkActor> create(
        String actorId,
        ZLinkActorContext context);
}

public interface ZLinkActorManager {
    CompletionStage<ZLinkActor> create(String actorId, String actorType);
    CompletionStage<Optional<ZLinkActor>> find(String actorId);
    CompletionStage<ZLinkActor> getOrCreate(String actorId, String actorType);
}
```

`actorType`은 application이 정하는 짧은 문자열 키다. 같은 actor id를 다른
actorType으로 다시 쓰면 설정 또는 런타임 오류로 실패해야 한다.

## 4. Session Binding

```java
public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bind(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bind(ZLinkActorRef actor);

    Optional<ZLinkSessionActor> find(String actorId);
}

public interface ZLinkSessionActor {
    String actorId();
    ZLinkActorRef ref();

    CompletionStage<Void> relay(
        ZLinkStreamHeader header,
        Message payload);

    CompletionStage<Void> notifyDisconnected();
}
```

session은 local actor instance 또는 framework actor locator인 `ZLinkActorRef`에
bind할 수 있다. remote binding은 binding 내부의 `ActorRef`나 actor route snapshot을
session public 입력으로 받지 않고, core ActorGateway와 logical actor handle을
사용한다.

## 5. Bound Session

actor에서 현재 client session으로 보내는 표면은 `ZLinkBoundSession`이다.
STREAM session은 framework가 만든 `ZLinkSessionContext`를 constructor로 받을 수 있다.
application session은 이 context의 `actors()`로 actor binding을 만들고, `client()`로
client reply 또는 push를 보낸다. sample 안에서 별도 session context나 bound session
stand-in을 만들어 이 경로를 대체하지 않는다.

```java
public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();
    boolean isJoined();
    ZLinkBoundSession boundSession();

    ZLinkSpot getSpot();
    <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType);

    ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request);
    ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid);
}

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);
    CompletionStage<ZLinkActorRef> submit();
}

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(
        Class<TReply> replyType);
}

public record ZLinkActorJoinResult<TReply>(
    int resultCode,
    ZLinkActorRef actor,
    TReply reply) {
}

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);
    CompletionStage<Void> disconnect();
}
```

`joinSpot(...)`은 actor가 Entry Spot 이후 실제 user Spot으로 들어가는 요청이다. 호출은
`CompletionStage`로 완료되며 framework는 backend `SpotNode.joinActor(...)` 결과를
받은 뒤 actor context의 `spotRid()`, `isJoined()`, `getSpot()` 상태를 갱신한다.
이 경로는 thread blocking helper를 제공하지 않는다. Kotlin에서는 같은 Java
`CompletionStage`를 `suspend` wrapper로 감싸서 사용한다.

`ZLinkBoundSession`은 server-to-client request API를 제공하지 않는다. client
request에 대한 응답은 actor request handler의 반환값과 원래 request correlation으로
처리한다.
`disconnect()`는 현재 actor에 묶인 client session을 backend binding에서 해제하고
actor context의 bound session을 비운다. 이 호출은 server가 session을 닫는 의미이므로
Spot actor disconnected callback을 대신 실행하지 않는다.

actor가 join한 SPOT의 실행 문맥 상태가 필요하면 `context.getSpot()` 또는 typed
`context.getSpot(MatchSpot.class)`로 현재 join된 user Spot 인스턴스를 가져온다. join
전이거나 Entry Spot 단계라면 user Spot이 없으므로 호출 가능 시점을 actor lifecycle에
맞춘다.

```java
MatchSpot spot = actor.context().getSpot(MatchSpot.class);
```

session actor의 `notifyDisconnected()`는 backend actor binding을 해제한 뒤,
그 binding이 actor context의 현재 bound session과 일치할 때만 disconnected lifecycle을
실행한다. 오래된 session binding에서 disconnect 알림이 늦게 도착해도 현재 bound
session과 disconnected lifecycle callback을 건드리지 않는다.
`relay(header, payload)`는 session이 받은 actor packet을 bound actor route로
전달한다. framework는 payload copy를 만들어 전송하므로 호출자가 넘긴 `Message`의
소유권은 호출자에게 남아 있다.

## 6. Handler

Entry Spot actor handler와 user Spot actor handler는 분리한다. Entry Spot은 인증,
초기 actor 생성, target Spot 선택 같은 입구 로직을 맡고, user Spot은 실제 room,
stage, zone 안의 domain packet을 처리한다.

```java
public interface ZLinkEntrySpotActorRequestHandler<
    TEntrySpot extends ZLinkEntrySpot,
    TActor extends ZLinkActor,
    TRequest,
    TReply> {
    CompletionStage<TReply> handleAsync(
        TEntrySpot entrySpot,
        TActor actor,
        ZLinkSpotActorRequestContext context,
        TRequest request);
}

public interface ZLinkSpotActorSendHandler<
    TSpot extends ZLinkSpot,
    TActor extends ZLinkActor,
    TMessage> {
    CompletionStage<Void> handleAsync(
        TSpot spot,
        TActor actor,
        ZLinkSpotActorSendContext context,
        TMessage message);
}
```

## 7. Runtime 규칙

- actor 생성은 `ZLinkActorManager`를 통해 명시적으로 수행한다.
- actor packet handler는 actor class가 아니라 Entry Spot 또는 user Spot registry에
  등록한다.
- session callback에서 받은 payload는 callback 동안 빌려온 값이다. relay할 수는
  있지만 임의로 dispose하거나 ownership을 이동하지 않는다.
- client close는 session binding cleanup만 수행한다. actor disconnect callback이
  필요하면 application이 `notifyDisconnected()`를 호출한다.
- remote actor로 relay할 때 Java framework는 backend stream의 bound actor send를
  사용한다.
- route mesh channel은 application Spot route egress용이다. session actor relay
  설정으로 해석하지 않는다.

## 8. Kotlin 사용 표면

Kotlin은 Java contract 위에 coroutine extension을 얹는다.

```kotlin
val actor = actorManager.getOrCreate("player-42", "player")
session.context.actors.bind(actor)

actor.context.boundSession.send(PlayerJoined(...)).submit()
```

Kotlin DSL은 등록 코드를 짧게 만들 뿐이고, actor lifecycle 의미는 Java contract와
같다.
