<!-- framework-adapter-nav:start -->
[문서 목록](../../../doc/README.ko.md) | [다음: ZLink Framework for .NET — 개요](./guide/01-overview.ko.md)
<!-- framework-adapter-nav:end -->

[Framework 문서](../../../doc/README.ko.md) | [공통 스펙](../../../doc/spec/README.ko.md)

[공통 스펙](../../../doc/spec/README.ko.md) | [비동기 실행](../../../doc/spec/async-execution-policy.ko.md) | [인터페이스](./spec/handler-interfaces.ko.md) | [channel](./spec/aspnet-core-channel-messaging.ko.md) | [SPOT](./spec/aspnet-core-spot.ko.md) | [SpotNode](./spec/spot-node.ko.md) | [Stage wrapper](./spec/stage-wrapper-on-spot.ko.md) | [STREAM](./spec/aspnet-core-stream.ko.md) | [Actor](./spec/aspnet-core-actor.ko.md) | [Session Actor Dispatch](./spec/session-actor-dispatch.ko.md) | [Stream Connector](./guide/samples/streaming-client.ko.md) | [Unity 가이드](../../../../doc/guide/unity-stream-connector.ko.md) | [Monitoring](./spec/aspnet-core-monitoring.ko.md) | [Registry](./spec/aspnet-core-registry.ko.md) | [Behavior Matrix](./internals/behavior-matrix.ko.md) | [DI Capability](./internals/di-capability-exposure-policy.ko.md) | [Regression Matrix](./internals/regression-test-matrix.ko.md) | [Lifecycle](./internals/lifecycle-and-failure-semantics.ko.md) | [Scope](./internals/implementation-scope-and-nongoals.ko.md) | [Backend Policy](./internals/backend-dependency-policy.ko.md) | [channel 샘플](./guide/samples/channel-messaging-samples.ko.md) | [SPOT 샘플](./guide/samples/spot-samples.ko.md) | [STREAM 샘플](./guide/samples/stream-samples.ko.md)

# ZLink Framework for .NET

> 이 묶음은 `.NET`/`ASP.NET Core`의 정식 `ZLink Framework` 문서다. 문서는
> `guide/`(샘플·튜토리얼), `spec/`(공개 계약), `internals/`(구현·검증 기준),
> `draft/`(미확정 항목)로 나뉜다. 공통 의미는 [공통 스펙](../../../doc/spec/README.ko.md)을
> 따르며, 여기서는 그 의미를 `.NET` 표면으로만 구체화한다.

## 1. 목적

이 문서는 `.NET` 바인딩 위에 올려 둘 `ZLink Framework`의 `.NET` 표면을 정리한다.
다루는 축은 다음 세 가지를 우선으로 한다.

- channel 이름을 기준으로 한 request / send 와 event messaging[^channel-messaging]
- `SPOT`[^spot]을 `ASP.NET Core` 애플리케이션에서 다루는 방법
- Registry 서버를 `ASP.NET Core`의 lifecycle 안에서 띄우고 topology[^topology]를 조회하는 방법

지금 단계의 목표는 새 runtime 을 만드는 것이 아니다. 기존 `.NET` 바인딩은 이미
`Discovery`, `DealerSocket`, `RouterSocket`, `SpotNode`, `Spot`, `Registry`
같은 기본 기능을 제공한다. 이 기능들을 그대로 활용하되, framework 사용자에게는
익숙한 DI, hosted service[^hosted-service], handler 모델로 감싸서 노출한다.

현재 구현 backend 는 `bindings/dotnet` 을 그대로 쓴다. 다만 framework 가
사용자에게 보여 주는 public contract 는 backend 구현체와 분리해서 유지하는 것을
원칙으로 둔다. 자세한 기준은
[backend-dependency-policy.ko.md](./internals/backend-dependency-policy.ko.md) 에서 다룬다.

## 1.1 지원 버전 기준

이 `.NET` 문서는 다음과 같이 버전 기준을 먼저 고정한다.

- 최소 지원 런타임: `.NET 8` (`net8.0`)
- 주 개발 기준: `.NET 10` (`net10.0`)
- 최소 지원 언어 버전: `C# 12`

