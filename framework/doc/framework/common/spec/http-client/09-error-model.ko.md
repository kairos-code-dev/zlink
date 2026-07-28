# 9. 에러 모델

> [공통 계약 목차](README.ko.md)

HTTP client는 자체 예외 계층을 만들지 않고 Framework 공용 오류와 retry advice를
사용한다.

## 9.1 공통 kind 집합 (계약)

| Kind | 상황 | Retry advice |
| --- | --- | --- |
| `ProtocolError` | Builder 형식, body 소스 중복, typed decode, 압축 해제 또는 redirect 형식이 올바르지 않다. | `DoNotRetry` |
| `Unavailable` | Network, DNS, proxy CONNECT 또는 target 연결을 현재 사용할 수 없다. | `RetryAfterBackoff` |
| `CapacityExceeded` | 설정한 response body byte 제한을 넘었다. | `DoNotRetry` |
| `DeadlineExceeded` | 시도당 timeout을 넘었다. | `RetryAfterBackoff` |
| `InternalFailure` | Typed 제출의 HTTP status가 400 이상이거나 위 kind로 분류할 수 없는 실행 실패다. | 기본 `DoNotRetry` |

호출자 cancellation은 Framework 오류로 바꾸지 않고 각 언어의 cancelled awaitable로
전달한다. Retry advice는 [redirect와 retry](06-redirect-retry-cookie.ko.md)의
재시도 대상 판정과 같아야 한다.

## 9.2 언어별 표현

| 언어 | Kind와 retry advice | Timeout 표현 | 전달 형태 |
| --- | --- | --- | --- |
| C++ | Framework 공통 enum과 retry advice | `deadline_exceeded` | `result_t` 또는 예외 |
| .NET | `ZLinkFrameworkErrorKind`, `ZLinkRetryAdvice` | `DeadlineExceeded`와 inner `TimeoutException` | `ZLinkFrameworkException` |
| Node.js | Framework 공통 kind와 retry advice | `deadlineExceeded`와 `TimeoutError` cause | 예외 |
| Java | Framework 공통 enum과 retry advice | `DEADLINE_EXCEEDED`와 `HttpTimeoutException` cause | 예외 |
| Kotlin | Java 계약을 Kotlin 표기로 투영한다. | Java와 같다. | 예외 |

`closed`는 HTTP client error kind가 아니다. 응답 body stream이나 transport handle이
닫힌 상태는 해당 객체의 boundary 상태로 보고, 위 kind 가운데 실제 실패 원인에
맞는 kind로 변환한다.

## 9.3 retriable 오염 금지

일반 예외를 일괄 재시도 대상으로 표시하면 안 된다. `RetryAfterBackoff`는 일시적인
전송 실패와 timeout에만 사용한다.
