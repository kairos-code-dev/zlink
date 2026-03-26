# `core` POSD 성능우선 리팩토링 랄프 루프 가이드

> 상태: active
> 대상 범위: `core/`, `core/tests/`, `doc/plan/refactor/3nd/`
> 기본 빌드 디렉터리: `core/build/`
> 실행 래퍼: `doc/plan/refactor/3nd/run_posd_perf_first_ralph_loop.sh`
> 상위 supervisor: `core/tools/run_codex_execution_guide_loop.sh`
> 우선순위: correctness/API-ABI 유지 > 성능 비퇴행 > POSD 기반 복잡도 감소
> 목적: 성능을 더 중요하게 보되, POSD 기준에서 남아 있는 구조 허브를 반복적으로 줄이고, 더 이상 의미 있는 리팩토링 필요가 없을 때까지 자동/반자동 반복을 가능하게 한다.
> 운영 방식: 이 문서는 일회성 마감 문서가 아니라, `core`에 큰 기능 변경이 들어올 때마다 다시 호출하는 상시 리팩토링 authority다.

## 1. 이 문서의 역할

이 문서는 `core`에 대한 3차 POSD 리팩토링 실행 authority다.

이번 루프의 핵심은 단순한 코드 정리가 아니다.
다음 두 조건을 동시에 만족해야 한다.

1. hot path 성능을 악화시키지 않는다.
2. 변경 증폭, 숨은 결합, 허브 객체, 얕은 wrapper를 줄인다.

이 문서는 사람용 계획서이면서, Codex supervisor가 반복 실행할 때 그대로 따를 수 있는 운영 문서다.
루프는 이 문서만 읽고도 다음 작업, 중단 조건, 종료 판정을 정할 수 있어야 한다.

이 문서는 "3차 리팩토링을 한 번 끝내고 버리는 문서"가 아니다.
앞으로 `core`에 큰 기능 추가, 큰 동작 변경, 새 서비스/transport/option 계열 확장이 들어올 때마다
다시 실행하는 운영 문서로 유지한다.

## 1.1 재호출 전제

아래 상황이 생기면 이 문서를 다시 호출하는 것을 기본값으로 한다.

- 새 서비스 계층 추가 또는 기존 서비스의 큰 책임 확장
- 새 transport/protocol/tls 옵션 계열 추가
- socket/core/protocol fast path의 의미 있는 구조 변경
- 기존 허브 파일에 새 정책 분기가 다시 누적된 경우
- 기능은 맞지만 수정 범위가 넓어져 다음 변경 비용이 커졌다고 판단되는 경우

즉 이 문서는 "POSD 리팩토링이 더 이상 영원히 필요 없다"는 선언문이 아니라,
"지금 시점에 추가로 손댈 가치가 큰 구조 허브가 없다"는 상태를 반복적으로 확인하는 문서다.

## 2. 해석 원칙

### 2.1 성능 우선 원칙

동일한 correctness와 계약 유지가 전제될 때 판단 우선순위는 아래처럼 고정한다.

1. 공개 계약(`core/include/zlink.h`, `core/src/libzlink.vers`)을 깨지 않는다.
2. 성능을 악화시킬 가능성이 있는 구조 변경은 증거 없이 밀어 넣지 않는다.
3. 성능이 같거나 더 좋아지는 범위에서 POSD 관점의 복잡도 감소를 추구한다.

즉 "더 예쁜 구조"는 허용 이유가 아니다.
성능상 불확실성이 크면 POSD 순도를 낮추더라도 구조 변경을 보류한다.

### 2.2 POSD 판단 기준

리팩토링 후보는 아래 질문으로 판정한다.

- 새 기능이나 옵션 하나를 넣을 때 수정 파일 수가 과도한가
- 서로 다른 정책이 같은 허브 파일에 몰려 있는가
- 하위 메커니즘을 감추지 못하고 상위 API가 내부 절차를 너무 많이 아는가
- 같은 transport/policy 분기가 여러 파일에 복제돼 있는가
- "얇은 forwarding wrapper"만 늘고 설명은 더 어려워지는가