따라서 이 디렉토리의 문서와 샘플은 위 최소 지원 기준에서 바로 컴파일·실행이
가능한 표면을 우선해서 설명한다. `C# 13`, `C# 14`, `preview`, `latest` 전용
문법이나 API 는 공개 framework 계약의 전제 조건으로 삼지 않는다.

## 1.1.1 CI 플랫폼 기준

이 문서의 CI[^ci] 기준은 특정 OS 하나를 대표 플랫폼으로 두지 않는다. 대신 저장소
안의 `bindings/dotnet/runtimes/` 와 `.github/workflows/build.yml` 이 이미 함께
관리하고 있는 native runtime 범위를 framework 쪽에서도 그대로 따른다.

현재 기준으로 반드시 지원해야 하는 runtime RID[^rid] 는 다음 여섯 가지다.

- `win-x64`
- `win-arm64`
- `linux-x64`
- `linux-arm64`
- `osx-x64`
- `osx-arm64`

따라서 `.NET` framework 의 regression 테스트와 release gate[^release-gate] 도
위 여섯 플랫폼을 모두 통과하는 것을 기본 조건으로 본다.

## 1.2 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../../doc/spec/README.ko.md) 과 그 하위 문서를 그대로
따른다. 즉 `.NET` 상세 문서는 공통 의미를 새로 정의하지 않는다. 이미 정해진
의미를 `.NET` 과 `ASP.NET Core` 표면에서 어떻게 구체화할지만 다룬다.

특히 다음 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)
  의 `Naming Policy`를 그대로 따른다. `.NET`에서는 public API 전체를
  `PascalCase`로 적고, 단어 구성 자체를 임의로 바꾸지 않는다.
- `Zlink` 와 `ZLink` 의 casing 의도는 다음과 같이 본다.
  - native binding 패키지(`bindings/dotnet/src/Zlink/...`)와 그 안에서
    export 하는 raw transport[^raw-transport] 타입(예: `DealerSocket`,
    `RouterSocket`, `SpotNode`, `Spot`)은 `Zlink` namespace 아래에 둔다. 즉
    wire/transport[^wire-transport] 레벨이다.
  - framework adapter 표면 타입은 `ZLink` prefix로 통일한다. 예를 들어
    `IZLinkSession`, `IZLinkActorContext`, `IZLinkBoundSession` 같은 형태다.
    즉 framework가 사용자에게 노출하는 모든
    interface, record, enum, exception 은 `ZLink`를 쓴다.
  - 패키지 id 와 namespace 단어(`Systems.Zlink.*`)는 native binding 규칙을
    따른다. 타입 이름의 casing 의도와 namespace 이름의 casing 의도는 서로
    별개다.
- `zlink.systems` 도메인을 기반으로 한 package 와 namespace는 역순 도메인
  규칙[^reverse-dns]을 따른다. `.NET`의 NuGet[^nuget] package id 와 namespace는
  `Systems.Zlink.*`를 사용한다. 예를 들어 framework는
  `Systems.Zlink.Framework`, Stream Connector는 `Systems.Zlink.Stream.Connector`가 된다.
- 수동 연결은 `channel + capability`[^capability] 또는 `spot node + capability`
  단위로 설명한다. 같은 역할 안에서는 `Discovery` 기반 자동 연결과 manual
  연결을 섞지 않는다.
- send 는 기본적으로 async submit 으로 설명한다. backpressure[^backpressure]는
  public no-wait 옵션을 따로 두지 않고, nonblocking send 와 pending queue,
  ready notification 을 활용해서 framework 내부에서 처리한다.
- `CancellationToken` 은 실제로 기다릴 수 있고 그 대기를 취소할 수 있는 public
  async 경계에만 둔다. request / actor join / channel submit / SPOT submit /
  stream connector write 처럼 queue, retry, transport write, reply 대기가 있는
  API 는 token 을 받는다. 반대로 session reply frame 작성, session close, timer
  cancel 처럼 현재 구현이 즉시 완료되거나 자체 종료 토큰으로 정리되는 API 는
  token 을 받지 않는다. token 을 받는다면 시작 전 검사만 하지 말고 실제 대기
  지점에 이어 주어야 한다.
- `SPOT` 을 다루는 문서는 Spot 타입 기준 factory 등록, `RoutingId` 기준 생성과 조회,
  lifecycle timer, 외부 spot publish 표면을 공통
  정책과 맞춰 설명해야 한다.
