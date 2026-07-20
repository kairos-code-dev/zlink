# RouteMesh Python·Go·Rust bindings 최신화 계획

## 1. 목적과 문서 책임

이 문서는 RouteMesh 전환에서 보류한 Python, Go와 Rust bindings를 최신 Core 공개 계약에 맞추기
위한 작업 범위, 실행 순서와 완료 gate를 정의한다. 대상 독자는 세 bindings를 구현하고 검증하는
개발자와 reviewer다. 이 문서는 “Core 수정이 계속되는 동안 세 bindings를 어떤 기준으로 갱신하고,
어느 시점에 완료로 판정하는가?”에 답한다.

Python, Go와 Rust bindings는 단순히 Core 기능을 호출할 수 있게 만드는 wrapper가 아니라 운영 환경에서
사용할 수 있는 **고성능 라이브러리**를 목표로 한다. 다만 이 계획에서는 perf 수치를 분석하거나 bindings
구현을 최적화하지 않는다. 이 계획의 perf 범위는 각 언어 perf가 C 기준과 같은 의미로 작성되어 있는지
검토하고, 저장소의 perf 정책을 준수하도록 고친 뒤 single·multi smoke를 통과하는 데까지다. 측정 결과를
바탕으로 한 성능 개선은 별도 단계에서 진행한다.

현재 진행 상태의 단일 기준은
[`RouteMesh 10.0.0 실행 진행표`](./route-mesh-10.0.0-execution-ledger.ko.md)다. 이 문서는 상태,
checkbox와 완료 증거를 소유하지 않는다. 실제 작업을 시작할 때 실행 진행표에 이 문서의 작업 ID를
담당 범위로 추가하고, 상태와 검증 결과는 그 행에만 기록한다. 실행 진행표에 기록된 Python·Go·Rust
보류 상태는 작업 시작 시 사용자의 별도 진행 결정을 반영해 갱신한다.

이 작업은 기존 네 framework 언어 lane과 분리한다. Python, Go와 Rust는 framework 구현 대상이
아니므로 `framework/languages/`의 source, sample, E2E와 package version을 변경하지 않는다.

### 1.1 실행 권한과 문서 수정 제한

이 문서와 PGR-00~PGR-REV 작업 범위는 전용 실행 범위다. 사용자 또는 전체 작업을 조정하는
coordinator가 명시적으로 인가하고, 실행 진행표에서 해당 PGR ID를 배정받은 에이전트만 다음 작업을
수행할 수 있다.

- 이 문서의 내용, 범위, 작업 ID와 완료 gate 수정
- PGR 범위의 bindings source, test, sample, package, workflow와 관련 문서 수정
- Core artifact 동기화, package 생성, 검증과 review 결과 기록
- PGR 항목의 시작, 완료, 차단과 재개 판정
- review snapshot 생성, finding 기록, clean 판정과 review 종료

인가받지 않은 에이전트는 이 문서를 읽기 전용 입력으로만 사용할 수 있다. `미착수` 상태의 PGR 항목이
있거나 관련 파일을 수정할 수 있다는 이유만으로 작업 권한을 추론해서는 안 된다. 인가받지 않은
에이전트는 이 문서와 PGR 범위의 파일을 수정하거나, 작업 ID를 가져오거나, 검증·review를 완료한 것으로
기록하지 않는다. 작업이 필요하면 문서를 변경하지 않고 사용자 또는 coordinator에게 인가와 담당 ID
배정을 요청한다.

인가받은 에이전트도 배정된 PGR ID 밖의 항목을 임의로 수행하지 않는다. 여러 에이전트가 명시적으로
인가된 경우에도 각자 배정받은 ID만 수정하고, 공통 계약·Core candidate·package 입력을 바꾸는 작업은
coordinator가 지정한 담당자만 수행한다.

review도 같은 인가 규칙을 적용한다. 사용자 또는 coordinator가 reviewer로 명시적으로 인가하고 실행
진행표에서 review ID와 대상 snapshot을 배정한 에이전트만 review를 수행할 수 있다. 인가받지 않은
에이전트의 finding, `CLEAN` 문구와 완료 보고는 review 증거나 완료 gate로 사용하지 않는다. 인가받은
reviewer는 배정된 snapshot과 review 축만 검토하며, 별도의 수정 권한을 함께 받지 않았다면 review 중
source, test, package와 문서를 변경하지 않는다. finding 수정은 배정받은 구현 담당자가 수행하고,
reviewer는 변경되지 않는 새 snapshot에서 재검토한다.

## 2. 정식 기준과 변경 경계

공개 동작은 다음 문서와 header만 기준으로 판단한다.

- [Core public contract governance](../../../../core/doc/spec/core/00-public-contract-governance.ko.md)
- [Core service 목차](../../../../core/doc/spec/core/service/README.ko.md)
- [MeshNode](../../../../core/doc/spec/core/service/01-mesh-node.ko.md),
  [dispatch](../../../../core/doc/spec/core/service/02-dispatch.ko.md),
  [Spot](../../../../core/doc/spec/core/service/03-spot.ko.md),
  [Actor](../../../../core/doc/spec/core/service/04-actor.ko.md),
  [STREAM session](../../../../core/doc/spec/core/service/05-stream-session.ko.md)