위 질문에 `예`가 반복해서 나오면 POSD 후보로 본다.

### 2.3 이번 루프의 기본 결론

이 루프는 특정 시점의 고정 후보 목록을 authority로 삼지 않는다.

각 호출과 각 iteration마다 현재 워크트리를 다시 읽고,
아래 질문에 가장 강하게 걸리는 영역을 우선 후보로 잡는다.

- 변경 하나에 수정 범위가 과도하게 넓어지는가
- 정책 분기나 ownership 판단이 여러 파일에 중복돼 있는가
- hot path와 control/policy 코드가 한 허브에 같이 몰려 있는가
- 최근 기능 변경으로 인해 예전에 정리된 경계가 다시 허브화됐는가

즉 우선순위는 문서가 미리 박아 두는 것이 아니라,
그 시점의 코드가 보여 주는 복잡도와 성능 리스크를 기준으로 매번 다시 정한다.

## 3. 범위와 금지 규칙

### 3.1 기본 범위

- 기본 리팩토링 대상 구현 변경: `core/src/`
- 회귀 검증 추가/보강: `core/tests/`
- 진행 문서/로그 갱신: `doc/plan/refactor/3nd/`

범위 해석:

- 이 문서가 말하는 POSD 리팩토링의 기본 대상은 `core/src/` 코드다.
- `core/tests/`와 `core/perf/`는 기본적으로 검증 surface다.
- 따라서 구조 개선의 본체는 `core/src/`에서 수행하고, 테스트와 perf는 그 결과를 검증하는 용도로 다룬다.

### 3.2 기본 금지

- `core/build/` 이외의 빌드 디렉터리 사용 금지
- `build/` 최상위 디렉터리 사용 금지
- ad-hoc repro 프로그램, `/tmp` 실험 바이너리로 정당화 금지
- 테스트나 perf 코드를 `core/src/` 리팩토링 대신 우회책으로 수정 금지
- perf/bench 코드를 `core/src/` 버그 우회용으로 수정 금지
- 테스트 약화, retry 추가, sleep 기반 동기화 추가 금지
- dead code, dead file, stale wrapper를 "나중에 정리" 항목으로 남기고 현재 slice를 닫는 것 금지
- unrelated 변경을 같은 작업 단위에 섞기 금지

테스트/perf 수정 허용 범위는 아래처럼 고정한다.

- `core/tests/` 수정은 새로운 회귀를 추가하거나, 기존 테스트가 실제 계약과 어긋난 것이 명확할 때만 허용한다.
- `core/perf/` 수정은 perf harness 자체 버그, perf 정책 오류, perf 실행 인프라 문제를 고치는 경우에만 허용한다.
- `core/src/` 문제를 가리거나, 리팩토링 비용을 줄이기 위해 테스트/perf 쪽을 바꾸는 것은 금지한다.
- 테스트/perf 실패가 `core/src/` 원인으로 재현되면 먼저 `core/src/`를 고친다.
- 테스트/perf 자체 버그가 아닌 한, 테스트/perf 코드는 가능한 한 그대로 둔다.

dead code/file 정리 규칙은 아래처럼 고정한다.

- 책임을 새 모듈로 옮겼다면 더 이상 참조되지 않는 기존 함수, 분기, 상수, helper, wrapper는 같은 작업 단위에서 제거한다.
- 더 이상 사용되지 않는 소스 파일, 헤더, 내부 helper 파일도 같은 작업 단위에서 삭제 정리한다.
- "일단 새 구조를 추가하고 옛 구조는 다음 iteration에서 지운다"는 기본값으로 허용하지 않는다.
- 단, 삭제 범위가 너무 커져 원인 추적이나 성능 검증이 흐려지면 slice를 더 작게 나눈 뒤 각 slice 안에서 dead code/file까지 닫는다.

