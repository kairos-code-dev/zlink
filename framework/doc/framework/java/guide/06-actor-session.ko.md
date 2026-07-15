<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Spot](05-spot.ko.md) | [다음: STREAM](07-stream.ko.md)
<!-- framework-adapter-nav:end -->

# Java Actor/Session Guide

## 1. actor란

actor는 **ID로 식별되는 상태 보유 객체**다. 같은 `actorId`로 들어오는 메시지는
항상 같은 인스턴스가 처리한다(일반 handler는 stateless라 메시지마다 새로 resolve된다).
게임에서 한 플레이어, 한 세션의 진행 상태를 담기에 맞다.

호출자는 actor가 어느 노드/Spot에 있는지 몰라도 된다. `actorId`만으로 호출하면
routing은 framework가 등록된 resolver로 푼다.

actor의 상태는 서로 독립인 두 축으로 본다.

| 축 | 값 |
|----|------|
| 위치(location) | Entry Spot(생성 직후 기본) <-> user Spot(join 후) |
| binding | unbound <-> STREAM session에 bound |

위치 이동과 session binding은 독립이다. user Spot join에 session bind가 꼭
필요한 것은 아니다. session disconnect는 actor membership을 바꾸지 않는다.

## 2. actor 등록과 작성

actor는 factory로 만든다. factory는 `actorType` 짧은 문자열로 등록한다.

```java
@Override
public void configure(ZLinkFrameworkOptions framework) {
    ZLinkSpotMeshBuilder node = framework.addSpotMesh("play");
    node.addActorFactory("player", PlayerActorFactory.class);
    // Entry Spot / user Spot 등록도 같은 SpotNode 쪽에서 (§3)
}

@Component
public final class PlayerActorFactory implements ZLinkActorFactory {
    @Override
    public ZLinkActor create(
        String actorId,
        ZLinkActorContext context) {
        return new PlayerActor(actorId, context);
    }
}
```

actor 안에서의 Spot join과 현재 상태 조회는 주입된 `ZLinkActorContext`로 한다.
channel outbound는 actor context의 기능이 아니며, Entry Spot 또는 user Spot
handler에서 받은 spot context로 호출한다.

| `ZLinkActorContext` 멤버 | 용도 |
|--------------------------|------|
| `spotRid()`, `isJoined()` | 현재 Spot join 상태 조회 |
| `boundSession()` | 자기 client로 push (§4) |
| `joinSpot(spotRid, request)` | user Spot으로 join. `.submit(...)`로 종결 |
| `joinEntrySpot(spotNodeRid, request)` | target SpotNode의 Entry Spot으로 이동. 빈 요청은 `ZLinkMessage.empty()` 또는 빈 DTO로 넘긴다 |
| `ZLinkEntrySpotContext.destroyActor(actor)` | Entry Spot에 있는 actor를 종료 |

`joinSpot(...).submit(replyType)`는 actor join 요청을 제출하고, join reply를
`replyType`으로 역직렬화한 뒤 `CompletionStage`로 반환한다. 성공하면 actor context의
`spotRid()`, `isJoined()`, `getSpot(Class)`가 join된 user Spot을 가리킨다. 테스트·클라이언트
시나리오에서는 `await(replyType)` blocking helper도 쓸 수 있다.

> **join이 성공하면 그 `actor` 객체를 더 접근하지 않는다.** join이 끝나면 actor는 이 Spot을 떠났고,
> **대상 Spot이 다른 노드면 이 노드의 actor 인스턴스는 retire**된다(접근하면 stale). 호출한 handler는
> join reply 결과만 쓰고 반환한다. join 직후의 client push 같은 후처리는 actor가 실제로 사는 **대상
> user Spot**(이동 후 그 Spot의 joined 콜백/handler)에서 한다. `onLeaveActor`는 source Spot에 actor가
> 떠났음을 알리는 membership 콜백이며, remote 이동에서는 이 콜백이 끝난 source actor instance를 더
> 사용하지 않는다.

remote 이동에서 함께 옮길 domain state가 있으면 actor type마다 `ZLinkActorTransferAdapter<TActor>`를
하나 등록한다. source adapter는 state를 `ZLinkMessage`로 만들고, target adapter는 framework가 준비한
`ZLinkActorContext`로 새 actor를 만든다. adapter가 없는 actor type도 실패하지 않으며 framework가 빈
state와 등록된 actor factory를 사용한다.

```java
public final class PlayerTransferAdapter
    implements ZLinkActorTransferAdapter<PlayerActor> {
    @Override
    public ZLinkMessage transferOut(PlayerActor actor, CancellationToken cancellationToken) {
        return ZLinkMessage.of(actor.snapshot()); // 이동할 domain state만 담는다.
    }

    @Override
    public PlayerActor transferIn(
        String actorId,
        ZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken) {
        return new PlayerActor(
            actorId,
            context,
            state.decode(PlayerSnapshot.class)); // target actor를 framework context로 복원한다.
    }
}

var node = options.addSpotMesh("game");
node.addActorFactory("player", PlayerActorFactory.class);
node.addActorTransferAdapter("player", PlayerTransferAdapter.class);
```

