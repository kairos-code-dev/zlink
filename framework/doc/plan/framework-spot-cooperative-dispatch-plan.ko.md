# Framework Spot yield dispatch 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤
> 공통 framework spec과 언어별 spec/guide 문서에 나누어 반영한다.

## 목적

Spot과 Entry Spot handler가 외부 channel request, 다른 Spot join request, worker completion 같은 I/O를
기다리는 동안 activation 전체 실행 줄을 붙잡으면 관계없는 actor 요청과 timer callback까지 함께 늦어진다.
Bingo sample의 match handler처럼 player 한 명의 입장 준비에서 API channel request와 room Spot join
request를 연속으로 기다리는 흐름은 player 수가 많아질수록 병목이 커진다.

하지만 모든 framework I/O await를 자동으로 interleave 지점으로 바꾸면 논리적 동기화 문제가 생긴다.
handler가 await 전에 읽은 Spot 공용 상태나 mutable 객체가 await 뒤에는 이미 다른 actor/timer 작업에 의해
바뀌었을 수 있기 때문이다.

따라서 기본 completion terminator는 기존 serial 의미를 유지한다. handler 작성자가 await 사이의
interleaving이 논리적으로 안전하다고 판단한 곳에서만 `yield` 계열 terminator를 명시적으로 사용한다.

```text
+---------------------------+
| Spot Scheduler            |
+---------------------------+
| actor mailbox: player-1   |
| actor mailbox: player-2   |
| timer mailbox: heartbeat  |
| spot mailbox: lifecycle   |
+---------------------------+
| ready continuations       |
+---------------------------+
```

## 핵심 결정

### 1. 기본 API는 기존 serial 의미를 유지한다

기존 terminator는 기존 동작을 유지한다.

| 언어 | 기존 terminator | 의미 |
|------|-----------------|------|
| `.NET` | `Async(...)` | handler가 await하는 동안 현재 Spot 실행 줄도 completion까지 기다린다. |
| Java | `submit(...)`, `await(...)` helper | 기존 `CompletionStage`/blocking helper 의미를 유지한다. |
| Kotlin | `submit(...).await()` | 기존 Java call object 결과를 coroutine으로 기다린다. |
| Node/NestJS | `submit(...)` | 기존 `Promise` completion까지 serial tail이 기다린다. |
| C++ | `async()` | 기존 `task_t` completion 의미를 유지한다. |

기본 API를 안전한 동작으로 남기면, shared mutable state를 다루는 기존 handler에 의도하지 않은 interleaving이
들어가지 않는다.

### 2. yield 계열 terminator는 명시적 interleaving 선언이다

yield 계열 terminator는 “이 I/O await 사이에 다른 mailbox 작업이 실행되어도 이 handler의 논리가 안전하다”는
선언이다.

| 언어 | yield terminator |
|------|------------------|
| `.NET` | `YieldAsync(...)` |
| Java | `yieldAwait(...)`, `yieldAsync(...)` |
| Kotlin | `yieldAwait(...)` |
| Node/NestJS | `yieldSubmit(...)` |
| C++ | `yield_async()` |

이 이름들은 언어별 기존 terminator 관례를 유지하면서 `yield`라는 공통 개념을 드러낸다. 여기서 yield는
thread scheduler yield가 아니라 **현재 Spot mailbox turn을 반납하고 completion 뒤 원래 mailbox에서
재개한다**는 뜻이다.

### 3. 사용 범위를 제한한다

사용 권장:

- player actor 한 명의 admission/preflight 작업
- 외부 request 대기 중 activation 전체 흐름을 막고 싶지 않은 경우
- await 전후에 Spot 공용 mutable state를 의사결정에 사용하지 않는 경우
- 같은 actor 또는 같은 timer mailbox 순서만 유지하면 충분한 경우
- callback/push로 request/reply 흐름을 쪼개면 코드가 지나치게 복잡해지는 경우

사용 금지:

- await 전에 읽은 mutable collection count로 await 뒤 결정을 내리는 코드
- await 전에 얻은 mutable object reference로 await 뒤 상태를 변경하는 코드
- room/player list, match queue, lobby state, timer-driven aggregate를 await 전후로 이어서 변경하는 코드
- 여러 actor가 함께 보는 shared state machine을 await 전후로 갱신하는 코드
- “성능이 좋아질 것 같아서” 안전성 검토 없이 일반 I/O await를 모두 바꾸는 경우

yield terminator는 일반 성능 옵션이 아니라 **제한적으로 쓰는 동시성 의미 변경 API**다.

### 4. await 전후는 다른 turn이다

yield terminator를 사용하면 같은 handler 안에서도 await 전후는 하나의 연속 실행 구간이 아니다.

```csharp
var room = spot.Rooms[roomId];
var count = room.Players.Count;

var matched = await entrySpot.Context.Outbound
    .RequestToChannel(SampleNames.ApiChannel, request)
    // 여기서는 player actor 단독 admission 흐름이라 interleaving을 명시적으로 허용한다.
    .YieldAsync<MatchBingoApiRes>(cancellationToken);

// 위험: count는 await 전 snapshot일 뿐 현재 상태가 아니다.
// await 뒤 shared state로 결정해야 한다면 다시 읽거나 generation을 확인한다.
```

같은 actor와 같은 timer의 재진입은 막는다. 하지만 다른 actor, 다른 timer, spot/global 작업은 interleave될
수 있다.

### 5. local context를 turn 저장소로 쓰지 않는다

기본 `Async` / `submit` / `async()` 경로에서는 언어 runtime의 local context를 일반적인 async context
용도로 사용할 수 있다. handler가 yield 계열 terminator를 호출하지 않으면 Spot scheduler가 handler
completion까지 serial turn을 붙잡기 때문에 tracing id, request id, logger scope, validation context 같은
임시 context는 기존 언어 runtime 규칙을 따른다.

하지만 yield 계열 terminator를 사용하는 경로에서는 local context를 turn, mailbox, actor, Spot 상태의
소유권 저장소로 쓰지 않는다. Spot yield dispatch의 turn 정보는 thread-local, coroutine-local,
async-local에 의존하지 않는다. turn은 scheduler가 소유하는 명시적 runtime state이며, framework call
object가 생성될 때 필요한 turn handle을 캡처한다.

금지:

- yield 경로에서 `ThreadLocal`, `AsyncLocal`, `AsyncLocalStorage`, `CoroutineContext`, `thread_local`을 turn 저장소로 삼는 구현
- yield completion callback에서 local context를 다시 조회해 원래 mailbox를 찾는 구현
- yield terminator가 Spot handler 안에서 호출됐는데 turn handle이 없을 때 normal async로 조용히 fallback하는 구현

허용:

- yield를 사용하지 않는 handler에서 tracing, logging, validation을 위한 일반 async context 사용
- handler 진입 시 현재 turn을 검증하기 위한 짧은 lookup
- diagnostics event에 activation id와 turn id를 붙이는 보조 용도
- call object 생성 시 명시적 turn handle을 주입하기 위한 lookup

## 현재 상태 요약

| 영역 | 현재 동작 | 변경 이유 |
|------|-----------|-----------|
| Spot callback 실행 | Spot/Entry Spot lifecycle, actor packet, route packet, subscription, timer, worker completion이 activation 실행 줄을 지난다. | handler가 I/O를 기다리면 관계없는 actor와 timer도 대기한다. |
| actor request reply | actor request handler가 reply 값을 반환한 뒤 framework가 client response를 보낸다. | callback/push로 흐름을 쪼개면 reply/error/cancellation 매핑이 handler 밖으로 새어 나간다. |
| actor dispatch mailbox | actor runtime에는 actor별 dispatch mailbox가 있다. | activation 전체 queue가 잡혀 있으면 actor별 mailbox 이점이 줄어든다. |
| timer overrun 정책 | timer options가 늦은 tick 처리 정책을 가진다. | timer mailbox는 overrun 정책을 다시 계산하지 않고 실행하기로 한 tick만 FIFO로 처리한다. |
| worker completion | worker completion은 다시 Spot 실행 줄로 들어온다. | yield worker completion도 원래 mailbox로 재투입되어야 한다. |

## 언어별 상세 계획

### 1. `.NET`

#### 내부 핵심 코드

| 현재 코드 | 변경 방향 |
|-----------|-----------|
| `ZLinkSpotActivation` / `ZLinkEntrySpotActivation` | 단일 serial queue 의존을 mailbox와 turn을 가진 scheduler로 바꾼다. |
| `ZLinkSpotActivationExecution` / `ZLinkEntrySpotActivationExecution` | `ExecuteAsync`, `ExecuteQueuedAsync`, `QueueSerialized`가 mailbox key와 turn을 만들도록 나눈다. |
| `ZLinkSerialExecutionQueue` / `ZLinkSpotSerialExecutor` | 기존 `Async(...)` 경로는 serial 의미를 유지하고, `YieldAsync(...)` 경로만 turn suspend/resume을 수행한다. |
| `ZLinkActorRuntimeState` / `ZLinkActorDispatchMailbox` | actor별 순서 보장의 기준으로 재사용한다. Spot scheduler가 같은 actor에 중복 queue를 만들지 않는다. |
| `ZLinkSpotClientCalls`, `ZLinkChannelCalls`, actor join call 구현체 | `YieldAsync(...)` terminator를 추가하고 submit/completion을 캡처된 turn handle과 연결한다. |
| `ZLinkSpotTimerRegistry` / `ZLinkTimer` | timer options가 실행하기로 한 tick만 timer mailbox에 enqueue한다. |

