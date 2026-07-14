# C++ ResilienceLifecycle E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-5-resilience-lifecycle.ko.md`

현재 C++ `ResilienceLifecycle`은 Redis location store를 공유하는 Provider, Consumer, Client target으로 public recovery 흐름을 실행한다. 별도 Registry role은 제거했고, topology 검증은 Consumer HTTP의 `/topology`와 `/topology/wait`가 location store를 직접 조회하는 방식으로 수행한다. Client target은 HTTP-only dispatcher이고, framework channel client는 Consumer/Provider role 안에서 실행한다. client support는 ResilienceLifecycle 전용 option/assert/evidence/topology header로 분리했고, Consumer host wiring은 `consumer_host_factory.hpp`로 분리했다. Provider admin endpoint는 `.NET`과 같은 drain/restore/weight/wait 이름을 제공하고, Consumer endpoint는 ResilienceLifecycle contract의 marker 필드를 보존한다.

최신 full runner proof는 `logs/20260708-133049-101113`이다. 이 실행은 `RL-B2`의 `kill -9`와
`RL-C2`의 SIGABRT crash처럼 시나리오가 의도한 failure injection만 허용하고, 그 외 provider
비정상 종료는 runner 실패로 드러내도록 보강한 뒤 통과했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `RL-A1` | 구현 | 전용 runner가 provider를 같은 endpoint와 같은 rid로 재시작하고, 실행 중인 client가 `rl_a1_provider_restart_scenario.hpp` 경로로 follow-up request를 성공시키는지 검증한다. |
| `RL-A2` | 구현 | 전용 runner가 같은 rid를 다른 endpoint로 재기동하고, 실행 중인 client가 `rl_a2_provider_endpoint_remap_scenario.hpp` 경로로 stale endpoint 대신 replacement provider로 전환되는지 검증한다. |
| `RL-A3` | 구현 | `rl_a3_reconnect_storm_scenario.hpp`가 Consumer HTTP `/profile/request/new-client`를 24번 호출해 reconnect storm 중 요청마다 새 client host를 만들고, reply provider id와 provider evidence marker를 검증한다. |
| `RL-A4` | 구현 | `rl_a4_drain_and_green_endpoint_scenario.hpp`가 provider B `/admin/drain`, green provider endpoint 시작, original provider shutdown, Consumer `/topology/wait` Ready 1, green provider evidence, green shutdown, original provider 복구, restored evidence를 `.NET`처럼 검증한다. |
| `RL-A5` | 구현 | runner가 provider B stop/restart를 3회 반복하고, `rl_a5_provider_flapping_scenario.hpp`가 down window의 Consumer HTTP request `api-a` 수렴, up window의 request 성공, provider B evidence prefix를 검증한다. |
| `RL-B1` | 구현 | runner가 Consumer HTTP `/profile/request/timeout/100`으로 timeout request를 보내 `TimeoutException`을 확인하고, 같은 consumer의 후속 request가 정상화되는지 검증한다. |
| `RL-B2` | 구현 | `inflight-crash` client가 Consumer HTTP `/profile/request/manual-b` slow request를 열고 provider B file evidence에서 start marker를 확인한 뒤 provider B crash를 관찰한다. 이후 Consumer `/topology/wait`가 `api-b` Ready 0개로 수렴하는지, in-flight request가 실패하는지, `api-a` follow-up과 provider B 재기동 뒤 restored request evidence가 남는지 검증한다. |
| `RL-B3` | 구현 | 전용 runner가 provider 하나를 정상 종료하고, 실행 중인 client가 `rl_b3_graceful_shutdown_scenario.hpp` 경로로 남은 provider에 request를 성공시키는지 검증한다. |
| `RL-B4` | 구현 | `rl_b4_runtime_drain_scenario.hpp`가 provider B의 `/admin/drain`, `/admin/restore`, `/admin/weight/wait` 경로를 사용해 신규 request가 A로만 가는지, drained provider evidence가 늘지 않는지, restore 후 provider B evidence가 회복되는지 `.NET`처럼 검증한다. |
| `RL-B5` | 구현 | `rl_b5_drain_inflight_scenario.hpp`가 Consumer HTTP slow request를 열고 실제 slow provider를 evidence file로 찾은 뒤 해당 provider를 `/admin/drain`한다. 신규 request가 healthy provider로 가는지, in-flight reply가 drained provider에서 끝나는지, drained provider evidence가 새 request를 받지 않는지, restore 뒤 evidence가 회복되는지 `.NET`처럼 검증한다. |
| `RL-B6` | 구현 | provider B의 gray fault mode를 켠 뒤 gray request의 `RequestFailed`와 healthy provider 성공을 함께 관찰하고, fault mode 해제 뒤 follow-up request가 정상화되는지 검증한다. |
| `RL-C1` | 구현 | `rl_c1_client_host_lifecycle_scenario.hpp`가 Consumer HTTP `/profile/request/new-client`로 `.NET`처럼 요청마다 새 client host를 만들고, 반복 request와 cleanup follow-up marker가 provider evidence에 남는지 검증한다. |
| `RL-C2` | 구현 | provider B의 `/admin/crash`를 호출한 뒤 Consumer `/topology/wait`가 `api-b` Ready 0개로 수렴하는지 확인하고, Consumer HTTP `/profile/request/new-client`가 살아 있는 `api-a`로 수렴하는지 확인한다. provider B 재기동 뒤 일반 request가 `api-b` evidence까지 회복되는지도 검증한다. |
| `RL-C3` | 구현 | `rl_c3_node_pause_recovery_scenario.hpp`가 provider B `/shutdown`, Consumer HTTP `/profile/request`의 `api-a` 수렴, provider B 재기동 뒤 Consumer `/topology/wait` Ready 1, recovered request evidence를 `.NET`처럼 검증한다. |
| `RL-C4` | 구현 | `rl_c4_location_store_outage_scenario.hpp`가 Redis location store outage 전 Consumer `/profile/request/manual`을 호출하고, runner가 Redis container를 pause한 상태에서도 Consumer role의 established manual channel request가 성공하고 provider evidence가 남는지 검증한다. Redis 복구와 provider A 재기동 뒤 Consumer `/profile/request/new-client`가 `rl-c4-after-restart` request와 provider evidence를 성공시키는지 확인한다. |
| `RL-D1` | 구현 | runner가 Consumer HTTP `/profile/request`로 120개 request burst를 만들고 provider evidence에서 `rl-d1-` marker가 남는지 검증한다. |
| `RL-D2` | 구현 | observer fault 중 missing request가 `HandlerNotFound`로 분류되고 `handler_missing:reply_error` evidence가 남으며, 이후 messaging이 계속 동작하는지 검증한다. |
| `RL-D3` | 구현 | Consumer HTTP의 missing request가 `HandlerNotFound`로 분류되고 provider flow log에 `handler_missing`/`reply_error` marker가 남는지 검증한다. |
| `RL-D4` | 구현 | 실제 provider/consumer E2E는 missing request의 `HandlerNotFound` public failure와 provider dispatch error evidence를 확인한다. runtime unit gate는 같은 channel error reply의 raw header에서 `Error=5`, camelCase `errorCode`/`errorMessage`, `status` 부재와 성공 `Response=2`를 직접 검증한다. |
| `RL-D5` | deferred | 공통 문서가 요구하는 동시 다수 client, 수 분 지속, request/send 혼합, latency drift 관측을 제공하는 soak harness가 없다. 기존 120회 순차 mixed burst는 이 계약을 검증하지 못하므로 scenario PASS 경로에서 제거했다. |
| Consumer role smoke | 구현 | runner가 전용 Consumer HTTP role을 띄우고 `/profile/request`, `/profile/request/manual`, `/profile/request/manual-b`, `/profile/request/new-client`, `/profile/request/timeout/100`, `/profile/request/missing`, `/profile/command`, `/profile/command/missing`, `/topology`, `/topology/wait`으로 provider request, established manual request, transient client host request, timeout cleanup, missing request/send, 정상 command 흐름, location store topology 조회를 확인한다. |

