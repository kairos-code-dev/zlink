[스펙 목차](../../spec/README.ko.md)

[계획 목록](./README.ko.md) | [Framework Adapter 정책](../../spec/draft/framework-adapter/policy/README.ko.md) | [.NET 초안](../../spec/draft/framework-adapter/bindings/dotnet/README.ko.md)

# .NET Framework Adapter 구현 실행 계획

이 문서는 `framework-adapter` `.NET` 초안을 실제 코드로 옮길 때, 구현 도중에
"다음에 무엇을 해야 하는가" 때문에 멈추지 않도록 순서와 완료 기준을 먼저 닫아 둔
내부 실행 문서다.

이 계획에서 framework 구현 루트는
`/home/hep7/project/kairos/zlink/framework/languages/dotnet`으로 고정한다.
즉 framework 코드는 `bindings/dotnet` 아래에 추가하지 않고, 그 바인딩을 backend로
참조하는 별도 계층으로 `framework/languages/dotnet` 아래에 구현한다.

## 1. 목적

이 계획은 아래 세 가지를 한 번에 만족시키는 데 목적이 있다.

1. `framework/doc/spec/draft/framework-adapter/policy/`와
   `framework/doc/spec/draft/framework-adapter/bindings/dotnet/`의 기준을 그대로
   코드 구조로 옮긴다.
2. 구현과 테스트를 따로 미루지 않고, 각 작업 묶음이 끝날 때마다 회귀 항목을 함께
   고정한다.
3. CI gate가 통과한 상태를 최종 완료 기준으로 삼는다.

## 2. 입력 기준 문서

구현 중 판단이 필요하면 아래 순서로 문서를 본다.

1. 공통 정책
   - [policy/README.ko.md](../../spec/draft/framework-adapter/policy/README.ko.md)
   - [overview.ko.md](../../spec/draft/framework-adapter/policy/overview.ko.md)
   - [interaction-model.ko.md](../../spec/draft/framework-adapter/policy/interaction-model.ko.md)
   - [message-model.ko.md](../../spec/draft/framework-adapter/policy/message-model.ko.md)
   - [channel-topology.ko.md](../../spec/draft/framework-adapter/policy/channel-topology.ko.md)
   - [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md)
2. `.NET` 기준 문서
   - [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md)
   - [aspnet-core-channel-messaging.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-channel-messaging.ko.md)
   - [aspnet-core-spot.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-spot.ko.md)
   - [aspnet-core-stream.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-stream.ko.md)
   - [aspnet-core-monitoring.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-monitoring.ko.md)
   - [aspnet-core-registry.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-registry.ko.md)
3. 구현 준비 문서
   - [behavior-matrix.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/behavior-matrix.ko.md)
   - [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md)
   - [regression-test-matrix.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/regression-test-matrix.ko.md)
   - [implementation-scope-and-nongoals.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/implementation-scope-and-nongoals.ko.md)
   - [backend-dependency-policy.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/backend-dependency-policy.ko.md)
4. 샘플 문서
   - [channel-messaging-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/channel-messaging-samples.ko.md)
   - [spot-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/spot-samples.ko.md)
   - [stream-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/stream-samples.ko.md)
5. 저장소 공통 설계/리팩토링 기준
   - [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)

## 2.1 POSD 기준 해석

이 계획에서 말하는 `POSD 기반 리팩토링`은 `AGENTS.md`가 요구하는 저장소 공통
규칙을 따르되, 실제 판단 기준은
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
`POSD Structure Policy`와 `POSD-Based Implementation Completion Policy`를 그대로
쓴다.

즉 이 계획에서 `POSD 기반 리팩토링`은 아래 질문으로 고정한다.

- public 타입이 얕은 래퍼인가
- 같은 능력이 여러 이름이나 여러 타입으로 중복 노출되는가
- 정책 하나를 바꾸려면 여러 파일을 함께 고쳐야 하는가
- 사용자가 내부 호출 순서를 알아야 올바르게 쓸 수 있는가
- native detail이 public 계약으로 새어 나오는가

그리고 POSD 단계 종료 조건은 아래로 고정한다.