이동이 commit된 직후에도 이전 generation의 actor ref를 가진 packet이 source node에 늦게 도착할 수
있다. framework는 기본 5초 동안 이 packet을 새 위치로 전달한 뒤 이전 ref를 제거한다. 배포 환경의
최대 지연이 더 짧거나 길면 framework 전체 옵션에서 이 시간을 조정한다. `Duration.ZERO`는 commit 뒤
이전 ref를 바로 제거해야 하는 환경에서만 사용한다.

```java
options.setActorTransferForwardWindow(
    Duration.ofSeconds(5)); // 늦게 도착한 이전 actor ref packet을 받아 줄 시간을 정한다.
```

actor 객체를 끝내려면 actor가 Entry Spot에 있는 상태에서
`ZLinkEntrySpotContext.destroyActor(actor)`를 호출한다. 이 호출은 lifecycle callback을
호출하지 않고 native actor ref와 framework registry를 정리한다. user Spot에 있는 actor는
바로 destroy할 수 없으므로 먼저 leave 또는
`joinEntrySpot(..., request)` 흐름을 완료해야 한다.

## 3. Entry Spot과 user Spot의 actor handler

actor packet handler는 actor 클래스가 아니라 Entry Spot / user Spot의 `configure()`에서
등록한다. lifecycle callback은 Spot/EntrySpot 인터페이스 메서드로 구현한다. Entry Spot과
user Spot은 등록 표면이 같지만
실행 정책이 다르다.

```java
public final class PlayerEntrySpot implements ZLinkEntrySpot<PlayerActor> {
    private final ZLinkEntrySpotContext context;

    public PlayerEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(AuthenticateHandler.class);
        context.handlers().addHandler(JoinMatchHandler.class);
    }
}
```

실행 순서가 위치마다 다르다.

| 대상 | 실행 라인 |
|------|-----------|
| Entry Spot actor packet | 대상 actor의 mailbox(같은 actor만 직렬) |
| Entry Spot lifecycle | Entry Spot 자체 실행 문맥 |
| user Spot actor packet / packet / timer / subscription | 그 user Spot의 단일 실행 큐(직렬) |

그래서 Entry Spot actor handler는 actor별 상태만 다룬다. 여러 actor가 공유하는 Entry Spot
가변 상태에 의존하면 서로 다른 actor packet이 동시에 진행될 때 race가 생긴다. user Spot
안의 room 상태 같은 가변 상태는 그 Spot 실행 큐 안에서 lock 없이 만질 수 있다.

Entry Spot actor handler에서는 `yield(...)`를 사용하지 않는다. 이 handler에는 반납할
Entry Spot 전체 실행 turn이 없으므로, handler 안에서 만든 call object의 `yield(...)`를 호출하면
시간 초과가 아니라 즉시 계약 오류가 난다. Entry Spot actor handler의 대기 작업은
`submit(...)` 또는 `await(...)`로 표현한다.

CPU 계산은 `context.runCpuWorker(...)`로 실행 큐 밖에 맡긴다. 비동기 I/O는
`context.runIoWorker(...)`에 `CompletionStage`를 반환하는 작업을 넘긴다. I/O 대기에는 bounded
CPU worker thread를 사용하지 않는다. 두 작업 모두 Spot 상태를 직접 변경하지 않고 완료 뒤 실행
큐로 복귀한 지점에서 상태를 다시 확인한다.

```java
return context.runCpuWorker(() -> ScoreCalculator.calculate(snapshot))
    .submit()
    .thenAccept(result -> currentScore = result); // 완료까지 현재 turn을 유지한다.
```

> **응답 body는 반환값으로.** actor request handler는 응답 body를 직접 보내지
> 않고 반환한 `TReply`로 정한다. Java 의 `ZLinkSpotActorRequestContext` 는
> `ZLinkHandlerContext` 만 확장하고 reply/metadata/compression 옵션 setter 표면은 없다.

## 4. session actor dispatch — 연결 서버와 로직 서버 분리

큰 게임 서버는 보통 두 역할로 나눈다.

- **Session 서버** — client STREAM 연결만 받는다(인증, actor binding, client 요청
  relay). 게임 로직은 돌리지 않는다.
- **Play(Actor) 서버** — actor, Entry Spot, user Spot을 호스팅하고 실제 로직을 돌린다.

client는 STREAM 하나만 유지하고, Play 서버가 보내는 메시지도 그 STREAM으로
되돌아간다. 재접속(다른 Session 서버일 수도 있음) 시 binding만 새 stream으로
교체되고 actor 인스턴스와 spot membership은 그대로 유지된다(actor id 기준 멱등).

