# .NET ExecutionTurn E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-execution-turn.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| TD-A1 | 구현 | request, actor join, worker, framework HTTP client가 `Submit`/`Async`/`Yield`를 공개하고 blocking 완료 API를 노출하지 않는지 확인한다. |
| TD-A2 | 구현 | `Async` 대기 뒤 continuation과 completion이 끝난 다음 같은 Spot probe가 실행되는 순서를 확인한다. |
| TD-A3 | 구현 | 같은 Spot에서 여덟 개 read-modify-write를 `Async`로 실행하고 counter가 정확히 8인지 확인한다. |
| TD-A4 | 구현 | 1초 `Async` 대기의 응답이 별도 completion 경로로 도착해 timeout 없이 재개되는지 확인한다. |
| TD-A5 | 구현 | `Async` 대기 중 같은 Spot timer가 지연되고 대기 완료 뒤 실행되는지 확인한다. |
| TD-B1 | 구현 | `Yield` 대기 중 같은 Spot probe가 실행되고 continuation이 이후 재개되는지 확인한다. |
| TD-B2 | 구현 | `Yield` continuation 앞에 큐에 들어간 세 probe가 순서대로 실행되는지 확인한다. |
| TD-B3 | 구현 | 여덟 개 read-modify-write가 `Yield` 구간에서 같은 이전 값을 관측해 lost update가 발생함을 확인한다. |
| TD-B4 | 구현 | `Yield` 대기 중 같은 Spot timer가 실행되는지 확인한다. |
| TD-C1 | 구현 | DI로 주입한 framework HTTP client의 `Yield`가 외부 HTTP API 대기 중 Spot probe를 허용하는지 확인한다. |
| TD-C2 | 구현 | 같은 HTTP 호출의 `Async`가 completion까지 Spot turn을 유지하는지 확인한다. |
| TD-C3 | 구현 | CPU worker pool보다 많은 비동기 HTTP 작업을 `RunIoWorker(...).Yield(...)`로 완료하고 `WorkerQueueFull`이 없는지 확인한다. |
| TD-C4 | 구현 | CPU worker 스레드 증거와 `Async`/`Yield`에 따른 같은 Spot probe 순서 차이를 확인한다. |
| TD-C5 | 구현 | CPU worker delegate에 blocking I/O 언래핑이 없는지 source gate로 확인한다. |
| TD-D1 | 구현 | actor A가 `Yield` 중일 때 actor B handler가 실행되는지 확인한다. |
| TD-D2 | 구현 | actor A의 `Yield` 구간에도 같은 actor A의 두 번째 handler가 재진입하지 않는지 확인한다. |
| TD-D3 | 구현 | timer의 `Yield` 구간에도 같은 timer의 다음 tick이 이전 tick 완료 뒤 시작하는지 확인한다. |
| TD-E1 | 구현 | Entry Spot actor handler의 `JoinSpot(...).Async(...)`가 user Spot join을 완료하는지 확인한다. |
| TD-E2 | 구현 | user Spot actor handler의 `JoinSpot(...).Async(...)`가 다른 user Spot으로 이동을 완료하는지 확인한다. |
| TD-E3 | 구현 | 서로 반대 방향으로 시작한 두 user Spot join이 모두 timeout 없이 완료되는지 확인한다. |
| TD-F1 | 구현 | 다른 노드의 Spot request를 기다린 continuation이 caller 노드로 돌아오는지 확인한다. |
| TD-F2 | 구현 | route bridge로 도달한 `play-b` Spot에서도 `Yield` 의미와 marker 순서가 같은지 확인한다. |
| TD-F3 | 구현 | session relay로 도달한 actor handler에서도 `Yield`의 mailbox 의미가 같은지 확인한다. |
| TD-F4 | 구현 | request timeout 뒤 같은 Spot probe가 정상 실행되는지 확인한다. |
| TD-F5 | 구현 | cancellation 뒤 같은 Spot probe가 정상 실행되며 별도 shutdown runner가 runtime 종료와 recovery를 확인한다. |
| TD-F6 | 구현 | 현재 Spot으로 되돌아오는 `Async` request가 timeout으로 끝나고 다음 probe가 실행되는지 확인한다. |
| TD-G1 | 구현 | 공통 terminator 표면과 `Async`/`Yield` marker 순서를 .NET 결과로 고정한다. |
