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
| `DeliveryDispatch.sln` | `framework/languages/cpp/CMakeLists.txt` | build | not-needed | C++는 solution 파일 대신 CMake target 묶음으로 role executable을 정의한다. |
| `Server/Dispatch/DeliveryDispatch.Server.Dispatch.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | C++의 dispatch API와 dispatch center target이 .NET Dispatch server 책임을 나눠 맡는다. |
| `Server/Dispatch/DispatchServerHostFactory.cs` | `Server/DispatchApi/main.cpp`; `Server/DispatchCenter/main.cpp` | server-role | done | HTTP API host와 dispatch worker host 구성이 C++에서 두 executable로 분리되어 있다. |
| `Server/Dispatch/DispatchWorkQueue.cs` | `Server/DispatchCenter/main.cpp` | infrastructure | done | C++ dispatch center role 내부 queue/worker loop가 대응한다. |
| `Server/Dispatch/DispatchWorker.cs` | `Server/DispatchCenter/main.cpp` | support | partial | courier offer, timeout 재배정, tracking status publish는 대응하지만 최신 .NET처럼 courier gateway/actor/session 경로로 제안하지 않는다. |
| `Server/Dispatch/Program.cs` | `Server/DispatchApi/main.cpp`; `Server/DispatchCenter/main.cpp` | server-role | done | C++는 HTTP edge와 worker를 별도 role executable로 실행한다. |
| `Server/CustomerGateway/DeliveryDispatch.Server.CustomerGateway.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | partial | C++는 별도 CustomerGateway target이 없고 `Server/Session`과 `Server/Tracking`으로 고객 session/tracking 책임을 나눠 구현한다. |
| `Server/CustomerGateway/CustomerGatewayHostFactory.cs` | `Server/Session/main.cpp`; `Server/Tracking/main.cpp` | server-role | partial | customer stream bind와 tracking notification이 분리되어 있지만 .NET의 CustomerGateway 단일 role과 같은 actor/session binding 구조는 아니다. |
| `Server/CustomerGateway/CustomerSession.cs` | `Server/Session/main.cpp` | session | partial | 고객 stream session과 delivery subscription은 대응하지만 bound customer actor session 구조는 단순화되어 있다. |
| `Server/CustomerGateway/CustomerActor.cs` | `Server/Tracking/Actors/customer_actor.hpp` | actor | partial | customer actor snapshot은 대응하지만 .NET CustomerGateway role의 bound session actor와 같은 runtime 배치는 아니다. |
| `Server/CustomerGateway/CustomerActorDirectory.cs` | `Server/Tracking/Spots/EntrySpot/customer_entry_spot.hpp`; `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp` | infrastructure | partial | customer id/delivery id lookup은 tracking role 내부 상태로 대응한다. |
| `Server/CustomerGateway/CustomerGatewayHandlers.cs` | `Server/Tracking/Handlers/tracking_handlers.hpp` | handler | partial | delivery status notification path는 대응하지만 CustomerGateway channel/actor split은 남아 있다. |
| `Server/CustomerGateway/SubscribeDeliverySessionHandler.cs` | `Server/Session/main.cpp` | handler | done | stream `SubscribeDelivery` 요청을 받아 고객별 delivery subscription에 연결한다. |
| `Server/CustomerGateway/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/Spots/EntrySpot/customer_entry_spot.hpp` | spot | partial | customer entry spot 판정은 header로 분리했지만 별도 CustomerGateway spot mesh role은 없다. |
| `Server/CustomerGateway/Spots/EntrySpot/Handlers/SubscribeDeliveryActorHandler.cs` | `Server/Tracking/Handlers/tracking_handlers.hpp` | handler | partial | customer actor subscribe 의미는 tracking handler로 대응하지만 actor-bound session 경로는 단순화되어 있다. |
| `Server/CustomerGateway/Program.cs` | `Server/Session/main.cpp`; `Server/Tracking/main.cpp` | server-role | partial | 고객 gateway role은 C++에서 session/tracking role로 나뉘어 실행된다. |
| `Server/CourierSession/DeliveryDispatch.Server.CourierSession.csproj` | not-implemented | build | gap | C++에는 courier stream session 전용 executable이 없다. |
| `Server/CourierSession/CourierSessionHostFactory.cs` | not-implemented | server-role | gap | courier stream connection과 actor bind host가 아직 없다. |
| `Server/CourierSession/CourierSession.cs` | not-implemented | session | gap | `.NET`처럼 courier-a/courier-b 각각의 stream session을 받지 않는다. |
| `Server/CourierSession/BindCourierSessionHandler.cs` | not-implemented | handler | gap | `BindCourierSession` public stream flow가 C++에는 아직 없다. |
| `Server/CourierSession/Program.cs` | not-implemented | server-role | gap | courier session role executable이 없다. |
| `Server/CourierGateway/DeliveryDispatch.Server.CourierGateway.csproj` | not-implemented | build | gap | C++에는 courier gateway 전용 executable이 없다. |
| `Server/CourierGateway/CourierGatewayHostFactory.cs` | not-implemented | server-role | gap | courier id를 actor node rid/session route로 해석하는 gateway host가 없다. |
| `Server/CourierGateway/CourierDirectory.cs` | not-implemented | infrastructure | gap | courier id별 actor node/session route directory가 없다. |
| `Server/CourierGateway/CourierGatewayHandlers.cs` | `Server/Courier/main.cpp` | handler | partial | C++ courier role이 offer accept/timeout을 직접 처리하므로 최신 .NET의 gateway -> actor node route 경로와 다르다. |
| `Server/CourierGateway/Program.cs` | not-implemented | server-role | gap | courier gateway role executable이 없다. |
| `Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj` | not-implemented | build | gap | C++에는 courier actor node target이 없다. |
| `Server/CourierActorNode/NodeHostFactory.cs` | not-implemented | server-role | gap | courier actor node host와 node rid별 placement가 없다. |
| `Server/CourierActorNode/ActorDirectory.cs` | not-implemented | infrastructure | gap | courier actor directory가 없다. |
| `Server/CourierActorNode/CourierActor.cs` | not-implemented | actor | gap | courier actor가 session route로 offer를 push하고 decision을 받는 경로가 없다. |
| `Server/CourierActorNode/RouteHandlers.cs` | not-implemented | handler | gap | courier actor node route handlers가 없다. |
| `Server/CourierActorNode/Spots/EntrySpot/EntrySpot.cs` | not-implemented | spot | gap | courier entry spot이 없다. |
| `Server/CourierActorNode/Spots/EntrySpot/Handlers/BindCourierSessionActorHandler.cs` | not-implemented | handler | gap | courier actor session bind handler가 없다. |
| `Server/CourierActorNode/Spots/EntrySpot/Handlers/CourierDecisionActorHandler.cs` | not-implemented | handler | gap | courier decision actor handler가 없다. |
| `Server/CourierActorNode/Program.cs` | not-implemented | server-role | gap | courier actor node role executable이 없다. |

