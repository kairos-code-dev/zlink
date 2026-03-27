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
  - `pipe.cpp` / `dist.cpp` `PUBSUB` publication path의
    dist-only non-recursive HWM check helper
  - `dist.cpp` one-matching-pipe `PUBSUB` send fast path와
    index-stable deactivate helper
  - `multipart_send_txn.cpp` / `socket_base_msg.cpp` logical multipart
    single public send scope contract fix
  - `test_multi_socket_contract_regressions.cpp` concurrent `PUB` publish
    regression 추가
  - `core/tests/CMakeLists.txt` / `test_router_mandatory_hwm.cpp`
    `ROUTER` mandatory-HWM 회귀를 ctest surface에 등록하고
    `zlink_send_rid()` coverage 추가
  - `bench_zlink_pubsub.cpp` no-topic payload-only surface alignment
  - surface-aligned `PUBSUB tcp 64B` first run `-24.51%`, rerun `-23.17%`
  - latest `PUBSUB` dist-only non-recursive HWM check
    isolated first/rerun `tcp/inproc -25.76% / -39.88%`,
    `-19.48% / -39.31%`
  - same delta의 broader single rerun은
    `PAIR tcp/inproc -18.89% / -17.22%`,
    `PUBSUB tcp/inproc -23.63% / -39.84%`,
    `DEALER_DEALER tcp/inproc -24.09% / -27.90%`,
    `DEALER_ROUTER tcp/inproc -27.28% / -27.07%`,
    `ROUTER_ROUTER tcp/inproc -54.97% / -30.77%`였다
  - multi `pubsub tcp 64B`는 `-16.65%`까지 회복했다
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
  - `xpub.cpp` single matching `nodrop` HWM+write fusion도
    `PUBSUB tcp 64B`를 `-34.30%`, rerun `-36.14%`로 다시 악화시켜 원복
  - `socket_base_routing.cpp` single-out-pipe routed lookup cache도
    `ROUTER_ROUTER tcp 64B` rerun `-56.74%`, inproc rerun `-26.10%`로
    broad win이 아니어서 원복
  - `socket_runtime.cpp` `public_api_state` 전체 enter/leave CAS fast path도
    `PAIR` public이 `tcp/inproc -38.34% / -33.15%`로 흔들리고
    `PAIR inproc raw`도 `-36.11%`까지 악화돼 원복
  - `socket_runtime.cpp` `unlock_public_api_sync_and_leave()` 단독 CAS fast path는
    raw는 좋아졌지만 public rerun에서 `DEALER_DEALER tcp/inproc`이
    `-27.23% / -30.85%`로 다시 흔들려 원복
  - `socket_runtime.cpp` `PAIR` no-sync send scope enter+leave fast path도
    raw는 일부 회복했지만 public seq에서 `PAIR tcp/inproc`이
    `-37.97% / -32.71%`로 다시 벌어져 원복
  - `socket_runtime.cpp` `PAIR` no-sync send scope leave-only fast path도
    `PAIR`는 덜 흔들렸지만 같은 seq run의 `DEALER_DEALER tcp/inproc`이
    `-37.43% / -34.21%`로 내려가 broad win이 아니어서 원복
  - `socket_base_msg.cpp` retry loop의
    `send_ready_handler_active() -> send_ready_armed` gate도
    `DEALER_DEALER tcp 64B`는 `-9.81%`까지 회복했지만
    `PAIR inproc 64B` / `DEALER_DEALER inproc 64B`가
    `-24.33%` / `-32.86%`로 다시 흔들려 원복
  - `socket_message_send_api.cpp` no-topic single-part `PUBSUB`
    public fast path도 isolated run에서 `tcp/inproc -32.84% / -45.80%`로
    다시 악화돼 원복
  - `socket_message_recv_api.cpp` `SUB/XSUB` raw multipart single-part recv
    fast path도 first/rerun이 `tcp -30.67% / -26.74%`,
    `inproc -41.47% / -50.68%`로 엇갈려 broad win이 아니어서 원복
  - `xpub.cpp` all-attached empty-prefix `send_to_all()` fast path도
    isolated `PUBSUB tcp 64B -22.28%`, multi `pubsub tcp 64B -21.60%`까지는
    회복했지만 broader single `PUBSUB tcp/inproc`가
    `-30.53% / -42.65%`로 남아 broad win이 아니어서 원복
  - `xpub.cpp` single-subscriber ready-count fast path도
    `PUBSUB tcp/inproc 64B` first/rerun이
    `-26.22% / -38.31%`, `-28.90% / -42.92%`로 keep-worthy broad win이
    아니어서 원복
  - `xpub.cpp` single attached empty-prefix matching fast path도
    `PUBSUB tcp/inproc 64B` first/rerun이
    `-23.74% / -36.67%`, `-31.16% / -47.67%`로 다시 흔들려 원복
  - `router.cpp` routed send의 prefix/HWM second-check elimination도
    `ROUTER_ROUTER tcp/inproc 64B`가 `-55.19%` / `-25.05%`로
    baseline 대비 미세 개선에 그쳐 broad win이 아니어서 원복
  - `socket_message_recv_api.cpp` / `router.cpp` routed recv
    source-rid zero-elision도 `ROUTER_ROUTER tcp/inproc 64B`가
    `-58.34%` / `-33.47%`로 더 흔들려 원복
  - `xpub.cpp` / `xsub.cpp` `xwrite_activated()` delivery-ready refresh 제거도
    single `PUBSUB tcp/inproc 64B`가 `-27.31%` / `-44.93%`로
    baseline보다 악화돼 원복
  - `xpub.cpp` no-monitor delivery-ready tracking gate와
    monitor-open ready-count priming도 isolated first/rerun이
    `PUBSUB tcp/inproc 64B -26.72% / -37.92%`,
    `-27.12% / -43.79%`로 accepted baseline보다 나빠져 원복
  - `lb.cpp` one-active-pipe no-recursive HWM helper도
    `DEALER` isolated run은 일부 회복했지만
    public serial guardrail의 `PAIR tcp/inproc 64B`가
    `-23.95% / -31.30%`로 무너져 원복
  - `pair.cpp` final-part no-recursive HWM helper도
    isolated `PAIR tcp 64B`는 `-8.49%`까지 회복했지만
    rerun `PAIR inproc 64B`가 `-21.18%`로 흔들리고
    serial guardrail의 `DEALER_DEALER inproc 64B` public/raw가
    `-31.36%` / `-30.57%`로 무너져 원복
  - `XPUB` prechecked no-HWM-recheck도
    isolated first run은 `PUBSUB tcp/inproc 64B -21.70% / -35.47%`로
    둘 다 좋아졌지만, clean rerun `PUBSUB inproc 64B`가 `-41.46%`로
    accepted baseline보다 다시 나빠져 원복