내부 타입 초안:

```csharp
internal sealed class ZLinkSpotYieldScheduler : IAsyncDisposable
{
    ValueTask<T> RunAsync<T>(
        ZLinkSpotMailboxKey key,
        Func<ZLinkSpotTurn, CancellationToken, ValueTask<T>> operation,
        CancellationToken cancellationToken);

    void Post(
        ZLinkSpotMailboxKey key,
        Func<ZLinkSpotTurn, CancellationToken, ValueTask> operation);
}

internal sealed class ZLinkSpotTurn
{
    ValueTask<T> YieldFrameworkCallAsync<T>(
        Func<CancellationToken, ValueTask<T>> submit,
        CancellationToken cancellationToken);
}
```

`AsyncLocal`은 기본 serial handler의 tracing/logging context로는 사용할 수 있다. 하지만 yield 경로에서는
turn 저장소로 쓰지 않고, handler 진입 시 현재 turn 검증용으로만 짧게 사용한다.
call object는 생성 시점에 `ZLinkSpotTurn?`을 명시적으로 캡처한다.

.NET handler는 일반 `async`/`await`로 작성되므로 `YieldAsync(...)`만 추가해서는 gate가 풀리지 않는다.
serial queue가 handler의 `ValueTask` 전체를 끝까지 await하면 activation은 여전히 막힌다. 따라서 handler
invocation과 serial queue는 yield terminator가 turn을 `Suspended`로 바꾼 순간을 관찰해야 한다. 그 시점에는
현재 mailbox turn을 pending으로 남기고 global drain은 다음 mailbox 작업을 실행할 수 있어야 한다. I/O
completion이 도착하면 scheduler가 원래 mailbox에 resume permit을 넣고, handler의 나머지 continuation은 그
permit이 실행될 때만 이어져야 한다.

#### 실제 사용 코드

```csharp
public async ValueTask<MatchBingoRes> HandleAsync(
    BingoEntrySpot entrySpot,
    PlayerActor actor,
    ZLinkSpotActorRequestContext context,
    MatchBingoReq message,
    CancellationToken cancellationToken)
{
    var matched = await entrySpot.Context.Outbound
        .RequestToChannel(SampleNames.ApiChannel, new MatchBingoApiReq
        {
            ActorId = actor.ActorId,
            DisplayName = actor.DisplayName,
            ActorNodeRid = entrySpot.Context.NodeRid.ToString(),
            Mode = message.Mode,
        })
        .Timeout(TimeSpan.FromSeconds(5))
        .YieldAsync<MatchBingoApiRes>(cancellationToken); // player 단독 admission I/O 동안 actor mailbox turn을 반납한다.

    var joined = await actor.Context.JoinSpot(
            RoutingId.From(matched.RoomId),
            new BingoRoomJoinReq
            {
                RoomId = matched.RoomId,
                ActorId = actor.ActorId,
                DisplayName = actor.DisplayName,
                ObserveOnly = false,
            })
        .YieldAsync<BingoRoomJoinRes>(cancellationToken); // 같은 actor mailbox continuation으로 돌아온다.

    return new MatchBingoRes
    {
        RoomId = matched.RoomId,
        State = joined.Reply.State,
        RoomOwnerNodeRid = matched.RoomOwnerNodeRid,
    };
}
```

#### public 인터페이스 초안

```csharp
public interface IZLinkRequestCall
{
    IZLinkRequestCall PacketName(string messageName);
    IZLinkRequestCall Timeout(TimeSpan timeout);
    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
    ValueTask<TReply> YieldAsync<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult> YieldAsync(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> YieldAsync<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinEntrySpotCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
    ValueTask<ZLinkActorJoinResult> Async(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult> YieldAsync(CancellationToken cancellationToken = default);
    ValueTask<ZLinkActorJoinResult<TReply>> YieldAsync<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkBoundSessionSendCall
{
    IZLinkBoundSessionSendCall PacketName(string packetName);
    IZLinkBoundSessionSendCall Metadata(string key, string value);
    ValueTask Async(CancellationToken cancellationToken = default);
    ValueTask YieldAsync(CancellationToken cancellationToken = default);
}

public interface IZLinkWorkerCall<TResult>
{
    IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout);
    ValueTask<TResult> Async(CancellationToken cancellationToken = default);
    ValueTask<TResult> YieldAsync(CancellationToken cancellationToken = default);
    void Submit(
        Func<TResult, CancellationToken, ValueTask> onCompleted,
        Func<Exception, CancellationToken, ValueTask>? onError = null,
        CancellationToken cancellationToken = default);
}
```

### 2. Java

#### 내부 핵심 코드

| 현재 코드 | 변경 방향 |
|-----------|-----------|
| `ZLinkSpotRuntime.SpotActivation` / `EntrySpotActivation` | Spot 공통 yield scheduler를 갖도록 바꾼다. |
| `DefaultSpotContext` / `DefaultEntrySpotContext` | outbound call object 생성 시 현재 turn handle을 주입한다. |
| `ZLinkSpotDispatchQueue` | yield call이 submit되면 gated 상태를 풀고, completion은 원래 mailbox의 resume permit으로 enqueue한다. |
| `ZLinkRequestCall`, actor join call, bound session send, worker call 구현체 | 기존 `submit(...)`과 `await(...)`는 유지하고, `yieldAsync(...)`와 `yieldAwait(...)` terminator를 추가한다. |
| yield-aware await helper | 기존 동기 handler 코드 모양을 유지하기 위해 `yieldAwait(...)`가 turn 반납, completion 대기, scheduler resume permit 대기를 함께 처리한다. |
| handler execution thread | yield를 쓰는 동기 handler가 platform thread를 오래 붙잡지 않도록 virtual thread 또는 전용 blocking executor에서 실행한다. |
| `ManagedTimer` | timer options가 선택한 tick만 timer mailbox에 넣는다. |

`ThreadLocal`은 기본 serial handler의 임시 context로는 사용할 수 있다. 하지만 yield 경로에서는 turn
저장소로 쓰지 않는다. 현재 Java runtime의 `currentOutbound` 같은 `ThreadLocal`은 handler 호출 시 짧게
쓰는 보조 수단으로만 남긴다.

#### 실제 사용 코드

```java
public Messages.MatchBingoRes handle(
    BingoEntrySpot entrySpot,
    PlayerActor actor,
    ZLinkSpotActorRequestContext context,
    Messages.MatchBingoReq request,
    CancellationToken cancellationToken) {

    if (cancellationToken.isCancellationRequested()) {
        throw new CancellationException();
    }

    Messages.MatchBingoApiRes matched = entrySpot.context().outbound()
        .requestToChannel(SampleNames.ApiChannel, new Messages.MatchBingoApiReq(
            actor.actorId(),
            actor.displayName(),
            request.mode(),
            entrySpot.context().nodeRid().toString()))
        .timeout(SampleTimings.RequestTimeout)
        .yieldAwait(Messages.MatchBingoApiRes.class); // player 단독 admission I/O 동안 actor mailbox turn을 반납한다.

    ZLinkActorJoinResult<Messages.BingoRoomJoinRes> joined = actor.context()
        .joinSpot(
            RoutingId.from(matched.roomId()),
            new Messages.BingoRoomJoinReq(
                matched.roomId(),
                actor.actorId(),
                actor.displayName(),
                false))
        .yieldAwait(Messages.BingoRoomJoinRes.class); // 같은 actor mailbox continuation으로 돌아온다.

    return new Messages.MatchBingoRes(
        matched.roomId(),
        joined.reply().state(),
        matched.roomOwnerNodeRid());
}
```

#### public 인터페이스 초안

