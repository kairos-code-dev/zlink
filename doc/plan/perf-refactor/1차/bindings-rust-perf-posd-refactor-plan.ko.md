# `bindings/rust/perf` POSD 리팩토링 계획

> POSD 리뷰 등급: **B+** — common.rs 추상화 우수, multi backpressure 중복과 클로저 래핑 반복
> 대상: `bindings/rust/perf/`

## 1. 목표

Rust perf 코드가 아래 성질을 구조적으로 만족하도록 만든다.

- multi client의 backpressure 루프가 공통 함수로 추출된다.
- single 패턴 파일의 클로저 래핑 반복이 매크로 또는 제네릭으로 축소된다.
- `core/perf`와 동일한 측정 의미를 유지한다.

## 2. 현재 문제 요약

### 2.1 multi backpressure 루프 70% 중복

- `perf_multi_dealer_dealer_client.rs` `run_send_phase` (80-163줄, 84줄)
- `perf_multi_router_router_client.rs` 유사 클로저 (43-100줄, 58줄)

두 파일 모두:
1. `modify_socket(POLLOUT)` 호출
2. pending flag 상태 머신
3. poller backpressure 처리
4. phase runner 클로저

알고리즘 동일, 소켓 타입만 다름.

### 2.2 single 패턴 클로저 래핑 반복

- `perf_pair.rs:36-40`: `socket.send(...)` 클로저
- `perf_pubsub.rs:38-41`: `socket.publish(...)` 클로저
- `perf_dealer_router.rs:39-41`: `socket.send(...)` 클로저

`send_loop`에 전달하는 클로저 생성이 95% 동일하나,
Rust의 borrow 시맨틱 때문에 단순 추출이 어려움.

### 2.3 single 콜백 등록 패턴 반복

- `perf_pair.rs:24-29`: `Arc` clone + `on_receive` 등록
- `perf_pubsub.rs:26-31`: 동일 패턴
- `perf_dealer_dealer.rs`: 유사 패턴

`Arc<Mutex<Collector>>` clone → `on_receive` 핸들러 등록이 반복.

## 3. 설계 원칙

- `core/perf`와 동일한 측정 의미 절대 우선
- Rust ownership/borrowing 규칙 준수
- common.rs의 기존 추상화 (send_loop, handle_recv) 유지 및 확장
- 매크로는 최소한으로 사용 (코드 가독성 우선)
- **inline 코드 요구사항** (`PERF_POLICY.md` 715-727줄):
  패턴 파일에서 send/recv API 호출, EAGAIN 처리, 모델 선택, ready gate,
  소켓 생성이 직접 보여야 한다. 설정/TLS/결과 출력만 공통화 가능.
- **매 단계 완료 시**:
  1. build + 테스트 통과
  2. smoke perf 실행: 정상 종료, RESULT line 정책 형식 출력, 결과 파일 생성 확인
     (수치 비교는 하지 않음 — 병렬 작업으로 측정값 왜곡 가능)
  3. hot-path에 새 lock/alloc/log 없음 확인
  4. full comparable run + 수치 비교는 **전체 리팩토링 완료 후 순차 실행**

## 4. 단계별 실행 계획

### 단계 0. 현황 동결

할 일:
- multi client 간 backpressure 로직 diff
- single 패턴 간 클로저/콜백 등록 diff
- `doc/perf/PERF_POLICY.md` recv/callback 모드 매트릭스 감사:
  - single: callback-only (전 패턴, 예외 없음)
  - multi: recv-only (SPOT/STREAM만 dual-mode)
  - 미지원 조합이 fail-fast하는지 확인
- STREAM 공유 클라이언트 경로 현황 확인
- direct native API 사용 현황 (unsafe FFI 직접 호출 grep)
- `core/perf` 대비 semantic parity 체크리스트:
  - throughput/bandwidth/latency 정의 일치
  - phase 구조 (ready → warmup → active)
  - RESULT line 형식
  - recv_mode / direction 일치

완료 기준:
- 통합 대상이 함수 단위로 정리됨
- PERF_POLICY 위반 목록이 작성됨

### 단계 1. phase drain 삭제 (recv drain loop는 유지)

