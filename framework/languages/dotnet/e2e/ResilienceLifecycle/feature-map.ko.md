# .NET ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RL-A1 | 구현 | 단일 serving provider의 accepted request 완료, SIGTERM 뒤 `Drained` 종료와 row 제거, 정확한 down 오류, 같은 endpoint의 새 generation과 `ConnectionReady`, follow-up 20건을 확인한다. |
| RL-A2 | 구현 | handler-start 뒤 harness가 provider를 SIGKILL하고 lease 만료를 기다린 다음, 같은 rid·다른 endpoint의 새 generation과 `ConnectionReady`, replacement evidence를 확인한다. |
| RL-A3 | 구현 | 100개 client host를 restart 전후로 유지하고, 각 host의 `ConnectionReady`와 고유 follow-up reply 100건을 30초 상한 안에서 확인한다. |
| RL-A4 | 구현 | 다른 rid의 green provider가 실제 request를 처리한 뒤 old provider를 차례로 drain하며, `Draining`, terminal `Drained`, row 제거와 고정 간격 무중단 traffic을 확인한다. |
| RL-A5 | 구현 | SIGTERM·row 제거·재시작을 5회 반복하고, 매 cycle 하나의 새 generation과 `ConnectionReady`, down 구간 A 처리와 A·B 분산 복구를 확인한다. |
| RL-B1 | 구현 | client cancellation / pending cleanup marker가 있다. |
| RL-B2 | 구현 | slow handler-start 뒤 harness SIGKILL, `RouteNotConnected` 또는 request timeout의 유한 완료, lease 만료와 surviving provider follow-up을 확인한다. |
| RL-B3 | 구현 | provider graceful shutdown 뒤 stale endpoint로 가지 않고 재기동하는 marker가 있다. |
| RL-B4 | 구현 | socket weight 0 전파 뒤 row·연결을 유지한 채 신규 부하에서 제외되고 weight 100 복원 뒤 다시 처리하는지 확인한다. |
| RL-B5 | 구현 | slow handler-start 뒤 socket weight만 0으로 바꾸고, 기존 in-flight reply 완료와 신규 부하 제외를 각각 확인한다. |
| RL-B6 | 구현 | gray fault mode에서 일부 실패와 healthy provider 성공을 함께 관측하고 fault 해제 후 정상화하는 marker가 있다. |
| RL-C1 | 구현 | 다수 client host 생성/종료 후 follow-up request marker가 있다. |
| RL-C2 | 구현 | harness SIGKILL로 row remove를 건너뛰고 owner lease 만료 뒤 topology 성공 결과 이탈, surviving provider 처리와 provider 복구를 확인한다. |
| RL-C3 | 구현 | SIGTERM 정상 종료의 old row 제거와 같은 rid·endpoint 재시작 뒤 단일 새 generation, `ConnectionReady`, messaging 복구를 확인한다. |
| RL-C4 | 구현 | location store outage 중 이미 설정된 socket이 유지되고, store와 provider 복구 뒤 새 location-enabled client host가 정상 route를 구성하는 marker가 있다. |
| RL-D1 | 구현 | high fanout request burst marker가 있다. |
| RL-D2 | 전환 필요 | observer fault 격리에 더해 public `IZLinkRuntimeErrorSink`가 `zlink.runtime_error`/`observer_failed`/`message_flow_observer` event를 한 번 받는지 검증해야 한다. |
| RL-D3 | 전환 필요 | dispatch-error evidence를 `outcome=failed`, `reason=no_handler`, `action=reply_error`, `packet_name`으로 재정렬해야 한다. |
| RL-D4 | 전환 필요 | missing request handler가 wire message kind `Error=5`와 `errorCode`·`errorMessage`를 반환하고 client에서는 정상 message가 아닌 request 예외로 끝나는지 검증해야 한다. 정상 follow-up request는 `Response=2`이며 error field가 없어야 한다. 현재 client 예외와 server dispatch-error evidence만으로는 wire 계약을 충족했다고 판정하지 않는다. |
| RL-D5 | 구현 | request/send 혼합 burst workload marker가 있다. |
