# RouteMesh 10.0.0 실행 진행표

## 0. 문서 상태와 사용 방법

이 문서는
[`RouteMesh 메시징 통합 계획`](./framework-route-mesh-messaging-consolidation.ko.md)과
[`MeshNode Core 공개 API 전환 검토`](./mesh-node-core-api-review.ko.md),
[`MeshNode·Spot·Actor framework 우선 dispatch 설계`](./mesh-node-framework-dispatch-design.ko.md)를 실제로
실행하기 위한 진행표다. 설계 근거와 목표 의미는 세 계획 문서를 따르고, 작업 순서·완료 상태·검증 증거는 이 문서를 기준으로
관리한다.

대상 독자는 이 전환을 실행하고 완료 증거를 승인하는 개발자와 reviewer다. 이 문서는 “지금 어떤 작업이
가능하며, 다음 stage로 넘어가기 위해 어떤 검증과 증거가 필요한가?”에 답한다. 진행표 자체와 이 진행표가
지시하는 모든 문서 작업은
[`기술문서 작성 원칙`](../../../../doc/principal/documentation/documentation-principles.ko.md)을 따른다.
설계 설명은 세 원본 계획 문서에 두고 이 문서에는 순서, 상태, gate와 증거만 기록한다.

이 파일은 `framework/doc/plan/v10.0/` 아래에서 **진행 상태를 소유하는 유일한 문서**다. 모든 작업자는
stage·항목의 상태, 담당 중인 작업, open finding과 완료 증거를 이 파일에만 갱신한다. 다른 plan 문서는
설계 결정, 초기 gap, inventory 또는 검증 방법만 기록하며 checkbox, 진행 요약이나 별도 상태 열로 현재
진행 상황을 복제하지 않는다. 병렬 작업자는 서로 다른 stage를 수행하더라도 이 표에서 각자 담당 행만
수정한다.

이번 변경은 Core와 bindings 10.0.0 계약을 한 번에 적용한다. 폐기 대상 alias, deprecated wrapper와
두 runtime mode를 함께 유지하지 않는다. 원칙적으로 각 stage는 앞 stage의 gate를 통과한 뒤 시작한다.
다만 S1 Core 계약 기준선이 승인된 뒤에는 Core 구현 red gate와 S4 작업을 S2·S3 문서 작업과 병렬로
진행할 수 있다. 이 병렬 작업의 source나 test는 S2·S3 계약의 근거가 아니며, S3에서 Core 계약이 바뀌면
S4가 해당 변경을 다시 반영하고 전체 검증을 실행해야 한다. S5 revision 동결은 S3가 완료되기 전에는
시작하지 않는다.

진행 상태는 다음 값만 사용한다.

| 상태 | 의미 |
|---|---|
| `미착수` | 선행 조건이 충족되지 않았거나 아직 시작하지 않음 |
| `진행 중` | 담당 범위에서 작업 또는 검증을 수행 중 |
| `리뷰 중` | 구현 변경을 멈추고 고정된 기준 revision을 독립 검토 중 |
| `수정 중` | review finding을 반영하고 관련 검증을 다시 수행 중 |
| `차단` | 외부 권한, 배포 환경 또는 확정되지 않은 계약 때문에 진행할 수 없음 |
| `완료` | 모든 checklist, 검증, 두 독립 리뷰와 증거가 충족됨 |

체크박스만 바꾸어 완료로 판정하지 않는다. 각 행의 `증거`에는 commit SHA, 명령과 결과, package
version, GitHub Actions run URL 또는 review log 경로 가운데 해당하는 정보를 기록한다.

### 0.1 정식 스펙 단방향 참조 규칙

S3에서 정식 스펙이 승인된 뒤 구현 계약은 정식 스펙만 소유한다. S4 이후 행의 완료 조건은 구현 작업과
검증 방법을 설명할 뿐 공개 동작을 다시 정의하지 않는다. 완료 조건의 문장과 아래 정식 스펙이 다르면
구현을 멈추고 S1 또는 S2와 S3 리뷰를 다시 연다. 정식 스펙에서는 이 임시 계획 디렉토리를 참조하지
않는다.

