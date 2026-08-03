# Go binding Core 11 진행 log

작성일: 2026-08-03

이 log는 Go binding 작업에서 확인한 사실과 현재 미완료 조건을 분리해 기록한다. Root `VERSION`,
`core/include/zlink.h`, 다른 binding과 framework의 dirty change는 이 작업의 변경 범위에 포함하지 않았다.

## 현재 판정

Go source와 Core 11 raw projection은 Linux x86_64에서 test, sample process, perf smoke와 file-proxy clean
consumer를 통과했다. 현재 전체 작업은 `PARTIAL / NOT CLEAN`이다. Candidate verify 파일은 존재하지만 V11-R2
review가 `independent: false`이고, Go–Rust parity, common submit draft 승인, Linux arm64/macOS payload와
독립 frontier review가 남아 있다.

## Candidate 입력

```text
candidate:
  path: .artifacts/v11/evidence/V11-M3-CORE-VERIFY/candidate-reply-match-completion-hwm-20260801.json
  sha256: d318525a4cf8496b6bef5d900c9a88330ea6d7e10ed4120ac0fd9f19d23f6765
  aggregateSha256: 327587596195a162374498b630f51a043977dd392eb556061af615bf05186703
  baseRevision: 73a9ce6d5bf275e9675333fc01e50948dbf895a2
review:
  path: .artifacts/v11/evidence/V11-R2/core-candidate-reply-match-completion-hwm-review-20260801.json
  sha256: 171a9cc8f7203500de08050dcb74ecd36b4c9ce55a75a14ce1bee283705c9e04
  status: passed
  independent: false
```

`V11-M3-CORE-PKG`의 기존 evidence는 현재 Go가 사용하는 11.1.0 candidate와 동일한 identity임을 확인하지
못했으므로 Go package 승인 증거로 사용하지 않았다.

## 구현과 설계 checkpoint

다음 path-limited checkpoint를 각각 commit하고 origin branch에 push했다.

| Commit | 범위 |
|--------|------|
| `afd96c43aa` | Core 11 raw projection, service API·header·sample·perf 제거, `/v11`, HWM `uint64` 전달 |
| `eab6cf9411` | `ZlinkError`/`InternalErrno`, context cancellation test, bounded handle progress pump, raw sample/perf runner |
| `f1210adaffc` | raw header/symbol allowlist, hot-path cost inventory, Go file-proxy package와 clean-consumer gate |

POSD와 DDD 판단은 다음 책임 경계를 기준으로 적용했다.

- Core raw adapter는 native pointer, callback userdata, codec와 transport detail을 내부에 둔다.
- Message, Received, operation builder와 request progress handle은 각각 buffer ownership, receive adoption,
  submit state와 completion lifetime의 owner다.
- Context cancellation은 Core function-group error mapping과 분리한다.
- Go-owned scratch buffer를 반환 전에 다시 복사하던 경로를 제거했으며, ownership에 필요한 native allocation,
  snapshot copy, submit preservation copy와 callback worker는 cost inventory에 분류했다.
- Service alias, forwarding projection, Core 10 compatibility export와 dead service path를 남기지 않았다.

## 검증 명령과 결과

실행 위치는 별도로 표시하지 않은 경우 `bindings/go`이다.

```bash
bash bindings/go/tests/run_tests.sh
```

결과: 종료 코드 `0`. `go test ./...`, `go vet ./...`, raw allowlist·optimization·hot-path guard와 process
sample이 통과했다. Sample 결과는 `pass=7 fail=0`이다.

```bash
go test -count=2 ./...
go vet ./...
go test ./internal/native -run 'Test(OptimizationGuard|RawCore11Allowlist|HotPathCostInventory)' -count=1
```

결과: 세 명령 모두 종료 코드 `0`.

```bash
perf/run_benchmarks.sh --smoke --pattern PAIR --duration 1 --msg-sizes 64 --transports inproc --runs 1
perf/run_benchmarks_multi.sh --smoke --pattern MULTI_DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp --runs 1 --clients 1
```

결과: 두 명령 모두 종료 코드 `0`. Single runner는 `PAIR` inproc, multi runner는 TCP
`MULTI_DEALER_ROUTER`에서 READY/active와 필수 `RESULT` metric을 출력했다. Smoke 실행은 공식 report를 만들지
않았다.

```bash
scripts/local-package/go/build-wsl.sh \
  --platforms linux-x86_64 \
  --output-root /home/hep7/project/kairos/zlink/.artifacts/wsl/go
```

결과: module `zlink.systems/zlink/v11`, version `v11.1.0`, clean consumer `pass`. Consumer는 빈
`GOMODCACHE`와 `GOCACHE`에서 `replace` 없이 module을 다운로드·build하고 Pair message roundtrip을 실행했다.
`ldd`는 module cache의 `native/linux-x86_64/libzlink.so.11`을 가리켰다.

현재 package evidence:

```text
evidence: .artifacts/wsl/go/go-package-v11.1.0.json
evidence sha256: 3b13349d2201d7bf441385c09058052ce98d203df3b3d5d9dc2d31074897895c
sourceRevision: d02edf6b4f609e5e4a7db1d0338e54e2d66de890
sourceManifestSha256: 7b4cb11a8c40937eb6c09a900378d1bab865528f4ab2fdf15991400861e161f1
packageScriptSha256: 3784ad3675b1f26d89b8f54fb6f3d1ee6e5cb6f7b97db4e281f5c7bc646a6825
moduleZipSha256: c1f0fa1ca09a9f845ee22a1daf29673c23c83ca4d8bd0e57d125fdcce4cfa394
headerSha256: 159c8024f8ed090e0c3acfe51e665339d3a43e93b37dc9e21490b703df717f1d
sourceSha256: 884cdfabd5c2cfebde9641447d98cbecdc61bfb3c40dee5f9b7f6025a7819e99
runtimeSha256: a790c7fbfd2a6d7b61c03209b9356e7ac1693afa9456768f867f4e93790b4991
```

```bash
unzip -Z1 /home/hep7/project/kairos/zlink/.artifacts/wsl/go/proxy/zlink.systems/zlink/v11/@v/v11.1.0.zip \
  | rg 'service|spot|actor|/build/|/results/'
```

결과: 금지된 zip entry가 출력되지 않았다. Package script는 기존 zip을 임시 파일로 만든 뒤 교체하므로
이전 실행의 삭제된 service entry가 산출물에 남지 않는다.

## 계약 문서

- `bindings/doc/spec/go/README.ko.md`와 `README.en.md`는 `/v11` raw Core 11 public contract, current
  `(bool, error)` submit signature, ownership, no-data와 error semantics를 반영한다.
- `bindings/go/README.godoc.md`와 sample runner는 구현된 public root projection을 기준으로 한다.
- Common submit draft 승인과 Go–Rust parity inventory 통합은 별도 gate로 남긴다.

## 남은 작업

1. 현재 Go package evidence를 V11-M3-CORE-PKG 승인 identity와 연결하고, platform별 package evidence를
   새로 만든다.
2. Linux arm64의 major 9 payload와 Darwin payload를 같은 Core 11 candidate runtime으로 교체한 뒤 native
   consumer와 loader evidence를 실행한다.
3. Go–Rust parity inventory와 submit 반환 draft를 승인하고, 현재 signature를 유지할지 error-only로 바꿀지
   공통 contract에 반영한다.
4. 구현자가 아닌 frontier reviewer가 같은 source manifest와 fresh test 결과를 read-only로 검토한다. `Critical`,
   `High`, `Medium` finding이 없고 최종 `CLEAN` 판정이 있어야 전체 완료로 올린다.