## Scenario 대응

| Scenario | C++ 대응 | 상태 | 비고 |
|----------|----------|------|------|
| registry discovery readiness | `Probe/main.cpp`; `run_e2e.sh` | done | Tracking route readiness를 client 실행 전에 확인한다. |
| successful delivery | `Client/delivery_dispatch_client_scenario.hpp` | partial | 고객 session 상태 push는 검증한다. 최신 .NET의 courier stream offer/decision 경로는 아직 없다. |
| reassigned delivery | `Client/delivery_dispatch_client_scenario.hpp`; `Server/Courier/main.cpp`; `Server/DispatchCenter/main.cpp` | partial | courier A timeout 뒤 courier B 재배정 상태는 검증한다. 최신 .NET처럼 courier-a/courier-b stream session과 actor node를 통한 offer push는 아직 없다. |
| server evidence self-check | `Server/DispatchApi/main.cpp`; `Server/Configuration/evidence_store.hpp` | done | `/self-check/assert`가 evidence log를 검증한다. |
| message-flow evidence | role `main.cpp`; `run_e2e.sh` | done | role별 trace log에 message-flow 기록이 남는지 확인한다. |

## 검증

- 2026-06-30: `./framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/evidence.log`, `logs/flow-*.log`
  - 의미: probe readiness, successful delivery stream push, reassignment marker, server evidence self-check,
    role별 message-flow evidence가 같은 runner에서 검증된다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 1차 실패
  - 원인: stream client DTO에 JSON payload hook이 없어 `SubscribeDelivery` payload의 `deliveryId`가
    비어 session role에 도착했다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: `Shared/Contracts/messages.hpp`에 typed JSON stream payload hook을 추가한 뒤 customer
    subscription, delivery status push, reassignment, server evidence self-check가 다시 통과했다.
    다만 최신 .NET 샘플의 courier session/gateway/actor-node 경로는 아직 gap이다.
