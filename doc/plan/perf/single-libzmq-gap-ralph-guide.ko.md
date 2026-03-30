# `single-libzmq` 성능 수렴 Ralph 실행 가이드

## 1. 목적

이 가이드는
[single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
의 과거 review/log 맥락을 참고 자료로만 두고,
상세 iteration 기록은
[logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
아래 개별 markdown 파일로 분리하면서,
[`core/bench/with_zmq/`](/home/hep7/project/kairos/zlink/core/bench/with_zmq)
를 기준 검증 surface로 사용해
`zlink`의 residual gap을
`single` diagnostic track과 `multi` workload track으로 분리해
검증/수렴하는 반복 작업을 끝까지 수행하기 위한 유일한 실행 문서다.

authority와 실행 규칙은 이 문서 하나에 고정한다.
다만 상세 iteration 실험 기록은 `logs/iterations/` 아래 개별 파일로 분리해
현재 우선순위와 과거 상세 히스토리를 섞지 않는다.

이 작업에서 POSD는 설계 판단의 보조 원칙이지, 성능 질문을 흐리는 장식이
아니다. current round의 1차 목표는 모든 숫자를 한 번에 복원하는 것이 아니라,
어떤 residual이 `single` hot-path diagnostic에만 남는지,
어떤 residual이 `multi` workload loss로 실제 번역되는지 분리하는 것이다.
따라서 current contract와 correctness를 유지하는 범위 안에서는,
더 빠르고 더 단순한 hot-path 구조 복원을 시도하되,
그 개선이 어느 track의 질문에 답하는지도 함께 명시해야 한다.

## 2. 권한과 로그

- 이 문서가 유일한 authority다.
- [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  는 authority가 아니라 short review 로그다.
- 다음 랄프루프 iteration은 review 문서와 무관하게 이 guide만 기준으로
  진행한다.
- 상세 iteration 기록은
  [logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
  아래 개별 markdown 파일로 남긴다.
- [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  는 hot-path 계약 문서다.
- historical regression source는 아래 두 문서다.
  - [with-zmq-regression-bisect-report.ko.md](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-report.ko.md)
  - [with-zmq-regression-bisect-log.ko.md](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-log.ko.md)
- historical good reference oracle은
  `/home/hep7/project/kairos/zlink-perf-regression-bisect` worktree의
  2026-03-05 good 상태(`7bea9e3f`)다.
  다음 structural round는 이 good state를 rollback 대상으로 보지 않고,
  current HEAD가 어떤 의미 단위를 추가로 떠안았는지 비교하는 설계 기준으로
  사용한다.
- 이 reference oracle을 사용할 때도 current HEAD의 thread-safe 계약,
  close/callback/public multipart 의미, 그리고 POSD 기반의 더 깊은 모듈
  구조는 유지해야 한다.
  즉 목표는 3/5 코드 복제가 아니라, 3/5가 갖고 있던 더 얇은 hot-path 의미를
  current contract 안에서 복원하는 것이다.
- 다만 위 원칙은 throughput 회복보다 우선하지 않는다.
  POSD는 방향성일 뿐이고, current correctness/thread-safe/public contract를
  깨지 않는 범위에서는 historical good 수준에 더 가까운 구조를 우선 채택한다.
- 필요하면 `/home/hep7/project/kairos/zlink-perf-regression-bisect` worktree에서
  같은 `with_zmq` single 조건을 다시 돌려 current HEAD와 비교/확인해도 된다.
  다만 그 수치는 current HEAD acceptance 기준이 아니라,
  reference oracle 확인용 diagnostic으로만 사용한다.
- 각 iteration은
  [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
  를 다시 읽는 것으로 시작한다.
- iteration 시작 시에는 이 guide 본문과 `hot-path` 계약만 먼저 읽는다.
- 새 iteration은 detailed iteration 로그를 처음부터 다시 읽지 않는다.
  먼저 guide와 hot-path를 읽고, 특정 rejected family나 kept delta를
  확인해야 할 때만
  `logs/iterations/` 아래 해당 파일을 역참조한다.
- guide나 `hot-path` 중 하나라도 현재 코드/해석/우선순위와 어긋나면 즉시 갱신한다.
- 실제 변경이 없더라도, iteration 결과가 이 guide와 `hot-path` 내용과 일치하는지
  확인하지 않으면 다음 iteration으로 넘어가면 안 된다.
- 짧은 summary가 긴 로그보다 우선한다.
  상세 실험 기록은 `logs/iterations/` 아래에서만 관리한다.
- 별도 main/master/gap/residual/spec 문서는 추가로 만들지 않는다.
- 이 루프의 기본 동작은 `--max-iterations 0`, 즉 목표 완료까지 무한 반복이다.
- 반복 횟수를 제한하고 싶을 때만 명시적으로 `--max-iterations <N>`을 넘긴다.
- 같은 wrapper scope에 대해 supervisor는 하나만 유지한다.
  새 실행을 시작할 때 같은 `guide + logs-dir + gate-label` 범위의 기존
  supervisor와, 같은 wrapper scope에 남아 있는 child `codex exec`가 있으면
  먼저 정리하고 새 세션으로 시작한다.
- 이 wrapper의 기본 실행 모델은 `gpt-5.4`이고,
  기본 `model_reasoning_effort`는 `medium`이다.
  별도 override를 주지 않으면 이 조합으로 랄프루프가 돈다.

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
- 상세 iteration 로그:
  [logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
- loop runtime 로그는 [`doc/plan/perf/logs/`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs)
  아래에 쌓는다.

### 4.3 주 검증 surface

- single diagnostic:
  [`core/bench/with_zmq/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/run_comparison.py)
- multi primary:
  [`core/bench/with_zmq/multi/run_comparison.py`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/multi/run_comparison.py)

상위 shell runner는 최종 aggregate artifact가 필요할 때만 사용한다.
그 경우에도 반드시 `--reuse-build`를 붙인다.

## 5. 성능 목표

운영 목표는 하나가 아니라 두 track으로 나뉜다. 다만 current primary
execution target은 `multi dealer_dealer`다.

- `single` track:
  internal hot-path fixed cost와 local transport residual을 진단한다.
- `multi` track:
  실제 topology에서 product-facing competitiveness를 판단한다.

즉 current loop는
"single 숫자를 모두 줄이는 것"과
"실제 workload 경쟁력을 지키는 것"을 같은 gate로 묶지 않는다.

### 5.1 `single` track 목표

- `single`은 diagnostic surface다.
- keep/reject authority는 relative diff가 아니라
  same-day prepatch 대비 `zlink absolute throughput`이다.
- `single` candidate의 목적은 broad product claim이 아니라
  target pattern의 fixed cost를 재현 가능하게 줄이는 것이다.

### 5.2 `multi` track 목표

- `multi`는 product-facing workload surface다.
- current primary target workload는 `multi dealer_dealer`, 특히
  `tcp/ipc 64B~1KB` 구간이다.
- 최종 retain 판단은 `multi` target에서 broad win 또는 clear no-regression까지
  확인되어야 한다.
- `single`에서만 좋아지고 `multi`에 번역되지 않는 patch는
  diagnostic value는 남겨도 retained code로는 두지 않는다.
- 따라서 current round의 candidate 선택은
  `multi dealer_dealer` improvement potential을 먼저 설명할 수 있어야 한다.

### 5.3 candidate class 기대치

- `micro` candidate:
  call elision, duplicate branch 제거, metadata/flag/check floor shaving
- `structural` candidate:
  `pipe`, `mailbox`, wake/activation ownership, public runtime/lifecycle boundary
  재배치
- `micro`와 `structural`은 같은 기대치로 평가하지 않는다.
  `micro`는 narrow target에서 작아도 재현 가능한 개선이면 의미가 있고,
  broad 큰 폭 개선은 주로 `structural`에서 기대한다.

### 5.4 baseline / keep 판정

- keep/reject 판정은 항상 아래 순서를 따른다.
  1. same-day prepatch baseline
  2. patch 적용 후 같은 명령 rerun
  3. 필요하면 즉시 rerun으로 재현성 확인
- 탐색 단계의 `runs=1` 결과는 candidate triage 용도로만 쓴다.
- keep 판정은 기본적으로 `runs>=3` 반복 측정으로 올린다.
- baseline이 흔들릴 때는 relative diff보다
  `zlink absolute throughput`과 same-day prepatch 대비 변화량을 우선한다.

### 5.5 secondary guardrail

- primary acceptance는 `multi dealer_dealer` target 구간 improvement다.
- 그 위 guardrail로
  multi `dealer_router`, `router_router`, `pubsub`의 `tcp 64B`가
  기존 best 대비 `5%` 이상 퇴행하면 retain하지 않는다.
- 개선 중인 패턴이 아닌 다른 패턴에서 기존 best 대비 `5%` 이상 퇴행하면
  종료로 처리하지 않는다.

### 5.6 raw/public 분리 guardrail

send-path를 건드린 iteration 뒤에는 반드시 `PAIR`, `DEALER_DEALER`의
raw/public 분리를 다시 찍는다.

- `zlink raw - zlink public`이 다시 커지면 public surface penalty가 재도입된 것이다.
- 이 경우 gap 해석을 다시 써야 하므로 로그 업데이트 없이 다음 단계로 넘어가면 안 된다.

## 6. 반복 루프

모든 iteration은 아래 순서로 진행한다.

1. 이 가이드 전체를 읽는다.
2. [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
   최신 상태를 읽는다.
   상세 iteration 기록은 `logs/iterations/` 아래에서 현재 family와 직접 관련된
   파일만 역참조한다.
3. 이 guide를 기준으로 현재 top hypothesis 하나만 고른다.
4. historical concrete change map을 사용할 때는 위 bisect report/log도
   먼저 확인한다.
5. 필요한 경우 `/home/hep7/project/kairos/libzmq` 대응 구현을 먼저 읽고,
   현재 후보의 semantic / ordering / hot-path work 차이를 짧게 정리한다.
   특히 next round 시작점은 아래 historical concrete change map이다.
   - `ff0140e5`: `pipe::_out_sync` steady-state serialization 추가
   - `a819ea3a`: `socket_base_t::send()` public admission/CAS 추가
   - `98e7d324`: public multipart `zlink_send/zlink_recv` 도입
   - `9b91234c`: bench hot-loop activation + `PERF_SINGLE_MAX_INFLIGHT` 제거
   새 iteration은 이 4개 변화 중 어느 의미가 current HEAD까지 남아 있는지
   먼저 짧게 정리한 뒤에만 code candidate를 선택한다.
   그리고 code candidate는 반드시
   `/home/hep7/project/kairos/zlink-perf-regression-bisect`
   3/5 good state(`7bea9e3f`) 대비
   `send entry / pipe duty / public surface / sender regime`
   중 어떤 의미를 current contract 안에서 얇게 복원하는지까지 함께 적어야 한다.
   필요하면 같은 단계에서 `zlink-perf-regression-bisect` worktree의
   `with_zmq` single rerun으로 reference oracle 쪽 수치를 다시 확인하되,
   그 결과는 acceptance가 아니라 원인 비교/설계 검증 근거로만 기록한다.
6. 새 단계나 새 candidate family로 넘어가기 전에는 `claude` 의견도 한 번
   수렴한다. 목적은 authority를 바꾸는 것이 아니라, 현재 가설을 다른 시각에서
   검토해 local search drift를 막는 것이다.
7. `core/`와 `core/tests/`를 우선 수정한다.
8. [`core/build/`](/home/hep7/project/kairos/zlink/core/build)로 빌드한다.
9. 영향 패턴의 targeted `multi dealer_dealer` 벤치를 먼저 돌린다.
10. `multi dealer_dealer`에서 의미 있는 개선이 보이면
    targeted `single dealer_dealer`와 raw/public 분리를 진단용으로 다시 확인한다.
11. 개선이 유지될 때만 broader `multi`와 필요한 `single` diagnostic을 수행한다.
12. 상세 결과 파일 경로, 숫자, 해석, 배제한 가설은
   [logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
   아래 새 markdown 파일로 기록한다.
13. 그 iteration의 kept/rejected 결론과 `Next Exact Step` 영향은
   이 guide와 해당 iteration log에 반영한다.
14. 현재 iteration 결과가
    [hot-path.ko.md](/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md)
    와도 일치하도록 계약/주의점/우선순위/배제 후보를 반영하거나,
    변경이 없음을 확인한다.
15. 아직 stop condition을 못 만족하면 다음 미해결 가설로 반복한다.

- 새 iteration은 먼저 이번 candidate가 `single` diagnostic용인지,
  `multi` workload용인지, 아니면 `single -> multi` 번역 검사용인지 적는다.
- 별도 강한 반증이 없으면 current top hypothesis는
  `multi dealer_dealer` 우선으로 잡는다.
- `single`은 current round에서 primary acceptance surface가 아니라
  `multi dealer_dealer` 결과를 해석하기 위한 diagnostic 보조 surface다.
- `single`에서 mixed/noise가 반복되는 `micro` candidate는
  같은 family에서 길게 끌지 않는다.
- 아래 둘 이상이 동시에 성립하면 current `single micro` round를 종료하고
  `structural-only` round로 전환한다.
  - 연속된 `micro` candidate 3개 이상이 mixed/noise reject
  - `single` gap이 `multi` target workload에 번역되지 않음
  - patch가 one-pattern win / one-pattern regression으로 반복됨
- `multi` target이 이미 competitive이고
  남은 gap이 `single` local diagnostic 의미만 가지면,
  `single` 숫자 chase를 종료하고 product-facing target으로 우선순위를 옮긴다.

- `guide/review/hot-path` reset iteration은 `6.2` trigger당 한 번만 허용한다.
  현재 reset이 이미 review summary에 기록되어 있으면, 다음 iteration은
  반드시 `core/` structural candidate patch 1개와 targeted guardrail을
  포함해야 한다.
- authority 문서들 사이에 실제 모순이 새로 생긴 경우를 제외하고,
  문서만 수정하고 끝나는 iteration은 금지한다.
- structural round의 code candidate는 한 iteration에 하나만 허용한다.
  candidate를 고른 뒤에는 broad single 또는 targeted public/raw guardrail까지
  돌려서 accept/reject를 결정해야 한다.

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
- reset iteration이 한 번 끝나고
  [single-libzmq-gap-review.ko.md](/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md)
  summary에 새 `Next Exact Step`이 기록되면,
  그 다음 iteration은 다시 reset을 반복하지 않는다.
  그때부터는 반드시 `core/` structural patch 1개와 targeted guardrail을
  수행해야 한다.

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

1. historical first direct cause는
   `raw send_exact/zlink_msg_recv -> public zlink_send/zlink_recv` surface
   전환이라는 bisect 결론을 기준선으로 유지한다.
   이 가설을 뒤집는 새 증거가 없으면, 랄프루프는 다시 "wrapper가 본체인지"
   를 처음부터 재탐색하지 않는다.
   다만 이 historical collapse를 만든 실제 hot-path 변화는
   `ff0140e5`, `a819ea3a`, `98e7d324`, `9b91234c`의 조합이라는 점을
   함께 유지한다.
2. current first-priority implementation target은
   [`core/src/sockets/socket_base_msg.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base_msg.cpp)
   /
   [`core/src/sockets/socket_runtime.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_runtime.cpp)
   kept boundary 위의 `xsend_initial` / public multipart-sender-regime ordering과
   [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
   의 `_out_sync` 아래 same-ordering publication work,
   그리고 routed/source-rid export differential이다.
   `a819ea3a` admission floor는 historical input으로 유지하되,
   latest send-scope split diagnostics 이후 immediate lifecycle fast-path
   target에서는 내린다.
   2026-03-28 current tree에는
   [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
   /
   [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
   의
   `write_message_unlocked()/rollback_unlocked()/flush_unlocked()` helper로
   `_out_active`, `_peers_msgs_read`, `_state`, `_out_pipe`
   outbound invariant map이 이미 고정돼 있다.
   다음 랄프루프는 이 추상을 다시 적는 단계가 아니라,
   먼저 `ff0140e5` `_out_sync` publication duty와
   `98e7d324/9b91234c` public multipart-sender-regime 흔적,
   routed/source-rid export differential을 current HEAD에서
   어떻게 줄일 수 있는지 보는 데서 시작한다.
   가장 최근
   `pipe.hpp` / `pipe.cpp` `process_activate_write()` already-active
   peer-progress snapshot split candidate는
   targeted `PAIR` / `DEALER_DEALER` public/raw rerun까지는 회복했지만,
   broader single `DEALER_DEALER inproc -29.32%`,
   `DEALER_ROUTER inproc -30.93%`, partial `ROUTER_ROUTER tcp -52.69%`와
   `comp_zlink_router_router zlink inproc 64` hang을 만들어 원복했다.
   같은 family의 `process_activate_write()` atomic peer-progress publish
   candidate도 targeted public
   `PAIR tcp/inproc -22.80% / -18.39%`,
   `DEALER_DEALER tcp/inproc -35.30% / -19.86%`,
   raw `PAIR tcp/inproc -23.83% / -31.74%`,
   `DEALER_DEALER tcp/inproc -11.45% / -15.34%`로
   targeted stage부터 keep-worthy broad win이 아니어서 원복했다.
   이어서 `DEALER` same-handle send serialization을
   `public_api_sync` 밖 external recursive mutex +
   external `socket_public_send_scope_t` serialized scope로 옮기는
   candidate도 시도했지만,
   public `PAIR tcp/inproc -9.23% / -16.03%`,
   `DEALER_DEALER tcp/inproc -13.09% / -32.97%`,
   raw `PAIR tcp/inproc -13.88% / -26.40%`,
   `DEALER_DEALER tcp/inproc -24.35% / -33.20%`로
   targeted stage부터 keep-worthy broad win이 아니어서 원복했다.
   이어서 existing public send sync가 이미 잡힌 `DEALER` caller에서
   final `write+flush`만 `_out_sync` 밖 pipe hot-send lease로 보내고
   rare `_out_pipe` mutation이 inflight send를 기다리게 하는 candidate도
   시도했지만,
   targeted public `PAIR tcp/inproc -28.83% / -19.45%`,
   `DEALER_DEALER tcp/inproc -15.40% / -22.79%`로
   public stage부터 keep-worthy broad win이 아니어서 원복했다.
   이어서
   [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
   recursive `fast_mutex_t`를 native recursive pthread mutex로 바꾸는
   primitive replacement candidate도 시도했지만,
   stream/contract smoke는 rebuild 뒤 통과했어도,
   targeted public `PAIR tcp/inproc -27.78% / -17.52%`,
   `DEALER_DEALER tcp/inproc +3.72% / -21.03%`,
   raw `PAIR tcp/inproc -13.25% / -21.63%`,
   `DEALER_DEALER tcp/inproc -7.74% / -15.86%`로
   unchanged control인 `PAIR public tcp`와 raw `PAIR inproc` guardrail을
   함께 지키지 못해 원복했다.
   이어서 current kept boundary 위에서
   common data-plane admission을 full public lifecycle coordinator 아래의
   dedicated public send lease로 다시 가르는 structural candidate도
   시도했지만,
   authority public rerun
   `PAIR tcp/inproc -15.65% / -25.43%`,
   `DEALER_DEALER tcp/inproc -24.41% / -32.10%`로
   baseline보다 더 악화돼 원복했다.
   latest env-gated send-scope split diagnostics
   [`pair_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_send_scope_profile_20260329.txt)
   /
   [`dealer_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_send_scope_profile_20260329.txt)
   에서는 no-sync `PAIR`
   `enter_public_api/leave_public_api 49.70/50.01 ticks`,
   sync-fast `DEALER_DEALER`
   `enter_public_api_and_lock_sync_fast/unlock_public_api_sync_and_leave`
   `49.66/49.67 ticks`,
   ctor/dtor total `174.36/175.98`, `174.80/176.78`만 확인됐다.
   즉 earlier `socket_scope_construct ~1266/1314 ticks` bucket은
   lifecycle atomics 단독이 아니었고,
   another admission-floor-only family를 immediate next candidate로
   두면 안 된다.
   따라서 다음 라운드는
   `process_activate_write()` peer-progress family,
   existing public-send-sync-held `send_serialized` helper family,
   `DEALER` external send-state mutex / external send-serialized scope family,
   existing public-send-sync-held pipe hot-send lease / outpipe lifetime split
   family,
   `fast_mutex.hpp` native recursive pthread primitive replacement family,
   dedicated public send lease split family,
   `public_api_inflight/public_api_closing/public_api_sync` split family,
   shared `public_api_state` public/send inflight lane split family,
   `public_api_sync` recursive mutex-backed split family,
   plain final-part sender-regime split family,
   `activate_write` progress-command coalesce family를
   반복하지 않고,
   send-scope construct + pipe serialization 구조를 함께 다시 고른다.
   다만 2026-03-28 현재 이 common send-side structural family는
   `process_activate_write()` snapshot/atomic,
   existing public-send-sync-held `send_serialized`,
   `DEALER` external send-state mutex/external serialized scope,
   existing public-send-sync-held hot-send lease/outpipe lifetime split,
   `fast_mutex.hpp` native recursive pthread primitive replacement,
   dedicated public send lease split,
   `public_api_inflight/public_api_closing/public_api_sync` split,
   shared `public_api_state` public/send inflight lane split,
   `public_api_sync` recursive mutex-backed split,
   `pipe.cpp` final-part `write_and_flush()` lock-free snapshot split,
   `pipe::_out_sync` plain non-recursive fast mutex split,
   send-side layout regroup,
   preflight-before-public-admission split,
   stronger-gate `public_api_inflight/public_api_closing/public_api_sync`
   split recheck,
   plain final-part sender-regime split,
   `activate_write` progress-command coalesce split까지
   broad win을 만들지 못했다.
   current implementation priority는 another lifecycle fast path가 아니라
   `xsend_initial` / `pipe::_out_sync` publication floor와
   `98e7d324/9b91234c` public multipart/sender-regime,
   routed/source-rid export differential 쪽으로 내린다.
   current kept boundary + direct profile reread와
   serial current-tree public/raw refresh 재확인은 이미 다시 수행했다.
   다만 late-session refresh/rerun이 earlier authority보다 더 낮은
   session-local baseline을 반복했으므로,
   immediate next round는 이 둘을 모두 guardrail로 유지한 채
   새 broad hypothesis 하나를 다시 열고
   new common send-side structural candidate 1개를 고르는 단계다.
   signal이 섞이면 candidate keep/reject 전에
   current-tree serial refresh를 먼저 다시 찍는다.
   이때 candidate는 반드시
   `7bea9e3f` good state 대비 무엇이 더 두꺼워졌는지 설명 가능해야 하며,
   thread-safe 계약 약화나 POSD 위반을 대가로 삼으면 안 된다.
3. 현재 accepted `PAIR` / `DEALER` 공통 delta를 기준선으로 유지하고,
   broad win 근거 없는 공통 미세 후보는 다시 파지 않는다.
4. `PUBSUB`는 code optimization 전에 semantic/backpressure map을 먼저 만든다.
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
5. `PUBSUB` semantic map 이후에만
   - semantic differential
   - core publication/lifecycle residual
   을 분리해 다음 code candidate를 고른다.
6. `ROUTER_ROUTER`는 common send-side pass와 `PUBSUB` semantic map이 끝난
   뒤에 본다.
7. 남아 있는 recv-side routed / strip / multipart export 경로는 마지막 단계로 둔다.

2026-03-28 현재 loop는 `ROUTER` local helper나 `PUBSUB` local helper보다,
이분 탐색이 잡아낸 common residual:
`send admission/lock + pipe serialization`을 먼저 본다.
새로운 broad win 근거가 나오기 전까지는 raw/public guardrail과 broader single
acceptance를 동시에 만족한 공통 send-side 후보만 먼저 올린다.

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
단, loop 내부 iteration에서 same scope wrapper를 다시 실행하면
existing supervisor 정리 로직과 충돌할 수 있으므로,
이 smoke는 `bash -n`과 `--help`까지만 허용한다.
같은 loop 안에서 `--init-only`나 실제 wrapper 재실행은 금지한다.

```bash
bash -n doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh \
  core/tools/ralphloop/run_codex_execution_guide_loop.sh \
  core/tools/ralphloop/run_execution_gate_loop.sh
```

```bash
./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh --help
```

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

현재 workspace에서는 `ctest --test-dir core/build -N`가
`core/tests` + sample/cpp contract를 함께 포함한 103개를 직접 열거한다.
`core/tests` regression/contract suite authority enumeration과 targeted regex
gate는 build artifact root를 `core/build`에 유지한 채 아래처럼
`core/build/core` test root를 사용해 확인한다.

```bash
ctest --test-dir core/build/core --output-on-failure -N
```

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
에 최소한 아래 short summary만 남긴다.

- 작업한 가설 1개
- candidate family 1개
- 왜 이 후보가 high-leverage 또는 semantic probe인지 한 줄 근거
- 참고한 `libzmq` 대응 파일
- `claude` consult 여부와 핵심 조언 1~3줄
- 수정한 파일 경로
- 실행한 명령
- 핵심 수치
- 유지한 변경 / 원복한 변경
- 다음 iteration 우선순위

rejected candidate는 반드시 로그에 남긴다.
같은 실패 실험을 이유 없이 반복하지 않는다.

상세 실험 기록은 항상
[logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
아래 새 markdown 파일로 남긴다.
파일명은 `YYYYMMDD-HHMM-<short-family>.ko.md` 형식을 사용한다.

새 iteration은 review 메인 파일의 오래된 본문을 처음부터 다시 읽지 않는다.
먼저 summary를 읽고, 필요한 family가 있을 때만 해당 iteration 로그 파일을
역참조한다.

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

이 절은 **현재 활성 레지스터 요약만** 유지한다.
상세 kept/rejected 실험 본문은
[logs/iterations/](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/iterations)
아래 개별 파일로 남기고, 새 iteration은 이 절 아래의 오래된 상세 본문을
처음부터 다시 읽지 않는다.

현재 활성 레지스터:
- common residual direct cause는
  `enter_public_api`가 포함된 common send scope construct floor와
  `_out_sync` write/flush serialization floor의 조합이다.
- 2026-03-30 `socket_base_msg.cpp` / `socket_runtime.cpp`
  direct single-part `send_fast_or_retry` candidate는
  same-day baseline 대비 public `PAIR tcp 3262.67 -> 2971.35 Kmsg/s` 악화와
  public/raw `DEALER_DEALER`, raw `PAIR`의 mixed result만 남겨 reject됐다.
  success-path scope object elision alone은 current broad candidate가 아니다.
- 2026-03-30 `socket_base_msg.cpp` `prepare_direct_send_message()` already-clean
  single-part no-op candidate도 same-day baseline 대비
  public `PAIR tcp/inproc 2861.95/2808.45 -> 3200.24/2972.41 Kmsg/s`,
  `DEALER_DEALER tcp/inproc 3230.05/2775.25 -> 3160.29/3007.07 Kmsg/s`,
  raw `PAIR tcp/inproc 2965.60/3441.14 -> 3131.84/2891.68 Kmsg/s`,
  `DEALER_DEALER tcp/inproc 3157.40/3041.54 -> 3048.70/3054.35 Kmsg/s`로
  `PAIR` public 일부만 좋아지고 raw `PAIR inproc`, `DEALER_DEALER tcp`가 함께
  악화돼 reject됐다. same-entry message reset micro-tuning alone도 current broad
  candidate가 아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp`
  `process_activate_read()` / `_in_active` split candidate도 same-day baseline
  대비
  `PAIR tcp/inproc 2981.66/3028.21 -> 3235.39/3087.14 Kmsg/s`,
  `PUBSUB tcp/inproc 2357.77/2029.99 -> 2221.61/2354.50 Kmsg/s`,
  `DEALER_DEALER tcp/inproc 3261.54/2935.31 -> 2972.66/3037.63 Kmsg/s`로
  `PAIR`만 좋아지고 `PUBSUB tcp`, `DEALER_DEALER tcp`가 함께 내려가
  reject됐다. inbound activation local split alone도 current broad candidate가
  아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp` same-order atomic credit publication
  candidate도 same-day authority baseline 대비
  public `PAIR tcp/inproc 2995.80/2839.52 -> 2816.88/3254.53 Kmsg/s`,
  `PUBSUB tcp/inproc 2123.02/2082.10 -> 2091.22/2185.06 Kmsg/s`,
  `DEALER_DEALER tcp/inproc 2999.91/2717.38 -> 2783.18/3049.99 Kmsg/s`로
  `inproc` 일부만 좋아지고 `PAIR tcp`, `PUBSUB tcp`,
  `DEALER_DEALER tcp`가 함께 내려가 reject됐다.
  `_peers_msgs_read/_msgs_written` credit publication만 atomic으로 떼는
  local ownership split alone도 current broad candidate가 아니다.
- 2026-03-30 non-thread-safe owner-thread `pipe` send candidate도
  same-day baseline
  [`perf_linux_20260330_082101_codex_20260330_owner_thread_pipe_send_baseline_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260330_082101_codex_20260330_owner_thread_pipe_send_baseline_public.txt)
  을 다시 찍은 뒤
  `pair.cpp` / `lb.cpp` / `router.cpp` / `dist.cpp`가
  `_out_sync` 없이 owner-thread `write/flush` helper를 쓰도록 올렸지만,
  authority gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  에서 `test_public_inproc_multipart_send`가
  `Assertion failed: check () (.../msg.cpp:579)`로 abort해
  correctness 단계에서 바로 reject됐다.
  caller-owned non-thread-safe serialization reuse alone도
  current broad candidate가 아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp` activation + credit atomic lane split
  candidate는 `_out_pipe` lifetime / termination은 `_out_sync` 아래에 두고
  `process_activate_read()` / `process_activate_write()` /
  `_peers_msgs_read` publication만 atomic lane으로 떼는 structural round였다.
  targeted gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  는 통과했고,
  same-day `multi dealer_dealer tcp 64B`는 `-17.28% -> -11.32%`까지
  회복했다.
  하지만 single public/raw는
  `DEALER_DEALER public tcp/inproc -26.82% / -28.76%`,
  `PAIR public tcp/inproc -29.83% / -28.31%`,
  raw `PAIR tcp/inproc -20.69% / -24.67%`,
  raw `DEALER_DEALER tcp/inproc -24.98% / -25.98%`로 mixed였고,
  broader multi guardrail에서도 `router_router tcp 64B -6.74%`가 남아
  retain하지 않고 전부 원복했다.
  activation + credit publication만 atomic으로 떼는 combined family도
  current broad candidate가 아니다.
- 2026-03-30 `socket_base_msg.cpp` `DEALER` single-part public send만
  `public_api_sync`를 우회하는 sender-regime relax candidate도
  targeted regression gate는 통과했고
  `multi dealer_dealer tcp 64B 1954.17 -> 1590.91 Kmsg/s (-18.59%)`,
  `single DEALER_DEALER tcp 64B 3636.79 -> 3140.98 Kmsg/s (-13.63%)`로
  일부 회복했지만,
  `single DEALER_DEALER inproc 64B 4171.98 -> 2830.89 Kmsg/s (-32.15%)`가
  same-day baseline보다 더 나빠져 reject됐다.
  `DEALER` single-part public sync bit 하나만 빼는 local sender-regime split도
  current broad candidate가 아니다.
- 2026-03-30 `socket_base_msg.cpp` / `multipart_send_txn.cpp`
  logical multipart continuation entry-poll reuse candidate도
  `send_scoped()` first frame 뒤 continuation frame만
  same public send scope에서 entry `process_commands(0, true)`를
  건너뛰게 했지만,
  targeted gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  는 통과한 반면 primary `multi dealer_dealer tcp 64B`가
  same-day authority baseline `1989.95 -> 1603.51 Kmsg/s (-19.42%)` 대비
  `1960.62 -> 1589.48 Kmsg/s (-18.93%)`로
  keep-worthy broad win을 만들지 못해 전부 원복했다.
  logical multipart continuation에서 entry poll만 재사용하는
  sender-regime family도 current broad candidate가 아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp`
  publication gate split candidate도
  steady-state send path가 `_state == active` enum을 직접 읽는 대신
  publication cluster 전용 gate만 보도록
  `_out_sync` 아래 publication readiness를 별도 bool로 분리했지만,
  targeted regression gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  는 통과한 반면 primary `multi dealer_dealer tcp 64B`가
  `1971.48 -> 1594.24 Kmsg/s (-19.13%)`로
  same-day authority baseline
  `1989.95 -> 1603.51 Kmsg/s (-19.42%)`
  대비 `zlink absolute throughput`이 `9.27 Kmsg/s` 낮아
  keep-worthy broad win을 만들지 못해 전부 원복했다.
  publication gate split alone도 current broad candidate가 아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp` lifecycle lock/snapshot split candidate도
  `_out_sync`에는 publication cluster만 남기고
  `_state/_delay/_in_active`를 separate lifecycle exclusion + atomic snapshot으로
  옮기려 했지만,
  targeted regression gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  는 통과한 반면 primary `multi dealer_dealer tcp 64B`가
  `1972.07 -> 1561.22 Kmsg/s (-20.83%)`로
  same-day authority baseline
  `1989.95 -> 1603.51 Kmsg/s (-19.42%)`보다 더 악화돼 reject됐다.
  lifecycle lock/snapshot split family도 current broad candidate가 아니다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp`
  lifecycle/activation atomic split candidate도
  `_state/_delay/_in_active`를 atomic lifecycle state로 떼고
  `process_activate_read()`와 read-side state check를 `_out_sync` 밖으로
  보내려 했지만,
  targeted regression gate
  `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
  는 통과한 반면 primary `multi dealer_dealer tcp 64B`가
  `1983.90 -> 1606.12 Kmsg/s (-19.04%)`로
  same-day authority baseline
  `1989.95 -> 1603.51 Kmsg/s (-19.42%)`
  대비 개선 폭이 `0.38%p`에 그쳐 keep-worthy broad win을 만들지 못했다.
  atomic field split alone도 current broad candidate가 아니므로 전부 원복했다.
- 2026-03-30 reference reread 결과,
  current [`core/src/core/pipe.hpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.hpp)
  /
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink/core/src/core/pipe.cpp)
  의 `_out_sync`는 아직
  `write/flush + _out_pipe + _out_active + _peers_msgs_read` publication cluster와
  `process_activate_read()/process_pipe_term()/process_delimiter()/set_nodelay()`
  lifecycle/activation cluster를 같은 lock domain에 묶어 둔다.
  반면 `7bea9e3f` good state와 current libzmq `pipe.cpp`는
  steady-state `write()/flush()`에서 같은 lifecycle coupling이 훨씬 얇다.
  따라서 next broad hypothesis는 helper-level send micro-tuning이나
  local sender-regime tweak가 아니라,
  `_out_sync` 아래 publication state와 lifecycle/activation state를
  더 큰 의미 단위로 ownership split할 수 있는지 보는 family로 다시 좁힌다.
  다만 just-tried lifecycle lock/snapshot split도 same-day baseline보다 더
  악화됐으므로, 다음 round는 separate lifecycle lock 도입 자체를 반복하지 않고
  termination/delimiter cleanup ordering을 publication duty와 함께
  더 크게 재배치하는 구조만 본다.
- 같은 날 `claude --help`는 확인됐지만,
  non-interactive consult는 `--print` stdin/prompt 요구 오류로 usable output을
  얻지 못했다.
  따라서 current candidate 선택 근거는
  current tree / `7bea9e3f` / libzmq code reread를 우선한다.
- 2026-03-30 validation surface realignment 결과,
  `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON` 뒤
  current root enumeration
  `ctest --test-dir core/build -N`는 `core/tests` + sample/cpp contract를 함께
  포함한 103개를 직접 열거했고,
  actual `core/tests` authority suite는
  `ctest --test-dir core/build/core -N`에서 87개로 확인됐다.
  따라서 current regression gate는 build artifact root를 `core/build`에
  유지하되, `core/tests` enumeration과 targeted regex는
  `core/build/core` test root 기준으로 읽는다.
- 같은 날 current workspace에서는
  `test_monitor_socket_contract`,
  `test_multi_socket_contract_regressions`,
  `test_public_inproc_multipart_send`가
  `core/build/bin/` 아래 `0`바이트 placeholder로 남아
  first authority gate가 `permission denied`로 깨졌다.
  `cmake --build core/build -j$(nproc)` 뒤에도 세 파일이 비어 있어
  corresponding `core/build/core/tests/CMakeFiles/.../link.txt`를 직접 실행해
  executable을 재링크했고,
  이후
  `ctest --test-dir core/build/core --output-on-failure -R '^(test_socket_with_handler|test_monitor_socket_contract|test_multi_socket_contract_regressions|test_public_inproc_multipart_send|test_pubsub_filter_xpub|test_xpub_nodrop|test_spot_pubsub_scenario)$' -j1`
  는 다시 전부 통과했다.
  즉 이번 단계의 blocker는 current code regression이 아니라
  authority validation surface artifact 손상이었고, 현재 `core/build`는
  다시 해당 gate를 정상 실행할 수 있다.
- artifact 복구 직후 current-tree primary baseline으로
  `BENCH_TRANSPORTS=tcp BENCH_MSG_SIZES=64 BENCH_MULTI_WARMUP_SECONDS=1 BENCH_MULTI_DURATION_SECONDS=3 python3 core/bench/with_zmq/multi/run_comparison.py dealer_dealer --build-dir core/build --runs 1`
  를 다시 찍었고,
  `multi dealer_dealer tcp 64B 1989.95 -> 1603.51 Kmsg/s (-19.42%)`
  를 다음 structural candidate의 same-day prepatch authority baseline으로 둔다.
- 2026-03-30
  [`core/tools/ralphloop/run_codex_execution_guide_loop.sh`](/home/hep7/project/kairos/zlink/core/tools/ralphloop/run_codex_execution_guide_loop.sh)
  `--init-only` early-exit path 재배치 뒤
  `bash -n doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh core/tools/ralphloop/run_codex_execution_guide_loop.sh core/tools/ralphloop/run_execution_gate_loop.sh`
  와
  `./doc/plan/perf/run_single_libzmq_gap_ralph_loop.sh --help`
  wrapper smoke는 다시 통과했다.
- next structural round는 `7bea9e3f` good reference oracle 대비
  `send entry / pipe duty / public surface / sender regime`
  중 무엇을 current contract 안에서 얇게 복원할지 설명 가능한
  common candidate 하나만 고른다.
- next structural round의 top hypothesis는
  `_out_sync` 아래 publication cluster와 lifecycle/activation cluster를
  둘로 가르는 ownership split family로 고정한다.
  즉 다음 code candidate는
  `_out_pipe/_out_active/_peers_msgs_read/_msgs_written` steady-state send
  publication과
  `_state/_delay/_in_active` + term/delimiter/activate 계열 전이를
  같은 exclusion에 계속 둘지부터 다시 자르는 구조여야 한다.
- next structural round는 owner-thread no-lock caller reuse가 아니라,
  public multipart cleanup/rollback invariant를 먼저 보존하면서
  `_out_sync` 아래 publication state와 lifecycle/activation state를
  더 큰 의미 단위로 다시 자르는 family만 올린다.
- next structural round는 방금 reject된
  `_state/_delay/_in_active` atomic field split alone을 반복하지 않고,
  publication cluster와 lifecycle cluster의 공동 exclusion /
  cleanup ordering을 더 명시적으로 재배치하는 구조만 올린다.
- next structural round도 separate lifecycle lock/snapshot split을 반복하지
  않고, publication cluster 분리와 termination/delimiter cleanup ordering
  재배치를 같은 의미 단위로 묶는 구조만 올린다.
- 2026-03-30 `pipe.hpp` / `pipe.cpp`
  lifecycle cleanup atomic split candidate도
  `process_activate_read()/process_pipe_term()/process_delimiter()/set_nodelay()`
  쪽 lifecycle state를 atomic snapshot/CAS로 떼고
  `_out_sync`를 publication cleanup에 더 가깝게 남기려 했지만,
  targeted gate는 통과한 반면
  primary `multi dealer_dealer tcp 64B`는
  `1985.72 -> 1605.14 Kmsg/s (-19.17%)` baseline에서
  `1978.99 -> 1625.42 Kmsg/s (-17.87%)`로 일부 회복하는 데 그쳤고,
  single public/raw가
  `PAIR public tcp/inproc -20.43% / -30.28%`,
  `DEALER_DEALER public tcp/inproc -14.41% / -33.16%`,
  raw `PAIR tcp/inproc -30.39% / -21.58%`,
  raw `DEALER_DEALER tcp/inproc -35.64% / -24.72%`로 무너져 reject됐다.
  publication duty와 termination/delimiter cleanup ordering을
  atomic state만으로 다시 자르는 family alone도 current broad candidate가
  아니다.
- next structural round도 activation + credit atomic lane split을 그대로
  반복하지 않고, `multi dealer_dealer` improvement가 왜
  `router_router` guardrail regression으로 번역됐는지까지 설명 가능한
  더 큰 send/publication ownership split family만 올린다.
- next structural round도 `DEALER` single-part public sync bit만 빼는
  local sender-regime split을 반복하지 않고,
  `send scope construct`와 `_out_sync` publication duty를 함께 다시 자르는
  더 큰 의미 단위 candidate만 올린다.
- next structural round는 방금 reject된 lifecycle cleanup atomic split까지
  포함해 publication/lifecycle ownership split family가
  `multi` 일부 회복과 public/raw broad regression으로 반복된 이유를
  guide/review/hot-path에서 먼저 재정렬한 뒤에만 다시 연다.
- `ROUTER`/`PUBSUB` pattern-local family는 current common structural round가
  끝나기 전까지 primary로 승격하지 않는다.

아래 본문은 legacy reference/archival record다. current authority는 위 요약과
상단 `Current Operating Summary`가 우선한다.

- 현재 유지 중인 latest delta
  - `pipe.hpp` / `pipe.cpp` `_out_sync` invariant map을
    `write_message_unlocked()/rollback_unlocked()/flush_unlocked()` helper로
    고정했고, `set_nodelay()/terminate()/process_delimiter()/
    send_disconnect_msg()/send_hiccup_msg()`의 recursive
    `rollback()/flush()` 의존을 제거했다
  - temporary direct instrumentation 로그
    [`pair_inproc_send_profile_20260328.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_inproc_send_profile_20260328.txt),
    [`dealer_inproc_send_profile_20260328.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_inproc_send_profile_20260328.txt)
    에서는 `process_commands`보다 `send scope construct`와
    `pipe_write_and_flush`가 더 큰 steady-state cost 축으로 남았음을 확인했고,
    계측 patch는 측정 뒤 원복했다
  - latest current-tree split instrumentation에서는
    `pipe_write_and_flush`가 `PAIR/DEALER_DEALER inproc 64B`
    `866.94/862.54 ticks`였고,
    내부 bucket은 `lock 70.95/70.63`, `hwm 25.12/25.33`,
    `write 38.50/38.98`, `flush 259.26/266.00`,
    `flush outcome true=8840861/8740895`,
    `false=539397/506441`였다.
    즉 current `pipe serialization floor`의 본체는
    false wakeup/no-op보다 successful publication/CAS path 쪽이었다.
    이번 계측 patch도 측정 뒤 원복했다
  - above refactor의 targeted public/raw guardrail은
    [`perf_linux_20260328_162242_codex_20260328_pipe_invariant_refactor_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_162242_codex_20260328_pipe_invariant_refactor_public.txt),
    [`perf_linux_20260328_162324_codex_20260328_pipe_invariant_refactor_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_162324_codex_20260328_pipe_invariant_refactor_raw.txt)
    로 다시 남겼다
  - `socket_base.hpp` / `socket_base_msg.cpp` /
    `socket_runtime.hpp` / `socket_runtime.cpp` /
    `multipart_send_txn.cpp` retained structural prep으로
    direct send retry를 `send_direct_with_retry()` 경계로 합쳤고,
    retry sync hold/release/reacquire 판단을
    `socket_public_send_scope_t` helper로 고정했으며,
    plain direct send scope 결정을
    `socket_base_t::direct_send_needs_public_api_sync()` 하나로 재사용한다
  - 위 prep의 targeted public/raw guardrail은
    [`perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_public.txt),
    [`perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_182242_codex_20260328_send_scope_boundary_prep_raw.txt)
    로 남겼다
  - common send-side structural round는
    `process_activate_write()` snapshot/atomic,
    existing public-send-sync-held `send_serialized`,
    `DEALER` external send-state mutex/external serialized scope,
    existing public-send-sync-held hot-send lease/outpipe lifetime split,
    `fast_mutex.hpp` native recursive pthread primitive replacement,
    dedicated public send lease split,
    shared `public_api_state` public/send inflight lane split,
    `public_api_sync` recursive mutex-backed split,
    `pipe.cpp` final-part `write_and_flush()` lock-free snapshot split,
    `pipe::_out_sync` plain non-recursive fast mutex split,
    non-conflate out-pipe concrete `ypipe_t` fast path,
    `ypipe_base.hpp` / `ypipe.hpp` / `ypipe_conflate.hpp`
    combined write+publication split,
    public API-boundary same-handle recursive mutex single-part fast path,
    same-thread parked send admission lease,
    `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache split,
    `activate_write` progress-command coalesce split,
    `msg_t::init_size()/close()` small-lmsg pooled materialize/free,
    send-side layout regroup,
    preflight-before-public-admission split,
    stronger-gate `public_api_inflight/public_api_closing/public_api_sync`
    split recheck,
    plain final-part sender-regime split까지
    broad win을 만들지 못했다.
    따라서 current next step은 이 rejected family를 반복하지 않고,
    historical `a819ea3a` admission floor 대 `ff0140e5` pipe floor
    재분리 위에서
    `98e7d324/9b91234c` public multipart/sender-regime 의미까지 함께 다시 본
    뒤에만
    새 broad hypothesis와 code family를 다시 고르는 것이다
  - serial current-tree `PAIR` / `DEALER_DEALER` public/raw refresh
    [`perf_linux_20260328_212318_codex_20260328_serial_refresh_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_212318_codex_20260328_serial_refresh_public.txt),
    [`perf_linux_20260328_212402_codex_20260328_serial_refresh_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_212402_codex_20260328_serial_refresh_raw.txt)
    에서는
    `PAIR public/raw tcp -12.03% -> -8.63%, inproc -17.18% -> -23.67%`,
    `DEALER_DEALER public/raw tcp -11.12% -> -12.06%, inproc -18.60% -> -20.63%`
    였다.
    즉 raw/public wrapper 제거는 `PAIR tcp`만 일부 회복하고 common broad win을
    만들지 못했으며, `PAIR`(no public-api sync)와 `DEALER_DEALER`
    (public-api sync held) 격차가 여전히 비슷하므로 dealer-only
    `public_api_sync` reuse family도 현재 primary blocker가 아니다.
    current residual direct cause는 wrapper나 dealer-only sync가 아니라
    `enter_public_api`가 포함된 common send scope construct floor와
    `_out_sync` write/flush serialization floor의 조합으로 다시 쓴다.
  - late-session serial current-tree refresh +
    rerun
    [`perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232530_codex_20260328_post_recursive_sync_refresh_public.txt),
    [`perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_232612_codex_20260328_post_recursive_sync_refresh_raw.txt),
    [`perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233054_codex_20260328_post_recursive_sync_refresh_public_rerun.txt),
    [`perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_233133_codex_20260328_post_recursive_sync_refresh_raw_rerun.txt)
    에서는
    public first/rerun
    `PAIR tcp/inproc -24.67% / -14.98% -> -27.72% / -18.03%`,
    `DEALER_DEALER tcp/inproc -14.63% / -32.73% -> -21.31% / -32.12%`,
    raw first/rerun
    `PAIR tcp/inproc -23.51% / -23.04% -> -23.54% / -25.42%`,
    `DEALER_DEALER tcp/inproc -32.68% / -34.30% -> -23.26% / -25.44%`였다.
    즉 current session baseline 자체가 earlier authority보다 더 낮은 상태로
    반복됐으므로,
    다음 broad hypothesis/next candidate는 early authority와
    current session low baseline을 둘 다 guardrail로 보고
    signal이 섞이면 current-tree serial refresh를 먼저 다시 찍은 뒤에만
    keep/reject를 결정한다
  - `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t`
    fast path candidate도 targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt)
    `PAIR tcp/inproc -18.01% / -35.55%`,
    `DEALER_DEALER tcp/inproc -15.12% / -24.02%`,
    raw
    [`perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt)
    `PAIR tcp/inproc -8.81% / -28.27%`,
    `DEALER_DEALER tcp/inproc -7.64% / -22.08%`로
    public과 raw `inproc` guardrail을 함께 못 지켜 원복했다.
    즉 current `pipe serialization floor`는
    type-erased out-pipe `write()/flush()` dispatch 한 겹만 걷는 것으로는
    설명되지 않는다
  - 같은 `flush true` dominant signal 위에서
    `ypipe_base.hpp` / `ypipe.hpp` / `ypipe_conflate.hpp`
    combined write+publication candidate도
    [`perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt)
    public `PAIR tcp/inproc -36.56% / -24.11%`,
    `DEALER_DEALER tcp/inproc -10.62% / -18.47%`로
    early authority와 session-local low baseline을 함께 못 지켜 원복했다.
    즉 successful publication/CAS path가 더 크더라도,
    same-ordering local `ypipe` helper fusion 하나만으로는 current
    `pipe serialization floor`를 설명하지 못한다
  - `socket_base_api.cpp` / `socket_base_msg.cpp` /
    `socket_message_send_api.cpp` public API-boundary same-handle recursive
    mutex single-part fast path candidate도 targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt)
    `PAIR tcp/inproc -14.34% / -31.65%`,
    `DEALER_DEALER tcp/inproc -13.44% / -24.93%`,
    `ROUTER_ROUTER tcp/inproc -57.41% / -23.50%`,
    raw
    [`perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt)
    `PAIR tcp/inproc -17.38% / -31.55%`,
    `DEALER_DEALER tcp/inproc -11.26% / -19.57%`,
    `ROUTER_ROUTER tcp/inproc -57.61% / -23.28%`로
    public과 raw guardrail을 함께 못 지켜 원복했다.
    즉 current `send scope construct floor`는
    outer same-handle API mutex로만 대체해도 broad fix가 되지 않는다
  - `socket_runtime.hpp` / `socket_runtime.cpp` /
    `socket_base_msg.cpp` same-thread parked send admission lease candidate도
    targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt)
    `PAIR tcp/inproc -13.55% / -29.90%`,
    `DEALER_DEALER tcp/inproc -23.40% / -22.04%`,
    raw
    [`perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt)
    `PAIR tcp/inproc -24.41% / -23.24%`,
    `DEALER_DEALER tcp/inproc -23.28% / -14.47%`로
    targeted public/raw guardrail을 함께 못 지켜 원복했다.
    즉 `a819ea3a` admission floor와 `9b91234c` sender-regime 흔적은
    same-thread parked send handoff 하나로만은 broad fix가 되지 않는다
  - `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache
    candidate도 targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt)
    `PAIR tcp/inproc -15.04% / -24.43%`,
    `DEALER_DEALER tcp/inproc -8.68% / -32.46%`,
    raw
    [`perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt)
    `PAIR tcp/inproc -20.98% / -20.36%`,
    `DEALER_DEALER tcp/inproc -20.37% / -22.58%`로
    targeted public/raw guardrail을 함께 못 지켜 원복했다.
    즉 current `pipe serialization floor`는
    steady-state `check_hwm()` arithmetic과 `_peers_msgs_read` refresh를
    cached credit 하나로 다시 쓰는 local pipe-family만으로는
    broad fix가 되지 않는다
  - `core/src/core/msg.cpp` `msg_t::init_size()/close()` small-lmsg pooled
    materialize/free candidate도 targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_022632_codex_20260329_msg_pool_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_022632_codex_20260329_msg_pool_public.txt)
    `PAIR tcp/inproc -15.43% / -24.47%`,
    `DEALER_DEALER tcp/inproc -32.71% / -35.28%`,
    raw
    [`perf_linux_20260329_022712_codex_20260329_msg_pool_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_022712_codex_20260329_msg_pool_raw.txt)
    `PAIR tcp/inproc -33.38% / -25.71%`,
    `DEALER_DEALER tcp/inproc -16.21% / -36.83%`로
    `DEALER_DEALER` public과 raw `PAIR/DEALER` guardrail을 함께 못 지켜
    원복했다.
    즉 current residual은 `msg_t` small-lmsg heap pair 하나만 걷는다고
    사라지지 않았고,
    message-local allocator pool family도 broad fix가 되지 않는다
  - `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp` send-side layout regroup
    candidate도 targeted ctest는 통과했지만,
    same-tag public/raw parallel diagnostic
    [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt),
    [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt)
    에서
    public `PAIR tcp/inproc -14.64% / -25.65%`,
    `DEALER_DEALER tcp/inproc -12.23% / -38.09%`,
    raw `PAIR tcp/inproc -24.84% / -18.60%`,
    `DEALER_DEALER tcp/inproc -22.59% / -19.83%`로
    public/raw guardrail을 함께 못 지켜 원복했다.
    즉 current residual은 lifecycle coordinator / pipe outbound hot-state의
    layout regroup 하나만으로는 broad fix가 되지 않는다
  - `socket_base.hpp` / `socket_base_msg.cpp`
    preflight-before-public-admission candidate도
    contract gate
    (`test_thread_safe_contract_policy`, `test_monitor_perf_contract`,
    `test_socket_with_handler`, `test_stream_threadsafe` 포함)는 통과했지만,
    public
    [`perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt)
    `PAIR tcp/inproc -26.28% / -26.58%`,
    `DEALER_DEALER tcp/inproc -24.14% / -35.55%`로
    public stage에서 early authority와 session-local low baseline을 함께
    못 지켜 raw 없이 바로 원복했다.
    즉 `a819ea3a` admission floor를 initial preflight 밖으로 미루는 것만으로는
    broad fix가 되지 않고, close/send contract risk를 감수할 만큼의 이득도
    없다
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_inflight/public_api_closing/public_api_sync`
    split family를 stronger contract gate로 다시 확인했지만,
    public
    [`perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt)
    `PAIR tcp/inproc -17.22% / -24.11%`,
    `DEALER_DEALER tcp/inproc -14.31% / -29.56%`로
    early authority와 session-local low baseline을 함께 못 지켜
    raw 없이 바로 원복했다.
    즉 current residual은 public lifecycle coordinator state packing
    하나를 stronger gate로 다시 확인해도 broad fix가 되지 않는다
  - `pipe.cpp` `write()/write_and_flush()/check_write_status()`는 current tree에서
    `_out_sync` 아래 generic `check_hwm_unlocked()`를 사용해 recursive
    `check_hwm()` 재진입을 이미 피한다
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
  - `socket_runtime.cpp` `public_api_sync` fast-mutex split candidate도
    public `PAIR tcp/inproc 64B -32.03% / -29.11%`,
    `DEALER_DEALER tcp/inproc -19.33% / -31.37%`로
    `PAIR`와 `DEALER_DEALER inproc`을 accepted baseline보다 더 악화시켜 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_inflight/public_api_closing/public_api_sync`
    split family를 stronger contract gate로 다시 확인한 run도
    public `PAIR tcp/inproc -17.22% / -24.11%`,
    `DEALER_DEALER tcp/inproc -14.31% / -29.56%`로
    early authority와 session-local low baseline을 함께 못 지켜 원복
  - `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp`
    plain non-routed final-part sender-regime split candidate도
    stronger contract gate는 통과했지만,
    public `PAIR tcp/inproc -12.23% / -29.92%`,
    `DEALER_DEALER tcp/inproc -11.44% / -34.04%`로
    early authority와 session-local low baseline을 함께 못 지켜 원복
  - `pipe.hpp` / `pipe.cpp` `_lwm` boundary `activate_write`
    progress-command coalesce candidate도 targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt)
    `PAIR tcp/inproc -10.58% / -34.54%`,
    `DEALER_DEALER tcp/inproc -30.23% / -25.32%`로
    early authority와 session-local low baseline을 함께 못 지켜
    raw 없이 바로 원복했다.
    즉 current residual은 `_lwm` boundary progress-command emission count
    하나만 줄이는 local pipe tweak로는 broad fix가 되지 않는다
  - `core/src/core/ypipe_base.hpp` / `core/src/core/ypipe.hpp` /
    `core/src/core/ypipe_conflate.hpp` combined write+publication candidate도
    targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt)
    `PAIR tcp/inproc -36.56% / -24.11%`,
    `DEALER_DEALER tcp/inproc -10.62% / -18.47%`로
    early authority와 session-local low baseline을 함께 못 지켜
    raw 없이 바로 원복했다.
    즉 `flush true` dominant signal도
    another local `ypipe` publication helper family를 정당화하지 않는다
  - `fast_mutex.hpp` native recursive `pthread_mutex` primitive replacement도
    stream/contract smoke는 통과했지만,
    public `PAIR tcp/inproc -27.78% / -17.52%`,
    `DEALER_DEALER tcp/inproc +3.72% / -21.03%`,
    raw `PAIR tcp/inproc -13.25% / -21.63%`,
    `DEALER_DEALER tcp/inproc -7.74% / -15.86%`로
    unchanged control인 `PAIR public tcp`와 raw `PAIR inproc` guardrail을
    함께 지키지 못해 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    dedicated public send lease split candidate도
    targeted ctest는 통과했지만,
    authority public rerun
    `PAIR tcp/inproc -15.65% / -25.43%`,
    `DEALER_DEALER tcp/inproc -24.41% / -32.10%`로
    baseline보다 더 악화돼 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    shared `public_api_state` public/send inflight lane split candidate도
    targeted ctest는 통과했지만,
    same-tag public/raw parallel run
    [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_public.txt),
    [`perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225749_codex_20260328_send_state_lane_split_raw.txt)
    는 noisy diagnostic으로만 두고,
    authority public
    [`perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225833_codex_20260328_send_state_lane_split_public_authority.txt)
    `PAIR tcp/inproc -20.45% / -24.95%`,
    `DEALER_DEALER tcp/inproc -23.79% / -23.41%`,
    authority raw
    [`perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_225912_codex_20260328_send_state_lane_split_raw_authority.txt)
    `PAIR tcp/inproc -22.49% / -24.59%`,
    `DEALER_DEALER tcp/inproc -10.46% / -19.57%`로
    baseline보다 더 악화돼 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_sync` recursive mutex-backed split candidate도
    targeted ctest는 통과했지만,
    same-tag public/raw parallel run
    [`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_public.txt),
    [`perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231525_codex_20260328_public_sync_recursive_mutex_raw.txt)
    는 noisy diagnostic으로만 두고,
    authority public
    [`perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231611_codex_20260328_public_sync_recursive_mutex_public_authority.txt)
    `PAIR tcp/inproc -16.85% / -21.61%`,
    `DEALER_DEALER tcp/inproc -18.65% / -36.20%`,
    authority raw
    [`perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_231650_codex_20260328_public_sync_recursive_mutex_raw_authority.txt)
    `PAIR tcp/inproc -7.34% / -17.60%`,
    `DEALER_DEALER tcp/inproc -18.83% / -34.78%`로
    baseline보다 더 악화돼 원복
  - `pipe.hpp` / `pipe.cpp`
    final-part `write_and_flush()` lock-free snapshot candidate도
    targeted ctest는 통과했지만,
    public
    [`perf_linux_20260328_234340_codex_20260328_pipe_write_flush_hot_snapshot_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_234340_codex_20260328_pipe_write_flush_hot_snapshot_public.txt)
    `PAIR tcp/inproc -32.16% / -20.55%`,
    `DEALER_DEALER tcp/inproc -9.76% / -23.34%`로
    `PAIR`가 early authority와 session-local low baseline을 둘 다 못 지켜
    public stage에서 원복
  - `core/src/utils/fast_mutex.hpp` / `pipe.hpp` / `pipe.cpp`
    `pipe::_out_sync` plain non-recursive fast mutex candidate도
    `claude -p` consult는 `code 124` timeout으로 unavailable이었고,
    targeted ctest는 통과했지만,
    public
    [`perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt)
    `PAIR tcp/inproc -22.34% / -24.82%`,
    `DEALER_DEALER tcp/inproc -9.78% / -32.93%`로
    `PAIR`와 `DEALER_DEALER inproc`이 both guardrail을 못 지켜
    public stage에서 원복
  - `pipe.hpp` / `pipe.cpp`
    non-conflate out-pipe concrete `ypipe_t` fast path candidate도
    targeted ctest는 통과했지만,
    public
    [`perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt)
    `PAIR tcp/inproc -18.01% / -35.55%`,
    `DEALER_DEALER tcp/inproc -15.12% / -24.02%`,
    raw
    [`perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt)
    `PAIR tcp/inproc -8.81% / -28.27%`,
    `DEALER_DEALER tcp/inproc -7.64% / -22.08%`로
    public과 raw `inproc` guardrail을 함께 못 지켜 원복
  - `pipe.hpp` / `pipe.cpp` hot send-only non-recursive lock split도
    public `PAIR tcp/inproc 64B -17.14% / -34.56%`,
    `DEALER_DEALER tcp/inproc -13.65% / -19.47%`,
    raw `PAIR tcp/inproc -8.61% / -25.46%`,
    `DEALER_DEALER tcp/inproc -20.69% / -21.15%`로
    `PAIR inproc` absolute throughput을 크게 무너뜨려 broad win이 아니어서 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    send-side lifecycle/scope hot-path header-inline codegen-only candidate도
    public `PAIR tcp/inproc 64B -21.70% / -23.83%`,
    `DEALER_DEALER tcp/inproc -10.64% / -17.99%`,
    raw `PAIR tcp/inproc -11.65% / -27.25%`,
    `DEALER_DEALER tcp/inproc -8.99% / -25.87%`로
    `PAIR` public/raw와 `DEALER_DEALER inproc raw`가 함께 흔들려 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp`
    common data-plane admission boundary helper extraction candidate도
    먼저 out-of-line helper 형태로
    public `PAIR tcp/inproc -28.25% / -30.71%`,
    `DEALER_DEALER tcp/inproc -15.00% / -22.13%`,
    raw `PAIR tcp/inproc -25.53% / -22.57%`,
    `DEALER_DEALER tcp/inproc -11.38% / -20.77%`로 흔들렸고,
    helper를 header-inline + original branch shape로 다시 맞춘 뒤에도
    public `PAIR tcp/inproc -21.50% / -14.91%`,
    `DEALER_DEALER tcp/inproc -10.08% / -19.13%`,
    raw `PAIR tcp/inproc -26.96% / -30.67%`,
    `DEALER_DEALER tcp/inproc -22.43% / -21.11%`로
    `PAIR` public/raw와 `DEALER_DEALER tcp raw` guardrail을 함께 지키지
    못해 전부 원복했다
  - `socket_public_send_scope_t` constructor lazy-sync acquire candidate도
    public `PAIR tcp/inproc -9.29% / -27.64%`,
    `DEALER_DEALER tcp/inproc -26.48% / -25.53%`,
    raw `PAIR tcp/inproc -13.68% / -25.35%`,
    `DEALER_DEALER tcp/inproc -25.66% / -19.94%`로
    `PAIR tcp`만 좋아지고 broad win을 만들지 못해 원복
  - `pipe.cpp` flush notify-outside-`_out_sync` candidate도
    public `PAIR tcp/inproc -16.34% / -25.54%`,
    `DEALER_DEALER tcp/inproc -26.94% / -23.32%`,
    raw `PAIR tcp/inproc -30.05% / -18.28%`,
    `DEALER_DEALER tcp/inproc -7.24% / -17.59%`로
    public baseline과 raw/public guardrail을 함께 지키지 못해 원복
  - `pipe.cpp` / `pair.cpp` / `lb.cpp` final non-routing payload flush helper도
    public `PAIR tcp/inproc -12.51% / -26.39%`,
    `DEALER_DEALER tcp/inproc -9.53% / -19.49%`로 `tcp`만 회복했고,
    raw `PAIR inproc -34.78%`,
    `DEALER_DEALER inproc -21.83%`,
    `DEALER_DEALER raw tcp timeout`까지 깨져 broad win과 raw/public
    guardrail을 지키지 못해 원복
  - `pipe.hpp` / `pipe.cpp`
    `process_activate_write()` already-active peer-progress snapshot split도
    targeted public rerun
    `PAIR tcp/inproc -13.02% / -17.35%`,
    `DEALER_DEALER tcp/inproc -14.66% / -18.53%`,
    raw rerun
    `PAIR tcp/inproc -9.67% / -19.42%`,
    `DEALER_DEALER tcp/inproc -8.19% / -19.85%`까지는 회복했지만,
    broader single
    `PAIR tcp/inproc -8.43% / -21.82%`,
    `PUBSUB tcp/inproc -18.27% / -15.75%`,
    `DEALER_DEALER tcp/inproc -11.48% / -29.32%`,
    `DEALER_ROUTER tcp/inproc -19.84% / -30.93%`,
    partial `ROUTER_ROUTER tcp -52.69%`와
    `comp_zlink_router_router zlink inproc 64` hang까지 생겨
    broad win이 아니어서 원복
  - `pipe.hpp` / `pipe.cpp`
    `process_activate_write()` atomic peer-progress publish candidate도
    targeted public
    `PAIR tcp/inproc -22.80% / -18.39%`,
    `DEALER_DEALER tcp/inproc -35.30% / -19.86%`,
    raw
    `PAIR tcp/inproc -23.83% / -31.74%`,
    `DEALER_DEALER tcp/inproc -11.45% / -15.34%`로
    targeted stage부터 broad win이 아니어서 원복
  - `pipe.hpp` / `pipe.cpp` / `lb.hpp` / `lb.cpp` / `dealer.cpp` / `router.cpp`
    existing public-send-sync-held `send_serialized` pipe helper candidate도
    public `PAIR tcp/inproc -13.23% / -17.01%`,
    `DEALER_DEALER tcp/inproc -23.74% / -31.19%`,
    raw `PAIR tcp/inproc -18.50% / -22.98%`,
    `DEALER_DEALER tcp/inproc -20.96% / -23.13%`로
    `PAIR` / `DEALER` public/raw guardrail과 broader public single
    (`DEALER_ROUTER tcp/inproc -29.36% / -28.30%`,
    `ROUTER_ROUTER tcp/inproc -55.88% / -22.76%`)을 함께 지키지 못해 원복
  - `socket_runtime.hpp` / `socket_runtime.cpp` / `socket_base.hpp` /
    `socket_base_api.cpp` / `socket_base_msg.cpp` /
    `multipart_send_txn.cpp` / `dealer.hpp` / `dealer.cpp`
    `DEALER` external send-state mutex + external `send_serialized` scope
    candidate도
    public `PAIR tcp/inproc -9.23% / -16.03%`,
    `DEALER_DEALER tcp/inproc -13.09% / -32.97%`,
    raw `PAIR tcp/inproc -13.88% / -26.40%`,
    `DEALER_DEALER tcp/inproc -24.35% / -33.20%`로
    targeted stage부터 `PAIR` / `DEALER` public/raw guardrail을 함께 지키지
    못해 원복
  - `pipe.hpp` / `pipe.cpp` / `lb.hpp` / `lb.cpp` / `dealer.cpp`
    existing public-send-sync-held pipe hot-send lease / outpipe lifetime
    split candidate도
    targeted public `PAIR tcp/inproc -28.83% / -19.45%`,
    `DEALER_DEALER tcp/inproc -15.40% / -22.79%`로
    public stage부터 broad win이 아니어서 원복
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
  - `pipe.cpp` / `router.cpp` `xsend_routed()` final-part one-lock helper도
    first complete run `ROUTER_ROUTER tcp/inproc 64B -54.37% / -23.05%`,
    transport-split rerun `tcp -57.14%`, `inproc -29.36%`로
    zlink absolute throughput이 `tcp ~1.21Mmsg/s`, `inproc ~2.41Mmsg/s`
    수준에서 거의 못 움직여 원복
  - `router.cpp` same-target routed send cache와
    `pipe.cpp` final-part one-lock helper combo도
    default `ROUTER_ROUTER tcp/inproc 64B -58.18% / -23.12%`,
    raw `-53.09% / -23.25%`,
    `DEALER_ROUTER tcp/inproc -30.17% / -25.42%`였지만,
    direct `comp_zlink_router_router` absolute throughput이
    `tcp 1214300.00`, `inproc 2419754.40 msg/s`로 baseline 수준에 머물러
    keep-worthy delta가 아니어서 원복
  - `router.cpp` routed recv current-in/source-rid cache와
    lazy prefetched-id prepare도
    default `ROUTER_ROUTER tcp/inproc 64B -57.01% / -22.77%`,
    raw `-52.96% / -20.99%`,
    `DEALER_ROUTER tcp/inproc -27.89% / -30.42%`였지만,
    direct `comp_zlink_router_router` absolute throughput이
    `tcp 1211724.60`, `inproc 2408252.00 msg/s`로 baseline 수준에 머물러
    keep-worthy delta가 아니어서 원복
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
  - `socket_base_msg.cpp` direct single-part `send()` / `send_routed()`
    initial public sync unlock + relock-around-`xsend()` candidate도
    public `PAIR tcp/inproc 64B -21.29% / -33.25%`,
    `DEALER_DEALER tcp/inproc -23.62% / -25.85%`,
    raw `PAIR tcp/inproc -9.47% / -20.90%`,
    `DEALER_DEALER tcp/inproc -28.00% / -26.50%`로
    raw/public guardrail과 broad win을 함께 만족시키지 못해 원복
  - `socket_base_msg.cpp` `DEALER` direct single-part admission-only +
    `lb.cpp` send-state lock candidate도
    public `PAIR tcp/inproc -6.31% / -30.93%`,
    `DEALER_DEALER tcp/inproc -15.36% / -21.40%`,
    raw `PAIR tcp/inproc -9.37% / -35.69%`,
    `DEALER_DEALER tcp/inproc -11.45% / -32.76%`로
    `DEALER` public은 회복했지만 raw/public guardrail과 `PAIR inproc`이 함께
    흔들려 stable broad win이 아니어서 원복
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
    current tree에서 `_out_sync` lock scope 안의
    `check_hwm_unlocked()`를 사용한다.
  - 따라서 generic recursive `check_hwm()` elide는 더 이상 pending candidate가
    아니라 current code에 이미 반영된 kept common delta다.
  - 별도 `pipe.cpp` / `dist.cpp` dist-only non-recursive HWM check는
    above generic helper 위에 얹힌 `PUBSUB` publication-specific retained delta로
    계속 유지한다.
  - 2026-03-28 `socket_base_msg.cpp` `DEALER` direct single-part
    admission-only + `lb.cpp` send-state lock candidate는
    `DEALER` public isolated win을 만들었지만
    raw/public guardrail과 `PAIR inproc`를 함께 지키지 못해 current code에는 없다.
  - 2026-03-28 `socket_runtime.cpp` lifecycle atomic CAS fast path A/B도
    `DEALER` raw 회복과 `PAIR`/public 흔들림이 엇갈렸다.
  - 따라서 send-side lifecycle/backpressure 첫 우선순위는 유지하되,
    현재 문서 기준으로 keep-worthy 공통 atomic fast path는 아직 없다.
  - 같은 날 `socket_runtime.cpp` `public_api_sync` fast-mutex split candidate도
    public `PAIR tcp/inproc -32.03% / -29.11%`,
    `DEALER_DEALER tcp/inproc -19.33% / -31.37%`로
    broad win이 아니어서 현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_runtime.hpp` / `socket_runtime.cpp`
    shared `public_api_state` public/send inflight lane split candidate도
    authority public/raw가 모두 baseline보다 크게 악화돼 현재 코드에는 없다.
  - 2026-03-29 stronger contract gate로 다시 확인한
    `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_inflight/public_api_closing/public_api_sync`
    split family도
    public `PAIR tcp/inproc -17.22% / -24.11%`,
    `DEALER_DEALER tcp/inproc -14.31% / -29.56%`로
    early authority와 session-local low baseline을 함께 못 지켜
    현재 코드에는 없다.
  - 같은 날 `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp`
    plain non-routed final-part sender-regime split candidate도
    public `PAIR tcp/inproc -12.23% / -29.92%`,
    `DEALER_DEALER tcp/inproc -11.44% / -34.04%`로
    early authority와 session-local low baseline을 함께 못 지켜
    현재 코드에는 없다.
  - 같은 날 `pipe.hpp` / `pipe.cpp` `_lwm` boundary `activate_write`
    progress-command coalesce candidate도
    public `PAIR tcp/inproc -10.58% / -34.54%`,
    `DEALER_DEALER tcp/inproc -30.23% / -25.32%`로
    early authority와 session-local low baseline을 함께 못 지켜
    현재 코드에는 없다.
  - 같은 날 `socket_runtime.hpp` / `socket_runtime.cpp`
    `public_api_sync` recursive mutex-backed split candidate도
    authority public/raw가 모두 baseline보다 크게 악화돼 현재 코드에는 없다.
  - 같은 날 `pipe.hpp` / `pipe.cpp`
    final-part `write_and_flush()` lock-free snapshot candidate도
    public `PAIR tcp/inproc -32.16% / -20.55%`로
    current guardrail을 못 지켜 현재 코드에는 없다.
  - 같은 날 `core/src/utils/fast_mutex.hpp` / `pipe.hpp` / `pipe.cpp`
    `pipe::_out_sync` plain non-recursive fast mutex candidate도
    public `PAIR tcp/inproc -22.34% / -24.82%`,
    `DEALER_DEALER tcp/inproc -9.78% / -32.93%`로
    both guardrail을 못 지켜 현재 코드에는 없다.
  - 같은 날 `pipe.hpp` / `pipe.cpp`
    non-conflate out-pipe concrete `ypipe_t` fast path candidate도
    public `PAIR tcp/inproc -18.01% / -35.55%`,
    raw `PAIR tcp/inproc -8.81% / -28.27%`,
    `DEALER_DEALER public/raw tcp/inproc -15.12% / -24.02%`,
    `-7.64% / -22.08%`로
    public과 raw `inproc` guardrail을 함께 못 지켜 현재 코드에는 없다.
  - 같은 날 `pipe.hpp` / `pipe.cpp` hot send-only non-recursive lock split도
    public `PAIR tcp/inproc -17.14% / -34.56%`,
    raw `PAIR tcp/inproc -8.61% / -25.46%`로
    특히 `PAIR inproc` absolute throughput이 크게 떨어져
    현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_runtime.hpp` / `socket_runtime.cpp`
    send-side lifecycle/scope hot-path header-inline codegen-only candidate도
    public `PAIR tcp/inproc -21.70% / -23.83%`,
    raw `PAIR tcp/inproc -11.65% / -27.25%`로
    broad win이 아니어서 현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_public_send_scope_t` constructor lazy-sync acquire
    candidate도 `PAIR tcp -9.29%`만 회복하고
    `PAIR inproc -27.64%`, `DEALER_DEALER tcp/inproc -26.48% / -25.53%`로
    broad win이 아니어서 현재 코드에는 남아 있지 않다.
  - 같은 날 `pipe.cpp` flush notify-outside-`_out_sync` candidate도
    public `PAIR tcp/inproc -16.34% / -25.54%`,
    raw `PAIR tcp/inproc -30.05% / -18.28%`로
    broad win이 아니어서 현재 코드에는 남아 있지 않다.
  - 같은 날 `PAIR` no-sync send scope 전용 enter+leave / leave-only fast path도
    각각 raw/public guardrail 또는 `DEALER` broad guardrail을 만족시키지 못해
    현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_base_msg.cpp` retry loop에서
    installed-but-idle send-ready handler까지 sync 유지 범위를 넓히는 후보도
    serial public/raw guardrail을 만족시키지 못해 현재 코드에는 남아 있지 않다.
  - 같은 날 `socket_base_msg.cpp` direct single-part `send()` /
    `send_routed()`에서 initial `process_commands()` 바깥으로
    public sync를 빼는 후보도 `PAIR`/`DEALER` public broad win이 아니었고,
    raw/public guardrail도 다시 엇갈려 현재 코드에는 남아 있지 않다.
  - 같은 날 `pipe.hpp` / `pipe.cpp` / `lb.hpp` / `lb.cpp` /
    `dealer.cpp` / `router.cpp` existing public-send-sync-held
    `send_serialized` pipe helper candidate도
    `PAIR` / `DEALER` public/raw guardrail과 broader public single을 함께
    만족시키지 못해 현재 코드에는 남아 있지 않다.
    current tree에는 `pipe` caller-owned send-serialized helper나
    `DEALER` / `ROUTER`의 `_out_sync` elision 경로가 없다.
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
  - current `pipe::_out_sync`는 `write()/flush()` hot path만이 아니라
    `process_activate_write()`, `process_activate_read()`, `process_hiccup()`,
    `process_pipe_term*()`와 same-thread direct `activate_write` publish도
    함께 직렬화한다.
  - 2026-03-28 current tree는 위 invariant를
    `write_message_unlocked()/rollback_unlocked()/flush_unlocked()` helper와
    `pipe.hpp` 주석으로 이미 고정했다.
  - 따라서 다음 iteration은 invariant map을 다시 쓰는 것이 아니라,
    send admission/scope construct와 `pipe write_and_flush` steady-state cost를
    실제로 줄이는 structural candidate에서 시작한다.
  - 같은 날 `router.cpp` routed send의 prefix/HWM second-check elimination은
    `ROUTER_ROUTER tcp/inproc 64B`를 `-55.19% / -25.05%`까지밖에
    못 줄였고 broad win이 아니어서 현재 코드에는 없다.
  - 같은 날 `socket_message_recv_api.cpp` / `router.cpp` routed recv
    source-rid zero-elision도 `ROUTER_ROUTER tcp/inproc 64B`를
    `-58.34% / -33.47%`로 더 흔들려 현재 코드에는 없다.
  - 따라서 current `ROUTER` 잔여 gap은 routed prefix/HWM recheck나
    source-rid zero-fill 제거 같은 micro-elision 하나로 설명되지 않는다.
  - 같은 날 `socket_message_send_api.cpp` blocking `ROUTER` send는 이미
    routing-id envelope를 `send_routed()` one-part path로 접어 보낸다는 것을
    다시 확인했고,
    같은 envelope fold를 `ZLINK_DONTWAIT` 경로까지 넓히는 candidate도
    `core/bench/with_zmq/single/zlink/bench_zlink_router_router.cpp` active
    phase가 여전히 blocking send를 쓴다는 점만 재확인한 채
    [`perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt)
    `ROUTER_ROUTER tcp/inproc -58.16% / -31.94%`로 더 나빠져 현재 코드에는
    없다.
    shared logical multipart entry-state reuse candidate도
    [`perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt)
    first `-54.15% / -29.86%`,
    [`perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt)
    rerun `-58.08% / -22.16%`로 relative diff만 흔들렸고, zlink 절대
    throughput은 `tcp 1296.50 -> 1292.20`, `inproc 2572.46 -> 2574.88 msg/s`
    수준에 머물러 현재 코드에는 없다.
    `socket_base_msg.cpp` / `socket_message_recv_api.cpp`
    routed source-rid zeroing-floor candidate도
    concurrent public/raw run
    [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_public.txt),
    [`perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055430_codex_20260329_router_recv_rid_zeroing_raw.txt)
    는 noisy diagnostic으로만 두고,
    authority public
    [`perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055517_codex_20260329_router_recv_rid_zeroing_public_authority.txt)
    `-56.10% / -32.31%`,
    authority raw
    [`perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_055548_codex_20260329_router_recv_rid_zeroing_raw_authority.txt)
    `-57.70% / -22.00%`로 broad win을 만들지 못했고,
    zlink absolute throughput도 public `1297.34 / 2575.26`,
    raw `1295.90 / 2570.82 Kmsg/s` 수준에 머물러 현재 코드에는 없다.
    `pipe.cpp` / `router.cpp` `xsend_routed()` final-part one-lock helper도
    complete run과 transport-split rerun에서 zlink 절대 throughput을
    거의 못 움직여 현재 코드에는 없다.
  - 따라서 current `ROUTER` 잔여 gap은 send wrapper 한 겹이나
    `xsend_routed()` final-part micro-fusion,
    nonblocking envelope handshake fast path,
    logical multipart entry-state reuse,
    local source-rid zeroing-floor보다
    routed recv ordering 또는 공통 `_out_sync` serialization floor를 더 직접
    분리해야 한다.
  - 같은 날 `router.cpp` routed recv current-in/source-rid cache와
    lazy prefetched-id prepare도 default/raw relative diff는 일부 좋아 보였지만,
    direct `comp_zlink_router_router` absolute throughput이
    `tcp 1211724.60`, `inproc 2408252.00 msg/s`로 baseline 수준에 머물러
    현재 코드에는 없다.
  - 대신
    `test_public_inproc_router_recv_multipart_with_source_rid_blocking()`과
    `test_public_inproc_router_msg_recv_rid_keeps_source_rid_across_reset()`
    회귀는 남겨 current routed recv/source-rid contract를 고정했다.
  - 따라서 current `ROUTER` next step은 local recv-state/source-rid cache를
    다시 누적하는 것이 아니라, 실제 prefetch ordering 차이와
    `recv_routed()` export path를 더 직접 분리하는 쪽이다.
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

- [x] common send-side structural round의 guide/review/hot-path 우선순위와
      diagnostic 해석을 다시 정렬했다.
      `socket_runtime.cpp` `public_api_sync` fast-mutex split candidate는
      public `PAIR tcp/inproc 64B -32.03% / -29.11%`,
      `DEALER_DEALER tcp/inproc -19.33% / -31.37%`로
      `PAIR`와 `DEALER_DEALER inproc`을 accepted baseline보다 더 악화시켜
      원복했다.
      같은 축의 `pipe.hpp` / `pipe.cpp` hot send-only non-recursive lock split도
      public `PAIR tcp/inproc 64B -17.14% / -34.56%`,
      `DEALER_DEALER tcp/inproc -13.65% / -19.47%`,
      raw `PAIR tcp/inproc -8.61% / -25.46%`,
      `DEALER_DEALER tcp/inproc -20.69% / -21.15%`로
      `PAIR inproc` absolute throughput을 크게 무너뜨려 원복했다.
      같은 축의 `socket_runtime.hpp` / `socket_runtime.cpp`
      send-side lifecycle/scope hot-path header-inline codegen-only candidate도
      public `PAIR tcp/inproc 64B -21.70% / -23.83%`,
      `DEALER_DEALER tcp/inproc -10.64% / -17.99%`,
      raw `PAIR tcp/inproc -11.65% / -27.25%`,
      `DEALER_DEALER tcp/inproc -8.99% / -25.87%`로
      broad win이 아니어서 원복했다.
      이어서 `socket_public_send_scope_t` constructor lazy-sync acquire도
      public `PAIR tcp/inproc -9.29% / -27.64%`,
      `DEALER_DEALER tcp/inproc -26.48% / -25.53%`,
      raw `PAIR tcp/inproc -13.68% / -25.35%`,
      `DEALER_DEALER tcp/inproc -25.66% / -19.94%`로
      broad win이 아니어서 원복했다.
      이어서 `pipe.cpp` flush notify-outside-`_out_sync` candidate도
      public `PAIR tcp/inproc -16.34% / -25.54%`,
      `DEALER_DEALER tcp/inproc -26.94% / -23.32%`,
      raw `PAIR tcp/inproc -30.05% / -18.28%`,
      `DEALER_DEALER tcp/inproc -7.24% / -17.59%`로
      broad win이 아니어서 원복했다.
      이어서 `PAIR`까지 public send scope를 넓히고
      `pipe` serialized write를 같은 exclusion으로 합치려는 candidate도
      직렬 rerun public `PAIR tcp/inproc -24.15% / -27.75%`,
      `DEALER_DEALER tcp/inproc -20.80% / -19.17%`,
      raw `PAIR tcp/inproc -8.40% / -22.59%`,
      `DEALER_DEALER tcp/inproc -5.92% / -20.81%`로
      `PAIR` public이 accepted baseline보다 더 악화돼 원복했다.
      이어서 `socket_runtime.cpp` `public_api_state` exact-state fast path도
      public `PAIR tcp/inproc -14.49% / -31.80%`,
      `DEALER_DEALER tcp/inproc -12.85% / -21.98%`,
      raw `PAIR tcp/inproc -12.00% / -24.87%`,
      `DEALER_DEALER tcp/inproc -23.71% / -16.20%`로
      `PAIR inproc` public과 `DEALER tcp` raw가 함께 흔들려 원복했다.
      따라서 guide 재작성 트리거가 다시 걸렸고,
      current iteration에서는 code patch 전에 요구된
      guide/review/hot-path 우선순위 재정렬과
      diagnostic 해석 갱신을 먼저 끝냈다.
- [x] non-thread-safe owner-thread `pipe` send structural candidate를
      authority baseline과 contract gate로 검증하고 reject했다.
      baseline
      [`perf_linux_20260330_082101_codex_20260330_owner_thread_pipe_send_baseline_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260330_082101_codex_20260330_owner_thread_pipe_send_baseline_public.txt)
      를 다시 찍은 뒤
      `pair.cpp` / `lb.cpp` / `router.cpp` / `dist.cpp`가
      `_out_sync` 없이 owner-thread `write/flush` helper를 쓰도록 올렸지만,
      authority gate
      `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      에서 `test_public_inproc_multipart_send`가
      `Assertion failed: check () (.../msg.cpp:579)`로 abort해
      performance stage로 올리지 않고 전부 원복했다.
      원복 뒤 같은 gate rerun은 다시 통과했다.
- [x] `_out_sync` 아래 activation + credit publication을 atomic lane으로
      떼는 structural candidate를 검증하고 reject했다.
      `pipe.hpp` / `pipe.cpp`에서 `_out_pipe` lifetime / termination은
      `_out_sync` 아래에 유지한 채
      `process_activate_read()` / `process_activate_write()` /
      `_peers_msgs_read` publication만 atomic lane으로 분리했다.
      targeted gate
      `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      는 통과했고,
      primary `multi dealer_dealer tcp 64B`는 same-day baseline
      `-17.28% -> -11.32%`까지 회복했다.
      하지만 single은
      `DEALER_DEALER public tcp/inproc -26.82% / -28.76%`,
      `PAIR public tcp/inproc -29.83% / -28.31%`,
      raw `PAIR tcp/inproc -20.69% / -24.67%`,
      raw `DEALER_DEALER tcp/inproc -24.98% / -25.98%`로 mixed였고,
      broader multi guardrail에서도
      `dealer_router tcp 64B -2.19%`,
      `router_router tcp 64B -6.74%`,
      `pubsub tcp 64B +8.35%`가 나와
      `router_router tcp` guardrail을 넘겼다.
      따라서 keep하지 않고 전부 원복했다.
- [x] `pipe.hpp` / `pipe.cpp` lifecycle cleanup atomic split candidate를
      검증하고 reject했다.
      `process_activate_read()/process_pipe_term()/process_delimiter()/
      set_nodelay()` 쪽 lifecycle state를 atomic snapshot/CAS로 떼고
      `_out_sync`를 publication cleanup에 더 가깝게 남기려 했지만,
      targeted gate
      `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      는 통과한 반면,
      primary `multi dealer_dealer tcp 64B`는
      `1985.72 -> 1605.14 Kmsg/s (-19.17%)` baseline에서
      `1978.99 -> 1625.42 Kmsg/s (-17.87%)`로 일부 회복하는 데 그쳤고,
      single public/raw는
      `PAIR public tcp/inproc -20.43% / -30.28%`,
      `DEALER_DEALER public tcp/inproc -14.41% / -33.16%`,
      raw `PAIR tcp/inproc -30.39% / -21.58%`,
      raw `DEALER_DEALER tcp/inproc -35.64% / -24.72%`로
      broad guardrail을 함께 지키지 못했다.
      따라서 전부 원복했고, 같은 ownership-split family는 문서 재정렬 없이
      바로 반복하지 않는다.
- [x] `a819ea3a` send admission/CAS 의미 단위 structural prep를
      current code에 retained change로 남겼다.
      current `socket_base_msg.cpp` / `socket_runtime.cpp` /
      `multipart_send_txn.cpp` send path는
      public inflight admission, same-handle send serialization,
      retry 시 sync unlock/relock, logical multipart scope reuse가
      `socket_public_send_scope_t`와 direct send helpers에 함께 엉켜 있다.
      이번 단계에서는 rejected micro-tweak family를 다시 파는 대신,
      위 의미 단위를 code-level 경계로 먼저 분리하는 retained structural prep다.
      최소 범위였던
      `socket_runtime.hpp/.cpp` retry sync phase,
      `socket_base_msg.cpp` direct send/retry runner,
      `multipart_send_txn.cpp` shared send-scope reuse를
      같은 contract 아래에서 다시 정리했고,
      same-handle concurrent send/publish 회귀와
      `PAIR` / `DEALER_DEALER` public/raw guardrail도 다시 확인했다.
- [x] retained send admission boundary prep 위에서
      actual common send-side structural candidate를 다시 골랐다.
      `socket_runtime.hpp/.cpp`
      `public_api_inflight/public_api_closing/public_api_sync` split candidate를
      시도했지만,
      targeted ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
      는 통과한 반면,
      public `PAIR tcp/inproc -24.72% / -26.53%`,
      `DEALER_DEALER tcp/inproc -19.44% / -22.01%`,
      raw `PAIR tcp/inproc -9.25% / -17.70%`,
      `DEALER_DEALER tcp/inproc -17.73% / -31.95%`로
      `PAIR` public과 `DEALER inproc` raw가 함께 악화돼 원복했다.
      즉 `a819ea3a` 잔여 비용을 public lifecycle coordinator state packing
      하나로 줄이는 family는 keep-worthy broad win이 아니었다.
- [x] retained send admission boundary prep + `_out_sync` unlocked helper 위에서
      actual common send-side structural candidate의 다음 라운드를 진행했다.
      current tree의
      `send_direct_with_retry()` /
      `socket_public_send_scope_t::should_hold_sync_during_retry()` /
      `socket_base_t::direct_send_needs_public_api_sync()` 경계와
      `pipe` helper 경계를 유지한 채,
      `pipe.hpp` / `pipe.cpp`
      `process_activate_write()` already-active peer-progress snapshot split을
      시도했다.
      targeted ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup)$' -j1`
      는 통과했고,
      targeted public rerun은
      `PAIR tcp/inproc -13.02% / -17.35%`,
      `DEALER_DEALER tcp/inproc -14.66% / -18.53%`,
      raw rerun은
      `PAIR tcp/inproc -9.67% / -19.42%`,
      `DEALER_DEALER tcp/inproc -8.19% / -19.85%`까지 회복했다.
      하지만 broader single에서
      `PAIR tcp/inproc -8.43% / -21.82%`,
      `PUBSUB tcp/inproc -18.27% / -15.75%`,
      `DEALER_DEALER tcp/inproc -11.48% / -29.32%`,
      `DEALER_ROUTER tcp/inproc -19.84% / -30.93%`,
      partial `ROUTER_ROUTER tcp -52.69%`가 나왔고,
      `comp_zlink_router_router zlink inproc 64` hang까지 생겨 원복했다.
      따라서 peer-progress snapshot-only split family는 keep-worthy broad win이
      아니었다.
- [x] send admission boundary prep + `_out_sync` unlocked helper 위의 다음
      common send-side structural round에서,
      existing public send sync가 이미 잡힌 `DEALER` / `ROUTER`
      caller만 `pipe` send serialization을 대신 맡도록 내리는
      `send_serialized` helper candidate를 검증했다.
      `libzmq` `socket_base.cpp` / `pipe.cpp` / `lb.cpp` / `pair.cpp`
      reference pass와 stdin 기반 `claude -p` consult 뒤,
      `pipe.hpp` / `pipe.cpp` / `lb.hpp` / `lb.cpp` / `dealer.cpp` /
      `router.cpp`에 candidate를 올렸다.
      targeted ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup)$' -j1`
      는 통과했지만,
      broader public single은
      `PAIR tcp/inproc -13.23% / -17.01%`,
      `DEALER_DEALER tcp/inproc -23.74% / -31.19%`,
      `DEALER_ROUTER tcp/inproc -29.36% / -28.30%`,
      `ROUTER_ROUTER tcp/inproc -55.88% / -22.76%`,
      raw guardrail은
      `PAIR tcp/inproc -18.50% / -22.98%`,
      `DEALER_DEALER tcp/inproc -20.96% / -23.13%`로
      broad win이 아니어서 원복했다.
- [x] 다음 common send-side structural round는
      `process_activate_write()` peer-progress family와
      existing public-send-sync-held `send_serialized` pipe helper family를
      반복하지 않고,
      current code에서도 public send sync가 `_out_sync` steady-state duty를
      대체하지 못한다는 점을 전제로
      `DEALER` same-handle send serialization을 `public_api_sync` 밖
      external recursive mutex +
      external `socket_public_send_scope_t` serialized scope로 옮기는
      후보 하나만 골라 검증했다.
      `libzmq` `socket_base.cpp` / `pipe.cpp` / `lb.cpp` / `pair.cpp`
      reference pass 뒤 stdin 기반 `claude -p` consult는
      `timeout 60s`로 종료돼 unusable로 기록했고,
      `socket_runtime.hpp` / `socket_runtime.cpp` /
      `socket_base.hpp` / `socket_base_api.cpp` /
      `socket_base_msg.cpp` / `multipart_send_txn.cpp` /
      `dealer.hpp` / `dealer.cpp` /
      `unittest_socket_runtime.cpp`에 candidate를 올렸다.
      targeted ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
      는 통과했지만,
      public는
      `PAIR tcp/inproc -9.23% / -16.03%`,
      `DEALER_DEALER tcp/inproc -13.09% / -32.97%`,
      raw는
      `PAIR tcp/inproc -13.88% / -26.40%`,
      `DEALER_DEALER tcp/inproc -24.35% / -33.20%`로
      targeted stage부터 broad win이 아니어서 전부 원복했다.
- [x] 다음 common send-side structural round는
      guide/review가 current code priority로 둔
      `hot send` 대 `rare teardown/outpipe lifetime` duty split을
      existing public send sync가 이미 잡힌 `DEALER` caller에서만
      final `write+flush` hot-send lease로 시도했다.
      `pair_inproc_send_profile_20260328.txt` /
      `dealer_inproc_send_profile_20260328.txt` 재독해와
      `libzmq` `socket_base.cpp` / `pipe.cpp` / `lb.cpp` / `pair.cpp`
      reference pass 뒤,
      `pipe.hpp` / `pipe.cpp` / `lb.hpp` / `lb.cpp` / `dealer.cpp` /
      `socket_base.hpp`에 candidate를 올렸다.
      targeted ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm)$' -j1`
      는 통과했지만,
      targeted public
      `PAIR tcp/inproc -28.83% / -19.45%`,
      `DEALER_DEALER tcp/inproc -15.40% / -22.79%`로
      public stage부터 broad win이 아니어서
      raw guardrail로 승격하지 않고 전부 원복했다.
- [x] 다음 common send-side structural round는
      `process_activate_write()` peer-progress family,
      existing public-send-sync-held `send_serialized` pipe helper family,
      `DEALER` external send-state mutex / external send-serialized scope
      family,
      existing public-send-sync-held pipe hot-send lease / outpipe lifetime
      split family를 반복하지 않고,
      current code에서도 public send sync나 caller-owned serialization reuse가
      `_out_sync` steady-state duty를 대체하지 못한다는 점을 전제로
      `send scope construct + pipe serialization` 의미 단위를 다른 구조로
      다시 가르는 후보 하나만 고른다.
      시작은 current kept boundary
      (`send_direct_with_retry()` /
      `socket_public_send_scope_t::should_hold_sync_during_retry()` /
      `socket_base_t::direct_send_needs_public_api_sync()` /
      `_out_sync` unlocked helper)
      와 guide/review/hot-path summary 재독해로 한다.
      이번 라운드의 구체 후보로
      [`core/src/utils/fast_mutex.hpp`](/home/hep7/project/kairos/zlink/core/src/utils/fast_mutex.hpp)
      의 recursive `fast_mutex_t` primitive replacement를 검증했다.
      `claude --help`는 통과했지만 stdin 기반 `claude -p` consult는
      응답 없이 끝났고 prompt 인자 재시도는
      `Input must be provided either through stdin or as a prompt argument`
      오류로 usable advisory를 얻지 못해 unavailable로 기록했다.
      candidate code는 `fast_mutex.hpp`와
      `unittest_socket_runtime.cpp`에만 올렸고,
      `cmake --build core/build -j$(nproc)` 뒤
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      gate를 재실행해 통과를 확인했다.
      initial parallel ctest는 build와 겹쳐
      `test_stream_send_blocking_wakeup`에서
      `libzlink.so.5 file too short` loader race를 냈지만,
      build 완료 뒤 동일 gate rerun은 전부 통과했다.
      targeted public은
      `PAIR tcp/inproc -27.78% / -17.52%`,
      `DEALER_DEALER tcp/inproc +3.72% / -21.03%`,
      raw는
      `PAIR tcp/inproc -13.25% / -21.63%`,
      `DEALER_DEALER tcp/inproc -7.74% / -15.86%`였고,
      unchanged control인 `PAIR public tcp`와 raw `PAIR inproc` guardrail을
      함께 지키지 못해 전부 원복했다.
- [x] 다음 common send-side structural round는
      `process_activate_write()` peer-progress family,
      existing public-send-sync-held `send_serialized` pipe helper family,
      `DEALER` external send-state mutex / external send-serialized scope
      family,
      existing public-send-sync-held pipe hot-send lease / outpipe lifetime
      split family,
      `fast_mutex.hpp` native recursive pthread primitive replacement family를
      반복하지 않고,
      current code에서도 public send sync나 caller-owned serialization reuse가
      `_out_sync` steady-state duty를 대체하지 못한다는 점을 전제로
      `send scope construct + pipe serialization` 의미 단위를 다른 구조로
      다시 가르는 후보 하나만 고른다.
      시작은 current kept boundary
      (`send_direct_with_retry()` /
      `socket_public_send_scope_t::should_hold_sync_during_retry()` /
      `socket_base_t::direct_send_needs_public_api_sync()` /
      `_out_sync` unlocked helper)
      와 guide/review/hot-path summary 재독해로 한다는 규칙까지는 유지하되,
      2026-03-28 current tree에서는 above family 다섯 계열이 연속 reject되어
      6.2 guide 재작성 trigger가 이미 켜졌음을 확인했다.
      따라서 immediate next step은 또 다른 local code patch가 아니라
      guide/review/hot-path 우선순위를 먼저 다시 정렬하는 쪽으로
      source-of-truth를 고쳤다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 90s claude -p --permission-mode bypassPermissions ...`
      consult는 다시 timeout으로 끝나 latest advisory는 unavailable로 남겼다.
      current tree 검증으로
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      concurrent public/raw baseline refresh도 한 번 돌려 결과 파일은 남겼지만,
      두 run을 겹쳐 띄운 탓에 baseline authority로는 쓰지 않고
      noisy diagnostic artifact로만 취급한다.
      이 단계의 실제 다음 exact step은
      historical `ff0140e5 -> a819ea3a -> 98e7d324 -> 9b91234c`
      map과 current retained boundary를 다시 붙여
      current residual direct cause를
      `admission floor` 대 `pipe floor`로 먼저 재서술한 뒤,
      serial current-tree public/raw refresh를 거쳐
      새 broad hypothesis를 다시 여는 것이다.
- [x] serial current-tree `PAIR` / `DEALER_DEALER` public/raw refresh를
      authority baseline으로 다시 찍고,
      historical `a819ea3a` admission floor 대 `ff0140e5` pipe floor를
      current residual direct cause 기준으로 다시 썼다.
      `claude --help`는 통과했지만 prompt 인자 기반
      `timeout 120s claude -p --permission-mode bypassPermissions ...`
      consult는 응답 없이 종료돼 latest advisory는 unusable/unavailable로
      기록했다.
      serial public refresh는
      `PAIR tcp/inproc -12.03% / -17.18%`,
      `DEALER_DEALER tcp/inproc -11.12% / -18.60%`,
      raw refresh는
      `PAIR tcp/inproc -8.63% / -23.67%`,
      `DEALER_DEALER tcp/inproc -12.06% / -20.63%`였다.
      따라서 wrapper/raw surface 차이나 dealer-only `public_api_sync` reuse가
      current common residual의 본체는 아니고,
      next exact step은 current kept boundary 위에서
      common data-plane admission을 full public lifecycle coordinator 아래의
      더 얇은 steady-state lease로 내리는 structural family를 하나 골라
      실제 `core/` patch와 targeted guardrail로 검증하는 것이다.
- [x] current kept boundary 위 dedicated public send lease split
      structural candidate를 검증하고 reject했다.
      `socket_runtime.hpp` / `socket_runtime.cpp` /
      `unittest_socket_runtime.cpp`에 candidate를 올렸고,
      `claude --help`는 통과했지만 prompt 인자 기반 `claude -p`는
      `Input must be provided either through stdin or as a prompt argument`
      오류로 unusable이었으며,
      stdin 기반 `timeout 120s claude -p ...` 재시도도 `code 124`로
      끝나 latest advisory는 unavailable로 기록했다.
      candidate 적용 뒤
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      다만 same-tag public/raw parallel run
      (`215617`, `215700`)은 noisy diagnostic으로만 두고,
      single-process authority public rerun
      `PAIR tcp/inproc -15.65% / -25.43%`,
      `DEALER_DEALER tcp/inproc -24.41% / -32.10%`
      ([`perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_215743_codex_20260328_send_lease_public_authority.txt))
      로 baseline보다 더 악화됨을 확인해 전부 원복했다.