- 얕은 래퍼가 남아 있지 않다.
- 변경 파급이 한 모듈로 모인다.
- 정보 은닉이 깨지는 public surface가 남아 있지 않다.
- 해당 작업 묶음 scope 안의 테스트와 샘플이 canonical API를 따른다.

## 3. 구현 범위 고정

이 계획은 아래 항목을 모두 구현 대상으로 본다.

- `AddZLinkFramework(...)` 등록 루트
- channel `server/client/publisher/subscriber`
- `UseDiscovery(...)`, channel connection manager
- `IZLinkClient`, `IZLinkEventPublisher`
- `AddSpotNode(...)`, `UseSpotDiscovery(...)`
- `IZLinkSpotManager`, `IZLinkSpotClient`, `IZLinkSpotPublisherClient`
- `STREAM` node 등록과 packet/raw session
- `AddZLinkRegistry(...)`, `IZLinkRegistryQuery`, `IZLinkRegistryQueryClient`
- `AddZLinkMonitoring(...)`
- DI, hosted service, startup validation, shutdown semantics
- regression test, multi-process test, CI gate

즉 channel만 먼저 끝내고 나머지를 별도 범위로 미루는 방식으로 진행하지 않는다.
구현 순서는 나누되, 최종 범위는 처음부터 위 전체로 고정한다.

## 4. 코드 배치 기준

현재 저장소에는 `framework/languages/dotnet/` 디렉토리가 비어 있고,
`bindings/dotnet/`에는 backend 라이브러리와 기존 테스트가 있다. 이 계획에서는 아래
구조를 기본으로 삼는다.

| 경로 | 역할 |
|------|------|
| `framework/languages/dotnet/src/Zlink.Framework/` | framework 공용 계약, runtime core, adapter layer, registration model |
| `framework/languages/dotnet/src/Zlink.Framework.AspNetCore/` | `IServiceCollection` 확장, hosted service, handler scan, DI 통합 |
| `framework/languages/dotnet/tests/Zlink.Framework.Tests/` | unit + single-process integration |
| `framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/` | multi-process topology, reconnect, discovery, registry, spot |
| `framework/languages/dotnet/testapps/` | multi-process 테스트에 쓰는 작은 generic host test app |

핵심 원칙은 아래와 같다.

- `bindings/dotnet/src/Zlink`는 backend 라이브러리로 사용하고, framework public
  contract를 거기에 섞어 넣지 않는다.
- framework public API는 `framework/languages/dotnet/src/` 아래에서 소유한다.
- backend 교체 가능성은 adapter layer에서 흡수한다.

## 5. 프로젝트 구성 기준

처음 scaffold 단계에서 아래 프로젝트를 만든다.

1. `Zlink.Framework`
   - target frameworks: `net8.0;net10.0`
   - `bindings/dotnet/src/Zlink/Zlink.csproj`를 참조한다.
2. `Zlink.Framework.AspNetCore`
   - `Microsoft.Extensions.DependencyInjection`
   - `Microsoft.Extensions.Hosting`
   - `Microsoft.Extensions.Logging.Abstractions`
   - `Zlink.Framework` 참조
3. `Zlink.Framework.Tests`
   - 테스트 프레임워크는 저장소 기존 `.NET` 테스트와 같은 `xUnit`으로 고정한다.
   - `Zlink.Framework`, `Zlink.Framework.AspNetCore`, `Zlink` 참조
4. `Zlink.Framework.MultiProcessTests`
   - 실제 host startup과 topology를 띄워서 검증한다.
   - 각 테스트는 고정 포트 충돌을 피하도록 동적 endpoint를 쓴다.

테스트 프로젝트는 기능이 완성된 뒤에 추가하지 않는다. scaffold 단계에서 같이 만든다.

solution 엔트리도 한 가지로 고정한다.

- framework 전용 solution 파일은
  `framework/languages/dotnet/Zlink.Framework.sln`으로 만든다.
- CI와 로컬 build/test의 기본 진입점은 이 solution이다.
- 기존 `bindings/dotnet/Zlink.sln`은 backend 바인딩 전용으로 유지한다.

## 6. 단계 공통 실행 루프

모든 작업 묶음은 아래 순서를 같은 방식으로 반복한다.

1. 구현
   - 해당 작업 묶음의 public surface, runtime, adapter, DI 등록을 먼저 코드로 만든다.
