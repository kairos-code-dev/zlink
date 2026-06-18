<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core SPOT Integration](aspnet-core-spot.ko.md) | [다음: ZLink Framework ASP.NET Core STREAM Integration](aspnet-core-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[.NET 묶음](../README.ko.md) | [SPOT](aspnet-core-spot.ko.md) | [SPOT 샘플](../guide/samples/spot-samples.ko.md) | [인터페이스](handler-interfaces.ko.md)

# Stage Wrapper On SPOT

## 1. 왜 이 문서가 필요한가

이 절은 `SPOT` 초안만으로는 `Stage` 모델을 충분히 설명하지 못하는 이유를 짚는다.

현재 `SPOT` 초안은 다음 기능을 중심으로 잡혀 있다.

- `SpotNode`[^spot-node] 등록
- `spotRid` 생성과 삭제
- current channel publish/subscribe
- attach된 channel client 기반 send/request
- topic publish/subscribe

이 정도면 `SPOT` 자체를 설명하기에는 충분한 출발점이다. 하지만 `playhouse` 의
`Stage` 처럼 한층 더 높은 수준의 모델을 그 위에 올리려면 이것만으로는 부족하다.

`Stage` 는 단순히 "메시지를 받을 수 있는 spot" 이 아니다. 보통 다음과 같은
성격을 함께 가진다.

- 단일 실행 문맥
- stage 생성 시점의 초기 payload 처리
- actor 또는 session membership[^membership]
- tick이나 timer 기반의 후속 작업
- stage id로 lookup이 가능한 운영 모델

요컨대 `Stage wrapper` 는 `SPOT` 을 그대로 노출하는 작업이 아니다. `SPOT` 위에
한 단계 높은 실행 모델을 세우는 작업이다.

## 2. 지금 초안으로 충분한 부분

이 절은 `SPOT` 초안 중 `Stage wrapper` 가 그대로 깔고 갈 수 있는 부분을
정리한다.

현재 스펙 중 `Stage wrapper` 의 바닥으로 그대로 활용할 수 있는 부분은 다음과
같다.

- `Spot`은 특정 service가 아니라 `SpotNode`에 종속된다는 점
- attach된 SPOT `Discovery`가 `SpotNode`의 active channel view를 정한다는 점
- 다른 channel 호출을 attach된 channel client 경로로 풀어준다는 점
- `spotRid`와 topic publish를 구분해서 설명하는 점
- `IZLinkSpotManager`로 spot 생성 lifecycle을 따로 분리해 둔 점

이 방향은 `playhouse` 의 `Stage` 를 `SPOT` 으로 통째로 대체하는 것이 아니다.
`SPOT` 을 밑에 깔고 `Stage` 를 다시 그 위에 감싸는 방향과 잘 맞는다.

## 3. framework 범위와 wrapper 범위

이 절은 `zlink framework` 와 `Stage wrapper` 의 책임 경계를 정리한다.

기본 `zlink framework` 가 직접 맡는 범위는 보통 다음 정도다.

- `SpotNode` 등록과 lifecycle[^lifecycle]
- `spotRid` 생성과 삭제
- current channel publish/subscribe
- attach된 channel client 기반 send/request
- publish/subscribe
- timer 등록과 취소
- DI[^di], handler, filter, context
- discovery[^discovery]와 수동 연결
- 같은 `spot`에 대한 dispatch 직렬화 같은 실행 계약

반면 다음 항목은 `SPOT` 자체보다 한 단계 위의 상위 모델에 가깝다.

- membership
- actor 또는 session model
- room / stage / zone 별 broadcast 정책
- 입장, 퇴장, 인증, 권한
- `stageId -> address` lookup helper

현재 draft framework 는 `membership / actor model` 의 최소 공용 계약까지는
포함하고 있다. 즉 다음 작업은 framework 가 제공한다.

- actor join 등록
- actor stream 연결 / 해제 bridge
- actor dispatch
- 같은 `SPOT` 실행 문맥의 직렬화

그 위의 room / stage / zone 정책은 `Stage wrapper` 나 응용 계층이 맡는 구조로
읽는 편이 맞다.

## 4. 아직 부족한 부분

### 4.1 실행 문맥 계약

이 절은 같은 spot 안의 작업이 어떤 문맥에서 실행되는지를 정리한다.

가장 먼저 필요한 것은 같은 spot 안의 작업이 어떤 문맥에서 실행되는가에 대한
계약이다.

`Stage` wrapper 를 올리려면 최소한 다음 질문에 대한 답이 정리되어 있어야 한다.

- 같은 `spotRid`로 들어오는 handler가 직렬로 실행되는가?
- timer handler도 같은 실행 문맥으로 들어오는가?
- publish subscription callback도 같은 문맥으로 들어오는가?
- **channel reply callback도 같은 문맥으로 들어오는가?**
- session/actor ingress가 spot 상태를 만지기 전에 같은 문맥으로 넘어오는가?
- 응용 코드가 별도 lock 없이 stage state를 다뤄도 되는가?

이 계약이 없으면 `Stage` wrapper 는 단순한 DTO 라우팅 레이어에 머무른다. 즉
실제 room / stage 상태를 안전하게 다루는 상위 모델로는 설명하기 어려워진다.

이 부분은 이제 **core spec 이 명확하게 답하는 영역** 이다. core spec 은 다음을
보장한다.

- 같은 `Spot` 의 dispatch callback 은 직렬화된다.
- `SUBSCRIBE_READABLE`, `ROUTED_READABLE`, **`CHANNEL_REPLY_READABLE`**,
  `TIMER_READABLE` 이 모두 같은 dispatch event 축으로 올라온다.
- channel request reply 의 transport owner 는 attached `DEALER` 다. 하지만
  callback delivery owner 는 request 를 시작한 `Spot` 의 dispatch stream 이다.
- `RequestToChannelAsync(...)` 의 continuation 도 같은 spot execution context 에서
  실행된다. 즉 임의의 thread 에서 직접 promise 를 resolve 하지 않는다.

따라서 framework 문서에서 해야 할 일은 새로운 의미를 만드는 것이 아니다. 그
계약을 상위 표면으로 분명하게 옮겨 주는 일이다.

여기서 한 가지 짚어 둘 점이 있다. 사용자에게 내부 실행기를 직접 노출하지 않는다는
것이다. 즉 `Stage wrapper` 사용자는 mailbox[^mailbox], queue, dispatch drain
loop 를 직접 다루지 않고, 등록 표면만 본다.

- packet handler 등록
- subscription handler 등록
- timer handler 등록
- session/actor 입장
- spot state 접근 규칙

wrapper 문서가 말해야 하는 것은 "같은 `Spot` 상태는 같은 실행 계약으로
처리된다" 는 점이다. 사용자가 내부 runtime 구현을 직접 소유한다는 뜻은 아니다.

`Stage` wrapper 관점에서는 다음과 같이 정리할 수 있다.

- `StageSpot.UserCount` 같은 mutable state 는 같은 `Spot` 실행 계약 안에서만
  접근된다고 본다.
- 다음 경로에서 `StageSpot` state 를 읽고 쓸 때는 별도 lock 이 필요하지 않다.
  - routed packet handler
  - subscription handler
  - timer handler
  - channel reply continuation
  - user Spot actor handler
- stream session callback 은 `StageSpot` state 를 직접 만지지 않는다. session
  callback 은 actor dispatch 나 spot 호출을 제출하기만 한다. 실제 `StageSpot`
  state 변경은 user Spot 실행 문맥 안에서 일어난다.
- 단, `Stage` wrapper 바깥에서 `SpotRid` 를 받아 state 를 직접 건드리려 하는
  경우는 다르다. 그 접근은 같은 실행 계약 바깥이므로 별도 동기화가 필요하다.

#### 4.1.1 actor join 이후의 내부 처리 모델

이 절은 actor 가 join 된 뒤에 packet 이 어떤 흐름으로 직렬화되는지를 정리한다.

위 실행 계약만 봐서는 "왜 actor dispatch 안에서 `Spot` 객체를 lock 없이 만져도
되는가" 가 곧바로 드러나지 않을 수 있다. `Stage wrapper` 나 room wrapper 를
실제로 만들려면, 내부에서 어떤 흐름으로 직렬화가 이뤄지는지 한 번은 짚어 둘
필요가 있다.

핵심은 다음 두 가지다.

- actor 는 반드시 특정 `Spot` 에 join 된 뒤에만 room packet 을 처리한다.
- join 된 actor 로 들어가는 모든 packet 은 **같은 `Spot` 실행 문맥** 으로 다시
  모아서 처리한다.

actor 가 `Spot` 에 붙었다는 것은 단순히 membership table 에 등록되었다는
뜻만이 아니다. 그 actor 에 대한 후속 packet, disconnect, timer 후속 작업까지
모두 그 `Spot` 이 소유한 실행 문맥으로 다시 들어온다는 의미를 포함한다.

내부 처리 흐름은 보통 다음 순서로 읽는 편이 가장 자연스럽다.

1. client session이 인증을 끝내고 actor를 찾는다.
2. `JoinRoom` 같은 packet이 오면 framework가 target `Spot` 실행 문맥으로 join
   요청을 넣는다.
3. join handler가 정상 응답을 반환하면 framework가 `actorId -> spot runtime`
   연결을 membership으로 기록한다.
4. 그 뒤 session에서 packet이 오면 framework가 header와 payload를 actor dispatch
   envelope로 정규화한다.
5. 정규화된 dispatch를 해당 actor가 attach된 `Spot` runtime의 inbox에 넣는다.
6. 그 `Spot` inbox를 소비하는 실행기는 하나뿐이라고 가정한다.
7. 그 실행기 안에서만 `IZLinkSpotContext.AddHandler(...)`로 등록한 actor handler가
   호출된다.
8. actor가 room 상태를 바꾸거나 `Spot` 메서드를 호출해도, 이미 같은 `Spot`의 실행
   문맥 안이므로 추가적인 lock이 필요 없다.

구현 관점에서는 다음 흐름으로 보면 된다.

```text
joined actor session packet
    -> normalize to session packet
    -> submit to spot-owned inbox
    -> single spot consumer
    -> actor packet handler
    -> actor accesses Spot state
```

이 모델에서 핵심은 다음과 같다. actor packet 처리를 session callback thread 에서
곧장 실행하지 않는다. session callback 은 actor 가 어느 `Spot` 에 붙어 있는지
확인하고, packet 을 그 `Spot` 이 소유한 실행 문맥으로 넘기는 역할까지만
책임진다. 실제 actor packet 처리는 반드시 `Spot` 실행 문맥 안에서만 일어난다.

이 규칙을 지키면 다음 입력원이 모두 한 줄로 정렬된다.

- routed packet handler
- subscribe handler
- timer handler
- channel reply continuation
- attach가 끝난 actor session packet
- actor session disconnect의 후속 처리

그래서 actor 안에서 `Spot` 상태를 읽고 쓸 때, "지금 이 시점에 다른 packet
handler 가 같은 `Spot` state 를 동시에 만지고 있는가" 를 매번 고민할 필요가
없다.

내부 구현은 mailbox, queue, executor, fiber 등 여러 방식으로 만들 수 있다.
하지만 wrapper 문서에서 못 박아야 하는 최소 의미는 다음 정도다.

- join 된 actor 의 packet 은 `Spot` 실행 문맥 바깥에서 직접 처리하지 않는다.
- framework 내부의 `Async(...)` 는 `Spot` 이 소유한 직렬 실행 규칙 안에서
  만 수행된다. 그 안에서 최종적으로 actor packet handler 가 호출된다.
- stream packet 은 framework 가 header와 payload를 보존한 actor dispatch로
  정규화한 뒤, 같은 actor dispatch 경로를 그대로 탄다.

"`Spot` 에 actor 가 join 된다" 는 말은 membership 만 가리키는 것이 아니다.
**그 actor 의 packet 처리 ownership 이 해당 `Spot` 으로 넘어간다** 는 뜻으로
읽어야 한다.

### 4.2 timer 등록

이 절은 `Stage` 모델에서 timer 를 어떻게 등록하고 실행하는지를 정리한다.

`Stage` 성격의 모델에서는 timer 가 사실상 필수에 가깝다.

예를 들면 다음 같은 작업은 대부분 timer 없이는 설명하기 어렵다.

- 매칭이 끝나고 5초 뒤 시작
- 일정 시간 동안 입력이 없으면 room 종료
- 100ms마다 state flush
- 1초마다 heartbeat[^heartbeat] publish

따라서 문서 초안도 timer를 두 갈래로 나누기보다는, spot lifecycle 안에서 등록하는
하나의 모델로 정리하는 편이 맞다.

문서 초안 수준에서 필요한 최소 표면은 다음 정도다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }
    ValueTask CancelAsync();
}

