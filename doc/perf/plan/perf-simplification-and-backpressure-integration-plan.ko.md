# perf 단순화 및 backpressure integration 분리 계획

> 상태: 제안
> 작성일: 2026-04-07
> 적용 범위: `core/perf`, `core/tests/integration`, 향후 bindings perf 정렬 기준

## 0. 사용 방법

이 문서는 **새 컨텍스트에서 단독으로 읽고 바로 작업을 시작할 수 있는 실행 문서**다.

이 문서의 기본 독자는 사용자가 아니라 **감독 Codex** 다.

즉, 새 세션에서 감독 Codex는 아래처럼 시작한다.

- 이 문서만 읽는다.
- 이 문서에 적힌 범위, 순서, 완료 기준만 따른다.
- 다른 perf 계획 문서는 참고하지 않아도 된다.
- 하위 Codex 에이전트에게 작업을 직접 분배한다.
- 에이전트 완료 응답을 감독 Codex가 다시 리뷰한다.
- 미완료가 있으면 감독 Codex가 같은 에이전트 또는 새 에이전트에게 재지시한다.
- 모든 종료 조건이 충족될 때까지 이 루프를 반복한다.
- 종료 조건을 만족한 뒤에만 사용자에게 최종 보고한다.

이 문서를 사용하는 감독 Codex는 아래를 전제로 한다.

- 현재 목표는 `core/perf`를 단순화하고, backpressure 검증을 `core/tests/integration`으로 분리하는 것이다.
- 이번 작업은 **문서 정리로 끝나는 작업이 아니라**, 문서 → 구현 → 테스트 → full success까지 끝내는 작업이다.
- 이번 범위에서 `STREAM`, `echo`, `PAIR` backpressure 테스트는 다루지 않는다.
- backpressure 통합테스트는 **one-way 패턴만** 대상으로 한다.

## 0.0 감독 Codex 운영 계약

감독 Codex는 이 문서를 읽은 뒤 아래 계약으로만 작업한다.

1. 직접 구현 주체가 되지 않는다.
2. 문서의 task 단위를 하위 Codex 에이전트에게 할당한다.
3. 에이전트가 완료를 보고하면 감독 Codex가 직접 결과를 리뷰한다.
4. 리뷰에서 미완료가 있으면 사용자가 아니라 에이전트에게 다시 지시한다.
5. 이 과정을 모든 종료 조건이 충족될 때까지 반복한다.
6. 사용자에게는 중간 진행 상황과 최종 완료 상태만 보고한다.

감독 Codex는 아래를 사용자에게 떠넘기면 안 된다.

- task 분해
- 완료 여부 판단
- 미완료 항목 식별
- 다음 작업 선택

이 문서의 목적은 감독 Codex가 위 네 가지를 **스스로 수행하게 만드는 것**이다.

## 0.1 감독 Codex가 바로 해야 하는 일

이 문서를 받은 감독 Codex는 아래를 **처음부터 끝까지 순서대로** 수행한다.

1. perf 정책 문서 3개를 이 문서와 일치하게 수정
2. `core/perf` single에서 warmup/queue/debug surface 제거
3. `core/perf` multi에서 warmup/queue/debug surface 제거
4. `core/tests/integration`에 backpressure 통합테스트 추가
5. perf 기본 shell script full 실행 성공 확인
6. integration lane 또는 새 integration target 성공 확인
7. 결과를 정리하고 종료

위 항목은 감독 Codex가 직접 구현하지 않고, 하위 Codex 에이전트에게 단계별로 맡기고 완료 여부를 반복 리뷰한다.

중간에 partial 상태로 끝내지 않는다.

중요:

- 진행 보고는 작업 종료가 아니다.
- Task 1, 2, 3만 끝났다고 멈추면 안 된다.
- Task 4, 5가 열려 있으면 같은 실행 안에서 계속 진행해야 한다.

## 0.2 이번 작업의 최종 종료 조건

이번 작업은 아래가 모두 만족될 때만 끝난다.

- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

가 이 문서와 일치한다.

- `core/perf` 기본 실행이 warmup 없이 동작한다.
- 기본 perf 출력 포맷의 RESULT line, markdown table, runner summary에는 아래 컬럼만 남는다.
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99
- `./core/perf/run_benchmarks.sh` full default가 성공한다.
- `./core/perf/run_benchmarks_multi.sh` full default가 성공한다.
- backpressure integration 테스트가 추가되고 성공한다.
- one-way pattern에 대해 pattern별 × transport별 backpressure 검증이 가능하다.

## 0.3 이번 작업에서 금지하는 것

- `STREAM` 공통 client 재설계
- echo backpressure 테스트 추가
- `PAIR` backpressure 테스트 추가
- perf에 새 진단 mode 추가
- queue/debug metric을 기본 perf surface에 남겨두는 것
- warmup을 다른 이름으로 다시 넣는 것

## 0.4 구현 순서 고정

