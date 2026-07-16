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

이번 변경은 Core와 bindings 10.0.0 계약을 한 번에 적용한다. 폐기 대상 alias, deprecated wrapper와
두 runtime mode를 함께 유지하지 않는다. 각 stage는 앞 stage의 gate를 통과한 뒤 시작한다.

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
| S4-01~S4-07 MeshNode lifecycle·peer·selection | [Core MeshNode](../../../../core/doc/spec/core/service/mesh-node.ko.md), [Core Dispatch](../../../../core/doc/spec/core/service/dispatch.ko.md) |
| S4-08~S4-14 message·claim·Logical Multicast·Spot | [Core message](../../../../core/doc/spec/core/message.ko.md), [Core Dispatch](../../../../core/doc/spec/core/service/dispatch.ko.md), [Core Spot](../../../../core/doc/spec/core/service/spot.ko.md) |
| S4-15~S4-15A Actor·STREAM transfer | [Core Actor](../../../../core/doc/spec/core/service/actor.ko.md), [Core STREAM session](../../../../core/doc/spec/core/service/stream-session.ko.md) |
| S4-16~S4-22A 제거·polling·관측·오류 | [Core public contract governance](../../../../core/doc/spec/core/00-public-contract-governance.ko.md), [polling](../../../../core/doc/spec/core/polling.ko.md), [monitoring](../../../../core/doc/spec/core/monitoring.ko.md), [errno](../../../../core/doc/spec/core/errno-map.ko.md), [raw socket](../../../../core/doc/spec/core/socket/README.ko.md) |
| S4-22B~S6 release와 ABI | [Core errors·version](../../../../core/doc/spec/core/errors.ko.md), [Core public contract governance](../../../../core/doc/spec/core/00-public-contract-governance.ko.md) |
| S7 bindings | 위 Core 정식 계약 전체와 [Core service 목차](../../../../core/doc/spec/core/service/README.ko.md) |
| S8 `.NET` framework | [Framework 공통 계약](../../framework/spec/README.ko.md), [server 계약](../../framework/spec/server/21-mesh-node.ko.md), [.NET exact interface](../../framework/spec/server/languages/dotnet/README.ko.md) |
| S8 location·transfer | [Location Runtime](../../framework/spec/server/40-location-runtime.ko.md), [Redis extension](../../framework/spec/server/41-location-store-redis.ko.md), [.NET Location Store](../../framework/spec/server/languages/dotnet/06-location-store.ko.md) |
| S8 monitoring·drain | [Runtime monitoring](../../framework/spec/server/50-runtime-monitoring.ko.md), [message flow](../../framework/spec/server/52-message-flow-tracing.ko.md), [graceful drain](../../framework/spec/server/54-graceful-drain-handoff.ko.md), [.NET RouteMesh runtime](../../framework/spec/server/languages/dotnet/05-route-mesh.ko.md) |
| S9 C++ | 공통 framework 계약과 `framework/doc/framework/spec/server/languages/cpp/`에서 구현 전에 리뷰한 exact interface |
| S9 Java/Kotlin | 공통 framework 계약과 `framework/doc/framework/spec/server/languages/java/`, `languages/kotlin/`에서 구현 전에 리뷰한 exact interface |
| S9 Node.js | 공통 framework 계약과 `framework/doc/framework/spec/server/languages/node/`에서 구현 전에 리뷰한 exact interface |

S9의 언어별 exact interface는 해당 lane의 첫 red gate다. 현재 파일을 그대로 구현 기준으로 간주하지
않고, 공통 10.0.0 계약을 언어 관용 표현으로 먼저 정식 문서에 고정하고 독립 리뷰한 뒤 source를 바꾼다.
Plan 고유 항목인 삭제 no-hit, test 명령, package provenance와 review 증거는 이 진행표가 계속 소유한다.

## 1. 고정 실행 순서

| Stage | 작업 | 병렬 실행 | 완료 판정 |
|---|---|---:|---|
| **S0** | Core 정식 spec 적용 범위와 결정 검증 | 아니요 | 구현 전에 정할 항목이 0개이고 정식 owner 문서와 gap 범위가 고정됨 |
| **S1** | Core 10.0.0 정식 spec 작성 | 아니요 | Core 목표 계약 전체가 reviewed 정식 spec에 고정됨 |
| **S2** | framework 정식 spec 변경과 E2E·sample 영향 검토 | 아니요 | 공통·언어별 계약과 영향 목록이 고정됨 |
| **S3** | 문서 독립 리뷰와 수정 반복 | 리뷰 2개만 병렬 | 두 리뷰어 모두 `DOC REVIEW CLEAN` |
| **S4** | Core 구현·제거 정리와 정식 spec 일치 | 아니요 | 기능·삭제·회귀·성능, header-spec 일치와 구현 후 internals gate 통과 |
| **S5** | Core 독립 리뷰, 수정과 POSD·DDD 리팩터링 반복 | 리뷰 2개만 병렬 | 두 리뷰어 모두 `CORE REVIEW CLEAN` |
| **S6** | Core 10.0.0 release-candidate GitHub Actions build와 pre-release 배포 | workflow 병렬 허용 | RC native artifact와 local Conan 검증 완료. stable tag·remote publish 없음 |
| **S7** | bindings 적용, 독립 리뷰와 local package E2E smoke | 언어별 제한적 병렬 | 모든 bindings local package 검증 완료 |
| **S8** | `.NET framework`, sample과 E2E 적용 및 리뷰 | 리뷰 2개만 병렬 | 두 리뷰어 모두 `DOTNET REVIEW CLEAN` |
| **S9** | C++, Java/Kotlin, Node.js framework 적용 | 세 lane 병렬 | 세 lane 구현과 검증 완료 |
| **S10** | 세 언어 lane별 독립 리뷰와 수정 반복 | 세 lane 병렬 | lane마다 두 리뷰어 모두 clean |
| **S11** | Core stable·bindings 외부 배포, 전체 최종 검토와 종료 | 배포 후 리뷰 2개만 병렬 | stable package smoke와 두 리뷰어 `FINAL REVIEW CLEAN` |

