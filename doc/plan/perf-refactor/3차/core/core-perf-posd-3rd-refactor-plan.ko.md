# `core/perf` POSD 3차 리팩토링 계획

> 현재 상태: `ready -> active` 기준, 기본 perf surface는 `throughput`,
> `bandwidth`, `latency`, `latency_p95`, `latency_p99`만 남도록 정리되었다.
> 그러나 공통 helper/stream client/bindings/bench/tracked artifact 계층에는
> 아직 정책 밖 책임과 중복 surface가 남아 있다.
>
> 목표: PERF 정책을 그대로 유지하면서, `core/perf`의 남은 중복 파일,
> 죽은 코드, 얕은 wrapper, tracked artifact를 정리해 전체 복잡도를 실제로
> 줄인다.
>
> 현재 완료 상태:
>
> - stage 1 `core/perf`: complete
> - stage 2 `bindings/cpp/perf`: complete
> - stage 3 `bindings/dotnet/perf`: complete
> - stage 4 `bindings/java/perf`: complete
> - stage 5 `bindings/rust/perf`: complete
> - stage 6 `bindings/go/perf`: pending
> - stage 7 `bindings/node/perf`: pending
> - stage 8 `bindings/python/perf`: pending
> - stage 9 `core/bench/with_zmq`: pending
> - stage 10 `core/bench/with_stream`: pending
> - stage 11 공통 tracked artifact 정리: pending
>
> 최상위 목적: perf의 본래 목적은 벤치 코드 자체를 보기 좋게 만드는 것이 아니라,
> **라이브러리 자체의 성능을 정확하게 측정하고, 성능 향상 여부와 성능 회귀
> 여부를 신뢰 가능하게 확인하는 것**이다. 모든 리팩토링은 이 목적을 보조해야
> 하며, 벤치 코드의 편의나 구조 정리가 측정 정확성보다 앞설 수 없다.
>
> 대상: `core/perf/`, `core/perf/common/streamclient/`, `bindings/*/perf/`,
> `core/bench/with_stream/`, `core/bench/with_zmq/`, `doc/perf/`, 관련 perf/bench
> 정책 테스트

## 0. 새 컨텍스트 시작 전 확인

새로운 컨텍스트에서 이 문서를 시작할 때는 아래를 먼저 확인한다.

- 이 문서는 실행 로그가 아니라, `manager`가 guide를 만들고 `guideloop`가 끝까지
  따라야 하는 **작업 기준 문서**다.
- 따라서 이 문서에는 현재 실행 상태, 최근 실패 로그, exit code, “이미 완료됨” 같은
  실행 시점 정보나 진행 보고를 쓰지 않는다. 그런 정보는 `logs/*.manager.md`,
  `logs/*.progress.md`로 분리한다.
- 이 문서는 “baseline 수치 비교” 문서가 아니라 “perf 정책 정렬 + POSD 리팩토링”
  문서다.
- 이번 단계의 종료 기준은 기존 baseline 대비 성능 우열이 아니다.
- 이번 단계의 종료 기준은 `core/perf`, `bindings/<lang>/perf`,
  `core/bench/with_zmq`, `core/bench/with_stream`가 각자 적용받는 정책을 지키면서도
  같은 측정 의미 또는 공정 비교 의미를 유지하도록 정렬되는 것이다.
- 측정 의미를 바꾸는 리팩토링은 금지한다. 특히 아래 anchor point는 임의로
  이동하면 안 된다.
  - ready 만족 판정
  - active 시작/종료 판정
  - 유효 recv 판정
  - throughput count 증가
  - latency sample 채취
  - RESULT line 확정
- `core/perf`와 `bindings/<lang>/perf`는 같은 perf 정책과 같은 측정 의미를
  따라야 한다.
- `with_zmq`, `with_stream`는 `doc/perf`를 그대로 복제하는 대상이 아니라 로컬
  bench 정책을 먼저 따르되, warmup 제거와 공정 비교 유지 범위에서 정렬한다.
- smoke test gate는 리팩토링 도중 수시 확인하는 용도가 아니라, 해당 단계의
  POSD 리팩토링과 dead code 정리가 끝났다고 판단된 뒤 수행하는 최종 확인이다.
- 다음 단계로 넘어가기 전 smoke test의 fail 항목 수는 반드시 0이어야 한다.
- 작업 중 문서와 코드가 충돌하면 `doc/perf` 정책 문서를 먼저 확인하고, 필요하면
  정책 문서부터 바로잡은 뒤 구현을 진행한다.

## 1. 목표

- `doc/perf` 정책 원칙을 단일 기준으로 삼는다.
- `core/perf`뿐 아니라 같은 측정 surface를 공유하거나 비교 기준을 공유하는
  `core/perf/common/streamclient`, `bindings/*/perf`, `core/bench/with_stream`,
  `core/bench/with_zmq`까지 함께 정렬 대상으로 본다.
- 각 bindings 의 perf는 변경된 `doc/perf` 정책을 실제로 따르도록 수정 대상에
  포함한다. 단순 감사나 문서 동기화로 끝내지 않고, policy surface와 POSD 기준을
  만족하도록 함께 리팩토링한다.
- perf의 1차 목적은 **라이브러리 성능 테스트**이며, 리팩토링의 목적도
  **정확한 성능 측정/성능 향상 확인/성능 회귀 검출 신뢰도 유지**에 있다.
- 이번 리팩토링 단계에서는 기존 baseline 수치와의 우열 비교를 종료 기준으로
  사용하지 않는다.
- 이번 단계의 목적은 `core/perf`와 `bindings/<lang>/perf`가 동일한 perf 정책과
  동일한 측정 의미를 따르도록 정렬하는 것이다.
- baseline 성능 측정과 baseline 대비 비교 평가는 본 리팩토링 작업 완료 후 별도
  단계에서 수행한다.
- 이번 리팩토링은 고정 범위 한 번 수행으로 끝내지 않는다. POSD 관점에서
  shallow wrapper, dead code, 과대 공통 계층, tracked noise, 변경 증폭
  지점이 더 이상 의미 있게 남아 있지 않고, 추가 구조 변경이 측정 정확성,
  비교 가능성, 성능 비회귀 신뢰도를 더 높이지 못한다고 판단될 때까지 계속
  진행한다.
- 기본 perf surface는 현재 정책 그대로 유지한다.
  - `throughput`
  - `bandwidth`
  - `latency`
  - `latency_p95`
  - `latency_p99`
- `cpu/mem`, queue/debug/probe, warmup 관련 계약이 문서/테스트/runner 어디에도
  다시 스며들지 않게 한다.
- runner, binary, policy, result artifact의 책임 경계를 더 선명하게 만든다.
- POSD 기준으로 shallow wrapper, change amplification, tracked noise를 줄인다.

## 2. 현재 진단 요약

### 2.1 남은 중복/불일치

- `core/perf` 문서/테스트와 실행 gate는 같은 stage 안에서 함께 정렬돼야 한다.
- `core/perf` 하위에는 tracked baseline/sample/analysis artifact가 여전히 남아 있다.

### 2.2 남은 과대 공통 계층

- [core/perf/multi/common/perf_multi_metrics.hpp](../../../../../core/perf/multi/common/perf_multi_metrics.hpp)
  에 queue probe, queue stats, latency helper, result helper가 함께 섞여 있다.
- [core/perf/single/common/bench_common.hpp](../../../../../core/perf/single/common/bench_common.hpp)
  는 metric queue, phase, metric worker, queue probe를 한 번에 끌어온다.
- 기본 perf surface에서 queue/backpressure를 제거했는데도 queue probe 계층이
  넓은 공용 API로 남아 있다.

### 2.3 남은 얕은 wrapper 구조

