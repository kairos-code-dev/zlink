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
- iteration 시작 시에는 긴 본문보다 각 문서 상단의 `Current Operating
  Summary` 블록을 먼저 읽는다.
- summary 블록이 최신 kept delta, 현재 가설, 배제 family, 다음 exact step을
  반영하지 못하면, 코드 수정이나 bench 실행 전에 먼저 summary를 갱신한다.
- 다만 로그 문서는 기본적으로 최신 요약/피벗/현재 작업 레지스터부터 읽는다.
  오래된 rejected candidate 구간은 현재 가설을 검증하는 데 필요할 때만
  역참조한다.
- 둘 중 하나라도 현재 코드/해석/우선순위와 어긋나면 즉시 갱신한다.
- 실제 변경이 없더라도, iteration 결과가 두 문서의 현재 내용과 일치하는지
  확인하지 않으면 다음 iteration으로 넘어가면 안 된다.
- 짧은 summary가 긴 로그보다 우선한다. 긴 로그는 summary를 갱신하거나
  현재 가설을 검증할 근거가 필요할 때만 깊게 읽는다.
- 별도 main/master/gap/residual/spec 문서는 추가로 만들지 않는다.
- 이 루프의 기본 동작은 `--max-iterations 0`, 즉 목표 완료까지 무한 반복이다.
- 반복 횟수를 제한하고 싶을 때만 명시적으로 `--max-iterations <N>`을 넘긴다.
- 같은 wrapper scope에 대해 supervisor는 하나만 유지한다.
  새 실행을 시작할 때 같은 `guide + logs-dir + gate-label` 범위의 기존
  supervisor와, 같은 wrapper scope에 남아 있는 child `codex exec`가 있으면
  먼저 정리하고 새 세션으로 시작한다.

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
   기본 순서는 최신 pivot/요약/현재 작업 레지스터 우선이며,
   오래된 로그 구간은 현재 가설과 직접 관련될 때만 다시 읽는다.
3. 두 문서를 기준으로 현재 top hypothesis 하나만 고른다.
4. 필요한 경우 `/home/hep7/project/kairos/libzmq` 대응 구현을 먼저 읽고,
   현재 후보의 semantic / ordering / hot-path work 차이를 짧게 정리한다.
5. 새 단계나 새 candidate family로 넘어가기 전에는 `claude` 의견도 한 번
   수렴한다. 목적은 authority를 바꾸는 것이 아니라, 현재 가설을 다른 시각에서
   검토해 local search drift를 막는 것이다.
