# `with_zmq` 성능 회귀 이분 탐색 프롬프트

작업 경로는 `/home/hep7/project/kairos/zlink-perf-regression-bisect` 이다.

## 목표

현재 bisect 로그에 기록된 `current bad anchor` 기준의 `with_zmq single`
성능 격차가 어떤 커밋 구간에서 본격적으로 발생했는지 찾는다.

지금 단계의 목적은 미세 최적화가 아니라:

1. `3/5` 기준의 good 상태를 확정하고
2. current bad 상태를 같은 조건으로 재현한 뒤
3. 이분 탐색으로 회귀가 시작된 커밋 구간을 좁히고
4. culprit commit과 그 구조적 원인을 특정하는 것

이 작업은 **원인이 특정될 때까지 반복**한다.
즉 한 번 good/bad를 찍고 끝내지 않고, 첫 bad commit 또는 매우 작은 culprit
구간이 나올 때까지 계속 구간을 반으로 줄여간다.

## 핵심 질문

1. `2026-03-05` 시점에는 왜 `zlink`가 `libzmq`와 비슷하거나 우세했는가?
2. 어떤 커밋부터 `PAIR` / `DEALER_DEALER` `64B` one-way 성능이 본격적으로
   무너졌는가?
3. 그 커밋의 변경은 `thread-safe socket`, `callback`,
   `recv-first/public surface`, `lifecycle`, `pipe/publication` 중 무엇과 가장
   직접적으로 연결되는가?

## 고정 조건

- `AGENTS.md`를 따른다.
- 사용자를 항상 `팀장님`으로 부른다.
- 빌드 디렉터리는 오직
  [`core/build/`](/home/hep7/project/kairos/zlink-perf-regression-bisect/core/build)
  만 사용한다.
- 지금 단계에서는 성능 개선 코드를 만들지 말고, 회귀를 만든 커밋과 원인 축을
  특정하는 데 집중한다.
- `core/perf` 또는 `core/bench` 코드는 측정 surface 자체가 틀렸다는 강한
  증거가 없는 한 수정하지 않는다.
- 동일 조건 비교를 유지한다. 측정 surface를 중간에 바꾸지 않는다.
- `single with_zmq` 기준으로 먼저 본다.
- 우선 패턴은 `PAIR`, `DEALER_DEALER` `64B` `tcp/inproc` 이다.
- `PUBSUB/ROUTER`는 공통 회귀 구간을 찾은 뒤 pattern-specific로 분리한다.

## 참고 파일

- 현재 bisect 로그:
  [`with-zmq-regression-bisect-log.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-log.ko.md)
- good/bad anchor와 최신 mid 상태는
  [`with-zmq-regression-bisect-log.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-log.ko.md)
  를 기준으로 본다.
- 기존 main worktree의 해석 문서가 필요하면 보조 참고로만 본다.
  - `/home/hep7/project/kairos/zlink/doc/plan/perf/single-libzmq-gap-review.ko.md`
  - `/home/hep7/project/kairos/zlink/doc/internal/hot-path.ko.md`
- 최종 회귀 원인 레포트:
  [`with-zmq-regression-bisect-report.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-report.ko.md)

## 작업 순서

1. bisect 로그의 `current bad` commit에서 current bad 상태를 같은 조건으로
   다시 확인한다.
2. `2026-03-05` baseline에 대응하는 good commit 후보를 찾고, 같은 조건으로
   실제로 good인지 재확인한다.
3. good/bad가 확인되면, 선형 순회 대신 **중간 commit을 찍어가며 반씩 좁히는
   방식**으로 회귀 구간을 축소한다.
4. 각 단계에서 `PAIR`, `DEALER_DEALER` `64B tcp/inproc` 결과를 기록한다.
5. 성능이 크게 꺾이는 commit 또는 매우 작은 구간으로 좁혀지면, 해당 diff를
   읽고 `libzmq` 대응 구현과 비교해 원인 축을 정리한다.
6. 마지막에는 아래를 명확히 설명한다.
   - 첫 bad commit
   - 바로 직전 good commit
   - 의심되는 변경
   - 왜 그 변경이 one-way 성능에 영향을 주는지
7. culprit commit 또는 작은 culprit 구간을 찾으면,
   [`with-zmq-regression-bisect-report.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-report.ko.md)
   에 최종 Markdown 레포트를 작성한다.

