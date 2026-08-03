# Go binding Core 11 최신화 실행 계획

> 대상 독자는 Go binding의 module, cgo bridge와 platform payload를 갱신하는 담당자와 reviewer다. 이 문서는
> “승인된 Core candidate를 받아 Go 작업만 독립적으로 완료하려면 무엇을 바꾸고 어떤 package consumer를
> 통과해야 하는가?”에 답한다.

## 1. 시작 조건과 현재 상태

[공통 계획](python-go-rust-core-11-update.ko.md)의 공통 시작 gate인 PGR-COMMON-01, PGR-COMMON-02와
PGR-COMMON-04가 통과하면 Python과 Rust의 진행 상태와 관계없이 이 작업을 시작할 수 있다. 시작 log에는
Core candidate identity, V11-R2 review, V11-M3-CORE-PKG evidence, install prefix와 raw symbol allowlist hash를
기록한다.

현재 module path는 `zlink.systems/zlink`다. Linux x86_64 header·runtime은 Core `10.6.0`, Linux aarch64
runtime은 major 9 SONAME을 사용한다. `contracts/service_spot.go`, root projection과 `internal/native`에는
MeshNode, Spot과 Actor 공개 API 및 구현이 있다.

상태는 **구현 미착수**다. 현재 `go test` 결과나 source `replace` consumer를 Core 11 package 증거로 사용하지
않는다.

## 2. Module path와 version 결정

Service API 제거는 breaking change이고 binding package는 Core 11 major/minor에서 다시 시작한다. 이 실행
계획의 목표 module은 다음과 같다.

```text
module zlink.systems/zlink/v11
version v11.1.0
```

Go semantic import versioning에 따라 source, test, sample, perf와 consumer import를 `/v11`로 함께 바꾼다.
구현 전 목표는 이 plan이 소유한다. 구현과 contract test가 통과하기 전에는 정식 Go spec을 목표 상태로
바꾸지 않는다. 다른 module path를 선택하려면 이 계획과 package version 정책을 먼저 review하여 변경한다.

구현을 마치면 Go source, test, sample, perf, module metadata와 이 작업이 사용하는 package script를 공통
형식의 binding source manifest에 봉인한다. 이후 test와 module zip은 이 manifest로 materialize한 격리
snapshot에서 만든다. Module zip, clean consumer와 독립 review evidence는 같은 manifest file SHA-256과
aggregate SHA-256을 기록한다.

## 3. 목표와 범위

Go module은 승인된 Core 11 raw C API만 투영한다. Context, message, raw socket, monitor, poller, timer와
utility를 Go 관례에 맞게 제공하고 service API와 이전 Core runtime을 포함하지 않는다.

다음 작업은 범위 밖이다.

- Framework service runtime 구현
- Windows cgo linker와 loader 신규 지원
- 새 Core API 설계
- 외부 module proxy 게시와 release tag 생성

## 4. 구현 작업

### GO-01 — Module과 공개 entrypoint

- `go.mod`를 `zlink.systems/zlink/v11`로 바꾼다.
- Root projection과 `contracts` package가 같은 raw 계약을 중복 노출하지 않게 한다.
- 공개 entrypoint 하나를 선택해 source, sample, perf와 GoDoc에서 일관되게 사용한다.
- Internal package와 cgo type이 public signature나 GoDoc에 나타나지 않게 한다.

### GO-02 — Raw cgo inventory

- `internal/native/ffi.go`와 복사된 header를 승인 Core 11 allowlist와 대조한다.
- cgo의 기본 include path는 package에 포함된 `include/`만 사용한다. Repository `core/include`를 개발 중에
  사용해야 하면 명시적인 option으로 분리하고 package gate에서는 거부한다.
- Package header tree에서 `zlink/service/`와 이전 service include를 제거한다.
- Header에 없는 service function, struct, enum과 callback 선언을 제거한다.
- Raw header 함수와 cgo 선언의 누락·추가를 machine-readable snapshot으로 검사한다.

### GO-03 — Service API 제거

- `contracts/service_spot.go`를 제거한다.
- `internal/native`의 MeshNode, Spot, Actor, service snapshot과 bridge 구현을 제거한다.
- Root projection에서 service type, constant, constructor와 operation export를 제거한다.
- Deprecated alias, compatibility package와 test helper로 service API를 유지하지 않는다.

### GO-04 — Error와 ownership

[Go·Rust parity inventory](go-rust-return-parity.ko.md)의 Go 열을 채우고 public API contract test를 연결한다.
No-data를 서로 다르게 설명하는 공통 spec의 한국어·영문 절을 PGR-COMMON-03에서 먼저 바로잡은 뒤 이 항목의
공개 시그니처와 test를 확정한다.

