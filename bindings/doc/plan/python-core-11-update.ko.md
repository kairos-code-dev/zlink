# Python binding Core 11 최신화 실행 계획

> 대상 독자는 Python binding의 source, wheel과 platform payload를 갱신하는 담당자와 reviewer다. 이 문서는
> “승인된 Core candidate를 받아 Python 작업만 독립적으로 완료하려면 무엇을 바꾸고 어떤 증거를 남겨야
> 하는가?”에 답한다.
>
> 이 문서는 실행 계획이며 현재 Python public contract가 아니다. 구현 전 목표나 검토 중인 이름은 정식
> bindings spec에 먼저 반영하지 않는다. 구현과 contract test가 통과한 뒤 승인된 결과만 정식 spec과 guide에
> 반영한다.

## 1. 시작 조건과 현재 상태

[공통 계획](python-go-rust-core-11-update.ko.md)의 공통 시작 gate인 PGR-COMMON-01, PGR-COMMON-02와
PGR-COMMON-04가 통과하면 Go와 Rust의 진행 상태와 관계없이 이 작업을 시작할 수 있다. 시작 log에는 Core
candidate manifest, V11-R2 review, V11-M3-CORE-PKG evidence, install prefix와 raw symbol allowlist hash를
기록한다.

현재 기준선은 [2026-08-03 조사 log](log/common/2026-08-03-baseline.ko.md)다. 조사 당시 `core/build` runtime은
`11.1.0`을 반환했지만 `core/include/zlink/socket/api.h`보다 오래되어 candidate 입력으로 사용할 수 없었다.
따라서 Python 작업은 시작 gate를 통과하지 않은 상태다. Core runtime을 다시 만든 뒤 candidate manifest와
review·package evidence가 같은 입력을 가리키는지 먼저 확인한다.

현재 source에서 확인한 Python 상태는 다음과 같다.

| 확인 항목 | 현재 사실 | 판정 |
|----------|----------|------|
| Package version | `bindings/python/pyproject.toml`이 `9.0.4`를 선언함 | `PENDING` |
| Python version | `requires-python = ">=3.9"`이지만 source에 `T \| None` 표현이 남아 있음 | `PENDING` |
| Native build | `setup.py`가 repository의 `core/include`와 `core/build/lib`를 직접 사용함 | `PENDING` |
| Native loader | `_native_loader.py`가 bundled library 뒤에 repository `core/build`를 개발용 후보로 탐색함 | `PENDING` |
| Service surface | `zlink` root, `contracts/service`, `_runtime/service`와 `ffi.py`에 Spot·Actor surface가 있음 | `PENDING` |
| Platform payload | Linux payload가 Core `10.6.0` 또는 major 9 SONAME이고 `libzlink_c`도 포함함 | `PENDING` |
| Type package | package root에 `py.typed`가 없음 | `PENDING` |

이 표의 `PENDING`은 source를 아직 바꾸지 않았다는 뜻이고, 현재 실패가 확인된 공통 Core 입력은
`BLOCKED`로 기록한다. 따라서 상태는 **구현 미착수**이며, 현재 source test·sample·perf 결과는 Core 11
package evidence로 승격하지 않는다.

## 2. 목표와 범위

Python `11.1.0` wheel은 승인된 Core 11 raw C API만 투영한다. Context, message, raw socket, monitor, poller,
timer와 utility를 Python 관례에 맞게 제공하고, service API와 이전 Core runtime을 포함하지 않는다.

이번 작업에서 책임을 나누는 기준은 다음과 같다.

| Python surface | Core 11 binding의 처리 |
|----------------|-----------------------|
| Context, Message, RoutingId, raw socket, STREAM, monitor, poller, timer, utility | public API와 runtime을 유지·정렬 |
| MeshNode, Spot, Actor, service dispatch, transfer와 bound STREAM session | binding에서 제거하고 Framework 요구로 별도 추적 |
| native handle, callback trampoline, `ctypes` 선언, buffer marshalling | `_native`와 `_runtime` 내부에 숨김 |
| Core candidate, runtime provenance와 package payload | 공통 candidate evidence와 Python wheel evidence로 연결 |