- [core/perf/run_benchmarks_multi.sh](../../../../../core/perf/run_benchmarks_multi.sh)는
  multi 옵션을 정규화한 뒤 [core/perf/run_benchmarks.sh](../../../../../core/perf/run_benchmarks.sh)를
  다시 호출하는 2단 shell wrapper 구조다.
- 현재 구조는 동작은 하지만, 공식 실행 surface가 shell 2개와 Python engine에
  분산되어 변경 증폭이 크다.

### 2.4 stream/bench/bindings 공용 잔재

- [core/perf/common/streamclient](../../../../../core/perf/common/streamclient)
  는 아직 `warmup -> measure -> drain` 모델과 `--warmup` 옵션을 유지한다.
- `core/perf` policy surface를 shared client나 downstream perf/bench stack이 그대로
  끌어다 쓰면, core만 `ready -> active`로 정리해도 bindings/bench에서 다시
  old contract가 살아날 수 있다.
- [core/bench/with_stream](../../../../../core/bench/with_stream)과
  [core/bench/with_zmq](../../../../../core/bench/with_zmq)는 비교/검증 surface로
  남아 있으므로, core perf와 모순되는 phase/metric/warmup 계약이 있으면 함께
  정리해야 한다.
- bindings perf도 각 언어별 구현 차이가 있지만, 기본 perf surface와 결과 해석
  계약은 core와 같은 방향으로 수렴해야 한다.
- 따라서 이번 계획에서 bindings perf는 참고 범위가 아니라 실제 수정/리팩토링
  범위다.

## 3. 설계 원칙

- `doc/perf` 원칙을 우선한다.
- perf는 벤치 프레임워크 자체의 성능이 아니라 **라이브러리 자체의 성능**을
  측정하는 도구다.
- 리팩토링은 측정값의 해석 가능성과 비교 가능성을 높여야 하며, 그 반대면
  구조가 더 예뻐져도 채택하지 않는다.
- 성능 향상 확인과 성능 회귀 검출 신뢰도가 구조 정리보다 우선이다.
- PERF 정책을 바꾸지 않는다.
- 측정 의미를 바꾸지 않는다.
- `RESULT` 계약을 늘리지 않는다.
- binary는 `pattern/transport/size/run` 단일 케이스만 책임진다.
- runner는 orchestration, table, result file만 책임진다.
- backpressure/queue 해석은 기본 perf가 아니라 integration으로 분리한다.
- dead code는 “나중에 쓸 수도 있음”을 이유로 유지하지 않는다.
- POSD 리팩토링에는 dead code뿐 아니라 dead branch, dead option, dead file
  정리가 반드시 포함된다.
- 기본 perf 정책에 없는 queue probe, queue stats, queue/debug result surface는
  삭제 대상이며, 꼭 필요하면 integration 또는 국소 diagnostic helper로
  이동한다.
- tracked artifact는 코드와 같은 계층에 두지 않는다.
- 종료 기준은 “이번 체크리스트를 한 번 소화했다”가 아니라, 추가 리팩토링이
  전체 복잡도를 실제로 더 낮추면서도 측정 정확성과 성능 비회귀 신뢰도를 더
  높인다고 보기 어려운 지점에 도달하는 것이다.

## 4. 실행 순서와 영역별 작업

이번 리팩토링의 실제 **top-level 운영 stage 고정 순서**는 아래 11단계를 기준으로
한다.

1. `core/perf` 정리
2. `bindings/cpp/perf`
3. `bindings/dotnet/perf`
4. `bindings/java/perf`
5. `bindings/rust/perf`
6. `bindings/go/perf`
7. `bindings/node/perf`
8. `bindings/python/perf`
9. `core/bench/with_zmq`
10. `core/bench/with_stream`
11. 공통 tracked artifact 정리

단, stage 1인 `core/perf`는 아래 4개의 **내부 substage**를 고정 순서로 진행한다.

- C1. 문서/테스트 수렴
- C2. queue/probe 계층 감사 후 축소
- C3. stream common client 정리
- C4. runner entrypoint 단순화

이 순서를 따르는 이유는 다음과 같다.

- `core/perf`가 정책/측정 surface의 기준 구현이므로 stage 1 내부에서 C1~C4를
  먼저 고정 순서로 정리해야 downstream 변경 증폭이 줄어든다.
- 각 `bindings/<lang>/perf`는 core perf 정책을 따라야 하는 downstream이며,
  언어별로 작업량과 구조 복잡도가 커서 독립 단계로 다뤄야 한다.
- `with_zmq`는 비교 bench지만 자체 bench 정책을 가지며, core perf와의 정렬은
  공통 측정 원칙과 의미 충돌 제거 범위에서 수행한다.
- `with_stream`는 shared STREAM client 정렬이 끝난 뒤 마지막에 맞추는 편이
  변경 증폭이 가장 작다.
- tracked artifact 정리는 모든 코드 경계가 정리된 뒤 마지막에 고정하는 편이
  경계가 가장 분명하다.

모든 영역에 공통으로 적용되는 기준은 같다.

- perf 단순화
- POSD 기반 리팩토링
- 반복 리뷰를 통한 리팩토링 이슈 재검출 방지
- 위 11개 top-level stage를 기준으로 한 단계별 smoke test gate

manager/guideloop 해석 규칙:

- `manager`는 이 문서를 그대로 수정하지 않고, 이 문서에서 stage/substage/gate/
  완료 정의를 읽어 별도 guide/progress를 만든다.
- `guideloop`는 guide를 실행하되, stage 소유권과 gate 타이밍은 이 문서 기준으로만
  해석한다.
- top-level stage만 다음 stage 진행 여부를 판정한다.
- `core/perf`의 C1~C4는 stage 1 내부 substage이며, 독립 stage 완료 판정이나 독립
  smoke gate 소유권을 갖지 않는다.

중요:

- `core/perf`의 C1~C4는 독립 smoke gate 소유 단계가 아니다.
- `core/perf` smoke test gate는 C1~C4가 모두 끝난 뒤, 즉 stage 1 전체가 구조적으로
  정리된 시점에 **한 번만** 수행한다.
- 따라서 C1, C2, C3 중간 상태에서 `run_benchmarks*.sh` 전체 패턴 실행을 돌려
  “얼마나 깨졌는지 먼저 본다”는 식의 운영은 허용하지 않는다.

단, 영역별로 실제 수정 항목은 다르므로 아래처럼 따로 정의한다.

### 4.1 `core/perf` 작업

#### C1. 정책 surface와 충돌하는 문서/테스트 정리

대상:

- [core/perf/README.md](../../../../../core/perf/README.md)
- [core/perf/README_KO.md](../../../../../core/perf/README_KO.md)
- [core/perf/single/tests/test_multi_run_comparison_policy.py](../../../../../core/perf/single/tests/test_multi_run_comparison_policy.py)
- [core/perf/single/tests/test_run_comparison_policy.py](../../../../../core/perf/single/tests/test_run_comparison_policy.py)

할 일:

- README를 정책 요약 index 수준으로 축소한다.
- warmup, single `recv`, `cpu/mem` 등 제거된 계약을 모두 삭제한다.
- policy test fixture를 “필수 5개 metric만 있으면 success” 기준으로 바꾼다.
- 테스트 이름도 old metric 부재를 직접 검증하는 방향으로 정리한다.

완료 정의:

- README가 policy와 모순되지 않는다.
- 테스트 fixture가 제거된 metric을 더 이상 사용하지 않는다.

#### C2. queue/probe 공통 surface 축소

대상:

- [core/perf/multi/common/perf_multi_metrics.hpp](../../../../../core/perf/multi/common/perf_multi_metrics.hpp)
- [core/perf/single/common/perf_single_queue_probe.hpp](../../../../../core/perf/single/common/perf_single_queue_probe.hpp)
- [core/perf/single/common/bench_common.hpp](../../../../../core/perf/single/common/bench_common.hpp)
- [core/perf/multi/src/perf_multi_spot_client.cpp](../../../../../core/perf/multi/src/perf_multi_spot_client.cpp)
- [core/perf/multi/common/perf_multi_spot_control.hpp](../../../../../core/perf/multi/common/perf_multi_spot_control.hpp)

할 일:

- queue probe, queue stats, queue/debug result helper가 기본 perf 계약에 직접
  기여하지 않는다면 공통 helper에서 제거한다.
- 꼭 필요한 경우 SPOT/local diagnostic 범위로 축소하고, public/common surface로
  노출하지 않는다.
- `bench_common.hpp`에서 debug/queue 계층 include를 분리해 패턴 파일이 필요
  이상의 내부를 보지 않게 한다.

완료 정의:

- 기본 perf 실행에 queue probe 공용 계층이 필수가 아니다.
- queue/backpressure 관련 helper가 공통 hot path의 설명 복잡도를 올리지 않는다.

#### C3. stream common client 정렬

대상:

- [core/perf/common/streamclient/README.md](../../../../../core/perf/common/streamclient/README.md)
- [core/perf/common/streamclient/README_KO.md](../../../../../core/perf/common/streamclient/README_KO.md)
- [core/perf/common/streamclient/perf_stream_bench_client.hpp](../../../../../core/perf/common/streamclient/perf_stream_bench_client.hpp)
- [core/perf/common/streamclient/perf_stream_client_options.hpp](../../../../../core/perf/common/streamclient/perf_stream_client_options.hpp)
- [core/perf/common/streamclient/perf_stream_client_session.hpp](../../../../../core/perf/common/streamclient/perf_stream_client_session.hpp)

할 일:

- stream common client에서 `warmup` phase와 `--warmup` 옵션 잔재를 제거한다.
- lifecycle을 `ready -> active` 기준으로 다시 맞춘다.
- STREAM shared client가 downstream perf/bench/bindings에 old contract를
  재주입하지 않게 한다.

완료 정의:

- stream common client 문서와 코드에 warmup contract가 남지 않는다.
- shared STREAM client가 core perf policy와 같은 phase/metric surface를 따른다.

#### C4. runner entrypoint 책임 재정렬

대상:

- [core/perf/run_benchmarks.sh](../../../../../core/perf/run_benchmarks.sh)
- [core/perf/run_benchmarks_multi.sh](../../../../../core/perf/run_benchmarks_multi.sh)
- [core/perf/run_benchmarks.ps1](../../../../../core/perf/run_benchmarks.ps1)
- [core/perf/run_benchmarks_multi.ps1](../../../../../core/perf/run_benchmarks_multi.ps1)
- [core/perf/run_comparison.py](../../../../../core/perf/run_comparison.py)
- [core/perf/single/run_comparison.py](../../../../../core/perf/single/run_comparison.py)

할 일:

- shell은 option normalization과 build gate까지만 맡긴다.
- orchestration 책임은 single/multi Python engine으로 더 직접 연결한다.
- multi shell이 single shell을 다시 호출하는 구조를 줄이거나 제거한다.
- 공식 실행기 정의를 문서와 코드에서 동일하게 맞춘다.

완료 정의:

- entrypoint 책임이 shell 2단 위임으로 퍼져 있지 않다.
- single/multi 실행 흐름이 문서 한 문단으로 설명 가능하다.

### 4.2 `bindings/<lang>/perf` 작업

`bindings/<lang>/perf`는 하나의 묶음 단계로 처리하지 않는다. 아래 언어별로 각각
독립 stage를 두고, 앞 언어가 구조적으로 완료되고 smoke test gate까지 통과해야 다음
언어로 넘어간다. 각 언어 단계는 `core/perf`와 비슷한 크기의 리팩토링 단계로 본다.

적용 대상 언어와 고정 순서는 다음을 기본으로 본다.

- `bindings/cpp/perf`
- `bindings/dotnet/perf`
- `bindings/java/perf`
- `bindings/rust/perf`
- `bindings/go/perf`
- `bindings/node/perf`
- `bindings/python/perf`

고정 순서:

- `cpp -> dotnet -> java -> rust -> go -> node -> python`

언어별로 아래 순서를 각각 독립 stage로 진행한다.

1. 현재 policy surface와 불일치하는 항목 감사
2. old warmup/cpu-mem/queue/debug contract 제거
3. runner/result/report 구조를 변경된 `doc/perf` 정책에 맞게 정렬
4. POSD 관점에서 얕은 wrapper/dead code/과대 공통 계층 정리
5. 해당 language stage의 smoke 테스트 통과 확인

여기서 각 언어 stage의 gate 소유권은 해당 bindings stage 자신에게 있다.
`core/perf` smoke test gate를 언어 stage의 기본 gate로 재사용하지 않는다.

언어별 stage 명시는 아래와 같이 고정한다.

- 단계 2. `bindings/cpp/perf`
- 단계 3. `bindings/dotnet/perf`
- 단계 4. `bindings/java/perf`
- 단계 5. `bindings/rust/perf`
- 단계 6. `bindings/go/perf`
- 단계 7. `bindings/node/perf`
- 단계 8. `bindings/python/perf`

#### B1. 언어별 bindings perf 공통 정렬 기준

대상:

- [bindings](../../../../../bindings)

할 일:

- bindings perf에서 old warmup/cpu-mem/queue contract가 남아 있는지 감사한다.
- 각 bindings perf 구현을 변경된 `doc/perf` 정책에 맞게 실제 수정한다.
- 각 bindings perf에서 core와 동일한 측정 의미를 직접 점검한다.
  - ready 만족 판정 위치
  - active 시작/종료 판정 위치
  - metric header / wire protocol contract
  - throughput count 증가 위치
  - latency sample 채취 위치
  - RESULT 5개 metric과 fail/skip/unsupported/partial 의미
  - hot path가 실제 binding public API를 통과하는지
- 각 bindings perf에서 pattern/transport semantic equivalence가 core와 같은지
  확인한다.
- 각 bindings perf에서도 POSD 기준으로 shallow wrapper, dead code, 과대 공통
  계층, 변경 증폭 지점을 함께 줄인다.
- `core/perf/common/streamclient`를 공유하는 경로는 core policy와 같은 phase
  surface로 정렬한다.

완료 정의:

- 각 언어 bindings perf와 core perf가 기본 phase/metric 계약에서 서로 모순되지
  않는다.
- 각 언어 bindings perf와 core perf가 같은 측정 anchor, 같은 RESULT 의미, 같은
  pattern/transport semantic meaning을 가진다.
- 각 언어 bindings perf가 변경된 `doc/perf` 정책을 코드와 실행 surface에서
  실제로 따른다.
- 각 언어 bindings perf도 POSD 관점에서 추가로 줄일 얕은 계층과 dead code가
  남지 않도록 함께 정리된다.

### 4.3 `core/bench/with_zmq` 작업

`with_zmq`는 perf 본계약을 대체하는 surface가 아니라 비교 bench surface로 본다.
`doc/perf`를 그대로 복제하는 대상이 아니라, [with_zmq 정책 문서](../../../../../core/bench/with_zmq/README.md)
를 우선 기준으로 유지하되 core perf와 공통 측정 원칙이 충돌하지 않게 정렬한다.
이번 작업에서 `with_zmq`의 1차 목적은 benchmark lifecycle에서 warmup 의존을
제거하는 것이고, 그 다음 POSD 기준으로 구조를 줄이는 것이다. 단, 비교군 간
공정 비교와 semantic equivalence는 끝까지 유지해야 한다.
따라서 아래를 정리 대상으로 둔다.