public interface IZLinkSpot
{
    IZLinkSpotContext Context { get; }
}

public interface IZLinkSpotContext
{
    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        ZLinkTimerOptions? options = null,
        CancellationToken cancellationToken = default);
}
```

하부 `.NET` 바인딩 자체는 이미 다음과 같은 timer 표면을 제공한다.

```csharp
public sealed class Timer : IZlinkTimer
{
    public static Timer FromSpot(Spot spot);
    public void Start(TimeSpan interval, ulong repeatCount);
    public void Stop();
    public ulong? Recv(RecvFlags flags = RecvFlags.None);
    public void OnFire(Action<IZlinkTimer, ulong> handler);
    public void Close();
}
```

framework 의 `Context.AddTimer<THandler>(...)` 는 native timer handle 을
그대로 드러내는 표면이 아니다. framework runtime 이 만든 managed `.NET` timer
를 spot lifecycle / DI 모델에 붙여 주는 wrapper 로 읽는 편이 맞다.

timer tick 이 발생하면 framework 가 그 tick 을 user Spot 의 같은 spot execution
context 안으로 enqueue 한다. Entry Spot timer 는 Entry Spot 전체 queue 에 묶지
않는다. 그 뒤 `IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)` 를 호출한다.
`IZLinkTimer.CancelAsync()` 는 이 managed timer loop 를 멈추고 정리하는 고수준
timer handle 이다.

같은 맥락으로 actor join 도 framework core 의 기본 표면보다는, stage wrapper 같은
상위 확장 계층에서 따로 정의하는 편이 더 자연스럽다. 그래야 wrapper 가 다음을
자기 도메인 규칙으로 닫아 둘 수 있기 때문이다.

- actor 를 어떤 `Spot` 에 귀속시킬지
- join 을 허용할지
- 결과 payload 에 무엇을 담을지

또한 이 표면에 `IServiceProvider` 를 매번 끼워 넣을 필요는 없다. 즉 굳이 public
계약으로 보일 필요가 없다는 뜻이다. `Spot`, timer handler, join handler 는
framework 가 만든 per-spot scope[^per-spot-scope] 에서 resolve 한다. 사용자에게는
"어떤 타입을 등록하는가" 만 보이도록 두는 편이 더 낫다.

여기서 더 중요한 것은 timer handler 가 어느 실행 문맥에서 도는가다.
`Context.AddTimer<THandler>(...)` 로 등록한 user Spot timer handler 는 같은 spot
실행 문맥에서 돈다. room, stage, match 상태처럼 권위 상태를 바꾸는 작업은 이
직렬 실행 문맥 안에서 처리해야 한다.

timer handler 는 `ZLinkTimerTick` 을 받아 callback 번호, fixed-rate 시간표의 tick
번호, 예정 시각, 시작 시각, 지연, 건너뛴 tick 수를 볼 수 있다. `ZLinkTimerOptions`
의 overrun 정책으로 늦은 tick 을 건너뛸지, 제한된 수만 catch-up 할지, handler 완료
뒤 period 를 다시 기다릴지 정한다.

따라서 wrapper 사용자는 low-level timer handle 을 직접 알 필요가 없다.
`Context.AddTimer<THandler>(...)` 만 보고, framework 가 같은 `Spot` 실행 계약
안에서 timer handler 를 호출한다고 이해하는 편이 맞다.

### 4.3 spot 생성 시 초기값 전달

이 절은 `Stage` 생성 시점에 초기 payload 를 함께 받는 표면이 왜 따로 필요한지를
정리한다.

현재 `IZLinkSpotManager` 는 `spotRid` 의 생성과 삭제를 설명하기에는 충분하다.
하지만 `Stage` 생성처럼 초기 payload 를 함께 받는 모델을 설명하기에는 부족하다.

예를 들면 `playhouse` 의 stage 생성은 보통 다음 정보를 함께 들고 들어간다.

- 어느 play node에 만들지
- stage type
- stage id
- create packet 또는 metadata

현재 `IZLinkSpotManager` 의 기본 정의는
[handler-interfaces.ko.md](handler-interfaces.ko.md) 의 section 6.3 을 따른다.
현재 framework 기본 계약은 `TSpot` 타입으로 factory를 고르는 생성과,
`TSpot + spotRid`로 기존 logical spot을 확보하는 수준이다.

`Stage wrapper` 를 만들려면 최소한 다음 중 하나가 더 필요하다.

- `TSpot + spotRid + metadata`
- create payload 를 handler 초기화 단계에 전달하는 별도의 contract

다만 이 부분은 현재 하부 C API 의 공개 계약에서 바로 읽히는 내용이 아니다. 따라서
다음 형태는 "framework 기본 계약" 보다는 "wrapper 전용 확장 후보" 로 보는 편이
맞다.

```csharp
public interface IStageSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot, TMetadata>(
        RoutingId spotRid,
        TMetadata metadata,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;
}
```

### 4.4 actor 또는 session membership

이 절은 stage 입장 / 퇴장 / broadcast 정책이 어디에 들어가야 하는지를 정리한다.

`SPOT` 자체는 주소를 가질 수 있는 논리 인스턴스까지만 설명하면 된다. 하지만
`Stage wrapper` 는 보통 membership 을 함께 가진다.

예를 들면 다음과 같다.

- 누가 이 stage 에 입장했는가
- 누가 나갔는가
- 현재 연결된 actor 집합이 무엇인가
- 특정 actor 에게만 보낼지
- stage 전체에 broadcast 할지

이것은 `SPOT` 의 필수 계약일 필요는 없다. 즉 기본 `zlink framework` 범위를 넘는
영역이다. 다만 `Stage wrapper` 초안에서는 별도의 축으로 명시되어 있어야 한다.

현재 `SPOT` 문서만으로는 다음을 충분히 설명하지 못한다.

- client connection과 spot의 연결
- actor 인증과 입장
- membership snapshot과 broadcast

### 4.5 stage directory 또는 lookup helper

이 절은 `stageId` 만으로 stage 위치를 찾아낼 helper 가 어디에 있어야 하는지를
정리한다.

`Stage wrapper` 를 실제로 사용하는 응용은 보통 `stageId` 만 들고 있는 경우가
많다.

상위 계층에는 다음 중 하나가 필요하다.

- `stageId -> stage 위치 정보` lookup
- `logical key -> channel / node / spot` 해석 helper
- wrapper 전용 registry[^registry] helper

이 기능을 `SPOT` 공용 API 에 곧바로 넣을 필요는 없다. 하지만 `Stage wrapper`
초안에서는 이 위치 해석을 누가 책임지는지 명확히 적어 두어야 한다.

## 5. 정리

이 절은 지금까지 정리한 책임 경계와 부족한 축을 짧게 요약한다.

지금의 `SPOT` 초안은 `Stage wrapper` 의 하부 transport 로 쓰기에 충분하다. 다만
`Stage` 처럼 그 위의 상위 모델을 곧바로 설명하기에는 아직 다음 네 축이 더
필요하다.

- 실행 문맥 계약
- timer 등록
- 초기 metadata를 받는 wrapper 확장 contract
- membership과 directory 같은 상위 모델 보조 축

요컨대 결론은 다음과 같다.

- 현재 `SPOT` 기능은 버려야 할 수준이 아니다.
- 오히려 방향은 맞다.
- 다만 `playhouse` 의 `Stage` 를 감싸려면 `SPOT` 문서와는 별개로 `Stage wrapper`
  계약을 한 단계 더 적어 두어야 한다.

## 6. 다음 단계 제안

이 절은 이 문서 다음에 이어질 작업 순서를 정리한다.

1. `IZLinkSpotManager` metadata 확장을 wrapper 후보로 따로 정리
2. `Stage wrapper` 전용 문서에서 membership, broadcast, directory를 별도 정의

## 7. 회귀 테스트

이 절은 Stage wrapper 가 어떤 테스트로 회귀를 막는지를 정리한다.

Stage wrapper 항목은 framework 가 stage 자체를 알지 못하더라도, 다음을 통해
상위 모델을 얹을 수 있는지를 확인한다.

- 현재 SPOT 의 실행 문맥
- timer
- spot manager
- actor channel 경로

wrapper 전용 API 가 생기면, 이 표에는 실제로 실행되는 테스트만 추가한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ActorLifecycleTests.SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext` | actor join 뒤 stage 역할의 spot 실행 문맥 안에서 packet이 처리된다. |
| `ManagerTests.Spot_Publish_Timer_And_Close_Stop_Callbacks_Work` | stage tick으로 쓰는 timer가 spot 종료 후 정확히 멈춘다. |
| `ManagerTests.SpotManager_Create_List_Close_And_Publish_Work_Through_FrameworkRuntime` | stage 생성·조회·종료에 필요한 spot manager와 scope 정리가 정상 동작한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 가리킨다.
[^stage]: `Stage`는 `playhouse`에서 게임 룸 같은 상위 실행 단위를 표현하는 모델이다. 단일 실행 문맥과 membership, lifecycle 같은 상위 개념을 함께 가진다.
[^spot]: `SPOT`은 동적으로 생성·소멸되는 논리적 노드 단위로 메시지를 라우팅하는 추상이다. 주소 가능한 인스턴스로서, 그 위에 room이나 stage 같은 상위 모델을 얹을 수 있다.
[^spot-node]: `SpotNode`는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^membership]: membership은 어떤 그룹(stage, room 등)에 누가 속해 있는지를 표현하는 정보다. 입장·퇴장 정책과 broadcast 대상 결정의 기반이 된다.
[^lifecycle]: lifecycle은 객체나 노드가 생성부터 소멸까지 거치는 단계들과 그 사이의 정해진 호출 규약을 가리킨다.
[^di]: DI(Dependency Injection)는 객체가 필요한 의존 컴포넌트를 직접 생성하지 않고 컨테이너로부터 주입받는 방식이다. `ASP.NET Core`에서는 `IServiceCollection` 기반으로 처리한다.
[^discovery]: discovery는 분산 환경에서 어떤 서비스가 어느 endpoint에 있는지를 자동으로 알아내는 메커니즘이다. ZLink에서는 registry가 그 역할을 한다.
[^mailbox]: mailbox는 액터 모델에서 메시지를 순서대로 쌓아 두는 큐를 가리킨다. actor는 자신의 mailbox에서 메시지를 하나씩 꺼내 처리한다.
[^heartbeat]: heartbeat는 연결이 살아 있는지를 확인하기 위해 일정 주기로 보내는 짧은 신호 메시지다.
[^per-spot-scope]: per-spot scope는 spot 하나가 살아 있는 동안에만 유지되는 DI scope다. spot이 사라지면 그 scope에서 만든 객체들도 함께 정리된다.
[^registry]: registry는 분산 노드의 위치, 상태, topology 정보를 모아 두는 서비스다. discovery의 데이터 출처 역할을 한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework ASP.NET Core SPOT Integration](aspnet-core-spot.ko.md) | [다음: ZLink Framework ASP.NET Core STREAM Integration](aspnet-core-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