```java
public interface ZLinkRequestCall {
    ZLinkRequestCall packetName(String packetName);
    ZLinkRequestCall metadata(String key, String value);
    ZLinkRequestCall timeout(Duration timeout);
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
    <TReply> TReply await(Class<TReply> replyType);
    <TReply> CompletionStage<TReply> yieldAsync(Class<TReply> replyType);
    <TReply> TReply yieldAwait(Class<TReply> replyType);
}

public interface ZLinkActorJoinSpotCall {
    ZLinkActorJoinSpotCall timeout(Duration timeout);
    CompletionStage<ZLinkActorJoinResult<Void>> submit();
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);
    ZLinkActorJoinResult<Void> await();
    <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType);
    CompletionStage<ZLinkActorJoinResult<Void>> yieldAsync();
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> yieldAsync(Class<TReply> replyType);
    ZLinkActorJoinResult<Void> yieldAwait();
    <TReply> ZLinkActorJoinResult<TReply> yieldAwait(Class<TReply> replyType);
}

public interface ZLinkActorJoinEntrySpotCall {
    ZLinkActorJoinEntrySpotCall timeout(Duration timeout);
    CompletionStage<ZLinkActorJoinResult<Void>> submit();
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> submit(Class<TReply> replyType);
    ZLinkActorJoinResult<Void> await();
    <TReply> ZLinkActorJoinResult<TReply> await(Class<TReply> replyType);
    CompletionStage<ZLinkActorJoinResult<Void>> yieldAsync();
    <TReply> CompletionStage<ZLinkActorJoinResult<TReply>> yieldAsync(Class<TReply> replyType);
    ZLinkActorJoinResult<Void> yieldAwait();
    <TReply> ZLinkActorJoinResult<TReply> yieldAwait(Class<TReply> replyType);
}

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);
    ZLinkBoundSessionSendCall metadata(String key, String value);
    CompletionStage<Void> submit();
    void await();
    CompletionStage<Void> yieldAsync();
    void yieldAwait();
}

public interface ZLinkWorkerCall<T> {
    ZLinkWorkerCall<T> timeout(Duration timeout);
    CompletionStage<T> submit();
    T await();
    CompletionStage<T> yieldAsync();
    T yieldAwait();
    void submit(
        BiConsumer<T, CancellationToken> onCompleted,
        BiConsumer<Throwable, CancellationToken> onError);
}
```

Java의 권장 사용 코드는 기존 동기 handler 모양을 유지한다. `yieldAwait(...)`는 단순히
`CompletionStage.join()`을 호출하는 helper가 아니다. 이 helper는 yield call이 캡처한 turn을 먼저
`Suspended`로 바꾸고, I/O completion이 도착하면 원래 mailbox에 resume permit을 넣은 뒤, scheduler가
그 permit을 실행할 때까지 기다린다. 그래서 handler의 나머지 코드는 사용자가 보기에는 동기식으로 이어지지만
Spot scheduler 관점에서는 원래 mailbox에서 재개된 turn으로 처리된다.

이 방식은 대기 중인 Java 실행 흐름을 만든다. 따라서 Java runtime은 yield-aware handler를 virtual thread
또는 전용 blocking executor에서 실행해야 한다. platform thread serial executor 위에서 `yieldAwait(...)`를
직접 막으면 player 수가 많을 때 thread 병목이 다시 생긴다.

### 3. Kotlin

Kotlin은 Java framework core 위의 coroutine adapter다. Kotlin 전용 scheduler나 coroutine-local turn
저장소를 만들지 않는다. Java call object가 캡처한 turn handle을 사용하고, Kotlin은 `yieldAwait(...)`
helper로 제한적 interleaving을 명시한다.

#### 실제 사용 코드

```kotlin
override suspend fun handle(
    entrySpot: BingoEntrySpot,
    actor: PlayerActor,
    context: ZLinkSpotActorRequestContext,
    request: MatchBingoReq,
    cancellationToken: CancellationToken,
): MatchBingoRes {
    val matched = yieldAwait(
        entrySpot.context().outbound()
            .requestToChannel(
                SampleNames.ApiChannel,
                MatchBingoApiReq(
                    actor.actorId,
                    actor.displayName,
                    request.mode,
                    entrySpot.context().nodeRid().toString(),
                ),
            )
            .timeout(Duration.ofSeconds(5)),
        MatchBingoApiRes::class.java,
    ) // Java call object가 actor mailbox turn을 반납한다.

    val joined = yieldAwait(
        actor.context().joinSpot(
            RoutingId.from(matched.roomId),
            BingoRoomJoinReq(matched.roomId, actor.actorId, actor.displayName, false),
        ),
        BingoRoomJoinRes::class.java,
    ) // 같은 actor mailbox continuation으로 돌아온다.

    return MatchBingoRes(matched.roomId, joined.reply.state, matched.roomOwnerNodeRid)
}
```

#### Kotlin helper 초안

```kotlin
suspend inline fun <reified TReply> yieldAwait(call: ZLinkRequestCall): TReply =
    yieldAwait(call, TReply::class.java)

suspend fun <TReply> yieldAwait(call: ZLinkRequestCall, replyType: Class<TReply>): TReply =
    call.yieldAsync(replyType).await()

suspend inline fun <reified TReply> yieldAwait(call: ZLinkActorJoinSpotCall):
    ZLinkActorJoinResult<TReply> =
    yieldAwait(call, TReply::class.java)

suspend fun <TReply> yieldAwait(
    call: ZLinkActorJoinSpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yieldAsync(replyType).await()

suspend inline fun <reified TReply> yieldAwait(call: ZLinkActorJoinEntrySpotCall):
    ZLinkActorJoinResult<TReply> =
    yieldAwait(call, TReply::class.java)

suspend fun <TReply> yieldAwait(
    call: ZLinkActorJoinEntrySpotCall,
    replyType: Class<TReply>,
): ZLinkActorJoinResult<TReply> =
    call.yieldAsync(replyType).await()

suspend fun yieldAwait(call: ZLinkBoundSessionSendCall) {
    call.yieldAsync().await()
}

suspend fun <T> yieldAwait(call: ZLinkWorkerCall<T>): T =
    call.yieldAsync().await()
```

`CoroutineContext`는 기본 serial handler의 coroutine context로는 사용할 수 있다. 하지만 yield 경로에서는
turn 저장소로 쓰지 않는다. Kotlin helper는 Java core가 제공하는 yield terminator를 호출하는 얇은
adapter다.

### 4. Node/NestJS

#### 내부 핵심 코드

| 현재 코드 | 변경 방향 |
|-----------|-----------|
| `SpotActivation` / `ZLinkEntrySpotActivation` | 기존 `submit(...)`은 serial 의미를 유지하고, `yieldSubmit(...)`만 handler `Promise` 전체를 serial tail에 매달지 않는다. |
| `spotSerialTurnStorage` | turn 저장소가 아니라 현재 실행 중 turn 검증용 보조 수단으로만 사용한다. |
| `wrapRequestCall`, actor join call, bound session send call | `yieldSubmit(...)`이 call object에 캡처된 turn handle을 사용한다. |
| routed Spot request wrapper | resolver 대기와 transport submit도 yield 경로에서 scheduler turn을 올바르게 반납하도록 분리한다. |
| timer runtime | timer options가 선택한 tick만 timer mailbox에 넣는다. |

`AsyncLocalStorage`는 기본 serial handler의 request/logging context로는 사용할 수 있다. 하지만 yield
경로에서는 turn lifetime을 소유하지 않는다. `Promise` continuation에서 store를 다시 조회해 mailbox를
찾는 구현은 금지한다.

#### 실제 사용 코드

```ts
async handle(
  entrySpot: BingoEntrySpot,
  actor: PlayerActor,
  context: ZLinkSpotActorRequestContext,
  request: MatchBingoReq
): Promise<MatchBingoRes> {
  const matched = await entrySpot.context.outbound
    .requestToChannel(SampleNames.ApiChannel, {
      actorId: actor.actorId,
      displayName: actor.displayName,
      actorNodeRid: String(entrySpot.context.nodeRid),
      mode: request.mode
    })
    .timeout(5000)
    .yieldSubmit<MatchBingoApiRes>(context.connectionAborted); // player 단독 admission I/O 동안 actor mailbox turn을 반납한다.

  const joined = await actor.context
    .joinSpot(matched.roomId, {
      roomId: matched.roomId,
      actorId: actor.actorId,
      displayName: actor.displayName,
      observeOnly: false
    })
    .yieldSubmit<BingoRoomJoinRes>(context.connectionAborted); // 같은 actor mailbox continuation으로 돌아온다.

  if (joined.resultCode !== 0) {
    throw new Error(`Room ${matched.roomId} rejected actor '${actor.actorId}'.`);
  }
  if (joined.reply === undefined) {
    throw new Error(`Room ${matched.roomId} accepted actor '${actor.actorId}' without a join reply.`);
  }

  return {
    roomId: matched.roomId,
    state: joined.reply.state,
    roomOwnerNodeRid: matched.roomOwnerNodeRid
  };
}
```

#### public 인터페이스 초안

```ts
export interface ZLinkRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<TReply>;
  yieldSubmit<TReply = unknown>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkActorJoinSpotCall {
  timeout(timeoutMs: number): this;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
  yieldSubmit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall {
  timeout(timeoutMs: number): this;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
  yieldSubmit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkBoundSessionSendCall {
  metadata(key: string, value: string): this;
  packetName(packetName: string): this;
  submit(signal?: AbortSignal): Promise<void>;
  yieldSubmit(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkWorkerCall<T> {
  timeoutMs(durationMs: number): this;
  submit(signal?: AbortSignal): Promise<T>;
  yieldSubmit(signal?: AbortSignal): Promise<T>;
  onCompleted(
    callback: (result: T, signal?: AbortSignal) => void | Promise<void>,
    onError?: (error: unknown, signal?: AbortSignal) => void | Promise<void>,
    signal?: AbortSignal,
  ): void;
}
```