### 3.3 멈춰도 되는 경우

아래 경우에만 루프를 멈추고 `사용자 입력 필요: ...`를 출력할 수 있다.

- `core/include/zlink.h` 또는 `core/src/libzlink.vers` 변경이 필요한데 계약 판단이 불분명한 경우
- 사용자 변경과 직접 충돌해 임의 진행이 위험한 경우
- `core/`와 `core/tests/`만으로는 해결할 수 없는 blocker가 확인된 경우
- 성능 저하 가능성이 크고 로컬 증거만으로는 수용 여부를 판단할 수 없는 경우

그 외에는 멈추지 않는다.

## 4. 반복 루프 계약

각 iteration은 아래 순서를 따른다.

1. 이 문서 전체를 다시 읽고 `8. 작업 레지스터`의 첫 미완료 항목을 찾는다.
2. 그 항목이 여전히 가장 큰 복잡도 원인인지 코드로 재검토한다.
3. 더 큰 허브가 새로 보이면 먼저 문서의 우선순위를 갱신한다.
4. 한 번에 하나의 bounded slice만 구현한다.
5. 그 slice에서 더 이상 쓰이지 않는 코드와 파일까지 함께 정리한다.
6. 구조 변경에 맞는 `core/tests/` 회귀를 추가하거나 보강한다.
7. 빌드, 관련 테스트, 필요 시 성능 게이트를 실행한다.
8. 문서의 상태/증거/다음 후보를 갱신한다.
9. 아직 남은 구조 항목이 있으면 다음 iteration으로 간다.

한 iteration에서 해야 할 일은 "코드 수정 + 검증 + 문서 갱신"까지다.
중간 요약만 하고 멈추지 않는다.

## 4.1 큰 기능 변경 이후 재시작 규칙

큰 기능 변경 직후 이 문서를 다시 호출할 때는 아래 순서를 먼저 수행한다.

1. 최근 기능 변경이 만든 새 책임/새 허브/새 hot path를 먼저 식별한다.
2. `8. 작업 레지스터`의 기존 항목이 여전히 우선순위가 맞는지 재평가한다.
3. 새 기능 때문에 생긴 허브가 더 크면 그 항목을 표의 최상단으로 올린다.
4. 기존 `완료` 항목도 새 기능으로 인해 다시 허브화됐으면 `진행중` 또는 `미착수`로 되돌린다.
5. 성능 baseline은 "현재 기능 변경 직후 상태"를 새 기준선으로 다시 잡는다.

즉 재호출 시에는 예전 표를 이어서 쓰되,
우선순위와 baseline은 현재 기능 변경 이후 상태로 다시 잡는다.

## 5. 성능 게이트 규칙

### 5.1 기본 원칙

모든 POSD 리팩토링이 perf 벤치마크를 강제하는 것은 아니다.
하지만 hot path나 fanout, polling, encoding, message fast path를 건드리면 성능 근거가 필요하다.

### 5.2 성능 게이트가 필수인 경우

아래를 수정하면 최소 1개의 성능 근거를 남긴다.

- `core/src/sockets/`
- `core/src/core/msg*`
- `core/src/core/pipe*`
- `core/src/engine/`
- `core/src/protocol/`
- `core/src/services/spot/` 중 data path/control hot path
- `core/src/services/discovery/` 중 steady-state message path

### 5.3 허용되는 성능 근거

아래 중 하나 이상을 선택한다.

- 기존 perf 로그와 비교 가능한 targeted perf 실행
- `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build`
- `./core/perf/run_benchmarks.sh ...`
- `./core/perf/run_benchmarks_multi.sh ...`

선택 규칙:

- socket/core/protocol fast path면 `core/perf/run_benchmarks.sh` 또는 `run_benchmarks_multi.sh`를 우선한다.
- thread-safe 계약 변경이면 `run_thread_safe_contract_perf.sh`를 우선한다.
- 서비스 local control path 정도의 변경이면 targeted integration + 기존 perf reasoning으로 충분할 수 있다.

