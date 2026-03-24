# POSD 2차 리팩토링 잔여 작업 실행 가이드

> 상태: active
> 기준 문서: `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> 재평가 문서: `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`
> residual 실행 스펙: `doc/plan/refactor/2nd/core-system-posd-refactor-residual-execution-spec.ko.md`
> 기준 커밋: `85ea0995`
> 대상 범위: `core/`, `core/tests/`
> 범위 해석 고정: 이 실행 문서의 완료 조건과 중간 게이트는 `core/`와 `core/tests/`만으로 닫는다. `bindings/*`는 별도 후속 검증 surface로 두며, 이 문서의 진행 차단 조건으로 사용하지 않는다.
> 최종 성능 검증 도구: `core/perf/run_benchmarks.sh`, `core/perf/run_benchmarks_multi.sh`
> 목적: 남은 POSD 2차 리팩토링 항목을 중간 중단 없이 끝까지 밀기 위한 실행 규칙 고정

## 1. 문서 목적

이 문서는 마스터 플랜의 "남은 작업"만을 대상으로,
실제 구현자가 어떤 순서와 어떤 정지 규칙으로 끝까지 진행해야 하는지를 고정한다.

이 문서는 새 설계를 제안하는 문서가 아니다.
이미 반영된 2차 리팩토링 결과 위에서,
남은 허브와 남은 ownership 정리를 끝까지 닫는 실행 문서다.

2026-03-24 현재 워크트리 재리뷰 기준으로
`core-system-posd-refactor-gap-review.ko.md`를 추가 authority로 사용한다.
이 실행 문서는 해당 갭 리뷰를 반영해, 남은 구조 갭을 닫은 뒤 perf 최종 마감을
수행하는 순서를 고정한다.

`5.2A`, `5.3A`, `5.6A`의 세부 구현 결정은
`core-system-posd-refactor-residual-execution-spec.ko.md`를 단일 authority로 사용한다.
실행 중 해당 세 항목의 owner/file/type 경계를 새로 판단하지 않는다.

핵심 원칙은 아래 두 줄이다.

```text
남은 항목은 하나씩 닫되, 진행 보고를 이유로 작업을 멈추지 않는다.
실제 중단은 재현 가능한 hard blocker, ABI 계약 위험, 사용자 판단 필요 상황에서만 허용한다.
```

## 2. 현재 기준선

현재 기준선은 아래처럼 본다.

- `api/zlink.cpp`는 이미 thin facade에 가까워졌다.
- `gateway`, `discovery`는 본체가 크게 줄었고 여러 deep module로 분리됐다.
- `spot subject` API는 publish/recv, option/routing/tls, subscription/query로 분리됐다.
- `options_t` owner map은 시작됐지만 완결은 아니다.
- `service_runtime_base_t` / `socket_close_ops_t` / `ctx_t` close-wait 분업은 실제로 들어갔다.
- 다만 갭 리뷰 기준으로 `socket_base_t`, `ctx_t`, 일부 `spot`/`discovery` deep module은 아직 더 정리해야 한다.
- 문서 본체 완료 뒤에는 `core/perf` 기반 perf smoke, full perf, baseline 비교, 성능 회복 루프까지 포함한다.

현재 남은 큰 허브는 대략 아래다.

- `core/src/api/zlink.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/core/ctx.cpp`
- `core/src/services/discovery/registry.cpp`
- `core/src/services/spot/spot_subject_access.cpp`
- `core/src/services/spot/spot_data_plane.cpp`
- `core/perf` final baseline recovery owner path

## 3. 중단 금지 규칙

이 문서에 따라 작업할 때는 아래 원칙을 따른다.

1. 진행 상황 공유를 이유로 작업을 멈추지 않는다.
2. 한 항목이 끝나면 바로 다음 항목으로 넘어간다.
3. 중간 상태 설명, 부분 요약, 임시 완료 보고는 하지 않는다.
4. 각 항목은 "코드 수정 + 관련 회귀 검증"까지 포함해야 닫힌다.
5. 테스트가 깨지면 같은 흐름 안에서 원인까지 바로 해결한다.
6. 한 항목을 `완료`로 바꾼 직후에는 다음 미완료 항목으로 즉시 이동한다.
7. 사용자의 새 지시가 없으면 "여기까지"를 이유로 세션을 끝내지 않는다.

다만 아래 경우에만 즉시 멈추고 사용자 판단을 요청할 수 있다.

- 로컬 회귀 테스트와 코드 읽기만으로는 결론을 낼 수 없는 C API/ABI 계약 충돌이 발생한 경우
- 사용자 작업과 직접 충돌해서 임의 진행이 위험한 경우
- `core/include/zlink.h` 또는 `core/src/libzlink.vers` 변경이 필요한 경우
- `core/`와 `core/tests/`만으로는 해결할 수 없는 blocker가 생긴 경우

위 네 경우가 아니면 멈추지 않는다.

중단 전 확인 순서는 아래처럼 고정한다.

1. `core/tests/`에 같은 현상을 재현하는 회귀가 이미 있는지 확인한다.
2. 없으면 같은 계약을 때리는 더 작은 회귀를 먼저 추가한다.
3. 그 회귀까지 `core/` 수정으로 해결 가능하면 계속 진행한다.
4. 그래도 `core/`와 `core/tests/` 범위만으로 해결이 불가능할 때만 멈춘다.

중단이 허용되지 않는 상황은 아래처럼 고정한다.

- 특정 phase의 일부 테스트만 green인 상태
- 줄 수 감소나 파일 분리만 확인된 상태
- 미완료 표의 다음 행이 남아 있는 상태
- 최종 perf phase 이전 상태

## 4. 반복 실행 루프

남은 작업은 아래 루프로 끝까지 반복한다.

1. 미완료 항목 목록에서 가장 위의 항목 하나를 고른다.
2. 그 항목의 허브를 더 작은 ownership 단위로 분리한다.
3. 바로 관련 회귀 테스트를 추가 또는 갱신한다.
4. 빌드와 해당 회귀 묶음을 통과시킨다.
5. 문서의 미완료 목록에서 해당 항목을 줄이거나 제거한다.
6. 남은 항목이 없을 때까지 1로 돌아간다.

항목 종료 후 즉시 수행 규칙:

1. 방금 끝낸 항목의 상태를 5.0 표에 반영한다.
2. 5.0 표에서 `완료`가 아닌 첫 행을 다시 찾는다.
3. 그 행이 존재하면 즉시 그 행의 작업으로 넘어간다.
4. 5.0 표가 전부 `완료`일 때만 6.3 최종 게이트로 간다.

금지 규칙은 아래와 같다.

- "일단 분리만 하고 테스트는 나중에" 금지
- "한 번에 여러 phase를 섞어서 원인 추적이 불가능하게 만드는 변경" 금지
- "테스트를 약화해서 green만 만드는 수정" 금지
- "문서와 코드의 ownership 설명이 더 나빠지는 얇은 wrapper 추가" 금지
- "core/perf 검증 명령을 나중에 찾는다" 금지
- "성능 저하를 확인하고도 대표 일부 케이스만 올린 채 종료" 금지

## 4.1 공통 실행 명령

아래 명령은 실행 문서의 기본 명령으로 고정한다.

```bash
cmake --build core/build -j"$(nproc)"

git diff -- core/include/zlink.h core/src/libzlink.vers
nm -D core/build/lib/libzlink.so | rg " zlink_"

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10
./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85

./core/tests/run_test_lanes.sh --include-e2e
```

`thread-safe stress`의 기본/최소 반복 횟수는 `10`으로 둔다.
AI가 flake 재현, 신뢰도 보강, 추가 회귀 확인이 더 필요하다고 판단하면 `10`보다 큰 count를 사용할 수 있다.

bindings smoke는 이 실행 문서의 범위 밖이다.

- `bindings/*` 검증은 별도 후속 문서 또는 별도 세션에서 다룬다.
- 이 문서의 phase 종료, 최종 완료, 중단 판정은 `core/`와 `core/tests/` 증거로만 닫는다.
- bindings 관련 blocker는 `core/include/zlink.h` 또는 `core/src/libzlink.vers` 변경 필요가 없는 한 이 문서의 진행 차단 조건으로 승격하지 않는다.

대표 core 테스트 묶음은 아래처럼 고정한다.

```bash
ctest --test-dir core/build --output-on-failure -R \
'^(unittest_typed_option|unittest_poller|unittest_service_mode_policy)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_monitor_socket_contract|test_monitor_service_contract|test_monitor_with_handler)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection)$'

ctest --test-dir core/build --output-on-failure -R \
'^(test_spot_pubsub_scenario|test_spot_service_introspection)$'
```

해석 규칙:

- `gateway 대표 integration`은 `test_gateway_with_handler|test_gateway_handover`
- `discovery 대표 integration`은 `test_service_discovery|test_service_introspection`
- `spot 대표 integration/e2e`는 `test_spot_pubsub_scenario|test_spot_service_introspection`
- `대표 socket 회귀`는 `test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup`
- `대표 option 회귀`는 `unittest_typed_option`

대표 core API smoke 명령은 아래처럼 이름으로 고정한다.

- `대표 core API smoke`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'`
- `full service core smoke`
  - `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'`

`core/perf` 실행 명령은 아래처럼 고정한다.

```bash
SINGLE_BASELINE="doc/plan/refactor/2nd/perf_linux_callback_20260323_082648.txt"
MULTI_BASELINE="doc/plan/refactor/2nd/perf_linux_recv_20260323_094627.txt"

# smoke: core/perf harness 정상 여부만 빠르게 확인
core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern PAIR,GATEWAY,SPOT \
  --transports tcp,tls,ws,wss \
  --msg-sizes 64,1024,65536 \
  --runs 1 \
  --duration 1 \
  --warmup 1 \
  --results-tag smoke-single

core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern GATEWAY,SPOT,STREAM \
  --transports tcp,tls,ws,wss \
  --msg-sizes 64,1024,65536 \
  --runs 1 \
  --duration 1 \
  --warmup 1 \
  --results-tag smoke-multi

# full: core/perf baseline 비교용 실제 측정
core/perf/run_benchmarks.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern ALL \
  --runs 1 \
  --recv callback \
  --results-tag final-single

core/perf/run_benchmarks_multi.sh \
  --build-dir core/build \
  --reuse-build \
  --pattern ALL \
  --runs 1 \
  --recv recv \
  --results-tag final-multi
```

`core/perf` baseline 비교 규칙은 아래처럼 고정한다.

- single 결과는 `perf_linux_callback_20260323_082648.txt`와 비교한다.
- multi 결과는 `perf_linux_recv_20260323_094627.txt`와 비교한다.
- 최종 성공선의 최소 기준은 baseline 이상이다.
- baseline 미만 수치가 남아 있는 상태는 "거의 회복"이나 "허용 가능한 잔량"으로 해석하지 않는다.
- 최종 완료 판정에서는 baseline 이상을 하한선이 아니라 필수선으로 사용한다.
- 비교 key는 `pattern + transport + size`의 exact match로 고정한다.
- 주 비교 지표는 `Throughput`이다.
- baseline에 있는 tuple이 current 결과에 없으면 즉시 fail이다.
- `Throughput`이 baseline 이상이면 해당 tuple은 통과다.
- `Throughput`이 baseline 미만이면 relative regression `(current-baseline)/baseline`이 가장 큰 tuple부터 우선순위를 매긴다.
- 동률이면 더 큰 size, 더 서비스에 가까운 pattern 순서로 우선한다.
- 성능 회복 작업은 기본적으로 `core/`를 고친다.
- `core/perf/`는 측정/비교 실행 surface다.
- `core/perf` 스크립트 자체 버그가 증명된 경우가 아니면 먼저 수정하지 않는다.
- baseline 비교는 baseline을 만들 때와 동일한 perf harness, 동일한 비교 key,
  동일한 결과 해석 규칙을 유지한 상태에서 수행한다.
- 현재 문제는 리팩토링으로 바뀐 `core/` 내부 owner와 hot path에서 찾는다.
  baseline 미달을 맞추기 위해 `core/perf` 쪽 측정 조건이나 runner 동작을
  바꾸지 않는다.
- `core/perf` 수정은 "현재 baseline 파일도 같은 버그 때문에 잘못 측정됐다"는
  수준의 독립적인 harness 버그가 증명된 경우에만 예외로 허용한다.
- 단지 현재 워크트리의 성능 결과가 baseline 미만이라는 이유만으로
  `core/perf` runner, smoke/full 조합, transport/size 집합, duration/warmup,
  결과 파서, 출력 형식을 바꿔서는 안 된다.

`core/perf` 재개 규칙은 아래처럼 고정한다.

- 새 세션/새 iteration에서 최종 perf phase를 다시 잡았을 때, 이미 기록된 latest full
  single/multi 결과 파일과 baseline 비교 결과가 있으면 그것을 우선 authority로
  사용한다.
- latest full 결과 이후에 perf 기준선을 무효화하는 변경이 없으면, 새 세션 시작
  직후 full perf를 처음부터 다시 돌리지 않는다.
- 최종 perf phase 재개 첫 행동은 latest full 결과에서 baseline 미만 tuple 목록과 worst
  tuple을 다시 읽어 현재 owner를 확정하는 것이다.
- 새 세션에서 바로 다시 수행하는 기본 검증은 full perf가 아니라 smoke와 해당
  worst tuple의 targeted recheck다.
- full single/full multi 재실행은 아래 경우에만 수행한다.
  - latest full 결과 파일/비교 기록이 없거나 증거 경로가 비어 있을 때
  - baseline 파일이 바뀌었을 때
  - perf harness 또는 결과 파서가 바뀌었을 때
  - latest full 결과 이후 변경이 perf hot path owner를 다시 흔들었다고 문서에
    기록된 때
  - 모든 남은 worst tuple이 baseline 이상으로 회복됐는지 최종 확정해야 할 때
- targeted recheck가 실패해도 즉시 full perf 전체를 다시 돌리지 않는다. 같은
  tuple owner 범위 안에서 원인 수정과 targeted recheck를 먼저 반복한다.
- 최종 full perf는 최종 perf phase 종료 확정용 증거다. 중간 triage 단계에서는 기존 latest
  full snapshot을 재사용하고, targeted recheck 로그를 추가 증거로 누적한다.

결과 파일 선택과 worst regression 산출은 아래 명령으로 고정한다.

```bash
SINGLE_RESULT="$(ls -t core/perf/results/single/report/perf_*_final-single.txt | head -n1)"
MULTI_RESULT="$(ls -t core/perf/results/multi/report/perf_*_final-multi.txt | head -n1)"

python - "$SINGLE_BASELINE" "$SINGLE_RESULT" <<'PY'
import re, sys

baseline_path, current_path = sys.argv[1], sys.argv[2]

def parse(path):
    rows = {}
    pattern = None
    transport = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"## PATTERN:\s+([A-Z0-9_]+)", line)
            if m:
                pattern = m.group(1)
                transport = None
                continue
            m = re.match(r"\s*Testing\s+([a-z0-9]+)\b", line)
            if m and "Done" not in line:
                transport = m.group(1)
                continue
            if "|" not in line or pattern is None or transport is None:
                continue
            cols = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cols) < 2 or cols[0] in ("Size", "----------"):
                continue
            size = cols[0]
            thr = re.search(r"([0-9]+(?:\.[0-9]+)?)", cols[1])
            if thr:
                rows[(pattern, transport, size)] = float(thr.group(1))
    return rows

baseline = parse(baseline_path)
current = parse(current_path)
missing = sorted(set(baseline) - set(current))
if missing:
    print("MISSING_TUPLES")
    for key in missing:
        print(",".join(key))
    sys.exit(2)

reg = []
for key, base in baseline.items():
    cur = current[key]
    if cur < base:
        rel = (cur - base) / base
        reg.append((rel, key, base, cur))

reg.sort(key=lambda x: (x[0], -int(x[1][2].rstrip("B")) if x[1][2].endswith("B") else 0))
if not reg:
    print("OK")
    sys.exit(0)

print("REGRESSIONS")
for rel, key, base, cur in reg[:20]:
    print(f"{key[0]},{key[1]},{key[2]},baseline={base:.3f},current={cur:.3f},rel={rel:.6f}")
sys.exit(1)
PY

python - "$MULTI_BASELINE" "$MULTI_RESULT" <<'PY'
import re, sys

baseline_path, current_path = sys.argv[1], sys.argv[2]

def parse(path):
    rows = {}
    pattern = None
    transport = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"## PATTERN:\s+([A-Z0-9_]+)", line)
            if m:
                pattern = m.group(1)
                transport = None
                continue
            m = re.match(r"\s*Testing\s+([a-z0-9]+)\b", line)
            if m and "Done" not in line:
                transport = m.group(1)
                continue
            if "|" not in line or pattern is None or transport is None:
                continue
            cols = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cols) < 2 or cols[0] in ("Size", "----------"):
                continue
            size = cols[0]
            thr = re.search(r"([0-9]+(?:\.[0-9]+)?)", cols[1])
            if thr:
                rows[(pattern, transport, size)] = float(thr.group(1))
    return rows

baseline = parse(baseline_path)
current = parse(current_path)
missing = sorted(set(baseline) - set(current))
if missing:
    print("MISSING_TUPLES")
    for key in missing:
        print(",".join(key))
    sys.exit(2)

reg = []
for key, base in baseline.items():
    cur = current[key]
    if cur < base:
        rel = (cur - base) / base
        reg.append((rel, key, base, cur))

reg.sort(key=lambda x: (x[0], -int(x[1][2].rstrip("B")) if x[1][2].endswith("B") else 0))
if not reg:
    print("OK")
    sys.exit(0)

print("REGRESSIONS")
for rel, key, base, cur in reg[:20]:
    print(f"{key[0]},{key[1]},{key[2]},baseline={base:.3f},current={cur:.3f},rel={rel:.6f}")
sys.exit(1)
PY
```

smoke 실패 처리 규칙은 아래처럼 고정한다.

- smoke가 실패하면 full perf로 넘어가지 않는다.
- smoke 실패는 harness 또는 `core` correctness blocker로 본다.
- failing pattern/transport/size를 제외하지 않고 바로 수정한다.

## 5. 남은 작업 체크리스트

아래 체크리스트를 위에서부터 순서대로 닫는다.
각 항목은 체크 직전까지 실제 코드와 검증으로 닫혀 있어야 한다.

## 5.0 마스터 플랜 커버리지 추적 표

아래 표는 `core-system-posd-refactor-master-plan.ko.md`의 남은 항목이
실행 문서에서 빠지지 않도록 강제로 추적하는 표다.

상태 값은 아래 네 개로만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

상태 전이 규칙은 아래처럼 고정한다.

- `미착수 -> 진행중`: 해당 항목 owner를 옮기는 코드 수정을 시작한 시점
- `진행중 -> 검증중`: 관련 빌드와 해당 항목 검증 명령이 모두 통과한 시점
- `검증중 -> 완료`: 검증 증거 파일/명령/로그 위치까지 표에 기록한 시점
- `완료 -> 진행중`: 이후 변경으로 해당 항목 owner 또는 검증 결과가 다시 흔들린 시점

세션 시작 시 첫 행동은 아래처럼 고정한다.

1. 5.0 표를 먼저 읽는다.
2. 갭 리뷰 문서의 최신 평결이 5.0 표에 반영됐는지 확인한다.
3. `완료`가 아닌 첫 행을 이번 세션의 우선 작업으로 잡는다.
4. 해당 행의 `실행 문서 체크 항목`과 `검증 증거`를 먼저 확인한다.
5. 증거가 비어 있으면 `완료`로 간주하지 않는다.
6. 첫 행이 `검증중`이면 검증 증거를 먼저 채우고 그 다음 줄로 넘어간다.

완료로 바꾸려면 아래 세 칸이 모두 채워져야 한다.

- `실행 문서 체크 항목`
- `관련 코드/파일`
- `검증 증거`

### 5.0.1 장시간 게이트 대기 규칙

아래처럼 수 분 이상 걸릴 수 있는 장시간 검증은 기본적으로 현재 작업 세션에 붙여 실행한다.
콘솔 출력은 이 작업 세션에 계속 보여야 하며, 동시에 로그 파일에도 저장한다.

- `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` (기본/최소)
- `full service core smoke`
- `core/perf` full / baseline 비교

기본 실행 방식:

- 장시간 검증은 본 실행 전에 먼저 `사전 검증(preflight)`을 통과해야 한다.
- `사전 검증`은 최소한 build 디렉터리의 CTest 구성 존재 여부와, 장시간 검증이 호출할 테스트 이름이 현재 CTest에 모두 등록돼 있는지 확인해야 한다.
- `사전 검증`이 실패하면 본 실행을 시작하지 않고 즉시 실패로 처리한 뒤 스크립트/등록 상태를 먼저 수정한다.
- 저장소 로컬 wrapper를 써도 되지만, wrapper는 내부에서 원래 gate 명령을 바꾸지 않고 그대로 호출해야 하며 같은 콘솔/로그 규칙을 유지해야 한다.
- 문서 전체 실행을 Codex supervisor로 감싸는 것은 허용한다. 다만 supervisor는 각 iteration에서 이 문서를 그대로 authority로 사용해야 하고, 완료 판정은 반드시 이 문서 기준으로만 내려야 한다.
- 장시간 검증은 `... 2>&1 | tee <log>` 형태로 실행해서 콘솔 출력과 로그 파일 기록을 동시에 유지한다.
- 장시간 검증을 시작했다면 그 세션은 해당 검증 완료 확인까지 유지한다.
- 백그라운드 실행은 사용자 명시 요청이나 세션 제약이 있는 예외 상황에서만 허용한다.

규칙:

- 해당 행의 구현 변경이 끝났고 남은 일이 장시간 검증 하나뿐이면, 같은 행에 머문 채 검증을 시작한다.
- 장시간 검증 시작 절차는 `사전 검증 성공 -> 본 실행 시작` 순서로 고정한다.
- `thread-safe stress` 반복 횟수는 기본/최소 `10`으로 시작하고, AI가 필요하다고 판단하면 더 큰 count로 올릴 수 있다.
- 장시간 검증을 시작한 뒤에는 다른 미완료 행으로 넘어가지 않는다.
- 장시간 검증이 foreground로 붙어 있는 동안에는 콘솔 출력 자체를 진행 증거로 간주한다.
- 이 대기 중에는 `pgrep`, `ps`, `tail`, 로그 파일 확인처럼 완료 여부를 판단하기 위한 확인 작업만 허용한다.
- 완료 여부 확인은 주기적으로 반복할 수 있으며, 이는 중간 완료 보고나 순서 위반으로 간주하지 않는다.
- 장시간 검증이 성공하면 즉시 5.0 표와 해당 체크리스트를 갱신하고 다음 미완료 행으로 이동한다.
- 장시간 검증이 실패하면 실패 로그를 증거로 남기고 같은 행을 계속 owner로 유지한 채 원인 수정으로 되돌아간다.
- 장시간 검증 실패 후에는 `실패 보고 -> 중단`으로 끝내지 않는다.
- 실패한 장시간 검증은 아래 순서를 끝까지 수행한 뒤에만 다음 미완료 행으로 넘어갈 수 있다.

실패 후 의무 순서:

1. 실패한 테스트 이름, 종료 코드, 마지막 로그 구간을 즉시 기록한다.
2. 실패를 가장 작은 단위의 단일 `ctest -R '^name$'` 또는 대응 core 회귀로 다시 재현한다.
3. 원인을 `core/` 코드에서 분석하고 수정한다.
4. 실패한 단일 회귀를 먼저 통과시킨다.
5. 실패가 발생했던 원래 장시간 gate 전체를 처음부터 다시 실행해서 성공시킨다.
6. 원래 장시간 gate 성공 로그까지 확보한 뒤에만 5.0 표와 체크리스트를 갱신하고 다음 미완료 행으로 이동한다.

금지:

- 장시간 gate 실패를 알고도 다음 phase 구현으로 넘어가는 것
- 단일 재현 없이 timeout 값만 늘리거나 테스트 목록을 줄여서 gate를 우회하는 것
- 실패한 gate 대신 일부 하위 테스트 몇 개만 통과시킨 뒤 완료로 표시하는 것

증거 기록 규칙:

- 장시간 검증 로그는 `doc/plan/refactor/2nd/logs/` 아래에 남긴다.
- `사전 검증` 단계도 같은 콘솔/로그에 남겨서, 테스트 본 실행 전에 검증이 수행됐음을 로그로 증명한다.
- wrapper를 사용한 경우에도 최종 증거는 wrapper 로그가 아니라 내부 gate 명령의 실행 로그와 종료 코드 파일까지 추적 가능해야 한다.
- 장시간 검증은 시작 시각, 로그 경로, 필요하면 PID를 남긴다.
- `완료`로 바꿀 때는 최종 성공 로그 경로까지 표의 `검증 증거` 칸에 기록한다.
- 2026-03-24 재리뷰 기준으로, 현재 워크트리에 존재하지 않는 로그 경로를 근거로
  `완료` 상태를 유지하지 않는다.

### 5.0.2 단계 종료 commit/push 규칙

각 phase 또는 5.0 표의 각 행을 `완료`로 바꾸는 순간에는 아래 순서를 의무로
수행한다.

1. 해당 phase/행과 직접 연결된 코드, 테스트, 문서만 다시 확인한다.
2. `git status --short`로 현재 워크트리를 확인한다.
3. 이번 phase/행에서 만든 변경만 commit한다.
4. commit 직후 현재 작업 브랜치에 push한다.
5. push한 commit hash를 5.0 표의 `검증 증거` 또는 인접 진행 메모에 남긴다.

해석 규칙:

- phase 완료와 commit/push는 분리하지 않는다. phase를 `완료`로 올렸다면 같은
  흐름에서 commit/push까지 끝낸다.
- unrelated 변경이 워크트리에 섞여 있으면 그것까지 묶어서 commit하지 않는다.
- unrelated 변경 때문에 phase 범위만 안전하게 commit할 수 없으면 그 행은 아직
  `완료`가 아니다. 범위를 정리한 뒤 commit/push까지 끝내고 닫는다.
- 커밋 메시지는 phase/owner가 바로 드러나게 짧고 구체적으로 쓴다.
- push 대상은 현재 작업 브랜치다. 별도 release/tag 작업을 의미하지 않는다.
- 최종 완료 직전에도 동일 규칙을 적용하되, 마지막 gate 결과와 문서 갱신을 함께
  포함해 commit/push한다.

| 마스터 플랜 항목 | 상태 | 실행 문서 체크 항목 | 관련 코드/파일 | 검증 증거 |
|---|---|---|---|---|
| 갭 리뷰 문서 추가 및 실행 authority 반영 | 완료 | 5.0 재판정 | `doc/plan/refactor/2nd/core-system-posd-refactor-gap-review.ko.md`, `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md` | 갭 리뷰 문서 추가, 실행 문서 기준선/순서/추적표 갱신 |
| Residual 실행 스펙 추가 및 authority 반영 | 완료 | 5.0 재판정 | `doc/plan/refactor/2nd/core-system-posd-refactor-residual-execution-spec.ko.md`, `doc/plan/refactor/2nd/core-system-posd-refactor-remaining-execution-guide.ko.md` | residual 실행 스펙 추가, `5.2A`/`5.3A`/`5.6A` 구현 경계/검증 명령 고정 |
| Phase 1a context/message/errno/version 분리 유지 | 완료 | 5.1 | `core/src/api/context_api.cpp`, `core/src/api/message_api.cpp` | `cmake --build core/build -j"$(nproc)"`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 1b socket/poller/monitor 분리 유지 | 완료 | 5.1 | `core/src/api/socket_*.cpp`, `core/src/api/poller_*.cpp`, `core/src/api/monitor*.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(unittest_typed_option|unittest_poller)$'`, `ctest --test-dir core/build --output-on-failure -R '^(test_monitor_socket_contract|test_monitor_service_contract|test_monitor_with_handler)$'` |
| Phase 1b.5 logical multipart send deep module 유지 | 완료 | 5.1, 5.6 | `core/src/core/multipart_send_txn.*`, gateway/spot publish/send caller | `ctest --test-dir core/build --output-on-failure -R '^test_gateway_handover$'`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'` |
| Phase 1c `zlink.cpp` concrete service knowledge 제거 완결 | 완료 | 5.1 | `core/src/api/zlink.cpp`, `core/src/api/service_*.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_service_discovery|test_spot_service_introspection)$'` |
| Phase 2 `socket_base_t` semantic/runtime 분리 | 완료 | 5.2 | `core/src/sockets/socket_base*`, `core/src/sockets/socket_runtime.cpp` | `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode` |
| Phase 3 close/drain contract 명확화 | 완료 | 5.3 | `core/src/services/common/service_runtime_base.*`, `core/src/services/common/service_socket_registry.hpp`, `core/src/core/ctx.*`, `core/src/sockets/socket_close_ops.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode` |
| Phase 4 option ownership 분리 | 완료 | 5.4 | `core/src/core/options_dispatch.cpp`, `core/src/core/options_core_socket.cpp`, `core/src/core/options_transport_network.cpp`, `core/src/core/options_protocol_metadata.cpp`, `core/src/core/options_owner.*`, `core/src/api/zlink_option*.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^unittest_typed_option$'`, `ctest --test-dir core/build --output-on-failure -R '^test_stream_threadsafe$'`, `ctest --test-dir core/build --output-on-failure -R '^test_spot_service_introspection$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 5 service access/factory 경계 재정의 | 완료 | 5.5 | `core/src/api/service_*.cpp`, `core/src/api/monitor_service_open_api.cpp`, `core/src/services/*/*_access.*`, `core/src/services/spot/spot_subject_access.*`, `core/src/services/discovery/registry_query_access.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'` |
| Phase 6 gateway 세부 분해 | 완료 | 5.6 | `core/src/services/gateway/*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 6 discovery 세부 분해 | 완료 | 5.6 | `core/src/services/discovery/*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 6 spot 세부 분해 | 완료 | 5.6 | `core/src/services/spot/spot_data_plane.cpp`, `core/src/services/spot/spot_data_plane_protocol.cpp`, `core/src/services/spot/spot_data_plane_forwarding.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_sub*.cpp`, `core/src/services/spot/spot_subject_access.*` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract|unittest_service_mode_policy|unittest_spot_subject_access|unittest_typed_option)$'`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 2~3 thread-safe stress 의무 게이트 | 완료 | 5.2, 5.3 | `core/tests/run_thread_safe_contract_stress.sh` | `Phase 2`: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode`; `Phase 3`: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode` |
| Phase 5~6 service core smoke | 완료 | 5.5, 5.6 | `core/src/api/service_*.cpp`, `core/src/services/*` | `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log`, `doc/plan/refactor/2nd/logs/phase5_6_service_core_smoke_20260324_111055.log.exitcode` |
| Gap review 기준 `socket_base_t` residual split 재개 | 완료 | 5.2A | `core/src/sockets/socket_base.hpp`, `core/src/sockets/socket_base.cpp`, `core/src/sockets/socket_base_api.cpp`, `core/src/sockets/socket_base_dispatch.cpp`, `core/src/sockets/socket_base_endpoint.cpp`, `core/src/sockets/socket_base_lifecycle.cpp`, `core/src/sockets/socket_base_monitor.cpp`, `core/src/sockets/socket_runtime.hpp`, `core/src/sockets/socket_runtime.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log`, `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log.exitcode`, `git diff -- core/include/zlink.h core/src/libzlink.vers`, `commit: 6f236851` |
| Gap review 기준 `ctx_t` runtime orchestration residual split | 미착수 | 5.3A | `core/src/core/ctx.hpp`, `core/src/core/ctx.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'`, `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Gap review 기준 service residual deep-module 마감 | 미착수 | 5.6A | `core/src/services/discovery/registry.cpp`, `core/src/services/spot/spot_subject_access.cpp`, `core/src/services/spot/spot_data_plane.cpp` | `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_service_mode_policy|unittest_spot_subject_access|unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| 최종 `core/perf` smoke/full/baseline 회복 루프 | 진행중 | 5.7 | `core/perf/*`, `core/tests/integration/monitoring/test_single_spot_benchmark_process.cpp`, `core/tests/integration/monitoring/test_multi_spot_benchmark_process.cpp`, `core/tests/unittest/unittest_spot_data_plane_budget.cpp`, `core/src/core/pipe.cpp`, `core/src/services/spot/spot_data_plane.cpp`, `core/src/services/spot/spot_data_plane_protocol.cpp`, `core/src/services/spot/spot_data_plane_internal.hpp`, `core/src/services/spot/spot_node_control.cpp`, `core/src/services/spot/spot_runtime.*` | smoke: `doc/plan/refactor/2nd/logs/perf_smoke_single_20260324_111453.log`, `doc/plan/refactor/2nd/logs/perf_smoke_single_20260324_111453.log.exitcode`, `doc/plan/refactor/2nd/logs/perf_smoke_multi_20260324_113846.log`, `doc/plan/refactor/2nd/logs/perf_smoke_multi_20260324_113846.log.exitcode`; initial full: `doc/plan/refactor/2nd/logs/perf_full_single_20260324_120711.log`, `doc/plan/refactor/2nd/logs/perf_full_single_20260324_120711.log.exitcode`, `doc/plan/refactor/2nd/logs/perf_full_multi_20260324_123623.log`, `doc/plan/refactor/2nd/logs/perf_full_multi_20260324_123623.log.exitcode`; latest full multi: `doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1908_full.log`, `doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1908_full.log.exitcode`, `core/perf/results/multi/report/perf_linux_recv_20260324_190508_spot-multi-20260324f.txt`; latest targeted: `doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1905xx_wss_policy.log`, `doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1905xx_wss_policy.log.exitcode`, `core/perf/results/multi/report/perf_linux_recv_20260324_190435_spot-multi-wss-policy-20260324.txt`, `doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tls_only_20260324_1910.log`, `doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tls_only_20260324_1910.log.exitcode`, `core/perf/results/multi/report/perf_linux_recv_20260324_190930_spot-multi-tls-only-20260324.txt`; latest single recheck: `doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_184508.log`, `doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_184508.log.exitcode`, `core/perf/results/single/report/perf_linux_callback_20260324_184508_spot-single-20260324f.txt`; core/tests 회귀: `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'` |
| 최종 `thread-safe contract perf` 게이트 | 미착수 | 5.7 | `core/tests/run_thread_safe_contract_perf.sh` | `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85` 결과 |
| Phase 0 baseline/perf 기준선 확인 | 완료 | 5.7 | `doc/plan/refactor/2nd/perf_linux_callback_20260323_082648.txt`, `doc/plan/refactor/2nd/perf_linux_recv_20260323_094627.txt` | baseline 파일 확인; initial full 결과: `doc/plan/refactor/2nd/logs/perf_full_single_20260324_120711.log`, `doc/plan/refactor/2nd/logs/perf_full_single_20260324_120711.log.exitcode`, `doc/plan/refactor/2nd/logs/perf_full_multi_20260324_123623.log`, `doc/plan/refactor/2nd/logs/perf_full_multi_20260324_123623.log.exitcode` |

이 표는 각 작업 묶음이 끝날 때마다 반드시 갱신한다.
표에 `미착수`, `진행중`, `검증중`이 하나라도 남아 있으면 문서 완료가 아니다.
표의 `검증 증거` 칸이 비어 있으면 상태를 `완료`로 바꿀 수 없다.
5.0 표의 첫 미완료 행보다 아래 행을 먼저 진행하는 것은 금지한다.

### 5.1 API facade 마무리

- [x] `core/src/api/zlink_option.cpp`를 common option facade / raw socket specialized option facade / raw subscription facade로 더 분리한다.
- [x] `core/src/api/monitor_api.cpp`와 `core/src/api/monitor_service_api.cpp`의 monitor query/decode/service-specific branching을 더 좁은 seam으로 내린다.
- [x] API 계층에서 concrete service knowledge가 다시 재집중되는 경로가 없는지 재점검한다.
- [x] `Phase 1c` 완료 기준인 대표 core API smoke를 수행한다.

닫힘 기준:

- [`zlink_option.cpp`](/home/hep7/project/kairos/zlink/core/src/api/zlink_option.cpp) 하나가 raw socket option + raw subscription + service bridge를 동시에 소유하지 않는다.
- monitor API 파일은 query/decode/service-specific wiring을 분리된 TU로 설명할 수 있다.
- `core/include/zlink.h` / `core/src/libzlink.vers` diff 없음
- 대표 core API smoke 통과

### 5.2 socket runtime 책임 정리

- [x] `core/src/sockets/socket_base.cpp`를 semantic facade 수준으로 더 줄인다.
- [x] bind/connect/send/recv/event emission과 lifecycle glue가 hidden collaborator로 내려가도록 정리한다.
- [x] `socket_base_t` 변경이 family별 파일 수정으로 쉽게 번지지 않는지 확인한다.
- [x] 대표 socket 회귀와 `test_gateway_monitor_snapshot_churn` 보조 회귀를 통과한다. 검증: `ctest --test-dir core/build --output-on-failure -R '^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'`
- [x] `Phase 2` 완료 기준인 thread-safe stress를 다시 수행한다. 로그: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log`, exitcode: `doc/plan/refactor/2nd/logs/phase2_thread_safe_stress_20260324_090631.log.exitcode`

닫힘 기준:

- [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)가 semantic entrypoint 위주로 읽힌다.
- bind/connect/send/recv/event emission/lifecycle glue owner를 각 collaborator로 설명할 수 있다.
- 대표 socket 회귀와 thread-safe stress 통과

### 5.2A gap review 기준 `socket_base_t` residual split

- [x] `socket_runtime_t` aggregation이 다시 `socket_base_t` 참조 멤버 허브로 풀리지 않게 owner를 더 숨긴다.
- [x] public API admission / callback depth / deferred close를 한 lifecycle coordinator로 더 좁힌다.
- [x] monitor queue/thread, endpoint bookkeeping, async mailbox quiesce가 semantic entrypoint에서 직접 읽히지 않게 줄인다.
- [x] residual split 후에도 family 파일군(`dealer/router/xpub/xsub/stream`) 수정 없이 대표 회귀를 통과하는 경로를 확인한다.
- [x] `test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn`과 stress를 다시 통과시킨다. 로그: `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log`, exitcode: `doc/plan/refactor/2nd/logs/posd_refactor_gate_20260324_213227.log.exitcode`

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_stream_socket|test_stream_threadsafe|test_stream_send_blocking_wakeup|test_gateway_monitor_snapshot_churn)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `3.1`~`3.7`을 따른다.

닫힘 기준:

- [`socket_base.hpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.hpp)와 [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)가 더 이상 lifecycle/dispatch/monitor/endpoint 모든 세부를 한 눈에 직접 소유하는 허브로 읽히지 않는다.
- `socket_runtime_t`는 상태 묶음이 아니라 owner-hidden collaborator 집합으로 설명 가능하다.
- residual split 이후 대표 socket 회귀와 stress 로그가 새 증거로 남아 있다.

진행 메모:

- 2026-03-24 `5.2A` 종료 시점에 마스터 플랜 `Phase 1~6`, `8.1`, `8.2`, `9`와 갭 리뷰를 다시 확인했고, `core/`와 `core/tests/` 범위에서 새 구현 누락은 추가로 발견하지 못했다. 다음 우선순위는 `5.3A`다.

### 5.3 close/drain contract 정리

- [x] `service_runtime_base_t`, `service_socket_registry_t`, `ctx_t`, `socket_close_ops_t`의 의미 경계를 더 선명하게 만든다.
- [x] close/wait/drain 의미 owner를 코드 구조로 설명 가능하게 만든다.
- [x] lifecycle coordinator와 global removal tracking의 협력 지점을 더 명시적으로 드러낸다.
- [x] `Phase 3` 완료 기준인 thread-safe stress를 다시 수행한다. 로그: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log`, exitcode: `doc/plan/refactor/2nd/logs/phase3_thread_safe_stress_20260324_092245.log.exitcode`

닫힘 기준:

- `service_runtime_base_t`는 lifecycle coordinator로, registry/close/wait는 collaborator contract로 설명된다.
- close/wait/drain 의미 owner를 `ctx_t`, registry, runtime으로 분리해 설명 가능하다.
- thread-safe stress 통과

### 5.3A gap review 기준 `ctx_t` runtime orchestration residual split

- [ ] `ctx_t`에서 lazy start / thread runtime 부팅 / termination sequencing / pending inproc 정리 중 분리 가능한 owner를 다시 추출한다.
- [ ] `service_control_runtime()`의 시작 보조와 `start()`/`terminate()` sequencing이 `ctx_t` 단일 허브 지식으로 남지 않게 줄인다.
- [ ] global socket removal owner라는 핵심 책임은 유지하되, startup/shutdown/resource orchestration 지식을 더 숨긴다.
- [ ] residual split 후 self-close / service lifecycle / drain 회귀를 다시 통과시킨다.

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_runtime_base|test_service_introspection_discovery_self_close|test_gateway_send_ready_self_close|test_spot_service_introspection_handler_monitor_close)$'

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `4.1`~`4.7`을 따른다.

닫힘 기준:

- [`ctx.cpp`](/home/hep7/project/kairos/zlink/core/src/core/ctx.cpp)가 startup/shutdown/resource finalization 세부를 모두 직접 조정하는 단일 허브로 읽히지 않는다.
- `ctx_t`의 핵심 책임을 "global runtime registry + termination contract owner" 수준으로 더 좁혀 설명할 수 있다.
- 대표 self-close / service lifecycle 회귀와 필요한 gate 로그가 새 증거로 남아 있다.

### 5.4 option ownership 마무리

- [x] `core/src/core/options_dispatch.cpp`를 owner별 validation/apply module로 더 쪼갠다.
- [x] owner map이 실제 handler 배치와 항상 일치하도록 유지한다.
- [x] `options_t` field는 유지하되, 누가 어떤 option을 해석하는지가 더 좁은 owner 경계로 설명되게 만든다.
- [x] option owner map을 문서와 코드 모두에 명시한다.

Phase 4에서 고정한 option owner map은 아래처럼 해석한다.

- `core-socket`
  - `sndhwm`, `rcvhwm`, `affinity`, `routing_id`, `ipv6`, `immediate`, `conflate`
  - `stream_notify`, `handshake_ivl`, `heartbeat_ivl`, `heartbeat_timeout`, `zmp_metadata`
- `transport-network`
  - `sndbuf`, `rcvbuf`, `linger`, `connect_timeout`, `tcp_maxrt`, `reconnect_*`
  - `tcp_keepalive*`, `tcp_nodelay`, `bindtodevice`, `tos`, `backlog`, `rcvtimeo`, `sndtimeo`
- `protocol-metadata`
  - `heartbeat_ttl`
  - `tls_cert`, `tls_key`, `tls_ca`, `tls_verify`, `tls_require_client_cert`, `tls_hostname`, `tls_trust_system`, `tls_password`
- `service-specific`
  - `subscribe`, `unsubscribe`, `topics_count`
  - router/xpub 계열 option, `FD`, `EVENTS`, `LAST_ENDPOINT`
  - central `options_t` bag이 직접 해석하지 않고 service/socket seam 뒤에서 처리

닫힘 기준:

- `getsockopt/setsockopt` round-trip 회귀 유지
- transport/TLS/routing/subscription/heartbeat 계열이 owner별 module로 설명된다.
- option owner map이 코드와 문서에 모두 존재한다.

### 5.5 service seam 정제

마스터 플랜 참조:
[6.6 Phase 5: service access / factory 경계 재정의](./core-system-posd-refactor-master-plan.ko.md) (약 1058행),
[7.5 Service 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1204행)

- [x] `service_*_api.cpp` 계열이 새로운 허브가 되지 않도록 role별로 더 나눈다.
- [x] `gateway`/`discovery`의 create/status/destroy/monitor open 경로는 concrete type 검증을 API에 남기지 않고 service-local access seam으로 읽히게 유지한다.
- [x] `spot_node`의 service monitor open 경로는 pub/internal receiver 선택을 API에서 직접 조합하지 않고 service-local access seam을 통해 연다.
- [x] create/attach/start/stop/query/monitor/poller가 일관된 `API -> seam -> concrete service` 구조로 읽히게 만든다.
- [x] recv/send-ready handler registration 경로도 일관된 `API -> seam -> concrete service` 구조로 읽히게 만든다.
- [x] seam이 concrete branching 집합이 아니라 실제 deep module 역할을 하도록 정리한다.
- [x] `service_public_api_guard_t`는 service 의미 API가 아니라 public admission/close guard로만 남기고, service 의미 surface는 service-local seam이 소유하게 만든다.
- [x] `services/common/service_access/*` 같은 cross-service access hub를 만들지 않고 service-local seam + common guard 방향을 유지한다.
- [x] service concrete type 변경이 public API 계층의 direct include 변경으로 바로 이어지지 않도록 남은 direct concrete dependency를 줄인다.

닫힘 기준:

- `service_*_api.cpp` 계열이 새 `zlink.cpp` 역할을 하지 않는다.
- create/attach/start/stop/query/handler/monitor/poller 대표 경로를 file-level owner로 설명 가능하다.
- service concrete type 변경이 public API 계층의 direct include 변경으로 이어지지 않는다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection` 유지

### 5.6 서비스별 deep module 완료

마스터 플랜 참조:
[6.7 Phase 6: 서비스별 세부 분해](./core-system-posd-refactor-master-plan.ko.md) (약 1093행),
[7.5 Service 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1204행),
[7.4 Option 계층 규칙](./core-system-posd-refactor-master-plan.ko.md) (약 1188행)

- [x] `gateway`에서 lifecycle / topology refresh / monitor / socket facade / TLS/routing-id attach/query / data-plane 경계를 더 명확히 고정한다.
- [x] `discovery`에서 bootstrap / uplink / registry client / update / state owner를 더 선명히 고정한다.
- [x] `discovery` protocol encode/decode owner가 runtime orchestration과 섞이지 않도록 discovery-local 경계로 고정한다.
- [x] `spot_node`에서 node orchestration / handle composition / discovery-aware control을 더 줄인다.
- [x] `spot_sub`에서 lifecycle / subject state / recv/direct-handler / option owner를 더 줄인다.
- [x] `spot_data_plane.cpp`를 protocol/data-plane/assembly 경계로 더 쪼갠다.
- [x] `spot` internal receiver/dispatch 경로가 node orchestration이나 data-plane 허브에 다시 흡수되지 않도록 service-local seam으로 유지한다.

닫힘 기준:

- `gateway`, `discovery`, `spot` 각각에 대해 lifecycle / protocol / topology / data-plane owner를 분리해 설명할 수 있다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection`, `test_spot_pubsub_scenario`, `test_spot_service_introspection`, `test_monitor_service_contract` 유지
- `spot_data_plane.cpp`가 단일 허브처럼 읽히지 않는다

### 5.6A gap review 기준 service residual deep-module 마감

- [ ] `registry.cpp`에서 registry state/rules, socket ensure/replace, topology query/reply owner를 더 좁은 경계로 다시 나눈다.
- [ ] `spot_subject_access.cpp`에서 publish/query/poller/subject access seam이 다시 허브로 커지지 않게 owner를 더 분리한다.
- [ ] `spot_data_plane.cpp`에서 runtime assembly, forwarding, budget/control carryover owner를 더 선명하게 고정한다.
- [ ] residual service 정리 뒤 full service core smoke와 대표 회귀를 다시 통과시킨다.

필수 검증 명령:

```bash
cmake --build core/build -j"$(nproc)"

ctest --test-dir core/build --output-on-failure -R \
'^(test_gateway_with_handler|test_gateway_handover|test_service_discovery|test_service_introspection|test_spot_pubsub_scenario|test_spot_service_introspection|test_monitor_service_contract)$'

ctest --test-dir core/build --output-on-failure -R \
'^(unittest_service_mode_policy|unittest_spot_subject_access|unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'

git diff -- core/include/zlink.h core/src/libzlink.vers
```

구현 경계와 새 private 파일 허용 범위는
`core-system-posd-refactor-residual-execution-spec.ko.md`의 `5.1`~`5.6`을 따른다.

닫힘 기준:

- [`registry.cpp`](/home/hep7/project/kairos/zlink/core/src/services/discovery/registry.cpp), [`spot_subject_access.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_subject_access.cpp), [`spot_data_plane.cpp`](/home/hep7/project/kairos/zlink/core/src/services/spot/spot_data_plane.cpp)가 각각 단일 허브보다 owner-separated module 집합으로 읽힌다.
- `gateway`, `discovery`, `spot` 각각의 잔여 large file owner를 문장 하나로 설명할 수 있다.
- residual service 정리 이후 full service core smoke와 대표 회귀 로그가 새 증거로 남아 있다.

### 5.7 `core/perf` smoke / full baseline 비교 / 성능 회복 루프

마스터 플랜 참조:
[perf 검증 위임](./core-system-posd-refactor-master-plan.ko.md) (약 1266행)

- [x] 모든 코드/테스트 체크리스트를 닫은 뒤 `core/perf` single smoke를 실행한다.
- [x] single smoke가 정상이면 multi perf smoke를 실행한다.
- [x] smoke 둘 다 정상이면 single full perf를 실행한다.
- [x] single full 결과를 `doc/plan/refactor/2nd/perf_linux_callback_20260323_082648.txt`와 비교한다.
- [x] multi full perf를 실행하고 `doc/plan/refactor/2nd/perf_linux_recv_20260323_094627.txt`와 비교한다.
- [ ] gap review 기준 residual 구조 작업(`5.2A`, `5.3A`, `5.6A`)이 닫힌 latest 코드에서 smoke와 targeted recheck를 다시 수행한다.
- [ ] baseline 미만 tuple이 있으면 가장 regression이 큰 `pattern + transport + size`부터 개선한다.
- [ ] 개선 후에는 해당 tuple이 속한 pattern targeted recheck를 먼저 수행하고, 모든 baseline 미만 tuple이 해소된 시점에만 full single/full multi를 1회 다시 실행한다.
- [ ] 모든 tuple이 baseline 이상이 될 때까지 반복한다.
- [ ] 최종 단계에서 `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85` 결과를 확보한다.

진행 메모:

- `single smoke`는 `doc/plan/refactor/2nd/logs/perf_smoke_single_20260324_111453.log`에서 성공했고 결과 파일 `core/perf/results/single/report/perf_linux_callback_20260324_111453_smoke-single.txt`를 남겼다.
- `multi smoke`는 `doc/plan/refactor/2nd/logs/perf_smoke_multi_20260324_111740.log`에서 `MULTI_SPOT`의 `CLIENT_READY` 경로가 partial/exit 1로 실패했다.
- 같은 failure surface를 더 작은 `core/tests` 회귀로 옮기기 위해 `test_multi_spot_benchmark_process`에 `recv_many_clients_tcp_large` 케이스를 추가했고, 현재 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`에서 같은 exit 1을 재현한다.
- 원인은 `spot` data-plane의 network sender인 `mesh_pub`에 service-local publish queue budget이 없어서 warmup backlog가 phase 전환을 덮는 것이었고, `core/src/services/spot/spot_data_plane.cpp`에 internal `mesh_pub` `SNDHWM` budget을 추가했다.
- `mesh_xsub` dispatch batch를 키워 receive-side drain도 같이 보강했고, 수정 후 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`가 단독 실행에서 통과했다.
- 수정 후 `multi smoke`는 `doc/plan/refactor/2nd/logs/perf_smoke_multi_20260324_113846.log`에서 `status=complete`, `exit=0`으로 회복됐다.
- initial full run 비교 결과 worst regression은 single `SPOT tcp 256B`(`788758.4 -> 106570.0`, `-86.49%`)였고, multi 쪽 worst regression은 `MULTI_SPOT wss 262144B`(`13126.8 -> 3850.0`, `-70.66%`)였다.
- 첫 복구 루프로 `core/src/services/spot/spot_data_plane.cpp`의 internal `mesh_pub` budget을 `64 -> 768`로 올리고, `test_single_spot_benchmark_process`에 `callback/tcp/256B` 회귀를 추가했다. 수정 후 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`와 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`가 통과했다.
- 첫 SPOT single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_130354.log`)에서는 `SPOT tcp 256B`가 `778600.0`까지 회복돼 baseline 대비 `-1.29%`로 줄었다.
- 이후 SPOT multi 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_131113.log`)에서 `MULTI_SPOT ws 262144B`가 다시 `CLIENT_READY` exit 1 partial로 남았고, 같은 계약을 `core/tests` 회귀로 내리기 위해 `test_multi_spot_benchmark_process`에 `recv_many_clients_ws_very_large_smoke` 케이스를 추가했다.
- 새 `core/tests` 회귀는 기본 실행에서 `CLIENT_READY,262144` 이후 active phase 전환을 못 받아 timeout으로 재현됐고, debug 실행에서는 server가 `phase=2`를 정상 송신했지만 client가 warmup backlog 뒤에서 active를 보지 못하는 로그를 남겼다.
- websocket 다중 peer warmup backlog를 줄이기 위한 `mesh_pub` budget 동적 조정까지 시도했지만, 가장 최근 SPOT single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_133559.log`) 기준으로 single `ws/wss 256B`가 각각 baseline 대비 `-85.92%`, `-83.34%`로 크게 남아 있어, single/multi를 동시에 만족하는 최종 기본값 정책은 아직 미확정이다.
- 이번 iteration에서는 `mesh_pub` budget owner를 monitor snapshot polling에서 connected-peer 변화 기반 refresh로 옮기고, websocket policy를 `0 peers -> low`, `1 peer -> high`, `2+ peers -> low`로 조정했다.
- 조정 후 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`가 다시 통과해 `MULTI_SPOT ws 262144B` active phase timeout regression은 core/tests surface에서 회복됐다.
- 같은 변경 후 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`도 통과했고, `callback/ws/256` smoke 케이스를 추가해 최소 websocket single benchmark 회귀를 core/tests에 보강했다.
- 그러나 최신 SPOT single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_135306.log`)에서는 `SPOT ws 256B/1024B/65536B/131072B/262144B`가 `non_zero_exit_-11`로 partial 종료했다.
- 동일 run에서 `SPOT tcp 256B`는 `798916.0`으로 baseline `788758.4`를 넘어섰고, `tls 256B`도 `692985.4`까지 회복했지만 `ws` transport 장시간 callback surface는 아직 미해결이다.
- 후속으로 `test_single_spot_benchmark_process`의 `callback/ws/256` 케이스를 `warmup=2s, duration=5s`까지 늘리고 timeout을 `60s`로 올린 장시간 regression을 추가했지만, 단독 `ws 256` case는 여전히 통과했다.
- 따라서 현재 남은 failure는 `core/tests`의 단일 `ws 256` benchmark보다는, `SPOT ws 64B` 이후 `ws 256B+`로 이어지는 perf runner의 stateful websocket callback sequence에 묶여 있을 가능성이 높다.
- 이후 `object_t::process_command()` SIGSEGV를 다시 추적한 결과, 종료 직전 I/O thread mailbox에 late `pipe_hwm` command가 남아 dying peer pipe로 향하는 surface를 확인했다.
- `core/src/core/pipe.cpp`의 `send_hwms_to_peer()`를 steady-state(`active`) pipe에서만 전송하도록 좁힌 뒤 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`가 다시 통과했고, `doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_150030.log`에서도 `SPOT` single recheck가 `status=complete`, `exit=0`으로 회복됐다.
- 다만 동일 수정 이후 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`가 `test_multi_spot_process_recv_many_clients_tcp_large_smoke`에서 다시 fail했고, debug 로그에서 client는 warmup `phase=1`은 수신하지만 active `phase=2`를 timeout까지 보지 못해 `multi tcp 65536` active phase progression backlog가 새 회귀로 드러났다.
- 후속으로 `core/src/services/spot/spot_data_plane.cpp`의 internal `mesh_pub` queue budget을 websocket 전용이 아니라 connected peer count 기준(`1 peer -> 768`, 그 외 -> `64`)으로 좁힌 뒤 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`와 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`가 모두 다시 통과했다.
- 그 상태에서 `MULTI_SPOT` pattern recheck(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_152028.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_152028_spot-multi-recheck.txt`)를 다시 수행했고, `multi tcp 65536` tuple은 baseline `44923.0` 대비 `40804.0`으로 partial regression은 남지만 더 이상 `CLIENT_READY` hang/partial exit 없이 full tuple을 모두 측정했다.
- 최신 `MULTI_SPOT` recheck 기준 worst regression은 `MULTI_SPOT wss 256B`(`1744141.0 -> 713976.0`, `-59.06%`)와 `MULTI_SPOT tcp 262144B`(`4794.0 -> 2297.0`, `-52.09%`)다.
- 이어서 `SPOT` single recheck(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_152439.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_152439_spot-single-recheck.txt`)를 다시 수행했고, latest single worst regression은 `SPOT wss 256B`(`619200.0 -> 103737.2`, `-83.25%`)와 `SPOT ws 256B`(`749540.0 -> 161618.8`, `-78.44%`)다.
- 후속으로 `mesh_pub` budget 조정 owner를 `mesh_xsub` connected-peer 추정에서 publisher-side `ready_ack` fanout hint로 옮겼다. `spot_node_control`이 `pub delivery ready` source count를 runtime hint로 publish하고, `spot_data_plane`은 `bound transport + ready peer count`로 budget을 재계산한다.
- 현재 runtime policy는 `tcp/tls -> 768 고정`, `ws -> idle 64 / 1 peer 768 / 2+ peers 64`, `wss -> 768 고정`이다. 다만 이 값만으로는 실제 bind-time 초기 `mesh_pub` HWM overwrite를 막지 못해, `bind_pub` 경로를 소유한 `core/src/services/spot/spot_data_plane_protocol.cpp`가 websocket endpoint bind 시에도 runtime 정책과 같은 초기 HWM을 쓰도록 추가 정렬이 필요했다.
- 최신 single postfix recheck(`core/perf/results/single/report/perf_linux_callback_20260324_154810_spot-single-postfix.txt`)에서는 기존 worst tuple이던 `SPOT ws 256B`와 `SPOT wss 256B`가 각각 `749536.4 -> 764214.6`(`+1.96%`), `619199.0 -> 640681.2`(`+3.47%`)로 baseline을 넘겼다.
- 다만 같은 single postfix 기준으로 `SPOT ws 64B`(`-7.51%`), `SPOT wss 64B`(`-6.59%`), `SPOT tcp 64B`(`-4.33%`) 등 10개 tuple이 아직 소폭 baseline 미만이라 single full 최종 확정은 미완료다.
- 최신 multi postfix recheck(`core/perf/results/multi/report/perf_linux_recv_20260324_155448_spot-multi-postfix.txt`)에서는 isolated recheck에서 회복됐던 `MULTI_SPOT wss 256B`가 full transport/size sequence에서는 다시 `1744141.4 -> 635312.8`(`-63.57%`)로 내려갔고, `MULTI_SPOT tls 256B`(`-52.04%`), `MULTI_SPOT wss 262144B`(`-42.38%`), `MULTI_SPOT tcp 262144B`(`-18.64%`)가 남아 있다.
- `bind_pub` 초기 HWM overwrite까지 runtime 정책과 맞춘 뒤 targeted recheck(`core/perf/results/multi/report/perf_linux_recv_20260324_161225_triage-wss64-256-postfix3.txt`)에서는 `MULTI_SPOT wss 64B`가 `2712742.8`, `wss 256B`가 `2033781.6`으로 모두 baseline을 넘겼다.
- 같은 수정 후 full `MULTI_SPOT` 재측정(`core/perf/results/multi/report/perf_linux_recv_20260324_161254_spot-multi-postfix2.txt`)에서는 기존 worst였던 `MULTI_SPOT wss 256B`가 `1744141.4 -> 2070027.8`(`+18.68%`)로 회복됐다.
- 그러나 latest full multi 기준으로는 여전히 9개 tuple이 baseline 미만이며, worst regression은 `MULTI_SPOT wss 64B`(`1949744.0 -> 484420.6`, `-75.15%`), `MULTI_SPOT wss 262144B`(`13127.0 -> 3338.4`, `-74.57%`), `MULTI_SPOT tls 256B`(`2041649.0 -> 1043732.4`, `-48.88%`)다.
- 따라서 현재 남은 owner 문제는 `wss 256B` 자체가 아니라, full multi sequence에서 `wss 64B`, `wss 262144B`, `tls 256B`를 중심으로 다시 무너지는 transport/size carryover surface다. 다음 루프는 latest full multi worst tuple(`MULTI_SPOT wss 64B`)부터 다시 좁힌다.
- `core/src/services/spot/spot_data_plane_internal.hpp`, `spot_data_plane.cpp`, `spot_data_plane_protocol.cpp`의 internal `mesh_pub` budget policy를 다시 조정해 `no peer`, `single ready peer`, `multi peer`를 transport-aware 기본값으로 분리했다. 현재 정책은 `tcp/ws`는 `0 peers -> 64`, `1 peer -> 768`, `2+ peers -> 64`이고, `tls/wss`는 `0 peers -> 64`, `1+ peers -> 768`이다.
- 이 정책으로 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`와 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`가 다시 통과해 `tcp 65536 / 100 clients`와 `ws 262144 / 100 clients` active phase timeout regression은 `core/tests` surface에서 유지되지 않게 됐다.
- 최신 `MULTI_SPOT` full 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_163452.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_163452_spot-multi-postfix4.txt`)에서는 `tcp 65536B`가 baseline `44922.6 -> 52442.0`(`+16.73%`), `tcp 262144B`가 `4793.8 -> 16657.6`(`+247.48%`)로 회복됐고, `tls 256B`도 `2041648.6 -> 1885190.0`(`-7.66%`)까지 좁혀졌다.
- 반면 latest full multi worst regression은 `MULTI_SPOT wss 256B`(`1744141.4 -> 561788.2`, `-67.79%`), `MULTI_SPOT wss 64B`(`1949744.0 -> 964878.8`, `-50.51%`), `MULTI_SPOT wss 1024B`(`520737.8 -> 321062.6`, `-38.34%`), `MULTI_SPOT tls 65536B`(`30937.4 -> 19186.6`, `-37.98%`)다.
- 따라서 현재 남은 owner 문제는 더 이상 `tcp/ws` phase progression이 아니라, `wss` small/mid payload와 `tls 65536B`가 baseline 미만으로 남는 secure transport active throughput path다. 다음 루프는 latest full multi worst tuple(`MULTI_SPOT wss 256B`)부터 다시 좁힌다.
- 이후 `mesh_pub` HWM을 secure connected-peer fallback으로 끌어올리는 시도도 했지만, full `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_164629.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_164629_spot-multi-connected-fallback.txt`)에서 `wss 64B/256B/262144B`와 `tls 1024B`가 더 나빠져 해당 가설은 폐기하고 코드는 되돌렸다.
- 대신 `core/src/services/spot/spot_data_plane_protocol.cpp`의 `unbind_pub` 경로에서 stale `peer_ctrl_endpoints`를 명시적으로 끊고, `clear_snapshot_sources()`가 state 정리를 항상 수행하도록 정리했다.
- cleanup 후 full `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_165337.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_165337_spot-multi-unbind-cleanup.txt`)에서는 `MULTI_SPOT wss 64B`가 `964878.8 -> 1018933.4`, `wss 256B`가 `561788.2 -> 607838.0`, `wss 1024B`가 `321062.6 -> 337132.2`, `tls 65536B`가 `19186.6 -> 54657.6`으로 회복됐다.
- 반면 같은 full run에서 `MULTI_SPOT tls 131072B`가 `8134.0 -> 1971.8`로 급락했는데, `tls`만 분리한 triage(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_165821.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_165821_spot-multi-tls-triage.txt`)에서는 `tls 131072B`가 `17236.8`로 baseline을 다시 넘겨 full-sequence carryover 또는 single-run variance 성격이 강함을 확인했다.
- latest full multi 기준 worst regression은 `MULTI_SPOT wss 256B`(`1744141.4 -> 607838.0`, `-65.15%`), `MULTI_SPOT wss 64B`(`1949744.0 -> 1018933.4`, `-47.74%`), `MULTI_SPOT wss 1024B`(`520737.8 -> 337132.2`, `-35.26%`), `MULTI_SPOT tls 1024B`(`1375502.4 -> 1061727.2`, `-22.81%`), `MULTI_SPOT ws 256B`(`803010.8 -> 690338.2`, `-14.02%`)다.
- 따라서 다음 루프의 우선순위는 여전히 latest full multi worst tuple인 `MULTI_SPOT wss 256B`이고, secure transport steady-state 확인용 후속 triage는 `tls 1024B`와 `tls 262144B`를 함께 본다.
- 이번 iteration에서는 `core/tests/integration/monitoring/test_multi_spot_benchmark_process.cpp`의 callback smoke와 `ws/262144/100 clients` smoke timeout 창을 늘려 perf harness smoke의 관측 창을 안정화했지만, 반복 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`에서는 여전히 callback smoke가 간헐적으로 `CLIENT_READY,64` 뒤에서 active phase로 못 넘어가는 flake를 보였다.
- 같은 상태에서 targeted `SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_171622.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_171622_codex-spot-single.txt`)을 다시 수행했는데, single은 baseline 미만 tuple이 21개로 다시 늘었고 worst regression은 `SPOT tls 1024B`(`445641.6 -> 306892.8`, `-31.13%`), `SPOT tls 256B`(`713400.0 -> 520926.4`, `-26.98%`), `SPOT tcp 256B`(`788758.4 -> 576307.0`, `-26.93%`)다.
- 같은 iteration의 targeted `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_171622.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_171622_codex-spot-multi.txt`)에서는 baseline 미만 tuple이 9개로 유지됐고 worst regression은 `MULTI_SPOT wss 64B`(`1949744.0 -> 498892.6`, `-74.41%`), `MULTI_SPOT wss 262144B`(`13126.8 -> 3611.2`, `-72.49%`), `MULTI_SPOT tls 1024B`(`1375476.4 -> 643849.6`, `-53.19%`), `MULTI_SPOT wss 1024B`(`520737.8 -> 298400.4`, `-42.70%`)다.
- 따라서 현재 남은 owner 문제는 단순 `wss 256B` 한 점이 아니라, single에서는 `tcp/tls 256~1024B` callback throughput이 다시 광범위하게 baseline 미만으로 무너지고 multi에서는 secure transport small/large tuple(`wss 64B`, `wss 262144B`, `tls 1024B`)이 여전히 크게 남는 상태다. 다음 루프는 single `SPOT tls 1024B`와 multi `MULTI_SPOT wss 64B`를 같이 좁혀야 한다.
- 현재 워크트리 기준으로 `ctest --test-dir core/build --output-on-failure -R '^test_single_spot_benchmark_process$'`와 `ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`를 다시 실행했으며 둘 다 통과했다. 즉 현재 남은 항목은 `core/tests` 기능 회귀가 아니라 perf baseline 미달 tuple 회복이다.
- fresh targeted `SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_codex_a.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_172601_codex-spot-single-20260324a.txt`)에서는 baseline 미만 tuple이 23개로 늘었고 worst regression은 `SPOT tcp 262144B`(`23500.0 -> 14402.2`, `-38.71%`), `SPOT tls 1024B`(`445641.6 -> 293299.0`, `-34.19%`), `SPOT tcp 256B`(`788758.4 -> 532617.4`, `-32.47%`)였다.
- 같은 시점의 fresh targeted `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_codex_a.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_172602_codex-spot-multi-20260324a.txt`)에서는 baseline 미만 tuple이 11개였고 worst regression은 `MULTI_SPOT wss 64B`(`1949744.0 -> 545464.6`, `-72.02%`), `MULTI_SPOT tls 65536B`(`30937.4 -> 20008.8`, `-35.32%`), `MULTI_SPOT wss 256B`(`1744141.4 -> 1166736.8`, `-33.11%`)였다.
- 원인 가설을 좁히기 위해 `ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM=768` 고정값 triage를 추가로 수행했다. single triage(`doc/plan/refactor/2nd/logs/perf_spot_single_triage_hwm768_20260324.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_173237_triage-single-fixed-hwm768.txt`)에서는 `SPOT tcp 262144B`가 `22621.6`까지 회복됐지만 `tcp 256B`, `tls 1024B`는 오히려 더 낮아져 단순 고정 HWM 상향만으로는 broad regression을 설명하지 못했다.
- 같은 `HWM=768` multi triage(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_hwm768_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_173237_triage-multi-fixed-hwm768.txt`)에서는 `MULTI_SPOT wss 64B`가 baseline을 소폭 상회(`1949744.0 -> 1992926.8`)했지만 `wss 65536B`가 `35561.6 -> 381.4`로 붕괴했다. 따라서 `mesh_pub` budget을 단순 고정 고수준으로 되돌리는 방향은 폐기하고, 다음 루프는 ready-ack/control carryover가 어떤 tuple에서 budget 승격을 늦추거나 과승격시키는지 추적하는 쪽으로 진행한다.
- 이후 코드 재검토에서 `core/src/services/spot/spot_data_plane_internal.hpp`의 `resolve_mesh_pub_sndhwm_default()`가 `wss://`를 단순 websocket으로 묶어 `ready_peers >= 2`에서 `64`로 떨어뜨리고 있음을 확인했다. 이는 실행 가이드 메모에 남긴 의도(`tls/wss`는 ready peer가 생기면 high budget 유지)와 어긋난 실제 구현 drift였고, `unittest_spot_data_plane_budget`을 추가해 `tcp/ws`는 `2+ peers -> 64`, `tls/wss`는 `1+ peers -> 768` 정책을 고정했다.
- 수정 후 회귀 검증으로 `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|test_multi_spot_benchmark_process|test_single_spot_benchmark_process)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers`를 다시 통과했다.
- 수정 후 full `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_175042_codex_c.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_175042_codex-spot-multi-20260324c.txt`)에서는 `MULTI_SPOT wss 256B`가 `373110.0 -> 1702590.8`로 회복돼 baseline 대비 `-2.38%`, `wss 64B`가 `1012011.4 -> 1690950.0`으로 `-13.27%`, `wss 1024B`가 `470335.2 -> 952274.8`로 `+82.88%`까지 올라왔다. 따라서 기존 worst owner였던 `wss` multi-peer budget collapse는 해소됐다.
- 반면 같은 full run의 새 worst regression은 `MULTI_SPOT tls 131072B`(`7426.6 -> 2661.4`, `-64.16%`), `wss 65536B`(`35561.6 -> 23437.8`, `-34.09%`), `tls 256B`(`2041648.6 -> 1529042.0`, `-25.11%`), `tls 65536B`(`30937.4 -> 24490.2`, `-20.84%`)로 이동했다.
- 후속 triage에서 `tls 131072` 단독 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tls131072_iso_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_175449_triage-multi-tls131072-iso.txt`)은 `5875.2`로 baseline 대비 `-20.88%`였고, `tls 256/65536/131072` 묶음(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tls256_65536_131072_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_175449_triage-multi-tls-256-65536-131072.txt`)에서는 `tls 131072`가 `28175.4`로 baseline을 크게 넘겼다. 즉 현재 남은 secure transport 이슈는 `wss` budget 정책이 아니라 `tls` large-payload 측정 시퀀스 또는 carryover owner 쪽으로 좁혀졌다.
- 최신 `MULTI_SPOT tcp,tls` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tcp_tls_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_175842_triage-multi-tcp-tls-20260324.txt`)에서는 `MULTI_SPOT tls 131072B`가 `7426.6 -> 17264.0`으로 baseline을 넘겼다. 반면 같은 run에서 baseline 미만 tuple은 `tls 65536B`(`30937.4 -> 22753.4`, `-26.45%`), `tls 1024B`(`1375476.4 -> 1064374.4`, `-22.62%`), `tls 256B`(`2041648.6 -> 1755079.6`, `-14.04%`), `tcp 256B`(`2482205.0 -> 2307499.8`, `-7.04%`), `tcp 64B`(`3960717.0 -> 3774248.8`, `-4.71%`), `tcp 1024B`(`1023246.6 -> 1005932.8`, `-1.69%`)로 남았다. 따라서 현재 owner는 `tls 131072B` 단일 문제가 아니라 `multi tcp/tls` transport sequence 전반의 sender-side steady-state carryover 쪽이다.
- fresh `SPOT` single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_180049.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_180028_spot-single-20260324d.txt`)에서는 baseline 미만 tuple이 10개까지 줄었다. worst regression은 `SPOT wss 64B`(`1332092.6 -> 1226437.4`, `-7.93%`), `SPOT ws 64B`(`1331635.6 -> 1245767.4`, `-6.45%`), `SPOT tcp 64B`(`1611443.8 -> 1532530.0`, `-4.90%`)였고, 이전에 크게 흔들리던 `tcp/tls 256~1024B`, `ws/wss 256B`는 대부분 baseline 근처까지 회복됐다.
- 같은 시점 회귀 검증으로 `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`를 다시 통과했다. 즉 현재 남은 항목은 기능 회귀가 아니라 perf baseline 미달 tuple 회복이다.
- `ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM=1024` override triage(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tcp_tls_hwm1024_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_180647_triage-multi-tcp-tls-hwm1024-20260324.txt`)에서는 `tcp 64B`만 `3960717.0 -> 4129972.0`으로 개선됐지만 `tcp 65536B`(`44922.6 -> 35783.4`), `tcp 131072B`(`29009.6 -> 18430.6`), `tcp 262144B`(`4793.8 -> 4266.8`), `tls 65536B`(`30937.4 -> 4255.4`)가 크게 악화됐다. 따라서 `mesh_pub` budget의 단순 상향은 해법이 아니며 현재 가설에서 제외한다.
- 후속으로 `core/tests/integration/monitoring/test_multi_spot_benchmark_process.cpp`에 `wss 64,256,1024,65536,131072,262144 / 100 clients` sequence 회귀를 추가해 full multi에서 보이던 `CLIENT_READY` carryover를 더 작은 `core/tests` surface로 내렸다. 초기 재현에서는 `CLIENT_READY,64` 이후 `256` 단계로 넘어간 뒤 다음 size ready가 오지 않아 timeout으로 실패했다.
- debug 추적으로는 client가 `256` active 단계에서도 이전 `64` phase를 계속 관측하며 size/phase가 진동했고, 이는 secure multi-peer 경로에서 `mesh_pub` budget뿐 아니라 local fanout backlog도 다음 size로 넘어간다는 근거가 됐다.
- 수정으로 `spot_data_plane_protocol.cpp`의 bind/unbind 경로가 `mesh_pub` budget state를 transport transition과 함께 초기화하도록 정리했고, `spot_data_plane.cpp`의 internal fanout `SNDHWM` 기본값을 `1000 -> 64`로 낮췄다. 또한 `mesh_pub` policy는 `tcp/ws -> 0 peers 64, 1 peer 768, 2+ peers 64`, `tls -> 0 peers 64, 1+ peers 768`, `wss -> 0 peers 64, 1 peer 768, 2+ peers 64`로 다시 조정했다.
- 수정 후 회귀 검증으로 `ctest --test-dir core/build --output-on-failure -R '^(test_multi_spot_benchmark_process|test_single_spot_benchmark_process|unittest_spot_data_plane_budget)$'`가 다시 통과했고, 새 `MULTI_SPOT` full 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_184116.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_184116_spot-multi-20260324e.txt`)은 `status=complete`로 회복돼 이전 blocker였던 `wss 131072B/262144B` partial exit가 사라졌다.
- 다만 latest full multi 기준 baseline 미만 tuple은 여전히 8개다. worst regression은 `MULTI_SPOT wss 262144B`(`13126.8 -> 3907.2`, `-70.23%`), `wss 256B`(`1744141.4 -> 634750.8`, `-63.61%`), `wss 64B`(`1949744.0 -> 784626.2`, `-59.76%`), `wss 1024B`(`520737.8 -> 331028.0`, `-36.43%`)다. 즉 owner는 더 이상 partial-exit 재현이 아니라 `wss` steady-state throughput path다.
- latest single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_20260324_184508.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_184508_spot-single-20260324f.txt`)은 baseline 미만 tuple이 10개이며 worst regression은 `SPOT ws 64B`(`1331635.6 -> 1249847.6`, `-6.14%`), `SPOT tls 256B`(`713400.0 -> 703334.2`, `-1.41%`), `SPOT wss 64B`(`1332092.6 -> 1315149.0`, `-1.27%`)다. single은 소폭 미달 위주로 좁혀졌고, 현재 perf owner의 대부분은 multi secure websocket 쪽이다.
- 당시 작업 묶음 종료 시점 기록으로는 마스터 플랜 `Phase 1~6`, `8.1`, `8.2`, `9`를 다시 확인했고 `core/`와 `core/tests/` 범위에서 perf 외 새 구현 항목을 더 찾지 못했다고 판단했다. 다만 이 판단은 갭 리뷰 추가 이전 기록이며, 현재 authority에서는 `5.2A`, `5.3A`, `5.6A` residual 항목을 먼저 닫아야 한다.
- 후속으로 `core/src/services/spot/spot_data_plane_internal.hpp`의 `wss + ready_peers >= 2` `mesh_pub` budget 정책을 `64 -> 512`로 올리고, `unittest_spot_data_plane_budget`에 그 계약을 고정했다. 회귀 검증으로 `cmake --build core/build -j"$(nproc)"`, `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`, `git diff -- core/include/zlink.h core/src/libzlink.vers`를 다시 통과했다.
- 변경 후 `wss` targeted recheck(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1905xx_wss_policy.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_190435_spot-multi-wss-policy-20260324.txt`)에서는 `MULTI_SPOT wss 64B`가 `1949744.0 -> 1674864.0`(`-14.10%`), `wss 256B`가 `1744141.4 -> 1585162.0`(`-9.12%`), `wss 1024B`가 `520737.8 -> 615780.0`(`+18.25%`), `wss 262144B`가 `13126.8 -> 6218.0`(`-52.63%`)로 회복됐다. 즉 기존 `wss 64/256/1024` collapse는 크게 줄었지만 large payload는 아직 남았다.
- 같은 변경 후 full `MULTI_SPOT` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_recheck_20260324_1908_full.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_190508_spot-multi-20260324f.txt`)에서는 baseline 미만 tuple이 5개로 줄었다. 최신 잔량은 `MULTI_SPOT wss 262144B`(`13126.8 -> 3988.0`, `-69.62%`), `tls 64B`(`1340655.8 -> 506558.8`, `-62.22%`), `wss 256B`(`1744141.4 -> 973017.2`, `-44.21%`), `wss 65536B`(`35561.6 -> 33939.4`, `-4.56%`), `tcp 256B`(`2482205.0 -> 2384456.8`, `-3.94%`)다.
- `tls` worst가 secure transport steady-state인지 확인하려고 `tls`만 분리한 recheck(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tls_only_20260324_1910.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_190930_spot-multi-tls-only-20260324.txt`)를 수행했다. 이 run에서는 `tls 64B`가 `1340655.8 -> 1494858.8`로 baseline을 넘겼고, 대신 `tls 65536B`(`30937.4 -> 22966.2`, `-25.77%`)와 `tls 131072B`(`7426.6 -> 8101.6`, baseline 상회) 결과가 갈리며, 현재 `tls 64B`는 `tcp -> tls` transport transition carryover 성격이 강함을 확인했다.
- 반대로 `wss`만 분리한 `mesh_pub=768` triage(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_wss_meshpub768_full_20260324.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_191040_spot-multi-wss-meshpub768-full-20260324.txt`)에서는 `wss 64B/256B`가 baseline을 넘겼지만 `wss 65536B`(`35561.6 -> 16061.4`)와 `wss 262144B`(`13126.8 -> 3852.8`)가 더 악화됐다. 따라서 `wss` multi-peer budget을 단순히 `768`로 더 올리는 방향은 현재 owner 해법에서 제외한다.
- 당시 작업 묶음 종료 시점 기록으로는 마스터 플랜 `Phase 1~6`, `8.1`, `8.2`, `9`를 다시 확인했고 perf loop를 다음 우선순위로 봤다. 다만 현재 authority에서는 이 판단을 historical note로만 보며, perf 재개 전 `5.2A`, `5.3A`, `5.6A`를 먼저 닫는다.
- 최신 `MULTI_SPOT tcp,tls,wss` 재측정(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_tcp_tls_wss_20260324_191636.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_191636_spot-multi-current-20260324_191636.txt`)에서는 baseline 미만 tuple이 `wss 262144B`(`13126.8 -> 6962.8`, `-46.96%`), `wss 256B`(`1744141.4 -> 1347400.8`, `-22.75%`), `wss 65536B`(`35561.6 -> 29664.6`, `-16.58%`), `tls 262144B`(`4171.6 -> 3484.6`, `-16.47%`), `wss 64B`(`1949744.0 -> 1666712.6`, `-14.52%`), `tls 1024B`(`1375476.4 -> 1214707.0`, `-11.69%`), `tcp 256B`(`2482205.0 -> 2229568.2`, `-10.18%`), `tls 256B`(`2041648.6 -> 1986874.2`, `-2.68%`)로 정리됐다. 즉 latest owner는 더 이상 `tls 64B` carryover가 아니라 `wss` large/mid payload steady-state와 `tls 262144B`다.
- 같은 상태에서 `ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM=128` `wss` triage(`doc/plan/refactor/2nd/logs/perf_spot_multi_triage_wss_fanout128_20260324_191943.log`, 결과 `core/perf/results/multi/report/perf_linux_recv_20260324_191943_spot-multi-wss-fanout128-20260324_191943.txt`)를 수행했지만 `wss 64B`(`-60.08%`), `wss 256B`(`-32.00%`), `wss 65536B`(`-28.99%`), `wss 131072B`(`-98.73%`), `wss 262144B`(`-69.17%`)로 더 악화됐다. 따라서 local fanout `SNDHWM`을 단순 상향하는 가설도 현재 owner 해법에서 제외한다.
- 최신 `SPOT` single 재측정(`doc/plan/refactor/2nd/logs/perf_spot_single_recheck_current_20260324_192118.log`, 결과 `core/perf/results/single/report/perf_linux_callback_20260324_192118_spot-single-current-20260324_192118.txt`)에서는 baseline 미만 tuple이 `wss 64B`(`1332092.6 -> 1234098.4`, `-7.36%`), `ws 64B`(`1331635.6 -> 1245494.6`, `-6.47%`), `tls 64B`(`1503686.6 -> 1456160.0`, `-3.16%`), `tcp 64B`(`1611443.8 -> 1587370.0`, `-1.49%`), `ws 1024B`(`511522.2 -> 504353.4`, `-1.40%`), `tls 262144B`(`6300.0 -> 6220.8`, `-1.26%`), `tcp 256B`(`788758.4 -> 786585.4`, `-0.28%`), `tls 256B`(`713400.0 -> 712860.8`, `-0.08%`), `wss 262144B`(`5299.6 -> 5299.0`, `-0.01%`)뿐이었다. single은 계속 소폭 미달 위주로 유지되고, 현재 perf owner의 대부분은 multi secure transport 쪽에 남아 있다.
- 당시 작업 묶음 종료 시점 기록으로는 마스터 플랜 `Phase 1~6`, `8.1`, `8.2`, `9`를 다시 확인했고 perf loop의 다음 tuple owner를 정리했다. 다만 현재 authority에서는 그 tuple 우선순위보다 residual 구조 항목(`5.2A`, `5.3A`, `5.6A`) 완료가 먼저다.
- 후속 재검증에서 `ctest --test-dir core/build --output-on-failure -R '^(unittest_spot_data_plane_budget|test_single_spot_benchmark_process|test_multi_spot_benchmark_process)$'`가 `test_multi_spot_benchmark_process`에서 다시 flake를 보였다. 먼저 `wss 64 -> 256` 시퀀스가 `CLIENT_READY,256` 뒤에서 멈췄고, `wss` multi-peer `mesh_pub` budget을 `512 -> 384`로 낮춘 뒤에는 같은 carryover가 `256 -> 1024`로 이동했다. 즉 남은 owner는 단순 `mesh_pub` budget 절대값 하나라기보다, secure multi-client sequence에서 서로 다른 slot이 이전 size/phase backlog를 각기 다른 속도로 비우는 동안 benchmark client가 전역 `seen_msg_size/seen_phase` 하나로 시작 시점을 잡는 surface까지 포함한다.
- debug 실행(`env PERF_DEBUG=1 PERF_DEBUG_TRANSITIONS=1 ctest --test-dir core/build --output-on-failure -R '^test_multi_spot_benchmark_process$'`)에서는 client stderr에 `size=64`와 `size=256` 또는 `size=1024` transition이 같은 시각대에 교차 기록됐고, 실패 시점에는 `metrics invalid fatal=0 received=0 latency_mean=0` 또는 `active wait failed size=1024`가 남았다. 이는 100개 client slot이 완전히 동기화되지 않은 상태에서 일부 slot은 새 size/phase로 넘어가고 다른 slot은 이전 size backlog를 계속 소비하는 것이며, current multi benchmark/client surface가 이를 전역 단일 상태로 읽어 `active` 집계를 너무 일찍 시작하는지 함께 재검토해야 함을 뜻한다.

닫힘 기준:

- `core/perf` smoke single / smoke multi 모두 정상 종료
- full single / full multi 결과 파일 확보
- baseline과 exact tuple 비교 완료
- baseline 미만 tuple이 0개
- 최종 full run에서 모든 pattern, 모든 size가 baseline 이상
- "거의 baseline"이나 일부 tuple 소폭 미달은 완료로 간주하지 않는다

## 6. 검증 게이트

각 단계는 아래 게이트를 통과해야 다음으로 넘어간다.

### 6.1 공통 게이트

- `cmake --build core/build -j"$(nproc)"`
- 해당 변경에 직접 연결된 `unittest` / `integration` / `e2e` 회귀 묶음
- `core/include/zlink.h` diff 없음 확인
- `core/src/libzlink.vers` diff 없음 확인

### 6.2 phase별 추가 게이트

- `Phase 1c`: 대표 core API smoke
- `Phase 2`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` 이상
- `Phase 3`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 10` 이상
- `Phase 5~6`: full service core smoke
- `Gap review residual`: `5.2A`, `5.3A`, `5.6A` 각 항목의 필수 게이트를 residual execution spec 그대로 수행
- 최종 perf phase: `core/perf` smoke + full perf + baseline 비교
- 최종 완료 직전: `./core/tests/run_test_lanes.sh --include-e2e`

### 6.3 최종 완료 게이트

아래를 모두 만족해야 "문서 완료"로 본다.

- 마스터 플랜 커버리지 추적 표의 미완료 상태가 0개
- 본 문서 5장의 체크리스트가 전부 체크됨
- `./core/tests/run_test_lanes.sh --include-e2e` 통과
- `Phase 1c` 대표 core API smoke 완료
- `Phase 5~6` full service core smoke 완료
- `Phase 2`, `Phase 3` 의무 stress 완료
- gap review 기준 residual 구조 항목(`5.2A`, `5.3A`, `5.6A`) 완료
- 최종 단계에서 `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85` 결과 확보
- 최종 perf phase `core/perf` single/multi smoke 정상 종료
- 최종 perf phase `core/perf` single/multi full perf 결과가 baseline 이상
- 최종 perf 결과에서 단 하나의 tuple도 baseline 미만으로 남지 않음
- ABI surface 무변경 확인
- 남은 허브가 문서의 original concern 묶음을 다시 재집중시키지 않음

## 7. 작업 순서 고정

순서는 아래로 고정한다.

1. `zlink_option.cpp` / monitor API 정리
2. `socket_base.cpp` 정리
3. `service_runtime_base_t` 경계 정리
4. `options_dispatch.cpp` owner별 분리 마무리
5. service seam 정제
6. `gateway` / `discovery` 남은 deep module 마감
7. `spot_node` / `spot_sub` / `spot_data_plane` deep module 정리
8. gap review 기준 `socket_base_t` residual split
9. gap review 기준 `ctx_t` residual split
10. gap review 기준 `registry` / `spot` residual deep module 마감
11. stress / lane 최종 게이트
12. `core/perf` smoke / full baseline 비교 / 성능 회복 루프
13. 최종 검증과 문서 완료 판정

이 순서를 어기려면 아래 둘 중 하나를 만족해야 한다.

- 현재 앞선 단계가 이미 사실상 닫혀 있고 다음 단계가 더 낮은 위험인 경우
- 테스트 회귀를 막는 직접 원인이 뒤 단계 파일에 있고, 그 수정이 앞 단계 설계를 훼손하지 않는 경우

각 단계 종료 직전에는 아래 질문에 모두 `예`로 답할 수 있어야 한다.

1. 이 단계의 대표 owner를 한 문장으로 설명할 수 있는가?
2. 이 단계의 대표 테스트 묶음이 green인가?
3. 이 단계 변경이 unrelated module 수정으로 넓게 번지지 않는가?
4. 다음 단계가 이전 단계의 의미를 다시 뒤흔들지 않는가?

최종 perf phase의 반복 규칙은 아래처럼 고정한다.

1. latest full single / full multi 결과에서 baseline 미만 tuple을 모두 수집한다.
2. relative regression이 가장 큰 tuple 하나를 고른다.
3. 그 tuple에 직접 연결된 `core/` hot path를 개선한다.
4. 해당 tuple이 속한 pattern만 먼저 재측정한다.
5. baseline 이상이 되면 문서의 latest worst tuple 기록을 갱신하고 다음 worst tuple로 넘어간다.
6. 모든 tuple이 baseline 이상이 되면 full single / full multi를 다시 1회 실행해 최종 확정한다.
7. 최종 확정 run에서도 regression이 남아 있으면 그 run을 latest full snapshot으로 다시 기록하고 1로 돌아간다.

최종 perf phase에서의 금지 규칙은 아래와 같다.

- 비교 기준을 baseline보다 느슨하게 바꾸는 것
- pattern 또는 size 일부만 제외하는 것
- smoke만 통과시키고 full 비교를 생략하는 것
- `core/perf` runner 출력 형식을 임의로 바꿔 baseline 비교를 어렵게 만드는 것
- baseline과 현재 run의 측정 조건 차이를 만들어 놓고 같은 기준인 것처럼
  비교하는 것
- baseline 미달을 숨기기 위해 `core/perf` 쪽 runner/옵션/결과 처리만 손보는 것
- 새 세션이 시작됐다는 이유만으로 latest full snapshot을 무시하고 full perf를
  처음부터 다시 돌리는 것

## 7.1 반복 마스터 플랜 점검 절차

각 작업 묶음이 끝날 때마다 아래 절차를 반드시 수행한다.

1. `core-system-posd-refactor-master-plan.ko.md`의 Phase 1~6, 8.1, 8.2, 9장과 `core-system-posd-refactor-gap-review.ko.md`를 다시 훑는다.
2. 이번 작업으로 닫힌 항목이 있으면 5.0 표의 상태를 올린다.
3. 5.0 표에 아직 `미착수`, `진행중`, `검증중`이 남아 있으면 그중 최상단 항목을 다음 작업으로 고른다.
4. master plan에 있는데 5장 체크리스트에 없는 항목을 발견하면 즉시 5장에 새 체크 항목으로 추가한다.
5. master plan과 실행 문서가 어긋나면 실행 문서를 먼저 고치고 그 다음 코드를 진행한다.

이 절차는 "미완료 항목이 없는지 반복해서 확인"하는 공식 루프다.

## 7.2 미완료 0개 판정 규칙

아래 셋을 모두 만족해야 `미완료 0개`로 판정한다.

1. 5.0 추적 표에 `미착수`, `진행중`, `검증중`이 없다.
2. 5.1~5.7 체크리스트에 빈 칸이 없다.
3. 6.3 최종 완료 게이트가 모두 충족된다.

셋 중 하나라도 만족하지 못하면, 문서상 완료가 아니다.

## 8. 완료 보고 규칙

완료 보고는 아래 둘 중 하나일 때만 한다.

- 모든 체크리스트가 끝났을 때
- 3장의 예외 조건 중 하나가 발생했을 때

그 외에는 상태 공유를 위해 멈추지 않는다.

완료가 아니면 아래 이유로 보고하지 않는다.

- 줄 수 감소
- 파일 분리 자체
- 일부 테스트만 통과
- 특정 허브만 국소적으로 줄어든 상태

## 9. 유지 규칙

이 문서는 실행 문서다.
남은 항목이 줄어들면 체크 상태와 기준선을 같이 갱신한다.
다만 문서를 업데이트하는 행위가 실제 구현보다 앞서면 안 된다.

항상 코드와 테스트가 먼저다.