S3, S5, S7, S8, S10과 S11의 review gate를 생략하거나 다음 stage에서 대신 처리하지 않는다.

## 2. 독립 리뷰 운영 규칙

### 2.1 리뷰어

| ID | 리뷰어 | 역할 |
|---|---|---|
| **R1** | Codex agent | 저장소의 실제 spec, source, test, package와 실행 증거를 독립 검토 |
| **R2** | Claude Fable 모델 | 같은 고정 revision과 동일한 review manifest를 독립 검토 |

R1과 R2는 서로의 finding을 보기 전에 첫 검토를 완료한다. 두 결과가 나온 뒤 coordinator가 중복을
합치고 하나의 finding ledger를 만든다. 한 리뷰어의 clean 판정으로 다른 리뷰어의 검토를 대신하지
않는다.

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

각 iteration은 `manifest.ko.md`, `codex-review.ko.md`, `claude-fable-review.ko.md`,
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
5. 새 revision을 고정하고 R1과 R2가 전체 scope를 다시 검토한다.
6. 두 리뷰어가 해당 stage의 exact clean 문구를 남길 때까지 1~5를 반복한다.

한 리뷰어가 실행되지 않았거나 결과가 중단되면 review gate는 `차단`이다. 시간 부족, finding 개수
감소 또는 test 통과만으로 clean 판정을 추정하지 않는다.

## 3. 전체 진행 현황

| Stage | 상태 | 현재 iteration | open finding | 완료 증거 |
|---|---|---:|---:|---|
| S0 정식 spec 범위·계약 확정 | 완료 | 0 | 0 | `s0-scope-baseline.ko.md`, `log/templates/manifest.ko.md` |
| S1 Core 정식 spec | 수정 중 | 1 | 6 | Codex Core 재리뷰의 blocker 2·high 4 수정 중; 수정 뒤 동일 전체 범위 재리뷰 |
| S2 framework spec | 진행 중 | 0 | 0 | 공통·server·.NET exact spec, S2 contract·E2E·sample inventory |
| S3 문서 review loop | 미착수 | 0 | 0 | - |
| S4 Core 구현·정식 spec 일치 | 미착수 | 0 | 0 | - |
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
| S0-19 | S/S application metadata 확정 | Node·Channel·Spot direct canonical frame, 1024-byte 상한, snapshot, malformed ingress, forwarding과 일반 reply metadata 미지원 계약 고정 | 완료 | `framework-route-mesh-messaging-consolidation.ko.md` D-23, `mesh-node-framework-dispatch-design.ko.md` FD-27 |
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
| S1-03 | MeshNode 정식 spec 한국어·영문 작성 | lifecycle, identity, membership, messaging, multicast와 status 포함 | 완료 | `core/doc/spec/core/service/mesh-node.ko.md`, `.md` |
| S1-04 | Spot 정식 spec 한국어·영문 작성 | Spot lifecycle, direct send/request metadata, local subscription, timer, dispatch와 queue ownership 포함 | 완료 | `core/doc/spec/core/service/spot.ko.md`, `.md` |
| S1-05 | Actor·STREAM 경계 정식 spec 작성 | ActorRef, lifecycle과 이동은 service/actor가, session barrier는 service/stream-session이 소유하고 raw socket에는 범용 계약만 남김 | 완료 | `core/doc/spec/core/service/actor.ko.md`, `.md`, `stream-session.ko.md`, `.md` |
| S1-06 | Core governance, index와 cross-link 작성 | spec-first, 10.0.0 적용 범위와 한영 index가 모든 정식 owner 문서를 연결 | 완료 | `core/doc/spec/core/00-public-contract-governance.*`, `service/README.*`, Core spec index |
| S1-07 | exact `zlink_mesh_node_*` signature 고정 | 생성·peer·node/channel send/request·one-shot reply, ready/claim/batch와 target-channel publisher API 확정 | 완료 | `service/mesh-node.*`, `dispatch.*`; formal reverse inventory FUNC 84 |
| S1-08 | type, enum과 status ABI 고정 | 구조체 크기, version field, 숫자 값과 field ownership을 exact ABI로 명시 | 완료 | service 정식 C block과 `service/README.*` versioned 구조체 분류; reverse inventory TYPE 30·ENUM_TYPE 16·ENUMERATOR 101·FIELD 202 |
| S1-09 | 삭제 C API 목록 고정 | 제거 API·enumerator·macro마다 대체 또는 제거 판정 명시 | 완료 | `s1-core-public-api-inventory.ko.md` 제거·대체 표와 exact replacement target 54개 |