2. 리뷰
   - spec과 코드가 어긋나지 않는지 문서 기준으로 다시 읽는다.
   - 공개 이름, lifecycle, validation, backend leakage를 우선 본다.
3. POSD 기반 리팩토링
   - 리뷰에서 드러난 중복, ownership 혼선, 구조 누수를 정리한다.
   - 이 단계에서는 새 기능을 늘리기보다 구조를 spec 기준으로 다시 닫는 데 집중한다.
4. 테스트
   - 해당 작업 묶음에 연결된 `unit`, `integration-single-process`,
     `integration-multi-process` 항목을 바로 추가하거나 갱신한다.
   - 새 테스트 없이 다음 작업 묶음으로 넘어가지 않는다.
5. 커밋
   - 하나의 작업 묶음에서 public surface, 테스트, 필요한 문서 정리가 함께 들어간
     상태로 커밋한다.
6. 푸시
   - 로컬 테스트와 형식 검사를 통과한 커밋만 푸시한다.
   - 푸시 단위는 "한 작업 묶음의 리뷰/리팩토링/테스트가 닫힌 상태"로 맞춘다.

즉 이 계획은 "구현을 여러 묶음 쌓아 둔 뒤 마지막에 한꺼번에 리뷰/리팩토링/테스트"
하는 방식을 허용하지 않는다.

## 7. 브랜치와 푸시 정책

무인 진행을 위해 브랜치와 푸시 정책도 아래처럼 고정한다.

- 작업은 `main` 브랜치에서 직접 진행한다.
- 별도 feature 브랜치나 PR 생성을 이 계획의 필수 단계로 두지 않는다.
- 첫 scaffold 커밋부터 `origin/main` 기준으로 누적 반영한다.
- 이후 각 작업 묶음이 green 상태로 닫힐 때마다 같은 `main` 흐름에 누적 푸시한다.
- remote 인증이나 push 권한이 없으면 그것은 설계 blocker가 아니라 환경 blocker로
  기록하고, 코드/테스트는 로컬 green 상태까지 계속 진행한다.

## 8. 작업 순서

### 8.1 작업 묶음 0: scaffold와 기준선

먼저 아래를 한 번에 만든다.

- `framework/languages/dotnet/Zlink.Framework.sln`
- src/tests/testapps 기본 디렉토리
- 공통 test helper
- 공통 adapter interface 초안
- CI에서 빈 프로젝트도 restore/build/test 되는
  `.github/workflows/framework-dotnet.yml` 최소 workflow

이 단계의 완료 기준은 아래다.

- `dotnet build`가 framework 프로젝트를 포함해 성공한다.
- 빈 테스트 프로젝트가 CI에서 돌아간다.
- backend 참조가 framework public API 바깥으로 새지 않는 기본 구조가 잡힌다.
- scaffold 결과를 리뷰하고, 디렉토리/프로젝트 ownership이 흔들리는 부분을 POSD 기반
  리팩토링으로 정리한다.
- scaffold용 기본 테스트와 build 검증이 통과한 상태로 커밋/푸시한다.

### 8.2 작업 묶음 1: 공용 계약과 registration surface

가장 먼저 고정해야 할 것은 runtime 내부보다 public registration surface다.

이 단계에서 구현할 것:

- `IZLinkFrameworkOptions`
- `IZLinkChannelBuilder`
- `IZLinkSpotNodeBuilder`
- `IZLinkStreamNodeBuilder`
- `UseDiscovery(...)`, `UseSpotDiscovery(...)`
- `UseFilter<TFilter>()`
- startup registration validation

같이 구현할 테스트:

- duplicate registration
- discovery/manual 혼용 금지
- stream session 중복 등록 금지
- `UseSpotDiscovery(...)` 없는 `AddSpotNode(...)` 금지

이 단계가 끝나면 코드에서 `AddZLinkFramework(...)`와 registration validation이 먼저
성립해야 한다.

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- handler-interfaces 기준으로 API 이름과 파라미터를 리뷰한다.
- validation과 builder ownership이 겹치면 POSD 기반 리팩토링으로 다시 정리한다.
- registration/validation 테스트가 모두 통과한다.
- 커밋 메시지는 registration surface와 validation 범위를 직접 드러내게 쓴다.

