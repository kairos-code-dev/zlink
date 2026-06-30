# ShoppingMall.Ts .NET 기준 포팅 Inventory

이 문서는 `framework/doc/plan/framework-node-sample-dotnet-porting-plan.ko.md`의
샘플 단위 절차에 따라 현재 Node ShoppingMall 샘플을 공통 event 샘플 문서와
`.NET` 기준 구현에 매핑한다. `gap`은 완료 판정이 아니라 다음 수정 대상이다.

## 파일과 역할 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `.NET: Client/ShoppingMallClientScenario.cs` | `Client/shoppingmall-client-scenario.ts` | client-scenario | done | 성공 주문, 중복 idempotency, 대기 중인 idempotency 복구, 재고 실패, 결제 실패, projection rebuild, scale-out 조회를 검증한다. |
| `.NET: Client/Program.cs` | `Client/main.ts` | client-entry | done | API A/B HTTP client를 만들고 scenario를 실행한다. |
| `.NET: Client/Configuration/SampleNames.cs` | `Client/Configuration/sample-config.ts`, `Shared/Configuration/sample-names.ts` | configuration | done | runner가 넘긴 API endpoint와 channel 이름을 읽는다. |
| `.NET: Server/Configuration/SampleFlowLog.cs` | role module의 message-flow trace 설정 | logging | not-needed | Node는 module별 trace log file과 label을 직접 설정한다. |
| `.NET: Server/Configuration/SampleNames.cs` | `Shared/Configuration/sample-names.ts` | configuration | done | workflow route channel과 registry 이름을 공유한다. |
| `.NET: Server/Registry/Program.cs` | `Server/Registry/registry-module.ts`, `Server/main.ts --role registry` | server-role | done | registry host를 독립 role로 실행한다. |
| `.NET: Server/CommerceApi/Program.cs` | `Server/CommerceApi/commerce-api-module.ts`, `Server/CommerceApi/commerce-api-server.ts` | server-role | done | HTTP API와 workflow route client를 제공한다. |
| `.NET: Server/CommerceApi/Application/OrderWorkflow/StartOrderUseCase.cs` | `Server/CommerceApi/Application/start-order-use-case.ts` | application | done | 주문 시작 검증, idempotency 예약, workflow command 생성을 HTTP adapter 밖으로 분리했다. |
| `.NET: Server/CommerceApi/Infrastructure/Http/HttpCommerceApiPeerClient.cs` | `Client/shoppingmall-client-scenario.ts`의 API A/B 호출 | test-support | not-needed | Node runner는 client가 두 API endpoint를 직접 호출해 scale-out을 검증한다. |
| `.NET: Server/CommerceApi/Infrastructure/ZLink/ZLinkOrderWorkflowRouter.cs` | `Server/CommerceApi/Infrastructure/ZLink/zlink-order-workflow-router.ts` | zlink-client | done | workflow route client를 outbound adapter로 분리했다. |
| `.NET: Server/CommerceApi/Ports/Outbound/WorkflowPorts.cs` | `Server/CommerceApi/Ports/Outbound/workflow-ports.ts` | port-contract | done | CommerceApi application이 쓰는 workflow command port를 정의한다. |
| `.NET: Server/OrderWorkflow/Application/OrderWorkflow/OrderWorkflowService.cs` | `Server/OrderWorkflow/Application/OrderWorkflow/order-workflow-service.ts` | application | done | channel handler는 service에 위임하고 workflow command/query 책임을 application service에 모았다. |
| `.NET: Server/OrderWorkflow/Domain/ShoppingMall/OrderDomain.cs` | `Server/OrderWorkflow/Domain/ShoppingMall/order-domain.ts` | domain | done | 주문 생성과 inventory/payment 결과에 따른 상태 전이를 domain helper로 분리했다. |
| `.NET: Server/OrderWorkflow/Infrastructure/ZLink/Handlers/OrderWorkflowRouteHandlers.cs` | `Server/OrderWorkflow/Handlers/start-order-handler.ts`, `Server/OrderWorkflow/Handlers/continue-order-workflow-handler.ts`, `Server/OrderWorkflow/Handlers/query-and-self-check-handlers.ts` | handler | done | workflow route request를 받아 `OrderWorkflowSpot`을 생성하거나 확보한 뒤 workflow service를 호출한다. |
| `.NET: Server/OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/*` | `Server/OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/*` | spot | done | route mesh와 같은 이름의 Spot mesh에 `OrderWorkflowSpot` factory와 request handler를 등록했다. |
| `.NET: Server/OrderWorkflow/Program.cs` | `Server/OrderWorkflow/shoppingmall-workflow-module.ts`, `Server/main.ts --role workflow-a|workflow-b` | server-role | done | workflow role 두 instance를 실행한다. |
| `.NET: Server/Shared/Domain/OrderEvents.cs` | `Server/Shared/Domain/order-events.ts` | domain-event | done | 주문 domain event union을 server shared domain 파일로 분리했다. |
| `.NET: Server/Shared/Domain/OrderProjection.cs` | `Shared/Contracts/messages.ts`, `Server/Shared/Store/order-store.ts` | projection | done | 조회 projection 상태를 저장하고 API 조회에 사용한다. |
| `.NET: Server/Shared/Ports/Outbound/Stores.cs` | `Server/Shared/Ports/Outbound/stores.ts` | port-contract | done | CommerceApi와 OrderWorkflow가 기대하는 store port interface를 명시했다. |
| `.NET: Server/Shared/Store/FileCommerceStores.cs` | `Server/Shared/Store/order-store.ts` | store | done | file-backed shared store로 idempotency, events, projection, commerce state를 유지한다. |
| `.NET: Shared/Contracts/Messages.cs` | `Shared/Contracts/messages.ts` | shared-contract | done | client HTTP 메시지와 workflow command/reply 메시지를 정의한다. |
| `.NET: run_sample.sh` | `run_sample.sh` | runner | done | registry, workflow A/B, API A/B, client self-check 순서로 실행한다. |
| `.NET: run_sample.ps1` | `run_sample.ps1` | runner | done | Unix PowerShell에서는 검증된 Linux runner를 호출해 같은 process 경계와 self-check marker를 사용한다. |

