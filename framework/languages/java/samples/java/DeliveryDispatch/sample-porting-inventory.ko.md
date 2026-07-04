# Java DeliveryDispatch sample porting inventory

이 문서는 `framework/doc/plan/framework-java-sample-dotnet-porting-plan.ko.md`의 샘플 단위 절차에 따라
`.NET` DeliveryDispatch 샘플과 공통 DeliveryDispatch 문서의 요구를 Java 샘플에 매핑한다.

## `.NET` 기준 파일 매핑

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: DeliveryDispatch.sln` | `standalone.settings.gradle.kts` | build | done | Shared, Client, Server/<Role> project를 포함한다. |
| `.NET: README.ko.md` | `README.ko.md` | doc | done | Java role 구조, runner, marker를 설명한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | Redis location store를 준비하고 server role부터 Client까지 실제 process를 띄워 marker를 검증한다. |
| `.NET: Shared/DeliveryDispatch.Shared.csproj` | `Shared/build.gradle.kts` | build | done | Shared contract project다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/src/main/java/.../shared/contracts/Messages.java` | shared-contract | done | 공통 메시지를 Java record와 enum으로 대응했다. |
| `.NET: Client/DeliveryDispatch.Client.csproj` | `Client/build.gradle.kts` | build | done | Client application project다. |
| `.NET: Client/Program.cs` | `Client/src/main/java/.../client/Program.java` | client-entry | done | HTTP client와 stream connector를 만들고 scenario를 실행한다. |
| `.NET: Client/DeliveryDispatchClientScenario.cs` | `Client/src/main/java/.../client/DeliveryDispatchClientScenario.java` | client-scenario | done | 성공 배차, timeout 재배정, server evidence marker를 검증한다. |
| `.NET: Server/Configuration/*.cs` | `Server/Configuration/src/main/java/.../server/configuration/*.java` | server-support | done | `EvidenceStore`, `SampleFlowLog`, `SampleNames`, `SampleTopology`, `SampleTimings`로 공통 설정과 evidence를 둔다. |
| `.NET: location store bootstrap` | `Server/Configuration/src/main/java/.../server/configuration/SampleLocationStore.java` | server-config | done | role들이 공유하는 Redis location store extension을 생성한다. |
| `.NET: Server/Tracking/*` | `Server/Tracking/src/main/java/.../server/tracking/*` | server-role | done | tracking channel, evidence 기록, customer push, server assertion을 처리한다. |
| `.NET: Server/CustomerGateway/*` | `Server/CustomerGateway/src/main/java/.../server/customergateway/*` | server-role | done | customer stream session, customer actor, entry spot, status push를 처리한다. |
| `.NET: Server/CourierSession/*` | `Server/CourierSession/src/main/java/.../server/couriersession/*` | server-role | done | courier stream session과 courier actor/session bind를 처리한다. |
| `.NET: Server/CourierGateway/*` | `Server/CourierGateway/src/main/java/.../server/couriergateway/*` | server-role | done | courier id를 actor node rid와 session route로 해석한다. |
| `.NET: Server/CourierActorNode/*` | `Server/CourierSpotNode/src/main/java/.../server/courierspotnode/*` | server-role | done | node 1/2 courier actor, entry spot, route handler를 제공한다. Java project명은 spot 책임을 드러내도록 `CourierSpotNode`로 둔다. |
| `.NET: Server/Dispatch/*` | `Server/Dispatch/src/main/java/.../server/dispatch/*` | server-role | done | HTTP API, dispatch worker, courier offer, tracking event, self-check endpoint를 제공한다. |