| 구현 항목 | 정식 계약 주소 |
|---|---|
| S4-01~S4-07 MeshNode lifecycle·peer·selection | [Core MeshNode](../../../../core/doc/spec/core/service/01-mesh-node.ko.md), [Core Dispatch](../../../../core/doc/spec/core/service/02-dispatch.ko.md) |
| S4-08~S4-14 message·claim·Logical Multicast·Spot | [Core message](../../../../core/doc/spec/core/02-message.ko.md), [Core Dispatch](../../../../core/doc/spec/core/service/02-dispatch.ko.md), [Core Spot](../../../../core/doc/spec/core/service/03-spot.ko.md) |
| S4-15~S4-15A Actor·STREAM transfer | [Core Actor](../../../../core/doc/spec/core/service/04-actor.ko.md), [Core STREAM session](../../../../core/doc/spec/core/service/05-stream-session.ko.md) |
| S4-16~S4-22A 제거·polling·관측·오류 | [Core public contract governance](../../../../core/doc/spec/core/00-public-contract-governance.ko.md), [polling](../../../../core/doc/spec/core/06-polling.ko.md), [monitoring](../../../../core/doc/spec/core/07-monitoring.ko.md), [errno](../../../../core/doc/spec/core/04-errno-map.ko.md), [raw socket](../../../../core/doc/spec/core/socket/README.ko.md) |
| S4-22B~S6 release와 ABI | [Core errors·version](../../../../core/doc/spec/core/03-errors.ko.md), [Core public contract governance](../../../../core/doc/spec/core/00-public-contract-governance.ko.md) |
| S7 bindings | 위 Core 정식 계약 전체와 [Core service 목차](../../../../core/doc/spec/core/service/README.ko.md) |
| S8 `.NET` framework | [Framework 공통 계약](../../framework/spec/README.ko.md), [server 계약](../../framework/spec/server/21-mesh-node.ko.md), [.NET exact interface](../../framework/spec/server/languages/dotnet/README.ko.md) |
| S8 location·transfer | [Location Runtime](../../framework/spec/server/40-location-runtime.ko.md), [Redis extension](../../framework/spec/server/41-location-store-redis.ko.md), [.NET Location Store](../../framework/spec/server/languages/dotnet/06-location-store.ko.md) |
| S8 monitoring·drain | [Runtime monitoring](../../framework/spec/server/50-runtime-monitoring.ko.md), [message flow](../../framework/spec/server/52-message-flow-tracing.ko.md), [graceful drain](../../framework/spec/server/54-graceful-drain-handoff.ko.md), [.NET RouteMesh runtime](../../framework/spec/server/languages/dotnet/05-route-mesh.ko.md) |
| S9 C++ | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/cpp/` exact interface |
| S9 Java/Kotlin | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/java/`, `languages/kotlin/` exact interface |
| S9 Node.js | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/node/` exact interface |

S2에서 다섯 언어의 exact interface를 공통 10.0.0 계약의 언어별 표현으로 정식 문서에 고정하고,
S3에서 공통·server 계약 및 E2E·sample 범위와 함께 독립 리뷰한다. S9의 각 lane은 이 reviewed exact
interface와 실제 source·package 사이의 차이를 red gate로 만들고 구현한다.
Plan 고유 항목인 삭제 no-hit, test 명령, package provenance와 review 증거는 이 진행표가 계속 소유한다.

### 0.2 에이전트 작업 지시 계약

10.0.0 작업을 에이전트에게 맡길 때는 이 파일과 담당 stage 또는 ID 범위만 전달한다. 작업 지시에 다른
plan 문서의 checklist를 다시 복사하지 않는다. 에이전트는 이 파일을 실행 진입점으로 사용하고, 담당
행이 연결한 정식 spec과 검증 자료만 추가로 읽는다.

최소 작업 지시는 다음 형식이다.

```text
framework/doc/plan/v10.0/route-mesh-10.0.0-execution-ledger.ko.md를 읽고
<stage 또는 ID 범위>를 진행한다. 진행 상태와 증거는 이 ledger에만 갱신한다.
```

에이전트는 작업을 시작할 때 다음 순서를 지킨다.

1. 루트 `AGENTS.md`와 이 문서의 §0~§3을 읽는다.
2. 담당 stage의 선행 조건, 항목별 완료 조건과 §0.1의 정식 spec 주소를 읽는다.
3. 이미 다른 작업자가 `진행 중` 또는 `리뷰 중`으로 표시한 행을 임의로 가져오지 않는다.
4. 담당 행만 `진행 중`으로 바꾸고 증거 칸에 담당 범위와 시작 기준 revision을 기록한다.
5. 설계·inventory·test matrix는 입력 자료로만 사용하고 그 문서에 현재 진행 상태를 기록하지 않는다.

작업을 마칠 때는 다음 순서를 지킨다.

1. 담당 행의 완료 조건과 해당 stage gate를 실제 명령·test·package·review 증거로 확인한다.
2. 완료하지 못한 항목은 `진행 중` 또는 `차단`으로 유지하고 정확한 남은 조건을 증거 칸에 기록한다.
3. 완료한 행만 `완료`로 바꾸며 log, hash, run URL 또는 파일 경로를 증거 칸에 남긴다.
4. 다른 작업자가 병렬로 갱신한 행을 덮어쓰지 않도록 저장 직전에 이 파일을 다시 읽는다.
5. review log와 finding ledger는 `log/`에 둘 수 있지만 stage·항목 상태의 정본은 이 파일 하나다.

작업 지시에서 담당 ID를 생략한 경우 에이전트는 현재 stage의 `미착수` 행 가운데 선행 조건이 충족된
첫 항목을 제안할 수는 있지만, 여러 lane의 소유권이 달라지는 작업을 임의로 시작하지 않는다. S3와 S4처럼
명시적으로 허용한 병렬 실행도 각자 담당 행만 갱신한다.

### 0.3 POSD 기반 고성능 시스템 원칙

RouteMesh 10.0.0의 Core, bindings와 framework는 POSD 철학을 설계 기준으로 삼는 고성능 시스템으로
작성한다. 기능 통과만으로 완료하지 않는다. 공개 interface는 작고 단순하게 유지하고 routing, codec,
queue, retry, peer admission과 lifecycle 복잡성은 책임을 소유한 깊은 모듈 안에 둔다. 같은 설계 지식이
여러 계층이나 언어에 반복되면 정보 누출로 판단하며, 호출자에게 transport detail이나 내부 policy를
추가로 요구하는 방식으로 성능 문제를 우회하지 않는다.

성능은 막연한 목표나 특정 구현 기법의 강제가 아니라 재현 가능한 검증 계약이다. 다음 기준을 모든 구현
stage와 I2 리뷰에 적용한다.

- 불필요한 message encode·decode, payload copy, allocation과 언어·C API callback 왕복을 추가하지 않는다.
- immutable message reference, batch·claim, ready index와 언어별 native scheduler를 사용해 hot path의
  queue contention과 FFI 경계를 줄인다. 다만 실제 측정 없이 복잡한 최적화를 public contract로 노출하지
  않는다.
- connection 수, direct·select-one·Logical Multicast, mixed traffic, reconnect와 shutdown을 실제 부하로
  측정하고 throughput, latency, memory와 queue/backpressure 결과를 기준선과 비교한다.
- benchmark는 실제 배포에 사용하는 runtime과 package를 대상으로 실행한다. 오래된 build, source-only
  결과나 test 전용 fast path로 성능 gate를 통과시키지 않는다.
- 성능을 위해 POSD 책임 경계를 깨지 않는다. 성능과 구조가 충돌하면 최소 두 설계안을 비교하고,
  benchmark와 호출자 복잡도 증거를 함께 기록한 뒤 깊은 모듈을 유지하는 안을 선택한다.

S4, S5와 각 bindings·framework 구현 및 리뷰 stage는 기능·회귀 검증과 별도로 성능·resource 증거를
남긴다. 요구한 benchmark 또는 성능 gate가 없거나, 의미 있는 회귀를 원인 분석 없이 승인한 상태에서는
해당 stage를 `완료`로 바꾸지 않는다.

## 1. 고정 실행 순서

| Stage | 작업 | 병렬 실행 | 완료 판정 |
|---|---|---:|---|
| **S0** | Core 정식 spec 적용 범위와 결정 검증 | 아니요 | 구현 전에 정할 항목이 0개이고 정식 owner 문서와 gap 범위가 고정됨 |
| **S1** | Core 10.0.0 정식 spec 작성 | 아니요 | Core 목표 계약 전체가 reviewed 정식 spec에 고정됨 |
| **S2** | framework 정식 spec 변경과 E2E·sample 영향 검토 | 아니요 | 공통·server 계약, 다섯 언어 exact interface, 공통·언어별 E2E·sample 문서와 public 예제 영향이 고정됨 |
| **S3** | 문서 독립 리뷰와 수정 반복 | 리뷰 2개만 병렬 | 두 리뷰어 모두 `DOC REVIEW CLEAN` |
| **S4** | Core 구현·제거 정리와 정식 spec 일치 | 아니요 | 기능·삭제·회귀·성능, header-spec 일치와 구현 후 internals gate 통과 |
| **S5** | Core 구현 3축 독립 리뷰와 수정 반복 | 리뷰 2개만 병렬 | 두 리뷰어의 I1 계약 일치·I2 POSD/DDD·I3 정리 완결성이 모두 clean이고 `CORE REVIEW CLEAN` |
| **S6** | Core 10.0.0 release-candidate GitHub Actions build와 pre-release 배포 | workflow 병렬 허용 | RC native artifact와 local Conan 검증 완료. stable tag·remote publish 없음 |
| **S7** | bindings 적용, 3축 독립 리뷰와 local package E2E smoke | 언어별 제한적 병렬 | 모든 bindings local package 검증과 두 리뷰어의 I1·I2·I3 clean 및 `BINDINGS REVIEW CLEAN` |
| **S8** | `.NET framework`, sample과 E2E 적용 및 3축 리뷰 | 리뷰 2개만 병렬 | 두 리뷰어의 I1·I2·I3가 모두 clean이고 `DOTNET REVIEW CLEAN` |
| **S9** | C++, Java/Kotlin, Node.js framework 적용 | 세 lane 병렬 | 세 lane 구현과 검증 완료 |
| **S10** | 세 언어 lane별 3축 독립 리뷰와 수정 반복 | 세 lane 병렬 | lane마다 두 리뷰어의 I1·I2·I3와 언어별 clean 문구가 모두 clean |
| **S11** | Core stable·bindings 외부 배포, 전체 3축 최종 검토와 종료 | 배포 후 리뷰 2개만 병렬 | stable package smoke, 두 리뷰어의 I1·I2·I3 clean 및 `FINAL REVIEW CLEAN` |

S3, S5, S7, S8, S10과 S11의 review gate를 생략하거나 다음 stage에서 대신 처리하지 않는다. S2·S3와
병렬로 시작한 S4 변경은 S3를 대신하지 않으며 S3 finding이 반영된 정식 계약에 다시 맞춰야 한다.

## 2. 독립 리뷰 운영 규칙

### 2.1 리뷰어

| ID | 리뷰어 | 역할 |
|---|---|---|
| **R1** | Codex agent | 저장소의 실제 spec, source, test, package와 실행 증거를 독립 검토 |
| **R2** | Claude Sonnet 모델 | 같은 고정 revision과 동일한 review manifest를 독립 검토 |

R1과 R2는 서로의 finding을 보기 전에 첫 검토를 완료한다. 두 결과가 나온 뒤 coordinator가 중복을
합치고 하나의 finding ledger를 만든다. 한 리뷰어의 clean 판정으로 다른 리뷰어의 검토를 대신하지
않는다.

모든 리뷰는 Codex agent와 Claude Sonnet 모델이 같은 frozen scope를 각각 독립 검토한다. 어느 한쪽
리뷰 결과로 문서·코드·테스트·설정 또는 증거가 하나라도 수정되면 새 revision을 고정하고 두 리뷰를
모두 다시 실행한다. 두 리뷰어가 모두 해당 stage의 exact clean 문구를 남겨야 gate를 통과한다.

S3은 정식 계약 문서의 완전성·일관성·구현 가능성을 검토하는 **문서 리뷰**다. S5, S7, S8, S10과
S11은 frozen spec을 기준으로 실제 source·test·package·artifact를 검토하는 **구현 리뷰**다. S3의
`DOC REVIEW CLEAN`은 아래 구현 리뷰 축의 판정을 대신하지 않으며, 구현 리뷰 결과도 S3 문서 리뷰를
대신하지 않는다.

모든 구현 리뷰는 다음 세 축을 서로 합치지 않고 독립적으로 판정한다.

| 축 | 필수 질문 | 축별 결과 계약 |
|---|---|---|
| **I1 계약 구현 일치** | frozen spec에서 누락되거나 잘못 구현된 항목, 관찰 가능한 동작·오류·수명·동시성 불일치가 없는가? | finding ID 또는 `없음`, `file:line`·test·package·실행 log 증거, `CLEAN` 또는 `NOT CLEAN` |
| **I2 POSD·DDD 리팩터링** | 깊은 모듈·정보 은닉·복잡성을 아래로 이동하는 원칙과 도메인 책임 경계에 비추어 의미 있는 리팩터링 요소가 남아 있지 않은가? | finding ID 또는 `없음`, 위험 신호·대안 비교·source 근거, `CLEAN` 또는 `NOT CLEAN` |
| **I3 정리 완결성** | 불필요하거나 죽은 code·file·test·build target·generated artifact, alias·adapter·forwarder 같은 호환 잔재가 남아 있지 않은가? | finding ID 또는 `없음`, scoped no-hit·package 내용·source 근거, `CLEAN` 또는 `NOT CLEAN` |

각 리뷰어는 세 축마다 finding, evidence와 `CLEAN`/`NOT CLEAN` 판정을 별도로 남긴다. `I2`의
POSD·DDD 한 줄이나 전체 stage clean 문구로 `I1` 또는 `I3` 판정을 갈음할 수 없다. 어느 축의 finding을
수정하더라도 새 revision에서 Codex agent와 Claude Sonnet이 세 축 전체를 다시 검토한다. 두 리뷰어의
세 축이 모두 `CLEAN`이고 같은 frozen revision에 대한 stage exact clean 문구가 모두 있어야 구현 review
gate를 통과한다.

### 2.2 review manifest

모든 review iteration은 다음 정보를 먼저 고정한다.

- stage와 iteration 번호
- review 대상 commit SHA와 working tree diff 범위
- reviewer provider, 실제 model identifier와 version
- invocation 또는 session ID, 시작·종료 시각과 process 종료 상태
- 읽어야 하는 정식 spec과 계획 문서
- 검토할 source, test, E2E, sample, package와 workflow 범위
- 제거 API와 금지 구현의 검색 문자열
- 실행해야 하는 검증 명령과 기존 결과 위치
- 직전 iteration finding과 반영 commit
- 리뷰어가 수정할 수 없는 file scope
- reviewer raw output의 보존 위치와 SHA-256 checksum

review manifest와 결과는 다음 경로 아래에 stage별로 보관한다.

`framework/doc/plan/v10.0/log/<stage>/<iteration>/`

각 iteration은 `manifest.ko.md`, `codex-review.ko.md`, `claude-sonnet-review.ko.md`,
`finding-ledger.ko.md`와 `verification.ko.md`를 가진다. review 파일은 append-only 증거로 취급하고 이전
iteration 결과를 덮어쓰지 않는다.

coordinator가 정리한 finding ledger는 reviewer 원본을 대신하지 않는다. provider/model, invocation,
대상 SHA, raw output와 checksum 가운데 하나라도 없거나 process가 정상 종료하지 않았으면 해당 reviewer
결과는 `차단`으로 기록하고 clean 문구를 인정하지 않는다.

### 2.3 finding 처리

| 필드 | 기록 내용 |
|---|---|
| Finding ID | stage, reviewer와 순번을 포함한 고유 ID |
| Severity | blocker, high, medium, low |
| 근거 | 실제 `file:line`, symbol, package entry 또는 실행 결과 |
| 위반 계약 | 해당 Core/framework spec 절 또는 완료 gate |
| 수정 범위 | code, test, spec, sample, package 또는 workflow |
| 검증 | finding을 재현하는 red gate와 수정 후 green 결과 |
| 상태 | open, fixing, resolved, rejected |
| 종료 근거 | 수정 commit과 재리뷰 iteration |

`resolved`는 구현자가 정하는 상태가 아니다. 수정과 검증 뒤 다음 iteration의 독립 리뷰에서 같은
문제가 해소되었음을 확인해야 한다. `rejected`는 구체적인 계약 근거와 두 리뷰어의 재검토가 있어야
한다.

### 2.4 반복 종료 조건

각 review stage는 다음 순서로 반복한다.

1. 같은 revision과 manifest로 R1과 R2를 병렬 실행한다.
2. 결과를 finding ledger에 합치고 모든 finding의 처리 방법을 정한다.
3. 구현 담당자가 finding을 수정한다.
4. 영향받은 unit, contract, E2E, sample과 package 검증을 다시 실행한다.
5. 새 revision을 고정하고 R1과 R2가 전체 scope를 다시 검토한다. 구현 리뷰는 수정된 축만이 아니라
   I1·I2·I3 세 축을 모두 다시 판정한다.
6. 구현 리뷰는 두 리뷰어의 I1·I2·I3가 모두 `CLEAN`이고, 두 리뷰어가 해당 stage의 exact clean 문구를
   남길 때까지 1~5를 반복한다. S3 문서 리뷰는 문서 리뷰 질문 전체와 `DOC REVIEW CLEAN`을 기준으로 한다.

한 리뷰어가 실행되지 않았거나 결과가 중단되면 review gate는 `차단`이다. 시간 부족, finding 개수
감소 또는 test 통과만으로 clean 판정을 추정하지 않는다.

## 3. 전체 진행 현황

| Stage | 상태 | 현재 iteration | open finding | 완료 증거 |
|---|---|---:|---:|---|
| S0 정식 spec 범위·계약 확정 | 완료 | 0 | 0 | `s0-scope-baseline.ko.md`, `log/templates/manifest.ko.md` |
| S1 Core 정식 spec | 완료 | 4 | 0 | `log/s1-core-review/iteration-4/`; 두 리뷰 finding 12건 수정, 자동 검증 통과, 사용자 구현 기준선 승인; 최종 hash `6cd163bf…ea71` |
| S2 framework spec | 완료 | 0 | 0 | 공통·server, 다섯 언어 exact interface, E2E 55·sample 32·runner 96·guide/internals 81 inventory와 자동 검증 통과 |
| S3 문서 review loop | 리뷰 중 | 1 | 0 | `log/s3-document-review/iteration-1/` 범위 동결 준비 |
| S4 Core 구현·정식 spec 일치 | 진행 중 | 0 | 0 | 표면 전환 완료(196 export 정확 일치, 제거 76 no-hit), Phase A(process-local)+Phase B(remote wire: admission·node/channel/spot direct·multicast·actor 전 경로) 구현·2-process contract test 7/7 green, ASAN/UBSAN 전체 suite clean, V8 throughput 게이트 통과. 잔여=transfer fence(Phase C)·TSAN·mesh perf 패턴·internals; S5 동결 전 S3 정합성 재검증 필요 |
| S5 Core review loop | 미착수 | 0 | 0 | - |
| S6 Core release candidate | 미착수 | 0 | 0 | - |
| S7 bindings local package | 미착수 | 0 | 0 | - |
| S8 `.NET framework` | 미착수 | 0 | 0 | - |
| S9 병렬 framework | 미착수 | 0 | 0 | - |
| S10 병렬 review loop | 미착수 | 0 | 0 | - |
| S11 Core stable·bindings 외부 배포·최종 검토 | 미착수 | 0 | 0 | - |

## 4. S0 — Core 정식 spec 범위와 결정 확정

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S0-01 | `AGENTS.md`의 Core 10.0.0 spec-first 예외 확인 | 10.0.0 Core 계약을 정식 spec에 먼저 쓰고 현재 checkout 구현 차이는 임시 실행 추적 문서가 관리하는 순서가 모든 plan에 일치 | 완료 | `AGENTS.md` §Documentation Writing Rules 5, `s0-scope-baseline.ko.md` §3 |
| S0-02 | Core 정식 owner 문서 경로 확정 | governance, MeshNode, Spot, Actor, STREAM session, router, STREAM, polling, monitoring, errno와 errors의 한국어·영문 경로 고정 | 완료 | `s0-scope-baseline.ko.md` §3 |
| S0-03 | Core spec-first 구현 일치 기준 확정 | 정식 spec 선행, gap 기록, 구현·header·test 일치와 구현 후 internals 갱신 기준 고정 | 완료 | `s0-scope-baseline.ko.md` §3 |
| S0-04 | main plan의 결정 반영 검증 | D-01~D-27의 모든 결정 상태가 `확정`이고 Core·framework 문서의 표현이 일치 | 완료 | `s0-scope-baseline.ko.md` §2 |
| S0-05 | MN-D01~MN-D24 확정 | callback, recv metadata, poller, option, query, lifecycle, owner와 Actor transfer의 location authority·Core sealed token·membership epoch 경계가 모두 확정 | 완료 | `mesh-node-core-api-review.ko.md` §14, `s0-scope-baseline.ko.md` §2 |
| S0-06 | Core 정식 spec owner 지도 확정 | MeshNode, Dispatch, Spot, Actor, STREAM session, raw socket, polling, monitoring과 errno의 owner 문서 결정 | 완료 | `s0-scope-baseline.ko.md` §3 |
| S0-07 | framework 정식 spec 변경 지도 확정 | 공통, server와 언어별 interface 대상 파일 확정 | 완료 | `s0-scope-baseline.ko.md` §4 |
| S0-08 | E2E와 sample inventory 추출 기준 확정 | config, sample, runner, package consumer 범위와 증거 형식 확정 | 완료 | `s0-scope-baseline.ko.md` §5.2 |
| S0-09 | 제거 symbol과 금지 구현 검색 목록 고정 | SpotNode mode, bridge, PUB/SUB plane, Core dispatch worker option, remote subject query, channel-dealer event, production in-memory location 등록, alias와 우회 문자열 목록 작성. raw channel metadata는 모든 raw socket의 유지 목록으로 분리 | 완료 | `s0-scope-baseline.ko.md` §6 |
| S0-10 | review manifest template와 clean 문구 고정 | R1/R2가 같은 입력과 종료 문구를 사용 | 완료 | `log/templates/manifest.ko.md` |
| S0-11 | 현재 checkout 기능·API·test·성능 baseline 기록 | 구현 후 비교할 수 있는 재현 가능한 결과 확보 | 완료 | `s0-scope-baseline.ko.md` §8 |
| S0-12 | process-local MeshNode cardinality 확정 | 같은 `MeshName`은 process당 하나이며 중복 등록 오류 고정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-03 |
| S0-13 | multicast direct target과 atomicity 결정 반영 | target channel 직접 선택, 조건부 local 대상과 NODROP admission/commit 확정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-08·D-09·D-14 |
| S0-14 | channel·route handler 보존 mapping 확정 | 두 handler context와 typed·handler-only overload가 MeshNode builder에 대응 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-05 |
| S0-15 | immutable bindings release 시점 확정 | S10 clean 전에는 local package만 사용하고 외부 공개는 S11에서 수행 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-21 |
| S0-16 | service dispatch 설계 확정 | mailbox, ready index, claim, batch, infrastructure completion, shutdown과 Actor transfer 신뢰 경계의 미결정 0개 | 완료 | `mesh-node-framework-dispatch-design.ko.md` §16 |
| S0-17 | 복수 ChannelName membership 확정 | MeshNode 하나의 immutable membership set과 channel-scoped handler 계약 고정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-03·D-07, `mesh-node-framework-dispatch-design.ko.md` FD-12·FD-33 |
| S0-18 | Spot timer backend 확정 | 관리형 언어 platform timer, C/C++ C API timer와 공통 generation/cancel 의미 고정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-22, `mesh-node-framework-dispatch-design.ko.md` FD-24 |
| S0-19 | S/S application metadata 확정 | Node·Channel·Spot direct와 Logical Multicast canonical frame, 1024-byte 상한, snapshot, malformed ingress, forwarding과 일반 reply metadata 미지원 계약 고정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-23, `mesh-node-framework-dispatch-design.ko.md` FD-27 |
| S0-20 | 기술문서 작성 원칙 적용 범위 확정 | 모든 대상 문서에 독자·질문·원본, current-state 또는 blueprint 성격, 2축 review와 render gate 지정 | 완료 | `s0-scope-baseline.ko.md` §0·§7, `log/templates/manifest.ko.md` §3 |
| S0-21 | location store 기본 정책 확정 | manual peer 연결과 location authority를 분리하고 공식 Redis production 기본, 명시적 등록, location store 미등록 시 startup failure와 test-only in-memory 경계를 고정 | 완료 | `s0-scope-baseline.ko.md` §2, `framework-route-mesh-messaging-consolidation.ko.md` D-10·D-27, `mesh-node-framework-dispatch-design.ko.md` FD-39 |

S0 완료 gate:

- [x] 미결정 decision이 0개다.
- [x] Core 10.0.0 계약을 정식 spec에 먼저 기록하고 현재 checkout 구현 차이는 임시 실행 추적 문서만 소유한다.
- [x] review, E2E, sample, package와 삭제 검증 범위가 모두 식별되어 있다.
- [x] 모든 문서 작업이 `documentation-principles.ko.md`의 작성·검증 절차와 연결되어 있다.

## 5. S1 — Core 10.0.0 정식 spec 작성

### 5.1 계약 구조와 공개 API

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-01 | 현재 checkout 공개 C API inventory 재생성 | 공개 header의 함수·type·enum type·enumerator·public macro가 전수 검토표에 정확히 한 번 대응 | 완료 | `s1-core-public-api-inventory.ko.md`; `PUBLIC API INVENTORY CLEAN`, FUNC 183·TYPE 51·ENUM_TYPE 48·ENUMERATOR 311·FIELD 158·MACRO 35·EXPORT 213 |
| S1-02 | exported symbol baseline 생성 | 현재 checkout library의 제거·유지·대체 symbol 분류 완료 | 완료 | 같은 inventory의 export 213개, headerless internal export 30개와 SHA-256 기록 |
| S1-03 | MeshNode 정식 spec 한국어·영문 작성 | lifecycle, identity, membership, messaging, multicast와 status 포함 | 완료 | `core/doc/spec/core/service/01-mesh-node.ko.md`, `.md` |
| S1-04 | Spot 정식 spec 한국어·영문 작성 | Spot lifecycle, direct send/request와 Logical Multicast metadata, local subscription, timer, dispatch와 queue ownership 포함 | 완료 | `core/doc/spec/core/service/03-spot.ko.md`, `.md` |
| S1-05 | Actor·STREAM 경계 정식 spec 작성 | ActorRef, lifecycle과 이동은 service/actor가, session barrier는 service/stream-session이 소유하고 raw socket에는 범용 계약만 남김 | 완료 | `core/doc/spec/core/service/04-actor.ko.md`, `04-actor.md`, `05-stream-session.ko.md`, `05-stream-session.md` |
| S1-06 | Core governance, index와 cross-link 작성 | spec-first, 10.0.0 적용 범위와 한영 index가 모든 정식 owner 문서를 연결 | 완료 | `core/doc/spec/core/00-public-contract-governance.*`, `service/README.*`, Core spec index |
| S1-06A | Core 정식 spec 파일 번호와 링크 정리 | 공통 `00~08`, service `01~05`, socket `01~08` 번호가 한영 문서·목차·저장소 참조에 일치 | 완료 | 번호 검사, frozen scope 52개 local link·fence 검사와 이전 경로 scoped no-hit |
| S1-07 | exact `zlink_mesh_node_*` signature 고정 | 생성·peer·node/channel send/request·one-shot reply, ready/claim/batch와 target-channel publisher API 확정 | 완료 | `core/doc/spec/core/service/01-mesh-node.*`, `02-dispatch.*`; formal reverse inventory FUNC 89 |
| S1-08 | type, enum과 status ABI 고정 | 구조체 크기, version field, 숫자 값과 field ownership을 exact ABI로 명시 | 완료 | service 정식 C block과 `service/README.*` versioned 구조체 분류; S3 반영 reverse inventory TYPE 31·ENUM_TYPE 16·ENUMERATOR 100·FIELD 220 |
| S1-09 | 삭제 C API 목록 고정 | 제거 API·enumerator·macro마다 대체 또는 제거 판정 명시 | 완료 | `s1-core-public-api-inventory.ko.md` 제거·대체 표와 exact replacement target 54개 |

### 5.2 동작 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-10 | MeshName·ChannelName·RID 불변 조건 작성 | cardinality, 복수 mesh, immutable ChannelName set과 변경 금지 시점이 명확함 | 완료 | `core/doc/spec/core/service/01-mesh-node.*` identity·membership·lifecycle 절 |
| S1-11 | peer admission과 readiness 계약 작성 | generation, duplicate RID, security profile과 drain 제외 규칙 명시 | 완료 | `core/doc/spec/core/service/01-mesh-node.*` peer admission·ready 절 |
| S1-12 | node·channel·multicast 선택 의미 작성 | target snapshot, round-robin과 no-member 결과를 operation 계약 안에 정의 | 완료 | `core/doc/spec/core/service/01-mesh-node.*` messaging·Logical Multicast 절 |
| S1-13 | ready·claim·batch 계약 작성 | wakeup-only callback, application/infrastructure claim, metadata, ownership과 close 규칙 확정 | 완료 | `core/doc/spec/core/service/02-dispatch.ko.md`, `.md` |
| S1-14 | Spot Logical Multicast 계약 작성 | target channel, channel-scoped subscription, local shared reference와 no-relay 명시 | 완료 | `core/doc/spec/core/service/01-mesh-node.*`, `03-spot.*` |
| S1-15 | `NODROP`과 backpressure 계약 작성 | local·remote admission, 기본값 1, timeout, DONTWAIT와 drop 명시 | 완료 | `core/doc/spec/core/service/01-mesh-node.*` Logical Multicast·option 절 |
| S1-16 | message ownership과 ordering 계약 작성 | multipart 원자성, reference count와 ordering 범위 확정 | 완료 | `core/doc/spec/core/02-message.*`, `service/02-dispatch.*`, `service/01-mesh-node.*` |
| S1-17 | actor·Spot·STREAM session 경계 작성 | owner MeshNode와 mailbox·claim 책임이 중복되지 않고 Actor가 Spot callback을 경유하지 않음 | 완료 | `core/doc/spec/core/service/03-spot.*`, `04-actor.*`, `05-stream-session.*` |
| S1-18 | classic PUB/SUB 비변경 계약 확인 | 독립 PUB/SUB socket API의 의미가 축소되지 않음 | 완료 | `socket/pub.*`, `sub.*`, `xpub.*`, `xsub.*`, `service/README.*` |
| S1-18A | request completion·reply 계약 | requester operation ID, responder one-shot reply token, generation·shutdown 오류, owner completion batch와 in-turn await 명시 | 완료 | `core/doc/spec/core/service/02-dispatch.ko.md` §5·§6, `02-dispatch.md` §5·§6 |
| S1-18B | service wire multipart 계약 | versioned routing envelope, optional metadata frame, borrowed submit input, retained Core reference와 payload part 경계 명시 | 완료 | `core/doc/spec/core/service/01-mesh-node.*`, `02-dispatch.*`, `core/02-message.*` |
| S1-18C | receive batch 재사용 계약 | empty-before-receive, non-empty `EBUSY`, reset·BUFFER_TOO_SMALL·retain 결과별 view/reference 수명 명시 | 완료 | `core/doc/spec/core/service/02-dispatch.ko.md` §3·§4, `02-dispatch.md` §3·§4 |

### 5.3 연관 Core 정식 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-19 | router 정식 spec 갱신 | generic ROUTER 계약과 service 경계를 분리 | 완료 | `core/doc/spec/core/socket/07-router.*`, `socket/README.*` |
| S1-20 | polling 정식 spec 갱신 | ready index, callback·receive poller 배타성, `POLLOUT`과 infrastructure 진행 보장 명시 | 완료 | `core/doc/spec/core/06-polling.*` |
| S1-21 | monitoring 정식 spec 갱신 | MeshNode source, peer, multicast, backpressure와 drop event 계약 명시 | 완료 | `core/doc/spec/core/07-monitoring.*`, `05-events.*` |
| S1-22 | errno·errors 정식 spec 갱신 | 신규 함수의 모든 result와 errno mapping 포함 | 완료 | `core/doc/spec/core/04-errno-map.*`, `03-errors.*` |
| S1-23 | option과 handle 지원 정식 표 | generic option이 MeshNode·Spot·publisher에서 가지는 의미 확정 | 완료 | `core/doc/spec/core/service/README.*`, `01-mesh-node.*`, `03-spot.*` |
| S1-24 | 10.0.0 version 계약 작성 | 공개 version macro, `zlink_version()`과 SOVERSION 10이 한영 정식 spec에 일치 | 완료 | `core/doc/spec/core/03-errors.ko.md`, `.md` §7 |
| S1-25 | 정식 spec 한국어·영문 parity 및 link 검증 | signature, result, ownership과 local link 차이 0개 | 완료 | reverse inventory 한영 C block exact parity, 한영 C identifier parity 25쌍, frozen scope 52개 local link·fence와 `git diff --check` 통과 |
| S1-26 | unresolved marker 검사 | TBD, 미결정, 나중에 확정 표현 scoped no-hit | 완료 | Core formal scope scoped no-hit |
| S1-27 | Core 구현 일치 임시 추적 문서 작성 | 정식 spec과 checkout 차이를 항목별 기록하며 formal 문서가 참조하지 않음 | 완료 | `s1-core-implementation-tracking.ko.md` CI-01~CI-15, formal plan-reference no-hit |

S1 완료 gate:

- [x] 구현자가 plan 문서를 추측하지 않고 Core 정식 spec만으로 C API를 구현할 수 있는 owner와 exact ABI가 있다.
- [x] 모든 삭제 API와 새 API가 exact signature 및 result 계약을 가진다.
- [x] iteration 4에서 Codex agent와 Claude Sonnet이 같은 frozen scope를 독립 리뷰했고, finding 12건을 모두 수정했다. 수정본의 clean 재리뷰는 실행하지 않았으며 사용자가 자동 검증을 통과한 hash `6cd163bf7fa4b010e3ddac02ea0c6e9cc90fe58622a29f0cc1a50bf85451ea71`을 구현 기준선으로 승인했다.
- [x] 현재 구현 차이는 `s1-core-implementation-tracking.ko.md`에 기록되고 정식 spec은 이를 참조하지 않는다.

## 6. S2 — framework 정식 spec과 영향 범위 변경

### 6.1 공통과 server 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-01 | public contract governance에 10.0.0 전환 절차 반영 | spec-first, gap 기록과 구현 확인 순서 명시 | 완료 | `framework/doc/framework/spec/00-public-contract-governance.ko.md`; formal plan-reference no-hit |
| S2-02 | overview 갱신 | RouteMesh, MeshNode, Logical Multicast와 classic fanout 경계 설명 | 완료 | `framework/doc/framework/spec/01-overview.ko.md`, `02-interaction-model.ko.md` |
| S2-03 | interaction model 갱신 | node, channel, Spot, Actor, fanout과 STREAM 의미 분리 | 완료 | `framework/doc/framework/spec/02-interaction-model.ko.md` |
| S2-04 | framework API 갱신 | topology registration 제거와 MeshNode-owned 기능 계약 반영 | 완료 | `framework/doc/framework/spec/05-framework-api.ko.md` |
| S2-05 | channel topology 갱신 | MeshName, immutable ChannelName set, RID와 full mesh membership 계약 반영 | 완료 | `framework/doc/framework/spec/server/10-channel-topology.ko.md` |
| S2-06 | channel messaging 갱신 | select-one, direct RID, timeout, cancellation과 reply 계약 반영 | 완료 | `framework/doc/framework/spec/server/11-channel-messaging.ko.md` |
| S2-07 | MeshNode owner 계약 작성 | `21-mesh-node.ko.md`의 파일명·제목·link와 책임 범위가 10.0.0 개념과 일치 | 완료 | `framework/doc/framework/spec/server/21-mesh-node.ko.md` |
| S2-08 | Spot messaging 갱신 | Logical Multicast와 channel-scoped local subscription, explicit publish target interface 반영 | 완료 | `server/20-spot-messaging.ko.md`; Core Spot·Dispatch 교차 검증 |
| S2-09 | Actor·Spot actor·address messaging 갱신 | bridge 제거, owner MeshNode와 위치 투명성 반영 | 완료 | `server/22-actor-model.ko.md`, `23-spot-actor.ko.md`, `24-spot-address-messaging.ko.md` |
| S2-10 | session actor dispatch 갱신 | STREAM 경계와 MeshNode 선택이 명확함 | 완료 | `server/30-stream-session.ko.md`, `31-session-actor-dispatch.ko.md` |
| S2-11 | location runtime과 Redis store 갱신 | MeshNode descriptor와 Spot·Actor location row를 분리하고 Redis를 production 기본 구현으로 지정. 명시적 등록, location store 미등록 시 startup failure, manual admission handshake와 test-only in-memory 경계를 반영 | 완료 | `server/40-location-runtime.ko.md`, `41-location-store-redis.ko.md`; Redis fixture 3개 parse·byte parity |
| S2-11A | Actor transfer authority store 계약 | participant-set CAS, transfer token, lease, prepared/commit/abort 복구, Redis 구현과 startup capability validation 명시 | 완료 | `server/23-spot-actor.ko.md`, `41-location-store-redis.ko.md`, `actor-transfer-v1.json` |
| S2-12 | monitoring과 graceful drain 갱신 | RouteMesh별 readiness, drain, multicast와 rollback 단위 반영 | 완료 | `server/50-runtime-monitoring.ko.md`, `54-graceful-drain-handoff.ko.md` |
| S2-12A | runtime metrics와 message-flow tracing 갱신 | route, multicast, fanout metric·flow 종류와 bounded label을 정의하고 client connector reconnect 계기를 server session 계기와 분리 | 완료 | `server/51-runtime-metrics.ko.md`, `52-message-flow-tracing.ko.md`, `stream-connector/32-stream-connector.ko.md`와 네 언어 connector exact interface |
| S2-12B | flow correlation 갱신 | direct channel multicast와 reply completion correlation 경계 반영 | 완료 | `server/53-flow-correlation.ko.md` |
| S2-12C | S/S application metadata 계약 | Node·Channel·Spot direct와 Logical Multicast canonical codec, last-write-wins builder와 hostile ingress failure, immutable context, relay 전이표, reply 비자동복사·일반 reply metadata 미지원 명시 | 완료 | Core MeshNode·Spot과 framework `03-message-model.ko.md`, 5개 언어 exact interface 교차 검증 |
| S2-12D | Spot timer backend 계약 | .NET·Java·Node platform timer와 C/C++ C API timer가 같은 keyed scheduling·cancel 의미를 제공 | 완료 | `04-async-execution-policy.ko.md`, `server/20-spot-messaging.ko.md`, `25-stage-wrapper-on-spot.ko.md` |

### 6.2 언어별 공개 interface

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-13 | `.NET` exact interface 작성 | `UseInMemoryLocationStores()`를 production 표면에서 제거하고 나머지 root option, AddRouteMesh, RID pin을 지원하는 `IZLinkMeshPeerConnections`, 두 handler family, Spot·Actor 멤버, client metadata·handler snapshot, NoDrop과 runtime-options signature·startup-only 오류 확정 | 완료 | .NET exact 문서 3개와 전용 inventory fixture 통과 |
| S2-14 | C++ exact interface 작성 | C++ builder, handler, client, value/result와 lifecycle exact signature를 정식 spec에 확정 | 완료 | C++ exact 문서와 location 문서 fixture 통과 |
| S2-15 | Java·Kotlin exact interface 작성 | Java builder·handler·client·Spring 등록과 Kotlin DSL·extension·async 경계의 exact signature를 각각 확정 | 완료 | Java·Kotlin exact 문서와 location 문서 fixture 통과 |
| S2-16 | Node.js exact interface 작성 | TypeScript declaration, Promise, NestJS 등록, peer·metadata·NoDrop·runtime option exact signature를 확정 | 완료 | Node.js exact 문서와 location 문서 fixture 통과 |
| S2-17 | 다섯 언어 제거 interface 표 작성 | root·builder·endpoint overload와 SpotNode 이름의 공개 멤버를 언어별로 전수 대응 | 완료 | `route-mesh-v10-contract-inventory.json`; 금지 surface no-hit |
| S2-18 | 구현 차이 추적 경계 고정 | 정식 spec은 현재 구현 상태를 기록하지 않고 임시 계획 문서만 Core·bindings·framework 구현 차이를 소유 | 완료 | formal plan-reference no-hit; `90-implementation-gap.ko.md` 분리 |
| S2-18A | 다섯 언어 현재 checkout builder 전수 mapping | root option과 ClientServer·RouteMesh·Spot builder의 모든 멤버가 언어별로 유지·이동·제거에 정확히 한 번 대응 | 완료 | verifier: transition owners 20, source members 263 exact-once |

### 6.3 E2E와 sample 영향 검토

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-19 | 공통·언어별 E2E 문서 inventory 생성 | 모든 config, 언어별 `feature-map.ko.md`, public 예제와 scenario의 영향·비영향·신규 검증 분류 | 완료 | 영향 inventory §11: 55/55 lane, 언어별 feature map 11개 |
| S2-20 | LocationMessaging 영향 검토 | select-one, round-robin, scale, direct RID scenario 대응 | 완료 | Config 1과 5개 언어 lane mapping |
| S2-21 | SpotService 영향 검토 | multicast, local topic match, reconnect와 channel call 대응 | 완료 | Config 2와 5개 언어 lane mapping |
| S2-22 | PubSub 영향 검토 | classic fanout 비변경 회귀 scenario 대응 | 완료 | Config 3과 5개 언어 classic fanout mapping |
| S2-23 | lifecycle·transfer·monitoring 영향 검토 | drain, owner 이동, endpoint remap과 관측 scenario 대응 | 완료 | Config 5·7·10·11 lane mapping |
| S2-23A | ToActorMessaging·StoreFailureRecovery 영향 검토 | actor owner route와 descriptor/location row 장애 복구 대응 | 완료 | Config 6·9 lane mapping |
| S2-24 | 공통·언어별 sample 문서 inventory 생성 | 변경, 삭제, 신규, 비영향 sample 문서와 public 예제를 언어별로 분류 | 완료 | 영향 inventory §12: 32/32 lane과 언어별 루트 안내 4/4 |
| S2-25 | 언어별 sample public API 예제 검토 | 제거 API, endpoint 배선과 raw helper 사용처가 다섯 언어에서 모두 기록됨 | 완료 | 공개 문서 old topology·stale location no-hit |
| S2-26 | package consumer와 runner 영향 검토 | local package, clean consumer와 E2E runner 변경점 기록 | 완료 | 영향 inventory §13: 96/96 runner |
| S2-27 | guide·internals 변경 지도 작성 | 구현 뒤 바꿀 모든 사용자·내부 문서 경로와 각 문서의 독자·질문·원본 식별. S2에서는 대상과 검증 기준만 정하고 internals 본문은 바꾸지 않음 | 완료 | 영향 inventory §14: 81/81 문서; 본문 변경은 S8·S9로 deferred |
| S2-27A | 문서 성격과 current-state 경계 검증 | spec·guide·internals에는 10.0.0 현재 계약만, 계획은 blueprint와 실행 추적만 기록 | 완료 | formal plan-reference와 current-history marker no-hit |
| S2-28 | link·anchor·render 검증 | S2 정식 spec 범위의 깨진 link·중복 anchor 0개이고 실제 render를 확인한 증거가 있음 | 완료 | 217개 render, 1,039 link, 150 table 문서, 111 fence 문서 오류 0 |
| S2-28A | 예제 API 강제 검사 설계 | 공개 계약 예제 compile/smoke, 필수 구성 누락과 원본·번역 동기 검사를 구현 stage에서 실행할 파일·명령·실패 조건으로 고정 | 완료 | transition inventory §7의 S8·S9 red/green 명령 |
| S2-29 | 공통 E2E 문서 적용 사항 검토 | Config 1~11마다 10.0.0 topology, 입력, 관찰 결과와 failure scenario의 변경·비변경·신규 검증을 파일 단위로 분류 | 완료 | 영향 inventory §3·§11 |
| S2-30 | 공통 sample 문서 적용 사항 검토 | sample마다 target API 예제, Redis 등록, manual topology와 runner 변경·비변경을 파일 단위로 분류 | 완료 | 영향 inventory §4·§12·§12.1 |
| S2-31 | runner template 변경 계획 고정 | topology setup, package 입력과 result marker 변경점을 파일별 기록 | 완료 | 영향 inventory §5·§13 |

S2 완료 gate:

- [x] reviewed Core 10.0.0 정식 spec과 framework 목표 계약 사이에 기능 또는 error 의미 차이가 없다.
- [x] E2E, sample, package consumer와 runner 영향이 파일 단위로 식별되어 있고 실제 변경·실행은 해당 framework 구현 stage의 gate로 연결되어 있다.
- [x] 공통·server 의미 계약과 .NET·C++·Java·Kotlin·Node exact public interface가 정식 spec에 있다.
- [x] 공통·언어별 E2E 문서, 공통·언어별 sample 문서와 public 예제가 파일 단위 inventory에 포함되어 있다.

## 7. S3 — 문서 독립 리뷰 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-01 | S1·S2 문서 revision 동결 | review manifest에 commit과 diff 범위 기록 | 완료 | [iteration 11 manifest](log/s3-document-review/iteration-11/manifest.ko.md): 관련 guide·gap·HTTP error를 포함한 195개 문서, aggregate `8d5851fd02395f8d80924a7feca67769d8bef121ca538e5471ed0a8976361023`, file-list `ba3393d0b5d5516c83c26de6fb2255830db17aa38801c7a45be2d8c54600946a` |
| S3-02 | Codex agent 문서 리뷰 | 누락, 모순, 구현 불가능 계약과 stale API finding 보고 | 리뷰 중 | [iteration 11 manifest](log/s3-document-review/iteration-11/manifest.ko.md)의 frozen scope 195개 전체 재리뷰 |
| S3-03 | Claude Sonnet 문서 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 리뷰 중 | [iteration 11 manifest](log/s3-document-review/iteration-11/manifest.ko.md)의 frozen scope 195개, 실제 Claude Sonnet 전체 재리뷰 |
| S3-04 | finding 병합과 중복 제거 | 모든 finding에 owner, severity와 red gate 지정 | 완료 | iteration 10 두 결과를 S3-F10-A~C로 병합. 죽은 절 인용과 C++ guide finding은 중복 병합 |
| S3-05 | Core 정식 spec finding 수정 | 관련 한국어·영문·signature·result table과 임시 구현 차이 추적을 함께 수정 | 완료 | iteration 8까지 발견한 Core 문서 finding 반영. iteration 9는 framework 문서만 검토했으며 framework 수정에서 Core 계약 변경이 필요하면 다시 연다 |
| S3-06 | framework spec finding 수정 | 공통·server spec, .NET·C++·Java·Kotlin·Node exact interface, 공통·언어별 E2E·sample 문서와 public 예제 영향 inventory를 함께 수정 | 완료 | iteration 10 S3-F10-A~C 수정 완료 |
| S3-07 | 문서 자동 검증 재실행 | link, signature, stale name, duplicate와 formatting 검사 통과 | 완료 | `FRAMEWORK DOC CONTRACTS CLEAN` — exact 24·connector exact 4·formal 53·fixture 19·declaration 1,162·feature map 55·scenario 955; iteration 11 scope hash·diff 검사 통과 |
| S3-08 | 전체 scope 재리뷰 | 이전 diff만이 아니라 S1·S2 전체를 두 리뷰어가 다시 검토 | 리뷰 중 | iteration 11에서 확장된 195개 전체 scope를 두 reviewer가 처음부터 재검토 |
| S3-09 | 문서별 2축 review 기록 | 각 문서를 원칙 준수와 1차 소스 부합으로 나누고 finding마다 축·severity·file:line·근거·제안 기록 | 완료 | iteration 9 두 reviewer가 모든 finding에 `[1차소스]` 또는 `[원칙]`, severity, file:line, 근거와 수정안을 기록 |
| S3-10 | 문서별 검증 증거 분리 | finding을 1차 소스로 확인한 뒤 문서별 수정 diff와 SHA-256을 독립 증거로 기록. 사용자가 별도로 요청하지 않으면 commit은 만들지 않음 | 진행 중 | iteration 9 raw output·finding merge와 수정 후 검증 증거 정리 중. commit은 만들지 않음 |

### 7.1 Iteration 9 수정 묶음

아래 행은 에이전트에게 진행표 경로와 ID 하나만 전달해도 수정 범위와 검증 기준을 찾을 수 있도록 한다.
각 담당자는 §0.2에 따라 자기 행만 갱신하고, 행에 적지 않은 파일에서 같은 계약 문제가 확인되면 증거에
기록한 뒤 coordinator에게 범위 확장을 요청한다.

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F9-A | Stream Connector exact interface와 flow field: `.NET`·Java의 공통 계약에 없는 receive-count API 제거, C++의 operation별 codec·connector registry 제거와 connector-level typed codec 정렬, `.NET ZLinkMessageFlowEvent`의 조건부 RID·topic·Spot·Actor field 보완, C++ location runtime 절 링크 정정 | 공통 Connector·flow 계약과 네 언어 exact interface parity, 관련 inventory·fixture·verifier와 scoped diff 검사 통과 | 완료 | [iteration 9 finding ledger](log/s3-document-review/iteration-9/finding-ledger.ko.md)의 Codex 5·6, Claude 1·2 반영. 13개 변경 파일 aggregate `ac2188f6…b9dc`; receive-count·C++ operation codec/registry no-hit, flow 조건부 field 5개 exact-once, JSON·table·fence·scoped diff 검사 통과. `FRAMEWORK DOC CONTRACTS CLEAN`(connector exact 4·formal 53·fixture 19) |
| S3-F9-B | E2E·sample 문서: 공통 sample RID 규칙을 domain identity와 자동 할당 infrastructure RID로 구분, Java·Kotlin ToActor 외부 Redis fallback 제거, C++ Bingo·TicTacToe endpoint 환경 변수 fallback 제거, Java E2E 세 README의 환경 변수 helper 제거, ShoppingMall 구어체 교정 | 공통 E2E·sample 계약과 언어별 문서 일치, 금지 fallback·구어체 scoped no-hit, link·table·fence와 diff 검사 통과 | 완료 | [iteration 9 finding ledger](log/s3-document-review/iteration-9/finding-ledger.ko.md)의 Codex 1~4·9 반영. 9개 문서 aggregate `72fdfd7e…6346`; 금지 fallback·환경 변수 helper·구어체 no-hit, link·table·fence 9/9, scoped `git diff --check`, `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·formal 48·feature map 55) 통과 |
| S3-F9-C | 다섯 언어 RuntimeMonitoring feature map: canonical `MON-*` 행에 현재 증거와 gap을 일대일 대응하고 `이전 A1`~`이전 D1` catalog 제거, `.NET`의 분리되지 않은 표 구조 정정 | 다섯 문서의 canonical scenario exact-once, legacy ID no-hit, table·fence·diff 검사 통과 | 완료 | 다섯 문서 45개 canonical scenario 전체 exact-once, legacy ID 0건, table·fence·scoped `git diff --check` 통과. S3-F9-A 반영 뒤 전체 verifier clean |