## 반복 규칙

- 현재 구간이 `good_commit .. bad_commit`이면, 항상 중간 commit을 하나 고른다.
- 중간 commit이 여전히 bad면 `good_commit .. mid_commit` 또는
  `mid_commit .. bad_commit` 중 bad가 포함된 절반만 남긴다.
- 중간 commit이 good이면 나머지 절반만 남긴다.
- 각 iteration이 끝날 때마다 `현재 good`, `현재 bad`, `다음에 볼 mid`를
  명시적으로 적는다.
- 아래 중 하나가 될 때까지 반복을 멈추지 않는다.
  - 첫 bad commit이 특정됨
  - culprit 구간이 2~3개 커밋 이하로 좁혀짐
  - 측정 noise 때문에 판정이 흔들려서 rerun 규칙이 필요해짐

## 권장 진행 방식

1. `good` / `bad` 기준 확정
2. `mid` commit 측정
3. 구간 축소 반복
4. culprit 후보 diff 분석
5. `libzmq` 대응 파일 대조
6. 최종 원인 요약

## 기록 방식

- 새 로그 문서를 하나 만들거나
  [`with-zmq-regression-bisect-log.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-log.ko.md)
  를 계속 갱신한다.
- 특히 새 컨텍스트에서 시작할 때는 긴 iteration 본문보다 로그 상단의
  `Current Bisect Summary`를 먼저 읽고, stale하면 먼저 갱신한다.
- 각 측정에는 아래를 남긴다.
  - commit hash
  - 실행 명령
  - 결과 파일 경로
  - 핵심 throughput 수치
  - good / bad 판정
- 최종적으로 `good -> bad` 회귀 지도가 보이도록 정리한다.

## 최종 레포트 요구사항

- culprit을 찾은 뒤에는 별도 Markdown 레포트를 반드시 남긴다.
- 레포트 파일은
  [`with-zmq-regression-bisect-report.ko.md`](/home/hep7/project/kairos/zlink-perf-regression-bisect/doc/plan/perf/with-zmq-regression-bisect-report.ko.md)
  를 사용한다.
- 레포트에는 최소한 아래가 들어가야 한다.
  - first bad commit
  - last good commit
  - 회귀 구간에서 새로 들어온 핵심 요소
  - 성능 저하를 만든 것으로 의심되는 변경과 그 근거
  - `libzmq` 대응 구현과의 차이
  - 왜 `oneway`에서 더 크게 드러났는지에 대한 해석
  - 남은 불확실성
- 가능하면 "무엇이 바뀌었는가"를 아래 축으로 분류한다.
  - thread-safe socket / lifecycle
  - callback / dispatch
  - recv-first / public surface
  - pipe / publication / serialization
  - pattern-specific path (`PUBSUB`, `ROUTER` 등)
- 레포트는 단순 결과 나열이 아니라,
  "어떤 요소가 들어왔고, 그중 무엇이 왜 성능 저하를 일으켰는가"를
  설명하는 분석 문서여야 한다.

## 중요

- 지금은 `어떻게 최적화할까`보다 `언제, 무엇 때문에 망가졌나`를 찾는
  단계다.
- 미세 helper 수정으로 들어가지 말고, 회귀를 만든 커밋과 구조 변화를 먼저
  특정한다.
- good/bad 판정이 애매하면 같은 조건으로 한 번 더 rerun해서 median 쪽 해석을
  택한다.
- 원인이 특정되기 전에는 탐색을 종료하지 않는다.