- 현재 framework core 문서에는 `targetRid + spotId` 형태의 direct routed public
  호출은 두지 않는다. 반면 actor join, actor factory 등록,
  stream-to-actor bridge[^stream-actor-bridge]는 현재 draft 구현 범위에 포함하므로
  공용 계약과 샘플 문서에 함께 반영한다.
- session actor dispatch[^session-actor-dispatch] 는 단일 gateway feature switch
  하나를 켜고 끄는 형태가 아니다. 대신
  `AddStreamNode` 뒤의 `RegisterSession<TSession>()`, actor factory, actor
  logical actor binding, actor-session binding, `IZLinkBoundSession` 의
  조합으로 설명한다. session 위치 조회를 위한 별도의 public API 는 두지 않는다.

## 2. 문서 구조와 역할 분담

문서는 **가이드**, **기준 문서**, **주제 문서**, **샘플 문서** 네 가지로 나눈다.
처음 접한다면 가이드부터 읽는다. 정식 계약은 기준/주제 문서가, 실행 코드는
샘플 문서가 다룬다.

### 2.0 가이드 (시작하기)

`guide/`는 `.NET`/`ASP.NET Core` 개발자가 각 기능을 **읽고 바로 따라 쓸 수
있도록** 개념과 사용법을 직접 설명한다. 개념의 정식 의미는 공통 스펙이, 정식
계약은 spec 문서가 다루며, 가이드는 그 의미를 실사용 코드로 풀어 준다. 실행
가능한 전체 샘플은 `guide/samples/`에 모여 있다.

케이스 스터디와 샘플 문서는 일부 코드 조각을 함께 다루지만 목적이 다르다.
케이스 스터디는 **도입 판단과 아키텍처 매핑**을 맡는다. 즉 어떤 도메인 난제가
있고, ZLink 를 넣으면 무엇이 줄어들며, 무엇은 여전히 DB·broker·도메인 로직에
남는지 설명하며 `guide/case-studies/`에 모여 있다. 샘플 문서는 **실행 가능한 구현
학습**을 맡는다. 즉 프로젝트 구조, 등록 코드, handler, DTO, 실행 방법을 따라 할 수
있게 정리한다.

| 문서 | 역할 |
|------|------|
| [guide/01-overview.ko.md](./guide/01-overview.ko.md) | 무엇/왜/누구를 위한 것, 기존 방식 대비 체감 난이도, 4축 |
| [guide/02-getting-started.ko.md](./guide/02-getting-started.ko.md) | 패키지부터 최소 예제 동작 확인까지 |
| [guide/03-concepts.ko.md](./guide/03-concepts.ko.md) | 핵심 개념과 공통 스펙 매핑 |
| [guide/04-channel-messaging.ko.md](./guide/04-channel-messaging.ko.md) | request / send / pub-sub 등록과 호출 사용법 |
| [guide/05-spot.ko.md](./guide/05-spot.ko.md) | room / stage / zone 같은 동적 SPOT 등록과 호출 사용법 |
| [guide/06-actor-session.ko.md](./guide/06-actor-session.ko.md) | actor lifecycle 과 session actor dispatch 사용법 |
| [guide/07-stream.ko.md](./guide/07-stream.ko.md) | 외부 client STREAM 서버와 Stream Connector 사용법 |
| [guide/08-registry.ko.md](./guide/08-registry.ko.md) | Registry 구동, clustering, topology 조회 사용법 |
| [guide/09-monitoring.ko.md](./guide/09-monitoring.ko.md) | socket / registry / spot runtime 이벤트 관찰 사용법 |
| [guide/10-feature-map.ko.md](./guide/10-feature-map.ko.md) | 기능 × 난이도 × 언제 쓰나 매트릭스 |
| [guide/11-interface-catalog.ko.md](./guide/11-interface-catalog.ko.md) | 모든 계약 인터페이스를 ContractTests 검증 코드로 색인 |
| [guide/12-grpc-alternative.ko.md](./guide/12-grpc-alternative.ko.md) | **ZLink 을 어디에 쓰나** — 사용처·문제 신호·경계 + 케이스 허브(도입 판단 문서) |
| [guide/case-studies/13-case-ecommerce-checkout.ko.md](./guide/case-studies/13-case-ecommerce-checkout.ko.md) | 케이스: 전자상거래 체크아웃 — channel messaging 도입 판단 + 양쪽 비교 |
| [guide/case-studies/14-case-microservice-mesh.ko.md](./guide/case-studies/14-case-microservice-mesh.ko.md) | 케이스: 내부 마이크로서비스 mesh + 운영(discovery/topology) |
| [guide/case-studies/15-case-realtime-game.ko.md](./guide/case-studies/15-case-realtime-game.ko.md) | 케이스: 실시간 멀티플레이 게임 — STREAM + SPOT + actor |
| [guide/case-studies/16-case-ride-hailing.ko.md](./guide/case-studies/16-case-ride-hailing.ko.md) | 케이스: 라이드헤일링 디스패치 — zone SPOT + 위치 fan-out |
| [guide/case-studies/17-case-chat-messaging.ko.md](./guide/case-studies/17-case-chat-messaging.ko.md) | 케이스: 채팅·메시징 — room SPOT + BoundSession |
| [guide/case-studies/17-1-case-marketplace-chat.ko.md](./guide/case-studies/17-1-case-marketplace-chat.ko.md) | 케이스: 마켓플레이스 채팅 — 구매자·판매자 conversation |
| [guide/case-studies/17-2-case-live-commerce-chat.ko.md](./guide/case-studies/17-2-case-live-commerce-chat.ko.md) | 케이스: 라이브 커머스·라이브스트림 채팅 — stream SPOT + moderation |
| [guide/case-studies/17-3-case-game-chat.ko.md](./guide/case-studies/17-3-case-game-chat.ko.md) | 케이스: 게임 채팅 — player actor + party/guild/match room |
| [guide/case-studies/18-case-trading-system.ko.md](./guide/case-studies/18-case-trading-system.ko.md) | 케이스: 트레이딩 — symbol SPOT 과 HFT 경계 |