### 7.2 Iteration 10 수정 묶음

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F10-A | 계약 owner·guide·gap 정리: C++ guide의 제거 topology·SpotMesh·bridge를 RouteMesh/MeshNode로 전환, C++ STREAM anchor 수정, `05-framework-api`의 handler filter·dispatch action·codec owner를 정식 절로 완결하고 모든 죽은 절 인용 정정, `.NET` Connector 검증 절 링크와 payload preview 분류 수정, async 문서의 다섯 exact owner 명시, gap table과 의인화·구어체 정리 | 제거 topology와 죽은 절 인용 no-hit, guide·spec·gap link·table·fence·문체·verifier·scoped diff 검사 통과 | 완료 | 시작 기준 revision `b0e4af22652b`; [iteration 10 finding ledger](log/s3-document-review/iteration-10/finding-ledger.ko.md) 반영. 제거 topology·죽은 `05` 절·잘못된 Connector 검증 링크·대상 문체 표현 scoped `rg` no-hit, fence 짝수와 scoped `git diff --check` 통과. `scripts/verify-framework-doc-contracts.sh` → `FRAMEWORK DOC CONTRACTS CLEAN languages=5 exact_documents=24 connector_exact=4 formal_documents=53 code_fixtures=19 declarations=1162 transition_owners=20 transition_members=263 feature_maps=55 scenario_rows=955` |
| S3-F10-B | flow·dispatch-error·runtime-error 공개 계약 정렬: 공통 event kind/field/reason/action과 observer/error sink를 한 owner에 정의하고 C++·Java·Kotlin·Node·.NET exact interface 및 Config 5 evidence를 같은 모델로 정렬 | 공통 owner와 다섯 언어 exact interface가 같은 닫힌 값·observer·sink를 제공하고 exception object 노출이 없으며 inventory·fixture·verifier 통과 | 완료 | 시작 기준 `b0e4af22652b`; [iteration 10 finding ledger](log/s3-document-review/iteration-10/finding-ledger.ko.md) 반영. 공통 `52-message-flow-tracing`을 `zlink.dispatch_error`/`zlink.runtime_error`의 단일 owner로 고정하고 5언어 observer·runtime error sink와 Config 5·ResilienceLifecycle evidence를 정렬. 16개 파일 aggregate `21ddfada…b1b3`; 이전 outcome·별도 dispatch event·exception object·internal sink helper scoped no-hit, JSON parse·scoped `git diff --check` 통과. Verifier에 5언어 flow/sink semantic gate를 추가했고 `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declarations 1,162·feature map 55·scenario 955) |
| S3-F10-C | Java·E2E 의미 정리: Java drain을 mesh별 runtime/result 하나로 통일하고 Kotlin 투영 정렬, timeout 우선순위를 호출별→MeshNode별→framework 전역으로 수정, Config 1 decode 실패를 `decode_error`로 수정, Spot messaging에 Spot control claim을 처음 사용 전에 정의·링크 | Java/Kotlin exact parity, Config 1 reason 일치, Spot control claim 문서 완결성, fixture·verifier·table·fence·diff 검사 통과 | 완료 | [iteration 10 finding ledger](log/s3-document-review/iteration-10/finding-ledger.ko.md)의 C10-03·05·06과 Claude low 2 반영. 5개 문서 aggregate `2a8dd2b7…7189`; 제거된 전역 drain 이름·이전 decode 분류·채널별 timeout 우선순위 no-hit, Java·Kotlin mesh별 drain parity와 Spot control claim 최초 사용 전 정의·링크 확인, link·table·fence 5/5와 scoped `git diff --check` 통과. `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declarations 1,162) |

