# POSD 2차 리팩토링 잔여 작업 실행 가이드

> 상태: active
> 기준 문서: `doc/plan/refactor/2nd/core-system-posd-refactor-master-plan.ko.md`
> 기준 커밋: `85ea0995`
> 대상 범위: `core/`, `core/tests/`
> 최종 성능 검증 도구: `core/perf/run_benchmarks.sh`, `core/perf/run_benchmarks_multi.sh`
> 목적: 남은 POSD 2차 리팩토링 항목을 중간 중단 없이 끝까지 밀기 위한 실행 규칙 고정

## 1. 문서 목적

이 문서는 마스터 플랜의 "남은 작업"만을 대상으로,
실제 구현자가 어떤 순서와 어떤 정지 규칙으로 끝까지 진행해야 하는지를 고정한다.

이 문서는 새 설계를 제안하는 문서가 아니다.
이미 반영된 2차 리팩토링 결과 위에서,
남은 허브와 남은 ownership 정리를 끝까지 닫는 실행 문서다.

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
- `socket_base_t`, `service_runtime_base_t`, `spot` 내부 deep module은 아직 더 정리해야 한다.
- 문서 본체 완료 뒤에는 `core/perf` 기반 perf smoke, full perf, baseline 비교, 성능 회복 루프까지 포함한다.

현재 남은 큰 허브는 대략 아래다.

- `core/src/api/zlink_option.cpp`
- `core/src/api/monitor_api.cpp`
- `core/src/api/monitor_service_api.cpp`
- `core/src/core/options_dispatch.cpp`
- `core/src/sockets/socket_base.cpp`
- `core/src/services/spot/spot_sub.cpp`
- `core/src/services/spot/spot_data_plane.cpp`

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
- "bindings/core-perf 검증 명령을 나중에 찾는다" 금지
- "성능 저하를 확인하고도 대표 일부 케이스만 올린 채 종료" 금지

## 4.1 공통 실행 명령

아래 명령은 실행 문서의 기본 명령으로 고정한다.

```bash
cmake --build core/build -j"$(nproc)"

git diff -- core/include/zlink.h core/src/libzlink.vers
nm -D core/build/lib/libzlink.so | rg " zlink_"

./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100
./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85

./core/tests/run_test_lanes.sh --include-e2e
```

bindings smoke 명령은 아래처럼 고정한다.

```bash
ROOT_DIR="$(pwd)"

bash bindings/cpp/build.sh ON
ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_

ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
  dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -v minimal

(
  cd "$ROOT_DIR/bindings/java"
  chmod +x ./gradlew
  ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    ./gradlew --no-daemon test integrationTest
)

(
  cd "$ROOT_DIR/bindings/node"
  ZLINK_LIB_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    npx --yes node-gyp rebuild
  LD_LIBRARY_PATH="$ROOT_DIR/core/build/lib:${LD_LIBRARY_PATH:-}" \
    npm test
)

(
  cd "$ROOT_DIR/bindings/python"
  ZLINK_LIBRARY_PATH="$ROOT_DIR/core/build/lib/libzlink.so" \
    PYTHONPATH=src \
    python -m pytest -q tests
)
```

적용 규칙은 아래처럼 고정한다.

- 대표 bindings smoke: `C++ + Node + Python`
- full bindings smoke: `C++ + .NET + Java + Node + Python`
- Node smoke는 항상 `node-gyp rebuild` 후 실행한다.
- bindings smoke 실패 시 bindings 코드를 먼저 고치지 말고 `core` 계약 침범 여부를 먼저 확인한다.

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

대표 bindings smoke 명령도 이름으로 고정한다.

- `대표 bindings smoke`
  - `bash bindings/cpp/build.sh ON`
  - `ctest --test-dir bindings/cpp/build --output-on-failure -R test_cpp_`
  - `cd bindings/node && ZLINK_LIB_PATH=... npx --yes node-gyp rebuild && npm test`
  - `cd bindings/python && ZLINK_LIBRARY_PATH=... PYTHONPATH=src python -m pytest -q tests`
