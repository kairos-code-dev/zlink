# Go·Rust submit 반환 계약 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다. 관련 정식 spec이 갱신되고 구현과 contract test가
> 통과하기 전에는 public signature의 근거로 사용하지 않는다.

## 1. 해결할 문제

공통 오류 처리 정책은 submit backpressure를 함수군별 error로 전달한다고 규정한다. 반면 현재 Go 구현은
`false, nil`, Rust 구현은 `Ok(false)`를 반환하며 비동기 완료 정책도 boolean 반환을 허용한다. 이 상태에서는
boolean이 정상적인 non-submit을 뜻하는지, error로 처리해야 할 실패를 뜻하는지 문서마다 다르게 해석된다.

Caller-provided receive의 no-data는 정상 상태이므로 이 초안의 대상이 아니다. Receive는 Go `(false, nil)`과
Rust `Ok(false)`를 유지한다.

## 2. 검토할 대안

### 대안 A — Boolean으로 backpressure 표현

- Go submit은 `(bool, error)`, Rust submit은 `Result<bool, SubmitError>`를 반환한다.
- `false` 또는 `Ok(false)`가 backpressure를 뜻한다.
- 현재 구현 변경은 작지만 공통 오류 처리 정책의 “모든 submit 실패는 함수군별 error” 규칙을 바꿔야 한다.
- 호출자는 no-data와 submit 실패를 모두 boolean으로 받으므로 함수군별 error 정책이 약해진다.

### 대안 B — Error로 backpressure 표현

- 성공 값이 없는 Go submit은 `error`, Rust submit은 `Result<(), SubmitError>`를 반환한다.
- Backpressure는 Go `*SubmitError`, Rust `SubmitError`이며 code는 `BACKPRESSURED`다.
- Boolean은 값 조회와 caller-provided receive처럼 `false`가 정상 상태인 API에만 남는다.
- 공통 오류 처리 정책과 일치하고 성공 값이 없는 terminal method의 반환 타입이 단순해진다.

권고안은 대안 B다. PGR-COMMON-03 review에서 공통 오류 처리 정책, 비동기 완료 정책과 현재 구현을 함께
대조한 뒤 승인 여부를 결정한다. 승인된 초안으로 구현과 contract test를 진행하고, 정식 spec은 그 결과가
통과한 뒤 갱신한다.

## 3. 권고 exact interface

Go의 성공 값 없는 send·publish·reply terminal method는 `Submit(ctx context.Context) error`를 사용한다.
Request callback submit은 `Submit(ctx context.Context, callback Callback) error`를 사용한다. Core 작업 실패는
함수군별 concrete error로 반환하고 `errors.As`로 검사할 수 있어야 한다.

Rust의 성공 값 없는 terminal method는 `submit() -> Result<(), SubmitError>`를 사용한다. Request callback도
callback 등록 성공 여부만 이 반환값으로 전달하며 reply 완료 실패는 callback 결과로 전달한다.

## 4. Go Context 결정 항목

Go는 `Context`를 builder에 저장하거나 이름 없는 인자로 버리지 않는다. Review에서 다음 동작을 확정해야 한다.

- Submit 호출 전에 취소된 Context가 반환할 표준 error
- Native submit이 완료되기 전 cancellation과 deadline이 중단시키는 범위
- Request가 접수된 뒤 Context가 취소될 때 callback 호출 여부와 정확히 한 번 호출 보장
- 늦게 도착한 native completion의 폐기와 message ownership
- Handle close 뒤 남아 있는 goroutine과 callback 정리

Context error는 `errors.Is`로, Core 함수군별 error는 `errors.As`로 각각 검사한다. 서로 다른 실패 원인을 하나의
wrapper가 동시에 나타내도록 강제하지 않는다.

## 5. 구현 차이와 승인 조건

| 대상 | 현재 상태 | 권고안과 차이 |
|------|-----------|---------------|
| Go send·request submit | `(bool, error)`, backpressure는 `false, nil` | `error`, backpressure는 `*SubmitError` |
| Rust send submit | `Result<bool, SubmitError>`, backpressure는 `Ok(false)` | `Result<(), SubmitError>`, backpressure는 `Err(SubmitError)` |
| Go Context | 여러 terminal method가 인자를 사용하지 않음 | cancellation·deadline과 callback 수명 결정 필요 |

다음 조건을 모두 충족해야 승인된 구현 입력이 된다.

1. 대안 B를 선택한 이유와 기존 정식 spec·구현의 차이가 implementation gap에 기록된다.
2. Go와 Rust exact signature 및 Context 결정 항목이 review에서 승인된다.
3. Backpressure, Context cancellation, callback 완료와 ownership contract test가 정의된다.
4. 구현과 contract test가 통과한 뒤 공통 오류 처리 정책, 비동기 완료 정책과 언어별 정식 spec의 한국어·영문
   문서에 이 초안의 내용을 나누어 반영한다.
