<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:end -->

[Java 문서](../README.ko.md) | [Kotlin 문서](../../kotlin/README.ko.md) | [Backend Policy](backend-dependency-policy.ko.md)

# Java/Kotlin Framework Runtime Lifecycle

## 1. 목적

이 문서는 Java와 Kotlin이 공유하는 Spring lifecycle과 내부 runtime 소유권을
설명한다. 사용자가 관찰하는 validation, timeout, cancellation과 reconnect 계약은
각 기능 spec이 소유한다.

## 2. 시작 순서

1. Spring auto-configuration이 options와 handler scanner 결과로 registration을 만든다.
2. registration validator가 channel, Spot, stream과 handler 조합을 검사한다.
3. Java binding public API를 사용하는 backend adapter와 framework context를 만든다.
4. location runtime과 자동 연결 loop를 시작한다.
5. channel, route, Spot과 stream runtime을 시작한다.
6. monitoring source와 runtime event dispatcher를 연결한다.

Spring bean을 생성하는 것만으로 native runtime을 부분 시작하지 않는다. 실제 시작은
framework `SmartLifecycle`이 소유하며, 시작 도중 실패하면 이미 만든 자원을 역순으로
정리한다.

## 3. 종료 순서

Spring context 종료 시 framework lifecycle은 새 dispatch 진입을 막고 monitoring,
Spot, route, stream, channel, location, backend context 순서로 정리한다. pending
completion과 coroutine continuation은 각 runtime 소유자가 완료하거나 실패시킨다.
JVM thread와 coroutine dispatcher를 blocking wait로 점유하지 않는다.

## 4. Java/Kotlin 공유 경계

Kotlin handler는 Java runtime의 registration과 실행 queue를 공유한다. `suspend`
continuation과 coroutine context를 연결하는 wrapper만 Kotlin 전용이며, 별도 native
runtime이나 별도 lifecycle을 만들지 않는다.

## 5. 회귀 테스트

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `HostTest.host_startsAndStops_frameworkRuntimeContext` | Spring lifecycle과 framework context 생성·정리가 함께 동작한다. |
| `ZLinkFrameworkAutoConfigurationTest.autoConfigurationStartsFrameworkLifecycleAndExposesClientBean` | auto-configuration이 lifecycle과 public client bean을 연결한다. |
| `ZLinkAsyncSubmitterTest.close_failsPendingItems` | runtime 종료가 pending submit을 남겨 두지 않는다. |
| `KotlinSuspendAnnotationHandlerTest.kotlinSuspendAnnotationCancellationCompletesJavaStageExceptionally` | Kotlin cancellation이 공유 Java completion에 전달된다. |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [다음: Regression Test Matrix](regression-test-matrix.ko.md)
<!-- framework-adapter-nav:bottom:end -->