다른 언어 binding에 같은 이름이 있어도 새 Python public API를 추가하는 근거로 사용하지 않는다. 계약 근거는
Core 11 raw header, 공통 bindings 정책과 Python 언어별 정식 spec이며, 공통 E2E나 기존 service 구현은 누락
조사 자료로만 사용한다.

다음 작업은 범위 밖이다.

- Framework service runtime 구현
- Python Framework package 또는 sample 구현
- 새 Core API 설계
- Registry 게시와 release tag 생성

Actor와 Spot sample에서 Framework로 옮길 요구가 있으면 별도 Framework plan에 기록한다. 이 binding 작업에서
Framework 구현까지 수행하지 않는다.

## 3. Package 입력과 version

`pyproject.toml`의 package version을 `11.1.0`으로 올린다. Core만 갱신한 최초 Python package는 patch 0에서
시작하고, 이후 Python binding만 수정하면 binding patch를 올린다. 정식 spec과 guide가 지원하는 Python 3.9를
유지한다. 따라서 Python 3.10에서 추가된 `T | None` annotation을 runtime source에 사용하지 않고
`typing.Optional[T]`처럼 Python 3.9에서 import 가능한 표현을 사용한다.

`requires-python`, CI의 최소·최고 Python version, wheel metadata와 type checker의 대상 version을 같은 정책으로
맞춘다. Python 3.9에서 해석되지 않는 annotation을 `from __future__ import annotations`만으로 숨기지 않는다.
공개 함수, `Protocol`, callback과 context manager가 사용하는 모든 인자·반환 type을 명시하고, `Any`는 native
userdata처럼 다른 방법으로 표현할 수 없는 경계에만 남긴다.

구현과 public contract 문서 갱신을 마치면 Python source, test, sample, perf, package metadata와 package
script를 공통 형식의 binding source manifest에 봉인한다. 이 manifest의 direct input에는 Core candidate
provenance와 공통·Python 정식 spec의 path 및 SHA-256을 기록한다. 이후 test와 wheel은 이 manifest로
materialize한 격리 snapshot에서 만든다. Wheel, clean consumer와 독립 review evidence는 같은 manifest
file SHA-256과 aggregate SHA-256을 기록한다.

현재 [`create-manifest.sh`](../../../scripts/local-package/bindings-candidate/create-manifest.sh)는 Core
candidate 입력을 만드는 script이므로 Python source manifest와 혼동하지 않는다. Python source를 봉인하는
schema, materializer와 변경 감지 검사를 candidate tooling에 추가하고, Core candidate manifest와 Python
binding source manifest의 SHA-256을 별도 field로 기록한다.

현재 `setup.py`는 두 native extension을 만들 때 `core/include`와 `core/build/lib`를 자동으로 사용한다. 이를
공통 gate가 승인한 Core install prefix를 명시적으로 입력받는 방식으로 바꾼다. 개발용 source override가
필요하면 별도 option으로 분리하되 package build에서는 거부한다. `_native_loader.py`의 repository `core/build`
fallback도 package consumer 경로에서는 선택되지 않게 하고, 개발 실행에서만 명시적으로 허용한다.

Wheel에는 지원 대상 platform마다 Core runtime을 하나만 포함한다. 같은 platform의 `linux-x64`와
`linux-x86_64`처럼 중복 directory를 함께 배포하지 않으며, 이전 major SONAME, `libzlink_c`와 source tree
rpath가 남으면 package 검증을 실패 처리한다. Inline type 정보를 wheel consumer에게 제공하기 위해 package
root에 `py.typed`를 포함하고 wheel contents에서 이를 확인한다.

## 4. 구현 작업

### PY-01 — Raw FFI inventory

- `src/zlink/_native/ffi.py`, `_zlink_native.c`, `_zlink_perf_native.c`의 선언과 호출을 Core 11 raw header
  allowlist와 대조한다. Python의 `ctypes` 선언과 compiled extension이 같은 raw 함수·struct·enum 의미를
  가리키는지 각각 확인한다.