- [x] same-handle concurrent `PUB` publish contract regression
      (`test_pubsub_publish_is_safe_from_multiple_threads`)을 고치고
      logical multipart send scope를 재검증했다.
- [x] send-side lifecycle/backpressure 공통 후보를 current code 기준으로
      소진했다. `public_api_state` CAS fast path, `PAIR` no-sync send scope,
      idle send-ready retry gate, generic same-thread `send_activate_read()`
      direct delivery 모두 keep-worthy broad win을 만들지 못했다.
      따라서 helper-level common lifecycle/backpressure 축은
      새 근거가 나올 때만 다시 올리고,
      actual next code 후보는 여전히 common send-side structural round
      (`a819ea3a` admission/CAS 의미 단위 + `_out_sync` steady-state duty)
      안에서 고른다.
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
- [x] `test_public_inproc_router_recv_multipart_with_source_rid_blocking()`으로
      `zlink_recv()` routed multipart source-rid contract를 회귀에 추가했다.
- [x] `test_public_inproc_router_msg_recv_rid_keeps_source_rid_across_reset()`으로
      `zlink_msg_recv_rid()` multipart reset 뒤 source-rid 유지 contract를
      회귀에 추가했다.
- [x] `ROUTER_ROUTER` routed path를 패턴 전용으로 본다.
      `socket_message_send_api.cpp` / `multipart_send_txn.cpp` /
      `socket_base_msg.cpp` / `router.cpp` routed-data view candidate도
      first/rerun `ROUTER_ROUTER tcp/inproc 64B`
      `-58.62% / -30.04%`, `-55.12% / -29.06%`로
      stable broad win이 아니어서 current code에는 남기지 않았다.
      이어서 latest recheck에서
      `ROUTER_ROUTER` default/raw는
      `tcp/inproc -56.84% / -28.68%`,
      `-58.04% / -23.52%`였고,
      `DEALER_ROUTER` current recheck도
      `tcp/inproc -30.83% / -31.56%`였다.
      같은 blocking default path에서
      `pipe.cpp` / `router.cpp` `xsend_routed()` final-part one-lock helper도
      first complete run `-54.37% / -23.05%`,
      transport-split rerun `tcp -57.14%`, `inproc -29.36%`로
      zlink 절대 throughput을 거의 못 움직여 current code에는 남기지 않았다.
      이번 라운드의
      `socket_message_send_api.cpp` public `ROUTER` nonblocking envelope
      same-path fast path도
      [`perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_053009_codex_20260329_router_public_nonblocking_envelope.txt)
      `ROUTER_ROUTER tcp/inproc -58.16% / -31.94%`로 accepted baseline보다 더
      나빴고, active hot loop가 아니라 handshake path에만 걸려 current
      code에는 남기지 않았다.
      `socket_runtime.cpp` / `socket_base_msg.cpp` shared logical multipart
      entry-state reuse candidate도
      [`perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054339_codex_20260329_multipart_sender_regime_public.txt)
      first `ROUTER_ROUTER tcp/inproc -54.15% / -29.86%`,
      [`perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_054423_codex_20260329_multipart_sender_regime_public_rerun.txt)
      rerun `-58.08% / -22.16%`였지만,
      zlink absolute throughput이
      `tcp 1296.50 -> 1292.20`, `inproc 2572.46 -> 2574.88 msg/s`로 거의
      안 움직여 current code에는 남기지 않았다.
      latest same-target routed send cache + one-lock combo도
      direct `comp_zlink_router_router` absolute throughput을
      `tcp 1214300.00`, `inproc 2419754.40 msg/s`로 거의 못 움직여
      current code에는 남기지 않았다.
      이어서 `router.cpp` routed recv current-in/source-rid cache와
      lazy prefetched-id prepare도
      default `ROUTER_ROUTER tcp/inproc -57.01% / -22.77%`,
      raw `-52.96% / -20.99%`,
      `DEALER_ROUTER tcp/inproc -27.89% / -30.42%`였지만,
      direct `comp_zlink_router_router` absolute throughput이
      `tcp 1211724.60`, `inproc 2408252.00 msg/s`로 baseline 수준에 머물러
      current code에는 남기지 않았다.
      대신 routed recv/source-rid contract는
      `test_public_inproc_router_recv_multipart_with_source_rid_blocking()`과
      `test_public_inproc_router_msg_recv_rid_keeps_source_rid_across_reset()`
      회귀로 고정했다.
      즉 current `ROUTER_ROUTER` 잔여 gap은 aggregate wrapper 한 겹이나
      send-side local cache / final-part micro-fusion / local recv-state cache보다
      routed recv ordering, `recv_routed()` source-rid export,
      공통 `_out_sync` serialization floor 차이가 더 큰 축일 가능성이 높다.
- [x] `recv_routed()` routed source-rid zeroing-floor candidate를 검증하고
      reject했다.
      stdin 기반
      `printf '...' | timeout 25s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 usable output 없이 `code 124` timeout으로 끝나
      advisory 없이 진행했다.
      candidate는 `socket_base_msg.cpp` /
      `socket_message_recv_api.cpp` /
      `test_public_inproc_multipart_send.cpp`에만 올렸고,
      targeted build/ctest
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_router_mandatory_hwm|test_stream_socket)$' -j1`
      는 통과했다.
      다만 concurrent public/raw diagnostic
      `055430` pair는 noisy artifact로만 두고,
      authority public
      `ROUTER_ROUTER tcp/inproc -56.10% / -32.31%`,
      authority raw
      `-57.70% / -22.00%`로 broad win을 만들지 못했다.
      zlink absolute throughput도 public `1297.34 / 2575.26`,
      raw `1295.90 / 2570.82 Kmsg/s`로 기존 범위에 머물러
      keep-worthy delta가 아니어서 전부 원복했다.