- `with_zmq` 로컬 정책과 `doc/perf` 공통 원칙의 경계를 먼저 확정
- 성능 테스트 단계에서 warmup 의존과 warmup contract를 제거
- core perf와 충돌하는 warmup/metric/result contract 분리 또는 제거
- bench 전용 개념과 perf 본계약의 경계 명확화
- runner/tmp/results tracked noise 정리
- POSD 관점의 dead code, shallow wrapper, 변경 증폭 경로 축소

완료 정의:

- `with_zmq`가 로컬 bench 정책을 유지하면서도 core perf의 공통 측정 원칙과
  모순되지 않는다.
- bench 전용 계약이 필요하면 perf 본계약과 섞이지 않게 명확히 분리된다.
- tmp/results/debug 잔재가 비교 surface의 설명 복잡도를 과도하게 올리지 않는다.

### 4.4 `core/bench/with_stream` 작업

`with_stream`는 shared STREAM client와 가장 직접적으로 연결되므로 마지막에 정렬한다.
`doc/perf`의 축소된 기본 result surface를 그대로 강제하는 대상이 아니라,
[with_stream 정책 문서](../../../../../core/bench/with_stream/README.md)를 우선
기준으로 유지하되 shared STREAM client와 core perf의 공통 원칙이 새지 않게
정렬한다. 이번 작업에서 `with_stream`의 1차 목적도 benchmark lifecycle에서
warmup 의존을 제거하는 것이고, 그 다음 POSD 기준으로 구조를 줄이는 것이다.
단, stack 간 공정 비교 조건과 로컬 bench 정책의 비교 의미는 유지해야 한다.
정리 대상은 다음과 같다.

- `with_stream` 로컬 정책과 `doc/perf` 공통 원칙의 경계를 먼저 확정
- 성능 테스트 단계에서 warmup 의존과 warmup contract를 제거
- shared STREAM client 변경 이후 남는 old STREAM warmup/measure/drain 잔재 제거
- core perf와 충돌하는 phase/result contract 제거
- stack별 공통/전용 책임 재정렬
- results/debug tracked noise 정리
- POSD 관점의 dead code, shallow wrapper, 변경 증폭 경로 축소

`with_stream`에 대해 공통 적용하는 것:

- POSD 리팩토링
- shallow wrapper/dead code/dead branch/dead file 제거
- shared STREAM client와 core perf 사이의 책임 경계 명확화
- 결과 해석과 비교 의미를 더 설명 가능하게 만드는 정리

`with_stream`에 대해 로컬 정책이 허용하면 유지 가능한 것:

- resource metric / resource log
- stack-specific comparison output
- single-pass echo 비교 surface

금지:

- `with_stream` 전용 계약이 `core/perf`의 공식 contract나 shared STREAM client의
  공용 의미를 다시 오염시키는 변경
- core perf에서 제거한 old contract를 shared 계층을 통해 다시 들여오는 변경

완료 정의:

- `with_stream`이 로컬 bench 정책을 유지하면서도 shared STREAM client와 core perf
  공통 원칙을 다시 오염시키지 않는다.
- shared STREAM client 변경 후에도 비교 surface로서 설명 가능한 구조를 유지한다.
- stack별 구현 차이가 있어도 공통 bench contract는 단순하게 유지된다.

### 4.5 공통 tracked artifact 정리 기준 수립

대상:

- [core/perf/baseline](../../../../../core/perf/baseline)
- tracked sample / analysis artifact

할 일:

- tracked baseline/result 파일을 어떤 기준으로 남길지 명시한다.
- 비교 기준이 무의미해진 old baseline은 코드 트리에서 제거하거나 별도 archive로
  이동한다.
- 공식 runtime output root인 [core/perf/results](../../../../../core/perf/results)는
  유지한다. 정리 대상은 tracked baseline/sample/analysis artifact로 한정한다.
- 실행 산출물과 문서 증거 파일의 저장 기준을 분리한다.

완료 정의:

- 코드/정책/실행 산출물이 같은 레벨에서 섞여 있지 않다.
- tracked perf artifact는 “왜 git에 있어야 하는지”가 설명되는 최소 집합만 남는다.
- [core/perf/results](../../../../../core/perf/results)는 정책상 공식 결과 저장
  루트로 유지되고, tracked cleanup 대상과 혼동되지 않는다.

## 5. 단계별 실행 계획

### 5.0 작업 순서 원칙

- 리팩토링은 병렬로 넓게 벌리지 않고 **단계 하나씩 순차 진행**한다.
- 각 top-level stage는 아래 **5단계 완료 루프**를 반드시 같은 순서로 따른다.
  1. 정책 불일치 수정
     - `doc/perf` 정책 문서 기준으로 현재 구현 불일치를 먼저 찾고 바로 수정한다.
  2. POSD 리팩토링
     - 확인된 불일치와 dead code/dead branch/dead file을 POSD 기준으로 줄인다.
  3. 반복 리뷰
     - 같은 stage를 다시 읽고 다시 점검해도 새 리팩토링 이슈가 다시 나오지
       않는지 반복 확인한다.
     - 같은 반복 리뷰 안에서 `@doc/perf`의 모든 관련 정책을 실제로 지키는지도
       다시 확인한다.
  4. 더 줄일 항목이 없는지 재확인
     - perf 정책을 위반하지 않고 측정 의미를 해치지 않는 범위에서, 더 진행할
       POSD 리팩토링이 남아 있지 않은지 다시 확인한다.
     - 이 재확인에는 `@doc/perf` 정책 위반이 새로 드러나지 않는지도 포함한다.
  5. gate 통과
     - 기능 확인과 로컬 검증을 마친 뒤, 해당 stage의 smoke test gate를 수행한다.
     - gate에서 실패가 없을 때만 다음 stage로 진행한다.
- 중요:
  - stage N 이 `complete` 로 명시되기 전에는 stage N+1 을 시작하지 않는다.
  - 일부 감사, 사전 탐색, 다음 stage 후보 파일 열람도 stage 전환으로 간주하므로
    금지한다.
  - 반드시 현재 stage 안에서 정책 리뷰, POSD 리뷰, 잔여 항목 재확인, gate를 모두
    끝낸 뒤에만 다음 stage heading 으로 내려간다.
- 한 단계라도 smoke test gate에서 실패하면 다음 단계로 넘어가지 않고, 해당
  단계에서 원인 수정 후 다시 같은 gate를 통과해야 한다.
- smoke test gate에서 fail 항목이 **하나라도** 남아 있으면 다음 단계로
  넘어갈 수 없다.
- `known fail`, `나중에 수정`, `partial success`를 이유로 다음 단계 진행을
  허용하지 않는다.
- bindings/bench 단계도 같은 원칙을 따른다. 각 stage는 자기 범위의 stage-specific
  smoke test gate를 통과한 뒤에만 다음 stage로 넘어간다.
- `core/perf` smoke test gate는 stage 1 전용 gate다.
- bindings/bench 단계에서는 해당 stage의 전용 smoke 테스트를 기본 gate로 사용한다.
- smoke test gate는 “리팩토링 도중 중간 상태를 계속 확인하는 용도”가 아니라,
  해당 단계의 POSD 리팩토링과 dead code 정리가 끝났다고 판단된 시점의
  최종 확인으로 사용한다.
- 현재 stage가 `complete`가 되기 전에는 다음 stage를 `in_progress`로 올리지
  않는다.
- stage 내부 substage는 구조 정리 순서를 나타내는 작업 단위이지, 별도 stage 상태를
  갖는 완료 단위가 아니다.

