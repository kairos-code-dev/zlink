# `core` 성능 개선 실행 가이드

> 상태: active
> 기준 baseline:
> - `doc/plan/perf/perf_linux_recv_20260323_094627.txt`
> - `doc/plan/perf/perf_linux_callback_20260323_082648.txt`
> 실행 루프: `doc/plan/perf/run_perf_improvement_loop.sh`
> 로그 디렉터리: `doc/plan/perf/logs/`
> 대상 범위: `core/`, `core/tests/`, `doc/plan/perf/`
> 목적: baseline 초과까지 중단 없이 반복 수행할 실전 실행 규칙 고정

`run_perf_improvement_loop.sh`는 별도 락으로 막지 않는다.
대신 `doc/plan/perf/logs/` 아래 로그 파일이 실제로 갱신되는 `perf` 실행이 감지되면
그 로그를 따라가며 대기하고, 완료되면 다음 iteration을 시작한다.
프로세스는 남아 있는데 로그 갱신이 멈춘 경우에는 stale로 간주하고
해당 `perf` 프로세스를 정리한 뒤 루프를 계속 진행한다.

## 1. 문서 목적

이 문서는 `core` 성능 개선 작업의 단일 authority다.
기존의 "마스터 플랜"과 "실행 가이드" 역할을 이 문서 하나에 통합한다.

이 문서의 핵심 목표는 아래 한 줄이다.

```text
baseline 두 파일에 기록된 모든 공식 측정 항목보다 현재 결과가 낮은 상태로 종료하지 않고,
모든 항목을 상회하는 증거를 남길 때까지 개선을 반복한다.
```

여기서 "모든 항목"은 다음 의미로 고정한다.

- `recv` baseline의 모든 패턴/transport/msg_size 조합
- `callback` baseline의 모든 패턴/transport/msg_size 조합
- 공식 결과 파일에 실제로 표로 기록된 throughput/bandwidth/latency 지표
- queue pending, timeout, skip, crash 같은 품질 신호

즉 일부 대표 tuple만 좋아진 상태, 특정 transport만 복구된 상태,
평균만 개선되고 tail latency가 악화된 상태는 완료로 보지 않는다.

## 2. authority와 범위

이 문서의 해석 규칙은 아래로 고정한다.

- 구현 방향, 실행 순서, 중단 규칙, 종료 판정 모두 이 문서가 authority다.
- baseline 자체는 위 두 성능 결과 파일이 authority다.
- 성능 개선 요청이라도 perf-only shortcut으로 닫지 않는다.
- 구조 문제로 판단되면 POSD 원칙에 따라 `core` 내부 복잡도를 줄이는 방향으로 고친다.

기본 작업 범위는 아래로 고정한다.

- 허용:
  - `core/`
  - `core/tests/`
  - `doc/plan/perf/`
- 기본 비허용:
  - `core/bench/`
  - `core/perf/`를 workaround surface로 바꾸는 수정
  - bindings 전용 우회

단, 공식 perf 실행기 자체의 버그 때문에 baseline 비교가 불가능한 경우에만
`core/perf/` 수정이 허용된다.
그 경우에도 목적은 harness 우회가 아니라 공식 측정 surface 복구여야 한다.

## 3. 설계 원칙

성능 작업은 아래 원칙을 동시에 만족해야 한다.

1. 성능 수치가 아니라 hot path 설명이 먼저 단순해져야 한다.
2. perf 전용 hidden fast path를 추가하지 않는다.
3. bench/perf에서만 켜지는 내부 shortcut으로 결과를 만들지 않는다.
4. queue/HWM/ready/monitor 계약을 약화해서 수치를 올리지 않는다.
5. 한 병목을 고치며 다른 tuple을 깨뜨리면 종료하지 않는다.
6. 복잡도를 늘리는 얇은 wrapper보다, ownership과 lifecycle을 더 명확히 만드는 deep module을 선호한다.

성능 개선의 우선 순위는 아래처럼 둔다.

1. 측정 신뢰도 복구
2. worst tuple 식별
3. 공통 hot path 단순화
4. transport/pattern별 잔여 병목 제거
5. full baseline 초과 재검증

## 4. baseline 해석 규칙

baseline은 archive가 아니라 active acceptance 기준이다.

- `recv`는 `MULTI_*` 표면을 기준으로 본다.
- `callback`은 single callback 표면을 기준으로 본다.
- 한 파일 안의 모든 표 행이 acceptance 대상이다.
- 특정 항목이 현재 실행에서 skip/fail되면 baseline 미달로 간주한다.
- throughput/bandwidth는 baseline 초과가 목표다.
- latency 계열(`Lat.Mean`, `Lat.P95`, `Lat.P99`)은 baseline 이하가 목표다.
- queue pending 관련 최대치/종료 잔량은 baseline 이하가 목표다.
- 동일 항목이 수치상 비슷해 보여도 오차 핑계로 닫지 않는다. baseline 초과를 명시적으로 확인해야 한다.

