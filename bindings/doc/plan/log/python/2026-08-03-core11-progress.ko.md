# Python binding Core 11 검증 log

작성일: 2026-08-03

이 log는 Python raw Core 11 작업에서 실행한 local candidate와 package evidence를 기록한다. 공통
`V11-R2`·`V11-M3-CORE-PKG` 승인 evidence가 현재 Core `11.2.0` candidate를 가리킨다는 뜻은 아니다.
이전 `11.1.0` evidence를 현재 candidate의 승인으로 사용하지 않았다.

## 현재 판정

Linux x86_64 local implementation gate는 통과했다. CPython 3.9 Docker와 host CPython 3.12에서 같은
candidate 절차를 각각 통과했다. 전체 상태는 `PARTIAL / NOT CLEAN`이며, 현재 Core candidate와 공통
V11 승인 evidence의 identity 확인 및 독립 frontier reviewer의 최종 `CLEAN` 판정이 남아 있다.

## Candidate identity

```text
sourceRevision: 각 `candidate-input.env`의 `CORE_REVISION`
coreManifest: .artifacts/wsl/bindings-candidate/core-11.2.0.env
coreManifestSha256: 각 `candidate-input.env`의 `CANDIDATE_MANIFEST_SHA256`
coreVersion: 11.2.0
coreRuntime: core/build/lib/libzlink.so.11.2.0
coreRuntimeSha256: 각 `candidate-input.env`의 `CORE_RUNTIME_SHA256`
coreSoname: libzlink.so.11
coreSymbolSha256: ac7b04ce8f3a8338b82328ca03d6e93892f56ae57bb78569f9901ba5f65d5823
coreSourceSha256: 9888dd12f90930fb88a9b57b632f06bf44b3c05c6229246ad4cd62d8c21de1ce
coreHeaderSha256: f8d51ae49c3c3bb7d2ea54d1d6f067af47de37922dc93ef4e2cc8a624345a5a9
coreSpecSha256: f89f006c105048acaf5bdfcb2ce252995bc72ded9b0a8e7354813a150dfc43b1
```

현재 Core worktree에서 공통 review를 요청할 후보도 별도로 만들었다. 이는 package 승인 evidence가
아니며, 현재 변경 경로를 고정해 reviewer가 동일한 입력을 확인하기 위한 draft다.

```text
coreLedgerCandidate: .artifacts/v11/evidence/V11-M3-CORE-VERIFY/candidate-current-20260803.json
coreLedgerCandidateBaseRevision: candidate JSON의 `baseRevision`
coreLedgerCandidateManifestSha256: candidate JSON 파일의 SHA-256
coreLedgerCandidateAggregateSha256: candidate JSON의 `aggregateSha256`
coreLedgerCandidatePathCount: candidate JSON의 `pathCount`
```

기존 `V11-R2` review evidence
(`.artifacts/v11/evidence/V11-R2/core-candidate-reply-match-completion-hwm-review-20260801.json`,
SHA-256 `171a9cc8f7203500de08050dcb74ecd36b4c9ce55a75a14ce1bee283705c9e04`)는 candidate SHA-256
`d318525a4cf8496b6bef5d900c9a88330ea6d7e10ed4120ac0fd9f19d23f6765`만 승인한다. 이 evidence를 현재
candidate에 입력해 `verify-candidate.mjs`를 실행한 결과는 의도대로 `review evidence does not approve
the supplied candidate manifest SHA-256`로 실패했다. 따라서 현재 candidate에 대한 독립 `V11-R2` review와
그 결과를 사용하는 `V11-M3-CORE-PKG` evidence가 아직 필요하다.

## Python source and wheel evidence

```text
sourceManifest: `.artifacts/wsl/bindings-candidate/python-source-manifest-11.2.0.json` 및 각 output root의 동일 manifest
sourceManifestSha256: 각 `candidate-input.env`의 `PYTHON_SOURCE_MANIFEST_SHA256`
sourceAggregateSha256: 각 `candidate-input.env`의 `PYTHON_SOURCE_AGGREGATE_SHA256`
candidateInput: `.artifacts/wsl/bindings-candidate/python39/python/candidate-input.env` and the corresponding
`python312/python/candidate-input.env`
wheel: `python39/python/wheels/zlink-11.2.0-cp39-cp39-linux_x86_64.whl` and
`python312/python/wheels/zlink-11.2.0-cp312-cp312-linux_x86_64.whl`
wheelSha256: 각 output root의 `python/SHA256SUMS` wheel entry
packagedNativePayloadSha256: 각 `candidate-input.env`의 `PACKAGED_NATIVE_PAYLOAD_SHA256`
```