6. `core/`와 `core/tests/`를 우선 수정한다.
7. [`core/build/`](/home/hep7/project/kairos/zlink/core/build)로 빌드한다.
8. 영향 패턴의 targeted single 벤치를 먼저 돌린다.
9. 의미 있는 개선이 보이면 raw/public 분리를 다시 확인한다.
10. 개선이 유지될 때만 broader single, 필요한 multi smoke를 수행한다.
11. 결과 파일 경로, 숫자, 해석, 배제한 가설을
   [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
   에 기록한다.
12. 현재 iteration 결과가
    [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
    와도 일치하도록 계약/주의점/우선순위/배제 후보를 반영하거나,
    변경이 없음을 확인한다.
13. 아직 stop condition을 못 만족하면 다음 미해결 가설로 반복한다.

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

### 6.2 guide 재작성 트리거

- 같은 패턴에서 같은 계열 후보가 `2`개 이상 연속으로 rejected candidate가 되면,
  다음 iteration은 코드 수정이 아니라 guide/review/hot-path 재정렬부터 시작한다.
- 특히 `PUBSUB`에서 `dist.cpp`, `xpub.cpp`, `pipe publication` 미시 후보가
  연속으로 broad win을 만들지 못하면, 더 좁은 local 후보를 계속 추가하지 않는다.
- 이 경우 먼저 semantic/backpressure map을 다시 만든 뒤에만 다음 code candidate로
  넘어간다.

### 6.3 미세 후보 제한 규칙

- 각 iteration의 top hypothesis는 아래 셋 중 하나여야 한다.
  - 공통 differential을 겨냥한 high-leverage 후보
  - semantic/backpressure/probe 분리 측정
  - 이미 유지된 delta의 일반화 또는 계약 보강
- 새 단계, 새 pattern family, 새 broad hypothesis로 넘어갈 때는
  local patch 전에 `libzmq` reference pass와 `claude` consult를 둘 다 거친다.
  둘 중 하나라도 건너뛰면 바로 코드 수정으로 넘어가지 않는다.
- 공통 differential이나 pattern-specific core path를 건드릴 때는
  local patch 전에 `/home/hep7/project/kairos/libzmq` 대응 구현을 먼저 읽는 것을 기본으로 한다.
- 목적은 upstream 동작을 복제하는 것이 아니라,
  현재 차이가 `semantic`, `ordering`, `hot-path work` 중 어디에 있는지
  분리하는 reference oracle로 쓰는 것이다.
- 아래 형태의 local tweak는 semantic map이나 broad hypothesis 없이
  바로 top hypothesis로 올리지 않는다.
  - bookkeeping / index / refresh 제거
  - same-thread wakeup / direct delivery / zero-elision
  - 특정 pattern 전용 small helper
- 같은 계열 local tweak가 `2`개 연속 rejected 되면,
  다음 iteration은 반드시 code patch가 아니라
  `raw/public 재분리`, `semantic probe`, `우선순위 재작성` 중 하나여야 한다.
- 유지된 code delta 없이 rejected candidate만 `3` iteration 연속 쌓이면,
  루프는 자동으로 탐색 단계로 되돌아간다.
  다음 iteration은 broad hypothesis를 다시 쓰기 전에는
  미세 최적화 패치를 시작하면 안 된다.
- `semantic probe` iteration에서는 기본적으로 `core/` hot-path 코드를 수정하지 않는다.
  측정 surface 자체가 잘못됐다는 강한 증거가 있을 때만 측정 코드를 손댄다.
- 즉 semantic probe iteration의 기본 산출물은
  - 결과 파일
  - gap 해석
  - 다음 broad hypothesis
  이 셋이어야 한다.

### 6.4 단계 승격 규칙

- 현재 후보가 다음 단계로 갈 수 있으려면 최소한 아래 중 하나를 만족해야 한다.
  - accepted baseline 대비 stable broad win
  - semantic differential을 가르는 sign flip 또는 큰 gap 축소
  - correctness contract를 강화하면서 성능도 유지
- 위 셋 중 아무것도 만들지 못하면,
  그 후보는 더 미세하게 파지 말고 rejected candidate로 기록한 뒤 종료한다.

- push 없이 다음 단계로 넘어가면 안 된다.

## 7. 작업 순서 우선순위

현재 우선순위는 아래 순서를 유지한다.

1. 현재 accepted `PAIR` / `DEALER` 공통 delta를 기준선으로 유지하고,
   broad win 근거 없는 공통 미세 후보는 다시 파지 않는다.
2. `PUBSUB`는 code optimization 전에 semantic/backpressure map을 먼저 만든다.
   최소 분리 축은 아래 네 가지다.
   - `XPUB_NODROP=1` 대 `0` 진단 probe
   - `tcp` 대 `inproc`
   - single 대 multi
   - default HWM 대 변경된 HWM
   - supplementary high-HWM probe
     [`perf_linux_20260328_124815.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_124815.txt)
     에서는 `hwm/sndhwm/rcvhwm = 1000000`인데도 `PAIR`, `PUBSUB`,
     `DEALER_DEALER` 64B gap이 `-22% ~ -39%`로 크게 남았다.
     따라서 current loop는 `backpressure only` 가설을 기본값으로 두지 않고,
     queue가 차지 않아도 남는 steady-state hot-path 고정비를 먼저 의심한다.
3. `PUBSUB` semantic map 이후에만
   - semantic differential
   - core publication/lifecycle residual
   을 분리해 다음 code candidate를 고른다.
4. `ROUTER_ROUTER`는 `PUBSUB` semantic map이 끝난 뒤에 본다.
5. 남아 있는 recv-side routed / strip / multipart export 경로는 마지막 단계로 둔다.

2026-03-28 현재 send-side lifecycle / backpressure 공통 fast-path 후보는
keep-worthy delta를 만들지 못해 actual implementation 우선순위에서 내렸다.
새로운 broad win 근거가 나오기 전까지는 raw/public guardrail과 broader single
acceptance를 동시에 만족한 pattern-specific publication/routed 후보만 올린다.

현재 가이드의 기본 원칙은 아래와 같다.

- 먼저 더 큰 분리 실험으로 원인을 좁힌다.
- 그 다음에만 local code candidate를 올린다.
- broad win 근거가 없는 미세 후보는 같은 family에서 반복하지 않는다.

naive lock 제거는 현재 배제된 후보로 유지한다.

## 8. 금지 규칙

- `_out_sync` 전체 no-op 같은 naive lock 제거를 다시 넣지 않는다.
- `pipe` reentrant 성질을 깨는 non-reentrant mutex 실험을 기본 후보로 올리지 않는다.
- thread-safe contract를 약화하거나 우회해서 성능을 맞추지 않는다.
- bench 수치를 좋게 보이게 하려고 benchmark API surface를 비대칭으로 바꾸지 않는다.
- 테스트 완화, sleep 추가, retry loop 추가로 문제를 숨기지 않는다.
- `PUBSUB` semantic map을 다시 만들기 전에는
  `dist.cpp` / `xpub.cpp` / `pipe publication` 미시 후보를 계속 반복하지 않는다.
- 같은 계열 rejected candidate가 누적된 상태에서 guide 재작성 없이 루프를 재시작하지 않는다.
- stable broad win 근거 없이 pattern-specific small helper만 계속 바꾸는 식의
  local search를 허용하지 않는다.
- semantic map이 끝나기 전에는 `accepted delta` 주변 helper를 더 얹는 방식의
  additive local search도 허용하지 않는다.
- `XPUB_NODROP=0/1` probe는 진단용으로만 사용한다.
  동일 조건의 `libzmq` 비교 목표를 대신하는 acceptance 기준으로 쓰지 않는다.

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

동일한 `logs-dir + gate-label`로 이미 루프가 살아 있으면,
wrapper는 같은 범위의 기존 supervisor와 남아 있는 child `codex exec`
를 먼저 정리한 뒤 새 세션을 시작해야 한다.

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

### 9.1.1 `libzmq` reference pass

후보가 send/backpressure/publication/routed path를 건드릴 때는,
패치 전에 아래처럼 upstream 대응 파일을 먼저 읽는다.

예시 대응:

- `PAIR` / `DEALER`
  - `/home/hep7/project/kairos/libzmq/src/socket_base.cpp`
  - `/home/hep7/project/kairos/libzmq/src/pipe.cpp`
  - `/home/hep7/project/kairos/libzmq/src/lb.cpp`
- `PUBSUB`
  - `/home/hep7/project/kairos/libzmq/src/xpub.cpp`
  - `/home/hep7/project/kairos/libzmq/src/dist.cpp`
  - `/home/hep7/project/kairos/libzmq/src/pipe.cpp`
- `ROUTER`
  - `/home/hep7/project/kairos/libzmq/src/router.cpp`
  - `/home/hep7/project/kairos/libzmq/src/socket_base.cpp`

이 단계의 산출물은 "무엇이 다른가"를 아래 세 축으로 한 줄씩 남기는 것이다.

- semantic 차이
- ordering / wakeup 차이
- hot-path work 차이

이 정리를 하지 않고 local helper patch부터 시작하지 않는다.

### 9.1.2 `claude` consult pass

새 단계나 새 broad hypothesis를 시작하기 전에는 `claude` 의견도 한 번 받는다.
다만 `claude` 의견은 어디까지나 참고용 advisory다. authority는 여전히
이 guide, 현재 로그 문서, hot-path 계약 문서, 그리고 실제 bench/test 결과다.

먼저 사용 가능 여부를 확인한다.

```bash
claude --help
```

그 다음 non-interactive 한 번 호출로 현재 가설을 검토하게 한다.
예시는 아래 형식을 기본으로 한다.

```bash
claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink \
  "다음 문서를 읽고 현재 top hypothesis와 다음 단계의 위험/누락을 짧게 검토해줘:
  1) /home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-ralph-guide.ko.md
  2) /home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md
  3) /home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md
  필요하면 /home/hep7/project/kairos/libzmq 대응 구현도 참고하고,
  local search drift 여부와 더 큰 병목 후보가 있는지 먼저 말해줘."