- Header에 없는 service function, struct, enum과 callback 선언을 제거한다.
- Header의 raw 공개 함수가 FFI에서 누락되었는지 machine-readable snapshot으로 검사한다.
- Python package가 runtime symbol을 직접 추측하지 않고 공통 allowlist를 사용하게 한다. `getattr` 기반 동적
  symbol lookup이나 이름 prefix만 막는 검사를 허용하지 않는다.
- Native extension을 build할 때 승인 install prefix의 header와 library만 사용하고, repository `core/include`와
  `core/build/lib`가 include·link 경로에 남으면 실패한다.

### PY-02 — 공개 API와 runtime 정리

- `src/zlink/contracts/service/`와 `src/zlink/_runtime/service/`를 제거한다.
- Package root와 `contracts/__init__.py`에서 MeshNode, Spot, Actor와 service result export를 제거한다.
- `router_spot_support.py`, `stream_actor_support.py`처럼 raw socket에 service 책임을 섞은 코드를 제거한다.
- Context, message, socket, eventing과 error API가 private native type을 공개하지 않는지 검사한다.
- Service 이름을 deprecated alias, compatibility module 또는 dynamic `__getattr__`로 유지하지 않는다.
- 모든 public 함수와 Protocol에 Python 3.9에서 import 가능한 parameter·return annotation을 제공한다.
- 현재 `contracts/messaging/received.py`와 runtime이 사용하는 `single_part_or_throw()`는 이름 초안의 승인
  결과를 반영해 변경한다. 빈 값, 한 part와 여러 part의 예외 및 ownership을 public contract test로 고정한다.
- Python에서 예외 발생을 뜻하는 `throw`를 public 이름에 사용하지 않는다. 현재 정식 spec의
  `single_part_or_throw()`는 [part 접근 이름 초안](../spec/draft/python-rust-single-part-naming.ko.md)에서
  `single_part()`로 바꾸는 안을 먼저 리뷰한다. 구현과 contract test가 통과하기 전에는 정식 spec을 변경하지
  않는다. 승인 전에는 현재 `single_part_or_throw()`를 임의로 삭제하거나 alias로 남기지 않는다.
- Python 예약어를 피하기 위한 `Message.from_(...)`와 `RoutingId.from_(...)`는 현재 정식 계약대로 유지한다.

### PY-03 — Error와 ownership

- Core 함수군별 error type, `code`와 `internal_errno`를 공통 bindings 오류 정책에 맞춘다.
- Core 작업 인자의 형식 오류는 해당 함수군의 `INVALID_ARGUMENT`로 변환한다.
- 독립 값 객체를 만들기 전의 Python 전용 형식 검증만 별도 validation exception으로 처리한다.
- Non-blocking no-data는 정상 반환으로 표현하고 실제 receive 실패만 `RecvError`로 전달한다. Native
  `NO_DATA` 이외의 오류를 빈 값이나 `False`로 숨기지 않는다.
- Caller-provided receive는 no-data를 `False`로 반환하고, monitor·timer처럼 값을 직접 반환하는 control-plane
  API는 no-data를 `None`으로 반환한다. 같은 API에서 두 표현을 섞지 않는다.
- Send 성공 뒤 message가 native로 이동하여 Python에서 다시 사용되지 않는지, receive 뒤 native message를
  Python wrapper가 소유하는지, copy·move와 close의 성공·실패 뒤 ownership을 contract test로 검증한다.
- Python reference count와 context manager가 native handle·callback보다 오래 유지되는지 확인한다. GIL을
  해제한 native wait 중에는 Python object를 읽거나 쓰지 않으며, callback 실행 직전에만 Python 실행 경계를
  복원한다.

### PY-04 — Hot path 설계 검토

- Message 생성, send, receive와 request completion에서 Python 객체와 native buffer가 만들어지는 위치를
  기록한다.
- 공개 계약이 snapshot copy를 요구하지 않는 경로에서 `bytes`, `list`, `tuple` 또는 임시 C buffer로 payload를
  반복 materialize하지 않는다.
