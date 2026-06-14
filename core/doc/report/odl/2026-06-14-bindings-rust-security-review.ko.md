# Rust 바인딩 보안 검토 보고서

- 작성일: 2026-06-14
- 대상 범위: `bindings/rust/src/runtime/messaging/message.rs`, `bindings/rust/src/runtime/handles/ctx.rs`, `bindings/rust/include/zlink/common.h`
- 검토 방식: FFI 메시지 wrapper, slice 변환, Drop close, context Send/Sync 선언, 공개 버전 매크로를 코드 기준으로 확인했다.
- 상태: 2026-06-14 주의 항목 1건 문서화와 회귀 테스트 추가 완료. 복제 `common.h`의 patch 버전 불일치도 수정했다. Codex 에이전트 리뷰 통과.

## 요약

Rust 바인딩은 core C API의 raw pointer와 `zlink_msg_t`를 안전한 Rust 타입 뒤에 숨기려는 구조다. 이번 검토에서는 unsafe block이 공개 API의 안전 계약을 깨지 않는지 확인했다.

메시지 wrapper는 Drop에서 native message를 닫고, slice 변환 시 null pointer와 size 0을 확인한다. 다만 native context에 대해 `Send`와 `Sync`를 수동으로 구현하고 있어, core context가 실제로 thread-safe라는 계약이 반드시 유지되어야 한다.

추가 대조에서 Rust 패키지가 복제해서 배포하는 `bindings/rust/include/zlink/common.h`의 `ZLINK_VERSION_PATCH` 기본값이 `3`으로 남아 있었다. `bindings/rust/include/zlink.h`와 core/C 바인딩의 `common.h`는 `6.0.4`를 가리키므로, 사용자가 `<zlink/common.h>`만 직접 include하면 같은 패키지 안에서도 patch 버전 판정이 달라졌다.

## 확인된 이슈

### RUST-BINDING-001: native context의 Send/Sync는 core thread-safety 계약에 의존한다

- 심각도: 중간
- 상태: 2026-06-14 문서화와 회귀 테스트 추가 완료
- 근거:
  - `bindings/rust/src/runtime/handles/ctx.rs:65-72`는 raw native context handle을 가진 `NativeContext`에 대해 `unsafe impl Send`와 `unsafe impl Sync`를 선언한다.
  - `bindings/rust/src/runtime/contract_bridge.rs:80-84`는 private runtime trait에도 `Send + Sync`를 요구한다.
  - raw pointer 자체는 Rust 컴파일러가 thread-safety를 증명할 수 없으므로, 이 선언은 core C API의 동시성 보장에 의존한다.
- 영향:
  - core context가 여러 스레드에서 안전하다는 계약이 맞으면 기능 문제는 없다.
  - 해당 계약이 깨지면 Rust 쪽에서는 컴파일러가 막아 주지 못해 data race나 use-after-free 위험이 생길 수 있다.
  - 성능 영향은 없다.
- 권장 조치:
  - `Context`의 thread-safety 계약을 public 문서와 테스트로 고정한다.
  - core context close와 socket 생성·사용이 동시에 일어나는 경계의 회귀 테스트를 추가한다.
- 처리 결과:
  - `bindings/rust/src/contracts/core/context.rs`의 public `Context` 주석에 `Send`/`Sync` 계약과 `Arc` 기반 공유 사용, 마지막 owner가 drop될 때 context가 종료된다는 수명 규칙을 적었다.
  - `bindings/rust/README.rustdoc.md`에 같은 thread-safety 계약을 public 문서로 추가했다.
  - `bindings/rust/tests/boundary_tests.rs`에 `Context: Send + Sync` compile-time 확인과 여러 스레드에서 같은 `Context`로 socket을 생성·사용하는 회귀 테스트를 추가했다.

## 메시지 소유권 확인

- `bindings/rust/src/runtime/messaging/message.rs:17-35`는 native 메시지 생성 실패를 `ConfigError`로 변환한다.
- 같은 파일 `47-70`은 `zlink_msg_data` 결과가 null이거나 size가 0이면 빈 slice를 반환한다.
- 같은 파일 `77-92`는 clone 실패 시 새로 만든 native 메시지를 닫는다.
- 같은 파일 `116-121`은 Drop에서 native message를 닫는다.
- 같은 파일 `155-160`은 raw `zlink_msg_t`의 소유권을 Rust `Message`로 넘기는 unsafe 생성자를 별도로 둔다.
- `bindings/rust/include/zlink/common.h:13`은 `ZLINK_VERSION_PATCH` 기본값을 `4`로 맞춘다.
- `bindings/rust/tests/contract_tests.rs`는 C 전처리기로 `<zlink/common.h>`만 직접 include해 patch 값이 `4`인지 확인한다.

## 기능·성능 검토

메시지의 `as_bytes()`와 `data_mut()`는 native buffer를 slice로 바로 노출한다. 복사를 줄이는 장점이 있지만, 이 안전성은 `Message` 객체가 살아 있고 native 메시지가 닫히지 않는다는 Rust 쪽 소유권 규칙에 달려 있다.

검증:

- `cargo test --manifest-path bindings/rust/Cargo.toml context_is_send_sync_and_shared_socket_creation_is_safe -- --nocapture` 통과.
- `cargo test --manifest-path bindings/rust/Cargo.toml direct_common_header_version_matches_package -- --nocapture` 통과.
- `cargo test --manifest-path bindings/rust/Cargo.toml` 통과.
- Codex 에이전트 리뷰에서 "추가 이슈 없음" 판정을 받았다.

## 결론

Rust 바인딩의 메시지 wrapper는 검토한 범위에서 기본적인 null, close, clone 실패 처리를 갖추고 있다. 2026-06-14에 native context의 thread-safety 계약을 public 문서와 회귀 테스트로 고정했고, 복제 public header의 버전 매크로 불일치도 직접 include 회귀 테스트로 고정했다.