```mermaid
sequenceDiagram
  participant C as Client
  participant S as Session 서버
  participant P as Play 서버(actor)
  C->>S: STREAM 연결 + auth
  S->>S: bind(actor)
  C->>S: PlaceMarkReq
  S->>P: actor.relay(payload)
  P->>P: actor handler 실행 (room 상태 변경)
  P-->>S: boundSession().send(TurnChangedNotify)
  S-->>C: STREAM push
```

### Session 서버: 인증과 relay

session 콜백에서 인증 후 `bind(...)`로 actor handle을 잡고, 이후 packet은
`ZLinkSessionActor.relay(...)`로 actor에 넘긴다. `payload`는 framework runtime이
registry와 함께 소유한 `ZLinkMessage`다. session은 인증 packet처럼 직접 처리할
payload만 `decode(...)`하고, actor로 넘길 packet은 decode하지 않은 채 그대로 relay한다.

```java
@Component
public final class TicTacToeSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkActorManager actors;

    public TicTacToeSession(
        ZLinkSessionContext context,
        ZLinkActorManager actors) {
        this.context = context;
        this.actors = actors;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override public void onConnected() {}
    @Override public void onDisconnected() {}
    @Override public void onError(ZLinkStreamError error) {}

    @Override
    public void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        if ("auth".equals(dispatch.packetName())) {
            AuthReq req = payload.decode(AuthReq.class);
            actors.getOrCreate(req.actorId(), "player")
                .thenCompose(actor -> context.actors().bind(actor))
                .thenCompose(bound -> context.client().reply(new AuthRep(true)).submit())
                .toCompletableFuture()
                .join();
            return;
        }
        context.actors().bound().stream().findFirst()
            .map(actor -> actor.relay(payload))
            .orElseThrow(() -> new IllegalStateException("no actor bound to this packet"))
            .toCompletableFuture()
            .join();
    }
}
```

### Play 서버: actor가 자기 client로 push

Spot actor handler는 stream을 직접 들지 않는다. 자기 client로 보내려면 handler가
받은 actor의 `context().boundSession()`을 쓴다.

```java
context.boundSession()
    .send(new MatchFound(roomId))
    .submit();
```

`ZLinkBoundSession`의 표면은 **`send(message)`** 와 **`disconnect()`** 둘뿐이다.
client로의 push는 단방향이며 별도 request 표면은 없다. `send(...).submit()`는
fire-and-forget(route 위임 완료이지 client app ack이 아님)이다. 다른 actor의
client로 보내야 하면 먼저 그 actor에게 메시지를 보낸 뒤, 해당 actor가 자기
`boundSession()`으로 push한다.

## 5. 등록 골격

session relay는 application route mesh channel로 흐르지 않는다. 같은 runtime 안에서
만든 local managed actor instance를 bind하는 direct stream 역할은 framework 내부 dispatch
경로를 쓴다. remote actor ref를 bind해야 하는 session gateway 역할에서는, STREAM의 actor-gateway
입구가 **같은 프로세스의 (router가 켜진) local SpotNode로 자동 연결**되고(별도 호출 없음),
`bind(...)`가 remote actor locator를 core SessionRelay 경로에 bind한다.

- **Session 서버**: `addSpotMesh`로 session-node(router)를 두면 `addStreamNode(...)`의 gateway가
  그 노드로 자동 연결된다.
- **Play 서버**: `addActorFactory(...)` + `addSpotMesh`로 play-node에
  `addEntrySpot(...)`, `addSpotFactory(...)`를 등록한다.

전체 등록 시그니처는
[spring-boot-actor-session](../../spec/server/languages/java/01-system-structure.ko.md)이
소유한다.

## 6. Reconnect와 gotcha

- 같은 actor id가 새 session에 다시 bind되면 actor 인스턴스와 Spot membership은
  유지하고 binding token만 갱신한다.
- session disconnect는 bound actor 전체에 자동 전파되지 않는다. 필요한 actor에게만
  `ZLinkSessionActor.notifyDisconnected()`를 호출한다.
- actor 위치 조회용 public resolver는 없다. actor<->session binding은 framework
  내부 상태다. Spot owner 조회만 `useRegistrySpotRemoteRefs(...)` 또는 custom
  `addSpotRemoteRefResolver(...)`로 public이다.

## 7. 더 보기

- SPOT 기반: [06-spot](05-spot.ko.md)
- STREAM session 작성: [08-stream](07-stream.ko.md)
- actor 런타임 오류 관찰: [10-monitoring](09-monitoring.ko.md)
- 전체 계약: [spring-boot-actor-session](../../spec/server/languages/java/01-system-structure.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Spot](05-spot.ko.md) | [다음: STREAM](07-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