### 2.1 기준 문서 (interface catalog)

| 문서 | 역할 |
|------|------|
| [handler-interfaces.ko.md](./spec/handler-interfaces.ko.md) | 모든 공용 인터페이스와 attribute 정의를 한 곳에 모은 기준 문서. 다른 문서에서 인터페이스를 인용할 때 항상 이 문서를 기준으로 한다. |

### 2.2 주제 문서 (programming model)

각 주제 문서는 프로그래밍 모델과 사용 방향을 설명한다. 인터페이스 전체 정의는
다시 나열하지 않는다. 필요한 부분이 있으면 handler-interfaces.ko.md 를 교차
참조한다.

| 문서 | 다루는 범위 |
|------|------------|
| [aspnet-core-channel-messaging.ko.md](./spec/aspnet-core-channel-messaging.ko.md) | channel 등록, handler 프로그래밍 모델, dispatch 흐름, outbound client 사용, router-capable channel의 SPOT route 수신, lifecycle, middleware / filter |
| [aspnet-core-spot.ko.md](./spec/aspnet-core-spot.ko.md) | SPOT 개념, SpotNode 등록, spot lifecycle, publish / subscribe, discovery, `AcceptSpotRoutesFromChannel` |
| [spot-node.ko.md](./spec/spot-node.ko.md) | Entry Spot routing id 설정, `ConfigureEntrySpot(...)` 적용 순서, Spot route kind 보존 규칙 |
| [aspnet-core-actor.ko.md](./spec/aspnet-core-actor.ko.md) | Actor 라이프사이클 (Entry Spot / session bind / user Spot join), handler, IZLinkBoundSession, session actor dispatch (gateway) 패턴 |
| [session-actor-dispatch.ko.md](./spec/session-actor-dispatch.ko.md) | session actor dispatch 의 .NET 시그니처와 등록 코드(`IZLinkBoundSession`, `ZLinkFrameworkException`, builder 시그니처, tic-tac-toe sample). cross-binding 정책은 [policy/session-gateway-usability.ko.md](../../../doc/spec/session-actor-dispatch.ko.md) 에서 다룬다. |
| [aspnet-core-stream.ko.md](./spec/aspnet-core-stream.ko.md) | STREAM 개념, framework session packet, monitor 기반 lifecycle, recv 비지원 방향 |
| [streaming-client.ko.md](./guide/samples/streaming-client.ko.md) | `.NET` Stream Connector, TCP / TLS / WS / WSS transport, header / payload packet 송수신, manual dispatch |
| [Unity Stream Connector 가이드](../../../../doc/guide/unity-stream-connector.ko.md) | Unity `MonoBehaviour`에서 공통 connector의 `Dispatch.Async()`를 호출하는 사용법 |
| [aspnet-core-monitoring.ko.md](./spec/aspnet-core-monitoring.ko.md) | socket / registry / spot runtime monitoring 이벤트와 snapshot 조회 모델 |
| [stage-wrapper-on-spot.ko.md](./spec/stage-wrapper-on-spot.ko.md) | `playhouse` Stage 같은 상위 모델을 SPOT 위에 감쌀 때 추가로 필요한 조건 |
| [aspnet-core-registry.ko.md](./spec/aspnet-core-registry.ko.md) | Registry 의 embedded / standalone 구동, topology 조회, 클러스터링 |