- [x] 이번 단계 send-path 변경 뒤 `PAIR`/`DEALER_DEALER` raw/public 분리를
      다시 기록했다.
- [x] broader single / multi smoke까지 통과하는 안정 지점을 남겼다.
- [x] `_out_sync` invariant map을 current tree의 unlocked helper로 반영하고,
      direct instrumentation 결과를 다음 structural candidate의 기준선으로
      남겼다.
- [x] same shared `public_api_state` 안에서 public/callback inflight와
      direct send inflight를 separate lane으로 가르는 structural candidate를
      검증하고 reject했다.
      `claude --help`는 통과했지만 stdin 기반 `claude -p` consult는 다시
      `code 124`로 끝나 unavailable로 기록했다.
      candidate 적용 뒤
      `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`,
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R 'unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe' -j1`
      는 통과했다.
      same-tag public/raw parallel launch (`225749`)은 noisy diagnostic으로만
      두고, authority public
      `PAIR tcp/inproc -20.45% / -24.95%`,
      `DEALER_DEALER tcp/inproc -23.79% / -23.41%`,
      authority raw
      `PAIR tcp/inproc -22.49% / -24.59%`,
      `DEALER_DEALER tcp/inproc -10.46% / -19.57%`로
      baseline보다 크게 악화돼 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `public_api_sync` wait primitive를 recursive mutex-backed sync로
      다시 가르는 structural candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 prompt argument 기반 `claude -p`는
      `Input must be provided either through stdin or as a prompt argument when using --print`
      오류로 unusable이었고, stdin 기반 `claude -p` 재시도도 `code 124`로
      끝나 unavailable로 기록했다.
      candidate는
      `socket_runtime.hpp` / `socket_runtime.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      same-tag public/raw parallel launch (`231525`)은 noisy diagnostic으로만
      두고, authority public
      `PAIR tcp/inproc -16.85% / -21.61%`,
      `DEALER_DEALER tcp/inproc -18.65% / -36.20%`,
      authority raw
      `PAIR tcp/inproc -7.34% / -17.60%`,
      `DEALER_DEALER tcp/inproc -18.83% / -34.78%`로
      baseline보다 크게 악화돼 전부 원복했다.
      원복 뒤 첫 ctest는 build와 겹쳐
      `libzlink.so.5: file too short` /
      BAD_COMMAND를 냈지만,
      build 완료 뒤 같은 gate rerun은 다시 통과했다.