문서 리뷰 필수 축:

S3 framework 리뷰 범위에는 framework 공통·server 정식 spec, .NET·C++·Java·Kotlin·Node exact public
interface, 공통·언어별 E2E 문서, 공통·언어별 sample 문서와 public 예제를 모두 포함한다. 일부 언어의
exact interface나 예제를 이후 구현 stage의 검토로 미루지 않는다.

- `documentation-principles.ko.md` 원칙 1~9 준수와 대상 독자가 한 번에 이해할 수 있는 한국어 산문
- 각 문서가 명시한 1차 소스와 실제 공개 header·구현·spec의 부합
- Core API inventory 누락과 exact signature 모순
- Core와 framework 사이 result, ownership, callback과 backpressure 의미 차이
- E2E·sample·package consumer·runner 변경 누락
- 제거 API, bridge, SpotNode PUB/SUB와 endpoint topology의 stale 설명
- guide와 internals에 들어갈 내용이 spec에 섞였는지 여부
- POSD 관점에서 호출자에게 peer 목록, encoding, queue와 retry 내부 정책을 노출하는 계약
- DDD 관점에서 MeshNode, Spot, Actor, session과 transport 책임이 섞인 계약

S3 완료 gate:

- [ ] open documentation finding이 0개다.
- [ ] 변경 문서의 실제 render, 예제 API, link와 원본·번역 동기 검증 증거가 있다.
- [ ] Codex agent 결과 마지막 줄이 `DOC REVIEW CLEAN`이다.
- [ ] Claude Sonnet 결과 마지막 줄이 `DOC REVIEW CLEAN`이다.
- [ ] 병렬 Core 구현은 S1 기준선의 red/green 작업으로만 진행됐고 S2·S3 계약의 근거로 사용되지 않았다.
- [ ] S3 finding이 Core 계약을 바꾼 경우 병렬 구현 담당자에게 변경을 전달했으며 S5 동결 전 재정렬 gate가 기록되어 있다.

