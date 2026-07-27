# 9. 에러 모델

> [공통 계약 목차](README.ko.md)

HTTP client는 자체 예외 계층을 만들지 않고 framework 공용 에러 모델
(`ZLinkFrameworkException` 계열 + error kind + `isRetriable`)을 재사용한다.

## 9.1 공통 kind 집합 (계약)

| kind (개념명) | 상황 | retriable |
| --- | --- | --- |
| `requestProtocolError` | 구성/사용 오류: builder 검증 실패, path 형식, body 소스 중복, one-shot 재제출, OpenSSL 부재 https(cpp) | false |
| `requestFailed` | 전송 실패, typed 제출의 status ≥ 400, redirect 한도/형식 오류, body 크기 초과, proxy CONNECT 거부 | 전송 실패·timeout만 true |
| `payloadDecodeFailed` | typed JSON 디코드 실패, 압축 해제 실패 | false |
| (timeout) | 시도당 timeout 초과. **전용 공통 error kind를 추가하지 않고 언어별 경계에서 식별**한다 — cpp는 `framework_exception_t::code() == std::errc::timed_out`, `.NET`은 `RequestFailed(IsRetriable=true)`와 inner `TimeoutException` | true |

- **timeout 실패는 프로그램적으로 식별 가능해야 하고 retriable이어야 한다.**
  다만 timeout 전용 공통 kind를 새로 만들지 않는다. `.NET`·Java·Kotlin·Node는 기존
  `requestFailed` kind, retriable 표식과 언어별 원인 예외를 함께 사용하고, cpp는 언어 고유 timeout
  경계를 사용한다([05 §13](../06-framework-api.ko.md#13-오류-kind)).
- `isRetriable`은 [6장 §6.2](06-redirect-retry-cookie.ko.md)의 재시도 대상
  판정과 일치해야 한다.

## 9.2 언어별 표현

| | kind enum 노출 | isRetriable 노출 | timeout 표현 | 전달 형태 |
| --- | --- | --- | --- | --- |
| cpp | O (3종) | O | `framework_exception_t::code() == std::errc::timed_out` | `result_t` 봉투 또는 예외 |
| dotnet | O (3종, PascalCase) | O | `RequestFailed(IsRetriable=true)` + inner `TimeoutException` | 예외 |
| node | O (3종, camelCase) | O | `requestFailed(isRetriable=true)` + `cause.name == "TimeoutError"` | 예외 |
| java | O (`kind()`, UPPER_SNAKE) | O (`retriable()`) | `REQUEST_FAILED(retriable=true)` + `HttpTimeoutException` cause | 예외 |
| kotlin | java와 동일 | java와 동일 | java와 동일 | 예외 |

`closed`는 HTTP client error kind가 아니다. 응답 body stream이나 transport handle이
닫힌 상태는 해당 객체의 boundary 상태로 보고, 위 세 kind 가운데 실제 실패 원인에
맞는 kind로 변환한다.

## 9.3 retriable 오염 금지

일반 예외를 일괄 retriable로 표시하면 안 된다(프로그래밍 오류까지 재시도
대상이 된다). retriable은 전송 실패·timeout에만 부여한다.
