# DeliveryDispatch Kotlin sample porting inventory

이 문서는 `.NET` DeliveryDispatch 샘플과 공통 샘플 문서의 요구를 Kotlin 샘플 구현에 매핑한다.
`bin`, `obj`, `build`, `logs` 산출물은 제외하고 실제 소스와 runner만 대조한다.

## 1. .NET 기준 파일 대응

| 기준 | Kotlin 대응 | 분류 | 상태 | 비고 |
|------|-------------|------|------|------|
| `.NET: DeliveryDispatch.csproj` | `build.gradle.kts`, `standalone.settings.gradle.kts` | build-root | done | Kotlin multi-project 루트와 standalone 실행 설정이다. |
| `.NET: README.ko.md` | `README.md`, `framework/doc/framework/kotlin/guide/samples/deliverydispatch-sample.ko.md` | sample-doc | done | 언어별 guide는 `framework/doc/` 아래에 둔다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | Gradle installDist, port allocation, readiness, cleanup, flow-log 검증을 수행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Windows 실행 entry다. |
| `.NET: Client/DeliveryDispatch.Client.csproj` | `Client/build.gradle.kts` | build-client | done | client app 의존성을 정의한다. |
| `.NET: Client/Program.cs` | `Client/src/main/kotlin/.../client/Program.kt` | client-entry | done | stream connector와 HTTP client를 만들고 scenario를 실행한다. |
| `.NET: Client/DeliveryDispatchClientScenario.cs` | `Client/src/main/kotlin/.../client/DeliveryDispatchClientScenario.kt` | client-scenario | done | 성공 배차, 재배차, server evidence self-check를 검증한다. |
| `.NET: Probe/DeliveryDispatch.Probe.csproj` | `Probe/build.gradle.kts` | build-probe | done | probe app 의존성을 정의한다. |
| `.NET: Probe/Program.cs` | `Probe/src/main/kotlin/.../probe/Program.kt` | probe | done | registry topology와 Tracking active probe를 확인한다. |
| `.NET: Shared/DeliveryDispatch.Shared.csproj` | `Shared/build.gradle.kts` | build-shared | done | shared contract project다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/src/main/kotlin/.../shared/contracts/Messages.kt` | shared-contract | done | Kotlin은 enum 대신 문자열 상수로 status 값을 둔다. |
| `.NET: Server/Configuration/DeliveryDispatch.Server.Configuration.csproj` | `Server/Configuration/build.gradle.kts` | build-configuration | done | 서버 공통 설정 project다. |
| `.NET: Server/Configuration/EvidenceStore.cs` | `Server/Configuration/src/main/kotlin/.../configuration/EvidenceStore.kt` | evidence-store | done | 상태 event와 self-check evidence를 파일 기반으로 공유한다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | framework message-flow trace 설정 | flow-log | not-needed | Kotlin/Java runtime의 `configureDispatch { traceLogFile(...) }`로 같은 증거 로그를 남긴다. |
| `.NET: Server/Configuration/SampleNames.cs` | `Server/Configuration/src/main/kotlin/.../configuration/SampleNames.kt` | sample-names | done | channel, role, courier id 이름을 모은다. |
| `.NET: Server/Configuration/SampleTopology.cs` | `Server/Configuration/src/main/kotlin/.../configuration/SampleTopology.kt` | topology | done | runner가 주입한 endpoint system property를 읽는다. |
| `.NET: Server/Courier/DeliveryDispatch.Server.Courier.csproj` | `Server/Courier/build.gradle.kts` | build-courier | done | courier role app 의존성을 정의한다. |
| `.NET: Server/Courier/Program.cs` | `Server/Courier/src/main/kotlin/.../courier/Program.kt` | courier-entry | done | `--courier`, `--mode` 인자를 받아 role을 시작한다. |
| `.NET: Server/Courier/OfferDeliveryHandler.cs` | `Server/Courier/src/main/kotlin/.../courier/handlers/OfferDeliveryHandler.kt` | courier-handler | done | accept와 timeout-reassign mode를 public handler로 구현한다. |
| `.NET: Server/DispatchApi/DeliveryDispatch.Server.DispatchApi.csproj` | `Server/DispatchApi/build.gradle.kts` | build-dispatch-api | done | Spring MVC suspend handler에 필요한 reactor coroutine runtime을 포함한다. |
| `.NET: Server/DispatchApi/Program.cs` | `Server/DispatchApi/src/main/kotlin/.../dispatchapi/Program.kt`, `DispatchApiApplication.kt` | dispatch-api-entry | done | HTTP boundary와 dispatch channel client를 시작한다. |
| `.NET: Server/DispatchCenter/DeliveryDispatch.Server.DispatchCenter.csproj` | `Server/DispatchCenter/build.gradle.kts` | build-dispatch-center | done | dispatch center app 의존성을 정의한다. |
| `.NET: Server/DispatchCenter/Program.cs` | `Server/DispatchCenter/src/main/kotlin/.../dispatchcenter/Program.kt`, `DispatchCenterApplication.kt` | dispatch-center-entry | done | dispatch channel server, courier clients, tracking client, worker를 시작한다. |
| `.NET: Server/DispatchCenter/AssignDeliveryHandler.cs` | `Server/DispatchCenter/src/main/kotlin/.../dispatchcenter/handlers/AssignDeliveryHandler.kt` | dispatch-handler | done | 배차 요청을 queue에 넣고 접수 결과를 반환한다. |
| `.NET: Server/DispatchCenter/DispatchWorkQueue.cs` | `Server/DispatchCenter/src/main/kotlin/.../dispatchcenter/DispatchWorkQueue.kt` | dispatch-worker | done | worker가 처리할 delivery work를 보관한다. |
| `.NET: Server/DispatchCenter/DispatchWorker.cs` | `Server/DispatchCenter/src/main/kotlin/.../dispatchcenter/DispatchWorker.kt` | dispatch-worker | done | 성공 배차와 timeout 재배차 흐름을 실행한다. |
| `.NET: Server/Registry/DeliveryDispatch.Server.Registry.csproj` | `Server/Registry/build.gradle.kts` | build-registry | done | registry role app 의존성을 정의한다. |
| `.NET: Server/Registry/Program.cs` | `Server/Registry/src/main/kotlin/.../registry/Program.kt`, `RegistryApplication.kt` | registry-entry | done | embedded registry host를 시작한다. |
| `.NET: Server/Session/DeliveryDispatch.Server.Session.csproj` | `Server/Session/build.gradle.kts` | build-session | done | session role app 의존성을 정의한다. |
| `.NET: Server/Session/Program.cs` | `Server/Session/src/main/kotlin/.../session/Program.kt`, `SessionApplication.kt` | session-entry | done | stream node, fanout subscriber, tracking client, spot mesh를 시작한다. |
| `.NET: Server/Session/CustomerSession.cs` | `Server/Session/src/main/kotlin/.../session/sessions/CustomerSession.kt` | stream-session | done | customer actor binding과 stream request dispatch를 맡는다. |
| `.NET: Server/Session/CustomerSessionDirectory.cs` | `Server/Session/src/main/kotlin/.../session/sessions/CustomerSessionDirectory.kt` | session-store | done | delivery subscription별 bound session을 찾는다. |
| `.NET: Server/Session/DeliveryStatusFanoutHandler.cs` | `Server/Session/src/main/kotlin/.../session/sessions/handlers/DeliveryStatusFanoutHandler.kt` | fanout-handler | done | status fanout을 stream client push로 연결한다. |
| `.NET: Server/Tracking/DeliveryDispatch.Server.Tracking.csproj` | `Server/Tracking/build.gradle.kts` | build-tracking | done | tracking role app 의존성을 정의한다. |
| `.NET: Server/Tracking/Program.cs` | `Server/Tracking/src/main/kotlin/.../tracking/Program.kt`, `TrackingApplication.kt` | tracking-entry | done | tracking channel, status fanout publisher, spot mesh를 시작한다. |
| `.NET: Server/Tracking/CustomerActor.cs` | `Server/Tracking/src/main/kotlin/.../tracking/actors/CustomerActor.kt` | actor | done | customer actor를 표현한다. |
| `.NET: Server/Tracking/Handlers.cs` | `Server/Tracking/src/main/kotlin/.../tracking/handlers/*.kt` | tracking-handlers | done | actor ensure, delivery join, status changed handlers로 분리했다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliverySpotDirectory.cs` | `Server/Tracking/src/main/kotlin/.../spots/deliverytrackingspot/DeliverySpotDirectory.kt` | spot-directory | done | delivery id별 spot 참조를 관리한다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliveryTrackingSpot.cs` | `Server/Tracking/src/main/kotlin/.../spots/deliverytrackingspot/DeliveryTrackingSpot.kt` | spot | done | delivery별 customer actor join을 소유한다. |
| `.NET: Server/Tracking/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/src/main/kotlin/.../spots/entryspot/CustomerEntrySpot.kt` | entry-spot | done | customer actor의 entry spot 역할을 맡는다. |

## 2. 공통 메시지 계약 대응

| 기준 | Kotlin 대응 | 분류 | 상태 | 비고 |
|------|-------------|------|------|------|
| `common: CreateDeliveryRequest` | `CreateDeliveryRequest(deliveryId, customerId, pickupAddress, dropoffAddress)` | shared-contract | done | HTTP boundary payload다. |
| `common: CreateDeliveryResponse` | `DeliveryCreated(deliveryId)` | shared-contract | done | Kotlin 샘플 이름은 기존 shared contract와 맞춰 `DeliveryCreated`를 쓴다. |
| `common: SubscribeDelivery` | `SubscribeDelivery(deliveryId)` | shared-contract | done | stream session request다. |
| `common: SubscribeDeliveryAccepted` | `SubscribeDeliveryAccepted(deliveryId)` | shared-contract | done | subscription 접수 응답이다. |
| `common: DeliveryStatusNotify` | `DeliveryStatusNotify(deliveryId, status, courierId, occurredAtUnixMs)` | shared-contract | done | Kotlin은 `DateTimeOffset` 대신 epoch milliseconds를 사용한다. |
| `common: AssignDelivery` | `AssignDelivery(deliveryId, customerId, pickupAddress, dropoffAddress)` | shared-contract | done | DispatchApi에서 DispatchCenter로 보낸다. |
| `common: AssignDeliveryResult` | `AssignDeliveryResult(deliveryId, courierId)` | shared-contract | done | 접수된 courier id를 포함한다. |
| `common: OfferDelivery` | `OfferDelivery(deliveryId, pickupAddress, dropoffAddress)` | shared-contract | done | DispatchCenter에서 Courier로 보낸다. |
| `common: OfferDeliveryResult` | `OfferDeliveryResult(deliveryId, courierId, accepted, reason)` | shared-contract | done | timeout-reassign은 accepted=false로 표현한다. |
| `common: DeliveryStatusChanged` | `DeliveryStatusChanged(deliveryId, status, courierId, occurredAtUnixMs)` | shared-contract | done | Tracking 상태 기록 요청이다. |
| `common: DeliveryStatusAck` | `DeliveryStatusAck(deliveryId, status)` | shared-contract | done | Tracking 처리 응답이다. |
| `common: EnsureCustomerActor` | `EnsureCustomerActor(customerId)` | shared-contract | done | Session에서 Tracking actor를 보장한다. |
| `common: CustomerActorEnsured` | `CustomerActorEnsured(customerId, actor)` | shared-contract | done | ActorRefSnapshot을 포함한다. |
| `common: SubscribeCustomerToDelivery` | `SubscribeCustomerToDelivery(customerId, deliveryId)` | shared-contract | done | customer actor를 delivery spot에 join시킨다. |
| `common: CustomerDeliverySubscribed` | `CustomerDeliverySubscribed(customerId, deliveryId)` | shared-contract | done | join 완료 응답이다. |
| `common: DeliverySpotCreate` | `DeliverySpotCreate(deliveryId)` | shared-contract | done | Tracking 내부 spot 생성 payload다. |
| `common: DeliverySpotCreated` | `DeliverySpotCreated(deliveryId)` | shared-contract | done | spot 생성 결과다. |
| `common: DeliverySpotJoin` | `DeliverySpotJoin(deliveryId, customerId)` | shared-contract | done | customer actor join payload다. |
| `common: DeliverySpotJoined` | `DeliverySpotJoined(deliveryId, customerId)` | shared-contract | done | join 결과다. |
| `common: Status Assigned/Accepted/Reassigned/PickedUp/Delivered` | `DeliveryStatuses` constants | shared-contract | done | self-check가 순서를 검증한다. |
| `.NET-only: CourierDecision` | 없음 | shared-contract | not-needed | 현재 Kotlin 흐름은 courier response를 바로 처리하므로 별도 message가 필요 없다. |
| `.NET-only: ReassignDelivery` | 없음 | shared-contract | not-needed | Kotlin worker가 재배정 상태와 다음 courier 제안을 직접 실행한다. |

## 3. 공통 검증 흐름 대응

| 기준 | Kotlin 대응 | 분류 | 상태 | 비고 |
|------|-------------|------|------|------|
| `common: Registry를 먼저 시작한다` | `run_sample.sh` | validation | done | `Server/Registry`를 첫 프로세스로 시작한다. |
| `common: Tracking readiness` | `run_sample.sh`, `Probe/src/main/kotlin/.../Program.kt` | validation | done | Tracking 시작 뒤 probe가 topology를 확인한다. |
| `common: Session stream server` | `Server/Session`, `run_sample.sh` | validation | done | stream endpoint를 열고 client subscription을 받는다. |
| `common: Courier A timeout mode` | `run_sample.sh`, `CourierOptions.kt` | validation | done | `--courier courier-a --mode timeout-reassign`로 시작한다. |
| `common: Courier B accept mode` | `run_sample.sh`, `CourierOptions.kt` | validation | done | `--courier courier-b --mode accept`로 시작한다. |
| `common: Dispatch Center` | `Server/DispatchCenter`, `run_sample.sh` | validation | done | dispatch worker와 channel handler를 시작한다. |
| `common: Dispatch API` | `Server/DispatchApi`, `run_sample.sh` | validation | done | HTTP `/deliveries`와 `/self-check/assert` endpoint를 준비한다. |
| `common: topology=ready` | `Probe/src/main/kotlin/.../Program.kt`, `build/sample-logs/probe.log` | validation | done | `topology=ready` marker를 출력한다. |
| `common: delivery-success status order` | `Client/src/main/kotlin/.../DeliveryDispatchClientScenario.kt` | validation | done | `Assigned -> Accepted -> PickedUp -> Delivered`와 courier-a를 검증한다. |
| `common: delivery-reassign status order` | `Client/src/main/kotlin/.../DeliveryDispatchClientScenario.kt` | validation | done | `Assigned -> Reassigned -> Accepted -> Delivered`와 courier-b를 검증한다. |
| `common: server evidence check` | `Server/DispatchApi/.../ServerAssertionHandler.kt`, `EvidenceStore.kt` | validation | done | 두 delivery의 상태 기록을 self-check 한다. |
| `common: message flow log` | `run_sample.sh`, `logs/flow-*.log` | validation | done | runner가 flow log 존재를 확인한다. |
| `common: 전체 sample runner 포함` | `framework/languages/java/samples/run_samples.sh` | validation | done | Java/Kotlin sample loop가 `DeliveryDispatch`를 실행한다. |

## 4. gap

현재 DeliveryDispatch Kotlin 샘플에는 `pending` 또는 `gap` 항목이 없다.
