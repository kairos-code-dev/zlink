# C++ YieldDispatch .NET 기준 포팅 inventory

기준 구현: `framework/languages/dotnet/e2e/YieldDispatch`

현재 C++ `YieldDispatch`는 Redis location store 기반 Delay, Play, Session, Client role target,
Track A YD-A1~YD-A4, Track B YD-B1~YD-B3, Track C YD-C1~YD-C3, Track D YD-D1~YD-D4,
Track E YD-E1~YD-E5 실행 코드, YD-E4 정적 검증, YD-E5 report 생성을 갖췄다. 이 inventory는
`.NET` Config 8 파일을 기준으로 C++에 만들어야 할 역할, shared contract, client scenario, support 파일을
계속 고정한다. Config 8은 stream connector client request가 session gateway를 거쳐 play node의
Spot/Entry Spot handler까지 도달해야 하므로, HTTP trigger나 direct Spot/route test driver로 대체하지
않는다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | metadata | done | C++ YieldDispatch 로그와 임시 산출물 제외 규칙을 추가했다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | scenario별 구현 상태와 최신 full runner proof를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | Redis container와 per-run key prefix를 준비하고, delay A/B, play A/B, session A/B, client를 빌드한 뒤 readiness와 정적 검사를 수행한다. registry role은 사용하지 않는다. 이 runner는 E2E target만 필요하므로 configure 때 C++ sample target은 끈다. YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D1~YD-D4, YD-E1~YD-E3 runner, YD-E4 static gate, YD-E5 report 생성을 반복 검증한다. 최신 Redis location store full runner proof는 `logs/20260707-151703-2374204`이다. |
| `Shared/YieldDispatch.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | role/client target 묶음은 추가됐다. shared는 header로 포함된다. |
| `Shared/Messages.cs` | `Shared/yield_dispatch_contracts.hpp` | shared | done | Track A용 ensure/evidence/delay/hold/yield/worker/probe DTO, actor binding/yield/fast/join-yield/push-yield DTO, timer command, D2 remote Spot yield DTO, E1 timeout DTO, E2 cancellation DTO, E3 shutdown DTO와 JSON 매핑이 있다. stream connector typed request/reply/notify가 실제 JSON payload를 쓰도록 `to_stream_payload`/`from_stream_payload` hook도 제공한다. |
| `Client/GlobalUsings.cs` | not-needed | client | not-needed | C++에는 대응 파일이 필요 없다. |
| `Client/YieldDispatch.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | client target이 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client | done | stream connector로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2~YD-D4, YD-E1, YD-E2 request를 시작하고 검증한다. `shutdown-wait`/`shutdown-recovery` mode로 YD-E3도 실행한다. client option 파싱, evidence wait, assertion, actor context는 support/scenario-support header로 분리됐다. YD-A2/A4는 yielded command와 probe/evidence 관측을 별도 connector로 분리한다. YD-B1/B3/C3는 같은 session-a connector에서 in-flight actor/spot request를 겹쳐 `.NET`과 같은 교차 실행선 release를 검증한다. D2 owner evidence 관측은 public evidence snapshot polling을 쓴다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_options.hpp` | support | done | session-a와 session-b stream endpoint, scenario 선택, shutdown flow option을 읽고 stream connector option을 만든다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/scenario_assert.hpp`; `Client/Support/evidence_wait.hpp` | support | done | marker 순서 검증, request line fragment 검증, result error 출력, evidence snapshot polling helper가 있다. C++에서는 assertion과 evidence wait를 별도 support header로 나눈다. |
| `Client/Scenarios/YieldActorScenarioContext.cs` | `Client/Scenarios/yield_actor_scenario_context.hpp` | scenario-support | done | actor binding과 session relay scenario가 공유하는 spot rid, actor A, actor B context를 분리했다. |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/Scenarios/yd_a1_basic_terminator_scenario.hpp` | scenario | done | YD-A1 flow는 별도 scenario header로 분리했고 runner로 통과했다. |
| `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/Scenarios/yd_a2_yield_terminator_scenario.hpp` | scenario | done | YD-A2 flow는 별도 scenario header로 분리했고 runner로 통과했다. yielded request와 probe/evidence 관측은 별도 connector로 분리한다. |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/Scenarios/yd_a3_continuation_context_scenario.hpp` | scenario | done | request id, spot rid, correlation id, yield continuation marker 순서를 별도 scenario header에서 runner로 검증한다. `.NET`도 stream metadata 직접 노출을 완료 조건에 넣지 않는다. |
| `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/Scenarios/yd_a4_worker_yield_scenario.hpp` | scenario | done | public worker call `yield()` flow는 별도 scenario header로 분리했고 runner로 통과했다. yielded request와 probe/evidence 관측은 별도 connector로 분리한다. |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/Scenarios/yd_b1_other_actor_progress_scenario.hpp` | scenario | done | actor A yield 중 actor B 진행은 별도 scenario header에서 runner로 통과했다. |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/Scenarios/yd_b2_same_actor_reentry_scenario.hpp` | scenario | done | 같은 stream connector session에서 같은 actor mailbox 재진입 금지 marker 순서를 runner로 검증한다. |
| `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/Scenarios/yd_b3_actor_join_yield_scenario.hpp` | scenario | done | 공통 문서가 허용한 `JoinEntrySpot` terminator 경로로 actor join yield 중 actor B fast request 진행을 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/Scenarios/yd_c1_timer_isolation_scenario.hpp` | scenario | done | timer A yield 중 timer B fast tick 진행은 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/Scenarios/yd_c2_timer_reentry_scenario.hpp` | scenario | done | 같은 timer yield 중 다음 tick 재진입 금지 marker 순서는 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/Scenarios/yd_c3_actor_timer_isolation_scenario.hpp` | scenario | done | actor yield 중 timer fast tick 진행과 timer yield 중 actor fast request 진행은 별도 scenario header에서 runner로 검증한다. actor-yield 중 timer-fast half도 같은 session-a connector에서 timer command와 evidence wait를 보내 actor/timer 실행선 release를 검증한다. 최신 통과 로그는 `logs/20260701-191329-11276`이다. |
| `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/Scenarios/yd_d2_remote_spot_yield_scenario.hpp` | scenario | done | remote Spot yield continuation 소유권 검증은 별도 scenario header에서 runner로 통과했다. owner evidence 관측은 public evidence snapshot polling을 쓴다. |
| `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/Scenarios/yd_d3_route_bridge_yield_scenario.hpp` | scenario | done | route bridge 경유 Spot handler yield 검증은 별도 scenario header에서 runner로 통과했다. |
| `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | `Client/Scenarios/yd_d4_session_relay_actor_yield_scenario.hpp` | scenario | done | session actor relay와 bound session push 격리는 별도 scenario header에서 runner로 통과했다. |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/Scenarios/yd_e1_timeout_scenario.hpp` | scenario | done | yield timeout 뒤 같은 Spot mailbox cleanup 검증은 별도 scenario header에서 runner로 통과했다. |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/Scenarios/yd_e2_cancellation_scenario.hpp` | scenario | done | public cancellation token을 넘긴 `yield(token)`이 delay reply 대기를 취소하고, cancellation 뒤 같은 Spot probe가 처리되는지 검증한다. |
| `Client/Scenarios/ShutdownYieldScenario.cs` | `Client/Scenarios/shutdown_yield_scenario.hpp` | scenario | done | pending yield 중 play node shutdown과 recovery 검증은 별도 scenario header와 runner orchestration으로 통과했다. |
| `Server/Registry/YieldDispatch.Registry.csproj` | not-needed | build | removed | C++ Config 8은 Redis location store 기반 auto-connect를 사용하므로 registry target을 빌드하지 않는다. |
| `Server/Registry/Program.cs` | not-needed | server-role | removed | registry role entrypoint는 삭제했다. |
| `Server/Registry/RegistryHostFactory.cs` | not-needed | server-role | removed | registry host factory와 registry option support는 삭제했다. Play/Session role은 Redis `redis_location_store_t`를 공유한다. |
| `Server/Delay/YieldDispatch.Delay.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | delay service target이 있다. |
| `Server/Delay/Program.cs` | `Server/Delay/main.cpp` | server-role | done | delay service entrypoint가 있다. delay host 구성은 factory header로 분리했다. |
| `Server/Delay/DelayHostFactory.cs` | `Server/Delay/delay_host_factory.hpp`; `Server/Delay/Support/delay_support.hpp` | server-role | done | delay channel server, handler registration, dispatch trace, health endpoint 구성을 factory header로 분리했고, delay option 읽기는 support header로 분리했다. Delay target과 runner로 검증했다. |
| `Server/Delay/DelayHandler.cs` | `Server/Delay/Handlers/delay_handler.hpp` | handler | done | delayed reply handler를 `Handlers/`로 분리했고, delay-started/delay-completed evidence entry를 남긴다. Delay target과 runner로 검증했다. |
| `Server/Delay/DelaySupport.cs` | `Server/Delay/Support/delay_support.hpp` | support | done | delay node state, option 읽기, evidence store를 `Support/`로 분리했다. C++ runner는 Play evidence를 주 검증 경로로 쓰지만 Delay evidence 파일도 남긴다. |
| `Server/Play/YieldDispatch.Play.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | play node target이 있다. |
| `Server/Play/Program.cs` | `Server/Play/main.cpp` | server-role | done | play node entrypoint가 있다. play host 구성은 factory header로 분리했다. |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/play_host_factory.hpp`; `Server/Play/Support/play_support.hpp` | server-role | done | play node의 control route, spot mesh, delay client, dispatch trace, health endpoint 구성을 factory header로 분리했고, play option 읽기는 support header로 분리했다. Play target과 runner로 검증했다. |
| `Server/Play/PlaySupport.cs` | `Server/Play/Support/play_support.hpp` | support | done | evidence store, node rid, option 읽기를 support header로 분리했다. Evidence snapshot/wait는 public control route로 검증하고 evidence 파일도 남긴다. |
| `Server/Play/Spots/PlaySpotRuntime.cs` | `Server/Play/Spots/play_spot_runtime.hpp` | spot | done | request id별 evidence와 `YieldProbeSpot` runtime을 분리했고 YD-A/B/C/D2~D4/E1/E3 runner 증거가 있다. C++ spot handler 등록은 member entrypoint가 필요하므로 runtime header가 helper handler를 호출한다. |
| `Server/Play/Spots/PlaySpotTypes.cs` | `Server/Play/Spots/play_spot_types.hpp` | spot | done | actor 타입, actor factory, timer handler/state 타입을 분리했다. |
| `Server/Play/Handlers/PlayBasicSpotHandlers.cs` | `Server/Play/Handlers/play_basic_spot_handlers.hpp`; `Server/Play/Spots/play_spot_runtime.hpp` | handler | done | YD-A1~YD-A4용 hold/yield/worker/probe handler 로직을 `Handlers/` helper header로 분리했고 Play target과 runner로 검증했다. C++ spot runtime member entrypoint는 이 helper를 호출한다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/Handlers/play_actor_handlers.hpp` | handler | done | YD-B1/B2 actor yield/fast handler, YD-B3 actor join-yield handler, YD-D4 actor push-yield handler를 분리했고 runner 증거가 있다. |
| `Server/Play/Handlers/PlayTimerSpotHandlers.cs` | `Server/Play/Handlers/play_timer_spot_handlers.hpp`; `Server/Play/Spots/play_spot_runtime.hpp` | handler | done | YD-C1~YD-C3 timer start/stop command와 timer yield/fast/next handler 로직을 `Handlers/` helper header로 분리했고 Play target과 runner로 검증했다. C++ timer runtime entrypoint는 이 helper를 호출한다. |
| `Server/Play/Handlers/PlayRemoteSpotHandlers.cs` | `Server/Play/Handlers/play_remote_spot_handlers.hpp`; `Server/Play/Spots/play_spot_runtime.hpp` | handler | done | YD-D2 remote Spot handler 로직은 helper header로 분리했고 `YieldProbeSpot` runtime entrypoint가 호출한다. public `spot_context_t::request_to(...).yield()`로 target Spot reply를 기다린다. D3는 existing Spot yield/probe handler와 route bridge 경로로 검증했다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | `Server/Play/Handlers/play_control_handlers.hpp` | handler | done | EnsureSpot, BindYieldActors, Evidence, EvidenceWait control route handler를 분리했고 YD-D3의 target Spot 준비와 evidence wait에도 같은 public control route를 사용해 runner로 검증했다. |
| `Server/Play/Handlers/PlayFailureSpotHandlers.cs` | `Server/Play/Handlers/play_failure_spot_handlers.hpp` | handler | done | YD-E1 timeout cleanup handler와 YD-E2 cancellation cleanup handler를 분리했고 Play target과 runner로 검증했다. |
| `Server/Session/YieldDispatch.Session.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | session gateway target이 있다. |
| `Server/Session/Program.cs` | `Server/Session/main.cpp` | server-role | done | session gateway entrypoint가 있다. stream session 등록과 mesh 구성은 factory header로 분리했다. |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/session_host_factory.hpp` | server-role | done | session gateway의 route client, spot mesh, stream node, dispatch trace, health endpoint 구성을 factory header로 분리했고 Session target과 runner로 검증했다. session option 읽기는 `Support/session_support.hpp`로 분리했다. |
| `Server/Session/Support/SessionSpotTypes.cs` | not-needed | support | not-needed | C++ session relay는 framework의 public `session_actor_manager_t`, `session_actor_t`, `actor_gateway_t`를 직접 사용하므로 `.NET`처럼 session-side actor wrapper 타입을 따로 만들지 않는다. |
| `Server/Session/Support/SessionSupport.cs` | `Server/Session/Support/session_support.hpp` | support | done | session role option 읽기를 support header로 분리했고 Session target build로 검증했다. 현재 C++ session role은 scenario evidence를 Play role evidence와 stream reply로 검증하므로 별도 session evidence store는 만들지 않는다. |
| `Server/Session/Support/YieldSession.cs` | `Server/Session/Support/yield_session.hpp` | session | done | connector session implementation을 support header로 분리했고 Session target과 runner로 검증했다. YD-E1 timeout packet relay와 YD-E3 shutdown/recovery packet relay도 같은 typed JSON/session route 경로로 검증했다. |
| `Server/Session/Support/YieldSessionRelay.cs` | `Server/Session/Support/yield_session.hpp` | session | done | Ensure/evidence/Track A relay 코드, actor bind/relay 코드, timer command relay 코드가 있고, session host의 spot mesh를 통해 public Spot route dispatch가 통과한다. worker yield relay, D2 remote Spot yield relay, D4 bound session stream binding, E1 timeout relay, E3 shutdown/recovery relay도 포함한다. |
| `Server/Session/Support/YieldShutdownRelay.cs` | `Server/Session/Support/yield_session.hpp` | session | merged | shutdown scenario relay는 C++ session implementation header 안에 합쳐서 구현했다. |