이번 작업은 아래 순서를 바꾸지 않는다.

1. 정책 문서 정리
2. perf single 단순화
3. perf multi 단순화
4. integration 테스트 추가
5. full perf 검증
6. integration 검증

이 순서를 바꾸면 문서와 코드가 다시 어긋나기 쉽다.

## 0.4.1 정책 문서 기준 리뷰 의무

감독 Codex는 Task 2 이후의 모든 구현과 리뷰를 아래 정책 문서 기준으로 수행해야 한다.

- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

즉:

- 정책 문서와 어긋나는 리팩토링은 승인하면 안 된다.
- 구현이 끝난 뒤에도 정책 문서 기준으로 다시 확인해야 한다.
- 계획 문서만 보고 끝내지 말고, 수정된 정책 문서와 실제 코드가 서로 일치하는지 반드시 리뷰해야 한다.

## 0.5 Agent Task Breakdown

이 절은 **감독 Codex가 하위 Codex 에이전트에게 그대로 작업 지시를 내리기 위한 분해 기준**이다.

원칙:

- 한 에이전트에게 문서 전체를 한 번에 맡기지 않는다.
- 아래 task 단위로 잘라서 맡긴다.
- 에이전트가 완료를 보고해도 그대로 믿지 않는다.
- 감독 Codex가 diff, 결과 파일, 테스트 결과를 다시 확인한다.
- 미완료가 있으면 같은 task를 다시 지시한다.
- 사용자는 이 루프에 개입하지 않는다.

## 0.6 감독 Codex 반복 루프

감독 Codex는 아래 루프를 자동으로 반복한다.

1. 현재 task를 선택한다.
2. 해당 task를 하위 Codex 에이전트에게 지시한다.
3. 에이전트 완료 응답을 기다린다.
4. 감독 Codex가 직접 리뷰한다.
5. 미완료가 있으면 같은 task를 다시 지시한다.
6. 완료되면 다음 task로 넘어간다.
7. 모든 task와 종료 조건이 끝나면 사용자에게 최종 보고한다.

각 task는 아래 둘 중 하나가 될 때까지 끝난 것으로 간주하지 않는다.

- 문서에 적힌 완료 조건 충족
- 감독 Codex가 직접 리뷰 후 승인

그리고 전체 작업은 아래 둘이 모두 만족될 때까지 끝난 것으로 간주하지 않는다.

- 모든 task 완료
- 0.2의 최종 종료 조건 충족

## 0.6.1 장기 실행 감시 규칙

감독 Codex는 perf full run, integration lane, 긴 benchmark/ctest/build 같은 장기 실행을 시작한 뒤 아래 규칙으로 감시해야 한다.

- 실행 시작은 완료가 아니다.
- 실행이 살아 있는 동안 task를 열린 상태로 유지한다.
- 60초마다 최소 한 번 현재 상태를 확인한다.
- 확인 방법:
  - 실행 세션 output poll
  - 결과 파일 tail
  - 로그/summary/status 확인
- 아래 중 하나가 될 때까지 멈추지 않는다:
  - 실제 성공 종료
  - 실제 실패 확인
  - 외부 blocker 확인

금지:

- "실행이 시작됐으니 완료"
- "benchmark loop에 들어갔으니 여기서 종료"
- "진행 중이라고 보고했으니 세션 종료"

장기 실행 감시도 감독 Codex의 책임이며, 사용자에게 넘기면 안 된다.

## 0.7 사용자 보고 규칙

감독 Codex는 사용자에게 다음만 보고한다.

- 현재 task
- 현재 상태
- 남은 미완료 항목 수
- 방금 확인한 핵심 결과
- 필요 시 최종 완료 상태

사용자 보고는 진행 상황 공유용이며, 사용자에게 task 분해나 리뷰를 맡기지 않는다.

장기 실행 중에는 보고 후에도 같은 task를 계속 감시한다.

### Task 1. 정책 문서 정리

목표:

- perf 정책 문서 3개를 이 문서와 일치시키기

수정 대상:

- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

반드시 반영할 내용:

- warmup 제거
- `ready -> active`만 사용
- perf는 throughput/bandwidth/latency만 측정
- backpressure 검증은 `core/tests/integration`
- one-way backpressure integration 범위와 제외 범위(`STREAM`, `echo`, `PAIR`)

금지:

- 정책 문서 외 다른 코드 수정
- 문서 안에 새 perf mode 추가

완료 조건:

- 세 문서가 이 문서와 용어/범위/단계 기준으로 일치

검증:

- 수정된 세 문서 diff 확인
- warmup, queue metric, integration 범위 문구 확인

리뷰 포인트:

- `e2e` 표현이 남아 있지 않은가
- `echo`, `PAIR`, `STREAM` 제외가 일관적인가
- one-way only 범위가 흔들리지 않는가

재지시 기준:

- 문서 간 용어가 다름
- warmup 관련 기존 문장이 남아 있음
- integration 범위가 불명확함