- [x] post-recursive current-tree serial public/raw refresh를 다시 찍고
      late-session low baseline 반복 여부를 확인했다.
      current code는 바꾸지 않았고,
      `python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_public`,
      `PERF_SINGLE_ZLINK_RAW_MSG_API=1 python3 core/bench/with_zmq/single/run_comparison.py --patterns PAIR,DEALER_DEALER --msg-sizes 64 --transport tcp,inproc --runs 1 --build-dir core/build --results-tag codex_20260328_post_recursive_sync_refresh_raw`,
      그리고 같은 조건 rerun
      `codex_20260328_post_recursive_sync_refresh_public_rerun`,
      `codex_20260328_post_recursive_sync_refresh_raw_rerun`
      을 순차로 다시 실행했다.
      first serial refresh는
      public `PAIR tcp/inproc -24.67% / -14.98%`,
      `DEALER_DEALER tcp/inproc -14.63% / -32.73%`,
      raw `PAIR tcp/inproc -23.51% / -23.04%`,
      `DEALER_DEALER tcp/inproc -32.68% / -34.30%`였고,
      rerun도
      public `PAIR tcp/inproc -27.72% / -18.03%`,
      `DEALER_DEALER tcp/inproc -21.31% / -32.12%`,
      raw `PAIR tcp/inproc -23.54% / -25.42%`,
      `DEALER_DEALER tcp/inproc -23.26% / -25.44%`로
      earlier authority보다 낮은 session-local baseline이 반복됐다.
      따라서 next candidate는 21:23/21:24 authority baseline과
      23:25/23:30 session-local low baseline을 둘 다 guardrail로 본다.
