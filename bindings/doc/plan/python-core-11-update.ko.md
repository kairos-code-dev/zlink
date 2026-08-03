# Python binding Core 11 최신화 실행 계획

> 이 문서는 Python binding의 raw Core 11 구현, package, sample, perf와 검증 evidence를 관리하는 실행
> 계획이다. Python public contract 자체는 `bindings/doc/spec/python/`이 소유한다.

## 1. 현재 판정

현재 checkout의 Core는 `11.2.0`이다. Python binding은 이 Core의 raw C API만 연결하도록 정리했고,
Linux x86_64에서 source test, clean wheel consumer, raw sample process와 perf smoke까지 통과했다. 다만
다음 조건이 남아 있으므로 전체 완료 판정은 `PARTIAL / NOT CLEAN`이다.

- 현재 `core/build`에서 만든 `11.2.0` candidate와 저장소에 있는 공통 `V11-R2`·`V11-M3-CORE-PKG`
  evidence가 같은 candidate identity인지 확인되지 않았다. 이전 `11.1.0` evidence를 `11.2.0`의 승인으로
  재사용하지 않는다.
- 실행 환경에는 Python 3.9 interpreter가 없어 CPython 3.12 clean consumer만 실행했다. Pyright는
  Python 3.9 target으로 통과했다.
- Linux x86_64 이외의 native payload는 같은 Core candidate로 build하고 consumer를 실행한 evidence가 없다.
- 구현자와 분리된 frontier reviewer의 최종 read-only `CLEAN` 판정이 없다. 현재 POSD·DDD 검토는 이 작업의
  self-review 기록이며 독립 review를 대신하지 않는다.

이 조건들은 source test나 local package 결과를 실패로 바꾸는 것이 아니라, 완료 gate와 local implementation
evidence를 구분하기 위한 것이다.

## 2. 입력과 책임 경계

이번 작업의 local candidate는 다음 입력을 사용한다.

| 항목 | 현재 값 또는 위치 |
|------|------------------|
| Core version | `11.2.0` |
| Core runtime | `core/build/lib/libzlink.so.11.2.0` |
| Core SONAME | `libzlink.so.11` |
| Python package | `zlink==11.2.0` |
| Python source manifest | `.artifacts/wsl/bindings-candidate/python-source-manifest-11.2.0.json` |
| package evidence | `.artifacts/wsl/bindings-candidate/python/candidate-input.env` |
| wheel | `.artifacts/wsl/bindings-candidate/python/wheels/` 아래 산출물 |

Core candidate의 header, spec, source, runtime, exported symbol inventory와 layout은
[`create-manifest.sh`](../../../scripts/local-package/bindings-candidate/create-manifest.sh)가 봉인한다.
Python source manifest는 Python source, test, sample, perf, package script와 Python spec/guide를 기록하고
Core manifest SHA-256을 direct input으로 포함한다. Candidate package script는 두 manifest의 revision과
hash가 현재 checkout과 일치하지 않으면 중단한다.

DDD 기준의 경계는 다음과 같다.

- Core raw bounded context는 `Context`, `Message`, `Received`, `RoutingId`, raw socket, monitor, poller,
  timer와 Core error 의미를 소유한다.
- Python adapter는 native handle, `ctypes` layout, callback trampoline, reference lifetime와 Python
  exception mapping을 내부에 둔다. 이 결정은 public type으로 노출하지 않는다.
- Framework 기능의 actor, spot, dispatch와 bound stream session은 Framework 경계가 소유한다. Python Core
  binding은 이 lifecycle을 재정의하지 않는다.
- `Context`, `Message`/`Received`, socket과 package adapter가 각각 handle·buffer·callback·candidate
  provenance의 aggregate owner다. 자세한 event와 invariant는
  [`POSD·DDD 검토 log`](log/python/2026-08-03-posd-ddd-review.ko.md)에 기록한다.

## 3. 구현 범위와 완료 사실

### PY-01 — Raw FFI와 Core 11 projection — PASS

- `ffi.py`, `_zlink_native.c`, `_zlink_perf_native.c`는 Core 11 raw symbol과 layout만 선언한다.
- Core header에 없는 이전 기능의 FFI, callback, include와 compiled entrypoint를 제거했다.
- `ctx_set_data`/`ctx_get_data`가 필요한 `uint64` option은 Core 함수군의 실제 ABI에 맞춰 연결했다.
- `Message`, `Received`, routing id, raw socket, monitor, poller와 timer의 production path를 유지했다.
- source test와 raw sample에서 native error, no-data, move/close ownership을 확인했다.

### PY-02 — Public API와 bounded context 정리 — PASS