- Blocking native wait 동안 Python 객체에 접근하지 않는 구간은 GIL이나 전역 lock으로 다른 socket 작업을
  불필요하게 막지 않게 한다. Callback에서 Python 객체를 다룰 때만 필요한 실행 경계를 복원한다.
- 반복 send·receive마다 새 worker, task, closure 또는 공용 lock을 만들지 않는다. Callback 등록이나 비동기
  완료가 요구하는 객체는 생성 시점과 호출 시점을 inventory에서 구분한다. 필요한 synchronization은 handle과
  callback lifetime을 보호하는 최소 범위로 제한한다.
- `tests/hot-path-cost-inventory.json`에서 비용 발생 source, 분류, 이유와 guard test를 연결한다.
- `python -m pytest -q tests/test_optimization_guard.py tests/test_boundary_ownership_contract.py`가 종료 코드
  0이고 inventory의 `unclassified`가 0건이어야 통과한다.
- 현재 `test_optimization_guard.py`가 확인하는 compiled extension의 `Py_BEGIN_ALLOW_THREADS`와 part 기반
  native bridge 경로를 유지하되, service 전용 검사는 raw-only target에 맞게 다시 작성한다. 성능 수치보다
  allocation·copy·lock의 owner와 제거 가능성을 먼저 기록한다.

### PY-05 — Sample과 perf smoke

- Pair, pub/sub, dealer/router request, STREAM receive·packet callback과 monitor sample을 유지한다.
- Spot, Actor와 service operation sample을 Python binding runner에서 제거한다.
- `samples/run_samples.py`의 canonical 목록에서 raw socket·STREAM·monitor sample만 유지한다. Runner는
  repository `src`를 import하거나 `core/build`을 고정하지 않고 clean wheel을 설치한 consumer에서 실행한다.
- Perf runner에서는 Pair, pub/sub, dealer/router와 raw STREAM scenario만 유지한다. 현재 single/multi runner의
  `SPOT`, Actor와 service helper·pattern을 제거하고, runner가 승인 wheel의 runtime path와 SHA-256을 출력하게
  바꾼다.
- 공식 report를 만들지 않는 `--smoke` mode를 runner에 추가하고 다음 명령을 실행한다.

```bash
# Single runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks.sh --smoke --pattern PAIR --duration 1 --msg-sizes 64 --transports inproc --runs 1

# Multi runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks_multi.sh --smoke --pattern MULTI_DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp --runs 1 --clients 1
```

- 두 명령은 공통 계획의 smoke 판정에 따라 ready, active, 필수 `RESULT` metric과 exit code만 확인한다. 결과
  수치는 공식 report나 성능 비교에 사용하지 않으며 실행 뒤 report 파일이 남지 않아야 한다.
- Sample은 public Python API만 사용하며 private FFI나 native symbol을 직접 호출하지 않는다.

### PY-06 — 구현 후 POSD·DDD 리팩터링과 Codex review

PY-01~05의 구현과 test가 통과하면 정식 문서와 wheel 작업을 시작하기 전에 다음 품질 gate를 수행한다.

- DDD 관점에서 Python wrapper, native handle, message buffer와 callback의 lifecycle·ownership·state transition
  owner를 정리한다. Python validation, binding error mapping과 Core error의 경계를 한 곳에서 일관되게 유지한다.
- POSD 관점에서 얕은 wrapper, 인자를 그대로 전달하는 helper, 실행 순서대로 나눈 module, 중복 FFI abstraction과
  private native 결정을 노출하는 public API를 찾는다. 비자명한 수정은 두 가지 이상 대안을 비교한 뒤 caller의
  복잡성과 변경 범위가 작은 안을 선택한다.
- Public Python API부터 C extension과 Core 호출까지 production hot path를 다시 추적한다. 계약에 필요하지 않은
  `bytes`·`list`·`tuple` materialization, payload copy, 임시 C buffer, reference-count churn, 넓은 GIL 구간과
  전역 lock 경합이 없어야 한다. 필요한 비용은 이유와 owner를 cost inventory에 남긴다.