- [message](../../../../core/doc/spec/core/02-message.ko.md),
  [polling](../../../../core/doc/spec/core/06-polling.ko.md),
  [monitoring](../../../../core/doc/spec/core/07-monitoring.ko.md),
  [errors](../../../../core/doc/spec/core/03-errors.ko.md)와
  [errno](../../../../core/doc/spec/core/04-errno-map.ko.md)
- [Core public header](../../../../core/include/zlink.h)와 `core/include/zlink/service/*.h`
- [bindings 공통 API 정책](../../../../bindings/doc/spec/README.ko.md) 및
  [Python 구현·정적 타입 원칙](../../../../bindings/doc/spec/python/README.ko.md#정적-타입-원칙),
  [Go](../../../../bindings/doc/spec/go/README.ko.md),
  [Rust](../../../../bindings/doc/spec/rust/README.ko.md)의 언어별 표현 규칙
- [perf 공통 정책](../../../../doc/perf/PERF_POLICY.md),
  [single 정책](../../../../doc/perf/PERF_SINGLE_TEST_POLICY.md),
  [multi 정책](../../../../doc/perf/PERF_MULTI_TEST_POLICY.md)과
  [Spot 정책](../../../../doc/perf/PERF_SPOT_TEST_POLICY.md)

RouteMesh 관련 설계 계획과 다른 bindings 구현은 계약을 이해하기 위한 입력으로만 사용한다. Core
정식 spec이나 public header에 없는 기능을 다른 언어에 있다는 이유로 추가하지 않는다. Python, Go와
Rust의 이름, 오류 표현과 resource 수명은 각 언어의 관례를 따르되 관찰 가능한 동작은 Core 계약과
같아야 한다.

### 2.1 포함 범위

- 세 언어의 public contract와 export projection
- native FFI 선언, struct·enum layout, callback와 handle 수명
- MeshNode lifecycle, channel membership, peer 연결, 상태와 조회
- ready 알림, claim, receive batch, message retain과 one-shot reply
- node·channel·Spot·Actor 메시징과 Logical Multicast
- Actor lifecycle·transfer와 STREAM session 연동 가운데 Core가 공개한 기능
- 오류·result 변환, thread safety, shutdown과 ownership
- 세 bindings의 test, sample, example와 perf source의 API 전환
- native payload, package metadata, local package와 깨끗한 consumer 검증
- Python wheel·sdist, Go module source, Rust crate와 release workflow의 비배포 검증
- Python 공개 API의 타입 완전성, `py.typed`, 엄격한 정적 타입 검사와 설치 패키지를 사용하는
  외부 프로젝트의 타입 검사
- 세 bindings perf의 C 기준 대응, `doc/perf` 정책 준수와 single·multi smoke

### 2.2 제외 범위

- Python·Go·Rust framework package 신설
- Core 정식 spec에 없는 편의 API, compatibility wrapper와 이전 이름 alias
- framework source, framework sample·E2E와 framework package pin 변경
- Core 또는 bindings 성능 분석·개선, 정량 성능 목표 변경, 기준선 비교와 full perf 실행
- PyPI, crates.io, GitHub Release와 Go module tag의 실제 외부 배포
- 지원 platform의 native artifact와 clean consumer 검증을 생략한 채 완료로 간주하는 것

perf smoke에서 출력되는 처리량과 latency는 경로가 실제로 실행됐음을 확인하는 결과일 뿐 이 계획의
성능 판정 자료가 아니다. 수치 비교, 병목 분석, 최적화와 full perf는 후속 성능 단계가 소유한다. 외부
배포는 local package와 clean consumer 검증이 끝난 뒤 별도 승인을 받아 진행한다.

### 2.3 perf 대응과 smoke 원칙

`bindings/c/perf`는 Python, Go와 Rust perf를 맞추는 기준 구현이다. 각 언어 perf는 C perf의 코드를
문법적으로 옮기는 데 그치지 않고 같은 시험을 같은 의미로 실행해야 한다. 다음 항목을 inventory와
review에서 확인한다.

- single·multi의 pattern, transport, process 구성, ready·active phase, handshake token과 timeout 의미가
  C 기준과 대응해야 한다. RouteMesh Spot 패턴은 `PERF_SPOT_TEST_POLICY.md`의 MeshNode 토폴로지와
  ready·claim 수신 흐름을 따른다.
- timestamp 기록, 유효 수신 판정, 처리량 증가, latency 표본과 `RESULT` 확정 위치가 C 기준과 같은
  측정 지점을 사용해야 한다. 필수 metric과 `success`, `unsupported`, `skip`, `fail`, `complete`,
  `partial`의 의미를 바꾸지 않는다.
- perf 바이너리는 해당 언어 binding의 공개 API로 data path를 직접 실행한다. binding 비공개 API,
  raw C API와 다른 언어 benchmark의 결과 중계로 수치를 만들지 않는다. 공통 raw STREAM client는
  정책이 허용한 `MULTI_STREAM` client에만 사용한다.
- 바이너리는 `pattern/transport/size/run` 한 case와 `RESULT` 출력을 담당하고, runner는 case 순회,
  process 제어, 집계, 결과 저장과 완료 판정을 담당한다. 두 책임을 한쪽으로 합치지 않는다.
- context auto-HWM, I/O 모델, backpressure, retry 금지, inflight 제한 금지, 결과 파일과 CLI option은
  `doc/perf` 공통·suite별 정책을 그대로 따른다. 실패를 workload 축소, 자동 재시도 또는
  `UNSUPPORTED`로 바꾸어 숨기지 않는다.
- C perf와 각 언어 perf의 source·runner·pattern 대응표를 작성하고, 누락·추가·의미 차이가 0개인지
  review한다. 언어에서 지원하지 않는 조합은 정책이 허용하는 근거와 상태 표현을 기록한다.

각 언어 lane은 공식 entrypoint에서 아래 smoke만 실행한다. 전체 pattern과 기본 transport를 64B 하나로
실행하며, single과 multi가 모두 `status=complete`이고 `fail`이 없어야 한다.

```bash
# single의 모든 pattern과 기본 transport가 공개 binding API로 실행되는지 확인한다.
./perf/run_benchmarks.sh --pattern ALL --msg-sizes 64

# multi의 모든 pattern과 기본 transport가 공개 binding API로 실행되는지 확인한다.
./perf/run_benchmarks_multi.sh --pattern ALL --msg-sizes 64
```

smoke 결과의 언어 간 수치 비율과 C 대비 성능은 이 계획에서 평가하지 않는다. smoke가 perf 구현 또는
runner 결함 때문에 실패하면 이 계획에서 고친다. Core나 binding의 별도 기능 결함이 발견되면 perf에서
우회하지 않고 재현 test와 담당 범위를 기록해 별도 결함 작업으로 넘긴다. 결함을 별도 ID로 넘겨도 현재
언어 lane은 차단 상태를 유지한다. 결함 수정 뒤 같은 Core candidate에서 회귀 test와 실패한 perf smoke를
다시 통과하기 전에는 해당 lane이나 PGR-REV를 완료하지 않는다.

`PERF_POLICY.md`가 최종 perf 리팩토링 뒤 요구하는 full single·multi 실행과 성능 비회귀 판정은
면제하지 않고 후속 `PGR-PERF`로 이관한다. `PGR-PERF`는 PGR-REV가 고정한 Core·bindings manifest,
C 대응표와 smoke 결과를 입력으로 받아 full C 기준선, 세 bindings full single·multi, C 대비 성능 판정과
성능 개선을 수행한다. 후속 단계의 정본은 시작 전에
`doc/perf/perf/route-mesh-python-go-rust-bindings-performance.ko.md`로 작성한다. 이 후속 단계는 별도 사용자
인가와 실행 진행표의 담당자 배정 전에는 시작하지 않는다. 따라서 이 계획의 완료는 full-run gate를
제외한 perf 계약·runner 정책 준수와 smoke 준비가 끝났다는 뜻이며, 최종 성능 검증이나 perf 개선이
끝났다는 증거로 사용할 수 없다.

## 3. 현재 기준선과 예상 규모

2026-07-20 checkout에서 Core source version은 `10.6.0`, SOVERSION은 `10`이다. 반면 실행 진행표가
현재 지정한 완료 목표는 Core와 bindings base version `10.7.0`이다. 따라서 `10.6.0`은 계획 작성 시점의
조사 기준일 뿐 구현 또는 package의 최종 고정 version이 아니다. PGR 작업은 `10.6.0`으로 시작할 수
있지만, 실행 진행표가 지정한 최종 Core candidate가 실제 source·runtime으로 만들어지고 독립 review를
통과하기 전에는 package와 PGR-REV를 완료하지 않는다. 실행 진행표의 목표가 새 `10.x.0`으로 바뀌면 그
값이 `10.7.0`을 대신하며 §4.1에 따라 세 lane을 다시 검증한다.

| 대상 | 현재 public/package 기준 | 전환 입력 |
|---|---|---|
| Python | `pyproject.toml` `9.0.4` | `zlink` export, `contracts/`, `_runtime/`, `_native/`, wheel·sdist |
| Go | `go.mod`에 package version 없음 | `contracts/`, root projection, `internal/native/`, bundled header·native library, module tag |
| Rust | `Cargo.toml` `9.0.4` | `lib.rs` re-export, `contracts/`, `runtime/`, bundled header·native library, crate |

초기 scoped 검색에서는 구 `SpotNode` 또는 route bridge 계열 이름이 Python 53개, Go 27개, Rust 46개
파일에서 발견됐다. 이 수치는 source, test, sample와 perf를 함께 센 전환 규모의 참고값이다. 작업을
시작할 때 최신 snapshot에서 같은 검색을 다시 실행해 실제 red gate와 제거 목록을 고정한다.

로컬 Core runtime 동기화는 기존
[`sync-local-core-libs.sh`](../../../../scripts/local-package/native/sync-local-core-libs.sh)가
`python go rust` 선택 인자를 이미 지원한다. 이 스크립트가 복사한 파일은 개발 중 검증 입력이며, 최종
package는 검증된 Core candidate와 provenance가 일치하는 artifact만 사용한다.

현재 검증·배포 경로에도 version drift가 보인다. Go의 direct-header test는 patch `2`, Rust의 같은
test는 patch `3`을 고정해 현재 Core patch `0`과 일치하지 않는다. 이 검사는 특정 과거 숫자를 기대하지
말고 package가 선언한 Core version과 bundled header·runtime이 같은지 비교해야 한다. Rust release
workflow에는 저장소에 없는 세 codec crate의 publish 명령도 남아 있다. PGR-02는 이런 stale 검증을
먼저 실패하게 만든 뒤 실제 package 구성과 맞게 정리한다.

## 4. Core candidate 고정과 변경 추적

세 lane이 같은 Core를 사용했는지 확인할 수 있도록 작업 시작, Core version 변경 직후, lane review
직전과 최종 package 생성 직전에 candidate manifest를 만든다. manifest와 증거 log는 실행 진행표가
지정한 `framework/doc/plan/v10.0/log/` 하위 작업 디렉터리에 둔다.

manifest에는 다음 값을 기록한다.

| 항목 | 확인 내용 |
|---|---|
| Core source | version, commit SHA와 작업 snapshot aggregate hash |
| 공개 계약 | Core 정식 spec과 `core/include/` aggregate hash |
| ABI | exported symbol 목록, service ABI version, struct size·alignment fixture |
| runtime | 실제 `libzlink` 경로, SHA-256, `zlink_version`, SONAME |
| build freshness | runtime이 source보다 오래되지 않았으며 manifest source에서 만들어졌다는 증거 |
| bindings 입력 | 세 언어에 복사한 header와 native payload의 hash |

개발 중에는 commit하지 않은 Core snapshot을 사용할 수 있다. 다만 그 snapshot은 aggregate hash로
식별하고 package release candidate로 사용하지 않는다. 최종 local package는 commit과 독립 review가
끝난 Core candidate로 다시 만들어야 한다.

### 4.1 Core version이 올라갔을 때의 처리

| 변경 | 필요한 조치 |
|---|---|
| 정식 spec·header·export·layout이 같고 계약 안의 Core 결함만 수정됨 | Core를 다시 build하고 세 native payload를 동기화한다. 결함을 재현하는 binding 회귀와 세 lane 전체 test, package consumer smoke를 다시 실행한다. |
| Core version만 새 `10.x.0`으로 올라감 | Python·Rust base package version을 같은 `10.x.0`으로 맞춘다. Go는 최종 `go/v10.x.0` tag 입력을 검증한다. 이전 Core에서 생긴 binding patch 숫자는 승계하지 않는다. |
| 정식 spec, public header, symbol 또는 layout이 바뀜 | 단순 결함 수정으로 처리하지 않는다. 공통 inventory와 구현 전 draft를 다시 검토하고 영향받은 세 lane의 완료 판정을 모두 무효화한다. |
| 특정 binding만 수정되고 Core가 그대로임 | 그 binding만 `10.x.1`, `10.x.2`처럼 patch를 올릴 수 있다. 다른 bindings와 Core version은 바꾸지 않는다. |

version 문자열만 비교해 재검증 범위를 줄이지 않는다. manifest의 spec, header, symbol과 layout hash를
함께 비교해야 한다. Core가 다시 올라가면 최종 package를 만들기 전에 세 bindings가 같은 runtime
SHA-256을 포함하는지 다시 확인한다.

## 5. 구현 전 계약과 red gate

현재 bindings 정식 spec과 세 언어 구현에는 구 `SpotNode` 중심 계약이 남아 있다. 새 public API를
구현하기 전에 `bindings/doc/spec/draft/route-mesh-python-go-rust.ko.md`를 만들고, 구현 전 초안이며 현재
공개 계약이 아니라는 점을 첫머리에 명시한다. 한 기능이 세 언어에 투영되는 작업이므로 draft 하나에서
공통 의미와 세 언어의 exact public interface를 함께 관리한다.

draft와 inventory는 다음 범주를 빠짐없이 대응시킨다.

1. MeshNode 생성, 설정, start·shutdown·destroy와 channel membership
2. manual peer 연결·제거, node·channel direct send/request와 publisher
3. node 상태, peer와 peer channel snapshot
4. ready handler, ready batch, claim, receive batch, retain과 generic reply
5. Spot 생성·subscription·send/request·publish detail
6. Actor 생성·조회·join·leave·destroy·transfer와 Actor-originated operation
7. STREAM session binding, Actor 방향 메시징과 transfer barrier
8. metadata snapshot, 최대 길이·개수 검증과 malformed 입력 거부
9. result·errno 변환, callback thread 경계, ownership과 close 재진입
10. 제거할 `SpotNode`, route bridge, dispatch worker option, service `*_part` 공개 wrapper와 이전 alias

구현 시작 전 다음 red gate를 만든다.

- 최신 Core header를 기준으로 FFI symbol·layout 대조가 실패한다.
- 새 MeshNode public surface를 사용하는 compile 또는 import fixture가 실패한다.
- 제거 대상 API의 compile-fail 또는 scoped no-hit 검사가 실패한다.
- claim·batch ownership, malformed metadata와 reply token 재사용의 계약 테스트가 실패한다.
- package consumer가 source tree 우회 없이 새 API를 사용할 수 없어 실패한다.
- C perf 대응표에서 pattern·transport·phase·handshake·metric 의미 차이가 발견된다.
- 정책에 따른 single 또는 multi 전체 pattern·64B smoke가 실패한다.

red gate는 기존 assertion을 약하게 바꾸거나 compatibility shim을 추가해 해소하지 않는다. 실패 원인을
public contract, native bridge, runtime ownership 또는 package projection 가운데 책임을 가진 계층에서
수정한다.

## 6. 실행 작업 묶음

아래 ID는 작업 분해 기준이다. 상태와 증거 열은 이 문서에 추가하지 않고 실행 진행표에서만 관리한다.

| ID | 작업 | 완료 조건 |
|---|---|---|
| PGR-00 | Core candidate manifest와 최신 전환 inventory | 세 bindings가 사용할 spec·header·symbol·runtime hash, 제거 대상 검색 결과와 C perf에 대한 언어별 perf 대응표가 고정됨 |
| PGR-01 | 구현 전 bindings draft | 공통 의미와 Python·Go·Rust exact interface, 오류·ownership 계약이 review됨 |
| PGR-02 | 공통 native·package 도구 준비 | 로컬 Core 동기화, provenance, version 대조, 언어별 package 생성과 clean consumer 진입점이 `scripts/local-package/` 정책에 맞음 |
| PGR-PY | Python binding 전환 | 공개 export, FFI, runtime, test·sample·패키지가 최신 Core 계약과 일치하고 Python 공개 API의 엄격한 타입 검사가 통과함 |
| PGR-GO | Go binding 전환 | public package, cgo runtime, test·sample·module consumer가 최신 Core 계약과 일치함 |
| PGR-RS | Rust binding 전환 | crate re-export, FFI, safe runtime, test·sample·crate consumer가 최신 Core 계약과 일치함 |
| PGR-X | 세 package의 공통 E2E smoke | 동일 Core runtime으로 direct·channel·Spot·Actor·claim·shutdown scenario가 통과함 |
| PGR-DOC | 정식 bindings spec과 API reference 갱신 | 구현이 확정된 계약만 정식 문서로 옮기고 draft와 stale 이름을 정리함 |
| PGR-REV | 독립 review와 최종 재검증 | 세 lane과 통합 package가 계약·설계·정리 세 축에서 clean이며 최종 Core manifest와 일치함 |
| PGR-PERF | 후속 full perf와 성능 개선 | PGR-REV manifest를 입력으로 full C 기준선과 세 bindings full single·multi를 실행하고 별도 성능 목표와 review gate를 닫음 |

PGR-00~PGR-02가 끝난 뒤 세 언어 lane은 병렬로 진행할 수 있다. Core candidate가 바뀌면 공통 native
입력을 먼저 다시 고정한 뒤 각 lane을 재검증한다. 어느 lane도 다른 언어 구현을 계약 근거로 사용하지
않는다. PGR-PERF는 이 계획의 구현·완료 범위가 아니라 후속 단계가 인수할 작업 ID다. PGR-REV가 끝나면
실행 진행표에는 최종 manifest, smoke 결과와 함께 `후속 대기`로 기록하고, 별도 인가를 받은 성능 계획이
상태와 full perf 증거를 이어서 소유한다.

### 6.1 Python lane

Python은 `zlink` package projection이 공개 표면을 소유하고 `_runtime`과 `_native`가 구현을 숨긴다.
[Python 바인딩 구현 청사진의 정적 타입 원칙](../../../../bindings/doc/spec/python/README.ko.md#정적-타입-원칙)을
이 lane의 필수 구현·리뷰 기준으로 적용한다. 타입 검사를 통과하기 위한 별도 우회 API를 만들지 않고,
Python 관례에 맞는 호출 형태와 엄격한 타입 정보를 함께 제공한다.

- `contracts/service/`에 MeshNode, dispatch claim·batch, Spot와 Actor 계약을 배치한다.
- `zlink.__init__`과 `__all__`은 공개 계약만 export하고 `_runtime` concrete type을 노출하지 않는다.
- 최소 지원 버전은 Python 3.12로 고정한다. `pyproject.toml`의 `requires-python`, wheel metadata,
  정적 타입 검사기의 대상 버전과 CI의 최소 버전을 함께 갱신한다.
- 공개 함수, 메서드, 속성, 팩토리, 콜백과 비동기 문맥 관리자의 모든 인자와 반환 타입을 선언하고,
  공개 계약에 암시적인 `Any`가 남지 않게 한다.
- `Protocol`은 구조적 계약이 필요한 공개 역할에 사용하고, 실제 런타임 타입 확인이 필요한 경우에만
  `runtime_checkable`을 사용한다. 타입 annotation이 native 입력 검증을 대신하게 하지 않는다.
- `_native/ffi.py`와 compiled private bridge의 symbol, argument, callback와 struct layout을 최신 header와
  자동 대조한다.
- blocking native wait는 GIL과 Python event loop를 막지 않게 기존 native wait 경계에서 처리한다.
- claim, receive batch, retained message와 reply token의 native 수명을 Python object가 안전하게 소유한다.
- wheel과 sdist에 같은 Core native version, 필요한 비공개 extension과 `py.typed`가 포함되는지 검사한다.
- `./tests/run_tests.sh`, sample 전체, 엄격한 정적 타입 검사와 패키지 설치 후 같은 공개 API 및 타입
  추론 검사를 실행한다. perf는 §2.3의 C 기준 대응 review와 single·multi smoke까지만 실행한다.

### 6.2 Go lane

Go는 `zlink.systems/zlink/contracts`와 합의된 root projection만 공개하고 cgo 구현은 `internal/`에 둔다.

- bundled Core header와 cgo 선언을 최신 header에 맞추고 struct size·alignment를 C fixture와 비교한다.
- MeshNode, claim·batch, Spot와 Actor를 Go의 return-based 오류 계약으로 투영한다.
- native callback에서 Go callback을 직접 오래 실행하지 않고 기존 Go-managed 전달 경계를 사용한다.
- `runtime.SetFinalizer`에 correctness를 맡기지 않고 명시적인 idempotent `Close`가 native handle을
  정리한다.
- sample, perf와 consumer test가 `internal/`이나 cgo bridge를 직접 import하지 않게 한다.
- `go test ./...`, `./tests/run_tests.sh`, `go vet ./...`, sample 전체와 깨끗한 module consumer를
  실행한다. perf는 §2.3의 C 기준 대응 review와 single·multi smoke까지만 실행한다.

Go source에는 package version 상수를 새로 만들지 않는다. 최종 version은 검증된 Core base version과
`go/vX.Y.Z` tag가 소유하며, 외부 tag 생성은 이 계획의 범위가 아니다.

### 6.3 Rust lane

Rust는 `lib.rs`와 `contracts/`가 공개 계약을 소유하고 `runtime/native/ffi.rs`를 외부에 노출하지 않는다.

- FFI symbol, `repr(C)` type, discriminant, size와 alignment를 최신 Core header와 대조한다.
- MeshNode, claim·batch, Spot와 Actor resource를 safe RAII wrapper와 `Result<T, E>`로 제공한다.
- native handle, claim과 retained message의 소유자는 하나여야 하며 `Drop`은 중복 release를 만들지
  않는다.
- `Send`와 `Sync`는 native thread-safety 계약이 증명된 type에만 구현한다.
- callback과 request completion에서 panic이 FFI 경계를 넘어가지 않게 하고 one-shot token 재사용을
  type 또는 runtime 검증으로 거부한다.
- release workflow는 실제 workspace에 존재하는 crate만 검증하고, 존재하지 않는 codec crate를 publish
  대상으로 선언하지 않는다.
- `cargo test --workspace --all-targets`, `./tests/run_tests.sh`, `cargo fmt --check`, sample 전체와
  `cargo package` 결과를 설치한 깨끗한 consumer를 실행한다. perf는 §2.3의 C 기준 대응 review와
  single·multi smoke까지만 실행한다.

### 6.4 누락 방지 필수 확인표

이 표는 진행 상태를 기록하는 별도 checklist가 아니다. 각 lane 담당자는 작업을 시작할 때 모든 행을
실행 진행표의 담당 행 또는 그 행이 가리키는 검증 log에 복사하고, 고정된 snapshot에서 확인한 파일,
명령과 결과를 채운다. 이 문서에는 상태 표시를 추가하지 않는다.

행을 삭제하거나 빈칸으로 남길 수 없다. 적용할 수 없는 항목은 `해당 없음`만 적지 말고 그 판단을
뒷받침하는 정식 spec, public header 또는 package 구조를 함께 기록한다. Core candidate가 바뀌면 입력
고정부터 영향받은 행을 다시 확인한다.

| 분류 | 반드시 확인할 내용 | 필수 증거 |
|---|---|---|
| Core 입력 | spec·public header·symbol·layout·runtime이 같은 candidate인가 | commit 또는 snapshot hash, spec·header hash, runtime 경로·version·SHA-256 |
| 공개 계약 | Core 기능마다 Python·Go·Rust의 정확한 공개 이름, 인자, 반환값과 오류가 대응하는가 | 기능별 contract inventory와 실제 export 위치 |
| 제거 계약 | `SpotNode`, route bridge, 이전 dispatch와 `*_part` 공개 wrapper가 남지 않았는가 | 언어별 compile-fail 또는 import-fail test와 scoped no-hit 결과 |
| ABI·FFI | enum 값, struct 크기·정렬, callback과 symbol 선언이 header와 같은가 | C fixture 또는 자동 대조 결과와 실행한 명령 |
| 소유권·수명 | claim, batch, retained message, reply token, handle과 callback 인자의 소유자가 명확한가 | 정상·중복 close, one-shot 재사용, callback 이후 접근 test |
| 동시성·종료 | callback thread, blocking wait, shutdown·drain과 close 재진입이 계약과 같은가 | thread·shutdown·timeout 회귀 test 결과 |
| Python 타입 | Python 3.12, 공개 타입 완전성, `py.typed`와 엄격한 외부 프로젝트 타입 검사가 충족되는가 | type checker 명령·결과, wheel·sdist 내용과 설치 후 검사 결과 |
| 언어 test·sample | 언어별 전체 test와 sample이 공개 API만 사용하고 통과하는가 | 표준 runner 명령, 종료 코드와 결과 요약 |
| perf C 대응 | single·multi pattern, transport, phase, handshake, metric, 상태와 runner 책임이 C 기준과 같은가 | C source·정책·언어별 source를 연결한 대응표와 차이 0개 review |
| perf 정책 | full-run gate를 제외한 `PERF_POLICY.md`와 single·multi·Spot 정책을 지키며 binding 공개 API만 사용하는가 | 금지 항목 검색 결과, 정책 항목별 검토 기록과 PGR-PERF 인수 정보 |
| perf smoke | 공식 entrypoint에서 전체 pattern·기본 transport·64B single/multi가 완료되는가 | 두 smoke 명령, 결과 파일, `status=complete`와 `fail` 0개 증거 |
| package | source tree 우회 없이 설치한 package가 같은 Core runtime과 공개 API를 사용하며 지원 platform 검증이 끝났는가 | platform별 artifact checksum과 깨끗한 consumer build·실행 결과 |
| 독립 review | 인가받은 reviewer가 고정 snapshot의 계약·설계·정리 축을 모두 검토했는가 | reviewer ID, snapshot hash, finding 0개와 지정된 clean 문구 |

담당자는 표의 결과를 근거로 구현 순서를 자율적으로 조정할 수 있다. 그러나 완료 보고는 모든 행의
증거가 있고 실행 진행표의 해당 gate가 닫힌 뒤에만 할 수 있다.

## 7. package와 공통 E2E smoke

local package 실행 스크립트는 bindings 디렉터리에 새 wrapper를 만들지 않고
[`scripts/local-package/`](../../../../scripts/local-package/README.ko.md) 아래에 둔다. 세 언어에는 다음
산출물과 consumer가 필요하다.

| 언어 | local 산출물 | clean consumer 기준 |
|---|---|---|
| Python | wheel과 sdist | Python 3.12 이상의 새 virtual environment에 wheel을 설치하고 source tree를 `PYTHONPATH`에 넣지 않은 상태에서 공개 API 실행과 엄격한 외부 프로젝트 타입 검사 |
| Go | 검증할 module source archive 또는 고정 snapshot | 빈 module에서 작업 tree 밖에 푼 검증 snapshot만 `replace` 대상으로 사용하고 public package import |
| Rust | `cargo package`가 만든 crate | 빈 Cargo project가 local crate artifact만 dependency로 사용 |

세 consumer는 package에 포함된 native runtime의 `zlink_version`, 파일 이름과 SHA-256을 먼저 검증한다.
Linux는 SONAME, macOS는 install name, Windows는 DLL 파일 선택이 package metadata와 loader 계약에
맞는지도 확인한다. Python·Rust의 source project reference, Go 작업 tree의 직접 참조, repository 상대
native 경로와 `core/build` fallback이 발견되면 package smoke는 실패해야 한다.

### 7.1 지원 platform 검증

현재 세 bindings가 native payload 디렉터리와 loader에서 선언한 지원 대상은 Linux, macOS와 Windows의
`x86_64`·`aarch64` 조합이다. PGR-00은 최신 package 구조에서 이 목록을 다시 고정하고, 지원 대상을
줄이려면 누락 artifact를 조용히 제외하지 말고 package 계약 변경으로 분리해 review를 받는다.

| platform | 필수 검증 |
|---|---|
| Linux `x86_64`, `aarch64` | native host 또는 해당 architecture runner에서 package 설치, native load, `zlink_version`과 최소 public send/request smoke |
| macOS `x86_64`, `aarch64` | native host 또는 해당 architecture runner에서 package 설치, native load, `zlink_version`과 최소 public send/request smoke |
| Windows `x86_64`, `aarch64` | native host 또는 해당 architecture runner에서 package 설치, native load, `zlink_version`과 최소 public send/request smoke |

모든 조합에서 archive 안의 payload 경로·파일 이름, Core version, SHA-256과 loader 선택이 일치해야 한다.
cross-build와 archive 내용 검사만으로 native 실행을 대신하지 않는다. 사용할 수 있는 native runner가 없는
조합은 완료로 표시하지 않고 실행 진행표에 차단 사유와 필요한 환경을 기록한다. 언어별 전체 test,
sample, 공통 E2E와 perf smoke는 주 개발 platform인 Linux `x86_64`에서 수행하고, 다른 platform에서는
위 표의 package·loader·최소 public 동작을 필수 gate로 사용한다.

### 7.2 비배포 workflow 검증

`.github/workflows/bindings-release.yml`의 Python·Go·Rust target은 standard test runner와 package
consumer를 실행해야 한다. Python에서 version·enum test 일부만 실행하거나 Go·Rust에서 package consumer를
생략한 상태는 release 검증 완료로 인정하지 않는다.

현재 workflow는 모든 target에 앞서 GitHub의 exact Core release tag를 요구하므로 작업 중인 미배포 Core
candidate 검증에 그대로 사용하지 않는다. PGR-02에서 다음 두 경로를 분리한다.

- candidate 경로는 `scripts/local-package/` 아래의 비배포 진입점 또는 동등한 CI job을 사용한다. 고정된
  Core commit과 manifest에서 package를 만들고, GitHub Release가 없어도 provenance·artifact·clean
  consumer gate를 모두 실행한다.
- release 경로는 기존 exact Core release tag 검증을 유지한다. 수동 검증 입력은 실제 workflow 이름인
  `target=python|go|rust`, `version=<binding package version>`, `core_version=<manifest Core version>`,
  `create_release=false`, `publish_registry=false`를 사용하며, 존재하지 않는 `publish` 입력을 만들지 않는다.

credential이 없다는 이유로 candidate 또는 release 경로의 필수 검증을 건너뛰지 않는다. 외부 registry
게시와 GitHub Release 생성만 false로 유지한다.

공통 E2E smoke는 각 언어의 process model과 public API만 사용해 다음 동작을 확인한다.

1. 두 process MeshNode의 start, peer admission과 orderly shutdown
2. RID direct send/request와 같은 channel의 node 선택
3. Spot direct send/request와 Logical Multicast의 node별 한 번 전달
4. metadata snapshot, malformed metadata 거부와 1024-byte 경계
5. ready batch에서 claim 인수, receive batch reset·retain과 중복 반환 방지
6. request completion과 one-shot reply token 재사용 거부
7. Actor join·leave와 지원되는 STREAM session 연동
8. backpressure, peer 재연결, drain과 shutdown 뒤 native handle·child process 잔존 0개

언어 제약 때문에 API 모양이 달라도 검증하는 결과를 줄이지 않는다. flaky test를 반복 실행으로 숨기지
않고 재현 조건과 마지막 진행 지점을 기록한다.

## 8. 문서 승격과 정리

구현과 contract test가 확정되기 전에는 draft 내용을 bindings 정식 spec에 섞지 않는다. 세 lane의 구현과
package smoke가 통과하면 다음 순서로 정식 문서를 갱신한다.

1. bindings 공통 정책에서 제거된 `SpotNode`, route bridge와 이전 dispatch 계약을 최신 Core service
   의미로 교체한다.
2. Python, Go와 Rust 문서에 실제 public signature, 오류와 ownership 계약을 반영한다.
3. 각 package의 API reference, README, sample와 코드 주석을 실제 export와 맞춘다.
4. 구현 전 draft의 내용을 정식 문서에 나누어 반영한 뒤 draft를 제거하거나 남은 미구현 범위만 유지한다.
5. 정식 spec, source, test, sample와 package에서 제거 이름에 대한 scoped no-hit를 실행한다.

정식 spec은 현재 확정된 계약만 설명하며 작업 진행률, Core version 변경 기록과 review log를 포함하지
않는다. 그 정보는 실행 진행표와 `log/` 증거가 소유한다.

## 9. review와 완료 gate

각 언어 lane은 변경되지 않는 snapshot에서 다음 세 축을 독립적으로 검토한다. 각 reviewer는 §1.1에
따라 사용자 또는 coordinator가 명시적으로 인가하고, 실행 진행표에서 해당 review ID와 snapshot을
배정해야 한다. 단순히 review를 실행할 수 있거나 다른 작업의 reviewer라는 이유로 이 범위의 review
권한을 추론하지 않는다.

| 축 | 검토 질문 |
|---|---|
| I1 계약 일치 | Core 정식 spec·header와 언어별 exact interface의 기능, 오류, ownership과 thread-safety가 빠짐없이 구현됐는가 |
| I2 POSD·DDD·perf 구조 | FFI, lifecycle, claim·batch와 callback 복잡성이 runtime 안에 숨겨지고 public surface가 단순하며 perf가 C 기준과 `doc/perf`의 책임 분리·측정 의미를 지키는가 |
| I3 정리 완결성 | 구 API, compatibility shim, 죽은 source·test·sample, stale package file과 생성물이 남지 않았는가 |

finding을 수정하면 해당 lane의 I1·I2·I3 전체를 새 snapshot에서 다시 검토한다. 세 lane이 각각 clean에
도달한 뒤 같은 Core candidate로 package와 공통 E2E를 다시 실행하고 통합 review를 수행한다.
인가받은 두 reviewer의 결과만 clean 판정에 포함하며 reviewer별 ID, 대상 snapshot hash, 세 축 판정과
최종 문구를 실행 진행표의 담당 행과 review log에 기록한다.

다음 조건을 모두 만족해야 작업이 완료된다.

- 최종 Core candidate의 commit, spec·header hash, symbol·layout manifest와 runtime SHA-256이 고정됐다.
- 최종 Core candidate의 version이 실행 진행표가 지정한 목표 version과 일치한다.
- Python, Go와 Rust가 같은 Core runtime을 사용하며 package 내부 version과 hash가 증거와 일치한다.
- 세 언어의 public API가 최신 Core service 계약을 빠짐없이 제공하고 언어별 오류·수명 관례를 따른다.
- Python 패키지가 Python 3.12 이상을 정확히 선언하고, 공개 타입에 암시적인 `Any`가 없으며,
  `py.typed`가 포함된 설치 패키지를 사용하는 외부 프로젝트의 엄격한 타입 검사가 통과한다.
- source와 package에서 제거 대상 이름, compatibility wrapper와 private API 우회가 발견되지 않는다.
- 각 언어의 전체 test, sample, package consumer와 공통 E2E smoke가 통과한다.
- 세 bindings perf의 C 기준 대응표와 full-run gate를 제외한 `doc/perf` 정책 review가 clean이며, 공식
  entrypoint의 single·multi 64B 전체 pattern smoke가 `status=complete`로 끝나고 `fail`이 없다.
- PGR-PERF가 인수할 최종 manifest, C 대응표, smoke 결과와 미실행 full perf 목록이 실행 진행표에
  `후속 대기`로 기록됐다.
- Linux, macOS와 Windows의 지원 architecture별 package·native loader·최소 public 동작 검증이
  §7.1 기준으로 통과했다.
- bindings 정식 spec, API reference, README와 sample이 실제 package export와 일치한다.
- 최종 Core version 변경 뒤 세 lane 전체와 package smoke를 다시 검증했다.
- 두 독립 reviewer가 각 lane과 통합 snapshot의 I1·I2·I3을 모두 clean으로 판정했다.
- 외부 registry, release와 tag를 만들지 않았으며 skipped required test와 open finding이 0개다.
