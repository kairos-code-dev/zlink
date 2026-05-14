# .NET multi PUBSUB POLLOUT wakeup gap

## 요약

`.NET` multi `MULTI_PUBSUB` server active publish loop를 정책의
`DONTWAIT` + `POLLOUT` backpressure 모델로 바꾸면 `tcp` smoke run에서
`RESULT`가 나오지 않고 runner timeout까지 진행된다.

## 관찰한 증상

- `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiPubSubServer.cs`
  에서 `PublishNoWait()` 실패 시 `Poller.Wait(-1)`로 `POLLOUT`을 기다리도록
  바꾸면 `tcp` 64B, 256B, 1024B가 모두 `result_timeout`으로 실패했다.
- C++ perf처럼 평소에는 `POLLOUT` registration을 `None`으로 두고, `EAGAIN` 뒤에만
  `POLLOUT`으로 `Modify()`한 다음 `Poller.Wait(-1)`로 기다리는 방식도 `tcp` 64B부터
  `result_timeout`으로 실패했다.
- 같은 실행 조건에서 기존 `DontWait` 재시도 경로로 되돌리면 `tcp` 64B는 정상
  완료했다.

## 임시 처리

현재 .NET `MULTI_PUBSUB` server active loop는 기존처럼 `PublishNoWait()` 실패 시
짧게 yield한 뒤 재시도한다. 이는 hot loop 정책 목표와 완전히 일치하지 않으므로
최종 상태가 아니다.

## 필요한 수정

1. `PubSocket` 또는 XPUB no-drop 경로의 `POLLOUT` readiness가 .NET public
   `Poller`로 안정적으로 전달되는지 확인한다.
2. core wakeup에는 문제가 없고 binding event 전달 문제라면 .NET binding을
   수정한다.
3. 수정 뒤 `MULTI_PUBSUB` server active loop를 `DONTWAIT` + `POLLOUT` 모델로
   다시 바꾸고 전체 transport/size matrix가 `complete`인지 확인한다.
