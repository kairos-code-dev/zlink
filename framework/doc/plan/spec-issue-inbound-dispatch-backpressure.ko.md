# spec 이슈 — 수신 dispatch queue의 backpressure

> 대상: framework 공통 spec 담당
> 제기: `dotnet/guide/16-options.ko.md`의 backpressure 절을 쓰면서 발견
> 확인 대상: `.NET` `Zlink.Framework` 현재 작업 트리
> 관련 spec: [06-framework-api](../framework/common/spec/06-framework-api.ko.md) ·
> [08-channel-messaging](../framework/common/spec/08-channel-messaging.ko.md)
> 상태: 문제 확인 완료, 방향 결정 필요

## 1. 요약

메시지가 도착해서 handler가 실행되기까지 사이에 `.NET` 구현은 **채널마다 1024개짜리
수신 queue**를 둔다. 이 queue가 가득 차면 backpressure가 걸리는 대신 **거절하거나
버린다.** 어느 spec도 이 queue의 존재, 크기, 넘쳤을 때의 동작을 정의하지 않는다.

그 결과 두 가지가 어긋난다.

1. 같은 channel send·request가 **RouteMesh 경로와 ClientServer 경로에서 다르게** 동작한다.
   한쪽은 보내는 쪽까지 압력이 전달되고, 다른 쪽은 받는 쪽에서 잘라 낸다.
2. 보내는 쪽은 전송이 정상 완료됐다고 판단하는데 받는 쪽에서 메시지가 사라질 수 있다.
   Send와 fanout에는 이 사실을 알리는 경로가 없다.

## 2. 현재 `.NET` 동작

### 2.1 ClientServer channel과 classic fanout

수신 루프는 소켓에서 꺼낸 즉시 queue로 넘기고 다음 메시지를 읽으러 간다. handler 완료를
기다리지 않는다.

| 항목 | 현재 값 | 근거 |
| --- | --- | --- |
| queue 깊이 | 1024 | `ZLinkChannelApplicationDispatchQueue.cs:7` |
| 넘겨주는 방식 | `TryWrite` — 차 있으면 기다리지 않고 실패한다 | 같은 파일 `:46` |
| 실행 | 단일 worker가 하나씩 `await`로 실행한다 | 같은 파일 `:14`, `:83` |
| 가득 찼을 때(request) | `Rejected` 오류 reply를 보낸다. 메시지는 `ClientServer channel '<name>' application queue is full.` | `ZLinkClientServerDispatcher.cs:70-92` |
| 가득 찼을 때(send) | 위 경로가 request가 아니면 그대로 반환한다 — 아무 통지 없이 사라진다 | 같은 위치 `:79-81` |
| 가득 찼을 때(fanout 구독) | `Dispose`만 호출한다 — 조용히 버린다 | `ZLinkChannelReceiveLoop.cs:239`, `:301` |

### 2.2 RouteMesh route 경로

이쪽에는 중간 queue가 없다. 드레인 루프가 dispatch를 기다린 뒤에 다음 메시지를 꺼낸다.

```csharp
// ZLinkSpotActivationDispatcher.cs:126-135
while (!cancellationToken.IsCancellationRequested)
{
    var received = nativeSpot.RecvRoute(RecvFlags.DontWait);
    if (received is null) return;
    await _routeDispatcher.DispatchAsync(received, cancellationToken);
}
```

그래서 이 경로의 channel handler가 느리면 드레인이 멈추고, Core pipe가 차고,
`ReceiveHighWaterMark`에 닿고, 결국 보내는 쪽 send가 기다린다. Spot·Actor 앞으로 온
payload는 Spot queue에 넣고 돌아오므로 이 루프를 오래 잡지 않는다.

### 2.3 두 경로의 차이

| | RouteMesh channel | ClientServer channel · fanout |
| --- | --- | --- |
| 받는 쪽이 밀릴 때 | 소켓 읽기가 멈춰 보내는 쪽까지 압력이 전달된다 | 소켓은 계속 읽고 queue 상한에서 잘라 낸다 |
| 초과분 | 생기지 않는다. 보내는 쪽이 기다린다 | request는 오류 reply, send·fanout은 유실 |
| 보내는 쪽이 아는 것 | `DeadlineExceeded`로 실패를 안다 | send·fanout은 성공으로 끝나고 알지 못한다 |

## 3. spec이 말하지 않는 것

- 수신 경로에 application dispatch queue가 있다는 사실 자체가 없다. Spec은
  [Spot application queue](../framework/common/spec/12-spot-messaging.ko.md)만 정의한다.