### 2.3 구현 준비 문서

다음 문서들은 public API 를 소개하기 위한 문서가 아니다. 실제 구현을 어디까지
진행할 수 있는지, 그리고 어떤 기준으로 완료를 판단할지를 미리 닫아 두기 위한
문서다.

| 문서 | 다루는 범위 |
|------|------------|
| [behavior-matrix.ko.md](./internals/behavior-matrix.ko.md) | 역할 조합별 기대 동작, startup validation, 허용 / 비허용 조합 |
| [di-capability-exposure-policy.ko.md](./internals/di-capability-exposure-policy.ko.md) | DI 로 노출되는 public service interface 와 역할 등록 조건 |
| [lifecycle-and-failure-semantics.ko.md](./internals/lifecycle-and-failure-semantics.ko.md) | startup / shutdown 순서, fail-fast[^fail-fast] 규칙, reconnect 와 runtime error 의 의미 |
| [regression-test-matrix.ko.md](./internals/regression-test-matrix.ko.md) | 구현 중에도 항상 유지해야 할 회귀 테스트 항목, CI 계층, release gate |
| [implementation-scope-and-nongoals.ko.md](./internals/implementation-scope-and-nongoals.ko.md) | 현재 계획의 전체 구현 범위, 비목표, 완료 판정 기준 |
| [backend-dependency-policy.ko.md](./internals/backend-dependency-policy.ko.md) | 현재 backend 의존 관계와 향후 저수준 라이브러리 교체 기준 |

### 2.4 샘플 문서

샘플 문서는 등록 코드부터 handler, client 호출까지 한 번에 보여 주는 실행 가능한
코드를 모아 둔다. 인터페이스 정의를 다시 나열하지는 않는다. 특정 도메인에 ZLink 를
도입할지 판단하는 설명은 12번 문서와 케이스 스터디가 맡고, 샘플은 그 판단 뒤에
실제 등록·실행 흐름을 확인하는 문서로 둔다.

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./guide/samples/channel-messaging-samples.ko.md) | channel 등록, handler, HTTP handler, outbound client 를 한 번에 보여 주는 샘플 |
| [spot-samples.ko.md](./guide/samples/spot-samples.ko.md) | room, stage, zone 기준 SPOT 등록과 handler, channel send / request, publish 를 한 번에 보여 주는 샘플 |
| [stream-samples.ko.md](./guide/samples/stream-samples.ko.md) | STREAM framework Header 기반 packet session 과 등록 코드를 한 번에 보여 주는 샘플 |
| [tictactoe-game-sample.ko.md](./guide/samples/tictactoe-game-sample.ko.md) | API 서버, Play 서버, STREAM connector, SPOT actor 를 함께 사용하는 틱택토 게임 샘플 초안(TicTacToe direct + session actor dispatch contract) |
| [bingo-game-sample.ko.md](./guide/samples/bingo-game-sample.ko.md) | matching room 기반 빙고 샘플 설계 노트. Session 서버, API 서버, Play 서버, Entry Spot lobby, room host 시작, timer 진행을 함께 보여 준다. |
| [supportchat-sample.ko.md](./guide/samples/supportchat-sample.ko.md) | 1:1 고객 상담 샘플. session gateway, conversation Spot, reconnect 이전성, idle timer→close, bound push 를 함께 보여 준다(JSON). |
| [deliverydispatch-sample.ko.md](./guide/samples/deliverydispatch-sample.ko.md) | 배송 배차 샘플. HTTP intake, timeout 재배정, status fanout, delivery Spot, 고객 stream push 를 함께 보여 준다(JSON). |
| [shoppingmall-sample.ko.md](./guide/samples/shoppingmall-sample.ko.md) | 주문 체크아웃 샘플. event sourcing, OrderId owner routing, projection rebuild, 보상, scale-out(2×2) 을 함께 보여 준다(JSON). |
| [gamequest-sample.ko.md](./guide/samples/gamequest-sample.ko.md) | quest 진행 샘플. gameplay event fanout 구독, player owner routing, quest event sourcing, reward idempotency 를 함께 보여 준다(JSON). |

