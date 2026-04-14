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
`SpotNode`, `Spot` 같은 기능 위에, 프레임워크 사용자가 익숙한 DI, hosted
service, handler 모델을 얹는 것이다.

## 2. 문서 목록

| 문서 | 설명 |
|------|------|
| [aspnet-core-service-messaging.ko.md](./aspnet-core-service-messaging.ko.md) | `ASP.NET Core`에서 direct service call, event messaging, discovery 통합 |
| [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md) | `ASP.NET Core`에서 `SPOT` node, publish, subscribe 통합 |

## 3. 핵심 방향

- `ASP.NET Core`의 DI와 hosted service 모델에 맞춘다.
- `service_name` 기준 direct call을 기본으로 둔다.
- gateway나 전용 load balancer 없이 Discovery와 client-side selection으로
  provider를 고른다.
- `SPOT`도 별도 low-level runtime이 아니라, framework lifecycle 안에서
  다룰 수 있어야 한다.