- 실패 가능한 메서드는 `(T, error)` 또는 `error`를 반환한다.
- 실제 error 값은 `*SubmitError`, `*RecvError`, `*BindError` 같은 함수군별 concrete type이다.
- 함수군별 error와 공통 `ZlinkError`는 `Code()`, `InternalErrno()`와 `errors.As`를 지원한다.
- 기존 `NativeErrno()`는 제거하고 호환 alias를 남기지 않는다.
- Core 작업의 인자 검증은 해당 함수군의 `INVALID_ARGUMENT`로 변환한다.
- No-data는 `false, nil`로 표현하고 실제 receive 실패는 `*RecvError`로 반환한다.
- Non-blocking submit backpressure는 실제 값이 `*SubmitError`인 `error`로 반환한다.
- Message send, receive, copy·move와 close의 ownership을 contract test로 검증한다.

### GO-05 — Hot path 설계 검토

- Message 생성, send, receive와 request completion에서 Go value, cgo pointer와 native buffer가 만들어지는
  위치를 기록한다.
- Ownership 계약에 필요하지 않은 `C.CBytes`, `C.GoBytes`, `[]byte` copy와 slice 재할당을 message마다
  반복하지 않는다. Go pointer를 Core가 호출 뒤에도 보관하게 하는 방식으로 copy를 없애지는 않는다.
- Request마다 progress polling goroutine이나 timer를 만들지 않는다. Completion progress를 공유할 수 있으면
  handle 단위에서 수명과 종료 책임을 관리한다.
- 사용자 callback을 실행하는 goroutine과 closure 비용은 inventory에서 `required`로 분류하고 현재 callback
  실행 의미는 바꾸지 않는다. Worker pool이나 공유 dispatcher 전환은 이번 최신화와 분리한다. 전환이 필요하면
  먼저 `bindings/doc/spec/draft/`의 별도 문서에서 사용자가 관찰할 수 있는 실행 위치, 격리, ordering과 느린
  callback의 영향을 설계하고 리뷰한다.
- 독립 socket의 hot path를 package 전역 mutex로 직렬화하지 않는다. 필요한 lock은 handle과 callback registry를
  보호하는 최소 범위로 제한한다.
- `tests/hot-path-cost-inventory.json`에서 비용 발생 source, 분류, 이유와 guard test를 연결한다.
- `go test ./internal/native -run 'TestOptimizationGuard'`가 종료 코드 0이고 inventory의 `unclassified`가
  0건이어야 통과한다.

### GO-06 — Sample과 perf smoke

- Pair, pub/sub, dealer/router request, STREAM receive·packet callback과 monitor sample을 유지한다.
- Spot, Actor와 service operation sample·perf scenario를 runner에서 제거한다.
- Sample과 perf는 선택한 public entrypoint만 import한다.
- Private cgo bridge, native symbol 직접 호출과 raw byte 우회를 사용하지 않는다.
- Runner가 승인 module package의 runtime path와 SHA-256을 출력하고 repository `core/build`을 선택하지 않게
  입력 경로를 바꾼다.
- 공식 report를 만들지 않는 `--smoke` mode를 runner에 추가하고 다음 명령을 실행한다.

```bash
# Single runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks.sh --smoke --pattern PAIR --duration 1 --msg-sizes 64 --transports inproc --runs 1

# Multi runner의 lifecycle과 metric 출력을 확인한다.
perf/run_benchmarks_multi.sh --smoke --pattern MULTI_DEALER_ROUTER --duration 1 --msg-sizes 64 --transports tcp --runs 1 --clients 1
```

- 두 명령은 공통 계획의 smoke 판정에 따라 ready, active, 필수 `RESULT` metric과 exit code만 확인한다. 결과
  수치는 공식 report나 성능 비교에 사용하지 않으며 실행 뒤 report 파일이 남지 않아야 한다.

### GO-07 — 정식 문서

구현과 contract test가 통과한 뒤 공통 bindings spec의 Go error 문구를 Go 언어 관례에 맞춘다. Go의 정적
반환 타입은 `error`이고 실제 값이 함수군별 concrete type이라는 점을 명시한다. Go 정식 spec의 한국어·영문
문서와 guide도 `/v11` module과 실제 raw 공개 API에 맞춘다.

## 5. Platform 검증

현재 cgo linker 설정은 `internal/native/ffi.go`, payload는 `native/`에 있다. 다음 표는 이 source에 분기가 있는
target만 나타낸다.