### 5. C++

#### 내부 핵심 코드

| 현재 코드 | 변경 방향 |
|-----------|-----------|
| `task_t<T>` | coroutine continuation scheduler가 원래 mailbox로 resume할 수 있게 확장한다. |
| `request_call_t<TReply>` / `channel_request_call_t` | 기존 `async()`는 serial 의미를 유지하고, `yield_async()`가 캡처된 turn handle을 사용한다. |
| `actor_join_spot_call_t` / `actor_join_entry_spot_call_t` | 현재 즉시 `result_t`를 감싸는 구조라 I/O 대기를 표현할 수 없다. `yield_async()`를 공개 완료로 보려면 call object 생성 시 dispatcher를 실행하지 않고, async submit 기반 call state가 dispatcher submit과 reply completion을 소유하도록 먼저 바꾼다. |
| `bound_session_send_call_t` | bound session send completion만 yield 대상으로 분리한다. 일반 channel send/publish와 Spot send에는 yield terminator를 추가하지 않는다. |
| `spot_context_t` / `entry_spot_context_t` | call object 생성 시 현재 turn handle을 전달한다. |
| `timer_runtime_t` | timer options가 전달하기로 한 tick만 timer mailbox에 넣는다. |

`thread_local`은 기본 serial handler의 짧은 runtime lookup으로는 사용할 수 있다. 하지만 yield 경로에서는
turn 저장소로 쓰지 않는다. coroutine은 resume thread가 바뀔 수 있으므로 call object나 `task_t` promise가
명시적 scheduler handle을 가져야 한다.

#### 실제 사용 코드

```cpp
task_t<match_bingo_res_t>
match_bingo_actor_handler_t::handle (bingo_entry_spot_t &entry_spot,
                                     player_actor_t &actor,
                                     spot_actor_request_context_t &context,
                                     const match_bingo_req_t &request)
{
    auto matched = co_await entry_spot.context ()
      .outbound ()
      .request_to_channel (sample_names::api_channel,
                           match_bingo_api_req_t{
                             actor.actor_id (),
                             actor.display_name (),
                             request.mode,
                             entry_spot.context ().node_rid ().to_string ()})
      .timeout (std::chrono::seconds (5))
      .yield_async<match_bingo_api_res_t> (); // player 단독 admission I/O 동안 actor mailbox turn을 반납한다.

    auto joined = co_await actor.context ()
      .join_spot (spot_rid_t::from (matched.room_id),
                  bingo_room_join_req_t{matched.room_id,
                                        actor.actor_id (),
                                        actor.display_name (),
                                        false})
      .yield_async<bingo_room_join_res_t> (); // 같은 actor mailbox continuation으로 돌아온다.

    co_return match_bingo_res_t{
      matched.room_id,
      joined.reply.state,
      matched.room_owner_node_rid};
}
```

#### public 인터페이스 초안

```cpp
template <typename TReply>
class request_call_t
{
  public:
    request_call_t &packet_name (std::string packet_name);
    request_call_t &metadata (std::string key, std::string value);
    request_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<TReply> async ();
    task_t<TReply> yield_async ();
};

class channel_request_call_t
{
  public:
    channel_request_call_t &packet_name (std::string packet_name);
    channel_request_call_t &metadata (std::string key, std::string value);
    channel_request_call_t &timeout (std::chrono::milliseconds timeout);

    template <typename TReply>
    task_t<TReply> async ();

    template <typename TReply>
    task_t<TReply> yield_async ();
};

class actor_join_spot_call_t
{
  public:
    actor_join_spot_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<actor_join_result_t> async ();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> async ();

    task_t<actor_join_result_t> yield_async ();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> yield_async ();
};

class actor_join_entry_spot_call_t
{
  public:
    actor_join_entry_spot_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<actor_join_result_t> async ();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> async ();

    task_t<actor_join_result_t> yield_async ();

    template <typename TReply>
    task_t<typed_actor_join_result_t<TReply>> yield_async ();
};

class bound_session_send_call_t
{
  public:
    bound_session_send_call_t &packet_name (std::string packet_name);
    bound_session_send_call_t &metadata (std::string key, std::string value);
    bound_session_send_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<void> async ();
    task_t<void> yield_async ();
};

template <typename T>
class worker_call_t
{
  public:
    using completion_callback_t = std::function<task_t<void> (result_t<T>)>;

    worker_call_t &timeout (std::chrono::milliseconds timeout);
    task_t<T> async ();
    task_t<T> yield_async ();
    void submit (completion_callback_t callback);
};
```

위 C++ actor join 예제와 인터페이스는 async submit 기반 call state 전환이 끝난 뒤에만 정식 문서와 sample에
반영한다. 현재처럼 join dispatcher가 call object 생성 전에 이미 실행되는 구조에서는 `yield_async()`를 붙여도
대기 중인 I/O 동안 mailbox turn을 반납할 수 없으므로 완료로 보지 않는다.

## runtime 설계

### 1. mailbox

| mailbox | key | 용도 |
|---------|-----|------|
| actor mailbox | actor id | Spot/Entry Spot actor request/send, actor lifecycle, disconnect 후속 처리 |
| timer mailbox | timer descriptor id 또는 timer name | Spot/Entry Spot timer tick과 timer handler continuation |
| spot/global mailbox | activation key | lifecycle, closing, route drain, subscription처럼 activation 전체 상태가 필요한 작업 |

mailbox는 FIFO queue다. 같은 mailbox 안에서는 이전 continuation보다 뒤에 들어온 같은 key 작업이 먼저
실행되지 않는다. 서로 다른 mailbox는 scheduler가 bounded ready budget이나 round-robin 정책으로
interleave한다.

### 2. turn

turn은 scheduler가 소유한다.

| 상태 | 의미 |
|------|------|
| `Running` | handler 구간이 scheduler 안에서 실행 중이다. |
| `Suspended` | yield terminator가 I/O completion을 기다리며 mailbox turn을 반납했다. |
| `Ready` | I/O completion이 도착해 continuation을 다시 실행할 수 있다. |
| `Completed` | handler가 정상 완료되어 reply 또는 one-way 처리가 끝났다. |
| `Faulted` | handler 예외, timeout, cancellation, submit 실패가 request 정책에 따라 처리되어야 한다. |

turn 상태 전이는 yield terminator와 scheduler만 수행한다. 사용자 코드가 turn을 release하거나 resume하지
않는다.

### 3. framework call object

지원 대상:

- channel request
- Spot outbound request
- actor `JoinSpot`
- actor `JoinEntrySpot`
- bound session send completion
- `RunWorker` completion

제외 대상:

- 사용자 코드가 만든 `Task`, `Promise`, `CompletionStage`
- 외부 HTTP client의 임의 async 호출
- channel send/publish, route mesh send/request처럼 이 문서에 public yield surface를 명시하지 않은 호출
- framework가 timeout과 cancellation을 reply error로 변환할 수 없는 호출
- shared mutable state를 await 전후로 이어서 다루는 handler

외부 async 작업을 Spot scheduler와 연결해야 한다면 worker 또는 별도 framework adapter를 사용한다.

### 4. snapshot과 validation

await 전에 필요한 값은 runtime-owned snapshot으로 보존한다.

- activation id와 generation
- actor id와 actor generation
- timer descriptor id 또는 timer generation
- source node routing id
- source session routing id
- bound session token
- request header와 metadata
- reply writer 또는 request sequence
- cancellation source

재개 시점에는 snapshot이 아직 유효한지 확인한다. actor가 다른 Spot으로 이동했거나 destroy되었으면
handler를 정상 continuation으로 재개하지 않고 cancellation/error path로 완료한다.

## 언어별 성능 검토 기준

| 항목 | 기준 |
|------|------|
| hot path allocation | 메시지 하나를 dispatch할 때 mailbox key, continuation wrapper, snapshot 객체를 불필요하게 반복 생성하지 않는다. |
| scheduler lock | lock을 잡은 채 사용자 handler, serializer, transport submit, callback을 호출하지 않는다. |
| fairness | ready continuation만 계속 처리하지 않는다. bounded ready budget을 둔다. |
| queue bound | actor/timer mailbox는 기존 dispatch queue capacity 또는 framework option과 연결한다. |
| local context | local context lookup은 handler 진입 또는 call object 생성 시점의 짧은 검증에만 사용한다. |
| timer policy | timer mailbox는 overrun 정책을 다시 계산하지 않는다. |
| metrics | pending count, ready count, yield count, resume latency, canceled continuation 수를 볼 수 있어야 한다. |

## 구현 순서

아래 순서는 구현을 나누어 출시한다는 뜻이 아니다. 기능은 Spot/Entry Spot 공통 기능으로 한 번에 완성한다.

