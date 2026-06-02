# Rust bindings 성능 개선 라운드 기록

이 파일은 Rust bindings 성능 개선 실험 로그다. 메인 계획 문서에는 complete 결과와 최종 판단만 남긴다.

## Single 공통 송신 직접 message 작성

- 변경 파일: `bindings/rust/perf/single/src/common.rs`
- 후보: single 공통 `send_loop`에서 중간 `Vec`를 채운 뒤 `Message::try_from(&buf)`로 복사하지 않고, public `Message::with_size(...).data_mut()`로 native message payload를 직접 채운다.
- public contract 변경: 없음.
- perf runner, HWM/profile, 목표치, C baseline 변경: 없음.

검증:

- `cargo test --manifest-path bindings/rust/perf/single/Cargo.toml --no-run`

Complete 측정:

- PUBSUB small:
  - `perf_rust_single_linux_20260602_162501_rust_single_direct_message_pubsub_small_probe_20260602.txt`
  - status=complete, expected/actual result lines 40/40
  - C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
  - 결과: `PUBSUB wss 64B`가 83.0%로 통과했다. `tcp 64B` 75.6%, `ws 64B` 77.0%, `tls 64B` 79.5%, `tls 256B` 78.5%는 기준에 못 닿아 미달이다.
- routed large:
  - `perf_rust_single_linux_20260602_162557_rust_single_direct_message_routed_large_probe_20260602.txt`
  - status=complete, expected/actual result lines 90/90
  - 결과: 새 통과 없음. `DEALER_ROUTER`/`ROUTER_ROUTER`의 tcp/ws/tls 65536B 이상은 모두 미달이다.
- SPOT tcp 1024B:
  - `perf_rust_single_linux_20260602_164334_rust_single_direct_message_spot_tcp1024_probe_20260602.txt`
  - status=complete, expected/actual result lines 5/5
  - 결과: 56.7%로 기준에 못 닿아 미달이다.

판단:

- 이 후보는 `PUBSUB wss 64B`를 새로 통과시켜 유지한다.
- routed large는 sender message 작성보다 public `recv(&mut Received, ...)` envelope와 routed receive path 비용이 지배적이다. C의 part 직접 수신 의미를 public contract 변경 없이 Rust perf에만 우회 적용하지 않는다.
- `SPOT tcp 1024B`는 기존 SPOT 전용 direct-message 후보가 이미 적용된 구간이고, 공통 `send_loop` 후보의 적용 대상이 아니므로 새 통과를 만들지 못했다.

## PUBSUB receive storage 재사용 재확인

- 변경 후보: `bindings/rust/perf/single/src/perf_pubsub.rs`에서 `TopicMessage::empty()`를 receive loop 밖으로 옮겨 같은 envelope를 재사용한다.
- public contract 변경: 없음.

Complete 측정:

- `perf_rust_single_linux_20260602_164508_rust_single_direct_message_pubsub_reuse_small_probe_20260602.txt`
- status=complete, expected/actual result lines 40/40
- C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
- 결과: 통과 수는 direct-message 단독 후보와 같고, `tcp 64B` 74.1%, `ws 64B` 75.4%, `wss 64B` 82.7%, `tls 64B` 79.2%, `tls 256B` 78.6%였다.

판단:

- 통과 수를 늘리지 못했고 direct-message 단독 후보보다 일부 수치가 낮아 최종 코드에 남기지 않았다.

## Runtime recv single-part storage fast path 후보 기각

- 변경 후보: Rust binding runtime의 `recv(out, ...)`와 `subscribe(out, ...)`에서 single-part 성공 경로는 새 `Vec<Message>`를 만들지 않고 caller-provided `Received`/`TopicMessage`의 `parts` storage를 직접 재사용한다.
- public contract 변경: 없음.
- 검증: `cargo test --manifest-path bindings/rust/Cargo.toml`, `cargo test --manifest-path bindings/rust/perf/single/Cargo.toml --no-run`

Complete 측정:

- PUBSUB small:
  - `perf_rust_single_linux_20260602_165034_rust_single_recv_into_pubsub_small_probe_20260602.txt`
  - status=complete, expected/actual result lines 40/40
  - 결과: 통과 수는 direct-message 단독 후보와 같았다. `PUBSUB wss 64B`는 84.0%로 통과했지만, `tcp 64B` 73.8%, `ws 64B` 75.6%, `tls 64B` 77.9%, `tls 256B` 78.7%는 미달이다.
- routed tcp large:
  - `perf_rust_single_linux_20260602_165145_rust_single_recv_into_routed_tcp_large_probe_20260602.txt`
  - status=complete, expected/actual result lines 30/30
  - 결과: 새 통과 없음. `DEALER_ROUTER`/`ROUTER_ROUTER` tcp 65536B 이상은 모두 미달이다.

판단:

- 새 통과를 만들지 못했고 runtime receive materialization 코드 복잡도를 늘리므로 최종 코드에 남기지 않았다.

## SPOT direct-message no-fill 후보 기각

- 변경 후보: `bindings/rust/perf/single/src/perf_spot.rs`의 1024B 이하 direct-message 경로에서 payload 전체 `fill(0)`를 제거하고 header만 쓴다.
- public contract 변경: 없음.

Complete 측정:

