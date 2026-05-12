[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [인터페이스](./handler-interfaces.ko.md)

# Draft -- Stage Wrapper On SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `playhouse`의 `Stage` 같은 상위 모델을 `SPOT` 위에
> 한 번 더 감싸서 올릴 때 필요한 조건을 정리한다.

## 1. 왜 이 문서가 필요한가

현재 `SPOT` 초안은 아래 기능을 중심으로 잡혀 있다.

- `SpotNode` 등록
- `spotRid` 생성과 삭제
- current channel publish/subscribe
- attach된 channel client 기반 send/request
- topic publish/subscribe

이 정도면 `SPOT` 자체를 설명하기에는 충분한 출발점이다.
하지만 `playhouse`의 `Stage`처럼 더 높은 수준의 모델을 올리려면 이것만으로는
부족하다.

`Stage`는 단순히 "메시지를 받을 수 있는 spot"이 아니라, 보통 아래 성격을 함께
가진다.

- 단일 실행 문맥
- stage 생성 시 초기 payload 처리
- actor 또는 session membership
- tick 또는 timer 기반 후속 작업
- stage id로 lookup 가능한 운영 모델

즉 `Stage wrapper`는 `SPOT`을 그대로 노출하는 일이 아니라, `SPOT` 위에 한 단계 더
높은 실행 모델을 세우는 일이다.

## 2. 지금 초안으로 충분한 부분

현재 초안 중 `Stage wrapper`의 바닥으로 그대로 써도 되는 부분은 아래다.

- `Spot`은 특정 service가 아니라 `SpotNode`에 종속된다는 점
- attach된 SPOT `Discovery`가 `SpotNode`의 active channel view를 정한다는 점
- 다른 channel 호출을 attach된 channel client 경로로 푼다는 점
- `spotRid`와 topic publish를 구분해서 설명하는 점
- `IZLinkSpotManager`로 spot 생성 lifecycle을 분리한 점

이 방향은 `playhouse`의 `Stage`를 `SPOT`으로 완전히 대체하는 것이 아니라,
`SPOT`을 밑에 두고 `Stage`를 다시 감싸는 방향과 잘 맞는다.

## 3. framework 범위와 wrapper 범위

이 문서에서 가장 먼저 분명히 해야 할 점은 `zlink framework`와 `Stage wrapper`의
책임 경계다.

기본 `zlink framework`가 직접 맡는 범위는 보통 아래 정도다.

- `SpotNode` 등록과 lifecycle
- `spotRid` 생성과 삭제
- current channel publish/subscribe
- attach된 channel client 기반 send/request
- publish/subscribe
- timer 등록과 취소
- DI, handler, filter, context
- discovery 와 수동 연결
- 같은 `spot`에 대한 dispatch 직렬화 같은 실행 계약

반면 아래는 `SPOT` 자체보다 한 단계 높은 상위 모델에 가깝다.

- membership
- actor 또는 session model
- room/stage/zone별 broadcast 정책
- 입장, 퇴장, 인증, 권한
- `stageId -> address` lookup helper

현재 draft framework는 `membership / actor model`의 최소 공용 계약까지는 포함한다.
즉 actor join 등록, actor stream 연결/해제 bridge, actor dispatch, 같은 `SPOT` 실행 문맥
직렬화는 framework가 제공하고, 그 위의 room/stage/zone 정책은 `Stage wrapper`나
응용 계층이 맡는 구조로 읽는 편이 맞다.

## 4. 아직 부족한 부분

### 4.1 실행 문맥 계약

가장 먼저 필요한 것은 같은 spot 안의 작업이 어떤 문맥에서 실행되는지에 대한
계약이다.

`Stage` wrapper를 올리려면 최소한 아래가 정리되어야 한다.

- 같은 `spotRid`로 들어오는 handler가 직렬 실행되는가
- timer handler도 같은 실행 문맥으로 들어오는가
- publish subscription callback도 같은 문맥으로 들어오는가
- **channel reply callback도 같은 문맥으로 들어오는가**
- stream session callback도 같은 문맥으로 들어오는가
- 응용이 별도 lock 없이 stage state를 다뤄도 되는가

이 계약이 없으면 `Stage` wrapper는 단순 DTO 라우팅 레이어만 되고, 실제 room/stage
상태를 안전하게 다루는 상위 모델로 설명하기 어렵다.

이 부분은 이제 **core spec이 명확하게 답하는 영역**이다. core spec은 아래를
보장한다.

- 같은 `Spot`의 dispatch callback은 직렬화된다.
- `SUBSCRIBE_READABLE`, `ROUTED_READABLE`, **`CHANNEL_REPLY_READABLE`**,
  `TIMER_READABLE` 이 모두 같은 dispatch event 축으로 올라온다.
- channel request reply 의 transport owner 는 attached `DEALER` 이지만,
  callback delivery owner 는 request 를 시작한 `Spot` dispatch stream 이다.
- `RequestChannelAsync(...)` 의 continuation 도 같은 spot execution context 에서
  실행된다 (arbitrary thread 에서 직접 promise resolve 하지 않는다).

따라서 framework 문서에서 해야 할 일은 새 의미를 만드는 것이 아니라, 그 계약을
상위 표면으로 분명하게 옮기는 일이다.

여기서 중요한 점은 사용자에게 내부 실행기를 직접 노출하지 않는다는 것이다.
`Stage wrapper` 사용자는 mailbox, queue, dispatch drain loop를 직접 다루지 않고,
등록 표면만 본다.

- packet handler 등록
- subscription handler 등록
- timer handler 등록
- session/actor 입장
- spot state 접근 규칙

즉 wrapper 문서가 말해야 하는 것은 "같은 `Spot` 상태는 같은 실행 계약으로 처리된다"는
점이지, 사용자가 내부 runtime 구현을 직접 소유한다는 뜻이 아니다.

`Stage` wrapper 관점에서는 아래처럼 정리할 수 있다.

- `StageSpot.UserCount` 같은 mutable state 는 같은 `Spot` 실행 계약 안에서만
  접근된다고 본다.
- routed packet handler, subscription handler, timer handler, channel reply
  continuation, stream session callback에서 `StageSpot` state 를 읽고 쓸 때 별도
  lock 이 필요 없다.
- 단, `Stage` wrapper 외부에서 `SpotRid` 를 받아 직접 state 를 건드리려 하면
  그 접근은 같은 실행 계약 바깥이므로 별도 동기화가 필요하다.

#### 4.1.1 actor join 이후 내부 처리 모델

위 실행 계약만으로는 "왜 actor dispatch 안에서 `Spot` 객체를 lock 없이 만져도
되는가"가 바로 보이지 않을 수 있다. `Stage wrapper`나 room wrapper를 실제로
만들려면 내부에서 어떤 흐름으로 직렬화하는지 한 번은 설명해 둘 필요가 있다.

핵심은 아래 두 가지다.

- actor는 반드시 특정 `Spot`에 join된 뒤에만 room packet을 처리한다.
- join된 actor로 들어가는 모든 packet은 **같은 `Spot` 실행 문맥**으로 다시
  모아서 처리한다.

즉 actor가 `Spot`에 붙었다는 것은 단순히 membership table에 들어갔다는 뜻만이
아니다. 그 actor에 대한 후속 packet, disconnect, timer 후속 작업도 모두 그
`Spot`이 소유하는 실행 문맥으로 다시 들어온다는 뜻까지 포함한다.

내부 처리는 보통 아래 순서로 읽는 것이 가장 자연스럽다.

1. client session이 인증을 끝내고 actor를 찾는다.
2. `JoinRoom` 같은 packet이 오면 framework가 target `Spot` 실행 문맥으로
   join 요청을 넣는다.
3. join handler는 `IZLinkSpotActorMembership.JoinActorAsync(actor)`를 호출해
   `actorId -> spot runtime` 연결을 membership으로 기록한다.
4. 그 뒤 session에서 packet이 오면 framework Header 기반 `header/body` 형태로
   정규화한다.
5. 정규화된 packet을 해당 actor가 attach된 `Spot` runtime inbox로 넣는다.
6. 그 `Spot` inbox를 소비하는 실행기는 하나뿐이라고 가정한다.
7. 그 실행기 안에서만 `IZLinkActorContext.AddPacket(...)`으로 등록한 handler가
   수행된다.
8. actor가 room 상태를 바꾸거나 `Spot` 메서드를 호출해도, 이미 같은 `Spot`
   실행 문맥 안이므로 추가 lock이 필요 없다.

즉 구현 관점에서는 아래 흐름으로 보면 된다.

```text
joined actor session packet
    -> normalize to header/body
    -> submit to spot-owned inbox
    -> single spot consumer
    -> actor packet handler
    -> actor accesses Spot state
```

이 모델에서 중요한 점은 actor packet 처리를 session callback thread에서 바로
실행하지 않는다는 것이다. session callback은 actor가 어느 `Spot`에 붙어 있는지
확인하고, packet을 그 `Spot`이 소유한 실행 문맥으로 넘기는 데까지만 책임을 가진다.
실제 actor packet 처리는 반드시 `Spot` 실행 문맥 안에서만 일어난다.

이 규칙을 지키면 아래 입력원이 모두 한 줄로 선다.

- routed packet handler
- subscribe handler
- timer handler
- channel reply continuation
- attach가 끝난 actor session packet
- actor session disconnect 후속 처리

그래서 actor 안에서 `Spot` 상태를 읽고 쓸 때 "이 시점에 다른 packet handler가
같은 `Spot` state를 동시에 만지는가"를 매번 고민할 필요가 없다.

내부 구현은 mailbox, queue, executor, fiber 등 여러 방식으로 만들 수 있다.
하지만 wrapper 문서에서 고정해야 하는 최소 의미는 아래 정도다.

- join된 actor의 packet은 `Spot` 실행 문맥 밖에서 직접 처리하지 않는다.
- framework 내부 `SubmitAsync(...)`는 `Spot`이 소유한 직렬 실행 규칙 안에서만
  수행되고, 그 안에서 최종적으로 actor packet handler가 호출된다.
- stream packet은 framework Header 기반 `header/body`로 정규화된 뒤 같은 actor
  dispatch 경로를 탄다.

즉 "`Spot`에 actor가 join된다"는 말은 membership만 뜻하는 것이 아니라,
**그 actor의 packet 처리 ownership이 해당 `Spot`으로 넘어간다**는 뜻으로 읽어야 한다.

### 4.2 timer 등록

`Stage` 성격의 모델에는 timer가 거의 필수다.

예를 들면 아래 작업은 대부분 timer 없이는 설명하기 어렵다.

- 매칭 후 5초 뒤 시작
- 일정 시간 입력이 없으면 room 종료
- 100ms마다 state flush
- 1초마다 heartbeat publish

따라서 문서 초안도 timer를 두 갈래로 나누기보다, spot lifecycle 안에 등록하는
한 가지 모델로 정리하는 편이 맞다.

문서 초안 수준에서 필요한 최소 표면은 아래 정도다.

```csharp
public interface IZLinkTimer : IAsyncDisposable
{
    bool IsDisposed { get; }
    ValueTask CancelAsync(CancellationToken cancellationToken = default);
}

public interface IZLinkSpot
{
}

public interface IZLinkSpotContext
{
    void AddActorJoin<THandler, TActor, TRequest, TReply>()
        where THandler : class
        where TActor : IZLinkActor;

    ValueTask<IZLinkTimer> AddTimer<THandler>(
        string name,
        TimeSpan period,
        CancellationToken cancellationToken = default);
}
```

하부 `.NET` 바인딩 자체는 이미 아래 timer 표면을 제공한다.

```csharp
public sealed class Timer : IDisposable, IAsyncDisposable
{
    public static Timer FromSpot(Spot spot);
    public void Start(ulong intervalNs, ulong repeatCount);
    public void Stop();
    public ulong Recv(int flags = 0);
    public void OnFire(Action<Timer, ulong> handler);
}
```

즉 framework의 `Context.AddTimer<THandler>(...)`는 native timer handle을 그대로 드러내는
표면이 아니라, framework runtime이 만든 managed `.NET` timer를 spot
lifecycle/DI 모델에 붙이는 wrapper로 읽는 편이 맞다. timer tick이 발생하면
framework는 그 tick을 같은 spot execution context 안으로 enqueue해서
`IZLinkSpotTimerHandler<TSpot>.HandleAsync(...)`를 호출한다.
`IZLinkTimer.CancelAsync()`는 이 managed timer loop를 멈추고 정리하는 고수준
timer handle이다.

같은 맥락으로 actor join은 framework core 기본 표면보다 stage wrapper 같은 상위
확장 계층에서 따로 정의하는 편이 더 자연스럽다. 그래야 actor를 어떤 `Spot`에
귀속시킬지, join을 허용할지, 결과 payload에 무엇을 담을지를 wrapper가 자기
도메인 규칙으로 닫을 수 있다.

또한 이 표면에 `IServiceProvider`를 매번 넣는 것은 굳이 public 계약으로 보일 필요가
없다. `Spot`, timer handler, join handler는 framework가 만든 per-spot scope에서
resolve하고, 사용자에게는 "무슨 타입을 등록하는가"만 보이게 두는 편이 더 낫다.

여기서 더 중요한 것은 timer handler가 어느 실행 문맥에서 도는가다.
`Context.AddTimer<THandler>(...)`로 등록한 timer handler는 가능하면 같은 spot 실행
문맥에서 도는 쪽이 `Stage wrapper`에 더 자연스럽다.

따라서 wrapper 사용자는 low-level timer handle을 직접 알 필요가 없다.
`Context.AddTimer<THandler>(...)`만 보고, framework가 같은 `Spot` 실행 계약 안에서
timer handler를 호출한다고 이해하는 편이 맞다.

### 4.3 spot 생성 시 초기값 전달

현재 `IZLinkSpotManager`는 `spotRid` 생성과 삭제를 설명하기에는 충분하지만,
`Stage` 생성처럼 초기 payload를 갖는 모델을 설명하기에는 부족하다.

예를 들면 `playhouse`의 stage 생성은 보통 아래 정보를 함께 가진다.

- 어느 play node에 만들지
- stage type
- stage id
- create packet 또는 metadata

현재 `IZLinkSpotManager`의 기본 정의는
[handler-interfaces.ko.md](./handler-interfaces.ko.md)의 section 6.3을 따른다.
즉 현재 framework 기본 계약은 `spotName`만 받는 생성과, `spotName + spotRid`
까지를 다루는 수준이다.

`Stage wrapper`를 위해서는 최소한 아래 중 하나가 더 필요하다.

- `spotName + spotRid + metadata`
- create payload를 handler 초기화에 전달하는 별도 contract

다만 이 부분은 현재 하부 C API 공개 계약에서 바로 읽히는 내용은 아니다. 즉 아래
형태는 "framework 기본 계약"보다 "wrapper 전용 확장 후보"로 보는 편이 맞다.

```csharp
public interface IStageSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TMetadata>(
        string spotName,
        RoutingId spotRid,
        TMetadata metadata,
        CancellationToken cancellationToken = default);
}
```

### 4.4 actor 또는 session membership

`SPOT` 자체는 주소 가능한 논리 인스턴스까지만 설명하면 된다.
하지만 `Stage wrapper`는 보통 membership를 같이 가진다.

예:

- 누가 이 stage에 입장했는가
- 누가 나갔는가
- 현재 연결된 actor 집합이 무엇인가
- 특정 actor에게만 보내는가
- stage 전체에 broadcast 하는가

이건 `SPOT`의 필수 계약일 필요는 없고, 기본 `zlink framework` 범위를 넘는
영역이다. 다만 `Stage wrapper` 초안에서는 별도 축으로 명시되어야 한다.

즉 현재 `SPOT` 문서만으로는 아래를 충분히 설명하지 못한다.

- client connection과 spot의 연결
- actor 인증과 입장
- membership snapshot과 broadcast

### 4.5 stage directory 또는 lookup helper

`Stage wrapper`를 실제로 쓰는 응용은 보통 `stageId`만 알고 있는 경우가 많다.

즉 상위 계층에는 아래 중 하나가 필요하다.

- `stageId -> stage 위치 정보` lookup
- `logical key -> channel / node / spot` 해석 helper
- wrapper 전용 registry helper

이 기능을 `SPOT` 공용 API에 바로 넣을 필요는 없지만, `Stage wrapper` 초안에서는
누가 이 위치 해석을 맡는지 분명히 적어야 한다.

## 5. 정리

지금 `SPOT` 초안은 `Stage wrapper`의 하부 transport로는 충분히 쓸 만하다.
하지만 `Stage`와 비슷한 상위 모델을 바로 설명하기에는 아직 아래 네 축이 더
필요하다.

- 실행 문맥 계약
- timer 등록
- 초기 metadata를 받는 wrapper 확장 contract
- membership 와 directory 같은 상위 모델 보조 축

즉 결론은 이렇다.

- 현재 `SPOT` 기능은 버려야 할 수준은 아니다.
- 오히려 방향은 맞다.
- 다만 `playhouse`의 `Stage`를 감싸려면 `SPOT` 문서와는 별개로
  `Stage wrapper` 계약을 한 단계 더 적어야 한다.

## 6. 다음 단계 제안

다음 문서 작업은 아래 순서가 자연스럽다.

1. `IZLinkSpotContext.AddTimer<THandler>(...)` timer 초안 정리
2. `IZLinkSpotManager` metadata 확장을 wrapper 후보로 별도 정리
3. `Stage wrapper` 전용 문서에서 membership, broadcast, directory를 별도 정의
