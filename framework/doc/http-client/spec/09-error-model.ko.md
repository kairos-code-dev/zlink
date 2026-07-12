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
| timeout | 시도당 timeout 초과 | true |

- **timeout 실패는 프로그램적으로 식별 가능해야 하고 retriable이어야 한다.**
  전용 kind 통일은 개정 후보 [R2](10-revision-candidates.ko.md) — 승격 전까지
  아래 언어별 매핑이 현행 계약이다.
- `isRetriable`은 [6장 §6.2](06-redirect-retry-cookie.ko.md)의 재시도 대상
  판정과 일치해야 한다.

## 9.2 언어별 매핑 현황

| | kind enum 노출 | isRetriable 노출 | timeout 표현 | 전달 형태 |
| --- | --- | --- | --- | --- |
| cpp | O (5종: 위 3종 + `timeout` + `closed`) | O | `timeout` kind | `result_t` 봉투 또는 예외 |
| dotnet | O (3종, PascalCase) | O | `System.TimeoutException` | 예외 |
| node | O (3종, camelCase) | O | `requestFailed(isRetriable=true)` | 예외 |
| java | **X (구현 갭)** | **X (구현 갭)** | `HttpTimeoutException` cause | 예외 |
| kotlin | java와 동일 | java와 동일 | java와 동일 | 예외 |

## 9.3 구현 갭 (계약 위반 — plan 문서에서 수정 추적)

1. **java/kotlin**: kind와 `isRetriable`을 노출하지 않는다. 호출자가 메시지
   문자열로만 실패를 구분할 수 있고 status도 꺼낼 수 없다. →
   framework 공용 kind 노출로 정렬해야 한다.
2. **dotnet**: timeout이 `TimeoutException`으로 zlink 에러 모델 밖으로 샌다.
3. cpp의 `closed`(runtime 종료 후 제출) kind는 cpp 실행 모델 고유 —
   언어 편차로 인정.

## 9.4 retriable 오염 금지

일반 예외를 일괄 retriable로 표시하면 안 된다(프로그래밍 오류까지 재시도
대상이 된다). retriable은 전송 실패·timeout에만 부여한다. cpp의 현행 일괄
매핑은 구현 결함으로 plan에서 추적한다.