### 8.3 작업 묶음 2: backend adapter layer

public API와 backend를 떼어 놓는 핵심 단계다.

이 단계에서 구현할 것:

- channel capability adapter
- discovery adapter
- spot node / spot manager adapter
- stream node adapter
- registry adapter
- monitoring adapter

adapter layer 규칙:

- framework service는 backend concrete type를 직접 public property로 내보내지 않는다.
- monitoring과 stream error의 native detail은 optional diagnostic으로만 노출한다.
- `RoutingId`, `Message`, `SendFlags` 같은 primitive만 public contract에 남긴다.

이 단계가 끝나면 이후 기능 구현은 adapter 뒤에서만 backend를 호출한다.

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- backend dependency policy 기준으로 leakage를 리뷰한다.
- concrete backend type가 public contract 근처로 새면 POSD 기반 리팩토링으로 adapter
  경계를 다시 자른다.
- adapter 단위 테스트와 최소 happy-path integration test를 같이 통과시킨다.
- adapter layer만 따로 쪼개진 독립 커밋으로 남긴다.

### 8.4 작업 묶음 3: host lifecycle과 validation

이 단계는 나중으로 미루면 startup/shutdown semantics가 계속 흔들린다.

이 단계에서 구현할 것:

- `AddZLinkFramework(...)` DI 등록
- hosted service startup 순서
- fail-fast validation
- shutdown drain / dispose 순서
- channel runtime의 startup 생성
- topology/discovery source 이름 검증

같이 구현할 테스트:

- invalid configuration fail-fast
- registry bind 뒤 discovery 시작
- shutdown 시 dispose 순서

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- lifecycle-and-failure-semantics 기준으로 startup/shutdown/error 흐름을 리뷰한다.
- hosted service ownership이나 startup 순서가 흐리면 POSD 기반 리팩토링으로 정리한다.
- fail-fast와 shutdown 순서 테스트를 통과시킨다.
- lifecycle과 validation이 같이 닫힌 상태로 커밋/푸시한다.

### 8.5 작업 묶음 4: channel messaging

가장 먼저 사용자-facing happy path가 나와야 하는 축이다.

이 단계에서 구현할 것:

- request handler dispatch
- send handler dispatch
- `IZLinkClient`
- `IZLinkEventPublisher`
- channel subscriber runtime
- filter pipeline
- HTTP handler에서 같은 DI container로 client 사용
- manual connection manager

같이 구현할 테스트:

- discovery client request/send
- manual client request/send
- publisher-only channel
- subscriber remote publish 수신
- filter 적용 순서
- HTTP handler 안에서 `IZLinkClient` 정상 동작

샘플 문서와 반드시 맞춰야 할 파일:

- `aspnet-core-channel-messaging.ko.md`
- `channel-messaging-samples.ko.md`

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- channel spec, 샘플, 실제 API를 함께 리뷰한다.
- dispatch/filter/client ownership이 섞이면 POSD 기반 리팩토링으로 나눈다.
- happy-path와 대표 failure-path 테스트를 같이 통과시킨다.
- channel 문서 샘플이 실제 API와 맞는 상태로 커밋/푸시한다.

### 8.6 작업 묶음 5: SPOT

SPOT은 channel 위에 덧붙는 기능이 아니라 별도 lifecycle과 실행 문맥을 가진다.
그래서 channel 다음 독립 단계로 본다.

이 단계에서 구현할 것:

- `AddSpotNode(...)`
- `IZLinkSpotManager`
- named spot factory 등록
- `ZLinkSpot` base class
- packet / subscribe / timer registration
- per-spot scope resolve
- `IZLinkSpotClient`
- `IZLinkSpotPublisherClient`
- attach channel client
- SPOT connection manager

같이 구현할 테스트:

- duplicate `spotName` factory
- `CreateAsync`, `GetAsync`, `ListAsync`, `RemoveAsync`
- `OnInitializeAsync(...)`와 per-spot scope
- local spot publish
- outbound-only publish client
- scope dispose 후 callback 중지

샘플 문서와 반드시 맞춰야 할 파일:

- `aspnet-core-spot.ko.md`
- `spot-samples.ko.md`
- `stage-wrapper-on-spot.ko.md`

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- spot lifecycle, manager ownership, per-spot scope를 문서 기준으로 리뷰한다.
- spot instance/context/attached client 경계가 흐리면 POSD 기반 리팩토링으로 다시
  자른다.
- create/list/remove, initialize, publish 관련 테스트를 모두 통과시킨다.
- SPOT 샘플과 stage wrapper 문서까지 같이 맞춘 뒤 커밋/푸시한다.

### 8.7 작업 묶음 6: STREAM

`STREAM`은 recv loop를 application 앞면에 노출하지 않는 방향이므로, session dispatch
중심으로 구현한다.

이 단계에서 구현할 것:

- `AddStreamNode(...)`
- packet session dispatch
- raw session dispatch
- `IZLinkStream` metadata 표면
- `Write(...)` overload
- `OnConnectedAsync(...)`, `OnDisconnectedAsync(...)`, `OnErrorAsync(...)`
- session serial execution 보장

같이 구현할 테스트:

- packet session
- raw session
- `ConnectionReady -> OnConnectedAsync(...)`
- session-correlatable transport error만 `OnErrorAsync(...)`
- peer metadata 값

샘플 문서와 반드시 맞춰야 할 파일:

- `aspnet-core-stream.ko.md`
- `stream-open-items.ko.md`
- `stream-samples.ko.md`

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- stream session lifecycle과 error contract를 리뷰한다.
- session serial execution, write overload, diagnostic ownership이 흔들리면 POSD
  기반 리팩토링으로 정리한다.
- packet/raw/connected/error/metadata 테스트를 통과시킨다.
- STREAM 문서와 샘플을 같은 커밋 범위에서 맞춘다.

### 8.8 작업 묶음 7: Registry와 Monitoring

운영 표면은 마지막에 붙이는 부가기능이 아니라 release gate에 직접 들어가는 항목이다.

이 단계에서 구현할 것:

- `AddZLinkRegistry(...)`
- `IZLinkRegistryQuery`
- `IZLinkRegistryQueryClient`
- `AddZLinkMonitoring(...)`
- socket/discovery event bridge
- registry/spot polling diff

같이 구현할 테스트:

- embedded registry startup
- remote query client
- monitoring source validation
- registry polling diff
- spot polling diff

샘플 문서와 반드시 맞춰야 할 파일:

- `aspnet-core-registry.ko.md`
- `aspnet-core-monitoring.ko.md`

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- monitoring event kind, optional diagnostic, polling diff semantics를 리뷰한다.
- source naming이나 runtime event ownership이 섞이면 POSD 기반 리팩토링으로
  framework-owned event 모델을 다시 닫는다.
- registry/query/monitoring 테스트를 통과시킨다.
- 운영 문서와 테스트를 함께 정리한 상태로 커밋/푸시한다.

### 8.9 작업 묶음 8: 샘플 정합성 정리

실제 구현 public surface가 잡히면 샘플이 drift 나기 쉽다. 이 단계에서 아래를 같이
정리한다.

- 문서 샘플과 실제 API 이름 대조
- sample 검증용 doc fixture test app 정리
- README 링크와 패키지 참조 예시 정리

완료 기준:

- 샘플 코드가 더 이상 "설명용 의사코드"가 아니라 실제 public surface와 충돌하지
  않는다.

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- 문서 샘플 전체를 다시 리뷰한다.
- drift가 큰 구간은 POSD 기반 리팩토링으로 API 또는 샘플 구조를 다시 맞춘다.
- 샘플 검증용 빌드 또는 doc fixture 테스트를 통과시킨다.
- 샘플 정합성만 따로 식별 가능한 커밋으로 푸시한다.

### 8.10 작업 묶음 9: release gate 고정

마지막 단계는 기능 추가가 아니라 "완료 판정"을 자동화하는 단계다.

이 단계에서 구현할 것:

- CI matrix
- platform별 test command
- release gate job
- failure triage 규칙 문서화

완료 기준:

- 회귀 테스트 문서에 있는 항목이 CI에서 실제 job이나 test case 이름으로 대응된다.
- platform 하나라도 빠지면 release gate가 실패한다.

이 작업 묶음은 아래까지 끝나야 닫힌 것으로 본다.