- 현재 코드/문서 정합 메모
  - `pipe.cpp`의 `write()`, `write_and_flush()`, `check_write_status()`는
    `_out_sync`를 잡은 뒤 `check_hwm()`에서 같은 recursive fast mutex를
    한 번 더 재진입한다.
  - 따라서 문서에 적힌 "`write+flush`로 final-part lock 2회 -> 1회"는
    현재 코드에서는 아직 완전히 성립하지 않는다.
  - 다만 2026-03-28 `check_hwm_locked()` helper A/B는 `PAIR`/raw guardrail이
    섞여 keep-worthy broad win으로 남지 못했다.
  - 이 재진입은 계속 cost-axis 후보로 보되, 현재는 active delta가 아니라
    generic 확대 후보로만 유지한다.
  - 다만 2026-03-28 `pipe.cpp` / `dist.cpp` dist-only
    non-recursive HWM check는 isolated first/rerun과
    broader single rerun, multi `pubsub tcp`까지 current code 기준
    keep-worthy broad win을 만들었다.
  - 따라서 current accepted delta는 generic helper rollout이 아니라
    `PUBSUB` publication path에 한정한 narrow pipe work 축소다.
  - 2026-03-28 `socket_runtime.cpp` lifecycle atomic CAS fast path A/B도
    `DEALER` raw 회복과 `PAIR`/public 흔들림이 엇갈렸다.
  - 따라서 send-side lifecycle/backpressure 첫 우선순위는 유지하되,
    현재 문서 기준으로 keep-worthy 공통 atomic fast path는 아직 없다.
  - 같은 날 `PAIR` no-sync send scope 전용 enter+leave / leave-only fast path도
    각각 raw/public guardrail 또는 `DEALER` broad guardrail을 만족시키지 못해
    현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_base_msg.cpp` retry loop에서
    installed-but-idle send-ready handler까지 sync 유지 범위를 넓히는 후보도
    serial public/raw guardrail을 만족시키지 못해 현재 코드에는 남아 있지 않다.
  - 2026-03-28 baseline 재검증에서
    `test_pubsub_publish_is_safe_from_multiple_threads`가
    `part_count Expected 1 Was 2`로 반복 실패했고,
    현재 코드는 `socket_base_msg.cpp` / `multipart_send_txn.cpp`에서
    logical multipart publish/send 전체를 하나의 public send scope로 묶어
    topic+payload interleave를 막도록 고쳤다.
  - 이후 `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
    는 다시 통과한다.
  - 다만 이 fix는 contract 회복 단계이지 keep-worthy perf delta가 아니다.
    latest single `PUBSUB` public rerun은
    `tcp/inproc -30.71% / -40.37%`였고,
    no-topic single-part direct-send fallback은
    `tcp/inproc -31.67% / -38.76%`로 broad recovery를 만들지 못해
    현재 코드에는 남기지 않았다.
  - post-fix serial raw/public rerun에서도 `PAIR` public→raw가
    `2717.91 -> 3212.40`, `3326.33 -> 3136.33`,
    `DEALER_DEALER` public→raw가
    `3126.42 -> 3135.91`, `3163.47 -> 3138.42`로 다시 엇갈렸다.
  - 따라서 raw/public 분리는 계속 guardrail로 유지하되,
    다음 iteration은 contract fix를 유지한 채 send-side lifecycle /
    publication cost를 더 줄이는 쪽으로 이어간다.
  - 같은 날 `socket_message_send_api.cpp` no-topic single-part `PUBSUB`
    public fast path도 isolated run에서 broad win을 만들지 못해
    현재 코드에는 남아 있지 않다.
  - 같은 날 `xpub.cpp` all-attached empty-prefix `send_to_all()` fast path도
    isolated `PUBSUB tcp 64B`와 multi `pubsub tcp 64B`는 회복했지만,
    `PUBSUB inproc 64B`가 `-43.96%`, broader single `PUBSUB tcp/inproc`가
    `-30.53% / -42.65%`여서 keep-worthy broad win이 아니었다.
  - 따라서 current `PUBSUB` 잔여 gap의 다음 후보는
    empty-prefix trie match 제거가 아니라 publication/lifecycle differential 쪽이다.
  - 같은 날 `xpub.cpp` single-subscriber ready-count fast path도
    clean first/rerun이 `-26.22% / -38.31%`,
    `-28.90% / -42.92%`로 accepted baseline을 넘지 못해 현재 코드에는 없다.
  - 같은 날 `xpub.cpp` single attached empty-prefix matching fast path도
    first run은 `tcp/inproc -23.74% / -36.67%`였지만,
    clean rerun이 `-31.16% / -47.67%`로 무너져 현재 코드에는 없다.
  - 따라서 current `PUBSUB` 잔여 gap은 delivery-ready bookkeeping이나
    single-attached empty-prefix trie match 제거보다
    publication/wakeup differential 자체를 더 직접 봐야 한다.
  - 같은 날 `test_router_mandatory_hwm`는 이제
    `core/tests/CMakeLists.txt`에 등록돼 ctest surface에 실제 포함되고,
    `zlink_send_rid()` mandatory-HWM subcase도 함께 돈다.
  - `ctest --test-dir core/build --output-on-failure -R '^(test_router_mandatory_hwm|test_public_inproc_multipart_send|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
    는 현재 다시 통과한다.
  - 같은 날 `router.cpp` routed send의 prefix/HWM second-check elimination은
    `ROUTER_ROUTER tcp/inproc 64B`를 `-55.19% / -25.05%`까지밖에
    못 줄였고 broad win이 아니어서 현재 코드에는 없다.
  - 같은 날 `socket_message_recv_api.cpp` / `router.cpp` routed recv
    source-rid zero-elision도 `ROUTER_ROUTER tcp/inproc 64B`를
    `-58.34% / -33.47%`로 더 흔들려 현재 코드에는 없다.
  - 따라서 current `ROUTER` 잔여 gap은 routed prefix/HWM recheck나
    source-rid zero-fill 제거 같은 micro-elision 하나로 설명되지 않는다.
  - 같은 날 `xpub.cpp` / `xsub.cpp` `xwrite_activated()`의
    delivery-ready refresh 제거도 single `PUBSUB tcp/inproc 64B`가
    `-27.31% / -44.93%`로 나빠져 현재 코드에는 없다.
  - 즉 current `PUBSUB` 잔여 gap은 `write_activated`에서의 monitor-ready
    refresh 하나를 빼는 수준으로는 줄지 않는다.
  - 같은 날 `xpub.cpp` no-monitor delivery-ready tracking gate와
    monitor-open ready-count priming도 isolated first/rerun이
    `PUBSUB tcp/inproc 64B -26.72% / -37.92%`,
    `-27.12% / -43.79%`로 accepted baseline보다 나빠져 현재 코드에는 없다.
  - 즉 current `PUBSUB` 잔여 gap은
    "monitor가 없을 때 ready-count recompute를 건너뛰자"는
    bookkeeping gate 하나로도 줄지 않는다.
  - 같은 날 `lb.cpp` one-active-pipe no-recursive HWM helper도
    isolated `DEALER` run은 일부 회복했지만
    public serial guardrail `PAIR tcp/inproc 64B`가
    `-23.95% / -31.30%`로 깨져 현재 코드에는 없다.
  - 같은 날 `pair.cpp` final-part no-recursive HWM helper도
    isolated `PAIR tcp 64B`는 `-8.49%`까지 회복했지만
    rerun `PAIR inproc 64B`가 `-21.18%`로 다시 흔들렸고,
    serial guardrail의 `DEALER_DEALER inproc 64B` public/raw가
    `-31.36%` / `-30.57%`로 무너져 현재 코드에는 없다.
  - 같은 날 `XPUB` prechecked no-HWM-recheck도
    isolated first run은 `PUBSUB tcp/inproc 64B -21.70% / -35.47%`로
    둘 다 회복했지만, clean rerun `PUBSUB inproc 64B`가 `-41.46%`로
    accepted baseline보다 다시 나빠져 현재 코드에는 없다.
  - 같은 날 current accepted `dist` helper 위
    `XPUB` all-attached empty-prefix `send_to_all()` v2도
    sequential seq1/seq2 `PUBSUB tcp/inproc 64B`가
    `-25.77% / -40.89%`, `-23.12% / -40.39%`로
    accepted baseline `-23.63% / -39.84%`를 stable하게 넘지 못해
    현재 코드에는 없다.
  - 같은 날 `socket_message_recv_api.cpp` `SUB/XSUB` raw multipart
    single-part recv fast path도 tcp/inproc 방향이 엇갈려
    현재 코드에는 남아 있지 않다.
  - 2026-03-28 직렬 raw/public spot-check
    (`codex_20260328_pair_public_serial`,
    `codex_20260328_pair_raw_serial`,
    `codex_20260328_dealer_public_serial`,
    `codex_20260328_dealer_raw_serial`)에서는
    zlink 절대 throughput 기준으로
    `PAIR tcp/inproc` public→raw가 `3200.10 -> 3367.91`,
    `2816.95 -> 3015.08`로 모두 회복됐지만,
    `DEALER_DEALER tcp/inproc`는 `2830.18 -> 3263.08`,
    `3145.71 -> 2799.88`로 inproc 방향이 다시 엇갈렸다.
  - 따라서 "`public penalty는 이미 low single-digit이고 secondary`"라는
    문장은 현재 guide의 고정 전제로 둘 수 없다.
  - raw/public 분리는 계속 guardrail로 유지하되, 이번 실행에서는
    패턴/transport별 serial 재측정으로만 해석을 갱신한다.
  - single `PUBSUB` zlink bench는 현재
    `zlink_publish(NULL, &part, 1)` + `zlink_recv(...)` no-topic payload-only
    경로로 정렬됐다.
  - aligned first run은 `PUBSUB tcp/inproc 64B`가
    `-24.51%` / `-41.79%`, rerun은 `-23.17%` / `-44.68%`였다.
  - 즉 empty-topic frame/topic-aware recv surface mismatch는 실제로 있었지만,
    이를 제거해도 `PUBSUB` 잔여 gap의 본체가 사라지지는 않는다.
- 아직 남은 핵심 미달
  - `PAIR tcp 64B`: `-18.89%`
  - `PAIR inproc 64B`: `-17.22%`
  - `DEALER_DEALER tcp 64B`: `-24.09%`
  - `DEALER_DEALER inproc 64B`: `-27.90%`
  - `DEALER_ROUTER tcp 64B`: `-27.28%`
  - `DEALER_ROUTER inproc 64B`: `-27.07%`
  - `PUBSUB tcp 64B`: `-23.63%`
  - `PUBSUB inproc 64B`: `-39.84%`
  - `ROUTER_ROUTER tcp 64B`: `-54.97%`
  - `ROUTER_ROUTER inproc 64B`: `-30.77%`
  - multi `dealer_dealer tcp 64B`: `-29.55%`
  - multi `pubsub tcp 64B`: `-16.65%`

- [x] same-handle concurrent `PUB` publish contract regression
      (`test_pubsub_publish_is_safe_from_multiple_threads`)을 고치고
      logical multipart send scope를 재검증했다.
- [ ] send-side lifecycle/backpressure retry cost를 더 줄일 구조를 찾는다.
      현재 current code 기준 keep-worthy 공통 delta는 없고,
      no-monitor delivery-ready tracking gate도 rejected candidate가 됐다.
      `lb.cpp` one-active-pipe no-recursive HWM helper도
      `PAIR` public guardrail을 깨뜨려 rejected candidate가 됐다.
      `pair.cpp` final-part no-recursive HWM helper도
      `DEALER_DEALER inproc` public/raw guardrail을 깨뜨려
      rejected candidate가 됐다.
      `XPUB` prechecked no-HWM-recheck도
      clean rerun `PUBSUB inproc`이 accepted baseline 아래로 다시 내려가
      rejected candidate가 됐다.
      current accepted `dist` helper 위
      `XPUB` all-attached empty-prefix `send_to_all()` v2도
      seq1/seq2 모두 keep-worthy broad win이 아니어서 rejected candidate다.
      `ROUTER` blocking envelope / `zlink_send_rid()` multipart
      routed-data view candidate도 first/rerun
      `ROUTER_ROUTER tcp/inproc 64B`
      `-58.62% / -30.04%`, `-55.12% / -29.06%`로
      stable broad win이 아니어서 rejected candidate가 됐다.
      다음 실제 code 후보는 여전히 다른 `pipe`/`PUBSUB` publication 축이나
      다른 `ROUTER_ROUTER` 전용 differential 정리다.
- [x] `pipe` send/publication 경로에서 ordering을 유지한 채 lock 안 work를 줄였다.
- [x] single `PUBSUB` no-topic bench surface를 현재 계약에 맞게 다시 정렬했다.
- [ ] `PUBSUB` publish/distribution path를 single-subscriber win에서
      inproc/multi까지 확장한다.
      current accepted `dist` helper 위
      `XPUB` all-attached empty-prefix `send_to_all()` v2도
      sequential seq1/seq2 `PUBSUB tcp/inproc 64B`
      `-25.77% / -40.89%`, `-23.12% / -40.39%`로
      accepted baseline `-23.63% / -39.84%`를 stable하게 넘지 못해
      rejected candidate가 됐다.
- [x] `test_router_mandatory_hwm`를 ctest에 등록하고
      `zlink_send_rid()` mandatory-HWM 회귀를 추가했다.
- [x] `test_public_inproc_router_send_rid_multipart_blocking()`으로
      `zlink_send_rid()` multipart blocking contract를 회귀에 추가했다.
- [ ] `ROUTER_ROUTER` routed path를 패턴 전용으로 본다.
      `socket_message_send_api.cpp` / `multipart_send_txn.cpp` /
      `socket_base_msg.cpp` / `router.cpp` routed-data view candidate도
      first/rerun `ROUTER_ROUTER tcp/inproc 64B`
      `-58.62% / -30.04%`, `-55.12% / -29.06%`로
      stable broad win이 아니어서 current code에는 남기지 않았다.
- [x] 이번 단계 send-path 변경 뒤 `PAIR`/`DEALER_DEALER` raw/public 분리를
      다시 기록했다.
- [x] broader single / multi smoke까지 통과하는 안정 지점을 남겼다.