### Task 2. single perf 단순화

목표:

- single perf에서 warmup/queue/debug surface 제거

수정 대상:

- `core/perf/single/*`
- `core/perf/single/run_comparison.py`
- `core/perf/run_benchmarks.sh`

반드시 반영할 내용:

- warmup phase 제거
- 기본 출력 포맷의 컬럼을 아래 5개로 고정
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

금지:

- single 결과 의미 변경
- size별 독립 실행 구조 변경
- backpressure integration 테스트를 perf 쪽으로 다시 끌고 오는 것

완료 조건:

- single 기본 실행이 warmup 없이 동작
- 기본 출력 포맷의 컬럼이 아래 5개만 유지됨
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

검증:

- `./core/perf/run_benchmarks.sh`

리뷰 포인트:

- RESULT line, stdout 테이블, 결과 파일 테이블에 아래 5개 컬럼만 남았는가
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

재지시 기준:

- warmup 관련 코드/옵션이 남아 있음
- single full default 실패
- 결과 포맷이 깨짐

### Task 3. multi perf 단순화

목표:

- multi perf에서 warmup/queue/debug surface 제거

수정 대상:

- `core/perf/multi/common/*`
- `core/perf/multi/src/*`
- `core/perf/run_comparison.py`
- `core/perf/run_benchmarks_multi.sh`

반드시 반영할 내용:

- warmup phase 제거
- `START` 이후 active만 사용
- 기본 출력 포맷의 컬럼을 아래 5개로 고정
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

금지:

- `STREAM` 공통 client 재설계
- 새 진단 mode 추가
- queue/backpressure probe를 기본 perf에 남기는 것

완료 조건:

- multi 기본 실행이 warmup 없이 동작
- 기본 출력 포맷의 컬럼이 아래 5개만 유지됨
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

검증:

- `./core/perf/run_benchmarks_multi.sh`

리뷰 포인트:

- `START`/ready 이후 active만 쓰는가
- `STREAM` 관련 범위 확장이 없었는가
- RESULT line, stdout 테이블, 결과 파일 테이블에 아래 5개 컬럼만 남았는가
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

재지시 기준:

- multi full default 실패
- queue/debug metric이 남음
- `STREAM` 범위 침범

### Task 4. backpressure integration 테스트 추가

목표:

- one-way backpressure integration 테스트를 pattern별 × transport별로 추가

수정 대상:

- `core/tests/integration/*`

반드시 반영할 내용:

- 대상 pattern:
  - single `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
  - multi `PUBSUB`, `SPOT`
- 대상 transport:
  - `tcp`, `tls`, `ws`, `wss`
- HWM bucket:
  - `1`, `10`, `100`, `1000`, `10000`
- 검증 시나리오:
  - 작은 `sndhwm` 에서 pressure 진입
  - pressure 후 재개
  - `rcvhwm` 변화 반영
  - `sndhwm` 증가에 따른 진행 가능량 변화
  - `SPOT` forwarding pressure

금지:

- echo backpressure 테스트 추가
- `PAIR` 추가
- `STREAM` 추가
- sleep/retry 기반 동기화

완료 조건:

- integration 테스트가 추가되고 빌드/실행 가능
- pattern별 × transport별 범위를 실제로 커버

검증:

- `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 필요한 경우 새 integration target 직접 실행

리뷰 포인트:

- one-way only 범위가 지켜졌는가
- transport 누락이 없는가
- `sndhwm`/`rcvhwm` 축이 모두 들어갔는가

재지시 기준:

- pattern 또는 transport 누락
- HWM bucket 누락
- sleep/retry 사용

### Task 5. full 검증 및 종료 정리

목표:

- 문서, 구현, 테스트가 모두 맞는지 최종 확인

수정 대상:

- 필요 시 소규모 follow-up 수정

반드시 확인할 것:

- `./core/perf/run_benchmarks.sh` full default success
- `./core/perf/run_benchmarks_multi.sh` full default success
- integration lane success
- 정책 문서와 코드가 일치

완료 조건:

- 최종 종료 조건(0.2)이 모두 충족

검증:

- full 결과 파일 직접 확인
- integration 결과 직접 확인

리뷰 포인트:

- partial/skip/fail이 남지 않았는가
- 문서와 코드가 서로 어긋나지 않는가

재지시 기준:

- full perf 실패
- integration 실패
- 문서/코드 불일치

## 1. 배경

현재 `perf`는 원래 목적보다 더 많은 책임을 가지고 있다.

- throughput / bandwidth / latency 측정
- ready / warmup / active phase 제어
- queue / backpressure 진단
- transport별 비교 해석
- 결과 formatting / 비교 스크립트 orchestration

이 구조는 다음 문제를 만들었다.

