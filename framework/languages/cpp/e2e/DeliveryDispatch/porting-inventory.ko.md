# C++ DeliveryDispatch .NET 기준 포팅 inventory

기준 구현: `framework/languages/dotnet/samples/DeliveryDispatch`

이 문서는 `.NET DeliveryDispatch` 샘플 파일이 C++ E2E에서 어디에 대응되는지 기록한다. C++ 구현은
샘플과 같은 public framework API를 사용하지만, `framework/languages/cpp/e2e/DeliveryDispatch` 아래에
별도 CMake target과 runner를 둔다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 실행 산출물을 제외한다. |
| `DeliveryDispatch.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | CMake target 묶음이 역할별 project 참조를 대신한다. |
| `README.ko.md` | `README.ko.md`; `feature-map.ko.md` | docs | done | 실행 방법과 검증 항목을 C++ 기준으로 기록한다. |
| `run_sample.sh` | `run_e2e.sh` | runner | done | registry, tracking, session, courier A/B, dispatch center, dispatch API, probe, client를 실행한다. |
| `run_sample.ps1` | not-needed | runner | not-needed | 이번 C++ E2E runner는 Linux shell 경로만 제공한다. |
| `logs/.gitignore` | `logs/.gitignore` | config | done | role flow/evidence log를 제외한다. |
| `logs/*.log` | `logs/*.log` | evidence | done | 실행 중 생성되는 flow/evidence log가 대응한다. |
| `Shared/DeliveryDispatch.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | shared header를 각 target include 경로로 사용한다. |
| `Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.hpp` | shared | done | delivery request, reply, notify, evidence DTO가 대응한다. |
| `Server/Configuration/DeliveryDispatch.Server.Configuration.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | configuration headers를 각 role target에서 include한다. |
| `Server/Configuration/EvidenceStore.cs` | `Server/Configuration/evidence_store.hpp` | infrastructure | done | 상태 evidence append/read/sequence 검증을 담당한다. |
| `Server/Configuration/SampleFlowLog.cs` | `sample_log_dir.hpp`; role `main.cpp` trace option | infrastructure | done | role별 message-flow log 파일 경로를 제공한다. |
| `Server/Configuration/SampleNames.cs` | `Server/Configuration/sample_names.hpp` | configuration | done | channel, route, actor, spot 이름이 대응한다. |
| `Server/Configuration/SampleTopology.cs` | `Server/Configuration/sample_topology.hpp` | configuration | done | role endpoint 환경 변수를 해석한다. |
| `Server/Registry/DeliveryDispatch.Server.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | registry target이 대응한다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp` | server-role | done | C++ registry role은 main에서 host를 구성한다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-role | done | registry role 진입점이다. |
| `Server/DispatchApi/DeliveryDispatch.Server.DispatchApi.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | dispatch API target이 대응한다. |
| `Server/DispatchApi/DispatchApiHostFactory.cs` | `Server/DispatchApi/main.cpp` | server-role | done | C++ dispatch API role은 main에서 HTTP host와 channel client를 구성한다. |
| `Server/DispatchApi/Program.cs` | `Server/DispatchApi/main.cpp` | server-role | done | `/deliveries`, `/self-check/assert` HTTP endpoint가 대응한다. |
| `Server/DispatchCenter/DeliveryDispatch.Server.DispatchCenter.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | dispatch center target이 대응한다. |
| `Server/DispatchCenter/DispatchCenterHostFactory.cs` | `Server/DispatchCenter/main.cpp` | server-role | done | C++ dispatch center role은 main에서 handler group과 client channels를 구성한다. |
| `Server/DispatchCenter/AssignDeliveryHandler.cs` | `Server/DispatchCenter/main.cpp` | handler | done | assign request를 받아 dispatch worker로 전달한다. |
| `Server/DispatchCenter/DispatchWorkQueue.cs` | `Server/DispatchCenter/main.cpp` | infrastructure | done | C++ role 내부 queue와 worker loop가 대응한다. |
| `Server/DispatchCenter/DispatchWorker.cs` | `Server/DispatchCenter/main.cpp` | support | done | courier offer, timeout 재배정, tracking status publish가 대응한다. |
| `Server/DispatchCenter/Program.cs` | `Server/DispatchCenter/main.cpp` | server-role | done | dispatch center role 진입점이다. |
| `Server/Courier/DeliveryDispatch.Server.Courier.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | courier target이 대응한다. |
| `Server/Courier/CourierServerHostFactory.cs` | `Server/Courier/main.cpp` | server-role | done | C++ courier role은 main에서 courier id와 mode env를 해석한다. |
| `Server/Courier/OfferDeliveryHandler.cs` | `Server/Courier/main.cpp` | handler | done | courier mode별 offer accept/timeout 동작이 대응한다. |
| `Server/Courier/Program.cs` | `Server/Courier/main.cpp` | server-role | done | courier A/B는 같은 executable에 env mode를 달리 주어 실행한다. |
| `Server/Tracking/DeliveryDispatch.Server.Tracking.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | tracking target이 대응한다. |
| `Server/Tracking/TrackingServerHostFactory.cs` | `Server/Tracking/main.cpp` | server-role | done | C++ tracking role은 main에서 tracking route, status publisher, handler group을 구성한다. |
| `Server/Tracking/CustomerActor.cs` | `Server/Tracking/Actors/customer_actor.hpp` | actor | done | customer actor id와 actor snapshot이 대응한다. |
| `Server/Tracking/Handlers.cs` | `Server/Tracking/Handlers/tracking_handlers.hpp` | handler | done | actor ensure, delivery subscription, status changed handler가 대응한다. |
| `Server/Tracking/Program.cs` | `Server/Tracking/main.cpp` | server-role | done | tracking role 진입점이다. |
| `Server/Tracking/Spots/DeliveryTrackingSpot/DeliverySpotDirectory.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp` | infrastructure | done | delivery id별 tracking spot lookup을 담당한다. |
| `Server/Tracking/Spots/DeliveryTrackingSpot/DeliveryTrackingSpot.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_tracking_spot.hpp` | spot | done | customer join과 delivery status history 기록이 대응한다. |
| `Server/Tracking/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/Spots/EntrySpot/customer_entry_spot.hpp` | spot | done | customer actor entry join 판정을 담당한다. |
| `Server/Session/DeliveryDispatch.Server.Session.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | session target이 대응한다. |
| `Server/Session/SessionServerHostFactory.cs` | `Server/Session/main.cpp` | server-role | done | C++ session role은 main에서 stream node, route client, fanout subscriber를 구성한다. |
| `Server/Session/CustomerSession.cs` | `Server/Session/main.cpp` | infrastructure | done | stream session과 subscription state가 대응한다. |
| `Server/Session/CustomerSessionDirectory.cs` | `Server/Session/main.cpp` | infrastructure | done | customer session lookup이 role 내부 상태로 대응한다. |
| `Server/Session/DeliveryStatusFanoutHandler.cs` | `Server/Session/main.cpp` | handler | done | fanout event를 subscribed stream client에 push한다. |
| `Server/Session/SubscribeDeliveryHandler.cs` | `Server/Session/main.cpp` | handler | done | stream subscribe 요청을 받아 고객별 session 상태에 연결한다. |
| `Server/Session/Program.cs` | `Server/Session/main.cpp` | server-role | done | session stream role 진입점이다. |
| `Probe/DeliveryDispatch.Probe.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | probe target이 대응한다. |
| `Probe/DeliveryDispatchReadinessCheck.cs` | `Probe/main.cpp` | probe | done | tracking route discovery readiness request가 대응한다. |
| `Probe/ProbeHostFactory.cs` | `Probe/main.cpp` | probe | done | C++ probe는 main에서 transient host와 route client를 구성한다. |
| `Probe/ProbeRunner.cs` | `Probe/main.cpp` | probe | done | C++ probe service가 readiness check 실행과 exit code를 담당한다. |
| `Probe/Program.cs` | `Probe/main.cpp` | probe | done | registry discovery readiness를 검증한다. |
| `Client/DeliveryDispatch.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | client target이 대응한다. |
| `Client/DeliveryDispatchClientScenario.cs` | `Client/delivery_dispatch_client_scenario.hpp` | scenario | done | successful delivery, reassignment, self-check marker를 검증한다. |
| `Client/Program.cs` | `Client/main.cpp` | client | done | api-url, stream endpoint를 받아 scenario를 실행한다. |

## Scenario 대응

| Scenario | C++ 대응 | 상태 | 비고 |
|----------|----------|------|------|
| registry discovery readiness | `Probe/main.cpp`; `run_e2e.sh` | done | Tracking route readiness를 client 실행 전에 확인한다. |
| successful delivery | `Client/delivery_dispatch_client_scenario.hpp` | done | `delivery-success` 상태 순서를 stream push로 검증한다. |
| reassigned delivery | `Client/delivery_dispatch_client_scenario.hpp`; `Server/Courier/main.cpp`; `Server/DispatchCenter/main.cpp` | done | courier A timeout 뒤 courier B 재배정을 검증한다. |
| server evidence self-check | `Server/DispatchApi/main.cpp`; `Server/Configuration/evidence_store.hpp` | done | `/self-check/assert`가 evidence log를 검증한다. |
| message-flow evidence | role `main.cpp`; `run_e2e.sh` | done | role별 trace log에 message-flow 기록이 남는지 확인한다. |

## 검증

- 2026-06-30: `./framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/evidence.log`, `logs/flow-*.log`
  - 의미: probe readiness, successful delivery stream push, reassignment marker, server evidence self-check,
    role별 message-flow evidence가 같은 runner에서 검증된다.