성능 근거를 남길 때는 비교 기준을 같이 기록한다.

- 가능하면 같은 branch의 직전 commit 또는 작업 직전 로그를 baseline으로 쓴다.
- baseline이 없다면 먼저 baseline 측정부터 수행한 뒤 구조 변경을 시작한다.
- baseline 로그와 변경 후 로그 경로를 모두 `8. 작업 레지스터`의 `검증 증거` 칸에 적는다.
- "기존 perf reasoning"만으로 닫는 경우에도 어떤 이유로 full perf를 생략했는지 `메모` 칸에 적는다.

큰 기능 변경 뒤 재호출하는 경우 baseline 해석은 아래처럼 고정한다.

- 예전 리팩토링 세션의 baseline을 그대로 쓰지 않는다.
- "새 기능이 이미 들어간 현재 HEAD"를 baseline 시작점으로 본다.
- 즉 기능 변경 자체의 cost와, 그 위에 추가로 수행한 POSD 리팩토링 cost를 섞어 해석하지 않는다.

### 5.4 성능 저하 해석

아래 중 하나라도 보이면 해당 리팩토링은 완료로 닫지 않는다.

- 명확한 throughput 하락
- tail latency 악화
- CPU 사용량 상승이 구조 단순화 이익보다 큰 경우
- hot path에 추가 virtual indirection, heap churn, lock contention이 들어간 경우

성능 악화가 보이면:

1. 리팩토링 범위를 더 작게 다시 자른다.
2. hot path 밖으로 정책/분기를 밀어낸다.
3. zero-cost 추상화 수준으로 바꿀 수 없으면 해당 설계를 보류한다.

반복 판정 일관성을 위해 아래 기본 기준을 사용한다.

- targeted perf 또는 thread-safe perf의 핵심 throughput 지표가 baseline 대비 3% 초과 하락하면 `완료`로 닫지 않는다.
- noise가 큰 환경이라고 판단되면 같은 명령을 3회까지 반복해 중앙값으로 비교한다.
- CPU 사용량 또는 tail latency를 수집할 수 있다면 동일 조건에서 비교하고, 정량 근거가 없으면 `보류-정당화됨` 또는 추가 측정으로 되돌린다.
- 수치가 애매하면 낙관적으로 해석하지 말고 더 작은 slice로 재시도한다.

## 6. 기본 검증 명령

모든 작업은 아래 공통 명령 집합을 기준으로 한다.

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build -j"$(nproc)"
```

기본 테스트 묶음:

```bash
ctest --test-dir core/build --output-on-failure -L unittest -j"$(nproc)"
ctest --test-dir core/build --output-on-failure -L integration -j1
./core/tests/run_test_lanes.sh
```

최종 완료 직전에는 targeted 검증만으로 닫지 않는다.
리팩토링 결과를 `완료` 또는 최종 종료로 판정하려면
현재 `core/build/` 기준으로 등록된 전체 테스트가 모두 통과해야 한다.

최종 전체 테스트 기준 명령:

```bash
ctest --test-dir core/build --output-on-failure
./core/tests/run_test_lanes.sh --include-e2e
```

해석 규칙:

- iteration 중간에는 관련 테스트와 targeted 게이트로 빠르게 검증할 수 있다.
- 하지만 최종 `미적용 사항이 없습니다.` 판정 전에는 전체 테스트 통과를 다시 확인한다.
- 전체 테스트가 하나라도 실패하면 해당 리팩토링은 닫지 않고 원인 수정 후 재검증한다.
- 성능 근거가 충분해도 전체 테스트가 깨져 있으면 `완료`나 `보류-정당화됨`으로 덮지 않는다.

## 6.1 최종 perf 무실패 게이트

`core/perf` 전체 패턴/전체 사이즈 무실패 확인은 모든 iteration의 기본 의무가 아니다.
이 게이트는 아래 조건이 동시에 맞을 때만 수행한다.

1. 문서 기준으로 더 진행할 리팩토링 후보가 거의 없다고 판단했다.
2. `7. 종료 판정`의 다른 조건들이 먼저 충족됐다.
3. 남은 일은 최종 종료 검증뿐이다.
4. 이번 호출에서 `git diff` 기준 `core/` 실코드 변경이 존재한다.

즉 `core/perf` 전체 확인은 중간 iteration마다 돌리지 않고,
"이제 더 손댈 POSD 리팩토링이 없다"는 판단 직후,
그리고 이번 호출에서 실제 `core/` 코드 변경이 있었을 때만 최종 게이트로 수행한다.

최종 perf 게이트 목적은 두 가지다.

- 모든 perf 패턴과 메시지 크기 조합이 실패 없이 끝나는지 확인한다.
- 구조 정리가 perf 실행 surface를 깨뜨리지 않았는지 확인한다.

최종 perf 게이트 예시 명령:

```bash
./core/perf/run_benchmarks.sh \
  --build-dir /home/hep7/project/kairos/zlink/core/build