### 5.2 동작 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-10 | MeshName·ChannelName·RID 불변 조건 작성 | cardinality, 복수 mesh, immutable ChannelName set과 변경 금지 시점이 명확함 | 완료 | `service/mesh-node.*` identity·membership·lifecycle 절 |
| S1-11 | peer admission과 readiness 계약 작성 | generation, duplicate RID, security profile과 drain 제외 규칙 명시 | 완료 | `service/mesh-node.*` peer admission·ready 절 |
| S1-12 | node·channel·multicast 선택 의미 작성 | target snapshot, round-robin과 no-member 결과를 operation 계약 안에 정의 | 완료 | `service/mesh-node.*` messaging·Logical Multicast 절 |
| S1-13 | ready·claim·batch 계약 작성 | wakeup-only callback, application/infrastructure claim, metadata, ownership과 close 규칙 확정 | 완료 | `core/doc/spec/core/service/dispatch.ko.md`, `.md` |
| S1-14 | Spot Logical Multicast 계약 작성 | target channel, channel-scoped subscription, local shared reference와 no-relay 명시 | 완료 | `service/mesh-node.*`, `service/spot.*` |
| S1-15 | `NODROP`과 backpressure 계약 작성 | local·remote admission, 기본값 1, timeout, DONTWAIT와 drop 명시 | 완료 | `service/mesh-node.*` Logical Multicast·option 절 |
| S1-16 | message ownership과 ordering 계약 작성 | multipart 원자성, reference count와 ordering 범위 확정 | 완료 | `core/message.*`, `service/dispatch.*`, `service/mesh-node.*` |
| S1-17 | actor·Spot·STREAM session 경계 작성 | owner MeshNode와 mailbox·claim 책임이 중복되지 않고 Actor가 Spot callback을 경유하지 않음 | 완료 | `service/spot.*`, `actor.*`, `stream-session.*` |
| S1-18 | classic PUB/SUB 비변경 계약 확인 | 독립 PUB/SUB socket API의 의미가 축소되지 않음 | 완료 | `socket/pub.*`, `sub.*`, `xpub.*`, `xsub.*`, `service/README.*` |
| S1-18A | request completion·reply 계약 | requester operation ID, responder one-shot reply token, generation·shutdown 오류, owner completion batch와 in-turn await 명시 | 완료 | `core/doc/spec/core/service/dispatch.ko.md` §5·§6, `dispatch.md` §5·§6 |
| S1-18B | service wire multipart 계약 | versioned routing envelope, optional metadata frame, borrowed submit input, retained Core reference와 payload part 경계 명시 | 완료 | `service/mesh-node.*`, `dispatch.*`, `core/message.*` |
| S1-18C | receive batch 재사용 계약 | empty-before-receive, non-empty `EBUSY`, reset·BUFFER_TOO_SMALL·retain 결과별 view/reference 수명 명시 | 완료 | `core/doc/spec/core/service/dispatch.ko.md` §3·§4, `dispatch.md` §3·§4 |

### 5.3 연관 Core 정식 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S1-19 | router 정식 spec 갱신 | generic ROUTER 계약과 service 경계를 분리 | 완료 | `core/socket/router.*`, `socket/README.*` |
| S1-20 | polling 정식 spec 갱신 | ready index, callback·receive poller 배타성, `POLLOUT`과 infrastructure 진행 보장 명시 | 완료 | `core/polling.*` |
| S1-21 | monitoring 정식 spec 갱신 | MeshNode source, peer, multicast, backpressure와 drop event 계약 명시 | 완료 | `core/monitoring.*`, `events.*` |
| S1-22 | errno·errors 정식 spec 갱신 | 신규 함수의 모든 result와 errno mapping 포함 | 완료 | `core/errno-map.*`, `errors.*` |
| S1-23 | option과 handle 지원 정식 표 | generic option이 MeshNode·Spot·publisher에서 가지는 의미 확정 | 완료 | `service/README.*`, `mesh-node.*`, `spot.*` |
| S1-24 | 10.0.0 version 계약 작성 | 공개 version macro, `zlink_version()`과 SOVERSION 10이 한영 정식 spec에 일치 | 완료 | `core/doc/spec/core/errors.ko.md`, `.md` §7 |
| S1-25 | 정식 spec 한국어·영문 parity 및 link 검증 | signature, result, ownership과 local link 차이 0개 | 완료 | `C BLOCK PARITY CLEAN 30`; local link·fence 검증과 `git diff --check` 통과 |
| S1-26 | unresolved marker 검사 | TBD, 미결정, 나중에 확정 표현 scoped no-hit | 완료 | Core formal scope scoped no-hit |
| S1-27 | Core 구현 일치 임시 추적 문서 작성 | 정식 spec과 checkout 차이를 항목별 기록하며 formal 문서가 참조하지 않음 | 완료 | `s1-core-implementation-tracking.ko.md` CI-01~CI-15, formal plan-reference no-hit |

S1 완료 gate:

- [x] 구현자가 plan 문서를 추측하지 않고 Core 정식 spec만으로 C API를 구현할 수 있는 owner와 exact ABI가 있다.
- [x] 모든 삭제 API와 새 API가 exact signature 및 result 계약을 가진다.
- [ ] Codex agent와 Claude Fable의 동일 frozen Core scope 독립 리뷰가 모두 `DOC REVIEW CLEAN`이다.
- [x] 현재 구현 차이는 `s1-core-implementation-tracking.ko.md`에 기록되고 정식 spec은 이를 참조하지 않는다.

## 6. S2 — framework 정식 spec과 영향 범위 변경

