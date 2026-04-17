[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 `ASP.NET Core`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `.NET` 바인딩 위에 올라가는 `ZLink Framework`의 `.NET` 방향을 정리한다.
특히 아래 세 축을 우선 다룬다.

- 서비스 간 direct call과 event messaging
- `SPOT`을 `ASP.NET Core` 애플리케이션에서 다루는 방법
- Registry 서버를 `ASP.NET Core` lifecycle 안에서 구동하고 topology를 조회하는 방법

현재 목표는 새 runtime을 만드는 일이 아니다.
기존 `.NET` 바인딩이 제공하는 `Discovery`, `DealerSocket`, `RouterSocket`,
`SpotNode`, `Spot`, `Registry` 같은 기능을 바탕으로, 프레임워크 사용자가 익숙한
DI, hosted service, handler 모델을 제공하는 것이다.

## 2. 문서 구조와 역할 분담

문서는 **기준 문서**와 **주제 문서**, **샘플 문서** 세 종류로 구분한다.

### 2.1 기준 문서 (interface catalog)

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | 모든 공용 인터페이스와 attribute 정의를 한 곳에 모은 기준 문서. 다른 문서에서 인터페이스를 참조할 때 이 문서를 기준으로 한다. |

### 2.2 주제 문서 (programming model)

각 주제 문서는 프로그래밍 모델과 사용 방향을 설명한다.
인터페이스 전체 정의를 다시 나열하지 않고, handler-interfaces.ko.md를
교차 참조한다.

| 문서 | 다루는 범위 |
|------|------------|
| [aspnet-core-service-messaging.ko.md](./aspnet-core-service-messaging.ko.md) | 서비스 등록, handler 프로그래밍 모델, dispatch 흐름, outbound client 사용, lifecycle, middleware/filter |
| [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md) | SPOT 개념, SpotNode 등록, spot lifecycle, publish/subscribe, discovery |
| [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md) | STREAM 개념, packet handler, raw handler, recv 비지원 방향 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | `playhouse` Stage 같은 상위 모델을 SPOT 위에 감쌀 때 필요한 추가 조건 |
| [aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md) | Registry embedded/standalone 구동, topology 조회, 클러스터링 |

### 2.3 샘플 문서

샘플 문서는 등록부터 handler, client 호출까지 한 번에 보여 주는 실행 가능한
코드를 모은다. 인터페이스 정의를 다시 나열하지 않는다.

| 문서 | 다루는 범위 |
|------|------------|
| [service-messaging-samples.ko.md](./service-messaging-samples.ko.md) | 서비스 등록, handler, HTTP handler, outbound client를 한 번에 보는 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | room, stage, zone 기준 SPOT 등록, handler, SendTo, publish를 한 번에 보는 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | STREAM packet handler, raw handler, 등록 코드를 한 번에 보는 샘플 |

### 2.4 범위 원칙

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 인터페이스, attribute, context 전체 정의 | handler-interfaces | 교차 참조 |
| 서비스 등록 (AddZLinkFramework), lifecycle | service-messaging | 필요하면 링크 |
| handler/client 사용 예시, dispatch 흐름 | service-messaging, 샘플 | |
| SPOT 개념, 등록, lifecycle | aspnet-core-spot | 필요하면 링크 |
| Registry 구동, topology 조회 | aspnet-core-registry | 필요하면 링크 |

## 3. 핵심 방향

- `ASP.NET Core`의 DI와 hosted service 모델에 맞춘다.
- handler, client, filter 생성은 같은 `.NET DI` 컨테이너를 기준으로 맞춘다.
- `service_name` 기준 direct call을 기본으로 둔다.
- gateway나 전용 load balancer 없이 service별 discovery channel로 직접 호출한다.
- `SPOT`도 별도 low-level runtime이 아니라, framework lifecycle 안에서
  다룰 수 있어야 한다.
- outbound 호출은 `serviceName` 호출, `router rid` direct 호출,
  `targetRid + spotRid` 호출을 모두 가져야 한다.
- `IZLinkClient`와 `IZLinkSpotClient`는 서로 다른 C API를 감싸는 별도 인터페이스다.
  다만 하부 기능이 겹치는 부분이 있으므로, 두 인터페이스가 비슷한 send/request
  함수군을 각각 가질 수 있다.
