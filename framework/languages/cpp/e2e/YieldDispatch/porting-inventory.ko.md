# C++ YieldDispatch .NET 기준 포팅 inventory

기준 구현: `framework/languages/dotnet/e2e/YieldDispatch`

현재 C++ `YieldDispatch`는 role target, Track A YD-A1~YD-A4, Track B YD-B1~YD-B3, Track C YD-C1~YD-C3, Track D YD-D2 실행 코드를
갖췄다. 이 inventory는
`.NET` Config 8 파일을 기준으로 C++에 만들어야 할 역할, shared contract, client scenario, support 파일을
계속 고정한다. Config 8은 stream connector client request가 session gateway를 거쳐 play node의
Spot/Entry Spot handler까지 도달해야 하므로, HTTP trigger나 direct Spot/route test driver로 대체하지
않는다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | metadata | done | C++ YieldDispatch 로그와 임시 산출물 제외 규칙을 추가했다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 아직 구현되지 않은 scenario와 public gap을 완료로 과장하지 않는다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | partial | registry, delay A/B, play A/B, session A/B, client를 빌드하고 readiness와 정적 검사를 수행한다. 이 runner는 E2E target만 필요하므로 configure 때 C++ sample target은 끈다. YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2 runner는 반복 통과 증거가 있다. 남은 YD-D/E scenario와 파일 분리가 있어 config 완료 판정은 보류한다. |
| `Shared/YieldDispatch.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | role/client target 묶음은 추가됐다. shared는 header로 포함된다. |
| `Shared/Messages.cs` | `Shared/yield_dispatch_contracts.hpp` | shared | partial | Track A용 ensure/evidence/delay/hold/yield/worker/probe DTO, actor binding/yield/fast/join-yield DTO, timer start/stop command, D2 remote Spot yield DTO와 JSON 매핑이 있다. stream connector typed request/reply가 실제 JSON payload를 쓰도록 `to_stream_payload`/`from_stream_payload` hook도 제공한다. shutdown DTO는 남아 있다. |
| `Client/GlobalUsings.cs` | not-needed | client | not-needed | C++에는 대응 파일이 필요 없다. |
| `Client/YieldDispatch.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | client target이 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client | partial | stream connector로 YD-A1~YD-A4, YD-B1~YD-B3, YD-C1~YD-C3, YD-D2 request를 시작하고 검증한다. client option 파싱, evidence wait, assertion, actor context는 support/scenario-support header로 분리됐다. YD-A2/A4는 yielded request와 probe/evidence 관측을 별도 connector로 분리한다. YD-B2와 YD-C3 일부는 같은 actor binding/spot에 붙은 별도 connector를 쓰므로 같은 stream session 증거는 아직 아니다. D2 owner evidence 관측은 public evidence snapshot polling을 쓴다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_options.hpp` | support | partial | `ZLINK_CPP_E2E_STREAM_ENDPOINT`를 읽고 stream connector option을 만든다. `.NET`의 scenario 선택, session B endpoint, shutdown flow option은 남아 있다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/scenario_assert.hpp`; `Client/Support/evidence_wait.hpp` | support | partial | marker 순서 검증, request line fragment 검증, result error 출력, evidence snapshot polling helper가 있다. `.NET` 수준의 독립 scenario assertion API로 완전히 분리하는 작업은 남아 있다. |
| `Client/Scenarios/YieldActorScenarioContext.cs` | `Client/Scenarios/yield_actor_scenario_context.hpp` | scenario-support | partial | actor binding scenario가 공유하는 spot rid, actor A, actor B context를 분리했다. session relay scenario 자체는 아직 gap이다. |
| `Client/Scenarios/YdA1BasicTerminatorScenario.cs` | `Client/Scenarios/yd_a1_basic_terminator_scenario.hpp` | scenario | done | YD-A1 flow는 별도 scenario header로 분리했고 runner로 통과했다. |
| `Client/Scenarios/YdA2YieldTerminatorScenario.cs` | `Client/Scenarios/yd_a2_yield_terminator_scenario.hpp` | scenario | done | YD-A2 flow는 별도 scenario header로 분리했고 runner로 통과했다. yielded request와 probe/evidence 관측은 별도 connector로 분리한다. |
| `Client/Scenarios/YdA3ContinuationContextScenario.cs` | `Client/Scenarios/yd_a3_continuation_context_scenario.hpp` | scenario | partial | request id/spot rid/correlation id 보존은 별도 scenario header에서 runner로 통과했다. metadata 보존 검증은 public contract gap으로 남긴다. |
| `Client/Scenarios/YdA4WorkerYieldScenario.cs` | `Client/Scenarios/yd_a4_worker_yield_scenario.hpp` | scenario | done | public worker call `yield()` flow는 별도 scenario header로 분리했고 runner로 통과했다. yielded request와 probe/evidence 관측은 별도 connector로 분리한다. |
| `Client/Scenarios/YdB1OtherActorProgressScenario.cs` | `Client/Scenarios/yd_b1_other_actor_progress_scenario.hpp` | scenario | done | actor A yield 중 actor B 진행은 별도 scenario header에서 runner로 통과했다. |
| `Client/Scenarios/YdB2SameActorReentryScenario.cs` | `Client/Scenarios/yd_b2_same_actor_reentry_scenario.hpp` | scenario | partial | 같은 actor mailbox 재진입 금지 marker 순서는 별도 scenario header에서 runner로 통과했다. fast request가 같은 actor ref에 bound된 별도 connector에서 들어가므로 `.NET`의 같은 stream session 증거와 같지는 않다. |
| `Client/Scenarios/YdB3ActorJoinYieldScenario.cs` | `Client/Scenarios/yd_b3_actor_join_yield_scenario.hpp` | scenario | done | 공통 문서가 허용한 `JoinEntrySpot` terminator 경로로 actor join yield 중 actor B fast request 진행을 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC1TimerIsolationScenario.cs` | `Client/Scenarios/yd_c1_timer_isolation_scenario.hpp` | scenario | done | timer A yield 중 timer B fast tick 진행은 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC2TimerReentryScenario.cs` | `Client/Scenarios/yd_c2_timer_reentry_scenario.hpp` | scenario | done | 같은 timer yield 중 다음 tick 재진입 금지 marker 순서는 별도 scenario header에서 runner로 검증한다. |
| `Client/Scenarios/YdC3ActorTimerIsolationScenario.cs` | `Client/Scenarios/yd_c3_actor_timer_isolation_scenario.hpp` | scenario | partial | actor yield 중 timer fast tick 진행과 timer yield 중 actor fast request 진행은 별도 scenario header에서 runner로 검증한다. actor-yield 중 timer-fast half는 observer connector를 사용하므로 `.NET`의 같은 stream session 증거와 같지는 않다. |
| `Client/Scenarios/YdD2RemoteSpotYieldScenario.cs` | `Client/Scenarios/yd_d2_remote_spot_yield_scenario.hpp` | scenario | done | remote Spot yield continuation 소유권 검증은 별도 scenario header에서 runner로 통과했다. owner evidence 관측은 public evidence snapshot polling을 쓴다. |
| `Client/Scenarios/YdD3RouteBridgeYieldScenario.cs` | `Client/Scenarios/yd_d3_route_bridge_yield_scenario.hpp` | scenario | gap | route bridge 경유 Spot handler yield 검증이 필요하다. |
| `Client/Scenarios/YdD4SessionRelayActorYieldScenario.cs` | `Client/Scenarios/yd_d4_session_relay_actor_yield_scenario.hpp` | scenario | gap | session actor relay와 bound session push 검증이 필요하다. |
| `Client/Scenarios/YdE1TimeoutScenario.cs` | `Client/Scenarios/yd_e1_timeout_scenario.hpp` | scenario | gap | yield timeout cleanup 검증이 필요하다. |
| `Client/Scenarios/YdE2CancellationScenario.cs` | `Client/Scenarios/yd_e2_cancellation_scenario.hpp` | scenario | gap | yield cancellation cleanup 검증이 필요하다. |
| `Client/Scenarios/ShutdownYieldScenario.cs` | `Client/Scenarios/shutdown_yield_scenario.hpp` | scenario | gap | pending yield 중 play node shutdown과 recovery 검증이 필요하다. |
| `Server/Registry/YieldDispatch.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | registry target이 있다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | partial | registry role entrypoint가 있다. discovery registry host 구성은 factory header로 분리했다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/registry_host_factory.hpp` | server-role | partial | registry pub/router endpoint, dispatch trace, health endpoint 구성을 factory header로 분리했고 Registry target과 runner로 검증했다. `.NET`의 CLI option record 수준 분리는 남아 있다. |
| `Server/Delay/YieldDispatch.Delay.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | delay service target이 있다. |
| `Server/Delay/Program.cs` | `Server/Delay/main.cpp` | server-role | partial | delay service entrypoint가 있다. delay host 구성은 factory header로 분리했다. |
| `Server/Delay/DelayHostFactory.cs` | `Server/Delay/delay_host_factory.hpp` | server-role | partial | delay channel server, handler registration, dispatch trace, health endpoint 구성을 factory header로 분리했고 Delay target과 runner로 검증했다. `.NET`의 CLI option record 수준 분리는 남아 있다. |
| `Server/Delay/DelayHandler.cs` | `Server/Delay/Handlers/delay_handler.hpp` | handler | partial | delayed reply handler를 `Handlers/`로 분리했고 Delay target과 runner로 검증했다. `.NET`처럼 evidence entry를 남기는 support는 아직 없다. |
| `Server/Delay/DelaySupport.cs` | `Server/Delay/Support/delay_support.hpp` | support | partial | delay node state를 `Support/`로 분리했다. `.NET`의 DelayOptions/EvidenceStore 수준 option/evidence support는 남아 있다. |
| `Server/Play/YieldDispatch.Play.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | play node target이 있다. |
| `Server/Play/Program.cs` | `Server/Play/main.cpp` | server-role | partial | play node entrypoint가 있다. play host 구성은 factory header로 분리했다. |
| `Server/Play/PlayHostFactory.cs` | `Server/Play/play_host_factory.hpp` | server-role | partial | play node의 control route, spot mesh, delay client, dispatch trace, health endpoint 구성을 factory header로 분리했고 Play target과 runner로 검증했다. `.NET`의 CLI option record와 evidence HTTP snapshot 수준 분리는 남아 있다. |
| `Server/Play/PlaySupport.cs` | `Server/Play/Support/play_support.hpp` | support | partial | evidence store는 support header로 분리했다. `.NET`의 PlayOptions/NodeOptions 수준 option support 분리는 남아 있다. |
| `Server/Play/Spots/PlaySpotRuntime.cs` | `Server/Play/Spots/play_spot_runtime.hpp` | spot | partial | request id별 evidence와 `YieldProbeSpot` runtime을 분리했고 YD-A/B/C/D2 runner 증거가 있다. basic/timer Spot handler 로직은 handler header로 분리했다. shutdown 범위와 remote handler 세부 파일 분리는 남아 있다. |
| `Server/Play/Spots/PlaySpotTypes.cs` | `Server/Play/Spots/play_spot_types.hpp` | spot | partial / gap | actor 타입, actor factory, timer handler/state 타입을 분리했다. shutdown 타입은 남아 있다. |
| `Server/Play/Handlers/PlayBasicSpotHandlers.cs` | `Server/Play/Handlers/play_basic_spot_handlers.hpp`; `Server/Play/Spots/play_spot_runtime.hpp` | handler | partial | YD-A1~YD-A4용 hold/yield/worker/probe handler 로직을 `Handlers/` helper header로 분리했고 Play target과 runner로 검증했다. C++ spot handler 등록 방식 때문에 `YieldProbeSpot`의 멤버 entrypoint는 runtime header에 남아 있다. |
| `Server/Play/Handlers/PlayActorHandlers.cs` | `Server/Play/Handlers/play_actor_handlers.hpp` | handler | partial | YD-B1/B2 actor yield/fast handler와 YD-B3 actor join-yield handler를 분리했고 runner 증거가 있다. D4 actor push-yield handler는 아직 gap이다. |
| `Server/Play/Handlers/PlayTimerSpotHandlers.cs` | `Server/Play/Handlers/play_timer_spot_handlers.hpp`; `Server/Play/Spots/play_spot_runtime.hpp` | handler | partial | YD-C1~YD-C3 timer start/stop command와 timer yield/fast/next handler 로직을 `Handlers/` helper header로 분리했고 Play target과 runner로 검증했다. C++ timer handler 등록 방식 때문에 `YieldProbeSpot`의 timer entrypoint는 runtime header에 남아 있다. |
| `Server/Play/Handlers/PlayRemoteSpotHandlers.cs` | `Server/Play/Spots/play_spot_runtime.hpp` | handler | partial | YD-D2 remote Spot handler는 runtime header 안의 `YieldProbeSpot`에 있다. public `spot_context_t::request_to(...).yield()`로 target Spot reply를 기다린다. 별도 remote handler helper 파일 분리는 남아 있다. |
| `Server/Play/Handlers/PlayControlHandlers.cs` | `Server/Play/Handlers/play_control_handlers.hpp` | handler | partial | EnsureSpot, BindYieldActors, Evidence, EvidenceWait control route handler를 분리했고 runner로 검증했다. D3 route bridge control handler는 아직 gap이다. |
| `Server/Play/Handlers/PlayFailureSpotHandlers.cs` | `Server/Play/Handlers/play_failure_spot_handlers.hpp` | handler | gap | shutdown, timeout, cancellation 관련 handler가 필요하다. |
| `Server/Session/YieldDispatch.Session.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | session gateway target이 있다. |
| `Server/Session/Program.cs` | `Server/Session/main.cpp` | server-role | partial | session gateway entrypoint가 있다. stream session 등록과 mesh 구성은 factory header로 분리했다. |
| `Server/Session/SessionHostFactory.cs` | `Server/Session/session_host_factory.hpp` | server-role | partial | session gateway의 route client, spot mesh, stream node, dispatch trace, health endpoint 구성을 factory header로 분리했고 Session target과 runner로 검증했다. `.NET`의 CLI option record와 evidence/session-side spot 타입 수준 분리는 남아 있다. |
| `Server/Session/Support/SessionSpotTypes.cs` | `Server/Session/Support/session_spot_types.hpp` | support | gap | session side spot/session DTO가 필요하다. |
| `Server/Session/Support/SessionSupport.cs` | `Server/Session/Support/session_support.hpp` | support | gap | session role option/evidence helper가 필요하다. |
| `Server/Session/Support/YieldSession.cs` | `Server/Session/Support/yield_session.hpp` | session | partial | connector session implementation을 support header로 분리했고 Session target과 runner로 검증했다. D4와 shutdown relay 범위는 남아 있다. |
| `Server/Session/Support/YieldSessionRelay.cs` | `Server/Session/Support/yield_session.hpp` | session | partial | Ensure/evidence/Track A relay 코드, actor bind/relay 코드, timer command relay 코드가 있고, session host의 spot mesh를 통해 public Spot route dispatch가 통과한다. worker yield relay와 D2 remote Spot yield relay도 포함한다. D4와 shutdown relay 범위는 남아 있다. |
| `Server/Session/Support/YieldShutdownRelay.cs` | `Server/Session/Support/yield_shutdown_relay.hpp` | session | gap | shutdown scenario relay가 필요하다. |

## Scenario 대응

| Scenario ID | C++ 대응 | 상태 |
|-------------|----------|------|
| `YD-A1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-A2` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-A3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | partial |
| `YD-A4` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-B1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-B2` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | partial |
| `YD-B3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp` | done |
| `YD-C1` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-C2` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-C3` | `Client/main.cpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | partial |
| `YD-D1` | Track A/B/C local topology runner slice | gap |
| `YD-D2` | `Client/Scenarios/yd_d2_remote_spot_yield_scenario.hpp`; `Server/Play/main.cpp`; `Server/Session/main.cpp`; `Server/Session/Support/yield_session.hpp`; `Server/Delay/main.cpp` | done |
| `YD-D3` | `Client/Scenarios/yd_d3_route_bridge_yield_scenario.hpp`; `Server/Play/Handlers/play_control_handlers.hpp` | gap |
| `YD-D4` | `Client/Scenarios/yd_d4_session_relay_actor_yield_scenario.hpp`; `Server/Session/Support/yield_session_relay.hpp` | gap |
| `YD-E1` | `Client/Scenarios/yd_e1_timeout_scenario.hpp`; `Server/Play/Handlers/play_failure_spot_handlers.hpp` | gap |
| `YD-E2` | `Client/Scenarios/yd_e2_cancellation_scenario.hpp`; `Server/Play/Handlers/play_failure_spot_handlers.hpp` | gap |
| `YD-E3` | `Client/Scenarios/shutdown_yield_scenario.hpp`; `run_e2e.sh` | gap |
| `YD-E4` | `run_e2e.sh` static checks | gap |
| `YD-E5` | cross-language report aggregation | gap |

## 검증

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
  - 로그: `logs/20260630-091449-3391388`
  - 의미: YD-A1~YD-A4 Track A가 통과한다. A3는 `.NET`과 같은 부분 범위로 request id, spot rid,
    correlation id 보존을 검증하고 metadata 보존은 public contract gap으로 남긴다. A4는 public
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
    진행과 timer yield 중 actor fast request 진행을 모두 marker 순서로 검증한다. actor-yield 중
    timer-fast half는 observer connector를 쓰므로 `.NET`의 같은 stream session 증거와 같지는 않다.
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

## 다음 작업 순서

1. Client/Server 코드를 목표 폴더 구조(`Client/Scenarios`, `Server/Play/Handlers` 등)로 나눈다.
2. YD-D/E의 route bridge, session relay, shutdown, timeout, cancellation scenario를 순서대로 추가한다.
3. 빠른 반복 실행에서 드물게 보이는 startup `errno=105`/`errno=22`와 빌드 산출물 손상은 runner 환경 안정화 후보로 분리한다.
