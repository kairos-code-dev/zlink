# `bindings/rust/perf` POSD 후속 리팩토링 계획

> POSD 리뷰 등급: **B+** - 기존 핵심 목표는 달성했지만, single 종료 게이트의 시간 폴링과 multi backpressure 상태 분산이 아직 남아 있다
> 대상: `bindings/rust/perf/`
> 기준: `core/perf` 와 동일한 측정 의미 유지, PERF 정책 준수

## TODO

- [ ] `wait_finished()` 시간 폴링 제거안 설계
- [ ] 명시적 완료 신호 방식 확정
- [ ] multi backpressure helper 분리안 정리
- [ ] `common.rs` lifecycle/config 추가 분리 가치 재판정
- [ ] `cargo check` single/multi 검증 완료
- [ ] single/multi smoke 검증 완료
- [ ] RESULT 포맷 및 정책 회귀 점검
- [ ] 완료 정의 충족 여부 최종 리뷰

## 1. 목표

Rust perf 코드가 아래 성질을 유지하면서 남아 있는 구조적 냄새만 더 줄인다.

- `core/perf` 와 동일한 측정 의미를 유지한다.
- ready → warmup → active phase 의미는 그대로 둔다.
- single callback-only, multi 는 기존 정책 허용 조합을 유지한다.
- send/recv API 호출은 패턴 파일에서 직접 보이게 유지한다.
- hot path 에 새 lock, retry, sleep 기반 우회가 들어가지 않게 한다.

이 문서는 이미 끝난 작업을 다시 제안하지 않는다.
`PHASE_DRAIN` 삭제, `PhaseResult`/`MetricCollector` 도입, recv mode fail-fast, warning 0건 정리는 완료 상태로 본다.

## 2. 현재 구조 요약

- `single/src/common.rs` 는 metric header, latency stats, `MetricCollector`, ready gate, callback handler, send loop, 종료 대기, CLI config 를 모두 담고 있다.
- `multi/src/common.rs` 는 metric header, latency stats, result 출력, env 기반 settings, CLI args, stdin stop helper 를 함께 담는다.
- single 패턴 파일은 `common::send_loop()`, `common::handle_recv()`, `common::wait_finished()` 를 조합해 각 소켓 모델을 실행한다.
- multi 패턴 파일은 각 파일이 자체 `Poller` 와 `pending` / `pollout_on` 상태를 갖고 backpressure 를 처리한다.

근거:

- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L1)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L152)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L164)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L187)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L256)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L266)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L1)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L166)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L194)
- [`perf_multi_dealer_dealer_client.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_dealer_dealer_client.rs#L9)
- [`perf_multi_router_router_client.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_router_router_client.rs#L17)
- [`perf_multi_stream_server.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_stream_server.rs#L31)

## 3. 남은 POSD 문제

### 3.1 single 종료 게이트가 시간 폴링이다

`wait_finished()` 는 `AtomicBool` 을 받지만 실제로는 그 값을 누가 `store(true)` 하는지 코드상 보이지 않는다.
현재 구현은 종료 신호를 기다리는 것처럼 보이지만, 사실상 warmup + active + grace 시간을 고정 sleep/poll 하면서 지나간다.

이 구조의 문제는 다음과 같다.

- 종료 조건이 데이터 흐름이 아니라 시간 경과에 의존한다.
- `AtomicBool` 인자는 의미가 약하다. 호출 측에서는 완료 신호처럼 보이지만 실제 종료 제어는 시간 폴링이 담당한다.
- single 패턴 전부가 같은 게이트를 공유하므로, 한 곳의 waiting 정책 수정이 전체 benchmark lifecycle 에 영향을 준다.

근거:

- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L256)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L259)
- [`single/src/perf_pair.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/perf_pair.rs#L26)
- [`single/src/perf_pubsub.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/perf_pubsub.rs#L28)
- [`single/src/perf_dealer_router.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/perf_dealer_router.rs#L28)

POSD 관점 문제:

- temporal decomposition 이 약하다.
- 완료 신호와 시간 대기가 분리되어 있지 않다.
- `core/perf` 와 같은 phase 의미를 유지하면서도 종료 대기만 더 명시적으로 만들 여지가 있다.

### 3.2 multi backpressure 상태가 파일별로 반복된다

`perf_multi_dealer_dealer_client.rs`, `perf_multi_router_router_client.rs`, `perf_multi_dealer_router_server.rs`, `perf_multi_router_router_server.rs`, `perf_multi_stream_server.rs` 는 모두 `pending` / `pollout_on` / `modify_socket()` / `wait_all()` 패턴을 자체적으로 구현한다.

핵심 알고리즘은 같지만 각 파일이 상태 전이와 readiness 관리까지 직접 소유한다.

이 구조의 문제는 다음과 같다.

- 같은 backpressure 상태 머신이 여러 파일에 중복된다.
- `pollout_on` 과 `pending` 의 책임 경계가 패턴마다 조금씩 달라져 유지보수 시 차이를 추적하기 어렵다.
- send/recv API 는 inline 으로 남겨야 하지만 readiness bookkeeping 은 더 좁은 보조 타입으로 빼도 된다.

근거:

- [`perf_multi_dealer_dealer_client.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_dealer_dealer_client.rs#L9)
- [`perf_multi_dealer_dealer_client.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_dealer_dealer_client.rs#L83)
- [`perf_multi_router_router_client.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_router_router_client.rs#L42)
- [`perf_multi_dealer_router_server.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_dealer_router_server.rs#L30)
- [`perf_multi_router_router_server.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_router_router_server.rs#L31)
- [`perf_multi_stream_server.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/multi/src/perf_multi_stream_server.rs#L31)

POSD 관점 문제:

- hidden coupling 이다. 같은 flow-control 상태가 여러 파일에 넓게 흩어진다.
- common.rs 는 측정과 프로토콜 중심이어야 하는데, backpressure 상태는 각 파일에서 재구현되고 있다.
- change amplification 이 있다. 새 multi pattern 을 추가하면 동일한 상태 전이 패턴을 다시 적게 된다.

### 3.3 공통 모듈이 아직 너무 넓다

`single/src/common.rs` 와 `multi/src/common.rs` 는 각각 deep module 에 가깝지만, 아직도 측정 primitives 와 lifecycle/config helper 가 한 파일에 함께 있다.

이건 즉시 분리하지 않아도 되지만 follow-up 이후 유지보수 기준에서는 분리 후보가 된다.

근거:

- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L1)
- [`single/src/common.rs`](/home/hep7/project/kairos/zlink/bindings/rust/perf/single/src/common.rs#L266)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L1)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L166)
- [`multi/src/common.rs`](../../../../bindings/rust/perf/single/src/common.rs#L194)

## 4. 우선순위

### P0. single 종료 게이트를 시간 폴링에서 명시적 완료 신호로 바꾼다

가장 먼저 정리할 부분이다.
이는 측정값 자체를 바꾸지 않으면서 불필요한 grace time 의존성과 잘못된 의미의 `AtomicBool` 사용을 제거한다.

### P1. multi backpressure 상태를 좁은 보조 타입으로 분리한다

두 번째로 정리할 부분이다.
핵심 send/recv 호출은 패턴 파일에 남기되, readiness bookkeeping 과 pending 상태 전이만 더 좁은 ownership 으로 묶는다.

### P2. 공통 모듈의 lifecycle/config 책임을 더 분리한다

세 번째는 maintenance-only 에 가까운 수준이다.
즉시 필요하진 않지만, `common.rs` 가 길어지는 속도를 줄이기 위해 분리 여지를 남긴다.

## 5. 단계별 작업

### 단계 0. 기준 고정

할 일:

- `core/perf` 와 동일한 측정 의미를 재확인한다.
- single 은 callback-only, multi 는 기존 정책 허용 모드 조합만 유지한다.
- `RESULT,current,...` 포맷과 phase 의미는 변경하지 않는다.

완료 기준:

- follow-up 범위가 perf 목적과 정책 안으로 한정된다.
- 이미 완료된 `PHASE_DRAIN`, `PhaseResult`, `MetricCollector`, warning 0건 과제는 재탕하지 않는다.

### 단계 1. single 종료 게이트 정리

할 일:

- `wait_finished()` 의 시간 폴링을 없앤다.
- sender thread 종료와 receiver drain 완료를 구분하는 명시적 완료 신호를 도입한다.
- 필요한 경우 condition variable 또는 channel 기반의 bounded wait 로 바꾸되, sleep 기반 busy wait 는 제거한다.

완료 기준:

- `wait_finished()` 의 시간 폴링 경로가 사라진다.
- single 패턴들의 종료 조건이 데이터 흐름 중심으로 바뀐다.
- 측정 구간과 RESULT 의미는 그대로 유지된다.

### 단계 2. multi backpressure 상태 분리

할 일:

- `pending` / `pollout_on` / `modify_socket()` / `wait_all()` 의 전이만 담당하는 작은 helper 를 둔다.
- `try_send` / `try_recv` / `try_subscribe` 는 패턴 파일에 inline 상태로 유지한다.
- 각 패턴 파일의 send/recv hot path 는 숨기지 않는다.

완료 기준:

- backpressure bookkeeping 중복이 줄어든다.
- send/recv API 호출은 여전히 패턴 파일에서 직접 보인다.
- 새 helper 가 hot path 에 lock 나 retry 를 추가하지 않는다.

### 단계 3. 공통 모듈 경계 정리

할 일:

- `common.rs` 에 남아 있는 lifecycle/config 책임을 별도 helper 로 뺄 가치가 있는지 다시 본다.
- 실제 분리가 이득이 없으면 maintenance-only 로 남긴다.

완료 기준:

- 분리 가치가 없으면 추가 리팩토링을 억지로 만들지 않는다.
- 분리 가치가 있으면 single/multi 공통 해석과 benchmark 측정 책임이 더 좁아진다.

### 단계 4. 검증

할 일:

- `cargo check` 를 single/multi 둘 다 다시 실행한다.
- single smoke 를 1건 이상 실행한다.
- multi smoke 를 `DEALER_DEALER`, `ROUTER_ROUTER`, `SPOT`, `STREAM` 중 대표 패턴으로 실행한다.
- `RESULT,current,...` 포맷과 `core/perf` 동일 의미를 확인한다.

완료 기준:

- 빌드 통과
- smoke 통과
- RESULT 포맷 유지
- 측정 의미 불변

## 6. 검증 방법

- `cargo check --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/single/Cargo.toml`
- `cargo check --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/single/Cargo.toml --bin perf_pair`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/single/Cargo.toml --bin perf_pubsub`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml --bin perf_multi_pubsub_server`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml --bin perf_multi_pubsub_client`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml --bin perf_multi_spot_server`
- `cargo run --manifest-path /home/hep7/project/kairos/zlink/bindings/rust/perf/multi/Cargo.toml --bin perf_multi_spot_client`

검증 통과 기준:

- `status=complete` 유지
- `RESULT,current,...` 형식 유지
- single callback-only 제약 유지
- multi 정책 허용 조합 유지
- `core/perf` 와 동일한 측정 의미 유지

## 7. 완료 정의

- `wait_finished()` 의 시간 폴링이 제거되거나 명시적 완료 신호로 대체된다.
- multi backpressure 상태 머신이 더 좁은 ownership 으로 정리된다.
- `common.rs` 의 lifecycle/config 책임이 더 이상 넓어지지 않는다.
- send/recv API 호출은 패턴 파일에서 직접 보인다.
- `RESULT,current,...` 포맷과 측정 의미가 바뀌지 않는다.
- PERF 정책 위반이 새로 생기지 않는다.

## 8. 비범위

- `core/perf` 는 변경하지 않는다.
- 이미 끝난 `PHASE_DRAIN` 삭제나 `MetricCollector` 도입을 다시 다루지 않는다.
- metric 계산식, RESULT 포맷, phase 의미를 바꾸지 않는다.
- native layer 나 unsafe FFI 를 새로 추가하지 않는다.
- retry loop, 무한 sleep, 정책 우회는 도입하지 않는다.
- send/recv hot path 를 공통 wrapper 뒤로 숨기지 않는다.

## 9. 결론

Rust perf 는 이미 핵심 목표를 대부분 달성했다.
그렇지만 single 종료 게이트의 시간 폴링과 multi backpressure 상태 분산은 아직 POSD 관점에서 정리할 가치가 있다.
따라서 완전한 maintenance-only 는 아니고, 범위가 좁은 follow-up 리팩토링은 여전히 의미가 있다.