- `full bindings smoke`
  - 위 대표 smoke +
  - `dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj -v minimal`
  - `cd bindings/java && ZLINK_LIBRARY_PATH=... ./gradlew --no-daemon test integrationTest`

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
- 비교 key는 `pattern + transport + size`의 exact match로 고정한다.
- 주 비교 지표는 `Throughput`이다.
- baseline에 있는 tuple이 current 결과에 없으면 즉시 fail이다.
- `Throughput`이 baseline 이상이면 해당 tuple은 통과다.
- `Throughput`이 baseline 미만이면 relative regression `(current-baseline)/baseline`이 가장 큰 tuple부터 우선순위를 매긴다.
- 동률이면 더 큰 size, 더 서비스에 가까운 pattern 순서로 우선한다.
- 성능 회복 작업은 기본적으로 `core/`를 고친다.
- `core/perf/`는 측정/비교 실행 surface다.
- `core/perf` 스크립트 자체 버그가 증명된 경우가 아니면 먼저 수정하지 않는다.

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
2. `완료`가 아닌 첫 행을 이번 세션의 우선 작업으로 잡는다.
3. 해당 행의 `실행 문서 체크 항목`과 `검증 증거`를 먼저 확인한다.
4. 증거가 비어 있으면 `완료`로 간주하지 않는다.
5. 첫 행이 `검증중`이면 검증 증거를 먼저 채우고 그 다음 줄로 넘어간다.

완료로 바꾸려면 아래 세 칸이 모두 채워져야 한다.

- `실행 문서 체크 항목`
- `관련 코드/파일`
- `검증 증거`

| 마스터 플랜 항목 | 상태 | 실행 문서 체크 항목 | 관련 코드/파일 | 검증 증거 |
|---|---|---|---|---|
| Phase 0 baseline/perf 기준선 확인 | 미착수 | 5.7 | `doc/plan/refactor/2nd/perf_linux_callback_20260323_082648.txt`, `doc/plan/refactor/2nd/perf_linux_recv_20260323_094627.txt` | baseline 파일, final perf 결과 |
| Phase 1a context/message/errno/version 분리 유지 | 검증중 | 5.1 | `core/src/api/context_api.cpp`, `core/src/api/message_api.cpp` | `cmake --build core/build -j"$(nproc)"`, `git diff -- core/include/zlink.h core/src/libzlink.vers` |
| Phase 1b socket/poller/monitor 분리 유지 | 진행중 | 5.1 | `core/src/api/socket_*.cpp`, `core/src/api/poller_*.cpp`, `core/src/api/monitor*.cpp` | `test_monitor_socket_contract`, `test_monitor_service_contract`, `test_monitor_with_handler`, `unittest_poller` |
| Phase 1b.5 logical multipart send deep module 유지 | 검증중 | 5.1, 5.6 | `core/src/core/multipart_send_txn.*`, gateway/spot publish/send caller | `test_gateway_with_handler`, `test_gateway_handover`, `test_spot_service_introspection` |
| Phase 1c `zlink.cpp` concrete service knowledge 제거 완결 | 진행중 | 5.1 | `core/src/api/zlink.cpp`, `core/src/api/service_*.cpp` | 대표 bindings smoke |
| Phase 2 `socket_base_t` semantic/runtime 분리 | 진행중 | 5.2 | `core/src/sockets/socket_base*` | `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100`, `test_stream_socket`, `test_stream_threadsafe`, `test_stream_send_blocking_wakeup` |
| Phase 3 close/drain contract 명확화 | 진행중 | 5.3 | `core/src/services/common/*`, `core/src/core/ctx.*`, `core/src/sockets/socket_close_ops.*` | `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100` |
| Phase 4 option ownership 분리 | 진행중 | 5.4 | `core/src/core/options*`, `core/src/api/zlink_option.cpp` | `unittest_typed_option`, `test_stream_threadsafe`, `test_spot_service_introspection` |
| Phase 5 service access/factory 경계 재정의 | 진행중 | 5.5 | `core/src/api/service_*.cpp`, `core/src/services/*/*_access.*` | `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection`, full bindings smoke |
| Phase 6 gateway 세부 분해 | 진행중 | 5.6 | `core/src/services/gateway/*` | `test_gateway_with_handler`, `test_gateway_handover` |
| Phase 6 discovery 세부 분해 | 진행중 | 5.6 | `core/src/services/discovery/*` | `test_service_discovery`, `test_service_introspection` |
| Phase 6 spot 세부 분해 | 진행중 | 5.6 | `core/src/services/spot/*` | `test_spot_pubsub_scenario`, `test_spot_service_introspection` |
| Phase 2~3 thread-safe stress 의무 게이트 | 미착수 | 5.2, 5.3 | `core/tests/run_thread_safe_contract_stress.sh` | `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100` 로그 |
| Phase 5~6 full bindings smoke | 미착수 | 5.5, 5.6 | `bindings/*` 실행 surface | full bindings smoke 로그 |
| 최종 `core/perf` smoke/full/baseline 회복 루프 | 미착수 | 5.7 | `core/perf/*` | `core/perf/results/single/report/perf_*_final-single.txt`, `core/perf/results/multi/report/perf_*_final-multi.txt` |