- package root와 contracts에서 Framework 전용 public surface와 이전 compatibility alias를 제거했다.
- raw socket에 Framework lifecycle을 섞던 branch와 helper, 관련 fixture, sample과 perf scenario를 제거했다.
- dynamic `__getattr__`로 public contract를 숨기지 않고 static export를 사용한다.
- `single_part_or_throw()`는 현재 구현과 contract test가 사용하는 이름을 유지한다. 이름을
  `single_part()`로 바꾸는 draft는 별도 review와 contract 변경 없이는 적용하지 않는다.

### PY-03 — Error, no-data와 ownership — PASS

- no-data는 함수군별 계약에 따라 `False` 또는 `None`으로 반환하고 native failure를 숨기지 않는다.
- submit, request, receive, bind, connect, config와 close error가 result와 native errno를 보존한다.
- message send 후 move, caller-provided receive storage, callback reference와 idempotent close를 test로
  고정했다.

### PY-04 — POSD·DDD와 hot-path cost — PASS (self-review)

- message allocation, receive owner, callback dispatcher, GIL 해제와 snapshot copy를
  `tests/hot-path-cost-inventory.json`에 owner와 guard test와 함께 기록했다.
- blocking native wait 중 Python object에 접근하지 않고, callback 경계에서만 Python 실행 상태를 복원한다.
- 기존 raw module을 유지하면서 Framework branch·export·FFI·fixture만 제거하는 대안을 선택했다. 새 facade를
  추가하는 재작성안은 public surface와 ownership 변경을 늘리므로 선택하지 않았다.
- optimization guard, source review와 runtime sample을 통과했다. 독립 reviewer가 아니므로 이 항목은 전체
  `CLEAN` 판정이 아니다.

### PY-05 — Sample과 perf — PASS (Linux x86_64)

Canonical sample은 다음 7개다.

- `request_reply_callback_sample.py`
- `pair_recv_sample.py`
- `dealer_router_recv_sample.py`
- `pubsub_recv_sample.py`
- `stream_recv_sample.py`
- `stream_packet_callback_sample.py`
- `monitor_recv_sample.py`

`samples/run_samples.py --installed`는 clean wheel consumer에서 sample directory만 `PYTHONPATH`에 두고,
repository `src`를 import하지 않는다. Single·multi perf runner는 `ZLINK_LIBRARY_PATH`를 명시적으로 받고
실행 runtime path와 SHA-256을 출력한다. `--smoke` 결과는 성능 report가 아니라 process lifecycle과 필수
`RESULT` row 확인으로만 사용한다.

### PY-06 — Package와 clean consumer — PASS (Linux x86_64)

`setup.py`는 `ZLINK_CORE_PREFIX`를 요구하고 repository `core/build`를 implicit build input으로 사용하지
않는다. Candidate package script는 manifest에서 만든 candidate prefix로 다음을 모두 확인한다.

1. Core manifest의 revision, version, header/spec/source hash, runtime hash, SONAME, symbol inventory,
   layout과 freshness를 다시 확인한다.
2. candidate prefix로 extension과 wheel을 build한다.
3. wheel에 `py.typed`와 현재 Linux x86_64 runtime 하나만 있는지 확인하고, 이전 SONAME·`libzlink_c`·다른
   platform payload·source path가 있으면 실패한다.
4. 새 virtual environment에 `--no-deps`로 설치하고 repository 밖 cwd에서 `PYTHONPATH`, `LD_LIBRARY_PATH`,
   `ZLINK_LIBRARY_PATH`를 제거한다.
5. `zlink.version()`과 실제 Pair message roundtrip을 실행하고 `/proc/self/maps`에서 venv의 wheel payload가
   load된 것을 확인한다.
6. 같은 clean environment에서 `run_samples.py --installed`를 실행해 7개 process sample을 확인한다.
7. source manifest를 package build 뒤 다시 생성해 source drift를 거부한다.

### PY-07 — 정식 문서 — PASS (현재 구현 기준)

한국어·영문 Python spec과 한국어 guide는 raw Core 11 public surface, ownership, no-data, error, Python 3.9
type policy와 현재 `11.2.0` candidate를 설명한다. 구현에 없는 Framework 기능과 이전 Core runtime을 지원
목록으로 제공하지 않는다. `single_part()` draft는 승인 전이므로 정식 문서는 현재 accessor 이름을 유지한다.

## 4. Platform 검증

이번 candidate에서 확인한 target과 미확인 target을 구분한다.

| Target | 상태 | 근거 |
|--------|------|------|
| Linux x86_64 | `PASS` | candidate wheel, clean consumer, 7 samples, Pair roundtrip, load map |
| Linux aarch64 | `NOT RUN` | 같은 Core candidate runtime과 native consumer evidence 없음 |
| macOS x86_64/aarch64 | `NOT RUN` | platform runtime과 loader evidence 없음 |
| Windows x86/x86_64/aarch64 | `NOT RUN` | platform build와 dependency load evidence 없음 |

Linux x86_64 결과를 다른 target의 완료 근거로 승격하지 않는다. 다른 target을 지원 목록에 넣으려면 같은
Core candidate identity로 payload를 build하고 package·native consumer·loader evidence를 추가해야 한다.