- regression-test-matrix와 실제 CI job 구성을 리뷰한다.
- matrix, artifact, gate 흐름이 중복되면 POSD 기반 리팩토링으로 workflow를 정리한다.
- 로컬과 CI에서 release gate를 모두 검증한다.
- 최종 커밋/푸시는 "기능 완료"가 아니라 "gate까지 닫힘" 상태에서만 한다.

## 9. multi-process harness 기준

multi-process 테스트는 아래 방식으로 고정한다.

- test app은 모두 `framework/languages/dotnet/testapps/` 아래의 작은 generic host
  console app으로 만든다.
- 각 test app은 startup 완료 시 stdout 한 줄로 `READY:<json>` 형태의 readiness
  marker를 반드시 출력한다.
- test runner는 readiness marker가 나오기 전까지 최대 30초 기다린다.
- endpoint는 테스트마다 동적으로 할당하고, 고정 포트를 쓰지 않는다.
- stdout/stderr는 모두 테스트 로그 디렉토리에 저장한다.
- 종료는 먼저 정상 종료 신호를 보내고, 10초 안에 끝나지 않으면 kill 한다.
- 실패 시 마지막 200줄 로그를 테스트 결과에 같이 남긴다.

즉 multi-process 테스트는 사람이 콘솔을 보면서 수동 확인하는 방식으로 진행하지
않는다.

## 10. 테스트 전략

테스트는 처음부터 아래 세 계층으로 고정한다.

| 계층 | 위치 | 목적 |
|------|------|------|
| `unit` | `Zlink.Framework.Tests` | registration, validation, dispatch lookup, option parsing |
| `integration-single-process` | `Zlink.Framework.Tests` | same-host runtime, DI, hosted service, HTTP handler 통합 |
| `integration-multi-process` | `Zlink.Framework.MultiProcessTests` | 실제 topology, reconnect, discovery change, remote registry, remote publish |

중요한 원칙은 아래다.

- 새 public surface는 같은 작업 묶음 안에서 최소 `unit` 또는 `single-process`
  테스트를 같이 만든다.
- topology, reconnect, discovery 변화처럼 process 경계가 의미인 항목은
  `multi-process`로만 검증한다.
- 기존 `bindings/dotnet/tests/Zlink.Tests`는 backend 보장 테스트로 보고, framework
  테스트가 그 위의 registration, lifecycle, DI, dispatch를 따로 검증한다.

## 11. 커밋과 푸시 규칙

커밋과 푸시는 아래 원칙을 따른다.

- 커밋 하나는 하나의 작업 묶음 또는 그 안의 명확한 하위 단위를 닫아야 한다.
- 코드만 먼저 커밋하고 테스트를 다음 커밋으로 미루지 않는다.
- spec 반영이 필요한 변경은 코드와 문서가 같은 커밋 묶음 안에 들어가야 한다.
- 푸시는 최소한 해당 작업 묶음의 리뷰와 POSD 기반 리팩토링, 테스트가 끝난 뒤에만
  한다.
- CI red 상태의 중간 커밋을 `main`에 푸시하는 흐름은 허용하지 않는다.

## 12. CI 기준

framework CI는 아래 기준을 만족해야 한다.

- target framework
  - `net8.0`
  - `net10.0`
- runtime RID
  - `win-x64`
  - `win-arm64`
  - `linux-x64`
  - `linux-arm64`
  - `osx-x64`
  - `osx-arm64`
- test mode
  - `Debug`
  - `Release`

workflow 파일명과 명령도 아래처럼 고정한다.

- workflow 파일: `.github/workflows/framework-dotnet.yml`
- restore:
  - `dotnet restore /home/hep7/project/kairos/zlink/framework/languages/dotnet/Zlink.Framework.sln`
- build:
  - `dotnet build /home/hep7/project/kairos/zlink/framework/languages/dotnet/Zlink.Framework.sln -c Debug`
  - `dotnet build /home/hep7/project/kairos/zlink/framework/languages/dotnet/Zlink.Framework.sln -c Release`
- unit + single-process test:
  - `dotnet test /home/hep7/project/kairos/zlink/framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug -f net8.0`
  - `dotnet test /home/hep7/project/kairos/zlink/framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Release -f net10.0`