## Scenario 대응

| Scenario ID | C++ 대응 | 상태 |
|-------------|----------|------|
| `YD-A1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-A2` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-A3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-A4` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-B1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-B2` | `Client/Scenarios/yd_b2_same_actor_reentry_scenario.hpp`; `Server/Play/Handlers/play_actor_handlers.hpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-B3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-C1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-C2` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-C3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-D1` | `run_e2e.sh`; `Server/Play/Support/play_support.hpp` | done |
| `YD-D2` | `Client/Scenarios/yd_d2_remote_spot_yield_scenario.hpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-D3` | `Client/Scenarios/yd_d3_route_bridge_yield_scenario.hpp`; `Server/Play/Spots/play_spot_runtime.hpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-D4` | `Client/Scenarios/yd_d4_session_relay_actor_yield_scenario.hpp`; `Server/Play/Handlers/play_actor_handlers.hpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-E1` | `Client/Scenarios/yd_e1_timeout_scenario.hpp`; `Server/Play/Handlers/play_failure_spot_handlers.hpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-E2` | `Client/Scenarios/yd_e2_cancellation_scenario.hpp`; `Server/Play/Handlers/play_failure_spot_handlers.hpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-E3` | `Client/Scenarios/shutdown_yield_scenario.hpp`; `Server/Session/Support/yield_session.hpp`; `run_e2e.sh` | done |
| `YD-E4` | `run_e2e.sh` static checks | done |
| `YD-E5` | `run_e2e.sh`; `logs/<run>/yield-dispatch-report.json` | done |

## 검증

- 2026-07-07: `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build CMAKE_BUILD_PARALLEL_LEVEL=2 nice -n 10 timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260707-151703-2374204`
  - 의미: YD-A1~YD-E5 full runner가 통과했다. YD-E2는 public `cancellation_token_source_t`와
    `cancellation_token_t`, `yield(token)`으로 delay request 대기를 취소하고 같은 Spot의 후속 probe가
    처리되는지 검증한다. runner 출력은 `scenario YD-E2 passed`, `scenario YD-E5 passed`,
    `yield-dispatch e2e result=passed`를 포함한다.
- 2026-07-02: `timeout 240s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260702-085155-85387`
  - 의미: 현재 트리에서 YD-A1~YD-D4, YD-E1, YD-E3, YD-E4 static gate, YD-E5 report 생성이 통과했다.
    runner 출력은 `scenario YD-D1 passed`, `scenario YD-E5 passed`, `yield-dispatch e2e result=passed`를
    포함한다. 당시 report는 YD-E2를 public cancellation token 계약 gap으로 유지했다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_registry zlink_cpp_e2e_yield_dispatch_delay zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: skeleton gate 통과
  - 로그: `logs/20260630-084019-3310404`
  - 의미: Registry, Delay, Play, Session, Client target skeleton이 빌드되고 readiness/static-check gate를
    통과한다. 아직 YD scenario 완료 증거는 아니다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-085825-3353947`
  - 의미: stream connector -> Session gateway -> Play route channel까지는 도달한다. Session의 public
    `route_client_t` spot request overload 송신은 `surface=spot_route`로 기록되지만, Play inbound는
    route mesh handler lookup으로 처리되어 `HoldReq`/`ProbeReq`가 `handler_missing`으로 실패한다.
    내부 dispatch 우회 없이 C++ public Spot route 경로를 확인해야 한다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-090734-3374231`
  - 의미: session host에 spot mesh를 열어 route backend가 public Spot route bridge를 사용할 수 있게
    했고, YD-A1/YD-A2가 stream connector -> Session gateway -> Play Spot handler -> Delay service
    경로로 통과한다. runner 출력은 `scenario YD-A1 passed`, `scenario YD-A2 passed`,
    `yield-dispatch track-a result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-143647-181455`
  - 의미: C++ framework typed JSON 기본 serializer 경로를 channel/route/framework `message_t`
    payload까지 유지하도록 고친 뒤, YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가
    `yield-dispatch track-a-d result=passed`로 통과한다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-091449-3391388`
  - 의미: YD-A1~YD-A4 Track A가 통과한다. A3는 `.NET`과 같은 완료 조건으로 request id, spot rid,
    correlation id 보존과 yield continuation marker 순서를 검증한다. A4는 public
    `spot_context_t::run_worker(...)` call object의 `yield()`가 Spot turn을 반납하고 probe 뒤 원래
    Spot mailbox에서 continuation을 재개하는 marker 순서를 검증한다. runner 출력은
    `scenario YD-A1 passed`, `scenario YD-A2 passed`, `scenario YD-A3 passed`,
    `scenario YD-A4 passed`, `yield-dispatch track-a result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-093239-3426210`
  - 의미: YD-A1~YD-A4와 YD-B1/YD-B2가 통과한다. B1은 actor A yield 중 actor B fast request가 먼저
    완료되는 actor mailbox 격리를 검증한다. B2는 같은 actor A fast request가 actor A yield continuation
    뒤에 실행되는 marker를 검증하지만, fast request는 같은 actor ref에 bound된 별도 connector에서 들어가므로
    `.NET`의 같은 stream session 증거와 같지는 않다. runner 출력은 `scenario YD-B1 passed`,
    `scenario YD-B2 passed`, `yield-dispatch track-a-b result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-101935-3481095`
  - 의미: YD-A1~YD-A4와 YD-B1~YD-B3가 통과한다. B3는 공통 문서가 허용한 `JoinEntrySpot`
    terminator 경로로 actor join `yield()` 중 actor B fast request가 먼저 완료되는 marker 순서를
    검증한다. runner 출력은 `scenario YD-B3 passed`, `yield-dispatch track-a-b result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-102640-3506576`
  - 의미: YD-A1~YD-A4, YD-B1~YD-B3, YD-C1이 통과한다. C1은 public `spot_context_t::add_timer<THandler>`
    timer handler에서 delay request를 `yield()`로 기다리는 동안 같은 Spot의 다른 timer fast tick이 먼저
    완료되는 marker 순서를 검증한다. runner 출력은 `scenario YD-C1 passed`,
    `yield-dispatch track-a-c result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-103011-3518610`
  - 의미: YD-A1~YD-A4, YD-B1~YD-B3, YD-C1/YD-C2가 통과한다. C2는 같은 timer의 다음 tick이
    이전 yield tick의 continuation과 completion 뒤에 실행되는 marker 순서를 검증한다. runner 출력은
    `scenario YD-C2 passed`, `yield-dispatch track-a-c result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-103612-3549306`
  - 의미: YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3가 통과한다. C3는 actor yield 중 timer fast tick
    진행과 timer yield 중 actor fast request 진행을 모두 marker 순서로 검증한다. 당시 actor-yield 중
    timer-fast half는 observer connector를 써서 `.NET`의 같은 stream session 증거와 같지는 않았다.
    runner 출력은 `scenario YD-C3 passed`, `yield-dispatch track-a-c result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-105453-3622781`
  - 의미: YD-D2 remote Spot yield가 단일 일반 runner에서 통과한다. `play-a` owner Spot이 `play-b`
    target Spot에 request를 보내고 `yield()`로 기다린 뒤, caller continuation marker가 `play-a`에 남는
    범위를 검증한다. runner 출력은 `scenario YD-D2 passed`,
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-111214-3697636`
  - 의미: 반복 실행에서 YD-C1 timer yield evidence wait가 실패하고 Play process가 segfault했다.
    앞선 반복에서는 `logs/20260630-111003-3686323`처럼 YD-C2 evidence wait 실패도 재현됐다.
    D2 자체가 아니라 기존 timer yield race가 완료 gate를 막고 있으므로 Track A-D 전체를 done으로 올리지 않는다.
- 2026-06-30: `ZLINK_CPP_E2E_PLAY_WRAPPER='valgrind --error-exitcode=99 --track-origins=yes --leak-check=no' ./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-111334-3705294`
  - 의미: valgrind 환경은 timing이 느려 YD-A2에서 실패했지만, Play shutdown 경로에서
    `native_spot_route_discovery_bridge_t`가 소유한 discovery를 destroy한 뒤 native discovery service
    thread가 다시 읽는 use-after-free도 관측됐다. timer yield race와 별개로 route discovery bridge
    종료 순서도 후속 안정화 후보로 남긴다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: timer handler coroutine이 suspension 뒤 지역 `timer_runtime_t`의 `this`를 다시 읽지 않도록
    Spot context를 coroutine frame에 보관하고, routed async reply가 full `received_t` 대신 routing id,
    spot id, request seq만 보관하도록 고친 뒤 target build가 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-114059-3822096`
  - 의미: 일반 runner에서 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh` 반복 실행
  - 결과: 통과 2회 뒤 실패
  - 통과 로그: `logs/20260630-114500-3835073`, `logs/20260630-114512-3836412`
  - 실패 로그: `logs/20260630-114522-3837343`
  - 의미: 앞의 두 번은 `yield-dispatch track-a-d result=passed`까지 통과했다. 세 번째 실패는
    scenario 진입 전 Play startup의 `Unknown error 704 (errno=105)` 때문에 `YD-A ensure spot request failed`로
    끝났고, C1/C2 timer yield race나 D2 handler exception과는 분리되는 환경/resource 계열 실패다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-114544-3838681`
  - 의미: 간격을 둔 일반 runner에서 다시 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-114601-3839936`
  - 의미: runner의 build 단계에서 `libzlink_stream_connector.a: file format not recognized`가 발생했다.
    `cmake --build framework/languages/cpp/build --target zlink_stream_connector zlink_cpp_e2e_yield_dispatch_client`로
    산출물을 다시 만들면 통과하므로 scenario 실패 증거로 보지 않는다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-114912-3852776`
  - 의미: scenario 진입 전 Session startup의 `Unknown error 702 (errno=22)` 때문에 readiness 대기에서
    실패했다. Track C timer yield나 D2 remote Spot yield handler 경로까지 도달하지 않은 startup 실패다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: client option/assert/evidence/context header 분리, shared stream connector JSON payload hook,
    Session EnsureSpotReq relay validation, Play EnsureSpotReq error detail 보존 변경 뒤 target build가 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-121845-3957741`
  - 의미: runner configure에서 C++ sample target을 끄고 E2E target만 빌드했다. client support 분리 뒤에도
    stream connector -> Session gateway -> Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4,
    YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-A1~YD-A4 client scenario header 분리 뒤 client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-122458-3977955`
  - 의미: YD-A1~YD-A4 client scenario header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-B1~YD-B3 client scenario header 분리 뒤 client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-122820-3987190`
  - 의미: YD-B1~YD-B3 client scenario header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-C1~YD-C3 client scenario header 분리 뒤 client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-123326-4011471`
  - 의미: YD-C1~YD-C3 client scenario header 분리와 main include 정리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-D2 client scenario header 분리 뒤 client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-123513-4016348`
  - 의미: YD-D2 client scenario header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play evidence store와 control route handler 분리 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-123901-4029574`
  - 의미: Play support/control handler 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play spot type header 분리 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-124201-4041598`
  - 의미: Play spot type header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play actor handler header 분리 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-124536-4057662`
  - 의미: YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3까지 통과한 뒤 YD-D2 RemoteSpotYieldReq가
    Play handler exception으로 실패했다. actor handler 분리 경로는 통과했으나 D2 간헐 실패와 구분하기 위해
    즉시 재실행했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-124619-4059721`
  - 의미: Play actor handler header 분리 뒤 재실행에서 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play `YieldProbeSpot` runtime header 분리 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-125153-4078147`
  - 의미: Play `YieldProbeSpot` runtime header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_session`
  - 결과: 통과
  - 의미: Session `yield_session_t`를 `Server/Session/Support/yield_session.hpp`로 분리한 뒤 Session target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-125756-4099056`
  - 의미: Session `yield_session_t` support header 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_delay`
  - 결과: 통과
  - 의미: Delay `delay_handler_t`와 `delay_state_t`를 `Server/Delay/Handlers/`, `Server/Delay/Support/`로 분리한 뒤 Delay target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-130227-4109990`
  - 의미: Delay handler/support 분리 뒤에도 stream connector -> Session gateway ->
    Play Spot/Entry Spot handler -> Delay service 경로로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다.
    runner 출력은 `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_registry`
  - 결과: 통과
  - 의미: Registry host 구성을 `Server/Registry/registry_host_factory.hpp`로 분리한 뒤 Registry target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-130511-4119514`
  - 의미: Registry host factory 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_delay`
  - 결과: 통과
  - 의미: Delay host 구성을 `Server/Delay/delay_host_factory.hpp`로 분리한 뒤 Delay target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-130810-4125409`
  - 의미: Delay host factory 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play host 구성을 `Server/Play/play_host_factory.hpp`로 분리한 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-131140-4140481`
  - 의미: Play host factory 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_session`
  - 결과: 통과
  - 의미: Session host 구성을 `Server/Session/session_host_factory.hpp`로 분리한 뒤 Session target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-131433-4150421`, `logs/20260630-131528-4154191`
  - 의미: 첫 실행은 YD-A4 WorkerYieldReq, 두 번째 실행은 기존에도 관측된 YD-D2 RemoteSpotYieldReq handler exception으로 실패했다.
    Session host factory 분리 뒤 고정 startup/build 실패는 아니지만, 이 실패 로그들은 완료 증거로 쓰지 않는다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-131613-4157208`
  - 의미: Session host factory 분리 뒤 최종 재실행에서 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play basic Spot handler 로직을 `Server/Play/Handlers/play_basic_spot_handlers.hpp`로 분리한 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-132035-4171285`
  - 의미: Play basic Spot handler helper 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: Play timer Spot handler 로직을 `Server/Play/Handlers/play_timer_spot_handlers.hpp`로 분리한 뒤 Play target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-132451-4183451`
  - 의미: Play timer Spot handler helper 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로로
    YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2가 통과했다. runner 출력은
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-D3 route bridge yield client scenario 추가 뒤 client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-144300-193620`
  - 의미: YD-D3는 stream connector command가 Session gateway route bridge를 거쳐 `play-b` target
    Spot handler로 들어가고, 해당 Spot handler가 `yield()` 중 probe command를 먼저 처리한 뒤 원래
    continuation을 재개하는 marker 순서를 검증한다. runner 출력은 `scenario YD-D3 passed`,
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_framework zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: bound session typed send가 erased serializer 경로로 빠지던 문제를 framework typed JSON
    serializer 경로로 고치고, YD-D4 actor push-yield scenario와 session stream binding 변경 뒤 target
    build가 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-151241-249658`
  - 의미: YD-D4는 stream session actor relay로 들어온 actor handler가 `yield()` 뒤 bound session
    push를 보내고, session-a의 bound connector만 `ActorPushNotify`를 받으며 session-b unbound connector는
    받지 않는 범위를 검증한다. runner 출력은 `scenario YD-D4 passed`,
    `yield-dispatch track-a-d result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-E1 timeout cleanup Spot handler와 client scenario 추가 뒤 Play/Client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-152535-269343`
  - 의미: YD-E1은 stream connector command가 Session gateway route bridge를 거쳐 Spot handler로 들어가고,
    delay service reply보다 짧은 timeout으로 `yield()` timeout marker를 남긴 뒤 같은 Spot이
    `ProbeCommand`를 처리하는 cleanup 범위를 검증한다. runner 출력은 `scenario YD-E1 passed`,
    `yield-dispatch track-a-e1 result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-152918-275717`
  - 의미: YD-E4 static gate가 `/yield` HTTP trigger/client 사용, Play Spot/Entry Spot handler 밖
    `yield()` 사용, client scenario thin helper 사용을 금지하고, full client가 실제 stream connector를
    만들며 각 YD scenario가 connector 참조를 직접 받는지 검사한다. runner 출력은
    `yield-dispatch track-a-e1 result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: YD-E3 shutdown/recovery DTO, session relay, Play evidence file append, client shutdown mode 추가 뒤 Play/Session/Client target이 통과했다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-153557-290422`
  - 의미: YD-E3는 shutdown-wait client가 pending yield를 시작하고, runner가 play-a evidence log에서
    `yield-released` marker를 확인한 뒤 play-a에 SIGTERM을 보내며, client가 request timeout이
    아닌 public connector error를 받는지 검증한다. 이후 같은 spot rid로 play-a를 재시작하고 recovery
    probe marker를 확인한다. 같은 실행에서 YD-E4 static gate도 통과한다. runner 출력은
    `yield-dispatch shutdown wait result=passed`, `yield-dispatch shutdown recovery result=passed`,
    `yield-dispatch e2e result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-154031-302809`
  - 의미: YD-D1 local topology gate가 full client 실행 뒤 `play-a.evidence.log`에서 Track A/B/C/E1
    local marker를 직접 확인한다. `hold-completed`, `yield-completed`, `worker-yield-completed`,
    `actor-yield-completed`, `timer-yield-completed`, `timeout-yield-completed`가 모두 `rid=play-a`로
    남아야 통과한다. 같은 실행에서 YD-E3/E4도 통과했다. runner 출력은 `scenario YD-D1 passed`,
    `yield-dispatch e2e result=passed`다.
- 2026-06-30: `./framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-154843-321810`
  - 의미: YD-B2는 같은 stream connector session에서 actor A yield request와 같은 actor A fast request를 보내고,
    fast request가 actor yield continuation/completion 뒤에 실행되는 marker를 검증한다. 같은 실행에서
    YD-D1, YD-E3, YD-E4도 통과했다. runner 출력은 `scenario YD-B2 passed`,
    `scenario YD-D1 passed`, `yield-dispatch e2e result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_session`
  - 결과: 통과
  - 의미: session role option 읽기를 `Server/Session/Support/session_support.hpp`로 분리한 뒤 Session
    target이 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-155403-331046`
  - 의미: session support 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로와
    shutdown/recovery orchestration이 통과했다. runner 출력은 `scenario YD-D1 passed`,
    `yield-dispatch e2e result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_registry`
  - 결과: 통과
  - 의미: registry role option 읽기를 `Server/Registry/Support/registry_support.hpp`로 분리한 뒤 Registry
    target이 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-155653-336186`
  - 의미: registry support 분리 뒤에도 registry -> delay -> play -> session -> client 전체 경로와
    shutdown/recovery orchestration이 통과했다. runner 출력은 `scenario YD-D1 passed`,
    `yield-dispatch e2e result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_delay zlink_cpp_e2e_yield_dispatch_play`
  - 결과: 통과
  - 의미: delay option/evidence support와 play option support 분리 뒤 Delay/Play target이 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-160112-346042`
  - 의미: delay option/evidence support와 play option support 분리 뒤에도 registry -> delay -> play ->
    session -> client 전체 경로와 shutdown/recovery orchestration이 통과했다. `delay-a.evidence.log`와
    `delay-b.evidence.log`에는 `delay-started`/`delay-completed` marker가 남는다. runner 출력은
    `scenario YD-D1 passed`, `yield-dispatch e2e result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_registry zlink_cpp_e2e_yield_dispatch_delay zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: shared contract에 E2 cancellation DTO와 JSON 매핑을 추가한 뒤 YieldDispatch role/client target이
    모두 통과했다. E2 handler/scenario는 public yield cancellation token gap으로 남긴다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-160702-358191`
  - 의미: E2 cancellation DTO 추가 뒤에도 registry -> delay -> play -> session -> client 전체 경로와
    shutdown/recovery orchestration이 통과했다. runner 출력은 `scenario YD-D1 passed`,
    `yield-dispatch e2e result=passed`다.
- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_client -j 4`
  - 결과: 통과
  - 의미: YD-A3 상태 정정 후 client target build를 확인했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-182120-718488`
  - 의미: A3를 `.NET`과 같은 완료 조건으로 정리한 뒤에도 YD-A1~YD-A4, YD-B1~YD-B3,
    YD-C1~YD-C3, YD-D1~YD-D4, YD-E1, YD-E3, YD-E4 static gate가 통과했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260630-210837-1224464`
  - 의미: YD-C3 actor-yield 중 timer-fast half를 `.NET`처럼 같은 connector로 보내도록 시도했지만,
    `ActorYieldReq`가 완료된 뒤 `TimerStartMsg`가 들어가 `YD-C3A marker order mismatch`로 실패했다.
    이 시점에는 observer connector를 쓰는 구현을 당시의 임시 근거로 유지했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-211459-1245708`
  - 의미: YD-A1~YD-E4 기존 gate와 YD-E5 `yield-dispatch-report.json` 생성/검증이 통과했다.
    runner 출력은 `scenario YD-E5 passed`, `yield-dispatch e2e result=passed`다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client`
  - 결과: 통과
  - 의미: 현재 public send/stream write call surface가 fire-and-forget `submit()` 중심으로 정리된 뒤에도
    Session relay와 client scenario target이 빌드된다. E2E client는 send 반환값 대신 뒤따르는
    evidence wait와 request reply로 성공을 검증한다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-002028-1556638`
  - 의미: public send `submit()` 반환값 의존을 제거한 뒤 YD-A1~YD-D4, YD-E1, YD-E3, YD-E4 static
    gate와 YD-E5 `yield-dispatch-report.json` 생성/검증이 다시 통과했다. 당시 report는 YD-C3를
    임시 상태로, YD-E2를 공개 계약 미정 항목으로 유지했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260701-003005-1569332`
  - 의미: YD-C3 actor-yield 중 timer-fast half를 현재 C++ stream connector의 같은 connector로 다시
    시도했지만, `ActorYieldReq` 완료 뒤 `TimerStartMsg`가 처리되어 `YD-C3A marker order mismatch`로
    실패했다. 이 시점에는 observer connector 기반 구현을 당시의 임시 근거로 유지했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 실패
  - 로그: `logs/20260701-013205-1681063`
  - 의미: YD-C3 임시 상태를 풀기 위해 session actor request relay를 `relay_request(...).async()` 대기
    대신 `relay(payload).submit()`으로 바꾸고 actor-yield 중 timer-fast half를 같은 connector로 다시
    시도했다. 이 변경은 YD-C3 이전의 YD-B1에서 `ActorFastReq` reply가 client로 돌아오지 않아 실패했다.
    당시 C++ `session_actor_t::relay(payload)`는 `.NET` `RelayAsync`처럼 request reply를 stream에
    deferred로 돌려주는 표면이 아니므로, request relay는 `relay_request(...).async()` 뒤
    `stream.reply_packet(...)`을 기다려야 했다. 이 우회는 폐기했고 당시 observer connector 기반 임시
    상태를 유지했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-013458-1684685`
  - 의미: 폐기한 session actor relay 우회를 되돌린 뒤 당시 observer connector 기반 YD-C3 임시 상태,
    YD-E2 공개 계약 미정 항목, YD-E5 report 검증을 유지한 상태로 YieldDispatch full runner가 다시 통과했다.