## 8. S4 — Core 구현·제거 코드 정리와 정식 spec 일치

### 8.1 red gate와 공개 API

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-01 | contract red test 작성 | 새 API 부재와 제거 API 존재를 test가 먼저 실패로 증명 | 진행 중 | surface gate가 red→green 전이 완료(현재 PASS: 196 export 정확 일치·제거 identifier 0). 계약 test는 `test_mesh_node_basic`(4 case)·`test_mesh_peer_admission`(2-process 7 case)로 red→green 진행. spec 절별 세부 matrix 확대 잔여 |
| S4-02 | public header를 10.0.0 spec에 맞춤 | 함수·type·enum과 result signature 일치 | 완료 | header 폐쇄가 frozen spec(52파일 `5bd7451d…`)과 일치(surface gate PASS, C/C++ compile OK). 신규 service header 6개 생성, 설치 규칙 포함 |
| S4-03 | export와 ABI 목록 갱신 | 새 symbol 존재, 제거 symbol 부재 | 완료 | `libzlink.vers` formal FUNC 196 명시 목록, `nm` 대조로 export=formal 정확 일치·제거/internal export 0(`contract_public_surface` PASS). SONAME 10 |
| S4-04 | MeshNode lifecycle과 handle kind 구현 | 생성, bind, start, drain, destroy 계약 통과 | 진행 중 | lifecycle+wire 구현: start가 node 소유 ROUTER 생성·bind(port 0 해석 포함)·ingress thread 기동, shutdown/destroy가 wire 정리. `test_mesh_node_basic`·`test_mesh_peer_admission` green |
| S4-05 | peer descriptor와 admission 구현 | manual·discovery endpoint가 같은 handshake를 사용하고 MeshName, identity, lifecycle generation, descriptor revision, duplicate, security, ready와 drain 계약 통과 | 진행 중 | HELLO/ADMIT/REJECT/UPDATE handshake 구현(`mesh_runtime`+`mesh_wire.cpp`): MeshName·trust·expected-RID·stale generation 검증, weight 변경의 revision 증가+admitted peer broadcast, readiness 재계산, DISCONNECTED 처리. 2-process contract test `test_mesh_peer_admission` 3/3 green(admission·READY 전이·weight 25 전파·MeshName 불일치 EEXIST 거부·PARTIAL_READY 유지). discovery adapter 관측·drain 세부 잔여 |
| S4-05A | manual peer lifecycle 구현 | endpoint 및 예상 RID pin, connect·disconnect, discovery와 중복 source 병합, 누락 peer 상태를 관측하고 운영자가 모든 peer 연결을 설정해야 하는 계약의 test 통과 | 진행 중 | 구현 완료: expected-RID pin 검증(ESTALE), connect_peer/remove_peer_connection/disconnect_peer(rid+generation, MIXED source 병합·부분 제거), peers/peer_channels query. admission·pin은 test green, remove/disconnect/MIXED 병합 전용 contract test 잔여 |

### 8.2 메시징과 runtime

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-06 | RID pipe와 channel index 구현 | 같은 MeshName의 ready RID만 선택 | 진행 중 | local+remote 후보 선택 구현: admitted 양수-weight channel member peer가 RR 후보로 합류(descriptor UPDATE로 weight 실시간 반영). RR 분포·weight 0 제외 전용 test 잔여 |
| S4-07 | node·channel 선택과 submit 구현 | direct와 round-robin이 한 send/request 호출 안에서 원자적으로 처리되고 RID-only 공개 select API가 없음을 contract test로 검증 | 진행 중 | 선택+submit 단일 호출 구현, select API 부재는 surface gate가 보증 |
| S4-08 | Node·Channel·Spot direct send/request와 service envelope 구현 | application metadata codec, timeout, operation ID와 borrowed/retained multipart ownership test 통과 | 진행 중 | versioned service envelope(v1: magic·type·flags, correlation·terminal result, metadata frame 분리) + Node direct·Channel remote send/request/reply 왕복 구현. 원격 request가 responder의 remote-origin reply route로 one-shot token을 재사용하고 completion이 requester infra lane에 정확히 한 번 도달(`test_mesh_peer_admission` round-trip case green). Spot direct remote도 구현: wire SPOT_SEND/REQUEST(target spot rid+generation 주소)로 원격 entry Spot request/reply 왕복 green, 생성 불일치는 ESTALE/ENOENT terminal completion. metadata codec·timeout·borrowed ownership은 local과 동일 경로 공유 |
| S4-08A | responder reply 구현 | opaque token one-shot, generation·shutdown 오류, source route 비노출과 S/S reply metadata 미지원 test 통과 | 진행 중 | one-shot sealed token(EALREADY 재사용 거부 test green), local·remote-origin 공용(remote는 route가 origin rid+correlation을 봉인, wire REPLY로 회신). generation guard(ESTALE)·requester timeout 뒤 도착 폐기 구현. shutdown 오류·metadata 미지원 전용 test 잔여 |
| S4-09 | mailbox·ready·claim·batch 구현 | Node·Spot·Actor 격리, infrastructure 우선 drain과 lost wakeup 0건 | 진행 중 | owner×domain mailbox·budget·claim serial·batch 구현, request/reply/completion round trip green(`test_mesh_node_basic`) |
| S4-10 | Logical Multicast multi-target submit 구현 | target channel 직접 선택, canonical metadata snapshot·검증, 조건부 local dispatch와 remote node당 1회 submit | 진행 중 | remote leg 구현: snapshot=admitted 양수-weight channel member peer + local match. NODROP 원자 reserve는 신설 router 내부 probe(`routed_target_writable`, socket_base/router에 추가)와 node별 wire send 직렬화 mutex로 local mailbox·remote pipe를 모두 선검사 후 commit. publish detail의 remote snapshot/admitted/dropped 실측 반영. 수신측은 channel member일 때 local 구독 match로 fan-out(peer당 1회 wire submit). `test_mesh_peer_admission` multicast case green(detail 1/1/0 검증) |
| S4-11 | shared message reference count 구현 | local Spot queue와 remote pipe 수명·실패 정리 검증 | 진행 중 | local fanout이 zlink_msg_copy refcount 공유 사용, multicast test green |
| S4-12 | NODROP와 backpressure 구현 | local·remote admission/commit 직렬화, 기본 1, 부분 전달 금지, timeout과 drop test 통과 | 진행 중 | NODROP=1 기본·all-or-none 구현: node mutex 아래 local mailbox 선검사 + wire send 직렬화 mutex 아래 remote pipe 전체 probe(`routed_target_writable`) 후 commit — 부분 전달 없음. DONTWAIT=EAGAIN 즉시 반환 green. blocking 호출은 SNDTIMEO까지 reserve 재시도 후 ETIMEDOUT(구현 완료, claim release가 재시도 신호) |
| S4-13 | no-relay와 duplicate guard 구현 | multicast loop와 중복 전달 0건 | 진행 중 | 구조적 no-relay: 수신 node는 local 구독 match에만 fan-out하고 재전파 경로 없음, sender는 peer당 정확히 1회 submit. 중복·loop 부재 전용 test 잔여 |
| S4-14 | Spot local subscription 분리 | channel-scoped 등록·해제·수신 API와 remote subscription 없는 exact/prefix match 동작을 구현하고 public inventory query를 만들지 않음 | 진행 중 | 구현+prefix match test green, inventory query 부재는 surface gate 보증 |
| S4-15 | Actor와 STREAM session owner 전환 | direct Actor mailbox, transfer fence, ActorRef와 bound session 회귀 통과 | 진행 중 | actor 원격 전 경로 구현: wire ACTOR_SEND/REQUEST(ActorRef node RID가 pipe 선택, generation 검증 후 actor mailbox 직접 enqueue), ACTOR_LOOKUP(completion kind_data=location), ACTOR_DESTROY, ACTOR_JOIN(entry flag 포함; accepted reply가 source의 유일한 membership commit point — target은 spot active count만 반영하고 wire reply의 실제 spot rid+generation으로 source가 epoch+1 commit), ACTOR_LEFT one-way(이전 remote Spot의 LEFT record·count 감소). actor_state에 spot_node_rid 추적 추가(원격 membership). `test_mesh_peer_admission` 7/7 green(lookup→request/reply→remote destroy, entry-spot join epoch 2 확인). transfer fence data plane(Phase C)·bound session 잔여 |
| S4-15A | Actor transfer fence·token protocol 구현 | Core prepare가 64-byte sealed token을 발급하고 commit이 이 token, transfer ID, Actor generation과 정확히 다음 membership epoch를 검증한 뒤 mailbox/session fence를 수행한다. deterministic fake location authority로 prepare·commit·activate·abort·stale token contract test 통과 | 진행 중 | 구현: `mesh_transfer_api.cpp` 신설 — source prepare가 active claim 해제 대기→app mailbox 동결(snapshot·final_sequence·reserve 계산)→fence(신규 submit EAGAIN·claim 차단)→64B sealed token, target prepare가 capacity 예약+placeholder actor+wire READY 교환(deadline 봉인), 자동 data plane(wire TRANSFER_DATA seq별 record 직렬화+TRANSFER_ACK contiguous high-water, 동일 key 재전송 1회 staging), 이전된 request의 reply는 target 재봉인 route→wire REPLY_RELAY→source 원 경로 중계. commit: epoch=expected+1 검증(위반 ESTALE)·완료 재호출 idempotent·다른 epoch EALREADY, target commit이 staging 완주 대기, source commit이 route/admission 제거+snapshot 해제. activate: committed target만(EINVAL 게이트)·staged를 app mailbox로 공개·idempotent. abort: 양측 복원/폐기, terminal 재호출 규칙(EALREADY). TRANSFER_CONTROL record(FENCED/PREPARING/COMMITTED/ACTIVATED/ABORTED)를 actor infra lane에 enqueue. fake-authority 2-process contract test green(`test_mesh_peer_admission` 8/8: backlog 2건이 target에서 순서대로 정확히 1회 표면화, fence 후 submit BACKPRESSURED, 오류 격자 전부 검증). 잔여: bound STREAM session participant·post-barrier allowance 협상(현재 allowance 0 동작)·data-plane failure의 TRANSFER_CONTROL 기록 |

