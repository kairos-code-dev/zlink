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
| `.NET: Server/Dispatch/Program.cs` | `Server/DispatchApi/*`, `Server/DispatchCenter/*`, `Server/main.ts --role dispatch-api/dispatch-center` | server-role | done | Node는 HTTP API와 worker를 두 role로 나누지만 dispatch channel, timeout, 재배정, tracking publish 책임은 같은 의미로 둔다. |
| `.NET: Server/Dispatch/DispatchWorkQueue.cs` | `Server/DispatchCenter/dispatch-work-queue.ts` | server-role | done | Dispatch Center worker가 처리할 요청을 보관한다. |
| `.NET: Server/Dispatch/DispatchWorker.cs` | `Server/DispatchCenter/dispatch-worker.ts` | server-role | done | 단일 courier gateway channel로 offer를 보내고 timeout 시 courier-b로 재배정한다. |
| `.NET: Server/CourierGateway/CourierDirectory.cs` | `Shared/Configuration/sample-names.ts`, `Server/Courier/bind-courier-handler.ts` | server-role | done | courier id를 actor node rid와 stream session route로 해석한다. |
| `.NET: Server/CourierGateway/CourierGatewayHandlers.cs` | `Server/Courier/bind-courier-handler.ts`, `Server/Courier/offer-delivery-handler.ts` | handler | done | `BindCourierReq`와 `OfferDeliveryReq`를 받고 actor-node route mesh로 전달한다. |
| `.NET: Server/CourierSession/CourierSession.cs` | `Server/CourierSession/courier-session.ts` | stream-session | done | 배송원 stream bind 요청을 받고 CourierGateway에 courier id와 session route를 등록한다. |
| `.NET: Server/CourierSession/BindCourierSessionHandler.cs` | `Server/CourierSession/courier-session.ts` | stream-handler | done | `BindCourierSessionReq`를 `BindCourierReq`로 relay하고 bind 응답을 client에 반환한다. |
| `.NET: Server/CourierActorNode/CourierActor.cs` | `Server/Courier/courier-actor.ts`, `Server/main.ts --role courier-actor-node1/2` | actor-node | done | Courier actor factory를 등록하고 node1은 `courier-a`, node2는 `courier-b`를 맡는다. |
| `.NET: Server/CourierActorNode/Spots/EntrySpot/EntrySpot.cs` | `Server/Courier/courier-entry-spot.ts` | entry-spot | done | courier actor Spot mesh의 entry spot을 등록한다. |
| `.NET: Server/CourierActorNode/RouteHandlers.cs` | `Server/Courier/offer-delivery-handler.ts` | route-handler | done | CourierGateway가 route mesh target node rid로 offer를 전달하고 actor manager가 courier actor를 보장한다. |
| `.NET: Server/CustomerGateway/CustomerActor.cs` | `Server/Tracking/customer-actor.ts` | actor | done | customer actor를 만들고 delivery join 상태를 유지한다. |
| `.NET: Server/Tracking/Handlers.cs` | `Server/Tracking/Handlers/tracking-handlers.ts` | handler | done | customer actor 보장, delivery subscription, status 기록과 fanout을 처리한다. |
| `.NET: Server/Tracking/Spots/EntrySpot/CustomerEntrySpot.cs` | `Server/Tracking/Spots/EntrySpot/customer-entry-spot.ts` | spot | done | customer actor 생성 entry point다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliveryTrackingSpot.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery-tracking-spot.ts` | spot | done | delivery별 고객 actor join을 표현한다. |
| `.NET: Server/Tracking/Spots/DeliveryTrackingSpot/DeliverySpotDirectory.cs` | `Server/Tracking/Spots/DeliveryTrackingSpot/delivery-spot-directory.ts` | spot-support | done | delivery Spot room route를 관리한다. |
| `.NET: Server/CustomerGateway/CustomerSession.cs` | `Server/Session/customer-session.ts` | stream-session | done | subscription 요청을 받고 status notify를 client에 push한다. |
| `.NET: Server/CustomerGateway/CustomerActorDirectory.cs` | `Server/Session/customer-session-directory.ts` | session-store | done | customer별 stream session을 찾는다. |
| `.NET: Server/CustomerGateway/CustomerGatewayHandlers.cs` | `Server/Tracking/Handlers/tracking-handlers.ts`, `Server/Session/delivery-status-fanout-handler.ts` | handler | done | Node는 customer actor 보장과 fanout push를 tracking/session role에 나눠 둔다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | Node는 공통 문서의 `accepted`와 .NET 기준 구현의 `courierId`를 함께 둔다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | registry, tracking, customer session, courier session, courier actor node1/2, courier gateway, dispatch center, dispatch API, probe, client 순서로 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 process 경계와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: 역할 Registry` | `Server/Registry/registry-module.ts` | server-role | done | registry host를 독립 process로 시작한다. |
| `common: 역할 DispatchApi` | `Server/DispatchApi/*` | server-role | done | HTTP `/deliveries`와 `/self-check/assert`를 제공한다. |
| `common: 역할 DispatchCenter` | `Server/DispatchCenter/*` | server-role | done | 배차 queue, timeout, 재배정, tracking publish를 맡는다. |
| `common: 역할 CourierGateway` | `Server/Courier/*`, `Server/main.ts --role courier-gateway` | server-role | done | 단일 courier channel을 열고 courier id를 actor node rid로 해석한다. |
| `common: 역할 CourierSession` | `Server/CourierSession/*`, `Server/main.ts --role courier-session` | server-role | done | 배송원 A/B stream bind를 처리한다. |
| `common: 역할 CourierSpotNode1/2` | `Server/Courier/*`, `Server/main.ts --role courier-actor-node1/2` | server-role | done | route mesh target node로 offer를 받고 Spot actor factory로 courier actor를 보장한 뒤 node별 courier decision을 만든다. |
| `common: 역할 Tracking` | `Server/Tracking/*` | server-role | done | status 기록, Spot join, fanout publish를 맡는다. |
| `common: 역할 CustomerGateway` | `Server/Session/*` | server-role | done | 고객 stream session과 delivery subscription을 유지한다. |
| `common: 역할 Probe` | `Server/Probe/probe.ts` | validation | done | registry topology readiness를 확인한다. |
| `common: message CreateDeliveryReq/CreateDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | HTTP 생성 요청과 응답 타입을 둔다. |
| `common: message SubscribeDeliveryReq/SubscribeDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | stream subscription 요청과 응답 타입을 둔다. |
| `common: message BindCourierReq/BindCourierRes` | `Shared/Contracts/messages.ts` | shared-contract | done | CourierSession이 CourierGateway에 session route를 등록한다. |
| `common: message BindCourierSessionReq/BindCourierSessionRes` | `Shared/Contracts/messages.ts` | shared-contract | done | 배송원 client stream bind 요청과 응답 타입을 둔다. |
| `common: message EnsureCourierActorReq/EnsureCourierActorRes` | `Shared/Contracts/messages.ts` | shared-contract | done | CourierGateway와 actor-node route 사이의 actor 보장 계약을 타입으로 둔다. |
| `common: message DeliveryStatusNotify` | `Shared/Contracts/messages.ts` | shared-contract | done | 상태 push payload를 정의한다. |
| `common: message AssignDeliveryReq/AssignDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | `AssignDeliveryRes`는 `deliveryId`, `accepted`, `courierId`를 포함해 공통 문서와 .NET 기준 구현을 모두 만족한다. |
| `common: message OfferDeliveryReq/OfferDeliveryRes` | `Shared/Contracts/messages.ts` | shared-contract | done | courier offer와 응답 타입을 둔다. |
| `.NET: message OfferDeliveryNotify/CourierDecisionMsg/ReassignDelivery` | `Shared/Contracts/messages.ts` | shared-contract | done | 배송원 push, decision, 재배정 메시지 계약 이름을 Node 타입으로 유지한다. |
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
