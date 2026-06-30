# C++ ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

현재 C++ `ResilienceLifecycle`은 대부분의 public recovery 흐름을 전용 Registry, Provider, Workflow,
Client target으로 실행한다. client scenario 구현은 ResilienceLifecycle 위치의 header로 일부 분리했지만,
아직 RegistryMessaging contract와 helper 이름을 재사용하는 1차 slice다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RL-A1` | 구현 | 전용 runner가 provider를 같은 endpoint와 같은 rid로 재시작하고, 실행 중인 client가 `RM-A4` failover flow로 follow-up request를 성공시키는지 검증한다. |
| `RL-A2` | 구현 | 전용 runner가 같은 rid를 다른 endpoint로 재기동하고, 실행 중인 client가 stale endpoint 대신 replacement provider로 전환되는지 검증한다. |
| `RL-A3` | 구현 | runner가 `quick` client를 반복 실행해 재접속 반복 후 request가 계속 성공하는지 검증한다. |
| `RL-A4` | 구현 | runner가 provider B를 runtime weight `0`으로 drain하고 restore한 뒤, drain 중 A로 신규 request가 가고 restore 후 B가 다시 traffic을 받는지 검증한다. |
| `RL-A5` | 구현 | runner가 provider B를 여러 차례 stop/restart하면서 살아 있는 provider로 request가 성공하고 복구 후에도 request가 정상화되는지 검증한다. |
| `RL-B1` | 구현 | 전용 runner가 `RM-C4` timeout isolation flow를 실행하고, timeout 뒤 후속 request가 정상화되는지 검증한다. |
| `RL-B2` | 구현 | `inflight-crash` client가 B provider의 느린 request를 열어 둔 뒤 runner가 provider를 `SIGKILL`하고, 해당 request 실패와 A provider follow-up 성공을 검증한다. |
| `RL-B3` | 구현 | 전용 runner가 provider 하나를 정상 종료하고, 실행 중인 client가 남은 provider로 request를 성공시키는지 `RM-B2` scale-in flow로 검증한다. |
| `RL-B4` | 구현 | provider B의 server weight를 `0`으로 drain하고, 신규 request가 A로만 간 뒤 restore 후 B가 다시 traffic을 받는지 검증한다. |
| `RL-B5` | 구현 | provider B의 slow request가 처리 중일 때 drain을 걸고, in-flight reply가 정상으로 돌아오는지 검증한다. |
| `RL-B6` | 구현 | drain/restore 동안 healthy provider A가 계속 request를 처리하고 restore 후 전체 request 흐름이 정상화되는지 검증한다. |
| `RL-C1` | 구현 | 반복 client request 뒤 follow-up request가 성공하고 client process가 정상 종료되는지 검증한다. |
| `RL-C2` | 구현 | provider B를 `SIGKILL`한 뒤 follow-up request가 public retry window 안에서 살아 있는 provider로 성공하는지 검증한다. |
| `RL-C3` | 구현 | provider B 정지/복구 뒤 각각 follow-up request가 성공하는지 검증한다. split-brain topology DTO 단언은 아직 없다. |
| `RL-C4` | gap | registry outage 중 established channel timeout/public behavior가 아직 정리되지 않았다. |
| `RL-D1` | 구현 | `resilience-stress` client가 다수 request를 보내고 모두 reply를 받는지 검증한다. |
| `RL-D2` | gap | observer failure event를 수집하는 C++ public harness 연결이 없다. |
| `RL-D3` | 구현 | 전용 runner가 missing packet flow를 실행하고 provider flow log의 `handler_missing`/`drop` marker를 검증한다. |
| `RL-D4` | 구현 | missing request handler가 typed reply를 반환하지 않고 public error path로 끝나는지 검증한다. raw wire error code 검증은 남아 있다. |
| `RL-D5` | 구현 | `resilience-stress` client가 request와 send를 섞은 burst workload를 실행하고 붕괴 없이 완료되는지 검증한다. 장시간 soak는 남아 있다. |

## 다음 구현 기준

- shared quick recovery, drain/restore, stress helper를 더 세부적인 `.NET` scenario 이름으로 나눌지
  검토한다. 현재는 같은 public recovery 동작을 여러 scenario가 함께 사용한다.
- 현재 구현 slice는 RegistryMessaging contract와 handler 이름을 내부적으로 재사용한다. 다음 단계에서는
  public 동작을 유지하면서 ResilienceLifecycle 전용 contract/support 이름으로 정리한다.
- public API로 구현할 수 없는 registry outage와 observer failure 항목은 runner-only adapter로 메우지
  않고 gap으로 유지한다.
