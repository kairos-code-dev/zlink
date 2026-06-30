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
| `run_sample.sh` | `run_e2e.sh` | runner | done | registry, tracking, customer gateway, courier session, courier gateway, courier actor node 2개, dispatch center, dispatch API, probe, client를 실행한다. customer stream과 courier stream endpoint도 분리한다. |
| `run_sample.ps1` | not-needed | runner | not-needed | 이번 C++ E2E runner는 Linux shell 경로만 제공한다. |
| `logs/.gitignore` | `logs/.gitignore` | config | done | role flow/evidence log를 제외한다. |
| `logs/*.log` | `logs/*.log` | evidence | done | 실행 중 생성되는 flow/evidence log가 대응한다. |
| `Shared/DeliveryDispatch.Shared.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | shared header를 각 target include 경로로 사용한다. |
| `Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.hpp` | shared | done | delivery request, reply, notify, evidence, `BindCourierReq`, `BindCourierSessionReq`, `EnsureCourierActorReq`, offer notify, courier decision DTO가 대응한다. CustomerGateway와 Courier 쪽 actor-bound session 경로도 대응한다. |
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
| `Server/DispatchCenter/DispatchWorker.cs` | `Server/DispatchCenter/main.cpp` | support | done | courier offer를 CourierGateway channel로 보내고, timeout 재배정과 tracking status publish를 처리한다. |
| `Server/DispatchCenter/Program.cs` | `Server/DispatchCenter/main.cpp` | server-role | done | dispatch center role 진입점이다. |
| `Server/Courier/DeliveryDispatch.Server.Courier.csproj` | `Server/Session/main.cpp`; legacy `Server/Courier/main.cpp` | build | not-needed | 최신 .NET 샘플에는 별도 Courier role이 없다. C++ runner는 Session role의 courier stream 경로를 사용하고, legacy target은 직접 응답 fallback으로만 남아 있다. |
| `Server/Courier/CourierServerHostFactory.cs` | `Server/Session/main.cpp`; legacy `Server/Courier/main.cpp` | server-role | not-needed | 최신 .NET 샘플의 courier client-visible 흐름은 stream session bind와 decision send로 검증한다. |
| `Server/Courier/OfferDeliveryHandler.cs` | `Server/Session/main.cpp`; legacy `Server/Courier/main.cpp` | handler | not-needed | 최신 runner는 `Server/Session`의 stream offer handler가 `OfferDeliveryNotify` push와 decision wait를 담당한다. |
| `Server/Courier/Program.cs` | `Server/Session/main.cpp`; legacy `Server/Courier/main.cpp` | server-role | not-needed | 최신 runner는 courier A/B process를 실행하지 않는다. |
| `Server/Tracking/DeliveryDispatch.Server.Tracking.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | tracking target이 대응한다. |
| `Server/Tracking/TrackingServerHostFactory.cs` | `Server/Tracking/main.cpp` | server-role | done | C++ tracking role은 main에서 tracking route, status publisher, handler group을 구성한다. |
| `Server/Tracking/CustomerActor.cs` | `Server/Tracking/Actors/customer_actor.hpp` | actor | done | customer actor id와 actor snapshot이 대응한다. |
| `Server/Tracking/Handlers.cs` | `Server/Tracking/Handlers/tracking_handlers.hpp` | handler | done | actor ensure, delivery subscription, status changed handler가 대응한다. |
| `Server/Tracking/Program.cs` | `Server/Tracking/main.cpp` | server-role | done | tracking role 진입점이다. |
| `Server/Tracking/Spots/DeliveryTrackingSpot/DeliverySpotDirectory.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp` | infrastructure | done | delivery id별 tracking spot lookup을 담당한다. |
| `Server/Tracking/Spots/DeliveryTrackingSpot/DeliveryTrackingSpot.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_tracking_spot.hpp` | spot | done | customer join과 delivery status history 기록이 대응한다. |
| `Server/Tracking/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/Spots/EntrySpot/customer_entry_spot.hpp` | spot | done | customer actor entry join 판정을 담당한다. |
| `Server/Session/DeliveryDispatch.Server.Session.csproj` | `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp`; legacy `Server/Session/main.cpp` | build | not-needed | 최신 `.NET` 샘플은 CustomerGateway와 CourierSession으로 분리되어 있어 C++ runner도 새 target 두 개를 실행한다. legacy Session target은 호환 경로로 남아 있다. |
| `Server/Session/SessionServerHostFactory.cs` | `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp` | server-role | not-needed | 통합 Session host 책임은 최신 분리 role로 나뉘었다. |
| `Server/Session/CustomerSession.cs` | `Server/CustomerGateway/main.cpp` | infrastructure | done | customer stream session과 subscription 처리가 CustomerGateway target으로 분리됐다. |
| `Server/Session/CustomerSessionDirectory.cs` | `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp` | infrastructure | done | customer subscription lookup과 courier session lookup이 각 role 내부 상태로 분리됐다. |
| `Server/Session/DeliveryStatusFanoutHandler.cs` | `Server/CustomerGateway/main.cpp` | handler | done | fanout event를 subscribed customer stream client에 push한다. |
| `Server/Session/SubscribeDeliveryHandler.cs` | `Server/CustomerGateway/main.cpp` | handler | done | stream `SubscribeDelivery` 요청을 받아 고객별 delivery subscription에 연결한다. |
| `Server/Session/Program.cs` | `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp` | server-role | not-needed | 최신 runner는 통합 Session role 대신 CustomerGateway와 CourierSession role을 실행한다. |
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
| `Server/Dispatch/DispatchWorker.cs` | `Server/DispatchCenter/main.cpp` | support | done | courier offer는 CourierGateway를 거쳐 CourierActorNode actor handler로 전달되고, timeout 재배정과 tracking status publish가 대응한다. |
| `Server/Dispatch/Program.cs` | `Server/DispatchApi/main.cpp`; `Server/DispatchCenter/main.cpp` | server-role | done | C++는 HTTP edge와 worker를 별도 role executable로 실행한다. |
| `Server/CustomerGateway/DeliveryDispatch.Server.CustomerGateway.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | CustomerGateway target이 대응한다. |
| `Server/CustomerGateway/CustomerGatewayHostFactory.cs` | `Server/CustomerGateway/main.cpp` | server-role | done | customer stream bind, customer actor spot mesh, status fanout subscriber를 구성한다. |
| `Server/CustomerGateway/CustomerSession.cs` | `Server/CustomerGateway/main.cpp` | session | done | 고객 stream session이 customer actor를 생성/바인드하고 stream을 `actor_gateway_t`에 연결한다. 알 수 없는 packet은 bound actor로 relay한다. |
| `Server/CustomerGateway/CustomerActor.cs` | `Server/CustomerGateway/main.cpp`; `Server/Tracking/Actors/customer_actor.hpp` | actor | done | CustomerGateway role의 runtime actor와 Tracking role의 snapshot actor가 각각 대응한다. |
| `Server/CustomerGateway/CustomerActorDirectory.cs` | `Server/CustomerGateway/main.cpp`; `Server/Tracking/Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp` | infrastructure | done | CustomerGateway는 delivery id별 customer actor lookup을 갖고, Tracking은 delivery status history lookup을 갖는다. |
| `Server/CustomerGateway/CustomerGatewayHandlers.cs` | `Server/CustomerGateway/main.cpp`; `Server/Tracking/Handlers/tracking_handlers.hpp` | handler | done | status fanout handler가 customer actor bound session으로 push하고, Tracking handler는 상태 기록과 fanout publish를 담당한다. |
| `Server/CustomerGateway/SubscribeDeliverySessionHandler.cs` | `Server/CustomerGateway/main.cpp` | handler | done | stream `SubscribeDelivery` 요청을 받아 customer actor를 bind하고 actor entry spot의 subscribe handler로 relay한다. |
| `Server/CustomerGateway/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/CustomerGateway/main.cpp`; `Server/Tracking/Spots/EntrySpot/customer_entry_spot.hpp` | spot | done | CustomerGateway entry spot은 actor subscribe handler를 등록하고, Tracking entry spot은 readiness/snapshot join 판정을 담당한다. |
| `Server/CustomerGateway/Spots/EntrySpot/Handlers/SubscribeDeliveryActorHandler.cs` | `Server/CustomerGateway/main.cpp` | handler | done | customer actor subscribe request가 delivery id별 customer binding을 저장하고 `SubscribeDeliveryRes`를 반환한다. |
| `Server/CustomerGateway/Program.cs` | `Server/CustomerGateway/main.cpp` | server-role | done | customer gateway role 진입점이다. |
| `Server/CourierSession/DeliveryDispatch.Server.CourierSession.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | CourierSession target이 대응한다. |
| `Server/CourierSession/CourierSessionHostFactory.cs` | `Server/CourierSession/main.cpp` | server-role | done | courier stream endpoint와 courier actor spot mesh bridge를 별도 role로 구성한다. |
| `Server/CourierSession/CourierSession.cs` | `Server/CourierSession/main.cpp` | session | done | courier-a/courier-b stream session을 받고 actor ref를 public `session_actor_manager_t`에 bind한 뒤 stream을 `actor_gateway_t`에 연결한다. decision packet은 bound actor로 relay한다. |
| `Server/CourierSession/BindCourierSessionHandler.cs` | `Server/CourierSession/main.cpp` | handler | done | `BindCourierSessionReq`를 public stream request로 받아 CourierGateway에서 actor ref를 얻고, actor entry spot의 bind handler로 relay한다. |
| `Server/CourierSession/Program.cs` | `Server/CourierSession/main.cpp` | server-role | done | courier session role 진입점이다. |
| `Server/CourierGateway/DeliveryDispatch.Server.CourierGateway.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | courier gateway target이 대응한다. |
| `Server/CourierGateway/CourierGatewayHostFactory.cs` | `Server/CourierGateway/main.cpp` | server-role | done | courier route server와 actor node route client를 구성한다. |
| `Server/CourierGateway/CourierDirectory.cs` | `Server/CourierGateway/main.cpp` | infrastructure | done | courier id별 actor node/session route binding을 role 내부 directory에 저장한다. |
| `Server/CourierGateway/CourierGatewayHandlers.cs` | `Server/CourierGateway/main.cpp` | handler | done | bind 요청은 actor node ensure route로 보내고, offer는 저장된 actor node rid로 라우팅한다. |
| `Server/CourierGateway/Program.cs` | `Server/CourierGateway/main.cpp` | server-role | done | courier gateway role 진입점이다. |
| `Server/CourierActorNode/DeliveryDispatch.Server.CourierActorNode.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | courier actor node target이 대응한다. runner가 node 1/2를 같은 executable의 다른 routing id로 실행한다. |
| `Server/CourierActorNode/NodeHostFactory.cs` | `Server/CourierActorNode/main.cpp` | server-role | done | courier actor node route mesh와 courier actor spot mesh를 구성한다. |
| `Server/CourierActorNode/ActorDirectory.cs` | `Server/CourierActorNode/main.cpp` | infrastructure | done | C++ role은 route handler의 actor manager와 decision directory로 courier actor 위치와 pending decision을 관리한다. |
| `Server/CourierActorNode/CourierActor.cs` | `Server/CourierActorNode/main.cpp` | actor | done | Courier actor가 actor context를 보유하고, entry spot handler가 bound session으로 `OfferDeliveryNotify`를 push한다. |
| `Server/CourierActorNode/RouteHandlers.cs` | `Server/CourierActorNode/main.cpp` | handler | done | `EnsureCourierActorReq`와 `OfferDeliveryReq` route handler가 대응한다. |
| `Server/CourierActorNode/Spots/EntrySpot/EntrySpot.cs` | `Server/CourierActorNode/main.cpp` | spot | done | courier entry spot이 actor join과 actor packet handler 등록을 담당한다. |
| `Server/CourierActorNode/Spots/EntrySpot/Handlers/BindCourierSessionActorHandler.cs` | `Server/CourierActorNode/main.cpp` | handler | done | `BindCourierSessionReq` actor request가 actor/session binding 확인 reply를 반환한다. |
| `Server/CourierActorNode/Spots/EntrySpot/Handlers/CourierDecisionActorHandler.cs` | `Server/CourierActorNode/main.cpp` | handler | done | `CourierDecisionMsg` actor send가 pending offer decision을 완료한다. |
| `Server/CourierActorNode/Program.cs` | `Server/CourierActorNode/main.cpp` | server-role | done | courier actor node role 진입점이다. |

