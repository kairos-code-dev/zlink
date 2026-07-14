# ZLink Framework for Kotlin -- 문서

> 이 묶음은 `Kotlin`(Spring Boot) 사용자를 위한 ZLink Framework 문서다.
> `zlink-framework-kotlin`은 Java `zlink-framework` 런타임을 그대로 재사용하는
> 얇은 coroutine idiom 레이어다. Java 표면은
> [Java spec](../common/spec/languages/java/README.ko.md)을 따르고, Kotlin 전용
> 공개 계약은 [Kotlin spec](../common/spec/languages/kotlin/README.ko.md)에 고정한다.
> 내부 기준은 [Java/Kotlin 문서](../java/README.ko.md)를 공유한다. `guide/`는
> Kotlin 사용자가 `suspend` 함수, coroutine handler, `Flow`만으로 따라 쓸 수 있도록
> **Kotlin 전용으로** 작성한다. 공통 의미는
> [공통 스펙](../common/README.ko.md)을 따른다.

비동기 실행, `CompletionStage`, Kotlin coroutine wrapper의 공통 의미는
[비동기 실행과 coroutine 정책](../common/spec/04-async-execution-policy.ko.md)을 따른다.

Sample과 E2E의 설정 파일, 환경 변수 금지와 `@ConfigurationProperties` binding 기준은
[Sample/E2E 설정 정책](../common/sample-e2e-configuration-policy.ko.md)을 따른다.

## 0. Kotlin 표면 한눈에

`zlink-framework-kotlin`은 새 transport를 만들지 않는다. Java framework가 노출하는
같은 channel·Spot·actor·stream 위에 coroutine 표면만 얹는다.

| Java 표면 | Kotlin 표면 |
|-----------|-------------|
| `ZLinkRequestHandler<T, R>` (plain `TReply` 반환) | `ZLinkSuspendingRequestHandler<T, R>` (`suspend fun handle`) |
| `ZLinkSendHandler` / `ZLinkPublishHandler` | `ZLinkSuspendingSendHandler` / `ZLinkSuspendingPublishHandler` |
| `ZLinkSpot<TActor>` / `ZLinkEntrySpot<TActor>` | `ZLinkSuspendingSpot<TActor>` / `ZLinkSuspendingEntrySpot<TActor>` (actor admission, joined, leave를 `suspend`로 처리) |
| `ZLinkActorTransferAdapter<TActor>` | `ZLinkSuspendingActorTransferAdapter<TActor>` (`transferOutSuspending`, `transferInSuspending`) |
| `ZLinkSession` | `ZLinkSuspendingSession` (`onConnectedSuspending` 등) |
| `client.requestToChannel(...).submit(R::class.java)` | `client.request<R>(channel, msg)` / `call.awaitReply<R>()` |
| `connector.on(name) { ... }` callback | `connector.kotlin().messages(name): Flow<...>` |

coroutine handler를 켜려면 framework 옵션에서 `useCoroutineHandlers(dispatcher)`를
호출한다. 자세한 건 [02-getting-started](guide/02-getting-started.ko.md)를 본다.

## 1. 사용자 guide (Kotlin 전용)

Kotlin/Spring Boot 개발자가 읽고 바로 따라 쓸 수 있도록 기능과 사용법을 Kotlin
관용구로 설명한다. 내부 backend adapter나 binding wrapper 구조는 guide에서 설명하지
않고, 필요하면 공유 `internals/`(아래 §3)로 연결한다.

| 문서 | 범위 |
|------|------|
| [01-overview](guide/01-overview.ko.md) | 한 줄 정의, 아키텍처, coroidiom 통합 축 |
| [02-getting-started](guide/02-getting-started.ko.md) | 첫 channel request까지 (`suspend` handler) |
| [03-concepts](guide/03-concepts.ko.md) | channel, 역할, Spring DI, coroutine 멘탈 모델 |
| [04-channel-messaging](guide/04-channel-messaging.ko.md) | request/send/pub-sub (`suspend`/`Flow`) |
| [05-spot](guide/05-spot.ko.md) | Spot 생성, 조회, timer (`ZLinkSuspendingSpot`) |
| [06-actor-session](guide/06-actor-session.ko.md) | actor lifecycle, session actor dispatch |
| [07-stream](guide/07-stream.ko.md) | STREAM server session과 connector (`Flow`) |
| [08-registry](guide/08-registry.ko.md) | Registry 구동과 query |
| [09-monitoring](guide/09-monitoring.ko.md) | runtime event 관찰 (`suspend` event handler) |
| [10-feature-map](guide/10-feature-map.ko.md) | 기능별 사용 시점과 구현 문서 연결 |
| [11-interface-catalog](guide/11-interface-catalog.ko.md) | 주요 Kotlin public interface |
| [12-grpc-alternative](guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 |

## 2. 공개 계약 spec

Kotlin은 같은 Spring Boot runtime 위에 coroutine 확장을 더한다. 그대로 사용하는
Java 타입은 Java spec을 따르고, Kotlin에서 새로 노출하는 `suspend`, `Flow`, adapter
시그니처는 Kotlin spec을 따른다.

| 문서 | 범위 |
|------|------|
| [Kotlin spec 목차](../common/spec/languages/kotlin/README.ko.md) | Kotlin 전용 공개 계약 문서 목록 |
| [Kotlin handler interfaces](../common/spec/languages/kotlin/02-handler-interfaces.ko.md) | suspending handler와 lifecycle adapter |
| [Java spec 목차](../common/spec/languages/java/README.ko.md) | Kotlin이 그대로 사용하는 Java 공개 계약 |
| [Java handler interfaces](../common/spec/languages/java/02-handler-interfaces.ko.md) | Java interface, annotation, context, options |
| [spring-boot-channel-messaging](../common/spec/languages/java/01-system-structure.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](../common/spec/languages/java/01-system-structure.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](../common/spec/languages/java/01-system-structure.ko.md) | actor factory, SessionRelay, bound session |
| [spring-boot-stream](../common/spec/languages/java/01-system-structure.ko.md) | stream node, header session |
| [stream-connector](../common/spec/languages/java/03-stream-connector.ko.md) | Java/Kotlin Stream Connector |
| [spring-boot-registry](../common/spec/languages/java/01-system-structure.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](../common/spec/languages/java/01-system-structure.ko.md) | runtime event, typed handler |

## 3. 내부 기준 — Java/Kotlin 공유

구현 구조, lifecycle, regression 기준은 같은 런타임을 쓰므로 **Java/Kotlin
`internals/`를 공유**한다.

| 문서 | 범위 |
|------|------|
| [backend-dependency-policy](../java/internals/backend-dependency-policy.ko.md) | Java binding 의존 격리 |
| [runtime-lifecycle](../java/internals/runtime-lifecycle.ko.md) | Spring lifecycle과 Java/Kotlin 공유 runtime 소유권 |
| [regression-test-matrix](../java/internals/regression-test-matrix.ko.md) | `.NET` 동등성 회귀 테스트 기준 |

## 4. 샘플 (Kotlin)

샘플은 Java와 같은 scenario set을 Kotlin coroutine 구현으로 제공한다. 정본 6종은
per-app 문서로, 기능 축 샘플은 별도 문서로 둔다.

정본 6종의 서버 역할, 메시지 계약, 상태 전이와 완료 기준은
[공통 샘플](../common/sample/README.ko.md)이 소유한다. Kotlin 문서는 이 계약을 다시
서술하지 않는다.

| 문서 | 범위 |
|------|------|
| [samples README](../../../languages/java/samples/README.md) | Java/Kotlin sample 구조와 실행 방법 |
