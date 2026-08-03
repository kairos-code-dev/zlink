# Go·Rust return-based parity inventory

> 대상 독자는 Go와 Rust bindings의 공개 시그니처와 error mapping을 구현·검토하는 개발자다. 이 문서는
> “두 언어의 문법은 달라도 어떤 성공, 실패, no-data와 ownership 의미를 같게 검증해야 하는가?”에 답한다.

## 1. 문서 역할

반환과 error 의미의 정본은 [공통 bindings spec](../spec/README.ko.md)의 `오류 처리 정책` 절이다. 이
문서는 Core 11 최신화 과정에서 대응 메서드를 비교하고 evidence를 연결하는 inventory다. 정식 계약을
추가하거나 변경하지 않는다.

정식 계약을 변경해야 하면 plan 또는 별도 draft에서 설계 review를 먼저 진행한다. 구현과 contract test가
통과한 뒤 공통 spec, [Go spec](../spec/go/README.ko.md)과 [Rust spec](../spec/rust/README.ko.md)의 한국어·영문
문서를 함께 갱신한다.

## 2. 공통 반환 의미

| 의미 | Go 표현 | Rust 표현 |
|------|---------|-----------|
| 성공 값 없음 | `error`가 `nil` | `Ok(())` |
| 성공 값 있음 | `(T, nil)` | `Ok(T)` |
| 값이 없을 수 있는 조회 | `(zero, false, nil)` | `Ok(None)` |
| non-blocking 수신 결과 없음 | `(false, nil)` | `Ok(false)` |
| non-blocking submit backpressure | 실제 값이 `*SubmitError`인 `error` | `Err(SubmitError)` |
| 단일 함수군 실패 | `error`의 실제 값이 함수군별 concrete error | `Err(<Category>Error)` |
| 여러 함수군을 조합한 실패 | 실제 값은 함수군별 concrete error이며 모두 `ZlinkError` interface 구현 | `Err(ZlinkError)` |

공통 spec의 caller-provided receive 절은 no-data를 정상 결과로 규정하지만 flags 절에는 Go·Rust가 error를
반환한다는 상충 문구가 있다. Source 구현 전에 두 절의 한국어·영문 내용을 정상 no-data 반환으로 통일한다.
실제 Core 실패는 `false`, `None` 또는 zero value로 숨기지 않는다. 동일한 Core 작업에서 성공하거나 실패했을
때 message part와 handle ownership도 두 언어에서 같아야 한다.

## 3. 함수군과 입력 검증

Core API가 반환하는 result enum이 함수군을 정한다.

| 함수군 | Go 실제 error type | Rust error type | 대표 작업 |
|--------|--------------------|-----------------|-----------|
| Submit | `*SubmitError` | `SubmitError` | send, publish와 request submit |
| Request | `*RequestError` | `RequestError` | request completion |
| Recv | `*RecvError` | `RecvError` | receive, subscription event와 monitor receive |
| Handler | `*HandlerError` | `HandlerError` | callback handler 등록 |
| Close | `*CloseError` | `CloseError` | close와 destroy |
| Bind | `*BindError` | `BindError` | bind |
| Connect | `*ConnectError` | `ConnectError` | connect, disconnect와 unbind |
| Config | `*ConfigError` | `ConfigError` | option, poller와 timer 설정 |

Bind endpoint 형식처럼 Core 작업의 인자를 검사하다 실패하면 함수군의 `INVALID_ARGUMENT`를 사용한다.
Core 작업과 독립된 값 객체를 만들기 전에 형식만 검사할 때만 별도 validation error를 사용할 수 있다.

Go의 공개 시그니처는 단일·복합 경계 모두 `error`를 반환한다. 실제 값은 함수군별 concrete error를 유지하며
`ZlinkError`, `Code()`, `InternalErrno()`와 `errors.As`를 지원한다. 복합 경계의 GoDoc은 발생 가능한 함수군을
나열한다. Rust의
함수군별 error는 `std::error::Error`, `code()`와 `internal_errno()`를 지원하며 단일 함수군 메서드의
`Result`에 직접 나타난다. 기존 `NativeErrno()`와 `native_errno()`는 major 전환에서 제거하고 호환 alias를
남기지 않는다.

