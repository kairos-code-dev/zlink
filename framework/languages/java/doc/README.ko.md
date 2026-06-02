# ZLink Framework for Java/Kotlin -- 문서

> 이 문서 묶음은 `ZLink Framework`의 **Java / Kotlin / Spring Boot 버전**을
> 정의하기 위한 구현 전 초안이다.
> 기준은 `framework/languages/dotnet`의 정식 계약과 **소스 코드**다. 목표는 하나다.
> **이 문서대로 구현하면 .NET 버전과 동일한 사용성, 기능, 구조를 가진 Java/Kotlin
> framework가 나온다.**
>
> 개념, 의미, 동작은 `.NET`과 동일하고, 표면만 Java, Kotlin, Spring Boot 모델로
> 옮긴다. 아직 구현 전 단계이므로 정식 공개 계약은 `doc/spec`가 아니라
> [draft](./draft/README.ko.md) 아래에 둔다. draft와 `.NET` 코드가 어긋나면
> `framework/languages/dotnet/src` 코드가 기능의 최종 기준이다.

## 0. 먼저 읽어야 하는 문서

- [Java/Kotlin 포팅 계획](./draft/java-kotlin-framework-porting-plan.ko.md)
  -- 포팅 범위, 구현 순서, 완료 판정
- [.NET -> Java/Kotlin 표면 매핑 정책](./draft/internals/dotnet-to-java-surface-mapping.ko.md)
  -- 모든 Java draft 문서가 따르는 번역 규칙
- [인터페이스 카탈로그](./draft/handler-interfaces.ko.md)
  -- public interface, annotation, context, options 기준

## 1. 사용자 가이드 초안 (`draft/guide/`)

Spring Boot 개발자가 읽고 바로 따라 쓸 수 있도록 기능과 사용법을 설명한다.

| 문서 | 범위 |
|------|------|
| [01-overview](./draft/guide/01-overview.ko.md) | 한 줄 정의, 아키텍처, 통합 축 |
| [02-getting-started](./draft/guide/02-getting-started.ko.md) | 첫 channel request까지 |
| [03-concepts](./draft/guide/03-concepts.ko.md) | channel, capability, Spring DI 멘탈 모델 |
| [04-feature-map](./draft/guide/04-feature-map.ko.md) | 기능별 사용 시점과 구현 문서 연결 |
| [05-channel-messaging](./draft/guide/05-channel-messaging.ko.md) | request/send/pub-sub |
| [06-spot](./draft/guide/06-spot.ko.md) | room/stage/zone, timer |
| [07-actor-session](./draft/guide/07-actor-session.ko.md) | actor lifecycle, session actor dispatch |
| [08-stream](./draft/guide/08-stream.ko.md) | STREAM server session과 connector |
| [09-registry](./draft/guide/09-registry.ko.md) | Registry 구동과 query |
| [10-monitoring](./draft/guide/10-monitoring.ko.md) | runtime event 관찰 |
| [11-interface-catalog](./draft/guide/11-interface-catalog.ko.md) | 주요 public interface |
| [12-grpc-alternative](./draft/guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 |

## 2. 계약 초안 (`draft/`)

구현 전 public surface의 기준이다. 구현이 끝난 뒤 실제 Java 코드, 테스트, binding
public API와 맞춘 다음 정식 spec으로 나눠 승격한다.

| 문서 | 범위 |
|------|------|
| [handler-interfaces](./draft/handler-interfaces.ko.md) | 모든 interface, annotation, context, options |
| [spring-boot-channel-messaging](./draft/spring-boot-channel-messaging.ko.md) | channel 등록, outbound client, dispatch |
| [spring-boot-spot](./draft/spring-boot-spot.ko.md) | Spot lifecycle, Entry Spot, timer |
| [spring-boot-actor-session](./draft/spring-boot-actor-session.ko.md) | actor factory, ActorGateway, bound session |
| [spring-boot-stream](./draft/spring-boot-stream.ko.md) | stream node, header session |
| [stream-connector](./draft/stream-connector.ko.md) | Java/Kotlin client Stream Connector |
| [spring-boot-registry](./draft/spring-boot-registry.ko.md) | embedded registry, remote query |
| [spring-boot-monitoring](./draft/spring-boot-monitoring.ko.md) | runtime event, typed handler |

## 3. 내부 정책 초안 (`draft/internals/`)

framework 경계, backend 의존, lifecycle, 회귀 기준을 정의한다.

| 문서 | 범위 |
|------|------|
| [dotnet-to-java-surface-mapping](./draft/internals/dotnet-to-java-surface-mapping.ko.md) | 이식 기준과 번역 규칙 |
| [backend-dependency-policy](./draft/internals/backend-dependency-policy.ko.md) | Java binding 의존 격리 |
| [di-capability-exposure-policy](./draft/internals/di-capability-exposure-policy.ko.md) | capability별 Spring bean 노출 규칙 |
| [lifecycle-and-failure-semantics](./draft/internals/lifecycle-and-failure-semantics.ko.md) | 시동, 종료, 실패 의미 |
| [behavior-matrix](./draft/internals/behavior-matrix.ko.md) | 기능별 validation/runtime 판정 |
| [implementation-scope-and-nongoals](./draft/internals/implementation-scope-and-nongoals.ko.md) | 구현 범위와 비목표 |
| [regression-test-matrix](./draft/internals/regression-test-matrix.ko.md) | `.NET` 동등성 회귀 테스트 기준 |

## 4. 구현 순서 권장

1. `dotnet-to-java-surface-mapping`으로 번역 규칙을 고정한다.
2. backend adapter와 public interface catalog를 먼저 구현한다.
3. channel messaging, Spot, actor/session, stream, registry, monitoring 순서로
   수직 슬라이스를 만든다.
4. Stream Connector를 별도 client 모듈로 구현한다.
5. `regression-test-matrix`와 sample self-check로 `.NET` 동등성을 검증한다.