- `core` 성능 문제보다 `perf` 자체의 버그와 phase 경계 문제를 더 오래 수정하게 된다.
- warmup/active 경계 버그가 생기면 실제 라이브러리 성능과 무관한 실패가 생긴다.
- queue/backpressure 진단 로직이 기본 perf surface를 복잡하게 만든다.
- bindings가 `core/perf`를 기준기로 따라갈 때 perf 자체 복잡성까지 함께 가져가게 된다.

이번 계획의 목적은 `perf`를 다시 얇은 기준기로 줄이고, backpressure/queue/phase 해석은 별도 integration 테스트로 분리하는 것이다.

## 2. 목표

### 2.1 기본 목표

- `perf`는 **성능 수치만 측정하는 얇은 도구**로 단순화한다.
- `backpressure`, `queue`, `NODROP`, `small HWM`, `fanout pressure` 검증은 **별도 integration 테스트**로 분리한다.
- `warmup` 단계를 제거한다.
- 기본 perf 결과는 `throughput / bandwidth / latency`만 출력한다.
- shell script 기본 실행(`run_benchmarks.sh`, `run_benchmarks_multi.sh`)이 설명하기 쉬운 단순 surface가 되게 한다.

### 2.2 유지해야 하는 것

- `RESULT` 포맷의 기본 축은 유지한다.
- throughput / bandwidth / latency 의미는 유지한다.
- size별 독립 실행 구조는 유지한다.
- shell script가 pattern / transport / size 순회를 담당하는 구조는 유지한다.
- perf는 여전히 `core` 및 bindings 비교 기준으로 쓸 수 있어야 한다.

## 3. 비목표

- 이번 계획은 `core` transport 구현 자체를 바꾸는 작업이 아니다.
- perf에서 backpressure를 더 정교하게 측정하는 새 진단 surface를 기본값으로 넣지 않는다.
- perf 안에 mode를 더 늘려서 복잡성을 유지하는 방향으로 가지 않는다.
- queue/backpressure probe를 기본 perf 실행에 계속 남기는 방향으로 가지 않는다.
- `STREAM` 공통 client를 이번 작업에서 함께 재설계하지 않는다.

## 3.1 이번 범위에서 `STREAM` 제외

이번 계획에서는 `STREAM` 을 기본 구현 대상에서 제외한다.

이유:

- `STREAM` 은 공통 client surface를 여러 프로그램이 공유할 가능성이 크다.
- 공통 stream client를 수정하면 `MULTI_STREAM` 하나만이 아니라 관련 사용처를 함께 열어야 할 수 있다.
- 이 경우 perf 단순화 작업이 `STREAM` 공통 client 정리 작업으로 커지면서 범위가 급격히 커진다.
- 이번 계획의 핵심은 `perf` 를 얇게 만들고 backpressure 검증을 integration으로 분리하는 것이다.
- `STREAM` 공통 client 재설계는 별도 독립 작업으로 다루는 편이 POSD 관점에서 더 낫다.

따라서 이번 계획의 기본 범위는 아래로 제한한다.

- single perf 공통부
- single one-way 패턴
- multi raw one-way 패턴
- `PUBSUB`
- `SPOT`

이번 계획에서 `STREAM` 은 다음처럼 처리한다.

- 현행 구조 유지
- 정책 문서 정리 시 “이번 단순화 범위 제외”로 명시
- 후속 별도 계획 문서에서 공통 client 영향 범위를 먼저 정리한 뒤 작업

## 4. 핵심 원칙

### 4.1 perf는 수치만 잰다

기본 perf는 아래만 책임진다.

- 단일 케이스 실행
- throughput 계산
- bandwidth 계산
- latency 계산
- 최소한의 결과 출력

기본 perf는 아래를 책임지지 않는다.

- queue depth 진단
- backpressure 여부 판정
- fanout pipe pressure 추적
- phase debug
- transport별 이상 현상 원인 분석

### 4.2 backpressure는 integration으로 검증한다

backpressure는 성능 수치와 다른 질문이다.

- 작은 HWM에서 실제로 block/drop/backpressure가 걸리는가
- `NODROP`가 의도대로 동작하는가
- fanout 상황에서 subscriber pressure가 sender에 전파되는가
- `SPOT` internal forwarding이 HWM budget과 맞게 동작하는가

이 질문들은 `perf`보다 integration 테스트가 더 적합하다.

### 4.3 warmup은 제거한다

현재 문제의 상당수는 warmup/active 경계를 구현하다가 생긴다.

- warmup 메시지를 active가 먼저 또는 나중에 읽는 문제
- active 메시지를 warmup recv loop가 먼저 consume 하는 문제
- phase control과 transport 지연이 섞이는 문제

이번 계획에서는 기본 perf에서 warmup을 제거한다.

새 기본 측정 모델:

- `ready`
- `active`

즉, 연결 준비가 끝나면 바로 active 측정으로 들어간다.

이로 인해 측정 의미는 다음으로 재정의된다.

- “안정화 후 steady-state만 측정”이 아니라
- “기본 실행 조건에서 active duration 동안 관측된 성능”을 측정한다.

