# Python binding Core 11 최신화 실행 계획

> 대상 독자는 Python binding의 source, wheel과 platform payload를 갱신하는 담당자와 reviewer다. 이 문서는
> “승인된 Core candidate를 받아 Python 작업만 독립적으로 완료하려면 무엇을 바꾸고 어떤 증거를 남겨야
> 하는가?”에 답한다.

## 1. 시작 조건과 현재 상태

[공통 계획](python-go-rust-core-11-update.ko.md)의 공통 시작 gate인 PGR-COMMON-01, PGR-COMMON-02와
PGR-COMMON-04가 통과하면 Go와 Rust의 진행 상태와 관계없이 이 작업을 시작할 수 있다. 시작 log에는 Core
candidate manifest, V11-R2 review, V11-M3-CORE-PKG evidence, install prefix와 raw symbol allowlist hash를
기록한다.

현재 Python package version은 `9.0.4`다. Linux x86_64 payload는 Core `10.6.0`, Linux aarch64 payload는
major 9 SONAME을 사용한다. `zlink` package root와 `contracts/service`, `_runtime/service`에는 SpotNode, Spot과
Actor 공개 API 및 구현이 있다.

상태는 **구현 미착수**다. 현재 source test나 sample 결과를 Core 11 package 증거로 사용하지 않는다.

## 2. 목표와 범위

Python `11.1.0` wheel은 승인된 Core 11 raw C API만 투영한다. Context, message, raw socket, monitor, poller,
timer와 utility를 Python 관례에 맞게 제공하고, service API와 이전 Core runtime을 포함하지 않는다.

다음 작업은 범위 밖이다.

- Framework service runtime 구현
- Python Framework package 또는 sample 구현
- 새 Core API 설계
- Registry 게시와 release tag 생성

Actor와 Spot sample에서 Framework로 옮길 요구가 있으면 별도 Framework plan에 기록한다. 이 binding 작업에서
Framework 구현까지 수행하지 않는다.

## 3. Package 입력과 version

`pyproject.toml`의 package version을 `11.1.0`으로 올린다. Core만 갱신한 최초 Python package는 patch 0에서
시작하고, 이후 Python binding만 수정하면 binding patch를 올린다.

구현을 마치면 Python source, test, sample, package metadata와 이 작업이 사용하는 package script를 공통
형식의 binding source manifest에 봉인한다. 이후 test와 wheel은 이 manifest로 materialize한 격리 snapshot에서
만든다. Wheel, clean consumer와 독립 review evidence는 같은 manifest file SHA-256과 aggregate SHA-256을
기록한다.

`setup.py`가 repository `core/include`와 `core/build/lib`를 자동 선택하지 않게 한다. Build는 공통 gate가
승인한 Core install prefix를 명시적으로 입력받는다. 개발용 source override가 필요하면 option으로 분리하고
package build에서는 거부한다.

Wheel에는 대상 platform의 Core runtime을 하나만 포함한다. 이전 major SONAME, `libzlink_c`와 source tree
rpath가 남으면 package 검증을 실패 처리한다.

## 4. 구현 작업

### PY-01 — Raw FFI inventory

- `src/zlink/_native/ffi.py`와 C extension 선언을 Core 11 raw header allowlist와 대조한다.
- Header에 없는 service function, struct, enum과 callback 선언을 제거한다.
- Header의 raw 공개 함수가 FFI에서 누락되었는지 machine-readable snapshot으로 검사한다.
- Python package가 runtime symbol을 직접 추측하지 않고 공통 allowlist를 사용하게 한다.

### PY-02 — 공개 API와 runtime 정리

- `src/zlink/contracts/service/`와 `src/zlink/_runtime/service/`를 제거한다.
- Package root와 `contracts/__init__.py`에서 MeshNode, Spot, Actor와 service result export를 제거한다.
- `router_spot_support.py`, `stream_actor_support.py`처럼 raw socket에 service 책임을 섞은 코드를 제거한다.
- Context, message, socket, eventing과 error API가 private native type을 공개하지 않는지 검사한다.
- Service 이름을 deprecated alias, compatibility module 또는 dynamic `__getattr__`로 유지하지 않는다.

### PY-03 — Error와 ownership

- Core 함수군별 error type, `code`와 `internal_errno`를 공통 bindings 오류 정책에 맞춘다.
- Core 작업 인자의 형식 오류는 해당 함수군의 `INVALID_ARGUMENT`로 변환한다.
- 독립 값 객체를 만들기 전의 Python 전용 형식 검증만 별도 validation exception으로 처리한다.
- Non-blocking no-data는 정상 반환으로 표현하고 실제 receive 실패만 `RecvError`로 전달한다.
- Send, receive, message copy·move와 close의 성공·실패 뒤 ownership을 contract test로 검증한다.

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

### PY-05 — Sample과 perf smoke

