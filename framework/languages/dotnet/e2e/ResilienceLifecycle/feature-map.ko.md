# .NET ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RL-A1 | 구현 | provider를 같은 endpoint로 재시작하고 consumer 재시작 없이 복구하는 marker가 있다. |
| RL-A2 | 구현 | 같은 rid provider를 다른 endpoint로 재기동하고 topology 갱신 뒤 원래 endpoint를 복구하는 marker가 있다. |
| RL-A3 | 구현 | 다수 client host를 동시에 생성해 재접속 storm 후 request가 정상화되는 marker가 있다. |
| RL-A4 | 미구현 | rolling update / blue-green marker가 없다. |
| RL-A5 | 미구현 | provider flapping marker가 없다. |
| RL-B1 | 구현 | client cancellation / pending cleanup marker가 있다. |
| RL-B2 | 미구현 | in-flight provider crash marker가 없다. |
| RL-B3 | 구현 | provider graceful shutdown 뒤 stale endpoint로 가지 않고 재기동하는 marker가 있다. |
| RL-B4 | 구현 | runtime drain / restore marker가 있다. |
| RL-B5 | 구현 | drain 중 in-flight 완료 marker가 있다. |
| RL-B6 | 구현 | gray fault mode에서 일부 실패와 healthy provider 성공을 함께 관측하고 fault 해제 후 정상화하는 marker가 있다. |
| RL-C1 | 미구현 | resource cleanup marker가 없다. |
| RL-C2 | 미구현 | stale registry cleanup marker가 없다. |
| RL-C3 | 미구현 | node pause/recovery marker가 없다. |
| RL-C4 | 구현 | registry outage 중 direct established socket이 유지되고, registry/provider 재시작 뒤 new discovery host가 복구되는 marker가 있다. |
| RL-D1 | 구현 | high fanout request burst marker가 있다. |
| RL-D2 | 구현 | dispatch-error observer fault 뒤 messaging follow-up이 계속 동작하는 marker가 있다. |
| RL-D3 | 구현 | dispatch-error evidence marker(reason/action/packetName)가 남는 marker가 있다. |
| RL-D4 | 구현 | missing request handler error reply 예외와 server dispatch-error evidence marker가 있다. |
| RL-D5 | 구현 | request/send 혼합 burst workload marker가 있다. |
