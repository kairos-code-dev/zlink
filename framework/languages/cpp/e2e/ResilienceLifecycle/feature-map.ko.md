# C++ ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

현재 C++ `ResilienceLifecycle`은 대부분의 public recovery 흐름을 전용 Registry, Provider, Consumer, Client target으로 실행한다. client support는 ResilienceLifecycle 전용 option/assert/evidence/topology
header로 분리했고, Consumer host wiring은 `consumer_host_factory.hpp`로 분리했다. Provider admin endpoint는
`.NET`과 같은 drain/restore/weight/wait 이름을 제공하고, Consumer endpoint는 ResilienceLifecycle contract의 marker 필드를 보존한다.
낡은 `rm_*` client scenario 파일과 selector는 제거했고, shared message file, namespace, handler group,
channel 이름도 ResilienceLifecycle 전용 이름으로 정리했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RL-A1` | 구현 | 전용 runner가 provider를 같은 endpoint와 같은 rid로 재시작하고, 실행 중인 client가 `rl_a1_provider_restart_scenario.hpp` 경로로 follow-up request를 성공시키는지 검증한다. |
| `RL-A2` | 구현 | 전용 runner가 같은 rid를 다른 endpoint로 재기동하고, 실행 중인 client가 `rl_a2_provider_endpoint_remap_scenario.hpp` 경로로 stale endpoint 대신 replacement provider로 전환되는지 검증한다. |
| `RL-A3` | 구현 | `rl_a3_reconnect_storm_scenario.hpp`가 Consumer HTTP `/profile/request/new-client`를 24번 호출해 reconnect storm 중 요청마다 새 client host를 만들고, reply provider id와 provider evidence marker를 검증한다. |
| `RL-A4` | 구현 | `rl_a4_drain_and_green_endpoint_scenario.hpp`가 provider B `/admin/drain`, green provider endpoint 시작, original provider `/shutdown`, Registry `/topology/wait` Ready 1, green provider evidence, green `/shutdown`, original provider 복구, restored evidence를 `.NET`처럼 검증한다. |
| `RL-A5` | 구현 | runner가 provider B stop/restart를 3회 반복하고, `rl_a5_provider_flapping_scenario.hpp`가 down window의 Consumer HTTP request `api-a` 수렴, up window의 request 성공, provider B evidence prefix를 검증한다. |
| `RL-B1` | 구현 | runner가 Consumer HTTP `/profile/request/timeout/100`으로 timeout request를 보내고, 같은 consumer의 `/profile/request` 후속 request가 정상화되는지 검증한다. |
| `RL-B2` | 구현 | `inflight-crash` client가 Consumer HTTP `/profile/request` slow request를 열고 provider B file evidence에서 start marker를 확인한 뒤 provider B crash를 관찰한다. 이후 Registry `/topology/wait`가 `api-b` Ready 0개로 수렴하는지, in-flight request가 실패하는지, `api-a` follow-up과 provider B 재기동 뒤 restored request evidence가 남는지 검증한다. |
| `RL-B3` | 구현 | 전용 runner가 provider 하나를 정상 종료하고, 실행 중인 client가 `rl_b3_graceful_shutdown_scenario.hpp` 경로로 남은 provider에 request를 성공시키는지 검증한다. |
| `RL-B4` | 구현 | `rl_b4_runtime_drain_scenario.hpp`가 provider B의 `/admin/drain`, `/admin/restore`, `/admin/weight/wait` 경로를 사용해 신규 request가 A로만 가는지, drained provider evidence가 늘지 않는지, restore 후 provider B evidence가 회복되는지 `.NET`처럼 검증한다. |
| `RL-B5` | 구현 | `rl_b5_drain_inflight_scenario.hpp`가 Consumer HTTP slow request를 열고 실제 slow provider를 evidence file로 찾은 뒤 해당 provider를 `/admin/drain`한다. 신규 request가 healthy provider로 가는지, in-flight reply가 drained provider에서 끝나는지, drained provider evidence가 새 request를 받지 않는지, restore 뒤 evidence가 회복되는지 `.NET`처럼 검증한다. |
| `RL-B6` | 구현 | provider B의 gray fault mode를 켠 뒤 `rl_b6_gray_fault_scenario.hpp`가 gray request 실패와 healthy provider 성공을 함께 관찰하고, fault mode 해제 뒤 follow-up request가 정상화되는지 검증한다. 최신 full 통과: `logs/20260701-173140-37072`, 출력: `scenario RL-B6 passed`. |
| `RL-C1` | 구현 | `rl_c1_client_host_lifecycle_scenario.hpp`가 Consumer HTTP `/profile/request/new-client`로 `.NET`처럼 요청마다 새 client host를 만들고, 반복 request와 cleanup follow-up marker가 provider evidence에 남는지 검증한다. |
| `RL-C2` | 구현 | provider B의 `/admin/crash`를 호출한 뒤 Registry `/topology/wait`가 `api-b` Ready 0개로 수렴하는지 확인하고, Consumer HTTP `/profile/request/new-client`가 살아 있는 `api-a`로 수렴하는지 확인한다. provider B 재기동 뒤 일반 request가 `api-b` evidence까지 회복되는지도 검증한다. |
| `RL-C3` | 구현 | `rl_c3_node_pause_recovery_scenario.hpp`가 provider B `/shutdown`, Consumer HTTP `/profile/request`의 `api-a` 수렴, provider B 재기동 뒤 Registry `/topology/wait` Ready 1, recovered request evidence를 `.NET`처럼 검증한다. |
| `RL-C4` | 구현 | `registry-outage` client가 registry 종료 전 manual channel request를 보낸 뒤, runner가 registry를 중지한 상태에서도 같은 client의 established channel request가 성공하고 provider evidence가 남는지 검증한다. 이후 runner가 `registry_host_factory.hpp` 경로로 구성된 registry와 provider A를 재기동하고, 새 discovery client가 `rl-c4-after-restart` request와 provider evidence를 성공시키는지 확인한다. |
| `RL-D1` | 구현 | runner가 Consumer HTTP `/profile/request`로 120개 request burst를 만들고 provider evidence에서 `rl-d1-` marker가 남는지 검증한다. |
| `RL-D2` | 구현 | `provider_host_factory.hpp`가 설치한 전용 `evidence_dispatch_error_observer.hpp` helper가 `handler_missing:reply_error` evidence를 `evidence_store.hpp`에 기록하고, `fault_state.hpp`의 observer fault mode가 켜져 있을 때 예외를 던진다. runner는 이후 follow-up request와 provider evidence가 계속 동작하는지 검증한다. |
| `RL-D3` | 구현 | runner가 Consumer HTTP `/profile/request/missing`으로 missing request를 실행하고 provider flow log의 `handler_missing`/`reply_error` marker를 검증한다. |
| `RL-D4` | 구현 | `rl_d4_missing_request_handler_scenario.hpp`가 Consumer HTTP `/profile/request/missing`을 호출하고, public failure payload와 provider dispatch error evidence를 검증한다. 최신 full 통과: `logs/20260701-173140-37072`, 출력: `scenario RL-D4 passed`. |
| `RL-D5` | 구현 | `rl_d5_mixed_burst_scenario.hpp`가 Consumer HTTP `/profile/request`와 `/profile/command`로 request/send mixed burst를 실행하고 provider evidence에서 request/send marker가 남는지 검증한다. 최신 full 통과: `logs/20260701-173140-37072`, 출력: `scenario RL-D5 passed`. |
| Consumer role smoke | 구현 | runner가 전용 Consumer HTTP role을 띄우고 `/profile/request`, `/profile/request/new-client`, `/profile/request/timeout/100`, `/profile/request/missing`, `/profile/command`로 provider request, transient client host request, timeout cleanup, missing request, 정상 command 흐름을 확인한다. Marker contract, Registry handler/evidence/fault infrastructure, Provider evidence store, observer/gray fault state, Consumer host factory, Provider admin shutdown/crash/drain/restore/weight/wait endpoint, Registry topology wait/shutdown endpoint 추가 뒤 최신 통과: `logs/20260701-173140-37072`, 출력: `scenario RL-B4 passed`, `scenario RL-B5 passed`, `scenario RL-C3 passed`, `scenario RL-D5 passed`, `resilience-lifecycle e2e result=passed`. |

## 완료 기준

- shared quick recovery와 drain/restore 흐름은 `.NET` scenario 이름에 맞는 전용 scenario header와
  runner orchestration으로 검증한다. RL-B6 gray fault는 전용 scenario header와 provider fault mode로
  분리했고, Provider admin endpoint는 drain/restore/weight/wait 이름을 `.NET`과 맞췄다.
- Client runner와 scenario selector는 ResilienceLifecycle 전용 이름으로 정리했다. Consumer role main은
  ResilienceLifecycle 전용 configuration/endpoint wrapper와 marker 보존 contract를 사용하고, host wiring은
  `consumer_host_factory.hpp`로 분리했다. Provider host wiring, dispatch error observer, evidence store,
  observer fault state, admin weight state는 전용 factory/handler/infrastructure 파일로 분리했다.
- `.NET`의 `ResilienceProcessManager`가 담당하는 provider/registry process 시작, health 대기, 종료,
  stdout/stderr 로그 저장 책임은 C++ `run_e2e.sh`가 담당한다. 이 차이는 언어별 harness 배치 차이이며
  scenario나 public 동작 차이가 아니다.
- registry host wiring은 `registry_host_factory.hpp`로 분리했고, registry outage는 established channel 유지와
  registry/provider 재기동 뒤 새 discovery client 복구까지 runner에서 검증한다. Registry evidence store와
  fault state는 infrastructure 파일로 분리했다. 선택적 registry channel endpoint가 설정되면
  `registry_handlers.hpp`의 profile handler와 dispatch error observer도 설치된다. Profile request/reply/send
  DTO는 `.NET`식 marker 필드를 지원하며, marker가 비어 있는 기존 scenario는 value 또는 command id를
  evidence marker로 계속 사용한다. Registry endpoint는 topology wait와 shutdown endpoint도 제공한다.
- Consumer role은 `RL-B1`, `RL-C1`, `RL-C2`, `RL-C3`, `RL-D1`, `RL-D3`, `RL-D4`, `RL-D5`까지 scenario 검증 경로로 넓혔다.
  RL-D4/RL-D5는 shell-only 검증에서 전용 client scenario header 경로로 옮겼다.
  `/profile/request/new-client`는 요청마다 transient client host를 만들고 별도 `storm-...-flow.log`를
  남긴다.