1. `.NET` runtime에서 activation scheduler, mailbox key, turn state, snapshot validation을 먼저 정리한다.
2. 기존 `.NET` `Async(...)`는 유지하고 `YieldAsync(...)`를 캡처된 turn handle에 연결한다.
3. actor, timer, route, subscription, lifecycle dispatch 경로를 mailbox scheduler 위로 옮긴다.
4. `.NET` sample과 E2E 코드를 검색해서 `YieldAsync(...)` 적용 후보가 있는지 확인하고, 필요한 곳은 실제 sample/E2E 코드까지 바꾼다.
5. Java core scheduler와 `yieldAsync(...)`/`yieldAwait(...)` 경로를 같은 의미로 구현하고, yield-aware handler 실행을 virtual thread 또는 전용 blocking executor에 연결한다.
6. Java sample과 E2E 코드를 검색해서 동기식 handler 모양으로 `yieldAwait(...)`를 적용할 곳이 있는지 확인하고, 필요한 곳은 실제 sample/E2E 코드까지 바꾼다.
7. Kotlin은 `yieldAwait(...)` helper가 Java core 동작을 따르게 한다.
8. Kotlin sample과 E2E 코드를 검색해서 Java core helper 위에서 바꿔야 할 곳이 있는지 확인하고, 필요한 곳은 실제 sample/E2E 코드까지 바꾼다.
9. Node는 `yieldSubmit(...)`이 handler `Promise` 전체를 serial tail에 매달지 않게 한다.
10. Node/NestJS sample과 E2E 코드를 검색해서 `yieldSubmit(...)` 적용 후보가 있는지 확인하고, 필요한 곳은 실제 sample/E2E 코드까지 바꾼다.
11. C++은 `task_t` continuation scheduler와 async submit 기반 join call state를 정리한 뒤 `yield_async()`에 연결한다.
12. C++ sample과 E2E 코드를 검색해서 coroutine handler의 `yield_async()` 적용 후보가 있는지 확인하고, 필요한 곳은 실제 sample/E2E 코드까지 바꾼다.
13. 모든 언어에서 기본 serial 의미를 검증해야 하는 sample/E2E는 기존 terminator로 남겼는지 다시 확인한다. 적용할 곳이 없으면 어떤 sample/E2E를 확인했고 왜 유지했는지 구현 기록에 남긴다.
14. 구현이 끝난 뒤 공통 framework spec과 언어별 spec/guide에는 실제 구현된 동작만 반영한다.
15. 구현, 테스트, 문서 반영이 끝나면 Codex 에이전트로 누락과 남은 이슈를 리뷰한다. 이슈가 나오면 수정한 뒤 다시 리뷰하며, 남은 이슈가 없다는 판정이 나올 때만 최종 완료로 처리한다.

## 문서 반영 계획

이 계획 문서는 구현 전 초안이다. 구현이 끝나기 전에는 `framework/doc/framework/**/spec/` 아래 정식
계약 문서에 yield 동작을 공개 계약처럼 옮기지 않는다. 구현과 회귀 테스트가 끝난 뒤 실제 코드에 존재하는
API와 동작만 `framework/doc/` 아래 문서에 나누어 반영한다.