### 6.1 공통과 server 계약

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-01 | public contract governance에 10.0.0 전환 절차 반영 | spec-first, gap 기록과 구현 확인 순서 명시 | 미착수 | - |
| S2-02 | overview 갱신 | RouteMesh, MeshNode, Logical Multicast와 classic fanout 경계 설명 | 미착수 | - |
| S2-03 | interaction model 갱신 | node, channel, Spot, Actor, fanout과 STREAM 의미 분리 | 미착수 | - |
| S2-04 | framework API 갱신 | topology registration 제거와 MeshNode-owned 기능 계약 반영 | 미착수 | - |
| S2-05 | channel topology 갱신 | MeshName, immutable ChannelName set, RID와 full mesh membership 계약 반영 | 미착수 | - |
| S2-06 | channel messaging 갱신 | select-one, direct RID, timeout, cancellation과 reply 계약 반영 | 미착수 | - |
| S2-07 | MeshNode owner 계약 작성 | `21-mesh-node.ko.md`의 파일명·제목·link와 책임 범위가 10.0.0 개념과 일치 | 완료 | `framework/doc/framework/spec/server/21-mesh-node.ko.md` |
| S2-08 | Spot messaging 갱신 | Logical Multicast와 channel-scoped local subscription, explicit publish target interface 반영 | 미착수 | - |
| S2-09 | Actor·Spot actor·address messaging 갱신 | bridge 제거, owner MeshNode와 위치 투명성 반영 | 미착수 | - |
| S2-10 | session actor dispatch 갱신 | STREAM 경계와 MeshNode 선택이 명확함 | 미착수 | - |
| S2-11 | location runtime과 Redis store 갱신 | MeshNode descriptor와 Spot·Actor location row를 분리하고 Redis를 production 기본 구현으로 지정. 명시적 등록, location store 미등록 시 startup failure, manual admission handshake와 test-only in-memory 경계를 반영 | 미착수 | - |
| S2-11A | Actor transfer authority store 계약 | participant-set CAS, transfer token, lease, prepared/commit/abort 복구, Redis 구현과 startup capability validation 명시 | 미착수 | - |
| S2-12 | monitoring과 graceful drain 갱신 | RouteMesh별 readiness, drain, multicast와 rollback 단위 반영 | 미착수 | - |
| S2-12A | runtime metrics와 message-flow tracing 갱신 | route, multicast, fanout metric·flow 종류와 bounded label 계약 반영 | 미착수 | - |
| S2-12B | flow correlation 갱신 | direct channel multicast와 reply completion correlation 경계 반영 | 미착수 | - |
| S2-12C | S/S application metadata 계약 | Node·Channel·Spot direct canonical codec, last-write-wins builder와 hostile ingress failure, immutable context, relay 전이표, reply 비자동복사·일반 reply metadata 미지원 명시 | 미착수 | - |
| S2-12D | Spot timer backend 계약 | .NET·Java·Node platform timer와 C/C++ C API timer가 같은 keyed scheduling·cancel 의미를 제공 | 미착수 | - |

### 6.2 언어별 공개 interface

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-13 | `.NET` exact interface 작성 | `UseInMemoryLocationStores()`를 production 표면에서 제거하고 나머지 root option, AddRouteMesh, RID pin을 지원하는 `IZLinkMeshPeerConnections`, 두 handler family, Spot·Actor 멤버, client metadata·handler snapshot, NoDrop과 runtime-options signature·startup-only 오류 확정 | 미착수 | - |
| S2-14 | C++ interface 후속 owner 고정 | S9의 C++ 구현 전에 exact interface를 먼저 작성할 정식 spec 경로와 검증 기준을 기록 | 미착수 | - |
| S2-15 | Java·Kotlin interface 후속 owner 고정 | S9의 Java·Kotlin 구현 전에 exact interface를 먼저 작성할 정식 spec 경로와 검증 기준을 기록 | 미착수 | - |
| S2-16 | Node.js interface 후속 owner 고정 | S9의 Node.js 구현 전에 exact interface를 먼저 작성할 정식 spec 경로와 검증 기준을 기록 | 미착수 | - |
| S2-17 | `.NET` 제거 interface 표 작성 | root·builder·endpoint overload와 SpotNode 이름의 `.NET` 공개 멤버를 전수 대응 | 미착수 | - |
| S2-18 | 구현 차이 추적 경계 고정 | 정식 spec은 현재 구현 상태를 기록하지 않고 임시 계획 문서만 Core·bindings·framework 구현 차이를 소유 | 미착수 | - |
| S2-18A | `.NET` 현재 checkout builder 전수 mapping | root option과 ClientServer·RouteMesh·Spot builder의 모든 멤버가 유지·이동·제거에 정확히 한 번 대응 | 미착수 | - |

### 6.3 E2E와 sample 영향 검토

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S2-19 | `framework/doc/framework/common/e2e/` inventory 생성 | 모든 config와 scenario의 영향·비영향·신규 검증 분류 | 미착수 | - |
| S2-20 | LocationMessaging 영향 검토 | select-one, round-robin, scale, direct RID scenario 대응 | 미착수 | - |
| S2-21 | SpotService 영향 검토 | multicast, local topic match, reconnect와 channel call 대응 | 미착수 | - |
| S2-22 | PubSub 영향 검토 | classic fanout 비변경 회귀 scenario 대응 | 미착수 | - |
| S2-23 | lifecycle·transfer·monitoring 영향 검토 | drain, owner 이동, endpoint remap과 관측 scenario 대응 | 미착수 | - |
| S2-23A | ToActorMessaging·StoreFailureRecovery 영향 검토 | actor owner route와 descriptor/location row 장애 복구 대응 | 미착수 | - |
| S2-24 | `framework/doc/framework/common/sample/` inventory 생성 | 변경, 삭제, 신규, 비영향 sample을 언어별로 분류 | 미착수 | - |
| S2-25 | sample public API 예제 검토 | 제거 API, endpoint 배선과 raw helper 사용처가 모두 기록됨 | 미착수 | - |
| S2-26 | package consumer와 runner 영향 검토 | local package, clean consumer와 E2E runner 변경점 기록 | 미착수 | - |
| S2-27 | guide·internals 변경 지도 작성 | 구현 뒤 바꿀 모든 사용자·내부 문서 경로와 각 문서의 독자·질문·원본 식별. S2에서는 대상과 검증 기준만 정하고 internals 본문은 바꾸지 않음 | 미착수 | - |
| S2-27A | 문서 성격과 current-state 경계 검증 | spec·guide·internals에는 10.0.0 현재 계약만, 계획은 blueprint와 실행 추적만 기록 | 미착수 | - |
| S2-28 | link·anchor·render 검증 | S2 정식 spec 범위의 깨진 link·중복 anchor 0개이고 실제 render를 확인한 증거가 있음 | 미착수 | - |
| S2-28A | 예제 API 강제 검사 설계 | 공개 계약 예제 compile/smoke, 필수 구성 누락과 원본·번역 동기 검사를 구현 stage에서 실행할 파일·명령·실패 조건으로 고정 | 미착수 | - |
| S2-29 | 공통 E2E 문서 적용 사항 검토 | Config 1~11마다 10.0.0 topology, 입력, 관찰 결과와 failure scenario의 변경·비변경·신규 검증을 파일 단위로 분류 | 미착수 | - |
| S2-30 | 공통 sample 문서 적용 사항 검토 | sample마다 target API 예제, Redis 등록, manual topology와 runner 변경·비변경을 파일 단위로 분류 | 미착수 | - |
| S2-31 | runner template 변경 계획 고정 | topology setup, package 입력과 result marker 변경점을 파일별 기록 | 미착수 | - |

