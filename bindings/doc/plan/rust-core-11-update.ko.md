# Rust binding Core 11 최신화 실행 계획

> 대상 독자는 Rust binding의 crate, FFI와 platform payload를 갱신하는 담당자와 reviewer다. 이 문서는
> “승인된 Core candidate를 받아 Rust 작업만 독립적으로 완료하려면 무엇을 바꾸고 어떤 package consumer를
> 통과해야 하는가?”에 답한다.

## 1. 시작 조건과 현재 상태

[공통 계획](python-go-rust-core-11-update.ko.md)의 공통 시작 gate인 PGR-COMMON-01, PGR-COMMON-02와
PGR-COMMON-04가 통과하면 Python과 Go의 진행 상태와 관계없이 이 작업을 시작할 수 있다. 시작 log에는 Core
candidate identity, V11-R2 review, V11-M3-CORE-PKG evidence, install prefix와 raw symbol allowlist hash를
기록한다.

현재 crate version은 `9.0.4`다. Linux x86_64 payload는 Core `10.6.0`, Linux aarch64 payload는 major 9
SONAME을 사용한다. `src/lib.rs`, `src/runtime/`, test, sample과 perf에는 SpotNode, Spot과 Actor 공개 API 및
구현이 남아 있다. `build.rs`는 repository의 `core/build/lib`에 runtime이 있으면 package의 `native/`보다
먼저 선택한다.

상태는 **구현 미착수**다. 현재 `cargo test` 결과, `cargo package --no-verify` 결과와 path dependency를 쓰는
consumer를 Core 11 package 증거로 사용하지 않는다.

## 2. Version, 목표와 범위

`Cargo.toml`의 crate version을 `11.1.0`으로 올린다. Core만 갱신한 최초 Rust package는 patch 0에서 시작하고,
이후 Rust binding만 수정하면 binding patch를 올린다.

구현을 마치면 Rust source, test, sample, perf, crate metadata와 이 작업이 사용하는 package script를 공통
형식의 binding source manifest에 봉인한다. 이후 test와 `.crate`는 이 manifest로 materialize한 격리
snapshot에서 만든다. `.crate`, clean consumer와 독립 review evidence는 같은 manifest file SHA-256과
aggregate SHA-256을 기록한다.

Rust crate는 승인된 Core 11 raw C API만 투영한다. Context, message, raw socket, monitor, poller, timer와
utility를 Rust 관례에 맞게 제공하고 service API와 이전 Core runtime을 포함하지 않는다.

다음 작업은 범위 밖이다.

- Framework service runtime 구현
- 새 Core API 설계
- 외부 registry 게시와 release tag 생성

## 3. Package 입력과 native library 선택

`build.rs`가 repository `core/build/lib`를 자동으로 우선하지 않게 한다. Package build와 clean consumer는
crate의 대상 platform payload만 link하고 load해야 한다. 개발 과정에서 Core source build를 써야 하면
명시적인 option으로 분리하고, package gate에서는 그 option과 repository native path를 거부한다.

Cargo registry는 crate의 한 version에 checksum 하나만 허용한다. `zlink 11.1.0` `.crate` 하나에 지원하는 모든
platform runtime을 넣고 `build.rs`가 현재 target의 directory만 선택하게 한다. 이전 major SONAME,
`libzlink_c`, repository build path가 들어간 rpath와 필요하지 않은 system DLL을 포함하면 package 검증을
실패 처리한다. 각 runtime의 파일명, SHA-256과 public symbol inventory는 platform provenance와 같아야 한다.

## 4. 구현 작업

### RS-01 — Raw FFI inventory

- `src/runtime/native/ffi.rs`와 복사된 header를 승인 Core 11 raw header allowlist와 대조한다.
- Package header tree에서 `zlink/service/`와 이전 service include를 제거한다.
- Header에 없는 service function, struct, enum과 callback 선언을 제거한다.
- Raw header 함수와 Rust FFI 선언의 누락·추가를 machine-readable snapshot으로 검사한다.

### RS-02 — 공개 API와 runtime 정리

- `src/lib.rs`에서 MeshNode, Spot, Actor와 service operation의 module 및 re-export를 제거한다.
- `src/runtime/`의 service bridge, storage trait와 native 호출 구현을 제거한다.
- Service 전용 test, sample과 perf target을 source inventory와 runner에서 제거한다.
- Deprecated alias, compatibility module과 test helper로 service API를 유지하지 않는다.
- Raw 공개 API가 private FFI type이나 runtime storage trait을 노출하지 않는지 검사한다.

### RS-03 — Error와 ownership