### 2.5 범위 원칙

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 인터페이스, attribute, context 전체 정의 | [handler-interfaces](./spec/handler-interfaces.ko.md) | 교차 참조 |
| channel 등록(AddZLinkFramework), lifecycle | [aspnet-core-channel-messaging](./spec/aspnet-core-channel-messaging.ko.md) | 필요할 때 링크만 |
| handler / client 사용 예시, dispatch 흐름 | aspnet-core-channel-messaging, 샘플 | |
| SPOT 개념, 등록, lifecycle | [aspnet-core-spot](./spec/aspnet-core-spot.ko.md) | 필요할 때 링크만 |
| Actor 라이프사이클, session bind, user Spot join, session actor dispatch | [aspnet-core-actor](./spec/aspnet-core-actor.ko.md) | 필요할 때 링크만 |
| Registry 구동, topology 조회 | [aspnet-core-registry](./spec/aspnet-core-registry.ko.md) | 필요할 때 링크만 |

## 3. 핵심 방향

- `ASP.NET Core` 의 DI 와 hosted service 모델을 따른다.
- handler, client, filter 의 생성도 동일한 `.NET DI` 컨테이너를 기준으로 맞춘다.
- 기본 호출 모델은 `channel name` 기준의 direct call 이다.
- 별도의 gateway 나 전용 load balancer 를 두지 않고, channel 별 `Discovery` 로
  직접 호출한다.
- channel messaging handler 는 attribute scan[^attribute-scan] 으로 찾는다.
  다만 발견된 handler 를 모든 channel 에 전역으로 노출하지 않는다.
  대신 `EnableServer(...)` 또는 `EnableSubscriber(...)` 같은 inbound 역할
  등록 시점에 어느 channel 로 매핑할지 명시한다.
- `[ZLinkRequest]`, `[ZLinkSend]`, `[ZLinkPublish]` 는 channel 이름을 인자로 받지
  않는다. channel 이름은 배포 환경과 topology 의 값이므로, handler attribute 가
  소유하지 않고 channel registration 이 소유한다.
- `SPOT` 도 별도의 low-level runtime 으로 떼어 두지 않고, framework lifecycle
  안에서 다룰 수 있어야 한다.
- 일반 channel messaging 은 target channel 을 뜻하는 `channelName` 기반 호출을 기본으로
  한다. 반면 routed Spot 경로는 caller 가 사용할 local egress channel 을 별도로 명시하고,
  source channel registration 에 target SpotNode ingress channel 이름을 둔다. target Spot
  은 `RoutingId`로 넘긴다. local egress socket 은 channel type 에 따라 route mesh `ROUTER`
  또는 client-server `DEALER`가 될 수 있다.
- `SPOT` 의 high-level 표면은 다음 세 가지를 다룬다.
  1. 현재 channel 의 publish / subscribe
  2. attach 된 channel 의 send / request
  3. local egress channel, target SpotNode ingress channel, Spot routing id 기반 routed
     send / request
- `IZLinkChannelClient` 와 `IZLinkSpotOutbound` 는 서로 다른 C API 를 감싸는 별개의
  인터페이스다. 다만 하부 기능이 일부 겹치므로, 두 인터페이스가 비슷한 모양의
  send / request 계열 함수를 함께 가질 수 있다.

## 4. 회귀 테스트

이 묶음의 모든 세부 문서는 회귀 테스트 기준을 함께 설명해야 한다. 그래서 문서가
추가되거나 이름이 바뀌면, 아래 테스트가 다음 세 가지를 함께 갱신했는지 확인한다.