- 제거한 service 계약에서 남은 export, dynamic fallback, wrapper, branch, helper, fixture, sample·perf scenario,
  import, dependency와 주석을 찾아 삭제한다. 실행되지 않는 호환 경로나 test만 참조하는 production 코드를
  남기지 않는다.
- 리팩터링 뒤 contract·unit test, type 검사, optimization guard, raw sample process와 perf smoke를 다시
  실행하고 Python binding source manifest를 새로 만든다.

구현자가 아닌 frontier Codex coding/review agent가 새 manifest와 전체 diff를 read-only로 검토한다. Contract와
architecture를 판단할 수 있는 model을 `high` 이상의 reasoning level로 사용하며, `contract`, `POSD`, `DDD`,
`performance-cost`, `dead-code`, `test/evidence` finding을 기록한다. 미해결 `Critical`, `High`, `Medium`
finding이 0건이고 `Low` finding이 모두 처리됐으며 같은 manifest의 필수 재검증이 통과해야 `CLEAN`으로
판정한다. `NOT CLEAN`이면 수정 후 새 manifest와 fresh test로 다시 review한다. `CLEAN` 전에는 PY-07 이후
작업으로 넘어가지 않는다.

### PY-07 — 정식 문서

구현과 contract test가 통과한 뒤 [Python 정식 spec](../spec/python/README.ko.md)의 한국어·영문 문서와
[Python guide](../guide/python/index.ko.md)를 실제 raw 공개 API에 맞춘다. `single_part()` 승인 결과와 Python 3.9 type policy를 정식 문서에
반영하고, 제거한 service surface와 이전 Core runtime을 문서의 지원 표에서 다시 제공하지 않는다. 구현 전
목표를 정식 spec에 먼저 기록하지 않는다.

## 5. Platform 검증

현재 loader 선택은 `src/zlink/_native/_native_loader.py`, payload는 `src/zlink/native/`에 있다. 다음 표는
지원 계약이 아니라 이 source에서 확인한 구현 기준선이다.

| Platform | 현재 loader 후보 | 현재 차이와 완료 조건 |
|----------|------------------|----------------------|
| Linux x86_64 | `linux-x86_64` | Core 10.6.0와 duplicate `linux-x64` payload를 Core 11 runtime 하나로 교체 |
| Linux aarch64 | `linux-aarch64` | major 9 SONAME payload를 Core 11 runtime으로 교체하고 native consumer 실행 |
| macOS x86_64/aarch64 | `darwin-*` | 현재 payload의 Core version·load·dependency를 platform별로 검증 |
| Windows x86/x86_64 | `PROCESSOR_ARCHITECTURE` 문자열로 선택 | x86 directory가 없고 dependency 선택 규칙이 고정되지 않아 지원 여부를 먼저 결정 |
| Windows aarch64 | 전용 payload가 있으나 loader 분기가 없음 | loader와 native consumer를 추가하기 전에는 release 대상에서 제외 |

PY-01에서 spec, guide, loader와 package payload를 대조해 지원 platform을 확정한다. 확정된 각 platform은 같은
Core candidate identity와 platform별 runtime provenance를 사용하고 native Python consumer를 실행한다. Linux
host에서 확인한 결과를 macOS·Windows의 실행 증거로 간주하지 않는다. platform artifact를 만들 수 없는 target은
지원 표에서 제외하고 별도 작업으로 남긴다.

## 6. Wheel과 clean consumer

### PY-08 — Wheel과 clean consumer

Candidate package script는
[`scripts/local-package/bindings-candidate/build-wsl.sh`](../../../scripts/local-package/bindings-candidate/build-wsl.sh)를
사용한다. 이 script는 승인 Core manifest의 revision, version, header·spec·source hash, runtime hash, SONAME,
export inventory와 freshness를 다시 확인한 뒤 Python package를 만든다. 예시는 다음과 같다.

```bash
scripts/local-package/bindings-candidate/build-wsl.sh \
  --language python \
  --manifest /absolute/path/to/core-candidate.env \
  --package-version 11.1.0
```