- 2026-07-01: `cmake --build framework/languages/cpp/build --target zlink_framework zlink_cpp_e2e_yield_dispatch_registry zlink_cpp_e2e_yield_dispatch_delay zlink_cpp_e2e_yield_dispatch_play zlink_cpp_e2e_yield_dispatch_session zlink_cpp_e2e_yield_dispatch_client -j 4`
  - 결과: 통과
  - 의미: stream host concurrent dispatch, per-dispatch reply context, YD-C3 same-connector scenario 변경 뒤 framework와 YieldDispatch role/client target이 빌드됐다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-051400-81089`
  - 의미: YD-C3 actor-yield 중 timer-fast half를 `.NET`처럼 같은 stream connector에서 실행해 통과했다. YD-A1~YD-D4, YD-E1, YD-E3, YD-E4 static gate, YD-E5 report 생성도 함께 통과했다. 당시 report는 YD-C3를 passed, YD-E2를 공개 계약 미정 항목으로 기록했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-051641-83562`
  - 의미: YD-C3 report status를 `passed`로 갱신한 뒤에도 full runner와 YD-E5 report 생성이 통과했다. 당시 report는 YD-C3를 passed, YD-E2를 공개 계약 미정 항목으로 기록했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-073830-56972`
  - 의미: 현재 트리에서 YD-A1~YD-D4, YD-E1, YD-E3, YD-E4 static gate, YD-E5 report 생성이 다시
    통과했다. runner 출력은 `scenario YD-D1 passed`, `scenario YD-E5 passed`,
    `yield-dispatch e2e result=passed`를 포함하고, 당시 report는 YD-E2를 public contract 미정 항목으로 기록했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-131002-82309`
  - 의미: runner readiness와 client connect timeout을 로컬 기준 3초로 낮춘 뒤에도 full runner가 통과했다.
    이 기록은 당시 observer 기반 B1/B3/C3 검증으로 얻은 과거 통과 증거다. 최신 완료 근거는 아래의
    같은 session-a connector 검증 결과를 사용한다. runner 출력은 `scenario YD-D1 passed`,
    `scenario YD-E5 passed`, `yield-dispatch e2e result=passed`를 포함하고, 당시 report는 YD-E2를 public
    contract 미정 항목으로 기록했다.