`core/perf` stage의 예외 규칙:

- stage 1은 C1 -> C2 -> C3 -> C4 순서의 내부 substage를 가진다.
- C1~C3은 구조 정리용 substage이며, 이 시점에는 cheap/local 확인만 허용한다.
- stage 1의 `core/perf` smoke test gate는 C4까지 끝난 뒤에만 수행한다.
- C1~C3에서 smoke 검증 범위를 넘는 전체 perf 실행을 먼저 돌리는 운영은 문서 위반이다.

#### 공통 작업 루프

모든 영역은 아래 **5단계 완료 루프**를 공통 작업 루프로 사용한다.

1. 정책 불일치 수정
   - perf 정책문서대로 현재 구현을 확인하고 불일치를 먼저 수정한다.
2. POSD 기반 리팩토링
   - dead code/dead branch/dead file까지 포함해 구조를 줄인다.
3. 반복 리뷰
   - 리뷰를 반복해도 새 리팩토링 이슈가 다시 나오지 않는지 확인한다.
   - 같은 반복 리뷰에서 `@doc/perf`의 관련 정책을 모두 다시 대조한다.
4. 더 줄일 항목이 없는지 재확인
   - 추가 POSD 리팩토링이 정말 남아 있지 않은지 다시 확인한다.
   - 추가 정책 위반이나 정책-구현 불일치가 남아 있지 않은지도 다시 확인한다.
5. gate 통과
   - 기능 확인 후 smoke 검증을 수행하고, fail 0일 때만 다음 stage로 진행한다.

- 여기서 `기능 확인`은 build/test/smoke/결과 shape 확인을 뜻한다.
- `smoke 검증`은 현재 stage 소유 범위의 syntax/build/policy test와 모든 지원
  패턴을 짧게 순회하는 실행 smoke를 뜻한다.
- `smoke 검증`은 해당 영역의 POSD 리팩토링이 사실상 마무리된 뒤에만 수행한다.
- 리팩토링은 perf 성능 테스트의 의미를 해치지 않는 선에서만 수행하며,
  `doc/perf` 정책을 위반하는 구조 단순화는 허용하지 않는다.
- 여기서 `리뷰를 반복해도 새 리팩토링 이슈가 다시 나오지 않는지 확인`은
  단발성 self-check가 아니라, 같은 stage를 다시 읽고 다시 점검해도
  “아직 남은 구조 정리 항목”, “문서와 구현의 불일치”, “과대 공통 계층”, “dead code”
  같은 리팩토링 이슈가 추가로 나오지 않는 상태를 뜻한다.
- 여기서 문서와 구현의 불일치에는 `@doc/perf` 정책 문서 전체와의 불일치도
  포함한다.
- 따라서 반복 리뷰는 POSD 이슈만 다시 보는 절차가 아니라, `@doc/perf`의 관련 정책을
  다시 대조해 policy miss가 추가로 나오지 않는지도 함께 확인하는 절차다.
- 따라서 test/smoke가 통과해도 반복 리뷰에서 리팩토링 이슈가 다시 나오면
  그 stage는 완료로 닫지 않는다.
- 따라서 test/smoke가 통과해도 반복 리뷰에서 `@doc/perf` 정책 위반이나 정책-구현
  불일치가 다시 나오면 그 stage는 완료로 닫지 않는다.

각 stage/substage의 bullet도 아래 **5단계 완료 루프**를 그대로 따른다고 해석한다.

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
   - POSD 이슈와 `@doc/perf` 정책 준수를 함께 다시 확인
4. 더 줄일 항목이 없는지 재확인
   - POSD 잔재와 정책 위반 잔재가 함께 없는지 재확인
5. gate 통과

### 단계 1 내부 substage C1. `core/perf` 문서/테스트 수렴

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] substage 범위의 local gate 확인

현재 반영된 작업:

- [x] README 2종을 정책 요약 index 수준으로 축약
- [x] policy test fixture를 Tier 1 5개 metric 기준으로 정리
- [x] 제거된 queue metric 부재를 검증하는 테스트 추가
- [x] Python policy test와 syntax 확인 완료

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과
   - 이 substage는 독립 smoke gate가 아니라 cheap/local 확인만 수행하고,
     stage 1 gate는 C1~C4 전체 완료 후 한 번만 수행한다.

- 정책 정합성 확인
  - README/tests가 현재 `doc/perf` 계약과 맞는지 확인한다.
- README 2종을 현재 정책 기준으로 축약한다.
- single/multi runner policy test fixture를 5개 metric 기준으로 재작성한다.
- POSD 리팩토링
  - 제거된 계약을 다시 살리는 얕은 fixture/wrapper를 걷어낸다.
- 기능 확인
- `rg`로 warmup/cpu-mem/legacy recv 문구가 README/tests에 남지 않는지 확인한다.
- smoke 검증
  - 이 substage에서는 수행하지 않는다.
  - C1~C4가 모두 끝난 뒤 stage 1 전체에 대해 한 번 수행한다.

### 단계 1 내부 substage C2. `core/perf` queue/probe 계층 감사 후 축소

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] substage 범위의 local gate 확인

현재 반영된 작업:

- [x] `bench_common.hpp`에서 과도한 공통 include 축소
- [x] 기본 perf 결과 surface에서 legacy queue metric 재노출 방지
- [x] queue/probe call graph 재감사 완료
- [x] 공용 queue/probe surface 완전 축소 완료

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과
   - 이 substage는 독립 smoke gate가 아니라 cheap/local 확인만 수행하고,
     stage 1 gate는 C1~C4 전체 완료 후 한 번만 수행한다.

- 정책 정합성 확인
  - queue/probe 계층이 현재 기본 perf 계약에 실제로 필요한지 확인한다.
- queue probe 관련 타입/함수의 실제 call graph를 정리한다.
- POSD 리팩토링
- 출력/판정/계약에 기여하지 않는 probe 계층은 제거한다.
- 제거가 위험하면 `spot diagnostic local helper` 같은 좁은 소유권으로 이동한다.
- 기능 확인
  - 제거 후 build/test/smoke가 유지되는지 확인한다.
- smoke 검증
  - 이 substage에서는 수행하지 않는다.
  - C1~C4가 모두 끝난 뒤 stage 1 전체에 대해 한 번 수행한다.

### 단계 1 내부 substage C3. `core/perf` stream common client 정리

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] substage 범위의 local gate 확인

현재 반영된 작업:

- [x] shared STREAM client 문서에서 `ready -> active` 기준 명시
- [x] `drain` 명칭을 `completion wait` 기준으로 정리
- [x] size transition completion wait 명칭 정리
- [x] old contract 재주입 여부 반복 리뷰 완료

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과
   - 이 substage는 독립 smoke gate가 아니라 cheap/local 확인만 수행하고,
     stage 1 gate는 C1~C4 전체 완료 후 한 번만 수행한다.

- 정책 정합성 확인
  - shared STREAM client의 phase/option surface가 `doc/perf`와 맞는지 확인한다.
- POSD 리팩토링
- shared STREAM client의 warmup/measure/drain contract를 제거한다.
- STREAM shared client README와 option surface를 현재 policy에 맞춘다.
- bindings/bench가 참조하는 공용 client 기준을 먼저 고정한다.
- 기능 확인
  - shared STREAM client 수정 후 core 측 동작이 유지되는지 확인한다.
- smoke 검증
  - 이 substage에서는 수행하지 않는다.
  - C1~C4가 모두 끝난 뒤 stage 1 전체에 대해 한 번 수행한다.

### 단계 1 내부 substage C4. `core/perf` runner entrypoint 단순화

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] stage 1 gate 통과

현재 반영된 작업:

- [x] multi shell이 single shell을 재호출하지 않도록 직접 Python engine 호출로 정리
- [x] single PowerShell entrypoint를 single-only surface로 고정
- [x] single bash entrypoint에서 dead `--recv` surface 제거
- [x] single callback worker processed-count drain을 실제 processed 기준으로 정렬
- [x] single runner dead recv-mode helper/signature 잔재 제거
- [x] SPOT 중복 callback-mode check 제거
- [x] `core/perf` smoke gate 최종 재실행 통과
- [x] stage 1 반복 리뷰 후 최종 gate 재확인 완료

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과
   - C1~C4 전체가 끝난 뒤 stage 1의 `core/perf` smoke test gate를 통과해야 한다.

- 정책 정합성 확인
  - 공식 entrypoint/책임과 현재 shell/python 연결 구조의 불일치를 확인한다.
- POSD 리팩토링
- shell wrapper 간 재호출 경로를 분석하고 최소 surface로 축소한다.
- single/multi 공통 옵션과 suite 전용 옵션의 ownership을 표로 정리한다.
- shell -> python 호출 경로를 한 단계 덜 얕게 만든다.
- 기능 확인
  - entrypoint 변경 후 옵션/결과/실행 흐름이 유지되는지 확인한다.
- smoke 검증
  - C1~C4 전체가 끝난 뒤 stage 1의 `core/perf` smoke test gate를 통과해야 한다.

### 단계 2. `bindings/cpp/perf` 정리

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 2가 smoke test gate를 통과해야 단계 3으로 넘어간다.

현재 반영된 작업:

- [x] `bindings/cpp/perf/run_binding_single.sh`에서 single dead `--recv`
      surface 제거 및 callback-only entrypoint 고정
- [x] `bindings/cpp/perf/single/common/*`과 single pattern 소스의 dead
      `queue_probe`/queue stats surface 제거
- [x] `bindings/cpp/perf/single`의 dead `phase_warmup`/`warmup_count`
      분기와 시그니처 제거
- [x] `bindings/cpp/perf/multi/common/perf_common.hpp`의 server queue metric
      RESULT surface 제거
- [x] `bindings/cpp/perf/multi/common/perf_stream_session.hpp`와
      `multi/src/perf_stream_server.cpp`의 stdin `QUEUE,<size>` probe path 제거
- [x] `bindings/cpp/perf/multi` server 출력의 dead queue metric line 제거
- [x] `bindings/cpp/perf/run_binding_multi.sh`의 unbound
      `WARMUP_SECONDS` 출력 제거
- [x] `bindings/cpp/perf/run_binding_single.sh`가 multi 위임 실행 시 실제
      `recv_mode`를 표시하도록 정렬
- [x] single/multi runner syntax 확인, python wrapper py_compile, 관련 C++
      target build 재확인 완료
- [x] `bindings/cpp/perf` smoke gate 재실행 통과

### 단계 3. `bindings/dotnet/perf` 정리

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 3이 smoke test gate를 통과해야 단계 4로 넘어간다.

현재 반영된 작업:

- single entrypoint에서 dead `--recv` / `--warmup` surface를 제거하고,
  single callback-only 계약과 README 설명을 정렬했다.
- multi runner에서 old stream client path / completion status / result count
  계산을 정리하고, `status=complete|partial`, `expected_result_lines`,
  `actual_result_lines`, `## Failures` 출력을 `doc/perf` 계약에 맞췄다.
- multi DEALER_ROUTER / ROUTER_ROUTER client는 warmup drain 단계를 제거하고
  public `TryRecv` surface로 수신을 단순화해 `ready -> active` anchor를
  core와 같은 의미로 맞췄다.
- multi DEALER_ROUTER / ROUTER_ROUTER server는 routed payload의 마지막 frame만
  기준으로 echo하고 fresh message를 재구성해 multipart/reuse 의존을 제거했다.
- multi STREAM server는 stdin EOF 종료를 제거하고 stop token 종료 계약을
  복구해 shared core stream client와 다시 맞췄다.
- stage 3 반복 리뷰에서 `doc/perf/PERF_POLICY.md`,
  `doc/perf/PERF_SINGLE_TEST_POLICY.md`,
  `doc/perf/PERF_MULTI_TEST_POLICY.md` 와 다시 대조해 runner/report/status
  surface와 pattern contract를 재확인했다.
- stage 3 gate 재실행:
  - `bash -n bindings/dotnet/perf/single/run_benchmarks.sh bindings/dotnet/perf/multi/run_benchmarks.sh bindings/dotnet/perf/run_benchmarks.sh bindings/dotnet/perf/run_benchmarks_multi.sh`
  - `python3 -m py_compile bindings/dotnet/perf/run_comparison.py bindings/dotnet/perf/single/run_comparison.py bindings/dotnet/perf/multi/run_comparison.py`
  - `dotnet build bindings/dotnet/perf/single/Zlink.BindingBench/Zlink.BindingBench.csproj -c Release`
  - `dotnet build bindings/dotnet/perf/multi/Zlink.BindingBench.Multi/Zlink.BindingBench.Multi.csproj -c Release`
  - `./bindings/dotnet/perf/run_benchmarks.sh --msg-sizes 64 --runs 1 --duration 1 --results-tag smoke_single_all_r3`
  - `./bindings/dotnet/perf/run_benchmarks_multi.sh --msg-sizes 64 --runs 1 --clients 4 --warmup 1 --duration 1 --results-tag smoke_multi_all_r6`

### 단계 4. `bindings/java/perf` 정리

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 4가 smoke test gate를 통과해야 단계 5로 넘어간다.

현재 반영된 작업:

- `bindings/java/perf/common`에서 old warmup/cpu-mem RESULT contract를 제거했다.
  - `PerfArgs`가 `--warmup`을 더 이상 받지 않는다.
  - `PerfReport`와 `PerfMetricsCollector`가 Tier 1 5개 metric만 출력한다.
  - `PerfUtil` phase 상수를 `ACTIVE/STOP/PROBE` 의미로 정리했다.
- `bindings/java/perf/single`에서 callback-only `ready -> active` 흐름으로 다시 정렬했다.
  - single runner에서 warmup 옵션과 cpu/mem/queue surface를 제거했다.
  - `PAIR/DEALER_DEALER/DEALER_ROUTER`는 active-only sender loop로 축소했다.
  - `PUBSUB`의 `preflight/primed` 단계를 제거했다.
  - `ROUTER_ROUTER`는 monitor-ready 뒤 단발 self-check 1회만 남겼다.
  - `SPOT`은 local probe-based ready barrier로 정렬했다.
- `bindings/java/perf/multi`에서 old warmup surface와 잘못된 측정 의미를 정리했다.
  - multi runner에서 warmup 옵션, cpu/mem/queue metric, `--print-perf-result` 의존을 제거했다.
  - `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`를 실제 echo server/client 측정으로 교체했다.
  - `MULTI_DEALER_DEALER`, `MULTI_PUBSUB`, `MULTI_SPOT`은 active-only phase로 다시 맞췄다.
  - `MULTI_SPOT`에는 policy가 허용한 stabilization/control-settle barrier를 넣었다.
  - `MULTI_STREAM`은 stdin EOF를 stop으로 보지 않게 바꾸고 stop-token echo를 제거했다.
