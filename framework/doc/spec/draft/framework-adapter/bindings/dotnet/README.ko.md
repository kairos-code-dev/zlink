[스펙 목차](../../../README.ko.md)

[Framework Adapter 정책](../../policy/README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM Decisions](./stream-open-items.ko.md) | [Monitoring](./aspnet-core-monitoring.ko.md) | [Registry](./aspnet-core-registry.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Regression Matrix](./regression-test-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md) | [Scope](./implementation-scope-and-nongoals.ko.md) | [Backend Policy](./backend-dependency-policy.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- ZLink Framework For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 `ASP.NET Core`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `.NET` 바인딩 위에 올라가는 `ZLink Framework`의 `.NET` 방향을 정리한다.
특히 아래 세 축을 우선 다룬다.

- channel 이름 기준 direct call과 event messaging
- `SPOT`을 `ASP.NET Core` 애플리케이션에서 다루는 방법
- Registry 서버를 `ASP.NET Core` lifecycle 안에서 구동하고 topology를 조회하는 방법

현재 목표는 새 runtime을 만드는 일이 아니다.
기존 `.NET` 바인딩이 제공하는 `Discovery`, `DealerSocket`, `RouterSocket`,
`SpotNode`, `Spot`, `Registry` 같은 기능을 바탕으로, 프레임워크 사용자가 익숙한
DI, hosted service, handler 모델을 제공하는 것이다.

현재 구현 backend는 `bindings/dotnet`을 사용한다. 다만 framework public contract는
backend 구현체와 분리해서 유지하는 편을 기본으로 본다. 자세한 기준은
[backend-dependency-policy.ko.md](./backend-dependency-policy.ko.md)를 참고한다.

## 1.1 지원 버전 기준

이 `.NET` 초안은 아래 버전 기준을 먼저 고정한다.

- 최소 지원 런타임: `.NET 8` (`net8.0`)
- 주 개발 기준: `.NET 10` (`net10.0`)
- 최소 지원 언어 버전: `C# 12`

따라서 이 디렉토리의 문서와 샘플은 최소 지원 기준에서 바로 구현 가능한 표면을
우선 설명한다. `C# 13`, `C# 14`, `preview`, `latest` 전용 문법이나 API를 공개
framework 계약의 전제로 두지 않는다.

## 1.1.1 CI 플랫폼 기준

이 초안의 CI 기준은 특정 OS 하나를 대표 플랫폼으로 두지 않는다. 현재 저장소의
`bindings/dotnet/runtimes/`와 `.github/workflows/build.yml`이 함께 관리하는
native runtime 범위를 framework 쪽도 그대로 따른다.

현재 기준의 필수 runtime RID는 아래 여섯 가지다.

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `osx-x64`
- `osx-arm64`

따라서 `.NET` framework의 regression / release gate도 위 여섯 플랫폼을 모두
통과하는 것을 기본으로 본다.

## 1.2 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../policy/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `.NET` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `.NET`과
`ASP.NET Core` 표면으로만 구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `.NET`에서는 public API 전체를 `PascalCase`로
  적고, 단어 구성 자체를 임의로 바꾸지 않는다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish는 기본 blocking submit으로 설명하고, optional non-blocking
  변형이 필요해도 별도 동사 이름을 만들지 않는다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spotName` 기준 생성,
  `spotRid -> spotName` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.

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
| [aspnet-core-channel-messaging.ko.md](./aspnet-core-channel-messaging.ko.md) | channel 등록, handler 프로그래밍 모델, dispatch 흐름, outbound client 사용, lifecycle, middleware/filter |
| [aspnet-core-spot.ko.md](./aspnet-core-spot.ko.md) | SPOT 개념, SpotNode 등록, spot lifecycle, publish/subscribe, discovery |
| [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md) | STREAM 개념, packet/raw session, monitor 기반 lifecycle, recv 비지원 방향 |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | STREAM serializer, write, monitor-event mapping의 결정 기준 |
| [aspnet-core-monitoring.ko.md](./aspnet-core-monitoring.ko.md) | socket/discovery/registry/spot runtime monitoring 이벤트와 등록 모델 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | `playhouse` Stage 같은 상위 모델을 SPOT 위에 감쌀 때 필요한 추가 조건 |
| [aspnet-core-registry.ko.md](./aspnet-core-registry.ko.md) | Registry embedded/standalone 구동, topology 조회, 클러스터링 |

### 2.3 구현 준비 문서

이 문서들은 public API 소개가 아니라, 실제 구현을 어디까지 진행할 수 있는지와
어떤 기준으로 완료를 판단할지를 닫는다.

| 문서 | 다루는 범위 |
|------|------------|
| [behavior-matrix.ko.md](./behavior-matrix.ko.md) | capability 조합별 기대 동작, startup validation, 허용/비허용 조합 |
| [lifecycle-and-failure-semantics.ko.md](./lifecycle-and-failure-semantics.ko.md) | startup/shutdown 순서, fail-fast 규칙, reconnect와 runtime error 의미 |
| [regression-test-matrix.ko.md](./regression-test-matrix.ko.md) | 구현 중 항상 유지해야 할 회귀 테스트 항목, CI 계층, release gate |
| [implementation-scope-and-nongoals.ko.md](./implementation-scope-and-nongoals.ko.md) | 현재 계획 전체 구현 범위, 비목표, 완료 판정 기준 |
| [backend-dependency-policy.ko.md](./backend-dependency-policy.ko.md) | 현재 backend 의존과 향후 저수준 라이브러리 교체 기준 |

### 2.4 샘플 문서

샘플 문서는 등록부터 handler, client 호출까지 한 번에 보여 주는 실행 가능한
코드를 모은다. 인터페이스 정의를 다시 나열하지 않는다.

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | channel 등록, handler, HTTP handler, outbound client를 한 번에 보는 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | room, stage, zone 기준 SPOT 등록, handler, channel send/request, publish를 한 번에 보는 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | STREAM packet/raw session과 등록 코드를 한 번에 보는 샘플 |

### 2.5 범위 원칙

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 인터페이스, attribute, context 전체 정의 | [handler-interfaces](./handler-interfaces.ko.md) | 교차 참조 |
| channel 등록 (AddZLinkFramework), lifecycle | [aspnet-core-channel-messaging](./aspnet-core-channel-messaging.ko.md) | 필요하면 링크 |
| handler/client 사용 예시, dispatch 흐름 | aspnet-core-channel-messaging, 샘플 | |
| SPOT 개념, 등록, lifecycle | [aspnet-core-spot](./aspnet-core-spot.ko.md) | 필요하면 링크 |
| Registry 구동, topology 조회 | [aspnet-core-registry](./aspnet-core-registry.ko.md) | 필요하면 링크 |

## 3. 핵심 방향

- `ASP.NET Core`의 DI와 hosted service 모델에 맞춘다.
- handler, client, filter 생성은 같은 `.NET DI` 컨테이너를 기준으로 맞춘다.
- `channel name` 기준 direct call을 기본으로 둔다.
- gateway나 전용 load balancer 없이 channel별 `Discovery`로 직접 호출한다.
- `SPOT`도 별도 low-level runtime이 아니라, framework lifecycle 안에서
  다룰 수 있어야 한다.
- 일반 channel messaging은 `channelName` 호출을 기본으로 두고, `rid` 지정은
  SPOT spot-to-spot 경로에만 남긴다.
- `SPOT` high-level 표면은 current channel publish/subscribe와 attach된 channel
  send/request를 먼저 설명한다.
- `IZLinkClient`와 `IZLinkSpotClient`는 서로 다른 C API를 감싸는 별도 인터페이스다.
  다만 하부 기능이 겹치는 부분이 있으므로, 두 인터페이스가 일부 비슷한
  send/request 계열 함수를 가질 수 있다.