이는 `single/multi`, `core/bindings`, `tcp/tls/ws/wss` 모두에 동일하게 적용되므로 비교 기준으로는 유지 가능하다.

## 5. 최종 구조

## 5.1 perf

### single / multi 공통

- `ready -> active`만 사용
- sampler thread 제거
- 결과는 기본적으로 아래만 남긴다.
  - throughput
  - bandwidth
  - latency mean
  - latency p95
  - latency p99

### 실행 계약

- shell script는 size별로 바이너리를 다시 실행한다.
- 바이너리는 한 케이스만 실행한다.
- active duration 동안만 측정한다.
- warmup phase는 없다.

## 5.2 integration

새 integration 테스트는 “성능 수치”가 아니라 “동작 계약”을 본다.

테스트 그룹:

1. small HWM backpressure
2. large HWM queue growth
3. NODROP / no-loss contract
4. transport별 backpressure propagation
5. `SPOT` internal forwarding / fanout pressure

## 5.3 integration 원칙

이번 계획의 backpressure 검증은 `core/tests/integration`으로 설계한다.

이유:

- 목적은 “실제 연결 관계에서 HWM/backpressure가 동작하는가”를 보는 것이다.
- 이 질문에는 별도 프로세스 분리보다 **실제 연결 topology 유지**가 더 중요하다.
- 같은 프로세스 안에서 server thread / client thread를 분리해도 public API와 연결 관계가 동일하면 계약 검증에는 충분하다.
- integration 테스트가 제어, timeout, fail-fast 구현에 더 유리하다.

따라서 기본 원칙은 다음과 같다.

- raw pattern HWM/backpressure 검증: `core/tests/integration`
- `SPOT` HWM/backpressure 검증: `core/tests/integration`
- 같은 프로세스 안에서 thread만 분리해도 연결 관계가 같으면 충분하다

## 6. 상세 작업

이 절은 실제 구현 순서다. 새 컨텍스트에서 작업을 시작할 때는 이 절부터 그대로 따라가면 된다.

## 6.1 1단계: 정책 문서 정리

대상:

- `doc/perf/PERF_POLICY.md`
- `doc/perf/PERF_SINGLE_TEST_POLICY.md`
- `doc/perf/PERF_MULTI_TEST_POLICY.md`

변경 내용:

- 기본 perf 목적을 “수치 측정 only”로 축소
- warmup 단계 제거 명시
- queue / backpressure metric 기본 surface 제거 명시
- backpressure 검증은 `core/tests/integration`으로 이동한다고 명시
- shell script 기본 실행은 perf 기본 surface만 사용한다고 명시

문서 문장에 반드시 들어가야 하는 내용:

- perf는 queue/backpressure 원인 분석 도구가 아니다.
- backpressure 관련 계약은 별도 integration 테스트가 책임진다.
- perf는 `ready -> active`만 사용한다.
- shell script 기본 결과는 성능 수치만 포함한다.

1단계 산출물:

- 정책 문서 3개 수정 완료
- 이 문서와 정책 문서 간 용어 차이 제거

## 6.2 2단계: perf single 단순화

대상:

- `core/perf/single/*`
- `core/perf/single/run_comparison.py`
- `core/perf/run_benchmarks.sh`

작업:

- warmup 관련 옵션/phase 제거
- callback/recv 측정에서 warmup 분기 제거
- active duration만으로 throughput/latency 계산 유지
- 결과 출력 포맷의 컬럼을 아래 5개로 고정
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

중요한 제약:

- callback/recv 측정 의미는 유지
- `RESULT`의 기본 수치 항목은 유지
- size별 독립 실행은 유지

2단계 산출물:

- single warmup 제거
- single queue/debug metric 제거
- single 기본 shell script가 최소 smoke 성공

## 6.3 3단계: perf multi 단순화

대상:

- `core/perf/multi/common/*`
- `core/perf/multi/src/*`
- `core/perf/run_comparison.py`
- `core/perf/run_benchmarks_multi.sh`

작업:

- warmup phase 제거
- `START` 이후 바로 active 시작
- default shell script 결과 포맷의 컬럼을 아래 5개로 고정
  - throughput
  - bandwidth
  - latency
  - latency_p95
  - latency_p99

주의:

- `SPOT` / `STREAM` callback 예외는 유지할 수 있다.
- 하지만 callback과 recv 모두 warmup 없이 같은 `ready -> active` 계약을 따른다.
- multi raw pattern과 SPOT barrier는 “ready”까지만 책임지고, warmup은 더 이상 없다.
- 단, 이번 단계의 기본 구현 대상에서는 `STREAM` 공통 client 재설계를 제외한다.
- `STREAM` 관련 변경이 공통 client surface까지 번질 경우, 해당 변경은 후속 별도 작업으로 분리한다.

3단계 산출물:

- multi warmup 제거
- multi queue/debug metric 제거
- multi 기본 shell script가 최소 smoke 성공

## 6.4 4단계: backpressure integration 추가

대상 디렉터리:

- `core/tests/integration/`

새 테스트 분류 예시:

- `test_backpressure_pubsub_*.cpp`
- `test_backpressure_spot_*.cpp`
- `test_backpressure_oneway_*.cpp`

4단계 산출물:

- `core/tests/integration`에 새 backpressure 테스트 추가
- single one-way / multi one-way 대상이 모두 포함됨
- pattern별 × transport별 테스트가 실제로 실행 가능함

테스트 원칙:

- 성능 수치 비교가 아니라 계약 검증
- retry 없음
- sleep 기반 동기화 없음
- bounded wait + 즉시 fail
- 같은 프로세스 안에서 thread 분리로 구성 가능
- public API만 사용
- pattern별 실제 연결 topology는 유지

### 6.4.1 공통 테스트 하네스

모든 backpressure 테스트는 아래 공통 구조를 사용한다.

- main thread
  - endpoint 생성
  - start/stop barrier 관리
  - hard timeout 관리
  - 최종 assertion 수행
- server thread
  - bind / accept / publish / echo / relay 역할 수행
- client thread(들)
  - connect / subscribe / recv / callback 역할 수행

필수 규칙:

- 프로세스는 하나여도 된다.
- thread는 역할별로 분리한다.
- 실제 연결 방식은 production/perf와 동일한 public API로 재현한다.
- sleep으로 “대충 안정화” 하지 않는다.
- readiness는 monitor event, connect 완료, bounded wait 등 계약에 맞는 방법으로만 확인한다.
- 이번 backpressure integration 테스트 대상은 one-way 패턴만이다.

### 6.4.2 공통 관찰 항목

모든 테스트는 아래 중 필요한 항목을 직접 assertion 한다.

- send가 bounded 시간 안에 `EAGAIN` 또는 backpressure 상태로 진입하는지
- backpressure 후 writable/ready 상태에서 전송이 재개되는지
- recv가 bounded 시간 안에 메시지를 계속 소비하는지
- `sndhwm` 이 작을 때 sender 진행률이 제한되는지
- `sndhwm` 이 클 때 더 오래 진행 가능한지
- `rcvhwm` 이 작아지면 sender pressure가 더 빨리 오는지
- `rcvhwm` 이 커지면 pressure 시점이 늦어지는지
- `NODROP` 계약일 때 메시지 유실이 없는지
- transport별로 같은 backpressure contract가 유지되는지

주의:

- queue depth 숫자 자체를 항상 읽어야 하는 것은 아니다.
- 더 중요한 것은 “보내는 쪽이 실제로 막히는가”, “받는 쪽이 실제로 메시지를 유지하는가”다.
- queue metric이 필요하면 테스트 내부 보조 계측으로만 사용하고, 기본 perf surface와 섞지 않는다.

필수 시나리오:

### A. 작은 `sndhwm` 에서 pressure 진입

- 구성:
  - pattern별 sender role thread
  - pattern별 receiver role thread(들)
  - sender는 `DONTWAIT` send를 반복한다
  - `sndhwm` 을 `1`, `10`, `100` 으로 줄인다
- assertion:
  - 작은 `sndhwm` 에서 bounded 시간 내 pressure 진입
  - transport별로 “영원히 안 막히는” 경우가 없어야 함

### B. pressure 후 재개

- 구성:
  - receiver는 초기에는 느리게 소비하거나 소비를 멈춘다
  - 이후 bounded 시점에 다시 소비를 재개한다
- assertion:
  - pressure 후 writable/ready가 다시 오면 send가 재개됨
  - `NODROP`이면 재개 이후 손실 없이 drain 가능

### C. `rcvhwm` 변화가 pressure에 반영되는지

- 구성:
  - sender/receiver topology는 pattern 그대로 유지
  - `rcvhwm` 을 `1`, `10`, `100`, `1000`, `10000` 으로 바꾼다
- assertion:
  - 작은 `rcvhwm` 에서 sender pressure가 더 빨리 옴
  - 큰 `rcvhwm` 에서 pressure 시점이 늦어짐
  - transport별로 같은 contract가 유지됨

### D. `sndhwm` 증가에 따른 진행 가능량 변화

- 구성:
  - sender/receiver topology는 pattern 그대로 유지
  - `sndhwm` 을 `1`, `10`, `100`, `1000`, `10000` 으로 바꾼다
- assertion:
  - `sndhwm` 이 커질수록 sender가 더 오래 진행 가능
  - transport 변경이 계약을 깨지 않아야 함

### E. `SPOT` forwarding pressure

- local subscriber pressure가 forwarding path에 전파되는지
- `spotnode` data-plane HWM budget이 의도대로 동작하는지
- 구성:
  - `spot pub` 1개
  - `spotnode` 1개
  - `spot sub` N개
  - 일부 `sub` 는 의도적으로 느리게 소비
