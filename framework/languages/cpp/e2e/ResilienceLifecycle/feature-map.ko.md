# C++ ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

현재 C++ `ResilienceLifecycle`은 대부분의 public recovery 흐름을 전용 Registry, Provider, Workflow,
Consumer, Client target으로 실행한다. client support는 ResilienceLifecycle 전용 option/assert/evidence/topology
header로 분리했고, Consumer endpoint는 ResilienceLifecycle contract의 marker 필드를 보존한다.
일부 client scenario 내부 helper 이름은 아직 RegistryMessaging 기반 구현을 함께 재사용한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RL-A1` | 구현 | 전용 runner가 provider를 같은 endpoint와 같은 rid로 재시작하고, 실행 중인 client가 `rl_a1_provider_restart_scenario.hpp` 경로로 follow-up request를 성공시키는지 검증한다. |
| `RL-A2` | 구현 | 전용 runner가 같은 rid를 다른 endpoint로 재기동하고, 실행 중인 client가 `rl_a2_provider_endpoint_remap_scenario.hpp` 경로로 stale endpoint 대신 replacement provider로 전환되는지 검증한다. |
| `RL-A3` | 구현 | runner가 `rl_a3_reconnect_storm_scenario.hpp` 경로를 반복 실행해 재접속 반복 후 request가 계속 성공하는지 검증한다. |
| `RL-A4` | 구현 | runner가 provider B를 runtime weight `0`으로 drain하고 restore한 뒤, `rl_a4_drain_and_green_endpoint_scenario.hpp` 경로로 drain 중 A 신규 request와 restore 후 B traffic 복구를 검증한다. |
| `RL-A5` | 구현 | runner가 provider B를 여러 차례 stop/restart하면서 `rl_a5_provider_flapping_scenario.hpp` 경로로 살아 있는 provider request와 복구 후 정상화를 검증한다. |
| `RL-B1` | 구현 | runner가 Consumer HTTP `/profile/request/timeout/100`으로 timeout request를 보내고, 같은 consumer의 `/profile/request` 후속 request가 정상화되는지 검증한다. |
| `RL-B2` | 구현 | `inflight-crash` client가 B provider의 느린 request를 열어 둔 뒤 runner가 provider를 `SIGKILL`하고, 해당 request 실패와 A provider follow-up 성공을 검증한다. |
| `RL-B3` | 구현 | 전용 runner가 provider 하나를 정상 종료하고, 실행 중인 client가 `rl_b3_graceful_shutdown_scenario.hpp` 경로로 남은 provider에 request를 성공시키는지 검증한다. |
| `RL-B4` | 구현 | provider B의 server weight를 `0`으로 drain하고, 신규 request가 A로만 간 뒤 restore 후 B가 다시 traffic을 받는지 검증한다. |
| `RL-B5` | 구현 | provider B의 slow request가 처리 중일 때 drain을 걸고, in-flight reply가 정상으로 돌아오는지 검증한다. |
| `RL-B6` | 구현 | drain/restore 동안 healthy provider A가 계속 request를 처리하고 restore 후 전체 request 흐름이 정상화되는지 검증한다. |
| `RL-C1` | 구현 | runner가 Consumer HTTP `/profile/request/new-client`로 `.NET`처럼 요청마다 새 client host를 만들고, 반복 request와 cleanup follow-up marker가 provider evidence에 남는지 검증한다. |
| `RL-C2` | 구현 | provider B를 `SIGKILL`한 뒤 Consumer HTTP `/profile/request/new-client`가 살아 있는 `api-a`로 수렴하는지 확인하고, provider B 재기동 뒤 일반 request가 `api-b` evidence까지 회복되는지 검증한다. |
| `RL-C3` | 구현 | provider B 정지 중 Consumer HTTP `/profile/request`가 `api-a`로 수렴하는지 확인하고, provider B 재기동 뒤 recovered request evidence가 `api-b`에도 남는지 검증한다. split-brain topology DTO 단언은 아직 없다. |
| `RL-C4` | 구현 | `registry-outage` client가 registry 종료 전 manual channel request를 보낸 뒤, runner가 registry를 중지한 상태에서도 같은 client의 established channel request가 성공하고 provider evidence가 남는지 검증한다. 이후 runner가 `registry_host_factory.hpp` 경로로 구성된 registry와 provider A를 재기동하고, 새 discovery client가 `rl-c4-after-restart` request와 provider evidence를 성공시키는지 확인한다. |
| `RL-D1` | 구현 | runner가 Consumer HTTP `/profile/request`로 120개 request burst를 만들고 provider evidence에서 `rl-d1-` marker가 남는지 검증한다. |
| `RL-D2` | 구현 | `provider_host_factory.hpp`가 설치한 전용 `evidence_dispatch_error_observer.hpp` helper가 `handler_missing:reply_error` evidence를 `evidence_store.hpp`에 기록하고, `fault_state.hpp`의 observer fault mode가 켜져 있을 때 예외를 던진다. runner는 이후 follow-up request와 provider evidence가 계속 동작하는지 검증한다. |
| `RL-D3` | 구현 | runner가 Consumer HTTP `/profile/request/missing`으로 missing request를 실행하고 provider flow log의 `handler_missing`/`reply_error` marker를 검증한다. |
| `RL-D4` | 구현 | missing request handler가 typed reply를 반환하지 않고 public error path로 끝나는지 검증한다. raw wire error code 검증은 남아 있다. |
| `RL-D5` | 구현 | runner가 Consumer HTTP `/profile/request`와 `/profile/command`로 request/send mixed burst를 실행하고 provider evidence에서 `rl-d5-req-`, `rl-d5-cmd-` marker가 남는지 검증한다. 장시간 soak는 남아 있다. |
| Consumer role smoke | 구현 | runner가 전용 Consumer HTTP role을 띄우고 `/profile/request`, `/profile/request/new-client`, `/profile/request/timeout/100`, `/profile/request/missing`, `/profile/command`로 provider request, transient client host request, timeout cleanup, missing request, 정상 command 흐름을 확인한다. Marker contract, Registry handler/evidence/fault infrastructure, Provider evidence store, observer fault state, Consumer option/endpoint wrapper와 new-client endpoint 추가 뒤 최신 통과: `logs/20260701-031111-1848503`, 출력: `scenario RL-consumer passed`, `scenario RL-C1 consumer passed`, `scenario RL-C1 passed`, `scenario RL-C2 passed`, `scenario RL-C3 passed`, `scenario RL-D1 passed`, `scenario RL-D4 passed`, `scenario RL-D5 passed`. |

## 다음 구현 기준

- shared quick recovery, drain/restore, stress helper를 더 세부적인 `.NET` scenario 이름으로 나눌지
  검토한다. 현재는 같은 public recovery 동작을 여러 scenario가 함께 사용한다.
- 현재 구현 slice는 RegistryMessaging 기반으로 시작한 일부 client helper 이름을 아직 재사용한다. Consumer role main은
  ResilienceLifecycle 전용 configuration/endpoint wrapper와 marker 보존 contract를 사용하고, Provider host wiring, dispatch error
  observer, evidence store, observer fault state는 전용 factory/handler/infrastructure 파일로 분리했다. 다음 단계에서는
  public 동작을 유지하면서 남은 scenario/handler 이름을 ResilienceLifecycle 전용 이름으로 더 정리한다.
- registry host wiring은 `registry_host_factory.hpp`로 분리했고, registry outage는 established channel 유지와
  registry/provider 재기동 뒤 새 discovery client 복구까지 runner에서 검증한다. Registry evidence store와
  fault state는 infrastructure 파일로 분리했다. 선택적 registry channel endpoint가 설정되면
  `registry_handlers.hpp`의 profile handler와 dispatch error observer도 설치된다. Profile request/reply/send
  DTO는 `.NET`식 marker 필드를 지원하며, marker가 비어 있는 기존 scenario는 value 또는 command id를
  evidence marker로 계속 사용한다.
- Consumer role은 `RL-B1`, `RL-C1`, `RL-C2`, `RL-C3`, `RL-D1`, `RL-D3`, `RL-D4`, `RL-D5`까지 scenario 검증 경로로 넓혔다.
  `/profile/request/new-client`는 요청마다 transient client host를 만들고 별도 `storm-...-flow.log`를
  남긴다. 남은 후속 정리는 public 동작 추가가 아니라 RegistryMessaging 기반에서 시작한 일부 helper와
  파일 이름을 ResilienceLifecycle 전용 이름으로 맞추는 작업이다.
