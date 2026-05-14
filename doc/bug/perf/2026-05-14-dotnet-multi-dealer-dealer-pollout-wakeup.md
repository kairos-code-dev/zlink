# .NET multi DEALER_DEALER POLLOUT wakeup gap

## 요약

`.NET` multi `MULTI_DEALER_DEALER` sender를 정책의
`DONTWAIT` + `POLLOUT` backpressure 모델로 바꾸면 일부 실행에서 active phase가
끝나지 않고 runner timeout까지 진행되는 현상이 있다.

## 관찰한 증상

- `bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/src/PerfMultiDealerDealerClient.cs`
  의 active sender loop를 모든 socket이 pending일 때 poller `POLLOUT` `-1` 대기로
  바꾸면 `tcp`, `tls`, `wss` 일부 size에서 간헐적으로 `FAIL`이 난다.
- 단일 케이스 재실행은 성공하는 경우가 많지만, 전체 transport/size 순회에서는
  `READY`, `AUTO_HWM_DETAIL`, `CLIENT_READY` 이후 `RESULT`가 나오지 않는 케이스가
  발생했다.
- 같은 코드에서 server 쪽을 wire stop token + poller `-1` 수신 모델로 바꾸는 것은
  단독 smoke test에서 동작했다.

## 임시 처리

현재 .NET perf sender active loop는 기존처럼 public binding API의
`SendFlags.DontWait`를 사용하되, 모든 send가 막힌 경우 짧게 yield한 뒤 다시
시도한다. 이는 측정 hot path의 정책 목표와 완전히 일치하지 않으므로 최종 상태가
아니다.

## 필요한 수정

1. .NET binding의 `PollManager` / `Poller`가 `DEALER` socket의 `POLLOUT` wakeup을
   C perf와 같은 의미로 안정적으로 전달하는지 확인한다.
2. core wakeup에는 문제가 없고 binding event 보존 또는 ready index 처리 문제라면
   binding 쪽을 수정한다.
3. 수정 뒤 `MULTI_DEALER_DEALER` sender active loop를 `send_pending` + `POLLOUT`
   모델로 다시 바꾸고, 전체 transport/size matrix가 `complete`인지 확인한다.