이 표는 각 작업 묶음이 끝날 때마다 반드시 갱신한다.
표에 `미착수`, `진행중`, `검증중`이 하나라도 남아 있으면 문서 완료가 아니다.
표의 `검증 증거` 칸이 비어 있으면 상태를 `완료`로 바꿀 수 없다.
5.0 표의 첫 미완료 행보다 아래 행을 먼저 진행하는 것은 금지한다.

### 5.1 API facade 마무리

- [ ] `core/src/api/zlink_option.cpp`를 common option facade / raw socket specialized option facade / raw subscription facade로 더 분리한다.
- [ ] `core/src/api/monitor_api.cpp`와 `core/src/api/monitor_service_api.cpp`의 monitor query/decode/service-specific branching을 더 좁은 seam으로 내린다.
- [ ] API 계층에서 concrete service knowledge가 다시 재집중되는 경로가 없는지 재점검한다.
- [ ] `Phase 1c` 완료 기준인 대표 bindings smoke를 수행한다.

닫힘 기준:

- [`zlink_option.cpp`](/home/hep7/project/kairos/zlink/core/src/api/zlink_option.cpp) 하나가 raw socket option + raw subscription + service bridge를 동시에 소유하지 않는다.
- monitor API 파일은 query/decode/service-specific wiring을 분리된 TU로 설명할 수 있다.
- `core/include/zlink.h` / `core/src/libzlink.vers` diff 없음
- 대표 bindings smoke 통과

### 5.2 socket runtime 책임 정리

- [ ] `core/src/sockets/socket_base.cpp`를 semantic facade 수준으로 더 줄인다.
- [ ] bind/connect/send/recv/event emission과 lifecycle glue가 hidden collaborator로 내려가도록 정리한다.
- [ ] `socket_base_t` 변경이 family별 파일 수정으로 쉽게 번지지 않는지 확인한다.
- [ ] `Phase 2` 완료 기준인 thread-safe stress를 수행한다.

닫힘 기준:

- [`socket_base.cpp`](/home/hep7/project/kairos/zlink/core/src/sockets/socket_base.cpp)가 semantic entrypoint 위주로 읽힌다.
- bind/connect/send/recv/event emission/lifecycle glue owner를 각 collaborator로 설명할 수 있다.
- 대표 socket 회귀와 thread-safe stress 통과

### 5.3 close/drain contract 정리

- [ ] `service_runtime_base_t`, `service_socket_registry_t`, `ctx_t`, `socket_close_ops_t`의 의미 경계를 더 선명하게 만든다.
- [ ] close/wait/drain 의미 owner를 코드 구조로 설명 가능하게 만든다.
- [ ] lifecycle coordinator와 global removal tracking의 협력 지점을 더 명시적으로 드러낸다.
- [ ] `Phase 3` 완료 기준인 thread-safe stress를 수행한다.

닫힘 기준:

- `service_runtime_base_t`는 lifecycle coordinator로, registry/close/wait는 collaborator contract로 설명된다.
- close/wait/drain 의미 owner를 `ctx_t`, registry, runtime으로 분리해 설명 가능하다.
- thread-safe stress 통과

### 5.4 option ownership 마무리

- [ ] `core/src/core/options_dispatch.cpp`를 owner별 validation/apply module로 더 쪼갠다.
- [ ] owner map이 실제 handler 배치와 항상 일치하도록 유지한다.
- [ ] `options_t` field는 유지하되, 누가 어떤 option을 해석하는지가 더 좁은 owner 경계로 설명되게 만든다.
- [ ] option owner map을 문서와 코드 모두에 명시한다.

닫힘 기준:

- `getsockopt/setsockopt` round-trip 회귀 유지
- transport/TLS/routing/subscription/heartbeat 계열이 owner별 module로 설명된다.
- option owner map이 코드와 문서에 모두 존재한다.

### 5.5 service seam 정제

- [ ] `service_*_api.cpp` 계열이 새로운 허브가 되지 않도록 role별로 더 나눈다.
- [ ] create/attach/start/stop/query/monitor/poller가 일관된 `API -> seam -> concrete service` 구조로 읽히게 만든다.
- [ ] seam이 concrete branching 집합이 아니라 실제 deep module 역할을 하도록 정리한다.

닫힘 기준:

- `service_*_api.cpp` 계열이 새 `zlink.cpp` 역할을 하지 않는다.
- create/attach/start/stop/query/monitor/poller 대표 경로를 file-level owner로 설명 가능하다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection` 유지

### 5.6 서비스별 deep module 완료

- [ ] `gateway`에서 lifecycle / topology refresh / monitor / socket facade / data-plane 경계를 더 명확히 고정한다.
- [ ] `discovery`에서 bootstrap / uplink / registry client / update / state owner를 더 선명히 고정한다.
- [ ] `spot_node`에서 node orchestration / handle composition / discovery-aware control을 더 줄인다.
- [ ] `spot_sub`에서 lifecycle / subject state / recv/direct-handler / option owner를 더 줄인다.
- [ ] `spot_data_plane.cpp`를 protocol/data-plane/assembly 경계로 더 쪼갠다.

닫힘 기준:

- `gateway`, `discovery`, `spot` 각각에 대해 lifecycle / protocol / topology / data-plane owner를 분리해 설명할 수 있다.
- `test_gateway_with_handler`, `test_gateway_handover`, `test_service_discovery`, `test_service_introspection`, `test_spot_pubsub_scenario`, `test_spot_service_introspection`, `test_monitor_service_contract` 유지
- `spot_data_plane.cpp`가 단일 허브처럼 읽히지 않는다

### 5.7 `core/perf` smoke / full baseline 비교 / 성능 회복 루프

- [ ] 모든 코드/테스트 체크리스트를 닫은 뒤 `core/perf` single smoke를 실행한다.
- [ ] single smoke가 정상이면 multi perf smoke를 실행한다.
- [ ] smoke 둘 다 정상이면 single full perf를 실행한다.
- [ ] single full 결과를 `doc/plan/refactor/2nd/perf_linux_callback_20260323_082648.txt`와 비교한다.
- [ ] multi full perf를 실행하고 `doc/plan/refactor/2nd/perf_linux_recv_20260323_094627.txt`와 비교한다.
- [ ] baseline 미만 tuple이 있으면 가장 regression이 큰 `pattern + transport + size`부터 개선한다.
- [ ] 개선 후에는 해당 tuple이 속한 pattern을 먼저 재측정하고, 필요 시 full single 또는 full multi를 다시 실행한다.
- [ ] 모든 tuple이 baseline 이상이 될 때까지 반복한다.

닫힘 기준:

- `core/perf` smoke single / smoke multi 모두 정상 종료
- full single / full multi 결과 파일 확보
- baseline과 exact tuple 비교 완료
- baseline 미만 tuple이 0개
- 최종 full run에서 모든 pattern, 모든 size가 baseline 이상

## 6. 검증 게이트

각 단계는 아래 게이트를 통과해야 다음으로 넘어간다.

### 6.1 공통 게이트

- `cmake --build core/build -j"$(nproc)"`
- 해당 변경에 직접 연결된 `unittest` / `integration` / `e2e` 회귀 묶음
- `core/include/zlink.h` diff 없음 확인
- `core/src/libzlink.vers` diff 없음 확인

### 6.2 phase별 추가 게이트

- `Phase 1c`: 대표 bindings smoke
- `Phase 2`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100`
- `Phase 3`: `./core/tests/run_thread_safe_contract_stress.sh --build-dir core/build --count 100`
- `Phase 5~6`: full bindings smoke
- 최종 perf phase: `core/perf` smoke + full perf + baseline 비교
- 최종 완료 직전: `./core/tests/run_test_lanes.sh --include-e2e`