### 1. 공통 framework 문서

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/async-execution-policy.ko.md` | 기본 terminator는 serial 의미를 유지하고, yield 계열 terminator만 mailbox turn을 반납한다는 공통 실행 정책을 적는다. |
| `framework/doc/framework/common/spec/actor-model.ko.md` | actor mailbox, timer mailbox, spot/global mailbox의 순서 보장과 interleaving 범위를 설명한다. |
| `framework/doc/framework/common/spec/interaction-model.ko.md` | request, actor join, bound session send completion, worker completion에서 yield가 reply/error/cancellation 정책과 어떻게 연결되는지 정리한다. |
| `framework/doc/framework/common/spec/framework-api.ko.md` | 언어별 이름은 나열하지 않고, 공통 개념으로 yield terminator의 의미와 제외 대상을 적는다. |
| `framework/doc/framework/common/use-cases/03-worker-dispatch.ko.md` | worker completion을 yield로 기다리는 경우와 callback terminator를 쓰는 경우의 차이를 설명한다. |
| `framework/doc/framework/common/sample/README.ko.md` | Bingo처럼 player 단독 admission/preflight에서 yield를 쓰는 예제를 공통 sample 설명에 연결한다. |

공통 spec에는 내부 scheduler class 이름을 넣지 않는다. mailbox와 turn 같은 내부 구조를 설명해야 하면
언어별 internals 문서로 연결한다.

### 2. 언어별 spec 문서

| 언어 | 수정 문서 | 반영 내용 |
|------|-----------|-----------|
| `.NET` | `framework/doc/framework/dotnet/spec/aspnet-core-spot.ko.md` | Spot/Entry Spot handler에서 `YieldAsync(...)`를 호출했을 때의 mailbox turn 반납과 재개 규칙을 적는다. |
| `.NET` | `framework/doc/framework/dotnet/spec/aspnet-core-actor.ko.md` | `IZLinkActorJoinSpotCall`, `IZLinkActorJoinEntrySpotCall`, `IZLinkBoundSessionSendCall`의 yield terminator 계약을 적는다. |
| `.NET` | `framework/doc/framework/dotnet/spec/handler-interfaces.ko.md` | handler가 yield 전후에 같은 actor/timer 재진입을 받지 않는다는 규칙과 exception/cancellation 처리를 적는다. |
| Java | `framework/doc/framework/java/spec/spring-boot-spot.ko.md` | `yieldAwait(...)`가 동기식 handler 코드 모양을 유지하면서 mailbox turn을 반납하는 계약을 적는다. |
| Java | `framework/doc/framework/java/spec/spring-boot-actor-session.ko.md` | actor join과 bound session send completion의 yield 계약을 적는다. |
| Java | `framework/doc/framework/java/spec/handler-interfaces.ko.md` | 기존 동기 handler와 `ZLinkAwait.await(...)`는 기본 serial 의미를 유지하고, `yieldAwait(...)`는 virtual thread 또는 전용 blocking executor 위에서만 사용한다고 적는다. |
| Kotlin | `framework/doc/framework/kotlin/guide/05-spot.ko.md` | Kotlin은 Java core 위 adapter이며 `yieldAwait(...)` helper로 같은 yield 의미를 사용한다고 설명한다. |
| Node/NestJS | `framework/doc/framework/node/spec/nestjs-spot.ko.md` | `yieldSubmit(...)`이 handler `Promise` 전체를 serial tail에 매달지 않는 규칙을 적는다. |
| Node/NestJS | `framework/doc/framework/node/spec/nestjs-actor.ko.md` | actor join과 bound session send completion의 `yieldSubmit(...)` 계약을 적는다. |
| Node/NestJS | `framework/doc/framework/node/spec/handler-interfaces.ko.md` | `AsyncLocalStorage`는 기본 serial handler의 request/logging context로만 보장하고, yield 경로의 turn 저장소가 아니라고 적는다. |
| C++ | `framework/doc/framework/cpp/spec/cpp-spot.ko.md` | `yield_async()`가 coroutine continuation을 원래 mailbox로 재개하는 규칙을 적는다. |
| C++ | `framework/doc/framework/cpp/spec/actor-gateway-session-relay.ko.md` | actor join과 bound session send completion의 yield 계약을 적는다. |
| C++ | `framework/doc/framework/cpp/spec/handler-interfaces.ko.md` | `task_t` handler에서 yield 전후 actor/timer 재진입 금지와 exception/error path를 적는다. |

언어별 spec은 해당 언어의 실제 public surface만 적는다. 다른 언어에 있는 이름을 근거로 아직 구현되지 않은
API를 spec에 먼저 쓰지 않는다.

### 3. 언어별 guide 문서

| 언어 | 수정 문서 | 반영 내용 |
|------|-----------|-----------|
| `.NET` | `framework/doc/framework/dotnet/guide/05-spot.ko.md`, `framework/doc/framework/dotnet/guide/06-actor-spot.ko.md` | Bingo match처럼 player 단독 admission I/O에서 `YieldAsync(...)`를 쓰는 예제와 사용 금지 패턴을 함께 둔다. |
| Java | `framework/doc/framework/java/guide/05-spot.ko.md`, `framework/doc/framework/java/guide/06-actor-session.ko.md` | `yieldAwait(...)` 예제와 기존 `await(...)` helper가 serial 의미를 유지한다는 설명을 둔다. |
| Kotlin | `framework/doc/framework/kotlin/guide/05-spot.ko.md`, `framework/doc/framework/kotlin/guide/06-actor-session.ko.md` | `yieldAwait(...)` 예제와 coroutine context를 turn 저장소로 쓰지 않는 가이드를 둔다. |
| Node/NestJS | `framework/doc/framework/node/guide/05-spot.ko.md`, `framework/doc/framework/node/guide/06-actor-session.ko.md` | `yieldSubmit(...)` 예제와 `AsyncLocalStorage` 사용 가능 범위, 금지 범위를 함께 적는다. |
| C++ | `framework/doc/framework/cpp/guide/08-spot.ko.md`, `framework/doc/framework/cpp/guide/09-actor-session.ko.md` | `yield_async()` 예제와 `thread_local`을 turn 저장소로 쓰지 않는 가이드를 둔다. |

guide에는 내부 scheduler class 이름을 설명하지 않는다. 사용자가 알아야 하는 것은 언제 yield를 쓰고 언제 쓰지
말아야 하는지, 그리고 await 뒤 shared state를 다시 확인해야 한다는 점이다.

### 4. internals와 regression 문서

| 언어 | 수정 문서 | 반영 내용 |
|------|-----------|-----------|
| `.NET` | `framework/doc/framework/dotnet/internals/behavior-matrix.ko.md`, `framework/doc/framework/dotnet/internals/regression-test-matrix.ko.md` | mailbox scheduler, turn state, snapshot validation, local context 검증 테스트를 적는다. |
| Java | `framework/doc/framework/java/internals/behavior-matrix.ko.md`, `framework/doc/framework/java/internals/regression-test-matrix.ko.md` | `yieldAwait(...)`가 turn 반납, completion 대기, scheduler resume permit 대기를 어떻게 연결하는지 적는다. |
| Node/NestJS | `framework/doc/framework/node/internals/behavior-matrix.ko.md`, `framework/doc/framework/node/internals/regression-test-matrix.ko.md` | `Promise` continuation 재투입, `AsyncLocalStorage` 비의존, fairness 테스트를 적는다. |
| C++ | `framework/doc/framework/cpp/internals/cpp-framework-overview.ko.md`, `framework/doc/framework/cpp/internals/regression-test-matrix.ko.md` | `task_t` continuation scheduler와 native message lifetime snapshot을 적는다. |

internals 문서에는 scheduler 구조와 failure path를 다이어그램으로 설명한다. ASCII 다이어그램을 쓰면 다이어그램
안의 텍스트는 영문만 사용한다.

### 5. feature-map과 sample 문서

각 언어의 feature-map에는 yield dispatch 지원 여부를 완료 항목으로 추가한다.

| 언어 | 수정 문서 |
|------|-----------|
| `.NET` | `framework/doc/framework/dotnet/guide/11-feature-map.ko.md` |
| Java | `framework/doc/framework/java/guide/10-feature-map.ko.md` |
| Kotlin | `framework/doc/framework/kotlin/guide/10-feature-map.ko.md` |
| Node/NestJS | `framework/doc/framework/node/guide/10-feature-map.ko.md` |
| C++ | `framework/doc/framework/cpp/guide/15-feature-map.ko.md` |

sample 문서는 실제 sample 코드가 yield 계열 terminator로 바뀐 뒤에만 갱신한다. sample이 여전히 기본
terminator를 쓰고 있으면 guide에 yield 예제를 먼저 넣지 않는다.

### 6. sample과 E2E 코드 반영

구현 뒤에는 언어별 sample과 E2E 코드를 함께 확인한다. yield dispatch는 사용자가 따라 할 공개 사용 패턴에
영향을 주므로 runtime만 바꾸고 sample/E2E를 그대로 두면 완료로 보지 않는다.

sample/E2E 반영은 문서 작성 작업이 아니라 구현 완료 조건이다. 각 언어 runtime을 구현한 뒤에는 해당 언어의
sample과 E2E 코드를 직접 조사한다. yield로 바꿔야 하는 handler가 있으면 실제 코드를 바꾸고, 없으면
미적용 사유를 기록한다. 이 과정을 생략한 상태에서는 feature-map을 완료로 표시하지 않는다.

이 조사는 문서 예제 작성보다 먼저 한다. 문서에는 실제로 바뀐 sample/E2E 코드의 public API 이름과 의미만
반영한다. sample이나 E2E에서 필요한 적용이 남아 있는데 guide 예제만 먼저 추가하는 것은 완료로 보지 않는다.

sample과 E2E는 사용자가 직접 따라 하는 공개 사용 패턴이므로, runtime 테스트만 통과해도 완료가 아니다.
각 언어 구현자는 sample/E2E에서 실제로 I/O 대기 흐름이 있는 handler를 열어 보고, 안전한 후보가 있으면
해당 코드까지 yield 계열 terminator로 바꾼다. 반대로 shared mutable state 위험이 있거나 기본 serial 의미를
검증하는 코드라면 그대로 두되, 그 이유를 구현 기록, 테스트 이름, 또는 가까운 테스트 주석에 남긴다.

적용 기준:

- player actor 한 명의 admission/preflight처럼 다른 actor와 timer를 막지 않는 것이 목적이고, await 전후
  shared mutable state 의존이 없는 sample handler에는 yield 계열 terminator를 적용한다.
- Bingo match handler처럼 외부 API channel request 뒤 actor `JoinSpot`을 이어서 기다리는 흐름은 우선 검토
  대상이다.
- SupportChat, TicTacToe, DeliveryDispatch, ShoppingMall, GameQuest sample에서도 같은 패턴이 있는지 언어별로
  검색한다.
- E2E는 Spot/Entry Spot handler를 실제로 실행하는 scenario가 있는 경우 yield 의미를 검증하는 scenario를
  추가하되, 기본 serial 의미를 검증하는 기존 scenario는 yield로 바꾸지 않는다. registry, discovery,
  codec처럼 Spot handler를 실행하지 않는 E2E는 억지로 yield scenario로 바꾸지 않고 미적용 사유를 남긴다.
- 기존 sample이 shared mutable state를 await 전후로 이어서 다루면 yield를 적용하지 않고, guide의 금지
  패턴 예시나 별도 테스트로 남긴다.
- sample에서 callback/push 방식으로 request/reply 흐름을 쪼개는 우회 코드를 만들지 않는다. yield가 필요한
  곳은 public yield terminator를 사용한다.
- sample 또는 E2E에서 안전한 yield 적용 후보가 발견됐는데 실제 코드가 기본 terminator로 남아 있으면
  문서 반영을 완료하지 않는다. 먼저 해당 sample/E2E 코드를 바꾸거나, shared mutable state 위험 같은
  미적용 사유를 구현 기록에 남긴다.
- framework/doc의 guide 예제는 실제 sample/E2E 코드와 같은 public API 이름을 사용한다. sample/E2E 코드가
  아직 바뀌지 않은 언어에서는 guide가 먼저 완료됐다고 표시하지 않는다.

조사 방법:

- 각 언어 sample에서 Spot/Entry Spot handler, actor handler, timer handler 안의 channel request, Spot
  request, actor join, bound session send, worker completion 대기 코드를 검색한다.
- 각 언어 E2E에서 sample과 같은 사용 패턴을 검증하는 코드가 있는지 확인한다. 필요한 경우 E2E scenario도
  public yield terminator를 사용하도록 바꾸고, scheduler 의미를 검증하는 E2E는 별도 scenario로 둔다.
  현재 트리에 해당 E2E가 없으면 무관한 topology/codec E2E를 바꾸지 말고, 없는 이유와 대신 적용한
  runtime/sample 회귀 테스트를 구현 기록에 남긴다.
- request 전후로 room list, match queue, aggregate state, mutable session map 같은 shared state를 이어서
  읽거나 쓰는지 확인한다. 이런 코드는 yield로 바꾸지 않는다.
- player 단독 admission처럼 await 전후에 actor-local 값과 reply 값만 사용하는 흐름은 yield 적용 후보로
  표시하고 실제 public terminator로 바꾼다.
- E2E에서는 sample 사용 패턴을 검증하는 scenario와 scheduler 의미를 검증하는 scenario를 분리한다. sample
  pattern E2E는 실제 sample과 같은 public API 이름을 사용하고, scheduler E2E는 같은 actor/timer 재진입
  금지와 다른 actor/timer interleaving을 직접 확인한다. Spot/Entry Spot E2E가 없는 언어는 runtime
  회귀 테스트와 sample compile/smoke로 의미를 고정하고, E2E 미적용 사유를 남긴다.
- yield를 적용하지 않은 sample/E2E는 “안전하지 않아서 유지”, “I/O 대기 없음”, “기본 serial 의미 검증용”처럼
  미적용 이유를 구현 기록이나 테스트 주석에 남긴다.
- 코드 검색 결과만으로 완료 처리하지 않는다. 후보 handler 파일을 열어서 await 전후에 actor-local 값과
  reply 값만 사용하는지, Spot 공용 mutable state나 timer aggregate를 이어서 쓰는지 확인한다.

확인 및 적용 대상:

| 영역 | 확인 내용 |
|------|-----------|
| `.NET` sample | Bingo `MatchBingoActorHandler`와 다른 Spot/Entry Spot handler에서 admission/preflight I/O가 있는지 확인한다. |
| Java sample | 동기식 handler 모양을 유지하면서 `yieldAwait(...)`를 적용할 곳이 있는지 확인한다. |
| Kotlin sample | Java core helper 위에서 `yieldAwait(...)`를 적용할 곳이 있는지 확인한다. |
| Node/NestJS sample | `yieldSubmit(...)`로 바꿔야 하는 Spot/Entry Spot handler가 있는지 확인한다. |
| C++ sample | `yield_async()`로 바꿔야 하는 coroutine handler가 있는지 확인한다. |
| 공통 E2E | 공통 E2E 트리가 있으면 actor yield 중 다른 actor 실행, 같은 actor 재진입 금지, timer yield, timeout/cancellation race를 scenario로 추가한다. 공통 E2E 트리가 없으면 runtime 회귀 테스트와 언어별 sample/E2E 확인 기록으로 대체한다. |
| 언어별 E2E | 각 언어 public API 이름으로 같은 의미가 검증되는지 확인한다. Spot/Entry Spot handler E2E가 없는 언어는 적용할 코드가 없다는 사유와 대신 실행한 runtime/sample 검증을 기록한다. |

확인 경로:

| 경로 | 처리 기준 |
|------|-----------|
| `framework/languages/dotnet/samples/` | 실제 Bingo admission/preflight handler부터 확인하고, 안전한 후보는 `YieldAsync(...)`로 바꾼다. |
| `framework/languages/java/samples/` | handler 코드 모양이 동기식으로 유지되는지 보면서 `yieldAwait(...)` 후보를 적용한다. |
| `framework/languages/java/samples/kotlin/` | Java core helper를 쓰는 Kotlin sample에서 `yieldAwait(...)` 후보가 있는지 확인한다. |
| `framework/languages/node/samples/` | `submit(...)`을 기다리는 Spot/Entry Spot handler 중 안전한 후보를 `yieldSubmit(...)`으로 바꾼다. |
| `framework/languages/cpp/samples/` | coroutine handler에서 `co_await ... async()`를 쓰는 후보를 확인하고 안전한 곳만 `yield_async()`로 바꾼다. |
| `framework/common/e2e/` | 디렉토리가 있으면 공통 scenario 정의가 yield dispatch를 요구하는지 확인하고, 요구하면 언어별 E2E에 반영할 기준을 적는다. 디렉토리가 없으면 없는 사실을 구현 기록에 남긴다. |
| `framework/languages/*/e2e/` | 언어별 public API 이름으로 sample pattern E2E와 scheduler semantics E2E를 검증할 수 있는 Spot/Entry Spot scenario가 있는지 확인한다. 없으면 무관한 E2E를 바꾸지 않고 미적용 사유를 남긴다. |

완료 체크:

- 각 언어 sample에서 yield 적용 후보를 검색한 결과가 구현 기록에 남아 있다.
- 적용 후보 중 안전한 곳은 sample 코드가 yield 계열 terminator로 바뀌어 있다.
- 안전하지 않은 후보는 guide의 금지 패턴과 연결되어 있으며, sample 코드는 기본 terminator를 유지한다.
- 각 언어 E2E에서 필요한 yield 적용 후보를 확인했고, 안전한 후보가 실제로 있으면 E2E 코드가 yield 계열
  terminator를 사용한다.
- E2E에 yield를 적용하지 않은 경우에는 shared mutable state 위험, I/O 대기 없음, 기본 serial 의미 검증,
  또는 현재 E2E가 Spot/Entry Spot handler를 실행하지 않는다는 미적용 사유가 남아 있다.
- Spot/Entry Spot E2E가 있는 언어에는 yield interleaving 자체를 검증하는 scenario와 기존 serial 동작을
  유지하는 scenario가 함께 있다. 해당 E2E가 없는 언어는 runtime 회귀 테스트와 sample compile/smoke로
  의미를 고정하고, E2E 미적용 사유를 남긴다.
- sample과 E2E가 문서 예제와 다른 public API 이름을 쓰지 않는다.
- sample과 E2E 적용 결과가 언어별 feature-map 완료 판정과 문서 반영 범위에 연결되어 있다.

### 7. 최종 Codex 리뷰

문서 반영까지 끝난 뒤에는 Codex 에이전트 리뷰를 별도 완료 조건으로 둔다. 리뷰는 한 번으로 끝내지 않고,
남은 이슈가 없을 때까지 반복한다.

리뷰 범위:

- 공통 spec, 언어별 spec, guide, internals, feature-map 사이의 설명이 서로 충돌하지 않는지 확인한다.
- 실제 구현된 public API와 문서의 API 이름, 반환 타입, timeout/cancellation 설명이 맞는지 확인한다.
- 기본 terminator의 serial 의미와 yield 계열 terminator의 interleaving 의미가 문서 전체에서 섞이지 않았는지 확인한다.
- Java의 `yieldAwait(...)`, Kotlin의 helper, Node의 `yieldSubmit(...)`, C++의 `yield_async()`처럼 언어별 차이가 빠지지 않았는지 확인한다.
- local context 사용 가능 범위와 yield 경로의 금지 범위가 문서 전체에서 같은 의미로 쓰였는지 확인한다.
- sample과 E2E 실제 코드가 필요한 위치에 yield 계열 terminator를 사용하고, 기본 serial 의미를 검증해야 하는 코드는 기존 terminator로 남겨 두었는지 확인한다.
- sample과 E2E 적용이 누락된 언어가 있으면 문서만 수정하지 말고 해당 sample/E2E 코드 또는 미적용 사유까지 보완했는지 확인한다.
- 회귀 테스트 계획이 실제 테스트 파일과 feature-map 완료 판정에 반영되었는지 확인한다.

반복 절차:

1. Codex 에이전트에 구현 diff와 `framework/doc/` 변경 diff를 함께 맡겨 누락, 모순, overclaim만 리뷰하게 한다.
2. 리뷰 결과에 substantive finding이 있으면 코드, 테스트, 문서 중 필요한 위치를 수정한다.
3. 수정 뒤 같은 범위로 다시 Codex 에이전트 리뷰를 요청한다.
4. Codex 에이전트가 `NO FINDINGS` 또는 동등한 clean 판정을 낼 때까지 1-3을 반복한다.
5. clean 판정 뒤에만 feature-map과 완료 기준을 최종 완료로 본다.

## 회귀 테스트 계획

회귀 테스트는 단순히 “다른 actor가 실행된다”만 확인하면 부족하다. yield는 scheduler, reply writer,
cancellation, actor lifetime, timer overrun, 언어별 async runtime이 함께 걸리는 기능이므로 아래 범주를
각 언어에서 가능한 수준까지 검증한다.

### 1. 기본 serial 의미 보존

| 테스트 | 기대 결과 |
|--------|-----------|
| basic request keeps serial gate | `Async`/`submit`/`async`만 사용하는 handler가 request를 기다리는 동안 같은 Spot/Entry Spot의 다음 작업이 시작되지 않는다. |
| basic worker submit keeps serial gate | worker call의 기본 awaitable terminator를 기다리는 handler는 기존 serial 의미를 유지한다. |
| basic actor join keeps serial gate | actor join을 기본 terminator로 기다리면 기존처럼 activation 실행 줄이 completion까지 유지된다. |
| basic timer handler keeps serial gate | timer handler가 기본 terminator를 기다리면 같은 activation의 다른 작업이 기존처럼 대기한다. |
| mixed basic and yield handlers | 같은 activation 안에서 기본 terminator를 쓰는 handler는 yield handler가 추가되어도 기존 serial 의미가 바뀌지 않는다. |

### 2. mailbox 순서와 interleaving

| 테스트 | 기대 결과 |
|--------|-----------|
| actor yield lets different actor run | actor A handler가 yield request를 기다리는 동안 actor B handler가 실행된다. |
| actor yield blocks same actor reentry | actor A handler가 yield 중이면 actor A의 다음 packet은 continuation 뒤에 실행된다. |
| actor yield preserves queued order | actor A의 continuation과 actor A의 다음 packet이 같은 mailbox FIFO 순서대로 실행된다. |
| timer yield lets actor run | timer T가 yield request를 기다리는 동안 actor handler가 실행된다. |
| timer yield blocks same timer reentry | timer T의 다음 tick은 이전 tick continuation 뒤에 실행된다. |
| different timers can interleave | timer T1이 yield 중이면 timer T2는 자기 mailbox 순서 안에서 실행될 수 있다. |
| spot lifecycle waits for global mailbox | close, activation dispose, subscription drain 같은 spot/global 작업은 필요한 global 순서를 깨지 않는다. |
| ready continuation fairness | ready continuation이 많이 쌓여도 새 inbound actor/timer 작업을 계속 굶기지 않는다. |

### 3. 지원 call object별 동작

| 테스트 | 기대 결과 |
|--------|-----------|
| channel request yield success | channel request reply가 도착하면 원래 mailbox에서 handler continuation이 재개되고 typed reply가 반환된다. |
| Spot outbound request yield success | Spot outbound request reply가 도착하면 기존 request success path와 같은 reply decode/error 정책을 사용한다. |
| actor JoinSpot yield success | `JoinSpot` reply가 도착하면 actor join result가 원래 actor mailbox continuation으로 전달된다. |
| actor JoinEntrySpot yield success | `JoinEntrySpot` reply가 도착하면 actor join result가 원래 actor mailbox continuation으로 전달된다. |
| bound session send yield success | bound session send completion이 원래 actor mailbox continuation으로 돌아온다. |
| RunWorker yield success | worker completion이 원래 actor/timer/spot mailbox continuation으로 돌아온다. |
| excluded call has no yield terminator | channel send/publish, route mesh send/request, 외부 async 작업에는 이 문서가 정의하지 않은 yield terminator가 노출되지 않는다. |
| one terminator only | 같은 call object에서 기본 terminator와 yield terminator를 중복 호출하면 기존 terminator 중복 오류 정책으로 실패한다. |

### 4. timeout, cancellation, reply race

| 테스트 | 기대 결과 |
|--------|-----------|
| request timeout before reply | timeout이 먼저 발생하면 pending turn을 정리하고 기존 request timeout error path로 완료한다. |
| reply before timeout | reply가 먼저 도착하면 timeout callback이 늦게 실행되어도 continuation을 다시 실행하지 않는다. |
| cancellation before submit | cancellation이 이미 요청된 상태에서 yield terminator를 호출하면 submit을 시작하지 않거나 즉시 canceled path로 완료한다. |
| cancellation while pending | pending yield turn이 cancellation으로 정리되고 같은 mailbox의 다음 작업이 막히지 않는다. |
| reply after cancellation | cancellation 뒤 늦게 도착한 reply는 dropped completion으로 처리되고 user continuation을 다시 실행하지 않는다. |
| submit failure | transport submit 자체가 실패하면 turn이 `Faulted`로 정리되고 기존 handler exception/error path로 보고된다. |
| decode failure after yield | reply decode 실패가 기존 request decode error와 같은 방식으로 보고된다. |
| handler exception after yield | continuation 이후 예외가 기존 handler exception path로 보고된다. |

### 5. actor, timer, activation lifetime

| 테스트 | 기대 결과 |
|--------|-----------|
| reply after actor destroy | actor destroy 뒤 도착한 reply는 handler를 재개하지 않고 error/cancel path로 정리된다. |
| reply after actor moved | actor가 yield 중 다른 Spot으로 이동했으면 actor generation mismatch로 continuation을 실행하지 않는다. |
| reply after activation close | activation close 이후 도착한 completion은 reply writer와 pending turn을 정리하고 user code를 실행하지 않는다. |
| shutdown drains pending turns | node shutdown이 pending yield turn을 cancellation/error path로 끝내고 queue에 turn을 남기지 않는다. |
| timer disposed while pending | timer가 dispose된 뒤 도착한 completion은 timer handler continuation을 실행하지 않는다. |
| timer overrun skip policy | timer runtime이 skip하기로 한 tick은 timer mailbox에 들어가지 않는다. |
| timer catch-up bounded policy | catch-up bounded 정책은 정해진 tick 수만 mailbox에 넣고 scheduler가 별도로 overrun을 재계산하지 않는다. |

### 6. local context와 snapshot

| 테스트 | 기대 결과 |
|--------|-----------|
| local context without yield | `Async`/`submit`/`async`만 사용하는 handler에서는 tracing/logging/validation용 local context가 기존 언어 runtime 규칙대로 유지된다. |
| local context with yield | yield 경로에서는 completion callback이 local context를 다시 조회해 turn이나 mailbox를 찾지 않는다. |
| missing turn in yield | Spot handler 안에서 yield terminator가 turn handle을 갖지 못하면 diagnostics/test failure가 난다. |
| immutable snapshot survives yield | request header, metadata, reply target, cancellation source는 await 전에 runtime-owned snapshot으로 보존된다. |
| mutable payload lifetime | native/C++ payload나 message view가 continuation보다 짧게 살아도 snapshot 복사 때문에 use-after-free가 나지 않는다. |
| stale shared state misuse sample | await 전에 읽은 shared state로 await 뒤 결정하는 금지 패턴을 sample/guide 검증에서 탐지한다. |

### 7. 언어별 surface와 runtime 특성

| 언어 | 테스트 |
|------|--------|
| `.NET` | `YieldAsync(...)`가 `AsyncLocal`에 의존하지 않고 캡처된 turn handle로 원래 mailbox에 재개되는지 확인한다. |
| `.NET` | `ValueTask`가 두 번 await되거나 terminator가 두 번 호출되는 사용 오류를 기존 정책대로 실패시키는지 확인한다. |
| Java | 기존 동기 handler와 `ZLinkAwait.await(...)`는 serial 의미를 유지하고, `yieldAwait(...)`를 호출한 handler만 interleaving을 허용하는지 확인한다. |
| Java | `yieldAwait(...)`를 platform thread serial executor에서 직접 막지 않고 virtual thread 또는 전용 blocking executor에서 실행하는지 확인한다. |
| Java | `ThreadLocal`이 completion thread에서 원래 turn을 찾는 데 사용되지 않는지 확인한다. |
| Kotlin | `yieldAwait(...)` helper가 Java `yieldAsync(...)`를 호출하고 `CoroutineContext`를 turn 저장소로 사용하지 않는지 확인한다. |
| Node/NestJS | `yieldSubmit(...)`이 handler `Promise` 전체를 serial tail에 매달지 않고, `AsyncLocalStorage` 없이 캡처된 turn handle로 재개되는지 확인한다. |
| Node/NestJS | `AbortSignal` cancellation과 late promise resolution이 pending turn을 한 번만 완료하는지 확인한다. |
| C++ | `yield_async()` continuation이 resume thread와 무관하게 원래 mailbox로 들어오는지 확인한다. |
| C++ | `task_t` cancellation/error와 native message lifetime snapshot이 use-after-free 없이 동작하는지 확인한다. |

### 8. 부하와 성능 회귀

| 테스트 | 기대 결과 |
|--------|-----------|
| many actors admission load | 많은 player actor가 admission yield request를 동시에 기다려도 unrelated actor/timer latency가 제한 안에 들어온다. |
| same actor pressure | 한 actor에 많은 packet이 몰려도 yield continuation과 다음 packet 순서가 깨지지 않는다. |
| ready queue pressure | ready continuation이 대량으로 도착해도 bounded ready budget 때문에 inbound 처리가 계속 진행된다. |
| pending turn cleanup | timeout/cancellation/shutdown 뒤 pending turn count가 0으로 돌아온다. |
| allocation budget | yield를 사용하지 않는 hot path에서 mailbox key, snapshot, continuation wrapper allocation이 불필요하게 증가하지 않는다. |

## 위험과 대응

| 위험 | 대응 |
|------|------|
| await 뒤 Spot 공용 상태가 바뀐다. | yield 사용 범위를 admission/preflight처럼 안전한 흐름으로 제한하고, guide에서 await 뒤 필요한 값을 다시 읽거나 generation을 확인하라고 설명한다. |
| yield가 일반 성능 옵션처럼 남용된다. | guide와 sample에서 사용 권장/금지 패턴을 함께 제시한다. |
| arbitrary async 작업까지 자동으로 yield 처리하려 한다. | framework call object의 yield terminator에만 적용한다. 외부 async는 worker 또는 adapter를 사용한다. |
| yield 경로가 local context에 의존한다. | 기본 serial handler의 일반 async context 사용은 허용한다. yield 경로의 turn handle은 scheduler-owned state로 두고 call object 생성 시점에 캡처한다. |
| ready continuation이 일반 inbound를 굶긴다. | bounded ready priority와 fairness 테스트를 둔다. |
| native message lifetime이 continuation보다 짧다. | await 전에 header, payload, metadata, reply target을 runtime-owned snapshot으로 복사한다. |
| actor가 yield 중 이동하거나 삭제된다. | actor generation과 activation generation을 snapshot으로 확인한다. |
| timer overrun 정책이 mailbox와 중복된다. | timer runtime만 overrun 정책을 결정한다. timer mailbox는 FIFO만 맡는다. |

## 완료 기준

- 기본 terminator의 기존 serial 의미가 유지된다.
- yield 계열 terminator 이름이 언어별로 통일된다.
- Spot/Entry Spot handler 안에서 yield terminator가 mailbox turn을 반납하고 원래 mailbox에서 재개한다.
- 같은 actor와 같은 timer는 yield 전후에도 재진입하지 않는다.
- 다른 actor와 다른 timer는 yield I/O 대기 중 interleave될 수 있다.
- shared mutable state를 await 전후로 이어서 다루는 handler에는 yield 사용을 권장하지 않는다.
- yield 경로에서 local context를 turn 저장소로 삼지 않는다.
- request success, timeout, cancellation, handler exception, shutdown path가 기존 reply/error 정책과 맞다.
- Bingo sample에서 callback/push로 handler를 쪼개지 않고도 player admission 병목이 줄어든다.
- 언어별 sample과 E2E 코드를 모두 확인했고, 필요한 sample과 Spot/Entry Spot E2E가 있으면 yield 계열
  terminator로 실제 코드까지 반영했다.
- sample/E2E에 적용하지 않은 후보는 shared mutable state 위험, I/O 대기 없음, 기본 serial 의미 검증,
  또는 현재 E2E가 Spot/Entry Spot handler를 실행하지 않는다는 미적용 사유가 기록되어 있다.