[Go·Rust parity inventory](go-rust-return-parity.ko.md)의 Rust 열을 채우고 public API contract test를 연결한다.
No-data를 서로 다르게 설명하는 공통 spec의 한국어·영문 절을 PGR-COMMON-03에서 먼저 바로잡은 뒤 이 항목의
공개 시그니처와 test를 확정한다.

- 단일 함수군 메서드는 `Result<T, BindError>`처럼 함수군별 구체 error type을 반환한다.
- 여러 함수군을 실제로 조합하는 메서드만 `Result<T, ZlinkError>`로 넓힌다.
- 함수군별 error는 `std::error::Error`, `code()`와 `internal_errno()`를 제공한다.
- 기존 `native_errno()`는 제거하고 호환 alias를 남기지 않는다.
- Core 작업의 인자 검증은 해당 함수군의 `INVALID_ARGUMENT`로 변환한다.
- Core 작업과 독립된 값 객체를 만들기 전의 형식 검사만 별도 validation error를 사용할 수 있다.
- No-data는 `Ok(false)` 또는 `Ok(None)`으로 표현하고 실제 receive 실패는 `Err(RecvError)`로 반환한다.
- Non-blocking submit backpressure는 `Err(SubmitError)`로 반환한다.
- Message send, receive, copy·move와 close의 ownership을 contract test로 검증한다.

### RS-04 — Hot path 설계 검토

- Message 생성, send, receive와 request completion에서 owned value, borrow와 native buffer가 만들어지는 위치를
  기록한다.
- 공개 계약이 소유권 이전이나 snapshot을 요구하지 않는 경로에서 `Vec` clone, `collect`와 payload 재할당을
  message마다 반복하지 않는다.
- 임의의 `&str`을 NUL 종료 문자열로 바꾸는 `CString`은 C ABI가 요구하는 conversion으로 분류한다. 고정된
  상수나 같은 입력을 반복 사용하는 경로에서만 재사용 가능성을 검토하며, 일반 입력의 lifetime과 validation을
  복잡하게 만들어 allocation을 제거하지 않는다.
- 반복 send·receive마다 `Box`, closure, thread 또는 channel을 만들지 않는다. Callback ownership 때문에
  필요한 heap allocation은 등록 시점과 호출 시점을 구분해 기록한다.
- 독립 socket의 hot path를 전역 `Mutex`나 하나의 shared queue로 직렬화하지 않는다. `Arc`, `Mutex`와 atomic은
  실제 shared lifetime 또는 synchronization을 보호하는 최소 범위로 제한한다.
- `tests/hot-path-cost-inventory.json`에서 비용 발생 source, 분류, 이유와 guard test를 연결한다.
- `cargo test --test optimization_guard_tests`가 종료 코드 0이고 inventory의 `unclassified`가 0건이어야
  통과한다.

### RS-05 — Sample과 perf smoke

- Pair, pub/sub, dealer/router request, STREAM receive·packet callback과 monitor sample을 유지한다.
- Spot, Actor와 service operation sample·perf scenario를 runner에서 제거한다.
- Sample과 perf는 crate의 public API만 사용한다.
- Private FFI, native symbol 직접 호출과 raw byte 우회를 사용하지 않는다.
- Runner가 승인 crate의 runtime path와 SHA-256을 출력하고 repository `core/build`을 선택하지 않게 입력 경로를
  바꾼다.
- 공식 report를 만들지 않는 `--smoke` mode를 runner에 추가하고 다음 명령을 실행한다.

```bash
# Single runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks.sh --smoke --pattern PAIR --duration 1 --msg-sizes 64 --transports inproc --runs 1

# Multi runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks_multi.sh --smoke --pattern MULTI_DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp --runs 1 --clients 1
```

- 두 명령은 공통 계획의 smoke 판정에 따라 ready, active, 필수 `RESULT` metric과 exit code만 확인한다. 결과
  수치는 공식 report나 성능 비교에 사용하지 않으며 실행 뒤 report 파일이 남지 않아야 한다.

### RS-06 — 정식 문서

구현과 contract test가 통과한 뒤 Rust 정식 spec의 한국어·영문 문서와 guide를 실제 raw 공개 API에 맞춘다.
구현 전 목표를 정식 spec에 먼저 기록하지 않는다.

## 5. Platform 검증

현재 target 선택은 `build.rs`, payload는 `native/`에 있다. 다음 표는 지원 계약이 아니라 이 source에서 확인한
구현 기준선이다.

