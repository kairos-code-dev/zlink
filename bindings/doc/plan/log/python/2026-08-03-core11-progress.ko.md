# Python binding Core 11 검증 log

작성일: 2026-08-03

이 log는 Python raw Core 11 작업에서 실행한 local candidate와 package evidence를 기록한다. 공통
`V11-R2`·`V11-M3-CORE-PKG` 승인 evidence가 현재 Core `11.2.0` candidate를 가리킨다는 뜻은 아니다.
이전 `11.1.0` evidence를 현재 candidate의 승인으로 사용하지 않았다.

## 현재 판정

Linux x86_64 local implementation gate는 통과했다. 전체 상태는 `PARTIAL / NOT CLEAN`이다. Python 3.9
interpreter와 Linux x86_64 이외의 platform consumer는 실행하지 않았고, 독립 frontier reviewer의 최종
`CLEAN` 판정도 없다.

## Candidate identity

```text
sourceRevision: cb5c4ffb0e41cfaf8601f744c3fdcd3f53833e98
coreManifest: .artifacts/wsl/bindings-candidate/core-11.2.0.env
coreManifestSha256: a4672da3ba54c70d6da42caa6671227d474d58351ddb354dd5c241f1d7aa421b
coreVersion: 11.2.0
coreRuntime: core/build/lib/libzlink.so.11.2.0
coreRuntimeSha256: 1c34ae4e2e631e7e04cdf50c1ce6c231d10c501a0fe41007d0eec8530bda24d8
coreSoname: libzlink.so.11
coreSymbolSha256: ac7b04ce8f3a8338b82328ca03d6e93892f56ae57bb78569f9901ba5f65d5823
coreSourceSha256: 9888dd12f90930fb88a9b57b632f06bf44b3c05c6229246ad4cd62d8c21de1ce
coreHeaderSha256: f8d51ae49c3c3bb7d2ea54d1d6f067af47de37922dc93ef4e2cc8a624345a5a9
coreSpecSha256: f89f006c105048acaf5bdfcb2ce252995bc72ded9b0a8e7354813a150dfc43b1
```

## Python source and wheel evidence

```text
sourceManifest: .artifacts/wsl/bindings-candidate/python-source-manifest-11.2.0.json
sourceManifestSha256: 65d1f726180914e588c03810b9ea291d25e7244851050a530ac6212db47bf008
sourceAggregateSha256: eefb3752e00405a2bc2b296547a1a746e8e1fe002a9a3ae39db2e30787af300c
candidateInput: .artifacts/wsl/bindings-candidate/python/candidate-input.env
wheel: .artifacts/wsl/bindings-candidate/python/wheels/zlink-11.2.0-cp312-cp312-linux_x86_64.whl
wheelSha256: d246f7222ef001943ff844cc9f1fb8342157e7abc945f7e069a3191185b81204
packagedNativePayloadSha256: 1c34ae4e2e631e7e04cdf50c1ce6c231d10c501a0fe41007d0eec8530bda24d8
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

결과: `51 passed`.

```bash
PYTHONPATH=bindings/python/src python3 -m py_compile \
  $(rg --files bindings/python/src/zlink -g '*.py')
```

결과: 종료 코드 `0`.

`pyright 1.1.411 --project bindings/python/pyrightconfig.json` 결과는 `0 errors, 0 warnings, 0 informations`이다.
설정 target은 Python `3.9`이고 대상은 public `src/zlink/contracts` tree다. 실제 Python 3.9 interpreter는
이 환경에 없어 runtime import는 CPython 3.12에서만 실행했다.

### Candidate package와 clean consumer

```bash
scripts/local-package/bindings-candidate/create-manifest.sh \
  .artifacts/wsl/bindings-candidate/core-11.2.0.env
scripts/local-package/bindings-candidate/build-wsl.sh \
  --language python \
  --manifest .artifacts/wsl/bindings-candidate/core-11.2.0.env \
  --package-version 11.2.0
```

결과: 종료 코드 `0`. Builder는 candidate prefix를 사용해 wheel을 만들고, 새 venv에서 source checkout과
`core/build` fallback 없이 `zlink.version()`과 Pair message roundtrip을 실행했다. `/proc/self/maps`는
venv 안의 wheel payload를 가리켰다. 같은 venv에서 `run_samples.py --installed`를 실행한 결과도 `7/7`
이다.

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

결과: `status=complete`, `actual_result_lines=5`, runtime SHA는 candidate와 동일하다.

Multi runner:

```bash
ZLINK_LIBRARY_PATH="$PWD/core/build/lib/libzlink.so.11.2.0" \
  LD_LIBRARY_PATH="$PWD/bindings/python/src/zlink/native/linux-x86_64" \
  PYTHONPATH=bindings/python/src \
  bindings/python/perf/run_benchmarks_multi.sh --smoke \
  --pattern DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp \
  --runs 1 --clients 1
```

결과: `status=complete`, `success=1`, `fail=0`, `actual_result_lines=5`, runtime SHA는 candidate와 동일하다.
두 smoke 실행은 공식 성능 report를 만들지 않았다.

## POSD·DDD와 남은 조건

`2026-08-03-posd-ddd-review.ko.md`에서 Context, Message/Received, Socket, callback dispatcher와
package adapter를 lifecycle·ownership owner로 분리했다. 기존 raw module을 유지하고 Framework branch,
export, FFI, fixture와 dead sample만 제거하는 대안을 선택했다. Cost inventory는 allocation, copy, lock,
GIL과 no-cost를 모두 분류했고 `unclassified=0`이다. 확인 가능한 Critical·High·Medium finding은 없다.

이는 Codex self-review이므로 다음 조건이 남는다.

1. 공통 담당자가 현재 `11.2.0` candidate와 독립 V11-R2·V11-M3-CORE-PKG evidence의 identity를 확인한다.
2. Python 3.9 clean consumer와 CI 최고 version evidence를 추가한다.
3. Linux aarch64, macOS와 Windows를 release target으로 둘지 결정하고, 지원할 target마다 같은 candidate로
   package와 native consumer를 실행한다.
4. 독립 frontier reviewer가 같은 source manifest와 fresh evidence를 읽고 최종 `CLEAN`을 판정한다.
