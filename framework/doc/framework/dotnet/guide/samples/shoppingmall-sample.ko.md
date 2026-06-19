<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: DeliveryDispatch Sample](deliverydispatch-sample.ko.md) | [다음: GameQuest Sample](gamequest-sample.ko.md)
<!-- framework-adapter-nav:end -->

# ShoppingMall Sample

[.NET 묶음](../../README.ko.md) | [channel](../../spec/aspnet-core-channel-messaging.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md) | [Registry](../../spec/aspnet-core-registry.ko.md)

> 이 문서는 실행 가능한 ShoppingMall 샘플 설명이다. 체크아웃 도메인에 ZLink 와
> event sourcing 을 도입할지 판단하려면
> [13-case-ecommerce-checkout](../case-studies/13-case-ecommerce-checkout.ko.md)을 먼저
> 보고, 이 문서에서는 DTO, 서버 구조, 실행 흐름을 확인한다. 언어 중립 공통 시나리오는
> [spec/sample/event/shoppingmall](../../../common/sample/event/shoppingmall.ko.md)이 다룬다.

## 1. 목적

ShoppingMall 은 외부 HTTP API 와 stateful workflow owner 를 분리해도 주문 상태
전이·복구·audit·조회 projection 이 분명함을 event sourcing 으로 보여 주는 샘플이다.
Kafka 나 Redis Stream 을 다시 만드는 것이 아니라, **작게 시작해 중간 규모까지 견고한
주문 workflow** 를 ZLink owner routing 으로 구성하는 것이 의도다.

이 샘플이 한 번에 보여 주는 것:

- `CommerceApi` 는 HTTP API, 입력 검증, idempotency lookup, projection **조회만** 한다.
- `OrderWorkflow` 의 owner route handler 가 `OrderId` 소유 `OrderWorkflowSpot`[^spot] 을
  보장한 뒤, `OrderWorkflowService` 가 주문 domain event 를 append 한다.
- 어느 `CommerceApi` instance 가 받아도 `OrderId` 기준 같은 owner 로 route 된다.
- projection 은 `OrderEventStore` replay 로 다시 만들 수 있다.
- 결제 실패, 재고 부족, 중복 요청, projection rebuild, scale-out routing 을 검증한다.

stateless API scale-out 과 stateful owner routing 을 함께 보이기 위해 `CommerceApi` 와
`OrderWorkflow` 를 각각 2 instance 로 실행한다. payload codec 은 JSON 을 쓴다.

## 2. 샘플 구성

public DTO[^dto] 는 현재 코드 기준으로
`framework/languages/dotnet/samples/ShoppingMall/Shared/Contracts/Messages.cs` 를 따른다.

client HTTP DTO 와 상태 모델:

```csharp
public sealed record StartOrderReq(
    string CartId,
    string ShippingAddressId,
    string PaymentMethodId,
    string IdempotencyKey);

public sealed record StartOrderRes(
    string OrderId,
    string Status);

public sealed record GetOrderStateReq(string OrderId);

public sealed record GetOrderStateRes(OrderState State);

public sealed record OrderState(
    string OrderId,
    string Status,
    string? ShippingAddressId,
    string? ReservationId,
    string? PaymentId,
    string? Reason,
    decimal? Amount,
    string? Currency,
    long UpdatedAtUnixMs);
```

`CommerceApi` → `OrderWorkflow` 내부 workflow command DTO:

```csharp
public sealed record StartOrderWorkflowReq(
    string OrderId,
    string CartId,
    string ShippingAddressId,
    string PaymentMethodId,
    string IdempotencyKey,
    OrderLineInput[] Lines,
    decimal Amount,
    string Currency);

public sealed record ContinueOrderWorkflowReq(string OrderId);

public sealed record RebuildOrderProjectionReq(string OrderId);
```

`Status` 는 `Created`, `InventoryReserved`, `PaymentAuthorized`, `Confirmed`, `Failed`
를 쓴다. `OrderId`/`EventId`/`SourceCommandId` 는 명시적 domain id 이며 routing id hex
문자열을 client 에 노출하지 않는다. `CartSeed`/`InventorySeed`/`PaymentMethodSeed` 는
self-check seed 데이터, `ServerAssertionReq`/`Res` 는 server-side 검증에 쓴다.

## 3. 서버 구성

| 서버 | instance | 책임 |
|------|:--------:|------|
| `CommerceApi` | 2 | HTTP API, 입력 검증, idempotency lookup, projection 조회(event append 안 함) |
| `OrderWorkflow` | 2 | `OrderWorkflowSpot` 호스팅, 상태 전이, event append, projection 갱신 |
| `Registry` | 1 | instance discovery |

저장소는 별도 ZLink 서버가 아니라 instance 들이 공유하는 dependency 다.