- [x] `pipe.cpp` final-part `write_and_flush()` lock-free snapshot candidate를
      검증하고 reject했다.
      candidate는 `pipe.hpp` / `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      public
      `perf_linux_20260328_234340_codex_20260328_pipe_write_flush_hot_snapshot_public.txt`
      에서
      `PAIR tcp/inproc -32.16% / -20.55%`,
      `DEALER_DEALER tcp/inproc -9.76% / -23.34%`로
      `PAIR`가 early authority와 session-local low baseline 둘 다 못 지켜
      public stage에서 바로 reject했고 raw는 실행하지 않았다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `pipe::_out_sync` plain non-recursive fast mutex candidate를
      검증하고 reject했다.
      `claude -p` consult는 출력 없이 `code 124` timeout으로 끝나
      unavailable로 기록했다.
      candidate는 `core/src/utils/fast_mutex.hpp` / `pipe.hpp` /
      `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260328_235615_codex_20260329_pipe_plain_mutex_public.txt)
      에서
      `PAIR tcp/inproc -22.34% / -24.82%`,
      `DEALER_DEALER tcp/inproc -9.78% / -32.93%`로
      `PAIR`와 `DEALER_DEALER inproc`이 both guardrail을 못 지켜
      public stage에서 바로 reject했고 raw는 실행하지 않았다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `pipe.hpp` / `pipe.cpp` non-conflate out-pipe concrete `ypipe_t`
      fast path candidate를 검증하고 reject했다.
      이번 candidate 전용 `claude` consult는 실행하지 못했고,
      candidate는 `pipe.hpp` / `pipe.cpp`만 건드렸다.
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002224_codex_20260329_pipe_concrete_public.txt)
      `PAIR tcp/inproc -18.01% / -35.55%`,
      `DEALER_DEALER tcp/inproc -15.12% / -24.02%`,
      raw
      [`perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_002310_codex_20260329_pipe_concrete_raw.txt)
      `PAIR tcp/inproc -8.81% / -28.27%`,
      `DEALER_DEALER tcp/inproc -7.64% / -22.08%`로
      public과 raw `inproc` guardrail을 함께 못 지켜 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `socket_base_api.cpp` / `socket_base_msg.cpp` /
      `socket_message_send_api.cpp` public API-boundary same-handle recursive
      mutex single-part fast path candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 prompt/ststdin 기반 `claude -p` consult는
      여전히 unusable/unavailable이어서 이번 candidate 전용 advisory는
      얻지 못했다.
      candidate는 `socket_base.hpp` / `socket_base_api.cpp` /
      `socket_base_msg.cpp` / `socket_message_send_api.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005659_codex_20260329_api_locked_send_public.txt)
      `PAIR tcp/inproc -14.34% / -31.65%`,
      `DEALER_DEALER tcp/inproc -13.44% / -24.93%`,
      `ROUTER_ROUTER tcp/inproc -57.41% / -23.50%`,
      raw
      [`perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_005807_codex_20260329_api_locked_send_raw.txt)
      `PAIR tcp/inproc -17.38% / -31.55%`,
      `DEALER_DEALER tcp/inproc -11.26% / -19.57%`,
      `ROUTER_ROUTER tcp/inproc -57.61% / -23.28%`로
      `PAIR` / `DEALER_DEALER` `inproc`와 routed `ROUTER_ROUTER` guardrail을
      함께 못 지켜 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `socket_runtime.hpp` / `socket_runtime.cpp` /
      `socket_base_msg.cpp` same-thread parked send admission lease candidate를
      검증하고 reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink ...`
      consult는 출력 없이 `code 124` timeout으로 끝나 usable advisory를
      얻지 못했다.
      candidate는 `socket_runtime.hpp` / `socket_runtime.cpp` /
      `socket_base_msg.cpp` / `unittest_socket_runtime.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012903_codex_20260329_send_parked_lease_public.txt)
      `PAIR tcp/inproc -13.55% / -29.90%`,
      `DEALER_DEALER tcp/inproc -23.40% / -22.04%`,
      raw
      [`perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_012948_codex_20260329_send_parked_lease_raw.txt)
      `PAIR tcp/inproc -24.41% / -23.24%`,
      `DEALER_DEALER tcp/inproc -23.28% / -14.47%`로
      targeted public/raw guardrail을 함께 못 지켜 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `pipe.hpp` / `pipe.cpp` peer-progress refreshed HWM-credit cache
      candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 prompt/ststdin 기반
      `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink`
      consult는 출력 없이 `code 124` timeout으로 끝나 usable advisory를
      얻지 못했다.
      candidate는 `pipe.hpp` / `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014653_pipe_hwm_credit_public_20260329.txt)
      `PAIR tcp/inproc -15.04% / -24.43%`,
      `DEALER_DEALER tcp/inproc -8.68% / -32.46%`,
      raw
      [`perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_014732_pipe_hwm_credit_raw_20260329.txt)
      `PAIR tcp/inproc -20.98% / -20.36%`,
      `DEALER_DEALER tcp/inproc -20.37% / -22.58%`로
      targeted public/raw guardrail을 함께 못 지켜 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      도 다시 통과했다.