./core/perf/run_benchmarks_multi.sh \
  --build-dir /home/hep7/project/kairos/zlink/core/build
```

해석 규칙:

- 여기서 요구하는 것은 우선 "모든 패턴, 모든 사이즈가 실패 없이 동작하는가"다.
- 중간 iteration에서는 targeted perf로 충분하지만, 최종 종료 직전에는 full perf 실행 surface 무실패를 확인한다.
- 이번 호출에서 `git diff -- core/` 기준 실코드 변경이 없으면 full perf gate는 생략할 수 있다.
- 이때 문서의 `검증 증거` 또는 `메모` 칸에 "이번 호출은 `core/` 코드 변경 없음, full perf gate 생략"을 명시한다.
- full perf 중 하나라도 실패하면 `미적용 사항이 없습니다.` 로 닫지 않는다.
- full perf 실패가 리팩토링 버그인지, perf harness/환경 문제인지 먼저 구분하고, 리팩토링 원인이라면 수정 후 전체 perf를 재실행한다.
- 이번 호출에서 `core/` 코드 변경이 있었다면 full perf 무실패 확인 전에는 "더 리팩토링할 항목이 없다"는 구조 판단만으로 종료하지 않는다.

서비스 구조 변경 시 권장 추가 검증:

```bash
./core/tests/run_test_lanes.sh --include-e2e
./core/tools/run_execution_gate_loop.sh \
  --logs-dir /home/hep7/project/kairos/zlink/doc/plan/refactor/3nd/logs \
  --label posd_perf_first_gate \
  --count 10
```

성능 게이트 예시:

```bash
./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build

./core/perf/run_benchmarks.sh \
  --build-dir /home/hep7/project/kairos/zlink/core/build \
  --pattern PAIR \
  --transports tcp \
  --msg-sizes 64,1024 \
  --runs 3 \
  --duration 5 \
  --recv callback