| Platform | 현재 native 후보 | 확인할 차이 |
|----------|------------------|-------------|
| Linux x86_64 | `linux-x86_64` | Core 10.6.0 교체 필요 |
| Linux aarch64 | `linux-aarch64` | Major 9 payload 교체 필요 |
| macOS x86_64/aarch64 | `darwin-*` | Core 11 runtime과 load 검증 필요 |
| Windows x86_64 | `windows-x86_64` | Core 11 DLL과 dependency inventory 검증 필요 |
| Windows aarch64 | `windows-aarch64` | Core 11 DLL과 native consumer 검증 필요 |

RS-01에서 spec, guide, `build.rs`와 crate payload를 대조해 지원 platform을 확정한다. 확정된 각 platform은 같은
Core candidate identity와 platform별 runtime provenance를 사용하고 native Rust consumer를 실행한다. Core
artifact 생성 경로나 loader가 없는 platform은 지원 완료로 표시하지 않고 별도 작업으로 남긴다.

## 6. Crate와 clean consumer

Candidate tooling은 먼저 `cargo package --allow-dirty`를 verification이 활성화된 상태로 실행한다.
`--no-verify`는 사용하지 않는다. 생성된 `.crate`의 file list와 압축 해제 결과에서 다음 항목을 확인한다.

- `Cargo.toml`의 version이 `11.1.0`이고 publish 대상 source가 모두 포함되어 있다.
- 지원하는 모든 platform의 Core 11 runtime과 필요한 public header만 포함되어 있다.
- 이전 runtime, service header, repository build output과 absolute path가 없다.
- Package runtime과 header hash가 platform provenance와 같다.

Clean consumer는 `.crate`를 source directory로 풀어 path dependency로 연결하지 않는다. Candidate tooling이
`.crate`와 dependency를 Cargo-compatible local registry에 등록하고, 새 consumer는 다음 조건으로 검증한다.

- 빈 `CARGO_HOME`과 target directory를 사용한다.
- Consumer dependency는 `zlink = "=11.1.0"`으로 선언하고 `path`와 `[patch]`를 사용하지 않는다.
- Cargo config는 명시한 local registry만 사용하며 network fallback을 허용하지 않는다.
- Local registry만 허용한 상태에서 `cargo generate-lockfile`로 dependency를 고정한다.
- 이어서 `cargo fetch --locked`, `cargo build --locked`, runtime version 확인과 raw message smoke를 실행한다.
- Binary의 dynamic dependency가 Cargo package에 포함된 runtime을 가리키는지 확인한다.
- `.crate`, registry index entry, checksum, runtime과 header hash를 package evidence에 기록한다.

## 7. 검증 표

| Gate | 상태 | Evidence |
|------|------|----------|
| 공통 candidate 입력 확인 | `PENDING` | — |
| Rust binding source manifest | `PENDING` | — |
| Crate version `11.1.0` | `PENDING` | — |
| Raw FFI·header·symbol allowlist | `PENDING` | — |
| Public API snapshot과 service 부재 | `PENDING` | — |
| `cargo test --workspace --all-targets` | `PENDING` | — |
| `cargo clippy --workspace --all-targets -- -D warnings` | `PENDING` | — |
| Hot path cost inventory와 optimization guard | `PENDING` | — |
| Perf runner smoke | `PENDING` | — |
| Go·Rust parity inventory | `PENDING` | — |
| Raw sample process runner | `PENDING` | — |
| Verification을 통과한 `.crate` contents | `PENDING` | — |
| Path dependency 없는 clean consumer | `PENDING` | — |
| 지원 platform native consumer | `PENDING` | — |
| 한국어·영문 spec, rustdoc과 guide | `PENDING` | — |
| 독립 review | `PENDING` | — |

명령, 종료 코드, test 수, `.crate` SHA-256과 실패 원인은 `bindings/doc/plan/log/rust/` 아래 날짜별 log에
기록한다.

## 8. 완료 조건

다음 조건을 모두 만족해야 Rust 작업이 완료된다.

1. Crate version은 `11.1.0`이고 승인된 Core candidate identity와 Rust binding source manifest를 기록한다.
2. Raw FFI와 공개 API가 Core 11 allowlist에 맞고 service API가 없다.
3. 함수군별 error, no-data와 ownership이 parity inventory의 Rust 열과 일치한다.
4. Source test, clippy, hot path design review, perf smoke와 raw sample process가 통과한다.
5. Path dependency 없는 clean consumer가 local registry package의 runtime으로 실제 message를 송수신한다.
6. 지원한다고 명시한 모든 platform에서 package contents와 runtime load가 검증된다.
7. 정식 spec, rustdoc과 guide가 구현과 일치한다.
8. 성능 수치 개선은 후속 Rust 계획으로 분리되어 있으며 이번 완료 근거로 사용하지 않는다.
9. Critical, high, medium finding과 실행하지 않은 필수 gate가 남아 있지 않다.