S2 완료 gate:

- [ ] reviewed Core 10.0.0 정식 spec과 framework 목표 계약 사이에 기능 또는 error 의미 차이가 없다.
- [ ] E2E, sample, package consumer와 runner 영향이 파일 단위로 식별되어 있고 실제 변경·실행은 해당 framework 구현 stage의 gate로 연결되어 있다.
- [ ] 언어 중립 의미 계약과 `.NET` exact public interface가 정식 spec에 있고, 나머지 언어는 각 구현 stage에서 spec-first로 exact interface를 확정할 owner와 gate가 식별되어 있다.

## 7. S3 — 문서 독립 리뷰 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S3-01 | S1·S2 문서 revision 동결 | review manifest에 commit과 diff 범위 기록 | 미착수 | - |
| S3-02 | Codex agent 문서 리뷰 | 누락, 모순, 구현 불가능 계약과 stale API finding 보고 | 미착수 | - |
| S3-03 | Claude Fable 문서 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 미착수 | - |
| S3-04 | finding 병합과 중복 제거 | 모든 finding에 owner, severity와 red gate 지정 | 미착수 | - |
| S3-05 | Core 정식 spec finding 수정 | 관련 한국어·영문·signature·result table과 임시 구현 차이 추적을 함께 수정 | 미착수 | - |
| S3-06 | framework spec finding 수정 | 공통·server·.NET exact interface와 E2E·sample 영향 inventory를 함께 수정 | 미착수 | - |
| S3-07 | 문서 자동 검증 재실행 | link, signature, stale name, duplicate와 formatting 검사 통과 | 미착수 | - |
| S3-08 | 전체 scope 재리뷰 | 이전 diff만이 아니라 S1·S2 전체를 두 리뷰어가 다시 검토 | 미착수 | - |
| S3-09 | 문서별 2축 review 기록 | 각 문서를 원칙 준수와 1차 소스 부합으로 나누고 finding마다 축·severity·file:line·근거·제안 기록 | 미착수 | - |
| S3-10 | 문서별 검증 증거 분리 | finding을 1차 소스로 확인한 뒤 문서별 수정 diff와 SHA-256을 독립 증거로 기록. 사용자가 별도로 요청하지 않으면 commit은 만들지 않음 | 미착수 | - |

문서 리뷰 필수 축:

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
- [ ] Claude Fable 결과 마지막 줄이 `DOC REVIEW CLEAN`이다.
- [ ] S3 완료 전 Core production source를 수정하지 않았다.

## 8. S4 — Core 구현·제거 코드 정리와 정식 spec 일치

### 8.1 red gate와 공개 API

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-01 | contract red test 작성 | 새 API 부재와 제거 API 존재를 test가 먼저 실패로 증명 | 미착수 | - |
| S4-02 | public header를 10.0.0 spec에 맞춤 | 함수·type·enum과 result signature 일치 | 미착수 | - |
| S4-03 | export와 ABI 목록 갱신 | 새 symbol 존재, 제거 symbol 부재 | 미착수 | - |
| S4-04 | MeshNode lifecycle과 handle kind 구현 | 생성, bind, start, drain, destroy 계약 통과 | 미착수 | - |
| S4-05 | peer descriptor와 admission 구현 | manual·discovery endpoint가 같은 handshake를 사용하고 MeshName, identity, generation, duplicate, security, ready와 drain 계약 통과 | 미착수 | - |
| S4-05A | manual peer lifecycle 구현 | endpoint 및 예상 RID pin, connect·disconnect, discovery와 중복 source 병합, 누락 peer 상태를 관측하고 운영자가 모든 peer 연결을 설정해야 하는 계약의 test 통과 | 미착수 | - |

### 8.2 메시징과 runtime

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-06 | RID pipe와 channel index 구현 | 같은 MeshName의 ready RID만 선택 | 미착수 | - |
| S4-07 | node·channel 선택과 submit 구현 | direct와 round-robin이 한 send/request 호출 안에서 원자적으로 처리되고 RID-only 공개 select API가 없음을 contract test로 검증 | 미착수 | - |
| S4-08 | Node·Channel·Spot direct send/request와 service envelope 구현 | application metadata codec, timeout, operation ID와 borrowed/retained multipart ownership test 통과 | 미착수 | - |
| S4-08A | responder reply 구현 | opaque token one-shot, generation·shutdown 오류, source route 비노출과 S/S reply metadata 미지원 test 통과 | 미착수 | - |
| S4-09 | mailbox·ready·claim·batch 구현 | Node·Spot·Actor 격리, infrastructure 우선 drain과 lost wakeup 0건 | 미착수 | - |
| S4-10 | Logical Multicast multi-target submit 구현 | target channel 직접 선택, 조건부 local dispatch와 remote node당 1회 submit | 미착수 | - |
| S4-11 | shared message reference count 구현 | local Spot queue와 remote pipe 수명·실패 정리 검증 | 미착수 | - |
| S4-12 | NODROP와 backpressure 구현 | local·remote admission/commit 직렬화, 기본 1, 부분 전달 금지, timeout과 drop test 통과 | 미착수 | - |
| S4-13 | no-relay와 duplicate guard 구현 | multicast loop와 중복 전달 0건 | 미착수 | - |
| S4-14 | Spot local subscription 분리 | channel-scoped 등록·해제·수신 API와 remote subscription 없는 exact/prefix match 동작을 구현하고 public inventory query를 만들지 않음 | 미착수 | - |
| S4-15 | Actor와 STREAM session owner 전환 | direct Actor mailbox, transfer fence, ActorRef와 bound session 회귀 통과 | 미착수 | - |
| S4-15A | Actor transfer fence·token protocol 구현 | Core prepare가 64-byte sealed token을 발급하고 commit이 이 token, transfer ID, Actor generation과 정확히 다음 membership epoch를 검증한 뒤 mailbox/session fence를 수행한다. deterministic fake location authority로 prepare·commit·activate·abort·stale token contract test 통과 | 미착수 | - |