| Platform | 현재 linker 후보 | 완료 조건 |
|----------|------------------|-----------|
| Linux amd64 | `linux-x86_64` | Core 11 runtime과 native consumer |
| Linux arm64 | `linux-aarch64` | Major 9 payload 교체와 native consumer |
| macOS amd64 | `darwin-x86_64` | Core 11 runtime과 native consumer |
| macOS arm64 | `darwin-aarch64` | Core 11 runtime과 native consumer |
| Windows | 없음 | 별도 구현 전에는 release 범위 밖 |

Linux와 macOS 각 artifact는 같은 Core candidate identity를 사용하되 runtime hash와 loader evidence는 platform별로
기록한다. Guide에 Windows 지원을 암시하는 내용이 있으면 구현 완료 후 실제 범위에 맞춘다.

## 6. Go module package와 clean consumer

Repository subtree를 tar로 묶거나 consumer `go.mod`에 local path `replace`를 넣는 방식은 package 완료
증거로 사용하지 않는다. Candidate tooling은 표준 file proxy layout을 만든다.

```text
<proxy>/zlink.systems/zlink/v11/@v/v11.1.0.info
<proxy>/zlink.systems/zlink/v11/@v/v11.1.0.mod
<proxy>/zlink.systems/zlink/v11/@v/v11.1.0.zip
```

Zip 내부 root는 `zlink.systems/zlink/v11@v11.1.0/`이다. Go file proxy는 module version마다 zip 하나만
제공하므로 이 zip에 지원하는 Linux·macOS platform runtime을 모두 포함한다. 각 payload는 해당 platform
provenance와 hash가 같아야 한다. 이전 runtime, service header와 repository build output은 포함하지 않는다.

Clean consumer는 빈 `GOMODCACHE`와 `GOCACHE`에서 다음 조건으로 실행한다.

- `GOPROXY=file://<absolute-proxy>,off`를 사용한다.
- Local candidate이므로 `GOSUMDB=off`를 사용하되 `GOPRIVATE`, `GONOSUMDB`와 `replace`는 사용하지 않는다.
- `go mod download`, `go build`, runtime version 확인과 raw message smoke를 실행한다.
- Binary의 dynamic dependency가 module cache 안의 package runtime을 가리키는지 확인한다.
- Module zip, `.mod`, `.info`, runtime과 header hash를 package evidence에 기록한다.

## 7. 검증 표

| Gate | 상태 | Evidence |
|------|------|----------|
| 공통 candidate 입력 확인 | `PENDING` | — |
| Go binding source manifest | `PENDING` | — |
| `/v11` module path와 version | `PENDING` | — |
| Raw cgo·header·symbol allowlist | `PENDING` | — |
| Public API snapshot과 service 부재 | `PENDING` | — |
| `go test ./...` | `PENDING` | — |
| `go vet ./...` | `PENDING` | — |
| Hot path cost inventory와 optimization guard | `PENDING` | — |
| Perf runner smoke | `PENDING` | — |
| Go·Rust parity inventory | `PENDING` | — |
| Raw sample process runner | `PENDING` | — |
| File proxy package contents | `PENDING` | — |
| Replace 없는 clean module consumer | `PENDING` | — |
| Linux·macOS native consumer | `PENDING` | — |
| 한국어·영문 spec, GoDoc와 guide | `PENDING` | — |
| 독립 review | `PENDING` | — |

명령, 종료 코드, test 수, module zip SHA-256과 실패 원인은 `bindings/doc/plan/log/go/` 아래 날짜별 log에
기록한다.

## 8. 완료 조건

다음 조건을 모두 만족해야 Go 작업이 완료된다.

1. Module은 `zlink.systems/zlink/v11@v11.1.0`이며 승인 Core candidate identity와 Go binding source manifest를
   기록한다.
2. Raw cgo와 공개 API가 Core 11 allowlist에 맞고 service API가 없다.
3. 함수군별 error, no-data와 ownership이 parity inventory의 Go 열과 일치한다.
4. Source test, `go vet`, hot path design review, perf smoke와 raw sample process가 통과한다.
5. Replace 없는 clean consumer가 file proxy package의 runtime으로 실제 message를 송수신한다.
6. Linux·macOS에서 package contents와 runtime load가 검증된다.
7. 정식 spec, GoDoc과 guide가 구현과 일치한다.
8. 성능 수치 개선은 후속 Go 계획으로 분리되어 있으며 이번 완료 근거로 사용하지 않는다.
9. Critical, high, medium finding과 실행하지 않은 필수 gate가 남아 있지 않다.
