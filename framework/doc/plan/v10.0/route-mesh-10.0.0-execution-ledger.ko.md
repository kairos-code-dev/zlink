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

review manifest는 작업 공간의 현재 파일이 아니라 시작 시점의 commit과 diff를 보존한 변경되지 않는
snapshot을 가리킨다. 리뷰 중 다른 담당자가 snapshot 범위의 문서를 수정할 수 있으며, 이 변경만으로
진행 중인 리뷰를 무효화하지 않는다. snapshot 생성은 대상 commit, 파일 목록과 aggregate hash를 기록하는
작업이며 build·test·sanitizer를 다시 실행하는 검증 단계가 아니다. 리뷰 대상 code나 계약이 바뀌면 다음
리뷰는 새 snapshot에서 해당 stage의 전체 범위를 다시 검토한다.

### 0.3 POSD 기반 구현 원칙

RouteMesh 10.0.0의 Core, bindings와 framework는 POSD 철학을 설계 기준으로 삼는다. 공개 interface는
작고 단순하게 유지하고 routing, codec,
queue, retry, peer admission과 lifecycle 복잡성은 책임을 소유한 깊은 모듈 안에 둔다. 같은 설계 지식이
여러 계층이나 언어에 반복되면 정보 누출로 판단하며, 호출자에게 transport detail이나 내부 policy를
추가로 요구하는 방식으로 성능 문제를 우회하지 않는다.