- Java perf TLS cert path를 repo `tests/certs` 기준으로 다시 찾도록 고쳤다.
- stage 4 gate 재실행:
  - `bash -n bindings/java/perf/run_benchmarks.sh bindings/java/perf/run_benchmarks_multi.sh bindings/java/perf/single/run_benchmarks.sh bindings/java/perf/multi/run_benchmarks.sh`
  - `gradle -q :perf-single:compileJava :perf-multi:compileJava`
  - `gradle -q :perf-single:installDist :perf-multi:installDist`
  - `./bindings/java/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --runs 1 --duration 1 --results-tag stage4_single_tcp_smoke_r2`
  - `./bindings/java/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage4_multi_tcp_smoke_r1`
  - 반복 리뷰 probe:
    - `./bindings/java/perf/run_benchmarks_multi.sh --pattern MULTI_DEALER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage4_dr_probe_r1`
    - `./bindings/java/perf/run_benchmarks_multi.sh --pattern MULTI_ROUTER_ROUTER --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage4_rr_probe_r1`
    - `./bindings/java/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage4_spot_cb_probe_r2`
    - `./bindings/java/perf/run_benchmarks_multi.sh --pattern MULTI_STREAM --recv callback --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage4_stream_cb_probe_r1`

### 단계 5. `bindings/rust/perf` 정리

상태: complete

체크리스트:

- [x] 정책 불일치 수정
- [x] POSD 리팩토링
- [x] 반복 리뷰
- [x] 더 줄일 항목이 없는지 재확인
- [x] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 5가 smoke test gate를 통과해야 단계 6으로 넘어간다.

현재 반영된 작업:

- `bindings/rust/perf/single`에서 old warmup surface와 `tcp://...:0` 직접
  connect 잔재를 제거하고, bind 후 resolved endpoint를 사용하도록 정렬했다.
- single runner는 `--recv callback`만 허용하도록 고정했고, `report/` 100파일
  보존 정책을 추가했다.
- single raw 패턴 callback path는 multipart payload의 마지막 frame만을 metric
  header로 해석하도록 고쳤다.
- single `PUBSUB`는 subscriber `CONNECTION_READY` gate와 stop-frame drain으로
  `ready -> active`를 다시 맞췄다.
- single `SPOT`은 publisher/subscriber node 분리 + local probe barrier로
  ready 판정을 복구했다.
- `bindings/rust/perf/multi`에서 old warmup/queue RESULT surface를 제거하고,
  `PERF_MULTI_*` 환경 변수와 5-metric report/completion format으로 정렬했다.
- multi runner는 callback 허용 범위를 `MULTI_SPOT`,`MULTI_STREAM`으로 제한하고,
  `report/` 보존 정책(`PERF_RESULTS_MAX_FILES`, 기본 100)을 추가했다.
- stage 5 반복 리뷰에서 `doc/perf/PERF_POLICY.md`,
  `doc/perf/PERF_SINGLE_TEST_POLICY.md`,
  `doc/perf/PERF_MULTI_TEST_POLICY.md`와 다시 대조해 Rust single/multi의
  ready/active, RESULT/report, callback mode 범위를 재확인했다.
- stage 5 gate 재실행:
  - `bash -n bindings/rust/perf/run_benchmarks.sh bindings/rust/perf/run_benchmarks_multi.sh`
  - `cargo build --release` in `bindings/rust/perf/single`
  - `cargo build --release` in `bindings/rust/perf/multi`
  - `./bindings/rust/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --runs 1 --duration 1 --results-tag stage5_single_tcp_smoke_r2`
  - `./bindings/rust/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage5_multi_tcp_smoke_r2`
  - 반복 리뷰 probe:
    - `./bindings/rust/perf/run_benchmarks_multi.sh --pattern MULTI_SPOT --recv callback --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage5_multi_spot_cb_probe_r1`
    - `./bindings/rust/perf/run_benchmarks_multi.sh --pattern MULTI_STREAM --recv callback --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag stage5_multi_stream_cb_probe_r1`

### 단계 6. `bindings/go/perf` 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 6이 smoke test gate를 통과해야 단계 7로 넘어간다.

### 단계 7. `bindings/node/perf` 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 7이 smoke test gate를 통과해야 단계 8로 넘어간다.

### 단계 8. `bindings/python/perf` 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
- POSD 리팩토링
- 기능 확인
- smoke 검증
  - 해당 bindings stage의 smoke 테스트를 수행한다.
- B1 공통 정렬 기준을 적용한다.
- 단계 8이 smoke test gate를 통과해야 단계 9로 넘어간다.

### 단계 9. `core/bench/with_zmq` 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
  - [with_zmq 정책 문서](../../../../../core/bench/with_zmq/README.md) 기준의
    로컬 bench 계약과 `doc/perf` 공통 원칙의 경계를 먼저 확정한다.
  - `with_zmq`가 core perf의 변경된 phase/metric 계약과 어디서 충돌하는지 확인한다.
- POSD 리팩토링
- `with_zmq`에서 core perf와 충돌하는 warmup/metric/result contract를 분리 또는
  정리한다.
- bench 전용 개념이 perf 본계약으로 되새어 나오지 않게 경계를 명확히 한다.
- 기능 확인
  - 비교 bench로서 필요한 기능이 유지되는지 확인한다.
- smoke 검증
  - `bash -n core/bench/with_zmq/run_benchmarks.sh`
  - `bash -n core/bench/with_zmq/run_benchmarks_multi.sh`
  - `pytest -q core/bench/with_zmq/single/tests/test_run_comparison_policy.py`
  - `./core/bench/with_zmq/run_benchmarks.sh --transports tcp --msg-sizes 64 --runs 1 --duration 1 --results-tag smoke_all`
  - `./core/bench/with_zmq/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --runs 1 --clients 4 --warmup 1 --duration 1 --results-tag smoke_multi_all`
  - 단계 완료 후 smoke test gate를 통과해야 한다.

### 단계 10. `core/bench/with_stream` 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
  - [with_stream 정책 문서](../../../../../core/bench/with_stream/README.md) 기준의
    로컬 bench 계약과 `doc/perf` 공통 원칙의 경계를 먼저 확정한다.
  - `with_stream`의 old STREAM contract 잔재를 확인한다.
- POSD 리팩토링
- `with_stream`에서 shared STREAM client 정렬 이후 남은 old contract를 제거한다.
- stack별 비교 surface를 유지하되 perf 본계약과 충돌하지 않게 정리한다.
- 기능 확인
  - 비교 bench로서 필요한 기능이 유지되는지 확인한다.
- smoke 검증
  - `python3 -m py_compile core/bench/with_stream/run_comparison.py`
  - `bash -n core/bench/with_stream/run_benchmarks.sh`
  - `./core/bench/with_stream/run_benchmarks.sh --stack all --size 64 --runs 1 --warmup 1 --duration 1`
  - 단계 완료 후 smoke test gate를 통과해야 한다.

### 단계 11. 공통 tracked artifact 정책 정리

상태: pending

체크리스트:

- [ ] 정책 불일치 수정
- [ ] POSD 리팩토링
- [ ] 반복 리뷰
- [ ] 더 줄일 항목이 없는지 재확인
- [ ] stage gate 통과

적용 순서:

1. 정책 불일치 수정
2. POSD 리팩토링
3. 반복 리뷰
4. 더 줄일 항목이 없는지 재확인
5. gate 통과

- 정책 정합성 확인
  - tracked artifact와 공식 runtime output root의 경계를 확인한다.
- POSD 리팩토링
- tracked baseline/results 후보를 분류한다.
- 문서 증거로 남길 파일과 실행 산출물로 분리할 파일을 구분한다.
- 필요한 `.gitignore` 및 저장 위치 규칙을 반영한다.
- 기능 확인
  - 결과 저장/보존 동작이 정책과 모순되지 않는지 확인한다.
- smoke 검증
  - 단계 완료 후 최종 smoke test gate를 통과해야 한다.

## 6. 검증 방법