## 5. 운영 원칙

핵심 원칙은 아래 두 줄로 고정한다.

```text
가장 나쁜 tuple부터 고치되, baseline 미달 항목이 남아 있으면 종료하지 않는다.
perf 수치만 올리는 우회가 아니라 core hot path를 더 단순하게 설명하는 수정으로만 닫는다.
```

중단 허용 조건은 아래뿐이다.

1. 사용자 결정이 필요한 공개 계약 변경
2. 로컬 환경 자체가 깨져 공식 perf 실행이 불가능한 상태
3. 사용자 변경과 직접 충돌하는 dirty worktree

위 세 경우가 아니면 작업을 멈추지 않는다.

## 6. 세션 시작 절차

매 세션 시작 시 아래를 순서대로 수행한다.

1. 이 문서 전체를 다시 읽는다.
2. `14.0 상태표`에서 `완료`가 아닌 첫 항목을 고른다.
3. baseline 두 파일에서 해당 항목의 기준 수치를 다시 확인한다.
4. 최신 결과와 비교해 실제 worst tuple을 정한다.
5. 구조 병목 가설을 적고 바로 코드/테스트/재측정으로 진행한다.

## 7. 공식 실행 명령

빌드와 공식 측정은 아래 명령만 사용한다.

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1

./core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --recv recv

./core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --recv callback \
  --pattern SPOT

./core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --recv callback
```

해석 규칙:

- `recv` baseline 검증은 `run_benchmarks_multi.sh --recv recv`가 공식이다.
- multi callback은 현재 공식 surface 제약 때문에 `SPOT`/`STREAM` 중심 spot 확인에만 쓴다.
- `callback` baseline 검증은 `run_benchmarks.sh --recv callback`이 공식이다.
- 최종 acceptance는 `recv full + callback full` 둘 다 있어야 한다.

## 8. 권장 spot 재측정

전체 full run 전에는 spot 재측정으로 병목을 줄인다.

권장 명령:

```bash
./core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --recv recv \
  --pattern DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,PUBSUB,SPOT,STREAM \
  --transports tcp,tls,ws,wss \
  --msg-sizes 64,256,1024,65536,131072,262144

./core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --recv callback \
  --pattern PAIR,PUBSUB,DEALER_DEALER,DEALER_ROUTER,ROUTER_ROUTER,GATEWAY,SPOT \
  --transports inproc,ipc,tcp,tls,ws,wss \
  --msg-sizes 64,256,1024,65536,131072,262144
```

spot 재측정도 baseline보다 낮은 tuple을 찾기 위한 수단이지,
full acceptance를 대체하지 않는다.

## 9. 수정 단위 규칙

하나의 iteration은 아래 묶음으로 닫는다.

1. worst tuple 선정
2. 원인 코드 읽기
3. 필요한 회귀 테스트 추가 또는 기존 테스트 보강
4. `core/` 수정
5. build + 관련 test
6. spot perf 재측정
7. 상태표 갱신

금지:

- 여러 병목을 한 커밋에 섞어 원인 추적 불가 상태 만들기
- perf 결과만 보고 구조 설명 없이 문서 닫기
- 테스트를 perf-friendly하게 약화
- 공식 runner 대신 비공식 실행 결과로 acceptance 판정

반복 루프는 아래 순서로 고정한다.

1. baseline과 최신 결과를 비교해 worst tuple을 고른다.
2. 그 tuple이 드러내는 공통 병목을 코드에서 설명한다.
3. `core/tests/` 범위에서 계약 회귀가 필요한지 먼저 추가한다.
4. `core/`를 수정한다.
5. spot 재측정으로 영향 범위를 확인한다.
6. 좋아졌더라도 다른 tuple 악화 여부를 확인한다.
7. 충분한 개선이 보이면 full recv/callback 재측정으로 baseline을 다시 비교한다.
8. 문서와 로그를 갱신하고 다음 worst tuple로 이동한다.

핵심은 "한 번의 큰 실험"이 아니라
"작은 구조 수정 + 재측정 + baseline 재비교"의 반복이다.

## 10. 장시간 실행 규칙

full perf는 오래 걸릴 수 있으므로 아래 규칙을 따른다.

- 로그는 반드시 파일과 콘솔에 동시에 남긴다.
- 장시간 실행 시작 후에는 완료 여부 확인 외 다른 작업으로 넘어가지 않는다.
- 실패하면 같은 항목을 owner로 유지하고 즉시 수정 루프로 되돌아간다.
- skip/timeout/crash도 baseline 미달로 취급한다.

권장 로그 예시:

```bash
mkdir -p doc/plan/perf/logs