## 4. 대응 API inventory

아래 표는 PGR-COMMON-03에서 Core 11 raw 공개 API를 대조해 채운다. 한쪽 메서드가 없으면 `GAP`으로 표시하며,
private helper나 test-only adapter로 메우지 않는다.

| Core 작업 | Go 공개 메서드 | Rust 공개 메서드 | 성공·no-data 의미 | Error 함수군 | Ownership | 상태 | Evidence |
|-----------|----------------|------------------|--------------------|--------------|-----------|------|----------|
| Context 생성·종료 | `TBD` | `TBD` | `TBD` | Config·Close | `TBD` | `PENDING` | — |
| Message 생성·copy·move·close | `TBD` | `TBD` | `TBD` | Config | `TBD` | `PENDING` | — |
| Socket bind | `TBD` | `TBD` | 성공 값 없음 | Bind | 유지 | `PENDING` | — |
| Socket connect·disconnect·unbind | `TBD` | `TBD` | 성공 값 없음 | Connect | 유지 | `PENDING` | — |
| Multipart send·publish | `TBD` | `TBD` | `TBD` | Submit | `TBD` | `PENDING` | — |
| Non-blocking submit backpressure | `TBD` | `TBD` | error 반환 | Submit | 유지 | `PENDING` | — |
| Multipart receive | `TBD` | `TBD` | no-data는 정상 결과 | Recv | `TBD` | `PENDING` | — |
| Request submit·completion | `TBD` | `TBD` | submit과 completion 분리 | Submit·Request | `TBD` | `PENDING` | — |
| Handler 등록 | `TBD` | `TBD` | 성공 값 없음 | Handler | 유지 | `PENDING` | — |
| Socket monitor | `TBD` | `TBD` | no-data는 정상 결과 | Config·Recv·Close | `TBD` | `PENDING` | — |
| Poller·timer | `TBD` | `TBD` | no-data는 정상 결과 | Config·Recv·Close | `TBD` | `PENDING` | — |
| Option set·get | `TBD` | `TBD` | `TBD` | Config | 유지 | `PENDING` | — |

## 5. Contract test 규칙

각 행은 Go와 Rust에서 같은 public-observable 조건을 만든다. Result enum을 private hook으로 직접 주입하지
않는다.

- 성공, no-data와 함수군별 대표 실패를 public API로 재현한다.
- Go는 반환된 `error`를 `errors.As`로 함수군별 type과 `ZlinkError`에 대조한다.
- Rust는 compile-time assertion으로 단일 함수군 메서드의 구체 error type을 확인한다.
- 두 언어에서 `code`와 `internal_errno`가 같은 Core 의미를 나타내는지 비교한다.
- 성공과 실패 뒤 message와 handle을 다시 사용하거나 close하여 ownership 변화를 확인한다.
- Submit 실패와 request completion 실패를 별도 scenario로 검증한다.
- Non-blocking send, publish와 request submit의 backpressure가 Go에서는 `*SubmitError`, Rust에서는
  `SubmitError`이고 두 error의 code가 `BACKPRESSURED`인지 검증한다.

실행 결과는 `bindings/doc/plan/log/common/`의 parity log에 기록하고 이 inventory의 Evidence 열에서 연결한다.

## 6. 동기화와 완료 조건

다음 대상이 같은 의미를 유지해야 한다.

- `bindings/doc/spec/README.ko.md`와 영문 대응 문서
- `bindings/doc/spec/go/README.ko.md`와 영문 대응 문서
- `bindings/doc/spec/rust/README.ko.md`와 영문 대응 문서
- Go와 Rust public API snapshot
- Go와 Rust contract test scenario

모든 필수 행이 `PASS`이고 Critical, high, medium finding이 없을 때 parity gate가 완료된다. Go 또는 Rust
한쪽만 통과하면 완료가 아니다.