```

규칙:

- `claude`는 조언 수집용 advisory다. authority는 여전히 이 guide와
  실제 bench/test 결과다.
- `claude` 의견이 현재 guide와 다르면, 바로 코드 패치부터 하지 말고
  guide/review/hot-path를 먼저 갱신할지 판단한다.
- `claude`가 unavailable이면 그 사실과 이유를 로그에 남기고 계속 진행한다.
- `semantic probe` 단계에서는 `claude`에게도 local tweak 제안보다
  broad hypothesis / semantic differential 위주 검토를 요청한다.

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

### 9.5.1 `PUBSUB` semantic / backpressure probe

기존 accepted baseline과 직접 비교할 때는 아래 두 probe를 먼저 찍는다.

```bash
python3 core/bench/with_zmq/single/run_comparison.py \
  --pattern PUBSUB \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

```bash
PERF_SINGLE_PUBSUB_XPUB_NODROP=0 \
python3 core/bench/with_zmq/single/run_comparison.py \
  --pattern PUBSUB \
  --msg-sizes 64 \
  --transport tcp,inproc \
  --runs 1 \
  --build-dir core/build \
  --results-tag <tag>
```

가능하면 같은 iteration에서 아래 multi smoke도 같이 남긴다.

```bash
BENCH_TRANSPORTS=tcp \
BENCH_MSG_SIZES=64 \
BENCH_MULTI_WARMUP_SECONDS=1 \
BENCH_MULTI_DURATION_SECONDS=3 \
python3 core/bench/with_zmq/multi/run_comparison.py pubsub \
  --build-dir core/build --runs 1
```

여기서 `XPUB_NODROP=0`이 큰 폭의 sign flip이나 gap 축소를 만들면,
다음 단계는 local code tweak가 아니라 semantic differential 정리다.

주의:

- `XPUB_NODROP=0/1`은 어디까지나 원인 분리용 probe다.
- 최종 비교/종료 판정은 항상 default benchmark 조건, 즉 `libzmq`와 같은 비교 조건으로만 한다.
- `NODROP`을 켰을 때만 좋아지는 후보는 acceptance 대상으로 올리지 않는다.

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
- candidate family 1개
- 왜 이 후보가 high-leverage 또는 semantic probe인지 한 줄 근거
- 참고한 `libzmq` 대응 파일
- `claude` consult 여부와 핵심 조언 1~3줄
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
  - `core/bench/with_zmq/CMakeLists.txt` `comp_zlink_pubsub`를
    `single/zlink/bench_zlink_pubsub.cpp`로 다시 연결했다
  - `bench_zlink_pubsub.cpp` receiver는 `zlink_recv()` aggregate path 대신
    `zlink_msg_recv()` single-part payload path를 사용하도록 정렬했다
  - realigned single `PUBSUB tcp/inproc 64B` default는
    `-15.29% / -24.92%`였다
  - 같은 realigned surface에서 `XPUB_NODROP=0` probe는
    `-0.00% / -0.64%`,
    `HWM=16` probe는 `-16.17% / +47.89%`였다
  - multi `pubsub tcp 64B`는 default `-29.83%`,
    `BENCH_MULTI_PUBSUB_HWM=16`에서 `-22.22%`였다
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
  - `xsub.hpp` / `xsub.cpp` empty-subscription accept-all fast path와
    `socket_base.hpp` / `socket_base_dispatch.cpp` /
    `spot_sub_recv.cpp` requested-only `last_recv_source_rid` capture를
    결합한 `xsub` receiver-drain specialization을 유지한다
  - 위 retained delta의 isolated single first/rerun은
    `PUBSUB tcp/inproc 64B -9.40% / -20.35%`,
    `-10.43% / -21.59%`였다
  - 위 retained delta의 broader single은
    `PAIR tcp/inproc -16.64% / -21.71%`,
    `PUBSUB tcp/inproc -11.57% / -20.78%`,
    `DEALER_DEALER tcp/inproc -26.40% / -21.90%`,
    `DEALER_ROUTER tcp/inproc -24.17% / -18.30%`,
    `ROUTER_ROUTER tcp/inproc -55.78% / -21.48%`였다
  - 같은 delta의 multi `pubsub tcp 64B` smoke는 first/rerun
    `+9.25%`, `+8.25%`였다