```

## 7. 종료 판정

루프는 아래 조건을 모두 만족할 때만 종료할 수 있다.

1. `8. 작업 레지스터`의 모든 항목이 `완료` 또는 `보류-정당화됨` 상태다.
2. 남은 후보가 있더라도 성능 리스크가 구조 이익보다 커서 지금 당장 손대지 않는 이유가 문서에 적혀 있다.
3. 새 기능 추가 시 반복 수정이 필요한 구조 허브가 더 이상 명확한 우선순위로 남아 있지 않다.
4. 최근 iteration에서 문서 갱신 없이 바로 손댈 만한 POSD 후보를 더 제시하기 어렵다.
5. 이번 호출에서 정리 대상 slice에 속한 dead code, dead branch, dead file이 남아 있지 않다.
6. 현재 `core/build/` 기준 전체 테스트가 통과한다.
7. 이번 호출에서 `core/` 코드 변경이 있었다면, 더 이상 진행할 리팩토링이 없다고 판단된 뒤에 수행한 `core/perf` 전체 패턴/전체 사이즈 실행이 실패 없이 완료된다.

위 조건들을 만족하면 정확히 아래 한 줄만 출력한다.

```text
미적용 사항이 없습니다.
```

아직 다음에 할 구조 작업이 있으면 정확히 아래 한 줄만 출력한다.

```text
계속 진행 필요
```

종료 판정 해석:

- 이번 종료는 "현재 코드 상태에서 즉시 손댈 가치가 큰 POSD 후보가 없다"는 뜻이다.
- 이후 큰 기능 변경이 들어오면 이 문서를 다시 호출해 종료 판정을 새로 계산한다.
- 따라서 `미적용 사항이 없습니다.` 는 영구 종료가 아니라, 현재 baseline에 대한 일시적 종료다.
- 단, 이 종료 해석은 전체 테스트 통과가 확인된 경우에만 유효하다.
- 그리고 `core/perf` full gate는 마지막 종료 직전에만 수행한다.
- 다만 이번 호출에서 `core/` 코드 변경이 없으면 full perf gate 없이 종료할 수 있다.
- 이번 호출에서 `core/` 코드 변경이 있었다면 full perf 무실패 확인 전에는 종료를 확정하지 않는다.

## 8. 작업 레지스터

상태 값은 아래만 사용한다.

- `미착수`
- `진행중`
- `검증중`
- `완료`
- `보류-정당화됨`

표 갱신 규칙:

- 새 허브를 찾으면 즉시 행을 추가한다.
- `보류-정당화됨`은 성능 리스크 또는 계약 리스크가 근거와 함께 기록된 경우에만 쓴다.
- `완료`에는 반드시 검증 증거를 적는다.
- 큰 기능 변경 뒤 재호출했다면 `메모` 칸에 어떤 기능 변경 이후 재평가인지 적는다.
- 예전 세션의 `완료` 항목이라도 새 기능으로 다시 복잡도가 커졌으면 상태를 되돌린다.
- 표의 우선순위는 고정 번호가 아니라 현재 시점 우선순위다. 재호출 시 재정렬을 허용한다.

| 우선순위 | 상태 | 영역 | 목표 | 성능 주의점 | 관련 파일 | 검증 증거 | 메모 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 미착수 | <현재 가장 큰 허브 영역> | <이번 호출에서 줄이려는 복잡도 한 문장> | <성능상 특히 조심할 점> | <현재 관련 파일/디렉터리> |  | <어떤 큰 기능 변경 이후 재평가인지 또는 선정 이유> |
| 2 | 미착수 | <다음 우선 후보> | <변경 증폭 또는 숨은 결합 설명> | <성능상 경계> | <관련 파일/디렉터리> |  | <보류/후순위 이유 포함 가능> |

## 9. 각 iteration의 체크리스트

각 iteration에서 아래를 순서대로 수행한다.

1. 현재 작업 레지스터의 첫 미완료 행을 읽는다.
2. 해당 영역의 핵심 허브 파일을 읽고 POSD 냄새를 한 문장으로 다시 적는다.
3. 구현 범위를 한 번의 patch 세트로 닫을 수 있을 만큼만 자른다.
4. hot path인지 판정한다.
5. hot path면 성능 게이트를 어떤 것으로 확인할지 먼저 정한다.
6. 코드 변경을 한다.
7. 같은 slice에서 dead code, dead branch, dead file이 생겼는지 확인하고 함께 제거한다.
8. 관련 테스트를 추가/보강한다.
9. 빌드와 targeted 검증을 실행한다.
10. 필요 시 성능 게이트를 실행한다.
11. 문서 표를 갱신한다.
12. 더 남아 있으면 `계속 진행 필요`, 없으면 `미적용 사항이 없습니다.`를 출력한다.

재호출 세션의 첫 iteration에서는 아래 두 단계를 먼저 앞에 추가한다.

1. 최근 큰 기능 변경이 무엇이었는지 `메모` 또는 새 행으로 적는다.
2. 기존 표의 우선순위와 상태를 현재 코드 기준으로 다시 정렬한다.

## 10. 구현 방향 메모

구현 방향은 특정 모듈 목록을 미리 박아 두지 않고,
현재 선택한 허브의 성격에 따라 아래 규칙으로 정한다.

### 10.1 ownership 허브

- 한 기능을 추가할 때 수정 지점이 여러 switch, 여러 owner map, 여러 wrapper로 퍼져 있으면 먼저 의심한다.
- 목표는 "새 정책 추가 시 수정 위치 수"를 줄이는 것이다.
- 단, hot path read 비용이나 기본 dispatch 비용을 늘리는 구조면 보류한다.
- 새 ownership 경계를 세웠다면 더 이상 필요 없는 옛 owner helper, forwarding path, 분산된 stale 분기는 같은 단계에서 제거한다.

### 10.2 transport/policy 중복 허브

- 같은 transport 판정, retry/holdoff/budget 정책, lifecycle 분기가 여러 파일에 복제돼 있으면 한 곳으로 모은다.
- 목표는 분기 규칙의 단일 source를 만드는 것이다.
- 단, 공통화 때문에 hot path에 간접 호출, 추가 lock, heap allocation이 들어가면 다시 자른다.
- 공통화 후 더 이상 쓰이지 않는 transport-specific helper/file은 그대로 남기지 말고 제거한다.

### 10.3 runtime/state 허브

- bootstrap, uplink, control tick, registration, summary, monitor, readiness처럼 서로 다른 책임이 한 타입에 몰려 있으면 분리 후보로 본다.
- 목표는 facade는 얇게 두고 내부 module을 더 깊게 만드는 것이다.
- 단, fail-fast 의미나 shutdown semantics, steady-state path 비용은 유지해야 한다.
- 분리 이후 더 이상 도달하지 않는 old runtime path나 obsolete 파일은 같은 단계에서 삭제한다.

## 11. 로그와 산출물

기본 로그 디렉터리는 아래로 고정한다.

```text
doc/plan/refactor/3nd/logs/
```

각 iteration은 최소 아래를 남기는 것을 권장한다.

- 어떤 허브를 줄였는지
- 어떤 테스트/게이트로 확인했는지
- 성능 이유로 무엇을 보류했는지
- 이번 호출이 어떤 큰 기능 변경 이후에 수행됐는지

게이트/성능 로그 규칙:

- `run_execution_gate_loop.sh`를 사용할 때는 항상 `--logs-dir /home/hep7/project/kairos/zlink/doc/plan/refactor/3nd/logs` 를 명시한다.
- perf 로그를 남길 때는 baseline과 candidate를 구분할 수 있게 파일명이나 메모에 태그를 남긴다.
- `검증 증거` 칸은 비워 두지 않는다. 명령만 적지 말고 실제 로그 파일 경로 또는 명시적 생략 사유를 함께 적는다.

## 12. 최종 원칙

이번 루프의 목표는 "POSD를 명분으로 구조를 계속 흔드는 것"이 아니다.

최종 목표는 아래 한 줄이다.

```text
성능을 지키면서도, 다음 변경이 더 적은 파일과 더 적은 개념으로 끝나는 core를 만든다.
```

그리고 이 원칙은 반복 호출을 전제로 한다.

- 큰 기능 변경이 들어오면 다시 이 문서를 호출한다.
- 그 시점의 baseline과 허브 우선순위를 새로 잡는다.
- 다시 "지금 시점에 더 손댈 가치가 큰 POSD 후보가 없다"는 상태까지 밀어 붙인다.