## 공통 메시지 계약 매핑

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `CreateDeliveryRequest` | `Messages.CreateDeliveryRequest` | shared-contract | done | `deliveryId`, `customerId`, `pickupAddress`, `dropoffAddress`를 가진다. |
| `CreateDeliveryResponse` | `Messages.CreateDeliveryResponse` | shared-contract | done | `deliveryId`, `status`를 가진다. |
| `SubscribeDelivery` | `Messages.SubscribeDelivery` | shared-contract | done | customer stream subscription 요청이다. |
| `SubscribeDeliveryAccepted` | `Messages.SubscribeDeliveryAccepted` | shared-contract | done | subscription 수락 응답이다. |
| `DeliveryStatusNotify` | `Messages.DeliveryStatusNotify` | shared-contract | done | customer stream push payload다. |
| `AssignDelivery` | `Messages.AssignDelivery` | shared-contract | done | dispatch worker 입력 메시지다. |
| `BindCourierSession` | `Messages.BindCourierSession` | shared-contract | done | courier stream session과 courier actor bind 요청이다. |
| `BindCourierSessionAccepted` | `Messages.BindCourierSessionAccepted` | shared-contract | done | courier session bind 응답이다. |
| `BindCourier` / `CourierBound` | `Messages.BindCourier`, `Messages.CourierBound` | shared-contract | done | CourierGateway가 actor 위치와 session route를 돌려준다. |
| `EnsureCourierActor` / `CourierActorEnsured` | `Messages.EnsureCourierActor`, `Messages.CourierActorEnsured` | shared-contract | done | target courier spot node의 actor를 보장한다. |
| `OfferDelivery` / `OfferDeliveryResult` | `Messages.OfferDelivery`, `Messages.OfferDeliveryResult` | shared-contract | done | dispatch worker가 courier에게 배송 제안을 보내고 결과를 받는다. |
| `OfferDeliveryNotify` / `CourierDecision` | `Messages.OfferDeliveryNotify`, `Messages.CourierDecision` | shared-contract | done | courier stream push와 courier client decision이다. |
| `ReassignDelivery` | `Messages.ReassignDelivery` | shared-contract | done | timeout 재배정 의미를 드러내는 shared message다. |
| `DeliveryStatusChanged` / `DeliveryStatusAck` | `Messages.DeliveryStatusChanged`, `Messages.DeliveryStatusAck` | shared-contract | done | Tracking server 기록 요청과 응답이다. |
| `EnsureCustomerActor` / `CustomerActorEnsured` | `Messages.EnsureCustomerActor`, `Messages.CustomerActorEnsured` | shared-contract | done | CustomerGateway actor 생성과 조회를 처리한다. |
| `ServerAssertionRequest` / `ServerAssertionResponse` | `Messages.ServerAssertionRequest`, `Messages.ServerAssertionResponse` | shared-contract | done | server-side evidence self-check 계약이다. |
| actor ref snapshot | `ZLinkActorRefSnapshot` | shared-contract | done | framework가 제공하는 actor snapshot으로 node rid, actor id, generation을 전달한다. |
| `DeliveryStatus` | `Messages.DeliveryStatus` | shared-contract | done | `Created`, `Assigned`, `Accepted`, `Reassigned`, `PickedUp`, `Delivered`, `Failed` 값을 가진다. |

## 공통 검증 흐름 매핑

| 기준 | Java 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| Redis location store is ready first | `run_sample.sh` | validation | done | role 시작 전에 Redis endpoint readiness를 확인하고 실행별 key prefix를 전달한다. |
| Tracking starts before gateway/dispatch | `run_sample.sh` | validation | done | tracking channel endpoint readiness를 확인한다. |
| CustomerGateway stream session | `CustomerSession`, `DeliveryDispatchClientScenario` | validation | done | `SubscribeDeliveryAccepted`와 status notify를 검증한다. |
| CourierSession stream session | `CourierSession`, `DeliveryDispatchClientScenario` | validation | done | courier-a/b가 독립 stream session으로 bind된다. |
| Courier spot node 1/2 | `CourierSpotNode` processes | validation | done | courier-a는 node-1, courier-b는 node-2 actor로 배치된다. |
| CourierGateway directory | `CourierDirectory` | validation | done | courier id를 actor node rid와 session route로 해석한다. |
| Dispatch create delivery | `DispatchHttpServer`, client scenario | validation | done | `POST /deliveries`로 success/reassign 배송을 만든다. |
| delivery-success statuses | `DeliveryDispatchClientScenario` | validation | done | `Assigned`, `Accepted`, `PickedUp`, `Delivered`와 `courier-a`를 검증한다. |
| delivery-reassign statuses | `DeliveryDispatchClientScenario` | validation | done | `Assigned`, `Reassigned`, `Accepted`, `Delivered`와 `courier-b`를 검증한다. |
| server evidence check | `DispatchHttpServer`, `EvidenceStore` | validation | done | `/self-check/assert`가 두 delivery의 상태 순서를 검증한다. |
| topology marker | `run_sample.sh` | validation | done | 모든 role readiness 뒤 `topology=ready`를 출력한다. |
| reassignment marker | `DeliveryDispatchClientScenario` | validation | done | `deliverydispatch-reassignment=completed`를 출력한다. |
| server evidence marker | `DeliveryDispatchClientScenario` | validation | done | `deliverydispatch-server-evidence=completed`를 출력한다. |
| final marker | `run_sample.sh`, client scenario | validation | done | `deliverydispatch=completed`와 runner 완료 marker를 검증한다. |

## 현재 결론

Java `DeliveryDispatch`는 `.NET` 기준의 Shared, Client, Server role 책임과 공통 DeliveryDispatch
문서의 검증 흐름을 같은 의미로 대응한다. public framework API만 사용했고, framework runtime
package나 private bridge, 테스트 전용 adapter로 우회하지 않았다.