현재 script가 수행하는 source test, wheel build와 version-only clean import만으로는 완료 조건을 충족하지
않는다. 다음 검사를 script와 Python consumer evidence에 추가한다.

1. 승인 Core prefix로 extension과 wheel을 build한다.
2. Wheel contents에서 package metadata, Python module, `py.typed`와 지원 platform별 native runtime을 확인한다.
   같은 platform의 runtime 하나만 허용하며 이전 SONAME, `libzlink_c`, source path가 있으면 실패한다.
3. Wheel의 각 runtime SHA-256, SONAME, `zlink_*` exported-symbol inventory를 platform provenance와 비교한다.
4. 새 virtual environment에 `--no-deps`로 wheel을 설치한다. `pip` cache와 editable install을 사용하지 않는다.
5. Repository 밖 directory에서 `PYTHONPATH`, `LD_LIBRARY_PATH`와 `ZLINK_LIBRARY_PATH`를 제거하고 import한다.
6. `zlink.version()`과 platform 대응 loader 검사로 wheel 내부 runtime이 load되었는지 확인한다. Linux에서는
   `/proc/self/maps`가 venv 안의 package payload를 가리키는지도 확인한다.
7. Public raw socket API로 실제 message를 보내고 받는다. 소스 checkout의 `src/zlink`이나 private FFI를
   import해 성공한 결과는 clean consumer evidence로 인정하지 않는다.
8. Python 3.9와 CI가 지원하는 최고 version에서 별도 clean consumer를 실행하고, `py.typed`를 이용한 public
   API type check가 source checkout 없이 통과하는지 확인한다.

Source checkout의 extension, editable install과 전역 Core library가 선택되면 실패한다. Wheel, consumer,
runtime과 manifest의 SHA-256은 같은 candidate-input evidence에 함께 기록한다.

## 7. 검증 표

| Gate | 상태 | Evidence 또는 다음 기록 |
|------|------|----------------------|
| 공통 candidate 입력 확인 | `BLOCKED` | [2026-08-03 기준선 log](log/common/2026-08-03-baseline.ko.md): `core/build` runtime이 header보다 오래됨 |
| Python binding source manifest | `PENDING` | 최종 source·test·sample·perf·package manifest와 file/aggregate SHA-256 |
| 승인 prefix를 사용하는 native build | `PENDING` | `setup.py` build log와 include/library path 검사 |
| Raw FFI와 symbol allowlist | `PENDING` | `ffi.py`·C extension snapshot, Core allowlist hash, 누락·추가 결과 |
| Public API snapshot과 service 부재 | `PENDING` | `zlink`/`contracts` export snapshot, service path·alias·private import 검사 |
| `pytest` contract·unit test | `PENDING` | `./tests/run_tests.sh`와 분리 실행한 test 수·종료 코드 |
| `single_part` naming draft와 contract test | `PENDING` | [part 이름 초안](../spec/draft/python-rust-single-part-naming.ko.md) 승인 결과와 세 part case |
| Python 3.9와 CI 최고 version import·contract test | `PENDING` | 각 version의 clean consumer log와 종료 코드 |
| `pyright`, `py.typed`와 package metadata 검사 | `PENDING` | type checker 대상 version, wheel file list와 type check 결과 |
| Hot path cost inventory와 optimization guard | `PENDING` | `tests/hot-path-cost-inventory.json`, inventory SHA-256, `unclassified=0` |
| Perf runner smoke | `PENDING` | single·multi `RESULT` rows, runtime path/hash, exit code, 잔여 report 없음 |
| 구현 후 POSD·DDD·성능 비용·dead code Codex review | `PENDING` | 새 manifest, finding 처리 결과와 최종 `CLEAN` 판정 |
| Raw sample process runner | `PENDING` | clean wheel에서 실행한 sample 목록, process exit code와 summary |
| Wheel contents와 provenance | `PENDING` | wheel SHA-256, native payload·SONAME·symbol hash, candidate identity |
| Clean virtual environment consumer | `PENDING` | 외부 cwd의 import, version, 실제 message roundtrip과 load map |
| 지원 platform native consumer | `PENDING` | 지원 표의 각 target별 artifact·runtime load evidence |
| 한국어·영문 spec과 guide | `PENDING` | 구현·contract test 뒤 갱신한 문서와 link check |
| Package·통합 최종 review | `PENDING` | 동일 manifest를 가리키는 wheel·consumer evidence와 finding 상태 |

