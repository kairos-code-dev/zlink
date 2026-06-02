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
public class ActorConfig implements ZLinkFrameworkOptionsCustomizer {
    @Override
    public void customize(ZLinkFrameworkOptions options) {
        options.addActorFactory("player", PlayerActorFactory.class);

        options.addSpotMesh("game.stage", mesh -> {
            mesh.useDiscovery(registry -> {
                registry.add("tcp://registry1:5551");
            });
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
    CompletionStage<ZLinkActor> createAsync(
        String actorId,
        ZLinkActorContext context);
}

public interface ZLinkActorManager {
    CompletionStage<ZLinkActor> createAsync(String actorId, String actorType);
    CompletionStage<Optional<ZLinkActor>> findAsync(String actorId);
    CompletionStage<ZLinkActor> getOrCreateAsync(String actorId, String actorType);
}
```

`actorType`은 application이 정하는 짧은 문자열 키다. 같은 actor id를 다른
actorType으로 다시 쓰면 설정 또는 런타임 오류로 실패해야 한다.

## 4. Session Binding

```java
public interface ZLinkSessionActors {
    List<ZLinkSessionActor> bound();

    CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActor actor);

    CompletionStage<ZLinkSessionActor> bindAsync(ActorRef actor);

    Optional<ZLinkSessionActor> find(String actorId);
}

public interface ZLinkSessionActor {
    String actorId();
    ActorRef ref();

    CompletionStage<Void> relayAsync(
        ZLinkStreamHeader header,
        Message payload);

    CompletionStage<Void> notifyDisconnectedAsync();
}
```

session은 local actor instance 또는 remote `ActorRef`에 bind할 수 있다. remote
binding은 actor route snapshot을 session public 입력으로 받지 않고, core
ActorGateway와 logical actor handle을 사용한다.

## 5. Bound Session

actor에서 현재 client session으로 보내는 표면은 `ZLinkBoundSession`이다.

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

public interface ZLinkBoundSession {
    <TMessage> ZLinkBoundSessionSendCall send(TMessage message);
    CompletionStage<Void> disconnectAsync();
}
```

`ZLinkBoundSession`은 server-to-client request API를 제공하지 않는다. client
request에 대한 응답은 actor request handler의 반환값과 원래 request correlation으로
처리한다.

actor가 join한 SPOT의 실행 문맥 상태가 필요하면 `context.getSpot()` 또는 typed
`context.getSpot(MatchSpot.class)`로 현재 join된 user Spot 인스턴스를 가져온다. join
전이거나 Entry Spot 단계라면 user Spot이 없으므로 호출 가능 시점을 actor lifecycle에
맞춘다.

```java
MatchSpot spot = actor.context().getSpot(MatchSpot.class);
```

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
  필요하면 application이 `notifyDisconnectedAsync()`를 호출한다.
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