## Scenario 대응

| Scenario | C++ 대응 | 상태 | 비고 |
|----------|----------|------|------|
| registry discovery readiness | `Probe/main.cpp`; `run_e2e.sh` | done | Tracking route readiness를 client 실행 전에 확인한다. |
| successful delivery | `Client/delivery_dispatch_client_scenario.hpp`; `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp`; `Server/CourierGateway/main.cpp`; `Server/CourierActorNode/main.cpp` | done | 고객 session 상태 push와 courier-a gateway/actor-node/courier-session offer/decision 경로를 검증한다. |
| reassigned delivery | `Client/delivery_dispatch_client_scenario.hpp`; `Server/CustomerGateway/main.cpp`; `Server/CourierSession/main.cpp`; `Server/CourierGateway/main.cpp`; `Server/CourierActorNode/main.cpp`; `Server/DispatchCenter/main.cpp` | done | courier-a stream offer timeout 뒤 gateway가 courier-b actor node로 재요청하고, courier-b stream offer/decision과 재배정 상태를 검증한다. |
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
    이 시점에는 최신 .NET 샘플의 courier session/gateway/actor-node 경로가 아직 gap이었다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: customer stream 1개와 courier stream 2개를 열고, `BindCourierSessionReq`,
    `OfferDeliveryNotify`, `CourierDecisionMsg` 흐름으로 successful delivery와 reassignment를 검증한다.
    이 시점의 남은 차이는 별도 CourierGateway/CourierActorNode role split이었다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: CourierGateway와 CourierActorNode 1/2 target을 runner에 추가한 뒤에도 successful delivery,
    reassignment, server evidence self-check가 통과했다. runner는 `flow-courier-gateway.log`,
    `flow-delivery-courier-node-1.log`, `flow-delivery-courier-node-2.log`의 `message flow`도 확인한다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: CustomerGateway와 CourierSession target을 추가하고 customer stream endpoint와 courier stream
    endpoint를 분리한 뒤에도 successful delivery, reassignment, server evidence self-check가 통과했다.
    runner는 `flow-customer-gateway.log`와 `flow-courier-session.log`의 `message flow`도 확인한다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: CourierSession이 actor ref를 bind하고 CourierActorNode entry spot actor handler가 bound session으로
    `OfferDeliveryNotify`를 push하는 경로로 successful delivery, reassignment, server evidence self-check가
    통과했다. runner는 courier session과 courier actor node 1/2 flow log의 `message flow`도 확인한다.
- 2026-06-30: `timeout 420s framework/languages/cpp/e2e/DeliveryDispatch/run_e2e.sh`
  - 결과: 통과
  - 의미: CustomerGateway가 customer actor를 stream에 bind하고 status fanout handler가 actor bound
    session으로 `DeliveryStatusNotify`를 push하는 경로까지 successful delivery, reassignment,
    server evidence self-check가 통과했다.