- 문서 목록
- 각 문서의 회귀 테스트 단락
- 대표 테스트 케이스 연결

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `RegressionTests.DotNetDraftDocuments_AllExposeRegressionTestSection` | `.NET` draft 문서마다 `회귀 테스트` 단락이 존재한다. |
| `RegressionTests.DotNetRegressionMatrix_References_AllDraftDocuments` | `regression-test-matrix.ko.md` 가 각 draft 문서 파일명을 모두 참조한다. |
| `ScaffoldSmokeTests.FrameworkRoot_IsDiscoverable_FromTestRuntime` | 테스트 runtime 에서 framework 루트를 찾을 수 있어, 문서 회귀 테스트가 저장소 기준으로 실행된다. |

[^public-contract]: public contract 는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^channel-messaging]: channel messaging 은 채널 이름을 키로 삼아 메시지를 주고받는 방식이다. request / send 는 요청-응답과 단방향 전달, event messaging 은 publish / subscribe 형태의 이벤트 전달을 가리킨다.
[^spot]: `SPOT` 은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 추상이다. `SpotNode` 는 하나 이상의 spot 인스턴스를 호스팅하는 컨테이너 노드를 가리킨다.
[^topology]: topology 는 어떤 노드(channel, spot, registry 등)가 어디에 있는지, 그리고 서로 어떻게 연결되어 있는지를 나타내는 구성 정보다.
[^hosted-service]: hosted service 는 `ASP.NET Core` 호스트가 시작·종료될 때 함께 시작·종료되는 백그라운드 컴포넌트를 뜻한다(`IHostedService`).
[^ci]: CI(Continuous Integration) 는 코드 변경이 들어올 때마다 자동으로 빌드와 테스트를 실행해 회귀를 빠르게 잡아내는 파이프라인을 가리킨다.
[^rid]: RID(Runtime Identifier) 는 `.NET` 이 OS·CPU 조합을 식별하는 문자열이다. 예: `win-x64`, `linux-arm64`.
[^release-gate]: release gate 는 새 버전을 배포하기 전에 반드시 통과해야 하는 검증 단계(테스트, 빌드, 점검)의 묶음을 가리킨다.
[^raw-transport]: raw transport 는 framework 추상화를 거치지 않은 저수준 소켓 계층의 송수신을 뜻한다.
[^wire-transport]: wire / transport 레벨은 실제 네트워크 위에서 바이트가 흘러가는 계층을 가리키며, 그 위에 framework 의 추상화가 쌓인다.
[^reverse-dns]: 역순 도메인 규칙(reverse-DNS) 은 도메인 이름을 거꾸로 뒤집어 namespace 충돌을 피하는 관례다. `zlink.systems` 도메인이면 `Systems.Zlink.*` 가 된다.
[^nuget]: NuGet 은 `.NET` 의 표준 패키지 매니저로, 라이브러리를 package id 단위로 배포·설치한다.
[^capability]: **역할**은 어떤 노드(channel, spot 등)가 외부에 노출하는 기능 단위(예: server, subscriber, publisher)를 가리킨다.
[^backpressure]: backpressure 는 송신 측이 수신 측의 처리 속도를 넘어 메시지를 밀어 넣지 못하도록 흐름을 조절하는 메커니즘이다.
[^stream-actor-bridge]: stream-to-actor bridge 는 STREAM 으로 들어온 외부 트래픽을 framework 내부의 actor 메시지로 이어 주는 연결 지점을 가리킨다.
[^session-actor-dispatch]: session actor dispatch 는 클라이언트 세션에서 들어온 요청을, 그 세션과 묶인 actor 로 자동 전달하는 패턴이다.
[^fail-fast]: fail-fast 는 잘못된 설정이나 상태를 발견하면 즉시 예외를 던지고 실행을 멈추는 전략이다. 늦게 발견되어 더 큰 문제로 번지는 것을 막는다.
[^attribute-scan]: attribute scan 은 어셈블리에 정의된 타입과 메서드를 훑어 보면서 특정 attribute 가 붙은 항목을 찾아 등록하는 방식이다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../doc/README.ko.md) | [다음: ZLink Framework for .NET — 개요](./guide/01-overview.ko.md)
<!-- framework-adapter-nav:bottom:end -->