- assertion:
  - sender pressure가 local subscriber 상태에 따라 바뀜
  - `sndhwm`/`rcvhwm` 변화가 forwarding path에서 실제로 반영됨
  - transport별로 같은 contract가 유지됨

## 6.4.3 패턴 × transport × HWM 매트릭스

이번 계획에서는 **전 패턴 × 전 transport 검증을 필수**로 둔다.

이유:

- backpressure/HWM 문제는 transport별로 다르게 보일 수 있다.
- 과거에는 pattern별 개별 버그도 실제로 존재했다.
- 따라서 “대표 패턴만 본다” 또는 “대표 transport만 본다”는 식의 축소는 허용하지 않는다.

기본 원칙:

- 모든 대상 pattern을 검증한다.
- 각 pattern은 `tcp`, `tls`, `ws`, `wss` 모두를 검증한다.
- 대신 HWM 값은 bucket으로 줄여 현실적인 test set으로 운영한다.

누락 방지를 위해 아래 매트릭스를 기준으로 테스트를 설계한다.

### 대상 pattern

single one-way 대상:

- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`

multi one-way 대상:

- `PUBSUB`
- `SPOT`

### transport

- `tcp`
- `tls`
- `ws`
- `wss`

### HWM bucket

- very small: `1`, `10`
- small: `100`
- default: `1000`
- large: `10000`

### pattern coverage 규칙

이번 계획에서 생략 없이 검증해야 하는 pattern 범위:

single one-way:

- `DEALER_DEALER`
- `DEALER_ROUTER`
- `ROUTER_ROUTER`

multi one-way:

- `PUBSUB`
- `SPOT`

`STREAM` 은 이번 계획 범위에서 제외하지만, 제외 사실과 이유를 문서에 명시한 상태로 유지한다.
echo 패턴과 `PAIR` 는 이번 backpressure integration 범위에서 제외한다.

### 테스트 파일 경계 규칙

single과 multi는 같은 pattern 이름을 쓰더라도 테스트 파일을 분리한다.

- single backpressure integration:
  - `test_backpressure_single_<pattern>_*.cpp`
- multi backpressure integration:
  - `test_backpressure_multi_<pattern>_*.cpp`

예:

- `test_backpressure_single_dealer_dealer_*.cpp`
- `test_backpressure_multi_pubsub_*.cpp`
- `test_backpressure_multi_spot_*.cpp`

### transport coverage 규칙

각 pattern은 아래 transport를 모두 검증해야 한다.

- `tcp`
- `tls`
- `ws`
- `wss`

특정 transport를 생략할 수 있는 경우는 없다.

### HWM coverage 규칙

HWM은 모든 정수를 다 훑지 않고 bucket으로 운영한다.

기본 bucket:

- `1`
- `10`
- `100`
- `1000`
- `10000`

필요 시 pattern 특성상 추가 bucket을 둘 수 있지만, 기본 bucket은 위 다섯 값을 유지한다.

## 6.4.4 테스트 작성 체크리스트

각 테스트는 아래 체크리스트를 모두 채워야 한다.

1. pattern은 무엇인가
2. transport는 무엇인가
3. topology는 무엇인가
4. sender와 receiver를 어떤 thread로 분리했는가
5. 어떤 HWM 값에서 어떤 현상을 기대하는가
6. 기대 현상이 timeout 안에 직접 assertion 되는가
7. sleep 기반 동기화가 없는가
8. retry loop가 없는가
9. public API만 사용하는가
10. 실패 시 어떤 계약이 깨졌는지 바로 설명 가능한가
11. 전 transport(`tcp/tls/ws/wss`) 중 어떤 조합을 커버하는가
12. 같은 pattern의 다른 transport를 누락하지 않았는가
13. 같은 transport의 다른 pattern을 누락하지 않았는가
14. `sndhwm` 변화 테스트가 포함됐는가
15. `rcvhwm` 변화 테스트가 포함됐는가

## 6.5 5단계: bindings 기준 재정렬

perf가 단순화되면 bindings도 같은 계약으로 따라간다.

정렬 대상:

- warmup 제거
- 기본 결과 surface 단순화
- backpressure/queue debug 제거
- 별도 integration 또는 binding-specific test로 이동

이 단계의 목표는 bindings perf가 다시 `core/perf`와 같은 질문만 하도록 만드는 것이다.

주의:

- 이번 작업의 종료 조건에는 bindings 구현 변경이 포함되지 않는다.
- 이번 단계는 **후속 작업 준비용 기준 정리**로만 사용한다.

## 7. 구현 세부 원칙

### 7.1 phase

기존:

- ready
- warmup
- active

신규:

- ready
- active

phase 전환 규칙:

- ready 완료 후 즉시 active 시작
- active 시작 시 count/latency state만 reset
- warmup drain, warmup receive window, warmup-specific header filtering 모두 제거

### 7.2 결과

기본 RESULT에 유지:

- throughput
- bandwidth
- latency
- latency_p95
- latency_p99

기본 RESULT에서 제거:

- queue pending
- client queue pending
- internal pressure probe
- debug-only phase metric

### 7.3 shell script

기본 shell script는 복잡한 진단 기능을 품지 않는다.

- `run_benchmarks.sh`
- `run_benchmarks_multi.sh`

기본 역할:

- build reuse
- pattern/transport/size 순회
- 결과 파일 저장
- complete / partial 판정

하지 않을 일:

- backpressure 해석
- queue metric 보강
- debug probe orchestration

## 8. 검증 계획

## 8.1 perf 검증

기본 검증:

- `./core/perf/run_benchmarks.sh`
- `./core/perf/run_benchmarks_multi.sh`

완료 기준:

- 기본 shell script가 full default로 complete
- 결과 파일에 불필요한 debug metric이 없음
- single/multi 모두 기본 surface가 간단하게 읽힘

반드시 실행할 명령:

- `./core/perf/run_benchmarks.sh`
- `./core/perf/run_benchmarks_multi.sh`

## 8.2 integration 검증

기본 검증:

- `ctest --test-dir core/build --output-on-failure -L integration -j1`
- 필요한 경우 새 test target 직접 실행

완료 기준:

- backpressure/HWM 관련 계약이 perf가 아니라 integration에서 재현/검증됨
- perf 제거 후에도 queue/backpressure 관련 회귀를 놓치지 않음

반드시 실행할 명령:

- `ctest --test-dir core/build --output-on-failure -L integration -j1`

필요하면 새 integration target도 직접 실행해서 패턴별/transport별 범위를 확인한다.

## 9. 단계별 완료 정의

### 단계 A: 문서 정리 완료

- 정책 문서 3개가 `ready -> active`와 `integration 분리`를 명시

### 단계 B: single perf 단순화 완료

- warmup/queue probe 제거
- 기본 shell script full success

### 단계 C: multi perf 단순화 완료

- warmup/queue probe 제거
- 기본 shell script full success

### 단계 D: integration 분리 완료

- PUBSUB/SPOT/backpressure 관련 필수 integration 테스트가 추가되고 lane에서 성공

### 단계 E: bindings 기준 정렬 시작 가능

- `core/perf`가 얇은 기준기로 다시 설명 가능

## 10. 리스크와 대응

### 리스크 1: warmup 제거로 초기 흔들림이 더 많이 보일 수 있음

대응:

- 이것은 의도된 단순화 결과로 받아들인다.
- 대신 모든 대상에 동일 규칙을 적용해 비교 가능성은 유지한다.

### 리스크 2: 기존 baseline과 직접 비교가 어려워질 수 있음

대응:

- baseline을 새 계약 기준으로 재생성한다.
- old baseline은 archived reference로 남긴다.

### 리스크 3: 일부 backpressure 회귀를 perf에서 바로 못 볼 수 있음

대응:

- e2e coverage를 먼저 추가하고 perf를 단순화한다.
- perf 단순화와 e2e 추가는 분리하지 않고 같은 작업 묶음으로 진행한다.

## 11. 최종 판단 기준

이 계획이 끝났다고 볼 조건:

- `perf` 기본 shell script가 설명하기 쉬운 얇은 구조가 된다.
- warmup 관련 phase 버그가 기본 perf surface에서 사라진다.
- queue/backpressure 해석 문제를 perf에서 더 이상 떠안지 않는다.
- backpressure 계약은 integration 테스트가 책임진다.
- `core/perf`를 다시 bindings 성능 비교 기준기로 사용할 수 있다.

## 12. 권장 실행 순서

1. 정책 문서 업데이트
2. backpressure integration 최소 세트 추가
3. single perf warmup 제거
4. multi perf warmup 제거
5. queue/debug metric 제거
6. shell script 기본 결과 surface 정리
7. full single/multi success 확인
8. 새 baseline 생성
9. `STREAM` 후속 별도 계획 수립
10. bindings perf 정렬 작업 시작

## 13. 새 컨텍스트용 한 줄 요약

새 컨텍스트에서 이 문서 하나만 주고 시작할 때는 아래처럼 이해하면 된다.

- perf는 warmup 없이 `ready -> active`만 사용하도록 단순화한다.
- perf 기본 결과는 throughput/bandwidth/latency만 남긴다.
- backpressure는 `core/tests/integration`으로 분리한다.
- backpressure integration 테스트 대상은 one-way only다.
- 대상 pattern은 single `DEALER_DEALER/DEALER_ROUTER/ROUTER_ROUTER`, multi `PUBSUB/SPOT` 이다.
- 각 pattern은 `tcp/tls/ws/wss` 모두 검증한다.
- HWM bucket은 `1,10,100,1000,10000` 이다.
- full perf 성공과 integration 성공까지 끝내야 작업이 완료된다.