./core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --recv recv \
  2>&1 | tee doc/plan/perf/logs/perf_recv_full_$(date +%Y%m%d_%H%M%S).log

./core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --recv callback \
  2>&1 | tee doc/plan/perf/logs/perf_callback_full_$(date +%Y%m%d_%H%M%S).log
```

## 11. 종료 판정

아래 조건을 모두 만족할 때만 이 문서 기준으로 종료할 수 있다.

1. `14.0 상태표` 전 행이 `완료`
2. 최신 `recv` 공식 결과 파일 확보
3. 최신 `callback` 공식 결과 파일 확보
4. 두 결과 모두 baseline 상회 판정 메모 기록
5. skip/fail tuple 없음

종료 직전에는 아래를 반드시 다시 확인한다.

1. baseline 두 파일을 다시 읽는다.
2. 최신 결과 파일 경로가 문서에 적혀 있는지 확인한다.
3. `완료`가 아닌 행이 없는지 확인한다.
4. full run이 아니라 spot run만 있는 행이 없는지 확인한다.

아래는 완료가 아니다.

- 일부 대표 transport만 개선
- 평균 latency만 개선
- callback만 개선하고 recv가 퇴행
- 단일 실험 옵션에서만 baseline 초과
- 문서와 로그 없이 "재측정해보니 좋아 보임" 상태

## 12. 금지 규칙

- baseline보다 낮은 항목이 남아 있는데 종료하지 않는다.
- 성능 문제를 테스트 약화로 숨기지 않는다.
- 대용량 tuple 악화를 소형 tuple 개선으로 상쇄했다고 해석하지 않는다.
- transport별 임시 ifdef/환경변수 우회로 닫지 않는다.
- ad-hoc binary나 `/tmp` repro로 근거를 대체하지 않는다.
- `core/build/` 외 다른 build 디렉터리를 쓰지 않는다.

## 13. 산출물

이 루프의 필수 산출물은 아래와 같다.

- 최신 공식 perf 결과 파일 경로
- worst tuple과 원인 메모
- 적용한 코드 변경
- 관련 회귀 테스트 또는 기존 회귀 재사용 근거
- baseline 상회 여부를 적은 상태표
- 루프 로그 디렉터리

## 14. 체크리스트

## 14.0 상태표

상태 값은 아래 네 개만 사용한다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

| ID | 항목 | 상태 | 실행 내용 | 검증 증거 |
|---|---|---|---|---|
| P1 | baseline authority와 비교 규칙 고정 | 완료 | baseline 두 파일과 단일 authority 문서 생성 | `doc/plan/perf/core-perf-optimization-execution-guide.ko.md` |
| P2 | recv worst tuple 식별 및 1차 병목 제거 | 미착수 | 첫 full recv 재측정 후 worst tuple 선정 | 미기록 |
| P3 | callback worst tuple 식별 및 1차 병목 제거 | 미착수 | 첫 full callback 재측정 후 worst tuple 선정 | 미기록 |
| P4 | cross-check: recv 개선이 callback을 깨지 않는지 확인 | 미착수 | 상호 영향 검증 | 미기록 |
| P5 | cross-check: callback 개선이 recv를 깨지 않는지 확인 | 미착수 | 상호 영향 검증 | 미기록 |
| P6 | full recv 결과가 baseline 전체를 상회 | 미착수 | 공식 full recv 재측정 및 비교 메모 | 미기록 |
| P7 | full callback 결과가 baseline 전체를 상회 | 미착수 | 공식 full callback 재측정 및 비교 메모 | 미기록 |
| P8 | 최종 문서/로그/증거 정리 | 미착수 | 결과 경로와 판단 근거 갱신 | 미기록 |

## 14.1 항목 진행 규칙

- 항상 `완료`가 아닌 첫 항목부터 진행한다.
- `검증 증거`가 비어 있으면 `완료`로 바꾸지 않는다.
- `P6`, `P7`은 반드시 full run 결과를 요구한다.
- 한 항목이 끝나면 즉시 다음 항목으로 이동한다.

## 14.2 iteration 종료 시 문서 갱신

매 iteration 끝에는 아래를 반영한다.

1. 방금 건드린 병목과 관련 파일
2. 실행한 test/perf 명령
3. 생성된 로그/결과 파일 경로
4. baseline 대비 남은 부족 항목
5. 다음 iteration의 첫 타깃

## 15. Codex supervisor 종료 메시지 규칙

반복 루프에서 아래 세 형식만 쓴다.

- 더 이상 미적용 사항이 없을 때:

```text
미적용 사항이 없습니다.
```

- 사용자 결정이 필요할 때:

```text
사용자 입력 필요: <한 줄 이유>
```

- 그 외 계속 진행해야 할 때:

```text
계속 진행 필요
```
