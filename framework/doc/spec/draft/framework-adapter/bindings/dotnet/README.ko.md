<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework Use Case Validation](../../usecase-validation.ko.md) | [다음: ZLink Framework .NET Interface Catalog](handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Framework Adapter 정책](../../policy/README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel](./aspnet-core-channel-messaging.ko.md) | [SPOT](./aspnet-core-spot.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [Actor](./aspnet-core-actor.ko.md) | [Session Actor Dispatch](./session-actor-dispatch.ko.md) | [Stream Connector](./streaming-client.ko.md) | [Unity Stream Connector](./unity-stream-connector.ko.md) | [STREAM Decisions](./stream-open-items.ko.md) | [Monitoring](./aspnet-core-monitoring.ko.md) | [Registry](./aspnet-core-registry.ko.md) | [Behavior Matrix](./behavior-matrix.ko.md) | [Regression Matrix](./regression-test-matrix.ko.md) | [Lifecycle](./lifecycle-and-failure-semantics.ko.md) | [Scope](./implementation-scope-and-nongoals.ko.md) | [Backend Policy](./backend-dependency-policy.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [STREAM 샘플](./stream-samples.ko.md)

# Draft -- ZLink Framework For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 `ASP.NET Core`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `.NET` 바인딩 위에 올라가는 `ZLink Framework`의 `.NET` 방향을 정리한다.
특히 아래 세 축을 우선 다룬다.

- channel 이름 기준 request/send와 event messaging
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
- `Zlink` vs `ZLink` casing 의도는 다음과 같이 본다.
  - native binding 패키지(`bindings/dotnet/src/Zlink/...`)와 그 안에서 export하는
    raw transport 타입(예: `DealerSocket`, `RouterSocket`, `SpotNode`, `Spot`)은
    `Zlink` namespace 아래에 있다. wire/transport 레벨이다.
  - framework adapter 표면 타입은 `ZLink` prefix로 통일한다 (예:
    `IZLinkSession`, `ZLinkStreamHeader`, `IZLinkActorContext`,
    `IZLinkSessionProxy`). 즉 framework가 사용자에게 노출하는 모든 interface, record,
    enum, exception은 `ZLink`를 쓴다.
  - 패키지 id와 namespace 단어(`Systems.Zlink.*`)는 native binding 규칙을 따른다.
    type 이름과 namespace 이름의 casing 의도는 별개다.
- `zlink.systems` 도메인 기반 package와 namespace는 역순 도메인 규칙을 따른다.
  `.NET` NuGet package id와 namespace는 `Systems.Zlink.*`를 사용한다.
  Unity package id는 lowercase reverse-DNS인 `systems.zlink.*`를 사용한다.
  예를 들어 framework는 `Systems.Zlink.Framework`, Stream Connector는
  `Systems.Zlink.Stream.Connector`, Unity adapter는
  `systems.zlink.stream.connector.unity`를 사용한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send는 기본 async submit으로 설명한다. backpressure는 별도 public no-wait 옵션이
  아니라 nonblocking send, pending queue, ready notification으로 framework 내부에서
  처리한다.
- `SPOT`을 지원하는 문서는 named spot factory 등록, `spotName` 기준 생성,
  `spotRid -> spotName` 조회, lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.
- 현재 framework core 문서에서는 `targetRid + spotRid` direct routed public 호출은
  두지 않는다. 반면 actor join, actor factory 등록, stream-to-actor bridge는 현재
  draft 구현 범위에 포함하므로 공용 계약과 샘플 문서에 함께 반영한다.
- session actor dispatch는 하나의 gateway feature switch가 아니라
  `AddStreamNode(...).AddHeaderSession<TSession>()`, actor factory, actor route
  resolver, actor-session binding, `IZLinkSessionProxy` 조합으로 설명한다. 공개
  resolver 축은 actor와 spot으로 제한하고, session 위치 조회는 별도 public API로
  두지 않는다.

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
| [aspnet-core-actor.ko.md](./aspnet-core-actor.ko.md) | Actor 라이프사이클 (Entry Spot / session bind / user Spot join), handler, IZLinkActorClient, IZLinkSessionProxy, session actor dispatch (gateway) 패턴 |
| [session-actor-dispatch.ko.md](./session-actor-dispatch.ko.md) | session actor dispatch의 .NET 시그니처와 등록 코드 (`IZLinkSessionProxy`, `IZLinkActorClient`, `ZLinkFrameworkException`, builder 시그니처, tic-tac-toe sample). cross-binding 정책은 [policy/session-gateway-usability.ko.md](../../policy/session-gateway-usability.ko.md). |
| [aspnet-core-stream.ko.md](./aspnet-core-stream.ko.md) | STREAM 개념, framework Header 기반 packet session, monitor 기반 lifecycle, recv 비지원 방향 |
| [streaming-client.ko.md](./streaming-client.ko.md) | `.NET` / Unity Stream Connector, TCP/TLS/WS/WSS transport, header/body packet 송수신 |
| [unity-stream-connector.ko.md](./unity-stream-connector.ko.md) | Unity package, `MonoBehaviour` wrapper, main thread callback dispatch, lifecycle |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | STREAM serializer, write, monitor-event mapping의 결정 기준 |
| [aspnet-core-monitoring.ko.md](./aspnet-core-monitoring.ko.md) | socket/registry/spot runtime monitoring 이벤트와 snapshot 조회 모델 |
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
| [stream-samples.ko.md](./stream-samples.ko.md) | STREAM framework Header 기반 packet session과 등록 코드를 한 번에 보는 샘플 |
| [tictactoe-game-sample.ko.md](./tictactoe-game-sample.ko.md) | API 서버, Play 서버, STREAM connector, SPOT actor를 함께 쓰는 틱택토 게임 샘플 초안 (TicTacToe direct + session actor dispatch contract) |

### 2.5 범위 원칙

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 인터페이스, attribute, context 전체 정의 | [handler-interfaces](./handler-interfaces.ko.md) | 교차 참조 |
| channel 등록 (AddZLinkFramework), lifecycle | [aspnet-core-channel-messaging](./aspnet-core-channel-messaging.ko.md) | 필요하면 링크 |
| handler/client 사용 예시, dispatch 흐름 | aspnet-core-channel-messaging, 샘플 | |
| SPOT 개념, 등록, lifecycle | [aspnet-core-spot](./aspnet-core-spot.ko.md) | 필요하면 링크 |
| Actor 라이프사이클, session bind, user Spot join, session actor dispatch | [aspnet-core-actor](./aspnet-core-actor.ko.md) | 필요하면 링크 |
| Registry 구동, topology 조회 | [aspnet-core-registry](./aspnet-core-registry.ko.md) | 필요하면 링크 |

## 3. 핵심 방향

- `ASP.NET Core`의 DI와 hosted service 모델에 맞춘다.
- handler, client, filter 생성은 같은 `.NET DI` 컨테이너를 기준으로 맞춘다.
- `channel name` 기준 direct call을 기본으로 둔다.
- gateway나 전용 load balancer 없이 channel별 `Discovery`로 직접 호출한다.
- channel messaging handler는 attribute scan으로 찾되, 모든 channel에 전역 노출하지
  않는다. `EnableServer(...)` 또는 `EnableSubscriber(...)` 같은 inbound capability
  등록에서 발견된 handler를 어느 channel에 매핑할지 명시한다.
- `[ZLinkRequest]`, `[ZLinkSend]`, `[ZLinkPublish]`는 channel 이름을 받지 않는다.
  channel 이름은 배포와 topology 값이므로 handler attribute가 아니라 channel
  registration이 소유한다.
- `SPOT`도 별도 low-level runtime이 아니라, framework lifecycle 안에서
  다룰 수 있어야 한다.
- 일반 channel messaging은 `channelName` 호출을 기본으로 둔다. spot-to-spot 경로는
  `IZLinkSpotClient.SendSpot(...)` / `RequestSpot(...)`이 spot name/id를 받고,
  resolver가 transport 위치값을 숨긴다.
- `SPOT` high-level 표면은 current channel publish/subscribe, attach된 channel
  send/request, spot name/id 기반 routed send/request를 설명한다.
- `IZLinkClient`와 `IZLinkSpotClient`는 서로 다른 C API를 감싸는 별도 인터페이스다.
  다만 하부 기능이 겹치는 부분이 있으므로, 두 인터페이스가 일부 비슷한
  send/request 계열 함수를 가질 수 있다.