## 5. 검증 명령과 ledger

실행 시점에는 다음 명령의 종료 코드와 산출물 hash를
[`python progress log`](log/python/2026-08-03-core11-progress.ko.md)에 기록한다.

```bash
# Core candidate manifest
scripts/local-package/bindings-candidate/create-manifest.sh \
  .artifacts/wsl/bindings-candidate/core-11.2.0.env

# Candidate wheel, clean consumer와 clean-wheel sample process
scripts/local-package/bindings-candidate/build-wsl.sh \
  --language python \
  --manifest .artifacts/wsl/bindings-candidate/core-11.2.0.env \
  --package-version 11.2.0

# Source tests
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  PYTHONPATH=bindings/python/src PYTHONDONTWRITEBYTECODE=1 \
  pytest -q bindings/python/tests

# Single perf smoke
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  LD_LIBRARY_PATH="$PWD/bindings/python/src/zlink/native/linux-x86_64" \
  PYTHONPATH=bindings/python/src \
  bindings/python/perf/run_benchmarks.sh --smoke --pattern PAIR \
  --duration 1 --msg-sizes 64 --transports inproc --runs 1

# Multi perf smoke
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  LD_LIBRARY_PATH="$PWD/bindings/python/src/zlink/native/linux-x86_64" \
  PYTHONPATH=bindings/python/src \
  bindings/python/perf/run_benchmarks_multi.sh --smoke \
  --pattern DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp \
  --runs 1 --clients 1
```

| Gate | 현재 상태 | evidence 또는 남은 조건 |
|------|----------|------------------------|
| Local Core candidate 입력 | `PASS` | `core-11.2.0.env`와 candidate-input의 동일 revision/hash |
| 공통 승인 candidate와의 identity | `BLOCKED` | 현재 11.2.0에 대응하는 독립 V11-R2·V11-M3 evidence 확인 필요 |
| Python source manifest | `PASS` | `python-source-manifest-11.2.0.json`, aggregate와 direct input hash |
| 승인 prefix native build | `PASS` | candidate build가 `ZLINK_CORE_PREFIX`로 wheel build |
| Raw FFI·symbol·layout | `PASS` | source tests, candidate manifest symbol/layout hash |
| Public API와 Framework surface 부재 | `PASS` | export/guard test와 source scan |
| Contract·unit test | `PASS` | `pytest`: 51 passed |
| `single_part` naming draft | `PARTIAL` | 현재 이름 유지; draft 승인 전 변경하지 않음 |
| Python 3.9 runtime와 최고 version | `PARTIAL` | Pyright 3.9 target PASS, CPython 3.12만 실행 |
| `pyright`·`py.typed` | `PASS` | public contracts 대상 pyright 0 errors, wheel file check |
| Hot-path inventory·optimization guard | `PASS` | `unclassified=0`, guard test PASS |
| Raw sample process | `PASS` | clean wheel `--installed`, 7/7 |
| Perf smoke | `PASS` | single/multi RESULT와 runtime hash 출력 |
| Wheel provenance·clean consumer | `PASS` | wheel SHA, payload SHA, SONAME/symbol, roundtrip, load map |
| Linux x86_64 platform | `PASS` | candidate wheel과 clean consumer |
| Other platforms | `NOT RUN` | target별 Core 11 payload와 consumer evidence 필요 |
| POSD·DDD Codex self-review | `PASS` | 검토 log와 cost inventory; independent review 아님 |
| 독립 frontier review | `BLOCKED` | 같은 최종 manifest와 fresh evidence를 읽은 reviewer 필요 |
| 정식 spec·guide | `PASS` | 현재 구현과 일치하도록 갱신 |

## 6. 완료 조건과 재개 순서

Python 작업을 전체 완료로 올리려면 local implementation evidence에 더해 공통 candidate identity, Python 3.9
clean consumer, 지원 platform 범위와 독립 review를 결정해야 한다. 현재는 다음 순서로 재개한다.

1. Core source와 package evidence가 같은 `11.2.0` candidate인지 공통 gate 담당자가 확인한다. 이전 `11.1.0`
   evidence는 재사용하지 않는다.
2. Python 3.9 interpreter 또는 CI job에서 clean wheel import, raw roundtrip, sample과 public type check를
   실행한다.
3. release 지원 target을 Linux x86_64로 한정할지 결정하거나, 추가 target의 candidate payload와 consumer
   evidence를 만든다.
4. 독립 reviewer가 최종 Python source manifest, 전체 diff, POSD·DDD cost inventory와 fresh test/package
   evidence를 read-only로 확인한다. 미해결 `Critical`·`High`·`Medium` finding이 없을 때만 `CLEAN`으로
   갱신한다.
5. 위 조건을 충족한 뒤에만 final ledger와 release-facing 문서의 완료 상태를 갱신한다.