### 6.3 최종 완료 게이트

아래를 모두 만족해야 "문서 완료"로 본다.

- 마스터 플랜 커버리지 추적 표의 미완료 상태가 0개
- 본 문서 5장의 체크리스트가 전부 체크됨
- `./core/tests/run_test_lanes.sh --include-e2e` 통과
- `Phase 1c` 대표 bindings smoke 완료
- `Phase 5~6` full bindings smoke 완료
- `Phase 2`, `Phase 3` 의무 stress 완료
- 최종 단계에서 `./core/tests/run_thread_safe_contract_perf.sh --build-dir core/build --min-ratio 0.85` 결과 확보
- 최종 perf phase `core/perf` single/multi smoke 정상 종료
- 최종 perf phase `core/perf` single/multi full perf 결과가 baseline 이상
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
8. bindings / stress / lane 최종 게이트
9. `core/perf` smoke / full baseline 비교 / 성능 회복 루프
10. 최종 검증과 문서 완료 판정

이 순서를 어기려면 아래 둘 중 하나를 만족해야 한다.

- 현재 앞선 단계가 이미 사실상 닫혀 있고 다음 단계가 더 낮은 위험인 경우
- 테스트 회귀를 막는 직접 원인이 뒤 단계 파일에 있고, 그 수정이 앞 단계 설계를 훼손하지 않는 경우

각 단계 종료 직전에는 아래 질문에 모두 `예`로 답할 수 있어야 한다.

1. 이 단계의 대표 owner를 한 문장으로 설명할 수 있는가?
2. 이 단계의 대표 테스트 묶음이 green인가?
3. 이 단계 변경이 unrelated module 수정으로 넓게 번지지 않는가?
4. 다음 단계가 이전 단계의 의미를 다시 뒤흔들지 않는가?

Phase 9의 반복 규칙은 아래처럼 고정한다.

1. full single / full multi 결과에서 baseline 미만 tuple을 모두 수집한다.
2. relative regression이 가장 큰 tuple 하나를 고른다.
3. 그 tuple에 직접 연결된 `core/` hot path를 개선한다.
4. 해당 tuple이 속한 pattern만 먼저 재측정한다.
5. baseline 이상이 되면 다음 worst tuple로 넘어간다.
6. 모든 tuple이 baseline 이상이 되면 full single / full multi를 다시 1회 실행해 최종 확정한다.
7. 최종 확정 run에서도 regression이 남아 있으면 1로 돌아간다.

Phase 9에서의 금지 규칙은 아래와 같다.

- 비교 기준을 baseline보다 느슨하게 바꾸는 것
- pattern 또는 size 일부만 제외하는 것
- smoke만 통과시키고 full 비교를 생략하는 것
- `core/perf` runner 출력 형식을 임의로 바꿔 baseline 비교를 어렵게 만드는 것

## 7.1 반복 마스터 플랜 점검 절차

각 작업 묶음이 끝날 때마다 아래 절차를 반드시 수행한다.

1. `core-system-posd-refactor-master-plan.ko.md`의 Phase 1~6, 8.1, 8.2, 9장을 다시 훑는다.
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