- 2026-07-01: `timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-135643-4880`
  - 의미: route request backend의 reply 대기 전체 직렬화를 제거한 뒤 B1/B3/C3가 session-b observer
    없이 같은 session-a connector에서 통과했다. D3는 `yield-released` evidence를 확인한 뒤 probe를
    보내 route bridge scheduling 차이 없이 yield turn release를 검증한다. runner 출력은
    `scenario YD-D1 passed`, `scenario YD-E5 passed`, `yield-dispatch e2e result=passed`를 포함하고,
    당시 report는 YD-E2를 public contract 미정 항목으로 기록했다.
- 2026-07-01: `ZLINK_CPP_E2E_BUILD_DIR=framework/languages/cpp/build timeout 420s framework/languages/cpp/e2e/YieldDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260701-191329-11276`
  - 의미: native route backend가 request reply 대기 전체를 직렬화하지 않고 ROUTER/spot bridge submit
    구간만 짧게 보호하도록 수정한 뒤에도 full runner가 통과했다. B1/B3/C3는 session-b observer 없이
    같은 session-a connector에서 통과하며, B1 evidence 순서는 `actor-yield-started`,
    `actor-yield-released`, `actor-fast-started`, `actor-fast-completed`, `actor-yield-resumed`,
    `actor-yield-completed`로 확인했다. local readiness와 stream connect timeout은 3초 기준이고,
    YD-E3 shutdown path의 session route spot request는 2초 안에 public `remote_error`를 올린다.
    runner 출력은 `scenario YD-D1 passed`, `scenario YD-E5 passed`,
    `yield-dispatch e2e result=passed`를 포함하고, 당시 report는 YD-E2를 public contract 미정 항목으로 기록했다.

## 후속 관리

Config 8 자체에는 남은 `partial` 또는 `gap` 항목을 두지 않는다. 이후 C++ E2E/sample 전체 점검은
상위 계획인 `framework/doc/plan/framework-cpp-e2e-sample-gap-closure-plan.ko.md`에서 관리한다.