- [x] `core/src/core/msg.cpp` `msg_t::init_size()/close()` small-lmsg pooled
      materialize/free candidate를 검증하고 reject했다.
      이번 candidate 전용 `claude` consult는 실행하지 않았다.
      candidate는 `core/src/core/msg.cpp` /
      `core/tests/unittest/unittest_msg_pool.cpp` /
      `core/tests/unittest/CMakeLists.txt`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_msg_pool|unittest_msg_view|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_022632_codex_20260329_msg_pool_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_022632_codex_20260329_msg_pool_public.txt)
      `PAIR tcp/inproc -15.43% / -24.47%`,
      `DEALER_DEALER tcp/inproc -32.71% / -35.28%`,
      raw
      [`perf_linux_20260329_022712_codex_20260329_msg_pool_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_022712_codex_20260329_msg_pool_raw.txt)
      `PAIR tcp/inproc -33.38% / -25.71%`,
      `DEALER_DEALER tcp/inproc -16.21% / -36.83%`로
      `DEALER_DEALER` public과 raw `PAIR/DEALER` guardrail을 함께 못 지켜
      reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_msg_view|test_public_inproc_multipart_send|test_multi_socket_contract_regressions)$' -j1`
      도 다시 통과했다.
- [x] `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp` send-side layout regroup
      candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 출력 없이 `code 124` timeout으로 끝나 usable advisory를
      얻지 못했다.
      candidate는 `socket_runtime.hpp` / `pipe.hpp` / `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe)$' -j1`
      는 통과했다.
      same-tag public/raw parallel diagnostic
      [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_public.txt)
      `PAIR tcp/inproc -14.64% / -25.65%`,
      `DEALER_DEALER tcp/inproc -12.23% / -38.09%`,
      [`perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_024626_codex_20260329_send_layout_regroup_raw.txt)
      `PAIR tcp/inproc -24.84% / -18.60%`,
      `DEALER_DEALER tcp/inproc -22.59% / -19.83%`로
      public/raw guardrail을 함께 못 지켜 reject했고 전부 원복했다.
      원복 뒤 clean rebuild
      `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `socket_base.hpp` / `socket_base_msg.cpp`
      preflight-before-public-admission candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
      candidate는 `socket_base.hpp` / `socket_base_msg.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_030207_codex_20260329_preflight_before_admission_public.txt)
      `PAIR tcp/inproc -26.28% / -26.58%`,
      `DEALER_DEALER tcp/inproc -24.14% / -35.55%`로
      public stage에서 early authority와 session-local low baseline을 함께
      못 지켜 raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 clean rebuild
      `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `socket_runtime.hpp` / `socket_runtime.cpp`
      `public_api_inflight/public_api_closing/public_api_sync`
      split family를 stronger contract gate로 다시 확인했지만 reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
      candidate는 `socket_runtime.hpp` / `socket_runtime.cpp` /
      `unittest_socket_runtime.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_031939_codex_20260329_close_sync_state_split_public.txt)
      `PAIR tcp/inproc -17.22% / -24.11%`,
      `DEALER_DEALER tcp/inproc -14.31% / -29.56%`로
      public stage에서 early authority와 session-local low baseline을 함께
      못 지켜 raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 clean rebuild
      `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `socket_base_msg.cpp` / `pair.cpp` / `dealer.cpp` / `lb.cpp`
      plain non-routed final-part sender-regime split candidate를 검증하고
      reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 120s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 끝까지 출력이 없었고 `code 124` timeout으로 종료됐다.
      candidate는 `socket_base.hpp` / `socket_base_msg.cpp` / `pair.hpp` /
      `pair.cpp` / `dealer.hpp` / `dealer.cpp` / `lb.hpp` / `lb.cpp`만
      건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_034150_codex_20260329_plain_final_regime_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_034150_codex_20260329_plain_final_regime_public.txt)
      `PAIR tcp/inproc -12.23% / -29.92%`,
      `DEALER_DEALER tcp/inproc -11.44% / -34.04%`로
      public stage에서 early authority와 session-local low baseline을 함께
      못 지켜 raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 clean rebuild
      `cmake --build core/build -j$(nproc)`와 같은 ctest gate도
      다시 통과했다.
- [x] `pipe.hpp` / `pipe.cpp` `_lwm` boundary `activate_write`
      progress-command coalesce candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 stdin/prompt 기반
      `claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult 시도들은 usable output 없이 멈췄고 short retry도
      `code 124` timeout으로 끝났다.
      candidate는 `pipe.hpp` / `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      는 통과했다.
      public
      [`perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_041240_codex_20260329_activate_write_coalesce_public.txt)
      `PAIR tcp/inproc -10.58% / -34.54%`,
      `DEALER_DEALER tcp/inproc -30.23% / -25.32%`로
      public stage에서 early authority와 session-local low baseline을 함께
      못 지켜 raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      도 다시 통과했다.
- [x] current-tree `pipe_write_and_flush` split instrumentation으로
      successful `flush` path dominance를 확인한 뒤,
      `ypipe` combined write+publication candidate를 검증하고 reject했다.
      direct current-tree instrumentation으로
      `PAIR/DEALER_DEALER inproc 64B`
      `pipe_write_and_flush total 866.94/862.54 ticks`,
      `lock 70.95/70.63`, `hwm 25.12/25.33`, `write 38.50/38.98`,
      `flush 259.26/266.00`,
      `flush outcome true=8840861/8740895`,
      `false=539397/506441`를 확인했고
      temporary 계측 patch는 바로 원복했다.
      이어서
      `ypipe_base.hpp` / `ypipe.hpp` / `ypipe_conflate.hpp` /
      `pipe.cpp` / `unittest_ypipe.cpp`에 candidate를 올렸고,
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_router_multiple_dealers|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_thread_safe_contract_policy|test_monitor_perf_contract|test_xpub_nodrop)$' -j1`
      는 통과했다.
      하지만 public
      [`perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_043927_codex_20260329_ypipe_write_publish_public.txt)
      `PAIR tcp/inproc -36.56% / -24.11%`,
      `DEALER_DEALER tcp/inproc -10.62% / -18.47%`로
      early authority와 session-local low baseline을 함께 못 지켜
      raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      같은 ctest gate도 다시 통과했다.
- [x] `pipe.cpp` `process_activate_read()` steady-state read-activation split
      candidate를 검증하고 reject했다.
      `claude --help`는 통과했지만 stdin 기반
      `timeout 90s claude -p --permission-mode bypassPermissions --add-dir /home/hep7/project/kairos/zlink --add-dir /home/hep7/project/kairos/libzmq`
      consult는 usable output 없이 `code 124` timeout으로 끝났다.
      candidate는 `pipe.cpp`만 건드렸고,
      적용 뒤 `cmake --build core/build -j$(nproc)`를 돌린 직후 첫
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_ypipe|unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      에서는 build overlap 때문에
      `core/build/lib/libzlink.so.5: file too short` loader race가 났지만,
      build 완료 뒤 같은 gate rerun은 통과했다.
      이어서 public
      [`perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_045615_codex_20260329_recv_activation_public.txt)
      `PAIR tcp/inproc -27.18% / -17.96%`,
      `DEALER_DEALER tcp/inproc -22.07% / -18.31%`로
      early authority와 session-local low baseline을 함께 못 지켜
      raw 없이 바로 reject했고 전부 원복했다.
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      같은 ctest gate도 다시 통과했다.
- [x] env-gated send-scope split instrumentation으로
      earlier `socket_scope_construct` bucket을 다시 갈라 보고 원복했다.
      temporary instrumentation은 `socket_runtime.cpp` 한 파일에만 올렸고,
      `cmake --build core/build -j$(nproc)`와
      `ctest --test-dir core/build --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_stream_send_blocking_wakeup)$' -j1`
      는 통과했다.
      diagnostic profile은
      [`pair_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/pair_send_scope_profile_20260329.txt)
      /
      [`dealer_send_scope_profile_20260329.txt`](/home/hep7/project/kairos/zlink/doc/plan/perf/logs/diagnostics/dealer_send_scope_profile_20260329.txt)
      에 남겼고,
      no-sync `PAIR`
      `enter_public_api/leave_public_api 49.70/50.01 ticks`,
      sync-fast `DEALER_DEALER`
      `enter_public_api_and_lock_sync_fast/unlock_public_api_sync_and_leave`
      `49.66/49.67 ticks`,
      ctor/dtor total `174.36/175.98`, `174.80/176.78`만 확인했다.
      profiler-on comparison throughput은
      [`perf_linux_20260329_051100_codex_20260329_pair_send_scope_profile.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_051100_codex_20260329_pair_send_scope_profile.txt)
      /
      [`perf_linux_20260329_051100_codex_20260329_dealer_send_scope_profile.txt`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/results/single/report/perf_linux_20260329_051100_codex_20260329_dealer_send_scope_profile.txt)
      에 남았지만 instrumentation overhead 때문에 authority로는 쓰지 않았다.
      계측 patch는 바로 원복했고,
      원복 뒤 `cmake --build core/build -j$(nproc)`와
      같은 ctest gate도 다시 통과했다.
- [x] `pipe.hpp` / `pipe.cpp` publication gate split candidate를 검증하고
      reject했다.
      `_out_sync` 아래에서 send hot path가 `_state == active` enum 대신
      publication cluster 전용 readiness gate만 보도록
      `_out_active/_out_pipe/_peers_msgs_read/_msgs_written`와
      steady-state publication readiness를 따로 묶어 보려 했지만,
      targeted gate
      `ctest --test-dir core/build/core --output-on-failure -R '^(unittest_socket_runtime|test_public_inproc_multipart_send|test_multi_socket_contract_regressions|test_socket_with_handler|test_router_mandatory_hwm|test_stream_send_blocking_wakeup|test_stream_threadsafe|test_xpub_nodrop)$' -j1`
      는 통과한 반면 primary `multi dealer_dealer tcp 64B`가
      `1971.48 -> 1594.24 Kmsg/s (-19.13%)`로
      same-day authority baseline
      `1989.95 -> 1603.51 Kmsg/s (-19.42%)`
      대비 `zlink absolute throughput`이 `9.27 Kmsg/s` 낮아
      keep-worthy broad win을 만들지 못했다.
      따라서 publication gate alias만 두는 local ownership split도
      current broad candidate가 아니어서 전부 원복했고,
      원복 뒤 `cmake --build core/build -j$(nproc)`로 `core/build`
      산출물을 현재 소스와 다시 맞췄다.
