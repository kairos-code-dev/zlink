# RouteMesh 10.0.0 실행 진행표

## 0. 문서 상태와 사용 방법

이 문서는
[`RouteMesh 메시징 통합 계획`](./framework-route-mesh-messaging-consolidation.ko.md)과
[`MeshNode Core 공개 API 전환 검토`](./mesh-node-core-api-review.ko.md),
[`MeshNode·Spot·Actor framework 우선 dispatch 설계`](./mesh-node-framework-dispatch-design.ko.md)를 실제로
실행하기 위한 진행표다. 2026-07-20에 추가된
[`ChannelName 단일 주소 설계 초안`](./channel-name-global-routing-draft.ko.md)은 새 공개 계약 후보를 정리한
입력 문서다. `S3-CH-03`이 끝나기 전까지 현재 정식 계약이 아니다. 설계 근거와 목표 의미는
이 계획 문서들을 따르고, 작업 순서·완료 상태·검증 증거는 이 문서를 기준으로
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

2026-07-21부터 11.0 통합 전환은
[`RouteMesh 11.0.0 통합 execution ledger`](../v11.0/route-mesh-11.0.0-execution-ledger.ko.md)가 소유한다.
이 문서의 미완료 행은 11.0 `V11-M0-01`에서 `raw-core-prerequisite`, `verified-baseline`,
`target-contract`, `reuse`, `superseded`, `discarded` 가운데 하나로 분류한다. 이 문서에서는 새 구현을
시작하지 않으며, 기존 실행 증거와 11.0 인계 근거만 보존한다.

이번 변경은 Core와 bindings 10.0.0 계약을 한 번에 적용한다. 폐기 대상 alias, deprecated wrapper와
두 runtime mode를 함께 유지하지 않는다. 원칙적으로 각 stage는 앞 stage의 gate를 통과한 뒤 시작한다.
다만 S1 Core 계약 기준선이 승인된 뒤에는 Core 구현 red gate와 S4 작업을 S2·S3 문서 작업과 병렬로
진행할 수 있다. 이 병렬 작업의 source나 test는 S2·S3 계약의 근거가 아니며, S3에서 Core 계약이 바뀌면
S4가 해당 변경을 다시 반영하고 전체 검증을 실행해야 한다. 구현·test·문서는 서로 다른 담당 범위에서
계속 수정할 수 있다. 리뷰는 작업 공간을 잠그지 않고 review manifest가 가리키는 변경되지 않는
snapshot을 대상으로 수행한다. S5의 최종 clean 판정은 S3 종료 상태와 snapshot 이후 계약 변경을
반영한 revision에서 내린다.

진행 상태는 다음 값만 사용한다.

| 상태 | 의미 |
|---|---|
| `미착수` | 선행 조건이 충족되지 않았거나 아직 시작하지 않음 |
| `진행 중` | 담당 범위에서 작업 또는 검증을 수행 중 |
| `리뷰 중` | 고정된 review snapshot을 독립 검토 중. 다른 담당 범위의 구현·test·문서 수정은 계속할 수 있음 |
| `수정 중` | review finding을 반영하고 관련 검증을 다시 수행 중 |
| `차단` | 외부 권한, 배포 환경 또는 확정되지 않은 계약 때문에 진행할 수 없음 |
| `후속 분리` | 사용자가 현재 release gate에서 제외하고 별도 후속 작업으로 진행하도록 결정함 |
| `11.0 승계` | 구현 또는 계약 자산을 11.0 ledger의 연결 ID에서 계속 처리함 |
| `폐기` | 사용자 결정 또는 11.0 책임 경계와 충돌하여 실행하지 않음. 기존 증거만 보존함 |
| `승인 종료` | clean 문구는 없지만 사용자가 반복 종료를 명시적으로 승인한 terminal 상태. clean으로 기록하지 않음 |
| `완료` | 해당 stage의 완료 gate가 요구하는 checklist, 검증, 리뷰와 증거가 충족됨 |

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
| S1·S2-CH~S3-CH Channel 송신 경로·ClientServer 계약 재개방 | [Core MeshNode](../../../../core/doc/spec/core/service/01-mesh-node.ko.md), [Core DEALER](../../../../core/doc/spec/core/socket/06-dealer.ko.md), [Core ROUTER](../../../../core/doc/spec/core/socket/07-router.ko.md), framework 공통 `01-overview`·`02-interaction-model`·`04-async-execution-policy`·`05-framework-api`, server `10`·`11`·`20`·`40`·`41`·`50`·`52`·`53`·`54`, 다섯 언어 exact interface, 공통 E2E·sample; 정확한 owner 범위는 S2-CH-01에서 고정 |
| S8 `.NET` lane | [Framework 공통 계약](../../framework/spec/README.ko.md), [server 계약](../../framework/spec/server/21-mesh-node.ko.md), [.NET exact interface](../../framework/spec/server/languages/dotnet/README.ko.md) |
| S8 location·transfer | [Location Runtime](../../framework/spec/server/40-location-runtime.ko.md), [Redis extension](../../framework/spec/server/41-location-store-redis.ko.md), [.NET Location Store](../../framework/spec/server/languages/dotnet/06-location-store.ko.md) |
| S8 monitoring·drain | [Runtime monitoring](../../framework/spec/server/50-runtime-monitoring.ko.md), [message flow](../../framework/spec/server/52-message-flow-tracing.ko.md), [graceful drain](../../framework/spec/server/54-graceful-drain-handoff.ko.md), [.NET RouteMesh runtime](../../framework/spec/server/languages/dotnet/05-route-mesh.ko.md) |
| S9 C++ | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/cpp/` exact interface |
| S9 Java/Kotlin | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/java/`, `languages/kotlin/` exact interface |
| S9 Node.js | 공통 framework 계약과 S2·S3에서 리뷰한 `framework/doc/framework/spec/server/languages/node/` exact interface |

S2에서 다섯 언어의 exact interface를 공통 10.0.0 계약의 언어별 표현으로 정식 문서에 고정하고,
S3에서 공통·server 계약 및 E2E·sample 범위와 함께 독립 리뷰한다. S8과 S9은 하나의 다섯 언어 병렬
구현 그룹이다. `.NET`, C++, Java, Kotlin과 Node.js는 reviewed exact interface와 실제 source·package
사이의 차이를 각자 red gate로 만들고 동시에 구현한다. Java와 Kotlin은 source와 build 환경을 공유하므로
하나의 JVM lane에서 두 언어를 함께 진행하며, 전체 실행 단위는 `.NET`, C++, JVM, Node.js의 네 lane이다.
어느 언어 구현도 다른 언어의 계약 출처나 선행 조건이 아니다.
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

작업 지시에 적힌 stage·ID 범위가 실행 권한이다. 선행 stage의 상태가 모순되거나 미완료여도 명시된
범위 밖 stage를 자동으로 수행하거나 수정하지 않는다. 담당 범위가 실제로 차단되면 그 경계에서 원인과
필요한 결정을 보고한다. 단, 담당 stage를 수행하면서 같은 공개 계약에 직접 영향을 주는 source·test·spec·
guide·internals를 함께 갱신하는 것은 범위 밖 stage를 대신 수행하는 것으로 보지 않는다.

작업 지시에서 담당 ID를 생략한 경우 에이전트는 현재 stage의 `미착수` 행 가운데 선행 조건이 충족된
첫 항목을 제안할 수는 있지만, 여러 lane의 소유권이 달라지는 작업을 임의로 시작하지 않는다. S3와 S4처럼
명시적으로 허용한 병렬 실행도 각자 담당 행만 갱신한다.

review manifest의 commit과 hash는 당시 증거를 식별한다. 작업 공간의 일반 변경은 기존 review를 무효화하지
않으며 변경 파일과 의존 범위만 추가로 확인한다. 공통 spec, private SPI, protocol 또는 native artifact의
관찰 가능한 의미가 바뀐 경우에만 영향받는 11.0 lane을 중지하고 관련 검증을 다시 수행한다. 문구·링크·
서식 변경은 자동 문서 검사와 해당 문서 확인으로 끝낸다.

§2 이하의 전체 재리뷰 문구와 stage별 snapshot 기록은 10.0 실행 이력으로 보존한다. 11.0 인계와 이후 변경
판정에는 [11.0 execution ledger](../v11.0/route-mesh-11.0.0-execution-ledger.ko.md)의 변경 영향 정책을 적용한다.

### 0.3 POSD 기반 구현 원칙

RouteMesh 10.0.0의 Core, bindings와 framework는 POSD 철학을 설계 기준으로 삼는다. 공개 interface는
작고 단순하게 유지하고 routing, codec,
queue, retry, peer admission과 lifecycle 복잡성은 책임을 소유한 깊은 모듈 안에 둔다. 같은 설계 지식이
여러 계층이나 언어에 반복되면 정보 누출로 판단하며, 호출자에게 transport detail이나 내부 policy를
추가로 요구하는 방식으로 성능 문제를 우회하지 않는다.

성능 측정과 개선은 각 framework 언어의 기능 구현 gate와 분리한다. Core의 현재 기능 결함 수정과
전체 회귀가 끝나면 §13.5의 독립 성능 lane을 언어별 framework 작업과 병렬로 실행한다. 성능 lane은
`doc/plan/spot-route-data-plane-performance-improvement-plan.ko.md`를 완료 조건의 정본으로 사용하며,
최종 Core version 동결과 bindings 내부 package 배포 전에 자체 gate를 닫는다. 다른 stage를 완료하기 위한 근거로
중간 benchmark 수치를 사용하지 않는다.

### 0.4 결함 수정 version과 병렬 실행 정책

2026-07-19 사용자 결정에 따라 공개 전 전환을 완료하는 동안 발견한 Core 결함 수정은 Core의 minor
version을 올린다. same-RID handover 수정은 `10.2.0`, 이어서 발견한 Core 결함을 반영한 현재 후보는
`10.7.0`이다. 네 bindings도 최신 Core native 기준 version을 따라 모두 `10.7.0`을 base package로
만든다. Core 변경 없이 특정 binding만 수정하면 해당 binding의 patch만 `10.7.1`, `10.7.2`처럼
올린다. Core minor가 다시 올라가면 이전 binding patch 숫자는 승계하지 않고
모든 binding을 새 Core version의 patch `0`으로 초기화한다. binding 전용 patch를 올릴 때는 다른
bindings와 Core version을 바꾸지 않는다.

Core version과 binding package version을 하나의 값으로 간주하던 release workflow·검증 스크립트는
binding별 patch release를 지원하지 못했다. `e6812889a`에서 두 version을 별도 입력과 검증 대상으로
분리하고 release checksum·exact Core version·source tag provenance gate를 추가했다. package가 참조하는
Core native version과 binding package version은 S11에서 각각 증거로 남긴다.

서로 다른 파일과 책임 범위는 가능한 한 병렬로 진행한다. C++, JVM과 Node.js framework lane도 정식
spec과 guide를 기준으로 독립 실행한다. 실제 source나 package 선행 조건 때문에 기존 순서를 적용할 수
없으면 공개 계약과 review gate는 유지하되 작업 순서를 조정할 수 있으며, 조정 이유와 남은 gate를 이
ledger의 담당 행에 기록한다.

### 0.5 local test timeout 판정 정책

로컬 request, reply, handoff, drain과 process readiness가 기존 제한 안에 끝나지 않으면 timeout을 늘려
완료로 처리하지 않는다. 먼저 대기 경계와 마지막 진행 지점을 기록하고 실제 경쟁, wakeup, 연결 전이 또는
cleanup 결함을 수정한다. 원인을 찾기 위한 일회성 진단 timeout은 사용할 수 있지만 정식 검증 전에 원래
값으로 되돌리고, 수정 뒤 실제 완료 시간을 증거로 남긴다.

여러 독립 시나리오를 순서대로 실행하는 전체 batch의 외부 안전 제한은 개별 동작 timeout과 구분한다.
batch 제한은 포함된 시나리오 수와 정상 실행 시간을 수용해야 하지만, 각 시나리오 안의 기능 timeout을
늘리는 근거로 사용하지 않는다. timeout 변경이 필요한 계약상의 이유가 있으면 정식 spec의 시간 경계와
실측을 함께 제시하고 별도 review를 거친다.

## 1. 고정 실행 순서

| Stage | 작업 | 병렬 실행 | 완료 판정 |
|---|---|---:|---|
| **S0** | Core 정식 spec 적용 범위와 결정 검증 | 아니요 | 구현 전에 정할 항목이 0개이고 정식 owner 문서와 gap 범위가 고정됨 |
| **S1** | Core 10.0.0 정식 spec 작성 | 아니요 | Core 목표 계약 전체가 reviewed 정식 spec에 고정됨 |
| **S2** | framework 정식 spec 변경과 E2E·sample 영향 검토 | 아니요 | 공통·server 계약, 다섯 언어 exact interface, 공통·언어별 E2E·sample 문서와 public 예제 영향이 고정됨 |
| **S3** | 문서 독립 리뷰와 수정 반복 | 리뷰 2개만 병렬 | 두 리뷰어 모두 `DOC REVIEW CLEAN`이거나, clean 없이 종료한다는 사용자 승인과 미해결 finding 0건이 기록됨 |
| **S4** | Core 구현·제거 정리와 정식 spec 일치 | 아니요 | 기능·삭제·회귀, header-spec 일치와 구현 후 internals gate 통과 |
| **S5** | Core 구현 3축 독립 리뷰와 수정 반복 | 리뷰 2개만 병렬 | 두 리뷰어의 I1 계약 일치·I2 POSD/DDD·I3 정리 완결성이 모두 clean이고 `CORE REVIEW CLEAN` |
| **S6** | Core 10.0.0 release-candidate GitHub Actions build와 pre-release 배포 | workflow 병렬 허용 | RC native artifact와 local Conan 검증 완료. stable tag·remote publish 없음 |
| **S7** | bindings·framework 공통 준비(언어 독립) | 아니요 | RC artifact 동기화, 제거 wrapper·금지 구현 정책과 no-hit 목록, 공통 smoke matrix 정의가 4개 lane 입력으로 고정됨 |
| **S8** | 네 언어 lane 병렬 파이프라인: 각 lane = bindings 적용→bindings 3축 리뷰→framework 적용→framework 3축 리뷰 | 네 lane(cpp·dotnet·jvm·node) 동시 실행 | lane마다 `BINDINGS REVIEW CLEAN`(bindings 단계)과 언어별 framework clean 문구(`CPP/DOTNET/JVM/NODE REVIEW CLEAN`)가 모두 나옴 |
| **S11** | 전체 3축 최종 검토, Core 동결·bindings 내부 package 배포와 종료 | 최종 리뷰 2개만 병렬 | 두 리뷰어의 I1·I2·I3 clean과 `FINAL REVIEW CLEAN` 뒤 내부 package 생성·smoke 완료 |

**stage 재구성(2026-07-18 사용자 결정)**: bindings 적용 대상은 **cpp·dotnet·jvm(Java+Kotlin)·node
네 언어**다. Python·Go·Rust는 이번 10.0.0 적용을 **보류**하며 코드는 삭제하지 않는다(framework는
원래 이 셋을 대상으로 하지 않는다). C ABI는 별도 언어 lane이 아니라 cpp lane의 선행 검증(C header·
shared library smoke)으로 포함한다. 이전 판의 S7(bindings 전체 stage)·S8(.NET framework)·S9(C++/JVM/
Node framework)·S10(lane 리뷰)은 **S8 네 언어 lane 파이프라인**으로 통합됐다. 각 lane은 bindings를
먼저 적용·리뷰해 `BINDINGS REVIEW CLEAN`을 낸 뒤, 그 언어의 framework를 적용·리뷰한다. lane끼리는
독립·병렬이고, 한 lane이 bindings clean에 도달하면 다른 lane을 기다리지 않고 그 lane의 framework를
바로 시작한다.

S3, S5, S8 lane과 S11의 review gate를 생략하거나 다음 stage에서 대신 처리하지 않는다. S2·S3와
병렬로 시작한 S4 변경은 S3를 대신하지 않으며 S3 finding이 반영된 정식 계약에 다시 맞춰야 한다.
S3이 `승인 종료`이면 S3 gate를 통과한 terminal 상태로 보고 downstream stage를 진행한다. 정식 계약이
바뀌지 않는 한 `DOC REVIEW CLEAN`을 만들기 위해 S3를 다시 열지 않는다.

S8의 네 lane(cpp·dotnet·jvm·node)은 S7 공통 준비가 끝나면 동시에 시작한다. 각 lane은 자기 언어
안에서 bindings→framework를 순차 진행하지만 lane 사이에는 선행 조건이 없다. 각 lane은 공통 spec과
자기 언어의 exact interface를 계약 기준으로 사용하고, 다른 언어 구현은 관찰 가능한 동작과 검증
방법을 비교하는 참고 증거로만 사용한다. 다른 언어에만 있는 public API나 내부 구조를 복제하지 않는다.
네 언어는 같은 관찰 가능한 공개 동작을 제공하되, 구현은 각 언어의 기존 framework 구조, 타입 체계,
비동기 처리, 오류 전달, resource 수명과 package 작성 규칙을 따른다. 언어 특성을 이유로 공개 동작을
줄이거나 내부 정책을 호출자에게 전달하지 않는다.

### 1.1 Python·Go·Rust bindings 계획 종료

Framework가 사용하지 않는 Python, Go와 Rust bindings의 추가 작업 계획은 폐기한다. 기존 source와 실행
증거는 Core 10.x 조합의 기록으로 보존하지만 새 package, review와 11.x 전환 작업을 진행하지 않는다.

| ID | 담당 | 선행 조건과 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| PGR-00 | Codex `/root` | Core candidate manifest, 최신 contract·제거 inventory와 C perf 대응표 고정 | 폐기 | 2026-07-21 사용자 결정. 기존 snapshot과 조사 증거만 보존 |
| PGR-01 | Codex `/root` | 세 언어 exact interface와 오류·ownership을 구현 전 draft에서 review | 폐기 | 기존 review 기록만 보존하고 계약 후보를 구현하지 않음 |
| PGR-02 | Codex `/root` | native 동기화, provenance, 비배포 package와 clean consumer 진입점 완성 | 폐기 | 기존 정적 검토 증거만 보존하고 candidate를 생성하지 않음 |
| PGR-PY | Codex `/root` | Python 구현·전체 검증·review·commit·push 완료 | 폐기 | 추가 Framework bindings 작업을 진행하지 않음 |
| PGR-GO | Codex `/root` | Go 구현·전체 검증·review·commit·push 완료 | 폐기 | 추가 Framework bindings 작업을 진행하지 않음 |
| PGR-RS | Codex `/root` | Rust 구현·전체 검증·review·commit·push 완료 | 폐기 | 추가 Framework bindings 작업을 진행하지 않음 |
| PGR-X | Codex `/root` | 세 package 공통 E2E와 지원 platform 검증 완료 | 폐기 | 새 package matrix를 만들지 않음 |
| PGR-DOC | Codex `/root` | 확정 구현을 정식 bindings spec·API reference·README에 반영 | 폐기 | 미구현 계약을 정식 문서에 반영하지 않음 |
| PGR-REV | 사용자 배정 reviewer | 각 lane과 통합 snapshot의 I1·I2·I3 review와 최종 재검증 완료 | 폐기 | 구현 lane 폐기로 review를 시작하지 않음 |
| PGR-PERF | 별도 사용자 인가 필요 | full C 기준선, 세 bindings full perf와 성능 개선 | 폐기 | 추가 bindings 성능 작업을 진행하지 않음 |

## 2. 독립 리뷰 운영 규칙

### 2.1 리뷰어

| ID | 리뷰어 | 역할 |
|---|---|---|
| **R1** | Codex agent | 저장소의 실제 spec, source, test, package와 실행 증거를 독립 검토 |
| **R2-DOC** | Claude Sonnet 모델 | S3 문서의 같은 고정 revision과 동일한 review manifest를 독립 검토 |
| **R2-CODE** | Claude Sonnet 모델 | S5·S8 각 언어 lane·S11의 source·test·build·package 코드와 구현 결과를 같은 고정 revision과 동일한 review manifest에서 독립 검토 |

R1과 해당 review 유형의 R2는 서로의 finding을 보기 전에 첫 검토를 완료한다. 두 결과가 나온 뒤 coordinator가 중복을
합치고 하나의 finding ledger를 만든다. 한 리뷰어의 clean 판정으로 다른 리뷰어의 검토를 대신하지
않는다. 모든 문서·구현 리뷰는 Codex agent와 Claude Sonnet의 두 독립 결과를 필수로 사용한다. 다른
Claude 모델을 우선 reviewer나 fallback으로 사용하지 않는다.

구현 stage의 담당 항목을 완료하면 중간 checkpoint나 pass 구분 없이 해당 stage 전체 리뷰를 바로
시작한다. S4 구현은 S5에서 Core 전체를, S7은 bindings 전체를, S8은 `.NET` 전체를, S9의 각 lane은
S10에서 해당 언어 전체를, S11은 release-candidate 전체를 검토한다.

모든 구현 리뷰는 **전체 리뷰 → 이슈 일괄 수정 → 일반 build·전체 테스트 → 수정본 전체 리뷰** 순서로
통일한다.

모든 리뷰 iteration에서 R1과 R2는 같은 commit, 같은 전체 범위와 **byte 단위로 동일한 프롬프트**를
받아 동시에 시작한다. 한 reviewer의 finding이나 coordinator의 해석을 다른 reviewer에게 먼저 제공하지
않는다. 첫 리뷰와 finding 수정 뒤의 재리뷰는 모두 최신 stage 전체 범위를 처음부터 검토하며, 수정
diff나 표본 검사만으로 재리뷰를 대신하지 않는다.

두 리뷰 결과가 나오면 coordinator는 finding을 하나의 ledger로 합치고 서로 연관된 이슈를 한 번에
수정한다. 수정 뒤에는 일반 build와 해당 stage의 전체 테스트를 실행한다. 이 검증이 통과해야 새 commit을
고정하고 같은 두 reviewer가 최신 stage 전체를 다시 리뷰한다.

리뷰 횟수는 같은 stage 전체를 두 reviewer가 검토한 iteration 수로 센다. 1~3회차에는 모든 severity의
finding이 0건이어야 리뷰를 종료한다. 4회차부터는 blocker·high·medium finding이 0건이면 리뷰를
종료한다. 이때 남은 low finding은 후속 정리 목록에 기록하며, low만으로 수정과 전체 재리뷰를 반복하지
않는다.

두 reviewer가 같은 snapshot에서 해당 횟수의 종료 조건을 충족하고 stage의 exact clean 문구를 남긴
시점이 **리뷰 종료 조건**이다. ASAN·UBSAN·TSAN, 공개 API·제거 항목, package metadata와 실제
package·consumer 검사는 리뷰 준비나 각 iteration 사이에 반복하지 않고 리뷰 종료 뒤 stage 종료
검증으로 한 번 실행한다. stage별로 해당하지 않는 검사는 생략할 수 있지만 ledger가 요구하는 종료
검증은 모두 통과해야 한다.

종료 검증에서 실패해 code, test, spec, package 또는 workflow의 의미를 바꾸면 일반 build와 전체
테스트를 다시 통과시킨 뒤 새 snapshot에서 두 reviewer의 stage 전체 리뷰를 다시 수행한다. 다시 clean이
된 뒤 종료 검증도 다시 실행한다.

internals는 구현 리뷰의 clean 근거로 사용하거나 review iteration마다 수정하지 않는다. 두 reviewer의
clean과 종료 검증으로 구현이 확정된 뒤 실제 source 구조를 기준으로 한 번 갱신한다. 갱신 뒤에는 source,
구조 test, thread·lifecycle 설명과 diagram의 일치만 문서 범위로 확인하며, internals 수정만으로 구현
전체 리뷰를 다시 열지 않는다. 이 확인에서 실제 code 결함이 발견된 경우에만 구현 review loop를 다시
연다. internals 확인까지 통과하면 review 문서, finding ledger와 stage 증거를 정리하고 다음 stage
구현으로 이동한다.

S3 문서 리뷰와 S5·S8 lane·S11 구현 리뷰는 모두 Codex agent와 Claude Sonnet이 같은 frozen scope를
각각 독립 검토한다. 문서 리뷰에서는 spec·guide·internals·E2E·sample의 계약과 설명을 검토하고, 구현
리뷰에서는 frozen spec을 기준으로 source·test·build·package와 실제 실행 결과의 일치를 검토한다.
구현 확정 전에 작성된 internals는 구현 판정의 근거와 수정 대상에서 제외한다.
Claude Sonnet을 사용할 수 없거나 정상 종료 결과를 얻지 못하면 다른 Claude 모델로 대체하지 않고 해당
review gate를 `차단`으로 기록한다. 같은 Claude Sonnet의 다음 session만 같은 snapshot의 미검토 범위부터
이어갈 수 있다.

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
수정하더라도 새 revision에서 Codex agent와 선택된 R2 모델이 stage 전체 범위를 처음부터 검토하고 세
축 판정을 다시 남긴다. 1~3회차의 `CLEAN`은 해당 축 finding 0건을 뜻한다. 4회차부터는 해당 축의
blocker·high·medium finding이 0건이면 low를 별도로 기록하고 `CLEAN`으로 판정할 수 있다. 두 리뷰어의
세 축이 모두 `CLEAN`이고 같은 최신 snapshot에 대한 stage exact clean 문구가 모두 있어야 구현 review
gate를 통과한다.

### 2.2 리뷰 동작 확인

리뷰에는 목표 시간이나 강제 종료 시간을 두지 않는다. 리뷰 소요 시간은 clean 판정이나 중단의 근거가
아니며, 두 reviewer가 배정된 전체 범위를 검토하고 결과 파일을 기록한 뒤 process가 정상 종료해야
완료로 인정한다.

각 reviewer는 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 작업 중에는 3분보다 긴
간격이 생기지 않도록 현재 검토 축, 파일 또는 명령, 남은 범위와 갱신 시각을 기록한다. coordinator는
3분마다 reviewer process의 실행 상태와 `progress.md`의 수정 시각·크기 변화를 함께 확인한다. 한 주기
동안 갱신이 없으면 clean으로 추정하거나 즉시 종료하지 않고 process와 현재 작업을 확인한다. process가
종료됐는데 최종 review 파일이 없거나 비정상 종료했다면 해당 reviewer 결과를 `차단`으로 기록한다.

reviewer의 산출물은 자신의 review 디렉터리에 기록하는 문서 두 개 — `progress.md`와 `review.ko.md` —
뿐이다. reviewer는 소스 정적 대조로만 판정하며 build, 테스트 실행, sanitizer, package 생성 등 어떤
실행 작업도 수행하지 않는다. 실행 증거가 필요하면 manifest에 기록된 coordinator의 검증 결과를
사용한다. coordinator는 review 디렉터리의 보고서 문서를 읽어 finding을 병합하고, 수정 구현·build·
전체 테스트·종료 검증·ledger 갱신을 모두 직접 수행한다.

일반 build와 전체 테스트는 finding 수정 뒤 coordinator가 실행하고, sanitizer·공개 API·package 종료
검증은 두 reviewer가 clean을 남긴 뒤 실행한다. reviewer에게 이 검증을 실행하게 하지 않는다.

### 2.3 review manifest

모든 review iteration은 다음 정보를 먼저 고정한다.

- stage와 iteration 번호
- review 대상 commit SHA, 전체 파일 범위, 파일 수와 aggregate SHA-256
- 두 reviewer에게 전달할 동일한 `prompt.md`와 그 SHA-256
- reviewer provider, 실제 model identifier와 version
- invocation 또는 session ID, 시작·종료 시각과 process 종료 상태
- 읽어야 하는 정식 spec과 계획 문서
- 검토할 source, test, E2E, sample, package와 workflow 범위
- 제거 API와 금지 구현의 검색 문자열
- 구현 중 또는 직전 finding 수정 뒤 통과한 일반 build·전체 테스트 결과 위치
- 리뷰 clean 뒤 실행할 sanitizer·공개 API·package 종료 검증 목록
- 직전 iteration finding과 반영 commit
- 리뷰어가 수정할 수 없는 file scope
- reviewer raw output의 보존 위치와 SHA-256 checksum

review manifest와 결과는 다음 경로 아래에 stage별로 보관한다.

`framework/doc/plan/v10.0/log/<stage>/<iteration>/`

각 새 iteration은 다음 파일 구조를 사용한다.

```text
<iteration>/
|-- manifest.ko.md
|-- prompt.md
|-- finding-ledger.ko.md
|-- verification.ko.md
|-- codex/
|   |-- progress.md
|   |-- review.ko.md
|   `-- raw-output.log
`-- claude-sonnet/
    |-- progress.md
    |-- review.ko.md
    `-- raw-output.log
```

두 reviewer는 서로 분리된 detached worktree에서 같은 commit을 읽고 각자 배정된 review 디렉터리에만
결과를 기록한다. 이 규칙을 적용하기 전에 생성된 평면 구조의 review 파일과 다른 model의 과거 결과는
당시 provider와 model을 그대로 표시해 보존하며 이름이나 내용을 바꾸지 않는다. 새 iteration의 R2
결과나 clean 판정을 대신하는 증거로는 사용하지 않는다. review 파일은 append-only 증거로 취급하고
이전 iteration 결과를 덮어쓰지 않는다.

coordinator가 정리한 finding ledger는 reviewer 원본을 대신하지 않는다. provider/model, invocation,
대상 SHA, raw output와 checksum 가운데 하나라도 없거나 process가 정상 종료하지 않았으면 해당 reviewer
결과는 `차단`으로 기록하고 clean 문구를 인정하지 않는다.

### 2.4 finding 처리

모든 리뷰어는 각 finding마다 이슈, 근거, 영향, 수정 범위와 검증 방향을 제시한다. 수정 방법을 함께
제안할 수 있지만, 그 제안은 확정된 해결책이 아니며 coordinator와 구현 담당자가 정식 계약과 기존
책임 경계를 확인한 뒤 채택 여부를 결정한다.

| 필드 | 기록 내용 |
|---|---|
| Finding ID | stage, reviewer와 순번을 포함한 고유 ID |
| Severity | blocker, high, medium, low |
| 이슈 | 실제로 잘못되었거나 누락된 동작·계약·책임 경계 |
| 근거 | 실제 `file:line`, symbol, package entry 또는 실행 결과 |
| 영향 | 영향을 받는 호출자, runtime, package, 검증 또는 release gate |
| 위반 계약 | 해당 Core/framework spec 절 또는 완료 gate |
| 수정 범위 | code, test, spec, sample, package 또는 workflow |
| 검증 방향 | finding을 재현할 red gate와 수정 후 확인할 green 결과 |
| 상태 | open, fixing, resolved, rejected |
| 종료 근거 | 수정 commit과 재리뷰 iteration |

finding은 공개 계약, 관찰 가능한 동작, compile·실행 가능성, concurrency·resource·security, package·
artifact, 검증 누락 또는 책임 경계에 구체적인 영향을 주어야 한다. 근거에는 실패 형태와 영향을
받는 호출자·runtime·package·검증을 함께 적는다. 더 자연스러운 표현, 취향 차이와 의미를 바꾸지 않는
문장 교정은 finding으로 등록하지 않는다. 다만 의미가 모호하거나 잘못되어 계약 해석이 달라지는 경우,
예제가 컴파일되지 않는 경우 또는 `AGENTS.md`의 명시적 금지 표현을 위반한 경우에는 finding으로 다룬다.

단순 표현 개선은 `editorial note`로 분리한다. editorial note는 open finding 수와 clean 판정에 포함하지
않고 stage를 차단하지 않으며, 마지막에 한 번 묶어서 처리한다. editorial note만 수정한 경우 독립
재리뷰를 새로 열지 않고 자동 문서 검증과 scoped diff 검사만 실행한다. 마지막 전체 pass가 끝난 뒤
editorial note를 반영했다면 두 reviewer가 계약과 실행 결과를 바꾸지 않았음을 확인하고 새 hash를
manifest에 덧붙인다.

`resolved`는 구현자가 정하는 상태가 아니다. 수정과 검증 뒤 다음 iteration의 독립 리뷰에서 같은
문제가 해소되었음을 확인해야 한다. `rejected`는 구체적인 계약 근거와 두 리뷰어의 재검토가 있어야
한다.

### 2.5 반복 종료 조건

각 구현 stage는 다음 순서로 종료한다.

1. 구현 항목을 완료하면 commit, 전체 범위, hash와 동일한 prompt를 기록하고 R1과 R2의 stage 전체
   리뷰를 동시에 시작한다. snapshot 생성만을 이유로 build나 전체 검증을 다시 실행하지 않는다.
2. 두 결과를 finding ledger에 합치고 finding을 한 번에 수정한다.
3. 수정 뒤 일반 build와 해당 stage의 전체 테스트를 실행한다.
4. build와 전체 테스트가 통과하면 새 snapshot에서 R1과 R2가 최신 stage 전체를 처음부터 다시
   검토한다.
5. 1~3회차에는 모든 finding이 0건이 될 때까지 2~4를 반복한다. 4회차부터는 blocker·high·medium
   finding이 남아 있을 때만 2~4를 반복하고 low는 후속 정리 목록으로 넘긴다.
6. 두 reviewer의 I1·I2·I3와 stage exact clean 문구가 해당 횟수의 종료 조건을 충족하면 리뷰를
   종료한다.
7. 리뷰 종료 뒤에만 stage가 요구하는 ASAN·UBSAN·TSAN, 공개 API·제거 항목, package metadata와
   package·consumer 검사를 실행한다.
8. 종료 검증이 통과하면 최종 source를 기준으로 internals를 한 번 갱신하고 source·구조 test·diagram·
   link 일치를 문서 범위로 확인한 뒤 review 결과와 ledger를 정리하고 다음 stage로 이동한다. 종료
   검증 때문에 의미 있는 구현 파일을 수정했다면 3번부터 다시 수행한다.

S3 문서 리뷰는 두 `DOC REVIEW CLEAN`을 기준으로 하되, 미해결 finding 0건과 사용자의 명시적 종료
승인이 기록되면 `승인 종료`로 끝낼 수 있다. 이때 clean으로 기록하지 않는다.

한 리뷰어가 실행되지 않았거나 결과가 중단되면 review gate는 `차단`이다. 리뷰 소요 시간, finding
개수 감소 또는 test 통과만으로 clean 판정을 추정하지 않는다.

## 3. 전체 진행 현황

`open finding`은 reviewer가 채택한 finding 수다. 아직 정식 finding으로 분류하지 않았지만 다음 review에서
반드시 판정해야 하는 race·deadlock 위험은 `known risk`에 별도로 센다.

| Stage | 상태 | 현재 iteration | open finding | known risk | 완료 증거 |
|---|---|---:|---:|---:|---|
| S0 정식 spec 범위·계약 확정 | 완료 | 0 | 0 | 0 | `s0-scope-baseline.ko.md`, `log/templates/manifest.ko.md` |
| S1 Core 정식 spec | 완료 | 4 | 0 | 0 | `log/s1-core-review/iteration-4/`; 두 리뷰 finding 12건 수정, 자동 검증 통과, 사용자 구현 기준선 승인; 최종 hash `6cd163bf…ea71` |
| S2 framework spec | 완료 | 0 | 0 | 0 | 공통·server, 다섯 언어 exact interface, E2E 55·sample 32·runner 96·guide/internals 81 inventory와 자동 검증 통과 |
| S3 문서 review loop | 승인 종료 | 28 | 0 | 0 | iteration 20~28도 시도됐지만 clean으로 채택되지 않았다. `log/s3-document-review/final-acceptance/`는 사용자 승인으로 추가 반복을 종료하고 1~19를 종료 기준 범위로 보존한다. |
| S1·S2·S3-CH ChannelName 단일 주소·ClientServer 계약 재개방 | **진행 중** | 0 | 0 | 3 | draft 전체 리뷰에서 Core membership 0개, ClientServer 전용 descriptor·자동 발견, handler context 분리를 선행 계약 항목으로 확인했다. 정식 spec·exact interface·E2E·sample 수정과 독립 review가 남아 있다. |
| S4 Core 구현·정식 spec 일치 | 완료 | 0 | 0 | 4 | 2026-07-17 HEAD `5857824c2`+working tree에서 84/84 suite·2-process 10/10·stress 3/3·ASAN/UBSAN/TSAN·surface gate·C ABI smoke·초기 internals 기록·no-hit 통과. 최종 internals 확정은 S5-11/12. known risk 4=TSAN 기존 기계 3계열+MIXED source 도달성(§8.1 S4-05A) |
| S5 Core review loop | 완료 | 16 | 0 | 4 | iteration 1~9 기록은 각 finding ledger에 보존. iteration 10(`a4e91c01d`): Codex 7건+Sonnet 1건 병합 8건(scheduler lost-wakeup, generation 고정, timeout ABA, monitor UAF, join flags, acceptor errno, 테스트 단위, stale internals→S5-11 이관) 일괄 수정, 85/85 → `c1c579ad1`. iteration 11(새 §2 절차): Sonnet CLEAN·Codex NOT CLEAN(수정분 신규 반례 5건: monotonic clock 앵커, actor join task 미회수, monitor 등록 재생성 race, Windows errno, 회귀 테스트 부재) 일괄 수정, 85/85 → `7f9d3e315`. iteration 10~16 반복(11부터 새 §2 절차, 리뷰어 문서 산출물만). 계열별 root-cause 수정: scheduler lost-wakeup/generation/timeout ABA/monitor UAF·등록원자성/error-atomicity 전 계층(operation transaction·completion 선예약·detach primitive·scheduler 무할당 봉인). iteration 16 `1f247af7a`에서 Codex·Sonnet 모두 `CORE REVIEW CLEAN`(세 축). 종료 검증: CTest 86/86, ASAN 7/7 report 0, surface gate·package metadata·diff-check PASS, TSAN 신규 Mesh/monitor race 0(기존 auto-HWM 14+mailbox 1 유지). S5-11/12 internals 확정 커밋 `2128ae91c`. known risk 4=S6 이후 sanitizer gate로 이월 |
| S6 Core release candidate | 완료(로컬 종결) | 0 | 0 | 0 | RC tag `core/v10.0.0-rc.1`. local Conan create·consumer smoke(`zlink 10.0.0`) 통과, SONAME 10, stable package 부재. GitHub native artifact·conan-release CI는 현재 내부 package 배포 범위에서 제외 |
| S7 bindings·framework 공통 준비 | 완료 | 0 | 0 | 0 | RC artifact 동기화(libzlink 10.0.0→4 lane native), 제거 정책 검색 문자열·공통 smoke 정의 고정, Python/Go/Rust 보류. release workflow는 S11 이월 |
| S8-CPP lane (C ABI+C++ bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-4 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~3 결함(ownership/claim수명/metadata/transfer/dead-code 연쇄) 전량 해소, no-hit ZERO, 라이브러리+15samples green. low 4건 follow-up(`iteration-4/low-followups.ko.md`). 다음=cpp framework 미러 |
| **S8-DN lane (.NET bindings→framework) [참조 lane]** | framework 구현 완료·UnitTests green | 0 | 0 | 0 | bindings CLEAN. framework compile-green + core-correct(lifecycle·stream·relay·transfer) + **S8-02/02A/03/05/06/16 완료**(AddRouteMesh 빌더·RouteMesh DI·node/channel handler dispatch·전송 배선·ready/claim pump·MeshName uniqueness, 구 AddSpotMesh/bridge 제거, build 0/0). **UnitTests 677/683 green**(6 fail=doc-regression, S8-09/10/17 추적). **MeshNode 생성 EINVAL 근본수정**(mesh-name 배선)이 native gap 3건 해소. 잔여=binding-surface gap batch(metadata·actor-row → S8-06A/04 완결)·samples/E2E(S8-09/10)·`DOTNET REVIEW CLEAN`(S8-13~15)·internals(S8-17/18). Logical Multicast는 별도 publish NODROP 옵션 없이 Core 내부 ROUTER의 HWM·timeout 계약을 그대로 사용하도록 S8-07에서 정리 |
| S8-JVM lane (Java/Kotlin bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-5 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~4(raw-layer ABI·router_recv_part arity·recv_handler 재매핑·C bridge dead 함수) 전량 해소. Java FFI/Panama, C bridge 실빌드 검증, 제거심볼 게이트 EMPTY. low 2 follow-up. 다음=jvm framework 미러 |
| S8-NODE lane (Node.js bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-4 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~3(enum 값·RouterSocket·kind_data·transfer·ready-handler·option 테이블) 전량 해소, no-hit 0, addon+tsc green. low 4건 follow-up. 다음=node framework 미러 |
| S11 Core 동결·bindings 내부 package 배포·최종 검토 | 미착수 | 0 | 0 | 0 | - |

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
| S0-13 | multicast direct target과 전달 경계 결정 반영 | target channel 직접 선택, 조건부 local 대상과 대상별 ROUTER submit 결과 확정 | 완료 | 2026-07-19 사용자 결정: 별도 Logical Multicast NODROP·all-or-none 보장 없이 기존 ROUTER HWM·timeout·DONTWAIT 의미를 사용. 정식 계약은 `core/doc/spec/core/service/01-mesh-node.*` §7과 `03-spot.*` §6 |
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
| S1-15 | Logical Multicast backpressure 계약 작성 | 별도 publish 옵션 없이 대상별 ROUTER HWM·timeout·DONTWAIT 결과와 local mailbox drop 경계를 명시 | 완료 | `core/doc/spec/core/service/01-mesh-node.*` §7, `03-spot.*` §6. 원격 대상은 일반 ROUTER send 결과를 따르며 대상 간 사전 probe·all-or-none·rollback을 보장하지 않음 |
| S1-15A | remote NODROP ingress 계약 후보 정리 | 수신 staging·peer 종료·relay 같은 publish 전용 동작을 공개 계약에서 제거 | 승인 종료 | 2026-07-19 사용자 결정으로 publish 전용 NODROP 의미를 채택하지 않음. 기존 ROUTER가 제공하는 HWM·timeout 범위만 유지하며 별도 수신 staging 구현은 제거 |
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
| S2-11 | location runtime과 Redis store 갱신 | MeshNode descriptor와 Spot·Actor location row를 분리하고 Redis를 production 기본 구현으로 지정. 명시적 등록, location store 미등록 시 startup failure, manual admission handshake와 test-only in-memory 경계를 반영 | 완료 | `server/40-location-runtime.ko.md`, `41-location-store-redis.ko.md`; Redis fixture 3개 hash 고정·JSON parse·canonical key·field 순서·byte parity gate 통과 |
| S2-11A | Actor transfer authority store 계약 | participant-set CAS, transfer token, lease, prepared/commit/abort 복구, Redis 구현과 startup capability validation 명시 | 완료 | `server/23-spot-actor.ko.md`, `41-location-store-redis.ko.md`, `actor-transfer-v1.json` |
| S2-12 | monitoring과 graceful drain 갱신 | RouteMesh별 readiness, drain, multicast와 rollback 단위 반영 | 완료 | `server/50-runtime-monitoring.ko.md`, `54-graceful-drain-handoff.ko.md` |
| S2-12A | runtime metrics와 message-flow tracing 갱신 | route, multicast, fanout metric·flow 종류와 bounded label을 정의하고 client connector reconnect 계기를 server session 계기와 분리 | 완료 | `server/51-runtime-metrics.ko.md`, `52-message-flow-tracing.ko.md`, `stream-connector/32-stream-connector.ko.md`와 네 언어 connector exact interface |
| S2-12B | flow correlation 갱신 | direct channel multicast와 reply completion correlation 경계 반영 | 완료 | `server/53-flow-correlation.ko.md` |
| S2-12C | S/S application metadata 계약 | Node·Channel·Spot direct와 Logical Multicast canonical codec, last-write-wins builder와 hostile ingress failure, immutable context, relay 전이표, reply 비자동복사·일반 reply metadata 미지원 명시 | 완료 | Core MeshNode·Spot과 framework `03-message-model.ko.md`, 5개 언어 exact interface 교차 검증 |
| S2-12D | Spot timer backend 계약 | .NET·Java·Node platform timer와 C/C++ C API timer가 같은 keyed scheduling·cancel 의미를 제공 | 완료 | `04-async-execution-policy.ko.md`, `server/20-spot-messaging.ko.md`, `25-stage-wrapper-on-spot.ko.md` |

### 6.2 언어별 공개 interface

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-13 | `.NET` exact interface 작성 | `UseInMemoryLocationStores()`를 production 표면에서 제거하고 나머지 root option, AddRouteMesh, RID pin을 지원하는 `IZLinkMeshPeerConnections`, 두 handler family, Spot·Actor 멤버, client metadata·handler snapshot, runtime-options signature·startup-only 오류 확정 | 완료 | .NET exact 문서 3개와 전용 inventory fixture 통과. publish 전용 NoDrop 표면은 Core 계약에서 제거되어 exact interface에도 두지 않음 |
| S2-14 | C++ exact interface 작성 | C++ builder, handler, client, value/result와 lifecycle exact signature를 정식 spec에 확정 | 완료 | C++ exact 문서와 location 문서 fixture 통과 |
| S2-15 | Java·Kotlin exact interface 작성 | Java builder·handler·client·Spring 등록과 Kotlin DSL·extension·async 경계의 exact signature를 각각 확정 | 완료 | Java·Kotlin exact 문서와 location 문서 fixture 통과 |
| S2-16 | Node.js exact interface 작성 | TypeScript declaration, Promise, NestJS 등록, peer·metadata·runtime option exact signature를 확정 | 완료 | Node.js exact 문서와 location 문서 fixture 통과. publish 전용 NoDrop 표면은 Core 계약에서 제거되어 exact interface에도 두지 않음 |
| S2-17 | 다섯 언어 제거 interface 표 작성 | root·builder·endpoint overload와 SpotNode 이름의 공개 멤버를 언어별로 전수 대응 | 완료 | `route-mesh-v10-contract-inventory.json`; 금지 surface no-hit |
| S2-18 | 구현 차이 추적 경계 고정 | RouteMesh 전환 차이·진행 상태는 임시 plan이 소유하고 framework 목표 계약과 현재 언어 구현의 차이는 비계약 `90-implementation-gap.ko.md`와 언어별 gap 문서가 소유 | 완료 | 정식 목표 spec의 plan-reference no-hit; gap 문서는 목표 계약·S2 완료 근거에서 제외하고 중앙 ledger만 stage 상태를 소유 |
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
| S2-27 | guide·internals 변경 지도 작성 | 구현 뒤 바꿀 모든 사용자·내부 문서 경로와 각 문서의 독자·질문·원본 식별. S2에서는 대상과 검증 기준만 정하고 internals 본문은 바꾸지 않음 | 완료 | 영향 inventory §14: 81/81 문서. guide는 S8·S9 구현 뒤 반영하고 internals는 review clean·종료 검증 뒤 S8·S10에서 확정 |
| S2-27A | 문서 성격과 current-state 경계 검증 | 정식 목표 spec은 10.0.0 목표 계약만 기록하고 plan은 blueprint·실행 추적만 기록. internals는 구현 review clean·종료 검증 전에는 현재 구조로 확정하지 않음 | 완료 | 정식 목표 spec의 plan-reference·current-history marker no-hit. 영향 inventory §14의 internals는 S8-17/18과 S10-CI/JI/NI gate로 연결 |
| S2-28 | link·anchor·render 검증 | S2 정식 spec 범위의 깨진 link·중복 anchor 0개이고 실제 render를 확인한 증거가 있음 | 완료 | 사용자 승인 종료 범위 203개 실제 pymdownx render, source local Markdown link 1,852개, file·anchor 오류 0; verifier·JSON·diff 통과 |
| S2-28A | 예제 API 강제 검사 설계 | 공개 계약 예제 compile/smoke, 필수 구성 누락과 원본·번역 동기 검사를 구현 stage에서 실행할 파일·명령·실패 조건으로 고정 | 완료 | transition inventory §7의 S8·S9 red/green 명령 |
| S2-29 | 공통 E2E 문서 적용 사항 검토 | Config 1~11마다 10.0.0 topology, 입력, 관찰 결과와 failure scenario의 변경·비변경·신규 검증을 파일 단위로 분류 | 완료 | 영향 inventory §3·§11 |
| S2-30 | 공통 sample 문서 적용 사항 검토 | sample마다 target API 예제, Redis 등록, manual topology와 runner 변경·비변경을 파일 단위로 분류 | 완료 | 영향 inventory §4·§12·§12.1 |
| S2-31 | runner template 변경 계획 고정 | topology setup, package 입력과 result marker 변경점을 파일별 기록 | 완료 | 영향 inventory §5·§13 |

S2 완료 gate:

- [x] reviewed Core 10.0.0 정식 spec과 framework 목표 계약 사이에 기능 또는 error 의미 차이가 없다.
- [x] E2E, sample, package consumer와 runner 영향이 파일 단위로 식별되어 있고 실제 변경·실행은 해당 framework 구현 stage의 gate로 연결되어 있다.
- [x] 공통·server 의미 계약과 .NET·C++·Java·Kotlin·Node exact public interface가 정식 spec에 있다.
- [x] 공통·언어별 E2E 문서, 공통·언어별 sample 문서와 public 예제가 파일 단위 inventory에 포함되어 있다.

2026-07-17에 40개 S2 행과 위 4개 gate를 대조한 Codex 독립 완료 감사 결과는
`S2 COMPLETION AUDIT CLEAN`이다. 감사에서 발견한 문서 경계, gap 소유권, Redis descriptor fixture gate와
render 증거 4건을 반영했고, 사용자 승인 종료 시점의 계약·render·JSON·diff 자동 검증도 다시 통과했다.

## 7. S3 — 문서 독립 리뷰 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-01 | S1·S2 문서 revision 동결 | review manifest에 commit과 diff 범위 기록 | 완료 | [사용자 승인 종료 manifest](log/s3-document-review/final-acceptance/manifest.ko.md): iteration 19 범위에 누락된 Kotlin gap을 보완한 203개 파일을 aggregate `7f505e82…99a8`로 기록하고 검증 시작·종료 hash 일치 |
| S3-02 | Codex agent 문서 리뷰 | 누락, 모순, 구현 불가능 계약과 stale API finding 보고 | 완료 | iteration 1~28의 Codex 결과를 보존했다. iteration 20~28은 clean으로 채택되지 않았으며 사용자가 추가 반복을 종료하도록 승인 |
| S3-03 | Claude Sonnet 문서 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 완료 | iteration 1~28의 Claude Sonnet 결과를 보존했다. iteration 20~28은 clean으로 채택되지 않았고 문서 리뷰에는 Fable을 사용하지 않음 |
| S3-04 | finding 병합과 중복 제거 | 모든 finding에 owner, severity와 red gate 지정 | 완료 | 채택된 finding은 iteration별 finding ledger와 S3-F9~F17 수정 묶음에 반영. hash drift로 무효 처리한 출력은 finding·clean 판정에 포함하지 않음 |
| S3-05 | Core 정식 spec finding 수정 | 관련 한국어·영문·signature·result table과 임시 구현 차이 추적을 함께 수정 | 완료 | iteration 8까지 발견한 Core 문서 finding 반영. iteration 9는 framework 문서만 검토했으며 framework 수정에서 Core 계약 변경이 필요하면 다시 연다 |
| S3-06 | framework spec finding 수정 | 공통·server spec, .NET·C++·Java·Kotlin·Node exact interface, 공통·언어별 E2E·sample 문서와 public 예제 영향 inventory를 함께 수정 | 완료 | iteration 14 참고 finding 7건을 S3-F14-A·B로 수정했고, 이후 drift로 되살아난 실제 render anchor 13건은 S3-F17-A로 복구 |
| S3-07 | 문서 자동 검증 재실행 | link, signature, stale name, duplicate와 formatting 검사 통과 | 완료 | 사용자 승인 종료 시 `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declaration 1,167·feature map 55·scenario 955), 실제 pymdownx render 203개·link 대상 포함 205개·source local Markdown link 1,852개·오류 0, JSON 15개와 scoped `git diff --check` 통과 |
| S3-08 | 전체 scope 재리뷰 | 이전 diff만이 아니라 S1·S2 전체를 두 리뷰어가 다시 검토 | 완료 | iteration 28까지 시도했지만 20~28은 clean으로 채택되지 않았다. 사용자는 final acceptance에서 1~19를 종료 기준 범위로 보존하고 추가 전체 재리뷰를 종료하도록 승인 |
| S3-09 | 문서별 2축 review 기록 | 각 문서를 원칙 준수와 1차 소스 부합으로 나누고 finding마다 축·severity·file:line·근거·제안 기록 | 완료 | iteration 9 두 reviewer가 모든 finding에 `[1차소스]` 또는 `[원칙]`, severity, file:line, 근거와 수정안을 기록 |
| S3-10 | 문서별 검증 증거 분리 | finding을 1차 소스로 확인한 뒤 문서별 수정 diff와 SHA-256을 독립 증거로 기록. 사용자가 별도로 요청하지 않으면 commit은 만들지 않음 | 완료 | iteration 1~28의 결과와 미채택 사유를 보존하고 [종료 검증](log/s3-document-review/final-acceptance/verification.ko.md)에 종료 기준 203개 파일별 SHA-256·aggregate·입력 hash를 기록. commit은 만들지 않음 |
| S3-11 | Logical Multicast backpressure 계약 독립 재리뷰 | 한영 정식 spec이 별도 publish NODROP 없이 기존 ROUTER 동작만 보장하는지 Codex와 Claude Fable이 독립 검토 | 진행 중 | 2026-07-19 계약과 구현을 단순화함. 대상별 ROUTER HWM·timeout·DONTWAIT, local mailbox drop, 부분 전달 허용, 사전 probe·rollback·수신 staging 부재를 새 snapshot에서 재리뷰 |

S3은 두 리뷰어의 clean 문구로 종료되지 않았다. iteration 20~28도 시도됐지만 clean으로 채택되지
않았고, 사용자가 추가 리뷰를 종료하도록 승인했다. final acceptance는 1~19를 종료 기준 범위로
보존한다. 따라서 아래 완료 gate는 존재하지
않는 clean 결과를 대신 만들지 않고, 누적 finding 처리·사용자 승인·현재 checkout 자동 검증을 종료
근거로 사용한다.

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

### 7.3 Iteration 11 수정 묶음

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F11-A | 계약·guide 정렬: common README의 target-first·Actor lifecycle owner 수정, .NET·Node channel guide를 RouteMesh API로 전환, C++ messaging facade를 선언된 단일 facade로 통일, Java/Kotlin/C++ worker cancellation exact interface 완결, HTTP client timeout/closed와 구현 이력 분리, channel canonical 이름과 지적 문체 수정 | 제거 API·미선언 type·구현 이력 no-hit, 공통·다섯 언어 exact parity, guide code·link·table·fence·verifier·diff 통과 | 완료 | 2026-07-17: 제거 surface·구현 이력 no-hit; `verify-framework-doc-contracts.sh` clean (`languages=5`, `exact_documents=24`, `formal_documents=53`, `declarations=1164`); guide 11개 link·table·fence clean; scoped `git diff --check` clean |
| S3-F11-B | E2E·render 정리: Config 1~3 dispatch reason/action을 `no_handler`·`reply_error`·`drop`으로 통일하고 enum 오기 제거, C++/.NET/Java gap과 Java exact 문서의 실제 mkdocs anchor 정정 | 닫힌 값 exact match, 195개 실제 render link·anchor 0 오류, verifier·scoped diff 통과 | 완료 | iteration 11 Claude Config 1~3·gap anchor finding 반영. 8개 대상 파일 aggregate `e51ebe65…00bf`; Config 1~3의 이전 enum 표기 no-hit, 실제 MkDocs Unicode slug·중복 heading 규칙으로 195개 문서의 Markdown 링크 1,651개를 렌더해 link·anchor 오류 0건, scoped `git diff --check` 통과. Verifier의 anchor 계산도 실제 렌더 규칙으로 정렬했고 `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declarations 1,164·feature map 55·scenario 955) |

### 7.4 Iteration 12 수정 묶음

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F12-A | C++ exact interface·guide 정렬: DI scope owner와 factory overload, interface catalog의 worker·Spot·Actor·STREAM 표면, STREAM builder factory와 끊긴 문장, Connector/server 책임, 지원 언어와 현재 계약 문체, overview 내부 구현 노출을 정리 | C++ exact declaration·guide 예제 일치, 미선언 API·내부 배선·roadmap·금지 문체 no-hit, code fixture·verifier·실제 render·link·table·fence·scoped diff 통과 | 완료 | 2026-07-17: [iteration 12 finding ledger](log/s3-document-review/iteration-12/finding-ledger.ko.md)의 C12-01~09·S12-01 반영. 9개 대상 파일 aggregate `6b397fdf…17cc`; 미선언 guide 표면·overview 내부 배선·지원 언어 roadmap·금지 문체 scoped no-hit, JSON parse와 scoped `git diff --check` 통과. `pymdownx` 실제 render 8개 문서·link 180·table 41·fence 98·오류 0. `scripts/verify-framework-doc-contracts.sh` → `FRAMEWORK DOC CONTRACTS CLEAN`(languages 5·exact 24·formal 53·fixture 19·declarations 1,164) |
| S3-F12-B | Node STREAM codec guide를 실제 `streamPayloadCodec`·`addStreamCodec(...)`·MessagePack/Protobuf extension 경로와 정렬 | 추후 범위 서술 no-hit, source symbol·guide 예제 대응, verifier·실제 render·link·table·fence·scoped diff 통과 | 완료 | [iteration 12 finding ledger](log/s3-document-review/iteration-12/finding-ledger.ko.md)의 S12-02 반영. Node STREAM guide SHA-256 `ed24d0d2…b86c`; JSON 전용·추후 범위 서술 no-hit, `streamPayloadCodec`·`addStreamCodec(...)` source와 `/framework` MessagePack·Protobuf extension 및 root builder 예제 대응 확인, codec contract 6/6 통과. 실제 MkDocs render 202개 문서·1,764개 링크에서 오류 0건, fence 14·table 0·scoped `git diff --check` 통과. `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declarations 1,164·feature map 55·scenario 955) |

### 7.5 Iteration 13 수정 묶음

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F13-A | Actor join commit 순서, Actor location의 Spot generation, Redis transfer key encoding, server·HTTP host·Connector codec owner, transfer terminal metric, 명시적 codec mismatch 오류와 모든 언어 exact·fixture·inventory 정렬 | 공통 계약과 다섯 언어 exact·Redis fixture·inventory 일치, stale 실패 무변경, 구분자 충돌 방지, codec owner·terminal outcome 닫힘, verifier·render·diff 통과 | 완료 | 2026-07-17: [iteration 13 finding ledger](log/s3-document-review/iteration-13/finding-ledger.ko.md)의 C13-01·02·03·06·07·08을 Core Actor 정식 계약과 재대조하고 Core 문서는 수정하지 않았다. 공통 계약·다섯 언어 exact·Redis fixture·inventory·verifier와 Config 10 commit evidence 순서를 정렬했다. 18개 대상·검증 입력 aggregate `38f362fd…daf7`; `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·connector exact 4·formal 53·fixture 19·declaration 1,164·feature map 55·scenario 955), Redis·inventory JSON parse, 실제 render 13개·link 106·table 32·fence 28, scoped `git diff --check` 통과 |
| S3-F13-B | Kotlin ToActor TA-A3·A4, Kotlin·Node transfer ST-B3 feature map과 Kotlin STREAM `close()` guide 정렬 | 미구현 lifecycle은 차단으로 정확히 표시하고 완료 증거로 사용하지 않음, 기본 빈 state transfer 성공 의미와 `close()` 공개 동작 일치, render·diff 통과 | 완료 | [iteration 13 finding ledger](log/s3-document-review/iteration-13/finding-ledger.ko.md)의 C13-04·05·09 반영. 4개 문서 aggregate `2d24fd87…591f`; Kotlin TA-A3·A4와 Kotlin ST-B3는 미구현 상태를 완료 증거에서 제외했다. Node ST-B3 focused runner는 기본 빈 state 성공 자체는 통과했지만 현재 `joined -> location_committed` 단언이 공통 순서와 달라 전환 필요로 정확히 기록했다. Java `ZLinkStreamSessionContextStateTest`와 `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·formal 53·feature map 55·scenario 955), 실제 render 4개·table 4·fence 6, scoped `git diff --check` 통과 |
| S3-F13-C | 전체 scope 실제 Markdown render에서 발견한 link·anchor 오류 12건 정리 | 202개 문서 전체를 pymdownx Unicode slug 규칙으로 렌더하고 모든 local file·anchor link 오류 0, verifier·scoped diff 통과 | 완료 | 실제 pymdownx Unicode slug가 `·`, `+`, `&`, `↔` 제거 뒤 양쪽 space를 각각 `-`로 보존하는 규칙에 맞춰 7개 문서의 잘못된 anchor link 12건을 수정하고 verifier 계산도 같은 규칙으로 정렬했다. 8개 대상 aggregate `d8dcf93d…612a`; 전체 202개 문서·Markdown link 1,791개 render에서 local file·anchor 오류 0, `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·formal 53·feature map 55·scenario 955), scoped `git diff --check` 통과 |

### 7.6 Iteration 14 수정 묶음

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F14-A | Actor durable location과 ready route 공개 시점 분리, transfer metric terminal, C++ channel request 예제, HTTP C++ timeout 공개 표현, C++ transfer gap 순서 정렬 | owner 계약과 E2E·exact 예제·gap이 같은 의미를 사용하고 internal type 노출·폐기 순서 no-hit, verifier·render·diff 통과 | 완료 | 2026-07-17: [iteration 14 finding ledger](log/s3-document-review/iteration-14/finding-ledger.ko.md)의 C14-01~05를 5개 문서와 C++ code fixture inventory에 반영. 6개 대상 aggregate `d420f47c…4bfb`; HTTP public spec의 internal `boundary_error_t` 노출·mesh 인자 없는 request 예제·폐기 transfer 순서 scoped no-hit, JSON parse와 scoped `git diff --check` 통과. `pymdownx` 실제 render 5개 문서·link 282·table 21·fence 57·오류 0. `scripts/verify-framework-doc-contracts.sh` → `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·formal 53·fixture 19·declarations 1,164·scenario 955) |
| S3-F14-B | message-flow reason/action stale 값과 C++·Java·Kotlin guide의 금지 문체·구어체 정리 | 닫힌 값 `no_handler`·`reply_error`·`drop` 일치, 대상 표현 no-hit, verifier·render·diff 통과 | 완료 | [iteration 14 finding ledger](log/s3-document-review/iteration-14/finding-ledger.ko.md)의 C14-06~07 반영. .NET guide·C++ PubSub/SpotService map·C++/Node gap의 message-flow reason/action을 `no_handler`·`reply_error`·`drop`으로 정렬하고 C++의 `빠르고 좋다`, Java/Kotlin의 `직렬로 돈다`를 중립 표현으로 바꿨다. 8개 영향 문서 aggregate `ac976d7e…3cf8`; 대상 stale 값·문체 scoped no-hit, 실제 pymdownx 전체 render 202개·link 1,794개·오류 0, `FRAMEWORK DOC CONTRACTS CLEAN`(exact 24·formal 53·feature map 55·scenario 955), scoped `git diff --check` 통과 |

### 7.7 Iteration 15 동결 복구

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F15-A | iteration 15 시작 전 덮어써진 7개 문서의 실제 pymdownx anchor 12건과 verifier slug 계산 복구 | 202개 전체 render의 local file·anchor 오류 0, verifier가 실제 pymdownx slug와 같은 결과, diff 통과 | 완료 | 7개 drift 파일의 anchor 12건을 실제 render ID로 복구하고 verifier가 punctuation 제거 뒤 공백 각각을 `-`로 보존하도록 정렬. `FRAMEWORK DOC CONTRACTS CLEAN`, 전체 render 202개·link 1,778·오류 0, scoped `git diff --check` 통과. 복구 뒤 문서 aggregate가 iteration 15 동결값 `2bbb5364…8324`와 다시 일치 |

### 7.8 Iteration 17 무효화 뒤 복구

| ID | 담당 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-F17-A | 별도 구현 작업이 덮어쓴 실제 pymdownx anchor 12건과 implementation-gap 절 이동으로 생긴 anchor 1건, verifier slug 계산 복구 | 202개 전체 scope를 실제 pymdownx로 렌더해 local file·anchor 오류 0, verifier가 같은 slug를 판정하고 JSON·diff 통과 | 완료 | 8개 문서의 anchor 13건을 실제 render ID로 수정하고 verifier가 punctuation 제거 뒤 생긴 각 공백을 별도 `-`로 보존하도록 복구. 대상 9개 파일 aggregate `4212ec97…2c5e`; `FRAMEWORK DOC CONTRACTS CLEAN`, 실제 render 202개·대상 포함 204개·local link 3,747개·오류 0, JSON 15개 parse와 scoped `git diff --check` 통과 |

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

- [x] 채택된 open documentation finding이 0개이며 무효 iteration의 미완료 출력을 finding이나 clean으로 계산하지 않았다.
- [x] 변경 문서의 실제 render, 예제 API, link와 원본·번역 동기 검증 증거가 있다.
- [x] iteration 1~28의 Codex·Claude Sonnet 결과와 미채택·hash drift 기록을 보존했다.
- [x] 두 리뷰어의 `DOC REVIEW CLEAN`이 없음을 명시하고, 사용자가 추가 반복 없이 S3를 종료하도록 승인했다.
- [x] 병렬 Core 구현은 S1 기준선의 red/green 작업으로 진행됐고 S2·S3 목표 계약의 근거로 사용되지 않았다.
- [x] iteration 8까지 Core 계약 finding을 반영했으며 이후 framework finding에서 Core 계약을 다시 바꾼 항목은 없다.

### 7.9 ChannelName 단일 주소·ClientServer 계약 재개방

2026-07-20 draft 리뷰에서 이전 S1·S2·S3 종료 snapshot에 없던 공개 계약 변경을 확인했다.
이 절은 이전 완료 이력을 덮어쓰지 않고 amendment의 현재 상태를 소유한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-CH-01 | Core membership 0개 MeshNode 정식 계약 | 호출 전용·Node direct MeshNode의 lifecycle, ready, drain, messaging과 오류를 `service/01-mesh-node.*`에 한영으로 고정하고 기존 “ChannelName 하나 이상” 조건을 정확히 변경 | 완료 | 시작 기준 `86258cb9a3ec`. `service/01-mesh-node.{ko.md,md}`에 membership 0개 start·ready·peer admission·Node direct·remote Channel·multicast·drain·query·오류와 §12 contract red test 요구를 한영으로 고정했다. 공개 C signature 추가 없음. SHA-256 ko `eeee24d4…ba2d4`, en `e0ecd6ba…34d27`; `check_public_surface.py . core/build/lib/libzlink.so` PASS(192 exports), 두 문서 link·fence 검사와 scoped `git diff --check` 통과 |
| S2-CH-01 | draft 결정·정식 owner·Core/bindings 영향 확정 | draft §10의 항목을 exact API·오류·handler context 분리·ClientServer descriptor·network identity·관측 계약으로 닫고 정식 문서 owner를 고정 | 완료 | 정식 owner를 공통 `01`·`02`·`04`·`05`, server `10`·`11`·`12`·`13`·`20`·`21`·`40`·`41`·`50`·`52`·`53`·`54`로 고정했다. Channel 호출은 ChannelName 하나, runtime 중복 검사는 process-local topology 등록, target 없음은 `RequestTargetNotFound`, 알려진 pipe 미준비는 `RouteNotConnected`, unsolicited reply는 `RequestProtocolError`로 닫았다. Channel·Node context, ClientServer 전용 descriptor·Redis key, listener identity와 물리 route 관측 owner를 분리했다. Core 추가 영향은 S1-CH-01의 membership 0개 계약뿐이며 기존 DEALER·ROUTER·actual endpoint 조회를 사용한다. Core bridge·relay, MeshNode descriptor 재사용과 새 bindings 우회 API는 도입하지 않는다. 다섯 언어 exact interface·gap은 S2-CH-03, E2E·sample·공용 fixture는 S2-CH-04가 소유한다 |
| S2-CH-02 | framework 공통·server 정식 spec 변경 | ChannelName 단일 주소, 프로세스 내부 송신 경로 중복 금지, RouteMesh Client/Server, ClientServer 방향·자동 발견·weight·drain, 다른 송신 경로 completion, bind·advertised host를 정식 owner에 중복 없이 기록 | 완료 | 위 17개 정식 문서에 단일 Channel route, 역할·중복·오류, cross-egress Spot completion, ClientServer 방향·발견·weight·drain·재시작, `channel-server` Redis record, BindHost·AdvertiseHost·자동 port와 topology별 record, context·monitoring·trace·correlation·drain을 반영했다. 문서 aggregate SHA-256 `cf6fff43…e7141`; 실제 pymdownx render 17개·local link 177개·오류 0, 금지 표현·tab no-hit, scoped `git diff --check` 통과. 전체 verifier는 병렬 S2-CH-03의 .NET·C++·Node exact code fixture와 Java source inventory가 아직 갱신 중이라 그 5건만 실패했으며 공통·server formal finding은 없었다 |
| S2-CH-03 | 다섯 언어 exact interface·gap 변경 | .NET·C++·Java·Kotlin·Node의 Channel client, RouteMesh·ClientServer builder, Channel·Node context, network identity와 오류 타입을 고정하고 현재 구현 차이를 `90-implementation-gap.ko.md`와 언어별 gap에 기록 | 완료 | 다섯 언어 exact interface에 ChannelName 단일 client, `Channel(name).Server()/Client()` 역할 builder, ClientServer builder·runtime·전용 descriptor, Channel·Node context, BindHost·AdvertiseHost·port 0 listener와 오류 표면을 고정했다. Kotlin은 Java 정본 타입을 재사용하고 DSL만 투영한다. `90` §12.39와 각 언어 gap에 현재 source·package 차이를 분리했다. 대상 21문서 aggregate SHA-256 `bd534b1a…c365`; pymdownx render 21개 성공, Markdown link 346개 inventory, exact link 오류·금지 표현·tab·trailing space 0, scoped `git diff --check` 통과. 전체 verifier의 새 표면 forbidden·code fixture·declaration inventory·required fragment 실패는 S2-CH-04 verifier/fixture 전환 대상이며 source 불일치는 §12.39 구현 gap으로 보존했다 |
| S2-CH-04 | 공통 E2E·sample 계약과 fixture 고정 | `config-12-channel-egress-routing.ko.md`에 `CH-E2E-01~10`·`CH-REG-01~09`를 고정하고 feature map·runner inventory, 7개 공통 sample의 호출·역할·물리 topology와 공용 fixture 위치를 확정 | 완료 | Config 12에 단일 Channel egress, RouteMesh 양방향 역할, ClientServer 방향·선택·drain·재시작, cross-egress Spot completion, 충돌·즉시 오류, 자동 port·AdvertiseHost와 회귀 9건을 고정했다. 공통 role JSON과 7개 sample topology JSON을 언어별 복사 없이 사용하는 fixture로 추가하고, 다섯 lane feature map·runner·compile-negative inventory를 기록했다. Unified/.NET inventory와 verifier를 새 exact 표면, transition owner, Config 12의 19개 ID와 두 fixture의 hash·semantic gate에 맞췄다. 22개 검증 대상 aggregate SHA-256 `ab6f82c2…25b9`; `FRAMEWORK DOC CONTRACTS CLEAN`(5개 언어·exact 24·connector exact 4·formal 55·code fixture 19·declaration 1,246·transition owner 13·member 190·feature map 55·scenario row 955), 실제 pymdownx render 17개·local link 182개·오류 0, JSON 4개 parse, stale 호출·tab·trailing space no-hit와 scoped `git diff --check` 통과 |
| S3-CH-01 | amendment review snapshot 동결 | S1-CH-01·S2-CH-02~04 전체 문서, exact interface, E2E·sample·fixture·verifier 입력의 commit·파일·aggregate hash 고정 | 완료 | [`iteration-1 manifest`](log/s3-channel-amendment/iteration-1/manifest.ko.md)에 기준 HEAD `86258cb9…734a`와 Core 2·formal 17·exact/gap 21·S2-CH-04 22에서 중복 5개를 제거한 57개 전체 scope를 동결했다. [`scope-files.txt`](log/s3-channel-amendment/iteration-1/scope-files.txt) SHA-256 `89c2155f…9428`, [`scope-files.sha256`](log/s3-channel-amendment/iteration-1/scope-files.sha256) aggregate `d5ecc21f…ff6d`; 57/57 파일 hash 재검증과 전체 framework verifier 통과. Codex·Claude Sonnet에 byte 단위로 동일하게 제공할 [`prompt.md`](log/s3-channel-amendment/iteration-1/prompt.md) SHA-256 `d91cf506…1f90`을 고정했고 manifest·prompt 실제 pymdownx render 2개·local link 4개·오류 0, tab·trailing space no-hit와 scoped `git diff --check` 통과 |
| S3-CH-02 | Codex·Claude Sonnet 독립 문서 review loop | 같은 snapshot의 Core·framework 계약·exact interface·E2E·sample을 전체 검토하고 finding 수정 뒤 전체를 다시 리뷰 | 진행 중(iteration 12 준비) | Iteration 11의 96개 snapshot에서 Codex는 `.NET` guide catalog·metric, Node 전역 drain·열린 reason, Config 11 reason과 gap 이력을 발견했고 Claude는 교차 언어 reason 철자와 공통 E2E README의 제거 policy 표현을 발견해 두 결과 모두 `NOT CLEAN`이었다. 모든 finding을 exact inventory와 공통 fixed drain 계약으로 수정하고 public symbol·metric·5언어 reason·policy 변형 verifier를 추가했다. 결과: `log/s3-channel-amendment/iteration-11/{codex,claude-sonnet}/review.ko.md`; iteration 12에서 전체 scope를 다시 검토한다 |
| S3-CH-03 | amendment 문서 gate 종료 | open finding 0, render·link·signature·fixture verifier 통과, 두 `DOC REVIEW CLEAN` 또는 사용자 승인 종료를 기록 | 미착수 | S3-CH-02 선행 |

이 계약과 영향이 없는 Core 결함 수정과 네 언어 lane 작업은 병렬로 계속할 수 있다. 다만
`S3-CH-03`이 끝나기 전에 Channel 공개 API, sample topology, 관련 E2E·guide·framework review를 완료로
판정하거나 draft interface를 추측해 구현하지 않는다.

### 7.10 Classic fanout 자동 연결 계약 재개방

2026-07-20 재검토에서 목표 설계의 classic fanout 자동 발견과 정식 spec·Config 3의 manual-only
계약이 서로 다름을 확인했다. 이 절은 다른 언어의 현재 구현만으로 공개 계약을 정하지 않으며, 목표
설계와 정식 owner를 먼저 맞춘 뒤 네 framework 언어의 공개 API·runtime·E2E를 같은 동작으로 검증한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-FO-01 | fanout 자동 연결 계약·구현·E2E 재감사 | 목표 설계, 공통 정식 spec, 네 언어 exact interface·public source, location row·lease·role 선택과 Config 3의 충돌·누락을 파일 단위로 확정 | 완료 | 목표 설계 F-05와 S3-CH Codex blocker가 subscriber의 동일 ChannelName publisher 자동 발견을 요구한다. C++·Java·Node의 generic peer 선택 primitive는 있으나 전용 fanout descriptor·store·runtime과 현재 Config 3 자동 discovery 증거가 없고, .NET은 endpoint 없는 subscriber 공개 표면도 없다. Core 변경은 필요하지 않다. Java와 .NET binding은 PUB routing ID와 actual endpoint public API를 이미 제공한다. C++과 Node binding은 PUB·XPUB typed routing ID public surface가 없으므로 두 binding의 최소 보강과 internal local package 갱신이 선행된다. 나머지는 framework location runtime·Redis extension·공개 builder·E2E가 owner다 |
| S2-FO-02 | 공통·server spec과 언어별 exact interface 수정 | publisher 게시, subscriber의 동일 ChannelName·publisher role 선택, lease 만료·재등록, manual mode 경계, location store startup 조건과 네 언어 공개 signature를 정식 spec에 먼저 고정하고 현재 차이는 gap에 기록 | 완료 | Store 등록 publisher만 fixed/allocated Publisher RID, actual advertised endpoint와 owner lease를 9-field 전용 descriptor로 게시한다. Store 없는 고정 endpoint publisher와 manual subscriber는 유지하고 endpoint 없는 automatic subscriber만 store를 필수로 한다. 다섯 언어에 전용 descriptor/key, update·remove·list operation과 `(kind,name)` routing allocation member를 맞추고 §12.40 gap에 현재 구현 차이를 기록했다. Exact 14개 render·link 249개와 5-lane parity gate가 통과했다 |
| S2-FO-03 | Config 3 자동 연결·회귀 시나리오 확정 | 자동 publisher 게시, 동일 channel publisher만 연결, 타 channel·타 role 미연결, publisher 추가·제거 수렴, lease 만료·재등록 복구, manual endpoint 비회귀와 잘못된 store 구성 startup 실패를 공통 scenario ID와 네 언어 feature map에 고정 | 완료 | Config 3에 PS-D1~D7와 PS-E1~E2를 추가하고 기존 PS-A1~C1을 유지했다. Dedicated kind·ChannelName 격리, add/remove, lease expiry·same RID re-register, store fail-static/recovery, port 0, bounded observer lifecycle, manual no-store와 automatic subscriber store 누락을 다섯 feature map에 고정했다. ClientServer·fanout Redis fixture의 canonical key·field order·kind isolation과 sample topology semantic gate를 verifier에 추가했고 전체 검증은 `FRAMEWORK DOC CONTRACTS CLEAN`(declarations 1,293·actual scenario rows 1,001)이다 |
| S3-FO-01 | fanout amendment 독립 문서 review loop | 변경된 공통·server spec, exact interface·gap, Config 3·feature map·fixture를 같은 snapshot으로 Codex와 Claude Sonnet이 독립 검토하고 open finding 0 및 verifier·render·link 통과 | 진행 중(iteration 12 준비) | Iteration 11의 finding은 guide와 fixed drain 교차 계약에 집중됐고 fanout 자동 연결 자체의 새 finding은 없었다. 모든 공통 finding 수정 후 Channel·fixed drain과 같은 iteration 12 snapshot으로 전체를 다시 검토한다 |

### 7.11 MeshNode drain policy 계약 재개방

2026-07-20 재검토에서 `ReleaseAndRecreate`의 자동 Spot 재구성 문구와 local-only Spot 생성 계약이 서로
다름을 확인했다. 이 절은 remote Spot resolve·messaging을 remote Spot creation으로 확대하지 않는다.
분산 activation을 새 공개 계약으로 추가해야 하는 설계는 구현하지 않고 별도 설계 이슈로 분리한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-DP-01 | drain policy 계약·구현 전수 감사 | 공통 spec, 다섯 언어 exact interface와 네 runtime에서 Spot create/get-or-create의 local-only 경계, remote resolve·messaging, 두 policy 분기와 자연 종료 관측 가능성을 파일·symbol로 확정하고 설계 A·B를 비교 | 완료(설계 A) | 네 runtime의 create/get-or-create는 local-only이고 resolver·remote send/request는 기존 owner row만 사용한다. .NET·Java·Node의 release 분기는 local Spot만 닫으며 C++ policy는 state에 저장되지만 runtime이 읽지 않는다. 어떤 언어에도 serving node 선택, remote create, owner claim, activation, fencing과 state 복원이 없다. `DrainNatural`의 자연 종료 조건도 공개 계약이나 runtime snapshot으로 관찰할 수 없다. Policy enum·builder를 제거하고 admission seal→accepted work→Actor handoff→STREAM barrier→남은 local Spot close→owner cleanup→bounded terminal의 단일 순서를 채택했다. 설계 B는 별도 distributed activation draft 대상이다. 감사: `log/s3-drain-policy-amendment/audit.ko.md` |
| S2-DP-02 | 고정 MeshNode drain 정식 계약과 exact interface 확정 | 설계 A를 채택하면 policy enum·builder option과 자동 재생성 문구를 제거하고 admission seal, accepted turn, Actor handoff, STREAM barrier, local Spot cleanup, owner release와 bounded force stop의 단일 순서를 공통·server spec과 다섯 언어 exact interface에 고정한다. 설계 B가 필요하면 정식 spec을 수정하지 않고 별도 draft로 분리한다 | 완료 | `05-framework-api`, server `20`·`24`·`40`·`54`가 local create, remote resolve와 fixed drain 책임을 같은 의미로 고정한다. 다섯 언어 exact interface에서 policy enum과 builder option을 제거했고 §12.41에 현재 source 차이를 기록했다. Distributed activation은 10.0.0 계약에 추가하지 않았다 |
| S2-DP-03 | drain E2E·sample·fixture·verifier 계약 확정 | 평상시 request 완료는 Spot을 닫지 않고, drain 뒤 신규 admission 거부, accepted turn·handoff·STREAM 선행, local Spot cleanup, remote resolve 비회귀, hidden remote create 금지와 terminal result 1회를 다섯 lane에서 검증하며 제거 표면 no-hit을 구조적으로 검사 | 완료 | Config 11 `OBS-C3`를 fixed drain 회귀로 교체하고 Bingo sample을 같은 계약에 연결했다. Exact inventory는 다섯 언어 policy 표면을 forbidden fragment로 고정하고 code/declaration fixture를 갱신했다. Verifier는 `FRAMEWORK DOC CONTRACTS CLEAN`(declarations 1,293, feature maps 55, actual scenario rows 1,001), `git diff --check`도 통과했다 |
| S3-DP-01 | drain amendment 독립 문서 review loop | 정식 spec·exact interface·gap·E2E·sample·verifier를 같은 snapshot으로 Codex와 Claude Sonnet이 독립 검토하고 open finding 0, render·link·verifier 통과 | 진행 중(iteration 13 준비) | Iteration 12 snapshot 뒤 fixed drain 순서, .NET guide의 FlowOrigin·metric·Spot interface, Node exact의 전역 drain 잔여와 다중 Mesh fail-fast 계약을 수정해 snapshot drift가 발생했다. C++·.NET·JVM·Node 구현도 같은 fixed order와 multi-mesh fail-fast로 갱신됐다. 변경 문서와 source를 새 iteration 13 scope로 다시 동결해 두 reviewer 모두 처음부터 재검토해야 하며 iteration 12 결과는 clean 증거로 사용하지 않는다 |

### 7.12 Instance Spot 주소 기반 분산 activation

Instance Spot은 제거한 MeshNode drain policy를 복원하지 않는다. 기존 Entry·Domain Spot 생성과 `SpotHandle`
호출은 local-only·existing-only로 유지하고, 명시적인 `InstanceSpotAddress`에만 target-side claim과 지연
activation을 적용한다. 설계 초안은
[`instance-spot-draft.ko.md`](./instance-spot-draft.ko.md)가 소유한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-IS-01 | draft 결정과 blocker 종료 | Target coordinator claim, Store-only CAS fence, Core monotonic owner deadline, 다중 Mesh source 선택, pre-admission redirect 1회, request 비재제출, Closing 복구, actor-free lifecycle, async submit 의존성과 기본값을 확정하고 §19 open decision을 모두 닫음 | 완료 | `instance-spot-draft.ko.md` §5~§19를 cold placement 전용 Core 입력, Ready owner의 기존 exact Spot direct API 재사용, `claim_owner`·`mark_ready`, Store-only CAS fence, actor-free lifecycle과 `4096`·`3초` 실제 기본값으로 통합했다. §20은 앞 절을 덮어쓰는 별도 계약이 아니라 POSD 재검토 근거와 적용 확인만 소유한다. 제거한 target·mode·authority-bearing driver record·빈 create·option 0 sentinel은 현재 목표 계약에 남기지 않았다 |
| S2-IS-02 | Core·Framework 정식 spec과 exact interface | S2-IS-01 결정 뒤 Core 한영 spec·versioned struct ownership, Framework 공통·server spec과 다섯 언어 exact interface를 구현 전에 고정하고 현재 차이를 gap에만 기록 | 완료 | Core service README와 Spot 한글·영문 spec에 일반 `spot.h`와 Framework runtime용 `instance_spot_driver.h`의 경계, 고정 layout placement·activation data·claim result, placement 전용 send/request, exact Spot route redirect와 handle 기반 renew·close를 고정했다. Framework 공통 spec과 .NET·Java·Kotlin·Node.js·C++ exact interface도 actor-free lifecycle, opaque driver wrapper, Store snapshot·fence와 async-only address call로 맞췄고 현재 구현 차이는 `90-implementation-gap.ko.md`에만 기록했다 |
| S2-IS-03 | contract test·E2E·sample·verifier 계약 | `IS-C01~11`, `IS-B01~05`, `IS-F01~15`, `IS-REG-01~14`, `IS-E2E-01~31`, PlayerQuest·OrderWorkflow reference sample과 forbidden surface를 공통 fixture·verifier에 고정 | 완료 | Config 14의 76개 ID, PlayerQuest·OrderWorkflow sample 계약, contract inventory와 두 verifier를 새 interface로 갱신했다. `verify-framework-instance-spot-contracts.sh`는 5개 언어·76개 scenario·Core 한영 3쌍·Redis 상태 3개·CAS operation 5개를 통과했고, `verify-framework-doc-contracts.sh`도 5개 언어·24개 exact 문서·20개 code fixture·1395개 declaration을 통과했다 |
| S3-IS-01 | 독립 문서 review loop | 같은 snapshot의 Core·Framework spec, exact interface, E2E·sample·verifier를 Codex와 Claude Sonnet이 독립 검토하고 open finding 0, render·link·verifier 통과 | 재검토 대기 | 기존 iteration 1은 당시 snapshot의 증거로 보존하지만 §20 amendment 뒤 snapshot에는 적용하지 않는다. S2-IS-01~03을 다시 닫은 뒤 새 hash로 독립 검토를 처음부터 수행한다 |

| 구현 ID | 단계 | 완료 조건 | 상태 | 선행 조건 |
|---|---|---|---|---|
| S4-IS-CORE | Core | Instance kind, placement token owner claim, local atomic activation·ordering, monotonic lease fencing, bounded queue·watchdog와 C contract test 통과 | 진행 중(interface amendment 대기) | Core ABI·runtime·wire와 contract test의 최초 구현 및 concurrency 수정이 진행된 상태다. 새 정식 계약에 맞춰 target·mode·owner 전송과 driver record의 version·authority field를 제거하고, placement 전용 symbol, claim·mark-ready, exact route redirect, handle 기반 renew·close로 바꿔야 한다. 기존 direct Spot admission에는 Instance Ready 상태와 owner deadline 검사를 한 번만 추가해야 한다. 일반 `spot.h`와 Framework용 `instance_spot_driver.h` 분리도 source에 반영해야 한다. S2-IS-01~03과 S3-IS-01을 다시 닫기 전에는 구현을 완료로 판정하지 않는다. 버전은 올리지 않았다 |
| S5-IS-CORE-REVIEW | Core 독립 review | 같은 Instance Core source·header·한영 spec·C contract snapshot을 독립 reviewer 둘이 검토하고 ABI·ownership·lifecycle open finding 0 | 미착수 | S4-IS-CORE |
| S6-IS-RC | Core candidate 재동결 | S5 결과를 반영한 Core build·test와 sanitizer gate를 통과하고 candidate header·runtime hash를 새 manifest에 고정 | 미착수 | S5-IS-CORE-REVIEW |
| S7-IS-COMMON | 공통 준비 | 최종 Core candidate header·runtime hash, bindings projection checklist, local package manifest와 네 lane 공통 smoke를 고정하고 언어 구현은 수행하지 않음 | 미착수 | S6-IS-RC |
| S8-IS-DN | .NET binding→Framework lane | .NET binding projection·contract·내부 package 검증 뒤 Address·target coordinator·store CAS·factory·drain·E2E·reference sample 완료 | 미착수 | S7-IS-COMMON·S8-SA-DN |
| S8-IS-CPP | C++ binding→Framework lane | C++ binding projection·contract·내부 package 검증 뒤 같은 Instance Spot 공개 계약, Redis store, E2E와 지원 sample 완료 | 미착수 | S7-IS-COMMON·S8-SA-CPP |
| S8-IS-JVM | JVM binding→Framework lane | Java binding projection·contract·내부 package 검증 뒤 actor-free Java runtime·Kotlin projection, Redis store, E2E와 지원 sample 완료 | 미착수 | S7-IS-COMMON·S8-SA-JVM |
| S8-IS-NODE | Node binding→Framework lane | Node binding projection·contract·내부 package 검증 뒤 actor-free NestJS runtime, Redis store, E2E와 지원 sample 완료 | 미착수 | S7-IS-COMMON·S8-SA-NODE |

최종 교차 언어 회귀는 §15.3의 `S11-10D` 한 행만 소유한다.

Core·bindings version은 기능 검증 전에 올리지 않는다. 최종 Core candidate가 올라가면 bindings base version의
마지막 숫자를 `0`으로 맞추고, 이후 binding별 수정이 있을 때만 patch를 올린다. Package는
`scripts/local-package/` 정책에 따른 내부 위치에만 만들며 외부 registry에는 배포하지 않는다.

### 7.13 Framework one-way submit API 단순화

[`framework-submit-api-simplification-draft.ko.md`](./framework-submit-api-simplification-draft.ko.md)는 public
one-way messaging call의 `TrySubmit` 계열을 제거하고 비동기 submit 하나로 통일한다. Core·bindings와 runtime
내부 non-blocking primitive는 유지한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-SA-01 | public interface·사용처 감사와 결정 | RouteMesh node·channel, ClientServer, Spot, Actor, Logical Multicast, classic fanout, bound session, STREAM send·reply별로 다섯 언어의 현재 public call·wrapper·반환 type과 목표 signature, timeout owner·기본값, cancellation 표현·경합, result/error와 binding writable primitive·wakeup·cleanup capability를 전수 감사하고 제거 범위와 내부 allowlist를 확정 | 완료 | `framework-submit-api-simplification-draft.ko.md` hash `864c912e7fa54a6bd2e3022848811ea34decc626ef2da34f35bbb244a3c0ceef`. Family×source/exact/target call matrix, status·error, millisecond ceiling 기준 `1..INT_MAX` finite timeout, cancellation·shutdown 경쟁, Logical Multicast single Core blocking publish commit barrier, classic fanout 전용 result, session Actor relay와 binding capability를 확정했다. Direct identity와 select-one 재선택 책임을 Core 현재 public 계약에 맞게 분리했다. JVM은 public cancellation 입력이 없으므로 첫 non-blocking 시도 뒤 반환한 stage의 pending cleanup만 보장하고 pre-cancel attempt 0은 .NET·Node.js에만 적용한다. 계획 문서의 상태 표는 구현 진행표로 사용하지 않도록 결정 당시 감사 결과로 고정했다. 최종 독립 검토 상태는 `S3-SA-01`이 소유한다 |
| S2-SA-02 | 공통·언어별 exact spec 변경 | 공통 async·timeout·cancellation 계약과 .NET·Java·Kotlin·Node·C++ exact interface에서 public `TrySubmit` 계열을 제거하고 gap을 기록 | 완료 | `02-interaction-model`, `04-async-execution-policy`, `05-framework-api`, `server/20-spot-messaging`, `90-implementation-gap`과 .NET·Java·Kotlin·Node·C++ server exact interface를 async-only 계약으로 갱신했다. Unified·.NET inventory fixture와 verifier를 새 선언으로 맞췄고 public exact `TrySubmit` 계열 no-hit, `verify-framework-doc-contracts.sh` clean(`declarations=1288`) |
| S2-SA-03 | E2E·sample·verifier 변경 | RouteMesh node·channel, ClientServer, Spot, Actor, Logical Multicast partial detail, classic fanout subscriber 0, bound session, STREAM send·reply별로 즉시 완료, pending-full, bounded wait→성공, timeout, target-not-found, route-not-connected, 지원 언어 cancellation, late admission 0, duplicate reply invalid-state, shutdown·drain·recovery·terminal 1회를 scenario ID로 고정하고 public declaration·consumer에만 forbidden no-hit 적용 | 완료 | Common E2E `config-13-submit-admission.ko.md`에 `SA-E2E-01~20`, `SA-REG-01~04`를 고정하고 README index·prefix를 추가했다. `verify-framework-submit-api.sh` hash `5d23bfe608b41c07090b412b4e67ab11746405294cd3a1fb10308683f411c9a8`; 현재 `--contract` CLEAN(`languages=5 scenarios=20 regressions=4`)이고 `--implementation`도 CLEAN(`removed_name_hits=60`, `allowlisted_internal_hits=60`, `violations=0`, `signature_failures=0`, allowlist 12 owners·22 symbols·67 occurrences)이다. Verifier는 family별 exact declaration, local transport·mailbox·relay first admission, JVM cancellation 범위, RouteMesh·ClientServer select-one의 독립 target 수를 각각 검사한다. Public source·API snapshot·guide·internals·consumer·sample·E2E·runtime·server integration package를 다섯 언어에서 포함하고 Stream Connector package와 generated output은 범위에서 제외한다. Doc contracts clean, script syntax 통과 |
| S3-SA-01 | 독립 문서 review | 같은 snapshot을 두 reviewer가 검토하고 open finding 0, render·link·verifier 통과 | 완료 | 18파일 combined hash `2b0042114efb26c0b81865c80536256828b32e81de96c35709cb62660bc06638`. Round 5~6의 high 5건을 반영한 뒤 Round 7 같은 snapshot을 두 reviewer가 독립 검토해 각각 `blocker=0`, `high=0`을 확인했다. Submit contract CLEAN, framework doc contracts CLEAN(`declarations=1288`), script syntax와 diff 검사 통과 |
| S7-SA-BIND | Core bound-session generation fence | Actor에서 bound session으로 보내는 Core API가 `expected_binding_generation`을 받아 local·remote relay 모두 같은 generation만 사용하고 unbind·rebind 뒤 새 binding으로 넘어가지 않는 한영 spec·header·contract test를 통과한다 | 완료 | Core 한영 STREAM session spec·header에 binding generation 입력과 stale 오류를 고정했다. Remote bind는 owner의 Actor generation·membership epoch ACK 뒤에만 commit하며, peer disconnect는 pending bind를 한 번만 종료한다. Transfer readiness 뒤 새로 생성된 reverse route는 target reservation에 보관하고 activation 전에는 공개하지 않는다. Unbind·rebind race와 old-generation payload 차단 회귀, remote bound-session 회귀 20회, 일반·pre-bound transfer 회귀 각각 10/10, 독립 peer admission 28/28이 통과했다. Core version과 package는 변경하지 않았다 |
| S7-SA-CORE | Core send-ready capability gate | Core 정식 spec에 있는 `SEND_READY` infrastructure record를 ROUTER·local mailbox capacity 회복 때 destination별로 실제 생성하고 peer lifecycle signal이 pending waiter를 깨우며 polling 없이 한 signal·한 retry를 만드는 Core contract test를 통과한다 | 완료 | Node·Channel·Spot·Actor의 destination별 interest broker를 local mailbox와 ROUTER capacity 회복 신호에 연결하고 inline callback 재시도의 새 interest를 보존했다. Transfer reverse queue 대기에서는 session별 submit-order 잠금을 먼저 해제하고 재시도 때 epoch·binding generation을 다시 검증해 commit 교착을 제거했다. 종료 상태를 send/ingress 양쪽에 먼저 공개해 무한 대기 send 종료 회귀 20/20을 통과했다. `test_mesh_node_basic` 15/15, `test_mesh_monitor_matrix` 11/11, 일반·pre-bound transfer 각 10/10, 독립 peer admission 28/28과 Core focused CTest 6/6가 통과했다. 최신 `core/build/lib/libzlink.so.10.6.0` SHA-256은 `0e545679…929824`이고 Core source보다 새롭다. timeout, version과 package는 변경하지 않았다 |
| S7-SA-DN | .NET binding capability gate | Bounded async admission에 필요한 public writable wakeup·timeout·cancel·cleanup primitive의 contract test를 통과한다. 부족하면 binding patch·내부 package를 만들고, 충분하면 source 변경 불필요 증거를 고정한다 | 완료 | Framework runtime에서 최초 non-blocking 1회, ready signal별 retry 1회, inline ready 보존, terminal 경쟁 직렬화와 non-head cancellation 즉시 제거를 구현했다. .NET native receive record에서 누락된 `source_binding_generation`을 native mirror·public projection·변환에 추가해 Core ABI와 맞췄고, binding source suite 142/142가 통과했다. Core SHA-256 `0e545679…929824`, Build ID `13333c4a…`와 같은 native를 포함한 격리 10.6.1 NuGet candidate의 SHA-256은 `a61cc4dd…0c024`이며 package, NuGet cache와 server output hash가 일치한다. Exact spec §5에 따라 `ConfigureRouterSocket().SendHighWaterMark`와 `SendTimeout`을 MeshNode startup 전에 적용했고 adapter·initializer 2/2와 scoped RouteCodec·SpotNode 28/28이 통과했다. Linux x64 `ReceiverGate`는 encoded body 32 KiB, 4096-byte forward buffer·socket buffer 요청과 HWM 1을 사용해 실제 pending을 만들며, Core `SEND_READY` 1회·retry 1회·transport attempt 2회·commit 1회와 cleanup resource 0을 기록했다(`SubmitAdmission/logs/20260721-071456-1749709`). Capacity 1에서는 두 번째 operation이 attempt 1회·commit 0의 `Backpressured`로 끝났다(`20260721-071153-1744153`). Timeout 증가, polling submit, 반복 public submit과 raw-frame 우회는 사용하지 않았다. Core가 단일 ROUTER HWM을 제공하고 binding에 receive timeout이 없으므로 `ReceiveHighWaterMark`·`ReceiveTimeout`과 중복 `ConfigureSpotPublisher` 표면은 전 언어 contract 재설계 gap으로 분리했다 |
| S7-SA-CPP | C++ binding capability gate | 같은 C++ public primitive와 task cancellation·cleanup contract를 검증하고 필요할 때만 binding patch·내부 package를 만든다 | 완료 | Framework의 owner·destination별 bounded pending runtime은 최초 시도 1회, signal별 retry 1회, finite timeout, shutdown cleanup과 `max_pending` 전파 focused contract를 통과했다. C++ binding의 Actor·MeshNode bound-session send에 Core 정식 spec의 필수 `expected_binding_generation`을 전달하고 receive record의 `source_binding_generation`을 public projection까지 보존하도록 수정했다. 최신 Core `libzlink.so.10.6.0`의 SHA-256 `0e545679…929824`, Build ID `13333c4a…`와 동일한 native를 포함하는 격리 C++ binding 10.6.0 candidate를 만들었고 binding header contract, Framework messaging·MeshNode vertical focused test가 통과했다. Candidate tree SHA-256은 `0cf36be8…ee7d74`다. Shared package와 version reference는 변경하지 않았다 |
| S7-SA-JVM | JVM binding capability gate | Java binding primitive와 CompletionStage·Kotlin cancellation 연결 contract를 검증하고 필요할 때만 binding patch·내부 package를 만든다 | 완료 | Java binding에 destination별 `SEND_READY` typed payload와 `source_binding_generation`을 추가하고 C `sizeof`·`offsetof` fixture로 receive record 1200 bytes, SEND_READY 1064 bytes를 고정했다. 누락된 generation 때문에 message part가 0개로 해석되던 회귀를 수정해 원래 2초 Spot request test가 통과했다. Per-call one-way gate, 최초 `DONT_WAIT`, signal 1회당 retry 1회, `CompletionStage.cancel(false)`·Kotlin coroutine cancellation, timeout·shutdown cleanup을 검증했다. 최신 Core SHA-256 `0e545679…929824`, Build ID `13333c4a…`와 jar 내부 native가 일치하는 격리 10.6.3 candidate에서 focused 22/22, Java 335/335와 Kotlin 42/42가 통과했다. Version과 shared reference는 10.6.3으로 유지했다 |
| S7-SA-NODE | Node binding capability gate | Node binding primitive와 AbortSignal wakeup·cleanup contract를 검증하고 필요할 때만 binding patch·내부 package를 만든다 | 완료 | Binding에 event loop를 점유하지 않는 async Logical Multicast publish와 `MeshPublishResult`를 추가했다. Payload·metadata deep copy, queued→started cancel CAS, commit 뒤 최종 detail 보존, close 중 native handle 수명과 ready callback의 실제 drain mask 반환을 contract test로 고정했다. 최신 Core SHA-256 `0e545679…929824`와 tar 내부 native가 일치하고 addon이 그 파일을 로드하는 격리 10.6.0 candidate(`53f1fa59…bd3156`)에서 binding 13/13, Framework Logical Multicast 6/6과 candidate typecheck가 통과했다. Version과 shared reference는 10.6.0으로 유지했다 |
| S8-SA-DN | .NET lane | Red public contract test 뒤 모든 public messaging call·사용처를 `SubmitAsync`로 전환하고 family contract·sample·E2E·package consumer 통과 | 진행 중 | Public `TrySubmit`과 동기 one-way submit을 제거했다. Signal 기반 admission, timeout `1..INT_MAX`와 sub-ms ceil, STREAM reply claim-before-cancel, Logical Multicast bounded direct handoff·commit barrier를 반영했고 focused 53/53, solution build warning 0/error 0과 verifier가 통과했다. Config 13 process runner에서 ready node·ChannelName, unknown·disconnected RID, pre-cancel·invalid 우선순위, local·remote node, subscriber 0과 handler 비대기 7/7 및 `SA-REG-01~02`를 검증했다. 최신 Core를 포함한 격리 10.6.1 NuGet candidate만 resolve하는 mode를 runner에 추가하고 package·cache·server output의 managed package hash와 native SHA-256·Build ID를 대조했다. RID-direct `SA-E2E-02`는 공통 32 KiB encoded body·`ReceiverGate` 절차에서 signal·retry·commit이 각각 한 번이고 resource final count가 0임을 통과했으며(`SubmitAdmission/logs/20260721-071456-1749709`), `SA-E2E-03`은 capacity 1의 두 번째 operation이 attempt 1회·commit 0의 `Backpressured`로 끝났다(`20260721-071153-1744153`). `SA-REG-04` process race는 host stop 2회와 gate open 뒤 terminal·cleanup 각 1회, resource 0과 exit code 0을 기록했고 native lifecycle error가 없었으며(`20260721-071548-1752017`), internal double-dispose·ready race를 포함한 `ZLinkAsyncSubmitterTests` 21/21이 통과했다. Timeout 증가, polling submit, 반복 public submit과 raw-frame 우회는 사용하지 않았다. Unknown RID 분류, self RID local dispatch, validation 우선순위와 fast-fail payload 정리의 기존 회귀는 unknown·disconnected 각 100회와 disposal 200개가 통과했다. 전체 unit의 기존 reflection·correlation·G0 ledger·Config 3 runner 4건, Config 13의 나머지 family·counter·barrier와 `SA-REG-04.b`, 최종 전체 package consumer matrix가 남아 있다 |
| S8-SA-CPP | C++ lane | Red public contract test 뒤 public `try_submit` 제거, task submit 전환과 family contract·sample·E2E·package consumer 통과 | 진행 중 | Generic·RouteMesh·fanout·Logical Multicast·Actor·bound session·STREAM·session Actor relay를 task result로 전환했다. MeshNode·Spot·Actor·channel·Fanout·STREAM signal admission과 Logical Multicast bounded direct-handoff single Core call을 구현했다. 정식 C++ exact interface에 이미 있던 `route_client_t.send_to_channel/request_to_channel`을 ChannelName 기반 runtime registry에 연결했고 중복 ChannelName은 startup에서 거부한다. Self RID Node direct는 기존 application dispatcher와 drain accounting을 사용하는 내부 local bridge로 처리하며 payload ownership, handler 비대기, capacity 1 backpressure, exception 격리, seal 뒤 terminal 결과를 vertical test로 고정했다. 최신 Core와 격리 C++ binding 10.6.0 candidate를 사용한 Config 13 process runner에서 `SA-E2E-01·08·09·14·20`, `SA-REG-01·02`가 통과했고 `SA-REG-03`은 Kotlin 전용이라 N/A다(`e2e/SubmitAdmission/logs/20260721-064207-1675425`). C++ runtime에 target logical existence를 판정할 descriptor·location resolve data가 없어 unknown RID와 known disconnected RID가 모두 `RouteNotConnected`로 반환되므로 `SA-E2E-05`는 미구현 상태를 `90-implementation-gap.ko.md`에 기록했다. 나머지 Config 13 family·pending·deadline·counter·barrier, 전체 sample·E2E runtime과 최종 package consumer가 남아 있다. Shared package, version과 timeout은 변경하지 않았다 |
| S8-SA-JVM | Java·Kotlin lane | Red public contract test 뒤 public `trySubmit` 제거, CompletionStage·coroutine projection 전환과 family contract·sample·E2E·package consumer 통과 | 진행 중 | Java/Kotlin public `trySubmit`·`void submit`을 제거하고 CompletionStage·Kotlin call object로 전환했다. Destination별 signal admission, finite timeout, cancellation·shutdown cleanup, Logical Multicast barrier와 duplicate submit의 per-call exceptional completion을 구현했다. Self RID Node direct는 Core remote pipe 결과를 바꾸지 않고 기존 Mesh application dispatcher·serial queue·drain claim을 사용하는 local bridge로 처리한다. Focused test는 payload 독립 소유, handler 비대기·1회, missing·sealed 결과와 handler 예외 뒤 claim 정리를 검증한다. Expected RID를 명시한 manual peer registry에서는 unknown·disconnected를 각 100회 `TargetNotFound`·`RouteNotConnected`로 구분했다. 최신 Core SHA-256 `0e545679…929824`, Build ID `13333c4a…`와 동일한 native를 포함한 격리 10.6.3 candidate만 resolve한 Config 13 `all`에서 process `SA-E2E-01·05·08·09·14·20`과 `SA-REG-01~03`이 통과했다(`e2e/SubmitAdmission/logs/20260721-064711-1689232`). Mixed untyped peer가 있으면 불완전한 registry로 unknown을 단정하지 않도록 제한한 뒤 `SA-E2E-05` 100+100회도 다시 통과했다(`20260721-065101-1701742`). Java 전체 338/338이 통과했다. Timeout 증가, polling submit, 반복 public submit과 raw-frame adapter는 사용하지 않았다. Discovery·untyped manual peer의 unknown 분류, process pending·deadline·attempt·commit observer, 나머지 family, `SA-REG-04`, 기존 binding 전체의 zero-membership MeshNode start 1건, 전체 sample·E2E runtime과 최종 package consumer가 남아 있다 |
| S8-SA-NODE | Node.js lane | Red public contract test 뒤 public `trySubmit` 제거, Promise submit 전환과 family contract·sample·E2E·package consumer 통과 | 진행 중 | Public `trySubmit`을 제거하고 Promise result로 통일했다. 최초 attempt 1회, signal별 retry 1회, timeout·abort waiter 즉시 제거, timeout `1..INT_MAX`, STREAM reply claim-before-abort와 Logical Multicast bounded direct handoff·commit barrier를 구현했다. Core 10.6.0 SHA-256 `0e545679…929824`와 binding candidate `53f1fa59…bd3156`을 사용하는 격리 package mode에서 binding 13/13, Logical Multicast 6/6과 Framework typecheck가 통과했다. Config 13 `all`은 process `SA-E2E-01·05·08·09·14·20`과 `SA-REG-01·02`를 통과했고 `SA-REG-03`은 Kotlin 전용이라 N/A다(`e2e/SubmitAdmission/log/20260721-062558-1635847`). Typed expected-RID manual registry에서 unknown·known disconnected를 각 100회 `TargetNotFound`·`RouteNotConnected`로 구분했고, self·remote direct는 handler를 각각 한 번 처리했다. Discovery와 untyped manual endpoint에는 두 상태를 구분할 authoritative catalog가 없어 `SA-E2E-05·08`은 부분 구현이다. 나머지 family·pending·deadline·counter·barrier, binding legacy typecheck 13건과 broad migration 실패, 최종 package consumer가 남아 있다 |

최종 교차 언어 회귀는 §15.3의 `S11-10E` 한 행만 소유한다.

## 8. S4 — Core 구현·제거 코드 정리와 정식 spec 일치

### 8.1 red gate와 공개 API

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-01 | contract red test 작성 | 새 API 부재와 제거 API 존재를 test가 먼저 실패로 증명 | 완료 | surface gate red→green 완료(`contract_public_surface` PASS: 196 export 정확 일치·제거 identifier 0·한영 C block 일치). spec 절별 계약 test 3파일 확대 완료: `test_mesh_node_basic` 8, `test_mesh_peer_admission`(2-process) 10, `test_mesh_monitor_matrix` 6, `test_mesh_stress` 3 — 전부 green (2026-07-17 HEAD `5857824c2`+working tree) |
| S4-02 | public header를 10.0.0 spec에 맞춤 | 함수·type·enum과 result signature 일치 | 완료 | header 폐쇄가 frozen spec(52파일 `5bd7451d…`)과 일치(surface gate PASS, C/C++ compile OK). 신규 service header 6개 생성, 설치 규칙 포함 |
| S4-03 | export와 ABI 목록 갱신 | 새 symbol 존재, 제거 symbol 부재 | 완료 | `libzlink.vers` formal FUNC 196 명시 목록, `nm` 대조로 export=formal 정확 일치·제거/internal export 0(`contract_public_surface` PASS). SONAME 10 |
| S4-04 | MeshNode lifecycle과 handle kind 구현 | 생성, bind, start, drain, destroy 계약 통과 | 완료 | lifecycle 상태표·child EBUSY·shutdown deadline revoke(recv ESHUTDOWN/release 안전)·reply-after-STOPPED ESHUTDOWN까지 test green(`test_mesh_node_basic`, `test_mesh_monitor_matrix`). 2026-07-20 reciprocal peer churn 뒤 종료 RED에서 Router pipe ACK는 끝났지만 async mailbox callback의 마지막 active 확인과 `_scheduled` 해제가 교차해 quiesce wakeup을 잃는 경쟁을 확인했다. `process_async_mailbox()`가 scheduled bit 해제 뒤 stop 상태를 다시 확인하도록 수정했고 socket/context unit 2개, MeshNode lifecycle/node 2개와 reciprocal replacement·blocking-send shutdown·same-RID reconnect focused test가 통과했다 |
| S4-CH-01 | membership 0개 MeshNode 구현·회귀 | S1-CH-01의 red test를 먼저 고정하고 start·ready·Node direct·peer admission·Channel outbound·drain·shutdown이 가짜 ChannelName·weight 0 우회 없이 통과하며 기존 membership 1개 이상 회귀가 통과 | 완료 | 시작 기준 `86258cb9a3ec`. 공개 C API test를 먼저 추가하자 membership 0개 `start`만 `ZLINK_CONFIG_INVALID_STATE(705)`로 RED이고 기존 14개 basic test는 통과했다. `mesh_node_api.cpp`의 start 선행 조건에서 `channels.empty()` 검사만 제거했으며 공개 API·가짜 ChannelName·weight 0·cross-mesh bridge는 추가하지 않았다. 별도 `.artifacts/build/core-s4-ch01`에서 `test_mesh_node_basic` 15/15, `test_mesh_peer_admission` 27/27과 CTest 2/2가 통과했다. 새 two-process test는 empty peer query, 양방향 Node send·request/reply, zero-membership caller의 remote Channel send·request, Channel·multicast target 제외, active Node request completion 중 drain과 새 submit `ESHUTDOWN`을 검증하며 두 시나리오를 추가 5회씩 반복해 모두 통과했다. static runtime SHA-256 `d8f45962…a52895`; scoped `git diff --check` 통과. 병렬 Core dirty 변경을 보존하기 위해 공식 `core/build` rebuild는 수행하지 않았다 |
| S4-05 | peer descriptor와 admission 구현 | manual·discovery endpoint가 같은 handshake를 사용하고 MeshName, identity, lifecycle generation, descriptor revision, duplicate, security, ready와 drain 계약 통과 | 완료 | HELLO/ADMIT/REJECT/UPDATE handshake green + PEER_ADMITTED/REJECTED(result_code·errno)/CLOSED/DRAINING monitor event 구현·검증. inbound 관측 peer는 DISCOVERY source로 기록. `test_mesh_peer_admission` 10/10 |
| S4-05A | manual peer lifecycle 구현 | endpoint 및 예상 RID pin, connect·disconnect, discovery와 중복 source 병합, 누락 peer 상태를 관측하고 운영자가 모든 peer 연결을 설정해야 하는 계약의 test 통과 | 완료 | expected-RID pin(ESTALE)·중복 endpoint 병합(동일 intent id)·remove(un-admitted/ENOENT)·disconnect(EINVAL/ENOENT/ESTALE) 전용 test green(`test_mesh_monitor_matrix`). MIXED 병합 분기의 실도달 경로(수동 intent와 inbound endpoint 불일치)는 known risk로 S5 판정 대상 |

### 8.2 메시징과 runtime

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-06 | RID pipe와 channel index 구현 | 같은 MeshName의 ready RID만 선택 | 완료 | local+remote RR 후보·weight 실시간 반영·weight 0 제외 green(`test_remote_channel_round_robin_and_zero_weight_exclusion`) |
| S4-07 | node·channel 선택과 submit 구현 | direct와 round-robin이 한 send/request 호출 안에서 원자적으로 처리되고 RID-only 공개 select API가 없음을 contract test로 검증 | 완료 | 선택+submit 단일 호출 원자성 test green, RID-only select API 부재는 surface gate 보증 |
| S4-08 | Node·Channel·Spot direct send/request와 service envelope 구현 | application metadata codec, timeout, operation ID와 borrowed/retained multipart ownership test 통과 | 완료 | local·remote 왕복(metadata·timeout·ownership 포함) green, Spot direct 원격 ESTALE/ENOENT completion green |
| S4-08A | responder reply 구현 | opaque token one-shot, generation·shutdown 오류, source route 비노출과 S/S reply metadata 미지원 test 통과 | 완료 | one-shot(EALREADY)·generation guard(ESTALE)·timeout 뒤 폐기·shutdown 오류(STOPPED→ESHUTDOWN, revoked claim recv ESHUTDOWN) test green. reply metadata 미지원은 signature 차원에서 보증(파라미터 없음) |
| S4-09 | mailbox·ready·claim·batch 구현 | Node·Spot·Actor 격리, infrastructure 우선 drain과 lost wakeup 0건 | 완료 | stress 검증: 4 producer×500 submit에서 lost wakeup 0·claim leak 0·active_claims 0·pending 0(`test_mesh_stress` 3/3, ASAN·TSAN clean) |
| S4-10 | Logical Multicast multi-target submit 구현 | target channel 직접 선택, canonical metadata snapshot·검증, 조건부 local dispatch와 remote node당 1회 submit | 완료 | 대상별 일반 ROUTER submit+local mailbox admission·detail 수치 green(2-process multicast case), MULTICAST_COMMITTED/DROPPED event 검증 |
| S4-11 | shared message reference count 구현 | local Spot queue와 remote pipe 수명·실패 정리 검증 | 완료 | 2-spot fanout×300 publish×3-part에서 producer 즉시 close 후 전 part 무결 수신(refcount 공유 경로, `test_mesh_stress`), ASAN leak 0 |
| S4-12 | 기존 ROUTER backpressure 연결 | caller flags를 각 remote ROUTER send에 그대로 전달하고 DONTWAIT EAGAIN·blocking SNDTIMEO 뒤 EAGAIN·부분 전달 수치를 검증 | 리뷰 중 | publish 전용 option·writable probe·all-or-none rollback 제거. local mailbox는 대상별 admission/drop, remote는 일반 ROUTER submit 결과를 집계. Core CTest와 ASAN 통과 후 S3-11 독립 리뷰 진행 |
| S4-12A | remote multicast ingress staging 후보 제거 | publish 전용 staging·재시도·overflow peer 종료가 남지 않음 | 승인 종료 | 2026-07-19 사용자 결정으로 특수 수신 경로를 채택하지 않음. 일반 ROUTER ingress와 각 Spot mailbox의 기존 수용 경계만 사용 |
| S4-13 | no-relay와 duplicate guard 구현 | multicast loop와 중복 전달 0건 | 완료 | 1 publish=정확히 1회 전달+이후 400ms 무추가 record 전용 검증을 원격 multicast case에 추가, green. 구조적 no-relay(수신 node는 local match만 fan-out) |
| S4-14 | Spot local subscription 분리 | channel-scoped 등록·해제·수신 API와 remote subscription 없는 exact/prefix match 동작을 구현하고 public inventory query를 만들지 않음 | 완료 | exact/prefix match·idempotent 등록 green, inventory query 부재는 surface gate 보증 |
| S4-15 | Actor와 STREAM session owner 전환 | direct Actor mailbox, transfer fence, ActorRef와 bound session 회귀 통과 | 완료 | actor 전 경로(원격 lookup/messaging/destroy/join)+bound session 회귀 green(`test_mesh_peer_admission` 10/10, `test_mesh_node_basic` 8/8) |
| S4-15A | Actor transfer fence·token protocol 구현 | Core prepare가 64-byte sealed token을 발급하고 commit이 이 token, transfer ID, Actor generation과 정확히 다음 membership epoch를 검증한 뒤 mailbox/session fence를 수행한다. deterministic fake location authority로 prepare·commit·activate·abort·stale token contract test 통과 | 완료 | prepare·data plane·ACK·commit·activate·abort·reply relay와 오류 격자(fake authority 2-process) green. dispatch tail UAF는 ASAN 적발 후 수정. data-plane failure 심화 matrix는 S5 리뷰 입력으로 기록 |

### 8.3 삭제와 관측

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-16 | SpotNode mode와 PUB/SUB plane 제거 | mesh_pub, mesh_xsub와 mode branch source no-hit | 완료 | `runtime/services/spot`·`api/spot`·`api/actor`·`api/service`·data plane 삭제, mode enum 제거, test tree 정리 후 core include/src/tests no-hit 0 확인 |
| S4-17 | route bridge와 raw helper 제거 | header, source, build, test와 symbol no-hit | 완료 | bridge source·선언·export·test 제거, symbol no-hit(`contract_public_surface`의 removed-identifiers 검사 + word-boundary 스캔 0 hit) |
| S4-17A | service receive·part API 제거 | channel·Spot send/request/reply·publish, Spot·Actor recv, Actor–STREAM `*_part`와 Actor join/lifecycle 전용 receive·reply symbol을 complete multipart API·Spot control batch로 대체하고 no-hit | 완료 | 제거 76 함수가 removal manifest(`removed-identifiers-10.0.0.json`)에 등재되어 surface gate가 부재를 상시 검증(PASS). 대체 표면=complete multipart receive batch+SPOT_CONTROL record(join/lifecycle 전용 recv 없음, `test_mesh_peer_admission` join case가 batch 경유 검증) |
| S4-18 | remote subscription protocol 제거 | registry, reconnect, control frame과 status no-hit | 완료 | 구 pubsub data plane(remote subject registry·reconnect 재구독·control frame) 일괄 삭제, no-hit 0. 신규 wire는 remote 구독 전파 없이 multicast를 수신 node의 local match로 fan-out |
| S4-19 | 폐기 alias와 forwarding wrapper 제거 | 폐기 이름, Core dispatch worker option과 remote subject query를 전달하는 production code no-hit | 완료 | worker pool·subject registry·dispatch handler 제거, 유지 raw reqrep은 `reqrep_internal`로 추출, alias/wrapper 0(신규 표면은 spec 이름만, surface gate 보증) |
| S4-20 | polling, status와 monitoring 구현 | reviewed S1 정식 spec의 source kind, event와 query test 통과 | 완료 | event matrix 완결: 미방출이던 7종(PEER_REJECTED/PEER_DRAINING/CHANNEL_CHANGED/MESSAGE_SUBMITTED/BACKPRESSURED/PROTOCOL_ERROR/CLAIM_REVOKED) 방출 구현 + `test_mesh_monitor_matrix` 6 case(mask 필터·counter mask 독립·child EBUSY·revoke) green. poller 연동 test 유지 green |
| S4-21 | errno와 result mapping 구현 | 모든 신규 API가 정해진 result를 반환 | 완료 | errno-map spec 전수 대조로 submit mapping 갭 6종(ENOBUFS/EACCES/ETERM/EDEADLK·EPERM/EOVERFLOW) 보강, `unittest_result_enum_mapping`+계약 test에서 result·errno 동시 검증 green |
| S4-22 | 제거 file과 CMake entry 정리 | include되지 않는 source와 orphan target 0개 | 완료 | CMake source 목록에서 제거 115행 정리+신규 mesh 파일 등록, orphan target 0(전체 빌드 green), `core/study/src` 폐기 코드 삭제 |
| S4-22A | owner completion infrastructure 통합 | channel dealer·service per-request callback·Spot reply drain 제거, raw DEALER/ROUTER `zlink_reply_handler_fn` 유지와 in-turn await 통과 | 완료 | in-turn await 전용 test green: application claim 보유 중 infra lane이 독립 전진해 completion 수신(`test_mesh_monitor_matrix`) |
| S4-22B | Core version·ABI metadata 갱신 | VERSION, public headers와 CMake project version은 10.0.0, SOVERSION은 10이며 Conan source에는 선택한 `10.0.0-rc.N` URL과 아직 게시하지 않은 stable `10.0.0` URL이 있음 | 완료 | VERSION·header·CMake 10.0.0, SONAME `libzlink.so.10`, conandata에 `10.0.0-rc.1`·미게시 `10.0.0` URL 추가 |
| S4-22C | 10.0.0 release note 작성 | 공개 기능, 지원 환경, package와 검증 결과 명시 | 완료 | `CHANGELOG.md` 10.0.0 RC 항목에 검증 수치(84/84·2-process 10 case·sanitizer·surface gate·C ABI smoke) 반영, stale Known gaps 절 제거 |
| S4-22D | Core RC/stable workflow 분기 구현 | `build.yml`은 `-rc.N` tag를 prerelease로, stable tag를 release로 게시한다. Conan workflow는 tag에서 RC/stable package version을 구분하고 RC remote upload를 금지하며 stable secret 부재 시 실패 | 완료 | `build.yml` prerelease 분기, `core-conan-release.yml` tag→version 파생·RC upload skip·stable secret 필수화. 실제 run 검증은 S6 |
| S4-22E | Core implementation gap 닫기 | 구현된 header·test를 S1 정식 spec의 MeshNode, Spot, Actor, router, polling, monitoring과 errno 계약에 대조하고 차이를 모두 해소 | 완료 | S1 spec 행동 절 대조로 발견한 갭 전부 해소: monitor event 7종 방출, reply-after-STOPPED ESHUTDOWN, submit errno 6종, inbound peer DISCOVERY source, release_claim rearm 오독(타 owner ready로 재신호) 수정. MIXED 도달성·peer DRAINING 상태 미사용·shutdown 시 무기한 operation은 known risk로 S5 판정 대상 |
| S4-22F | Core 정식 spec parity와 index 검증 | 한국어·영문, service index, public header, errno와 ownership 차이 0개 | 완료 | `contract_public_surface`가 한영 C block 동일성·header 폐쇄·formal identifier 존재·removed 부재·export 일치를 상시 검증(PASS, 84/84 suite 포함) |
### 8.4 Core 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-23 | unit와 contract test | 전체 통과, skip 증가 없음 | 완료 | 2026-07-17 HEAD `5857824c2`+working tree에서 `ctest --test-dir core/build -j` 84/84 통과(신규 `test_mesh_monitor_matrix`·`test_mesh_stress` 포함), skip 증가 없음 |
| S4-24 | integration과 topology test | direct, channel, multicast, reconnect와 drain 통과 | 완료 | 2026-07-17 HEAD `5857824c2`+working tree에서 2-process `test_mesh_peer_admission` 10/10(direct·channel RR·multicast·drain·reconnect 포함), 전체 topology 84/84 suite와 함께 재실행 green |
| S4-25 | callback·claim·ownership stress | close, rearm, claim leak/revoke, multipart와 reference count 오류 0건 | 완료 | `test_mesh_stress` 3 case green: ①4-producer 동시 submit/claim/release(lost wakeup 0·claim leak 0·completion exactly-once 250건) ②multicast 2-spot fanout 300×3-part refcount 무결 ③ready handler 200회 등록/해제 churn 하 무손실. ASAN clean·TSAN 신규 race 0 |
| S4-26 | sanitizer와 race 검증 | ASAN/UBSAN/TSAN 적용 범위에서 신규 오류 0건 | 완료 | 기존 ASAN/UBSAN 80/80 clean 유지 + 신규 테스트 ASAN clean. TSAN이 신규 stress에서 mesh 결함 2건 적발·수정: ①release_claim이 node mutex 해제 후 ready set 무동기 읽기(rearm 결정 lock 안으로 이동) ②mesh handle registry 정적 소멸 vs 잔여 스레드(immortal 전환). 수정 후 mesh 신규 코드 TSAN race 0. 기존 기계 3계열(part_helper/auto-HWM lock order/mailbox ypipe)은 S5 판정 대상 유지 |
| S4-27 | 대규모 peer benchmark | 별도 성능 개선 작업의 입력으로 분리 | 후속 분리 | 기존 측정 기록은 보존하되 현재 S4 gate를 위해 추가 benchmark를 실행하지 않는다 |
| S4-28 | mixed traffic 성능 검증 | 별도 성능 개선 작업의 입력으로 분리 | 후속 분리 | 기존 측정 기록은 보존하되 p99 판정과 추가 성능 측정은 현재 S4·S5 gate에서 제외한다 |
| S4-29 | install과 package consumer | 설치 header와 shared library로 clean consumer 통과 | 완료 | staging 설치+clean C11 consumer single-node round trip `C ABI SMOKE PASS (zlink 10.0.0)`, header 설치 충돌 수정 유지 |
| S4-30 | 삭제 범위 최종 no-hit | v10 plan·review record의 삭제 추적만 제외하고 source, 현재 계약·guide·internals, test, build와 package에서 제거 symbol·enumerator·macro·metadata 부재 | 완료 | word-boundary 스캔으로 core include/src/tests/CMake/packaging + doc(guide·internals·spec sample) 전 범위 no-hit 0 달성(removal manifest·부재 검증 test 제외). 이 과정에서 guide 20편·internals 12편의 stale SpotNode/bridge/dispatch-handler 서술 정리, `spot-internals.*` 삭제. bindings 확장은 S7 |

### 8.5 구현 검증 후 Core internals 확정

`internals`는 계획 단계의 예상 구조를 기록하지 않는다. S4-23부터 S4-26과 S4-29까지의 기능, 구조,
stress, sanitizer와 package 검증이 통과해 실제 구현 구조가 확정된 뒤에만 갱신한다. 갱신한 문서는 source와
구조 test에 다시 대조하며, 이 검증이 실패하면 S4 구현 단계로 돌아간다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-31 | Core internals 초기 반영 기록 | S4 구현 시점에 작성한 internals를 역사로 보존하고 최종 확정은 S5-11로 이관 | 완료 | `services-internals.{ko.md,md}`, `threading-model`, `posd-module-structure`, `architecture`, `protocol-zmp`, `stream-socket`, `thread-safety`, `connection-memory`의 S4 시점 반영 기록 보존. S5 review finding 수정 중에는 갱신하지 않고 S5 종료 검증 뒤 최종 source에 맞춘다 |
| S4-32 | Core internals 초기 검사 기록 | S4 시점의 current-state·link 검사 결과를 보존하고 최종 검사는 S5-12로 이관 | 완료 | 제거 구조 no-hit와 변경 문서 link 검사 결과는 S4 시점 증거로 보존. 최종 구현과의 일치는 S5-12에서 다시 확인 |

S4 완료 gate:

- [x] Core 기능, 삭제, 회귀, stress와 sanitizer 검증이 모두 통과한다. (84/84 suite, 2-process 10/10, stress 3/3, ASAN/UBSAN/TSAN — 2026-07-17)
- [x] 구현된 `core/include/zlink.h` 공개 계약과 Core 정식 spec의 한국어·영문이 일치한다. (`contract_public_surface` PASS)
- [x] S4 시점의 Core internals 초안과 검사 기록을 보존했으며, 최종 확정은 S5 review·종료 검증 뒤
  S5-11/12에서 수행한다.
- [x] 실패를 숨기는 sleep, retry-only workaround, raw frame과 test 전용 우회가 없다. (테스트 대기 루프는 miss-시 msleep 폴링만 사용, production 경로 무우회 — S5 I3 재검증 대상)
- [x] S5 review manifest에 필요한 revision과 전체 검증 결과가 준비되어 있다.

## 9. S5 — Core 구현 3축 독립 리뷰와 수정 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S5-01 | Core review snapshot 동결 | manifest에 commit, 전체 파일 범위·hash와 동일 prompt 기록. snapshot 생성을 위한 build·test 재실행 없음 | 완료 | iteration 1~12 manifest를 `log/s5-core-review/iteration-N/`에 보존. iteration 10 `a4e91c01d`(536d62e8…), 11 `c1c579ad1`(56a1b0c1…), 12 `7f9d3e315`(539d94ab…, prompt SHA-256 588f639b…) 각 631파일 |
| S5-02 | Codex agent Core 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 완료 | iteration 10~15 NOT CLEAN finding 병합 수정, iteration 16 `CORE REVIEW CLEAN`(세 축) |
| S5-03 | Claude Sonnet Core 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 완료 | iteration 11~16 대부분 CLEAN, iteration 16 `CORE REVIEW CLEAN`(세 축) |
| S5-04 | 정확성·누락 finding 수정 | spec과 다른 동작, 빠진 test·상태·오류를 보완 | 완료 | 초기 I1 finding 10건과 iteration 2~8 ledger의 후속분을 수정. iteration 9 병합 7건도 request transaction, completion storage·domain commit 원자성, STREAM close 2단계 commit, 10.0.0 package metadata, 제거 gate·dead code와 OOM policy 일원화로 해소 |
| S5-05 | POSD 위험 신호 목록 작성 | 각 항목의 위반 원칙과 두 설계안 기록 | 완료 | iteration 1 F-I2-01: `mesh_wire` 결합(codec·admission·ingress·transport). 대안 A(ingress/egress 파일 분할)=domain 지식 반복 vs 대안 B(결정별 깊은 모듈)=선택. finding ledger에 기록 |
| S5-06 | 의미 있는 리팩터링 수행 | 선택 이유와 호출자 복잡도 감소 근거 기록 | 완료 | 대안 B 실행: `mesh_wire_codec/admission/ingress/wire` 4모듈+`mesh_wire_internal.hpp`, 공개 표면 불변, 85/85 green 유지 |
| S5-07 | DDD event와 경계 재검토 | lifecycle, membership, dispatch, ownership과 observation 책임 정리 | 완료 | admission 상태 기계와 ingress 라우팅을 별도 모듈 소유로 분리, BACKPRESSURED 방출을 admit_record 단일 지점으로 유지, timer 수명은 mesh seam이 소유(타이머 기계는 hook만) |
| S5-08 | dead code와 file 제거 | 도달 불가능 branch, 미사용 type·helper·target·file no-hit | 완료 | F3 무의미 삼항 제거, per-node `next_claim_serial` 필드 제거, `valid_utf8_public` 중복 validator 제거. I3는 iteration 1에서 Codex CLEAN |
| S5-09 | 리뷰 종료 뒤 Core 종료 검증 | 두 reviewer의 `CORE REVIEW CLEAN` 뒤 ASAN·UBSAN·TSAN, 공개 API·제거 항목, package metadata와 package·consumer gate 통과 | 완료 | iteration 16 두 `CORE REVIEW CLEAN` 뒤 `verification.ko.md`에 전체 CTest 86/86, ASAN 7/7 report 0, TSAN 신규 Mesh operation/monitor race 0, scheduler warning 0, public surface 196·제거 identifier 0·package metadata 10.0.0/SOVERSION 10와 diff-check 통과를 기록했다. 기존 TSAN known-risk 계열은 S11 종료 검증 대상으로 유지한다. |
| S5-10 | 두 리뷰어 전체 재리뷰 | 어느 축을 수정했든 Core 전체 scope와 I1·I2·I3 전부 재검토 | 완료 | iteration 16에서 두 리뷰어가 최신 snapshot 전체 scope를 재검토하고 세 축 모두 CLEAN |
| S5-11 | 확정 Core internals 갱신 | 두 review clean과 S5-09 종료 검증 뒤 최종 source의 ROUTER 배선, mailbox·ready·claim·batch, lock·thread, timeout과 Actor·STREAM lifecycle을 `core/doc/internals/`에 반영 | 완료 | 커밋 `2128ae91c`: services-internals §4·§8 신규 구조(timeout task 소유·detach primitive·µs generation·monitor pin·무할당 scheduler) 반영, stale SPOT 참조를 MeshNode dispatch로 교체 |
| S5-12 | Core internals 확정 검사 | S5-11 문서와 최종 source·구조 test·diagram·link의 차이 0개. 문서 검사만으로 구현 전체 재리뷰를 열지 않음 | 완료 | stale 식별자(spot_sub_recv 등) no-hit, internals가 가리키는 소스 파일 전수 존재, `git diff --check` 통과. 코드 결함 미발견으로 재리뷰 미개방 |
| S5-13 | framework core-blocked 결함 수정과 POSD 재검 | BUG-1~5를 결정적 회귀로 확인하고 실제 책임 계층을 수정한 뒤 Core 전체 I1·I2·I3 재리뷰와 종료 검증 | 진행 | BUG-1 transport `connection_id`, BUG-2 논리·물리 연결 전이 직렬화와 표준 ROUTER handover, BUG-4 session별 FIFO gate, BUG-5 exact `EPROTO` monitor 승격, BUG-8 DRAINING→CLOSED event를 수정했다. stale/disconnect/reconnect/drain 반복 30/30, 별도 reconnect 50/50, backpressure shutdown 10/10을 통과했다. HELLO-before-READY, bounded reliable monitor queue와 transfer fence ordering 회귀를 추가했다. 최종 Core 전체 CTest 86/86과 두 Codex 재리뷰 `NO ISSUES`를 확인했다. 10.1.0 네 binding local package는 같은 Core runtime SHA-256 `8361fe95...f198`로 재배포했다. 2026-07-19 추가 재검에서 same-RID replacement READY가 old disconnect보다 먼저 도착하면 새 pipe를 duplicate로 거부하는 경쟁을 확인해, READY handover가 현재 `connection_id`를 선택하도록 Core 10.2.0에서 수정했다(`b8c45e23f`, origin/main push 완료). 결정적 admission 17/17과 10회 반복, ancillaries 2/2, public surface 192 exports가 통과했다. 이어 multicast publish가 HWM에 도달하면 `_out_active`를 연결 종료로 잘못 해석해 route를 제거하는 결함을 Core 10.3.0에서 수정했다(`864ec7306`, origin/main push 완료). blocking publish와 nonblocking backpressure 결정적 회귀, mandatory HWM 회귀와 unreachable accounting 회귀가 통과했다. clean `BUILD_TESTS=ON` Release build의 전체 CTest 86/86을 다시 통과했다. 이 과정에서 발견한 NuGet artifact 이름의 10.2.0 잔여를 10.3.0으로 맞췄고 public-surface contract도 포함해 통과했다(`475e3e673`, origin/main push 완료). framework E2E와 BUG-3·7 재판정, 전체 Core 재리뷰가 남았다. **2026-07-20 추가 Core 회귀**: Node 비교 계측에서 애플리케이션 송신이 `node->mutex`를 획득한 채 `wire_send_mutex`를 기다려 동기 `peers()` 조회를 정지시킬 수 있는 lock convoy를 확인했다. 무한 ROUTER backpressure에서 두 번째 송신을 대기시키고 peer 조회가 shutdown 전에 완료되는지 검증하는 회귀를 추가했으며, 두 mutex를 `std::lock`으로 함께 획득해 어느 mutex도 다른 mutex 대기 중 장기 점유하지 않도록 수정했다. `test_mesh_peer_admission` 21/21과 핵심 same-RID handover 20/20이 통과했다. 다만 수정 Core를 사용한 Node ZoneWorld G3에서도 heartbeat 정지가 남아 이 결함을 ZoneWorld 단독 원인으로 판정하지 않는다. **2026-07-20 bound-session 역방향 유실 수정**: Kotlin TicTacToe의 실제 배선처럼 source prepare reserve가 0이고 target activate 뒤 source commit 전에 역방향 push 3개가 도착하며 source queue 한도가 2인 회귀를 추가했다. 수정 전에는 wire ingress가 source binding의 `EAGAIN`을 무시해 이미 수락된 frame을 닫았고 결정적 RED가 발생했다. target이 transfer source peer로 reverse route를 유지하고, source binding이 bounded FIFO와 ingress backpressure를 소유하며 source commit이 FIFO를 비운 뒤 전이를 확정하도록 수정했다. 집중 회귀 10/10, `test_mesh_peer_admission` 21/21, Core 전체 CTest 87/87이 통과했다. 공식 `core/build`를 두 번 재빌드했고 소스보다 오래된 runtime이 없으며 `libzlink.so.10.6.0` SHA-256은 `671fc61d...2b33ffe`다. JVM 실제 sample 재검은 S9-J05에서 계속한다. **2026-07-20 native callback 경계 재분류**: C++ Bingo와 AutomaticTurnDispatch 실패를 비교 추적한 결과 Core ROUTER·pipe·mailbox의 frame 유실은 없었다. C++ `stream_session_dispatcher_t::dispatch()`가 Core I/O callback에서 애플리케이션 작업을 offload한 뒤 `task.result()`로 완료를 기다리고, 그 작업이 같은 I/O thread에 할당된 Mesh transport의 reply를 기다려 상호 대기가 발생했다. Core I/O callback은 application async 작업을 기다리지 않고 즉시 반환해야 하며, framework가 stream별 순서를 유지하는 비동기 queue를 소유해야 한다. 근거는 `/home/hep7/.cache/zlink-core-validation/cpp-bingo-request-loss-round175.log`, `/home/hep7/.cache/zlink-core-validation/bingo188b.strace.2595407`, `/home/hep7/.cache/zlink-core-validation/bingo188b.strace.2595558`, `framework/languages/cpp/e2e/AutomaticTurnDispatch/logs/20260720-080004-2592179`다. 이 항목은 Core 수정 대상에서 제외하고 언어별 framework lane이 회귀와 함께 수정한다. |

| S5-CH-01 | membership 0개 Core amendment 독립 리뷰·종료 검증 | S4-CH-01 변경을 Core 전체 I1·I2·I3로 두 reviewer가 재검토하고 `CORE REVIEW CLEAN`, 전체 CTest·sanitizer·public surface·package metadata·consumer와 internals 일치 통과 | 미착수 | S4-CH-01·S3-CH-03 선행 |

**S5-13 callback 경계 확인 뒤 Core 기준 재검(2026-07-20)**: 임시 Core 계측을 모두 제거한 뒤
공식 `core/build`의 SHA-256이 `671fc61d...2b33ffe`로 복원되고 stale source가 없음을 확인했다.
Debug 기능 suite는 benchmark를 제외한 86/86이 한 실행에서 통과했고, topology benchmark 16-peer
단독 실행은 admission 24.4ms·16/16·drain 완료, peer admission 단독 실행은 22/22가 통과했다.
최초 전체 실행은 topology benchmark가 일시적으로 180초 timeout된 뒤 같은 실행의 peer suite가 연쇄
실패했으므로 87/87 단일 실행 증거로 채택하지 않는다.

**S5-13 STREAM session→local Actor 경계 재검(2026-07-20)**: Node에서 STREAM session
request dispatch 뒤 Actor handler가 보이지 않던 현상을 framework 없이 C++ Core API로 분리했다.
`test_stream_session_actor_submit_reaches_local_mailbox`가 같은 live session binding에서
`zlink_stream_session_request_to_actor()`를 호출하고 Actor application claim의 request·reply token,
Node infrastructure claim의 동일 operation completion과 payload까지 확인한다. Debug
`test_mesh_node_basic` 14/14가 통과했으며 로그는
`/home/hep7/.cache/zlink-core-validation/test-mesh-node-basic-stream-session-local-actor-request.log`다.
Node 실제 sample은 stable Core에서 lazy Entry Spot materialization과 actor-capable primary MeshNode
선택을 수정한 뒤 observer subscription, 양 player join, remote milestone notify와 전체 PASS를 확인했다.
따라서 이 현상은 Core local actor mailbox·ready 유실이 아니라 Node의 다중 RouteMesh lifecycle·primary
선택 결함으로 재분류한다.

**S5-13 reciprocal same-RID endpoint 교체 재검(2026-07-20)**: Node Bingo에서 기존
`play2`가 종료되고 같은 RID의 replacement가 다른 endpoint를 게시한 뒤 `play1` survivor와의
`ConnectionReady` 재형성이 한 번 지연된 현상을 Core API로 분리했다.
`test_reciprocal_peer_replacement_moves_same_rid_to_new_endpoint`는 별도 프로세스의
`play1`·`play2`가 reciprocal intent로 admission된 상태에서 기존 `play2`를 종료하고, survivor의
기존 endpoint가 15초 안에 `ADMITTED`에서 제외된 뒤 intent를 제거한다. 이어 같은 RID·새 endpoint의
replacement와 expected-RID intent를 동시에 연결하고 deterministic 물리 방향 handover 뒤 양쪽 admission이
유지되는지 확인한다. Debug 단독 실행과 추가 20회 반복이 모두 통과했으며 반복 로그는
`/home/hep7/.cache/zlink-core-validation/test-mesh-peer-admission-reciprocal-new-endpoint-round190-r1.log`
부터 `...-r20.log`까지다. 따라서 현재 stable Core에서 different-endpoint same-RID reciprocal handover
유실은 재현되지 않았고, 언어 framework의 old disconnect 관측과 새 intent 제출 경계를 계속 비교한다.

**S5-13 C++ Bingo bound-session 경계 재검(2026-07-20)**: 기존 reverse FIFO 회귀가 source
readiness 뒤에 바인딩된 non-participant만 검증하던 차이를 보완했다.
`test_remote_actor_transfer_prebound_session_fence`는 framework의 일반 세션처럼 source prepare 전에
바인딩하고 readiness participant가 된 세션을 target의 현재 ActorRef로 activate 직후·source commit 전에
push한다. 기존 post-readiness 회귀와 새 pre-bound 회귀가 각각 통과했고, 새 회귀는 추가 10/10을
통과했다. 반복 로그는
`/home/hep7/.cache/zlink-core-validation/test-mesh-peer-admission-prebound-reverse-r1.log`부터
`...-r10.log`까지다. C++ Bingo에서 관찰한 STREAM RID `00000002`에서 `00000003`으로의 변경은
RouteMesh handover가 아니라 connector heartbeat timeout 뒤 재연결이었다. Core STREAM server 경로가
`$zlink.heartbeat.ping`을 제어 frame으로 처리하지 않고 Actor application dispatch로 넘겨
`handler_not_found`가 반복됐고, C++ framework의 다른 STREAM 경로는 같은 ping에 pong을 반환한다.
따라서 이 실패는 Core reverse route 유실에서 제외하고 C++ framework heartbeat 처리 대상으로
재분류했다.

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

- [x] 적용되는 종료 기준의 open Core finding이 0개다(iteration 16, 4회차 이후 기준 blocker·high·medium 0).
- [x] 두 리뷰어의 I1에 finding·evidence와 `CLEAN` 판정이 있다(iteration 16 review.ko.md).
- [x] 두 리뷰어의 I2에 finding·evidence와 `CLEAN` 판정이 있다.
- [x] 두 리뷰어의 I3에 finding·evidence와 `CLEAN` 판정이 있다.
- [x] 어느 축 수정 뒤에도 두 리뷰어가 Core 전체 scope의 I1·I2·I3를 모두 다시 검토했다(매 iteration byte 동일 prompt 전체 pass).
- [x] Codex agent 결과 마지막 줄이 `CORE REVIEW CLEAN`이다(`iteration-16/codex/review.ko.md`).
- [x] R2 결과 마지막 줄이 `CORE REVIEW CLEAN`이다(`iteration-16/claude-sonnet/review.ko.md`).
- [x] 두 clean 결과 뒤 sanitizer·공개 API·package 종료 검증이 통과했다(`iteration-16/verification.ko.md`).
- [x] S5-11/12에서 최종 Core internals를 반영하고 source와의 일치를 확인했다(커밋 `2128ae91c`, stale 참조 no-hit).

## 10. S6 — Core 10.0.0 release-candidate build와 pre-release 배포

**진행방식 변경(2026-07-18 사용자 결정)**: 이번 릴리스의 "배포"는 S11 전까지
모두 **로컬 package 배포**를 뜻한다. S6는 로컬 package 검증(local Conan
create·isolated consumer smoke·shared library/SONAME/symbol)으로 종결하고,
GitHub Actions native artifact(build.yml)와 core-conan-release(conandata
sha256)의 CI 경로 정리는 현재 완료 범위에서 제외하고 별도 후속 작업으로 분리한다. RC
tag `core/v10.0.0-rc.1`은 소스 참조로 유지한다(로컬 tarball로 sha256을 확정해
검증했고, GitHub archive tarball의 재현 안정성 판단은 S11에서 한다).


| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S6-01 | 리뷰된 version source 불변성 확인 | S5 clean commit의 VERSION, 두 public header와 CMake가 tag commit과 동일 | 완료 | `git diff 1f247af7a..f864e4325 -- core/CMakeLists.txt core/include core/src` 변경 0. VERSION 10.0.0·SOVERSION 10 불변 |
| S6-02 | 리뷰된 ABI와 Conan metadata 불변성 확인 | S5 clean commit의 SOVERSION 10과 선택한 `10.0.0-rc.N` source가 RC tag commit과 일치하며 stable `10.0.0` URL은 S11 전까지 resolve하지 않음 | 완료 | `core/v10.0.0-rc.1^{commit}=f864e4325`; `1f247af7a..tag`의 Core CMake·include·src diff 0, tag CMake project 10.0.0·SOVERSION 10을 재확인했다. `core/v10.0.0` local tag와 GitHub Release가 모두 없음을 2026-07-20 재확인했다. |
| S6-03 | 리뷰된 release note 불변성 확인 | S5 clean 뒤 공개 기능과 검증 결과 변경 없음 | 완료 | `git diff --exit-code 1f247af7a..core/v10.0.0-rc.1 -- CHANGELOG.md` 변경 0을 재확인했다. |
| S6-03A | RC/stable workflow guard 검증 | RC tag의 `prerelease=true`, `zlink/10.0.0-rc.N` create와 Conan upload skip, stable tag의 `zlink/10.0.0` create와 publish-required failure test 통과 | 완료 | build.yml `Detect release channel`이 `-rc.` → `prerelease=true`, core-conan-release.yml이 push 이벤트 RC에서 upload skip·stable에서 publish-required로 확인 |
| S6-04 | RC 전 local gate 실행 | clean build, full test, 선택한 RC source entry의 local Conan create, package, symbol과 SONAME 통과 | 완료 | 로컬 build 86/86. `git archive core/v10.0.0-rc.1` tarball(sha256 `abed0b94…`)로 `conan create zlink/10.0.0-rc.1` 성공(libzlink.so.10.0.0·SONAME 10·헤더 14). with_tls=False+WS 조합의 기존 transport 빌드 결함(`options_t::tls_hostname`, mesh scope 밖)은 별도 추적, 기본 옵션에서는 무관 |
| S6-05 | RC commit과 tag 생성 | 검증된 commit에 순번 `core/v10.0.0-rc.N` tag 생성·push. stable tag 없음 | 완료 | `core/v10.0.0-rc.1` → commit `f864e4325`(core 소스는 리뷰본 `1f247af7a`와 동일) 생성·push. stable tag 없음 |
| S6-06 | native build workflow 감시 | `.github/workflows/build.yml`이 RC tag/commit으로 성공 | 후속 분리 | 현재 내부 package 배포 범위에 포함하지 않는다. 참고: build.yml이 2026-07-17 이후 GitHub "workflow file issue"로 파싱 거부(마지막 성공 07-16 23:55)됐던 기록은 외부 배포를 다시 진행할 때 별도 검증한다. |
| S6-07 | GitHub pre-release 검증 | RC native asset을 prerelease로 게시하고 stable Conan remote publish는 실행하지 않음 | 후속 분리 | 현재 내부 package 배포 범위에서 제외한다. |
| S6-08 | RC asset 검증 | platform archive, checksum, source archive와 header 일치 | 후속 분리 | 현재 내부 package 배포 범위에서 제외한다. |
| S6-09 | shared library 검증 | filename 10.0.0, SONAME 10과 제거 symbol 부재 | 완료 | S6-04의 실제 RC Conan 산출물 `libzlink.so.10.0.0`·SONAME 10 증거와 S5 iteration 5~8의 `check_public_surface.py` 결과(196 exports exact, 제거 identifier 0)를 대조했다. tag의 CMake도 VERSION 10.0.0·SOVERSION 10으로 불변이다. |
| S6-10 | local Conan package 설치 검증 | 실제 RC tag source로 만든 isolated local `zlink/10.0.0-rc.N` consumer가 build·실행하고 stable `zlink/10.0.0`은 public remote에 없음 | 완료 | isolated consumer(conanfile.txt+CMake)가 local `zlink/10.0.0-rc.1`을 소비해 build·실행: `zlink 10.0.0` 출력, rc=0. `conan list`에 stable `zlink/10.0.0` 없음(rc.1만). conandata 임시 수정은 원복 |

S6 완료 gate:

- [x] local Conan package(`zlink/10.0.0-rc.1`)가 생성·consumer smoke 검증됐다(SONAME 10, `zlink 10.0.0`).
- [x] GitHub native artifact·conan-release CI 경로는 현재 내부 package 배포 범위에서 제외했고, 실패를 성공으로 오판하지 않았다.
- [x] RC source tarball sha256 `abed0b94…`와 tag `core/v10.0.0-rc.1`이 S7 입력으로 기록됐다.
- [x] `core/v10.0.0` stable tag와 stable GitHub Release가 존재하지 않음을 재확인했고, S6-10의 `conan list`에서 stable remote package 부재를 확인했다. 외부 배포는 현재 완료 범위에 포함하지 않는다.

## 11. S7 — bindings·framework 공통 준비 (언어 독립)

S7은 네 언어 lane(cpp·dotnet·jvm·node)이 공유하는 언어 독립 준비를 한 번
수행한다. RC artifact 동기화, 제거 wrapper·금지 구현 정책과 no-hit 목록,
공통 smoke matrix 정의가 여기서 고정되며 각 lane은 이를 입력으로만 쓴다.
언어별 bindings·framework 적용과 리뷰는 S8 lane 파이프라인에서 수행한다.
Python·Go·Rust는 이번 10.0.0 적용을 보류하므로 이 준비에서 대응하지 않는다
(코드는 삭제하지 않는다). C ABI는 별도 언어가 아니라 cpp lane의 선행 검증
(C header·shared library smoke)으로 포함한다.

### 11.1 공통 적용

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-00 | RC tag parser와 runtime version 분리 | local-package script가 `core/v10.0.0-rc.N` asset tag는 그대로 사용하고 C/header runtime version은 숫자 `10.0.0`으로 기록하며 version macro에 `-rc.N` suffix를 남기지 않음 | 완료 | `release-tag.sh`가 stable·RC tag와 GitHub URL을 공통 해석해 asset tag와 숫자 runtime version을 분리한다. `update-zlink-libs.sh`와 `fetch-release-binaries.sh`가 이 parser를 사용하며 RC suffix를 header version에 전달하지 않는다. |
| S7-00A | RC fixture·provenance test | RC/stable tag fixture가 version marker, source SHA, checksum과 asset URL을 검증하고 malformed tag·suffix 잔존·checksum 불일치에서 실패 | 완료 | `test-release-contract.sh`: stable 1·RC 1 양성 fixture와 malformed tag 4·header suffix·checksum·source SHA·asset URL 불일치 8개 음성 fixture 통과. `verify-release-provenance.sh`를 실제 release fetch에 연결했고, `build.yml`이 source SHA·checksum/source archive SHA-256·asset URL manifest를 게시한다. bash syntax, workflow YAML parse, diff-check 통과. |
| S7-01 | Core RC artifact 동기화 | update-zlink-libs script가 Core asset의 runtime version, source SHA와 checksum을 검증하고 복사 | 완료 | 기존 10.0.0 RC 동기화에 이어 Core minor를 선택해 네 binding에 동기화할 수 있도록 `sync-local-core-libs.sh`에 언어 선택 인자를 추가했다(`ca6be59d8`). 최신 Core 10.3.0 runtime SHA-256 `6b8b0bf2…07ed4`를 네 binding base 10.3.0에 동기화했다. C++ install의 `libzlink.so.10.3.0`, .NET 10.3.1 package와 Node 10.3.1 package의 native payload가 이 hash와 일치한다 |
| S7-02 | binding API inventory 작성 | 적용 대상 C ABI·C++·.NET·Java/Kotlin·Node 전체 대응. Python·Go·Rust는 보류 대상으로 표시만 하고 갱신하지 않음 | 완료 | 현재 bindings는 9.0.4 API(cpp CMake VERSION 9.0.4). 제거 대상 hit(전환 전 기준): SpotNode 95파일·spot_node 42·bridge 29·dispatch_worker 10, selectNode 0. lane 소스 규모: cpp 76·dotnet 252·java 283·node 253. Python/Go/Rust 보류 |
| S7-03 | 제거 wrapper와 generated API 정책·no-hit 목록 | SpotNode mode, bridge, Core dispatch worker option, remote subject query, Spot·Actor–STREAM service `*_part`, Actor join/lifecycle 전용 receive·reply, channel-dealer event와 old alias no-hit 검색 문자열 고정. 모든 raw socket용 channel metadata wrapper 유지 기준. 실제 적용은 각 lane | 완료 | 검색 문자열 고정: `SpotNode`·`spot_node`·`bridge/RouteBridge`·`dispatch_worker/DispatchWorker`·`selectNode/selectOne/selectMany`·`*_part`(Spot/Actor–STREAM). 전환 전 hit는 S7-02 기록. 각 lane이 전환 후 no-hit 달성을 bindings clean 조건으로 검증 |
| S7-CH-01 | membership 0개 MeshNode 네 bindings 검증 | C++·.NET·Java/Kotlin·Node가 channel 등록 없이 MeshNode를 생성·시작하고 Node direct·peer·shutdown을 사용하는 public binding test 통과. binding 내부 validation이 막으면 동일 Core version의 binding patch가 아닌 Core minor 증가 후 patch 0 규칙으로 네 package를 맞춤 | 완료 | 시작 기준 `86258cb9a3ec`. 네 binding test가 `addChannel*` 없이 start→READY·channel count 0, missing peer intent·peer snapshot, Node direct `NOT_CONNECTED`, shutdown→STOPPED를 기존 공개 API로 검증한다. C++ 별도 build의 `test_cpp_contract_options` 1/1, .NET isolated artifact의 `test_mesh_node_publish_contract` 4/4, Java `MeshNodeRoutingIdLifecycleTest` 2/2와 공유 Java binding을 소비하는 Kotlin `:kotlin-samples:compileKotlin`, Node `mesh_node_routing_id` 3/3이 통과했다. C++·Node `ldd`, Java·.NET `ZLINK_LIBRARY_PATH`가 별도 Core runtime SHA-256 `2a91ea82…a7fb`를 사용함을 확인했다. binding 내부 validation 수정과 공개 API 추가는 필요 없었고 C++·.NET의 오래된 channel 필수 공개 주석만 현재 계약에 맞췄다. version·package·framework 변경과 package 배포 없음. scoped `git diff --check` 통과 |
| S7-07 | bindings release workflow 수정 | Core native version과 binding package version을 별도로 입력·검증하고 release asset의 source SHA·checksum provenance를 보존. 네 언어 공통 workflow 골격 | 완료 | `e6812889a`: 두 version 분리, exact Core version·tag source·checksums 검증, Node prebuild와 C++ Core-version contract, .NET 정식 native 입력 경로를 release gate에 연결. YAML policy·shell·Node syntax와 C++ contract 1/1 통과 |
| S7-08 | `.NET` native 입력 경로 통일 | workflow와 pack이 `bindings/dotnet/native/<rid>/`만 source 입력으로 사용 | 완료 | `Zlink.csproj`의 pack 입력과 `bindings-release.yml`의 build·test·pack 입력이 `bindings/dotnet/native/<rid>/`를 사용한다. `framework-dotnet.yml` path filter에 남은 구 `bindings/dotnet/runtimes/**`를 `bindings/dotnet/native/**`로 교정했으며, 현재 workflow·script·pack metadata의 구 source 경로 no-hit를 확인했다. |
| S7-SMOKE | 공통 smoke matrix 정의 | node/channel/Spot direct send/request와 Logical Multicast metadata snapshot·malformed·1024 경계·relay·reply 비자동복사, ROUTER backpressure·부분 전달, batch reset/retain과 shutdown을 각 lane이 실행할 공통 scenario로 고정 | 완료 | 공통 scenario를 spec(core/doc/spec/core/service)과 framework E2E inventory에서 확정. publish 전용 NODROP option은 matrix에서 제거. 각 lane의 S8-*-V·S8-SMOKE에서 실행 |

S7 완료 gate:

- [x] RC artifact 동기화 도구의 provenance test가 통과한다. 실제 최종 RC artifact 동기화는 S11 release gate에서 다시 검증한다.
- [ ] 제거 wrapper 정책·no-hit 검색 문자열과 공통 smoke matrix가 네 lane 입력으로 고정됐다.
- [ ] Python·Go·Rust는 보류로 표시되고 코드가 삭제되지 않았다.

### 11.2 S8 lane 파이프라인 (cpp·dotnet·jvm·node 병렬)

S7 준비가 끝나면 네 언어 lane을 동시에 시작한다. 각 lane은 자기 언어 안에서
다음 순서를 지키고, lane 사이에는 선행 조건이 없다.

1. **bindings 적용**: MeshNode API·claim/batch 수명·ownership/error mapping·
   source/package snapshot 갱신. cpp lane은 C ABI consumer(header·shared
   library·C smoke)를 선행 검증으로 포함한다.
2. **bindings 리뷰**: Codex·Sonnet 3축 독립 리뷰(§2 절차) 반복 →
   `BINDINGS REVIEW CLEAN`. 리뷰 clean 뒤 local package·consumer·공통 smoke
   검증.
3. **framework 적용**: 그 언어의 framework(공통·server 계약과 exact
   interface)·sample·E2E 전환. dotnet은 §12(구 S8) 상세 항목, cpp·jvm·node는
   §13(구 S9) 항목을 lane 내부 단계로 사용한다.
4. **framework 리뷰**: Codex·Sonnet 3축 독립 리뷰 반복 → 언어별 clean 문구
   (`CPP REVIEW CLEAN`·`DOTNET REVIEW CLEAN`·`JVM REVIEW CLEAN`·
   `NODE REVIEW CLEAN`). 리뷰 clean 뒤 package·consumer 검증과 언어별
   internals 확정.

| lane | bindings 대상 | bindings clean | framework 대상 | framework clean | 상태 |
|---|---|---|---|---|---|
| **S8-CPP** | C ABI + C++ bindings | `BINDINGS REVIEW CLEAN` | C++ framework (§13.2) | `CPP REVIEW CLEAN` | bindings 전환 착수 |
| **S8-DN** | .NET bindings | `BINDINGS REVIEW CLEAN` | .NET framework (§12) | `DOTNET REVIEW CLEAN` | 미착수 |
| **S8-JVM** | Java/Kotlin bindings | `BINDINGS REVIEW CLEAN` | Java/Kotlin framework (§13.3) | `JVM REVIEW CLEAN` | 미착수 |
| **S8-NODE** | Node.js bindings | `BINDINGS REVIEW CLEAN` | Node.js framework (§13.4) | `NODE REVIEW CLEAN` | 미착수 |

### 11.2.0 lane별 bindings 전환 규모 (2026-07-18 정량화)

4개 lane 모두 Service 레이어(SpotNode·bridge·spot-part 제거)의 대규모
전환이 필요하다. 각 lane 소스 규모와 구 API hit:

| lane | 소스 파일 | SpotNode hit | bridge hit | 비고 |
|---|---:|---:|---:|---|
| cpp | 164 | 95 | 29 | Service 5549줄, 1036 컴파일 에러(§11.2.1) |
| dotnet | 252 | 61 | 11 | |
| jvm(java+kotlin) | 283+ | 조사 예정 | 조사 예정 | java 283 소스 |
| node | 253 | 조사 예정 | 조사 예정 | |

각 lane은 Runtime/raw-socket 레이어는 유지하고 Service 레이어를 Core
10.0.0 dispatch/claim/batch/MeshNode API로 재작성한다. lane 순서: cpp(최소)
→ dotnet → jvm → node. 병렬 가능하나 세션 단위로 lane별 완결 진행한다.

### 11.2.1 S8-CPP lane bindings 전환 범위 (2026-07-18 정량화)

10.0.0 core 헤더/lib로 cpp bindings(164 소스)를 빌드해 전환 범위를 확정했다.

- **Runtime/Sockets·Core·Messaging·Options·Eventing**: 10.0.0과 호환. 유일
  예외는 `message.cpp`의 `zlink_msg_gets`(제거된 per-message metadata API) 1건.
- **Runtime/Service 전면 재작성 필요** (총 1036 컴파일 에러, 전부 이 레이어):
  - `spot_node.cpp`(89), `spot_route_bridge.cpp`(16): 구 SpotNode·route bridge
    API가 10.0.0에서 삭제됨 → **파일 삭제 대상**.
  - `spot.cpp`(23)·`spot_send.cpp`(16)·`spot_receive.cpp`(9)·`spot_publish`:
    구 `zlink_spot_*_spot_part`·`zlink_spot_publish_part`를 MeshNode/Spot
    dispatch API(`zlink_spot_send_to_channel/to_spot`·`zlink_spot_publish`·
    claim/receive batch)로 재작성.
  - `actor.cpp`(5)·`actor_operations.cpp`(18): 구 `zlink_spot_node_actor_*`를
    `zlink_mesh_node_actor_*`(new/lookup/join/leave/transfer)로 재작성.
  - `request_reply.cpp`·`reply_operations.cpp`·`stream.cpp`: 구
    `zlink_stream_bind_actor`·`zlink_*_reply_spot_part`를 STREAM session
    service·`zlink_mesh_reply`로 재작성.
- 제거 심볼 no-hit 대상(bindings clean 조건): `zlink_spot_node_*`,
  `zlink_*_spot_part`, `zlink_spot_route_bridge_*`, `zlink_stream_bind_actor`,
  `zlink_spot_publish_part`.

계약 기준: Core Service spec(`core/doc/spec/core/service/01-mesh-node~05`)과
C++ exact interface(`framework/doc/framework/spec/server/languages/cpp/`).
Runtime 레이어는 유지, Service 레이어를 spec 기준으로 재작성한 뒤 bindings
리뷰 campaign(Codex·Sonnet)을 연다.

각 lane의 bindings 단계와 framework 단계는 별도 review campaign이고 별도
finding ledger를 가진다. lane이 bindings clean에 도달하면 다른 lane을
기다리지 않고 그 lane의 framework 적용을 시작한다. 네 lane이 모두 framework
clean에 도달하면 S11 최종 통합 리뷰로 넘어간다.

### 11.3 lane별 bindings 검증 참조 (구 언어별 항목)

아래는 각 lane의 bindings 리뷰 clean 뒤 실행하는 언어별 package·consumer
검증이다. 보류한 Python·Go·Rust 항목은 제거했다.

| ID | 범위 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-CPP-V | C ABI + C++ | public header·shared library·C smoke와 C++ contract·package consumer·E2E smoke 통과 | 미착수 | - |
| S8-DN-V | .NET | contract, NuGet consumer와 E2E smoke 통과 | 미착수 | - |
| S8-JVM-V | Java/Kotlin | contract, package consumer와 E2E smoke 통과 | 미착수 | - |
| S8-NODE-V | Node.js | contract, npm consumer와 E2E smoke 통과 | 미착수 | - |
| S8-SMOKE | 공통 smoke matrix | 네 lane이 S7-SMOKE 정의를 각 언어에서 실행하고 통과 | 미착수 | - |

### 11.3 bindings 독립 리뷰와 내부 package 배포 전 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-15 | Codex agent bindings 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S7-16 | Claude Sonnet bindings 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S7-17 | finding 일괄 수정과 일반 검증 | finding을 한 번에 수정하고 일반 build와 bindings 전체 테스트 통과 | 진행 | **bindings stale-test 정리(2026-07-20)**: .NET의 제거된 `SubjectKind`·`SpotRole`·`ISpotNode`·`CreateSpotNode`·callback dispatch·`DetachStream`·message property 테스트를 현재 MeshNode pull-dispatch와 raw socket 계약으로 이관하거나 폐기했다. 이 과정에서 Spot·MeshNode publisher의 필수 topic 검증 누락을 보완하고 Core가 연결별 RID를 할당하는 raw STREAM에서 자체 routing-id public method를 제거했다. `dotnet test bindings/dotnet/tests/Zlink.Tests/Zlink.Tests.csproj` 140/140 통과. Node `spot_dispatch_drain.test.ts`는 `createMeshNode`와 ready batch/claim/receive batch로 이관했고 `npm run build` 및 focused 4/4가 통과했다. 다른 bindings 전체 gate와 독립 재리뷰는 아직 남아 있다. |
| S7-18 | 두 리뷰어 전체 재리뷰 반복 | 세 축과 두 stage 결과가 모두 `CLEAN`; 둘 다 `BINDINGS REVIEW CLEAN` | 미착수 | - |
| S7-19 | 리뷰 종료 뒤 local package 묶음 검증 | 두 `BINDINGS REVIEW CLEAN` 뒤 publish-all-wsl 및 별도 언어 package·consumer 검증 통과 | 미착수 | - |
| S7-20 | 리뷰 종료 뒤 배포 없는 workflow 검증 | 두 `BINDINGS REVIEW CLEAN` 뒤 create_release=false, publish_registry=false로 전체 job 성공 | 미착수 | - |
| S7-21 | 언어별 내부 package 배포 입력 준비 | local package 경로, version, checksum, 설치·smoke 명령과 이전 pin 복구 절차 기록 | 진행 | 2026-07-20 사용자 결정: 이번 완료 범위는 bindings 내부 package 배포이며 GitHub Release와 외부 registry 배포는 수행하지 않는다. `scripts/local-package/README.ko.md`가 `.artifacts/wsl`·`.artifacts/windows`의 NuGet·Maven·npm·C++ install 경로와 언어별 build·소비 명령을 소유한다. 최종 10.7.0 package checksum과 이전 pin 복구 값은 S7-22에서 동결한다. |
| S7-22 | framework pin 입력 기록 | 검증된 local package version, checksum과 경로 확보 | 진행 | 10.1.0 WSL package 경로와 checksum을 `route-mesh-10.0.0-core-blocked-bug-report.ko.md` BUG-9에 기록했다. 네 package의 Linux x86-64 Core runtime은 `core/build/lib/libzlink.so.10.1.0` SHA-256 `8361fe95...f198`와 일치한다. framework 중앙 pin과 E2E 판정은 담당 작업자가 수행한다. |

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
- [ ] 적용되는 종료 기준의 open bindings finding이 0개다. 1~3회차는 전체 severity, 4회차부터는
  blocker·high·medium을 기준으로 한다.
- [ ] 두 리뷰어의 I1·I2·I3 각각에 finding 또는 `없음`, evidence와 `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 bindings 전체 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] Codex agent와 R2 결과가 모두 `BINDINGS REVIEW CLEAN`이다.
- [ ] 외부 immutable 10.0.0 package는 아직 공개하지 않았다.

## 12. S8-DN lane 상세 — .NET (bindings clean 뒤 framework 단계)

S8은 S9의 C++·JVM·Node.js lane과 동시에 시작한다. `.NET` 구현은 다른 언어의 선행 기준 구현이
아니며, 공통 framework spec과 `.NET` exact interface를 계약 기준으로 사용한다. 다른 lane과 비교가
필요하면 관찰 가능한 동작, sample·E2E scenario와 검증 방법만 참고하고 내부 구조나 `.NET` 전용 public
API를 다른 언어의 구현 기준으로 사용하지 않는다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-01 | 검증된 bindings local package pin을 10.0.0으로 갱신 | 중앙 version과 lock·restore 결과 일치 | 진행 | Core blocker 수정 package를 재검하기 위해 `Directory.Packages.props`와 ZoneWorld 별도 중앙 props를 `10.1.0`으로 올렸고 restore·E2E가 이 package를 사용한다. 정식 10.0.0 동결 조건과 10.1.0 native payload 일치는 S8-11·BUG-9에서 다시 확인한다. |
| S8-02 | AddRouteMesh·ChannelName 구현 | 복수 logical membership과 정식 `.NET` interface가 source snapshot과 일치 | 완료 | AddRouteMesh(meshName):IZLinkMeshNodeBuilder + ChannelName:IZLinkMeshChannelBuilder (spec 05 exact-interface), 구 AddSpotMesh/SpotNodeBuilder 제거 no-hit 0, build 0/0 |
| S8-02A | RouteMesh runtime-options DI 구현 | 기존 `IZLinkChannelRuntimeOptions` 제거, `IZLinkRouteMeshRuntimeOptions` singleton 등록, MeshNode socket setter의 startup-only 오류와 runtime channel Weight 반영 통과 | 완료 | IZLinkRouteMeshRuntimeOptions singleton DI, 구 IZLinkChannelRuntimeOptions 제거, MeshNode socket setter startup-only + runtime Channel Weight 반영 |
| S8-03 | MeshNode-owned handler·Spot·Actor 등록 구현 | channel·route handler context와 모든 Spot·Actor builder 멤버 보존 | 완료 | NodeSend/NodeRequest→route handler(ZLinkRouteHandlerInvoker), ChannelSend/ChannelRequest→channel-membership handler(ChannelName keyed), reply token 경유. Spot/Actor builder 멤버 보존, DI 스캔 |
| S8-04 | location descriptor와 connection planner 구현 | Redis 자동 discovery와 manual `IZLinkMeshPeerConnections`가 같은 admission을 사용하고 MeshName 범위, expected RID pin, lifecycle generation, descriptor revision, source 병합과 ready index 검증 | 완료 | **ZLinkPeerLocation→ZLinkMeshNodeDescriptor migration 완료**(2026-07-18): 06-location-store exact 계약(descriptor/spot/actor row·store 5-role·change stamp ulong·watch internal화), (LifecycleGeneration,DescriptorRevision)/(SpotGeneration)/(actorGen,MembershipEpoch) 단조 guard, planner/reconciler descriptor 전환(draining=신규선택 제외·revision 단조·claim 후 generation stamp renew), route store·ZLinkActorSessionRouteLifecycle·ZLinkPeerCapabilities 제거(40 §2.3), spot pub/sub plane=`<mesh>#pub` namespace 분리, 진단표면 MeshNode-only 재편, Redis 3-kind(tag `mesh`) Lua/json codec 정렬(41 §2 writer-json 보존). Framework+AspNetCore+Redis build 0/0, UnitTests 651/657(실패 6=기존 doc-regression 추적분). Redis.Tests migration 완료(로컬 Redis 실측 36/36 green, cross-language 2건은 하네스 env skip) + pinned fixture(mesh-node-descriptor-v1/actor-location-v2) byte-for-byte 검증 green. 잔여(타 행 이관)=ContractTests migration(S8-02 builder 제거로 인한 선행 파손, S8-DN-V)·multi-source merge E2E 검증(S8-09/10)·OwnerNodeGeneration join(리뷰 단계) — gap 90 §12.33 |
| S8-04A | Redis Actor transfer authority 구현 | participant-set CAS, transfer token, lease, prepared/commit/abort crash recovery, unsupported store startup failure와 distributed transfer E2E 통과 | 완료 | IZLinkActorTransferStore(prepare/commit/activate/abort/takeover/resolve, participant-set CAS·transfer id·recovery lease) — in-memory state machine + Redis Lua per-transition. coordinator cross-node 호출+distributed E2E는 build-only 미실행. commit actor-row rewrite는 actor-row-shape gap 90 §12.27 결합(코드에 기록) |
| S8-04B | Redis production 기본 정책 구현 | Redis extension 명시 등록, 자동 discovery·분산 Spot/Actor 주소 조회를 사용하면서 location store를 등록하지 않은 구성의 startup failure, 사용자 store capability와 test-only in-memory 경계 검증 | 완료 | production fail-fast(distributed Spot/Actor/transfer without store → ValidateSpotNode 실패, auto-discovery는 store 없이 선택 불가로 구조적 차단) + UseInMemoryLocationStores internal/test-only 경계 검증 |
| S8-05 | channel/direct/Spot/Actor 전송 연결 | bindings MeshNode public API만 사용 | 완료 | compile-green 전환(Option B): 프레임워크-소유 dispatch record가 MeshNode public API만 사용해 channel/direct/Spot/Actor 전송 배선 |
| S8-06 | ready/claim pump 구현 | infrastructure 우선 drain, Node·Spot·Actor keyed scheduling과 claim leak 0건 | 완료 | DrainReady pump seam이 record를 per-owner serial-executor 큐로 fan, claim은 finally 해제(leak 0). MeshOperationId↔Completion 콜백 테이블 |
| S8-06A | S/S metadata 연결 | Node·Channel·Spot direct send/request와 Logical Multicast의 mutation snapshot, immutable handler view, malformed ingress, 1024 경계, relay allowlist, reply 비자동복사와 일반 reply metadata 미지원 통과 | 완료 | `IZLinkMetadataCall<TSelf>`와 call 10종이 `ZLinkCallMetadata`의 last-write-wins snapshot을 사용한다. Channel·Node·Spot send/request와 Logical Multicast는 metadata를 backend seam까지 관통하고 send/publish `TrySubmit()`은 one-shot DONTWAIT admission 결과를 반환한다. node-direct send/request의 target·DONTWAIT·canonical metadata 관통, last-write-wins, 1024-byte 경계와 malformed frame을 focused UnitTests 5/5로 확인했다. Spot generation과 publish fan-out detail 회귀도 기존 focused test가 보존한다. 해소된 gap §12.36은 현재 gap 문서에서 제거했다. |
| S8-06B | Spot timer 연결 | `Task.Delay` 기반 tick이 lifecycle generation과 cancel 규칙을 거쳐 keyed scheduler에 제출되고 Core timer FFI를 사용하지 않음 | 완료 | Task.Delay tick이 per-owner keyed serial executor 경유, stop-token/generation gated, Core timer FFI 미사용 — 검증만(변경 불필요) |
| S8-07 | Logical Multicast publish 옵션 제거 | Core·bindings·framework에 publish 전용 NoDrop 표면이 없고 기존 ROUTER backpressure만 사용 | 완료 | **독립 current-tree 재리뷰(2026-07-20)**: 정식 Core `01-mesh-node` §7·`03-spot` §6과 framework `05-framework-api` §7·`20-spot-messaging` §4.1을 먼저 대조했다. 제거 식별자 6종(`ZLINK_MESH_PUBLISH_OPT_NODROP`, `zlink_mesh_publish_option_t`, Mesh publisher·Spot publish option getter/setter)은 Core, C/C++/.NET/Java/Node bindings와 C++/.NET/Java/Node framework source에서 scoped no-hit이고, framework 구현의 `NoDrop` 철자 변형도 no-hit다. Core `contract_public_surface` 1/1, raw PUB/XPUB `test_xpub_nodrop`·`unittest_typed_option` 2/2, C header mirror 3/3, C++ binding contract 1/1, .NET binding build 0 warning/0 error, Java binding `compileJava testClasses`, Node binding build와 raw XPUB test 1/1이 통과했다. Framework는 C++ build+contract 1/1, .NET build 0/0+publisher-config contract 1/1, Java core classes, Node 전체 build가 통과했다. Logical Multicast remote leg는 `wire_publish_remote()`가 target별로 MeshNode `router_socket`의 `send_application_data_message()`를 호출해 HWM·timeout·`DONTWAIT` 결과를 사용하며 별도 PUB/XPUB socket을 만들지 않는다. raw socket의 `ZLINK_PUB_OPT_NODROP`과 C++ `no_drop`, .NET/Java/Node `NoDrop`/`noDrop`은 별도 PUB/XPUB 계약으로 유지됨을 source 목록과 raw focused test로 확인했다. |
| S8-08 | 기존 topology API와 runtime 제거 | v10 plan·review record만 제외하고 builder, registration, production `UseInMemoryLocationStores()`, bridge, Spot·Actor–STREAM service part와 Actor join/lifecycle 전용 wrapper, test와 현재 docs no-hit | 완료 | 기존 builder·production in-memory store 선택 표면·bridge를 제거한 뒤, 내부에 남아 있던 route channel/client-server registration·bundle·receive pump·auto-connect와 별도 Spot pub/sub plane도 제거했다. Spot의 ChannelName 호출은 현재 MeshNode outbound를 직접 사용하며 metadata를 함께 전달한다. 중복 `SpotMeshChannels` registry를 없애 MeshNode registration 하나가 MeshName과 peer lifecycle을 소유한다. 금지 surface는 .NET source·test·sample·E2E·현재 guide/internals에서 scoped no-hit다. UnitTests 프로젝트 build 0 warning/0 error, topology·location focused 29/29, metadata/fanout focused 6/6, ContractTests 43/43, non-documentation UnitTests 613/613을 확인했다. 현재 MeshNode channel/RID request의 flow-event exact test 3건과 G0 ledger proof gate 1/1도 통과했다. |
| S8-09 | `.NET` sample 전환 | 분산 sample은 공식 Redis extension을 등록하고 지정된 manual sample은 `IZLinkMeshPeerConnections`를 사용하며 S2 inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 완료 | **compile 이행 완료**(2026-07-18): 8개 sample + testapps 전부 AddRouteMesh/Listen/PeerConnections + TrySubmit 표면, EnablePubSub plane 제거(logical multicast), TicTacToe manual peer=PeerConnections.Connect(rid,ep), build 0/0. ChannelName membership 필수(validator)로 전 mesh 등록에 기본 membership 추가, 기본 ZLinkBindingsPackageVersion 9.0.8→10.0.0, 러너의 pub/sub wait 제거. **런타임 근본수정 체인 완료(2026-07-19, f69014b4b) — TicTacToe 클라 시나리오 cross-node 전 구간 통과**: (1) bind hang 근본원인=pump의 DrainReady/claim.Receive 기본 blocking(첫 claim 안에서 pump 스레드 park→전 owner 기아)→DontWait 전환+poison record 생존, (2) binding ActorInterop.FromNative가 빈 NodeRid/ActorId 레코드 전면 거부→허용, (3) completion terminal 값을 SubmitResult로 오캐스팅(Conflict→InternalError)→zlink_request_result_t 직매핑, (4) BindActor NotConnected 레이스(코어 세션 liveness=비동기 observer)→timeout 내 재시도, (5) **location row 생성값 도메인 교정**: row는 core generation 원문(spot row=core spot lifecycle generation, claim-then-stamp 제거, store renew guard=owner-only, 41 §3.1 store gen≠row generation)—cross-node SpotRequest CONFLICT/ESTALE 해소, (6) actor-owner ready record의 spot 귀속(ActorLookup)+ownerActor 스레딩, (7) **cross-node 세션 relay plane 신설**(spec 31 §6): bound-session push→세션 노드(`$zlink.session.push-relay.v1`), 이주 액터로의 세션 frame→액터 노드(`$zlink.actor.frame-relay.v1`), no-bind reply는 pump가 등록한 core reply token으로 회신, remote join commit이 세션 node rid를 구체값으로 전달, source-side 이주 정리가 세션측 바인딩 레지스트리 보존, (8) spot pub/sub publish 채널=mesh channel(topic=filter)로 교정(채널=topic placeholder는 원격 불가). UnitTests 651/657(6=doc-regression 추적분) 유지. **TicTacToe run_sample.sh 전 구간 통과(exit 0, 3c051db71)**: 재입장 admission 크래시 근본원인=ToActorJoinRequest가 zero-filled SourceActor 사용→control payload의 CurrentActor로 교정. **원격 액터 세션 바인딩 일반화**(Bingo 유형: 세션 호스트가 타 노드 소유 액터를 인증 시 바인드): native bind는 로컬 액터 전용이므로 remote ref는 native bind 생략+frame-relay plane으로 inbound 상대(재시도 포함), bound-session coordinator 노드 폴백=router-capable 노드(팩토리 없는 세션 호스트). EnableActorDispatch를 Bingo/SupportChat/DeliveryDispatch/ZoneWorld-Gateway/GameQuest stream node에 반영. **샘플 진행(2026-07-19, ~4514b0f02)**: TicTacToe·Bingo·SupportChat·DeliveryDispatch `run_sample.sh` 통과 실측(각 exit 0). 추가 근본수정: (9) publish/route-spot `TrySubmit`=one-shot DontWait 실구현(§12.36 stub 해제, route-channel fallback만 stub 유지), (10) 세션 식별 없는 frame이 구체 세션 바인딩을 clobber하지 않도록 EnterDispatch 가드, (11) observed-generation 가드에 liveness 선행(사망 owner row는 세대 floor 미기록)+확정 miss 시 actor floor forget→재생성 노드의 신선 row resolve 복구(ZoneWorld replacement 기동 통과), (12) discovery로 다이얼되는 채널 서버는 구체 routing id 필수(descriptor row가 (MeshName,Rid) 키)—SupportChat/ZoneWorld 서버 채널에 rid(ZW는 할당 그룹) 부여, (13) ConfirmRemoteBinding retriable 재시도, (14) 세션 호스트(팩토리 없음)용 router-capable 노드 폴백. **ZoneWorld**: A/G/E 시나리오군 통과; (15) 원격 push 배달의 backpressure 재시도(1건 드롭이 ZW-B1 대기 무산—Delivered/Backpressured/Stale 3상, stale만 드롭) 반영 후에도 ZW-B1 잔존 — MoveMsg는 frame-relay로 정상 dispatch됨을 확인, 상태 notify에 이동 반영이 안 되는 층(스팟 tick vs move 적용) 클라 payload 계측 필요. C1·D1/D2(ops notify·announce fanout)도 잔여. **GameQuest**: 러너 stale spot-pub 포트 probe 2건 제거 후 시나리오 후반 request timeout 1건 잔여(quest notify는 흐름 확인). **주의**: TicTacToe/DD 재검은 병렬 perf 부하(load ~8)에서 flake — idle에서 재확인 필요. **ZW 원인 축소(2026-07-19)**: publish detail 계측으로 zone-node 간 cross-node 전량 실패의 공통 원인= zn1↔zn2 mesh admission 부재 확정(SnapshotRemoteTargets=0; gw01↔zn1/zn2는 admitted, 같은 Redis rows 사용). descriptor rows(zn1/zn2/gw01)는 정상 발행 확인(런 중 Redis 실사). planner pairwise-initiator(낮은 hex rid가 dial: zn1→zn2, gw01→both)와 rows는 정합 — 다음 계측 지점=zn1의 desired set/executor ConnectPeer 실행 여부·wire admission(HELLO) 실패 여부(ZLinkAutoConnectLoop/Executor에 debug 지점 필요). **ZW 원인 재축소(e04358939)**: admission은 정상(늦은 tick publish가 Remote 2/2 — 초기 0은 기동 타이밍이었음, autoconnect dial 계측으로 zn1→zn2 dial 확인). push 유실 2종 근본수정: backpressure 재시도 + rebind release→bind 갭(NoBinding)은 재시도·다른 세션(WrongSession)만 드롭 — B1 런은 배달 무손실 확정. **그럼에도 B1 잔존**: MoveMsg dispatch까지 확인(프레임 relay·핸들러 오류 없음), ZoneStateNotify payload에 이동 반영이 안 되는 층이 남음 → 다음=클라이언트 수신 payload 덤프(GameClient에 env-gate 덤프 추가)로 predicate 불일치 내용 확인. **수락 기준 통과(2026-07-19, 078899263)**: `samples/run_samples.sh` 전체 스윕 exit 0(TicTacToe·Bingo·SupportChat·ShoppingMall·DeliveryDispatch·GameQuest) + ZoneWorld 자체 러너 `zoneworld=completed`(A/B/C/D/E/G 전 시나리오군). ZW 마감 근본수정 3건: (16) actor takeover claim이 기존 row의 MembershipEpoch를 계승(epoch 리셋이 전이 후 row를 lagging으로 영구 은닉→세션 relay가 구 노드에 고착), (17) observed actor 축=epoch-major(generation은 노드별 카운터라 cross-node 전이에서 재시작)+relay resolve가 transfer-commit 창을 재시도, (18) 신규 공개 옵션 `ZLinkLocationOptions.ObservedMeshNames`(관찰자 호스트의 mesh 열거 확장, api snapshot 재생성)·fanout publisher rid 광고. GameQuest 마감: identity-less 세션 reply는 현재 바인딩으로 송신(빈 native token 게이트 제거). ZW-B1 배달 무손실 검증 프로브(ZONEWORLD_DEBUG_INBOUND) 포함. env-gate 계측(ZLINK_DEBUG_PUMP/ZONEWORLD_DEBUG_INBOUND) 제거는 S8-12A 이전 |
| S8-10 | `.NET` framework E2E 전환 | `e2e/run_e2e_all.sh`의 전체 config 통과 | 진행 | **compile 이행 완료**(2026-07-18): 26개 e2e 프로젝트 build 0/0 — 호스트 AddRouteMesh 이행, weight admin=IZLinkRouteMeshRuntimeOptions.Channel(mesh,channel).Weight(provider 채널→mesh membership 전환: Resilience/StoreFailure/RuntimeMonitoring), 운영 표면 descriptor 조회(ListMeshNodeDescriptorsAsync), store 장식자(Delayable/PollingOnly/CleanupGated) 새 계약 재작성, evidence의 spot/actor 목록은 resolve-only 계약에 따라 소거. **런타임 진행(2026-07-19, ~a27e6c6c4)**: (A) 전 e2e EvidenceStore의 단일-release 세마포어가 동시 /evidence/wait를 기아시키는 lost-wakeup(교체식 TCS pulse로 전면 교정, 10개 사본) → **LocationMessaging 16/16 통과**. (B) **PubSub config-3 store-free 전환 완료**: Redis package·provisioning·인자와 peer location endpoint를 제거하고 subscriber가 manual publisher endpoint를 사용하도록 이행했다. publisher 재시작은 subscriber socket의 `Disconnected`·`ConnectionReady`로 판정하며, monitor 활성화 뒤 최초 연결이 발생하도록 runner·late-subscriber의 연결 gate를 정렬했다. Publisher·Subscriber·Client build 0 warning/0 error, 구현·runner의 Redis/location-row scoped no-hit, `PubSub/run_e2e.sh` PS-A1~C1 7개 전체 exit 0. (C) RegistrationCodec 통과. (D) **핵심 표면 구현**: ① `IZLinkRouteClient.SendToChannel/RequestToChannel(mesh,channel)`(스펙 02 exact, entry-spot seam 관통, metadata+one-shot TrySubmit) — classic dealer는 mesh router와 와이어 비호환이라 caller도 mesh member가 되는 것이 10.0.0 계약, ② `IZLinkRouteMeshRuntime`(스펙 05 §8/50: snapshot·event polling 파생·shared drain 위임; core 미노출 필드는 gap §12.37), ③ IZLinkActorJoinCall의 계약 외 Submit 제거, api snapshot 재생성 유틸(scratch)로 갱신, ContractTests 46/46. (E) **근본수정 3건**: local row가 LifecycleGeneration=0 하드코딩(core는 wall-clock 단조 할당—restart 순서의 기준) → node MeshStatus 값 관통; `:0` ephemeral bind가 row에 literal 광고 → resolved endpoint 광고; 교체/제거된 peer의 admitted lifetime을 명시 은퇴(`DisconnectPeerLifetime(rid,gen)` seam, initiator 비게이트—core가 same-RID successor admission을 predecessor disconnect 뒤로 큐잉). seam request submit이 터미널 실패를 backpressure로 뭉개던 것 표면화. (F) ResilienceLifecycle consumer+storm fleet를 mesh member로 이행(할당 rid+ephemeral bind, IZLinkRouteClient 호출, mesh peer snapshot 기반 연결 evidence—peers 테이블 endpoint는 lifetime 교체 후 stale이라 row 광고 endpoint로 라벨), 구 9.x 단언 재정렬(down-window=RequestTargetNotFound, 스펙 05 §13.1) → **RL-A1 통과**. TD-A1 터미네이터 단언 스펙 정렬(request/join=Async/Yield only). SpotService 러너 stale spot-pub probe 제거. **잔여**: RL-A2(remap) — **원인 축소 완료**: kill−9 후 consumer의 old pipe EOF 감지가 수 초 지연(관찰: down-window 내내 peer ready 유지), replacement 재승인 뒤 지연 도착한 stale pipe-term이 `handle_peer_down`(rid 단일 키, mesh_wire_admission.cpp "Every lifetime sharing this RID rides the same transport")에서 admitted successor를 ENOTCONN ERROR로 강등 → teardown이 다음 admission을 죽이는 자기 영속 flap 후 ERROR 고착. framework 측 은퇴(DisconnectPeerLifetime)는 이미 관통(retire 시점엔 이미 CLOSED=605 NotFound 확인). **core 소관**(pipe-term을 현재 route pipe와 대조해야 함)이며 병렬 core 작업(asio engine 수정 중, 미커밋 .so 혼재)과 얽혀 있어 core 수렴 후 재검 필요, **추가 진행(2026-07-19, ~014ee95da)**: (G) spot resolve에 confirmed-miss forget(actor 규칙 미러) → 재기동 spot의 신선 row resolve 복구 → **AutomaticTurnDispatch 전체 통과(exit 0)**(TD-F5 shutdown recovery 포함). (H) row 부재만으로 admitted transport를 은퇴하지 않도록 환원(SF-B2 계약: store 장애는 기존 연결로 계속) — SF-A1/A2/B1 통과. StoreFailure consumer도 mesh member 이행(장식자 store가 slot allocation 미제공이라 고정 rid). (I) **원격 세션 disconnect 전파 구현**: 세션 transport 종료 시 원격 바인딩 액터에 disconnect frame을 ForwardPart(원격=frame-relay) 경로로 전파(기존 경로는 native bound-session 기록만 정리) — TA-A4 통과. (J) EnterDispatch: 세션 identity 없는 frame은 바인딩 생성 금지(미바인딩 액터에 유령 empty binding 생성 → push가 NotBound 대신 성공하던 결함) — TA-A2 통과. (K) manual endpoint disconnect가 admitted lifetime엔 core 정확 (rid,generation) disconnect 사용. TA-B3 단언을 스펙 05 §13.1 변환표로 재정렬(명시 disconnect→member 제거→ActorRouteNotFound)+재승인 폴링. **core-blocked 군**(동일 계열, 병렬 core asio/mesh_wire 작업과 얽힘): ① RL-A2/TA-B3-recovery — 동일 프로세스 lifetime의 disconnect→reconnect 재승인이 CLOSED row ESTALE로 영구 거부(core admission: ERROR/CONNECTING만 same-lifetime 재수용, mesh_wire_admission.cpp)+kill-9 후 stale pipe-term이 successor를 ENOTCONN 강등하는 자기영속 flap, ② SF-B2 — store 회복 창에서 zombie transport(원인 미상, core EOF 감지 지연 관찰). **추가 진행 2(2026-07-19)**: (L) 세션 relay resolve에 raw-row presence 관통 — 확정 miss(파괴/미존재)는 fail-fast, row-present(전이 중 gen-0 claim 창)만 재시도 → SM-B8(파괴 액터 오류 회신) 통과. (M) §12.36 잔여 stub 2종 실구현: spot outbound classic 채널 send TrySubmit(one-shot dealer DontWait)·spot-direct send TrySubmit(TrySendToSpotViaRouterChannelOnce) → SM-C2/C3 통과. (N) seam request submit의 NotConnected를 ZlinkSubmitException으로 던져 submitter의 retriable 분류 복원(터미널만 framework 예외) — blocking 호출이 admission 창에서 즉사하던 회귀 교정. SM-F6 첫 cross-node 호출의 admission 레이스는 시나리오 폴링으로 정합(스펙 04 §1.1: blocking submit은 send timeout 경계까지만 대기). **SpotService 진행: 30+ 시나리오 통과, SM-G1(crash-recovery)에서 정지 — crash-kill 후 재기동 노드의 wire 재승인이 core-blocked ①(stale pipe-term flap)과 동일 계열**. **잔여 config**: RuntimeMonitoring(**브리지 구현 완료, e1c2d59e4**: `IZLinkMonitoringOptions.AddMeshNodeEvents(meshName)` — mesh runtime 이벤트 스트림(스펙 50)을 ZLinkMeshRuntimeEvent(IZLinkRuntimeEvent, source=mesh)로 이벤트 버스에 브리지, preflight 검증 포함, ContractTests 46/46. **svc/시나리오 이행 진행**: svc가 AddMeshNodeEvents(mesh)+MeshEventRecorder(peer ready/disconnected→ConnectionReady/Disconnected, endpoint는 snapshot 조회+last-known 캐시)로 이행, MON-A1 mesh evidence 재작성 → **MON-A1·A2 통과**. **MON-A3 해결**: subject 표면을 framework 소유 추적으로 구현(ZLinkSpotSubscriptionTracker — spot seam의 SetSubscription/Dispose에서 (spot,topic) 추적, Subjects()가 이를 반환) → MON-A1·A2·A3 통과. **추가 진행**: MON-A4의 weight 관찰 구현(peer 이벤트 시 descriptor row의 ChannelWeights 조회→PeerAdmissionChanged|value=N evidence, row 광고 endpoint 우선) — drain/failover/restore 구간 통과, 말미 same-rid 신규 endpoint 교체 재승인만 잔존(=core-blocked ① RL-A2와 동형). **B1·B2·C1 통과** → RM 6/9 그린(A1·A2·A3·B1·B2·C1). 잔여=A4 말미(core-blocked ①), A5(HandshakeFailed: 무자격 TCP의 handshake 실패는 peer 엔트리가 없어 폴링 관찰 불가 — core mesh monitor 이벤트의 binding 표면(zlink_mesh_node_monitor_*) 노출 필요), D1(소스명 재정렬 반영; kill→동일 endpoint 재기동 재승인 대기에서 정지 — core-blocked ① 동형, 재승인 실패 evidence로 Disconnected 2회·재-ready 부재 실측). **RM 정리: 6/9 그린 + 3건 전부 core 구간 대기(재승인 flap ×2, monitor binding 표면 ×1)**. 병렬 세션의 NoDrop publisher 표면 제거(스펙+bindings 광역 리팩토링)가 dotnet lane 파일에 동승—UnitTests 총계 655로 갱신(제거된 NoDrop 케이스), 기준선 649/655+6 doc-regression), SpotService(SM-G1+ = core-blocked ①), SpotActorTransfer(**ST-C3 해결, 50a0fe20f**: joined 콜백 실패는 completion 재시도로 회복 불가한 종단 거부 — 타깃이 quarantine+rollback 후 RequestRejected 종단 회신, 소스 completion reconciliation의 terminal 술어에 RequestRejected/ActorRouteNotFound 추가(무한 재시도→timeout 해소). 러너가 runtime-marker 단언의 SPOT_DISCOVERY 게이트 소유. **ST-F1 프레임 소실 3결함 해결**: ① actor client 검증이 transfer 창(claim된 gen-0 row)을 ActorRouteNotFound로 오분류 → presence-aware 통과, ② frame relay가 세션 identity 없는(caller-routed) 전달 프레임을 거부(포워더는 이미 소비 → 무손실 위반) → sessionless 허용, ③ 수신 핸들러 RoutingId.FromHex("")가 빈 세션 hex에서 사망 → 빈 값 가드. 패킷이 target까지 end-to-end 도달·디스패치 확인. **잔여 ST-F1 본질 — 판정 완료(스펙 23 §10)**: §10.1 "Prepare가 admission을 닫은 뒤 도착한 packet은 source backlog 보존, commit하면 target 전달"+ST-C3의 joined-실패=join 거부·소스 복원 계약은 **joined 콜백이 commit-accept 이전(커밋 단계 내)에 완료**되어야 성립 — 현 구현은 joined를 completion 단계에서 실행해 (a) ST-C3에 quarantine 우회가 필요했고 (b) commit-accept 시점 cutover로 ST-F1의 소스 capture 창이 사라짐. **수정 반영**: joined 콜백을 commit 단계로 이동(JoinRoutedActorAsync가 회신 전 CompleteTransferredActorTarget 실행, completion은 replay·publish만) — 게이트 중 소스 capture 창 유지 확인(P1~P3가 소스 backlog 캡처→completion trailing으로 target 순서 replay, `handoff_backlog` 마커 소스 stdout 로거로 발행 확인, ST-F1 전반부 단언 통과). **ST-F1/F2 해결(c2aca9ea9·dcd1fc220)**: ① commit 처리를 per-request 취소에서 분리(joined 콜백이 RPC timeout을 넘겨도 dedup 재시도가 같은 preparation 대기), ② target이 더 이상 인정하지 않는 completion은 종단 RequestRejected(무한 재시도 InvalidOperationException 제거, UnitTests 단언 정렬), ③ backlog frame 복원이 sessionless(caller-routed) frame의 빈 rid byte 허용 — ST-F1 3/3 결정적 통과, 13+ 시나리오. **잔여 ST-F3+**: bound-session cross-move — 게이트 중 native 세션으로 보낸 S1/S2가 소스 pump에 레코드로 표면화되지 않음(actor의 join turn 진행 중 core 큐 대기로 추정; ST-F1의 router-plane send는 같은 창에서 표면화됐음 — 세션 전달 도메인과 claim 생명주기 차이 계측 필요). **후속 수정 반영**: 세션 relay resolve도 transfer 창(row present·미해석)에서 스핀하지 않고 기존 bound ref로 즉시 진행(ST-F1과 동형) → S1~S4 전량 배달·마커 대기 통과·join 수락. capture를 detached 병렬 dispatch(파이프라인)에서 pump 이벤트 순서가 보장되는 ingress로 이동. **후속 직렬화 2건 반영**: entry pump의 actor 배치 dispatch와 frame-relay 수신 dispatch를 per-actor FIFO 체인으로 직렬화(형제 배치 추월 제거). **잔여 flake(~1/3)**: 여전히 쌍별 스왑 발생 — 원인은 다운스트림이 아니라 **세션 호스트의 inbound 처리 동시성**: 같은 스트림 세션의 두 send가 RelayToActorAsync에 병렬 진입해 native relay write(`_relaySender.SendAsync`)가 core 진입 전에 역전(소스 trailing 캡처가 S2,S1로 기록됨을 실측). **배제 결과(추가 조사)**: 세션 runtime inbound는 이미 ZLinkStreamSessionSerialExecutor로 per-session 직렬(추정 오류였음), pump의 RaiseActor→entry 핸들러도 pump 스레드 동기 호출로 순서 보존 — 그리고 ST-F3 경로는 세션 relay가 아니라 **EnableActorDispatch의 native 바인딩 직행**(client→stream node→core actor records→pump). 3자 판별 완료(캡처 지점 계측): 실패 런에서 S1/S2 모두 **ingress에서 pump 순서 그대로 캡처**됐는데 그 pump 순서 자체가 S2,S1 — 프레임워크 전 구간(클라 connector 바운디드 채널 SingleReader FIFO·노드 ingress serial executor·세션별 serial executor·relay await·pump RaiseActor 동기·ingress 단일스레드 캡처) 순서 보존 배제 완료. **역전 창=core `SendBoundActor`→actor record 표면화 구간(core 소관)** → ST-F3 잔여 flake를 core-blocked 군 ⑤로 편입(병렬 core asio/mesh 작업의 미커밋 .so 사용 중이라 core 수렴 후 재검), ObservabilityOps(**OBS-B3 해결(83598f5af)**: ① ZLinkLocationStoreRead가 취소 비협조 store 명령을 WaitAsync로 경계(paused Redis가 응답 보류해도 read timeout 내 강등), ② workflow /evidence의 보조 row 조회에 500ms 경계 — pause 창 관통 관찰 성립, OBS A1~B3 7 시나리오 그린. **OBS-B4 해결**: 원인 2겹 — track-b lease TTL 3s < B3 pause 11s(의도된 lateness 측정이 owner fencing으로 전락해 host 자멸) → 러너가 track 공통 lease 30s 부여, pause 직후 첫 store 쓰기의 multiplexer 회복 지연 → 시나리오 폴링. **OBS 8/12 그린(A1~B4)**. **전체 통과(run_e2e.sh exit 0, 14 시나리오)**: play/workflow /evidence에 resolve-only 관찰(spotRid/actorId 쿼리 → IZLinkSpotHandleResolver/IZLinkActorDirectory 단건 resolve) 추가, C1·C2·C3·C5 시나리오를 그 표면으로 재정렬, B4 join의 room row 발행 창 폴링 — **ObservabilityOps 완전 그린(5번째 config)**. 이후 S8-12A 사전 정리로 진단용 `ZLINK_DEBUG_PUMP` env-gate 계측 전량 제거(15파일 −165줄, UnitTests 649/655·TA/ST core-blocked 지점 불변 확인), ToActorMessaging(TA-B3 recovery = core-blocked ①). **feature-map 실측 정렬**: SpotService는 최신 default-batch·SM-F6·SM-G2 통과분만 구현으로 반영하고 SM-G1은 core 대기, RuntimeMonitoring은 A1·A2·A3·B1·B2·C1만 구현이며 A4·A5·D1은 core 대기, ResilienceLifecycle·StoreFailure·SpotActorTransfer·ToActorMessaging은 최신 전체 실행의 최초 core 중단 지점 뒤 행을 재검 대기로 기록했다. doc-contract verifier clean, 공통 E2E fixture regression의 미구현 잔여는 SM-C6·SM-G1 두 건이다. 오래된 문서 회귀 5건과 연관 matrix 검사를 10.0 계약으로 갱신한 뒤 UnitTests 654/655를 확인했으며, 유일한 실패는 두 시나리오를 보고하는 공통 E2E fixture regression이다. **10.1.0 package 재검(2026-07-19 10:50 KST)**: MON-A5와 ST-F3·F4·F5는 통과했다. RL-A2·TA-B3·SF-A2→SF-B1·MON-D1은 재승인·target 보존에서 다시 실패했고, MON-A4는 endpoint 종료·교체 event 구분 뒤 channel weight event 변환에서 실패했다. SM-G1은 control route 선행 실패로 미도달했다. 상세 명령·로그·최초 단언은 core blocked bug report의 Framework 담당자 확인 기록에 고정했다 |
| S8-11 | 리뷰 종료 뒤 source/package contract 검증 | 두 `DOTNET REVIEW CLEAN` 뒤 `scripts/verify_packaged_contract.sh`, NuGet consumer와 native payload 일치 | 진행 | 순서 변경으로 package gate를 선실행했다. frozen package snapshot을 10.1.0 dependency와 현재 XML hash로 갱신한 뒤 `scripts/verify_packaged_contract.sh`가 8개 pack·source/package API snapshot·standalone HTTP package·clean NuGet consumer를 모두 통과했다(`public_api_snapshot_sha256=8f8bd482efa1981577aa5f410afea7c8ba0fb15e04e54b251b384cc3f5486698`). 다만 NuGet Linux x64 native hash `7aa845…`와 `core/build/lib/libzlink.so` hash `d0365a…`가 다르고 arm64 payload가 `libzlink.so.9`이므로 BUG-9 재배포 뒤 native 일치와 두 review clean이 남았다. |
| S8-12 | 성능과 resource 회귀 | 별도 성능 개선 작업의 입력으로 분리 | 후속 분리 | 현재 S8 gate를 위해 성능 측정을 실행하지 않음 |
| S8-09A | `.NET` TicTacToe 단일 MeshNode topology 재검 | API·Play process당 물리 MeshNode 1개, 동일 ChannelName 집합, 수동 handler·peer 계약 유지와 실제 sample 통과 | 진행(Core duplicate-peer RED) | 2026-07-20에 API·Play 프로세스를 각각 `AddRouteMesh(SampleNodes.Mesh)` 1개와 동일한 `Api`·`PlayA`·`PlayB`·MeshName membership으로 합쳤다. TicTacToe의 `DisableImplicitHandlerAutoRegistration()`과 typed handler 수동 등록은 유지했고, STREAM actor dispatch와 channel request의 MeshName도 같은 물리 MeshNode로 맞췄다. runner의 channel·Spot·Spot pub/sub 전용 설정과 probe를 제거해 port를 13개에서 8개로 줄였으며 shell·PowerShell 설정을 동기화했다. solution build 0 warning/0 error와 TicTacToe 집중 sample 회귀 15/15가 통과했다. 실제 runner는 `/tmp/tmp.gc4E06kn5H`에서 actor remote join·request/reply까지 통과한 뒤 owner Play의 Logical Multicast가 local Spot에만 전달되고 remote observer에는 전달되지 않았다. publish detail fail-fast 실행 `/tmp/tmp.9zVMZOXlMB`에서는 `Submitted`, remote snapshot/admitted `2/2`, local `1/1`이었다. Core 집중 회귀 `test_multicast_selects_inbound_eligible_peer_over_zero_weight_outbound_peers`는 eligible peer의 inbound 연결 뒤 reciprocal intent가 추가되고 weight 0 outbound peer 2개가 있는 topology를 재현한다. 회귀에서 public peer 목록은 logical RID `eligible-inbound`를 `MANUAL`과 `DISCOVERY` 두 ADMITTED row로 중복 보유해 총 4개가 되었고, weight 0 peer는 제외됐지만 같은 eligible RID가 multicast snapshot/admission에 2회 포함됐다. 기대 remote `1/1` 대비 실제 `2/2`로 RED이며, diagnostic 실행에서는 eligible peer의 실제 수신은 확인됐다. timeout 증가는 하지 않았다. |
| S8-12A | `.NET` guide 갱신 | 구현·sample·E2E 일반 검증이 통과한 뒤 guide에 정식 공개 계약의 사용법을 반영 | 완료 | 14개 guide 장의 10.0.0 사용 표면을 갱신했다(기준 commit `7118f2a5d`와 후속 정리). 독립 개념명 `SpotNode` 75곳을 `MeshNode`로 정렬하되 실제 공개 식별자 `spotNodeRid`·`ZLinkSpotNodeStatus`는 보존했고, 사용자 문서에서 internal test-only `UseInMemoryLocationStores()`를 제거했다. 후속 재검에서 `02-getting-started.ko.md`에 남은 구 channel client 예제를 찾아 실제 TicTacToe와 같은 `AddRouteMesh`·`ChannelName`·manual peer·`IZLinkRouteClient` 호출로 교정했고 `13-interface-catalog.ko.md`의 outbound client·builder·connection·socket catalog에서도 같은 구 interface와 builder를 제거하고 MeshName·ChannelName·MeshNode 설정 및 현재 submit terminator로 교정했다. `09-stream.ko.md`의 session 서비스 호출과 `14-grpc-alternative.ko.md`의 비교 표·호출 예제도 `IZLinkRouteClient`의 MeshName·ChannelName 표면으로 맞췄고, gRPC 비교 장의 ASCII diagram 내부 텍스트를 영문으로 정렬했다. `10-location.ko.md`의 자동 discovery 예제도 MeshNode descriptor 모델로 바꾸고 requester-only membership의 weight 0 이유를 설명했다. `.NET` 문서 루트 README의 호출·handler·수동 연결 방향도 통합 `IZLinkRouteClient`와 MeshNode ownership 기준으로 다시 정리했다. `03-concepts.ko.md`에서는 client-server 역할과 내부 소켓 설명을 제거하고 MeshName·ChannelName membership·zero-weight requester 관계 및 typed handler 등록 예제로 교정했다. `01-overview.ko.md`도 구 client-server·route channel 예제와 하부 소켓 설명을 제거하고 `AddRouteMesh`·`IZLinkRouteClient` 중심의 공개 사용 흐름과 guide/internals 경계로 맞췄다. `06-spot.ko.md`는 별도 client-server channel·내부 소켓 설명을 제거하고 Spot context의 MeshName·ChannelName membership 모델로 바꿨으며, `IZLinkSpotManager`·`IZLinkSpotClient`·Logical Multicast·worker·Actor registry 책임 경계를 정식 interface spec과 일치시켰다. 갱신한 문서의 구 topology 표면 no-hit와 `FRAMEWORK DOC CONTRACTS CLEAN`을 다시 확인했다. 구 topology 표면·금지 문체 scoped no-hit, `FRAMEWORK DOC CONTRACTS CLEAN`, UnitTests 654/655를 확인했으며, 유일한 실패는 공통 E2E fixture regression의 SM-C6·SM-G1이다. |
| S8-13 | Codex agent `.NET` 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S8-14 | Claude Sonnet `.NET` 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S8-15 | finding 수정·일반 검증·전체 재리뷰 반복 | finding을 일괄 수정하고 일반 build·전체 테스트 통과 뒤 두 리뷰어가 `.NET` 전체와 세 축을 재검토해 둘 다 `DOTNET REVIEW CLEAN` | 미착수 | - |
| S8-16 | process-local MeshName uniqueness 검증 | 중복 AddRouteMesh 실패와 multi-mesh 독립 동작 통과 | 완료 | process-local MeshName uniqueness: 중복 AddRouteMesh AddUnique 실패 + per-node stream-dispatch keying(multi-mesh 독립) |
| S8-17 | 확정 `.NET` internals 갱신 | 두 `DOTNET REVIEW CLEAN`과 S8-11 종료 검증 뒤 최종 pump·scheduler·location·timer 구조를 internals에 반영 | 미착수 | - |
| S8-18 | `.NET` internals 확정 검사 | S8-17 문서와 최종 source·구조 test·diagram·link의 차이 0개. 문서 검사만으로 구현 전체 재리뷰를 열지 않음 | 미착수 | - |

최신 current-tree 추가 증거(2026-07-20): `SpotActorTransfer/run_e2e.sh all`은 기본 17개와
별도 process generation의 ST-B2·C2·C1을 모두 통과했다
(`SpotActorTransfer/logs/20260720-044205-2109114`). ST-C1은 공통 스펙의
pending-admission timeout cleanup marker를 30초 bounded wait로 검증한다. 전체 `.NET`
E2E 집합과 종료 unit/package gate가 아직 남아 있으므로 S8-10 상태는 `진행`으로 유지한다.

StoreFailure 복구 경계 추가 증거(2026-07-20): 고정 route settle 5초를 제거하고 각 시나리오를
새 Redis와 process graph에서 독립 실행하도록 바꿨다. readiness와 harness window는 3초 local
bound를 유지하고 wall clock 변동에 영향받지 않는 monotonic clock으로 판정한다. change-stamp
preflight가 store 장애를 감지한 직후 fallback 전체 읽기가 성공하면 불완전한 recovery snapshot을
정상 diff로 적용해 두 transport를 끊던 경쟁을 회귀로 고정하고, preflight 실패도 fail-static
recovery barrier에 연결했다. 관련 auto-connect 31/31, `.NET` UnitTests 634/634와
SampleRegressionTests 112/112가 통과했다. SF-D1은 1.58초 store 정지로 수정 뒤 5/5와 계측 제거
뒤 1/1, SF-B2는 최종 1/1, StoreFailure 독립 전체 SF-A1·A2·B1·B2·D1·D3·C2·C1·D2·E1
10/10이 통과했다. ResilienceLifecycle과 LocationMessaging에 섞여 있던 dynamic readiness 15초
증액도 3초 monotonic gate로 환원했다. ResilienceLifecycle은 고정 settle을 제거하고 public route
snapshot을 포함한 topology readiness를 사용해 RL-C1이 통과했으며, LocationMessaging RM-A4도
3초 readiness로 통과했다. version과 package는 변경하지 않았다.

RuntimeMonitoring 추가 증거(2026-07-20): MON-C1의 느린 local target handler가 실제로는 즉시
완료되어 1 MiB publish를 수만 번 반복하고, application gate request의 30초 operation timeout을
먼저 소진하던 test-fixture 결함을 확인했다. handler가 짧게 대기하는 동안 bounded mailbox가 즉시
채워지도록 고쳐 timeout 증액 없이 부분 local-target drop을 만들었다. MON-B2 집중 검증과 MON-C1
집중 검증이 각각 통과했고, 이후 `RuntimeMonitoring/run_e2e.sh all`에서 MON-A1·A2·A3·A4·A5·B1·
B2·C1·D1 9개를 새 Redis와 process graph로 각각 격리해 모두 통과했다. 최종 MON-C1 log는
`RuntimeMonitoring/logs/20260720-103334-3132097`, 전체 완료 marker는 2026-07-20 10:34 KST
실행이다. local readiness는 3초를 유지했고 version과 package는 변경하지 않았다.

`.NET` 전체 runner 재실행 추가 증거(2026-07-20): `run_e2e_all.sh`에서 LocationMessaging 16개,
PubSub 7개, RegistrationCodec 11개와 ResilienceLifecycle RL-A1~D1이 연속 통과했다. RL-D2를 시작할
때 runner가 build 전에 하나씩 선택하고 해제한 ephemeral port를 다른 병렬 실행이 먼저 점유해
`Address already in use`로 종료된 harness 경쟁을 확인했다. 9개 role port를 한 번에 중복 없이
선택하고 build·Redis 준비가 끝난 뒤 배정하도록 바꾸었고, readiness 3초는 유지한 채 RL-D2·D3·D4·D5가
각각 통과했다. 이어 실행한 RuntimeMonitoring은 MON-A1~C1을 통과했지만 MON-D1 첫 same-RID·same-endpoint
재기동에서 새 `svc-b` process와 endpoint가 정상인데 observer snapshot이 Ready로 복구되지 않았다
(`RuntimeMonitoring/logs/20260720-113652-3355682`). 시나리오 안에 남아 있던 35초 wall-clock
ready/not-ready 대기를 monotonic 3초로 교정한 뒤에도 같은 지점에서 결정적 RED가 재현됐다
(`RuntimeMonitoring/logs/20260720-113829-3360295`). timeout 증액이나 framework 우회 없이 Core의
reciprocal handover·old pipe 종료 경계 수정 뒤 전체 11-config runner를 다시 실행한다. 같은 현재
트리에서 MON-D1을 제외한 후반 6-config 묶음은 SpotService 전체, SpotActorTransfer 전체,
StoreFailure 10개, ToActorMessaging 전체, AutomaticTurnDispatch 전체와 ObservabilityOps 전체를 모두
통과해 `run_e2e_all.sh`가 `total PASS (590s)`로 종료됐다. 따라서 현재 `.NET` 기능 RED는
RuntimeMonitoring MON-D1의 same-RID 재승인 한 경계로 축소됐다.

`.NET` 고정 settle 제거 추가 증거(2026-07-20): PubSub은 subscriber의 실제
`ConnectionReady` evidence를 3초 안에 확인하도록 바꾸고 PS-A1~C1 7개가 모두 통과했다
(`PubSub/logs/20260720-103633-3138150`). RegistrationCodec requester는 public RouteMesh
snapshot의 peer ready와 channel selectable을 함께 확인하며 RC-A1~B5 전체가 통과했다
(`RegistrationCodec/logs/20260720-103857-3145024`). LocationMessaging의 네 consumer도 필요한
ready peer 수와 `profile` channel selectable을 확인한 뒤 시작했고 RM-A1~C9 전체가 통과했다
(`LocationMessaging/logs/20260720-104009-3148358`). SpotService는 control ping 앞의 5초 sleep과
30초 readiness window를 제거하고 monotonic 3초 gate만 사용해 동일 RID 재기동 `sm-g1`이 통과했다
(`SpotService/logs/20260720-104426-3160947`). AutomaticTurnDispatch에서는 물리 peer ready만으로
진행하지 않고 session→play control request와 play→delay channel selectable을 확인했다. 이 과정에서
별도 physical mesh인 `await.delay`를 Spot owner mesh의 outbound로 호출하던 sample 결함을 정식
`IZLinkRouteClient.RequestToChannel(meshName, channelName, ...)`로 교정하고, STREAM Actor dispatch의
`EnableActorDispatch(await.spot)` 누락을 보완했다. 재기동 뒤에는 control request reply와 public Spot
resolver의 새 owner 관찰을 각각 3초 안에 확인한다. 최종 TD-A1~G1과 shutdown wait/recovery 전체가
통과했다(`AutomaticTurnDispatch/logs/20260720-110654-3252732`). `.NET` E2E runner의 고정
route/scenario settle과 3초 초과 local readiness 검색 결과는 0건이다. 이 변경을 모두 포함한 현재
트리에서 UnitTests 634/634, ContractTests 43/43, SampleRegressionTests 112/112를 순서대로 다시
실행해 모두 통과했다. version과 package는 변경하지 않았다.

추가 local-bound 재검(2026-07-20): RuntimeMonitoring의 MON-A1~A5·B1에 남아 있던
30~35초 wall-clock 상태 대기를 모두 monotonic 3초로 교정했다. 이때 MON-A5는 Redis command의
기본 약 5초 timeout 때문에 location `degraded` 관찰이 3초를 넘기는 별도 RED가 드러났다.
RuntimeMonitoring E2E fixture의 Redis `AsyncTimeout`·`ConnectTimeout`만 500ms로 제한해 장애 관찰을
운영 기본값에 숨기지 않았고, MON-A1·A2·A3·A4·A5·B1 집중 실행이 모두 통과했다. MON-D1은 같은
3초 기준에서 계속 RED이므로 Core same-RID 재승인 blocker로 유지한다. SpotService SM-C6의 runner
ack와 exactly-one delivery에 남아 있던 30초 wall-clock fallback도 monotonic 3초로 줄였고,
`run_e2e.sh sm-c6`가 non-blocking 2ms·blocking 252ms로 통과했다
(`SpotService/logs/20260720-115624-3442929`). timeout 증액, version 변경과 package 배포는 없었다.

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
- [ ] 두 clean 결과 뒤 S8-11의 package·consumer 종료 검증이 통과했다.
- [ ] S8-11 뒤 S8-17/18에서 최종 `.NET` internals를 한 번 반영하고 source와의 일치를 확인했다.

### 12.9 실행 로그 (order·method 변경, 2026-07-18)

coordinator가 자율 판단으로 순서·방법을 변경한 내역과 framework 구현이 드러낸
binding-surface gap을 기록한다(사용자 지시: 변경은 로그로 남기고 모든 항목을 완료).

**순서·방법 변경**

- framework 전환 아키텍처를 **Option B**(framework-소유 dispatch record가 MeshReceiveRecord에서
  생성, bindings의 Received/TopicMessage로 되돌리지 않음. DrainReady pump가 record를 per-owner
  serial-executor 큐로 fan, claim은 finally 해제. MeshOperationId↔Completion 콜백 테이블이
  builder `.Submit(callback)`을 대체)로 확정. 근거=bindings CLEAN 표면이 framework에서
  Received/TopicMessage를 직접 생성할 수 없어(binding-gap) spec 근거로 record 소유를 framework에 둠.
- 구현 항목을 파일-disjoint 기준으로 **병렬 실행**(S8-04군 / S8-06군 / UnitTests drift)해 통합.

**framework가 드러낸 binding-surface gap (후속 batch — spec 근거 확인 후 binding 추가 + focused 재리뷰)**

1. **Received metadata**(S8-06A): handler context가 send-metadata를 immutable view로 노출하려면
   binding `Received` 타입이 metadata를 실어야 함(spec 02-handler-interfaces 근거). 현 frozen `Received`는
   확장 불가 → codec+seam decode만 착지, full send-builder API·context threading 잔여.
2. **actor-row generation 필드**(S8-04A, gap 90 §12.27): `ZLinkActorLocation`에
   OwnerNodeGeneration/MembershipEpoch/SpotGeneration 부재 → transfer commit의 actor-location-row
   rewrite가 상태만 진행. descriptor 모델 정렬(§12.33/S8-04)과 함께 처리.

이 gap들은 "다른 언어 구현만으로 신규 public contract 추가 금지" 원칙(AGENTS.md)에 따라 각기
spec/guide 근거를 확인한 뒤 binding에 추가하고, 해당 lane bindings의 focused 재리뷰를 1회 재개방한다.

**완료(2026-07-18)**: S8-02/02A/03/05/06/16(빌더·DI·handler dispatch·전송 배선·pump·MeshName),
S8-04A/04B(Redis transfer authority·production 정책), S8-06B(timer 검증). Framework+AspNetCore+Redis 0/0.

**UnitTests 이관 완료(2026-07-18)**: ~14 test 파일 10.0.0 MeshNode surface로 이관. 통합 중 발견한
**MeshNode 생성 EINVAL 근본 버그**(framework가 mesh-name 없이 CreateMeshNode → Core zlink_mesh_node_new
거부) 수정 = SpotMeshChannelName/ActorDispatchMeshName을 CreateSpotNode/CreateStreamSocket에 배선.
이 한 수정이 리뷰어 C 보고 native gap 3건(CreateMeshNode EINVAL·stream shutdown timeout·통합 hang/crash)을
모두 해소. **전체 UnitTests 677/683 green**; 남은 6건은 Documentation.RegressionTests(E2E fixture 8/15
미연결·contract-ledger/README/acceptance 미갱신)로 S8-09/10/12A/17/18 완료 시 green이 되는 추적 게이트.

**현재 checkout 재검(2026-07-19 13:24 KST, `eab8b069d`)**: UnitTests는 623/625다. 실패는 G0 spec
snapshot hash 1건과 공통 E2E fixture에서 `SM-C6`·`SM-G1`이 구현 상태가 아닌 1건이다.
`03-message-model.ko.md`의 실제 SHA-256 `5bec94bb…65182`는 G0 ledger에 반영했다. `SM-G1` 단독
실행은 두 번 모두 첫 원격 actor-session bind에서 timeout됐으며 두 번째 실행 log는
`e2e/SpotService/logs/20260719-132359-22978/`이다. `session-a`는 internal bind를 반복 제출했지만
`play-a` 수신 증거가 없으므로 `SM-G1`은 완료로 올리지 않는다.

**mesh 선택 수정 후 재검(2026-07-19 13:39 KST)**: 정식 `.NET` interface의
`IZLinkActorClient.SendToActor/RequestToActor(meshName, ...)`를 실제 공개 surface와 runtime에 반영하고,
STREAM의 `EnableActorDispatch(meshName)`을 bind·frame relay·disconnect 경로까지 관통했다. 전체 solution
build는 0 warning/0 error다. `SM-G1`은 첫 bind를 통과해 `crash-1-ready`와 `restart-1-ready`까지 진행했으며,
재시작한 `play-a`의 ready 대기가 504로 끝났다. log는
`e2e/SpotService/logs/20260719-133919-44907/`이다. 따라서 framework의 첫 control-mesh 오선택은
수정됐고, 같은 RID 재승인 잔여는 Core 10.2.0 handover 회귀 수정 뒤 다시 검증한다.

**최신 Core 재검(2026-07-19 20:03 KST)**: Core 10.6.0과 동일한 SHA-256의 native
runtime을 넣어 `.NET` 10.6.1 local package를 다시 만들고 격리된 NuGet cache에서 `sm-g1`을
재실행했다. `crash-1-ready`와 `restart-1-ready` 뒤 재기동 `play-a`는 `ControlPingReq`를
수신하고 handler reply 단계까지 진행했지만 gateway의 ready request는 다시 504로 끝났다.
log는 `e2e/SpotService/logs/20260719-200140-930722/`이다. 같은 시나리오는 C++의
`RL-B2`와 Java의 `SM-G1`에서 통과했으므로 일반 same endpoint/RID reconnect 회귀로
분류하지 않는다. 임시 Core 계측에서는 gateway가 받은 correlation 1 reply가 이미 timeout으로
제거된 이전 operation이었고, 재기동 `play-a`의 control reply는 framework의 `replied` flow
marker 뒤 Core reply API에 진입하지 않았다. 계측 코드는 제거했으며, 다음 조사는 `.NET`의
inbound route reply token 보존과 submit 결과 경계에서 계속한다.

**SM-G1 crash 재claim 수정과 반복 검증(2026-07-19 21:29 KST)**: 같은 package version으로
native payload를 다시 만들었을 때 NuGet global cache와 E2E `bin`에 이전 Core가 남아 있음을 SHA-256
대조로 확인했다. cache를 격리해 local package의 `libzlink.so.10.6.0`과 실제 실행 파일을
`4d508742…70391`로 맞춘 뒤 재기동 Actor generation이 단조 증가했다. 두 번째 crash 뒤 play-b의
`JoinReq` reply completion은 정상인데 gateway가 새 Actor row를 10초 동안 찾지 못한 직접 원인은,
만료된 owner의 stale row가 Redis에 남으면 observed generation guard가 이전 membership epoch floor를
계속 보존해 새 owner의 per-instance epoch를 lagging row로 거부한 것이었다. owner lease 만료도 row
부재와 같은 lifecycle 종료로 처리하되, live owner의 lagging replica에는 floor를 유지하도록 resolver를
수정했다. `LocationResolverTests` 24/24가 통과했고, `sm-g1`은 계측 실행 1회와 비계측 반복 5회,
임시 계측 제거 뒤 최종 build 포함 실행 1회가 모두 통과했다. 최종 log는
`e2e/SpotService/logs/20260719-212849-1143155/`이다. 따라서 `.NET` SM-G1의 Core-blocked 분류는
해제한다. `.NET` 전체 E2E gate와 나머지 S8-10 항목은 계속 진행한다.

**LocationMessaging 전환 완료(2026-07-19 22:26 KST)**: 구 socket monitor를 정식
`AddMeshNodeEvents("profile")`로 바꾸고, RM-A2를 location store가 없는 manual peer consumer
경로로 정렬했다. drain 때 기존 channel bundle만 weight 0으로 바꾸던 누락을 고쳐 현재 MeshNode의
모든 membership과 descriptor에 같은 값을 반영했으며, crash failover는 Redis row뿐 아니라 실제
MeshNode admission을 확인한 뒤 traffic을 시작한다. known disconnected peer와 never-known target의
오류도 각각 `RouteNotConnected`와 `RequestTargetNotFound`로 분리했다. RM-C7에서 발견한 Core의
양수 weight 무시는 smooth weighted round-robin과 75:25 회귀로 수정했다. RM-C8에서는 `.NET`
MeshNode binding의 `MaxMessageSize` public option과 framework startup·live 연결을 추가하고, Core가
실행 중 상한을 complete wire message 경계에 적용하도록 수정했다. Core peer suite 21/21과
MeshNode basic·monitor·lifecycle 3/3이 통과했다. Core runtime과 NuGet package의 native SHA-256을
`aeed0a89…e119a`로 맞춘 격리 cache에서 RM-C7·C8 focused 실행이 각각 통과했고, 최종
`e2e/LocationMessaging/run_e2e.sh`는 RM-A1/A2/A4/A6, RM-B1/B2/B3,
RM-C1/C2/C3/C4/C5/C7/C8/C9 전부를 138초에 통과했다. 최종 log는
`e2e/LocationMessaging/logs/20260719-222317-1281422/`이다. 버전 reset과 내부 package 배포는
전체 언어 gate 뒤 수행한다.

## 13. S8-CPP·JVM·NODE lane 상세 — C++·Java/Kotlin·Node.js framework 단계

### 13.1 병렬 작업 격리

S8의 `.NET`과 S9의 C++·Java·Kotlin·Node.js는 S7 gate 뒤 동시에 구현한다. 다섯 언어를 네 실행 lane으로
나누며 Java와 Kotlin은 공유 JVM lane에서 함께 진행한다. 각 lane은 다른 언어의 구현 완료나 clean 판정을
기다리지 않는다. 한 lane에서 공통 계약 문제가 발견된 경우에만 해당 finding의 직접 영향 범위를
coordinator가 판정하고, 필요한 lane을 멈춘 뒤 S2·S3을 다시 연다.

| 원칙 | 적용 방법 |
|---|---|
| file ownership | `.NET`, C++, JVM, Node.js lane은 자기 언어 source·test·sample·언어별 문서만 수정 |
| 공통 문서 | common spec과 main plan은 coordinator만 수정하고, 이 진행표는 각 lane이 배정받은 ID 행만 수정 |
| 진행 기록 | 각 lane이 자기 ID 행의 상태와 증거를 이 진행표에 직접 기록한다. 상세 log를 별도로 남겨도 현재 상태는 이 진행표에만 기록 |
| build output | 각 lane은 독립 build directory와 package cache를 사용 |
| `.NET` 제한 | 중앙 package version과 NuGet cache를 다른 lane의 package 기준으로 변경하지 않음 |
| JVM 제한 | bindings/java와 framework/languages/java build를 동시에 실행하지 않고 Gradle `--no-parallel` 사용 |
| Node 제한 | stale `dist`를 제거하고 build 완료 뒤 test를 순차 실행 |
| C++ 제한 | 전용 CMake build directory에서 package consumer와 CTest 실행 |
| merge | 네 lane의 검증 commit을 따로 만들고 공통 tree에 한 lane씩 병합·재검증 |

### 13.1A ChannelName 단일 주소·ClientServer 네 lane 적용

다음 항목은 기존 lane의 완료 증거를 역사적 증거로 보존하면서 새 amendment로 바꾸어야 하는
공개 API·runtime·sample·E2E를 별도로 추적한다. 네 항목은 서로 병렬 진행한다.

| ID | Lane | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-08A | .NET | ChannelName index·중복 startup 오류, RouteMesh Client/Server, ClientServer 방향·descriptor·자동 발견·weight·drain, 공통 network identity, Spot `Async`/`Yield` completion을 구현하고 Config 12·CH-REG·7 sample·전체 E2E 통과 | 미착수 | S3-CH-03·S5-CH-01·S7-CH-01 선행 |
| S9-C02D | C++ | S8-08A와 같은 공개 동작을 C++ exact interface와 기존 scheduler·adapter 경계로 구현하고 C++ Config 12·CH-REG·7 sample·전체 E2E 통과 | 미착수 | S3-CH-03·S5-CH-01·S7-CH-01 선행 |
| S9-J03D | JVM | Java·Kotlin 모두에 S8-08A와 같은 공개 동작을 제공하고 JVM Config 12·CH-REG·7 sample·전체 E2E 통과 | 미착수 | S3-CH-03·S5-CH-01·S7-CH-01 선행 |
| S9-N02D | Node.js | S8-08A와 같은 공개 동작을 TypeScript·NestJS exact interface와 기존 pump 경계로 구현하고 Node Config 12·CH-REG·7 sample·전체 E2E 통과 | 미착수 | S3-CH-03·S5-CH-01·S7-CH-01 선행 |

Classic fanout 자동 연결은 아래 네 항목으로 병렬 적용한다. 기존 manual endpoint 표면과 PS-A1~C1은
삭제하지 않고 회귀로 유지한다.

| ID | Lane | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-FO-DN | .NET | 정식 exact interface의 endpoint 없는 subscriber 자동 연결, fanout peer descriptor·publisher 게시·role/channel 선택·lease 수렴을 구현하고 Config 3 자동·manual·negative 회귀 전체 통과 | 미착수 | S3-FO-01 선행 |
| S9-FO-CPP | C++ | 기존 peer-location 구현을 정식 계약과 대조해 누락을 보완하고 C++ Config 3 자동·manual·negative 회귀 전체 통과 | 미착수 | S3-FO-01 선행 |
| S9-FO-JVM | JVM | Java·Kotlin 공개 표면과 location runtime을 정식 계약에 맞추고 두 lane의 Config 3 자동·manual·negative 회귀 전체 통과 | 미착수 | S3-FO-01 선행 |
| S9-FO-NODE | Node.js | TypeScript·NestJS 공개 표면과 location runtime을 정식 계약에 맞추고 Node Config 3 자동·manual·negative 회귀 전체 통과 | 미착수 | S3-FO-01 선행 |

MeshNode 고정 drain 계약은 아래 네 항목으로 병렬 적용한다. Policy enum·builder와 runtime 분기를 제거하고
public API를 추가하지 않은 채 같은 cleanup 순서를 구현한다.

| ID | Lane | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-DP-DN | .NET | MeshNode·Spot drain policy 공개·내부 표면을 제거하고 accepted turn, Actor handoff, STREAM barrier 뒤 모든 남은 local Spot을 닫아 owner cleanup과 terminal result 1회를 완료하며 .NET OBS-C1~C5와 제거 no-hit 통과 | 진행 중(구현·OBS-C3 완료) | Policy surface를 제거하고 admission seal→draining publish→accepted zero→Actor→STREAM→Spot→cleanup→terminal 순서, one-shot force와 multi-mesh native call 전 fail-fast를 구현했다. Drain coordinator·dispatcher 집중 test 30/30 통과. OBS-C3는 normal Spot 유지, 신규 admission 거부, accepted turn, remote resolve/request/send, stale handle hidden create 금지, 명시적 local GetOrCreate·state replay와 terminal 1회를 검증해 PASS(`ObservabilityOps/logs/20260720-234350-769553`). OBS-C1·C2·C4·C5 전체 lane gate와 S3 문서 gate가 남아 있다 |
| S9-DP-CPP | C++ | 미소비 policy state를 제거하고 C++ host drain에 accepted work·Actor·STREAM 선행, local Spot close·empty wait와 bounded force stop을 연결하며 OBS-C1~C5와 제거 no-hit 통과 | 진행 중(구현·집중 검증 완료) | Policy state를 제거하고 app admission tracking, fixed phase order, Spot cleanup과 one-shot force를 연결했다. Multi-mesh `drain(mesh)`는 global native cleanup 전에 fail-fast하며 callback 0을 검증했다. Build, focused CTest 5개, OBS-C3와 diff-check가 통과했다. OBS-C1~C5 전체 runner와 S3 문서 gate가 남아 있다 |
| S9-DP-JVM | JVM | Java runtime의 policy 저장·변환·분기를 제거해 고정 cleanup을 실행하고 Kotlin projection과 함께 두 OBS-C1~C5·sample·제거 no-hit 통과 | 진행 중(구현·집중 검증 완료) | Java·Kotlin policy surface를 제거하고 mesh-keyed atomic claim/seal/await-zero, fixed phase order, stale handle typed not-found와 단일-mesh host 제한·multi-mesh fail-fast를 구현했다. Core·starter·Kotlin 전체 unit test, sample·E2E compile과 binding source composite가 통과했다. 동일 process 두 native MeshNode 생성의 Core INTERNAL_ERROR blocker와 최종 local package·OBS-C1~C5 runner가 남아 있다 |
| S9-DP-NODE | Node.js | framework·NestJS policy type·builder·resolver와 type별 분기를 제거해 모든 local activation을 고정 순서로 정리하고 OBS-C1~C5·sample·제거 no-hit 통과 | 진행 중(구현·집중 검증 완료) | Mesh-keyed admission seal/await-zero, fixed phase order, Actor zero-target bounded force, stale handle typed 오류, multi-mesh fail-fast와 snake_case force reason을 구현하고 구 policy/control symbol을 제거했다. lint·build, drain/store/stream/Nest와 Spot 집중 test 102/102, diff-check가 통과했다. 기존 Actor lifecycle·route call drift 4건과 OBS-C1~C5 전체 runner, S3 문서 gate가 남아 있다 |

`S3-CH-03`이 끝나면 기존 증거 중 영향받는 .NET `S8-02·03·05·08·09·10·12A·16`, C++
`S9-C02~C06`, JVM `S9-J02~J07`, Node `S9-N02~N06`을 새 계약에서 재검증한다. 이 네 항목과
Config 12·sample topology fixture가 통과하기 전에는 언어별 framework review snapshot을 동결하지 않는다.

### 13.2 C++ lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-C01 | bindings local package 10.6.0 pin | CMake central version과 package resolve 확인 | 완료 | CMake central pin과 local install base를 Core 10.6.0에 맞췄다. C++ binding 수정본으로 framework를 다시 구성했고 install package consumer가 `zlink-native` install interface를 통해 실제 링크까지 통과했다. binding patch version은 전체 gate 뒤 결정한다 |
| S9-C02 | RouteMesh/MeshNode interface 구현 | C++ 정식 interface와 source 일치 | 진행 중 | MeshNode builder/runtime/host, node·channel·Spot·Actor send/request와 pull dispatch vertical을 구현했다. 정식 `mesh_node_builder_t`의 Entry Spot·Spot·Actor factory·transfer adapter 등록 표면을 기존 lifecycle/handler 등록 상태와 연결했다. 생성자 의존성이 있는 Spot도 activation scope에서 생성하도록 정식 C++ interface에 factory overload를 먼저 고정하고, 기존 Spot lifecycle builder에 위임했다. 단일 MeshNode pull loop가 operation ID별 completion을 demux하고, application `SPOT_CONTROL`과 `ACTOR_SEND`/`ACTOR_REQUEST`를 typed Spot·Actor handler로 전달한다. nested Actor join 중에도 pull loop가 completion을 계속 소비하도록 application dispatch를 bounded worker로 분리했다. MeshNode가 기존 typed `route_client_t`의 node send/request transport에 연결되지 않아 `RM-C2`가 `route channel ... is not registered`로 실패하던 결함을 MeshNode 전용 node transport로 연결했다. location readiness snapshot으로 `api-b` router 준비를 확인한 뒤 targeted request를 검증하며 forward·reverse·shuffle 시작 순서가 모두 통과했다(`e2e/RegistryMessaging/logs/20260719-215605-1215221`, `20260719-215618-1217297`, `20260719-215624-1217293`). `.artifacts/build/cpp-framework-e2e106`에서 MeshNode vertical, target contract, app host, runtime integration, stream framework focused test 5/5도 통과했다. `tcp://127.0.0.1:0`의 실제 bind endpoint를 descriptor에 반영하고, 같은 MeshName·RID의 RouteMesh host를 새 endpoint로 12회 순차 재생성해 stable provider targeted request와 전체 cleanup이 통과했다(`e2e/ResilienceLifecycle/logs/20260720-022259-1836671`) |
| S9-C02A | C++ metadata·timer 연결 | S/S metadata 전체 공통 matrix와 C API Spot timer adapter가 handler·generation·cancel 계약 통과 | 진행 중 | Mesh metadata codec과 node·channel·Spot·Actor vertical metadata 전달은 fixed binding 기반 test에서 통과했다. public Spot timer 등록·idle close·overrun policy를 검증하는 `SM-E2`, `SM-E3`, `SM-E4`도 각각 통과했다(`e2e/SpotService/logs/20260720-024323-1873328`, `20260720-024338-1873826`, `20260720-024349-1874057`). metadata 전체 공통 matrix는 아직 검증하지 않았다 |
| S9-C02B | C++ Actor transfer authority 연결 | 정식 store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 진행 중 | Core Actor create/join completion과 pull dispatch를 연결하고, cross-node 이동을 source prepare/fence → target prepare/commit/activate → source commit 순서로 연결했다. target은 source Actor generation을 가진 Core placeholder를 사용하며 별도 native Actor를 생성하지 않는다. Actor generation은 membership 이동 전후 동일하고 membership epoch만 증가한다. A→B→A 왕복에서 같은 Actor generation을 사용하는 target owner가 이전 source fence를 물려받던 Core 결함을 target prepare/activate fence 교체로 수정했고 focused Core test가 통과했다. transfer 중 Core claim callback에서 actor record를 순서대로 backlog에 넣어 elastic executor의 P2/P1/P3 역전을 제거했다. `.artifacts/build/cpp-framework-e2e106`에서 `ST-E1 ST-E2 ST-F1 ST-F2 ST-F3 ST-F4 ST-F5` 조합 실행이 모두 통과했다(`e2e/SpotActorTransfer/logs/20260719-213550-1161479`). 재기동한 provider를 두 번째로 종료한 뒤 같은 C++ client 프로세스가 alternate peer 응답을 완료하는 `RL-B2` 보강 시나리오도 5회 연속 통과했다(`e2e/ResilienceLifecycle/logs/20260719-213411-1155973`부터 `20260719-213506-1160058`까지). Config 10 전체 gate는 아직 남아 있다 |
| S9-C02C | C++ location 기본 정책 연결 | 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 진행 중 | policy-free raw store와 owner lease를 결합하는 `live_location_reader_t` 경계를 unit test에 반영해 location 5개 CTest가 통과했다. Redis·manual peer·startup failure 전체 E2E gate는 남아 있다 |
| S9-C03 | 기존 topology 제거 | alias, runtime, test와 sample no-hit | 완료 | 설치 public header·runtime·contract test에서 `spot_mesh_builder_t`, `spot_drain_policy_t`, `connect_peer_pub`, `spot_node_options_builder_t`, `add_spot_node`와 router·PUB/SUB builder 표면을 제거하고 current `mesh_node_builder_t`, `mesh_peer_connections_t` 계약으로 전환했다. C++ framework·test·sample·E2E 범위에서 legacy topology 이름 scoped no-hit을 확인했다. `.artifacts/build/cpp-framework-e2e106` 전체 target build와 MeshNode vertical·header contract·app host·runtime·stream·install consumer 6개 focused CTest가 통과했고, `RM-C2` targeted RouteMesh E2E도 통과했다(`e2e/RegistryMessaging/logs/20260720-041615-2054592`). 전체 target-contract에는 이 제거 작업과 무관한 기존 증거 누락 3건(`IMP-CP-02`, `E2E-CP-63`, `E2E-CP-53`)이 남아 S9-C05에서 계속 추적한다 |
| S9-C04 | sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 진행 중 | legacy `add_spot_mesh`가 남았던 sample source 14개를 MeshNode 표면으로 전환했다. Bingo 2개, TicTacToe 1개, DeliveryDispatch 5개, GameQuest 2개, ShoppingMall 2개, SupportChat 2개 서버 target은 `.artifacts/build/cpp-framework-e2e106`에서 모두 컴파일·링크됐다. `samples/run_samples.sh` 전체 실행은 아직 남아 있다 |
| S9-C05 | contract·E2E 일반 검증 | CTest와 `e2e/run_e2e_all.sh` 통과 | 진행 중 | Core 10.6 artifact 기준 C++ 전체 target build가 통과했다. MeshNode vertical, target contract, app host, runtime integration, stream framework focused CTest 5/5가 통과했다. Config 10의 과거 `ST-E1` 중단을 해소했고 `ST-E1 ST-E2 ST-F1 ST-F2 ST-F3 ST-F4 ST-F5` 조합 실행이 모두 통과했다(`e2e/SpotActorTransfer/logs/20260719-213550-1161479`). Java형 second-crash alternate peer 검증을 추가한 `RL-B2`는 5회 연속 통과했다. 전체 runner에서 RegistrationCodec은 전부 통과했고 RegistryMessaging은 `RM-C2`까지의 첫 중단을 수정해 focused 시작 순서 3종이 통과했다. SpotService의 Play·Session·Gateway·MultiNode·MultiNodeRequester·Client는 제거된 `enable_server`·`add_spot_mesh` 호출을 current MeshNode builder로 바꿔 모두 컴파일된다. 생성자 의존성이 있는 Spot의 factory overload를 정식 interface부터 복원해 `SM-A1`은 forward·reverse·shuffle에서 통과했다(`e2e/SpotService/logs/20260719-221616-1265087`, `20260719-221647-1266350`, `20260719-221711-1266881`). actor error envelope code를 RouteMesh relay와 actor client가 공용 오류 매퍼로 보존하도록 수정해 `SM-B5`도 세 시작 순서에서 통과했다(`20260719-222156-1275144`, `20260719-222208-1275829`, `20260719-222208-1275828`). `SM-B6`의 첫 RED(`20260719-222240-1279822`)는 Core STREAM socket의 공개 disconnect monitor를 session scope 정리와 연결하지 않았고, 새 MeshNode app wiring에 actor disconnect dispatcher가 없어서 발생했다. Core monitor의 routing ID로 `on_disconnected`를 호출하고, actor 소유 node에 내부 disconnect request를 모든 등록 RouteMesh로 전달해 실제 actor mesh가 lifecycle callback을 처리하도록 복구했다. 고정 300ms 대기를 제거하고 `/evidence/wait`가 `StreamDisconnectNotified`·`StreamUnbound`·`ActorDisconnected`를 기다리게 했으며 forward·reverse·`shuffle:23`이 모두 통과했다(`20260719-230004-1390019`, `20260719-230004-1390033`, `20260719-230004-1390048`). 이어진 `SM-D1` 첫 RED(`20260719-230147-1398832`)의 typed wait decode 예외가 promise를 완료하지 않는 결함을 connector 공통 call 경계에서 `frame_decode_failed`로 완료하도록 수정했다. 두 번째 RED(`20260719-230514-1407440`)는 actor bound-session payload가 JSON serializer 결과인데 remote actor record의 기본 MessagePack codec으로 header를 기록해 client decode가 실패한 것이 원인이었다. Bound-session 송신 codec을 session inbound 상태가 아니라 payload serializer content type에서 결정하도록 정렬했고 forward·reverse·`shuffle:23`이 모두 통과했다(`20260719-231631-1437412`, `20260719-231659-1439551`, `20260719-231659-1439552`). Spot timer `SM-E2`·`SM-E3`·`SM-E4` focused 실행도 모두 통과했다(`20260720-024323-1873328`, `20260720-024338-1873826`, `20260720-024349-1874057`). 최신 focused CTest는 MeshNode vertical·header contract·HTTP·runtime·stream 5개가 통과했고 target contract만 별도 증거 누락 6건으로 실패했다. PubSub은 `PS-A1` 본문 통과 뒤 cleanup process status 1로 runner가 실패했다(`e2e/PubSub/logs/20260719-214444-1180698`). 전체 CTest, sample과 `e2e/run_e2e_all.sh`의 나머지 gate는 남아 있다 **2026-07-20 peer-handover 비교**: C++ MeshNode의 비동기 framework 경계에서 node/channel send·request와 claim recv를 `dontwait`로 정렬했고 `NO_DATA` claim을 release한 뒤 다시 대기하도록 수정했다. 최신 Core local package로 ST-F3~F5는 3회 중 2회 전체 통과(`20260720-045125-2140314`, `20260720-045141-2141544`)했고, 1회는 기존 F3 FIFO 증거 `S3` 누락으로 실패했지만 route 소실·`RequestTargetNotFound`는 없었다(`20260720-045157-2143146`). F5 단독 3회는 모두 통과했다(`20260720-045252-2146576`, `20260720-045258-2148399`, `20260720-045314-2149982`). C++ 전체 target build가 통과했고 doc verifier의 C++ fixture·declaration·transition inventory 실패는 0건이다. `IMP-CP-02`는 stale session disconnect가 새 binding token을 지우지 않는 runtime 회귀를 추가해 닫았으며 target-contract 잔여는 `E2E-CP-63`, `E2E-CP-53` 두 건이다. **2026-07-20 local timeout 재검**: 전용 build 131 target과 focused CTest 17/17(17.90초)가 통과했고 target-contract의 기존 textual gap도 현재 source에서 통과했다. reciprocal two-peer `RM-C2`는 3초 readiness·request 제한을 그대로 유지한 forward·reverse·shuffle에서 10.774/3.974/3.912초에 통과했다(`e2e/RegistryMessaging/logs/20260720-101443-3072684`, `20260720-101453-3073371`, `20260720-101457-3073772`). 같은 MeshName·RID host를 12회 재생성하는 `RL-C1`은 기능 본문은 반복 통과했고 3회 연속 전체 통과도 확인했다(`20260720-101316-3068401`, `20260720-101333-3069375`, `20260720-101351-3070349`). 다만 4회차(`20260720-101407-3071334`)에서는 본문 12/12 이후 3초 SIGTERM 종료 gate가 RED였다. runner의 무제한 `wait`와 정상 종료 중 `ps` 1을 오판하던 `set -e` 경쟁은 bounded 종료·thread wait-state 증거로 고쳤다. 일회성 15초 진단에서도 종료되지 않아 즉시 3초로 복구했다. host trace는 `mesh-host-pump-join-end` 뒤 `mesh-host-node-stop-begin`에서 끝나고 main thread는 `do_sys_poll`, ZLINK I/O thread는 `epoll_wait`에 머물러 Core MeshNode graceful shutdown progress 결함으로 확정했다. timeout으로 가리지 않고 C++ 전체 gate는 열어 두었다. |
| S9-C05A | C++ RL-C1 bounded teardown 회귀 닫기 | timeout 증액 없이 same-RID 재생성과 reciprocal peer churn 뒤 3초 teardown이 5회 연속 통과 | 완료 | 결정적 내부 회귀에서 engine이 제거된 session의 socket→session pipe에 완성 message가 delimiter 앞에 남으면 pipe가 `waiting_for_delimiter`, socket owner가 `term_req_sent1`에 머물고 마지막 term ack가 반환되지 않는 것을 확인했다. engine이 없을 때는 전달할 consumer도 없으므로 session 종료가 undeliverable tail을 버리고 pipe handshake를 완료하도록 고쳤다. 수정 전 회귀는 term ack `1`을 관측했고 수정 후 `test_ctx_destroy` 20/20, socket runtime, MeshNode lifecycle/basic, peer admission 24/24가 통과했다. 부모 process의 미래 high-water mark를 fork한 두 child가 같은 generation을 발급하던 별도 same-RID 회귀도 PID 전환 시 상속 floor를 원자적으로 제거하도록 고정했으며, 강제 미래 floor 회귀 20/20과 peer 전체 3회 연속 통과를 확인했다. C++ RL-C1은 3초 gate 그대로 5/5(`20260720-120327-3484837`, `20260720-120346-3486155`, `20260720-120410-3487276`, `20260720-120425-3488538`, `20260720-120443-3490145`)와 최종 shared runtime 3/3(`20260720-122932-3590304`, `20260720-122958-3592014`, `20260720-123009-3593759`)이 통과했다. 임시 own/pipe 계측은 제거했다. |
| S9-C06 | C++ guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 진행 중 | 01~06장은 MeshNode·ChannelName 중심 설명과 `add_route_mesh(...)` 예제를 포함한다. 그러나 `README.ko.md`에는 제거된 `.add_node(...).enable_router(...)` 예제가 남아 있고, 목차와 본문이 아직 존재하지 않는 07~16장 문서 열 개를 링크한다. sample·E2E 일반 gate 통과 뒤 이 장들을 현재 공개 계약과 실제 sample에 맞춰 완성해야 한다 |

### 13.3 Java/Kotlin lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-J01 | bindings local package 10.3.2 pin | version catalog와 dependency resolve 확인 | 완료 | Core 10.3.0 base 초기화 뒤 Java binding의 MeshNode routing ID lifecycle API 수정으로 package patch를 10.3.1로 올렸다(`ba25b8d04`). 10.3.1 JAR의 native payload가 이전 Core 10.1.0으로 남은 것을 artifact 직접 검사로 발견해 Java package만 10.3.2로 다시 올렸다(`4770678e7`). 10.3.2 JAR SHA-256 `3796f36b…cdb4`, 내부 `libzlink.so.10.3.0` SHA-256 `6b8b0bf2…07ed4`가 Core build와 일치하며 framework pin도 10.3.2다 |
| S9-J02 | Java RouteMesh/MeshNode 구현 | Java 정식 interface와 source 일치 | 진행 중 | `ffa53a936`: 정식 `addRouteMesh` 구성, MeshNode lifecycle·peer·channel runtime, callback wakeup과 단일 pull-dispatch pump, claim·reusable batch·207 재할당·retained Message 수명, monitoring snapshot을 구현했다. 후속 작업에서 STREAM actor dispatch와 Spot/Actor lifecycle을 정식 MeshNode에 연결하고 callback domain coalescing·LEFT lifecycle wakeup·transfer backlog 직렬화 deadlock·MeshNode lookup의 빈 결과 해석을 수정했다. Java SpotService의 Play·Gateway를 정식 MeshNode로 전환했으며 SM-G1에서 play-a 강제 종료, play-b 생존 요청, 동일 endpoint·RID 재기동과 재기동 node의 actor 요청·응답 완료를 확인했다. Binding perf의 남은 SpotNode 기반 single/multi Spot benchmark를 정식 MeshNode pull dispatch로 전환했으며 binding root `./gradlew test` 17-task가 통과하고 perf tree의 제거 API scoped no-hit를 확인했다. Framework root `./gradlew test` 46-task도 통과했다. 전체 E2E와 legacy topology 소비자 전환은 남았다 |
| S9-J03 | Kotlin interface와 DSL 구현 | Kotlin 정식 interface와 source 일치 | 진행 중 | `ffa53a936`에서 Kotlin `addRouteMesh` DSL 첫 slice와 전체 Kotlin compile을 통과했다. Java runtime 전환과 함께 contract snapshot·E2E 검증이 남았다 |
| S9-J03A | JVM metadata·timer 연결 | Java/Kotlin S/S metadata 전체 공통 matrix와 `ScheduledExecutorService` timer가 immutable context·keyed scheduler 계약 통과 | 완료 | Worker timeout·caller cancellation·pool shutdown과 Kotlin coroutine cancellation을 공통 `ZLinkWorkerCancellation`에 연결했고, immutable `ZLinkTimerOptions`와 같은 key의 이전 generation 취소·late callback 억제를 contract test로 고정했다. STREAM session↔Actor metadata는 방향별 허용 목록, 기본 drop, immutable handler snapshot과 reply 비상속 계약을 적용했다. 이어 Core metadata view를 Java binding의 MeshNode·Channel·Spot send/request와 logical multicast public overload에 연결하고, framework의 copy-on-write metadata builder와 1,024-byte canonical codec, malformed/duplicate/trailing UTF-8 검증을 구현했다. Node·Channel·Spot direct·logical multicast handler는 immutable snapshot을 받고 기존 두 인자 Spot handler는 source-compatible context overload로 확장했다. fake backend 전체 86/86, binding root 17-task와 framework root `--refresh-dependencies test` 46-task가 통과했다. local package는 version을 바꾸지 않고 provisional `10.6.3`으로 재생성했으며 Core·전체 bindings의 `10.7.0` reset은 S9 전체 gate 고정 뒤 일괄 수행한다. Peer handover 비교를 위해 Java SpotActorTransfer가 같은 node 집합에서 `ST-F3,ST-F4,ST-F5`를 연속 실행하도록 runner를 보강했고 3회 모두 통과했다(`e2e/SpotActorTransfer/log/20260720-042515-2075461`, `.../20260720-043126-2089566`, `.../20260720-043226-2092636`). Redis owner 갱신 최대 관측 간격은 각 4.530초·5.001초·4.508초였고 두 번째 실행은 세 owner가 함께 약 5초 지연된 뒤 4.33초 간격으로 따라잡았지만 route peer 소실·`RequestTargetNotFound`는 없었다. RL-C1과 SM-G1도 monitor를 켠 비교 실행에서 통과했다(`ResilienceLifecycle/logs/20260720-042729-2080592`, `SpotService/logs/20260720-042953-2086647`). |
| S9-J03B | JVM Actor transfer authority 연결 | Java/Kotlin store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 진행 중 | Core 정식 `04-actor.ko.md`의 prepare·commit·activate·abort 계약을 Java binding public surface에 투영했다. Java binding RED는 transfer type `ClassNotFoundException`으로 고정했고, 구현 뒤 전용 contract test와 binding root 17-task가 통과했다. framework는 source/target Core token과 membership epoch를 authority commit에 연결했다. 이 과정에서 Core fence가 일반 Actor ingress를 무조건 `EAGAIN`으로 거부하는 계약 불일치를 RED로 재현하고, bounded private participant·ACK·seal·target staging과 source commit 뒤 stale ActorRef forwarding route를 Core 내부에 구현했다. Core `test_mesh_peer_admission` 20/20과 Java ST-F1~F6이 통과했다. Java package는 provisional `10.6.3`으로 검증했으며 Core·전체 bindings의 `10.7.0` reset은 S9 전체 gate 고정 뒤 일괄 수행한다. Store CAS·lease·복구·startup capability 전체 gate는 남아 있다. |
| S9-J03C | JVM location 기본 정책 연결 | Java/Kotlin 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 진행 중 | 정식 Java/Kotlin exact interface와 현재 location runtime·Redis extension·manual peer·startup validation·test-only in-memory 경계를 대조했다. Root exact interface에 없는 `useInMemoryLocationStores()`와 implicit in-memory store 생성 경로를 제거하고 contract test를 no-method gate로 바꿨다. Runtime test는 test-only `ZLinkInMemoryLocationStore`를 `addLocationStore(...)`로 명시적으로 주입하며, store 없는 manual MeshNode peer 등록은 기존 focused test가 허용한다. `git diff --check`는 통과했다. focused Gradle test는 이 변경 전부터 함께 진행 중인 J02 binding 연결에서 `MeshNode.joinActorSpot`, `joinActorEntrySpot`, `leaveActor`, `closeActorBoundSession` 네 symbol을 찾지 못해 `compileJava`에서 차단됐다. 공식 Redis option이 exact `connectionString(...)`·`keyPrefix(...)`가 아닌 구형 setter 표면이고 store 구현도 구형 peer/route schema에 남아 있어 완료 처리하지 않는다. |
| S9-J04 | 기존 topology 제거 | Java/Kotlin alias, runtime, test와 sample no-hit | 미착수 | - |
| S9-J05 | Java/Kotlin sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 진행 중 | 공통 sample §13의 TicTacToe 수동 등록 규약을 기준으로 Java no-scan 상태를 재확인하고 Kotlin의 `addHandlersFromPackageOf`·handler group 자동 등록을 제거했다. Kotlin API·Play request handler와 Entry Spot·game Spot handler를 구성 지점에서 직접 등록했으며, suspend channel handler는 정식 `ZLinkSuspendingRequestHandler`를 구현하도록 정렬했다. focused contract는 RED 뒤 12-task green이고 Java/Kotlin TicTacToe source의 scan 호출은 scoped no-hit다. 실제 Kotlin `run_sample.sh`에서 제거된 legacy SpotNode 기동 실패가 확인되어 sample을 `addRouteMesh(...).listen(...).channelName(...).peerConnections()`와 STREAM `enableActorDispatch(meshName)`로 전환했다. 이후 2개 Play·2개 API 기동, create/auth, 양 node join, cross-node Actor transfer와 첫 `PlaceMarkReq/Res`까지 진행했으나 remote bound-session의 `GameStateNotify`가 guest stream에 도착하지 않아 client timeout과 cleanup deadline 실패로 끝났다(`/tmp/tmp.gorrn2SOG9/logs`). 같은 실패는 현재 framework source에서도 `/tmp/tmp.Or9WMBCJTz/logs`로 재현했다. target은 `play-node-1`의 `player-o` generation `1784491461998439`에서 source node `play-node-2`, session `2`로 259-byte frame을 Core에 제출했고 submit은 accepted였지만 guest stream은 수신하지 못했다. 실행에 사용된 provisional Java `10.6.3` JAR 내장 Core SHA-256은 `345610491e3073f8984a3e6c8bf4eac4cd3b22a117aa1fb1d1cc4d8edb33d755`로, target transfer commit에서 bound-session reverse route를 설치하는 현재 source와 그 회귀(`test_remote_actor_transfer_fence`)가 반영되기 전 산출물이다. 현재 Core 21/21과 C++·.NET ST-E1 성공 증거에 따라 새 framework 우회는 추가하지 않았다. **fresh Core 재검(2026-07-20)**: Java package를 다시 만들지 않고 `ZLINK_LIBRARY_PATH=/home/hep7/project/kairos/zlink/core/build/lib/libzlink.so`를 지정해 Kotlin 실제 sample을 실행했다. 실행 직전 runtime SHA-256은 `3eaad86dc1cb93c9189db94c2de26fdf10efe158761270f196622a2e92aa8656`이었다. target의 `target_session_bound`·`location_committed`와 첫 `PlaceMarkReq` 응답까지 확인됐지만 `GameStateNotify`는 guest stream에 도착하지 않았고 client가 다시 timeout으로 종료했다(`/tmp/tmp.CQzGkJLD6H/logs`). 따라서 오래된 10.6.3 JAR native가 단독 원인이라는 가설은 반증됐다. **첫 분기점 확정(2026-07-20)**: Java source prepare의 peer는 target node이고(`ZLinkActorSpotJoinCall.java:480-496`), target prepare의 peer와 native bound-session route는 각각 source actor node와 source session RID를 그대로 사용한다(`ZLinkActorSpotAdmission.java:333-356,442-455`; `ZLinkActorRuntime.java:1428-1457`). target Core commit은 reverse route를 source peer로 설치한 뒤(`mesh_transfer_api.cpp:1197-1208`) joined callback을 실행하며, Kotlin game callback은 이 구간에 기존 player에게 `GameStateNotify`를 보낸다(`TicTacToeGame.kt:101-118`). target submit은 성공하지만 source STREAM binding은 source commit 전 `transfer_serial != 0`이므로 `zlink_mesh_node_actor_send_bound_session()`이 `EAGAIN`을 반환하고(`mesh_stream_session_api.cpp:1395-1434`), wire ingress가 반환값을 확인하지 않은 채 parts를 닫아 message를 유실한다(`mesh_wire_ingress.cpp:1202-1206`). 기존 Core 회귀는 source commit 뒤에만 target push를 제출하므로 이 창을 검증하지 않는다(`test_mesh_peer_admission.cpp:3249-3261,3444-3475`). deterministic RED는 target activate 직후·source commit 신호 전 `target-bound-precommit`을 제출하고, source commit 뒤 같은 client가 이를 한 번 수신함을 검사해야 한다. Java framework 우회는 추가하지 않는다. **Core·JVM 실제 재검 완료(2026-07-20)**: bounded reverse FIFO가 반영된 공식 `core/build` runtime SHA-256 `671fc61d...2b33ffe`와 Java bindings source를 package 발행 없이 임시 composite build로 연결했다. Java formal interface의 `publish(channelName, topic, message)`와 달리 구현이 mesh 이름을 channel로 전송하던 오류를 수정해 observer multicast가 `play` channel의 양쪽 Spot에 전달되도록 맞췄다. 이어 target으로 이전된 `player-o`가 Java에서 `createActor()`가 반환한 local `Actor` facade가 없다는 이유로 leave·destroy cleanup에 실패하던 결함을 확인했다. Core C API와 Node binding처럼 Java `MeshNode`에 `ActorRef` 기반 join·leave·bound-session close를 제공하고 framework binding adapter가 이 표면을 사용하도록 수정했다. Java binding unit 74/74, framework core 309/309, fake backend 86/86과 Javadoc gate가 통과했다. 실제 Kotlin runner는 `/tmp/tmp.akpAZJppon/logs`에서 observer 승리 milestone, `player-o`·`player-x` destroy 완료를 모두 확인하고 `PASS TicTacToe.Kotlin`으로 종료했다. package와 version은 올리지 않았으며 나머지 `samples/run_samples.sh` 전체 gate는 계속 진행한다. |
| S9-J06 | contract·E2E 일반 검증 | Gradle, `framework/languages/java/e2e/run_e2e_all.sh`와 `framework/languages/java/e2e-kotlin/run_e2e_all.sh` 모두 통과 | 진행 중 | Java `SpotService/run_e2e.sh SM-G1` 통과: 다중 peer에서 play-a를 종료한 동안 play-b 요청이 성공하고, 동일 endpoint·RID로 play-a를 재기동한 뒤 새 stream 연결의 auth·join·actor echo 응답이 완료됐다. 공통 SM-G1의 두 번째 복구 경로가 기존 Java 시나리오에서 빠진 것을 보완해, 재기동 검증 뒤 play-a를 다시 강제 종료하고 계속 실행 중인 gateway가 play-b의 `room-b`에 public Spot request를 보냈다. play-b handler reply와 gateway `REPLY_RECEIVED`가 모두 확인되어 shared Core의 동일 조건에서는 caller completion 유실이 재현되지 않았다. Formal Core transfer lifecycle을 사용한 `SpotActorTransfer/run_e2e.sh` ST-F1~F6이 모두 통과했다. ST-F1~F3은 moving 중 Core private participant에 수용된 Actor·bound-session traffic의 FIFO target dispatch를 확인했다. ST-F4·F5는 source commit 뒤 Core forwarding route가 framework의 설정된 window 동안 old ActorRef traffic을 한 번만 target으로 전달하고, window 종료 뒤 cleanup 호출이 route를 제거하는 것을 확인했다. ST-F6은 target에서 `ProbeReq`를 수신·응답하고 source에서는 같은 request를 application handler로 다시 dispatch하지 않았으며, caller correlation·normal timeout·late handler 무시도 확인했다. target ReplyToken은 Core 정식 계약대로 target에서 reseal되고 transport relay는 Core 내부에서 처리되므로 framework reply 우회 표면을 추가하지 않았다. Core `test_mesh_peer_admission` 20/20, Java binding root 17-task, Java/Kotlin framework root 46-task가 통과했다. Java·Kotlin sample 전체 141-task도 이전 gate에서 통과했다. Core weighted load balancer가 transient write failure를 peer weight 0으로 바꿔 재활성화를 영구 차단하던 결함을 focused RED(`expected 100, was 0`)로 고정했고, configured weight를 보존한 채 pipe만 inactive로 전환하도록 수정했다. `test_router_multiple_dealers` 5/5와 `test_mesh_peer_admission` 21/21이 통과했으며, 이후 shared runtime과 provisional Java `10.6.3` Maven package 내부 native SHA-256은 `2cacc5d2b481bf234c5a41113c2415dbe0602b1f93d1507fa9f2f10e65b058a4`로 일치했다. 이 runtime에서 Kotlin `DiscoveryRegistryHa` SF-D1·SF-D2가 단독 순서 실행으로 통과했고 SF-D2는 별도 집중 실행도 통과했다. Store 복구 직후 첫 snapshot이 비어 있을 수 있는데 한 heartbeat 뒤 기존 연결을 끊던 auto-connect 경합은 virtual-clock RED로 고정하고, 소유자가 lease를 다시 게시할 수 있도록 recovery defer를 `max(heartbeat, owner lease TTL)`로 보정했다. 해당 test class와 framework root 46-task가 통과했으며 Java `StoreFailure` SF-D2 집중 실행도 통과했다. Native `SPOT_SEND` record에도 0 operation ID가 존재할 수 있는데 Java가 이를 request sequence로 해석해 send를 request·reply-error로 오분류하던 결함은 record kind를 함께 검사하도록 수정했고, focused unit과 SpotService SM-E1이 통과했다. 또한 `addRouteMesh(...).channelName(...)`으로 만든 native MeshNode 채널에서 public `requestToNode`·`sendToNode`가 legacy route socket만 찾던 누락을 native node 경로에 연결했으며 SpotService SM-F1~F4가 통과했다. 수정 후 framework root `--refresh-dependencies test` 46-task가 통과했다. Java RegistrationCodec·RegistryMessaging 전체와 PubSub PS-A1~B1·C1, PS-B2 집중 검증도 통과했다. RuntimeMonitoring fixture의 channel membership 누락은 정식 Core 계약에 맞게 보정했다. 이어 Java exact spec의 `ZLinkRouteMeshRuntime` snapshot·bounded observer event·readiness·drain 타입과 Spring DI, `ZLinkRouteMeshRuntimeOptions`의 live max-message-size·channel weight 표면을 구현했고 fake-backend unit과 starter 자동 구성 test를 추가했다. Core가 아직 노출하지 않는 snapshot 세부 값은 gap §12.38에 한정해 기록했으며 canonical Config 7 Java/Kotlin port와 stable Core package 기반 실행은 남아 있다. 구형 AutomaticTurnDispatch ATD-A1은 `submit` 중 probe 실행을 기대해 실패했지만 현재 Config 8 정식 계약은 `async`가 turn을 유지하고 `yield`만 반납한다고 고정하므로 runtime 결함으로 처리하지 않았다. ResilienceLifecycle 전체 재실행은 RL-A1·C3·A2·A4·A5 뒤 RL-B1 follow-up에서 한 번 HTTP 500이 발생했으나 RL-B1 집중 재실행은 통과했다. 전체 Java/Kotlin runner는 병렬 부하에서 각각 Java SF-C2 HTTP EOF와 Kotlin SF-D1 `NOT_ADMITTED`가 발생했고, Kotlin 단독 전체 config는 SF-D1·SF-D2를 통과한 뒤 SF-E1의 동시 location 조회가 HTTP 5초 timeout으로 중단됐다. Java 단독 전체 config는 SF-A1~D1을 통과한 뒤 SF-D2에서 9.15초 successful-traffic stall이 한 번 재현되어 전체 gate는 남아 있다. **Config7 추가 증거(2026-07-20)**: Java binding monitor가 상태 변경이 아닌 이벤트의 `mesh_state=0`을 enum으로 변환하던 결함을 수정하고, native monitor status의 backpressured 누계를 snapshot에 연결했다. application claim은 handler completion까지 유지하며 infrastructure claim을 계속 진행하도록 dispatch completion 소유권을 정렬했다. Java binding 17-task와 Java/Kotlin framework 46-task가 통과했고, provisional 10.6.3 isolated Maven package로 `RuntimeMonitoring/run_e2e.sh all`의 MON-A1~A5·B1·B2·C1·D1 9개가 모두 통과했다(`logs/20260720-013848-1763980`). feature map에는 아직 A4·D1과 B1·B2 target 세부 snapshot, C1 claim event·명시적 sequence gap을 완료로 과장하지 않고 남겼다. **Core 경계 비교 재검(2026-07-20)**: Java RL-C1을 3회 반복해 매회 13개 request/reply가 api-a·api-b 양쪽에 분산되고 follow-up까지 완료됨을 확인했다(`ResilienceLifecycle/logs/20260720-031329-1926960`, `.../20260720-031344-1928138`, `.../20260720-031356-1929231`). 같은 endpoint·RID 재기동과 다중 peer 생존 경로는 `ZLINK_ROUTER_DEBUG=1 SpotService/run_e2e.sh SM-G1` 3회에서 모두 통과했다(`SpotService/logs/20260720-031425-1930491`, `.../20260720-031514-1932132`, `.../20260720-031558-1933566`). 각 실행에서 reciprocal connector의 물리 방향 교체가 `replace duplicate rid=play-a existing_local=1 new_local=0`으로 두 차례 기록됐고, 재기동 뒤 actor reply와 play-b의 두 생존 request가 모두 완료됐다. 새 public API나 test 전용 runtime 우회는 추가하지 않았다. |
| S9-J06A | Java SM-G1 owner lease evidence 정렬 | 전역 기본값·메시징 timeout을 바꾸지 않고 crash owner 만료를 관찰한 뒤 동일 RID 재기동과 양쪽 request/reply 완료 | 완료 | **RED→GREEN(2026-07-20 17:28 KST)**: location 전역 기본값이 heartbeat 10초·owner lease 30초로 정렬된 뒤에도 runner가 crash 후 20초를 고정 대기해 첫 `play-a` 재기동의 `room-a` 신규 claim이 기존 owner와 충돌했다(`SpotService/logs/20260720-171032-4157404`). 전역 기본값과 request/reply timeout은 바꾸지 않고 Play 역할의 typed E2E 설정에서 public `configureLocations()`로 heartbeat 500ms·lease 5초를 명시했다. 고정 `sleep 20`은 crash 전에 public location store로 확인한 owner의 Redis lease PTTL 만료 gate로 교체했으며, polling 상한은 lease와 heartbeat의 합 5.5초를 100ms 간격 55회로 환산한다. 현재 Core SHA-256 `a58c248d...a5b5c`와 격리 candidate binding JAR SHA-256 `8a83a348...f150` 조합에서 재기동 actor request/reply와 두 번째 crash 뒤 survivor request/reply가 모두 통과했다(`SpotService/logs/20260720-172819-4187639`, `/tmp/s8-09a-java-sm-g1-lease-evidence-green.log`). version 변경과 local package 배포는 수행하지 않았다. |
| S9-J07 | Java/Kotlin guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 미착수 | - |

### 13.4 Node.js lane

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-N01 | bindings local package 10.6.0 pin | package와 lockfile이 같은 version을 resolve | 진행 중 | workspace와 framework package, lockfile은 local `@zlink-systems/zlink` 10.6.0을 resolve한다. `zlink_stream_detach`는 정식 Core header·spec·export ABI가 아닌데 addon이 직접 선언한 비공개 의존이었다. addon close를 공개 `zlink_close()` 성공 뒤 TSFN·slot을 정리하도록 고치고 같은 source에서 tgz를 다시 만들었다. local LEFT·JOINED lifecycle 수정까지 포함한 최신 tgz SHA-1은 `2e1746e4…f00e13`, SHA-256은 `f51421b7…3835a`이고 addon SHA-256은 `a15b1631…01bdbab`이며 addon의 undefined symbol에서 `zlink_stream_detach`가 사라졌다. framework clean install과 native STREAM child 32/32가 통과했지만 version bump와 최종 packaged consumer 검증 전에는 완료하지 않는다 |
| S9-N02 | RouteMesh/MeshNode interface 구현 | Node 정식 interface와 source snapshot 일치 | 진행 중 | `addRouteMesh` registration, MeshNode lifecycle·pull dispatch pump, 실제 Core Spot lifecycle generation을 location row와 route target에 전달하는 경계, completion-table Actor join, Entry·User Spot dispatch, native bound-session adapter를 구현했다. Core `RoutingId`는 표시 문자열로 재해석하지 않고 opaque 값으로 보존한다. 제거된 local-first Actor join coordinator와 그 전용 fixture 4건을 제거한 현재 기준에서 `npm run build`와 backend·Nest·actor·stream 계약 212/212가 통과했다. `ZLinkRequestCall`·`ZLinkPublishCall`의 metadata·admission·yield와 `ZLinkChannelClient(meshName, channelName, ...)`, `setActorTransferTimeout(...)`, STREAM `enableActorDispatch(meshName)`를 정식 declaration과 runtime registration에 연결했다. focused call 계약 10/10과 transfer timeout builder 계약이 통과했다. exact catalog의 현재 첫 잔여는 구 Spot monitoring의 `SubjectsChanged`·Spot snapshot을 정식 Mesh snapshot으로 바꾸는 작업이며, 이후 Spot actor handler의 구형 `spot` 인자와 소비자 전환도 남아 있다. durable transfer와 남은 legacy public builder·sample/E2E 전환은 별도 행에서 계속 추적한다 |
| S9-N02A | Node metadata·timer 연결 | S/S metadata 전체 공통 matrix와 `setTimeout` timer가 immutable context·generation·cancel·keyed scheduler 계약 통과 | 진행 중 | handler metadata가 실제 mutable `Map`을 노출하던 문제를 private immutable `ReadonlyMap` snapshot으로 수정해 source 변경·runtime mutation·snapshot 재복사 집중 계약 3/3이 통과했다. timer registry는 같은 key를 `Set`에 중복 등록하던 구현을 generation별 단일 entry로 바꾸고, 재등록·cancel 뒤 Spot serial queue에 남은 이전 generation callback을 실행 직전 차단한다. Node 전체 build와 tick·close·validation·overrun·failure·key replacement 집중 계약 6/6이 통과했다. Node·Channel·Spot direct·Logical Multicast 전체 metadata E2E matrix는 남았다 |
| S9-N02B | Node Actor transfer authority 연결 | Node store interface의 CAS·lease·복구·startup capability와 distributed transfer E2E 통과 | 진행 중(집중 transfer gate 통과, 전체 gate 잔여) | exact `ZLinkActorTransferStore`의 in-memory·Redis 원자 전이에 Core `TransferControl` consumer를 연결했다. Target fence는 source Actor generation·membership epoch를 검증해 prepare하고 commit·activation·abort를 durable state로 투영한다. Foreign recovery lease가 유효하면 결정을 유지하고 만료 뒤 successor가 takeover하여 prepared→committed→activated를 이어간다. Transfer adapter가 있는데 durable authority capability가 없거나 production in-memory store를 선택한 구성은 startup에서 실패한다. 최신 Core의 local LEFT·JOINED record를 Node lifecycle에 연결했고 Entry Spot activation을 MeshNode 시작 중 미리 만들어 actor 생성 직후 `onCreateActor`가 빠지는 race를 제거했다. 원격 formal join은 공개 API를 추가하지 않고 기존 framework 전용 opaque payload에 actor type·create request·adapter state를 실어 target에서 private materialization한 뒤 admission을 수행한다. Target admission은 materialization보다 먼저 수행하고, JOINED record의 Spot generation·membership epoch로 target location을 claim한 뒤 target node ActorRef를 bind한다. Source 성공 회신은 Core terminal ack와 target location 관찰 뒤에만 반환한다. Transfer adapter lookup은 등록 factory type을 기준으로 고쳐 실제 application actor의 constructor와 등록 key가 달라도 state transfer를 수행한다. 최신 독립 실행에서 ST-A1·A2·A3, ST-B1·B2·B3·B4, ST-C1·C3, ST-D1·D2, ST-F1·F2·F4·F6이 통과했다. Backlog는 snapshot 이전 send-only packet만 target에서 재생하고 request는 Core direct forwarding으로 reply correlation과 caller timeout을 유지한다. Core bound-session route는 binding generation을 유지하면서 현재 local Actor generation을 검증하도록 고쳤고, Node는 Core ingress의 실제 local Spot materialization만 재귀 forwarding에서 제외한다. 최신 Core SHA `a1d81288b5a7724eca7ed4668422d48b36f8e5d553b2f19858231ba647e205f4`에서 ST-F3 `log/20260720-000458-1541891`, ST-E1 `log/20260720-000530-1545252`, ST-C2 `log/20260720-000541-1546853`, ST-F5 `log/20260720-000555-1547847`이 모두 통과했다 |
| S9-N02C | Node location 기본 정책 연결 | 공식 Redis extension, manual peer, location store 미등록 시 분산 location startup failure와 test-only in-memory 경계가 정식 interface와 일치 | 진행 중 | Actor·Spot public row의 exact 필드를 필수 값으로 전환하고 Actor key를 `(MeshName, ActorId)`로 고정했다. Core Actor join completion과 transfer target `actorLookup`의 generation·epoch를 row writer까지 전달하며 같은 Spot RID 재생성의 stale row를 거부한다. in-memory와 Redis에 MeshNode descriptor store를 구현하고 공용 `mesh-node-descriptor-v1.json`, `actor-location-v2.json` fixture와 byte 단위 일치를 확인했다. `ZLinkLocationStore`는 정식 계약의 MeshNode·Spot·Actor·owner lease·Actor transfer store만 합성하며 `removeAllByOwner`는 `bigint`, Actor·Spot 제거는 write status를 직접 반환한다. 운영 pagination은 public store 요구에서 분리한 내부 query capability가 소유한다. 후속 exact RED에서 Actor row에 남은 store fencing `generation`과 중복 `nodeRid`·`locationKind`·`spotMeshName`을 확인했다. public row에서는 이를 제거하고 lifecycle이 store generation을 별도로 추적하며 in-memory·Redis store가 generation을 내부 metadata로만 보관하도록 수정했다. resolver filter는 `ownerNodeRid`와 `spotKind`를 사용하고 Redis JSON은 공용 `actor-location-v2.json`과 같은 exact field만 왕복한다. 최신 `npm run build`, contract surface·in-memory·Redis·runtime·filter·transfer 집중 계약 59/59와 Actor manager 65/65가 통과했다. exact catalog는 store 불일치를 해소하고 다음 잔여인 `ZLinkLocationAutoConnectType`의 제거 topology 값 불일치를 보고하므로 S9-N03에서 계속 전환한다. 기존 peer·route store 공개 제거와 tree no-hit은 S9-N03, 새 native package가 필요한 실행 검증은 S9-N01·N05에서 계속 추적한다 |
| S9-N03 | 기존 topology 제거 | alias, runtime, test와 sample no-hit | 진행 중 | `ZLinkLocationAutoConnectType`을 `Invalid=0`, `RouteMesh=1`, `Fanout=2` exact 값으로 고정하고 framework·Nest source에서 기존 topology builder와 runtime 명칭을 제거했다. sample·E2E·cross-language의 직접 사용도 정식 `addRouteMesh`·`channelName`·`peerConnections` 경로로 옮겨 해당 source 범위 no-hit과 package build, contract surface·ZoneWorld 집중 33/33을 통과했다. 실제 TicTacToe는 양쪽 Play node의 `ready`·`spotPeerReady`와 API→Play `game-created`까지 도달했으나 cross-node `ObserveMilestoneReq`가 timeout되어 완료 gate는 열어 두었다(`/tmp/zlink-tictactoe.ts-KnOd7J`). Nest 전체 회귀와 나머지 sample에는 RouteMesh의 필수 listen/routing id 및 갱신된 handler 계약을 반영해야 하는 잔여가 있어 version·package 배포는 진행하지 않는다 |
| S9-N04 | sample 전환 | inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 진행 중(Core lifecycle gate) | 새 native package에서 Chromium TicTacToe의 인증 3개, observer subscription과 host join은 통과한다. guest의 다른 Play node에서 room owner node로 향하는 formal Core join은 target `onActorJoin` 전에 reject되어 sample이 중단된다. rejection reply가 없을 때 `reply.error`를 읽던 sample 오류는 optional reply 처리로 고쳐 실제 reject 원인을 보존한다. Bingo의 formal Mesh peer `endpoint`가 기존 Node monitoring event의 `peerEndpoint`로 전달되지 않던 변환 결함을 수정해 실제 endpoint marker를 확인했다. slot 2 교체 노드가 slot 1 생존 노드에 연결하는 RID 방향에 맞춰 handoff 대기 주체도 교체 노드로 바로잡았다. 이후 반복 실행에서 교체 노드가 같은 slot·새 generation으로 시작한 뒤 생존 peer를 다시 연결하지 못하는 경우가 남아 `/tmp/zlink-bingo.ts-Wwklun`에서 중단된다. Stream Connector package root는 정식 계약대로 browser ESM-only로 유지하고, Node에서 실행하는 sample client만 build 단계에서 connector source를 CommonJS bundle하도록 분리했다. 이 경계에서 ShoppingMall 실제 runner가 전 구간 통과했다. ZoneWorld는 CJS export 오류 없이 G1~G4를 통과한 실행이 있었지만 `/tmp/zlink-zoneworld-Z2YUrF`에서는 최초 node의 lease가 사라져 다음 node가 같은 slot 1을 취득했고 G2에서 중단됐다. full sample gate는 이 Core·heartbeat blocker 뒤 다시 실행한다 |
| S9-N05 | contract·E2E 일반 검증 | build·test와 `e2e/run_e2e_all.sh` 통과 | 진행 중(집중 gate 통과, 전체 gate 잔여) | 전체 Node build, Actor handoff 계약 8/8, Core-ingress owner 집중 계약 1/1, bound-session 경로 집중 계약 3/3이 통과했다. SpotActorTransfer 최신 증거는 ST-B3 `log/20260719-221312-1259665`, ST-B4 `log/20260719-221357-1261572`, ST-F1 `log/20260719-215415-1208399`, ST-F2 `log/20260719-215344-1207122`, ST-F6 `log/20260719-215438-1209549`, ST-F3 `log/20260720-000458-1541891`, ST-E1 `log/20260720-000530-1545252`, ST-C2 `log/20260720-000541-1546853`, ST-F5 `log/20260720-000555-1547847`이다. 같은 Core artifact에서 `test_stream_socket` 12/12, `test_asio_ws` 12/12, `test_mesh_peer_admission` 21/21도 통과했다. Broad actor-manager·spot-manager 동시 실행에서는 오래된 mock과 제거된 legacy relay 기대가 formal dispatcher 전환을 아직 반영하지 않아 20건이 실패했으며, 이번에 선별한 formal 계약은 모두 통과했다. Route target에 실제 Core Spot lifecycle generation을 전달하고 host-level Spot send/request를 MeshNode completion 경로에 연결한 뒤 Node build, Spot manager와 connector 회귀 54/54, routing-id allocation 10/10, Redis allocation 1/1이 통과했다. message가 없는 continuous ready residue도 batch마다 timer에 제어를 넘기며 두 timer-fairness 회귀가 통과한다. lease renew 실패 여부와 무관하게 monotonic fencing deadline을 넘긴 runtime은 늦은 renew 성공 전에 fence하도록 고쳐 stale owner 부활을 차단했다. focused location·RID 계약 43/43, exact call 계약 10/10, Node build와 framework doc verifier가 통과했다. ZoneWorld는 구 client-server topology와 stale 2-argument channel/publish 호출을 제거하고 3초 owner lease를 유지한 채 sample build가 통과했지만, 실제 runner 반복과 전체 E2E는 official Core `core/build` NO-GO가 해소될 때까지 실행하지 않는다 |
| S9-N06 | Node.js guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 진행 중(Core·location gate) | 제거된 언어별 장별 guide를 복구하지 않고 현재 문서 정책에 따라 Node README를 정식 공통 spec·Node exact interface·실행 가능한 sample의 사용 안내 index로 정렬했다. internals의 구 SpotMesh·client/server topology, 구 handler signature, stale lifecycle 기동·종료 순서를 현재 MeshNode·ChannelName·immutable lifecycle 계약과 실제 runtime 순서에 맞췄다. Node 문서 회귀 17/17과 package verifier 전단이 통과했으며 전체 sample gate의 Core·location 결함이 해소된 뒤 최종 사용 안내를 다시 검증한다 |

**S9-N04·S9-N05 Node pump 재검(2026-07-20)**: Bingo의 두 번째 원격 Actor
join은 source leave를 완료했지만 target의 terminal acknowledgement가 request timeout으로
종료됐다. Core timeout을 늘리지 않고, handler가 같은 MeshNode의 후속 record를 기다리기 직전에
ready callback이 도착하는 순서를 fake backend로 고정했다. 수정 전 새 회귀는 1.004초에 RED였고,
pull-dispatch pump가 이미 누적된 ready domain을 handler await 전에 별도 drain으로 예약하도록
수정한 뒤 Node 전체 build와 pump fairness·재귀·누적 ready 집중 계약 4/4가 통과했다. 실제
`Bingo.Ts` 재실행과 전체 sample·E2E gate는 고부하 실행 조율 뒤 계속한다.

**S9-N02·S9-N04 timeout·공개 표면 재검(2026-07-20)**: 로컬 기능 실패를 긴 대기로
가리지 않도록 Bingo server/client의 개별 request timeout을 20초에서 3초로 되돌렸다. user Spot이
actor를 내보내는 sample은 Spot context의 비정식 `leaveActor(actor)` 대신 정식 actor context의
`leaveSpot()`을 사용하고, Node public `ZLinkSpotContext`에서도 `leaveActor`를 제거했다. 일반 Actor
reply는 metadata setter를 제공하지 않는 공통 message 계약에 맞춰 compression만 노출하고 metadata는
빈 snapshot으로 전달한다. stale Entry Spot actor request fixture도 정식 3-argument handler 계약으로
고쳐 local response gate 1/1을 통과했다. 전체 Node build와 관련 sample lifecycle 2/2, worker·pump·
immutable metadata·timer generation·reply option 집중 계약 16/16이 통과했다. exact interface gate의 다음 잔여는 현재
live actor 객체를 받는 `ZLinkSpotActorLifecycle`을 정식 `ActorRef`·membership epoch snapshot 계약으로
전환하는 작업이며, runtime과 sample·E2E 소비자를 함께 바꾸기 전까지 완료로 판정하지 않는다.

**S9-N02·S9-N05 Actor lifecycle snapshot 진행(2026-07-20)**: Node public lifecycle을
`ZLinkActorJoinRequest`와 `ZLinkActorMembership`의 immutable snapshot으로 바꾸고, local·native·remote
admission과 Core `LEFT`·`JOINED`·`DISCONNECTED` control record가 실제 ActorRef와 membership epoch를
전달하도록 runtime 경계를 연결했다. snapshot 외부 객체와 내부 ActorRef를 모두 동결하며 framework가
관리하지 않는 actor context는 구성 오류로 거부한다. Node package build와 actor·Spot lifecycle 집중
계약 29/29가 통과했다. 이어 일반 session reply의 계약 밖 metadata setter를 제거하고 빈 metadata
frame 집중 계약 1/1을 통과했다. `ZLinkStreamSessionError`도 정식 `Internal`·`TransportError`
두 값으로 맞췄다. `ZLinkNestFrameworkAdditionalOptions`는 정식 여섯 옵션만 노출하는 interface로
축소하고 builder 내부 registration state와 분리했다. Node package build, exact interface catalog와
Nest RouteMesh options builder 집중 계약 2/2가 통과해 현재 exact interface gate가 모두 통과한다.
Node E2E lifecycle 구현은 snapshot
소비로 전환했으며 ToActorMessaging과 SpotService MultiNode·Session compile gate가 통과했다. DeliveryDispatch,
GameQuest, ShoppingMall sample도 compile gate가 통과했다. Bingo, TicTacToe, SupportChat, ZoneWorld는
lifecycle callback이 mutable actor를 보관하거나 직접 바꾸는 구조를 Actor send/request 기반으로 옮기는
작업이 남았으므로 sample·전체 multiprocess gate와 version 갱신은 아직 실행하지 않는다. timeout은
늘리지 않았다.

**S9-N02·S9-N04·S9-N05 Actor/Spot 책임 경계 후속(2026-07-20)**: Bingo, TicTacToe,
SupportChat, ZoneWorld의 lifecycle callback은 이제 immutable membership snapshot만 저장하고, Actor
상태 초기화·알림·leave·destroy는 정식 Actor send/request handler가 소유한다. user Spot은 ActorRef와
불변 participant projection만 보관한다. 같은 Spot turn에서 operation reply를 기다리는 handler는 공개
`yield()`를 사용하며, Nest에서 주입한 outbound도 ambient Spot turn을 해제하도록 runtime 연결을
보완했다. package와 일곱 sample compile, lifecycle·sample·public surface 집중 계약 66/66,
serial executor 전체 22/22, formal Core remote join binder 2/2가 통과했다. RouteMesh transition
inventory는 제거된 Node builder owner 세 개를 실제 `ZLinkMeshNodeBuilder` owner로 바꾸고 exact
handler·location fixture를 현재 source와 대조해 갱신했으며 framework doc verifier의 Node 실패는 0개다.
고정 scenario sleep과 local readiness 기본값은 monotonic evidence polling 기준 3초 이하로 맞췄고
Node sample·E2E·script 범위의 3초 초과 fixed sleep/readiness 검색은 0건이다. TicTacToe multiprocess는
host의 첫 `PlaceMarkReq` reply까지 즉시 진행하지만, 다른 node session에 바인딩된 guest Actor가 room
owner node로 이주한 뒤 두 번째 request에서 `Actor route 'player-o' is stale after re-resolve.`로
중단된다(`/tmp/zlink-tictactoe.ts-pjizJO`). Node는 Core join completion의 새 ActorRef로 source
session을 unbind/rebind한 뒤 응답하며 해당 focused binder gate도 통과한다. 이후 실제 relay는
`ZLinkManagedStream.sendBoundActor(actorId)`가 Core 소유 bound route를 사용하고 여기서 Conflict가
반환되므로 남은 실패는 Core reciprocal handover 경계다. timeout 증액이나 relay 우회 없이 Core 수정
뒤 같은 runner와 전체 sample·E2E를 다시 실행하며, 그 전에는 version·package 배포를 진행하지 않는다.

**S9-N04·S9-N06 문서·실제 sample 후속(2026-07-20)**: Node 언어 문서는 제거가
확정된 12개 장별 guide를 복구하지 않고 README에서 정식 공통 spec·Node exact interface와 현재
sample로 사용 경로를 안내한다. internals의 RouteMesh topology, handler 입출력, immutable lifecycle,
기동·종료 순서를 현재 source와 맞췄고 Node 문서 회귀 17/17이 통과했다. 전체 문서 verifier에서
Node 실패는 0개이며 현재 병렬 작업 중인 C++ interface fixture 차이만 보고한다. package verifier
전단은 registry 배포 없이 임시 package와 clean consumer를 사용해
`NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs`로 통과했다. 실제
`GameQuest.Ts`와 `DeliveryDispatch.Ts` runner는 각각 rehydrate·동시 owner·server evidence와
reassignment·server evidence를 완료하고 통과했다. `ShoppingMall.Ts`는 success·idempotency·concurrent·
pending·resume까지 완료한 직후 다른 node에서 이미 claim된 order Spot을 `getOrCreate`한 같은 turn의
location 조회가 `undefined`를 반환해 중단됐다(`/tmp/zlink-shoppingmall.ts-2LewGQ`). 이는 긴 timeout으로
해결하지 않고 claim 결과와 live owner lease의 즉시 가시성 경계 결함으로 추적한다. `SupportChat.Ts`는
누락된 physical MeshNode routing ID와 handler가 없는 outbound ChannelName의 weight를 정식 topology로
보정해 build와 집중 계약이 통과했다. 실제 runner는 인증과 Actor bind를 완료한 직후 첫 bound Actor
request에서 session relay route가 준비되지 않았다는 오류로 즉시 중단됐다
(`/tmp/zlink-supportchat.ts-Wzt0Hs`). session bind 뒤 공개 relay 경로를 우회하거나 timeout을 늘리지
않는다. TicTacToe의 stale Actor route Core blocker도 유지한다(`/tmp/zlink-tictactoe.ts-pjizJO`).
timeout·version은 올리지 않았고 local package를 외부 registry에 배포하지 않았다.

**S9-N03·S9-N04·S9-N05 process당 단일 MeshNode 정렬(2026-07-20)**: 공통 sample
규약과 TicTacToe·ZoneWorld 정식 scenario를 다시 대조해, 수동 연결 예외도 별도의 물리 MeshNode를
요구하지 않음을 확인했다. Node TicTacToe는 API와 Play process가 각각 `play-node` physical
MeshNode 하나만 만들고 `tictactoe.api`, `tictactoe.play`, Spot·Actor route를 ChannelName으로
함께 운반하도록 통합했다. 수동 peer endpoint와 request handler 직접 등록은 그대로 유지했다.
ZoneWorld는 Gateway, Ops와 zone을 소유한 ZoneNode가 각각 `zoneworld.zones` physical MeshNode
하나만 만들고 bridge, report, actors와 node별 ops route를 ChannelName으로 옮겼다. ZoneWorld의
location store 자동 discovery와 `addHandlerGroup` 기반 자동 handler 등록은 유지했으며 STREAM과
fanout transport는 변경하지 않았다. 제거된 물리 transport 전용 endpoint 설정은 runner와 config에서
함께 삭제했고 TicTacToe peer readiness 상한은 3초로 고정했다. Node 전체 `npm run build`, 두 sample
build, ZoneWorld 집중 gate 9/9와 TicTacToe topology·수동 등록 집중 gate 3/3이 통과했으며 scoped
`git diff --check`도 통과했다. focused Core 실제 실행에서 TicTacToe는 인증 3개, observer 구독과
host·guest cross-node join을 통과한 뒤 host의 첫 `PlaceMarkReq`가 원격 guest route를 사용할 때
`Actor route 'player-o' is stale after re-resolve.`로 중단됐다(`/tmp/zlink-tictactoe.ts-DgQNSJ`).
ZoneWorld는 G1·G2와 graceful slot handoff G3를 통과했지만, G4에서 east slot 1 owner를 강제 종료한
직후 west process가 계속 상태 report를 보내는 동안 west slot 2 lease가 먼저 사라져 crash replacement가
slot 2로 bind했다(`/tmp/zlink-zoneworld-nA6XW5`). 그 전 zone tick의 logical multicast submit도
`meshNodePublisherPublish failed: No such file or directory`를 반복해 physical 통합 뒤 첫 multicast
경계를 별도로 보존했다. 두 실패 모두 timeout·retry·sleep 증액이나 framework 우회, Core 수정 없이
실패 지점만 기록했고 version과 package 배포는 진행하지 않았다.

S9 완료 gate:

- [ ] 세 lane이 자기 file scope만 수정했다.
- [ ] 각 lane의 일반 build·전체 test, sample, E2E와 stale no-hit가 통과한다.
- [ ] 세 lane이 S8의 구현 결과나 clean 판정을 계약 근거 또는 선행 조건으로 사용하지 않았다.
- [ ] 공통 spec 변경이 필요해진 경우 구현을 멈추고 S2·S3 계약 review를 다시 연다.

### 13.5 Core Spot 데이터 경로 성능 lane

이 lane은 현재 Core 기능 결함 수정과 전체 회귀가 끝난 뒤 시작한다. 시작 뒤에는 C++·JVM·Node.js와
`.NET` framework의 남은 구현·sample·E2E 작업과 병렬로 진행할 수 있다. 측정 조건, 비교 기준과 정량
목표는 `doc/plan/spot-route-data-plane-performance-improvement-plan.ko.md`를 따르며, 조사 결과가
계획의 병목 가정과 다르면 공개 계약을 바꾸지 않는 범위에서 구현 순서를 조정한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S9-P01 | correctness와 측정 신뢰성 복구 | TLS/WSS·WS 종료·대용량 전환 결함의 작은 회귀, deterministic metric test와 동일 방향 ROUTER 기준 구현이 통과 | 완료 | TLS 실제 runner red→green, 2-peer 72 cell `success=72`, `fail=0`; deterministic metric test `1/1`; 동일 방향 ROUTER 기준 2-peer 4 transport `success=4`, `fail=0`, 10-peer tcp `2.963656 Mmsg/s`; `doc/perf/perf/core/log/2026-07-19-round-165-spot-tls-correctness.ko.md` |
| S9-P02 | Spot 데이터 경로 병목 측정과 개선 | profile과 paired run으로 병목을 증명하고 공개 API·수신 완전성을 바꾸지 않은 변경만 유지 | 진행 중 | 100 peer·5초 matched 기준에서 client blocking ready가 REQREP 7.29배, SENDSEND 5.91배 개선되어 유지됐다. peer별 process·context·I/O thread를 맞춘 1회 smoke는 REQREP 81.02%, SENDSEND 56.48%로 아직 90% 미달이다. echo 지연을 ROUTER와 같은 one-way 추정값으로 보정하고 deterministic metric·policy 30개 test를 통과시켰다. process-global claim table의 node-local 전환, server blocking ready와 intrusive mailbox FIFO는 focused lifecycle·stress를 통과했지만 c100 개선이 없어 모두 원복했다. correctness 수정 뒤 현재 runtime으로 다시 잰 1회 tcp 64바이트 비율은 PUBSUB 44.11%, REQREP 73.74%, SENDSEND 59.39%다. 10-peer `strace`에서 Spot REQREP는 메시지당 `futex` 약 8.89회와 `sched_yield` 약 5.23회를 기록했고 matched ROUTER의 `futex`는 약 4.89회였다. lifecycle 대기와 dispatch 대기를 분리한 전용 condition variable 후보는 c100에서 46.15%, 80.27%, 58.71%로 공통 개선을 입증하지 못해 회귀와 함께 원복했다. Callgrind c1 raw를 재판정해 1,968개 request에서 `pthread_mutex_lock` 5,914회(약 3.00회/request)를 확인했다. 이전 796,863은 호출수가 아니라 lock 함수 내부 instruction 수다. 빈 `DONTWAIT` 조회용 atomic hint는 c100 비율 46.86%, 81.22%, 57.38%이고 지연 gate도 실패해 원복했다. ingress 검증·reply route·admission의 lock을 결합한 후보도 focused 5/5 뒤 c100에서 48.65%, 63.89%, 55.26%에 그쳐 원복했다. monitor 미설치 event fast path도 c100에서 45.05%, 62.79%, 56.19%이고 지연 gate를 통과하지 못해 5회 gate로 확장하지 않고 원복했다. outbound application admission을 좁은 atomic lifecycle gate로 분리한 후보는 focused 37/37 뒤 c100에서 48.61%, 76.99%, 60.03%였지만 REQREP·SENDSEND 절대 처리량과 지연이 개선되지 않아 원복했다. server yield를 분리 계측한 결과 c100·3초 REQREP에서 backpressure 재시도는 0회이고 빈 ready polling 뒤 yield는 23,211,721회였다. ingress frame vector 재사용 후보는 basic·stress 17/17과 기존 flaky 단독 재실행을 통과했지만 c100에서 44.21%, 63.37%, 62.39%로 공통 개선이 없어 원복했다. 원복 뒤 공식 `core/build`의 `libzlink.so.10.6.0` SHA-256은 correctness gate와 같은 `671fc61d…2b33ffe`이며 source보다 오래된 runtime이 없다. 이어진 저부하 source 분석에서 Mesh가 전체 wire multipart를 이미 소유하면서 envelope와 payload마다 공개 part helper의 handle-state lookup·mutex·spec/RID 비교를 반복하는 경계를 확인했다. ROUTER send scope 하나로 전체 wire message를 보내고 post-envelope OOM rollback을 유지하는 후보를 적용했다. 별도 Debug build에서 basic 14·lifecycle 14·stress 3과 peer 24가 처음 통과했다. remote metadata exact 회귀 추가 뒤에는 해당 회귀 1/1, peer 전체 23/24가 통과했고 기존 flaky `test_peer_drain_and_reconnect` 단독 재실행이 통과했다. 아직 성능 측정 전 후보이므로 유지 판정하지 않았고, 다른 framework startup gate와 자원 경합을 피하기 위해 추가 고부하 perf는 coordinator 재개 신호까지 중단했다. 상세: `doc/perf/perf/core/log/2026-07-20-round-167-spot-tcp64-red-and-wait-policy.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-168-spot-matched-core-candidates.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-170-spot-ready-wakeup-profile.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-189-spot-ingress-lock-fusion-rejected.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-191-monitor-absent-fastpath-rejected.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-192-spot-outbound-admission-gate-rejected.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-193-spot-server-yield-split.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-194-spot-ingress-frame-reuse-rejected.ko.md`, `doc/perf/perf/core/log/2026-07-20-round-195-spot-wire-multipart-scope-candidate.ko.md`. **2026-07-20 16:20 KST 인수 점검**: 공식 `core/build/lib/libzlink.so.10.6.0`은 Core source보다 새롭고 SHA-256은 `a58c248d…a5b5c`라 stale하지 않다. 다만 기존 `bindings/c/perf/run_benchmarks_multi.sh` PID `3870425`가 같은 `core/build` runtime으로 100-peer·5초 full matrix를 실행 중이며, 별도 중지 상태 PID `8508`도 보존되어 있다. 같은 host에서 paired candidate 측정을 겹쳐 실행하면 CPU·scheduler 자원 경합으로 비교 수치가 무효가 되므로 새 benchmark를 시작하지 않았다. 실행 중 report `perf_c_multi_linux_20260720_155432.txt`는 아직 완료되지 않아 성능 증거로 채택하지 않는다. 다음 순서는 기존 실행의 정상 종료를 확인한 뒤 `cmake --build core/build --parallel`, `test -z "$(find core/src core/include -type f -newer core/build/lib/libzlink.so -print -quit)"`, `sha256sum core/build/lib/libzlink.so.10.6.0`을 순서대로 실행하고, 다른 perf process가 없을 때 `python3 bindings/c/perf/run_spot_paired_gate.py --patterns SPOT_PUBSUB,SPOT_REQREP,SPOT_SENDSEND --transports tcp --msg-sizes 64 --runs 1 --clients 100 --duration 5 --tag s9-p02-wire-scope-candidate`로 후보를 판정한다. **2026-07-20 17:56 KST 종료 회귀 수정**: reciprocal pipe ACK를 local completion callback보다 먼저 enqueue해야 한다는 순서 invariant는 pipe-level test에서 이전 순서 `Expected 1 Was 0` RED와 수정 순서 GREEN으로 고정했다. 다만 100-peer mass barrier test는 이전 순서에서도 10/10 통과했으므로 원인 gate가 아닌 보조 stress로만 유지한다. 실제 `_fq._pipes.empty()` abort는 ACK 회계가 아니라 Router FQ 동시 갱신이었다. 실패 직전 `term_acks=0`, `term_pipes=0`, `registered=received`, `attached=0`인데 residue pipe의 `xpipe_terminated` 진입 당시 `in_fq=1`, `anonymous=0`이었고 duplicate attach는 없었다. 기존 Router FQ 경로 중 `xrecv`와 `xrecv_routed`만 공통 dispatch lock 밖에 있어 종료 callback의 제거와 수신 갱신이 경쟁했다. 두 수신 경로를 기존 recursive dispatch lock에 포함한 runtime SHA-256 `cc0ee228…a37cd`는 별도 Debug exact `MULTI_SPOT_REQREP`, tcp, 64·256바이트, 100 clients, 1초, cooldown 0 조건의 20회 반복에서 두 size 모두 통과하고 abort 0회를 기록했다. `test_ctx_destroy` 전체 10회, peer drain/reconnect·blocking send shutdown·endpoint reconnect·remote Spot req/rep·mass barrier teardown, `test_router_multiple_dealers`도 통과했다. timeout과 assertion은 변경하지 않았다. |
| S9-P03 | 정량 성능 gate | 100 peers·5초·cell별 5회 paired median에서 Spot 3패턴이 대응 ROUTER의 90% 이상이고 latency·일반 pub/sub 회귀 목표 통과 | 진행 중(RED) | 안정 runtime `671fc61d…2b33ffe`로 tcp 64바이트·100 peer·5초·5회 paired 중앙값을 실행했다. PUBSUB은 처리량 44.63%, mean/p95/p99 비율 1.6482/1.7108/1.7122, REQREP는 64.24%, 3.2349/2.1791/2.7840, SENDSEND는 56.30%, 1.5219/2.0854/2.6372로 모든 처리량·지연 gate가 실패했다. multicast drop은 0이고 source/runtime identity는 실행 중 불변이었다. P02의 공통 병목 개선 전에는 나머지 matrix와 P04를 시작하지 않는다. 상세: `doc/perf/perf/core/log/2026-07-20-round-173-spot-p03-tcp64-median-red.ko.md` |
| S9-P04 | 반복·안전성 종료 gate | 72 cell 성공, full perf 3회 연속 complete, child/assertion/timeout 0, 관련 sanitizer와 비-SPOT 회귀 통과 | 대기 | 현재 2-peer correctness smoke 72/72와 metric·policy 30/30만 통과했다. 정식 100-peer paired matrix 0/72, full perf 0/3, sanitizer와 비-SPOT 회귀는 미실행이다. P03 통과 뒤 같은 source·runtime으로 72-cell full을 독립 3회 실행하고 각 실행의 child `waitpid`, runner assertion·timeout, 실행 후 남은 process 0을 함께 확인한다. 상세: `doc/perf/perf/core/log/2026-07-20-round-169-spot-p04-gate-design.ko.md` |

**2026-07-20 18:28 KST S9-P02/P03 gate 갱신**

- S9-P02: 공식 Release runtime `a57d91a…b3301`은 Core source보다 새롭고 runner가 실제
  경로를 출력했다. `MULTI_SPOT_REQREP` tcp 64·256바이트, 100 peer, 1초 조건은 20회
  모두 통과했고 pending queue, dropped target, assertion과 timeout은 0이었다. 같은
  조건의 WSS는 두 크기 모두 5회 통과했지만 TLS는 매번 첫 64바이트 단계에서 client
  종료 코드 1로 실패했다. 2-peer 대조 실행에서는 TLS/WSS 네 cell이 모두 통과했으므로
  100-peer TLS 시작 또는 admission 과정의 규모별 blocker로 남긴다.
- Router FQ 경쟁은 `ZLINK_BUILD_TESTS` 전용 barrier hook으로 수신이 선택한 pipe와 termination을
  결정적으로 겹치게 했다. 현재 `xrecv`·`xrecv_routed` dispatch lock에서는 두 회귀가 GREEN이고 focused
  20회 반복과 ASan·UBSan도 통과했다. 동일 source 복사본에서 두 lock만 제거한 RED build는 기존 2개
  test를 통과하면서 신규 두 회귀만 정확히 실패해 원인과 수정 경계를 고정했다.
- S9-P03: correctness 전체 통과를 선행 조건으로 적용해 100 peer·5초·5회 paired median을
  실행하지 않았다. 따라서 기존 RED 상태와 수치는 유지한다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-196-router-fq-lock-correctness-gate.ko.md`.

**2026-07-20 18:47 KST TLS blocker 종료와 S9-P03 재측정**

- S9-P02: 100-peer TLS 실패는 Core가 아니라 fork된 성능 도구가 같은 인증서·key·CA 파일을
  각각 101회 `O_TRUNC`로 열어 reader와 writer가 겹친 파일 생성 경쟁이었다. POSIX helper가
  완성한 별도 임시 파일을 원자적으로 publish하도록 바꿨다. 같은 공식 runtime에서 tcp는
  64·256바이트 20회, TLS와 WSS는 각각 5회 모두 통과했고 pending queue, dropped target,
  assertion, timeout과 비정상 자식 종료는 0이었다. Core source·spec과 공개 API는 바꾸지 않았다.
- S9-P03: correctness 통과 뒤 tcp 64바이트·100 peer·5초·5회 paired 중앙값을 실행했다.
  PUBSUB 처리량은 ROUTER의 79.44%이고 mean/p95/p99 비율은 1.8936/3.8601/5.1811,
  REQREP는 64.01%와 3.5544/2.5069/3.2258, SENDSEND는 52.18%와
  1.7807/2.8338/3.6576이었다. 세 cell 모두 처리량과 지연 gate가 실패했으므로 P03은 RED를
  유지한다. 실행 중 source tree와 runtime SHA는 변하지 않았고 multicast drop은 0이었다.
  상세: `doc/perf/perf/core/log/2026-07-20-round-197-tls-cert-publication-and-p03-red.ko.md`.

**2026-07-20 19:00 KST timeout 취소 wakeup 후보 반려**

- S9-P02: 10-peer pattern trace를 수신 건수로 정규화하면 `futex` 호출은 PUBSUB 약
  2.43회/message, REQREP 약 8.69회/operation, SENDSEND 약 4.86회/operation이고 matched
  ROUTER REQREP는 약 4.89회/operation이었다. echo 두 패턴만 terminal completion에서
  timeout scheduler의 전역 condition variable을 매번 깨우는 경계를 확인했다. PUBSUB에는
  request operation이 없으므로 이 후보가 세 패턴에 같은 효과를 내지 않는 이유도 분리했다.
- 취소가 남은 earliest deadline을 더 이르게 만들 수 없다는 점에 따라 cancel wakeup을 제거한
  후보는 timeout scheduler와 Mesh basic·stress·monitor 집중 회귀 4/4를 통과했다. 그러나
  tcp 64바이트·100 peer·5초 paired 1회에서 PUBSUB·REQREP·SENDSEND 처리량 비율은 각각
  82.13%·60.46%·55.99%였다. REQREP Spot 절대 처리량은 Round 197 중앙값보다 약 8.4%
  높았지만 ROUTER 대비 비율은 64.01%에서 60.46%로 낮아졌고 지연도 함께 개선되지 않았다.
  PUBSUB 종료 snapshot에도 application message 230개와 14,720바이트가 남았다. 유지 조건과
  수신 완전성 조건을 만족하지 않아 후보 hunk만 원복했고 정식 5회 측정으로 확장하지 않았다.
- 원복 뒤 공식 runtime SHA-256은 `a57d91a…b3301`로 복원됐고 Core source보다 새롭다.
  timeout·assertion·version·package와 배포는 변경하지 않았다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-198-timeout-cancel-wakeup-rejected.ko.md`.

**2026-07-20 19:20 KST public send 직렬화 mutex 후보 반려**

- S9-P02: 현재 Spot server Callgrind에서 ROUTER public send 직렬화의 무제한 atomic busy-spin이
  경쟁 대기를 소유하는 경계를 확인했다. PUBSUB은 전체 instruction의 97.79%, SENDSEND는 99.91%가
  `lock_public_api_sync()` 안에 기록됐다. 계측기가 thread 실행 속도를 크게 바꾸므로 native CPU 비중으로
  사용하지 않고, 경쟁 시 대기 위치를 특정하는 자료로만 사용했다. REQREP에서는 `pthread_mutex_lock`
  자체가 12.16%여서 mailbox·completion mutex 비용도 별도임을 확인했다.
- 제한된 spin 뒤 yield와 내부 mutex를 비교해, 별도 spin 횟수 정책과 기아 가능성을 만들지 않는 mutex
  후보를 먼저 검증했다. S3 iteration 3의 70-file snapshot 밖인 Core 두 파일만 임시 수정했으며 Router
  동시 수신·다중 dealer와 Mesh basic·stress·peer admission 집중 회귀 5/5가 통과했다.
- tcp 64바이트·100 peer·5초 paired 1회에서 PUBSUB·REQREP·SENDSEND 처리량 비율은
  88.46%·67.83%·58.19%였다. Round 197 중앙값보다 세 패턴의 절대 처리량, 비율과 지연은 모두
  개선됐지만 처리량 90%·지연 1.25배 gate를 통과하지 못했고 PUBSUB 종료 snapshot에도 application
  message 213개와 13,632바이트가 남았다. 채택 조건 미달로 후보 hunk만 원복했다.
- 원복 뒤 공식 runtime SHA-256은 `a57d91a…b3301`로 복원됐고 Core source보다 새롭다. timeout,
  assertion, version, package와 배포는 변경하지 않았다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-199-public-send-sync-mutex-rejected.ko.md`.

**2026-07-20 19:25 KST perf pattern 종류 캐시 후보 반려**

- S9-P02: Round 199 REQREP server profile에서 C 성능 도구가 매 메시지마다 pattern 문자열을 만들고
  검색하는 `pattern_kind()`가 전체 instruction의 3.16%를 사용하며 string 할당·해제 비용도 동반하는
  것을 확인했다. CMake target에 enum을 중복 주입하는 방식과 함수 내부에서 한 번만 분류하는 방식을
  비교해 후자를 검증했다. 두 perf source는 S3 iteration 3의 70-file snapshot 밖이었다.
- 여섯 Spot binary build와 metric test 1/1은 통과했다. 2-peer 기능 smoke는 `success=3`, `fail=0`이지만
  PUBSUB pending 2개와 dropped target 331,209개로 완전성 diagnostic은 RED였다. c100 paired에서도
  PUBSUB·REQREP·SENDSEND 처리량 비율은 83.40%·62.15%·54.83%이고 모든 pattern의 지연 gate가
  실패했으며 PUBSUB pending 199개가 남았다.
- 실제 반복 비용이지만 현재 격차의 주원인이 아니므로 후보의 두 함수 hunk만 원복하고 perf binary도
  원복 source로 다시 만들었다. 공식 runtime은 `a57d91a…b3301`로 계속 fresh하다. timeout, assertion,
  version, package와 배포는 변경하지 않았다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-200-perf-pattern-kind-cache-rejected.ko.md`.

**2026-07-20 19:44 KST 내부 poller FD 조회 후보 반려**

- S9-P02: 최신 Spot server Callgrind caller tree에서 PUBSUB 97.79%, SENDSEND 99.91%가 ingress의
  `socket_poller_t::rebuild()`가 public `getsockopt(FD)`를 거쳐 send 직렬화와 경쟁한 busy-spin에
  기록됐다. Poller item에 FD를 복제하는 대안과 socket의 내부 FD 조회를 사용하는 대안을 비교해,
  mailbox 소유 지식을 socket에 유지하는 두 번째 후보를 검증했다.
- Debug focused suite 6/6은 통과했다. 그러나 공식 runtime의 tcp 64바이트·100 peer·5초 paired
  1회에서 PUBSUB·REQREP·SENDSEND 처리량 비율은 84.86%·57.19%·50.45%였고, PUBSUB mean을 제외한
  모든 지연 gate가 실패했다. PUBSUB pending application message도 302개 남아 후보 hunk만
  원복했다. 원복 runtime SHA-256은 `a57d91a…b3301`이고 fresh하다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-201-internal-poller-fd-rejected.ko.md`.

**2026-07-20 20:31 KST ready transition coalescing 후보 반려**

- S9-P02: claim 중인 mailbox에 record를 추가할 때 ready handler를 반복 호출하는 현상을 진단 test로
  고정했다. 기존 구현은 claim을 유지한 채 32개를 추가하면 handler를 총 33회 호출했고, 후보는 claim
  release가 한 번 다시 등록하도록 바꿔 2회로 줄였다. Focused Core suite 5/5가 통과했고 10-peer
  `strace`의 PUBSUB `futex`는 message당 약 2.43회에서 1.91회로 21% 감소했다.
- 공식 runtime의 tcp 64바이트·100 peer·5초 paired 1회에서 PUBSUB·REQREP·SENDSEND 처리량 비율은
  85.24%·60.08%·50.47%였다. 세 pattern 모두 latency gate를 통과하지 못했고 PUBSUB 종료 snapshot에
  application message 225개가 남았다. 후보 hunk와 진단 test를 원복했다. 원복 runtime SHA-256은
  `a57d91a…b3301`이고 Core source보다 새롭다. timeout, assertion, version, package와 배포는 변경하지
  않았다. 상세:
  `doc/perf/perf/core/log/2026-07-20-round-202-ready-transition-coalescing-rejected.ko.md`.

성능 lane 완료 gate:

- [ ] `core/build` runtime이 Core source보다 새롭고 runner가 실제 사용한 `libzlink.so`를 출력했다.
- [ ] 결과와 조사 기록을 `doc/perf/perf/core/log/` 아래에 남겼다.
- [ ] S9-P01~P04가 모두 완료되었고 성능 변경 뒤 Core 전체 회귀를 다시 통과했다.
- [ ] 성능 변경을 포함한 최종 Core와 bindings version은 §0.4 정책에 따라 검증 뒤 한 번만 올렸다.

## 14. lane framework 리뷰 상세 (S8-CPP/JVM/NODE framework 단계 리뷰)

S9의 세 구현 lane 리뷰는 서로 병렬 실행할 수 있다. 각 lane 안에서는 Codex agent와 R2 리뷰를
같은 revision으로 병렬 실행한다. reviewer는 다른 lane을 수정하지 않는다.

| ID | Lane | Codex 결과 | R2 결과 | open finding | 상태 | 증거 |
|---|---|---|---|---:|---|---|
| S10-C | C++ | 대기 | 대기 | 0 | 미착수 | - |
| S10-J | Java/Kotlin | 대기 | 대기 | 0 | 미착수 | - |
| S10-N | Node.js | 대기 | 대기 | 0 | 미착수 | - |

각 lane은 다음 검토를 반복한다.

- 정식 언어 interface와 구현·package snapshot 일치
- 공통 framework spec, 언어별 exact interface와 Core 공개 계약이 요구하는 관찰 가능한 동작 일치
- sample과 E2E inventory 누락
- internal/private API, raw frame, codec 우회와 언어 전용 임시 public API
- POSD 위험 신호와 DDD 책임 중복
- 불필요하거나 도달 불가능한 code, file, test와 build target
- 제거 API, alias, 현재 계약·guide, sample과 package entry 잔존
- package·consumer 종료 검증을 막을 source, workflow와 metadata 결함

각 lane의 목록도 §2.1의 I1·I2·I3로 분리한다. I1은 정식 interface 대비 누락·오구현·관찰 가능한
동작 불일치, I2는 POSD·DDD 관점의 의미 있는 리팩터링 잔여, I3는 불필요·죽은 code·file·test·build
target·호환 잔재를 판정한다. 각 리뷰어와 각 축마다 finding 또는 `없음`, evidence와
`CLEAN`/`NOT CLEAN`을 기록한다. 어느 축을 수정해도 그 lane의 두 리뷰어가 I1·I2·I3 전체를 다시
검토한다.

Java/Kotlin lane에서 finding을 수정한 뒤에는 일반 build와 Java
`framework/languages/java/e2e/run_e2e_all.sh`, Kotlin
`framework/languages/java/e2e-kotlin/run_e2e_all.sh` 전체 테스트를 실행하고 결과를 분리해 기록한다.
두 reviewer가 clean을 남긴 뒤 package·consumer 종료 검증을 실행한다.

언어별 종료 문구:

- C++: `CPP REVIEW CLEAN`
- Java/Kotlin: `JVM REVIEW CLEAN`
- Node.js: `NODE REVIEW CLEAN`

두 reviewer가 해당 lane의 clean 문구를 남긴 뒤 다음 종료 검증을 실행한다.

| ID | Lane | 리뷰 종료 검증 | 상태 | 증거 |
|---|---|---|---|---|
| S10-CV | C++ | `verify_packaged_contract`, clean package consumer와 package metadata 검사 통과 | 미착수 | - |
| S10-JV | Java/Kotlin | `verify_packaged_contract`, clean package consumer와 package metadata 검사 통과 | 미착수 | - |
| S10-NV | Node.js | `verify_packaged_contract`, clean npm consumer와 package metadata 검사 통과 | 진행 중(전단 통과) | 정식 계약과 달리 남아 있던 Stream Connector package root의 server CommonJS export를 제거하고 browser ESM-only manifest로 복구했다. Node 실행 sample은 공개 root에 `require` export를 되살리지 않고 build 전용 bundle 경계에서 connector를 포함한다. package root에 CommonJS 조건이 없고 생성된 sample client에도 bare connector `require`가 없음을 회귀로 고정했다. verifier가 central local HTTP client tgz도 clean server consumer에 설치하도록 고친 뒤 `NODE_PACKAGED_CONTRACT_PASS packages=7 browser=esm server=commonjs`가 다시 통과했다. 이는 Core native operation을 실행하지 않는 전단 증거이며, 두 reviewer clean과 호환되는 새 10.6.0 native package 동기화 뒤 같은 검사를 다시 통과해야 종료한다 |
| S10-CI | C++ | S10-CV 뒤 최종 adapter·scheduler·timer internals 반영과 source·구조 test·diagram·link 일치 | 미착수 | - |
| S10-JI | Java/Kotlin | S10-JV 뒤 최종 pump·scheduler·location·timer internals 반영과 source·구조 test·diagram·link 일치 | 미착수 | - |
| S10-NI | Node.js | S10-NV 뒤 최종 pump·scheduler·location·timer internals 반영과 source·구조 test·diagram·link 일치 | 미착수 | - |

S10 완료 gate:

- [ ] 세 lane 모두 적용되는 종료 기준의 open finding이 0개다. 1~3회차는 전체 severity, 4회차부터는
  blocker·high·medium을 기준으로 한다.
- [ ] 세 lane 모두 두 리뷰어의 I1·I2·I3 각각에 finding 또는 `없음`, evidence와 `CLEAN` 판정이 있다.
- [ ] 세 lane 모두 두 리뷰어의 해당 clean 문구가 있다.
- [ ] 어느 축의 finding이든 일괄 수정한 뒤 각 lane의 일반 build·전체 테스트를 실행하고 두 리뷰어가
  해당 lane 전체의 I1·I2·I3를 재검토했다.
- [ ] 각 lane의 두 clean 결과 뒤 package·consumer 종료 검증이 통과했다.
- [ ] S10-CV/JV/NV 뒤 S10-CI/JI/NI에서 각 언어의 최종 internals를 한 번 반영하고 source와의
  일치를 확인했다.

## 15. S11 — 전체 최종 검토, bindings 내부 package 배포와 종료

### 15.1 최종 리뷰 대상 준비

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-01 | 모든 lane 병합과 clean checkout 확인 | 의도하지 않은 file과 미병합 변경 0개 | 미착수 | - |
| S11-00A | Core 최종 candidate revision 동결 | 최종 source·ABI와 local runtime hash를 고정하고 S11 검증 중 변경 없음 | 미착수 | - |

### 15.2 최종 독립 리뷰

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-13 | Codex agent 전체 리뷰 | 전체 scope의 I1·I2·I3 각각에 finding·evidence·축별 판정 | 미착수 | - |
| S11-14 | Claude Sonnet 전체 리뷰 | 같은 전체 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S11-15 | final finding 일괄 수정과 일반 검증 | finding을 한 번에 수정하고 일반 build와 영향받은 전체 테스트 통과 | 미착수 | - |
| S11-16 | 전체 scope 재리뷰 반복 | 최신 release-candidate snapshot의 Core·bindings·모든 framework 언어·spec·guide·internals·test·sample·E2E·package·workflow 전체를 처음부터 검토하며, 세 축이 모두 clean이고 두 리뷰어가 모두 `FINAL REVIEW CLEAN` | 미착수 | - |

### 15.3 최종 리뷰 종료 검증

이 절은 두 reviewer가 같은 snapshot에 `FINAL REVIEW CLEAN`을 남긴 뒤에만 실행한다.

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-02 | 정식 spec과 public API 전수 대조 | Core, bindings와 framework 언어별 차이 0개이며 Channel 단일 주소, RouteMesh·ClientServer builder, Channel·Node context 분리와 location descriptor가 exact interface와 일치 | 미착수 | - |
| S11-03 | 제거 항목 repository no-hit | 허용된 v10 plan·review record 외 stale 이름 0개 | 미착수 | - |
| S11-04 | Core 전체 종료 검증 | build, test, ASAN·UBSAN·TSAN과 package consumer 통과 | 미착수 | - |
| S11-04A | Core Spot 성능 lane 종료 검증 | S9-P01~P04 증거와 최종 Core source·runtime·report가 일치 | 미착수 | - |
| S11-05 | bindings local package smoke 재실행 | S7에서 검증한 모든 local package E2E와 언어별 binding primitive·token lifecycle contract command 통과 | 미착수 | - |
| S11-06 | `.NET` 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-07 | C++ 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-08 | Java/Kotlin 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-09 | Node.js 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-10 | classic fanout과 generic STREAM 회귀 | Actor binding 확장점을 제외한 비변경 socket 기능의 public 동작과 baseline 유지 | 미착수 | - |
| S11-10A | Channel 송신 경로·ClientServer·sample topology 교차 언어 회귀 | 네 lane의 최종 package로 `CH-E2E-01~10`·`CH-REG-01~09`, 공통 topology fixture, 중복 물리 peer·listener 부재, request terminal completion 한 번을 모두 통과 | 미착수 | S3-CH-03·S8-08A·S9-C02D·S9-J03D·S9-N02D 선행 |
| S11-10B | classic fanout 자동 연결 교차 언어 회귀 | 네 lane의 최종 package로 publisher 게시, 동일 ChannelName·publisher role 선택, 타 channel·role 미연결, 추가·제거·lease 만료·재등록 수렴, manual endpoint 비회귀와 startup negative를 모두 통과 | 미착수 | S3-FO-01·S8-FO-DN·S9-FO-CPP·S9-FO-JVM·S9-FO-NODE 선행 |
| S11-10C | MeshNode 고정 drain 교차 언어 회귀 | 네 lane의 최종 package로 normal request의 Spot 유지, 신규 admission seal, accepted turn·Actor handoff·STREAM barrier 선행, local Spot cleanup·owner row release, hidden remote create 금지, explicit local GetOrCreate와 terminal result 1회를 모두 통과하고 제거 policy 표면 no-hit | 미착수 | S3-DP-01·S8-DP-DN·S9-DP-CPP·S9-DP-JVM·S9-DP-NODE 선행 |
| S11-10D | Instance Spot 교차 언어 회귀 | 네 lane의 최종 local package로 `IS-REG-01~14`, `IS-E2E-01~31`, 다중 Mesh 격리, stale owner fencing, Ready ordering과 PlayerQuest·OrderWorkflow sample parity 통과 | 미착수 | S3-IS-01·S8-IS-DN·S8-IS-CPP·S8-IS-JVM·S8-IS-NODE 선행 |
| S11-10E | one-way submit API 교차 언어 회귀 | 네 lane의 최종 local package에서 family별 local·remote 즉시 수락, bounded wait, timeout, 지원 언어 cancellation·late admission 0, partial publish, STREAM reply exactly-once, shutdown·drain·recovery와 handler 비대기 의미가 같고 scoped public `TrySubmit` no-hit·pending leak 0 | 미착수 | S3-SA-01·S8-SA-DN·S8-SA-CPP·S8-SA-JVM·S8-SA-NODE 선행 |
| S11-11 | docs, link와 sample API 검증 | 깨진 link, stale 예제와 내부 구현 노출 0개이며 7개 sample의 호출·Channel 역할·물리 연결이 공통 topology fixture와 일치 | 미착수 | - |
| S11-12 | version과 artifact 대조 | Core와 C++·.NET·Java·Node binding 및 framework pin이 모두 10.7.0이고, binding package의 Core native version·hash와 package version을 별도 대조 | 미착수 | - |

### 15.4 최종 리뷰와 종료 검증 뒤 bindings 내부 package 배포

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-00B | Core 최종 local runtime 생성 | `FINAL REVIEW CLEAN` candidate에서 Core 10.7.0 shared library를 다시 만들고 source보다 최신인 runtime만 내부 package 입력으로 사용 | 미착수 | - |
| S11-00C | Core local artifact 검증 | checksum, headers, SONAME 10, symbols와 clean local consumer 통과 | 미착수 | - |
| S11-00D | 최종 Core 기반 bindings 재검증 | 같은 Core runtime을 네 binding native 입력에 동기화하고 S7 package·공통 E2E smoke 결과 일치 | 미착수 | - |
| S11-01A | bindings 내부 package revision 동결 | S7 clean source, Core runtime SHA와 package checksum·local 경로가 기록과 일치 | 미착수 | - |
| S11-01B | 언어별 bindings 내부 package 배포 | C++·.NET·Java·Node 10.7.0 package를 `scripts/local-package/` 정책에 따라 `.artifacts/<env>`에 생성 | 미착수 | - |
| S11-01C | 내부 package 위치와 metadata 확인 | NuGet·Maven·npm·C++ install의 실제 version, native payload와 checksum이 기록과 일치 | 미착수 | - |
| S11-01D | 내부 package E2E smoke | 각 local package를 빈 workspace에 설치해 공통 smoke 통과 | 미착수 | - |
| S11-01E | framework 내부 package 재검증 | 각 언어 framework pin을 10.7.0 local package로 맞추어 package·sample·E2E 통과 | 미착수 | - |
| S11-17 | package 이후 재검증 | final source, local runtime·package checksum과 framework가 실제 소비한 artifact가 일치 | 미착수 | - |
| S11-18 | 완료 보고서 작성 | 모든 stage 증거, 남은 issue 0과 최종 SHA 기록 | 미착수 | - |

S11 완료 gate:

- [ ] final Core source와 local runtime hash가 동결되고 네 bindings 내부 package의 native payload와 일치한다.
- [ ] 두 리뷰어의 I1에 Core spec부터 bindings·모든 framework 언어·sample·E2E·release-candidate package·workflow의
  누락·오구현·동작 불일치 finding, evidence와 `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I2에 전체 구조의 POSD·DDD 관점에서 의미 있는 리팩터링 잔여 finding, evidence와
  `CLEAN` 판정이 있다.
- [ ] 두 리뷰어의 I3에 제거 대상과 불필요·죽은 code·file·API·test·문서·호환 잔재 finding, evidence와
  `CLEAN` 판정이 있다.
- [ ] 어느 축 수정 뒤에도 두 리뷰어가 전체 통합 scope의 I1·I2·I3를 모두 다시 검토했다.
- [ ] 두 리뷰어가 최신 snapshot의 전체 통합 scope를 처음부터 검토했다.
- [ ] Codex agent 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
- [ ] R2 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
- [ ] 두 clean 결과 뒤 S11-02~S11-12의 종료 검증이 통과했다.
- [ ] S3-CH-03과 S11-10A가 끝났고 ChannelName 단일 주소·ClientServer·sample topology 계약이 네 언어
  최종 package에서 같은 결과를 낸다.
- [ ] 적용되는 종료 기준의 open finding, skipped required test와 검증되지 않은 내부 package artifact가
  0개다. 4회차부터 남은 low는 후속 정리 목록에 기록되어 있다.

## 16. 차단, 재개와 이전 stage 재개방 규칙

- 정식 spec을 바꿔야 하는 구현 finding은 현재 stage에서 임시 처리하지 않고 S1 또는 S2를 다시 연다.
- 공통 계약을 바꾸면 S3 문서 리뷰를 다시 통과한 뒤 downstream stage를 재검증한다.
- S6 RC 뒤 Core ABI나 동작을 바꾸면 외부 tag를 만들지 않고 새 candidate revision으로 S5, S7과
  모든 framework stage를 다시 통과한 뒤 내부 package를 다시 만든다.
- bindings 공개 계약이나 native payload를 바꾸면 S7 review와 local package smoke를 다시 통과한다.
- S8 또는 S9의 어느 언어 구현에서든 공통 계약 gap이 발견되면 S2·S3을 다시 열고, 직접 영향을 받는
  lane의 완료 판정을 취소한다.
- ChannelName 단일 주소 amendment는 S1·S2·S3-CH로 재개방한다. membership 0개 Core 계약과 네
  bindings 검증은 S4-CH·S5-CH·S7-CH, 네 framework 적용은 S8-08A·S9-C02D·S9-J03D·S9-N02D,
  최종 교차 회귀는 S11-10A가 소유한다.
- 병렬 lane에서 공통 문제를 발견하면 한 lane의 helper로 우회하지 않고 coordinator가 계약 stage를
  재개방한다.
- reviewer 또는 local package build 도구를 사용할 수 없으면 관련 stage를 `차단`으로 기록한다.
- flaky test는 성공할 때까지 반복해서 숨기지 않는다. 재현 조건과 root cause를 finding으로 기록한다.
- S11에서 framework가 내부 package를 최종 pin한 뒤 source 또는 계약 결함이 발견되면 기존 검증
  checksum을 덮어쓰지 않는다. binding만 수정하면 해당 binding patch를 올리고, Core를 수정하면 Core
  minor를 올린 뒤 모든 binding patch를 0으로 다시 맞춘다.
- 계획의 범위를 바꾸는 결정은 사용자의 명시적인 승인과 decision record 없이 적용하지 않는다.

## 17. 최종 완료 기록

| 항목 | 값 |
|---|---|
| 최종 source commit | - |
| 검증한 Core local runtime | 10.7.0 SHA-256 기록 예정 |
| bindings 내부 package | C++·.NET·Java·Node 10.7.0 경로·checksum 기록 예정 |
| 최종 Codex review | - |
| 최종 구현 R2 review | Claude Sonnet session과 결과 기록 |
| Core local build·consumer | - |
| bindings local package build | - |
| 전체 E2E 결과 | - |
| 전체 sample 결과 | - |
| benchmark report | 별도 성능 개선 작업에서 기록 |
| stale no-hit 결과 | - |
| open finding | 1~3회차는 전체 severity `0`, 4회차부터는 blocker·high·medium `0`이어야 종료 |
