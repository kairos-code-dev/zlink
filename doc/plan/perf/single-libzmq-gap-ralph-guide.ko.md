# `single-libzmq` 성능 수렴 Ralph 실행 가이드

## 1. 목적

이 가이드는
[single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
를 지속 로그로 유지하면서,
[`core/bench/with_zmq/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq)
를 기준 검증 surface로 사용해
`zlink`의 상대 성능을 `libzmq`와 비슷한 수준까지 끌어올리는 반복 작업을
끝까지 수행하기 위한 유일한 실행 문서다.

이 문서는 계획서와 실행서를 분리하지 않는다.
현재 iteration의 우선순위, 검증 규칙, 종료 조건, 로그 유지 규칙은 모두
이 문서 하나에 고정한다.

## 2. 권한과 로그

- 이 문서가 유일한 authority다.
- [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  는 authority가 아니라 지속 로그다.
- [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  는 hot-path 계약 문서다.
- 각 iteration은
  [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  와
  [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  를 둘 다 다시 읽는 것으로 시작한다.
- 둘 중 하나라도 현재 코드/해석/우선순위와 어긋나면 즉시 갱신한다.
- 실제 변경이 없더라도, iteration 결과가 두 문서의 현재 내용과 일치하는지
  확인하지 않으면 다음 iteration으로 넘어가면 안 된다.
- 별도 main/master/gap/residual/spec 문서는 추가로 만들지 않는다.
- 이 루프의 기본 동작은 `--max-iterations 0`, 즉 목표 완료까지 무한 반복이다.
- 반복 횟수를 제한하고 싶을 때만 명시적으로 `--max-iterations <N>`을 넘긴다.

## 3. 범위

### 3.1 포함

- `core/` 성능 개선 코드
- `core/tests/` 회귀/계약 테스트
- `doc/plan/perf/` 로그와 실행 가이드
- 필요 시 `doc/internal/hot-path.ko.md`

### 3.2 제외

- `core/perf/`와 `core/bench/`를 성능 숫자 맞추기용 우회 수단으로 수정하는 것
- bench helper 변경으로 `core` 병목을 가리는 것
- thread-safe 계약을 약화하는 최적화
- 새로운 보조 계획 문서 생성

bench/perf 코드는 측정 surface다. 수정은 아래 둘 중 하나일 때만 허용한다.

1. 측정 surface 자체가 잘못돼 동일 비교가 깨진 것이 증명될 때
2. 사용자가 bench/perf 코드 변경을 명시적으로 요구했을 때

## 4. 고정 입력

### 4.1 빌드 디렉터리

- 오직 [`core/build/`](/home/hep7/project/kairos/zlink/core/build) 만 사용한다.

### 4.2 주 로그 파일

- [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
- loop runtime 로그는 [`doc/plan/perf/logs/`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs)
  아래에 쌓는다.

### 4.3 주 검증 surface

- single primary:
  [`core/bench/with_zmq/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/run_comparison.py)
- multi secondary:
  [`core/bench/with_zmq/multi/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/multi/run_comparison.py)

상위 shell runner는 최종 aggregate artifact가 필요할 때만 사용한다.
그 경우에도 반드시 `--reuse-build`를 붙인다.

## 5. 성능 목표

운영 목표는 단순하다.

- `throughput 기준으로 zlink가 libzmq와 비슷한 성능이 나올 때까지 반복한다.`

실제 종료 판정은 흔들리지 않도록 `64B` `tcp/inproc` 기준 상대 throughput gap으로
정량화한다.

### 5.1 primary stop condition

아래 single 패턴이 모두 `-10%` 이상이면 기본적으로 `비슷한 성능`으로 본다.

- `PAIR`
- `DEALER_DEALER`
- `DEALER_ROUTER`
- `PUBSUB`
- `ROUTER_ROUTER`

위 조건은 `tcp`, `inproc` 각각에 대해 모두 만족해야 한다.

여기서 `-10% 이상`은 예를 들어 `-9.9%`, `-3%`, `+2%`를 모두 포함한다.

### 5.2 stretch goal

가능하면 아래 둘은 `-5%` 이내까지 더 좁힌다.

- `PAIR`
- `DEALER_DEALER`

stretch goal은 종료 필수 조건은 아니다.

### 5.3 secondary guardrail

- multi `dealer_dealer`, `dealer_router`, `router_router`, `pubsub`의
  `tcp 64B` gap이 모두 `-15%` 이상이어야 한다.
- 개선 중인 패턴이 아닌 다른 패턴에서 기존 best 대비 `5%` 이상 퇴행하면
  종료로 처리하지 않는다.

### 5.4 raw/public 분리 guardrail

send-path를 건드린 iteration 뒤에는 반드시 `PAIR`, `DEALER_DEALER`의
raw/public 분리를 다시 찍는다.

- `zlink raw - zlink public`이 다시 커지면 public surface penalty가 재도입된 것이다.
- 이 경우 gap 해석을 다시 써야 하므로 로그 업데이트 없이 다음 단계로 넘어가면 안 된다.

## 6. 반복 루프

모든 iteration은 아래 순서로 진행한다.

1. 이 가이드 전체를 읽는다.
2. [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
   와
   [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
   최신 상태를 둘 다 읽는다.
3. 두 문서를 기준으로 현재 top hypothesis 하나만 고른다.
4. `core/`와 `core/tests/`를 우선 수정한다.
5. [`core/build/`](/home/hep7/project/kairos/zlink/core/build)로 빌드한다.
6. 영향 패턴의 targeted single 벤치를 먼저 돌린다.
7. 의미 있는 개선이 보이면 raw/public 분리를 다시 확인한다.
8. 개선이 유지될 때만 broader single, 필요한 multi smoke를 수행한다.
9. 결과 파일 경로, 숫자, 해석, 배제한 가설을
   [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
   에 기록한다.
10. 현재 iteration 결과가
    [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
    와도 일치하도록 계약/주의점/우선순위/배제 후보를 반영하거나,
    변경이 없음을 확인한다.
11. 아직 stop condition을 못 만족하면 다음 미해결 가설로 반복한다.

### 6.1 단계별 commit / push

- 유지하기로 결정한 변경 묶음 하나를 한 단계로 본다.
- 각 단계는 아래를 모두 만족한 뒤 바로 commit 하고 push 한다.
  - 필요한 코드/테스트/문서 갱신 완료
  - targeted bench와 필요한 smoke 검증 완료
  - [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
    와
    [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
    갱신 완료
- 한 commit 에 여러 단계 변경을 섞지 않는다.
- 실험했다가 버린 변경은 commit 하지 않는다.
- push 가 끝난 뒤 commit hash 와 검증 결과 파일 경로를
  [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  에 남긴다.
- push 없이 다음 단계로 넘어가면 안 된다.

## 7. 작업 순서 우선순위

현재 우선순위는 아래 순서를 유지한다.

1. send-side lifecycle / backpressure retry cost
2. send-side `pipe` publication / ordering 경로의 lock 안 work 축소
3. `PUBSUB` publish / distribution path
4. `ROUTER_ROUTER` routed path
5. 남아 있는 recv-side routed / strip / multipart export 경로

naive lock 제거는 현재 배제된 후보로 유지한다.

## 8. 금지 규칙

- `_out_sync` 전체 no-op 같은 naive lock 제거를 다시 넣지 않는다.
- `pipe` reentrant 성질을 깨는 non-reentrant mutex 실험을 기본 후보로 올리지 않는다.
- thread-safe contract를 약화하거나 우회해서 성능을 맞추지 않는다.
- bench 수치를 좋게 보이게 하려고 benchmark API surface를 비대칭으로 바꾸지 않는다.
- 테스트 완화, sleep 추가, retry loop 추가로 문제를 숨기지 않는다.

## 9. 표준 명령

### 9.0 loop wrapper smoke

wrapper나 guide를 수정한 뒤에는 아래 순서로 최소 스모크를 한다.

```bash
bash -n doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh \
  core/tools/ralphloop/run_codex_execution_guide_loop.sh \
  core/tools/ralphloop/run_execution_gate_loop.sh
```

```bash
./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh --help
```

```bash
./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh --init-only
```

마지막 명령은 session/log 디렉터리 초기화만 확인하는 스모크다.
이 경우 exit code `0`이 정상이다.

무한 반복 기본 동작으로 실제 루프를 시작하려면:

```bash
./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh
```

반복 횟수를 제한하고 싶을 때만:

```bash
./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh --max-iterations 5
```

### 9.1 빌드

```bash
cmake --build core/build -j$(nproc)
```

### 9.2 targeted single public

```bash
python3 core/bench/with_zmq/single/run_comparison.py \
  --pattern PAIR \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

패턴 자리는 `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`,
`ROUTER_ROUTER` 중 하나로 바꾼다.

### 9.3 raw/public 분리

```bash
PERF_SINGLE_ZLINK_RAW_MSG_API=1 \
python3 core/bench/with_zmq/single/run_comparison.py \
  --pattern PAIR \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

`PAIR`, `DEALER_DEALER`는 send-path 변경 뒤 반드시 다시 찍는다.

### 9.4 broader single acceptance

```bash
python3 core/bench/with_zmq/single/run_comparison.py \
  --patterns PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

### 9.5 multi smoke

```bash
BENCH_TRANSPORTS=tcp \
BENCH_MSG_SIZES=64 \
BENCH_MULTI_WARMUP_SECONDS=1 \
BENCH_MULTI_DURATION_SECONDS=3 \
python3 core/bench/with_zmq/multi/run_comparison.py dealer_dealer \
  --build-dir core/build --runs 1
```

같은 형식으로 `dealer_router`, `router_router`, `pubsub`를 확인한다.
`stream`은 필요한 경우에만 별도 smoke로 본다.

### 9.6 최종 aggregate artifact가 필요할 때만

```bash
./core/bench/with_zmq/run_benchmarks.sh \
  --reuse-build \
  --pattern ALL \
  --msg-sizes 64 \
  --transports tcp,inproc \
  --runs 1 \
  --duration 3 \
  --results-tag <tag>
```

```bash
./core/bench/with_zmq/run_benchmarks_multi.sh \
  --reuse-build \
  --pattern dealer_dealer,dealer_router,router_router,pubsub \
  --msg-sizes 64 \
  --transports tcp \
  --runs 1 \
  --warmup 1 \
  --duration 3 \
  --results-tag <tag>
```

## 10. 로그 업데이트 규칙

각 iteration 끝에는
[single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
에 최소한 아래를 남긴다.

- 작업한 가설 1개
- 수정한 파일 경로
- 실행한 명령
- 생성된 결과 파일 경로
- 핵심 수치
- 유지한 변경 / 원복한 변경
- 다음 iteration 우선순위

rejected candidate는 반드시 로그에 남긴다.
같은 실패 실험을 이유 없이 반복하지 않는다.

각 iteration 끝에는
[hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
도 반드시 다시 검토한다.

- hot-path 계약/주의점/우선순위가 바뀌면 즉시 갱신한다.
- 실제 변경이 없더라도, 이번 iteration 결과가 기존 문서와 모순되지 않음을
  확인해야 한다.

## 11. 완료 판정

아래가 모두 참이면 완료다.

1. 5장의 stop condition 충족
2. raw/public guardrail 이상 없음
3. `cmake --build core/build -j$(nproc)` 성공
4. 관련 `core/tests/` 회귀 테스트 성공
5. broader single acceptance 성공
6. multi smoke에 치명적 퇴행 없음
7. 로그 문서와 hot-path 문서가 현재 코드와 일치

이 상태에서 더 이상 남은 가설이나 해야 할 수정이 없을 때만
`미적용 사항이 없습니다.` 로 종료한다.

## 12. 현재 작업 레지스터

- 현재 유지 중인 latest delta
  - `lb.cpp` one-active-pipe `DEALER` send fast path
  - `dist.cpp` one-matching-pipe `PUBSUB` send fast path와
    index-stable deactivate helper
  - `test_multi_socket_contract_regressions.cpp` concurrent `PUB` publish
    regression 추가
  - `PUBSUB tcp 64B` quick run이 `-24.23%`, rerun이 `-26.00%`까지 회복
- 현재 배제 유지 후보
  - `fq.cpp` one-active-pipe recv fast path
  - `DEALER_DEALER inproc 64B`가 `-34.71%`로 악화돼 원복
  - `object.cpp` same-thread `send_activate_read()` direct delivery
  - generic 적용은 `PAIR inproc`만 일부 회복했지만
    `DEALER_DEALER tcp 64B`를 `-25.06%`로 악화시켜 원복
  - `PAIR` no-handler 전용 gate도 `PAIR tcp/inproc 64B`를
    `-26.32%` / `-32.96%`로 악화시켜 원복
  - `socket_message_send_api.cpp` single-part public fast path의
    중복 `msg->check()` 제거도 `DEALER_DEALER inproc 64B`를
    `-31.51%`로 악화시켜 원복
- 아직 남은 핵심 미달
  - `PAIR tcp 64B`: `-15.78%`
  - `PAIR inproc 64B`: `-17.62%`
  - `DEALER_DEALER tcp 64B`: `-13.19%`
  - `DEALER_DEALER inproc 64B`: `-18.46%`
  - `DEALER_ROUTER tcp 64B`: `-33.98%`
  - `DEALER_ROUTER inproc 64B`: `-24.43%`
  - `PUBSUB tcp 64B`: `-26.00%`
  - `PUBSUB inproc 64B`: `-42.51%`
  - `ROUTER_ROUTER tcp 64B`: `-56.66%`
  - `ROUTER_ROUTER inproc 64B`: `-25.32%`
  - multi `dealer_dealer tcp 64B`: `-29.55%`
  - multi `pubsub tcp 64B`: `-26.97%`

- [ ] send-side lifecycle/backpressure retry cost를 더 줄일 구조를 찾는다.
- [ ] `pipe` send/publication 경로에서 ordering을 유지한 채 lock 안 work를 줄인다.
- [ ] `PUBSUB` publish/distribution path를 single-subscriber win에서
      inproc/multi까지 확장한다.
- [ ] `ROUTER_ROUTER` routed path를 패턴 전용으로 본다.
- [x] 이번 단계 send-path 변경 뒤 `PAIR`/`DEALER_DEALER` raw/public 분리를
      다시 기록했다.
- [ ] broader single / multi smoke까지 통과하는 안정 지점을 남긴다.