**중요 구분**: recv drain loop (poller POLLIN→nonblocking recv)는 유지.
`PHASE_DRAIN` enum과 그것을 사용하는 별도 phase 코드만 삭제.

할 일:
- `PHASE_DRAIN` 상수 삭제 (`single/src/common.rs:22`, `multi/src/common.rs:10`)
- `phase == PHASE_DRAIN` 분기 삭제 (`single/src/common.rs:155`)
- warmup→active 전환 시 drain 메시지 전송 삭제 (`single/src/common.rs:211`)
- recv 모델의 poller event loop 내 nonblocking recv은 유지
- warmup 메시지가 active latency에 혼입되지 않는지 확인

완료 기준:
- `PHASE_DRAIN` enum grep 0건
- recv drain loop 정상 유지
- smoke perf: 정상 종료 + RESULT line 출력 + 결과 파일 생성 확인
- build + 테스트 통과

### 단계 2. multi 측정 인프라 공통화

할 일:
- `common.rs`에 측정 인프라만 추가:
  - `PhaseResult` 타입, metric header encode/decode, percentile 계산, RESULT 출력
- **`modify_socket(POLLOUT)`, pending flag/deque, EAGAIN 처리, send API 호출은
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)
- backpressure 상태 머신 로직은 패턴 파일에 남김 (inline 요구사항)

완료 기준:
- metric/RESULT 인프라가 common.rs 1곳
- POLLOUT/pending/EAGAIN/send API는 패턴 파일에서 직접 보임

### 단계 3. single 콜백 인프라 공통화

할 일:
- `common.rs`에 `MetricCollector` 생성 보조만 추가
- **`socket.on_receive(...)` 등록과 callback 모델 선택은
  각 패턴 파일에 inline 유지** (`PERF_POLICY.md` 715-727줄)
- `Arc<Mutex<_>>` 패턴은 남기되 `MetricCollector` 초기화만 공통화

완료 기준:
- MetricCollector 생성이 공통 헬퍼
- `on_receive` 등록은 패턴 파일에서 직접 보임

### 단계 4. 검증

할 일:
- `cargo build` 확인
- single/multi 전 패턴 smoke
- `core/perf` 대비 semantic parity 확인
- 결과 파일 `bindings/rust/perf/results/` 확인

완료 기준:
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지

## 5. 리스크

### 리스크 1. Rust borrow checker와의 충돌

backpressure 루프의 제네릭 추출 시 lifetime annotation이 복잡해질 수 있음.

대응:
- 과도한 제네릭화보다 trait object (`&dyn Socket`) 사용 검토
- 컴파일 타임 비용이 허용 범위인지 확인
- 추출이 borrow 규칙을 복잡하게 만들면 현상 유지

### 리스크 2. 클로저 래핑의 본질적 반복

Rust에서 소켓 메서드를 클로저로 감싸는 것은 borrow 시맨틱 상
완전한 제거가 불가능할 수 있음.

대응:
- 매크로 대신 helper function + 클로저 팩토리 시도
- 그래도 3줄 이하 반복이면 현상 유지 허용

## 6. 완료 정의

- metric/RESULT 인프라가 common.rs로 통합됨
- POLLOUT/pending/EAGAIN/send API, `on_receive` 등록은 각 패턴 파일에서 직접 보임
- 전체 패턴/전체 사이즈 정상 동작
- `core/perf`와 동일한 측정 의미 유지
- `doc/perf/PERF_POLICY.md` recv/callback 모드 제약이 코드에서 강제됨
- 미지원 recv 모드 조합이 fail-fast
- direct unsafe FFI 호출 0건 (Rust binding crate API만 사용)
- Rust 컴파일 경고 0건

---

## 완료 상태

> 완료일: 2026-04-06
> 검증: 빌드 통과 + 스모크 테스트 통과 + 완료 정의 대비 코드 리뷰 통과

단계 0-4 완료. `PHASE_DRAIN` 삭제, `PhaseResult`/`MetricCollector` 추가, recv mode fail-fast 정리가 반영되었고, 현재 `cargo check`는 single/multi 모두 경고 0건으로 통과한다. 최신 스모크 기준 `14 RESULT lines`도 유지된다.