### 8.3 삭제와 관측

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-16 | SpotNode mode와 PUB/SUB plane 제거 | mesh_pub, mesh_xsub와 mode branch source no-hit | 완료 | `runtime/services/spot`·`api/spot`·`api/actor`·`api/service`·data plane 삭제, mode enum 제거, test tree 정리 후 core include/src/tests no-hit 0 확인 |
| S4-17 | route bridge와 raw helper 제거 | header, source, build, test와 symbol no-hit | 완료 | bridge source·선언·export·test 제거, symbol no-hit(`contract_public_surface`의 removed-identifiers 검사 + word-boundary 스캔 0 hit) |
| S4-17A | service receive·part API 제거 | channel·Spot send/request/reply·publish, Spot·Actor recv, Actor–STREAM `*_part`와 Actor join/lifecycle 전용 receive·reply symbol을 complete multipart API·Spot control batch로 대체하고 no-hit | 완료 | 제거 76 함수가 removal manifest(`removed-identifiers-10.0.0.json`)에 등재되어 surface gate가 부재를 상시 검증(PASS). 대체 표면=complete multipart receive batch+SPOT_CONTROL record(join/lifecycle 전용 recv 없음, `test_mesh_peer_admission` join case가 batch 경유 검증) |
| S4-18 | remote subscription protocol 제거 | registry, reconnect, control frame과 status no-hit | 완료 | 구 pubsub data plane(remote subject registry·reconnect 재구독·control frame) 일괄 삭제, no-hit 0. 신규 wire는 remote 구독 전파 없이 multicast를 수신 node의 local match로 fan-out |
| S4-19 | 폐기 alias와 forwarding wrapper 제거 | 폐기 이름, Core dispatch worker option과 remote subject query를 전달하는 production code no-hit | 완료 | worker pool·subject registry·dispatch handler 제거, 유지 raw reqrep은 `reqrep_internal`로 추출, alias/wrapper 0(신규 표면은 spec 이름만, surface gate 보증) |
| S4-20 | polling, status와 monitoring 구현 | reviewed S1 정식 spec의 source kind, event와 query test 통과 | 진행 중 | mesh monitor(open/recv/status/close, bounded queue+overflow aggregate, peer/state/multicast/completion event) 구현, node status/peers/peer_channels query green. poller fd 연동 완료: node 내장 signaler를 `poller_subject_mesh_node`로 등록, POLLIN=ready index non-empty(레벨 트리거 — drain_ready가 재무장), event는 `ZLINK_POLLER_SOURCE_MESH_NODE`+node handle 반환, handler↔poller 상호 배타(EBUSY)와 remove 후 재등록 test green(`test_mesh_node_basic` 5/5). event matrix 세부 test 잔여 |
| S4-21 | errno와 result mapping 구현 | 모든 신규 API가 정해진 result를 반환 | 진행 중 | 신규 errno(EALREADY/EDEADLK/ESHUTDOWN 등)와 result enum 상수 정의, 공용 `submit_errno_result` mapping, 주요 경로(EEXIST/ESTALE/EACCES/ENOTCONN/ENOENT/EAGAIN/EALREADY)는 contract test로 green. 전 API×result 전수 mapping test 잔여 |
| S4-22 | 제거 file과 CMake entry 정리 | include되지 않는 source와 orphan target 0개 | 완료 | CMake source 목록에서 제거 115행 정리+신규 mesh 파일 등록, orphan target 0(전체 빌드 green), `core/study/src` 폐기 코드 삭제 |
| S4-22A | owner completion infrastructure 통합 | channel dealer·service per-request callback·Spot reply drain 제거, raw DEALER/ROUTER `zlink_reply_handler_fn` 유지와 in-turn await 통과 | 진행 중 | 구 channel dealer·per-request callback·Spot reply drain은 spot 기계와 함께 삭제(신규 completion은 owner infra mailbox 단일 경로), raw `zlink_reply_handler_fn` 유지(V6 회귀 green). in-turn await 전용 test 잔여 |
| S4-22B | Core version·ABI metadata 갱신 | VERSION, public headers와 CMake project version은 10.0.0, SOVERSION은 10이며 Conan source에는 선택한 `10.0.0-rc.N` URL과 아직 게시하지 않은 stable `10.0.0` URL이 있음 | 완료 | VERSION·header·CMake 10.0.0, SONAME `libzlink.so.10`, conandata에 `10.0.0-rc.1`·미게시 `10.0.0` URL 추가 |
| S4-22C | 10.0.0 release note 작성 | 공개 기능, 지원 환경, package와 검증 결과 명시 | 진행 중 | `CHANGELOG.md` 10.0.0 RC 항목 작성(Added/Removed/Changed/Known gaps). 검증 결과 수치는 S4 완료 시 보강 |
| S4-22D | Core RC/stable workflow 분기 구현 | `build.yml`은 `-rc.N` tag를 prerelease로, stable tag를 release로 게시한다. Conan workflow는 tag에서 RC/stable package version을 구분하고 RC remote upload를 금지하며 stable secret 부재 시 실패 | 완료 | `build.yml` prerelease 분기, `core-conan-release.yml` tag→version 파생·RC upload skip·stable secret 필수화. 실제 run 검증은 S6 |
| S4-22E | Core implementation gap 닫기 | 구현된 header·test를 S1 정식 spec의 MeshNode, Spot, Actor, router, polling, monitoring과 errno 계약에 대조하고 차이를 모두 해소 | 미착수 | - |
| S4-22F | Core 정식 spec parity와 index 검증 | 한국어·영문, service index, public header, errno와 ownership 차이 0개 | 미착수 | - |
### 8.4 Core 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-23 | unit와 contract test | 전체 통과, skip 증가 없음 | 진행 중 | suite 전체 재실행 green 114/114(contract_public_surface·`test_mesh_node_basic` 포함, `test_reconnect_options`는 병렬 flake로 단독 재실행 통과 — S0에서 기록된 기존 flake). mesh 절별 테스트 확대 잔여 |
| S4-24 | integration과 topology test | direct, channel, multicast, reconnect와 drain 통과 | 진행 중 | 2-process topology test(`test_mesh_peer_admission`) 9 case green: admission/readiness/weight 전파, node direct·spot direct request/reply, multicast(NODROP detail), actor lookup/messaging/destroy/join, transfer fence, drain+재연결(peer 종료 시 admitted count 0·inbound peer는 readiness에 불간섭, 동일 RID 재기동 peer 재admission+새 pipe 왕복). 구현 결함 2건 발견·수정: 재연결 시 ERROR intent가 re-HELLO 하지 않던 문제(CONNECTION_READY가 ERROR/ADMITTED intent도 재handshake), inbound peer가 readiness 계산에 포함되던 문제(spec은 intent 기준 — inbound 플래그로 제외). channel 원격 RR 분포 test 잔여 |
| S4-25 | callback·claim·ownership stress | close, rearm, claim leak/revoke, multipart와 reference count 오류 0건 | 미착수 | - |
| S4-26 | sanitizer와 race 검증 | ASAN/UBSAN/TSAN 적용 범위에서 신규 오류 0건 | 진행 중 | ASAN/UBSAN+leak 전체 suite(80): 초회 11건 적발 → 전부 해소. 결함 2건 수정: ① `request_timeout_scheduler_internal.cpp` exit-시 heap-UAF(detached timeout thread vs static 소멸자) → immortal singleton, ② `msg.cpp` slice_content_pool thread_local 캐시가 thread 종료 시 엔트리 미해제 → 소멸자에서 free. 테스트측 누수 2건 수정: backpressure matrix 헬퍼 send 실패 시 part 미close, testutil_unity finalize_recv의 malloc 배열 미해제(thread-local 버퍼 직접 반환으로 교체). 최종 전체 suite 재확인 green(80/80), mesh 2-process test ASAN clean. transfer 구현 직후 ASAN이 신규 결함 1건 추가 적발·수정: wire reply tail 파싱이 envelope frame close 뒤 해제된 버퍼를 읽는 heap-UAF(`mesh_wire.cpp` dispatch — release에서는 우연히 통과) → tail을 close 전에 복사. 수정 후 ASAN+leak `test_mesh_peer_admission` 8/8 green. TSAN 실행 완료(mesh test 5/5·2-process 8/8 통과, 2-process는 ASLR off 필요): mesh 신규 코드의 race 0건. 유지 기계에서 3계열 검출·S5 검토 대상으로 기록 — ① `part_helper_state` check-then-set이 무동기(9.x부터, thread-safe send 계약 하 동시 최초 send 시 state 유실 가능; perf 핫패스라 벤치 없는 수정 보류), ② socket 생성 경로 auto-HWM plan의 lock-order-inversion(잠재 deadlock, 9.x 동일), ③ mailbox ypipe 계열 race 경고(무주석 lock-free 동기화의 TSAN 한계로 추정) |
| S4-27 | 1천·1만 peer benchmark | connection, lookup, multicast, reconnect 결과 기록 | 진행 중 | multi-process topology bench 신설(`core/tests/bench/bench_mesh_topology.cpp`, ctest `bench_mesh_topology` 16 peer): hub가 N개 fork peer를 admit→admission 수렴/원격 actor lookup 평균/NODROP multicast fan-out/drain 시간을 BENCH 라인으로 기록. in-process 다중 peer는 프로세스당 MeshName 1개 제약으로 불가하므로 fork 방식 채택(matrix §7의 in-process 대체 증거 항목을 이 설계로 대체). **구현 결함 1건 발견·수정**: multicast 원격 NODROP의 pipe 쓰기가능 probe(`routed_target_writable`)가 process_commands 없이 out-pipe 맵을 읽어 갓 admit된 peer의 pipe-attach 커맨드 미반영 stale 관측→간헐 publish 실패(errno EAGAIN/ETIMEDOUT). send 경로처럼 probe 진입 시 process_commands 선행하도록 수정. peer 수 sweep 측정치는 이 행에 계속 기록. 1천/1만은 WSL fd·메모리 한계 검토 후 대표 N으로 |
| S4-28 | mixed traffic 성능 검증 | request p99와 resource가 S0 threshold 통과 | 진행 중 | perf harness 10.0.0 이식 완료: `zlink_router_recv_part` 신규 signature 반영, SPOT 패턴 6종·spot-node auto-HWM 스냅샷 기계·러너 SPOT 분기 제거, bindings/c/include를 core 10.0.0 헤더로 미러링, 러너 정책 test 36/36 green, perf 전 타깃 빌드 green. 유지 패턴 ROUTER_ROUTER_REQREP(tcp, 64/1024B, runs 3·duration 3·clients 100) 정지 상태 2회 측정: throughput 64B 98.5%/98.2%·1024B 93.5%/93.2% (S0 §8.4 대비, 게이트 90% 통과). p99는 동일 코드 정지 샘플 간 28% 요동(1024B 0.536↔0.684ms)으로 이 WSL 호스트에서 120% 게이트의 분해능 밖 — 각 size 최량 샘플은 64B 112%·1024B 114.5%로 통과, S4 prep 시점의 동일 코드 123~124% 초과 기록과 일치. 증거=results/multi/report/perf_c_multi_linux_20260717_{061953,062119}_v10_s4_gate_quiet*.txt (061831 첫 실행은 ASAN 빌드 경합 load 152로 무효 처리). MeshNode 신설 패턴은 S4-27과 함께 확정. **성능 리뷰 항목(S5 I2, POSD)**: ① `wire_send_mutex`가 node 발신 전체를 단일 mutex로 직렬화(정확성 우선 1차 설계 — NODROP 원자성 요구 지점만 좁은 구간으로 축소 후보, 벤치 동반 필수) ② NODROP multicast의 blocking send가 lock 보유 중 SNDTIMEO×N까지 지연 가능 ③ transfer data plane이 ACK당 1 record stop-and-wait(윈도우화 후보) ④ ingress 20ms poll 주기의 지연 하한. 모두 계약 green 확보 후 S0 게이트(throughput 90%/p99) 재측정과 함께 개선 |
| S4-29 | install과 package consumer | 설치 header와 shared library로 clean consumer 통과 | 진행 중 | staging 설치 + C11 clean consumer가 single-node RouteMesh round trip 통과(`C ABI SMOKE PASS (zlink 10.0.0)`). 설치 규칙의 header 충돌(zlink/common.h ↔ service/common.h 동일 목적지) 결함 수정 |
| S4-30 | 삭제 범위 최종 no-hit | v10 plan·review record의 삭제 추적만 제외하고 source, 현재 계약·guide·internals, test, build와 package에서 제거 symbol·enumerator·macro·metadata 부재 | 진행 중 | core include/src/tests no-hit 0 확인(word-boundary 스캔). guide/internals·bindings 범위 잔여 |

### 8.5 구현 검증 후 Core internals 확정

`internals`는 계획 단계의 예상 구조를 기록하지 않는다. S4-23부터 S4-29까지의 기능, 구조, stress,
sanitizer, 성능과 package 검증이 통과해 실제 구현 구조가 확정된 뒤에만 갱신한다. 갱신한 문서는 source와
구조 test에 다시 대조하며, 이 검증이 실패하면 S4 구현 단계로 돌아간다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-31 | Core internals 갱신 | 검증된 실제 ROUTER 배선, mailbox·ready·claim·batch, lock·thread와 Actor transfer 경계를 `core/doc/internals/`에 반영하고 source·구조 test·다이어그램의 차이 0개 | 미착수 | - |
| S4-32 | Core internals current-state 검증 | 계획의 대안이나 미구현 목표를 현재 구조로 서술하지 않았으며 제거된 SpotNode·bridge·PUB/XSUB 구조가 현재 설명에 남지 않음 | 미착수 | - |

S4 완료 gate:

- [ ] Core 기능, 삭제, 회귀, stress, sanitizer와 성능 검증이 모두 통과한다.
- [ ] 구현된 `core/include/zlink.h` 공개 계약과 Core 정식 spec의 한국어·영문이 일치한다.
- [ ] Core internals가 구현 뒤 갱신되었고 실제 소켓, queue, thread와 lifecycle 구조를 정확히 설명한다.
- [ ] 실패를 숨기는 sleep, retry-only workaround, raw frame과 test 전용 우회가 없다.
- [ ] S5 review manifest에 필요한 revision과 전체 검증 결과가 준비되어 있다.

## 9. S5 — Core 구현 3축 독립 리뷰와 수정 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S5-01 | Core revision과 검증 결과 동결 | manifest에 source·test·package·benchmark 범위 기록 | 미착수 | - |
| S5-02 | Codex agent Core 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S5-03 | Claude Sonnet Core 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S5-04 | 정확성·누락 finding 수정 | spec과 다른 동작, 빠진 test·상태·오류를 보완 | 미착수 | - |
| S5-05 | POSD 위험 신호 목록 작성 | 각 항목의 위반 원칙과 두 설계안 기록 | 미착수 | - |
| S5-06 | 의미 있는 리팩터링 수행 | 선택 이유와 호출자 복잡도 감소 근거 기록 | 미착수 | - |
| S5-07 | DDD event와 경계 재검토 | lifecycle, membership, dispatch, ownership과 observation 책임 정리 | 미착수 | - |
| S5-08 | dead code와 file 제거 | 도달 불가능 branch, 미사용 type·helper·target·file no-hit | 미착수 | - |
| S5-09 | 전체 Core 검증 재실행 | S4-23~S4-30 결과가 리팩터링 뒤에도 통과 | 미착수 | - |
| S5-10 | 두 리뷰어 전체 재리뷰 | 어느 축을 수정했든 Core 전체 scope와 I1·I2·I3 전부 재검토 | 미착수 | - |

Core 구현 리뷰는 §2.1의 I1·I2·I3를 각각 판정한다. 다음 항목은 축별 최소 검토 범위다.

- 정식 Core spec의 함수·type·enum·ownership·error 계약 누락 또는 오구현
- concurrency, close, reentrancy, partial submit과 backpressure race
- local Spot queue reference count와 remote pipe message lifetime
- route bridge, PUB/SUB plane, old mode, alias와 제거 file 잔존
- 패스스루 method, 얕은 adapter, 시간적 분해와 중복 policy owner
- MeshNode, peer selection, destination dispatch, Actor/session과 transport 책임 혼합
- 이름만 DDD 형태인 manager/service와 실제 지식을 소유하지 않는 class
- 사용되지 않는 code, test fixture, build target, generated file와 monitor branch
- benchmark 또는 E2E를 통과시키기 위한 특수 경로와 숨은 retry
- 10.0.0 version, SOVERSION, Conan metadata와 release note 누락 또는 불일치

S5 완료 gate:

- [ ] open Core finding이 0개다.
- [ ] 두 리뷰어의 I1에 spec 누락·오구현·동작 불일치 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I2에 POSD 위험 신호·DDD 경계·의미 있는 리팩터링 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I3에 불필요·죽은 code·file·호환 잔재 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 Core 전체 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] Codex agent 결과 마지막 줄이 `CORE REVIEW CLEAN`이다.
- [ ] Claude Sonnet 결과 마지막 줄이 `CORE REVIEW CLEAN`이다.

