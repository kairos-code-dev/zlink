[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Regression Matrix](./regression-test-matrix.ko.md) | [Backend Policy](./backend-dependency-policy.ko.md)

# Draft -- ZLink Framework .NET Implementation Scope And Non-Goals

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET` framework의 현재 계획 구현 범위와 비목표를
> 정리한다.

## 1. 목적

문서가 많아질수록 "설명은 되어 있지만 지금 계획에 포함되는가"가 흐려질 수 있다.
이 문서는 현재 `.NET` 문서 묶음에서 실제로 구현할 범위와, 애초에 framework 기본
범위 밖에 두는 항목을 분리한다.

## 2. 현재 계획 구현 범위

현재 계획에서는 아래 항목을 모두 구현 범위에 둔다.

- `AddZLinkFramework(...)` 등록 루트
- channel `server/client/publisher/subscriber` capability (`EnableServer(...)`,
  `EnableClient(...)`, `EnablePublisher(...)`, `EnableSubscriber(...)` 빌더)
- 채널 등록 분기 — `AddClientServerChannel(...)`, `AddFanoutChannel(...)`,
  `AddDealerMeshChannel(...)`, `AddRouteChannel(...)`, `AddRouteMeshChannel(...)`
- 전역 `UseDiscovery(...)`
- channel manual connection manager (`UseManualConnections(...)`,
  `IZLinkChannelConnectionManager`, `IZLinkEndpointConnections`)
- `IZLinkClient`, `IZLinkEventPublisher`
- `AddSpotMesh(...)`, `AddSpotNode(...)`, `UseSpotDiscovery(...)`
- `IZLinkSpotManager`, `IZLinkSpotClient`, `IZLinkSpotPublisherClient`,
  `IZLinkSpotConnectionManager`
- `[ZLinkHandlerGroup("...")]` 클래스 attribute + channel 등록의
  `channel.MapHandlerGroup("...")` 호출로 이루어지는 handler group mapping 모델.
  `MapHandlersFromAssemblyContaining<TMarker>()` 같은 assembly scan은 보조 표면
  으로만 유지하고, 정식 sample, scope, regression 기준은 group mapping 모델로
  맞춘다.
- spot packet/subscribe/timer descriptor
- `AddStreamNode(...)`와 framework Header 기반 packet session 등록
- `AddZLinkRegistry(...)`, `IZLinkRegistryQuery`(`MemberPeersAsync(string, CancellationToken)`),
  `IZLinkRegistryQueryClient`
- `AddZLinkMonitoring(...)`과 socket/registry/spot source
- `.NET DI`와 hosted service lifecycle 통합
- backend adapter layer와 backend dependency policy 적용
- 기본 codec은 JSON 한 종류로 framework core가 lock-in한다. protobuf, msgpack
  같은 추가 codec은 framework core 패키지가 아니라 별도 codec extension package에서
  제공한다 (예: `Systems.Zlink.Framework.Codec.Protobuf`). sample이 protobuf
  payload를 다루더라도 framework core 자체는 protobuf 의존을 갖지 않는다.
- 저장소가 현재 패키징하는 runtime RID 전체(`win-x64`, `win-arm64`, `linux-x64`,
  `linux-arm64`, `osx-x64`, `osx-arm64`)를 CI gate 범위에 포함

구현 순서는 나눌 수 있어도, 위 항목 중 일부만 따로 떼어 기본 scope 밖으로 두지는
않는다.

## 3. 비목표

아래 항목은 설명이나 배경은 있어도 framework 기본 구현 범위로 두지 않는다.

- worker dispatch 보장, retry, in-flight task semantics
- scatter-gather aggregate helper
- workflow orchestration metadata와 compensation model
- stage wrapper 전용 metadata/membership 모델
- `targetRid + spotRid` direct routed public 호출 표면
- framework 기본 표면의 channel별 typed wrapper
- automatic embedded registry discovery endpoint 추론
- `IHealthCheck` 자동 등록
- `IObservable` 기반 topology event 표면
- framework 기본 패키지 안의 serializer 구현
- preview language feature 의존

## 4. 별도 확장 후보

아래 항목은 framework core와 분리된 별도 패키지나 helper로 두는 편을 기본으로 본다.

- typed client wrapper package
- serializer extension package 묶음
- health check integration package
- stage wrapper 전용 package
- aggregate helper package
- richer monitoring dashboard helper

## 5. 구현 완료 판정 기준

현재 계획 구현 완료는 아래를 뜻한다.

- `implementation scope`에 있는 항목이 모두 코드와 테스트로 존재한다.
- `regression-test-matrix.ko.md`의 release gate를 만족한다.
- `.NET` 문서 샘플이 실제 public surface와 충돌하지 않는다.
- `backend-dependency-policy.ko.md`의 backend leakage 금지 규칙을 어기지 않는다.
- 현재 저장소가 패키징하는 여섯 runtime RID 모두에서 CI 기준이 유지된다.

반대로 use case 문서에 이름이 나온 모든 개념을 다 구현하는 것을 뜻하지는 않는다.