성능 측정과 개선은 RouteMesh 10.0.0 기능 구현·배포 gate에서 제외하고 별도 후속 작업으로 진행한다.
현재 stage를 완료하기 위해 benchmark나 p99 측정을 새로 실행하지 않는다.

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
| **S11** | 전체 3축 최종 검토, Core stable·bindings 외부 배포와 종료 | 최종 리뷰 2개만 병렬 | 두 리뷰어의 I1·I2·I3 clean과 `FINAL REVIEW CLEAN` 뒤 stable package 배포·smoke 완료 |

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
| S4 Core 구현·정식 spec 일치 | 완료 | 0 | 0 | 4 | 2026-07-17 HEAD `5857824c2`+working tree에서 84/84 suite·2-process 10/10·stress 3/3·ASAN/UBSAN/TSAN·surface gate·C ABI smoke·초기 internals 기록·no-hit 통과. 최종 internals 확정은 S5-11/12. known risk 4=TSAN 기존 기계 3계열+MIXED source 도달성(§8.1 S4-05A) |
| S5 Core review loop | 완료 | 16 | 0 | 4 | iteration 1~9 기록은 각 finding ledger에 보존. iteration 10(`a4e91c01d`): Codex 7건+Sonnet 1건 병합 8건(scheduler lost-wakeup, generation 고정, timeout ABA, monitor UAF, join flags, acceptor errno, 테스트 단위, stale internals→S5-11 이관) 일괄 수정, 85/85 → `c1c579ad1`. iteration 11(새 §2 절차): Sonnet CLEAN·Codex NOT CLEAN(수정분 신규 반례 5건: monotonic clock 앵커, actor join task 미회수, monitor 등록 재생성 race, Windows errno, 회귀 테스트 부재) 일괄 수정, 85/85 → `7f9d3e315`. iteration 10~16 반복(11부터 새 §2 절차, 리뷰어 문서 산출물만). 계열별 root-cause 수정: scheduler lost-wakeup/generation/timeout ABA/monitor UAF·등록원자성/error-atomicity 전 계층(operation transaction·completion 선예약·detach primitive·scheduler 무할당 봉인). iteration 16 `1f247af7a`에서 Codex·Sonnet 모두 `CORE REVIEW CLEAN`(세 축). 종료 검증: CTest 86/86, ASAN 7/7 report 0, surface gate·package metadata·diff-check PASS, TSAN 신규 Mesh/monitor race 0(기존 auto-HWM 14+mailbox 1 유지). S5-11/12 internals 확정 커밋 `2128ae91c`. known risk 4=S6 이후 sanitizer gate로 이월 |
| S6 Core release candidate | 완료(로컬 종결) | 0 | 0 | 0 | RC tag `core/v10.0.0-rc.1`. local Conan create·consumer smoke(`zlink 10.0.0`) 통과, SONAME 10, stable package 부재. GitHub native artifact·conan-release CI는 S11 외부배포로 이월(build.yml workflow-file issue·conandata sha256) |
| S7 bindings·framework 공통 준비 | 완료 | 0 | 0 | 0 | RC artifact 동기화(libzlink 10.0.0→4 lane native), 제거 정책 검색 문자열·공통 smoke 정의 고정, Python/Go/Rust 보류. release workflow는 S11 이월 |
| S8-CPP lane (C ABI+C++ bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-4 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~3 결함(ownership/claim수명/metadata/transfer/dead-code 연쇄) 전량 해소, no-hit ZERO, 라이브러리+15samples green. low 4건 follow-up(`iteration-4/low-followups.ko.md`). 다음=cpp framework 미러 |
| **S8-DN lane (.NET bindings→framework) [참조 lane]** | framework 구현 완료·UnitTests green | 0 | 0 | 0 | bindings CLEAN. framework compile-green + core-correct(lifecycle·stream·relay·transfer) + **S8-02/02A/03/05/06/16 완료**(AddRouteMesh 빌더·RouteMesh DI·node/channel handler dispatch·전송 배선·ready/claim pump·MeshName uniqueness, 구 AddSpotMesh/bridge 제거, build 0/0). **UnitTests 677/683 green**(6 fail=doc-regression, S8-09/10/17 추적). **MeshNode 생성 EINVAL 근본수정**(mesh-name 배선)이 native gap 3건 해소. 잔여=binding-surface gap batch(metadata·actor-row → S8-06A/04 완결)·samples/E2E(S8-09/10)·`DOTNET REVIEW CLEAN`(S8-13~15)·internals(S8-17/18). Logical Multicast는 별도 publish NODROP 옵션 없이 Core 내부 ROUTER의 HWM·timeout 계약을 그대로 사용하도록 S8-07에서 정리 |
| S8-JVM lane (Java/Kotlin bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-5 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~4(raw-layer ABI·router_recv_part arity·recv_handler 재매핑·C bridge dead 함수) 전량 해소. Java FFI/Panama, C bridge 실빌드 검증, 제거심볼 게이트 EMPTY. low 2 follow-up. 다음=jvm framework 미러 |
| S8-NODE lane (Node.js bindings→framework) | **bindings CLEAN** | 0 | 0 | 0 | **bindings 게이트 통과**(iter-4 두 리뷰어 R1 opus·R2 Sonnet 모두 `BINDINGS REVIEW CLEAN`). iter-1~3(enum 값·RouterSocket·kind_data·transfer·ready-handler·option 테이블) 전량 해소, no-hit 0, addon+tsc green. low 4건 follow-up. 다음=node framework 미러 |
| S11 Core stable·bindings 외부 배포·최종 검토 | 미착수 | 0 | 0 | 0 | - |

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

## 8. S4 — Core 구현·제거 코드 정리와 정식 spec 일치

### 8.1 red gate와 공개 API

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-01 | contract red test 작성 | 새 API 부재와 제거 API 존재를 test가 먼저 실패로 증명 | 완료 | surface gate red→green 완료(`contract_public_surface` PASS: 196 export 정확 일치·제거 identifier 0·한영 C block 일치). spec 절별 계약 test 3파일 확대 완료: `test_mesh_node_basic` 8, `test_mesh_peer_admission`(2-process) 10, `test_mesh_monitor_matrix` 6, `test_mesh_stress` 3 — 전부 green (2026-07-17 HEAD `5857824c2`+working tree) |
| S4-02 | public header를 10.0.0 spec에 맞춤 | 함수·type·enum과 result signature 일치 | 완료 | header 폐쇄가 frozen spec(52파일 `5bd7451d…`)과 일치(surface gate PASS, C/C++ compile OK). 신규 service header 6개 생성, 설치 규칙 포함 |
| S4-03 | export와 ABI 목록 갱신 | 새 symbol 존재, 제거 symbol 부재 | 완료 | `libzlink.vers` formal FUNC 196 명시 목록, `nm` 대조로 export=formal 정확 일치·제거/internal export 0(`contract_public_surface` PASS). SONAME 10 |
| S4-04 | MeshNode lifecycle과 handle kind 구현 | 생성, bind, start, drain, destroy 계약 통과 | 완료 | lifecycle 상태표·child EBUSY·shutdown deadline revoke(recv ESHUTDOWN/release 안전)·reply-after-STOPPED ESHUTDOWN까지 test green(`test_mesh_node_basic`, `test_mesh_monitor_matrix`) |
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
| S5-09 | 리뷰 종료 뒤 Core 종료 검증 | 두 reviewer의 `CORE REVIEW CLEAN` 뒤 ASAN·UBSAN·TSAN, 공개 API·제거 항목, package metadata와 package·consumer gate 통과 | 미착수 | iteration 10 전 같은 source의 예비 결과는 전체 85/85, ASAN/UBSAN 42/42, TSAN 신규 Mesh race·잠금 순환 0, surface/package gate PASS였다. 새 절차에서는 clean 리뷰 뒤 종료 검증을 실행해 이 행을 닫는다 |
| S5-10 | 두 리뷰어 전체 재리뷰 | 어느 축을 수정했든 Core 전체 scope와 I1·I2·I3 전부 재검토 | 완료 | iteration 16에서 두 리뷰어가 최신 snapshot 전체 scope를 재검토하고 세 축 모두 CLEAN |
| S5-11 | 확정 Core internals 갱신 | 두 review clean과 S5-09 종료 검증 뒤 최종 source의 ROUTER 배선, mailbox·ready·claim·batch, lock·thread, timeout과 Actor·STREAM lifecycle을 `core/doc/internals/`에 반영 | 완료 | 커밋 `2128ae91c`: services-internals §4·§8 신규 구조(timeout task 소유·detach primitive·µs generation·monitor pin·무할당 scheduler) 반영, stale SPOT 참조를 MeshNode dispatch로 교체 |
| S5-12 | Core internals 확정 검사 | S5-11 문서와 최종 source·구조 test·diagram·link의 차이 0개. 문서 검사만으로 구현 전체 재리뷰를 열지 않음 | 완료 | stale 식별자(spot_sub_recv 등) no-hit, internals가 가리키는 소스 파일 전수 존재, `git diff --check` 통과. 코드 결함 미발견으로 재리뷰 미개방 |

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
sha256)의 CI 경로 정리는 실제 외부 배포가 일어나는 **S11로 이월**한다. RC
tag `core/v10.0.0-rc.1`은 소스 참조로 유지한다(로컬 tarball로 sha256을 확정해
검증했고, GitHub archive tarball의 재현 안정성 판단은 S11에서 한다).


| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S6-01 | 리뷰된 version source 불변성 확인 | S5 clean commit의 VERSION, 두 public header와 CMake가 tag commit과 동일 | 완료 | `git diff 1f247af7a..f864e4325 -- core/CMakeLists.txt core/include core/src` 변경 0. VERSION 10.0.0·SOVERSION 10 불변 |
| S6-02 | 리뷰된 ABI와 Conan metadata 불변성 확인 | S5 clean commit의 SOVERSION 10과 선택한 `10.0.0-rc.N` source가 RC tag commit과 일치하며 stable `10.0.0` URL은 S11 전까지 resolve하지 않음 | 미착수 | - |
| S6-03 | 리뷰된 release note 불변성 확인 | S5 clean 뒤 공개 기능과 검증 결과 변경 없음 | 미착수 | - |
| S6-03A | RC/stable workflow guard 검증 | RC tag의 `prerelease=true`, `zlink/10.0.0-rc.N` create와 Conan upload skip, stable tag의 `zlink/10.0.0` create와 publish-required failure test 통과 | 완료 | build.yml `Detect release channel`이 `-rc.` → `prerelease=true`, core-conan-release.yml이 push 이벤트 RC에서 upload skip·stable에서 publish-required로 확인 |
| S6-04 | RC 전 local gate 실행 | clean build, full test, 선택한 RC source entry의 local Conan create, package, symbol과 SONAME 통과 | 완료 | 로컬 build 86/86. `git archive core/v10.0.0-rc.1` tarball(sha256 `abed0b94…`)로 `conan create zlink/10.0.0-rc.1` 성공(libzlink.so.10.0.0·SONAME 10·헤더 14). with_tls=False+WS 조합의 기존 transport 빌드 결함(`options_t::tls_hostname`, mesh scope 밖)은 별도 추적, 기본 옵션에서는 무관 |
| S6-05 | RC commit과 tag 생성 | 검증된 commit에 순번 `core/v10.0.0-rc.N` tag 생성·push. stable tag 없음 | 완료 | `core/v10.0.0-rc.1` → commit `f864e4325`(core 소스는 리뷰본 `1f247af7a`와 동일) 생성·push. stable tag 없음 |
| S6-06 | native build workflow 감시(→S11 이월) | `.github/workflows/build.yml`이 RC tag/commit으로 성공 | 후속 분리 | 로컬 검증 종결 방침에 따라 GitHub native artifact 경로는 S11 외부배포로 이월. 참고: build.yml이 2026-07-17 이후 GitHub "workflow file issue"로 파싱 거부(마지막 성공 07-16 23:55) — S11에서 실제 오류 확인·수정 |
| S6-07 | GitHub pre-release 검증(→S11 이월) | RC native asset을 prerelease로 게시하고 stable Conan remote publish는 실행하지 않음 | 후속 분리 | 로컬 검증 종결 방침. 외부 배포는 S11 |
| S6-08 | RC asset 검증(→S11 이월) | platform archive, checksum, source archive와 header 일치 | 후속 분리 | 로컬 검증 종결 방침. 외부 배포는 S11 |
| S6-09 | shared library 검증 | filename 10.0.0, SONAME 10과 제거 symbol 부재 | 미착수 | - |
| S6-10 | local Conan package 설치 검증 | 실제 RC tag source로 만든 isolated local `zlink/10.0.0-rc.N` consumer가 build·실행하고 stable `zlink/10.0.0`은 public remote에 없음 | 완료 | isolated consumer(conanfile.txt+CMake)가 local `zlink/10.0.0-rc.1`을 소비해 build·실행: `zlink 10.0.0` 출력, rc=0. `conan list`에 stable `zlink/10.0.0` 없음(rc.1만). conandata 임시 수정은 원복 |

S6 완료 gate:

- [x] local Conan package(`zlink/10.0.0-rc.1`)가 생성·consumer smoke 검증됐다(SONAME 10, `zlink 10.0.0`).
- [x] GitHub native artifact·conan-release CI 경로는 S11 외부배포로 명시 이월했고, 실패를 성공으로 오판하지 않았다.
- [x] RC source tarball sha256 `abed0b94…`와 tag `core/v10.0.0-rc.1`이 S7 입력으로 기록됐다.
- [ ] `core/v10.0.0` stable tag, stable GitHub Release와 Conan remote package는 아직 존재하지 않는다.

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
| S7-00 | RC tag parser와 runtime version 분리 | local-package script가 `core/v10.0.0-rc.N` asset tag는 그대로 사용하고 C/header runtime version은 숫자 `10.0.0`으로 기록하며 version macro에 `-rc.N` suffix를 남기지 않음 | 미착수 | - |
| S7-00A | RC fixture·provenance test | RC/stable tag fixture가 version marker, source SHA, checksum과 asset URL을 검증하고 malformed tag·suffix 잔존·checksum 불일치에서 실패 | 미착수 | - |
| S7-01 | Core RC artifact 동기화 | update-zlink-libs script가 `core/v10.0.0-rc.N` asset의 runtime version 10.0.0, source SHA와 checksum을 검증하고 복사 | 완료 | `sync-local-core-libs.sh`가 로컬 build의 libzlink 10.0.0(SONAME 10)과 헤더를 4개 lane native로 동기화. native 산출물은 release 입력이라 커밋하지 않음(lane 빌드 입력) |
| S7-02 | binding API inventory 작성 | 적용 대상 C ABI·C++·.NET·Java/Kotlin·Node 전체 대응. Python·Go·Rust는 보류 대상으로 표시만 하고 갱신하지 않음 | 완료 | 현재 bindings는 9.0.4 API(cpp CMake VERSION 9.0.4). 제거 대상 hit(전환 전 기준): SpotNode 95파일·spot_node 42·bridge 29·dispatch_worker 10, selectNode 0. lane 소스 규모: cpp 76·dotnet 252·java 283·node 253. Python/Go/Rust 보류 |
| S7-03 | 제거 wrapper와 generated API 정책·no-hit 목록 | SpotNode mode, bridge, Core dispatch worker option, remote subject query, Spot·Actor–STREAM service `*_part`, Actor join/lifecycle 전용 receive·reply, channel-dealer event와 old alias no-hit 검색 문자열 고정. 모든 raw socket용 channel metadata wrapper 유지 기준. 실제 적용은 각 lane | 완료 | 검색 문자열 고정: `SpotNode`·`spot_node`·`bridge/RouteBridge`·`dispatch_worker/DispatchWorker`·`selectNode/selectOne/selectMany`·`*_part`(Spot/Actor–STREAM). 전환 전 hit는 S7-02 기록. 각 lane이 전환 후 no-hit 달성을 bindings clean 조건으로 검증 |
| S7-07 | bindings release workflow 수정(→S11 이월) | RC와 최종 `core/v10.0.0`의 동일 source SHA·checksum 검증 및 release asset 사용, tag run에도 provenance 필수. 네 언어 공통 workflow 골격 | 후속 분리 | 로컬 배포 방침에 따라 release workflow(외부 배포)는 S11에서. lane은 로컬 package로 검증 |
| S7-08 | `.NET` native 입력 경로 통일 | workflow와 pack이 `bindings/dotnet/native/<rid>/`만 source 입력으로 사용 | 후속 분리 | S8-DN lane framework 단계에서 확인 |
| S7-SMOKE | 공통 smoke matrix 정의 | node/channel/Spot direct send/request와 Logical Multicast metadata snapshot·malformed·1024 경계·relay·reply 비자동복사, ROUTER backpressure·부분 전달, batch reset/retain과 shutdown을 각 lane이 실행할 공통 scenario로 고정 | 완료 | 공통 scenario를 spec(core/doc/spec/core/service)과 framework E2E inventory에서 확정. publish 전용 NODROP option은 matrix에서 제거. 각 lane의 S8-*-V·S8-SMOKE에서 실행 |

S7 완료 gate:

- [ ] RC artifact 동기화와 provenance test가 통과한다.
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

### 11.3 bindings 독립 리뷰와 외부 배포 전 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-15 | Codex agent bindings 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S7-16 | Claude Sonnet bindings 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S7-17 | finding 일괄 수정과 일반 검증 | finding을 한 번에 수정하고 일반 build와 bindings 전체 테스트 통과 | 미착수 | - |
| S7-18 | 두 리뷰어 전체 재리뷰 반복 | 세 축과 두 stage 결과가 모두 `CLEAN`; 둘 다 `BINDINGS REVIEW CLEAN` | 미착수 | - |
| S7-19 | 리뷰 종료 뒤 local package 묶음 검증 | 두 `BINDINGS REVIEW CLEAN` 뒤 publish-all-wsl 및 별도 언어 package·consumer 검증 통과 | 미착수 | - |
| S7-20 | 리뷰 종료 뒤 배포 없는 workflow 검증 | 두 `BINDINGS REVIEW CLEAN` 뒤 create_release=false, publish_registry=false로 전체 job 성공 | 미착수 | - |
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
| S8-01 | 검증된 bindings local package pin을 10.0.0으로 갱신 | 중앙 version과 lock·restore 결과 일치 | 완료 | `Directory.Packages.props`와 ZoneWorld 별도 중앙 props의 `ZLinkBindingsPackageVersion=10.0.0`; `dotnet restore Zlink.Framework.sln --force-evaluate` 성공. 존재하는 프로젝트의 restore asset 전수 검사에서 `Systems.Zlink/10.0.0` 208건, 다른 version 0건이며 설치된 nupkg nuspec도 10.0.0이다. NuGet lock mode를 사용하지 않아 `packages.lock.json`은 0개이고 restore graph가 실제 해석 결과를 소유한다. |
| S8-02 | AddRouteMesh·ChannelName 구현 | 복수 logical membership과 정식 `.NET` interface가 source snapshot과 일치 | 완료 | AddRouteMesh(meshName):IZLinkMeshNodeBuilder + ChannelName:IZLinkMeshChannelBuilder (spec 05 exact-interface), 구 AddSpotMesh/SpotNodeBuilder 제거 no-hit 0, build 0/0 |
| S8-02A | RouteMesh runtime-options DI 구현 | 기존 `IZLinkChannelRuntimeOptions` 제거, `IZLinkRouteMeshRuntimeOptions` singleton 등록, MeshNode socket setter의 startup-only 오류와 runtime channel Weight 반영 통과 | 완료 | IZLinkRouteMeshRuntimeOptions singleton DI, 구 IZLinkChannelRuntimeOptions 제거, MeshNode socket setter startup-only + runtime Channel Weight 반영 |
| S8-03 | MeshNode-owned handler·Spot·Actor 등록 구현 | channel·route handler context와 모든 Spot·Actor builder 멤버 보존 | 완료 | NodeSend/NodeRequest→route handler(ZLinkRouteHandlerInvoker), ChannelSend/ChannelRequest→channel-membership handler(ChannelName keyed), reply token 경유. Spot/Actor builder 멤버 보존, DI 스캔 |
| S8-04 | location descriptor와 connection planner 구현 | Redis 자동 discovery와 manual `IZLinkMeshPeerConnections`가 같은 admission을 사용하고 MeshName 범위, expected RID pin, lifecycle generation, descriptor revision, source 병합과 ready index 검증 | 완료 | **ZLinkPeerLocation→ZLinkMeshNodeDescriptor migration 완료**(2026-07-18): 06-location-store exact 계약(descriptor/spot/actor row·store 5-role·change stamp ulong·watch internal화), (LifecycleGeneration,DescriptorRevision)/(SpotGeneration)/(actorGen,MembershipEpoch) 단조 guard, planner/reconciler descriptor 전환(draining=신규선택 제외·revision 단조·claim 후 generation stamp renew), route store·ZLinkActorSessionRouteLifecycle·ZLinkPeerCapabilities 제거(40 §2.3), spot pub/sub plane=`<mesh>#pub` namespace 분리, 진단표면 MeshNode-only 재편, Redis 3-kind(tag `mesh`) Lua/json codec 정렬(41 §2 writer-json 보존). Framework+AspNetCore+Redis build 0/0, UnitTests 651/657(실패 6=기존 doc-regression 추적분). Redis.Tests migration 완료(로컬 Redis 실측 36/36 green, cross-language 2건은 하네스 env skip) + pinned fixture(mesh-node-descriptor-v1/actor-location-v2) byte-for-byte 검증 green. 잔여(타 행 이관)=ContractTests migration(S8-02 builder 제거로 인한 선행 파손, S8-DN-V)·multi-source merge E2E 검증(S8-09/10)·OwnerNodeGeneration join(리뷰 단계) — gap 90 §12.33 |
| S8-04A | Redis Actor transfer authority 구현 | participant-set CAS, transfer token, lease, prepared/commit/abort crash recovery, unsupported store startup failure와 distributed transfer E2E 통과 | 완료 | IZLinkActorTransferStore(prepare/commit/activate/abort/takeover/resolve, participant-set CAS·transfer id·recovery lease) — in-memory state machine + Redis Lua per-transition. coordinator cross-node 호출+distributed E2E는 build-only 미실행. commit actor-row rewrite는 actor-row-shape gap 90 §12.27 결합(코드에 기록) |
| S8-04B | Redis production 기본 정책 구현 | Redis extension 명시 등록, 자동 discovery·분산 Spot/Actor 주소 조회를 사용하면서 location store를 등록하지 않은 구성의 startup failure, 사용자 store capability와 test-only in-memory 경계 검증 | 완료 | production fail-fast(distributed Spot/Actor/transfer without store → ValidateSpotNode 실패, auto-discovery는 store 없이 선택 불가로 구조적 차단) + UseInMemoryLocationStores internal/test-only 경계 검증 |
| S8-05 | channel/direct/Spot/Actor 전송 연결 | bindings MeshNode public API만 사용 | 완료 | compile-green 전환(Option B): 프레임워크-소유 dispatch record가 MeshNode public API만 사용해 channel/direct/Spot/Actor 전송 배선 |
| S8-06 | ready/claim pump 구현 | infrastructure 우선 drain, Node·Spot·Actor keyed scheduling과 claim leak 0건 | 완료 | DrainReady pump seam이 record를 per-owner serial-executor 큐로 fan, claim은 finally 해제(leak 0). MeshOperationId↔Completion 콜백 테이블 |
| S8-06A | S/S metadata 연결 | Node·Channel·Spot direct send/request와 Logical Multicast의 mutation snapshot, immutable handler view, malformed ingress, 1024 경계, relay allowlist, reply 비자동복사와 일반 reply metadata 미지원 통과 | 진행 | codec+receive-seam decode(기존) 위에 **call 표면 exact 정렬 완료**: `IZLinkMetadataCall<TSelf>`+`TrySubmit/SubmitAsync`+`ZLinkSubmitStatus/Result·ZLinkLogicalMulticastDetail/PublishResult`(spec §2 그대로), call 10클래스 이관, `ZLinkCallMetadata`(last-write-wins·1024 encode) 단일 소유. **Spot direct·publish는 metadata를 seam(SubmitResult 반환+metadata+spotGeneration 인자)→binding까지 관통**, publish는 fan-out detail 반환. 발견 결함 수정: seam `SendToSpot` generation 0 하드코딩(Core EINVAL) → snapshot.Generation 관통(actor join·handoff 경로 포함). 잔여 2조각은 gap 90 §12.36 명시(mesh TrySubmit 동기 admission, node-direct/channel metadata router-seam 관통 — NotSupported fail-fast). classic dealer plane은 TrySubmit 구현·metadata NotSupported(§6 범위 밖). UnitTests 679/685(추적 6 유지) |
| S8-06B | Spot timer 연결 | `Task.Delay` 기반 tick이 lifecycle generation과 cancel 규칙을 거쳐 keyed scheduler에 제출되고 Core timer FFI를 사용하지 않음 | 완료 | Task.Delay tick이 per-owner keyed serial executor 경유, stop-token/generation gated, Core timer FFI 미사용 — 검증만(변경 불필요) |
| S8-07 | Logical Multicast publish 옵션 제거 | Core·bindings·framework에 publish 전용 NoDrop 표면이 없고 기존 ROUTER backpressure만 사용 | 리뷰 중 | Core C ABI의 Mesh publisher/Spot option과 네 bindings wrapper, .NET framework config·적용 코드·전용 테스트를 제거. raw PUB/XPUB의 기존 `ZLINK_PUB_OPT_NODROP`은 별도 socket 계약이므로 유지. Core CTest·ASAN, C/C++·.NET·Java·Node focused build 후 독립 재리뷰 진행 |
| S8-08 | 기존 topology API와 runtime 제거 | v10 plan·review record만 제외하고 builder, registration, production `UseInMemoryLocationStores()`, bridge, Spot·Actor–STREAM service part와 Actor join/lifecycle 전용 wrapper, test와 현재 docs no-hit | 진행 | 구 spot-node builder·SpotRouteBridge wrapper 제거, production UseInMemoryLocationStores test-only화 + 미등록 store startup fail-fast(S8-04B). 전수 no-hit는 리뷰 단계 확정 |
| S8-09 | `.NET` sample 전환 | 분산 sample은 공식 Redis extension을 등록하고 지정된 manual sample은 `IZLinkMeshPeerConnections`를 사용하며 S2 inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 완료 | **compile 이행 완료**(2026-07-18): 8개 sample + testapps 전부 AddRouteMesh/Listen/PeerConnections + TrySubmit 표면, EnablePubSub plane 제거(logical multicast), TicTacToe manual peer=PeerConnections.Connect(rid,ep), build 0/0. ChannelName membership 필수(validator)로 전 mesh 등록에 기본 membership 추가, 기본 ZLinkBindingsPackageVersion 9.0.8→10.0.0, 러너의 pub/sub wait 제거. **런타임 근본수정 체인 완료(2026-07-19, f69014b4b) — TicTacToe 클라 시나리오 cross-node 전 구간 통과**: (1) bind hang 근본원인=pump의 DrainReady/claim.Receive 기본 blocking(첫 claim 안에서 pump 스레드 park→전 owner 기아)→DontWait 전환+poison record 생존, (2) binding ActorInterop.FromNative가 빈 NodeRid/ActorId 레코드 전면 거부→허용, (3) completion terminal 값을 SubmitResult로 오캐스팅(Conflict→InternalError)→zlink_request_result_t 직매핑, (4) BindActor NotConnected 레이스(코어 세션 liveness=비동기 observer)→timeout 내 재시도, (5) **location row 생성값 도메인 교정**: row는 core generation 원문(spot row=core spot lifecycle generation, claim-then-stamp 제거, store renew guard=owner-only, 41 §3.1 store gen≠row generation)—cross-node SpotRequest CONFLICT/ESTALE 해소, (6) actor-owner ready record의 spot 귀속(ActorLookup)+ownerActor 스레딩, (7) **cross-node 세션 relay plane 신설**(spec 31 §6): bound-session push→세션 노드(`$zlink.session.push-relay.v1`), 이주 액터로의 세션 frame→액터 노드(`$zlink.actor.frame-relay.v1`), no-bind reply는 pump가 등록한 core reply token으로 회신, remote join commit이 세션 node rid를 구체값으로 전달, source-side 이주 정리가 세션측 바인딩 레지스트리 보존, (8) spot pub/sub publish 채널=mesh channel(topic=filter)로 교정(채널=topic placeholder는 원격 불가). UnitTests 651/657(6=doc-regression 추적분) 유지. **TicTacToe run_sample.sh 전 구간 통과(exit 0, 3c051db71)**: 재입장 admission 크래시 근본원인=ToActorJoinRequest가 zero-filled SourceActor 사용→control payload의 CurrentActor로 교정. **원격 액터 세션 바인딩 일반화**(Bingo 유형: 세션 호스트가 타 노드 소유 액터를 인증 시 바인드): native bind는 로컬 액터 전용이므로 remote ref는 native bind 생략+frame-relay plane으로 inbound 상대(재시도 포함), bound-session coordinator 노드 폴백=router-capable 노드(팩토리 없는 세션 호스트). EnableActorDispatch를 Bingo/SupportChat/DeliveryDispatch/ZoneWorld-Gateway/GameQuest stream node에 반영. **샘플 진행(2026-07-19, ~4514b0f02)**: TicTacToe·Bingo·SupportChat·DeliveryDispatch `run_sample.sh` 통과 실측(각 exit 0). 추가 근본수정: (9) publish/route-spot `TrySubmit`=one-shot DontWait 실구현(§12.36 stub 해제, route-channel fallback만 stub 유지), (10) 세션 식별 없는 frame이 구체 세션 바인딩을 clobber하지 않도록 EnterDispatch 가드, (11) observed-generation 가드에 liveness 선행(사망 owner row는 세대 floor 미기록)+확정 miss 시 actor floor forget→재생성 노드의 신선 row resolve 복구(ZoneWorld replacement 기동 통과), (12) discovery로 다이얼되는 채널 서버는 구체 routing id 필수(descriptor row가 (MeshName,Rid) 키)—SupportChat/ZoneWorld 서버 채널에 rid(ZW는 할당 그룹) 부여, (13) ConfirmRemoteBinding retriable 재시도, (14) 세션 호스트(팩토리 없음)용 router-capable 노드 폴백. **ZoneWorld**: A/G/E 시나리오군 통과; (15) 원격 push 배달의 backpressure 재시도(1건 드롭이 ZW-B1 대기 무산—Delivered/Backpressured/Stale 3상, stale만 드롭) 반영 후에도 ZW-B1 잔존 — MoveMsg는 frame-relay로 정상 dispatch됨을 확인, 상태 notify에 이동 반영이 안 되는 층(스팟 tick vs move 적용) 클라 payload 계측 필요. C1·D1/D2(ops notify·announce fanout)도 잔여. **GameQuest**: 러너 stale spot-pub 포트 probe 2건 제거 후 시나리오 후반 request timeout 1건 잔여(quest notify는 흐름 확인). **주의**: TicTacToe/DD 재검은 병렬 perf 부하(load ~8)에서 flake — idle에서 재확인 필요. **ZW 원인 축소(2026-07-19)**: publish detail 계측으로 zone-node 간 cross-node 전량 실패의 공통 원인= zn1↔zn2 mesh admission 부재 확정(SnapshotRemoteTargets=0; gw01↔zn1/zn2는 admitted, 같은 Redis rows 사용). descriptor rows(zn1/zn2/gw01)는 정상 발행 확인(런 중 Redis 실사). planner pairwise-initiator(낮은 hex rid가 dial: zn1→zn2, gw01→both)와 rows는 정합 — 다음 계측 지점=zn1의 desired set/executor ConnectPeer 실행 여부·wire admission(HELLO) 실패 여부(ZLinkAutoConnectLoop/Executor에 debug 지점 필요). **ZW 원인 재축소(e04358939)**: admission은 정상(늦은 tick publish가 Remote 2/2 — 초기 0은 기동 타이밍이었음, autoconnect dial 계측으로 zn1→zn2 dial 확인). push 유실 2종 근본수정: backpressure 재시도 + rebind release→bind 갭(NoBinding)은 재시도·다른 세션(WrongSession)만 드롭 — B1 런은 배달 무손실 확정. **그럼에도 B1 잔존**: MoveMsg dispatch까지 확인(프레임 relay·핸들러 오류 없음), ZoneStateNotify payload에 이동 반영이 안 되는 층이 남음 → 다음=클라이언트 수신 payload 덤프(GameClient에 env-gate 덤프 추가)로 predicate 불일치 내용 확인. **수락 기준 통과(2026-07-19, 078899263)**: `samples/run_samples.sh` 전체 스윕 exit 0(TicTacToe·Bingo·SupportChat·ShoppingMall·DeliveryDispatch·GameQuest) + ZoneWorld 자체 러너 `zoneworld=completed`(A/B/C/D/E/G 전 시나리오군). ZW 마감 근본수정 3건: (16) actor takeover claim이 기존 row의 MembershipEpoch를 계승(epoch 리셋이 전이 후 row를 lagging으로 영구 은닉→세션 relay가 구 노드에 고착), (17) observed actor 축=epoch-major(generation은 노드별 카운터라 cross-node 전이에서 재시작)+relay resolve가 transfer-commit 창을 재시도, (18) 신규 공개 옵션 `ZLinkLocationOptions.ObservedMeshNames`(관찰자 호스트의 mesh 열거 확장, api snapshot 재생성)·fanout publisher rid 광고. GameQuest 마감: identity-less 세션 reply는 현재 바인딩으로 송신(빈 native token 게이트 제거). ZW-B1 배달 무손실 검증 프로브(ZONEWORLD_DEBUG_INBOUND) 포함. env-gate 계측(ZLINK_DEBUG_PUMP/ZONEWORLD_DEBUG_INBOUND) 제거는 S8-12A 이전 |
| S8-10 | `.NET` framework E2E 전환 | `e2e/run_e2e_all.sh`의 전체 config 통과 | 진행 | **compile 이행 완료**(2026-07-18): 26개 e2e 프로젝트 build 0/0 — 호스트 AddRouteMesh 이행, weight admin=IZLinkRouteMeshRuntimeOptions.Channel(mesh,channel).Weight(provider 채널→mesh membership 전환: Resilience/StoreFailure/RuntimeMonitoring), 운영 표면 descriptor 조회(ListMeshNodeDescriptorsAsync), store 장식자(Delayable/PollingOnly/CleanupGated) 새 계약 재작성, evidence의 spot/actor 목록은 resolve-only 계약에 따라 소거. **런타임 진행(2026-07-19, ~a27e6c6c4)**: (A) 전 e2e EvidenceStore의 단일-release 세마포어가 동시 /evidence/wait를 기아시키는 lost-wakeup(교체식 TCS pulse로 전면 교정, 10개 사본) → **LocationMessaging 16/16 통과**. (B) **PubSub config-3 store-free 전환 완료**: Redis package·provisioning·인자와 peer location endpoint를 제거하고 subscriber가 manual publisher endpoint를 사용하도록 이행했다. publisher 재시작은 subscriber socket의 `Disconnected`·`ConnectionReady`로 판정하며, monitor 활성화 뒤 최초 연결이 발생하도록 runner·late-subscriber의 연결 gate를 정렬했다. Publisher·Subscriber·Client build 0 warning/0 error, 구현·runner의 Redis/location-row scoped no-hit, `PubSub/run_e2e.sh` PS-A1~C1 7개 전체 exit 0. (C) RegistrationCodec 통과. (D) **핵심 표면 구현**: ① `IZLinkRouteClient.SendToChannel/RequestToChannel(mesh,channel)`(스펙 02 exact, entry-spot seam 관통, metadata+one-shot TrySubmit) — classic dealer는 mesh router와 와이어 비호환이라 caller도 mesh member가 되는 것이 10.0.0 계약, ② `IZLinkRouteMeshRuntime`(스펙 05 §8/50: snapshot·event polling 파생·shared drain 위임; core 미노출 필드는 gap §12.37), ③ IZLinkActorJoinCall의 계약 외 Submit 제거, api snapshot 재생성 유틸(scratch)로 갱신, ContractTests 46/46. (E) **근본수정 3건**: local row가 LifecycleGeneration=0 하드코딩(core는 wall-clock 단조 할당—restart 순서의 기준) → node MeshStatus 값 관통; `:0` ephemeral bind가 row에 literal 광고 → resolved endpoint 광고; 교체/제거된 peer의 admitted lifetime을 명시 은퇴(`DisconnectPeerLifetime(rid,gen)` seam, initiator 비게이트—core가 same-RID successor admission을 predecessor disconnect 뒤로 큐잉). seam request submit이 터미널 실패를 backpressure로 뭉개던 것 표면화. (F) ResilienceLifecycle consumer+storm fleet를 mesh member로 이행(할당 rid+ephemeral bind, IZLinkRouteClient 호출, mesh peer snapshot 기반 연결 evidence—peers 테이블 endpoint는 lifetime 교체 후 stale이라 row 광고 endpoint로 라벨), 구 9.x 단언 재정렬(down-window=RequestTargetNotFound, 스펙 05 §13.1) → **RL-A1 통과**. TD-A1 터미네이터 단언 스펙 정렬(request/join=Async/Yield only). SpotService 러너 stale spot-pub probe 제거. **잔여**: RL-A2(remap) — **원인 축소 완료**: kill−9 후 consumer의 old pipe EOF 감지가 수 초 지연(관찰: down-window 내내 peer ready 유지), replacement 재승인 뒤 지연 도착한 stale pipe-term이 `handle_peer_down`(rid 단일 키, mesh_wire_admission.cpp "Every lifetime sharing this RID rides the same transport")에서 admitted successor를 ENOTCONN ERROR로 강등 → teardown이 다음 admission을 죽이는 자기 영속 flap 후 ERROR 고착. framework 측 은퇴(DisconnectPeerLifetime)는 이미 관통(retire 시점엔 이미 CLOSED=605 NotFound 확인). **core 소관**(pipe-term을 현재 route pipe와 대조해야 함)이며 병렬 core 작업(asio engine 수정 중, 미커밋 .so 혼재)과 얽혀 있어 core 수렴 후 재검 필요, **추가 진행(2026-07-19, ~014ee95da)**: (G) spot resolve에 confirmed-miss forget(actor 규칙 미러) → 재기동 spot의 신선 row resolve 복구 → **AutomaticTurnDispatch 전체 통과(exit 0)**(TD-F5 shutdown recovery 포함). (H) row 부재만으로 admitted transport를 은퇴하지 않도록 환원(SF-B2 계약: store 장애는 기존 연결로 계속) — SF-A1/A2/B1 통과. StoreFailure consumer도 mesh member 이행(장식자 store가 slot allocation 미제공이라 고정 rid). (I) **원격 세션 disconnect 전파 구현**: 세션 transport 종료 시 원격 바인딩 액터에 disconnect frame을 ForwardPart(원격=frame-relay) 경로로 전파(기존 경로는 native bound-session 기록만 정리) — TA-A4 통과. (J) EnterDispatch: 세션 identity 없는 frame은 바인딩 생성 금지(미바인딩 액터에 유령 empty binding 생성 → push가 NotBound 대신 성공하던 결함) — TA-A2 통과. (K) manual endpoint disconnect가 admitted lifetime엔 core 정확 (rid,generation) disconnect 사용. TA-B3 단언을 스펙 05 §13.1 변환표로 재정렬(명시 disconnect→member 제거→ActorRouteNotFound)+재승인 폴링. **core-blocked 군**(동일 계열, 병렬 core asio/mesh_wire 작업과 얽힘): ① RL-A2/TA-B3-recovery — 동일 프로세스 lifetime의 disconnect→reconnect 재승인이 CLOSED row ESTALE로 영구 거부(core admission: ERROR/CONNECTING만 same-lifetime 재수용, mesh_wire_admission.cpp)+kill-9 후 stale pipe-term이 successor를 ENOTCONN 강등하는 자기영속 flap, ② SF-B2 — store 회복 창에서 zombie transport(원인 미상, core EOF 감지 지연 관찰). **추가 진행 2(2026-07-19)**: (L) 세션 relay resolve에 raw-row presence 관통 — 확정 miss(파괴/미존재)는 fail-fast, row-present(전이 중 gen-0 claim 창)만 재시도 → SM-B8(파괴 액터 오류 회신) 통과. (M) §12.36 잔여 stub 2종 실구현: spot outbound classic 채널 send TrySubmit(one-shot dealer DontWait)·spot-direct send TrySubmit(TrySendToSpotViaRouterChannelOnce) → SM-C2/C3 통과. (N) seam request submit의 NotConnected를 ZlinkSubmitException으로 던져 submitter의 retriable 분류 복원(터미널만 framework 예외) — blocking 호출이 admission 창에서 즉사하던 회귀 교정. SM-F6 첫 cross-node 호출의 admission 레이스는 시나리오 폴링으로 정합(스펙 04 §1.1: blocking submit은 send timeout 경계까지만 대기). **SpotService 진행: 30+ 시나리오 통과, SM-G1(crash-recovery)에서 정지 — crash-kill 후 재기동 노드의 wire 재승인이 core-blocked ①(stale pipe-term flap)과 동일 계열**. **잔여 config**: RuntimeMonitoring(**브리지 구현 완료, e1c2d59e4**: `IZLinkMonitoringOptions.AddMeshNodeEvents(meshName)` — mesh runtime 이벤트 스트림(스펙 50)을 ZLinkMeshRuntimeEvent(IZLinkRuntimeEvent, source=mesh)로 이벤트 버스에 브리지, preflight 검증 포함, ContractTests 46/46. **svc/시나리오 이행 진행**: svc가 AddMeshNodeEvents(mesh)+MeshEventRecorder(peer ready/disconnected→ConnectionReady/Disconnected, endpoint는 snapshot 조회+last-known 캐시)로 이행, MON-A1 mesh evidence 재작성 → **MON-A1·A2 통과**. **MON-A3 해결**: subject 표면을 framework 소유 추적으로 구현(ZLinkSpotSubscriptionTracker — spot seam의 SetSubscription/Dispose에서 (spot,topic) 추적, Subjects()가 이를 반환) → MON-A1·A2·A3 통과. **추가 진행**: MON-A4의 weight 관찰 구현(peer 이벤트 시 descriptor row의 ChannelWeights 조회→PeerAdmissionChanged|value=N evidence, row 광고 endpoint 우선) — drain/failover/restore 구간 통과, 말미 same-rid 신규 endpoint 교체 재승인만 잔존(=core-blocked ① RL-A2와 동형). **B1·B2·C1 통과** → RM 6/9 그린(A1·A2·A3·B1·B2·C1). 잔여=A4 말미(core-blocked ①), A5(HandshakeFailed: 무자격 TCP의 handshake 실패는 peer 엔트리가 없어 폴링 관찰 불가 — core mesh monitor 이벤트의 binding 표면(zlink_mesh_node_monitor_*) 노출 필요), D1(소스명 재정렬 반영; kill→동일 endpoint 재기동 재승인 대기에서 정지 — core-blocked ① 동형, 재승인 실패 evidence로 Disconnected 2회·재-ready 부재 실측). **RM 정리: 6/9 그린 + 3건 전부 core 구간 대기(재승인 flap ×2, monitor binding 표면 ×1)**. 병렬 세션의 NoDrop publisher 표면 제거(스펙+bindings 광역 리팩토링)가 dotnet lane 파일에 동승—UnitTests 총계 655로 갱신(제거된 NoDrop 케이스), 기준선 649/655+6 doc-regression), SpotService(SM-G1+ = core-blocked ①), SpotActorTransfer(**ST-C3 해결, 50a0fe20f**: joined 콜백 실패는 completion 재시도로 회복 불가한 종단 거부 — 타깃이 quarantine+rollback 후 RequestRejected 종단 회신, 소스 completion reconciliation의 terminal 술어에 RequestRejected/ActorRouteNotFound 추가(무한 재시도→timeout 해소). 러너가 runtime-marker 단언의 SPOT_DISCOVERY 게이트 소유. **ST-F1 프레임 소실 3결함 해결**: ① actor client 검증이 transfer 창(claim된 gen-0 row)을 ActorRouteNotFound로 오분류 → presence-aware 통과, ② frame relay가 세션 identity 없는(caller-routed) 전달 프레임을 거부(포워더는 이미 소비 → 무손실 위반) → sessionless 허용, ③ 수신 핸들러 RoutingId.FromHex("")가 빈 세션 hex에서 사망 → 빈 값 가드. 패킷이 target까지 end-to-end 도달·디스패치 확인. **잔여 ST-F1 본질 — 판정 완료(스펙 23 §10)**: §10.1 "Prepare가 admission을 닫은 뒤 도착한 packet은 source backlog 보존, commit하면 target 전달"+ST-C3의 joined-실패=join 거부·소스 복원 계약은 **joined 콜백이 commit-accept 이전(커밋 단계 내)에 완료**되어야 성립 — 현 구현은 joined를 completion 단계에서 실행해 (a) ST-C3에 quarantine 우회가 필요했고 (b) commit-accept 시점 cutover로 ST-F1의 소스 capture 창이 사라짐. **수정 반영**: joined 콜백을 commit 단계로 이동(JoinRoutedActorAsync가 회신 전 CompleteTransferredActorTarget 실행, completion은 replay·publish만) — 게이트 중 소스 capture 창 유지 확인(P1~P3가 소스 backlog 캡처→completion trailing으로 target 순서 replay, `handoff_backlog` 마커 소스 stdout 로거로 발행 확인, ST-F1 전반부 단언 통과). **ST-F1/F2 해결(c2aca9ea9·dcd1fc220)**: ① commit 처리를 per-request 취소에서 분리(joined 콜백이 RPC timeout을 넘겨도 dedup 재시도가 같은 preparation 대기), ② target이 더 이상 인정하지 않는 completion은 종단 RequestRejected(무한 재시도 InvalidOperationException 제거, UnitTests 단언 정렬), ③ backlog frame 복원이 sessionless(caller-routed) frame의 빈 rid byte 허용 — ST-F1 3/3 결정적 통과, 13+ 시나리오. **잔여 ST-F3+**: bound-session cross-move — 게이트 중 native 세션으로 보낸 S1/S2가 소스 pump에 레코드로 표면화되지 않음(actor의 join turn 진행 중 core 큐 대기로 추정; ST-F1의 router-plane send는 같은 창에서 표면화됐음 — 세션 전달 도메인과 claim 생명주기 차이 계측 필요). **후속 수정 반영**: 세션 relay resolve도 transfer 창(row present·미해석)에서 스핀하지 않고 기존 bound ref로 즉시 진행(ST-F1과 동형) → S1~S4 전량 배달·마커 대기 통과·join 수락. capture를 detached 병렬 dispatch(파이프라인)에서 pump 이벤트 순서가 보장되는 ingress로 이동. **후속 직렬화 2건 반영**: entry pump의 actor 배치 dispatch와 frame-relay 수신 dispatch를 per-actor FIFO 체인으로 직렬화(형제 배치 추월 제거). **잔여 flake(~1/3)**: 여전히 쌍별 스왑 발생 — 원인은 다운스트림이 아니라 **세션 호스트의 inbound 처리 동시성**: 같은 스트림 세션의 두 send가 RelayToActorAsync에 병렬 진입해 native relay write(`_relaySender.SendAsync`)가 core 진입 전에 역전(소스 trailing 캡처가 S2,S1로 기록됨을 실측). **배제 결과(추가 조사)**: 세션 runtime inbound는 이미 ZLinkStreamSessionSerialExecutor로 per-session 직렬(추정 오류였음), pump의 RaiseActor→entry 핸들러도 pump 스레드 동기 호출로 순서 보존 — 그리고 ST-F3 경로는 세션 relay가 아니라 **EnableActorDispatch의 native 바인딩 직행**(client→stream node→core actor records→pump). 3자 판별 완료(캡처 지점 계측): 실패 런에서 S1/S2 모두 **ingress에서 pump 순서 그대로 캡처**됐는데 그 pump 순서 자체가 S2,S1 — 프레임워크 전 구간(클라 connector 바운디드 채널 SingleReader FIFO·노드 ingress serial executor·세션별 serial executor·relay await·pump RaiseActor 동기·ingress 단일스레드 캡처) 순서 보존 배제 완료. **역전 창=core `SendBoundActor`→actor record 표면화 구간(core 소관)** → ST-F3 잔여 flake를 core-blocked 군 ⑤로 편입(병렬 core asio/mesh 작업의 미커밋 .so 사용 중이라 core 수렴 후 재검), ObservabilityOps(**OBS-B3 해결(83598f5af)**: ① ZLinkLocationStoreRead가 취소 비협조 store 명령을 WaitAsync로 경계(paused Redis가 응답 보류해도 read timeout 내 강등), ② workflow /evidence의 보조 row 조회에 500ms 경계 — pause 창 관통 관찰 성립, OBS A1~B3 7 시나리오 그린. **OBS-B4 해결**: 원인 2겹 — track-b lease TTL 3s < B3 pause 11s(의도된 lateness 측정이 owner fencing으로 전락해 host 자멸) → 러너가 track 공통 lease 30s 부여, pause 직후 첫 store 쓰기의 multiplexer 회복 지연 → 시나리오 폴링. **OBS 8/12 그린(A1~B4)**. **전체 통과(run_e2e.sh exit 0, 14 시나리오)**: play/workflow /evidence에 resolve-only 관찰(spotRid/actorId 쿼리 → IZLinkSpotHandleResolver/IZLinkActorDirectory 단건 resolve) 추가, C1·C2·C3·C5 시나리오를 그 표면으로 재정렬, B4 join의 room row 발행 창 폴링 — **ObservabilityOps 완전 그린(5번째 config)**. 이후 S8-12A 사전 정리로 진단용 `ZLINK_DEBUG_PUMP` env-gate 계측 전량 제거(15파일 −165줄, UnitTests 649/655·TA/ST core-blocked 지점 불변 확인), ToActorMessaging(TA-B3 recovery = core-blocked ①). **feature-map 실측 정렬**: SpotService는 최신 default-batch·SM-F6·SM-G2 통과분만 구현으로 반영하고 SM-G1은 core 대기, RuntimeMonitoring은 A1·A2·A3·B1·B2·C1만 구현이며 A4·A5·D1은 core 대기, ResilienceLifecycle·StoreFailure·SpotActorTransfer·ToActorMessaging은 최신 전체 실행의 최초 core 중단 지점 뒤 행을 재검 대기로 기록했다. doc-contract verifier clean, 공통 E2E fixture regression의 미구현 잔여는 SM-C6·SM-G1 두 건이다. UnitTests 기준선 649/655(6=doc-regression) 유지 |
| S8-11 | 리뷰 종료 뒤 source/package contract 검증 | 두 `DOTNET REVIEW CLEAN` 뒤 `scripts/verify_packaged_contract.sh`, NuGet consumer와 native payload 일치 | 미착수 | - |
| S8-12 | 성능과 resource 회귀 | 별도 성능 개선 작업의 입력으로 분리 | 후속 분리 | 현재 S8 gate를 위해 성능 측정을 실행하지 않음 |
| S8-12A | `.NET` guide 갱신 | 구현·sample·E2E 일반 검증이 통과한 뒤 guide에 정식 공개 계약의 사용법을 반영 | 미착수 | - |
| S8-13 | Codex agent `.NET` 리뷰 | I1·I2·I3 각각의 finding·evidence·축별 판정 보고 | 미착수 | - |
| S8-14 | Claude Sonnet `.NET` 리뷰 | 같은 scope에서 I1·I2·I3를 독립 판정 | 미착수 | - |
| S8-15 | finding 수정·일반 검증·전체 재리뷰 반복 | finding을 일괄 수정하고 일반 build·전체 테스트 통과 뒤 두 리뷰어가 `.NET` 전체와 세 축을 재검토해 둘 다 `DOTNET REVIEW CLEAN` | 미착수 | - |
| S8-16 | process-local MeshName uniqueness 검증 | 중복 AddRouteMesh 실패와 multi-mesh 독립 동작 통과 | 완료 | process-local MeshName uniqueness: 중복 AddRouteMesh AddUnique 실패 + per-node stream-dispatch keying(multi-mesh 독립) |
| S8-17 | 확정 `.NET` internals 갱신 | 두 `DOTNET REVIEW CLEAN`과 S8-11 종료 검증 뒤 최종 pump·scheduler·location·timer 구조를 internals에 반영 | 미착수 | - |
| S8-18 | `.NET` internals 확정 검사 | S8-17 문서와 최종 source·구조 test·diagram·link의 차이 0개. 문서 검사만으로 구현 전체 재리뷰를 열지 않음 | 미착수 | - |

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
| S9-C05 | contract·E2E 일반 검증 | CTest와 `e2e/run_e2e_all.sh` 통과 | 미착수 | - |
| S9-C06 | C++ guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 미착수 | - |

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
| S9-J06 | contract·E2E 일반 검증 | Gradle, `framework/languages/java/e2e/run_e2e_all.sh`와 `framework/languages/java/e2e-kotlin/run_e2e_all.sh` 모두 통과 | 미착수 | - |
| S9-J07 | Java/Kotlin guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 미착수 | - |

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
| S9-N05 | contract·E2E 일반 검증 | build·test와 `e2e/run_e2e_all.sh` 통과 | 미착수 | - |
| S9-N06 | Node.js guide 갱신 | 구현·sample·E2E 일반 검증 통과 뒤 정식 공개 계약의 사용법을 guide에 반영 | 미착수 | - |

S9 완료 gate:

- [ ] 세 lane이 자기 file scope만 수정했다.
- [ ] 각 lane의 일반 build·전체 test, sample, E2E와 stale no-hit가 통과한다.
- [ ] 세 lane이 S8의 구현 결과나 clean 판정을 계약 근거 또는 선행 조건으로 사용하지 않았다.
- [ ] 공통 spec 변경이 필요해진 경우 구현을 멈추고 S2·S3 계약 review를 다시 연다.

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
| S10-NV | Node.js | `verify_packaged_contract`, clean npm consumer와 package metadata 검사 통과 | 미착수 | - |
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

## 15. S11 — 전체 최종 검토, Core stable·bindings 외부 배포와 종료

### 15.1 최종 리뷰 대상 준비

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-01 | 모든 lane 병합과 clean checkout 확인 | 의도하지 않은 file과 미병합 변경 0개 | 미착수 | - |
| S11-00A | Core stable revision 동결 | 최종 source가 마지막 S6 RC와 같은 commit이고 S7~S10 동안 Core source·ABI 변경이 없음 | 미착수 | - |

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
| S11-02 | 정식 spec과 public API 전수 대조 | Core, bindings와 framework 언어별 차이 0개 | 미착수 | - |
| S11-03 | 제거 항목 repository no-hit | 허용된 v10 plan·review record 외 stale 이름 0개 | 미착수 | - |
| S11-04 | Core 전체 종료 검증 | build, test, ASAN·UBSAN·TSAN과 package consumer 통과 | 미착수 | - |
| S11-05 | bindings local package smoke 재실행 | S7에서 검증한 모든 local package E2E 통과 | 미착수 | - |
| S11-06 | `.NET` 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-07 | C++ 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-08 | Java/Kotlin 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-09 | Node.js 전체 종료 검증 | contract, package, sample와 E2E 통과 | 미착수 | - |
| S11-10 | classic fanout과 generic STREAM 회귀 | Actor binding 확장점을 제외한 비변경 socket 기능의 public 동작과 baseline 유지 | 미착수 | - |
| S11-11 | docs, link와 sample API 검증 | 깨진 link, stale 예제와 내부 구현 노출 0개 | 미착수 | - |
| S11-12 | version과 artifact 대조 | Core·bindings 10.0.0과 framework pin 일치 | 미착수 | - |

### 15.4 최종 리뷰와 종료 검증 뒤 외부 배포

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S11-00B | Core stable tag와 외부 배포 | `FINAL REVIEW CLEAN` candidate와 같은 commit에 `core/v10.0.0` tag를 붙이고 native build와 Conan release workflow 성공 | 미착수 | - |
| S11-00C | Core stable artifact 검증 | GitHub Release, checksums, headers, SONAME, symbols와 remote `zlink/10.0.0` consumer 통과 | 미착수 | - |
| S11-00D | stable Core 기반 bindings 재검증 | stable asset으로 native payload를 다시 동기화하고 S7 package·공통 E2E smoke 결과가 RC와 일치 | 미착수 | - |
| S11-01A | bindings 외부 배포 revision 동결 | S7 clean source, Core stable release SHA와 package checksum이 manifest와 일치 | 미착수 | - |
| S11-01B | 언어별 10.0.0 tag 배포 | cpp, dotnet, java, node, python, go, rust workflow 정상 종료 | 미착수 | - |
| S11-01C | 실제 배포 채널 확인 | NuGet, Maven, npm, PyPI, crates.io, Go tag/module과 C++ GitHub Release 존재 | 미착수 | - |
| S11-01D | 배포 package E2E smoke | 각 채널의 새 package를 빈 workspace에 설치해 공통 smoke 통과 | 미착수 | - |
| S11-01E | framework 배포 package 재검증 | local package pin을 같은 10.0.0 배포 package로 바꾸어 package·sample·E2E 통과 | 미착수 | - |
| S11-17 | post-push origin 재검증 | origin commit, tag, workflow와 artifact가 local 증거와 일치 | 미착수 | - |
| S11-18 | 완료 보고서 작성 | 모든 stage 증거, 남은 issue 0과 최종 SHA 기록 | 미착수 | - |

S11 완료 gate:

- [ ] final Core tag가 마지막 RC와 같은 source commit을 가리키고 GitHub Release와 Conan remote package가 검증되었다.
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
- [ ] 적용되는 종료 기준의 open finding, skipped required test와 검증되지 않은 배포 artifact가
  0개다. 4회차부터 남은 low는 후속 정리 목록에 기록되어 있다.

## 16. 차단, 재개와 이전 stage 재개방 규칙

- 정식 spec을 바꿔야 하는 구현 finding은 현재 stage에서 임시 처리하지 않고 S1 또는 S2를 다시 연다.
- 공통 계약을 바꾸면 S3 문서 리뷰를 다시 통과한 뒤 downstream stage를 재검증한다.
- S6 RC 뒤 Core ABI나 동작을 바꾸면 stable tag를 만들지 않고 새 commit으로 S5, 새 `rc.N+1`로 S6,
  S7과 모든 framework stage를 다시 통과한다.
- bindings 공개 계약이나 native payload를 바꾸면 S7 review와 local package smoke를 다시 통과한다.
- S8 또는 S9의 어느 언어 구현에서든 공통 계약 gap이 발견되면 S2·S3을 다시 열고, 직접 영향을 받는
  lane의 완료 판정을 취소한다.
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
| 최종 구현 R2 review | Claude Sonnet session과 결과 기록 |
| Core workflow run | - |
| Core RC prerelease run·checksum | - |
| Conan workflow run | - |
| bindings workflow runs | - |
| 전체 E2E 결과 | - |
| 전체 sample 결과 | - |
| benchmark report | 별도 성능 개선 작업에서 기록 |
| stale no-hit 결과 | - |
| open finding | 1~3회차는 전체 severity `0`, 4회차부터는 blocker·high·medium `0`이어야 종료 |