## 공통 요구 매핑

| 기준 | Node 대응 | 분류 | 상태 | 비고 |
|------|-----------|------|------|------|
| `common: Registry 1 instance` | `Server/Registry/registry-module.ts` | server-role | done | registry host를 하나 실행한다. |
| `common: CommerceApi 2 instances` | `Server/main.ts --role api-a|api-b`, `run_sample.sh` | server-role | done | API A/B를 별도 HTTP endpoint로 실행한다. |
| `common: OrderWorkflow 2 instances` | `Server/main.ts --role workflow-a|workflow-b`, `run_sample.sh` | server-role | done | workflow A/B route endpoints를 registry discovery로 노출한다. |
| `common: CommerceApi는 주문 상태를 직접 바꾸지 않는다` | `Server/CommerceApi/Application/start-order-use-case.ts`, `Server/CommerceApi/Infrastructure/ZLink/zlink-order-workflow-router.ts` | boundary | done | 주문 시작 상태 전이는 workflow port로 relay하고, HTTP adapter는 request/response 변환만 맡는다. |
| `common: OrderWorkflowSpot owner routing` | `Server/OrderWorkflow/Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot.ts`, `Server/OrderWorkflow/Handlers/start-order-handler.ts` | spot | done | route handler가 `OrderId` 기준 Spot을 확보하고 workflow role이 `OrderWorkflowSpot`을 호스팅한다. |
| `common: OrderEventStore append` | `Server/Shared/Store/order-store.ts` | event-store | done | order별 event stream을 file store에 append한다. |
| `common: OrderReadModelStore projection` | `Server/Shared/Store/order-store.ts` | projection | done | API 조회는 projection을 읽고 rebuild 뒤 복구를 확인한다. |
| `common: CommerceStateStore` | `Server/Shared/Store/order-store.ts` | commerce-state | done | cart, stock, payment, idempotency 상태를 file store로 공유한다. |
| `common: message StartOrderReq/StartOrderRes` | `Shared/Contracts/messages.ts` | shared-contract | done | client HTTP 주문 시작 계약을 둔다. |
| `common: message GetOrderStateReq/GetOrderStateRes` | `Shared/Contracts/messages.ts` | shared-contract | done | 조회 응답은 projection 상태를 반환한다. |
| `common: message StartOrderWorkflowReq/Res` | `Shared/Contracts/messages.ts` | shared-contract | done | CommerceApi가 workflow role로 보내는 command를 둔다. |
| `common: message ContinueOrderWorkflowReq/Res` | `Shared/Contracts/messages.ts` | shared-contract | done | workflow 진행 command를 둔다. |
| `common: message RebuildOrderProjectionReq/Res` | `Shared/Contracts/messages.ts` | shared-contract | done | projection rebuild command를 둔다. |
| `common: validation payment failure` | `Client/shoppingmall-client-scenario.ts` | validation | done | 결제 실패 상태와 reason, reservation evidence를 확인한다. |
| `common: validation inventory failure` | `Client/shoppingmall-client-scenario.ts` | validation | done | 재고 실패 상태와 reason을 확인한다. |
| `common: validation duplicate idempotency` | `Client/shoppingmall-client-scenario.ts` | validation | done | 같은 key 재요청이 기존 order id를 반환하는지 확인한다. |
| `common: validation waiting idempotency recovery` | `Client/shoppingmall-client-scenario.ts` | validation | done | 대기 중인 idempotency mapping을 seed하고 workflow가 같은 order id를 확정하는지 확인한다. |
| `common: validation projection rebuild` | `Client/shoppingmall-client-scenario.ts` | validation | done | projection 삭제 뒤 rebuild 결과와 API B 조회를 확인한다. |
| `common: validation scale-out routing` | `Client/shoppingmall-client-scenario.ts`, `run_sample.sh` | validation | done | API A/B와 workflow A/B 로그, cross API 조회를 확인한다. |
| `common: success marker shoppingmall-server-evidence=completed` | `Client/shoppingmall-client-scenario.ts` | validation | done | server evidence 통과 뒤 출력한다. |
| `common: success marker shoppingmall=completed` | `Client/main.ts` | validation | done | scenario 완료 뒤 출력한다. |

## 남은 확인

- `OrderWorkflowSpot` owner 구조, domain/application/port 디렉토리, route mesh 실행 검증은 Node 샘플에 반영했다.
- PowerShell runner의 Windows 전용 경로는 별도 Windows 환경에서 확인해야 한다.