- Python:
  - `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
- Policy tests:
  - `pytest -q core/perf/single/tests`
- Shell syntax:
  - `bash -n core/perf/run_benchmarks.sh`
  - `bash -n core/perf/run_benchmarks_multi.sh`
- Build:
  - `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
- Performance smoke:
  - `./core/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --runs 1 --duration 1 --results-tag smoke_single_all`
  - `./core/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag smoke_multi_all`
  - 결과 파일이 정책 형식으로 생성되는지 확인한다.
  - `status: complete|partial`, 필수 5개 metric line, 결과 table shape를 확인한다.
  - 현재 작업 중인 top-level stage에서 POSD 리팩토링과 dead code 정리가 끝났다고
    판단된 완료 시점에만 smoke run을 수행한다.
  - smoke run 전에는 “측정 의미를 해치지 않았는지”, “perf 정책을 위반하지 않는지”를
    먼저 점검한다.
- smoke는 runner의 기본 패턴 집합이 현재 stage 소유 범위와 일치하는 경우에는
    기본 패턴 집합 실행을 사용하고, 그렇지 않은 경우에만 범위를 제한하는 인자를
    추가하는 방식으로 정의한다.
- 각 명령은 `runs=1`, 대표 size, 짧은 duration/warmup을 사용한다.
- Surface audit:
  - core/shared policy audit:
    `rg -n "cpu_pct|mem_mb|server_cpu_pct|server_mem_mb|warmup|Q\\.Snd|Q\\.Rcv" core/perf bindings doc/perf`
  - bench-local contract audit:
    `rg -n "warmup|cpu_pct|mem_mb|server_cpu_pct|server_mem_mb|Q\\.Snd|Q\\.Rcv" core/bench/with_stream core/bench/with_zmq`
  - bench-local contract audit 결과는 “문자열 존재 여부”만으로 fail 처리하지 말고,
    각 README의 로컬 bench 정책과 충돌하는지 기준으로 해석한다.

### 6.1 Smoke Test Gate 정의

각 단계에서 POSD 리팩토링과 dead code 정리가 더 이상 남아 있지 않다고 판단된
시점에, 현재 stage 소유 범위의 smoke test gate를 수행한다.

stage 1 `core/perf` smoke test gate는 아래를 기준으로 한다.

1. `python3 -m py_compile core/perf/run_comparison.py core/perf/single/run_comparison.py`
2. `pytest -q core/perf/single/tests`
3. `bash -n core/perf/run_benchmarks.sh`
4. `bash -n core/perf/run_benchmarks_multi.sh`
5. `cmake --build core/build -j4 --target comp_src_spot_server comp_src_stream_server`
6. `./core/perf/run_benchmarks.sh --transports tcp --msg-sizes 64 --runs 1 --duration 1 --results-tag smoke_single_all`
7. `./core/perf/run_benchmarks_multi.sh --transports tcp --msg-sizes 64 --runs 1 --clients 4 --duration 1 --results-tag smoke_multi_all`

- 위 1~7에서 실패가 없어야 stage 1을 통과한 것으로 본다.
- 위 1~7 결과에서 fail 항목 수는 반드시 0이어야 한다.
- bindings/bench 단계에서는 해당 대상 전용 smoke 테스트를 각 stage의 기본 gate로
  사용한다.
- smoke gate의 실행 범위는 문서에 명시한 명령 목록 그 자체를 기준으로 한다.
- 기본 패턴 집합이 현재 stage 소유 범위와 정확히 일치하는 runner는 `--pattern`
  또는 `--stack` 생략을 허용한다.
- `with_zmq` 단계에서는 `core/bench/with_zmq/README.md`의 로컬 bench 계약이
  유지되는지도 함께 확인한다.
- `with_stream` 단계에서는 `core/bench/with_stream/README.md`의 로컬 bench 계약이
  유지되는지도 함께 확인한다.
- smoke test gate에 들어가기 전, 해당 단계는 perf 성능 테스트의 의미를 해치지 않는
  선에서 POSD 리팩토링이 마무리되어 있어야 하며 `doc/perf` 정책 위반 상태여서는
  안 된다.
- stage 1에서는 C1~C4 전체가 끝난 뒤에만 위 smoke test gate를 수행한다.

bindings/bench stage gate 계약:

- 단계 2~8 `bindings/*/perf`:
  - 각 bindings stage는 해당 언어 perf 디렉터리의 runner/test/smoke 실행을 자기
    stage의 기본 gate로 사용한다.
  - 어떤 명령을 gate로 삼을지는 각 언어 디렉터리의 현재 공식 perf runner와 policy
    test surface를 기준으로 guide에서 구체화한다.
  - 다른 stage의 smoke gate를 대신 통과했다고 보고 넘어가면 안 된다.
- 단계 9 `core/bench/with_zmq`:
  - 이 문서 9단계에 적은 `bash -n`, `pytest`, `run_benchmarks*.sh` 명령 묶음을
    stage gate로 사용한다.
- 단계 10 `core/bench/with_stream`:
  - 이 문서 10단계에 적은 `py_compile`, `bash -n`, `run_benchmarks.sh` 명령 묶음을
    stage gate로 사용한다.
- 단계 11 공통 tracked artifact 정리:
  - tracked artifact 정리 후에는 정책 문서, 저장 위치 규칙, 결과 저장 루트가
    충돌하지 않는지 확인하는 최종 stage gate를 사용한다.

## 7. 완료 정의

- 이 문서 기준의 완료 단위는 11개 top-level stage다.
- stage 1의 C1~C4는 완료 정의를 보조하는 내부 substage이며, 개별 완료 보고나 개별
  smoke gate 완료로 닫지 않는다.
- `core/perf` 문서, runner, 테스트가 현재 policy surface와 일치한다.
- 제거된 metric/phase를 테스트 fixture나 README가 다시 들고 있지 않다.
- queue/debug/probe 계층이 기본 perf 계약 밖이면 공통 surface에서 제거되거나
  명확히 국소화된다.
- dead code, dead branch, dead option, dead file이 “나중에 쓸 수도 있음” 상태로
  남아 있지 않다.
- `core/perf/common/streamclient`가 더 이상 old warmup contract를 유지하지 않는다.
- runner entrypoint 구조가 지금보다 더 짧고 설명 가능해진다.
- bindings perf / with_stream / with_zmq가 core perf와 충돌하는 phase/metric
  surface를 다시 들고 오지 않는다.
- tracked perf artifact 정리 기준이 문서화되고 코드 트리에 반영된다.
- POSD 관점에서 추가로 제거할 shallow wrapper, dead code, 과대 공통 계층,
  변경 증폭 지점이 남아 있지 않거나, 남아 있어도 제거 비용 대비 복잡도 감소
  이득이 작고 측정 정확성/비교 가능성/성능 비회귀 신뢰도를 추가로 높이지
  못한다는 판단 근거를 설명할 수 있다.
- 반복 리뷰를 다시 수행해도 새 리팩토링 이슈가 나오지 않아야 한다.
- 즉 build/test/smoke 통과는 완료의 필요조건일 뿐이며, 반복 리뷰 기준으로도
  구조 정리와 정책 정합성 이슈가 없어야만 stage를 `complete`로 닫는다.

## 8. 비범위

- throughput/bandwidth/latency 계산식 변경
- one-way vs echo 의미 변경
- `RESULT` 포맷 확장
- backpressure 검증을 perf 기본 surface로 복귀시키는 작업
- transport별 성능 수치 튜닝

## 9. 메모

- 이번 3차는 “기능 추가”가 아니라 “기능을 줄인 뒤 남은 구조적 잔재 제거”가
  목적이다.
- policy 준수만으로 충분하지 않다. POSD 기준으로 설명 가능한 구조가 되어야
  리팩토링이 완료된 것으로 본다.