| 저장소 | 책임 |
|--------|------|
| `OrderEventStore` | `OrderId` 별 주문 event stream(기준 저장소) |
| `OrderReadModelStore` | client 조회용 projection |
| `CommerceStateStore` | cart, stock, payment, idempotency mapping |

`OrderWorkflow` 서버는 DDD/hexagonal 구조를 따른다.

```text
Server/OrderWorkflow/
  Domain/
    ShoppingMall/       # OrderAggregate, OrderState, OrderEvents, OrderPolicy
  Application/
    OrderWorkflow/      # Start/Continue/Rebuild use case, 보상 흐름
  Adapters/
    ZLink/
      Spots/
        Handlers/       # StartOrderWorkflow, ContinueOrderWorkflow, RebuildOrderProjection
```

`Domain` 은 ZLink/DB/Kafka 를 직접 참조하지 않는다. `OrderWorkflowSpot` 은 adapter 로,
`OrderEventStore` 에서 stream 을 읽어 `OrderAggregate` 를 복원하고 domain 이 반환한
event 를 다시 append 한다.

### 필수 경계

- `CommerceApi` 는 `OrderWorkflowSpot` 을 호스팅하거나 event 를 append 하지 않는다.
- 주문 시작·재개·rebuild 는 `OrderWorkflowSpot` 으로 가는 명시적 command 메시지로 처리한다.
- `GetOrCreate` create payload 나 `OnCreate` 에서 업무 상태 전이를 실행하지 않는다.
- `GetOrderStateReq` 는 projection 만 읽고 event 를 유발하지 않는다.

## 4. 실행 흐름

- **성공**: `StartOrderReq` → `CommerceApi` 가 idempotency lookup·검증 후 `OrderId` 생성 →
  `StartOrderWorkflowReq` 전달 → owner route handler 가 Spot 을 보장하고 `OrderWorkflowService`
  가 `OrderStartedEvent → InventoryReservedEvent → PaymentAuthorizedEvent → OrderConfirmedEvent`
  를 append 하며 단계마다 projection 갱신. client 는 `GetOrderStateReq` 로 진행/최종 상태 조회.
- **재고 실패**: `OrderStartedEvent → InventoryReservationFailedEvent → OrderFailedEvent`.
- **결제 실패(보상)**: `OrderStartedEvent → InventoryReservedEvent → PaymentFailedEvent →
  InventoryReleasedEvent → OrderFailedEvent` 로 재고 예약을 되돌린다.
- **중복/복구**: 같은 `IdempotencyKey` 는 같은 `OrderId` 로 모이고, pending → started
  mapping 으로 중복 시작과 중간 실패 복구를 구분한다.

## 5. 완료 기준 / self-check

- 같은 `OrderId` 의 event 는 항상 같은 `OrderWorkflowSpot` owner 흐름에서 append 된다.
- `OrderEventStore` append 는 expected version check 와 `SourceCommandId` dedupe 를 한다.
- projection 을 삭제해도 `RebuildOrderProjectionReq` 로 event replay 만으로 재생성된다.
- 중복 `StartOrderReq` 는 같은 `OrderId` 를 반환하고 event 를 중복 append 하지 않는다.
- 결제 실패 후 `InventoryReleasedEvent` 보상이 append 된다.
- scale-out self-check 는 `CommerceApi x2`, `OrderWorkflow x2` 구성에서 두 `CommerceApi`
  instance 어디서 조회해도 같은 projection(event sequence·count)을 반환함을 검증한다.

## 6. 회귀 테스트

ShoppingMall 샘플을 구현할 때는 아래 기존 회귀 테스트가 깨지지 않아야 한다. 이
테스트들은 이 샘플이 사용하는 framework 표면(workflow Spot instance 초기화, Spot
lifecycle, route mesh request, projection read model 저장)을 이미 고정하고 있다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ManagerTests.SpotManager_GetOrCreateAsync_Initializes_Once_With_First_CreatePayload` | 같은 OrderId workflow Spot instance 가 최초 payload 로 한 번만 초기화된다. |
| `ManagerTests.SpotManager_Create_List_Close_And_Publish_Work_Through_FrameworkRuntime` | OrderWorkflow Spot 생성·조회·close·publish 가 framework runtime 으로 동작한다. |
| `ClientServerTests.DiscoveryClient_Request_And_Send_Work_Across_Hosts` | CommerceApi channel request·send 가 Registry/Discovery 경로로 동작한다. |
| `PublisherTests.OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel` | projection 갱신 event publish 가 target channel 로 전달된다. |

[^spot]: `SPOT` 은 동적으로 생성ㆍ소멸되는 논리적 노드(예: order workflow instance 등) 단위로 메시지를 라우팅하는 추상이다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: DeliveryDispatch Sample](deliverydispatch-sample.ko.md) | [다음: GameQuest Sample](gamequest-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
