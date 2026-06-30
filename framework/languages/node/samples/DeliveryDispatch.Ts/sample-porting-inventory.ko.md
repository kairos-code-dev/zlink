# DeliveryDispatch.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node DeliveryDispatch 샘플을 공통 샘플 문서와
`.NET` 기준 구현에 매핑한다. `gap`은 완료 판정이 아니라 다음 수정 대상이다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/DeliveryDispatchClientScenario.cs` | `Client/deliverydispatch-client-scenario.ts` | client-scenario | done | stream 연결, subscription, 성공 배차, 재배차, server evidence 검증을 수행한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | HTTP client와 stream connector를 만들고 scenario를 실행한다. |
| `.NET: Probe/Program.cs` | `Server/Probe/probe.ts`, `Server/main.ts --role probe` | probe | done | registry topology readiness를 확인하고 `topology=ready`를 출력한다. |
| `.NET: Server/Configuration/EvidenceStore.cs` | `Server/Configuration/evidence-store.ts` | shared-server-state | done | 상태 event 순서와 evidence 로그를 보관한다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | `@zlink-systems/nestjs` message-flow trace 설정 | logging | not-needed | Node는 role module의 trace 설정으로 flow 로그를 남긴다. |
| `.NET: Server/Configuration/SampleNames.cs` | `Shared/Configuration/sample-names.ts` | configuration | done | channel, fanout, Spot mesh 이름을 공유한다. |
| `.NET: Server/Configuration/SampleTopology.cs` | `Server/Configuration/sample-config.ts`, `Client/Configuration/sample-config.ts` | configuration | done | runner가 넘긴 endpoint 환경 변수를 읽는다. |
| `.NET: Server/Registry/Program.cs` | `Server/Registry/registry-module.ts`, `Server/main.ts --role registry` | server-role | done | registry host를 독립 role로 실행한다. |
| `.NET: Server/DispatchApi/Program.cs` | `Server/DispatchApi/dispatch-api-module.ts`, `Server/DispatchApi/dispatch-api-server.ts` | server-role | done | HTTP 요청을 dispatch channel request로 변환한다. |
| `.NET: Server/DispatchCenter/AssignDeliveryHandler.cs` | `Server/DispatchCenter/Handlers/assign-delivery-handler.ts` | handler | done | 요청을 queue에 넣고 접수 응답을 반환한다. |
| `.NET: Server/DispatchCenter/DispatchWorkQueue.cs` | `Server/DispatchCenter/dispatch-work-queue.ts` | server-role | done | Dispatch Center worker가 처리할 요청을 보관한다. |
| `.NET: Server/DispatchCenter/DispatchWorker.cs` | `Server/DispatchCenter/dispatch-worker.ts` | server-role | done | courier A timeout, courier B 재배정, tracking status publish를 처리한다. |
| `.NET: Server/Courier/OfferDeliveryHandler.cs` | `Server/Courier/offer-delivery-handler.ts` | handler | done | courier mode에 따라 accept 또는 timeout 흐름을 만든다. |
| `.NET: Server/Tracking/CustomerActor.cs` | `Server/Tracking/customer-actor.ts` | actor | done | customer actor를 만들고 delivery join 상태를 유지한다. |
| `.NET: Server/Tracking/Handlers.cs` | `Server/Tracking/Handlers/tracking-handlers.ts` | handler | done | customer actor 보장, delivery subscription, status 기록과 fanout을 처리한다. |
| `.NET: Server/Tracking/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/Spots/EntrySpot/customer-entry-spot.ts` | spot | done | customer actor 생성 entry point다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliveryTrackingSpot.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery-tracking-spot.ts` | spot | done | delivery별 고객 actor join을 표현한다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliverySpotDirectory.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery-spot-directory.ts` | spot-support | done | delivery Spot room route를 관리한다. |
| `.NET: Server/Session/CustomerSession.cs` | `Server/Session/customer-session.ts` | stream-session | done | subscription 요청을 받고 status notify를 client에 push한다. |
| `.NET: Server/Session/CustomerSessionDirectory.cs` | `Server/Session/customer-session-directory.ts` | session-store | done | customer별 stream session을 찾는다. |
| `.NET: Server/Session/DeliveryStatusFanoutHandler.cs` | `Server/Session/delivery-status-fanout-handler.ts` | fanout-handler | done | discovery 기반 fanout subscriber로 status push를 session에 연결한다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | Node는 공통 문서의 `accepted`와 .NET 기준 구현의 `courierId`를 함께 둔다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | registry, tracking, session, courier A/B, dispatch center, dispatch API, probe, client 순서로 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 process 경계와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: 역할 Registry` | `Server/Registry/registry-module.ts` | server-role | done | registry host를 독립 process로 시작한다. |
| `common: 역할 DispatchApi` | `Server/DispatchApi/*` | server-role | done | HTTP `/deliveries`와 `/self-check/assert`를 제공한다. |
| `common: 역할 DispatchCenter` | `Server/DispatchCenter/*` | server-role | done | 배차 queue, timeout, 재배정, tracking publish를 맡는다. |
| `common: 역할 Courier` | `Server/Courier/*` | server-role | done | courier A는 timeout, courier B는 accept mode로 실행된다. |
| `common: 역할 Tracking` | `Server/Tracking/*` | server-role | done | status 기록, Spot join, fanout publish를 맡는다. |
| `common: 역할 Session` | `Server/Session/*` | server-role | done | stream session과 delivery subscription을 유지한다. |
| `common: 역할 Probe` | `Server/Probe/probe.ts` | validation | done | registry topology readiness를 확인한다. |
| `common: message CreateDeliveryReq/CreateDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | HTTP 생성 요청과 응답 타입을 둔다. |
| `common: message SubscribeDeliveryReq/SubscribeDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | stream subscription 요청과 응답 타입을 둔다. |
| `common: message DeliveryStatusNotify` | `Shared/Contracts/messages.ts` | shared-contract | done | 상태 push payload를 정의한다. |
| `common: message AssignDeliveryReq/AssignDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | `AssignDeliveryRes`는 `deliveryId`, `accepted`, `courierId`를 포함해 공통 문서와 .NET 기준 구현을 모두 만족한다. |
| `common: message OfferDeliveryReq/OfferDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | courier offer와 응답 타입을 둔다. |
| `common: message DeliveryStatusReq/DeliveryStatusRes` | `Shared/Contracts/messages.ts` | shared-contract | done | tracking status event와 ack를 둔다. |
| `common: message EnsureCustomerActorReq/EnsureCustomerActorRes` | `Shared/Contracts/messages.ts` | shared-contract | done | session이 tracking에 customer actor를 요청한다. |
| `common: message SubscribeCustomerToDeliveryReq/SubscribeCustomerToDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | customer actor를 delivery Spot에 join시킨다. |
| `common: message DeliverySpotCreateReq/DeliverySpotCreateRes` | `Shared/Contracts/messages.ts` | shared-contract | done | delivery Spot 생성 payload와 결과를 둔다. |
| `common: message DeliverySpotJoinReq/DeliverySpotJoinRes` | `Shared/Contracts/messages.ts` | shared-contract | done | delivery별 actor join payload와 결과를 둔다. |
| `common: validation delivery-success Assigned` | `Client/deliverydispatch-client-scenario.ts` | validation | done | `courier-a`의 `Assigned` push를 기다리고 검증한다. |
| `common: validation delivery-success Accepted/PickedUp/Delivered` | `Client/deliverydispatch-client-scenario.ts` | validation | done | 각 상태가 `courier-a`에서 왔는지 검증한다. |
| `common: validation delivery-reassign Assigned` | `Client/deliverydispatch-client-scenario.ts` | validation | done | 최초 `Assigned`가 `courier-a`에서 왔는지 검증한다. |
| `common: validation delivery-reassign Reassigned/Accepted/Delivered` | `Client/deliverydispatch-client-scenario.ts` | validation | done | 재배정 이후 상태가 `courier-b`에서 왔는지 검증한다. |
| `common: validation server evidence` | `Server/DispatchApi/dispatch-api-server.ts` | validation | done | 두 delivery의 상태 순서를 evidence store로 확인한다. |
| `common: success marker topology=ready` | `Server/Probe/probe.ts` | validation | done | topology 준비 시 출력한다. |
| `common: success marker deliverydispatch-reassignment=completed` | `Client/deliverydispatch-client-scenario.ts` | validation | done | 재배정 self-check 뒤 출력한다. |
| `common: success marker deliverydispatch-server-evidence=completed` | `Client/deliverydispatch-client-scenario.ts` | validation | done | server evidence 통과 뒤 출력한다. |
| `common: success marker deliverydispatch=completed` | `Client/main.ts` | validation | done | scenario 완료 뒤 출력한다. |

## 남은 확인

- PowerShell runner의 Windows 전용 경로는 별도 Windows 환경에서 확인해야 한다.
- 공통 문서의 `AssignDeliveryRes.Accepted`와 `.NET` 구현의 `CourierId` drift를 별도 문서 또는 후속 계약 정리로 처리해야 한다.