- 현재 배제 유지 후보
  - `fq.cpp` one-active-pipe recv fast path
  - `DEALER_DEALER inproc 64B`가 `-34.71%`로 악화돼 원복
  - `object.cpp` same-thread `send_activate_read()` direct delivery
  - generic 적용은 `PAIR inproc`만 일부 회복했지만
    `DEALER_DEALER tcp 64B`를 `-25.06%`로 악화시켜 원복
  - `PAIR` no-handler 전용 gate도 `PAIR tcp/inproc 64B`를
    `-26.32%` / `-32.96%`로 악화시켜 원복
  - `dist.cpp` final-part same-thread `send_activate_read()` inline wakeup도
    isolated `PUBSUB tcp/inproc 64B`를 `-25.70% / -42.49%`로
    accepted baseline 아래로 내려 원복
  - `socket_message_send_api.cpp` single-part public fast path의
    중복 `msg->check()` 제거도 `DEALER_DEALER inproc 64B`를
    `-31.51%`로 악화시켜 원복
  - `xpub.cpp` single matching `nodrop` HWM+write fusion도
    `PUBSUB tcp 64B`를 `-34.30%`, rerun `-36.14%`로 다시 악화시켜 원복
  - `pipe.hpp` / `pipe.cpp` / `dist.hpp` / `dist.cpp` / `xpub.cpp`
    single-matching `XPUB_NODROP=1` one-lock helper도
    isolated seq1/seq2/seq3 `PUBSUB tcp/inproc 64B`
    `-27.73% / -38.36%`, `-20.44% / -38.87%`, `-12.75% / -38.71%`,
    broader single `PUBSUB tcp/inproc 64B -21.12% / -26.31%`까지는
    회복했지만 multi `pubsub tcp 64B` first/rerun/`--runs 3`가
    `-25.93%`, `-21.63%`, `-25.68%`로 latest baseline `-17.24%`를
    안정적으로 지키지 못해 원복
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
  - `multipart_send_txn.cpp` / `socket_base_msg.cpp`
    no-topic single-part `PUBSUB` blocked retry sync release도
    publish contract 회귀는 통과했지만
    single `PUBSUB tcp/inproc 64B`가
    `-38.21% / -36.11%`로 `tcp`가 크게 악화돼 원복
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
  - `XPUB` same first-part retry matching cache도
    `PUBSUB tcp/inproc 64B`가 `-26.40% / -44.32%`로
    `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져 원복
  - same-thread `activate_write` mailbox 정렬도
    `PUBSUB tcp/inproc 64B`가 `-26.81% / -43.47%`로
    `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져 원복
  - `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path도
    sequential seq1/seq2/seq3 `PUBSUB tcp/inproc 64B`가
    `-24.81% / -43.41%`, `-22.71% / -34.67%`,
    `-24.42% / -41.68%`로 흔들려 stable broad win이 아니어서 원복
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
  - 같은 날 `XPUB` same first-part retry matching cache도
    `PUBSUB tcp/inproc 64B -26.40% / -44.32%`로
    `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져
    현재 코드에는 없다.
  - 같은 날 same-thread `activate_write` mailbox 정렬도
    `PUBSUB tcp/inproc 64B -26.81% / -43.47%`로
    `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져
    현재 코드에는 없다.
  - 같은 날 current accepted `dist` helper 위
    `XPUB` all-attached empty-prefix `send_to_all()` v2도
    sequential seq1/seq2 `PUBSUB tcp/inproc 64B`가
    `-25.77% / -40.89%`, `-23.12% / -40.39%`로
    accepted baseline `-23.63% / -39.84%`를 stable하게 넘지 못해
    현재 코드에는 없다.
  - 같은 날 `dist.cpp` single-pipe `match()/activated()` bookkeeping
    fast path도 sequential seq1/seq2/seq3가
    `-24.81% / -43.41%`, `-22.71% / -34.67%`,
    `-24.42% / -41.68%`로 다시 흔들려 현재 코드에는 없다.
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
  - `compile_commands.json` 확인 결과 current build target
    `comp_zlink_pubsub`가 실제로 `perf_pubsub.cpp`를 빌드하고 있음을 확인한 뒤,
    `core/bench/with_zmq/CMakeLists.txt`를 `single/zlink/bench_zlink_pubsub.cpp`
    기준으로 고쳤다.
  - 현재 realigned single `PUBSUB` zlink bench는
    `zlink_publish(NULL, &part, 1)` +
    `zlink_msg_recv()` single-part payload path를 사용한다.
  - surface realignment 직후의 historical single `PUBSUB tcp/inproc 64B`는
    `-15.29% / -24.92%`였다.
  - 같은 realigned surface의 historical probe에서
    `XPUB_NODROP=0`은 `-0.00% / -0.64%`,
    `HWM=16`은 `-16.17% / +47.89%`였다.
  - historical multi `pubsub tcp 64B`는 default `-29.83%`,
    `BENCH_MULTI_PUBSUB_HWM=16`에서 `-22.22%`였다.
  - 다만 2026-03-28 current retained code direct recheck에서는
    single `PUBSUB tcp/inproc 64B`가
    seq1 `-21.73% / -19.43%`,
    rerun `-22.44% / -31.08%`였고,
    `XPUB_NODROP=0` probe는 `-0.06% / +0.04%`,
    latest multi `pubsub tcp 64B`는 `-22.75%`였다.
  - 따라서 위 `-15.29% / -24.92%`와 `-29.83%`는 surface realignment 당시의
    historical anchor로만 두고,
    현재 실행의 수치 판단은 recheck 값으로 갱신한다.
  - 즉 surface mismatch 자체가 same-day `PUBSUB tcp` gap의 큰 일부였고,
    realigned 기준에서도 default HWM + `XPUB_NODROP=1` differential과
    multi regression이 여전히 남는다.
  - 따라서 low-HWM single win을 acceptance로 쓰지 않고,
    actual next `PUBSUB` code 후보는 realigned surface 기준의
    default HWM + `XPUB_NODROP=1` publication/backpressure differential이다.
  - earlier `perf_pubsub.cpp` auxiliary surface의 추가 HWM sweep
    (`codex_20260328_pubsub_hwm_probe_16/64/256/1000`)은 historical diagnostic으로만
    남긴다.
  - current acceptance surface에서는
    `XPUB_NODROP=0` sign flip과
    default-vs-multi gap이 더 직접적인 우선순위다.
  - 같은 날 safe single-pipe `nodrop` fusion도 direct
    `./core/build/bin/test_xpub_nodrop`에서
    heap corruption / invalid msg assert로 바로 탈락해 현재 코드에는 없다.
  - full revert 뒤 repeated direct `test_xpub_nodrop` run은
    PASS와 abort가 섞였다.
    `Assertion failed: check() (.../msg.cpp:559)`,
    `Bad address (.../xsub.cpp:56/68)`,
    `malloc(): corrupted top size`가 PASS 사이에 섞여 나와
    deterministic baseline이 아니었다.
  - 이후 `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`로
    current `core/build`를 재configure하자
    `test_xpub_nodrop`가 ctest surface에 다시 포함됐고,
    `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
    는 현재 통과한다.
  - 따라서 current workspace에서는 direct binary flake를 historical
    auxiliary diagnostic으로만 두고, targeted ctest `test_xpub_nodrop`를
    primary gate로 사용한다.
  - 기존 `perf_pubsub.cpp` 기반 single comparison report는 queue probe
    지표를 저장했지만, 현재 realigned `bench_zlink_pubsub.cpp` surface는
    그 지표를 직접 내보내지 않는다.
  - 따라서 earlier queue metrics는 auxiliary diagnostic으로만 유지하고,
    current acceptance surface의 해석은 realigned throughput/latency와
    `XPUB_NODROP=0`, `HWM=16`, multi smoke 숫자 위주로 다시 쓴다.
- 아직 남은 핵심 미달
  - `PAIR tcp 64B`: `-18.89%`
  - `PAIR inproc 64B`: `-17.22%`
  - `DEALER_DEALER tcp 64B`: `-24.09%`
  - `DEALER_DEALER inproc 64B`: `-27.90%`
  - `DEALER_ROUTER tcp 64B`: `-27.28%`
  - `DEALER_ROUTER inproc 64B`: `-27.07%`
  - `PUBSUB tcp 64B`: `-22.44%`
  - `PUBSUB inproc 64B`: `-31.08%`
  - `ROUTER_ROUTER tcp 64B`: `-54.97%`
  - `ROUTER_ROUTER inproc 64B`: `-30.77%`
  - multi `dealer_dealer tcp 64B`: `-29.55%`
  - multi `pubsub tcp 64B`: `-22.75%`

- [x] same-handle concurrent `PUB` publish contract regression
      (`test_pubsub_publish_is_safe_from_multiple_threads`)을 고치고
      logical multipart send scope를 재검증했다.