명령, 종료 코드, test 수, wheel SHA-256과 실패 원인은 `bindings/doc/plan/log/python/` 아래 날짜별 log에
기록한다. `BLOCKED` gate는 실패한 입력과 재개 명령을 함께 기록하며, 다른 gate의 `PENDING`을 임의로
`PASS`로 바꾸지 않는다.

## 8. 완료 조건

다음 조건을 모두 만족해야 Python 작업이 완료된다. 아래 조건은 계획 문서의 작성 완료가 아니라 binding
구현 작업의 완료 판정이다.

1. Wheel version은 `11.1.0`이고 승인된 Core candidate identity와 Python binding source manifest를 기록한다.
2. Raw FFI와 공개 API가 Core 11 allowlist에 정확히 맞고 service API·service header·deprecated alias가 없다.
3. `single_part()`와 Python 3.9 annotation 정책이 승인된 draft·contract test·정식 spec에 같은 의미로 반영된다.
4. 함수군별 error, `code`, `internal_errno`, no-data와 message·receive·close ownership이 contract test를
   통과한다.
5. Source test, static type 검사, hot path design review, perf smoke와 raw sample process가 통과한다.
6. 구현 후 POSD·DDD 리팩터링, 불필요한 allocation·copy·contention과 dead code 검토가 끝났고 Codex review가
   `CLEAN`이다.
7. Clean virtual environment가 wheel 내부 runtime으로 실제 message를 송수신하고, 다른 Core library를
   load하지 않는다.
8. Python 3.9와 CI가 지원하는 최고 version의 clean consumer가 wheel을 import하고 public API type 검사를
   통과하며 wheel에 `py.typed`가 있다.
9. 지원한다고 명시한 모든 platform에서 package contents와 runtime load가 검증되고, 지원하지 않는 target은
   spec·guide에 암시되지 않는다.
10. 정식 spec과 guide가 구현과 일치하며 구현 전 draft와 이전 Core service draft의 처리 결과가 기록된다.
11. 성능 수치 개선은 후속 Python 계획으로 분리되어 있으며 이번 완료 근거로 사용하지 않는다.
12. 미해결 `Critical`, `High`, `Medium` finding과 실행하지 않은 필수 gate가 남아 있지 않다.

## 9. 재개 순서

1. 최신 Core source로 `core/build` runtime을 다시 만들고 PGR-COMMON-01·02의 candidate와 raw symbol
   inventory를 생성한다. stale runtime이나 `core/build` 직접 참조를 Python package 입력으로 사용하지 않는다.
2. PGR-COMMON-04에서 V11-R2와 V11-M3-CORE-PKG가 같은 candidate manifest·install prefix를 가리키는지 확인한다.
3. `PY-01`과 `PY-02`로 raw FFI와 public projection을 정리하고, `PY-03`의 error·ownership contract를
   먼저 고정한다. `single_part` 이름 변경은 draft review 뒤에만 수행한다.
4. `PY-04`의 cost inventory와 guard를 만든 뒤 source test, type check, raw sample process와 perf smoke를
   승인 package 경로에서 실행한다.
5. PY-06에서 POSD·DDD 리팩터링과 성능 비용·dead code 검토를 수행한다. Codex review가 `CLEAN`일 때만 정식
   문서와 package 단계로 진행한다.
6. 최종 Python source manifest를 만들고 candidate package script로 wheel, clean consumer와 platform evidence를
   생성한다. Core candidate, 정식 spec, source 또는 package script가 바뀌면 manifest와 필수 gate를 다시
   만든다.
7. 독립 reviewer가 동일한 manifest와 wheel·consumer evidence를 대상으로 확인한 뒤에만 검증 표를 완료로
   바꾼다.
