<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Draft -- ZLink Framework .NET STREAM Decisions](./stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[.NET 묶음](../README.ko.md) | [SPOT](../spec/aspnet-core-spot.ko.md) | [인터페이스](../spec/handler-interfaces.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [공통 스펙](../../../../doc/spec/framework-api.ko.md)

# Draft -- ZLink Framework .NET SPOT Timer Policy

> 이 문서는 `.NET` SPOT timer 정책을 구현하기 전에 작성한 draft 기록이다.
> 구현이 끝난 뒤의 공개 계약[^public-contract]은 정식 spec 문서가 소유한다.
> 이 문서는 왜 기존 `AddTimer` 표면을 강화했는지와 어떤 회귀 테스트로 막는지를
> 확인하는 배경 자료로 유지한다.

## 1. 목적

`SPOT`[^spot] timer는 room, stage, match 같은 서버 실행 문맥 안에서 주기 작업을
실행하기 위한 표면이다. 현재 문서와 구현은 timer가 framework runtime이 만든
managed `.NET` timer 위에서 동작한다고 설명한다. 이 방향은 유지한다.

이 timer는 client[^client] render frame, input polling, local interpolation 을 위한
timer가 아니다. 서버가 권위 상태[^authoritative-state]를 진행하거나 정리할 때 쓰는
server-side timer다. cleanup, heartbeat, timeout sweep, matchmaking wait check,
room tick, match tick이 모두 같은 범용 timer의 사용 사례다.

다만 짧은 주기의 server tick처럼 지연이 의미 있는 사용 사례에서는 단순히
`period`마다 callback을 호출하는 것만으로는 부족하다. application은 현재 tick이 몇
번째인지, 예정 시각보다 얼마나 늦었는지, 밀린 tick을 어떻게 처리했는지 알 수 있어야
한다.

이 초안의 목표는 새 timer API를 추가하는 것이 아니다. 기존
`Context.AddTimer<THandler>(...)`와 `IZLinkSpotTimerHandler<TSpot>`의 계약을
강화해서, lifecycle timer와 server tick을 같은 표면으로 설명할 수 있게 하는
것이다.

## 2. 현재 기준

이 draft 작성 전 `.NET` framework 구현은 `System.Threading.PeriodicTimer`를
사용했다. 즉 SPOT timer는 CAPI timer handle을 직접 노출하지 않았다. 또한
client-side frame timer와 역할을 섞지 않았다.

현재 흐름은 다음과 같다.

1. application이 `Context.AddTimer<THandler>(name, period, ...)`를 호출한다.
2. framework runtime이 managed `.NET` timer loop를 만든다.
3. timer tick이 발생하면 runtime이 등록된 timer handler를 호출한다.
4. user Spot에서는 timer handler가 같은 Spot 실행 queue 안에서 실행된다.
5. timer handle의 `CancelAsync()`는 framework가 만든 managed timer loop를
   중단하고 정리한다.

이 모델은 Spot 상태를 안전하게 다룰 수 있다는 장점이 있다. 하지만 현재 계약은
다음 항목을 충분히 드러내지 않는다.

- tick 번호와 시간 정보
- 지연된 tick 처리 정책
- handler 실행 시간이 period보다 길 때의 동작
- callback 예외 처리 정책
- timer가 범용 서버 timer이며 client timer가 아니라는 점
- Entry Spot timer가 user Spot timer와 같은 등록 표면을 쓰되 전체 직렬화에
  묶이지 않는다는 점
- timer 시간 계산은 wall clock 보정에 흔들리지 않는 monotonic 기준을 써야 한다는 점

## 3. 결정

### 3.1 CAPI timer는 공개 표면으로 올리지 않는다

SPOT timer는 계속 managed `.NET` scheduler가 소유한다. framework가 내부에서 native
poller나 CAPI timer를 활용할 수는 있지만, application이 보는 계약은 CAPI timer가
아니다.

이 결정을 유지하는 이유는 다음과 같다.

- Spot callback은 framework의 실행 문맥, DI scope[^di-scope], cancellation 규칙과
  함께 돌아야 한다.
- CAPI timer handle을 그대로 노출하면 사용자가 Spot 실행 queue 밖에서 상태를
  변경하기 쉽다.
- timer 구현을 바꾸더라도 public 계약은 `AddTimer`와 timer handler 중심으로
  유지되어야 한다.

### 3.2 timer 종류를 나누지 않고 기존 AddTimer를 강화한다

cleanup timer와 room 서버 tick을 별도 API나 별도 timer 종류로 나누지 않는다. 둘 다
서버 실행 문맥 안에서 주기 callback을 받는다는 점은 같기 때문이다.

공개 계약 관점에서 timer는 한 종류다. 별도 `AddTick` 같은 이름도 만들지 않는다.
기존 timer 표면은 아래 방향으로 바꿨다.

```csharp
public interface IZLinkSpotContext
{
    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;
}

public interface IZLinkEntrySpotContext
{
    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default)
        where THandler : class;
}
```

`options`는 timer의 실행 정책을 설명한다. 이름은 여전히 `AddTimer`다. 호출자가
짧은 주기의 server tick을 원하면 짧은 period와 명시적인 overrun 정책을 사용한다.

### 3.3 timer handler는 tick metadata를 받는다

draft 작성 전 handler는 timer tick에 대한 정보를 받지 못했다. 이 방식은 sweep이나
cleanup 에는 충분하지만, 고정 주기 서버 루프에는 부족했다.

따라서 handler 계약은 아래처럼 바꿨다.

```csharp
public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken);
}
```

`ZLinkTimerTick`은 최소한 아래 정보를 가져야 한다. 시간 계산은 monotonic clock
기준으로 한다. `DateTimeOffset` 값은 로그와 운영 관찰을 위한 wall-clock 값이고,
지연 계산과 skip 계산의 기준은 `ScheduledElapsed`, `StartedElapsed`다.

```csharp
public readonly record struct ZLinkTimerTick(
    string Name,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    TimeSpan Period,
    DateTimeOffset ScheduledAt,
    DateTimeOffset StartedAt,
    TimeSpan ScheduledElapsed,
    TimeSpan StartedElapsed,
    TimeSpan Delay,
    ulong SkippedTicks);
```

- `Name`은 `AddTimer`에 넘긴 timer 이름이다.
- `DeliveryIndex`는 timer 시작 뒤 application handler에 실제 전달된 callback
  번호다. 첫 callback은 `1`이다.
- `ScheduledIndex`는 fixed-rate 기준 시간표에서 이 callback이 대표하는 tick
  번호다. tick을 건너뛰면 `ScheduledIndex`는 `DeliveryIndex`보다 커질 수 있다.
- `Period`는 등록된 기본 주기다.
- `ScheduledAt`은 이 tick이 실행되어야 했던 예정 시각을 wall-clock 기준으로
  표현한 값이다. 로그와 관찰용이며 정밀 계산의 기준이 아니다.
- `StartedAt`은 handler 실행을 시작한 실제 시각을 wall-clock 기준으로 표현한
  값이다.
- `ScheduledElapsed`는 timer 시작 뒤 이 tick이 실행되어야 했던 monotonic 경과
  시간이다.
- `StartedElapsed`는 timer 시작 뒤 handler 실행을 시작한 monotonic 경과 시간이다.
- `Delay`는 `StartedElapsed - ScheduledElapsed`이다. 시스템 시간 보정 때문에
  `DateTimeOffset` 값이 움직여도 `Delay` 계산은 흔들리지 않아야 한다.
- `SkippedTicks`는 overrun 정책 때문에 application handler에 전달하지 않고
  건너뛴 tick 수다. 이 값은 현재 callback이 대표하는 tick 앞에서 생략된 수만
  센다. 현재 callback 자신은 포함하지 않는다.

이 정보가 있으면 application은 같은 `AddTimer` 표면으로도 sweep, heartbeat, room
server tick을 구분해서 처리할 수 있다.

### 3.4 overrun 정책을 명시한다

timer handler가 period보다 오래 걸리거나 Spot 실행 queue가 밀리면 tick이 늦어진다.
이 상황을 숨기면 짧은 주기 서버 tick에서는 상태 진행이 흔들린다.

`ZLinkTimerOptions`에는 최소한 overrun 정책이 들어가야 한다.

```csharp
public sealed record ZLinkTimerOptions
{
    public ZLinkTimerOverrunPolicy OverrunPolicy { get; init; } =
        ZLinkTimerOverrunPolicy.SkipLateTicks;

    public int MaxCatchUpTicks { get; init; } = 1;

    public bool StopOnUnhandledException { get; init; }
}

public enum ZLinkTimerOverrunPolicy
{
    SkipLateTicks = 1,
    CatchUpBounded = 2,
    DelayNextTick = 3
}
```

정책 의미는 다음과 같다.

| 정책 | 의미 | 기본 사용처 |
|------|------|-------------|
| `SkipLateTicks` | 늦은 tick은 합쳐서 건너뛰고 최신 예정 시각 기준으로 이어 간다. `SkippedTicks`에 건너뛴 수를 기록한다. | room server tick, realtime simulation |
| `CatchUpBounded` | 밀린 tick을 최대 `MaxCatchUpTicks`개의 callback으로 연속 실행한다. 그 이상은 건너뛴다. | deterministic[^deterministic] 진행을 일부 유지해야 하는 서버 simulation |
| `DelayNextTick` | handler가 끝난 뒤 다시 period를 기다린다. 고정 주기보다 간단한 반복 작업에 맞다. | cleanup, timeout sweep, heartbeat |

기본값은 `SkipLateTicks`로 둔다. 짧은 주기 서버 tick에서 무제한 catch-up은 서버를
더 밀리게 만들 수 있기 때문이다.

정책별 시간표 의미는 다르다.

- `SkipLateTicks`와 `CatchUpBounded`는 fixed-rate 기준 시각을 유지한다. 즉 다음
  예정 시각은 timer 시작 시각과 `Period`의 누적으로 계산한다.
- `SkipLateTicks`의 skip 계산은 handler 시작 직전의 monotonic 시각을 기준으로 한다.
  handler 완료 시각을 기준으로 이미 실행 중인 callback의 `SkippedTicks`를 나중에
  바꾸지 않는다.
- `CatchUpBounded`의 `MaxCatchUpTicks`는 한 번 늦어진 구간에서 application handler에
  연속 전달할 callback 수의 상한이다. 이 수는 현재 실행할 callback을 포함한다.
  예정된 tick 수가 이 상한을 넘으면 framework는 오래된 초과 tick을 전달하지 않고,
  첫 번째로 전달하는 callback의 `SkippedTicks`에 그 수를 기록한다.
- `DelayNextTick`은 fixed-rate가 아니라 fixed-delay 정책이다. handler가 끝난 뒤
  다시 `Period`를 기다린다. 이 정책에서는 `ScheduledIndex`가 실제 callback 순서와
  같은 의미가 되며, `SkippedTicks`는 항상 `0`이어야 한다.
- `MaxCatchUpTicks`는 `CatchUpBounded`에서 `0`보다 커야 한다.
  `CatchUpBounded`가 아닌 정책에서는 framework가 이 값을 무시한다.
- 알 수 없는 `ZLinkTimerOverrunPolicy` 값은 설정 오류로 실패한다.

### 3.5 callback 예외 정책을 공개 계약으로 둔다

현재 구현은 tick handler 예외를 잡고 timer loop를 계속 돌린다. 이 동작은
cleanup timer에는 편하지만, 서버 상태를 진행하는 room tick에서는 오류를 숨길 수 있다.

새 계약에서는 기본값을 다음처럼 둔다.

- 기본값은 예외를 runtime monitoring[^monitoring]에 `TimerHandlerFailed`
  event로 기록하고 timer를 계속 실행한다.
- `StopOnUnhandledException = true`이면 첫 번째 처리되지 않은 예외에서 timer를
  중단한다. 이때 `TimerStoppedAfterUnhandledException` event를 기록한다.
- cancellation으로 발생한 `OperationCanceledException`은 오류로 기록하지 않는다.

이 정책은 timer를 멈출지 계속 돌릴지를 application이 선택하게 한다.

runtime monitoring에는 아래 값을 추가한다.

```csharp
public enum ZLinkSpotEventKind
{
    StatusChanged = 0,
    PeersChanged = 1,
    SubjectsChanged = 2,
    TimerHandlerFailed = 3,
    TimerStoppedAfterUnhandledException = 4
}

public readonly record struct ZLinkSpotTimerDiagnostic(
    RoutingId SpotRid,
    string SpotName,
    bool IsEntrySpot,
    string TimerName,
    string HandlerType,
    ulong DeliveryIndex,
    ulong ScheduledIndex,
    string ExceptionType,
    string ExceptionMessage);
```

`ZLinkSpotEvent`에는 `ZLinkSpotTimerDiagnostic? TimerDiagnostic` field를 추가한다.
`TimerHandlerFailed`와 `TimerStoppedAfterUnhandledException`은 snapshot interval을
기다리는 상태 diff event가 아니라, 실패가 발생한 시점에 즉시 발행되는 point-in-time
event다. 기존 `AddSpotEvents(sourceName, interval)`의 `interval`은 상태, peer,
subject snapshot diff에만 적용하고 timer failure event를 지연시키지 않는다.

exception 객체 자체는 public event payload에 넣지 않는다. monitoring event는 운영
관찰 표면이므로 직렬화 가능한 요약 정보를 남긴다.

### 3.6 실행 문맥은 Spot 종류에 맞춘다

timer 등록 표면은 Entry Spot과 user Spot이 같아야 한다. 차이는 실행 직렬화 정책뿐이다.

- user Spot timer callback은 user Spot packet, subscription, actor join, channel
  reply와 같은 Spot 실행 queue에서 직렬로 실행한다.
- Entry Spot timer callback은 Entry Spot 전체 직렬화 queue에 묶지 않는다.
  Entry Spot의 packet handler 정책과 동일하게, framework가 별도 task에서 callback을
  실행한다.
- 단일 timer instance 안에서는 이전 callback이 끝나기 전에 같은 timer의 다음
  callback을 겹쳐 실행하지 않는다. 즉 Entry Spot timer가 전역 queue에 묶이지
  않는다는 말은 여러 Entry Spot callback과 병렬로 실행될 수 있다는 뜻이지, 같은
  timer callback을 reentrant[^reentrant]하게 호출한다는 뜻은 아니다.
- 여러 Entry Spot timer 또는 Entry Spot packet handler는 서로 동시에 실행될 수
  있다. Entry Spot은 공용 입구이므로 timer에서 공유 mutable state를 직접 소유하지
  않아야 한다. room, stage, match 상태처럼 직렬 보호가 필요한 상태는 user Spot에서
  다룬다.
- 두 경우 모두 현재 Spot context는 handler 실행 중 조회 가능해야 한다.

이 기준은 "Entry Spot과 Spot의 기능 표면은 같고, user Spot만 모든 요청을
직렬화한다"는 SPOT 모델과 맞춘 것이다.

## 4. 서버 timer 기준

이 timer는 hard realtime[^hard-realtime] 보장을 제공하지 않는다. `.NET` runtime,
GC[^gc], OS scheduler의 영향을 받는다. 따라서 문서에는 "정확히 16.67ms마다 반드시
실행된다" 같은 보장을 쓰면 안 된다.

또한 이 timer는 client frame timer가 아니다. client는 렌더링 frame, 입력 샘플링,
보간을 자체 loop에서 처리할 수 있다. framework SPOT timer는 서버가 room 상태와 match
상태를 진행하거나 정리하기 위한 timer다.

대신 framework가 제공해야 하는 기준은 다음과 같다.

- `SkipLateTicks`와 `CatchUpBounded` timer는 fixed-rate 기준 시각을 유지한다.
- `DelayNextTick` timer는 handler 완료 뒤 period를 다시 기다리는 fixed-delay
  기준을 따른다.
- 지연은 `ZLinkTimerTick.Delay`로 드러낸다.
- 늦은 tick을 어떻게 처리했는지는 `ZLinkTimerOptions.OverrunPolicy`로 정한다.
- 무제한 catch-up은 제공하지 않는다.
- room, stage, match 같은 권위 상태 변경은 user Spot 실행 queue 안에서만 일어난다.
- application은 tick metadata를 보고 서버 simulation 진행량을 직접 결정할 수 있다.

예를 들어 60Hz room server tick은 다음처럼 등록한다. 60Hz는 서버 simulation 주기를
뜻한다. client rendering frame rate를 뜻하지 않는다.

```csharp
_gameLoop = await Context.AddTimer<GameLoopTimerHandler>(
    "game-loop",
    TimeSpan.FromMilliseconds(1000.0 / 60.0),
    new ZLinkTimerOptions
    {
        OverrunPolicy = ZLinkTimerOverrunPolicy.SkipLateTicks
    },
    cancellationToken);
```

handler는 tick 정보를 보고 상태를 진행한다.

```csharp
public sealed class GameLoopTimerHandler
    : IZLinkSpotTimerHandler<GameRoomSpot>
{
    public ValueTask HandleAsync(
        GameRoomSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        var elapsedTicks = tick.SkippedTicks + 1;
        spot.AdvanceServerSimulation(tick.Period, elapsedTicks);
        return ValueTask.CompletedTask;
    }
}
```

`elapsedTicks`는 생략된 tick과 현재 callback이 대표하는 tick을 합친 값이다.
application이 실제 경과 시간을 직접 계산하고 싶으면 `Delay`나 `StartedElapsed`를
사용할 수 있다.

## 5. 구현 반영 항목

구현은 기존 timer 표면을 바꾸는 breaking change로 반영했다. compatibility overload는
두지 않았다.

1. `ZLinkTimerOptions`, `ZLinkTimerOverrunPolicy`, `ZLinkTimerTick`을 public
   contract에 추가했다.
2. `IZLinkSpotContext.AddTimer<THandler>(...)`와
   `IZLinkEntrySpotContext.AddTimer<THandler>(...)`에 `ZLinkTimerOptions?` 인자를
   추가했다.
3. `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)`에 `ZLinkTimerTick` 인자를
   추가했다.
4. `ZLinkTimer`를 단순 `PeriodicTimer` wrapper에서 policy-aware managed scheduler로
   바꿨다. `SkipLateTicks`와 `CatchUpBounded`는 fixed-rate loop를 쓰고,
   `DelayNextTick`은 fixed-delay loop로 분기한다.
5. user Spot timer는 기존처럼 Spot 실행 queue에 enqueue한다.
6. Entry Spot timer는 Entry Spot 전체 직렬화 queue에 묶지 않고 실행한다.
7. timer 예외는 runtime monitoring의 timer event로 기록하고, option에 따라 계속
   실행하거나 중단한다.
8. 기존 샘플과 테스트의 timer handler signature를 새 계약에 맞췄다.

## 6. 회귀 테스트 계획

아래 테스트로 timer 정책과 실행 문맥을 고정했다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `TimerTests.SpotTimer_Provides_Tick_Metadata` | timer handler가 `Name`, `DeliveryIndex`, `ScheduledIndex`, `Period`, `ScheduledAt`, `StartedAt`, `ScheduledElapsed`, `StartedElapsed`, `Delay`를 받는다. |
| `TimerTests.SpotTimer_Skips_Late_Ticks_When_Configured` | `SkipLateTicks` 정책에서 늦은 tick이 handler에 무제한으로 전달되지 않고 `SkippedTicks`로 드러난다. |
| `TimerTests.SpotTimer_Catches_Up_Within_Configured_Limit` | `CatchUpBounded` 정책이 `MaxCatchUpTicks`를 넘지 않는다. |
| `TimerTests.SpotTimer_DelayNextTick_Waits_After_Handler_Completion` | `DelayNextTick` 정책은 handler 완료 뒤 period를 다시 기다린다. |
| `TimerTests.SpotTimer_NonCatchUpPolicy_Ignores_MaxCatchUpTicks` | `CatchUpBounded`가 아닌 정책에서는 `MaxCatchUpTicks`가 scheduling 의미를 바꾸지 않는다. |
| `TimerTests.SpotTimer_CatchUpPolicy_Rejects_Invalid_MaxCatchUpTicks` | `CatchUpBounded` 정책에서 `MaxCatchUpTicks <= 0`은 설정 오류다. |
| `TimerTests.SpotTimer_Rejects_Unknown_OverrunPolicy` | 알 수 없는 overrun 정책 값은 설정 오류다. |
| `TimerTests.SpotTimer_Reports_Handler_Exception_To_Monitoring` | handler 예외가 `TimerHandlerFailed` event와 timer diagnostic payload로 기록된다. |
| `TimerTests.SpotTimer_StopOnUnhandledException_Stops_Timer` | option이 켜져 있으면 handler 예외 뒤 같은 timer가 다시 실행되지 않고 `TimerStoppedAfterUnhandledException` event가 기록된다. |
| `TimerTests.EntrySpotTimer_Does_Not_Block_EntrySpot_Callbacks_Globally` | 긴 Entry Spot timer callback이 다른 Entry Spot packet 또는 actor packet callback을 전역으로 막지 않는다. |
| `TimerTests.EntrySpotTimer_Does_Not_Reenter_Same_Timer` | Entry Spot timer가 전역 queue에는 묶이지 않더라도 같은 timer callback을 겹쳐 실행하지 않는다. |
| `TimerTests.SpotTimer_CancelAsync_Stops_Managed_Timer_Loop` | `CancelAsync()` 이후 handler가 더 이상 호출되지 않고 timer loop가 정리된다. |

## 7. 문서 반영 결과

이 draft 내용은 아래 정식 문서에 나누어 반영했다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/spec/framework-api.ko.md` | 공통 framework timer 의미, managed scheduler 기준, server-side timer 기준, hard realtime 비보장, overrun 정책을 공통 계약으로 반영했다. |
| `framework/doc/spec/interaction-model.ko.md` | user Spot timer가 Spot 실행 queue에 직렬화되고 Entry Spot timer는 전체 직렬화에 묶이지 않는 실행 규칙을 반영했다. |
| `framework/doc/spec/actor-model.ko.md` | actor가 join된 user Spot의 timer callback이 같은 Spot 상태 문맥에서 실행된다는 설명을 현재 실행 모델과 맞췄다. |
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | `ZLinkTimerOptions`, `ZLinkTimerOverrunPolicy`, `ZLinkTimerTick`, timer handler signature를 정식 `.NET` 계약으로 반영했다. |
| `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md` | `Context.AddTimer<THandler>(...)`의 새 인자, callback 실행 문맥, 예외 정책, server tick 사용 기준을 반영했다. |
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | 이미 언급된 `ZLinkTimerTick` pseudo code를 실제 timer metadata 계약과 맞추고, Entry Spot timer가 전역 직렬화에 묶이지 않는 규칙을 반영했다. |
| `framework/languages/dotnet/doc/spec/aspnet-core-monitoring.ko.md` | timer handler failure event와 `ZLinkSpotTimerDiagnostic` payload, timer failure event가 interval을 기다리지 않는다는 규칙을 monitoring 계약으로 반영했다. |
| `framework/languages/dotnet/doc/spec/stage-wrapper-on-spot.ko.md` | stage/room timer 설명을 새 tick metadata와 overrun 정책 기준으로 갱신했다. |
| `framework/languages/dotnet/doc/guide/samples/spot-samples.ko.md` | 샘플 timer handler signature와 room server tick 예제를 갱신했다. |
| `framework/languages/dotnet/doc/internals/regression-test-matrix.ko.md` | 위 회귀 테스트 계획을 실제 테스트 이름과 함께 반영했다. |

정식 spec에는 이 draft의 설명을 그대로 복사하지 않고, 공통 문서는 언어 중립 의미만
남기며, `.NET` 문서는 public signature와 ASP.NET Core 사용 예시만 가져갔다.

## 8. 구현 주의 항목

구현에서 유지해야 하는 항목은 다음과 같다.

- `TimeSpan.FromMilliseconds(1000.0 / 60.0)` 같은 소수 period의 누적 오차를 줄이기
  위해 내부 scheduler는 deadline을 누적 계산해야 한다.
- `SkipLateTicks`와 `CatchUpBounded`의 deadline 누적 계산은 monotonic clock 기준으로
  해야 한다. wall-clock `DateTimeOffset` 값은 event payload와 로그용으로만 만든다.

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^spot]: `SPOT` 은 room, stage, match 같은 실행 문맥을 나타내는 framework 추상이다.
[^client]: client 는 게임 클라이언트나 사용자 단말에서 실행되는 application을 뜻한다.
[^authoritative-state]: authoritative state 는 서버가 최종 기준으로 소유하고 판단하는 게임 또는 room 상태를 뜻한다.
[^di-scope]: DI scope 는 dependency injection container가 객체 생명 주기를 묶는 범위를 뜻한다.
[^deterministic]: deterministic 은 같은 입력과 같은 tick 순서에서 같은 결과가 나오도록 만드는 성질을 뜻한다.
[^monitoring]: runtime monitoring 은 framework 내부 동작과 오류를 application 또는 운영 도구가 관찰할 수 있게 하는 이벤트 표면이다.
[^hard-realtime]: hard realtime 은 정해진 시간 안에 반드시 실행되어야 한다는 시스템 수준 보장을 뜻한다.
[^gc]: GC 는 garbage collection 을 뜻하며, managed runtime 이 사용하지 않는 객체 메모리를 정리하는 과정이다.
[^reentrant]: reentrant 는 같은 callback 이 끝나기 전에 같은 callback 이 다시 들어와 동시에 실행되는 상황을 뜻한다.