- `perf_rust_single_linux_20260602_165917_rust_single_spot_no_fill_tcp1024_probe_20260602.txt`
- status=complete, expected/actual result lines 5/5
- C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
- 결과: `SPOT tcp 1024B`는 59.1%로 기준에 못 닿아 미달이다.

판단:

- 기존 문서의 최종 SPOT direct-message 값보다도 낮고 새 통과를 만들지 못해 최종 코드에 남기지 않았다.

## Received context enum inline 후보 기각

- 변경 후보: `Received`의 routed send/reply context를 `Box<dyn ...>` 대신 `pub(crate)` enum 값으로
  보관해 routed receive마다 발생하는 heap allocation과 dynamic dispatch를 줄인다.
- public contract 변경: 없음. `Received.send()`/`Received.reply()` public 의미와 `.d.ts`에
  해당하는 공개 표면은 바꾸지 않았다.
- perf runner, HWM/profile, 목표치, C baseline 변경: 없음.

검증:

- 후보 적용 뒤 `cargo test --manifest-path bindings/rust/Cargo.toml`: 통과
- 후보 적용 뒤 `cargo test --manifest-path bindings/rust/perf/single/Cargo.toml --no-run`: 통과

Complete 측정:

- `perf_rust_single_linux_20260602_174853_rust_single_received_context_inline_routed_65536_probe_20260602.txt`
- status=complete, expected/actual result lines 30/30
- C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
- 결과:
  - `DEALER_ROUTER tcp/ws/tls 65536B`: 12.6% / 28.7% / 55.1%
  - `ROUTER_ROUTER tcp/ws/tls 65536B`: 12.7% / 24.9% / 54.4%

판단:

- routed one-way 기준 70%에 닿은 항목이 없어 새 통과를 만들지 못했다.
- 일부 값은 메인 문서의 기존 routed large 수치보다 낮아 회귀 위험이 있다.
- 후보 코드는 되돌렸고, 되돌린 뒤 `cargo test --manifest-path bindings/rust/Cargo.toml`를 다시
  통과했다.

## PUBSUB first_part 직접 접근 후보 기각

- 변경 후보: single `PUBSUB` receive loop에서 `common::message_payload(received.parts())` 대신
  public `TopicMessage::first_part().as_bytes()`를 직접 사용해 slice 조회와 `last()` 호출을 줄인다.
- public contract 변경: 없음.
- perf runner, HWM/profile, 목표치, C baseline 변경: 없음.

검증:

- 후보 적용 뒤 `cargo test --manifest-path bindings/rust/perf/single/Cargo.toml --no-run`: 통과

Complete 측정:

- `perf_rust_single_linux_20260602_175730_rust_single_pubsub_first_part_probe_20260602.txt`
- status=complete, expected/actual result lines 40/40
- C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`
- 결과:
  - `PUBSUB tcp 64/256B`: 74.4% / 83.3%
  - `PUBSUB ws 64/256B`: 76.2% / 83.8%
  - `PUBSUB wss 64/256B`: 83.7% / 89.6%
  - `PUBSUB tls 64/256B`: 78.4% / 78.1%

판단:

- 새 통과 항목이 없다.
- 기존 채택값과 비교하면 `wss 64B`만 소폭 높고 `ws 256B`, `wss 256B`, `tls` 등은 낮아져
  회귀 위험이 있다.
- 후보 코드는 되돌렸고, 되돌린 뒤 `cargo test --manifest-path bindings/rust/perf/single/Cargo.toml --no-run`를
  다시 통과했다.

## Rust single table transport full 확인

- 목적: Rust single에서 추가 후보를 적용/기각한 뒤 다음 언어로 넘어갈 수 있는지 table transport
  범위 full로 확인한다.
- 명령: `PERF_FAIL_FAST=1 bindings/rust/perf/run_benchmarks.sh --transports tcp,ws,wss,tls --duration 1 --runs 3 --results-tag rust_single_after_candidates_table_transports_full_20260602`
- 결과 파일: `perf_rust_single_linux_20260602_175921_rust_single_after_candidates_table_transports_full_20260602.txt`
- status=complete, expected/actual result lines 720/720
- C 기준: `perf_c_single_linux_20260530_231803_round_20260530_c_single_baseline.txt`

요약:

- 통과 119개, 미달 25개.
- 잔여 미달:
  - `PUBSUB tcp 64B`, `PUBSUB ws 64B`, `PUBSUB tls 64/256B`
  - `DEALER_ROUTER`/`ROUTER_ROUTER` `tcp/ws/tls 65536/131072/262144B`
  - `SPOT tcp/ws/tls 1024B`

판단:

- single 공통 송신 직접 message 작성은 `PUBSUB wss 64B` 통과를 유지한다.
- `PUBSUB first_part`와 `Received context enum inline` 후보는 새 통과가 없거나 회귀 위험이 있어
  되돌렸다.
- routed 대용량은 public `Received` envelope와 routed send context를 유지해야 한다. C의 part 직접
  수신 의미를 Rust perf에만 우회 적용하려면 public contract 확장이나 perf-only 내부 우회가 필요해
  이번 원칙에 맞지 않는다.
- `SPOT tcp/ws/tls 1024B`는 direct-message/no-fill/recv storage 후보 뒤에도 기준에 못 닿았다.
- Rust single은 10% gate를 넘지만, 현재 public contract와 perf 원칙을 지키는 추가 내부 후보가
  확인되지 않아 잔여 항목은 미달로 유지한다.