- multi-process test:
  - `dotnet test /home/hep7/project/kairos/zlink/framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0`
  - `dotnet test /home/hep7/project/kairos/zlink/framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net10.0`

플랫폼 기준을 낮추지 않는 이유는 간단하다. `bindings/dotnet` native runtime이 이미
해당 플랫폼을 지원하더라도, framework는 그 위에 DI, hosted service, monitoring,
registration validation 계층을 추가하므로 platform별 차이를 다시 확인해야 한다.

기존 저장소 전체 workflow가 이미 red인 경우도 있을 수 있다. 이 계획에서는 그 상태를
framework 설계 blocker로 보지 않는다. 완료 판정의 직접 gate는
`.github/workflows/framework-dotnet.yml`과 그 안의 framework 전용 build/test job으로
고정한다.

즉 최종 완료는 아래 둘을 구분해서 본다.

- framework 완료 gate
  - `framework-dotnet.yml` green
- 저장소 전체 baseline 상태
  - 별도 운영 리스크로 기록

다른 workflow의 기존 red가 framework 변경과 무관하다면, 그것만으로 framework 작업
완료 판정을 막지 않는다. 다만 PR 설명과 최종 보고에는 해당 baseline red 상태를
명시적으로 기록한다.

## 13. 구현 중 문서 처리 규칙

구현 중 아래 상황이 나오면 멈추지 말고 같은 작업 묶음 안에서 문서와 코드를 함께
정리한다.

1. spec와 code가 충돌하면
   - 먼저 어떤 문서가 기준인지 확인한다.
   - 문서가 분명히 잘못됐으면 draft spec을 먼저 수정한다.
   - 그 다음 코드를 맞춘다.
2. 샘플 문서가 실제 API와 어긋나면
   - 샘플을 즉시 같이 고친다.
3. backend leakage가 보이면
   - public API를 늘리기 전에 adapter layer나 framework-owned type으로 흡수한다.

즉 "코드는 먼저 두고 문서는 나중에" 방식으로 진행하지 않는다.

## 14. 중단 조건

아래 경우만 구현 진행을 잠시 멈추고 판단을 다시 한다.

- `bindings/dotnet` backend가 실제로 제공하지 않는 capability를 framework spec이
  필수로 요구하는 경우
- 여섯 runtime RID 중 하나에서 backend 자체가 동작하지 않아 framework 계층으로는
  해결할 수 없는 경우
- public contract를 깨지 않고는 구현할 수 없는 구조 충돌이 새로 드러난 경우

그 외에는 작업 묶음 안에서 문서 수정, adapter 조정, 테스트 추가로 계속 진행한다.

## 15. 완료 기준

이 계획에서 "구현과 테스트가 완료됐다"는 말은 아래를 모두 뜻한다.

1. `implementation-scope-and-nongoals.ko.md`에 있는 항목이 모두 코드로 존재한다.
2. `regression-test-matrix.ko.md`의 항목이 테스트로 고정된다.
3. `behavior-matrix.ko.md`의 비허용 조합이 startup validation 또는 runtime test로
   모두 막힌다.
4. 각 작업 묶음마다 리뷰와 POSD 기반 리팩토링이 끝난 상태가 기록으로 남아 있다.
5. 문서 샘플이 실제 public surface와 충돌하지 않는다.
6. `backend-dependency-policy.ko.md`의 leakage 금지 규칙을 어기지 않는다.
7. `net8.0`, `net10.0`과 여섯 runtime RID 전체에서 CI gate가 통과한다.
8. 마지막 상태가 테스트 통과 후 커밋/푸시까지 끝난 기준선 상태다.

## 16. 바로 시작할 첫 작업

실제 구현 착수는 아래 순서로 시작한다.

1. `framework/languages/dotnet/src/`, `tests/`, `testapps/` scaffold
2. `Zlink.Framework`, `Zlink.Framework.AspNetCore` 프로젝트 생성
3. `framework/languages/dotnet/Zlink.Framework.sln` 생성
4. registration surface와 startup validation 구현
5. channel happy path + unit/single-process test 고정

이 다섯 가지가 첫 커밋 묶음으로 잡히면, 그 다음부터는 위 작업 묶음 순서대로
중단 없이 진행할 수 있다.