- [x] send-side lifecycle/backpressure 공통 후보를 current code 기준으로
      소진했다. `public_api_state` CAS fast path, `PAIR` no-sync send scope,
      idle send-ready retry gate, generic same-thread `send_activate_read()`
      direct delivery 모두 keep-worthy broad win을 만들지 못했다.
      따라서 common lifecycle/backpressure 축은 새 근거가 나올 때만 다시 올리고,
      actual next code 후보는 pattern-specific `pipe`/`PUBSUB` publication 축과
      `ROUTER_ROUTER` 전용 differential 정리로 넘긴다.
- [x] `pipe` send/publication 경로에서 ordering을 유지한 채 lock 안 work를 줄였다.
- [x] single `PUBSUB` comparison surface를 current guide 계약에 맞는
      no-topic payload-only binary까지 다시 정렬했다.
      [`core/bench/with_zmq/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/CMakeLists.txt)
      `comp_zlink_pubsub`를
      [`core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/zlink/bench_zlink_pubsub.cpp)
      로 다시 연결했고,
      receiver helper도 `zlink_msg_recv()` single-part path로 정렬했다.
      `compile_commands.json`에는 새 target source가 반영됐고,
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
      가 통과했다.
- [x] `PUBSUB` semantic/backpressure map을 다시 만들었다.
      surface realignment 직후 historical map은
      `PUBSUB tcp/inproc 64B -15.29% / -24.92%`,
      `XPUB_NODROP=0 -0.00% / -0.64%`,
      `HWM=16 -16.17% / +47.89%`,
      multi `pubsub tcp 64B default -29.83%`,
      `BENCH_MULTI_PUBSUB_HWM=16 -22.22%`였다.
      다만 2026-03-28 current retained code direct recheck에서는
      single `PUBSUB tcp/inproc 64B`가
      seq1 `-21.73% / -19.43%`,
      rerun `-22.44% / -31.08%`,
      `XPUB_NODROP=0` probe는 `-0.06% / +0.04%`,
      latest multi `pubsub tcp 64B`는 `-22.75%`였다.
      즉 surface mismatch correction 사실 자체는 유지되지만,
      현재 실행의 source-of-truth baseline은 위 recheck 값으로 갱신한다.
      여전히 current gap의 큰 축은
      default HWM + `XPUB_NODROP=1` differential이고,
      low-HWM single win은 multi/general acceptance를 대체하지 못한다.
- [x] single comparison report가 `PUBSUB` queue probe 지표를 저장하도록
      갱신했다.
      [`core/bench/with_zmq/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/run_comparison.py)가
      `snd_pending_max` / `rcv_pending_max` / `rcv_pending_end`를 결과 파일에
      남기도록 바뀌었고, default report는
      `PUBSUB tcp/inproc 64B snd_pending_max 654 / 1450`,
      `rcv_pending_max 513 / 725`를 저장했다.
      다만 current realigned `bench_zlink_pubsub.cpp` surface는 이 지표를
      직접 내보내지 않으므로, above queue metrics는 auxiliary diagnostic으로만
      유지한다.
- [x] `PUBSUB` delivery-ready monitor snapshot/reopen contract regression을
      추가했다.
      [`core/tests/integration/monitoring/test_monitor_socket_contract.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/monitoring/test_monitor_socket_contract.cpp)에
      `test_pubsub_delivery_ready_snapshot_and_reopen_after_ready()`를 추가했고,
      baseline current code에서
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
      가 통과했다.