### 8.3 삭제와 관측

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-16 | SpotNode mode와 PUB/SUB plane 제거 | mesh_pub, mesh_xsub와 mode branch source no-hit | 미착수 | - |
| S4-17 | route bridge와 raw helper 제거 | header, source, build, test와 symbol no-hit | 미착수 | - |
| S4-17A | service receive·part API 제거 | channel·Spot send/request/reply·publish, Spot·Actor recv, Actor–STREAM `*_part`와 Actor join/lifecycle 전용 receive·reply symbol을 complete multipart API·Spot control batch로 대체하고 no-hit | 미착수 | - |
| S4-18 | remote subscription protocol 제거 | registry, reconnect, control frame과 status no-hit | 미착수 | - |
| S4-19 | 폐기 alias와 forwarding wrapper 제거 | 폐기 이름, Core dispatch worker option과 remote subject query를 전달하는 production code no-hit | 미착수 | - |
| S4-20 | polling, status와 monitoring 구현 | reviewed S1 정식 spec의 source kind, event와 query test 통과 | 미착수 | - |
| S4-21 | errno와 result mapping 구현 | 모든 신규 API가 정해진 result를 반환 | 미착수 | - |
| S4-22 | 제거 file과 CMake entry 정리 | include되지 않는 source와 orphan target 0개 | 미착수 | - |
| S4-22A | owner completion infrastructure 통합 | channel dealer·service per-request callback·Spot reply drain 제거, raw DEALER/ROUTER `zlink_reply_handler_fn` 유지와 in-turn await 통과 | 미착수 | - |
| S4-22B | Core version·ABI metadata 갱신 | VERSION, public headers와 CMake project version은 10.0.0, SOVERSION은 10이며 Conan source에는 선택한 `10.0.0-rc.N` URL과 아직 게시하지 않은 stable `10.0.0` URL이 있음 | 미착수 | - |
| S4-22C | 10.0.0 release note 작성 | 공개 기능, 지원 환경, package와 검증 결과 명시 | 미착수 | - |
| S4-22D | Core RC/stable workflow 분기 구현 | `build.yml`은 `-rc.N` tag를 prerelease로, stable tag를 release로 게시한다. Conan workflow는 tag에서 RC/stable package version을 구분하고 RC remote upload를 금지하며 stable secret 부재 시 실패 | 미착수 | - |
| S4-22E | Core implementation gap 닫기 | 구현된 header·test를 S1 정식 spec의 MeshNode, Spot, Actor, router, polling, monitoring과 errno 계약에 대조하고 차이를 모두 해소 | 미착수 | - |
| S4-22F | Core 정식 spec parity와 index 검증 | 한국어·영문, service index, public header, errno와 ownership 차이 0개 | 미착수 | - |
### 8.4 Core 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S4-23 | unit와 contract test | 전체 통과, skip 증가 없음 | 미착수 | - |
| S4-24 | integration과 topology test | direct, channel, multicast, reconnect와 drain 통과 | 미착수 | - |
| S4-25 | callback·claim·ownership stress | close, rearm, claim leak/revoke, multipart와 reference count 오류 0건 | 미착수 | - |
| S4-26 | sanitizer와 race 검증 | ASAN/UBSAN/TSAN 적용 범위에서 신규 오류 0건 | 미착수 | - |
| S4-27 | 1천·1만 peer benchmark | connection, lookup, multicast, reconnect 결과 기록 | 미착수 | - |
| S4-28 | mixed traffic 성능 검증 | request p99와 resource가 S0 threshold 통과 | 미착수 | - |
| S4-29 | install과 package consumer | 설치 header와 shared library로 clean consumer 통과 | 미착수 | - |
| S4-30 | 삭제 범위 최종 no-hit | v10 plan·review record의 삭제 추적만 제외하고 source, 현재 계약·guide·internals, test, build와 package에서 제거 symbol·enumerator·macro·metadata 부재 | 미착수 | - |

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

## 9. S5 — Core 독립 리뷰와 POSD·DDD 리팩터링 반복

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S5-01 | Core revision과 검증 결과 동결 | manifest에 source·test·package·benchmark 범위 기록 | 미착수 | - |
| S5-02 | Codex agent Core 리뷰 | 아래 모든 review 축의 finding 보고 | 미착수 | - |
| S5-03 | Claude Fable Core 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 미착수 | - |
| S5-04 | 정확성·누락 finding 수정 | spec과 다른 동작, 빠진 test·상태·오류를 보완 | 미착수 | - |
| S5-05 | POSD 위험 신호 목록 작성 | 각 항목의 위반 원칙과 두 설계안 기록 | 미착수 | - |
| S5-06 | 의미 있는 리팩터링 수행 | 선택 이유와 호출자 복잡도 감소 근거 기록 | 미착수 | - |
| S5-07 | DDD event와 경계 재검토 | lifecycle, membership, dispatch, ownership과 observation 책임 정리 | 미착수 | - |
| S5-08 | dead code와 file 제거 | 도달 불가능 branch, 미사용 type·helper·target·file no-hit | 미착수 | - |
| S5-09 | 전체 Core 검증 재실행 | S4-23~S4-30 결과가 리팩터링 뒤에도 통과 | 미착수 | - |
| S5-10 | 두 리뷰어 전체 재리뷰 | 직전 finding뿐 아니라 Core 전체 scope 재검토 | 미착수 | - |