- Pair, pub/sub, dealer/router request, STREAM receive·packet callback과 monitor sample을 유지한다.
- Spot, Actor와 service operation sample을 Python binding runner에서 제거한다.
- Perf runner에서는 raw socket scenario만 유지한다. Runner가 승인 wheel의 runtime path와 SHA-256을 출력하고
  repository `core/build`을 선택하지 않게 입력 경로를 바꾼다.
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

### PY-06 — 정식 문서

구현과 contract test가 통과한 뒤 Python 정식 spec의 한국어·영문 문서와 guide를 실제 raw 공개 API에 맞춘다.
구현 전 목표를 정식 spec에 먼저 기록하지 않는다.

## 5. Platform 검증

현재 loader 선택은 `src/zlink/_native/_native_loader.py`, payload는 `src/zlink/native/`에 있다. 다음 표는
지원 계약이 아니라 이 source에서 확인한 구현 기준선이다.

| Platform | 현재 loader 후보 | 확인할 차이 |
|----------|------------------|-------------|
| Linux x86_64 | `linux-x86_64` | Core 10.6.0 교체 필요 |
| Linux aarch64 | `linux-aarch64` | Major 9 payload 교체 필요 |
| macOS x86_64/aarch64 | `darwin-*` | Runtime version과 load 검증 필요 |
| Windows x86/x86_64 | architecture 문자열로 선택 | x86 payload directory 존재 여부 확인 필요 |
| Windows aarch64 | 전용 payload는 있으나 loader가 x86_64로 선택할 수 있음 | 별도 loader 지원 없이는 release 대상 아님 |

PY-01에서 spec, guide, loader와 package payload를 대조해 지원 platform을 확정한다. 확정된 각 platform은 같은
Core candidate identity와 platform별 runtime provenance를 사용하고 native Python consumer를 실행한다.
지원이 구현되지 않은 platform은 release 완료 표에서 제외하고 별도 작업으로 남긴다.

## 6. Wheel과 clean consumer

Candidate package script는 다음 순서로 검증한다.

1. 승인 Core prefix로 extension과 wheel을 build한다.
2. Wheel contents에서 예상 platform runtime 하나, package metadata와 Python module을 확인한다.
3. Runtime SHA-256과 public symbol inventory를 platform provenance와 비교한다.
4. 새 virtual environment에 `--no-deps`로 wheel을 설치한다.
5. Repository 밖 directory에서 `PYTHONPATH`, `LD_LIBRARY_PATH`와 `ZLINK_LIBRARY_PATH`를 제거하고 import한다.
6. `zlink.version()`과 platform 대응 loader 검사로 wheel 내부 runtime이 load되었는지 확인한다.
7. Public raw socket smoke를 실행해 실제 message 송수신을 확인한다.

Source checkout의 extension, editable install과 전역 Core library가 선택되면 실패한다.

## 7. 검증 표

| Gate | 상태 | Evidence |
|------|------|----------|
| 공통 candidate 입력 확인 | `PENDING` | — |
| Python binding source manifest | `PENDING` | — |
| Raw FFI와 symbol allowlist | `PENDING` | — |
| Public API snapshot과 service 부재 | `PENDING` | — |
| `pytest` contract·unit test | `PENDING` | — |
| `pyright`와 package metadata 검사 | `PENDING` | — |
| Hot path cost inventory와 optimization guard | `PENDING` | — |
| Perf runner smoke | `PENDING` | — |
| Raw sample process runner | `PENDING` | — |
| Wheel contents와 provenance | `PENDING` | — |
| Clean virtual environment consumer | `PENDING` | — |
| 지원 platform native consumer | `PENDING` | — |
| 한국어·영문 spec과 guide | `PENDING` | — |
| 독립 review | `PENDING` | — |

명령, 종료 코드, test 수, wheel SHA-256과 실패 원인은 `bindings/doc/plan/log/python/` 아래 날짜별 log에
기록한다.

## 8. 완료 조건

다음 조건을 모두 만족해야 Python 작업이 완료된다.

1. Wheel version은 `11.1.0`이고 승인된 Core candidate identity와 Python binding source manifest를 기록한다.
2. Raw FFI와 공개 API가 Core 11 allowlist에 맞고 service API가 없다.
3. Source test, static type 검사, hot path design review, perf smoke와 raw sample process가 통과한다.
4. Clean virtual environment가 wheel 내부 runtime으로 실제 message를 송수신한다.
5. 지원한다고 명시한 모든 platform에서 package contents와 runtime load가 검증된다.
6. 정식 spec과 guide가 구현과 일치한다.
7. 성능 수치 개선은 후속 Python 계획으로 분리되어 있으며 이번 완료 근거로 사용하지 않는다.
8. Critical, high, medium finding과 실행하지 않은 필수 gate가 남아 있지 않다.