- [x] socket handler / receive callback 활성 중 `zlink_msg_recv()`도
      `EBUSY`를 유지하는 contract regression을 추가했다.
      [`core/tests/integration/test_socket_with_handler.cpp`](/home/hep7/project/kairos/zlink/core/tests/integration/test_socket_with_handler.cpp)에
      `PAIR/DEALER/ROUTER/STREAM/SUB/XSUB`에 대한
      `zlink_msg_recv(..., ZLINK_DONTWAIT) -> EBUSY` 확인을 추가했고,
      `cmake --build core/build -j$(nproc)` 뒤
      `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
      가 통과했다.
- [x] realigned single `PUBSUB` surface 기준으로
      `default HWM + XPUB_NODROP=1` publication/backpressure differential을
      pattern-specific code candidate로 줄인다.
      historical realigned baseline은
      `PUBSUB tcp/inproc 64B -15.29% / -24.92%`,
      `XPUB_NODROP=0 -0.00% / -0.64%`,
      `HWM=16 -16.17% / +47.89%`,
      multi `pubsub tcp 64B default -29.83%`,
      `BENCH_MULTI_PUBSUB_HWM=16 -22.22%`였다.
      다만 2026-03-28 current retained code direct recheck에서는
      single `PUBSUB tcp/inproc 64B`가
      seq1 `-21.73% / -19.43%`,
      rerun `-22.44% / -31.08%`,
      `XPUB_NODROP=0` probe는 `-0.06% / +0.04%`,
      latest multi `pubsub tcp 64B`는 `-22.75%`였다.
      즉 current execution baseline은 위 recheck 값으로 본다.
      `XPUB` same first-part retry matching cache도
      `PUBSUB tcp/inproc 64B -26.40% / -44.32%`로
      `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져
      rejected candidate가 됐다.
      same-thread `activate_write` mailbox 정렬도
      `PUBSUB tcp/inproc 64B -26.81% / -43.47%`로
      `tcp`는 noise 수준, `inproc`은 semantic-map baseline보다 더 나빠져
      rejected candidate가 됐다.
      `pipe::process_activate_write()` already-active fast path도
      first/rerun `PUBSUB tcp/inproc 64B`
      `-9.53% / -28.30%`, `-22.77% / -24.83%`로
      `tcp`와 `inproc` 방향이 다시 엇갈려
      stable isolated win조차 만들지 못해 rejected candidate가 됐다.
      `xpub/xsub` delivery-ready lazy tracking candidate도
      isolated first/rerun `PUBSUB tcp/inproc 64B`
      `-14.47% / -23.50%`, `-14.44% / -29.17%`,
      broader single `PUBSUB tcp/inproc 64B -15.52% / -28.17%`,
      multi `pubsub tcp 64B -20.48%`로
      `tcp` isolated improvement만 보이고 `inproc` rerun과
      broader single / multi guardrail을 동시에 지키지 못해
      rejected candidate가 됐다.
      다만 이 라운드에서 late monitor reopen / snapshot semantic은
      `test_pubsub_delivery_ready_snapshot_and_reopen_after_ready()`로
      current code에 retained regression으로 남긴다.
      current accepted `dist` helper 위
      `XPUB` all-attached empty-prefix `send_to_all()` v2도
      sequential seq1/seq2 `PUBSUB tcp/inproc 64B`
      `-25.77% / -40.89%`, `-23.12% / -40.39%`로
      accepted baseline을 stable하게 넘지 못해 rejected candidate가 됐다.
      `dist.cpp` single-pipe `match()/activated()` bookkeeping fast path도
      seq1/seq2/seq3 `PUBSUB tcp/inproc 64B`
      `-24.81% / -43.41%`, `-22.71% / -34.67%`,
      `-24.42% / -41.68%`로 stable broad win이 아니었다.
      `dist.cpp` final-part same-thread `send_activate_read()` inline wakeup도
      isolated `PUBSUB tcp/inproc 64B`가 `-25.70% / -42.49%`로
      accepted baseline 아래라 rejected candidate가 됐다.
      safe single-pipe `nodrop` fusion도 direct
      `./core/build/bin/test_xpub_nodrop`에서
      heap corruption / invalid msg assert로 bench 전에 rejected 됐다.
      low-HWM single win과 `XPUB_NODROP=0` sign flip은 acceptance가 아니라
      semantic probe 결과로만 유지한다.
      추가 HWM sweep `16/64/256/1000`은
      `+12.34% / +5.19%`, `-32.19% / -38.29%`,
      `-22.05% / -39.03%`, `-28.83% / -45.87%`로 non-monotonic했다.
      queue-probe report에서는 default / `XPUB_NODROP=0` / `HWM=16`이
      `snd_pending_max tcp/inproc 654 / 1450`,
      `508 / 946`, `9 / 20`이었다.
      즉 current `inproc` 잔여 gap은 receiver latency보다
      sender backlog/publication 누적과 더 잘 맞는다.
      `test_xpub_nodrop`는
      `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON` 뒤 current ctest
      surface에 포함됐고,
      `ctest --test-dir core/build --output-on-failure -R '^(test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
      는 현재 통과한다.
      direct binary flake는 historical auxiliary diagnostic으로만 유지한다.
      `pipe.hpp` / `pipe.cpp` / `dist.hpp` / `dist.cpp` / `xpub.cpp`
      single-matching `XPUB_NODROP=1` one-lock helper도
      isolated seq1/seq2/seq3 `PUBSUB tcp/inproc 64B`
      `-27.73% / -38.36%`, `-20.44% / -38.87%`, `-12.75% / -38.71%`,
      broader single `PAIR tcp/inproc -15.37% / -19.27%`,
      `PUBSUB tcp/inproc -21.12% / -26.31%`,
      `DEALER_DEALER tcp/inproc -15.61% / -23.86%`,
      `DEALER_ROUTER tcp/inproc -19.54% / -23.27%`까지는 회복했지만
      multi `pubsub tcp 64B` first/rerun/`--runs 3`가
      `-25.93%`, `-21.63%`, `-25.68%`로 latest baseline `-17.24%`를
      안정적으로 지키지 못했고,
      `PAIR`/`DEALER_DEALER` raw/public도
      `-27.95% -> -34.15%`, `-21.32% -> -24.18%`,
      `-30.30% -> -22.31%`, `-23.02% -> -31.53%`로 mixed여서
      keep-worthy delta가 아니었다.
      no-topic single-part `PUBSUB` blocked retry sync release도
      publish contract 회귀는 통과했지만
      single `PUBSUB tcp/inproc 64B`
      `-38.21% / -36.11%`로 `tcp`가 크게 악화돼 rejected candidate가 됐다.
      `pipe.hpp` / `pipe.cpp` HWM precheck atomic candidate도
      same-day baseline `-25.63% / -37.06%` 대비
      first `-26.71% / -34.02%`, rerun `-23.86% / -37.64%`로
      `tcp`/`inproc`가 다시 엇갈려 stable broad win이 아니어서 원복했다.
      `claude --help`는 통과했지만 이번 단계의 non-interactive consult는
      prompt 전달 실패와 `timeout 20s` 재시도 둘 다 응답 없이 끝나
      unavailable로 기록한다.
      `PERF_SINGLE_QUEUE_SAMPLE_MS=100000` sparse queue-probe semantic run도
      `PUBSUB tcp/inproc 64B -27.48% / -35.63%`여서
      current gap의 본체를 measurement-surface probe overhead로
      설명하진 못했다.
      `pipe.hpp` / `pipe.cpp` / `socket_base_endpoint.cpp` /
      `ctx_inproc_registry.cpp` `inproc PUBSUB` peer-progress notify interval
      tighten candidate도 tests는 통과했지만
      single `PUBSUB tcp/inproc 64B -22.42% / -37.17%`,
      multi `pubsub tcp 64B -26.38%`로 keep-worthy delta가 아니었고,
      `snd_pending_max 963 / 178` 대신 `rcv_pending_max 189 / 1125`로
      backlog 위치만 옮겼다.
      같은 파일군의 HWM-full peer snapshot refresh candidate도
      tests는 통과했지만
      single `PUBSUB tcp/inproc 64B -22.84% / -38.24%`,
      multi `pubsub tcp 64B -22.78%`로 rejected candidate가 됐다.
      `pipe.hpp` / `pipe.cpp` `pipe::read()` peer read-progress direct publish
      candidate는 `_lwm` boundary에서 active peer의 `_peers_msgs_read`를
      direct publish해 `activate_write` command를 건너뛰는 형태였지만,
      bench 전 gate인
      `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop)$' -j1`
      에서 `test_xpub_nodrop`가
      `/home/hep7/project/kairos/zlink/core/tests/integration/test_xpub_nodrop.cpp:383:test:PASS`
      뒤 `***Timeout 10.01 sec`로 hang해 바로 원복했다.
      `xsub.cpp` matching recv의 `last_recv_source_rid` snapshot 제거 candidate도
      gate는 통과했고 first/rerun `PUBSUB tcp/inproc 64B`
      `-20.13% / -28.12%`, `-19.83% / -23.68%`로
      current recheck baseline `-22.44% / -31.08%` 대비
      isolated single은 `tcp/inproc` 모두 개선 신호였지만,
      safe integration 없이 `spot`/public source-rid contract를 바로 줄일 수는
      없어서 단독 candidate로는 원복했다.
      `xsub` empty-subscription accept-all fast path도
      gate는 통과했고 first/rerun `PUBSUB tcp/inproc 64B`
      `-16.33% / -20.00%`, `-15.20% / -27.16%`로
      current recheck baseline `-22.44% / -31.08%` 대비
      first/rerun 모두 `tcp/inproc`를 함께 끌어올린 이번 라운드의 가장 강한
      receiver-drain 신호였지만,
      broader single / multi guardrail을 확인하기 전에는 단독 retained delta로
      승격하지 않았다.
      그 뒤 `xsub.hpp` / `xsub.cpp` empty-subscription accept-all fast path와
      `socket_base.hpp` / `socket_base_dispatch.cpp` /
      `spot_sub_recv.cpp` requested-only source-rid capture scope를 결합해
      current `spot`/public source-rid contract를 유지한 채
      `xsub::xrecv()` normal steady-state의 snapshot cost를 on-demand로만
      남기도록 재구성했다.
      targeted gate
      `ctest --test-dir core/build --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop|test_spot_pubsub_scenario)$' -j1`
      는 통과했고, isolated single first/rerun
      `PUBSUB tcp/inproc 64B`
      `-9.40% / -20.35%`, `-10.43% / -21.59%`,
      broader single
      `PAIR tcp/inproc -16.64% / -21.71%`,
      `PUBSUB tcp/inproc -11.57% / -20.78%`,
      `DEALER_DEALER tcp/inproc -26.40% / -21.90%`,
      `DEALER_ROUTER tcp/inproc -24.17% / -18.30%`,
      `ROUTER_ROUTER tcp/inproc -55.78% / -21.48%`,
      multi `pubsub tcp 64B` smoke first/rerun `+9.25%`, `+8.25%`를 확인했다.
      즉 current recheck baseline 대비 `PUBSUB tcp`와 multi `pubsub`는
      stop-condition 수준까지 줄였고, `inproc`도 함께 회복했으므로
      이 단계의 retained delta는 `xsub` receiver-drain specialization으로
      승격한다.
      다만 이 단계에서 확인한 `zlink_msg_recv(...)=EBUSY` contract와
      late monitor reopen / snapshot contract는 각각
      `test_socket_with_handler`, `test_monitor_socket_contract`
      regression으로 계속 유지한다.
      이번 단계의 `claude -p` non-interactive consult는
      `Input must be provided either through stdin or as a prompt argument when using --print`
      오류로 usable advisory를 얻지 못해 unavailable로 기록한다.
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