Core 리뷰 필수 축:

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
- [ ] POSD 위험 신호와 DDD 경계 검토 결과가 기록되어 있다.
- [ ] 불필요하거나 도달 불가능한 code와 file이 남아 있지 않다.
- [ ] Codex agent 결과 마지막 줄이 `CORE REVIEW CLEAN`이다.
- [ ] Claude Fable 결과 마지막 줄이 `CORE REVIEW CLEAN`이다.

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
| S7-04 | MeshNode API 구현 | lifecycle, peer, node/channel call·one-shot reply, optional application metadata frame, ready/claim/batch와 publisher 공개 | 미착수 | - |
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
| S7-SMOKE | 공통 smoke matrix | node/channel/Spot direct send/request metadata snapshot·malformed·1024 경계·relay·reply 비자동복사, multicast, NODROP, batch reset/retain과 shutdown 통과 | 미착수 | - |

### 11.3 bindings 독립 리뷰와 외부 배포 전 검증

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S7-15 | Codex agent bindings 리뷰 | parity, ownership, dead wrapper와 package finding 보고 | 미착수 | - |
| S7-16 | Claude Fable bindings 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 미착수 | - |
| S7-17 | finding 수정과 전체 재검증 | 모든 affected language와 package smoke 재실행 | 미착수 | - |
| S7-18 | 두 리뷰어 전체 재리뷰 반복 | 둘 다 `BINDINGS REVIEW CLEAN` | 미착수 | - |
| S7-19 | local package 묶음 검증 | publish-all-wsl 및 별도 언어 package 검증 통과 | 미착수 | - |
| S7-20 | `all` target 배포 없는 workflow 검증 | create_release=false, publish_registry=false로 전체 job 성공 | 미착수 | - |
| S7-21 | 언어별 외부 배포 manifest 준비 | tag, 실제 배포 채널, credential, 설치·smoke 명령과 rollback 기록 | 미착수 | - |
| S7-22 | framework pin 입력 기록 | 검증된 local package version, checksum과 경로 확보 | 미착수 | - |

bindings 리뷰에는 reflection/private symbol, raw frame, fallback symbol lookup, 폐기된 native payload,
도달 불가능 wrapper, 중복 DTO와 언어별 임시 public API 검사를 반드시 포함한다. POSD 관점에서는
패스스루 wrapper, 반복되는 ownership·error mapping과 얕은 native adapter를 검토한다. DDD 관점에서는
MeshNode, Spot, Actor와 session 개념이 언어별 transport 세부와 섞이지 않았는지 확인한다. 사용하지
않는 generated code, native declaration, mock, test fixture와 package file도 삭제 대상으로 검토한다.

S7 완료 gate:

- [ ] 모든 bindings local package E2E smoke가 통과한다.
- [ ] open bindings finding이 0개다.
- [ ] Codex agent와 Claude Fable 결과가 모두 `BINDINGS REVIEW CLEAN`이다.
- [ ] 외부 immutable 10.0.0 package는 아직 공개하지 않았다.

## 12. S8 — `.NET framework`, sample과 E2E 적용 및 리뷰

| ID | 작업 | 완료 조건 | 상태 | 증거 |
|---|---|---|---|---|
| S8-01 | 검증된 bindings local package pin을 10.0.0으로 갱신 | 중앙 version과 lock·restore 결과 일치 | 미착수 | - |
| S8-02 | AddRouteMesh·ChannelName 구현 | 복수 logical membership과 정식 `.NET` interface가 source snapshot과 일치 | 미착수 | - |
| S8-02A | RouteMesh runtime-options DI 구현 | 기존 `IZLinkChannelRuntimeOptions` 제거, `IZLinkRouteMeshRuntimeOptions` singleton 등록, MeshNode socket setter의 startup-only 오류와 runtime channel Weight 반영 통과 | 미착수 | - |
| S8-03 | MeshNode-owned handler·Spot·Actor 등록 구현 | channel·route handler context와 모든 Spot·Actor builder 멤버 보존 | 미착수 | - |
| S8-04 | location descriptor와 connection planner 구현 | Redis 자동 discovery와 manual `IZLinkMeshPeerConnections`가 같은 admission을 사용하고 MeshName 범위, expected RID pin, generation, source 병합과 ready index 검증 | 미착수 | - |
| S8-04A | Redis Actor transfer authority 구현 | participant-set CAS, transfer token, lease, prepared/commit/abort crash recovery, unsupported store startup failure와 distributed transfer E2E 통과 | 미착수 | - |
| S8-04B | Redis production 기본 정책 구현 | Redis extension 명시 등록, 자동 discovery·분산 Spot/Actor 주소 조회를 사용하면서 location store를 등록하지 않은 구성의 startup failure, 사용자 store capability와 test-only in-memory 경계 검증 | 미착수 | - |
| S8-05 | channel/direct/Spot/Actor 전송 연결 | bindings MeshNode public API만 사용 | 미착수 | - |
| S8-06 | ready/claim pump 구현 | infrastructure 우선 drain, Node·Spot·Actor keyed scheduling과 claim leak 0건 | 미착수 | - |
| S8-06A | S/S metadata 연결 | Node·Channel·Spot direct send/request의 mutation snapshot, immutable handler view, malformed ingress, 1024 경계, relay allowlist, reply 비자동복사와 일반 reply metadata 미지원 통과 | 미착수 | - |
| S8-06B | Spot timer 연결 | `Task.Delay` 기반 tick이 lifecycle generation과 cancel 규칙을 거쳐 keyed scheduler에 제출되고 Core timer FFI를 사용하지 않음 | 미착수 | - |
| S8-07 | Logical Multicast와 NoDrop 연결 | 기본 true와 명시적 false 회귀 통과 | 미착수 | - |
| S8-08 | 기존 topology API와 runtime 제거 | v10 plan·review record만 제외하고 builder, registration, production `UseInMemoryLocationStores()`, bridge, Spot·Actor–STREAM service part와 Actor join/lifecycle 전용 wrapper, test와 현재 docs no-hit | 미착수 | - |
| S8-09 | `.NET` sample 전환 | 분산 sample은 공식 Redis extension을 등록하고 지정된 manual sample은 `IZLinkMeshPeerConnections`를 사용하며 S2 inventory 반영 후 `samples/run_samples.sh` 전체 통과 | 미착수 | - |
| S8-10 | `.NET` framework E2E 전환 | `e2e/run_e2e_all.sh`의 전체 config 통과 | 미착수 | - |
| S8-11 | source/package contract 검증 | `scripts/verify_packaged_contract.sh`, NuGet consumer와 native payload 일치 | 미착수 | - |
| S8-12 | 성능과 resource 회귀 | mixed traffic p99와 fanout baseline gate 통과 | 미착수 | - |
| S8-12A | `.NET` guide와 internals 갱신 | 구현·sample·E2E·package·성능 검증이 모두 통과한 뒤 guide에는 사용 계약만, internals에는 검증된 실제 pump·scheduler·location·timer 구조만 반영하고 source·구조 test·정식 spec과 대조 | 미착수 | - |
| S8-13 | Codex agent `.NET` 리뷰 | 정확성, 누락, POSD·DDD와 dead code finding 보고 | 미착수 | - |
| S8-14 | Claude Fable `.NET` 리뷰 | 같은 scope를 독립 검토하고 finding 보고 | 미착수 | - |
| S8-15 | finding 수정·전체 검증·재리뷰 반복 | 둘 다 `DOTNET REVIEW CLEAN` | 미착수 | - |
| S8-16 | process-local MeshName uniqueness 검증 | 중복 AddRouteMesh 실패와 multi-mesh 독립 동작 통과 | 미착수 | - |

