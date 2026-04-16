[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 `ASP.NET Core`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `.NET` 바인딩 위에 올라가는 `ZLink Framework`의 `.NET` 방향을 정리한다.
특히 아래 두 축을 우선 다룬다.

- 서비스 간 direct call과 event messaging
- `SPOT`을 `ASP.NET Core` 애플리케이션에서 다루는 방법

현재 목표는 새 runtime을 만드는 일이 아니다.
기존 `.NET` 바인딩이 제공하는 `Discovery`, `DealerSocket`, `RouterSocket`,
`SpotNode`, `Spot` 같은 기능을 바탕으로, 프레임워크 사용자가 익숙한 DI, hosted
service, handler 모델을 제공하는 것이다.

## 2. 문서 목록

| 문서 | 설명 |
|------|------|
| [aspnet-core-service-messaging.ko.md](./aspnet-core-service-messaging.ko.md) | `ASP.NET Core`에서 direct service call, event messaging, discovery 통합 |
| [service-messaging-samples.ko.md](./service-messaging-samples.ko.md) | 등록, handler, HTTP handler, outbound client를 한 번에 보는 `.NET` 샘플 모음 |
| [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md) | `ASP.NET Core`에서 `SPOT` node, publish, subscribe 통합 |
| [spot-samples.ko.md](./spot-samples.ko.md) | room, stage, zone 기준으로 `SPOT` 등록, handler, `SendToSpot`, publish를 한 번에 보는 샘플 |
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `.NET` 메시지 handler 실제 인터페이스 초안 |

## 3. 핵심 방향

- `ASP.NET Core`의 DI와 hosted service 모델에 맞춘다.
- `service_name` 기준 direct call을 기본으로 둔다.
- gateway나 전용 load balancer 없이 service별 discovery channel로 직접 호출한다.
- `SPOT`도 별도 low-level runtime이 아니라, framework lifecycle 안에서
  다룰 수 있어야 한다.
- outbound 호출은 `serviceName` 호출, `router rid` direct 호출,
  `spot rid` 호출을 모두 가져야 한다.
- 이때 `spot`은 주소 체계가 다르므로 `SendToSpot` / `RequestToSpot` 같은 별도
  함수로 구분하는 편이 더 자연스럽다.
- `IZLinkClient`와 `IZLinkSpotClient`는 서로 다른 C API를 감싸는 별도 인터페이스다.
  다만 하부 기능이 겹치는 부분이 있으므로, 두 인터페이스가 비슷한 send/request
  함수군을 각각 가질 수 있다.
