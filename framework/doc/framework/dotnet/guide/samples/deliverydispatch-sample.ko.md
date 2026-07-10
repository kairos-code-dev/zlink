<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:end -->

# DeliveryDispatch Sample

[.NET 묶음](../../README.ko.md) | [channel](../../spec/aspnet-core-channel-messaging.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [SPOT](../../spec/aspnet-core-spot.ko.md)

> 이 문서는 실행 가능한 DeliveryDispatch 샘플 설명이다. "요청을 만들고 수행자를
> 배정한 뒤 상태를 실시간으로 보여 주는" 도메인에 ZLink 를 도입할지 판단하려면
> [16-case-ride-hailing](../case-studies/16-case-ride-hailing.ko.md)을 먼저 보고, 이
> 문서에서는 DTO, 서버 구조, 실행 흐름을 확인한다. 언어 중립 공통 시나리오는
> [spec/sample/deliverydispatch](../../../common/sample/deliverydispatch/README.ko.md)이 다룬다.

## 1. 목적

DeliveryDispatch 는 배송 배차와 상태 추적을 보여 주는 실시간 업무형 샘플이다. 외부
경계(고객 HTTP·stream[^stream])는 그대로 웹 기술을 쓰고, **내부 배차 메시징·상태
fanout·delivery 별 상태 참여** 를 ZLink 역할 메시지로 구성한다. 같은 구조는 음식 배달,
퀵 배송, 택시 호출, 현장 출동 같은 도메인에 적용된다.

이 샘플이 한 번에 보여 주는 것:

- 고객 HTTP 로 배송을 만들고, stream session 으로 상태 변경을 받는다.
- Dispatch Center 가 배송원 선택, timeout, 재배정을 background worker 로 처리한다.
- Courier 가 의도적으로 무응답해 timeout 재배정 흐름을 만든다.
- Tracking 이 상태 event 를 기록하고 `DeliveryStatusNotify` 를 fanout 으로 publish 한다.
- Session 이 고객 stream 연결과 delivery subscription 을 유지하고 상태를 push 한다.

샘플은 `delivery-success` 와 `delivery-reassign` 두 흐름을 **반드시** 포함한다. payload
codec 은 JSON 을 쓴다.

## 2. 샘플 구성

public DTO[^dto] 는 현재 코드 기준으로
`framework/languages/dotnet/samples/DeliveryDispatch/Shared/Contracts/Messages.cs` 를
따른다. 상태는 문자열이 아니라 enum 으로 둔다.

```csharp
public enum DeliveryStatus
{
    Created, Assigned, Accepted, Reassigned, PickedUp, Delivered, Failed
}
```

고객 HTTP 와 stream DTO:

```csharp
public sealed record CreateDeliveryRequest(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record DeliveryCreated(string DeliveryId);

public sealed record SubscribeDelivery(string DeliveryId);

public sealed record SubscribeDeliveryAccepted(string DeliveryId);

public sealed record DeliveryStatusNotify(
    string DeliveryId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);
```

dispatch·courier·tracking 내부 DTO:

```csharp
public sealed record AssignDelivery(
    string DeliveryId,
    string CustomerId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDelivery(
    string DeliveryId,
    string PickupAddress,
    string DropoffAddress);

public sealed record OfferDeliveryResult(
    string DeliveryId,
    string CourierId,
    bool Accepted,
    string? Reason);

public sealed record ReassignDelivery(
    string DeliveryId,
    string PreviousCourierId,
    string NextCourierId,
    string Reason);

public sealed record DeliveryStatusChanged(
    string DeliveryId,
    DeliveryStatus Status,
    string? CourierId,
    DateTimeOffset OccurredAt);

public sealed record SubscribeCustomerToDelivery(
    string CustomerId,
    string DeliveryId);
```

`DeliveryId`/`CustomerId` 는 도메인 식별자다. `ServerAssertionReq`/`ServerAssertionRes`
는 server-side evidence check 에 쓰고, `DeliverySpotCreate`/`DeliverySpotJoin` 류는
Tracking 내부 Spot 구성에 쓴다.

이 샘플의 `CustomerActor`와 `CourierActor`는 transfer adapter를 등록하지 않는다. customer actor에는
node 간 이동으로 보존할 별도 domain state가 없고, courier actor의 진행 중 request 대기 정보는 다른
node에서 이어서 처리할 이동 state가 아니다. 따라서 factory만 등록한다.

```csharp
options.AddSpotMesh(SampleNames.CustomerActorDiscovery)
    // adapter 미등록 actor type은 framework 기본 빈 state transfer를 사용한다.
    .AddActorFactory<CustomerActorFactory>(SampleNames.CustomerActorType);
```

remote transfer가 필요하면 source는 빈 `ZLinkMessage`를 보내고 target은 등록된 actor factory로 actor를
materialize한다. adapter 미등록은 오류가 아니며 별도의 stateless 등록 API도 필요하지 않다.

## 3. 서버 구성

| 프로세스 | 책임 | ZLink 요소 |
|----------|------|------------|
| `DispatchApi` | 고객 HTTP `POST /deliveries` → `AssignDelivery` 전달 | client-server channel client |
| `DispatchCenter` | 배차 접수, 배송원 선택, timeout, 재배정 | channel server/client + dispatch worker |
| `Courier` (A/B) | 제안 수락 또는 의도적 무응답(timeout) | channel server (`OfferDelivery`) |
| `Tracking` | 상태 event 기록, `DeliveryTrackingSpot`[^spot], status fanout publish | fanout publisher + Spot mesh + actor[^actor] factory |
| `Session` | 고객 stream session, delivery subscription, 상태 push | stream node + fanout subscriber |
| `Probe` | readiness 확인 | active probe |
| location store | 서버 간 endpoint 자동 연결 | Redis location store 또는 테스트용 in-memory store |

역할 간 channel/fanout 이름은 `deliverydispatch.dispatch`,
`deliverydispatch.courier.a/b`, `deliverydispatch.tracking`, `deliverydispatch.status`
(fanout), `delivery-spots`(Spot mesh) 를 기준으로 둔다.

책임 분리 기준:

- Dispatch API 는 HTTP 변환과 ZLink 요청 전송만 맡는다.
- Dispatch Center 는 배차 흐름과 timeout 재시도를 소유한다.
- Courier handler 는 제안을 수락하거나 timeout 을 만드는 역할만 맡는다.
- Tracking 은 상태 event 기록과 status fanout publish 를 맡는다.
- Session 은 stream session, subscription, client push 를 맡는다.

## 4. 실행 흐름

- **성공 배차(`delivery-success`)**: client `SubscribeDelivery` → `POST /deliveries` →
  `AssignDelivery` → Courier A `OfferDelivery` 수락 →
  `Assigned → Accepted → PickedUp → Delivered` 가 fanout 을 거쳐 client stream 으로 push.
- **timeout 재배차(`delivery-reassign`)**: Courier A 가 timeout 까지 무응답 →
  `Assigned → Reassigned` 기록 후 Courier B `OfferDelivery` 수락 →
  `Accepted → PickedUp → Delivered`. 재배정 건의 `Accepted`/`PickedUp`/`Delivered` 는
  `courier-b` 처리다.

Courier A 는 `timeout-reassign` mode(`delivery-success` 는 수락하고 `delivery-reassign`
건만 timeout), Courier B 는 `accept` mode 로 띄워 재배정이 실제로 일어나게 한다.

## 5. 완료 기준 / self-check

- client 는 `SubscribeDelivery` 후 `SubscribeDeliveryAccepted` 의 `DeliveryId` 를 확인한다.
- `delivery-success` 는 `Assigned → Accepted → PickedUp → Delivered` 순서로 도착한다.
- `delivery-reassign` 은 `Assigned → Reassigned → Accepted → PickedUp → Delivered` 순서로 도착한다.
- 재배정 건의 `Reassigned`/`Accepted`/`Delivered` 가 `courier-b` 처리임을 검증한다
  (`PickedUp` 의 courier 는 client 가 아니라 server evidence 가 확인한다).
- server evidence check(`ServerAssertionReq`)가 두 delivery 의 상태 순서를 누락 없이
  기록했는지 확인한다.

smoke 성공 로그는 `topology=ready`, `deliverydispatch-reassignment=completed`,
`deliverydispatch-server-evidence=completed`, `deliverydispatch=completed` 를 포함한다.

## 6. 회귀 테스트

DeliveryDispatch 샘플을 구현할 때는 아래 기존 회귀 테스트가 깨지지 않아야 한다. 이
테스트들은 DeliveryDispatch 가 사용하는 framework 표면(channel request/send, 상태
fanout, 재배정 timer, 고객 stream push)을 이미 고정하고 있다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `LocationMessaging` E2E | DispatchApi/DispatchCenter/Courier 사이 channel request·send 가 location store 자동 연결로 동작한다. |
| `FanoutTests.Publisher_And_Subscriber_Work_Across_Hosts` | 배송 상태 fanout 이 publisher/subscriber 로 전달된다. |
| `ManagerTests.Spot_Publish_Timer_And_Close_Stop_Callbacks_Work` | timeout 재배정 timer 와 Spot lifecycle 정리가 framework timer 계약과 맞는다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | 고객 stream push 와 request/reply correlation 이 유지된다. |

[^stream]: `STREAM` 은 클라이언트와 서버 사이에 지속 연결을 유지하면서 framework Header 기반 packet 을 주고받는 세션형 통신 추상이다.
[^actor]: actor 는 자신만의 메일박스와 상태를 가지고 메시지를 순서대로 처리하는 동시성 단위다.
[^spot]: `SPOT` 은 동적으로 생성ㆍ소멸되는 논리적 단위(예: delivery, room 등)로 메시지를 라우팅하는 추상이다.
[^dto]: DTO(Data Transfer Object) 는 컴포넌트 사이에서 데이터를 옮기기 위해 정의한 단순 데이터 클래스를 가리킨다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: SupportChat Sample](supportchat-sample.ko.md) | [다음: ShoppingMall Sample](shoppingmall-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
