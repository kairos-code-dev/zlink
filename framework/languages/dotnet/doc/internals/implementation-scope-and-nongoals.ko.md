<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework .NET Regression Test Matrix](./regression-test-matrix.ko.md) | [다음: ZLink Framework .NET Backend Dependency Policy](./backend-dependency-policy.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[.NET 묶음](../README.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Regression Matrix](./regression-test-matrix.ko.md) | [Backend Policy](./backend-dependency-policy.ko.md)

# ZLink Framework .NET Implementation Scope And Non-Goals

## 1. 목적

문서가 늘어나다 보면 "설명은 있지만 정작 지금 구현 계획에 들어가는가" 라는 점이
흐려지기 쉽다. 이 문서는 그 경계를 분명히 한다. 즉 현재 `.NET` 문서 묶음에서
실제로 구현 대상으로 잡은 항목과, 처음부터 framework 기본 범위 밖으로 빼 둔
항목을 한 자리에서 구분한다.

## 2. 현재 계획 구현 범위

지금 계획에서는 아래 항목을 모두 구현 범위 안에 둔다.

- `AddZLinkFramework(...)` 등록 루트
- channel 의 `server / client / publisher / subscriber` capability[^capability]
  (`EnableServer(...)`, `EnableClient(...)`, `EnablePublisher(...)`,
  `EnableSubscriber(...)` 빌더)
- 채널 등록의 형태별 분기 — `AddClientServerChannel(...)`,
  `AddFanoutChannel(...)`, `AddDealerMeshChannel(...)`, `AddRouteMeshChannel(...)`
- 전역 `UseDiscovery(...)` 설정
- channel 의 startup manual connection 설정(`UseManualConnections(...)`)
- 클라이언트/퍼블리셔 표면인 `IZLinkChannelClient`, `IZLinkFanoutClient`
- `AddSpotMesh(...)`, `mesh.AddNode(...)` 같은
  `SPOT`[^spot] 등록 표면
- `IZLinkSpotManager`, `IZLinkSpotClient`, `IZLinkSpotPublisherClient`
- handler group mapping 모델. 즉 `[ZLinkHandlerGroup("...")]` 클래스
  attribute[^attribute] 와 channel 등록 쪽의 `channel.AddHandlerGroup("...")`
  호출을 짝으로 두는 모델이다.
  `MapHandlersFromAssemblyContaining<TMarker>()` 같은 assembly 전체
  scan[^assembly-scan] 은 보조 수단으로만 남긴다. 정식 sample, scope, regression
  기준은 group mapping 모델에 맞춘다.
- spot 의 packet / subscribe / timer descriptor
- `AddStreamNode(...)` 와 framework Header 기반 packet session 등록
- `AddZLinkRegistry(...)`, `IZLinkRegistryQuery`
  (`MemberPeersAsync(string, CancellationToken)`), `IZLinkRegistryQueryClient`
- `AddZLinkMonitoring(...)` 과 socket / registry / spot 모니터링 source
- `.NET DI`[^di] 와 hosted service[^hosted-service] lifecycle 통합
- backend adapter layer[^backend-adapter] 와 backend dependency policy 적용
- 기본 codec[^codec] 은 framework core 가 JSON 하나로 고정(lock-in) 한다.
  protobuf, msgpack 같은 추가 codec 은 framework core 패키지에 두지 않는다.
  대신 별도의 codec extension package
  (예: `Systems.Zlink.Framework.Codec.Protobuf`) 에서 제공한다. sample 이
  protobuf payload 를 다루더라도, framework core 자체가 protobuf 에 의존하게
  만들지는 않는다.
- 저장소가 지금 함께 패키징하는 runtime RID[^rid] 여섯 가지
  (`win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `osx-x64`, `osx-arm64`)
  를 모두 CI[^ci] gate 범위 안에 둔다.

구현 순서는 단계를 나눠도 된다. 다만 위 항목 중 일부만 따로 떼어 기본 scope
밖으로 밀어 두지는 않는다.

## 3. 비목표

아래 항목은 본문에 설명이나 배경 설명이 있더라도, framework 기본 구현 범위에는
포함하지 않는다.

- worker dispatch 보장, retry, in-flight task semantics
- scatter-gather aggregate helper[^scatter-gather]
- workflow orchestration[^workflow-orchestration] 메타데이터와 compensation 모델
- stage wrapper[^stage-wrapper] 전용 metadata / membership 모델
- `targetRid + spotId` 형태의 direct routed public 호출 표면
- framework 기본 표면에 두는 channel 별 typed wrapper
- automatic embedded registry discovery endpoint 추론
- `IHealthCheck` 자동 등록
- `IObservable` 기반 topology[^topology] event 표면
- framework 기본 패키지 안에 포함된 serializer 구현체
- preview 단계 언어 기능(`preview language feature`) 의존

## 4. 별도 확장 후보

아래 항목들은 framework core 와 분리해서 별도 패키지나 helper 로 두는 편을
기본으로 본다. 즉 필요하면 확장 패키지로 추가하되, framework core 자체에는
끌어들이지 않는다.

- typed client wrapper 패키지
- serializer extension 패키지 묶음
- health check 통합 패키지
- stage wrapper 전용 패키지
- aggregate helper 패키지
- 더 풍부한 monitoring dashboard helper

## 5. 구현 완료 판정 기준

지금 계획의 "구현 완료" 는 다음을 모두 만족하는 상태를 뜻한다.

- `implementation scope` 에 적힌 항목이 모두 코드와 테스트로 존재한다.
- `regression-test-matrix.ko.md` 의 release gate[^release-gate] 를 통과한다.
- `.NET` 문서의 샘플이 실제 public surface[^public-surface] 와 어긋나지 않는다.
- `backend-dependency-policy.ko.md` 가 정한 backend leakage[^backend-leakage]
  금지 규칙을 어기지 않는다.
- 저장소가 패키징하는 여섯 runtime RID 전부에서 CI 기준이 유지된다.

반대로, use case 문서에 이름이 한 번이라도 나온 모든 개념을 빠짐없이 구현해야
한다는 의미는 아니다.

## 6. 회귀 테스트

구현 범위와 비목표는 public surface 가 의도치 않게 부풀어 오르는 것을 막아 주는
회귀 테스트와 함께 관리한다. 즉 새 API 가 추가되면, 이 문서의 범위 표와 public
surface 테스트를 같이 갱신해야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `ScaffoldSmokeTests.PublicSurface_Removes_DirectRouteContracts_And_Exposes_ActorContracts` | 비목표인 direct route public 호출은 사라지고, actor / session 표면은 그대로 남아 있다. |
| `ScaffoldSmokeTests.PublicSurface_DoesNotExpose_BackendConcreteTypes` | backend 교체 범위 밖의 concrete type 이 public API 로 새어 나오지 않는다. |
| `BackendAdapterFactoryTests.BackendFactory_Creates_Channel_Registry_Spot_And_Stream_Wrappers` | 현재 구현 범위인 channel, Registry, SPOT, STREAM backend wrapper 가 정상적으로 생성된다. |
| `RegressionTests.DotNetDraftDocuments_AllExposeRegressionTestSection` | 범위 문서를 포함한 모든 draft 문서가 회귀 테스트 단락을 유지한다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^capability]: capability 는 어떤 노드(channel, spot 등)가 외부에 노출하는 역할이나 기능 단위(예: server, client, publisher, subscriber)를 가리킨다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다.
[^attribute]: attribute 는 `.NET` 에서 타입이나 메서드에 부가 메타데이터를 붙이는 선언 문법이다. framework 는 이 attribute 를 보고 handler 를 찾는다.
[^assembly-scan]: assembly scan 은 어셈블리에 정의된 타입을 모두 훑어 보면서 조건에 맞는 항목을 찾아 등록하는 방식이다.
[^di]: DI(Dependency Injection) 는 객체가 필요로 하는 의존성을 컨테이너가 대신 만들어 주입하는 패턴이다. `.NET DI` 는 `Microsoft.Extensions.DependencyInjection` 의 컨테이너 모델을 가리킨다.
[^hosted-service]: hosted service 는 `ASP.NET Core` 호스트가 시작·종료될 때 함께 시작·종료되는 백그라운드 컴포넌트를 뜻한다(`IHostedService`).
[^backend-adapter]: backend adapter layer 는 framework 가 사용자에게 보여 주는 표면과 실제 저수준 라이브러리(backend) 사이를 연결해 주는 중간 계층이다. 이 계층 덕분에 backend 가 바뀌어도 public API 가 흔들리지 않는다.
[^codec]: codec 은 메시지 payload 를 직렬화·역직렬화하는 방식(JSON, protobuf, msgpack 등)을 가리킨다.
[^rid]: RID(Runtime Identifier) 는 `.NET` 이 OS·CPU 조합을 식별하는 문자열이다. 예: `win-x64`, `linux-arm64`.
[^ci]: CI(Continuous Integration) 는 코드 변경마다 자동으로 빌드와 테스트를 돌려 회귀를 빠르게 잡아내는 파이프라인을 가리킨다.
[^scatter-gather]: scatter-gather 는 한 요청을 여러 노드로 흩뿌리고(scatter) 결과를 다시 모으는(gather) 호출 패턴이다.
[^workflow-orchestration]: workflow orchestration 은 여러 단계를 가진 작업 흐름을 조율하고, 실패 시 보상 동작(compensation) 까지 묶어서 관리하는 모델이다.
[^stage-wrapper]: stage wrapper 는 `playhouse` 의 Stage 같은 상위 모델을 SPOT 위에 얹어서 쓰기 위한 추가 추상이다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^release-gate]: release gate 는 새 버전을 배포하기 전에 반드시 통과해야 하는 검증 단계(테스트, 빌드, 점검)의 묶음이다.
[^public-surface]: public surface 는 외부 사용자에게 노출되는 모든 타입·메서드·attribute 의 총합을 가리킨다.
[^backend-leakage]: backend leakage 는 framework 내부에서만 써야 할 backend 구현 타입이 public API 로 새어 나오는 현상을 가리킨다.