`candidate-input.env`의 Core manifest SHA, runtime SHA, source manifest SHA와 aggregate SHA는 위 값과
일치한다. Wheel에는 `py.typed`와 `linux-x86_64/libzlink.so.11.2.0`만 포함되며 `libzlink_c`, 이전 SONAME,
cross-platform payload와 source path는 포함되지 않는다.

## 실행 결과

### Source test와 static check

```bash
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  PYTHONPATH=bindings/python/src PYTHONDONTWRITEBYTECODE=1 \
  pytest -q bindings/python/tests
```

결과: source extension을 선택한 interpreter로 먼저 build한 뒤 `63 passed`.

```bash
PYTHONPATH=bindings/python/src python3 -m py_compile \
  $(rg --files bindings/python/src/zlink -g '*.py')
```

결과: 종료 코드 `0`.

`npx --yes pyright@1.1.411 --project bindings/python/pyrightconfig.json` 결과는 `0 errors, 0 warnings,
0 informations`이다. 설정 target은 Python `3.9`이고 대상은 public `src/zlink/contracts` tree다.

### Candidate package와 clean consumer

```bash
scripts/local-package/bindings-candidate/create-manifest.sh \
  .artifacts/wsl/bindings-candidate/core-11.2.0.env
scripts/local-package/bindings-candidate/build-wsl.sh \
  --language python \
  --manifest .artifacts/wsl/bindings-candidate/core-11.2.0.env \
  --package-version 11.2.0 \
  --python-executable python3.12

# CPython 3.9 Docker에서도 같은 command에 --python-executable python을 지정한다.
```

결과: 각 output root의 `candidate-input.env`가 가리키는 동일 Core candidate를 기준으로 CPython 3.9
Docker와 host CPython 3.12 모두 종료 코드 `0`. 각 source test는 `63 passed`, clean wheel
consumer는 Pair roundtrip과 wheel payload load-map 확인을 통과했다.
Builder는 같은 Core prefix와 source manifest를 사용해
각 interpreter용 wheel을 만들고, 새 venv에서 source checkout과 `core/build` fallback 없이
`zlink.version()`과 Pair message roundtrip을 실행했다. `/proc/self/maps`는 각 venv의 wheel payload를
가리켰다. 같은 venv에서 `run_samples.py --installed`를 실행한 결과도 각각 `7/7`이다.

Source runner 결과는 다음 7개 process가 모두 통과했다.

```text
dealer-router/request-reply/callback
pair/recv
dealer-router/recv
pubsub/recv
stream/recv
stream/packet-callback
monitor/recv
```

### Perf smoke

Single runner:

```bash
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  LD_LIBRARY_PATH="$PWD/bindings/python/src/zlink/native/linux-x86_64" \
  PYTHONPATH=bindings/python/src \
  bindings/python/perf/run_benchmarks.sh --smoke --pattern PAIR \
  --duration 1 --msg-sizes 64 --transports inproc --runs 1
```

결과: `status=complete`, `actual_result_lines=5`, runtime SHA는
`aff90818cc40df2ebeeb375489e147f7e23791bda28b0dac85bdc9462f59236e`이다.

Multi runner:

```bash
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  LD_LIBRARY_PATH="$PWD/bindings/python/src/zlink/native/linux-x86_64" \
  PYTHONPATH=bindings/python/src \
  bindings/python/perf/run_benchmarks_multi.sh --smoke \
  --pattern DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp \
  --runs 1 --clients 1
```

결과: `status=complete`, `success=1`, `fail=0`, `actual_result_lines=5`, runtime SHA는
`aff90818cc40df2ebeeb375489e147f7e23791bda28b0dac85bdc9462f59236e`이다.
두 smoke 실행은 공식 성능 report를 만들지 않았다.

## POSD·DDD와 남은 조건

`2026-08-03-posd-ddd-review.ko.md`에서 Context, Message/Received, Socket, callback dispatcher와
package adapter를 lifecycle·ownership owner로 분리했다. 초기 독립 review가 확인한 ChannelName,
close retry, input error, enum mapping과 dead example 문제를 owning layer에서 수정했다. Cost inventory는
allocation, copy, lock, GIL과 no-cost를 모두 분류했고 `unclassified=0`이다. 수정 후 source test는
`63 passed`이며, 같은 최종 manifest를 읽은 독립 re-review는 아직 필요하다.

이는 Codex self-review이므로 다음 조건이 남는다.

1. 공통 담당자가 현재 `11.2.0` candidate와 독립 V11-R2·V11-M3-CORE-PKG evidence의 identity를 확인한다.
2. 독립 frontier reviewer가 같은 source manifest와 fresh evidence를 읽고 최종 `CLEAN`을 판정한다.

Linux aarch64, macOS와 Windows는 현재 release target이 아니므로 별도 candidate와 native consumer
검증을 완료하기 전에는 지원 범위에 넣지 않는다.
