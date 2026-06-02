<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [다음: Draft -- ZLink Framework Java Channel Messaging Samples](./channel-messaging-samples.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[Framework Adapter 정책](../../../../doc/spec/README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md) | [실행 계획](./implementation-execution-plan.ko.md) | [표면 매핑](./internals/dotnet-to-java-surface-mapping.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [guide](./guide/01-overview.ko.md) | [regression](./internals/regression-test-matrix.ko.md) | [channel](./spring-boot-channel-messaging.ko.md) | [SPOT](./spring-boot-spot.ko.md) | [Actor/session](./spring-boot-actor-session.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [Stream Connector](./stream-connector.ko.md) | [Samples](./sample-implementation-plan.ko.md) | [Monitoring](./spring-boot-monitoring.ko.md) | [Registry](./spring-boot-registry.ko.md)

# Draft -- ZLink Framework For Java

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Java`와 `Spring Boot`에서 `ZLink Framework`를 어떤
> 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 `Java` 바인딩 위에 올라가는 `ZLink Framework`의 `Java`와 `Kotlin`
방향을 정리한다. 대표 프레임워크는 `Spring Boot`로 둔다.

현재 목표는 새 웹 프레임워크를 만드는 일이 아니다.
기존 `Spring Boot`가 제공하는 bean lifecycle, configuration, scheduler,
application event 모델 위에 zlink runtime을 자연스럽게 얹는 것이다.

이 초안은 현재 `.NET` framework 코드를 포팅 기준으로 삼는다. 즉 public surface는
`framework/languages/dotnet/src/Zlink.Framework`,
`framework/languages/dotnet/src/Zlink.Framework.AspNetCore`,
`framework/languages/dotnet/src/Systems.Zlink.Stream.Connector`의 역할을
`Java`/`Kotlin` 생태계에 맞게 옮기는 것을 목표로 한다. 이름은 `.NET` 개념을
기준으로 맞추되, Java 메서드는 `camelCase`, Kotlin 사용 예시는 coroutine
친화 표면을 함께 제공한다.

## 1.1 공통 정책 적용

이 디렉토리의 모든 문서는
[Framework Adapter 정책](../../../../doc/spec/README.ko.md)과 그 하위 문서를 그대로 따른다.
즉 `Java` 상세 문서는 공통 의미를 다시 정의하지 않고, 그 의미를 `Java`와
`Spring Boot` 표면으로만 구체화한다.

특히 아래 항목은 이 디렉토리 전체에 공통으로 적용된다.

- 네이밍 규칙은
  [doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
  `Naming Policy`를 그대로 따른다. `Java` 문서에서는 메서드는 `camelCase`,
  클래스와 annotation은 `PascalCase`를 쓴다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다. 같은 capability 안에서는 `Discovery`와 manual 연결을 섞지 않는다.
- send/publish는 기본 async submit으로 설명한다. backpressure는 public
  non-blocking 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
- `SPOT`을 지원하는 문서는 Spot type 기준 factory 등록, `spotRid` 기준 조회,
  lifecycle timer, 외부 spot publish 표면을
  공통 정책과 맞춰 설명해야 한다.
- monitoring을 지원하는 문서는 socket/discovery/registry/spot runtime event를
  typed event와 등록 표면으로 설명해야 한다.

## 2. 문서 구조와 역할 분담

문서는 `.NET` 묶음과 같은 세 층으로 나눈다.

### 2.0 사용자 guide 초안

| 문서 | 역할 |
|------|------|
| [guide/01-overview.ko.md](./guide/01-overview.ko.md) | Java/Kotlin framework 한 줄 정의, 아키텍처, 통합 축 |
| [guide/02-getting-started.ko.md](./guide/02-getting-started.ko.md) | Spring Boot에서 첫 channel request를 붙이는 흐름 |
| [guide/03-concepts.ko.md](./guide/03-concepts.ko.md) | channel, capability, handler exposure, Spring DI 개념 |
| [guide/04-feature-map.ko.md](./guide/04-feature-map.ko.md) | 기능별 사용 표면과 기준 문서 연결 |
| [guide/05-channel-messaging.ko.md](./guide/05-channel-messaging.ko.md) | request/send/pub-sub guide |
| [guide/06-spot.ko.md](./guide/06-spot.ko.md) | Spot 생성, 조회, timer guide |
| [guide/07-actor-session.ko.md](./guide/07-actor-session.ko.md) | actor/session relay guide |
| [guide/08-stream.ko.md](./guide/08-stream.ko.md) | STREAM server session과 connector guide |
| [guide/09-registry.ko.md](./guide/09-registry.ko.md) | Registry 구동과 query guide |
| [guide/10-monitoring.ko.md](./guide/10-monitoring.ko.md) | runtime monitoring guide |
| [guide/11-interface-catalog.ko.md](./guide/11-interface-catalog.ko.md) | 주요 public interface guide |
| [guide/12-grpc-alternative.ko.md](./guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 |

### 2.1 기준 문서

| 문서 | 역할 |
|------|------|
| [java-kotlin-framework-porting-plan.ko.md](./java-kotlin-framework-porting-plan.ko.md) | `.NET` framework 기준 Java/Kotlin 포팅 범위, 패키지 구조, 구현 순서, 검증 계획 |
| [implementation-execution-plan.ko.md](./implementation-execution-plan.ko.md) | 실제 구현 phase, 산출물, gate, release 순서 |
| [handler-interfaces.ko.md](./handler-interfaces.ko.md) | `Java` 공용 인터페이스, annotation, options, context를 한 곳에 모은 기준 문서 |
| [internals/dotnet-to-java-surface-mapping.ko.md](./internals/dotnet-to-java-surface-mapping.ko.md) | `.NET` 표면을 Java/Kotlin/Spring Boot로 옮기는 번역 규칙 |
| [java-framework-completion-prompt.ko.md](./java-framework-completion-prompt.ko.md) | 이 문서 묶음으로 Java/Kotlin framework 포팅을 끝까지 수행하기 위한 실행 프롬프트 |

### 2.2 주제 문서

| 문서 | 다루는 범위 |
|------|------------|
| [spring-boot-channel-messaging.ko.md](./spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, handler 모델, dispatch, filter |
| [spring-boot-spot.ko.md](./spring-boot-spot.ko.md) | `SPOT` bean lifecycle, publish/subscribe, channel attach |
| [spring-boot-actor-session.ko.md](./spring-boot-actor-session.ko.md) | actor lifecycle, session binding, ActorGateway relay, bound session |
| [spring-boot-stream.ko.md](./spring-boot-stream.ko.md) | stream node, header session, session context, stream connector |
| [stream-connector.ko.md](./stream-connector.ko.md) | Java/Kotlin client Stream Connector public API, option, 상태 전이, codec, reconnect |
| [stream-open-items.ko.md](./stream-open-items.ko.md) | stream에서 이미 닫은 결정과 첫 구현 이후 편의 기능 후보 |
| [spring-boot-monitoring.ko.md](./spring-boot-monitoring.ko.md) | runtime monitoring 등록, typed event, 운영 샘플 |
| [stage-wrapper-on-spot.ko.md](./stage-wrapper-on-spot.ko.md) | stage 같은 상위 모델을 `SPOT` 위에 감쌀 때 필요한 조건 |
| [spring-boot-registry.ko.md](./spring-boot-registry.ko.md) | embedded registry, remote query, topology 조회 |
| [sample-implementation-plan.ko.md](./sample-implementation-plan.ko.md) | .NET sample과 같은 수준의 Java/Kotlin Bingo, TicTacToe, streaming client 구현 기준 |

### 2.3 샘플 문서

| 문서 | 다루는 범위 |
|------|------------|
| [channel-messaging-samples.ko.md](./channel-messaging-samples.ko.md) | channel 등록, handler, HTTP controller, outbound client 샘플 |
| [spot-samples.ko.md](./spot-samples.ko.md) | room/stage/zone 기준 `SPOT` 등록과 publish/request 샘플 |
| [stream-samples.ko.md](./stream-samples.ko.md) | stream 등록, header session, actor relay 샘플 |

### 2.4 내부 정책 문서

| 문서 | 다루는 범위 |
|------|------------|
| [internals/backend-dependency-policy.ko.md](./internals/backend-dependency-policy.ko.md) | Java binding 의존 격리와 backend adapter |
| [internals/di-capability-exposure-policy.ko.md](./internals/di-capability-exposure-policy.ko.md) | capability별 Spring bean 노출 규칙 |
| [internals/lifecycle-and-failure-semantics.ko.md](./internals/lifecycle-and-failure-semantics.ko.md) | startup/shutdown 순서와 실패 의미 |
| [internals/behavior-matrix.ko.md](./internals/behavior-matrix.ko.md) | `.NET`과 같은 validation/runtime 판정 |
| [internals/implementation-scope-and-nongoals.ko.md](./internals/implementation-scope-and-nongoals.ko.md) | 첫 구현 범위와 비목표 |
| [internals/regression-test-matrix.ko.md](./internals/regression-test-matrix.ko.md) | 회귀 테스트와 sample release gate |

## 3. 핵심 방향

- `Spring Boot` bean lifecycle에 맞춘다.
- channel messaging은 `channel name` 호출을 기본으로 둔다.
- channel capability는 startup 시점에 등록한다.
- 같은 capability에서 자동 연결과 수동 연결을 섞지 않는다.
- 일반 request/send handler dispatch는 local `ROUTER(server)` ingress 기준으로 설명한다.
- outbound `DEALER(client)`는 주로 reply correlation 경로로 본다.
- `rid` 직접 지정은 `SPOT` spot-to-spot 경로에만 남긴다.

## 4. Java에서 기대하는 표면

- `@EnableZLinkFramework`
- `ZLinkFrameworkOptionsCustomizer`
- `ZLinkClient`
- `ZLinkFanoutClient`
- `ZLinkRouteClient`
- `ZLinkActorManager`
- `ZLinkStreamConnector`
- `ZLinkCodecRegistryBuilder`
- `ZLinkDispatchOptions`
- `@ZLinkRequest`, `@ZLinkSend`, `@ZLinkPublish` (annotation은 `Mapping` 접미사
  없이, publish는 `Event`가 아니다)
- `ZLinkRequestContext`, `ZLinkSendContext`, `ZLinkPublishContext`

기본 packet key는 payload 타입의 `SimpleName`을 쓰고, 충돌이나 외부 계약 때문에
다른 이름이 필요할 때만 annotation 또는 options에서 override하는 쪽을 기준으로
본다.