## 10. S6 — Core 10.0.0 release-candidate build와 pre-release 배포

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S6-01 | 리뷰된 version source 불변성 확인 | S5 clean commit의 VERSION, 두 public header와 CMake가 tag commit과 동일 | 미착수 | - |
| S6-02 | 리뷰된 ABI와 Conan metadata 불변성 확인 | S5 clean commit의 SOVERSION 10과 선택한 `10.0.0-rc.N` source가 RC tag commit과 일치하며 stable `10.0.0` URL은 S11 전까지 resolve하지 않음 | 미착수 | - |
| S6-03 | 리뷰된 release note 불변성 확인 | S5 clean 뒤 공개 기능과 검증 결과 변경 없음 | 미착수 | - |
| S6-03A | RC/stable workflow guard 검증 | RC tag의 `prerelease=true`, `zlink/10.0.0-rc.N` create와 Conan upload skip, stable tag의 `zlink/10.0.0` create와 publish-required failure test 통과 | 미착수 | - |
| S6-04 | RC 전 local gate 실행 | clean build, full test, 선택한 RC source entry의 local Conan create, package, symbol, SONAME와 perf 통과 | 미착수 | - |
| S6-05 | RC commit과 tag 생성 | 검증된 commit에 순번 `core/v10.0.0-rc.N` tag 생성·push. stable tag 없음 | 미착수 | - |
| S6-06 | native build workflow 감시 | `.github/workflows/build.yml`이 RC tag/commit으로 성공 | 미착수 | - |
| S6-07 | GitHub pre-release 검증 | RC native asset을 prerelease로 게시하고 stable Conan remote publish는 실행하지 않음 | 미착수 | - |
| S6-08 | RC asset 검증 | platform archive, checksum, source archive와 header 일치 | 미착수 | - |
| S6-09 | shared library 검증 | filename 10.0.0, SONAME 10과 제거 symbol 부재 | 미착수 | - |
| S6-10 | local Conan package 설치 검증 | 실제 RC tag source로 만든 isolated local `zlink/10.0.0-rc.N` consumer가 build·실행하고 stable `zlink/10.0.0`은 public remote에 없음 | 미착수 | - |

S6 완료 gate:

- [ ] native GitHub Actions와 실제 RC pre-release artifact, local Conan package가 모두 검증되었다.
- [ ] 실패 또는 publish 생략을 workflow 성공으로 오판하지 않았다.
- [ ] 검증된 Core 10.0.0 RC artifact URL, source SHA와 checksum이 S7 입력으로 기록되어 있다.
- [ ] `core/v10.0.0` stable tag, stable GitHub Release와 Conan remote package는 아직 존재하지 않는다.

## 11. S7 — bindings 적용, 리뷰와 local package E2E smoke

### 11.1 공통 적용

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-00 | RC tag parser와 runtime version 분리 | local-package script가 `core/v10.0.0-rc.N` asset tag는 그대로 사용하고 C/header runtime version은 숫자 `10.0.0`으로 기록하며 version macro에 `-rc.N` suffix를 남기지 않음 | 미착수 | - |
| S7-00A | RC fixture·provenance test | RC/stable tag fixture가 version marker, source SHA, checksum과 asset URL을 검증하고 malformed tag·suffix 잔존·checksum 불일치에서 실패 | 미착수 | - |
| S7-01 | Core RC artifact 동기화 | update-zlink-libs script가 `core/v10.0.0-rc.N` asset의 runtime version 10.0.0, source SHA와 checksum을 검증하고 복사 | 미착수 | - |
| S7-02 | binding API inventory 작성 | C ABI, C++, .NET, Java, Node, Python, Go, Rust 전체 대응 | 미착수 | - |
| S7-03 | 제거 wrapper와 generated API 정리 | SpotNode mode, bridge, Core dispatch worker option, remote subject query, Spot·Actor–STREAM service `*_part`, Actor join/lifecycle 전용 receive·reply, channel-dealer event와 old alias no-hit. 모든 raw socket용 channel metadata wrapper 유지 확인 | 미착수 | - |
| S7-04 | MeshNode API 구현 | lifecycle, peer, node/channel call·one-shot reply, direct와 publisher의 optional application metadata frame, ready/claim/batch와 publisher 공개 | 미착수 | - |
| S7-04A | claim·batch wrapper 수명 구현 | deterministic release, destroy 뒤 finalizer release, borrowed metadata/payload view와 retain contract 통과 | 미착수 | - |
| S7-05 | ownership과 error mapping 구현 | Core contract와 언어별 예외·result 의미 일치 | 미착수 | - |
| S7-06 | source/package API snapshot 갱신 | 10.0.0 공개 표면과 native payload 일치 | 미착수 | - |
| S7-07 | bindings release workflow 수정 | RC와 최종 `core/v10.0.0`의 동일 source SHA·checksum 검증 및 release asset 사용, tag run에도 provenance 필수 | 미착수 | - |
| S7-08 | `.NET` native 입력 경로 통일 | workflow와 pack이 `bindings/dotnet/native/<rid>/`만 source 입력으로 사용 | 미착수 | - |

### 11.2 언어별 검증

| ID | 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-C | C ABI consumer | public header, shared library와 C smoke 통과 | 미착수 | - |
| S7-CPP | C++ bindings | contract, package consumer와 E2E smoke 통과 | 미착수 | - |
| S7-DN | .NET bindings | contract, NuGet consumer와 E2E smoke 통과 | 미착수 | - |
| S7-J | Java bindings | contract, package consumer와 E2E smoke 통과 | 미착수 | - |
| S7-N | Node.js bindings | contract, npm consumer와 E2E smoke 통과 | 미착수 | - |
| S7-PY | Python bindings | contract, wheel consumer와 E2E smoke 통과 | 미착수 | - |
| S7-GO | Go bindings | contract, module consumer와 E2E smoke 통과 | 미착수 | - |
| S7-RS | Rust bindings | contract, crate consumer와 E2E smoke 통과 | 미착수 | - |
| S7-SMOKE | 공통 smoke matrix | node/channel/Spot direct send/request와 Logical Multicast metadata snapshot·malformed·1024 경계·relay·reply 비자동복사, NODROP, batch reset/retain과 shutdown 통과 | 미착수 | - |

### 11.3 bindings 독립 리뷰와 외부 배포 전 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-15 | Codex agent bindings 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S7-16 | Claude Sonnet bindings 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S7-17 | finding 수정과 전체 재검증 | 영향 언어와 package smoke 재실행 후 두 리뷰어의 I1·I2·I3 전체 재리뷰 | 미착수 | - |
| S7-18 | 두 리뷰어 전체 재리뷰 반복 | 세 축과 두 stage 결과가 모두 `CLEAN`; 둘 다 `BINDINGS REVIEW CLEAN` | 미착수 | - |
| S7-19 | local package 묶음 검증 | publish-all-wsl 및 별도 언어 package 검증 통과 | 미착수 | - |
| S7-20 | `all` target 배포 없는 workflow 검증 | create_release=false, publish_registry=false로 전체 job 성공 | 미착수 | - |
| S7-21 | 언어별 외부 배포 manifest 준비 | tag, 실제 배포 채널, credential, 설치·smoke 명령과 rollback 기록 | 미착수 | - |
| S7-22 | framework pin 입력 기록 | 검증된 local package version, checksum과 경로 확보 | 미착수 | - |

bindings 리뷰에는 reflection/private symbol, raw frame, fallback symbol lookup, 폐기된 native payload,
도달 불가능 wrapper, 중복 DTO와 언어별 임시 public API 검사를 반드시 포함한다. POSD 관점에서는
패스스루 wrapper, 반복되는 ownership·error mapping과 얕은 native adapter를 검토한다. DDD 관점에서는
MeshNode, Spot, Actor와 session 개념이 언어별 transport 세부와 섞이지 않았는지 확인한다. 사용하지
않는 generated code, native declaration, mock, test fixture와 package file도 삭제 대상으로 검토한다.

위 목록은 하나의 POSD·DDD 판정으로 합치지 않는다. I1은 spec/API·ownership·동작 parity, I2는 의미
있는 POSD·DDD 리팩터링 잔여, I3는 불필요·죽은 wrapper·generated code·package file·호환 잔재를
각각 finding·evidence·축별 판정으로 기록한다.

S7 완료 gate:

- [ ] 모든 bindings local package E2E smoke가 통과한다.
- [ ] open bindings finding이 0개다.
- [ ] 두 리뷰어의 I1·I2·I3 각각에 finding 또는 `없음`, evidence와 `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 bindings 전체 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] Codex agent와 Claude Sonnet 결과가 모두 `BINDINGS REVIEW CLEAN`이다.
- [ ] 외부 immutable 10.0.0 package는 아직 공개하지 않았다.

## 12. S8 — `.NET framework`, sample과 E2E 적용 및 리뷰

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-01 | 검증된 bindings local package pin을 10.0.0으로 갱신 | 중앙 version과 lock·restore 결과 일치 | 미착수 | - |
| S8-02 | AddRouteMesh·ChannelName 구현 | 복수 logical membership과 정식 `.NET` interface가 source snapshot과 일치 | 미착수 | - |
| S8-02A | RouteMesh runtime-options DI 구현 | 기존 `IZLinkChannelRuntimeOptions` 제거, `IZLinkRouteMeshRuntimeOptions` singleton 등록, MeshNode socket setter의 startup-only 오류와 runtime channel Weight 반영 통과 | 미착수 | - |
| S8-03 | MeshNode-owned handler·Spot·Actor 등록 구현 | channel·route handler context와 모든 Spot·Actor builder 멤버 보존 | 미착수 | - |
| S8-04 | location descriptor와 connection planner 구현 | Redis 자동 discovery와 manual `IZLinkMeshPeerConnections`가 같은 admission을 사용하고 MeshName 범위, expected RID pin, lifecycle generation, descriptor revision, source 병합과 ready index 검증 | 미착수 | - |
| S8-04A | Redis Actor transfer authority 구현 | participant-set CAS, transfer token, lease, prepared/commit/abort crash recovery, unsupported store startup failure와 distributed transfer E2E 통과 | 미착수 | - |
| S8-04B | Redis production 기본 정책 구현 | Redis extension 명시 등록, 자동 discovery·분산 Spot/Actor 주소 조회를 사용하면서 location store를 등록하지 않은 구성의 startup failure, 사용자 store capability와 test-only in-memory 경계 검증 | 미착수 | - |
| S8-05 | channel/direct/Spot/Actor 전송 연결 | bindings MeshNode public API만 사용 | 미착수 | - |
| S8-06 | ready/claim pump 구현 | infrastructure 우선 drain, Node·Spot·Actor keyed scheduling과 claim leak 0건 | 미착수 | - |
| S8-06A | S/S metadata 연결 | Node·Channel·Spot direct send/request와 Logical Multicast의 mutation snapshot, immutable handler view, malformed ingress, 1024 경계, relay allowlist, reply 비자동복사와 일반 reply metadata 미지원 통과 | 미착수 | - |
| S8-06B | Spot timer 연결 | `Task.Delay` 기반 tick이 lifecycle generation과 cancel 규칙을 거쳐 keyed scheduler에 제출되고 Core timer FFI를 사용하지 않음 | 미착수 | - |
| S8-07 | Logical Multicast와 NoDrop 연결 | 기본 true와 명시적 false 회귀 통과 | 미착수 | - |
| S8-08 | 기존 topology API와 runtime 제거 | v10 plan·review record만 제외하고 builder, registration, production `UseInMemoryLocationStores()`, bridge, Spot·Actor–STREAM service part와 Actor join/lifecycle 전용 wrapper, test와 현재 docs no-hit | 미착수 | - |
| S8-09 | `.NET` sample 전환 | 분산 sample은 공식 Redis extension을 등록하고 지정된 manual sample은 `IZLinkMeshPeerConnections`를 사용하며 S2 inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 미착수 | - |
| S8-10 | `.NET` framework E2E 전환 | `e2e/run_e2e_all.sh`의 전체 config 통과 | 미착수 | - |
| S8-11 | source/package contract 검증 | `scripts/verify_packaged_contract.sh`, NuGet consumer와 native payload 일치 | 미착수 | - |
| S8-12 | 성능과 resource 회귀 | mixed traffic p99와 fanout baseline gate 통과 | 미착수 | - |
| S8-12A | `.NET` guide와 internals 갱신 | 구현·sample·E2E·package·성능 검증이 모두 통과한 뒤 guide에는 사용 계약만, internals에는 검증된 실제 pump·scheduler·location·timer 구조만 반영하고 source·구조 test·정식 spec과 대조 | 미착수 | - |
| S8-13 | Codex agent `.NET` 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S8-14 | Claude Sonnet `.NET` 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S8-15 | finding 수정·전체 검증·재리뷰 반복 | 어느 축을 수정했든 두 리뷰어가 세 축 전체를 재검토하고 둘 다 `DOTNET REVIEW CLEAN` | 미착수 | - |
| S8-16 | process-local MeshName uniqueness 검증 | 중복 AddRouteMesh 실패와 multi-mesh 독립 동작 통과 | 미착수 | - |

S8 구현 리뷰는 §2.1의 I1·I2·I3를 독립 판정하고 framework 책임에 맞게 다음을 추가한다.

- Core selection과 peer index를 framework에서 다시 구현하지 않았는지 확인
- typed JSON codec 책임을 handler나 sample에 전달하지 않았는지 확인
- location, lifecycle, callback과 transport 지식이 여러 runtime에 중복되지 않았는지 확인
- sample이 endpoint, peer RID, raw frame과 내부 type을 직접 다루지 않는지 확인
- 제거된 public API, fake, fixture, source comment와 package snapshot이 남지 않았는지 확인

S8 완료 gate:

- [ ] 두 리뷰어의 I1에 `.NET` exact interface와 구현·sample·E2E·package의 누락·오구현·동작 불일치
  finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I2에 POSD·DDD 관점의 의미 있는 리팩터링 잔여 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I3에 불필요·죽은 code·file·test·호환 잔재 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 `.NET` 전체 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] 두 리뷰어 결과가 모두 `DOTNET REVIEW CLEAN`이다.

## 13. S9 — C++, Java/Kotlin과 Node.js 병렬 적용

### 13.1 병렬 작업 격리

| 원칙 | 적용 방법 |
|---|---|
| file ownership | C++, JVM, Node lane은 자기 언어 source·test·sample·언어별 문서만 수정 |
| 공통 문서 | common spec과 main plan은 coordinator만 수정하고, 이 진행표는 각 lane이 배정받은 ID 행만 수정 |
| 진행 기록 | 각 lane이 자기 ID 행의 상태와 증거를 이 진행표에 직접 기록한다. 상세 log를 별도로 남겨도 현재 상태는 이 진행표에만 기록 |
| build output | 각 lane은 독립 build directory와 package cache를 사용 |
| JVM 제한 | bindings/java와 framework/languages/java build를 동시에 실행하지 않고 Gradle `--no-parallel` 사용 |
| Node 제한 | stale `dist`를 제거하고 build 완료 뒤 test를 순차 실행 |
| C++ 제한 | 전용 CMake build directory에서 package consumer와 CTest 실행 |
| merge | lane별 검증 commit을 따로 만들고 공통 tree에 한 lane씩 병합·재검증 |

### 13.2 C++ lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-C01 | bindings local package 10.0.0 pin | CMake central version과 package resolve 확인 | 미착수 | - |
| S9-C02 | RouteMesh/MeshNode interface 구현 | C++ 정식 interface와 source 일치 | 미착수 | - |
| S9-C02A | C++ metadata·timer 연결 | S/S metadata 전체 공통 matrix와 C API Spot timer adapter가 handler·generation·cancel 계약 통과 | 미착수 | - |
| S9-C02B | C++ Actor transfer authority 연결 | 정식 store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 미착수 | - |
| S9-C02C | C++ location 기본 정책 연결 | 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 미착수 | - |
| S9-C03 | 기존 topology 제거 | alias, runtime, test와 sample no-hit | 미착수 | - |
| S9-C04 | sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 미착수 | - |
| S9-C05 | contract·package·E2E 검증 | CTest, verify_packaged_contract와 `e2e/run_e2e_all.sh` 통과 | 미착수 | - |
| S9-C06 | C++ guide와 internals 갱신 | 구현·sample·E2E·package 검증 통과 뒤 검증된 실제 adapter·scheduler·timer 구조와 사용 계약을 문서 종류에 맞게 반영하고 source·구조 test와 대조 | 미착수 | - |

### 13.3 Java/Kotlin lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-J01 | bindings local package 10.0.0 pin | version catalog와 dependency resolve 확인 | 미착수 | - |
| S9-J02 | Java RouteMesh/MeshNode 구현 | Java 정식 interface와 source 일치 | 미착수 | - |
| S9-J03 | Kotlin interface와 DSL 구현 | Kotlin 정식 interface와 source 일치 | 미착수 | - |
| S9-J03A | JVM metadata·timer 연결 | Java/Kotlin S/S metadata 전체 공통 matrix와 `ScheduledExecutorService` timer가 immutable context·keyed scheduler 계약 통과 | 미착수 | - |
| S9-J03B | JVM Actor transfer authority 연결 | Java/Kotlin store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 미착수 | - |
| S9-J03C | JVM location 기본 정책 연결 | Java/Kotlin 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 미착수 | - |
| S9-J04 | 기존 topology 제거 | Java/Kotlin alias, runtime, test와 sample no-hit | 미착수 | - |
| S9-J05 | Java/Kotlin sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 미착수 | - |
| S9-J06 | contract·package·E2E 검증 | Gradle, verify_packaged_contract, `framework/languages/java/e2e/run_e2e_all.sh`와 `framework/languages/java/e2e-kotlin/run_e2e_all.sh` 모두 통과 | 미착수 | - |
| S9-J07 | Java/Kotlin guide와 internals 갱신 | 구현·sample·E2E·package 검증 통과 뒤 검증된 실제 pump·scheduler·location·timer 구조와 사용 계약을 문서 종류에 맞게 반영하고 source·구조 test와 대조 | 미착수 | - |

### 13.4 Node.js lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-N01 | bindings local package 10.0.0 pin | package와 lockfile이 같은 version을 resolve | 미착수 | - |
| S9-N02 | RouteMesh/MeshNode interface 구현 | Node 정식 interface와 source snapshot 일치 | 미착수 | - |
| S9-N02A | Node metadata·timer 연결 | S/S metadata 전체 공통 matrix와 `setTimeout` timer가 immutable context·generation·cancel·keyed scheduler 계약 통과 | 미착수 | - |
| S9-N02B | Node Actor transfer authority 연결 | Node store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 미착수 | - |
| S9-N02C | Node location 기본 정책 연결 | 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 미착수 | - |
| S9-N03 | 기존 topology 제거 | alias, runtime, test와 sample no-hit | 미착수 | - |
| S9-N04 | sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 미착수 | - |
| S9-N05 | contract·package·E2E 검증 | verify_packaged_contract, clean npm consumer와 `e2e/run_e2e_all.sh` 통과 | 미착수 | - |
| S9-N06 | Node.js guide와 internals 갱신 | 구현·sample·E2E·package 검증 통과 뒤 검증된 실제 pump·scheduler·location·timer 구조와 사용 계약을 문서 종류에 맞게 반영하고 source·구조 test와 대조 | 미착수 | - |

S9 완료 gate:

- [ ] 세 lane이 자기 file scope만 수정했다.
- [ ] 각 lane의 sample, E2E, package consumer와 stale no-hit가 통과한다.
- [ ] 공통 spec 변경이 필요해진 경우 구현을 멈추고 S2·S3 계약 review를 다시 연다.

## 14. S10 — 언어별 병렬 독립 리뷰 반복

세 언어 lane의 리뷰는 서로 병렬 실행할 수 있다. 각 lane 안에서는 Codex agent와 Claude Sonnet 리뷰를
같은 revision으로 병렬 실행한다. reviewer는 다른 lane을 수정하지 않는다.

| ID | Lane | Codex 결과 | Claude Sonnet 결과 | open finding | 상태 | 증거 |
|---|---|---|---|---:|---|---|
| S10-C | C++ | 대기 | 대기 | 0 | 미착수 | - |
| S10-J | Java/Kotlin | 대기 | 대기 | 0 | 미착수 | - |
| S10-N | Node.js | 대기 | 대기 | 0 | 미착수 | - |

각 lane은 다음 검토를 반복한다.

- 정식 언어 interface와 구현·package snapshot 일치
- Core/.NET 기준과 관찰 가능한 기능 parity
- sample과 E2E inventory 누락
- internal/private API, raw frame, codec 우회와 언어 전용 임시 public API
- POSD 위험 신호와 DDD 책임 중복
- 불필요하거나 도달 불가능한 code, file, test와 build target
- 제거 API, alias, 현재 계약·guide·internals, sample과 package entry 잔존
- clean package consumer, E2E와 runner의 실제 실행 증거

각 lane의 목록도 §2.1의 I1·I2·I3로 분리한다. I1은 정식 interface 대비 누락·오구현·관찰 가능한
동작 불일치, I2는 POSD·DDD 관점의 의미 있는 리팩터링 잔여, I3는 불필요·죽은 code·file·test·build
target·호환 잔재를 판정한다. 각 리뷰어와 각 축마다 finding 또는 `없음`, evidence와
`CLEAN`/`NOT CLEAN`을 기록한다. 어느 축을 수정해도 그 lane의 두 리뷰어가 I1·I2·I3 전체를 다시
검토한다.

Java/Kotlin lane은 매 review iteration에서 Java
`framework/languages/java/e2e/run_e2e_all.sh`와 Kotlin
`framework/languages/java/e2e-kotlin/run_e2e_all.sh`를 각각 실행하고 결과를 분리해 기록한다.

언어별 종료 문구:

- C++: `CPP REVIEW CLEAN`
- Java/Kotlin: `JVM REVIEW CLEAN`
- Node.js: `NODE REVIEW CLEAN`

S10 완료 gate:

- [ ] 세 lane 모두 open finding이 0개다.
- [ ] 세 lane 모두 두 리뷰어의 I1·I2·I3 각각에 finding 또는 `없음`, evidence와 `CLEAN` 판정이 있다.
- [ ] 세 lane 모두 두 리뷰어의 해당 clean 문구가 있다.
- [ ] 어느 축의 finding이든 수정한 뒤 각 lane의 전체 package·sample·E2E를 다시 실행하고 두 리뷰어가
  해당 lane의 I1·I2·I3 전체를 재검토했다.

## 15. S11 — Core stable·bindings 외부 배포, 전체 최종 검토와 종료

### 15.1 전체 통합 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-01 | 모든 lane 병합과 clean checkout 확인 | 의도하지 않은 file과 미병합 변경 0개 | 미착수 | - |
| S11-00A | Core stable revision 동결 | 최종 source가 마지막 S6 RC와 같은 commit이고 S7~S10 동안 Core source·ABI 변경이 없음 | 미착수 | - |
| S11-00B | Core stable tag와 외부 배포 | 같은 commit에 `core/v10.0.0` tag를 붙이고 native build와 Conan release workflow 성공 | 미착수 | - |
| S11-00C | Core stable artifact 검증 | GitHub Release, checksums, headers, SONAME, symbols와 remote `zlink/10.0.0` consumer 통과 | 미착수 | - |
| S11-00D | stable Core 기반 bindings 재검증 | stable asset으로 native payload를 다시 동기화하고 S7 package·공통 E2E smoke 결과가 RC와 일치 | 미착수 | - |
| S11-01A | bindings 외부 배포 revision 동결 | S7 clean source, Core stable release SHA와 package checksum이 manifest와 일치 | 미착수 | - |
| S11-01B | 언어별 10.0.0 tag 배포 | cpp, dotnet, java, node, python, go, rust workflow 정상 종료 | 미착수 | - |
| S11-01C | 실제 배포 채널 확인 | NuGet, Maven, npm, PyPI, crates.io, Go tag/module과 C++ GitHub Release 존재 | 미착수 | - |
| S11-01D | 배포 package E2E smoke | 각 채널의 새 package를 빈 workspace에 설치해 공통 smoke 통과 | 미착수 | - |
| S11-01E | framework 배포 package 재검증 | local package pin을 같은 10.0.0 배포 package로 바꾸어 package·sample·E2E 통과 | 미착수 | - |
| S11-02 | 정식 spec과 public API 전수 대조 | Core, bindings와 framework 언어별 차이 0개 | 미착수 | - |
| S11-03 | 제거 항목 repository no-hit | 허용된 v10 plan·review record 외 stale 이름 0개 | 미착수 | - |
| S11-04 | Core 전체 검증 재실행 | build, test, sanitizer, perf와 package consumer 통과 | 미착수 | - |
| S11-05 | bindings 배포 채널 smoke 재실행 | 모든 배포 package E2E 통과 | 미착수 | - |
| S11-06 | `.NET` 전체 검증 재실행 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-07 | C++ 전체 검증 재실행 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-08 | Java/Kotlin 전체 검증 재실행 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-09 | Node.js 전체 검증 재실행 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-10 | classic fanout과 generic STREAM 회귀 | Actor binding 확장점을 제외한 비변경 socket 기능의 public 동작과 baseline 유지 | 미착수 | - |
| S11-11 | docs, link와 sample API 검증 | 깨진 link, stale 예제와 내부 구현 노출 0개 | 미착수 | - |
| S11-12 | version과 artifact 대조 | Core·bindings 10.0.0과 framework pin 일치 | 미착수 | - |

### 15.2 최종 독립 리뷰

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-13 | Codex agent 전체 리뷰 | 전체 scope의 I1·I2·I3 각각에 finding·evidence·축별 판정 | 미착수 | - |
| S11-14 | Claude Sonnet 전체 리뷰 | 같은 전체 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S11-15 | final finding 수정과 영향 검증 | finding 관련 stage와 downstream 검증 뒤 두 리뷰어의 세 축 전체 재리뷰 | 미착수 | - |
| S11-16 | 전체 scope 재리뷰 반복 | 세 축이 모두 clean이고 두 리뷰어가 모두 `FINAL REVIEW CLEAN` | 미착수 | - |
| S11-17 | post-push origin 재검증 | origin commit, tag, workflow와 artifact가 local 증거와 일치 | 미착수 | - |
| S11-18 | 완료 보고서 작성 | 모든 stage 증거, 남은 issue 0과 최종 SHA 기록 | 미착수 | - |

S11 완료 gate:

- [ ] final Core tag가 마지막 RC와 같은 source commit을 가리키고 GitHub Release와 Conan remote package가 검증되었다.
- [ ] 두 리뷰어의 I1에 Core spec부터 bindings·모든 framework 언어·sample·E2E·package·release artifact의
  누락·오구현·동작 불일치 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I2에 전체 구조의 POSD·DDD 관점에서 의미 있는 리팩터링 잔여 finding, evidence와
  `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I3에 제거 대상과 불필요·죽은 code·file·API·test·문서·호환 잔재 finding, evidence와
  `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 전체 통합 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] Codex agent 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
- [ ] Claude Sonnet 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
- [ ] open finding, skipped required test와 검증되지 않은 배포 artifact가 0개다.

## 16. 차단, 재개와 이전 stage 재개방 규칙

- 정식 spec을 바꿔야 하는 구현 finding은 현재 stage에서 임시 처리하지 않고 S1 또는 S2를 다시 연다.
- 공통 계약을 바꾸면 S3 문서 리뷰를 다시 통과한 뒤 downstream stage를 재검증한다.
- S6 RC 뒤 Core ABI나 동작을 바꾸면 stable tag를 만들지 않고 새 commit으로 S5, 새 `rc.N+1`로 S6,
  S7과 모든 framework stage를 다시 통과한다.
- bindings 공개 계약이나 native payload를 바꾸면 S7 review와 local package smoke를 다시 통과한다.
- `.NET` 구현에서 공통 계약 gap이 발견되면 S2·S3을 다시 열고 S8 완료를 취소한다.
- 병렬 lane에서 공통 문제를 발견하면 한 lane의 helper로 우회하지 않고 coordinator가 계약 stage를
  재개방한다.
- reviewer 또는 GitHub Actions·registry를 사용할 수 없으면 관련 stage를 `차단`으로 기록한다.
- flaky test는 성공할 때까지 반복해서 숨기지 않는다. 재현 조건과 root cause를 finding으로 기록한다.
- S11의 Core stable 또는 첫 bindings immutable 10.0.0 package를 외부에 공개한 뒤 source 또는 계약 결함이 발견되면 같은 version을
  덮어쓰지 않는다. S11을 `차단`으로 기록하고 사용자 승인 아래 patch version 계획을 별도로 연다.
  이를 피하기 위해 S7의 배포 없는 workflow가 S11과 같은 package 입력과 packaging step을 사용해야 한다.
- 계획의 범위를 바꾸는 결정은 사용자의 명시적인 승인과 decision record 없이 적용하지 않는다.

## 17. 최종 완료 기록

| 항목 | 값 |
|---|---|
| 최종 source commit | - |
| 검증한 Core RC tag | `core/v10.0.0-rc.N` 예정 |
| Core release tag | `core/v10.0.0` 예정 |
| bindings release tags | 언어별 `v10.0.0` 예정 |
| 최종 Codex review | - |
| 최종 Claude Sonnet review | - |
| Core workflow run | - |
| Core RC prerelease run·checksum | - |
| Conan workflow run | - |
| bindings workflow runs | - |
| 전체 E2E 결과 | - |
| 전체 sample 결과 | - |
| benchmark report | - |
| stale no-hit 결과 | - |
| open finding | `0`이 되어야 종료 |