## 완료 기준

- shared quick recovery와 drain/restore 흐름은 `.NET` scenario 이름에 맞는 전용 scenario header와
  runner orchestration으로 검증한다. RL-B6 gray fault는 전용 scenario header와 provider fault mode로
  분리했고, Provider admin endpoint는 drain/restore/weight/wait 이름을 `.NET`과 맞췄다.
- Client runner와 scenario selector는 ResilienceLifecycle 전용 이름으로 정리했고 HTTP-only dispatcher로 유지한다. Consumer role main은
  ResilienceLifecycle 전용 configuration/endpoint wrapper와 marker 보존 contract를 사용하고, host wiring은
  `consumer_host_factory.hpp`로 분리했다. Provider host wiring, dispatch error observer, evidence store,
  observer fault state, admin weight state는 전용 factory/handler/infrastructure 파일로 분리했다.
- `.NET`의 `ResilienceProcessManager`가 담당하는 provider process 시작, health 대기, 종료,
  stdout/stderr 로그 저장 책임은 C++ `run_e2e.sh`가 담당한다. Redis는 runner가 loopback container로
  시작하며, 사용자 환경에서 넘긴 외부 Redis endpoint를 공유 Redis로 재사용하지 않는다.
- Consumer host가 Redis location store를 조회해 topology endpoint를 제공한다. 별도 registry host,
  registry evidence store, registry fault state, registry handler는 현재 C++ 경로에서 제거했다.
  Profile request/reply/send DTO는 `.NET`식 marker 필드를 지원하며, marker가 비어 있는 기존 scenario는
  value 또는 command id를 evidence marker로 계속 사용한다.
- Consumer role은 `RL-B1`, `RL-C1`, `RL-C2`, `RL-C3`, `RL-D1`, `RL-D3`, `RL-D4`까지 scenario 검증 경로로 넓혔다.
  RL-D4는 전용 client scenario와 raw envelope unit gate를 함께 사용한다. RL-D5는 지속 부하
  harness가 마련될 때까지 `deferred`다.
  `/profile/request/new-client`는 요청마다 transient client host를 만들고 별도 `storm-...-flow.log`를
  남긴다.