- 그 queue의 깊이와 적용 범위(channel별인지 node별인지)가 없다.
- 가득 찼을 때 backpressure를 거는지, 거절하는지, 버리는지가 없다.
- 거절할 때 어떤 오류로 끝나는지가 없다. 현재 구현은 `Rejected`를 쓰지만 이 값의 정의는
  "target admission seal 또는 application policy가 거부함"이라 용량 부족과 맞지 않는다.
  같은 enum에 "placement, queue 또는 bounded resource에 여유가 없다"는 `CapacityExceeded`가
  이미 있다(`06-framework-api` §의 오류 표 6번).
- 한 channel의 handler가 동시에 몇 개까지 실행되는지가 없다. 현재 구현은 단일 worker라
  같은 channel에서 두 handler가 겹쳐 실행되지 않는데, `dotnet/guide/05-channel-messaging.ko.md`는
  "서로 다른 요청은 동시에 실행될 수 있다"고 적고 있다. 어느 쪽이 계약인지 정해야 한다.

## 4. 왜 문제인가

**유실이 조용하다.** send와 fanout은 보내는 쪽이 이미 성공으로 끝난 뒤 받는 쪽에서
사라진다. 보내는 쪽에는 재시도할 근거가 남지 않고, 받는 쪽에도 "몇 개를 버렸다"를
알리는 공개 표면이 없다.

**같은 API가 경로에 따라 다르게 동작한다.** application은 `SendToChannel(name, msg)` 하나만
쓰는데, 그 channel이 RouteMesh인지 ClientServer인지에 따라 과부하 때 결과가 달라진다.
어느 topology를 쓸지는 배치 결정이지 신뢰성 결정이 아니었다.

**상한을 조정할 방법이 없다.** 1024는 코드 상수이고 옵션으로 노출되어 있지 않다.
`ReceiveHighWaterMark`를 조정해도 이 queue에는 영향을 주지 않는다.

## 5. 제안하는 방향

**queue가 가득 차면 버리지 않고 backpressure를 건다.** 수신 루프가 queue에 넣지 못하면
소켓에서 더 읽지 않고 자리가 날 때까지 기다린다. 그러면 Core pipe가 차고
`ReceiveHighWaterMark`에 닿아 보내는 쪽까지 압력이 전달된다 — RouteMesh 경로가 이미 그렇게
동작하므로 두 경로의 의미가 같아진다.

이렇게 하면 유실이 사라지고, 과부하는 보내는 쪽의 `DeadlineExceeded`로 드러난다.
보내는 쪽이 실패를 알고 재시도나 우회를 결정할 수 있다.

함께 정해야 할 항목이다.

1. **queue 깊이를 공개 옵션으로 둘 것인가.** 지금은 1024 고정이다. 노출한다면 어느
   단위(channel별·node별)에 어떤 이름으로 둘지 정한다.
2. **queue 자체를 유지할 것인가.** RouteMesh처럼 중간 queue 없이 dispatch를 기다리는 편이
   계약이 단순하다. 유지한다면 그 이유(수신 루프와 handler 실행의 분리)를 spec에 적는다.
3. **거절을 남길 것인가.** 모든 과부하를 backpressure로 흡수하면 느린 handler 하나가 그
   channel 전체를 멈출 수 있다. 상한 시간을 두고 그 뒤에는 거절하는 방식을 남길지 정한다.
   남긴다면 오류는 `Rejected`가 아니라 `CapacityExceeded`가 맞다.
4. **한 channel의 handler 동시 실행 수.** 1로 고정할지, 옵션으로 둘지, 아니면 지금 guide가
   적은 대로 동시 실행을 계약으로 삼을지 정한다. 이 값이 정해져야 위의 queue 논의도
   결론이 난다.
5. **버린 수를 관측할 표면.** 어떤 결론이든 과부하로 처리하지 못한 수는 monitoring으로
   드러나야 한다. [11-monitoring](../framework/dotnet/guide/11-monitoring.ko.md)의 message
   flow에 `dropped`가 있으므로 이 경로를 그 값에 연결할지 확인한다.

## 6. 남은 작업

1. 위 다섯 항목을 결정하고 `06-framework-api`에 수신 dispatch 계약 절을 만든다.
2. `.NET` ClientServer·fanout 수신 루프를 결정한 방식으로 고친다.
3. Java·Kotlin, Node.js와 C++의 수신 경로가 같은 방식인지 확인하고 맞춘다.
4. 과부하 상황의 contract test를 추가한다 — 유실 여부, 보내는 쪽이 관찰하는 결과, 관측
   지표.
5. `dotnet/guide/05-channel-messaging.ko.md`의 handler 동시 실행 서술과
   `16-options.ko.md`의 backpressure 절을 결정에 맞춘다.