S8 review는 S5와 같은 축을 사용하고 framework 책임에 맞게 다음을 추가한다.

- Core selection과 peer index를 framework에서 다시 구현하지 않았는지 확인
- typed JSON codec 책임을 handler나 sample에 전달하지 않았는지 확인
- location, lifecycle, callback과 transport 지식이 여러 runtime에 중복되지 않았는지 확인
- sample이 endpoint, peer RID, raw frame과 내부 type을 직접 다루지 않는지 확인
- 제거된 public API, fake, fixture, source comment와 package snapshot이 남지 않았는지 확인

## 13. S9 — C++, Java/Kotlin과 Node.js 병렬 적용

### 13.1 병렬 작업 격리

| 원칙 | 적용 방법 |
|---|---|
| file ownership | C++, JVM, Node lane은 자기 언어 source·test·sample·언어별 문서만 수정 |
| 공통 문서 | common spec, main plan과 이 진행표는 coordinator만 수정 |
| 진행 기록 | lane은 자기 log에 증거를 기록하고 coordinator가 master 표에 병합 |
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

세 언어 lane의 리뷰는 서로 병렬 실행할 수 있다. 각 lane 안에서는 Codex agent와 Claude Fable 리뷰를
같은 revision으로 병렬 실행한다. reviewer는 다른 lane을 수정하지 않는다.

| ID | Lane | Codex 결과 | Claude Fable 결과 | open finding | 상태 | 증거 |
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

Java/Kotlin lane은 매 review iteration에서 Java
`framework/languages/java/e2e/run_e2e_all.sh`와 Kotlin
`framework/languages/java/e2e-kotlin/run_e2e_all.sh`를 각각 실행하고 결과를 분리해 기록한다.

언어별 종료 문구:

- C++: `CPP REVIEW CLEAN`
- Java/Kotlin: `JVM REVIEW CLEAN`
- Node.js: `NODE REVIEW CLEAN`

S10 완료 gate:

- [ ] 세 lane 모두 open finding이 0개다.
- [ ] 세 lane 모두 두 리뷰어의 해당 clean 문구가 있다.
- [ ] finding 수정 뒤 각 lane의 전체 package·sample·E2E를 다시 실행했다.

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
| S11-13 | Codex agent 전체 리뷰 | spec부터 package·sample·E2E·release까지 검토 | 미착수 | - |
| S11-14 | Claude Fable 전체 리뷰 | 같은 전체 scope를 독립 검토 | 미착수 | - |
| S11-15 | final finding 수정과 영향 검증 | finding 관련 stage와 downstream 검증 재실행 | 미착수 | - |
| S11-16 | 전체 scope 재리뷰 반복 | 두 리뷰어가 모두 `FINAL REVIEW CLEAN` | 미착수 | - |
| S11-17 | post-push origin 재검증 | origin commit, tag, workflow와 artifact가 local 증거와 일치 | 미착수 | - |
| S11-18 | 완료 보고서 작성 | 모든 stage 증거, 남은 issue 0과 최종 SHA 기록 | 미착수 | - |

S11 완료 gate:

- [ ] final Core tag가 마지막 RC와 같은 source commit을 가리키고 GitHub Release와 Conan remote package가 검증되었다.
- [ ] Core spec부터 모든 framework 언어, sample, E2E, package와 release artifact가 하나의 10.0.0
  계약을 따른다.
- [ ] 제거 대상 code, file, API, test와 문서가 허용 위치 밖에 남아 있지 않다.
- [ ] POSD·DDD 재검토에서 의미 있는 리팩터링 finding이 남아 있지 않다.
- [ ] Codex agent 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
- [ ] Claude Fable 결과 마지막 줄이 `FINAL REVIEW CLEAN`이다.
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
| 최종 Claude Fable review | - |
| Core workflow run | - |
| Core RC prerelease run·checksum | - |
| Conan workflow run | - |
| bindings workflow runs | - |
| 전체 E2E 결과 | - |
| 전체 sample 결과 | - |
| benchmark report | - |
| stale no-hit 결과 | - |
| open finding | `0`이 되어야 종료 |
