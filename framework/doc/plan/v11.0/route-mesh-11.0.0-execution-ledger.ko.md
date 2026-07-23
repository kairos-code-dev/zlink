# RouteMesh 11.0.0 통합 execution ledger

## 0. 문서 역할

이 문서는 RouteMesh 11.0.0 작업의 **단일 실행 상태 기준**이다. Core service 계약 이관, C++·.NET·JVM·
Node.js Framework runtime, stateful maintenance, socket liveness 정책, Core·bindings 제거, package와 smoke를
하나의 선행 조건 구조로 관리한다.

작업 상태, 담당 lane, 기준 revision, 이후 변경 목록, artifact hash, test와 review 증거는 이 ledger의 담당
행에만 기록한다. 다른 문서에 진행표나 완료 증거를 만들지 않는다. 정식 spec, source, test와 package 자체는
각 소유 디렉터리에 두되 이 ledger에서 정확한 revision과 결과 위치를 참조한다.

외부 registry에는 배포하지 않는다. Package gate는 local/internal package 생성, clean consumer 설치와
artifact provenance만 검증한다.

## 1. 목표 구조와 범위

Core 11.0은 context, message, raw socket, transport, poller와 generic monitor만 제공한다. MeshNode, Channel,
dispatch, Spot, Actor, Instance Spot, bound STREAM session과 maintenance는 Core에서 제거한다.

C++, .NET, JVM과 Node.js는 각 언어가 service runtime 구현을 소유한다. 공통 Framework native runtime과
네 runtime이 호출하는 private C SPI는 만들지 않는다. 각 runtime은 해당 언어 binding의 설치된 public raw
socket API만 사용한다.

언어 사이에 공유하는 구현 입력은 다음으로 제한한다.

- Framework 공통 공개 계약
- protocol schema와 생성 상수
- golden frame, normalized trace와 오류 fixture
- 공통 E2E scenario와 결과 schema

Java와 Kotlin은 Java binding과 JVM service runtime 하나를 공유한다. 공개 계약은 Java와 Kotlin을 각각
검증하지만 구현 worker는 JVM lane 하나만 둔다. 별도 Kotlin runtime은 만들지 않는다.

C, Python, Go와 Rust bindings는 Core 10.x의 마지막 지원 조합에 격리한다. Core 11.0 build·package·CI에
포함하거나 11.0 호환을 표기하지 않으며 후속 11.x 전환 계획을 만들지 않는다.

## 2. 상태와 기록 규칙

| 상태 | 의미 |
|---|---|
| `대기` | 선행 gate 또는 담당 배정을 기다린다. |
| `진행 중` | 기준 revision과 소유 파일을 기록하고 작업 중이다. |
| `검토 준비 완료` | 구현과 자체 검증은 끝났으나 독립 review가 남아 있다. |
| `리뷰 중` | 독립 reviewer가 기준 revision과 누적 변경을 검토 중이다. |
| `완료` | 완료 gate, review와 이후 변경 영향 확인이 모두 통과했다. |
| `차단` | 미확정 계약이나 외부 환경 때문에 안전하게 진행할 수 없다. |
| `폐기` | 11.0 책임 경계와 충돌하여 실행하지 않는다. 기존 증거는 보존한다. |

작업자는 다음 규칙을 지킨다.

1. 담당 ID, 기준 revision, 이후 변경 목록, 소유 파일과 실행할 test를 증거 칸에 기록한 뒤 시작한다.
2. 정식 spec이나 exact interface가 바뀌면 변경 파일, 바뀐 의미와 직접 의존 ID를 먼저 기록한다.
3. 공통 spec, protocol schema·fixture와 shared manifest는 coordinator만 수정한다.
4. 각 language lane은 자기 언어 source·test·sample과 package 설정만 수정한다.
5. 같은 언어 runtime의 lifecycle과 data path를 서로 다른 worker가 동시에 수정하지 않는다.
6. timeout 증가, polling, raw-frame adapter, reflection과 private Core·binding 접근으로 실패를 우회하지 않는다.
7. skipped required test, 미검증 package, 중단된 review와 일부 언어 결과를 완료 증거로 인정하지 않는다.
8. Core, binding 또는 Framework runtime package가 바뀌면 그 artifact를 사용하는 test·E2E·package 증거를
   다시 만든다.
9. 문서나 aggregate hash 변화 자체는 작업을 차단하지 않는다. 의미가 바뀐 spec, protocol, public API와 주요
   불변 조건의 영향 범위만 다시 검증한다.
10. SPEC 선행 gate가 완료되지 않아도 담당 범위의 문서 작성과 자체 검증은 병렬로 진행할 수 있다.
    다만 모든 선행 ID가 `완료`가 되기 전에는 해당 행을 `검토 준비 완료`, `리뷰 중`, `완료`로
    전환하거나 독립 review를 시작하지 않는다.
11. 기존 Framework public interface, sample과 E2E source는 migration 불변 입력이다. Contract amendment가
    승인되기 전에는 Core service runtime 이관을 이유로 기존 source, scenario ID나 실행 registration을
    수정·삭제하지 않는다. 승인한 public contract 변경으로 영향을 받는 항목은 amendment impact manifest에
    기존 hash, disposition, 새 acceptance intent와 대체 coverage를 먼저 기록한 뒤에만 변경한다. 영향받지 않은
    source와 registration은 Git diff 0을 유지한다.
12. Core service ABI를 대체하는 public·private binding service API를 만들지 않는다. Framework 내부에
    private binding-facing port를 두고 해당 언어 binding의 설치된 public raw API만 호출한다.
13. Framework service runtime이 완성되기 전에는 sample·E2E를 실행해 완료를 판정하지 않는다. Contract
    amendment impact manifest는 각 항목을 `retain`, `amend`, `replace`, `add`, `remove`로 분류하고 실행 상태를
    기록한다. 실행 source와 registration은 `pending-disabled-by-contract-amendment`로 고정한다. Review할
    E2E·sample contract 문서는 `active-contract-spec`으로 구분해 runtime과 병렬로 고칠 수 있지만 실행 증거로
    사용하지 않는다. Review를 이미 통과한 source 변경을 보존해야 하면 exact hash와 함께
    `pending-disabled-reviewed-source`로 기록한다. 세 상태 모두 skip이나 성공 결과가 아니며 source와
    registration을 삭제하거나 주석 처리하지 않는다.
14. Runtime 구현 build에서는 sample·E2E project와 task만 실행 graph에서 분리한다. Sample·E2E source가 새
    interface 때문에 compile되지 않아도 runtime 통과를 위해 source를 임시 수정하거나 compatibility helper를
    추가하지 않는다. 이 구간은 Core raw regression, binding raw contract, protocol fixture, public declaration
    snapshot과 Framework internal unit·contract·resource test를 실행한다.
15. 전체 runtime과 production placeholder 제거가 끝난 뒤 E2E·sample spec을 승인한 contract snapshot에 맞춰
    최종 확정한다. E2E는 topology, stateful object, maintenance, race·cross-language 순서로 작은 묶음씩
    활성화하며 각 묶음의 runtime gap을 닫은 뒤 다음 묶음을 시작한다. Sample은 전체 E2E가 통과한 뒤
    활성화하고 public API만 사용해 compile·run한다.

### 2.1 Runtime-first 실행 순서

RouteMesh 11.0 migration은 다음 순서를 바꾸지 않는다.

1. Core 11 raw runtime과 regression·sanitizer 결과를 고정한다.
2. Core 11 local package와 Framework runtime이 필요로 하는 최소 raw binding package를 만든다.
3. M5 뒤 public contract amendment의 identity, remote create, placement, failure, maintenance 의미와 다섯 언어
   exact interface를 확정한다. 변경될 E2E scenario, sample과 유지할 regression test를 impact manifest에
   나열하고 baseline source·registration hash를 봉인한다.
4. E2E·sample 실행을 source 변경 없이 격리한 뒤 각 Framework 언어의 private binding-facing port에 새 contract의
   실제 동작을 연결한다. Protocol codec, transport·session, liveness, routing·registry, Spot·Actor lifecycle과
   maintenance를 순서대로 구현하되, 기존 Framework source의 구조 변경은 동작 복구에 필요한 최소 범위로
   제한한다.
5. 각 vertical slice가 internal unit·contract·protocol regression을 통과하면 다음 기능으로 진행한다. 새
   public behavior는 E2E 대신 deterministic internal contract test로 먼저 검증한다. 네 언어 runtime과
   production placeholder 제거가 모두 끝날 때까지 E2E·sample source를 구현 편의에 맞춰 바꾸지 않는다.
6. Runtime 전체 합류 뒤 amendment impact manifest를 기준으로 E2E spec과 sample spec을 최종 확정한다.
   영향받은 source만 승인한 disposition에 따라 바꾸고 영향받지 않은 source·registration은 diff 0을 유지한다.
7. E2E를 topology, stateful object, maintenance, race·cross-language 순서로 재활성화해 runtime gap을 닫는다.
8. 전체 E2E 통과 뒤 sample을 재활성화하여 public API 사용, startup, operation과 cleanup을 검증한다.

M4까지는 sample·E2E source와 registration의 Git diff 0만 확인하고 실행하지 않는다. M5와 contract amendment,
M6 runtime 구현은 internal runtime contract만 완료 증거로 사용한다. Contract amendment가 승인한 변경은
impact manifest가 old→new source·registration hash와 coverage를 소유한다. 최초 runtime candidate의 단계별 E2E
활성화와 전체 matrix는 M6 후반과 M7이 소유하고, sample 실행은 전체 E2E 뒤 M7이 소유한다. Final package
조합의 재검증 결과는 M9가 소유한다.

이 순서에서 public interface는 승인된 contract snapshot을 따른다. 제거 대상인 Core service ABI나 binding
service projection을 compatibility layer로 되살리지 않는다. Contract amendment에서 replace·remove한 Framework
surface도 구현 편의를 위한 adapter로 유지하지 않는다. Sample과 E2E 호출부 변경은 runtime 구현 중이 아니라
spec 최종화 뒤 impact manifest가 승인한 항목에만 적용한다.

## 3. Codex profile과 병렬 lane

각 lane은 아래 중앙 profile 하나를 지정한다. Profile은 작업 성격에 맞는 기본 model과 reasoning
effort를 중앙에서 관리한다. 각 stage 행은 model 문자열을 반복하지 않고 profile만 지정한다.
Lane을 시작할 때 실제 사용한 model, reasoning effort와 profile을 증거 칸에 기록한다. 기본
model을 사용할 수 없으면 대체 model과 이유를 기록하며 조용히 effort를 낮추지 않는다.
기본 model과 effort 선택은 [Codex model·reasoning 공식 안내](https://learn.chatgpt.com/docs/models)를
참고한다. 이 외부 링크의 접근 가능 여부나 문서 hash는 실행 gate와 candidate 판정에 사용하지 않는다.

| Profile | 기본 model·effort | 적용 범위 |
|---|---|---|
| `P-DEEP` | `gpt-5.6 high` | 공개 계약·POSD, 동시성·ownership·recovery, 독립 review |
| `P-XHIGH` | `gpt-5.6-sol xhigh` | Final E2E·sample spec처럼 public contract와 다섯 언어 coverage를 함께 판단하는 교차 영역 독립 review |
| `P-DELIVERY` | `gpt-5.6 medium` | 일반 기능 구현, E2E, package, smoke와 consumer 검증 |
| `P-SCAN` | `gpt-5.6-terra low` 또는 `gpt-5.6-terra medium` | manifest·링크·inventory, 범위 분류와 read-heavy scan |

`P-SCAN`은 단순 수집·no-hit 검사에 `low`를 사용하고 분류 판단이 필요하면 `medium`을 사용한다.
`xhigh`는 재현되지 않은 race, protocol·ABI 판단 또는 최종 통합 감사를 high effort로 종료할 수 없을 때
사용한다. `P-XHIGH`는 `V11-R4A`, `V11-R4B`와 `V11-R5D`에서 의무적으로 사용한다.
그 밖의 row에서 `xhigh`를 임의로 사용하는 경우 상향 이유와 조정 범위는 해당 행의 증거 칸에 기록한다.

병렬 실행의 단일 소유자와 합류 gate는 다음과 같다.

| 항목 | 단일 소유자 | 병렬 lane 입력 | 합류 gate |
|---|---|---|---|
| Framework 공통 spec | contract coordinator | 승인된 spec snapshot | 다섯 exact interface와 contract test |
| Core raw spec | Core contract lead | 승인된 raw contract | Core header·export·raw test |
| Protocol | protocol coordinator | schema·생성 상수·fixture revision | 네 codec과 cross-language E2E |
| C++ runtime | C++ lead | 공통 계약과 C++ public binding | C++ contract·race·package gate |
| .NET runtime | .NET lead | 공통 계약과 .NET public binding | .NET contract·race·package gate |
| JVM runtime | JVM lead | Java·Kotlin 계약과 Java public binding | Java·Kotlin ABI·coroutine·package gate |
| Node.js runtime | Node.js lead | 공통 계약과 Node public binding | Node event-loop·export·package gate |
| Cross-language E2E | E2E coordinator | scenario와 fixture revision | 방향이 있는 `4 x 4` 결과 |

### 3.1 Ledger 경로와 ID만 사용하는 작업 지시

작업 프롬프트는 이 ledger의 repository 상대 경로와 담당 ID 목록만 전달한다. 작업자는 다른 v11 plan을 찾아서
checklist를 보완하지 않는다. 각 ID를 시작할 때 다음 순서로 이 문서 안에서 실행 정보를 확인한다.

1. 담당 row의 선행 ID가 모두 `완료`인지 확인한다.
2. 아래 authoritative source 표에서 계약을 읽는 목적을 확인하고 해당 정식 spec·common internals·exact
   interface를 먼저 읽는다. Framework runtime 구현 row는 migration machine inventory가 연결한 보존된 Core
   service 구현과 test snapshot도 함께 읽는다. 새 runtime을 처음부터 별도로 설계하지 않고 기존 구현의 component
   분리, state machine, algorithm, ordering·ownership과 failure 처리를 언어별 runtime에 맞게 옮긴다. 정식
   문서가 목표 계약의 정본이며 보존된 Core service 구현은 구현의 기준 자료다.
3. §3.4의 같은 ID execution card에서 수정 가능한 path, 필수 명령, 산출물과 증거 위치를 확인한다.
4. 명시한 path 밖의 변경이 필요하면 작업을 확장하지 않고 담당 row 증거 칸에 사유와 영향 ID를 기록한다.
5. 필수 명령의 전체 command line, exit code, candidate manifest와 결과 path를 담당 row의 증거 칸에 기록한다.

각 작업의 상세 증거는 repository에 commit하지 않는
`.artifacts/v11/evidence/<ID>/result.json`에 저장한다. 담당 row의 증거 칸에는 기준 revision, 변경 파일,
authoritative input revision, candidate manifest, 명령과 exit code, 결과 JSON의 절대 경로·SHA-256, finding과
재검증 결과를 요약한다. 진행 상태의 단일 기준은 이 ledger row이며 별도 진행 문서를 만들지 않는다.

여기서 기준 revision은 Git base revision과 review 시점의 candidate manifest를 함께 뜻한다. Candidate가 아직
commit되지 않았거나 working tree에 다른 작업이 함께 있다는 이유만으로 review를 막지 않는다. Reviewer는
manifest에 포함된 파일과 당시 content digest를 기준으로 결과를 남긴다. Review 뒤 파일이 바뀌면 바뀐 의미와
직접 의존 범위를 다시 확인하고 manifest를 갱신하며, 이전 hash와 다르다는 사실만으로 finding을 만들거나
전체 review를 처음부터 반복하지 않는다.

### 3.2 Authoritative source와 확인 목적

| 계약 입력 | 이 입력에서 확인할 내용 |
|---|---|
| [Core 11 raw 공개 경계](../../../../core/doc/spec/core/09-runtime-boundary.ko.md) | Core에 남길 raw C ABI, 제거할 service·ZMP heartbeat 공개 표면과 ownership |
| [Core 11 raw 내부 경계](../../../../core/doc/internals/runtime-boundary.ko.md) | 제거할 engine·timer·state와 남길 generic transport·timer·monitor 구조 |
| [Framework 공통·server 정식 spec](../../framework/spec/README.ko.md) | Application이 관찰하는 topology, messaging, object, maintenance, liveness와 오류 계약 |
| [Framework 공통 service internals](../../framework/common/internals/README.ko.md) | 네 언어 runtime이 공유할 protocol, mailbox, stateful object, maintenance, session, liveness와 resource 목표 구조. M6 완료 전 target model 표기는 ledger 구현 gap과 함께 해석함 |
| [다섯 언어 exact interface](../../framework/spec/server/languages/README.ko.md) | C++·.NET·Java·Kotlin·Node.js의 정확한 public signature와 package owner |
| 보존된 Core service 구현·test snapshot | 기존 component 경계, type 관계, state machine, algorithm, queue·lock 순서, 오류·종료 처리. 새 runtime은 이를 언어별 구조와 raw binding 경계에 맞게 이관하며 목표 의미가 spec·internals와 다를 때만 해당 차이를 적용하지 않음 |
| [Service wire schema](../../../../framework/runtime/protocol/service-wire-v1.schema.json) | Command·field·bound·encoding과 생성 상수의 단일 wire 정본 |
| [공통 E2E 계약](../../framework/common/e2e/README.ko.md)과 이 ledger §14 | 공통 scenario ID, 방향성 조합, race·crash·functional 완료 조건 |
| [공통 sample spec](../../framework/common/sample/README.ko.md) | 다섯 언어 sample이 보여 줄 public 사용 흐름, 역할, message와 acceptance marker |
| `route-mesh-11.0.0-contract-amendment-impact.json` | Contract amendment가 영향을 주는 public member, E2E scenario, sample, registration과 regression test의 baseline hash, disposition, owner와 활성화 단계 |
| [Local package 규칙](../../../../scripts/local-package/README.ko.md) | version 고정 지점, local/internal output, clean consumer와 provenance |
| [Migration machine inventory](../../contract-inventory/route-mesh-v11-core-service-migration-inventory.json) | 제거·보존·대체할 symbol, source, test, package 입력과 담당 gate |

### 3.3 필수 명령과 신규 runner CLI 계약

다음 명령은 repository root에서 실행한다. 모든 row는 `DOC`와 자신의 owned path만 검사하는
`DIFF-OWNED`를 실행한다. `INV`는 shared inventory owner·join·review·final row에서만 실행하고,
`WIRE`는 protocol 영향 card에서 실행한다. 최종 `V11-R7`만 revision으로 고정한 전체 candidate snapshot에
`DIFF-SNAPSHOT`을 사용한다.

```bash
# DOC
scripts/verify-framework-doc-contracts.sh

# INV
scripts/verify-v11-core-service-migration-inventory.sh

# WIRE
node framework/runtime/protocol/validate-service-wire-schema.mjs \
  --self-test framework/runtime/protocol/service-wire-v1.schema.json

# DIFF-OWNED
git diff --check -- <§3.4 card의 owned path 목록>

# DIFF-SNAPSHOT (V11-R7 only)
git diff --check <reviewed-base-revision>..<candidate-revision> -- .

# CORE
./core/build.sh
core/tests/run_test_lanes.sh \
  --build-dir core/build --include-e2e --include-regression

# CORE-ASAN
test ! -e .artifacts/v11/build/core-asan
cmake -S core -B .artifacts/v11/build/core-asan \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTS=ON -DENABLE_ASAN=ON
cmake --build .artifacts/v11/build/core-asan
ctest --test-dir .artifacts/v11/build/core-asan --output-on-failure

# BIND-TEST
./bindings/cpp/tests/run_tests.sh
./bindings/dotnet/tests/run_tests.sh
./bindings/java/tests/run_tests.sh
npm --prefix bindings/node test

# BIND-PKG
scripts/local-package/native/sync-local-core-libs.sh cpp dotnet java node
scripts/local-package/build-wsl.sh cpp
scripts/local-package/build-wsl.sh dotnet
scripts/local-package/build-wsl.sh java
scripts/local-package/build-wsl.sh node

# FW-CPP
cmake -S framework/languages/cpp \
  -B .artifacts/v11/build/framework-cpp \
  -DZLINK_FRAMEWORK_CPP_BUILD_TESTS=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_SAMPLES=ON \
  -DZLINK_FRAMEWORK_CPP_BUILD_E2E=ON
cmake --build .artifacts/v11/build/framework-cpp
ctest --test-dir .artifacts/v11/build/framework-cpp --output-on-failure

# FW-DN
dotnet test framework/languages/dotnet/Zlink.Framework.sln

# FW-JVM
framework/languages/java/gradlew \
  -p framework/languages/java --no-daemon clean check

# FW-NODE
npm --prefix framework/languages/node ci
npm --prefix framework/languages/node test

# SAMPLES
framework/languages/cpp/samples/run_samples.sh
framework/languages/dotnet/samples/run_samples.sh
framework/languages/java/samples/run_samples.sh
framework/languages/node/samples/run_samples.sh

# RAW-PERF-SMOKE
cmake --build core/build
bindings/c/perf/run_benchmarks_multi.sh --pattern ROUTER_ROUTER_REQREP

# E2E-CURRENT
framework/languages/cpp/e2e/run_e2e_all.sh
framework/languages/dotnet/e2e/run_e2e_all.sh
framework/languages/java/e2e/run_e2e_all.sh
framework/languages/java/e2e-kotlin/run_e2e_all.sh
framework/languages/node/e2e/run_e2e_all.sh

# PACKAGE-CONTRACT
framework/languages/cpp/scripts/verify_packaged_contract.sh \
  .artifacts/v11/build/framework-cpp
framework/languages/dotnet/scripts/verify_packaged_contract.sh
framework/languages/java/scripts/verify_packaged_contract.sh java
framework/languages/java/scripts/verify_packaged_contract.sh kotlin
framework/languages/node/scripts/verify_packaged_contract.sh
```

아래 runner는 현재 없는 v11 전용 강제 장치다. 지정한 owner ID가 정확한 path와 CLI로 새로 만들고 unit
self-test를 추가한다. 이후 card가 이 명령을 요구하면 runner가 없는 상태, 다른 CLI 또는 schema가 없는 결과를
완료로 인정하지 않는다.

| 명령 ID | 신규 path와 CLI 계약 | 최초 owner |
|---|---|---|
| `ORACLE` | `scripts/v11/run-oracle.sh --manifest framework/testdata/v11/oracle/oracle-manifest-v1.json --scenario <exact-scenario-id> --output <absolute-trace.json>`; oracle는 child process로만 실행하고 normalized trace를 출력 | `V11-M2-ORACLE` |
| `ROW-GATE` | `scripts/v11/run-ledger-gate.sh --id <exact-ledger-id> --candidate-manifest <absolute-candidate.json> --owned-path-manifest <absolute-owned-paths.json> --evidence <absolute-result.json>`; candidate와 owned-path schema로 provenance·freshness·exit, 변경 path가 §3.4 소유 범위 안인지와 `git diff --check -- <owned paths>` 결과를 검증 | `V11-M2-READY` |
| `REMOVE` | `scripts/v11/verify-removal.sh --scope <core|binding:cpp|binding:dotnet|binding:jvm|binding:node|framework:cpp|framework:dotnet|framework:jvm|framework:node|common> --inventory framework/doc/contract-inventory/route-mesh-v11-core-service-migration-inventory.json --evidence <absolute-result.json>`; export·include·generated·build·package와 runtime load를 검사 | `V11-M2-READY` |
| `CORE-PKG` | `scripts/local-package/core/build-wsl.sh --build-dir core/build --candidate-manifest <absolute-V11-M3-CORE-VERIFY-candidate.json> --review-evidence <absolute-V11-R2-result.json> --output-root <absolute-local-root> --evidence <absolute-result.json>`; R2가 exact candidate SHA를 승인한 install tree·provenance를 만들고 `scripts/v11/verify-core-package-consumer.sh --prefix <absolute-core-install-prefix> --candidate-manifest <absolute-V11-M3-CORE-VERIFY-candidate.json> --review-evidence <absolute-V11-R2-result.json> --evidence <absolute-result.json>`로 빈 C consumer를 검증 | `V11-M3-CORE-CLEAN` |
| `CORE-PKG-TEST` | `scripts/local-package/core/test-build-wsl.sh --self-test --dry-run --evidence <absolute-result.json>`; install artifact를 발행하지 않고 service header copy 0, argument·manifest·clean C consumer fixture를 검증 | `V11-M3-CORE-CLEAN` |
| `BIND-PKG-TEST` | `scripts/local-package/<cpp|dotnet|java|node>/verify-consumer.sh --self-test --dry-run --evidence <absolute-result.json>`; ID suffix `JVM`은 `java`를 선택한다. Package를 발행하지 않고 각 lane이 소유한 package metadata·resolver·public-only consumer fixture를 검증 | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` |
| `WIRE-GEN` | `node framework/runtime/protocol/generate-service-wire-assets.mjs --schema framework/runtime/protocol/service-wire-v1.schema.json <--write|--check>`; C++·C#·Java·TypeScript 생성물과 golden fixture의 source schema revision·drift를 검증 | `V11-M5-PROTOCOL` |
| `TRACE` | `node scripts/generate-v11-public-contract-trace.mjs <--write|--check>`; `framework/doc/contract-inventory/route-mesh-v11-public-contract-trace.json`의 member identity, disposition, POSD decision, spec·test·E2E·package owner와 implementation ledger ID를 deterministic하게 검증 | `SPEC-06` |
| `AMENDMENT-IMPACT` | `scripts/v11/verify-contract-amendment-impact.sh --manifest framework/doc/plan/v11.0/route-mesh-11.0.0-contract-amendment-impact.json --mode <quarantine|finalized> --evidence <absolute-result.json>`; 모든 영향 항목의 baseline·approved hash, disposition, spec·runtime·activation owner를 검증하고 quarantine을 skip·pass로 집계하지 않음 | `V11-CA-IMPACT` |
| `M6-RUNTIME` | `scripts/v11/run-framework-runtime-regression.sh --language <cpp|dotnet|jvm|node> --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; sample·E2E project와 task를 실행 graph에서 제외하고 compile, public declaration snapshot, internal unit·contract·resource·protocol regression만 실행 | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` |
| `V11-E2E` | `scripts/v11/run-cross-language-e2e.sh --slice <topology|stateful|maintenance|full> --matrix 4x4 --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; §14의 유지·변경·대체·추가 ID를 approved impact manifest와 대조하고 required skip을 실패로 처리 | `V11-M6A-E2E`, `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-M9-E2E-4X4` |
| `V11-SMOKE` | `scripts/v11/run-smoke.sh --kind <functional|perf> --candidate-manifest <absolute-candidate.json> --evidence <absolute-result.json>`; perf는 §15 최소 operation·provenance·cleanup만 판정 | `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` |
| `FW-PKG` | `scripts/local-package/framework/build-wsl.sh <cpp|dotnet|jvm|node> --binding-manifest <absolute-binding-manifest.json> --output-root <absolute-local-root> --evidence <absolute-result.json>`; persistent Framework package를 만들고 package version·Core·binding provenance를 기록 | `V11-M8-CLEAN-COMMON` |
| `FW-PKG-TEST` | `scripts/local-package/framework/test-build-wsl.sh --language <cpp|dotnet|jvm|node> --self-test --dry-run --evidence <absolute-result.json>`; persistent package를 발행하지 않고 package manifest·provenance·clean consumer fixture를 검증 | `V11-M8-CLEAN-COMMON` |

### 3.4 ID별 execution card catalog

`공통`은 `DOC`와 `DIFF-OWNED`를 뜻한다. 개별 language lane은 shared migration inventory를
수정하지 않고 자신의 candidate delta와 no-hit evidence만 만든다. `INV`는 inventory owner, join, review와
final card에서 coordinator가 병렬 lane delta를 합친 뒤만 실행한다. Wire·protocol을 읽거나 바꾸는 card는
`WIRE`도 실행한다. `V11-R7`은 임의의 dirty worktree가 아니라 reviewed base와 final candidate revision 사이의
전체 snapshot을 `DIFF-SNAPSHOT`으로 검사한다. `<ID>`는
담당 row의 exact ID이며 wildcard나 stage 이름으로 바꾸지 않는다. 모든 output은
`.artifacts/v11/candidates/<ID>.json` candidate manifest,
`.artifacts/v11/candidates/<ID>-owned-paths.json` owned-path manifest와
`.artifacts/v11/evidence/<ID>/result.json`을 포함한다.

Group card의 `<lang>`은 runtime wildcard가 아니라 ID suffix에 따른 다음 고정 매핑을 줄여 쓴 표기다.
`CPP`는 `bindings/cpp/`와 `framework/languages/cpp/`, `DN`은 `bindings/dotnet/`과
`framework/languages/dotnet/`, `JVM`은 `bindings/java/`과 `framework/languages/java/`, `NODE`는
`bindings/node/`와 `framework/languages/node/`를 뜻한다. Owned-path manifest에는 `<lang>`이나 glob을
남기지 않고 해당 ID의 실제 repository path를 나열한다.

| ID | Authoritative input | Owned source·test·package path와 산출물 | 필수 명령·runner | 완료 증거 |
|---|---|---|---|---|
| `SPEC-01` | migration machine inventory와 정식 Framework owner | inventory JSON·generator·verifier; 미분류 0 inventory | `INV`, `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-02` | Framework 공통·server spec | `framework/doc/framework/spec/`; 완결된 service 공개 계약 | `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-03` | Core raw 공개·내부 경계 | Core `09-runtime-boundary` 한국어·영문, 관련 inventory; raw-only·ZMP heartbeat 제거 계약 | `DOC`, `INV`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-04` | 다섯 언어 exact interface README와 기능별 interface | `framework/doc/framework/spec/server/languages/*/interfaces/`; member trace | `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `SPEC-05` | Framework 공통 service internals와 wire schema | `framework/doc/framework/common/internals/`, `framework/runtime/protocol/`, golden·negative fixture; schema·formal semantic drift 0 | `WIRE`, `DOC`, `DIFF-OWNED` | 담당 row, schema·formal 대조 결과와 `<ID>` result |
| `SPEC-06` | §5.2·§5.6, 다섯 exact interface, §14 scenario | `route-mesh-v11-public-contract-trace.json`, deterministic generator·checker와 ledger member trace; ctor·overload·generic·callback·enum·extension·export 누락 0 | `TRACE --write`, `TRACE --check`, `DOC`, `INV`, `DIFF-OWNED` | 담당 row, member·owner·disposition·implementation ID 미분류 0과 `<ID>` result |
| `V11-R1`, `V11-R2`, `V11-R3`, `V11-R4`, `V11-R4A`, `V11-R4B`, `V11-R5A`, `V11-R5B`, `V11-R5C`, `V11-R6` | 직접 선행 row의 candidate·evidence, §18의 I1·I2·I3·D1·D2 | source 수정 없음; Codex·Claude 독립 finding과 수렴 결과 | §18 독립 review, `DOC`, `INV`, `DIFF-OWNED`; protocol 영향 시 `WIRE` | 담당 review row, 두 reviewer 결과와 `<ID>` result |
| `V11-R7` | final candidate revision, 직접 선행 row의 evidence, §18 | source 수정 없음; reviewed base→final candidate 전체 snapshot | §18 독립 review, `DOC`, `INV`, `WIRE`, `DIFF-SNAPSHOT` | final review row, 두 reviewer 결과와 `<ID>` result |
| `V11-M2-ORACLE` | 10.x disposition, §14 baseline 대상 | `scripts/v11/run-oracle.sh`, `framework/testdata/v11/oracle/`; frozen manifest·normalized trace | `ORACLE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-CORE-READINESS` | Core raw 공개·내부 경계, machine inventory | Core·perf removal classification; Core removal manifest | `INV`, inventory 기반 Core no-hit probe, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE` | Core raw spec과 현재 binding public surface | `bindings/cpp`, `bindings/dotnet`, `bindings/java`, `bindings/node`의 read-only probe·gap 결과 | 언어별 binding build·test, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M2-BIND-READINESS` | 네 raw probe와 machine inventory | bindings·package·C/Python/Go/Rust 격리 manifest | `INV`, inventory 기반 binding no-hit probe, 공통 | 담당 row와 `<ID>` result |
| `V11-M2-READY` | M2 전체 evidence와 package 규칙 | `scripts/v11/run-ledger-gate.sh`, schema, `verify-removal.sh`; removal-first candidate contract | `ROW-GATE`, `REMOVE`, `DOC`, `INV`, `WIRE`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M3-PERF-LEGACY` | §15와 oracle manifest | `bindings/c/perf`, `scripts/v11/run-perf-legacy.mjs`; active Spot 입력 제거·10.x archive 격리와 재현 가능한 evidence 생성 | `REMOVE --scope common`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-REMOVE` | Core raw 공개·내부 경계와 Core removal manifest | `core/include`, `core/src`, `core/tests`, Core build manifest; service·ZMP heartbeat 제거 | `CORE`, `REMOVE --scope core`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-CLEAN` | POSD·DDD 원칙, M3 removal diff | Core raw source·test·build, `scripts/local-package/core/`, `scripts/local-package/native/sync-local-core-libs.sh`, `scripts/v11/verify-core-package-consumer.sh`; aggregate·unused cleanup과 review 대상 package tooling | `CORE`, `CORE-PKG-TEST`, `REMOVE --scope core`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-VERIFY` | Core raw spec과 final M3 candidate | Core build·ASAN result, raw test evidence, migration inventory JSON·generator·verifier | `CORE`, `CORE-ASAN`, `CORE-PKG-TEST`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M3-CORE-PKG` | `V11-R2` approved revision, review된 `CORE-PKG` tooling | source·tooling 수정 없음; Core 11 install artifact·header·provenance와 clean C consumer result | `CORE`, `CORE-PKG`, `ROW-GATE`, 공통 | 담당 row, approved revision·artifact path·SHA-256과 `<ID>` result |
| `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` | Core 11 package, Core raw spec, 언어 binding public surface | 각 `bindings/<lang>` source·generated·test와 `scripts/local-package/<lang>/` package metadata·clean-consumer verifier; service·heartbeat projection 0. JVM은 `build.gradle`의 Core source include를 제거하고 Node는 ESM·CJS public export를 모두 제공 | 대응 `BIND-TEST`, 대응 `BIND-PKG-TEST`, 대응 `REMOVE --scope binding:<lang>`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M4-BIND-JOIN` | 네 binding candidate·raw proof | 통합 removal·capability manifest, migration inventory JSON·generator·verifier | binding scope 네 `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M4-PKG-CPP`, `V11-M4-PKG-DN`, `V11-M4-PKG-JVM`, `V11-M4-PKG-NODE` | `V11-R3` approved revisions와 review된 package tooling | source·tooling 수정 없음; `.artifacts/wsl`의 C++ install·NuGet·Maven·tgz와 provenance | 대응 `BIND-PKG`, `ROW-GATE`, 공통 | 각 담당 row, approved revision·artifact SHA-256과 각 `<ID>` result |
| `V11-M4-CONSUMER-JOIN` | 네 local binding package와 중앙 Framework version 지점 | clean consumer workspace·resolution manifest | 네 `BIND-PKG`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M5-PROTOCOL` | wire schema, 공통 service wire·resource internals, service liveness spec | schema·generator·golden·negative fixture와 internal `livenessProbe`·`livenessAck` | `WIRE-GEN --write`, `WIRE-GEN --check`, `WIRE`, `ROW-GATE`, `DOC`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M5-SCAFFOLD-CPP`, `V11-M5-SCAFFOLD-DN`, `V11-M5-SCAFFOLD-JVM`, `V11-M5-SCAFFOLD-NODE` | 해당 exact interface, 새 binding package, 공통 service runtime architecture | 각 Framework private binding-facing port, 보존 public value type ownership과 internal compile contract; 새 port의 Core adapter·production placeholder·fake data 0. 기존 service owner 실행 경로의 제거는 M6 vertical slice와 `V11-M6-SCAFFOLD-ZERO`가 소유 | 대응 Framework M5 internal contract, 신규 owned path의 forbidden-reference audit, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M5-FOUND-CPP`, `V11-M5-FOUND-DN`, `V11-M5-FOUND-JVM`, `V11-M5-FOUND-NODE` | 공통 service runtime·wire·mailbox·resource internals, wire schema | 각 Framework transport·codec·operation·resource source와 unit test | 대응 Framework command, `WIRE`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M5-FOUND-JOIN` | 네 foundation evidence와 required E2E catalog | codec cross-language fixture·pending scenario manifest, migration inventory JSON·generator·verifier | `WIRE`, `INV`, `ROW-GATE`, 공통 | 담당 row, pending·completed·skipped count와 `<ID>` result |
| `V11-CA-DECISION` | 두 contract 변경 제안, POSD 공개 경계, M5 foundation evidence | global identity·remote create·placement·reservation·handover·failure의 결정 기록과 proposal open item disposition | `DOC`, `TRACE --check`, `ROW-GATE`, 공통 | open item 미결정 0, 대안·선택 이유·정식 spec owner와 `<ID>` result |
| `V11-CA-SPEC` | 승인한 amendment decision, Framework 공통·server 정식 spec·common internals | `framework/doc/framework/spec/`, `framework/doc/framework/common/internals/`; 변경된 목표 계약과 runtime model. 두 임시 target 문서 디렉터리와 repository link는 0 | `DOC`, `TRACE --write`, `TRACE --check`, `ROW-GATE`, 공통 | identity·placement·failure 의미, runtime owner와 migration 입력의 미분류 0과 `<ID>` result |
| `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE` | 승인한 amendment spec, 다섯 언어 exact interface | 대응 exact interface와 public declaration trace. JVM row는 Java·Kotlin을 함께 소유 | `DOC`, `TRACE --write`, `TRACE --check`, `ROW-GATE`, 공통 | public member parity·표현 차이 미분류 0과 각 `<ID>` result |
| `V11-CA-PROTOCOL` | 승인한 amendment spec·common internals와 wire schema | placement reservation·aggregate relocation·route·identity command와 schema·generated constant·golden·negative fixture | `WIRE-GEN --write`, `WIRE-GEN --check`, `WIRE`, `DOC`, `ROW-GATE`, 공통 | formal·schema·fixture semantic drift 0과 `<ID>` result |
| `V11-CA-IMPACT` | 승인한 amendment spec, baseline E2E·sample·registration·regression catalog | `route-mesh-11.0.0-contract-amendment-impact.json`, verifier와 self-test; 각 항목의 baseline hash·disposition·owner·activation stage | `AMENDMENT-IMPACT --mode quarantine`, `DOC`, `INV`, `ROW-GATE`, 공통 | 미분류 0, `pending-disabled-by-contract-amendment` 수, executed·skipped 0과 `<ID>` result |
| `V11-CA-JOIN` | amendment spec·네 exact interface·protocol·impact evidence | amendment candidate aggregate와 public contract trace·impact manifest reconcile | `DOC`, `INV`, `WIRE`, `TRACE --check`, `AMENDMENT-IMPACT --mode quarantine`, `ROW-GATE`, 공통 | 정식 계약·언어·wire·E2E·sample·regression owner 누락 0과 `<ID>` result |
| `V11-CA-DRAFT-RETIRE` | `V11-R4A`가 승인한 amendment candidate와 두 임시 변경 제안 | 두 proposal 삭제, 이 README·ledger의 임시 link 제거와 decision disposition 요약 | `DOC`, `TRACE --check`, `AMENDMENT-IMPACT --mode quarantine`, `ROW-GATE`, 공통 | 채택 내용의 formal spec·exact interface·protocol owner 누락 0, 미채택·수정 이유 미기록 0, 두 proposal과 repository link 0, `<ID>` result |
| `V11-CA-SPOT-FLUENT` | 승인한 Instance Spot direct messaging 결정, 공통 Spot spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node exact interface, document inventory·trace와 검증 script; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | global SpotRid·Spot 전용 fluent call·User-only manager·type inference·internal close parity와 `<ID>` result |
| `V11-CA-RELOCATION-LIFECYCLE` | `CA-D37~CA-D43`, 공통 Spot Actor·Location·maintenance spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node Actor·Spot·configuration·Location exact interface, document inventory·trace; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --refresh-review`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | opaque bytes·adapter kind·Snapshot invocation·Restore-before-commit·Entry callback·queue·timer·bounded concurrency parity와 `<ID>` result |
| `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | amended formal spec·exact interface, 공통 topology·dispatch·liveness internals, 보존된 Core service 구현·test | 각 Framework topology·dispatch·Location·liveness source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | amended formal spec·exact interface, 공통 mailbox·stateful object·STREAM·resource internals, 보존된 Core service 구현·test | 각 Framework Spot·Actor·STREAM·Instance source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE` | amended formal spec·exact interface, 공통 maintenance·monitoring·resource internals, 보존된 Core service 구현·test | 각 Framework maintenance·monitoring·hosting source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6-SCAFFOLD-ZERO` | 네 Framework runtime과 amendment impact manifest | production scaffold·placeholder·fake data와 quarantine 실행 graph 검사 결과 | Framework scope 네 `REMOVE`, `AMENDMENT-IMPACT --mode quarantine`, `INV`, `ROW-GATE`, 공통 | production 금지 count 0, sample·E2E source 삭제·임시 우회 0과 `<ID>` result |
| `V11-E2E-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 E2E 계약 | `framework/doc/framework/common/e2e/`, §14와 impact manifest의 approved scenario·registration hash | `DOC`, `TRACE --write`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required scenario·negative·race·matrix owner와 acceptance 누락 0 |
| `V11-SAMPLE-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 sample spec | `framework/doc/framework/common/sample/`과 impact manifest의 approved sample·registration hash | `DOC`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | 다섯 언어 sample의 public 흐름·marker·owner 누락 0 |
| `V11-R5D` | final E2E·sample spec candidate, finalized impact manifest, §18의 I1·I2·I3·D1·D2 | source 수정 없음; Codex `gpt-5.6-sol xhigh`·Claude Sonnet 독립 finding과 수렴 evidence | §18 독립 review, `DOC`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `INV`, `DIFF-OWNED` | assertion 약화·coverage 손실·언어 parity gap 0, 두 reviewer 결과와 `<ID>` result |
| `V11-M6A-E2E` | final E2E·sample spec, topology·liveness impact 항목 | topology fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice topology`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M6B-E2E` | final E2E·sample spec, stateful object impact 항목 | stateful fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice stateful`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M6C-E2E` | final E2E·sample spec, maintenance·hosting impact 항목 | maintenance·hosting fixture·runner·registration의 승인 변경과 실제 directional result | `V11-E2E --slice maintenance`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required skip 0, 발견한 runtime gap 0과 `<ID>` result |
| `V11-M7-CONTRACT` | amended common spec, 다섯 exact interface | 네 language contract suite와 public declaration comparison | 네 Framework command, `ROW-GATE`, 공통 | required contract skipped 0과 `<ID>` result |
| `V11-M7-RACE-CRASH` | §14.2와 공통 maintenance·concurrency resource internals | race·phase crash·pause·resource test와 seed manifest | `V11-E2E --slice full`, `ROW-GATE`, 공통 | 담당 row, seed·반복·결과와 `<ID>` result |
| `V11-M7-E2E-4X4` | final E2E spec과 approved impact manifest | full directional matrix result | `V11-E2E --slice full`, `E2E-CURRENT`, `ROW-GATE`, 공통 | 담당 row, required/skipped cell과 `<ID>` result |
| `V11-M7-SAMPLES` | final sample spec과 approved impact manifest | 다섯 언어 sample source·build·run evidence | `SAMPLES`, 언어별 Framework command, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` | §14 functional, §15 perf smoke-only | v11 smoke runner·result schema·provenance | 대응 `V11-SMOKE`; perf row는 `RAW-PERF-SMOKE`도 실행, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M7-JOIN` | M7 모든 candidate·evidence | correctness aggregate manifest | `ROW-GATE`, `DOC`, `INV`, `WIRE`, `DIFF-OWNED` | 담당 row와 `<ID>` result |
| `V11-M8-INVENTORY` | machine inventory와 M7 load·coverage evidence | Framework·common cleanup inventory | Framework·common `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M8-CLEAN-CPP`, `V11-M8-CLEAN-DN`, `V11-M8-CLEAN-JVM`, `V11-M8-CLEAN-NODE` | POSD·DDD 원칙, 해당 runtime·test | 각 Framework source·test·sample·build·package metadata·clean-consumer verifier cleanup | 대응 Framework command·`PACKAGE-CONTRACT` self-test, `REMOVE --scope framework:<lang>`, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M8-CLEAN-COMMON` | wire·fixture·E2E·CI inventory | protocol·fixture·runner·manifest·CI cleanup, review 대상 `scripts/local-package/framework/build-wsl.sh` | `WIRE`, 네 언어 `FW-PKG-TEST`, `REMOVE --scope common`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M8-CLEAN-JOIN` | M8 clean candidates | 네 clean build·contract와 aggregate removal result, migration inventory reconcile | 네 Framework command, 모든 Framework·common `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-RAW-FINAL` | approved Core·binding source와 package manifests | final raw·ASAN·removal·oracle isolation evidence, migration inventory reconcile | `CORE`, `CORE-ASAN`, Core·binding `REMOVE`, `INV`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` | `V11-R6` Framework source, approved Core·binding packages와 review된 `FW-PKG` tooling | source·tooling 수정 없음; persistent final local/internal Framework package와 clean consumer | 대응 `FW-PKG`, Framework command, `PACKAGE-CONTRACT`, `ROW-GATE`, 공통 | 각 담당 row, approved revision·artifact SHA-256과 각 `<ID>` result |
| `V11-M9-E2E-4X4` | final package manifests와 §14 | final full directional matrix | `V11-E2E --slice full`, `ROW-GATE`, 공통 | 담당 row와 `<ID>` result |
| `V11-M9-SMOKE-FUNCTIONAL`, `V11-M9-SMOKE-PERF` | final packages, §14·§15 | final functional·perf smoke-only result | 대응 `V11-SMOKE`; perf row는 `RAW-PERF-SMOKE`도 실행, `ROW-GATE`, 공통 | 각 담당 row와 각 `<ID>` result |
| `V11-M9-DOCS` | final public API·source·test·package | 정식 spec·guide·internals·migration·package 문서 | `DOC`, `INV`, `WIRE`, `DIFF-OWNED`, `ROW-GATE` | 담당 row와 `<ID>` result |

## 4. 전체 실행 순서

```text
SPEC-01..06 -> V11-R1 -> M2 oracle/readiness
            -> M3 Core removal/review/package
            -> M4 bindings removal/review/package
            -> M5 private binding port/foundation/review
            -> contract amendment/spec/interfaces/protocol/impact/review
            -> temporary proposal retirement
            -> E2E/sample execution quarantine
            -> M6A topology runtime/review
            -> M6B stateful runtime/review
            -> M6C maintenance runtime/review
            -> M6 production placeholder zero
            -> final E2E/sample spec -> gpt-5.6-sol xhigh/Sonnet independent review
            -> topology E2E -> stateful E2E -> maintenance E2E
            -> M7 full E2E/race -> samples -> correctness/smoke
            -> M8 Framework cleanup/review
            -> M9 final package/re-proof/E2E/smoke/docs/review
```

정식 spec과 exact interface를 확정하기 전에 구현을 시작하지 않는다. 제거 전 Core service 구현과 test는
별도 snapshot에 이미 보존되어 있으며, 이 보존본을 상태 기계, queue, wire encoding, ordering과 오류 처리의
읽기 전용 구현 기준으로 사용한다. Framework runtime을 처음부터 각각 새로 설계하지 않고 계약이 유지된
component와 algorithm을 각 언어 runtime에 포팅한다. 11.0에서 변경한 의미만 정식 spec·internals·schema를
기준으로 바꾼다. 보존 snapshot은 Core 11에 다시 포함하거나 runtime dependency로 유지하지 않는다. 실제
migration은 removal-first 순서로 진행한다. 동작 결과는 별도 Core 10 process의 normalized trace와 대조한다.
새 Core·bindings·Framework candidate는 oracle artifact를 compile, link 또는 load하지 않는다.

Core와 bindings는 각각 제거·POSD·DDD review가 끝난 뒤에만 version을 올리고 local/internal package를 만든다.
그 package를 입력으로 네 Framework runtime의 private binding-facing port와 service runtime을 같은 계약
snapshot에서 병렬 구현한다. 완료한 초기 SPEC·M5 evidence는 당시 snapshot의 이력으로 유지하고 다시 열지
않는다. M5 뒤 contract amendment가 변경된 정식 spec, exact interface, protocol과 영향 목록을 새 snapshot으로
고정하고 독립 review를 통과한 뒤 M6를 시작한다. M6는 internal contract와 독립 review에서 합류하며
topology·stateful·maintenance runtime과 production placeholder 제거가 끝날 때까지 E2E·sample 실행을 격리한다.
그 뒤 E2E·sample spec을 최종 확정하고 E2E를 작은 묶음부터 활성화한다. 전체 E2E와 race·crash가 통과한 뒤
sample을 실행한다.

## 5. SPEC — 구현 전 계약 확정

SPEC은 모든 구현 stage보다 우선한다. `V11-R1`이 완료되기 전에는 public API, runtime source, Core·binding
제거와 package version 변경을 수행하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `SPEC-01` | 전수 contract·source·test·package inventory와 10.x disposition | coordinator, `P-SCAN` | 없음 | 완료 | Core service 의미, 네 bindings projection, 다섯 Framework public member, test·sample·package·perf 항목의 누락·미분류 0 | `INV` exit 0: 4,331 records(Core public 523, export 98, binding declaration 1,020, semantic unit 100, exact Core-symbol reference 1,009, file 1,581). Core 문서 75개는 `Remove` 6·`Rewrite` 46·`Retain` 23이며 document 2·raw rewrite 1·file routing 10·public header 2의 negative mutation 15건이 모두 검출됐다. `DOC` exit 0에서 exact 문서 56개·package declaration owner 1,575개와 inventory drift 0을 확인했다. |
| `SPEC-02` | Framework 공통 정식 spec | contract lane, `P-DEEP` | `SPEC-01` | 완료 | topology, messaging, Spot, Actor, Instance, STREAM, lifecycle, maintenance, 오류와 관측 의미 완결 | Framework formal 136개, target spec·internals 20개와 semantic owner 10개를 `DOC` exit 0으로 검증했다. Service public 의미와 Core raw 책임 경계의 미분류·끊어진 link는 0건이다. |
| `SPEC-03` | Core raw 정식 spec과 ZMP heartbeat 제거 계약 | Core contract lane, `P-DEEP` | `SPEC-01` | 완료 | Core raw 범위, service API와 heartbeat option·frame·engine timer 제거, 유지할 generic timer·monitor 확정 | 정본 `core/doc/spec/core/09-runtime-boundary.*`, `core/doc/internals/runtime-boundary.*`와 inventory를 `DOC`·`INV`·`git diff --check` exit 0으로 검증했다. ZMP heartbeat option·command·engine timer는 제거 대상이고 PGM SPM·generic timer·raw monitor는 보존 대상이다. |
| `SPEC-04` | 다섯 언어 exact interface | CPP·DN·JVM·NODE contract lanes, `P-DEEP` | `SPEC-02` | 완료 | C++, .NET, Java, Kotlin, Node.js public member parity 100%, Java·Kotlin 별도 ABI 확정 | Exact 문서 56개의 fence 178개를 package contract 160·application example 15·documentation support 3으로 분류하고 package declaration owner 1,575개와 canonical member 6,069개를 검증했다. C++ 10/33/345, .NET 18/41/311, Java 9/25/444, Kotlin 9/20/45, Node.js 10/41/430(문서/package fence/owner)이며 duplicate·unknown owner·forbidden exact surface는 0건이다. |
| `SPEC-05` | Service·maintenance protocol과 major invariants | protocol·architecture lanes, `P-DEEP` | `SPEC-02`, `SPEC-03` | 완료 | frame·field·version·error·상한, lifecycle·authority·ordering·recovery 불변 조건 승인 | `WIRE` exit 0: schema 37 command·132 type·4 flag·24 bound, durable fixture 3개, logical·JSON·authority-key fixture 각 1개, negative self-test 124개. Formal·target termination outcome·reason drift, stale generation과 semantic drift 0건을 `DOC`의 termination mutation 3건과 함께 확인했다. |
| `SPEC-06` | POSD API review와 contract·E2E·package 추적표 | architecture·E2E lanes, `P-DEEP` | `SPEC-02`, `SPEC-03`, `SPEC-04`, `SPEC-05` | 완료 | 비자명 API마다 대안 2개, caller 부담·정보 은닉 판단, 모든 계약의 test·package owner 존재 | POSD decision 60개와 canonical trace member 6,069개를 deterministic checker로 검증했다. Exact signature 1,945, canonical signature 238, reviewed override 230, intentional removal 912, unclassified·ambiguous·unknown owner 0, negative mutation 15건이다. Instance Spot·async submit verifier와 E2E `M01~M68`, liveness `L01~L06`, race `RACE01~RACE38` 112개가 각각 한 owner에 연결됐다. |
| `V11-R1` | SPEC 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `SPEC-01`, `SPEC-02`, `SPEC-03`, `SPEC-04`, `SPEC-05`, `SPEC-06` | 완료 | 두 독립 reviewer의 finding을 모두 반영하고 post-fix machine gate 통과 | Base `86258cb9a3ec`, candidate `.artifacts/v11/spec-review-candidate.json`은 repository path·mode·base/current SHA-256을 가진 255개 파일과 direct fixture 19개를 고정한다. 1회차 finding 16건과 2회차 고유 finding 5건을 모두 반영한 뒤 `DOC`, `INV`, `WIRE`, `TRACE`, `INSTANCE`, `SUBMIT`, candidate check와 `git diff --check`가 모두 exit 0이었다. 2026-07-21 사용자가 추가 review를 종료하고 구현 전 계약을 승인했으므로 3회차는 실행하지 않았다. Aggregate와 승인 기록은 candidate와 `.artifacts/v11/evidence/V11-R1/`이 소유하며 ledger 본문에 복제하지 않는다. |

### 5.1 무손실 이관 inventory

아래 범주는 `SPEC-01`의 machine inventory에서 symbol, public member, source, target와 package 입력 단위로
확장한다. 표에 없는 service 항목이 검색되면 먼저 이 표와 machine inventory에 추가한 뒤 처리한다.

| 현재 소유 영역 | 11.0 처리 | 구현·제거 gate |
|---|---|---|
| `core/include/zlink/service/{common,dispatch,mesh_node,spot,actor,stream_session,instance_spot_driver}.h` | Application이 관찰하는 의미는 Framework 정식 spec이 소유하고 구현 구조는 공통 service internals가 소유한다. C ABI type·token·함수는 제거 | `SPEC-01`, `SPEC-02`, `SPEC-03`, `V11-M3-CORE-REMOVE` |
| `core/include/zlink/eventing/api.h`의 Spot timer·Mesh monitor, `socket/api.h`의 ChannelName, `zlink_enum.h`의 MeshNode poller source, `zlink.h`, install header와 `libzlink.vers` | Generic timer·raw monitor·raw poller는 유지하고 service 결합 type·symbol·include만 제거 | `SPEC-03`, `V11-M3-CORE-REMOVE` |
| `core/src/api/mesh/`, `core/src/runtime/services/mesh/` | Protocol·mailbox·Actor·Spot·transfer·monitor 불변 조건은 정본과 oracle trace로 보존하고 source는 먼저 제거 | `SPEC-05`, `V11-M2-ORACLE`, `V11-M3-CORE-REMOVE` |
| `service_control_runtime`, Spot timer seam, ChannelName option·socket field와 service build 입력 | Raw 사용자에게 필요한 generic scheduler·timer·request 경로는 유지하고 service 전용 state와 target만 제거 | `SPEC-03`, `V11-M3-CORE-CLEAN` |
| Core Mesh·Spot·Actor·Instance·STREAM integration, unit, benchmark와 public-surface test | Service test·benchmark는 제거하고 raw 회귀만 Core 11 gate로 유지. Service 의미 검증은 이후 Framework contract·E2E가 소유 | `V11-M3-PERF-LEGACY`, `V11-M3-CORE-REMOVE`, `V11-M7-JOIN` |
| Core guide·internals·spec의 service 의미 | Explicit reviewed manifest에서 파일별 `Remove`·`Rewrite`·`Retain`을 고정한다. ZMP guide의 SPOT envelope는 Framework service wire owner로 옮기고 Core 문서는 raw ZMP만 설명한다 | `SPEC-01`, `SPEC-03`, `V11-M3-CORE-CLEAN`, `V11-M9-DOCS` |
| C++·.NET·Java·Node.js bindings의 service wrapper·generated symbol·test | Core 11 package를 기준으로 projection을 제거하고 일반 raw socket 사용자에게도 유효한 public capability만 보완 | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` |
| C, Python, Go와 Rust bindings | 10.x oracle 조합에 격리. Core 11 build·package·CI와 호환 표기에서 제외하고 새 candidate가 link·load하지 않음 | `V11-M2-BIND-READINESS`, `V11-M9-RAW-FINAL` |
| C++·.NET·Java·Kotlin·Node.js Framework public surface | 초기 exact interface evidence는 이력으로 유지한다. M5 이후 amendment에서 global identity·remote placement 계약과 다섯 언어 표현을 다시 고정하며 Java·Kotlin은 JVM runtime 하나를 공유 | `SPEC-04`, `V11-CA-SPEC`, `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE`, `V11-M6-SCAFFOLD-ZERO` |
| Sample, common E2E, race·crash test와 hosting integration | Amendment impact manifest에서 `retain`·`amend`·`replace`·`add`·`remove`와 대체 coverage를 먼저 확정한다. Runtime 중에는 source·registration을 보존하고 실행만 격리하며, runtime 완료 뒤 spec을 확정하고 topology→stateful→maintenance→full matrix→sample 순서로 활성화한다 | `V11-CA-IMPACT`, `V11-E2E-SPEC-FINAL`, `V11-SAMPLE-SPEC-FINAL`, `V11-M6A-E2E`, `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-M7-SAMPLES`, `V11-M7-JOIN`, `V11-M9-E2E-4X4` |
| `bindings/c/perf`의 active Spot suite | 10.x oracle trace를 봉인한 뒤 active Spot source·target·pattern·parser·CI 입력을 제거. Raw socket perf와 읽기 전용 10.x archive는 유지 | `V11-M2-ORACLE`, `V11-M3-PERF-LEGACY`, §15 |
| Core·binding·Framework 문서와 package metadata | 정식 spec·common internals를 구현에 맞춰 검증하고 제거한 service API 링크·version·package 입력을 정리 | `V11-M9-DOCS`, `V11-R7` |

Core 제거 inventory는 최소한 `zlink_mesh_node_*`, ready·receive batch와 claim·reply, `zlink_spot_*`, Actor,
Instance Spot, STREAM session, Mesh monitor와 Spot timer symbol family를 각각 펼쳐 기록한다. Prefix 하나를 완료
증거로 사용하지 않으며 구조체 field, enum 값, callback, exported symbol과 generated binding member도 독립 행을
가진다.

Core 문서 inventory는 blanket regex 결과를 곧바로 처리 결정으로 사용하지 않는다. Review한 exact path 75개와
각 target owner를 manifest에 기록하고, regex는 manifest 밖의 새 service 문서를 찾는 음성 검증에만 사용한다.
Allowlist에서 기존 ZMP guide를 제거하는 변이와 새 service guide를 추가하는 변이가 모두 실패해야
`SPEC-01`의 문서 누락·미분류 0을 인정한다.

초기 v11 입력에서 확인한 의미는 아래 표의 현재 소유 문서가 담당한다. 이전 입력 파일이나 문서 hash는 보존
조건이 아니다. 표에 적힌 계약, 실행 gate와 machine inventory 항목이 현재 소유 문서에서 확인되는지를 기준으로
판단한다.

| 대조한 입력 | 고유하게 확인할 내용 | 현재 단일 소유자 |
|---|---|---|
| 통합 구현 계획 | 언어별 runtime 경계, stage 선행 조건, package·rollback·cleanup gate | 이 ledger의 §1·§4·§6~§19, [v11 README](README.ko.md) |
| Core service 이관 inventory | Core spec 절·공개 symbol·binding projection·dirty 변경의 처리, raw capability proof | `route-mesh-v11-core-service-migration-inventory.json`, 정식 Framework spec·common internals owner, `SPEC-01`, `V11-M2-CORE-READINESS`, `V11-M2-BIND-READINESS` |
| Core socket heartbeat 계획 | ZMP heartbeat option·frame·engine timer·binding projection 제거와 Framework internal liveness command·lease·transport monitor의 분리 | Core raw 정식 spec·internals, §5.5, `V11-M3-CORE-VERIFY`, `V11-M5-PROTOCOL`, `V11-M6A-E2E`, `V11-M7-E2E-4X4`, `V11-M9-RAW-FINAL` |
| Service 성능 test 이관 계획 | 세 Framework smoke workload, fail-closed provenance, Core 10.x archive와 active suite 분리, 정량 성능 비목표 | `V11-M2-ORACLE`, `V11-M3-PERF-LEGACY`, §12의 `V11-M7-SMOKE-PERF`, §15 |
| Stateful rolling maintenance 초안 | Host 상태·종료 결과, preflight, authority CAS, transfer, transfer·recovery·object별 규칙 | [Location runtime](../../framework/spec/server/40-location-runtime.ko.md), [Actor](../../framework/spec/server/22-actor-model.ko.md), [Spot Actor](../../framework/spec/server/23-spot-actor.ko.md), [STREAM](../../framework/spec/server/31-session-actor-dispatch.ko.md), [stateful maintenance internals](../../framework/common/internals/stateful-maintenance-runtime.ko.md), §11·§14 |
| 통합 public-contract 문서 묶음 | 다섯 언어 public member 전수 범위, 의도적 delta 분류, public raw dependency와 package 검증 | 언어별 `interfaces/`, 아래 public member trace, `SPEC-04`, `SPEC-06`, `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE`, `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` |
| Stateful maintenance public-contract 문서 묶음 | `Retire`·`Shutdown`, typed transfer policy, authority·transfer와 언어별 비동기 표현 | [Location runtime](../../framework/spec/server/40-location-runtime.ko.md), [Host maintenance](../../framework/spec/server/54-graceful-drain-handoff.ko.md), 다섯 언어 exact `configuration-host`·`location-maintenance`·`monitoring`, `SPEC-04` |

진행 문서는 이 ledger 하나만 유지한다. Framework spec, common internals와 언어별 exact interface는 각각
공개 계약, 내부 구조와 signature를 소유하며 진행 상태와 migration 이력을 포함하지 않는다.

### 5.2 SPEC-04 exact interface 감사 증거

다섯 언어의 exact interface는 기능별 `interfaces/` 문서가 소유한다. 2026-07-21 문서 검증은 56개 문서의
fence 178개를 package contract 160개, application example 15개와 documentation support 3개로 분류했다.
Package declaration owner 1,575개를 확인했으며 code fence, 상대 link, 중복 package owner와 금지 public 표면은
모두 0건이다. Application example의 public type을 package owner로 분류하는 변이는 검증에 실패한다.

| 언어 | 확정한 경계 | 문서 검증 | 구현 단계에 남은 gate |
|---|---|---|---|
| C++ | ChannelName-only, role builder, manual·automatic classic fanout, opaque authority CAS, Retire·Shutdown | exact 10개 문서·package fence 33개·owner 345개 검증 통과 | install-tree header, clean consumer와 package ABI 비교 |
| .NET | generic Actor lifecycle, actor-free Instance, LocationStore authority capability, async-only submit | exact 18개 문서·package fence 41개·owner 311개 검증 통과 | Instance Spot target member 미구현으로 발생하는 source build 오류 해결 |
| Java | JVM runtime의 Java 정본, role builder, factory policy, opaque authority CAS | exact 9개 문서·package fence 25개·owner 444개 검증 통과 | binding/source 불일치 해결 뒤 fresh JAR ABI 비교 |
| Kotlin | Java runtime 재사용, coroutine extension과 Java member 충돌 회피 | exact 9개 문서·package fence 20개·owner 45개 검증 통과 | Java blocker 해결 뒤 Kotlin artifact와 compiler bridge 재감사 |
| Node.js | ChannelName-only, role builder, event-loop 비동기 submit, opaque authority CAS | exact 10개 문서·package fence 41개·owner 430개 검증 통과 | packed artifact export 재감사 |

다음 표면은 다섯 언어 target에서 제거됐다.

- Channel call의 MeshName+ChannelName overload와 `ChannelName(...)` alias
- Server 역할을 선택하기 전 weight·handler 설정
- Actor·Instance phase별 Store와 factory policy와 분리된 transfer registry
- 공개 `TrySubmit`과 MeshNode drain policy

마지막 항목은 MeshNode별 policy enum과 partial-drain 설정을 뜻한다. 언어 exact interface가 기존 사용자를 위해
host 전체 `Shutdown`에 위임하는 deprecated `Drain` 이름을 유지하는 것은 별도 host-wide compatibility 표면이며,
MeshNode drain policy가 남은 것으로 세지 않는다. 이 facade는 새 outcome, reason이나 component별 drain
authority를 만들 수 없다.

Exact 문서 검증은 runtime 구현 완료 증거가 아니다. Fresh source·package가 아직 target signature를 제공하지
않으면 해당 언어 구현 lane의 gap으로 남긴다. 마지막 성공 artifact의 hash나 이전 문서 aggregate hash를 현재
계약의 승인 근거로 사용하지 않고, 바뀐 declaration과 직접 영향받는 artifact를 다시 비교한다.

`SPEC-06`은 exact 문서의 각 public declaration owner를 member 단위 trace에 연결한다. 파일별 type 수만 세거나
대표 예제로 대신하지 않는다. Constructor, overload, generic bound, callback, enum value, extension·decorator,
DI token과 package export도 각각 추적한다. 각 행은 다음 정보를 가져야 한다.

- 언어, package와 fully qualified member identity
- 공통 정식 spec과 언어 exact interface 주소
- `verified-baseline`, `v11-first-implementation`, `intentional-removal` 중 하나의 분류
- 비교한 interface 대안 두 개 이상과 caller 부담·정보 은닉을 기준으로 한 선택 근거
- contract test, 공통 E2E scenario와 package·clean-consumer owner
- 검증한 source·package candidate와 구현 차이를 소유하는 ledger ID

Fully qualified identity는 machine trace에서 member를 구분하는 key다. 언어별 exact interface의 source signature는
이름 충돌이 없으면 import와 짧은 type 이름을 사용하는 등 해당 언어의 일반적인 표기를 유지한다.

`verified-baseline`만 이전 backend와 shadow 결과를 비교한다. `v11-first-implementation`은 정식 spec,
contract test와 E2E로 처음 검증하며 존재하지 않는 이전 구현과 동등하다고 기록하지 않는다. Runtime 이관만으로
생긴 public rename·removal·wrapper addition은 `intentional-removal`의 정식 계약 근거가 없으면 blocker다.

Member trace는 다음 공개 범주를 빠짐없이 포함한다.

| 범주 | 계약 owner | 구현·package owner |
|---|---|---|
| Host lifecycle, bootstrap와 hosting | `05-framework-api`, target `01`·`07`·`08`, 언어별 `configuration-host`·`monitoring` | `V11-M5-FOUND-JOIN`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` |
| Topology, discovery와 RID allocation | target `01`·`07`, 언어별 topology·location·allocation | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE`, `V11-M6A-E2E`, `V11-M7-E2E-4X4` |
| Messaging, handler와 execution context | target `02`, 언어별 common-runtime·channel-messaging | `V11-M5-FOUND-JOIN`, `V11-M6A-E2E`, `V11-M7-E2E-4X4` |
| Spot와 Instance Spot | target `03`·`06`, 언어별 spots | `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE`, `V11-M6B-E2E`, `V11-M7-E2E-4X4` |
| Actor와 transfer | target `04`·`07`, 언어별 actors·location-maintenance | `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| STREAM과 bound session | target `05`, 언어별 stream-session | `V11-M6B-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| Location authority, transfer와 target eligibility | target `07`, 언어별 location-maintenance | `V11-M6A-E2E`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |
| Observability, diagnostics와 error | target `08`, 언어별 monitoring | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE`, `V11-M6C-E2E`, `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH` |

### 5.3 SPEC에서 고정할 major invariants

- lifecycle 상태 writer는 각 process의 해당 언어 Framework runtime 하나다.
- `Retire`는 preflight 뒤 continuity transfer를 수행하며, `Shutdown`은 새 transfer를 시작하지 않고 bounded
  terminal 결과로 완료된다.
- Preflight가 실패하면 admission과 node 상태를 변경하지 않는다.
- Preflight는 target eligibility와 bounded headroom만 확인한다. Reversible admission seal 뒤 exact accepted
  inventory를 만들고 target reservation ACK와 모든 object의 `Prepared` CAS를 완료한 뒤에만 `Draining`을
  게시한다.
- Admission seal 전에 수락된 work, STREAM barrier와 timer ordering은 terminal cleanup 전에 처리한다.
- Location Store는 TTL 없는 durable owner·phase·generation authority를 CAS로 갱신한다. Process owner lease와
  Transfer Store의 leased opaque state는 authority row 수명과 분리한다.
- Owner lease를 사용하는 host는 routing ID 자동 할당 여부와 무관하게
  `renew interval + renew timeout < TTL - owner lease fencing margin`을 startup에서 검증한다. Store failure grace는
  discovery reconcile만 유예하며 owner·coordinator lease와 local monotonic admission deadline을 연장하지 않는다.
- Authority row는 1 MiB 이하 compact metadata와 transfer root reference만 보유한다. Journal, reply payload와
  full terminal completion은 chunked transfer stream이 한 번만 소유하며 scan page는 encoded byte 상한을 가진다.
- Stale owner, transfer token, generation, reply와 timer tick은 새 owner admission을 통과하지 못한다.
- Commit 전후 crash, orphan transfer와 committed target replacement가 하나의 terminal transaction으로
  수렴한다.
- Stable transfer ID는 transfer·journal·replay·terminal identity를 유지하고 target attempt generation만
  replacement reservation마다 증가한다.
- Transfer phase는 main authority owner와 target field의 closed table을 따른다. `Preparing`·`Captured`의 main
  owner는 immutable source이고 target field는 없으며, `Prepared`는 source owner와 exact target
  token·attempt·reservation·transfer를 갖는다. `Prepared`에서 `Committed`로 바뀌는 단일 owner CAS가 main
  owner를 target으로 변경하고 이후 `Completed`까지 같은 current target fence를 유지한다.
- `Activated` target은 restore·replay와 route staging만 끝낸 상태이며 application·session admission은
  `Completed` CAS까지 sealed다. Source cleanup terminal 뒤 `Completed`가 된 경우에만 ready와 ingress를 열고
  transfer를 해제한다. 이후 owner loss는 종료된 transfer transfer를 replay하지 않는다.
- Cancellation은 waiter만 중단하며 이미 시작한 lifecycle·transfer operation을 되돌리지 않는다.
- Request의 reply·timeout·cancellation·shutdown 경쟁은 terminal completion 하나만 만든다.
- Transfer journal의 request reply는 authenticated request-source ACK 또는 accepted record에 고정한 exact
  request-source owner lease의 만료가 확인될 때까지 durable하다. Physical connection 종료나 caller timeout만으로
  terminal 처리하지 않으며 그 전에는 `Completed`와 transfer release를 허용하지 않는다.
- Terminal completion 추가와 ACK·exact request-source lease-expiry 전이는 새 immutable transfer root를
  만든다. Expected authority store version CAS 한 번으로 root·checksum·terminal completion count·pending relay
  count를 함께 교체하며, authority count와 참조 transfer vector가 다르면 recovery error로 처리하고
  `Completed`를 금지한다.
- Target replacement가 발생하면 같은 stable transfer의 target factory와 Snapshot restore는 attempt마다 한 번 이상
  호출될 수 있고 stale attempt와 겹칠 수 있다. Current exact owner와 target attempt만 completion commit과
  application admission을 열 수 있으며 public callback에 internal transfer ID를 노출하지 않는다.
- Transfer journal, accepted backlog, queue와 pending operation은 정식 상한을 가진다.
- Protocol 상수와 wire 값은 schema·생성물 밖에서 수동으로 복제하지 않는다.

### 5.4 Core 포팅 입력과 v11 protocol delta

각 기능 lane은 구현을 시작하기 전에 보존된 Core service 구현·test snapshot의 관련 component, type,
state machine, algorithm과 failure case를 inventory record에 연결한다. 새 Framework runtime을 독립적인
greenfield 구현으로 만들지 않고 기존 구현을 해당 언어의 memory·concurrency model과 public raw binding 경계에
맞게 포팅한다. 보존된 구현과 정식 spec이 다르면 spec이 우선하고 차이를 해당 lane evidence에 기록한다. 한
언어 구현을 새 기준으로 삼아 나머지 언어가 순차적으로 따라가지 않고, 네 runtime이 같은 Core reference와
계약 snapshot에서 병렬로 포팅한다.

현재 Core wire에서 그대로 보존하는 값은 command ID `1..4`, `16..39`, magic `ZM`, wire major `1`과 flag
`0x01`·`0x02`·`0x04`다. 다음 항목은 현재 Core 구현 완료 증거가 아니라 v11 runtime에서 새로 구현하고
fixture로 검증할 delta다.

- `service-wire-v1.schema.json` 한 곳에서 네 언어 protocol 상수와 codec table을 생성한다.
- Framework service liveness에 `livenessProbe=5`, `livenessAck=6`을 추가한다. Probe ID는 connection lifetime
  안에서 0이 아닌 값이며 ack는 같은 ID를 그대로 반환한다.
- Stateful maintenance reply relay에는 `replyRelayAck=46`을 추가한다. Stable transfer ID·operation ID, accepted
  record에 고정한 exact request-source owner·lease·node fence와 terminal status만 전달하며 application payload나
  metadata를 싣지 않는다. Physical connection identity는 인증된 transport context일 뿐 terminal 증거가 아니다.
- Required capability `framework-service-v11`과 descriptor extension flag `0x08`을 추가한다.
- Runtime state, application version, type·state capability, capacity와 maintenance wave를 descriptor TLV로
  검증한다.
- Endpoint 상한을 목표 schema의 4,096 byte로 통일하고 allocation 전에 모든 count·length·UTF-8을 검증한다.
- Command가 허용하지 않는 flag, unknown bit, duplicate TLV, trailing byte와 malformed payload를 공통 protocol
  error로 거부한다.
- Instance activation은 Location Store authority snapshot과 local monotonic admission deadline을 message, timer,
  factory completion과 phase update에 적용한다.
- Reply public operation은 첫 terminator가 한 번 소비하고, runtime 내부 admission operation이 send-ready,
  timeout·shutdown과 source terminal failure를 끝까지 소유한다.
- Actor·Instance phase별 provider method는 제거하고 Location Store의 opaque expected-version CAS가 owner와
  transfer phase를 같은 payload로 갱신한다.

이 delta는 `SPEC-05` 승인, schema·golden·normalized trace와 네 decoder의 negative fixture가 통과하기 전에는
Core 동작을 그대로 포팅했다는 근거로 사용할 수 없다.

### 5.5 ZMP heartbeat 제거와 service liveness 규칙

`SPEC-03`은 Core의 `ZLINK_OPT_HEARTBEAT_IVL`, `ZLINK_OPT_HEARTBEAT_TTL`,
`ZLINK_OPT_HEARTBEAT_TIMEOUT`, ZMP `HEARTBEAT`·`HEARTBEAT_ACK` frame, TCP·WebSocket engine timer와 관련
test를 제거 대상으로 고정한다. C++·.NET·Java·Node bindings도 같은 option projection을 M4에서 제거한다.
PGM transport 자체의 `PGM_HEARTBEAT_SPM`은 ZMP socket heartbeat와 다른 protocol 설정이므로
Core raw transport에 명시적으로 `Retain`한다.

C·Python·Go·Rust의 heartbeat projection은 Core 11 이관 대상이 아니다. Core 10.x 전용 조합에
`Retain/OutOfScopeV11`로 격리하고 Core 11 package·CI·compatibility metadata에서만 제외한다.

Framework runtime은 Core heartbeat option이나 제거한 frame을 호출하지 않는다. Orderly close, FIN,
RST와 local transport failure는 public raw monitor event를 관측한 즉시 반영한다. 이 전환에 intentional delay를
두지 않으며 E2E의 5초는 상태 전환 지연이 아니라 process·monitor 결과를 확인하는 test observation
budget이다.

RouteMesh와 ClientServer의 admitted bidirectional connection에서 transport event가 없는 half-open 상태는
Framework service wire의 `livenessProbe=5`·`livenessAck=6`과 monotonic deadline으로 판정한다. Application
traffic과 관계없이 5초마다 0이 아닌 connection-local probe ID를 보내고, ack는 같은 ID를 그대로 반환한다.
Current connection에서 보낸 probe의 matching ACK가 15초 동안 없으면 다른 inbound application frame이 계속
도착해도 해당 connection을 not-ready로 전환하고 닫는다. 다른 valid inbound frame은 diagnostics를 갱신할 수
있지만 round-trip deadline을 연장하지 않는다.

Manual·automatic classic fanout은 raw PUB/SUB 단방향이므로 ACK를 보내지 않는다. Subscriber는 automatic
publisher descriptor 또는 manual publisher endpoint별로 전용 SUB socket 하나를 사용해 failure를 publisher에
귀속시킨다. Publisher는 application traffic과 관계없이 5초마다 schema가 예약한 one-way
fanout liveness beacon을 보낸다. Subscriber는 유효한 application frame과 beacon을 모두 receive activity로
계산하고 15초 동안 둘 다 받지 못하면 해당 publisher만 not-ready로 전환해 disconnect·reconnect한다.
Beacon은 application queue, topic filter의 사용자 payload와 handler에 전달하지 않는다.

Beacon은 `topicFrameBytes=01 5A 4C 46 31`, `reservedTopicMatch=exact-only`,
`payloadFrameBytes=5A 46 01 01`, `multipartFrameCount=2`로 고정한다. Reserved topic과 일치하지만 payload나
2-frame 구조가 잘못된 record는 `malformedReservedTopic=protocol-error-immediate-not-ready`로 처리한다.
Subscriber는 application topic filter와 별도로 exact reserved beacon topic을 항상 구독한다. Readiness는 첫
valid application frame 또는 valid beacon을 수신한 뒤에만 성립한다.
Public fanout publish는 exact reserved topic만 호출 인자 오류로 거부하고 같은 prefix 뒤에 byte가 하나 이상
추가된 topic은 일반 application topic으로 허용한다. `V11-M5-PROTOCOL` codec negative fixture는 exact topic의
잘못된 payload와 extra frame을 각각 거부한다. `V11-M6A-E2E`는 Config 3 `PS-F1~F5`와 Config 5
`RL-E1~E5`를 네 runtime에서 실행한다.

이 값과 command를 application public option으로 노출하지 않는다. Probe·ACK·fanout beacon의 exact wire
계약은 `service-wire-v1.schema.json`이 정본이며 M5에서 golden·negative fixture와 네 codec을 만든다.

Location owner lease는 distributed authority와 process-pause fencing만 판정한다. Service liveness probe, owner lease,
STREAM session heartbeat, request timeout과 reconnect deadline을 서로 대체하지 않는다. Terminal cleanup은
`livenessProbe` scheduler, reconnect timer와 monitor subscription을 connection보다 늦게 남기지 않는다.

### 5.6 POSD 공개 경계 결정

비자명한 공개 경계는 최소 두 대안을 비교하고 caller가 알아야 하는 상태와 순서를 더 적게 만드는 쪽을
선택했다.

| 결정 | 비교한 대안 | 선택과 이유 |
|---|---|---|
| Service runtime 배치 | 공통 native C runtime / 언어별 runtime | 언어별 runtime. Task·coroutine·Promise와 DI lifecycle을 언어 안에 감추고 Framework 전용 C ABI를 만들지 않음 |
| Connection liveness 소유 | Core ZMP heartbeat 유지 / Core heartbeat 제거와 Framework topology별 liveness | 후자. Raw socket protocol에서 service failure 판단을 제거하고 RouteMesh·ClientServer probe·ACK와 단방향 fanout beacon의 차이를 Framework 내부에 감춤 |
| Manual peer lifecycle | Caller가 persistent generation을 설정 / runtime opaque lifecycle token과 current connection handover | 후자. Store가 없는 배포에서도 caller storage를 요구하지 않고 token equality와 physical connection lifetime으로 stale event를 fence하며 숫자 대소를 해석하지 않음 |
| Fanout publisher failure 귀속 | Shared SUB와 publisher identity 공개 / publisher별 dedicated SUB | 후자. Publisher 식별·timeout·reconnect를 runtime 내부 socket 수명으로 해결하고 caller에게 transport identity와 분기 책임을 노출하지 않음 |
| Handler topology context | 모든 handler에 MeshName / direct node에만 MeshName과 source node RID | Direct node context에만 두 필드를 둠. Channel·Spot handler가 물리 mesh를 알 필요가 없음 |
| Channel target 선택 | MeshName+ChannelName / process-local unique ChannelName | ChannelName 하나. 물리 topology lookup을 runtime에 감추고 direct RID만 MeshName을 요구함 |
| Actor transfer 시간 설정 | Actor 전용 transfer timeout과 forwarding window / forwarding window와 host deadline | Forwarding window 하나만 Framework option으로 제공한다. 기본값은 30초이고 0은 전달을 끈다. Transfer 전체 상한은 `Retire` host deadline이 소유한다 |
| Channel builder | 역할 전 공용 설정 / `Client()`·`Server()` 뒤 역할별 설정 | 역할별 builder. Client에 weight·handler 같은 무효 상태를 표현하지 않음 |
| Owner authority Store | phase별 provider method / opaque expected-version CAS | Location provider의 CAS 하나. Framework 상태 기계를 provider마다 복제하지 않음 |
| Instance activation 공개 경계 | target·token과 begin/commit/close lifecycle 공개 / global Spot RID fluent call과 factory 등록만 공개 | 후자. Owner claim·fencing·activation barrier는 언어별 runtime이 소유하고 caller는 owner ID, generation, epoch와 native token을 전달하지 않음 |
| Transfer policy | 별도 adapter registry / factory-attached policy와 adapter | Factory-attached `Disabled`·`Recreate`·`Snapshot`. Snapshot adapter는 application-owned opaque bytes만 처리하고 format·version을 Framework에 노출하지 않음 |
| Transfer 시작 | Object별 explicit operation / host `Retire`만 사용 | Host `Retire`만 사용. Application이 target·phase·retry를 조합하는 public state machine을 만들지 않음 |
| Host 종료 | Drain variant 확장 / `Retire`·`Shutdown` | 두 operation을 정본으로 사용. Legacy host Drain은 별도 의미 없는 deprecated `Shutdown` facade만 허용 |
| One-way submit | `TrySubmit`과 async submit / async submit 하나 | Async submit 하나. Cold resolve와 bounded admission을 caller에게 노출하지 않음 |
| Spot lifecycle 구성 | 모든 Spot에 lifecycle callback 8개를 한 interface로 노출 / base·Actor·Entry capability와 actor-free Instance interface 분리 | 후자. Instance Spot은 `Configure`·initialize·closing만 제공하고 Actor callback을 알 필요가 없다. User·Entry Spot도 실제 역할의 interface만 조합하므로 무효 callback 구현을 요구하지 않음 |
| Remote maintenance | 공통 고정 HTTP route / hosting·application endpoint | 공통 route 제외. 인증·인가와 deployment targeting을 hosting 경계에 유지함 |
| Transfer Store 구성 | Location Store에 payload 통합 / 별도 Location·Transfer capability | 후자. Transfer Store는 immutable payload를 먼저 준비하고 Location Store의 reference CAS가 visibility와 aggregate commit을 결정한다. 같은 Redis deployment를 공유할 수 있지만 cross-store transaction을 요구하지 않음 |
| Core raw 문서 | v11 target과 Core formal 중복 / Core formal 단일 정본 | Core formal 단일 정본. v11의 09 파일은 위치만 가리킴 |
| Actor reference | Actor type과 owner fence를 reference에 포함 / Actor ID·node RID·object generation만 공개 | 후자. Type은 factory·authority가 검증하고 owner generation·lease는 runtime이 숨김 |
| Actor 생성 | Caller가 target node를 선택 / global Actor ID·stable type과 optional initial Mesh intent | 후자. Runtime이 compatible target을 선택하고 manager `Find`는 existing-only 조회만 제공하며 caller에게 physical owner 선택을 노출하지 않음 |
| Host owner lease | Component마다 lease를 claim / host lifecycle lease 하나와 component별 routing slot | 후자. 한 process의 liveness token을 중복하지 않고 각 component RID 수명은 slot으로 분리 |
| Owner lease margin | Routing ID allocation 전용 margin / 모든 owner authority에 적용하는 owner lease margin | 후자. `OwnerLeaseFencingMargin` 하나를 owner lease를 사용하는 모든 host에서 검증하고 stale owner의 local monotonic admission deadline을 같은 의미로 계산함 |
| Store failure grace | Owner lease까지 연장 / discovery reconcile과 신규 outbound connect만 유예 | 후자. 마지막 stable desired set과 기존 transport만 유지하고 stateful owner·coordinator deadline은 연장하지 않음 |
| Authority generation | Runtime별·key별 counter와 tombstone / provider-domain global counter와 opaque Store version | 후자. Object·owner·Store revision을 CAS 하나에서 발급하고 key별 영구 metadata를 남기지 않음 |
| Authority generation 이름 | `OwnerGeneration` 하나로 표현 / object·authority owner·host lease generation을 이름으로 구분 | 후자. `ObjectGeneration`, `AuthorityOwnerGeneration`, `LeaseGeneration`을 분리해 다른 수명의 fence를 호출자가 혼동하지 않게 함 |
| Owner cleanup | Owner ID string으로 bulk delete / exact owner token으로 ephemeral descriptor만 정리 | 후자. 같은 owner ID의 새 lease와 durable authority를 오래된 cleanup이 삭제하지 못함 |
| Store enumeration | Provider 전체 snapshot / bounded page와 stable scope change stamp | 후자. Caller에게 paging 재시도 상태를 노출하지 않고 runtime이 안정된 full snapshot에서만 diff를 적용함. Owner lease 전체 list는 제거하고 exact read를 사용 |
| Authority scan cursor | Public scan ID·watermark·cursor 조합 / provider-issued opaque cursor 하나 | 후자. Framework는 4,096-byte 이하 token을 해석·조합하지 않고 다음 page에 그대로 돌려주며 provider가 snapshot lease와 watermark를 소유함 |
| Provider cancellation | Cancellation을 no-commit으로 해석 / invocation 뒤 exact read·fence로 reconcile | 후자. Waiter 종료와 durable commit을 분리해 duplicate generation transition과 transfer loss를 막음 |
| Provider byte ownership | Mutable buffer lifetime을 호출자마다 추측 / operation 단위 immutable input과 stable result | 후자. Framework는 input을 async 완료까지 유지하고 provider는 retain·mutable pool 경계에서 copy해 application에 buffer lifetime을 노출하지 않음 |
| Generation exhaustion | Provider exception으로 불명확하게 보고 / authority write의 closed terminal result | 후자. `GenerationExhausted`가 row·index·counter를 바꾸지 않았음을 공통으로 보장하고 transport error와 분리함 |
| Typed JSON interop | 언어 serializer 기본값과 byte-identical 재인코딩 / `framework-json-v1` 의미 profile과 opaque application bytes | 후자. DTO 의미만 공통으로 고정하고 canonical bytes는 Framework manifest·envelope에만 요구 |
| Message size | 숨은 고정 16 MiB / connection startup에서 negotiated complete-message bound | 후자. 양쪽 상한의 작은 값을 allocation 전에 적용하고 live setter를 만들지 않음 |
| Transfer 크기 | Whole payload 64 MiB / 64 MiB immutable chunk와 root manifest | 후자. Public transfer option을 늘리지 않고 bounded streaming replay와 256 GiB logical ceiling을 runtime이 소유 |
| Recovery enumeration | Redis `SCAN` 또는 전체 copy / leased snapshot watermark와 bounded MVCC page | 후자. Startup Serving gate와 concurrent create·delete 의미를 provider 안에 감춤 |
| Transfer identity | Replacement마다 transfer transaction을 복제 / stable transfer ID와 target attempt generation 분리 | 후자. Immutable transfer·journal·terminal key는 유지하고 reservation attempt만 fence함 |
| Transfer 공개 경계 | `Activated`에서 target ingress 공개 / source cleanup과 `Completed` 뒤 steady normalize·Ready | 후자. Replacement transfer에 없는 post-activation work를 만들지 않아 data loss window를 제거 |
| Reply durability | One-way reply relay 또는 별도 장기 tombstone / internal relay ACK를 completion barrier에 포함 | 후자. Public 상태를 추가하지 않고 authenticated ACK 또는 accepted record의 exact request-source lease expiry 전 transfer를 해제하지 않음 |
| Journal source authority | 모든 accepted work에 Store lease 강제 / lease-backed accepted work만 durable capture | 후자. Manual connection-bound request와 one-way send는 모두 `Captured` 전에 terminal 완료하고 실패하면 reversible abort하므로 manual topology에 Location Store를 강제하지 않음 |
| Session-to-Actor relay | Ambient handler context 또는 별도 request family / explicit current dispatch context의 common relay | 후자. Reply capability를 한 번 runtime에 넘기고 correlation·retry를 caller가 조합하지 않음 |
| Descriptor registration | 언어별 truncate·split / 공통 count·1 MiB 상한의 startup atomic validation | 후자. Partial descriptor를 게시하지 않고 모든 언어가 같은 configuration failure를 반환 |

업무 개념을 소유하는 경계도 다음처럼 고정한다. 이 분리는 하나의 상태 기계를 두 runtime에
중복하거나 raw transport 정보를 application 계약으로 노출하지 않게 한다.

| DDD 경계 | 소유하는 개념과 불변 조건 | 다른 경계에 노출하지 않는 정보 |
|---|---|---|
| Core raw transport | socket, endpoint, routing ID, message ownership, transport monitor | MeshName, ChannelName, Spot·Actor identity, owner lease, maintenance phase |
| Framework topology·messaging | MeshNode, role별 Channel, peer registry, outbound admission | Core engine·pipe, Store provider key, application handler state |
| Stateful object runtime | Spot turn, Actor membership, Instance activation, session binding | Physical socket, Redis command, application이 조합하는 activation·transfer phase |
| Authority·maintenance | owner CAS, lease fence, transfer reference, Retire·Shutdown terminal state | Provider별 transaction 구현, handler DI, raw transport reconnect 정책 |
| Hosting·application | DI scope, factory, typed handler, remote operation endpoint의 인증·인가 | Store CAS token 해석, wire command, Core handle·errno |

경계 사이 변환은 각 runtime의 internal adapter가 담당한다. Core error를 Framework typed result로
변환하는 규칙, logical address를 owner route로 해석하는 규칙과 provider별 CAS 구현을 public
handler에 전달하지 않는다.

선택한 표면이 구현 과정에서 pass-through method, 두 번째 state machine 또는 언어 하나만의 public 동작을
요구하면 `SPEC-06`으로 되돌려 대안을 다시 검토한다.

## 6. M2 — 10.x oracle 봉인과 제거 준비

M2는 비교 기준과 제거 준비만 고정한다. Framework service 기능을 구현하지 않는다. 10.x oracle는 읽기 전용
artifact를 별도 process에서 실행하고 versioned normalized trace만 출력한다. 새 candidate process에는 oracle
library 경로를 주입하지 않으며 build·link manifest와 실제 load 목록에서 oracle payload가 0건인지 확인한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M2-ORACLE` | Core 10.x oracle baseline과 normalized trace 봉인 | baseline·E2E lane, `P-DELIVERY` | `V11-R1` | 완료 | frozen artifact provenance, 별도 process protocol, 대표 정상·오류 trace와 archive 제외 규칙 통과 | SHA로 고정한 Core 10 executable을 child process로 실행해 `M17`·`M12` normalized trace를 생성했다. `ORACLE` self-test·`DOC`·`DIFF-OWNED` exit 0. 증거: `.artifacts/v11/evidence/V11-M2-ORACLE/result.json` |
| `V11-M2-CORE-READINESS` | Core service·ZMP heartbeat 제거 manifest와 raw 보존 경계 확인 | Core·inventory lanes, `P-SCAN` | `V11-R1` | 완료 | service와 heartbeat option·frame·timer·test 분류 100%, generic timer·monitor disposition 미분류 0 | Core와 shared perf 815건을 분류했고 ZMP heartbeat 22건, generic timer 8건, raw monitor 4건과 Core 10.x perf archive 18건의 disposition을 검증했다. PGM 관련 record와 미분류 record는 0건이며 `INV`·`DOC`·readiness self-test·`DIFF-OWNED`가 통과한다. 증거: `.artifacts/v11/evidence/V11-M2-CORE-READINESS/result.json` |
| `V11-M2-RAW-CPP` | C++ binding raw capability 확인 | C++ binding lane, `P-DELIVERY` | `V11-R1` | 완료 | 현재 설치 package에서 multipart·monitor·STREAM·ready·shutdown capability와 보완 목록 확정 | public header의 다섯 raw capability와 targeted binding build·contract test가 통과했고 `ROW-GATE` exit 0이다. 전체 service runner의 ABI drift는 `V11-M4-BIND-CPP` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-CPP/result.json` |
| `V11-M2-RAW-DN` | .NET binding raw capability 확인 | .NET binding lane, `P-DELIVERY` | `V11-R1` | 완료 | public assembly만 사용해 raw capability와 보완 목록 확정, reflection·internal P/Invoke 접근 0 | public assembly의 다섯 raw capability와 targeted binding test가 통과했고 reflection·internal P/Invoke 접근 0, `ROW-GATE` exit 0이다. service sample namespace 충돌은 `V11-M4-BIND-DN` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-DN/result.json` |
| `V11-M2-RAW-JVM` | Java binding raw capability 확인 | JVM binding lane, `P-DELIVERY` | `V11-R1` | 완료 | public package만 사용해 raw capability와 보완 목록 확정, package-private·JNI 직접 접근 0 | public jar의 다섯 raw capability와 targeted binding test가 통과했고 package-private·JNI 직접 접근 0, `ROW-GATE` exit 0이다. service sample lifecycle 문제는 `V11-M4-BIND-JVM`·`V11-M7-SAMPLES` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-JVM/result.json` |
| `V11-M2-RAW-NODE` | Node binding raw capability 확인 | Node binding lane, `P-DELIVERY` | `V11-R1` | 완료 | package export·`.d.ts`로 raw capability와 보완 목록 확정, addon 내부 접근 0 | public root export·`.d.ts`의 다섯 raw capability, addon 내부 접근 0, raw test 18/18과 `ROW-GATE`가 통과했다. legacy service test의 internal module 의존은 `V11-M4-BIND-NODE` 이슈로 분리했다. 증거: `.artifacts/v11/evidence/V11-M2-RAW-NODE/result.json` |
| `V11-M2-BIND-READINESS` | 네 bindings projection·package와 10.x 전용 binding 격리 확인 | binding·package inventory lane, `P-SCAN` | `V11-M2-RAW-CPP`, `V11-M2-RAW-DN`, `V11-M2-RAW-JVM`, `V11-M2-RAW-NODE` | 완료 | 제거·보완·보존 파일 미분류 0, C·Python·Go·Rust service·heartbeat projection은 10.x `Retain/OutOfScopeV11`로 격리되고 Core 11 입력·호환 metadata에서 제외됨 | binding·legacy 8개 언어의 분류에서 raw capability 누락·legacy disposition 불일치·compatibility claim이 모두 0이며 `INV`·self-test·`ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M2-BIND-READINESS/result.json` |
| `V11-M2-READY` | Removal-first 착수 gate | coordinator, `P-DEEP` | `V11-M2-ORACLE`, `V11-M2-CORE-READINESS`, `V11-M2-BIND-READINESS` | 완료 | 새 candidate의 oracle compile·link·load 0, Framework 기능 변경 0, Core 제거 입력 완결 | canonical schema 3종과 `ROW-GATE`·`REMOVE`를 구현했다. self-test 2종, 선행 M2 artifact 7개의 개별 `ROW-GATE`, `DOC`·`INV`·`WIRE`·`DIFF-OWNED`와 READY 자체 `ROW-GATE`가 모두 exit 0이다. 실제 source·package no-hit은 M3·M4 post-removal gate가 판정한다. 증거: `.artifacts/v11/evidence/V11-M2-READY/result.json` |

Raw capability가 부족하면 M4에서 일반 raw socket 사용자에게도 유효한 public API로 보완한다. Framework 전용
helper, private header, JNI·N-API 직접 호출과 설치되지 않는 symbol은 추가하지 않는다. 10.x oracle와의 대조는
`verified-baseline` 항목에만 적용하고 `v11-first-implementation` 기능의 완료 근거로 사용하지 않는다.

## 7. M3 — Core 11 service 제거, cleanup과 package

M3는 Core service header·export·source·test·active perf와 ZMP heartbeat option·frame·engine timer를 먼저
제거한다. Core에는 raw socket·transport·poller, generic timer와 monitor만 남긴다. 제거 뒤 POSD·DDD와 사용되지
않는 코드 review를 통과하기 전에는 Core 11 version metadata를 확정하거나 internal package를 배포하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M3-PERF-LEGACY` | Core service active perf 제거와 10.x archive 격리 | perf lane, `P-SCAN` | `V11-M2-READY` | 완료 | Spot source·target·runner pattern 0, raw perf 유지, oracle archive가 active baseline으로 선택되지 않음 | active Spot source 3개와 paired gate·test 2개를 제거했다. raw perf unit 37/37, 대표 raw target build, archive isolation, `REMOVE --scope common`, `DOC`, `DIFF-OWNED`, `ROW-GATE`가 모두 exit 0이다. 증거: `.artifacts/v11/evidence/V11-M3-PERF-LEGACY/result.json` |
| `V11-M3-CORE-REMOVE` | Core service와 ZMP heartbeat header·export·source·test·build 제거 | Core lane, `P-DEEP` | `V11-M3-PERF-LEGACY` | 완료 | service surface 0, heartbeat public·internal option·frame codec·engine timer·test 0, generic timer·monitor 유지 | service public header 7개와 Mesh API·runtime source, service test·build 입력을 제거했다. 독립 audit의 Spot·ChannelName state, service-named seam, stale build/test 입력과 `REMOVE` field 오탐 4건을 모두 수정했다. full build 100%, raw targeted 45/45, `REMOVE --scope core` 722 records·violations 0, post-fix audit clean, `DIFF-CHECK`와 `ROW-GATE`가 모두 exit 0이다. optional full lane의 비소유 환경 실패는 result의 issues에 분리했다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-REMOVE/result.json` |
| `V11-M3-CORE-CLEAN` | Core POSD·DDD와 unused code, Core package tooling 정리 | Core cleanup lane, `P-DEEP` | `V11-M3-CORE-REMOVE` | 완료 | service aggregate·compat facade·pass-through·끊긴 build 입력 0, service header copy 0, Core package·clean C consumer tooling self-test 통과 | stale internal service seam과 service-only guide 6개를 제거하고 Core 문서 39개를 raw-only로 고쳤다. Package tooling은 service header 재복사를 차단하고 네 version 지점과 clean C consumer를 검증한다. Canonical clean build, unittest 18/18, integration 42/42, regression 20/20, `CORE-PKG-TEST`, `REMOVE` 722 records·violations 0, `DOC`, `DIFF-OWNED`가 통과했다. `ROW-GATE`가 1MB 초과 base inventory에서 Node 기본 buffer로 종료되는 결함도 128MB 명시 상한으로 수정하고 self-test·candidate 182 files 검증을 통과했다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-CLEAN/result.json` |
| `V11-M3-CORE-VERIFY` | Core raw 회귀·sanitizer, package tooling와 oracle 격리 재검증 | Core test·inventory coordinator, `P-DELIVERY` | `V11-M3-CORE-CLEAN` | 완료 | clean build, full raw suite·sanitizer·package tooling self-test 통과, inventory reconcile, ZMP heartbeat 잔여와 oracle link·load 0 | 첫 transport 전 ID 0 queue와 reconnect 이후 nonzero stale 차단을 분리해 reconnect/connect 회귀를 수정했고 weighted reactivation fixture는 표준 pipe termination protocol로 정리했다. Core 11 clean build에서 unittest 18/18, integration 42/42, regression 20/20, exact 11.0.0 ASAN 80/80·sanitizer finding 0과 SONAME 11을 확인했다. Package tooling은 base revision과 candidate record에서 격리 build snapshot을 materialize하며 hash 변이, stale build, version·SONAME 변이를 거부한다. C++ consumer가 찾은 OpenSSL transitive dependency 선언을 포함해 final candidate 204 files·Core changed path 193개를 봉인한다. 병렬 binding delta의 inventory reconcile은 `V11-M4-BIND-JOIN`에 인계한다. 증거: `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/result.json` |
| `V11-R2` | Core 제거·POSD·DDD 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M3-CORE-VERIFY` | 완료 | I1·I2·I3, raw 보존과 제거 범위 review clean | 첫 C++ installed-package configure가 exported OpenSSL dependency 선언 누락을 찾아 Core config에 conditional `find_dependency(OpenSSL)`을 추가했다. 최종 SHA `06d72dab…`에 대해 두 reviewer가 blocking finding 0·clean/approved를 기록했고 package audit은 isolated build·C consumer와 외부 CMake `find_package(zlink)`·OpenSSL target·link·run을 통과했다. R2 `ROW-GATE` 3 files·5 commands 통과. 증거: `.artifacts/v11/evidence/V11-R2/result.json` |
| `V11-M3-CORE-PKG` | Core 11 version와 local/internal package 실행 | Core package lane, `P-DELIVERY` | `V11-R2` | 완료 | review한 source·tooling revision을 수정하지 않고 11.0.0 install artifact 생성, clean C consumer·provenance 통과, 외부 배포 0 | R2 승인 SHA `06d72dab…`를 isolated Release build해 기존 local prefix를 교체했다. Provenance 18 files, clean C consumer, runtime 11.0.0·SONAME 11, service header 0과 transitive OpenSSL CMake consumer가 통과했다. Provenance SHA-256 `7146fc20…`, runtime SHA-256 `6e950f27…`, 외부 배포 0. 증거: `.artifacts/v11/evidence/V11-M3-CORE-PKG/result.json` |

Core review 뒤 source가 바뀌면 package를 만들기 전에 영향받는 raw test와 review를 다시 실행한다. 제거한 service
symbol을 fake export나 빈 구현으로 남겨 bindings build를 통과시키지 않는다.

## 8. M4 — Bindings service projection 제거, cleanup과 package

네 bindings lane은 review를 통과한 Core 11 package를 공통 입력으로 사용한다. Service projection을 제거한 뒤
M2에서 확인한 raw capability를 보완하고 POSD·DDD와 사용되지 않는 wrapper·generated code를 정리한다. 각 언어
package는 통합 bindings review가 끝난 뒤에만 local/internal 위치에 배포한다.

M4의 test 범위는 raw binding contract와 clean package consumer로 제한한다. 기존 Framework public
sample·E2E source와 실행 registration은 Git 기준으로 보존하고 이 stage에서는 실행하지 않는다. Binding test
runner가 sample이나 Framework E2E를 함께 실행하면 raw-only subset을 별도 command로 선택하되, 이를 이유로
기존 runner registration을 삭제하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M4-BIND-CPP` | C++ projection 제거·raw 보완·cleanup | C++ binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated symbol 0, public raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Raw contract 10/10, final removal 6 records·violations 0과 actual external CMake consumer가 통과했다. Package gate는 승인된 Core provenance·candidate, version 11.0.0과 SONAME 11을 강제한다. Sample/E2E 변경·실행은 0이며 final candidate 121 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-CPP/ledger-result.json` |
| `V11-M4-BIND-DN` | .NET projection 제거·raw 보완·cleanup | .NET binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated P/Invoke 0, public raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Exact Core 11.0.0으로 127/127, final removal 7 records·violations 0과 isolated NuGet consumer가 통과했다. 기존 local native payload를 output으로 복사하는 item과 ambient fallback, 사용처 없는 service snapshot helper를 제거했고 실제 loaded runtime SHA·SONAME을 검증한다. Sample/E2E 변경·실행은 0이며 final candidate 101 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-DN/ledger-result.json` |
| `V11-M4-BIND-JVM` | Java projection 제거·raw 보완·cleanup | JVM binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat member·generated JNI 0, `build.gradle`의 Core source·Boost include 0, installed Core package-only raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Raw tests 64/64, final removal 9 records·violations 0과 isolated Maven consumer가 통과했다. 승인된 Core candidate·runtime SHA·SONAME을 POM·module metadata와 JAR provenance에서 검증한다. Sample runner는 Git 기준으로 복구했고 sample/E2E source diff·실행은 0이다. Final candidate 186 files의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-JVM/ledger-result.json` |
| `V11-M4-BIND-NODE` | Node projection 제거·raw 보완·cleanup | Node binding lane, `P-DEEP` | `V11-M3-CORE-PKG` | 완료 | service·heartbeat export·generated addon projection 0, public ESM·CJS export와 raw proof·clean build·test, package metadata·consumer tooling self-test 통과 | Registration을 유지한 raw-only selector로 build·typecheck·native rebuild와 raw test 8 files·failure 0·sample execution 0을 다시 기록했다. Linux resolver를 SONAME 11로 고정했고 package build 뒤 generated `prebuilds`·provenance가 source에 남지 않도록 정리했다. Final removal 17 records·violations 0, exact Core provenance gate와 ESM·CJS·type consumer tooling이 통과했으며 candidate `9cbfaee1…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-NODE/ledger-result.json` |
| `V11-M4-BIND-JOIN` | Bindings 제거·raw proof 합류 | binding coordinator, `P-DELIVERY` | `V11-M4-BIND-CPP`, `V11-M4-BIND-DN`, `V11-M4-BIND-JVM`, `V11-M4-BIND-NODE` | 완료 | 네 언어 service projection과 ZMP heartbeat option projection 0, public raw capability 결과 일치 | Final inventory 1,826 records·package tooling 38 paths와 joined removal C++ 6, .NET 7, JVM 9, Node 17 records·violations 0을 확인했다. 네 child candidate를 final inventory와 actual package consumer evidence로 다시 봉인했고 join `ROW-GATE`가 통과했다. Immutable sample/E2E는 M7 owner로 유지한다. 증거: `.artifacts/v11/evidence/V11-M4-BIND-JOIN/result.json` |
| `V11-R3` | Bindings 제거·POSD·DDD 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M4-BIND-JOIN` | 완료 | public raw 경계, generated output, package input과 제거 범위 review clean | 두 독립 reviewer와 package audit가 C++ `b765413a…`, .NET `70aca597…`, JVM `00e3248f…`, Node `9cbfaee1…`, join `a3b8a1a0…`를 승인했다. Actual CMake·NuGet·Maven consumer, Node package consumer tooling, Core provenance·candidate·runtime SHA·SONAME 11, sample/E2E diff 0과 row freshness를 확인했고 blocking finding은 0이다. R3 candidate `8ce37cf7…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R3/result.json` |
| `V11-M4-PKG-CPP` | C++ binding 11 local/internal package | C++ package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision package와 clean CMake consumer 통과, 외부 배포 0 | R3 승인 source를 CMake prefix에 다시 설치하고 exact package configure·compile·link·load·run을 확인했다. Package tree SHA-256은 `a37a40c7…`, 외부 배포는 0이며 `ROW-GATE` 1 file·4 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-CPP/result.json` |
| `V11-M4-PKG-DN` | .NET binding 11 local/internal package | .NET package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision NuGet과 public-only clean consumer 통과, 외부 배포 0 | `Systems.Zlink.11.0.0.nupkg` SHA-256 `727ba451…`과 isolated public-only consumer restore·build·run, loaded Core 11 SHA·SONAME을 확인했다. 외부 배포는 0이며 `ROW-GATE` 1/4가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-DN/result.json` |
| `V11-M4-PKG-JVM` | Java binding 11 local/internal package | JVM package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision Maven artifact·module metadata·clean consumer 통과, 외부 배포 0 | Maven `systems.zlink:zlink:11.0.0`을 생성해 clean Gradle consumer로 resolve·compile·run했다. JAR SHA-256은 `7c2b21cc…`이며 POM·module metadata와 Core provenance가 일치한다. 외부 배포 0, `ROW-GATE` 1/4가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-JVM/result.json` |
| `V11-M4-PKG-NODE` | Node binding 11 local/internal package | Node package lane, `P-DELIVERY` | `V11-R3` | 완료 | review revision tgz·ESM·CJS·`.d.ts` clean consumer 통과, 외부 배포 0 | `zlink-systems-zlink-11.0.0.tgz` SHA-256 `81e1ce89…`를 clean npm consumer에 설치해 CJS·ESM import와 type export를 확인했다. 외부 배포 0, `ROW-GATE` 1 file·6 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-PKG-NODE/result.json` |
| `V11-M4-CONSUMER-JOIN` | 새 bindings package 합류 | package coordinator, `P-SCAN` | `V11-M4-PKG-CPP`, `V11-M4-PKG-DN`, `V11-M4-PKG-JVM`, `V11-M4-PKG-NODE` | 완료 | 중앙 Framework 참조 version 고정, clean consumer provenance와 Core payload 일치 | C++·.NET·JVM·Node 중앙 binding version을 11.0.0으로 고정했고 Node workspace manifest와 lockfile도 같은 tgz를 해석한다. CMake tree `a37a40c7…`, NuGet `727ba451…`, Maven JAR `7c2b21cc…`, npm tgz `81e1ce89…`와 Core payload provenance를 resolution manifest에서 확인했다. Candidate `02b85a71…`의 `ROW-GATE` 8 files·8 commands가 통과했다. 증거: `.artifacts/v11/evidence/V11-M4-CONSUMER-JOIN/result.json` |

2026-07-23 Core cleanup 이후 package refresh는 기존 11.0.0 version을 유지한 채 다시 실행했다. Core raw suite와
ASAN은 각각 80/80을 통과했고 Core provenance SHA-256은 `c0feeac0…`, runtime SHA-256은 `871e5306…`이다.
이 Core를 사용해 C++·.NET·JVM·Node local package와 isolated consumer를 다시 검증했다. 중앙 Framework version
지점은 변경하지 않았다. 증거:
`.artifacts/v11/evidence/V11-M3-CORE-PKG/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-BIND-CPP/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-BIND-DN/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-PKG-JVM/refresh-20260723.json`,
`.artifacts/v11/evidence/V11-M4-PKG-NODE/refresh-20260723.json`.

## 9. M5 — Private binding-facing port와 runtime foundation

M5는 Framework가 사용하던 Core service adapter를 제거하고, 각 언어 Framework 내부에 private
binding-facing port를 먼저 구현한다. 이 port는 binding API를 그대로 전달하는 facade가 아니라 service
runtime이 필요한 raw transport 동작과 binding type 변환을 내부에 감춘다. 해당 언어 binding의 설치된 public
raw API만 호출하며 Framework public interface, binding public service API와 공통 private C SPI를 추가하지 않는다.

구현은 기존 Framework public interface에서 안쪽으로 진행한다. M5는 기존 public FQN의 value type 소유권과
private port seam을 compile 가능한 상태로 고정하고, raw transport·codec·operation·resource 동작을 internal
contract로 확인한다. 기존 service owner·bridge의 실제 실행 경로를 새 port에 연결하고 제거하는 작업은 M6
vertical slice가 기능 단위로 수행하며 `V11-M6-SCAFFOLD-ZERO`에서 잔여 0을 확인한다. 이 단계에서 기존
Framework public class를 새 abstraction으로 일괄 교체하거나 호출자 코드를 새 runtime 형태에 맞춰 변경하지
않는다. 네 언어에서 동작이 복구된 뒤 중복이 확인될 때만 같은 언어 내부에서 refactoring한다.

각 M5·M6 runtime row는 구현 전에 관련 Framework 정식 spec, 해당 언어 exact interface와 공통 internals를
읽는다. Migration machine inventory가 가리키는 보존된 Core service 구현·test snapshot에서
component 분리, state machine, algorithm, ordering, ownership, timeout·shutdown·recovery failure case를 확인하고
row evidence에 참고한 snapshot revision과 path를 기록한다. 이 구현을 언어별 runtime 구조로 포팅하되 제거한
Core service symbol에 다시 의존하지 않는다. Spec 또는 internals와 다른 부분만 목표 문서를 적용하고 차이를
evidence에 남긴다.

`V11-M5-SCAFFOLD-*`는 기존 ID를 유지하지만 production scaffold를 만드는 작업이 아니다. 이 row는 private
port seam과 기존 public Framework value type의 compile 연결, 새 port 내부의 제거한 Core adapter 참조 0과
production `RuntimeNotReady`·fake data 0을 검증한다. 기존 service owner 실행 경로 전체의 제거 여부는
`V11-M6-SCAFFOLD-ZERO`가 검증한다. M5는 protocol fixture와 internal contract만 실행한다.
sample·E2E는 source·registration을 보존한 채 `pending`으로 유지한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M5-PROTOCOL` | Protocol 생성 상수·codec·golden foundation | protocol lane, `P-DEEP` | `V11-M4-CONSUMER-JOIN` | 완료 | schema의 livenessProbe `5`·livenessAck `6` 포함 생성물, probe ID echo·canonical frame·malformed fixture와 네 decoder 입력 확정 | Schema에서 C++·.NET·JVM·Node.js command·flag 상수를 생성하는 deterministic generator와 drift check를 추가했다. Liveness probe/ack canonical frame 2개는 같은 nonzero probe ID를 사용하고 wrong magic·unknown command·forbidden flag·zero/truncated ID·trailing byte 6개를 decoder 입력으로 고정했다. Schema self-test 124건, canonical 2건·malformed 6건과 probe ID echo, candidate `43ddd29f…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-PROTOCOL/result.json` |
| `V11-M5-SCAFFOLD-CPP` | C++ private binding-facing port와 기존 public API 연결 | C++ lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public raw binding-only port, 제거 projection 참조·production placeholder·fake data 0, internal compile contract 통과 | Public zlink_cpp 11 ROUTER API만 사용하는 private raw port와 isolated compile contract를 구현하고 shipped public FQN `zlink::spot_kind`를 Framework가 소유하게 했다. 새 M5 owned path의 금지 참조와 sample/E2E diff는 0이며 candidate `84bf351a…`의 `ROW-GATE`가 통과했다. 기존 service owner 6개의 include 7건은 placeholder로 우회하지 않고 M6 입력으로 남겼다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-CPP/result.json` |
| `V11-M5-SCAFFOLD-DN` | .NET private binding-facing port와 기존 public API 연결 | .NET lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public binding-only port, reflection·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public FQN의 value type은 Framework assembly로 이관하고 public `IContext`·`IRouterSocket`만 사용하는 private raw ROUTER port를 production owner와 분리해 구현했다. 실제 inproc multipart와 request/reply correlation, exception path resource cleanup, 금지 참조 0과 sample/E2E diff 0을 확인했으며 candidate `9c4c7201…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-DN/result.json` |
| `V11-M5-SCAFFOLD-JVM` | JVM private binding-facing port와 Java·Kotlin public API 연결 | JVM lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public Java binding-only port, JNI 우회·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public FQN 66개를 Framework artifact가 소유하고 public binding raw ROUTER API만 사용하는 private port를 production owner와 분리했다. Production fake-success 구현은 모두 제거했으며 isolated Gradle task 11/11, 새 owned path 금지 참조와 sample/E2E diff 0, candidate `709854af…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-JVM/result.json` |
| `V11-M5-SCAFFOLD-NODE` | Node private binding-facing port와 기존 public API 연결 | Node lane, `P-DELIVERY` | `V11-M4-CONSUMER-JOIN` | 완료 | public package-only port, private addon 우회·제거 projection·production placeholder·fake data 0, internal compile contract 통과 | 보존된 public export의 DTO·enum·value type은 Framework package로 이관하고 public raw Router port를 production owner와 분리했다. Fake mesh foundation은 제거했고 isolated scaffold compile, 새 owned path 금지 참조와 sample/E2E diff 0, candidate `9559af6e…`의 `ROW-GATE`가 통과했다. 기존 owner의 제거 API compile gap은 M6 입력이다. 증거: `.artifacts/v11/evidence/V11-M5-SCAFFOLD-NODE/result.json` |
| `V11-M5-FOUND-CPP` | C++ transport·codec·operation foundation | C++ lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-CPP` | 완료 | public binding-only gateway, codec·completion·clock·resource unit proof 통과 | Generated constants 기반 strict codec, bounded terminal-once registry, destructor shutdown과 callback exception isolation을 구현했다. Raw port·codec·operation test 3/3과 candidate `54d3b5ec…`의 `ROW-GATE`가 통과했다. Full owner build와 full removal은 M6가 소유한다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-CPP/result.json` |
| `V11-M5-FOUND-DN` | .NET transport·codec·operation foundation | .NET lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-DN` | 완료 | public binding-only gateway, codec·Task completion·clock·resource unit proof 통과 | Generated constants 기반 codec, terminal-once completion, injectable clock·fence와 reverse resource cleanup을 구현했다. `FailAll`이 실제 terminal result를 보존하는 회귀와 raw roundtrip을 isolated executable로 검증했고 candidate `489aad47…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-DN/result.json` |
| `V11-M5-FOUND-JVM` | JVM transport·codec·operation foundation | JVM lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-JVM` | 완료 | Java binding-only gateway, codec·executor·coroutine·resource unit proof 통과 | Authoritative single-frame codec이 shared canonical 2건·malformed 6건을 직접 decode하고 terminal-once registry, bounded mailbox와 raw resource ownership을 검증한다. Production compile과 분리된 Gradle task 11/11과 candidate `3915812f…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-JVM/result.json` |
| `V11-M5-FOUND-NODE` | Node transport·codec·operation foundation | Node lane, `P-DEEP` | `V11-M5-PROTOCOL`, `V11-M5-SCAFFOLD-NODE` | 완료 | public package-only gateway, codec·Promise·event-loop resource unit proof 통과 | Generated definition codec, terminal-once Promise·injectable clock, infrastructure/application event-loop queue와 reverse cleanup을 구현했다. Shared fixture를 포함한 isolated test 4/4와 candidate `d6fed872…`의 `ROW-GATE`가 통과했고 production fake mesh runtime은 남기지 않았다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-NODE/result.json` |
| `V11-M5-FOUND-JOIN` | Foundation 합류와 pending E2E 감사 | foundation coordinator, `P-DELIVERY` | `V11-M5-FOUND-CPP`, `V11-M5-FOUND-DN`, `V11-M5-FOUND-JVM`, `V11-M5-FOUND-NODE` | 완료 | 네 codec golden·negative fixture 일치, required E2E `executed=0`, `skipped=0`, `pending=required` | 네 foundation의 generated protocol·decoder fixture가 일치하고 inventory 1,908건과 새 M5 owned path 금지 참조 0을 확인했다. Required scenario 112건은 `pending=112`, `executed=0`, `skipped=0`이며 sample/E2E source·registration diff는 0이다. Candidate `052b93c6…`의 `ROW-GATE`가 통과했다. 기존 production owner의 full removal은 `V11-M6-SCAFFOLD-ZERO` 입력으로 기록했다. 증거: `.artifacts/v11/evidence/V11-M5-FOUND-JOIN/result.json` |
| `V11-R4` | Runtime foundation 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M5-FOUND-JOIN` | 완료 | schema, public binding 경계, production placeholder 0과 resource ownership review clean | C++/.NET과 JVM/Node·protocol을 나눈 독립 review에서 발견한 no-op·synthetic success·wire frame·terminal cleanup 문제를 모두 제거하고 재검토했다. Blocking finding 0, sample/E2E diff 0이며 candidate `3a7f6399…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R4/result.json`, `.artifacts/v11/evidence/V11-R4-M5/` |

### 9.1 M5 이후 public contract amendment와 실행 격리

이 stage는 M5 완료 뒤 추가된 global identity와 remote placement 변경을 M6 입력으로 확정한다. 두 변경 제안은
설계 입력일 뿐이며, 공개 계약은 Framework 공통·server 정식 spec과 다섯 언어 exact interface에 먼저 반영한다.
Protocol과 E2E·sample 영향 목록도 같은 candidate에서 확정하고 독립 review를 통과한다. 이 stage에서는
Framework runtime source를 구현하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-CA-DECISION` | Global identity·remote placement 공개 경계 결정 | architecture·contract lane, `P-DEEP` | `V11-R4` | 완료 | 두 제안과 Store amendment의 open item 미결정 0, 대안·POSD 판단·정식 owner 확정 | `CA-D01~CA-D36`으로 global identity, placement, generic reservation과 Location·Relocation Store 분리 결정을 고정했다. Location canonical participant authority와 Relocation lookup manifest, write-before-CAS publication, 별도 Redis class, `RelocationDataLost=34`까지 미결정 0이다. |
| `V11-CA-SPEC` | Framework 공통 spec·common internals 통합 | contract·internals lanes, `P-DEEP` | `V11-CA-DECISION` | 완료 | identity·remote create·placement·reservation·handover·failure 의미와 runtime owner 누락 0, 임시 target 문서·link 0 | 43개 결정을 공통 Framework spec과 common internals의 정식 owner에 반영하고 `42-relocation-store-redis.ko.md`를 추가했다. `target-spec`, `target-internals`는 mapping을 inventory·trace에 반영한 뒤 삭제했다. |
| `V11-CA-IFACE-CPP` | C++ exact interface amendment | C++ contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | C++ public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 `uint32_t` CRC32C, 별도 Store·Redis class, inventory digest, `relocation_data_lost=34`와 publication 순서를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-IFACE-DN` | .NET exact interface amendment | .NET contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | .NET public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 `uint` CRC32C, `IZLinkRelocationStore`, 별도 Redis class, inventory digest와 `RelocationDataLost=34`를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-IFACE-JVM` | Java·Kotlin exact interface amendment | JVM contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | Java·Kotlin public signature·example·trace가 amended spec과 일치 | Java Put 결과의 `long` CRC32C 범위를 맞췄다. Kotlin Location·Relocation Store의 Java `CompletionStage` method는 protected suspend hook과 연결하며 Java·Kotlin 모두 `Relocation` 공개 이름만 제공한다. |
| `V11-CA-IFACE-NODE` | Node.js exact interface amendment | Node contract lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | Node.js public signature·example·trace가 amended spec과 일치 | Relocation Put 결과의 bounded numeric CRC32C, 별도 Store·Redis class, inventory digest와 `RelocationDataLost=34`를 고정했다. `Transfer*` public alias는 제공하지 않는다. |
| `V11-CA-PROTOCOL` | Placement·identity protocol과 fixture amendment | protocol lane, `P-DEEP` | `V11-CA-DECISION` | 완료 | schema·generated constant·golden·negative fixture와 formal 의미 drift 0 | `RelocationDataLost`는 wire 35 → public 34로 고정하고 wire 23..34를 예약했다. Permanent payload missing, checksum mismatch와 inventory digest mismatch를 닫힌 terminal trigger 3개로 고정했으며 WIRE 37 commands·151 types·159 negative self-tests, generated 35 fixtures와 decoder 24 error·3 malformed가 통과했다. |
| `V11-CA-IMPACT` | E2E·sample·regression 영향 목록과 실행 격리 | E2E·sample·regression lanes, `P-DELIVERY` | `V11-CA-DECISION` | 완료 | 모든 항목 baseline hash·disposition·owner·activation stage 분류, `pending-disabled-by-contract-amendment`, executed·skipped 0 | Approved base `1f5b979675`와 현재 trace의 exact delta에서 public member 추가 935·제거 805를 생성하고 현재 Core·bindings raw regression 213개를 보존 대상으로 기록했다. 제거 member는 exact signature replacement 147개와 closed decision·behavior replacement 658개로 분류했다. Cross-language 76개 group과 Kotlin source/JVM 43개 group을 재검증한 결과 unmatched·ambiguous·parity mismatch와 Core PGM 항목은 0이다. 전체 2,998개에 대한 `--check`와 quarantine self-test는 pending 2,420, executed·skipped 0, negative mutation 14개로 통과했다. |
| `V11-CA-JOIN` | Contract amendment 합류 | amendment coordinator, `P-DELIVERY` | `V11-CA-SPEC`, `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE`, `V11-CA-PROTOCOL`, `V11-CA-IMPACT` | 완료 | 정식 spec·다섯 interface·wire·impact manifest·trace의 미분류와 semantic drift 0 | Review finding 반영 뒤 `DOC`, `INV`, `TRACE --check`, `WIRE`, `WIRE-GEN --check`, decoder fixture, impact generator `--check`, quarantine self-test, Instance Spot contract와 diff check가 통과했다. Trace는 documents 56, owners 1622, members 6199, unclassified·ambiguous·unknown 0이다. E2E·sample source diff는 0이며 Core PGM·perf는 사용자 확인에 따라 별도 병행 작업으로 제외했다. |
| `V11-R4A` | Contract amendment 독립 review | Codex xhigh review lane, `P-XHIGH` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-CA-JOIN` | 완료 | public contract·protocol·영향 disposition·대체 coverage의 I1·I2·I3 review clean | Candidate `ddf7747688`, `9f5a9caa34`, `46da38f29b`, `d5560840f5`, `4a566820c8`의 finding을 모두 반영한 `edc361796a`를 재검토했다. Codex 5.6 sol xhigh와 Claude Sonnet은 blocking finding 0으로 판정했고 별도 policy audit도 clean이다. 제거 member 805개의 replacement는 exact signature 147개와 closed decision·behavior 658개이며, cross-language 76개 group과 Kotlin source/JVM 43개 group의 unmatched·ambiguous·parity mismatch가 0이다. Core PGM·perf는 별도 작업으로 제외했다. |
| `V11-CA-DRAFT-RETIRE` | 임시 contract 변경 제안 흡수 확인과 삭제 | contract coordinator, `P-DELIVERY` | `V11-R4A` | 완료 | 채택 내용은 정식 spec·exact interface·protocol에 모두 존재하고 E2E·sample 영향은 manifest에 분류되며, 미채택·수정 결정은 ledger에 이유가 기록되고 두 proposal과 link가 repository에서 0 | R4A clean 뒤 임시 입력 두 개를 삭제하고 README·ledger·document inventory의 link와 허용 목록을 제거했다. 정식 spec·internals·다섯 exact interface·protocol·impact manifest와 이 ledger만으로 M6 입력을 구성하며 `DOC`, `TRACE --check`, impact quarantine self-test와 repository link 0 검증이 통과했다. 증거: `.artifacts/v11/evidence/V11-CA-DRAFT-RETIRE/result.json` |
| `V11-CA-SPOT-FLUENT` | Instance Spot fluent cold activation 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | global SpotRid 시작 method, Spot 전용 fluent call, User-only manager, type inference·initial Mesh·internal close가 공통 spec과 다섯 exact interface에서 일치하고 source·sample·E2E 변경 0 | `InstanceSpotAddress`를 복구하지 않고 Spot direct fluent call에 explicit Instance intent를 고정했다. 후속 `CA-D47`은 Missing activation의 source-side reservation을 제거했다. Source는 first-message activation envelope를 target에 제출하고 target CAS winner가 generic reservation으로 authority와 pending capacity를 함께 확보하며 Ready commit 뒤 envelope message를 local queue에 한 번 제출한다. Spot create terminal result는 `SpotRef`, 세 state(`Existing`, `Created`, `Rejected`)와 optional reply를 반환한다. 최초 contract self-test와 R4B review는 source-side reservation 문구를 대상으로 했으므로 target-owned activation 변경은 `V11-R5B`와 최종 E2E review에서 다시 검증한다. |
| `V11-CA-RELOCATION-LIFECYCLE` | Actor·Spot relocation adapter와 Entry Spot lifecycle 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | opaque bytes adapter, Snapshot invocation scope, Restore-before-commit, Entry Spot callback, queue·timer와 bounded concurrency 규칙이 공통 spec·다섯 exact interface에서 일치하고 이전 `Transfer*` 공개 이름 0 | `CA-D37~CA-D43`을 공통 spec과 다섯 언어 exact interface에 반영했다. Public contract와 protocol vocabulary를 `Relocation`으로 변경하고 호환 alias는 두지 않았다. Adapter state는 최대 64 MiB opaque bytes이며 factory·Restore와 journal validation·staging을 authority commit 전에 끝낸다. Entry Spot에는 commit 뒤 `OnActorRelocated`를 알린다. Current turn 하나만 source에서 완료하고 미실행 queue, accepted journal, logical timer registration과 pending tick은 Framework가 payload에 포함해 복원한다. Permit을 얻은 ready unit부터 이전하며 process 기본값은 outbound·inbound 64, Capture·Restore 8, payload in-flight 256 MiB다. Spot closing reason은 `RelocationOut`으로 통일한다. Runtime 구현과 최종 독립 review는 `V11-R4B`에서 clean 판정을 받았다. |
| `V11-R4B` | Instance Spot fluent·relocation lifecycle 계약 독립 review | Codex review lane, `P-XHIGH` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-CA-SPOT-FLUENT`, `V11-CA-RELOCATION-LIFECYCLE` | 완료 | global identity·cold activation·type inference·User-only manager, relocation adapter·callback·failure와 다섯 언어 parity의 I1·I2·I3 review clean | Codex 5.6 sol xhigh가 찾은 standalone Actor old Entry cleanup-before-replay, User Spot aggregate participant cardinality, exact request-source terminal identity, sequence domain, Java·Kotlin Instance timer, 이전 용어·metric과 impact hash 불일치를 모두 수정했다. Wire는 37 commands·157 types·36 bounds와 negative self-test 186개, DOC은 formal 137·exact 56, trace는 owner 1,650·member 6,389·미분류 0, impact quarantine은 3,458개 중 pending 2,850·executed/skipped 0으로 통과했다. 최종 Codex 재검토와 Claude Sonnet focused review는 모두 `CLEAN`이다. |

`V11-R4B` 완료 증거는 당시 source-side Instance reservation 계약에 대한 기록이다. 이후 확정한 `CA-D47`의
target-owned activation envelope는 해당 과거 판정을 소급해 바꾸지 않으며, 현재 `V11-R5B`와
`V11-E2E-SPEC-FINAL`의 필수 재검토 범위에 포함한다. 네 runtime은 target이 reservation을 소유하고 source claim이
0건임을 구현하기 전까지 Instance activation gap을 완료로 판정하지 않는다.
`CA-D47`은 공통 Framework API·Spot messaging·Location runtime·flow tracing, 다섯 언어 Spot exact interface와
Config 14 E2E에 반영했다. 계약 검증기는 target-owned claim, source reservation 0과 Ready 뒤 envelope message
one-shot을 required fragment로 고정했으며 check 112개와 negative mutation 14개가 통과했다. Source runtime과
wire command 39의 기존 preclaimed authority 경로는 구현 gap으로 남겨 `V11-M6B-*`와 `V11-R5B`에서 교체한다.

이 교정으로 최종 E2E·sample spec에서 다시 확정할 직접 영향 범위는 Instance relocation·concurrent cold request·cold
one-way와 request·kind collision·claim generation·source-loss recovery·activation failure release다. 현재 catalog ID로는
`M06`, `M07`, `M24`, `M25`, `M40`, `M57`, `M62`, `M63`이며, 각 scenario는 manager create가 아니라 Spot 전용
fluent call의 marker 유무, type 생략·명시, initial Mesh option과 최초 message one-shot dispatch를 검증해야 한다.
`V11-E2E-SPEC-FINAL`과 `V11-SAMPLE-SPEC-FINAL` 전에는 기존 source·registration을 수정하거나 실행하지 않는다.

#### 9.1.1 Contract amendment decision register

아래 결정은 두 임시 proposal, Store amendment와 후속 relocation lifecycle 교정의 open item을 합친 정식 spec 작성
입력이다. `CA-D01~D47`에 미결정 상태를
허용하지 않는다. 각 결정은 caller가 target node, owner fence, Store transaction과 retry state machine을
조합하지 않게 하는 방향을 선택했다. 언어별 표현은 달라도 identity, option, deadline, closed result와
failure 의미는 같아야 한다.

| ID | 비교한 대안 | 선택한 계약 | 정식 owner |
|---|---|---|---|
| `CA-D01` | `(MeshName, ActorId)` key / Store namespace global `ActorId` | `ActorId` 하나를 global key로 사용한다. UTF-8 1..255 bytes, case-sensitive exact equality이며 normalization과 case folding을 하지 않는다. MeshName은 initial placement attribute다. `ActorRef`는 `{ActorId, ObjectGeneration, MeshName, NodeRid}` location snapshot이고 message target이 아니다. JSON generation은 decimal string이다. | Framework API, Actor model, Location runtime, 다섯 Actor·serialization interface |
| `CA-D02` | Mesh별 Spot RID / global Spot RID | User·Instance Spot은 RoutingId의 1..255-byte exact value를 global logical key로 사용한다. `SpotRef`는 `{SpotRid, ObjectGeneration, MeshName, NodeRid}` snapshot이며 generation JSON은 decimal string이다. User Spot `Create`는 RID를 생성하고 `GetOrCreate`는 caller RID와 stable type을 받는다. 다른 kind·type은 `SpotTypeMismatch`다. Entry Spot RID는 Framework가 발급하며 caller create 대상이 아니다. | Spot messaging, Spot Actor, Location runtime, 다섯 Spot·serialization interface |
| `CA-D03` | 암묵적인 default Mesh / 명시적인 ambiguity failure | `InMesh`가 있으면 그 Mesh를 사용한다. 생략했을 때 object role Mesh가 하나면 자동 선택하고, 0개면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한 Mesh가 없으면 `MeshNotFound`로 끝낸다. | Framework API, MeshNode, Actor·Spot interface, error interface |
| `CA-D04` | overload 조합·재사용 builder / single-use fluent call | Actor·User Spot `Create`·`GetOrCreate`는 required identity·stable type을 받고 Mesh·request·placement·timeout을 option으로 받는다. Spot direct는 별도 Spot call builder에서 Instance intent·stable type·initial Mesh·placement를 option으로 받는다. 같은 option의 두 번째 설정은 `InvalidConfiguration`, 두 번째 terminal submit은 `AlreadySubmitted`다. Terminal call 시작 시 하나의 end-to-end deadline을 고정한다. | Framework API, Actor·Spot interface, async policy |
| `CA-D05` | Bind source의 Store refresh·hidden retry / exact ActorRef 한 번 제출 | Session bind는 `ActorId + ObjectGeneration`을 고정한다. Active forwarding만 relay하며 mapping 없음은 `ActorLocationStale`, generation 불일치는 `ActorGenerationStale`, pre-commit seal은 `ActorMoving`이다. Local Actor overload와 hidden retry를 제거한다. | Actor model, Session Actor dispatch, 다섯 STREAM interface |
| `CA-D06` | STREAM에 MeshName 고정 / global object capability enable | `EnableActorDispatch()`는 Mesh 인자가 없다. Object Client 또는 Server role과 Location Store가 하나 이상 구성됐는지 startup에서 확인한다. Global ID가 Mesh를 resolve하며 multi-Mesh 자체는 오류가 아니다. | Session Actor dispatch, configuration·STREAM interface |
| `CA-D07` | Actor를 먼저 이동 / proposal 뒤 atomic relocation | Cross-node join은 target proposal → shared policy preflight → source seal → durable capture → target reservation·factory·필요한 Restore·journal staging → owner와 membership aggregate commit → target callback·source leave와 old membership durable cleanup → journal replay → 남은 source resource cleanup → `Completed` → route ACK·steady normalization → target Ready 순서다. Commit 전 실패는 source를 유지하고 commit 뒤 실패는 published Relocation reference로 target recovery를 계속한다. | Spot Actor, maintenance, protocol |
| `CA-D08` | Join 전용 policy / factory에 고정한 공통 policy | Actor·User Spot·Instance Spot factory는 `Disabled`, `Recreate`, `Snapshot` 중 하나를 반드시 등록한다. Snapshot factory는 object kind에 맞는 Relocation Adapter를 함께 등록하고 adapter는 application-owned opaque bytes만 capture·restore한다. Join과 maintenance는 같은 factory policy와 adapter를 사용한다. Same-node join은 relocation이 아니므로 policy로 차단하지 않는다. | Framework API, Spot Actor, configuration·maintenance interface |
| `CA-D09` | create 경쟁을 caller에게 노출 / authority attempt에 합류 | Exclusive `Create`는 Ready existing이면 `AlreadyExists`, 다른 type이면 `TypeMismatch`다. `GetOrCreate`는 같은 type의 Ready 또는 Creating attempt에 합류해 같은 ref를 반환한다. CAS loser는 다른 factory를 시작하지 않는다. | Actor·Spot lifecycle, Location runtime, manager interface |
| `CA-D10` | CAS loser가 새 owner 재선택 / 같은 attempt의 bounded wait | Creation terminal call의 deadline은 resolve, reservation, factory와 Ready barrier 전체를 포함한다. Creating loser는 같은 attempt를 관찰하고 deadline이면 `DeadlineExceeded`로 끝내며 새 create를 시작하지 않는다. 다음 call은 exact authority를 reconcile한다. | Actor·Spot lifecycle, async policy, Location runtime |
| `CA-D11` | source memory payload / durable creation intent | Encoded creation request는 최대 1 MiB다. Reservation 전 immutable content reference와 hash를 creation intent에 기록하고 Ready 또는 fenced failure cleanup까지 유지한다. CAS winner만 payload를 사용하며 factory는 `(logical key, ObjectGeneration, attempt)` 기준 at-least-once·retry-safe다. Loser는 payload를 message로 바꾸지 않는다. | Message model, Actor·Spot lifecycle, Location Store, protocol |
| `CA-D12` | Spot generation 비공개 / public exact generation | `SpotRef.ObjectGeneration`을 공개한다. 값은 non-zero unsigned 63-bit conceptual value이며 언어 exact interface는 손실 없이 표현하고 JSON은 decimal string을 사용한다. Close·stale fence에 사용한다. | Spot messaging, common contracts, 다섯 Spot interface |
| `CA-D13` | implementation class 이름 / stable User Spot type | User·Instance Spot type은 UTF-8 1..255 bytes의 case-sensitive stable name이다. Normalization하지 않고 언어 class FQN을 wire·Store identity로 사용하지 않는다. 같은 server에 duplicate stable type을 등록하면 startup 오류다. | Framework API, Spot messaging, configuration interface |
| `CA-D14` | 자유 형식 selector / 작은 typed placement surface | Actor·User Spot create와 Instance Spot cold activation call은 optional `PlacementProfile`과 `AffinityKey`만 받는다. 둘 다 UTF-8 1..255 bytes stable value다. Required capability, region·zone과 deployment 정책은 server가 등록한 profile 내부에서 해석하며 caller callback·target RID·predicate를 받지 않는다. | Framework API, MeshNode, Location runtime, manager·configuration interface |
| `CA-D15` | participant별 visible CAS / bounded aggregate commit | User Spot과 member Actor는 non-zero 128-bit aggregate ID, 최대 1024 participant와 encoded 최대 1 MiB의 aggregate record를 사용한다. Generic Store transaction이 owner·membership visibility를 한 commit generation으로 전환한다. Commit 전 partial owner를 resolve하지 않으며 commit 뒤에는 전체 target recovery만 허용한다. | Spot Actor, Location runtime·Store, maintenance, protocol |
| `CA-D16` | cache를 hidden fixed profile로 고정 / 운영 가능한 두 public duration | `RouteCacheMaxAge` 기본 15초와 `RelocationForwardingWindow` 기본 30초를 공통 Location option으로 공개한다. 둘 다 0이면 cache·forwarding을 끈다. 양수이면 cache age가 forwarding window보다 최소 5초 작아야 한다. Runtime 변경은 새 entry와 새 relocation에만 적용한다. | Framework API, Location runtime, configuration interface |
| `CA-D17` | stale hop마다 Store 조회 / committed mapping chain | Relay는 committed source→target mapping만 사용하고 Store를 읽지 않는다. AuthorityOwnerGeneration은 hop마다 증가해야 하며 최대 8 hops다. Mapping 하나의 대기열은 1024 message·16 MiB 이하이고 negotiated message bound도 함께 지킨다. Original operation ID, generation, payload와 reply route를 보존하고 loop·bound 초과는 stale-route error다. | Actor·Spot messaging, Location runtime, protocol·monitoring |
| `CA-D18` | relocation policy 생략 overload / explicit policy | 모든 object Server factory는 policy를 명시한다. 생략 overload와 compatibility default를 두지 않는다. 이동을 지원하지 않아도 `Disabled`를 등록한다. | Framework API, 다섯 configuration interface |
| `CA-D19` | fixed RID 전면 제거 / manual topology에 한정 | Fixed Routing ID는 Location Store descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만 허용한다. Object Client·Server 또는 automatic mode와 함께 설정하면 startup 오류다. | MeshNode, network identity, 다섯 routing configuration interface |
| `CA-D20` | slot allocation 유지 / prefix+random lifecycle RID | Public slot count·group, allocation Store·provider와 result type을 제거한다. Automatic RID는 diagnostic prefix와 128-bit CSPRNG lowercase hex suffix를 사용한다. | MeshNode, Location Store, 다섯 routing interface |
| `CA-D21` | 별도 RID provider / descriptor owner CAS 재사용 | Prefix는 ASCII `[A-Za-z0-9._-]` 1..64자이고 full RID는 `prefix-<32 lowercase hex>`이며 255 bytes 이하이다. Descriptor owner CAS가 `(MeshName, RID)` active conflict를 확인한다. 최대 8회 새 RID를 만들고 계속 충돌하면 `RoutingIdConflict`로 startup을 실패한다. Replacement lifecycle은 새 RID를 사용한다. | MeshNode, Location Store, protocol·monitoring |
| `CA-D22` | Channel weight 합성 / node-wide Router weight | Placement weight는 Channel weight와 분리한 0..100 값이며 기본 100이다. 0은 신규 placement·transfer target에서만 제외한다. Reservation이 완료된 attempt와 existing traffic은 이후 weight 변경으로 취소하지 않는다. Startup builder, runtime option, descriptor와 snapshot이 같은 값을 사용한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D23` | capacity 미설정 / finite node·type capacity | Object Server 기본 node capacity는 active 10,000·pending 128이다. Stable type별 limit은 생략하면 node limit을 공유하고 명시하면 1..`2^31-1` 범위에서 더 작은 limit을 적용한다. Active·pending filter가 weight보다 먼저이며 exhaustion은 `PlacementCapacityExhausted`다. Reservation recovery가 stale pending을 exact fence로 회수한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D24` | 두 CAS와 compensation / generic atomic reservation | Store는 object-specific method 대신 generic `Reserve`, `Commit`, `Abort` closed operation을 제공한다. Reservation은 object kind·global key·stable type·target descriptor key·lifecycle generation·capacity delta와 exact owner fence를 가진다. TTL을 두지 않고 Creating authority와 target owner lease로 recovery·takeover·abort한다. | Location runtime·Store, Redis provider, 다섯 provider interface |
| `CA-D25` | `InstanceSpotAddress` overload / Manager explicit create / Spot direct fluent activation | `InstanceSpotAddress`와 Instance manager create를 모두 제공하지 않는다. `SendToSpot`·`RequestToSpot`은 global SpotRid를 받고 Spot 전용 fluent call을 반환한다. Marker 없는 call은 existing-only다. Instance marker가 있는 Missing call만 optional stable type과 initial Mesh로 cold activation한다. Selected Mesh의 distinct type이 하나면 생략한 type을 자동 선택하고 여러 type이면 명시를 요구한다. Existing authority는 저장된 kind·type과 current Mesh를 사용한다. | Spot address messaging, Location runtime, 다섯 Spot·messaging interface |
| `CA-D26` | directory·handle surface 유지 / manager와 operational query로 통합 | Actor directory, Spot handle·resolver, Actor-Spot handle resolver와 unbounded list를 제거한다. Manager `Find(global ID)`와 current Spot query, page size 1..1000·encoded 4 MiB 이하의 operational query로 대체한다. Entry registration은 Object Server builder가 소유한다. | Framework API, Actor·Spot·Location spec, 다섯 interface |
| `CA-D27` | ID-only destroy·close / exact ref mutation | Destroy와 Close는 exact ref를 받는다. 같은 incarnation이 이미 없으면 idempotent `false`, 다른 generation은 stale-generation error, moving은 typed moving error다. Current ref를 다시 찾아 새 incarnation을 종료하지 않는다. | Actor·Spot lifecycle, manager·error interface |
| `CA-D28` | Client·Server 독립 flag와 local fallback / closed object role | MeshNode role은 `None`, `Client`, `Server` 중 하나며 Server가 Client capability를 포함한다. Client·Server는 Location Store가 필수다. None은 manager·factory·placement를 제공하지 않고 hidden local object runtime도 만들지 않는다. Factory는 Server builder에서만 등록한다. | Framework API, MeshNode, Location runtime, 다섯 configuration·DI interface |
| `CA-D29` | .NET source를 새 정본으로 승격 / formal spec parity 유지 | Current source-only member는 계약 근거로 사용하지 않는다. Formal common spec과 다섯 exact interface를 한 candidate에서 갱신하고 Kotlin extension·JVM ABI snapshot까지 별도 검증한다. Gap은 implementation row가 소유한다. | 다섯 exact interface, public contract trace |
| `CA-D30` | Missing·Creating·Store failure negative cache / negative cache 없음 | Missing, Creating과 Store failure를 cache하지 않는다. Positive Ready cache도 current owner lease의 local admission deadline까지만 유효하다. Store recovery 또는 higher StoreVersion·stale result가 있으면 즉시 invalidate하고 다음 operation이 exact resolve한다. | Location runtime·Store, monitoring |
| `CA-D31` | Location Store가 relocation payload까지 소유 / Location·Relocation capability 분리 | Location Store는 owner·location·generation, relocation phase·RelocationId·source·target fence, relocation reference·checksum, placement reservation과 aggregate authority를 소유한다. Relocation Store는 application state, accepted journal, reply·replay·recovery payload를 immutable chunk·root로 저장한다. Actor·Spot별 Store interface는 추가하지 않는다. | Framework API, Location runtime, Location·Relocation Redis spec, 다섯 provider interface |
| `CA-D32` | Relocation manifest를 participant authority로 사용 / Location canonical participant set을 authority로 사용 | Location aggregate가 bounded canonical participant set, participant별 mutation, aggregate generation과 inventory digest를 한 transaction에서 commit한다. Relocation manifest는 participant payload 탐색용 projection이며 authority가 아니다. Target은 Location participant set과 Relocation manifest의 canonical digest가 같을 때만 restore한다. | Spot Actor, Location runtime·Store, Relocation Store, protocol |
| `CA-D33` | Store capability 묶음 등록 / 별도 public interface와 등록 | Root는 Location Store와 Relocation Store를 별도 public API로 등록하며 capability마다 최대 하나만 허용한다. Location 기능을 사용하는 host는 Location Store가 정확히 하나 필요하다. `Recreate` 또는 `Snapshot` factory가 하나라도 있으면 Relocation Store가 정확히 하나 필요하고, `Disabled` factory와 same-node join만 사용하는 host에는 필요하지 않다. Missing·duplicate는 socket bind 전에 startup configuration error다. `Disabled` cross-node 이동은 payload capture 전에 거부한다. | Framework API, 다섯 configuration interface |
| `CA-D34` | Redis composite class / 별도 Redis implementation class | 공식 Redis package는 Location Store와 Relocation Store의 concrete class를 따로 제공한다. 같은 Redis deployment·cluster를 서로 다른 key prefix로 사용할 수 있고 별도 Redis로 분리할 수도 있다. Client connection 공유와 disposal 방식은 언어별 구현이 소유하며 공통 correctness 조건이 아니다. | Location Redis spec, Relocation Redis spec, 다섯 Redis interface |
| `CA-D35` | Cross-store transaction·2PC / immutable payload 뒤 Location CAS publication | Runtime은 Relocation root를 먼저 저장하고 reference·checksum·retention을 검증한 뒤 Location authority 또는 aggregate CAS로 공개한다. CAS 전 실패·conflict payload는 orphan retention 또는 idempotent delete로 제거한다. Root 교체는 새 root 저장→Location reference CAS→이전 root 정리, 삭제는 Location reference 해제 CAS→Relocation payload 삭제 순서다. Cross-store transaction과 2PC를 요구하지 않는다. | Location runtime, Relocation Store, protocol·recovery internals |
| `CA-D36` | Transfer vocabulary / Relocation vocabulary와 data-loss terminal | Public contract와 protocol vocabulary를 Relocation Store·reference·root·envelope으로 통일한다. `Snapshot`, `Capture`, `Restore`, `Retire`, `Shutdown`은 state policy·callback·host operation 이름이므로 유지한다. Published reference의 payload가 영구적으로 없거나 checksum 또는 participant inventory digest가 일치하지 않으면 이전 owner로 rollback하지 않고 non-retriable `RelocationDataLost`로 끝낸다. 이전 `Transfer*` public alias는 제공하지 않는다. | Framework API, maintenance·monitoring, protocol, 다섯 exact interface |
| `CA-D37` | Generic `TState`·state contract ID / application-owned opaque bytes | Relocation adapter는 opaque byte sequence만 주고받는다. Application이 format, version, compatibility와 migration을 관리하며 Framework는 state contract ID, state type과 relocation codec을 제공하거나 descriptor에 싣지 않는다. | Framework API, Actor model, Spot messaging, Location runtime, 다섯 relocation interface |
| `CA-D38` | 하나의 generic state adapter / Actor·Spot adapter 분리 | Actor에는 `ActorRelocationAdapter`, User·Instance Spot에는 `SpotRelocationAdapter`를 등록한다. 두 interface의 operation 이름은 `Capture`와 `Restore`이며 target factory가 instance를 만들고 `Restore`는 그 instance에 bytes를 적용한 뒤 instance를 반환하지 않는다. | Spot Actor, configuration, 다섯 Actor·Spot exact interface |
| `CA-D39` | Operation별 adapter / factory Snapshot adapter 공유 | `Snapshot`으로 등록한 Actor의 maintenance와 cross-node User·Entry Spot join은 같은 Actor adapter를 사용한다. User Spot aggregate에서는 Spot과 각 Snapshot member Actor adapter를 각각 호출한다. Same-node join, `Disabled` 거부와 `Recreate`에서는 adapter를 호출하지 않는다. | Spot Actor, maintenance, Relocation Store, 다섯 exact interface |
| `CA-D40` | Commit 뒤 Restore / Restore 뒤 authority commit과 Entry lifecycle | Target factory·`Restore`를 owner·membership commit 전에 끝낸다. Entry Spot 자체는 이동하지 않지만 maintenance로 Actor membership이 source Entry에서 target Entry로 바뀌면 commit 뒤 target `OnActorRelocated`와 source `OnLeaveActor`를 호출한다. Callback failure는 rollback하지 않고 target admission을 닫은 채 재시도한다. User Spot aggregate는 membership callback을 호출하지 않는다. `Capture`·`Restore` failure는 pre-commit source 유지와 retry-safe·at-least-once 규칙을 따른다. | Spot Actor, Location runtime, maintenance, 다섯 Entry Spot·relocation exact interface |
| `CA-D41` | 전체 queue drain / current turn 뒤 queue·timer relocation | Framework infrastructure notification이 execution queue의 turn boundary에 도달하면 현재 실행 중인 turn만 source에서 완료한다. 아직 실행하지 않은 message, accepted journal, logical timer registration과 pending tick은 Relocation Store에 저장하고 target에서 순서를 보존해 복원한다. Application이 timer를 다시 등록하지 않는다. | Async policy, Spot Actor, Location runtime, maintenance, E2E |
| `CA-D42` | 종류별·random batch / readiness-first bounded sliding relocation | Standalone Actor, Instance Spot과 User Spot aggregate에 internal relocation notification을 전달한다. Permit을 즉시 얻은 ready unit만 seal하고 이전하며, permit을 얻지 못한 unit은 source에서 업무를 계속하고 notification을 재예약한다. User Spot과 member Actor는 분리하지 않는다. Public ready callback은 추가하지 않는다. | Maintenance, execution queue, 다섯 runtime |
| `CA-D43` | 4개 고정 relocation / count·callback·byte 독립 gate | Process 기본값은 active outbound 64, active inbound 64, concurrent Capture 8, concurrent Restore 8, encoded payload in flight 268,435,456 bytes다. 모든 값은 application configuration으로 변경할 수 있고 양수여야 한다. 모든 permit 전 source를 seal하지 않으며 byte 한도를 넘는 단일 User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행한다. | Framework API, Location option, maintenance, 다섯 exact interface, E2E |
| `CA-D44` | Authority Put이 opaque payload에서 target owner를 추출 / owner metadata와 public transition 분리 | Public authority transition은 Active row의 `Preserve`·`NewOwner`와 delete만 제공한다. `Preserve`는 target owner token을 받지 않고 `NewOwner`는 exact `LocationOwnerToken`과 relocation capacity fence를 반드시 받는다. Provider는 같은 transaction에서 target owner lease를 검증하고 owner ID·lease generation metadata를 기록한다. Missing→Pending 생성과 initial object·owner generation 발급은 generic `Reserve`만 수행하며 compare-exchange에는 Missing expectation과 별도 create transition을 제공하지 않는다. Standalone Actor relocation은 single-key `NewOwner` CAS를 유지하고 User Spot은 aggregate target owner를 사용한다. | Location runtime·Store, Redis provider, 다섯 provider exact interface |
| `CA-D45` | Reservation provider가 Creating·Ready authority payload를 합성 / opaque payload와 provider allocation metadata 분리 | `Reserve` request는 Framework가 encode한 Creating authority payload를 받고, `Commit`은 Ready authority payload를 받는다. Provider는 payload body를 해석하거나 합성하지 않고 exact bytes를 저장한다. 별도 current placement allocation은 `Pending`·`Active` state, kind, stable type, descriptor key·lifecycle generation과 `1..2^31-1` capacity delta를 저장한다. Reserve는 Missing→Pending, exact Commit은 Pending→Active, exact Abort는 Pending→Missing만 수행하며 snapshot·stored·scan result가 allocation을 반환한다. Owner metadata는 allocation과 분리한다. | Location runtime·Store, Redis provider, 다섯 provider exact interface |
| `CA-D46` | Missing create reservation을 relocation에도 재사용 / existing object용 relocation capacity fence 분리 | Existing Actor·Spot relocation은 create reservation을 재사용하지 않는다. 별도 relocation capacity reservation은 current authority StoreVersion·owner와 durable Active allocation의 source descriptor key·lifecycle generation·kind·stable type·capacity delta를 request와 exact-match하고 target descriptor lifecycle·owner lease·capability·pending capacity를 live/exact로 검증해 target pending만 예약한다. Source descriptor row·lease가 stale·missing이어도 durable allocation match로 recovery할 수 있다. Standalone Actor `NewOwner` CAS가 Reserved fence를 직접 소비한다. User Spot aggregate prepare는 `NewOwner` participant와 일대일인 fence를 aggregate ID·generation에 atomic bind하며 commit 또는 aggregate abort만 이를 finalize한다. Direct abort와 다른 aggregate는 bind된 fence를 바꾸지 못한다. Commit은 target live fence를 다시 확인하며 Capacity, owner, allocation과 membership을 같은 transaction에서 전환한다. Delete는 live current owner lease와 Active allocation을 검증하고 active delta를 atomic하게 감소시킨다. Recovery는 exact fence·authority·allocation을 사용하고 TTL에 의존하지 않는다. | Location runtime·Store, maintenance, Redis provider, 다섯 provider exact interface |
| `CA-D47` | Source가 Instance owner claim 뒤 target에 first message 전송 / target-owned first-message activation envelope | Ready authority는 source가 current owner로 일반 direct call을 보낸다. Missing+Instance intent에서는 source가 target만 선택하고 global Spot RID·stable type·descriptor fence·operation identity·reply correlation·deadline과 first message를 activation envelope로 target transport에 제출하며 owner claim과 reservation을 만들지 않는다. Target은 current authority와 local exact instance를 확인하고 Missing이면 자신을 owner로 generic Reserve를 수행한다. CAS winner만 factory·initialize·Commit을 실행하고 Ready barrier 뒤 envelope message를 local queue에 exactly once 제출한다. CAS loser는 local instance를 만들지 않고 Ready winner로 original operation을 한 번 redirect하거나 Creating completion에 합류한다. Authority와 일치하지 않는 local instance는 fence한다. | Framework API, Spot messaging, Location runtime, 다섯 Spot exact interface, Instance E2E |
| `CA-D48` | 언어별 Redis authority layout 중 하나를 복사 / 장점을 결합한 공통 hybrid schema | Authority current state와 active-scan history는 authority별 HASH에 두고 global counter·capacity·membership·versioned index만 shared HASH/ZSET에 둔다. Creation reservation·relocation fence·aggregate는 operation별 HASH다. Provider transaction domain 전체가 literal `{zlink-location-v1}` hash tag를 공유하고 모든 Lua key를 `KEYS`로 전달한다. Public descriptor 5-field HASH와 admission metadata HASH를 분리한다. Watermark·immutable history·tombstone·durable cursor로 1000 item·4 MiB snapshot page를 만들며 전체 materialization과 numeric revision score를 금지한다. | Redis Location Store 공통 spec·fixture, C++·.NET·JVM·Node provider와 cross-language Redis test |
| `CA-D49` | Capacity bucket과 `objectKind`를 언어별 enum 표현에 맡김 / Redis physical encoding을 고정 | Current authority의 `objectKind`는 `actor`, `user_spot`, `instance_spot` token만 사용한다. Capacity node bucket은 canonical descriptor key와 lifecycle generation decimal을 UTF-8 byte length-prefix로 encode하고, type bucket은 같은 값 뒤에 canonical `objectKind` token과 stable type을 같은 방식으로 붙인다. 이 규칙은 Unicode, enum 이름과 숫자값 차이에도 네 provider가 같은 field를 갱신하게 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider physical schema test |
| `CA-D50` | Owner lease를 언어별 string·HASH·dual-write로 유지 / 하나의 HASH 계약으로 고정 | `owner-lease:D`는 `ownerId`, `generation`, `expiresAt` 세 field와 key TTL만 사용한다. Descriptor·authority·RoutingId allocation은 이 HASH를 직접 검증·갱신하며 별도 legacy lease value나 owner lease index를 쓰지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider owner lease test |
| `CA-D51` | Descriptor owner index를 owner ID raw suffix로 구성 / exact owner token digest로 구성 | Descriptor index는 canonical descriptor key member를 저장하는 `descriptor:mesh:index` SET 하나를 사용한다. Cleanup index는 `ownerId + NUL + LeaseGeneration decimal`의 SHA-256 lower-hex suffix를 사용하고 같은 canonical key를 member로 저장한다. Owner ID만 일치하는 다른 host lifecycle descriptor는 제거하지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider descriptor cleanup test |
| `CA-D52` | Authority history를 language별 JSON·field grouping으로 저장 / revision-prefixed field encoding으로 고정 | Revision hex `R`마다 full snapshot은 `R:deleted=0`과 exact current 13개 `R:<field>`를 저장하고 tombstone은 `R:deleted=1`, `R:authorityKey`만 저장한다. Membership history는 `R` field에 immutable bytes를 저장한다. 어느 언어가 만든 watermark snapshot도 다른 언어가 복원할 수 있어야 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider concurrent scan test |
| `CA-D53` | Automatic RouteMesh initiator와 duplicate-pipe admission을 한 문장으로 설명 / 시작 규칙과 안전장치를 분리 | Automatic RouteMesh는 canonical RID가 더 작은 MeshNode만 pairwise connect를 시작한다. Manual topology의 양방향 connect와 automatic의 경합·stale discovery 후보만 공통 duplicate-pipe admission에서 RID·lifecycle generation을 확인해 하나의 ready connection으로 수렴한다. ClientServer는 Client가 server별 intent를 만들고 classic fanout은 Subscriber가 publisher별 intent를 만드는 비대칭 topology다. | `10-channel-topology.ko.md`, `12-client-server-channel.ko.md`, `21-mesh-node.ko.md`, topology regression |
| `CA-D54` | Immutable digest를 언어별 descriptor serialization hash로 계산 / 공통 canonical preimage hash | Admission HASH의 `immutableDigest`는 `zlink-mesh-node-immutable-v1` domain부터 immutable descriptor·capability field를 UTF-8 byte length-prefix segment로 연결한 preimage의 SHA-256 lower-hex다. Channel name, capability와 placement profile은 unsigned UTF-8 byte lexical order로 정렬한다. Descriptor revision, weight 값, maintenance wave, runtime state, owner token, timestamp와 usage count는 제외한다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider byte-level contract test |

`CA-D16`은 두 값을 공개하지만 invalid 조합을 runtime에 넘기지 않는다. `CA-D23`의 기본 capacity는 deployment가
별도 설정 없이도 bounded pending admission을 갖게 하며, 더 큰 값을 선택하면 descriptor와 Store reservation이
같은 값을 게시·검증한다. `CA-D11`, `CA-D15`, `CA-D17`, `CA-D21`의 byte·count bound는 protocol schema의
정식 bound로 추가하고 네 runtime이 같은 negative fixture를 사용한다.

Impact manifest는 public member, E2E scenario, sample, 실행 registration과 회귀 test를 각각 독립 항목으로
기록한다. 각 항목은 stable ID·path, baseline hash, `retain`·`amend`·`replace`·`add`·`remove`, 새 acceptance
intent, spec owner, runtime owner, activation stage와 approved hash를 가진다. Approved hash는 spec 최종화 전까지
비워 둘 수 있지만 disposition과 대체 coverage는 `V11-R4A` 전에 확정한다. `remove`는 단순 삭제를 뜻하지 않는다.
공개 계약에서 해당 흐름이 없어지는 근거와 같은 의미를 검증할 대체 coverage가 함께 있어야 한다.

Runtime 중 유지할 regression 목록에는 Core·binding raw regression, public declaration snapshot, wire golden·negative,
mailbox ordering, terminal winner, ownership·CAS·lease·fencing과 resource cleanup test를 포함한다. 새 public behavior는
각 언어의 deterministic internal contract test owner를 지정한다. E2E·sample source와 registration은 삭제하거나
주석 처리하지 않고 build·test 실행 graph에서만 분리한다.

R4A에서 확정한 채택 내용은 정식 spec·internals, exact interface, protocol/schema와 impact manifest에 반영했다.
채택하지 않았거나 표현을 바꾼 항목의 결론과 이유는 `CA-D01~CA-D46`이 소유한다. 임시 설계 입력은
`V11-CA-DRAFT-RETIRE`에서 삭제했으며 이후 M6 작업 지시는 정식 문서와 이 ledger만으로 구성한다.

## 10. M6 — Framework vertical slice 네 병렬 lane

각 slice는 C++·.NET·JVM·Node.js가 같은 amended spec·schema revision에서 public interface의 실제 동작을
각각 구현하고 internal contract와 독립 review에서 합류한다. 보존된 Core service 구현·test의 component,
state machine, algorithm, ordering·ownership과 failure 처리를 참고하되 정식 spec·internals와 다른 부분은
목표 계약을 따른다. 언어별 lane은 transport부터 public operation까지 하나의 vertical slice를 완성한 뒤 다음
기능으로 진행한다. 한 언어 구현을 다른 언어가 source 수준에서 포팅하지 않으며, 동작 검증 전에 공통
abstraction을 만들기 위한 대규모 통합 refactoring을 수행하지 않는다.

M6 runtime row는 `M6-RUNTIME`으로 sample·E2E project와 task를 제외한 build, public declaration snapshot,
internal unit·contract·resource·protocol regression만 실행한다. 이 격리는 E2E·sample 실패를 숨기는 skip이 아니다.
Impact manifest의 실행 source와 registration은 계속 `pending-disabled-by-contract-amendment` 또는
`pending-disabled-reviewed-source`이며, review 중인 E2E·sample contract 문서는 `active-contract-spec`이다.
Executed·skipped는 모두 0이어야 한다. Source와
registration을 삭제하거나 runtime 통과용 compatibility helper를 추가하지 않는다.

### 10.1 M6A — Topology, dispatch, Location과 liveness

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6A-CPP` | C++ topology·dispatch·Location·liveness runtime | C++ lane, `P-DEEP` | `V11-R4B` | 수정 진행 | node·Channel·ClientServer·manual·automatic classic fanout, remote placement, mailbox·CAS·reconnect·liveness internal contract 통과 | Public raw ROUTER·DEALER·PUB·SUB API로 node·Channel send/request, ClientServer 독립 admission·send/request, Location descriptor publish/watch/CAS, manual·automatic classic fanout과 publisher별 reconnect, bounded mailbox, terminal-once registry, 5초/15초 liveness를 구현했다. 실제 `mesh_node_runtime` public host가 Framework-owned raw owner를 생성하고 app·host dispatch를 이 경계로 연결한다. 11.0 bindings를 사용하는 전체 `zlink_framework` target compile과 M6A·M6B focused contract·unit 5/5가 통과했다. Compatibility header나 Core·bindings 수정은 없고 Sample·E2E 변경·실행은 0이다. |
| `V11-M6A-DN` | .NET topology·dispatch·Location·liveness runtime | .NET lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·mailbox·CAS·Task terminal winner·liveness internal contract 통과 | 최신 `Systems.Zlink` 11.0.0 package의 public raw API로 managed MeshNode를 구현했다. 실제 두 node admission·remote `ToChannel`을 포함한 foundation 11/11과 backend·monitor·dispatch·Location 62/62가 통과했다. R5A 수정에서 descriptor extension의 필수·unknown TLV와 원본 descriptor bytes를 보존하고, 같은 lifecycle의 revision 증가·같은 revision exact-byte idempotence·immutable field 변경 거부를 mutation 전에 검사했다. Foundation focused regression 13/13이 통과했다. .NET source에는 target exact interface의 object role·placement weight·active/pending capacity public builder가 아직 없어 실제 application configuration 연결과 physical connection identity 기반 duplicate-pipe 판정은 후속 public-contract parity가 필요하다. 상수로 완료 처리하지 않았다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6A-JVM` | JVM topology·dispatch·Location·liveness runtime | JVM lane, `P-DEEP` | `V11-R4B` | 수정 진행 | Java·Kotlin API, remote placement, CAS·executor·coroutine·reconnect internal contract 통과 | 최신 `systems.zlink:zlink:11.0.0` public raw binding만 사용해 Framework ROUTER owner와 exact hello·admit·update, Node·Channel send/request/reply, bounded mailbox, Location CAS/watch, placement selector, reconnect와 5초/15초 liveness를 구현했다. Classic fanout connection fence·beacon·timeout contract를 포함한 service·binding regression과 M5 foundation, Java·Kotlin compile이 통과했다. 전체 core test 383개 중 M6B stateful Spot·Actor 구현을 요구하는 기존 6개만 격리됐다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6A-NODE` | Node topology·dispatch·Location·liveness runtime | Node lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·CAS·Promise·event-loop·reconnect internal contract 통과 | Public raw binding만 사용하는 owner에 admission, node·Channel send/request, mailbox, topology·placement, Location CAS, liveness와 전용 ClientServer·fanout registry를 구현하고 public host factory를 연결해 제거된 `createMeshNode` 의존을 없앴다. Framework TypeScript compile, M6A 7/7, M5 4/4와 changed-source ESLint가 통과했다. M6B 기능은 가짜 성공 없이 `NotSupported`로 유지하며 Sample·E2E 변경·실행은 0이다. |
| `V11-R5A` | Topology runtime slice 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | 수정 진행 | topology·dispatch·placement·authority·liveness와 실행 격리의 I1·I2·I3 review clean | Codex 5.6 sol xhigh review에서 확인한 C++ public host의 삭제된 Core Service header·owner 잔존은 Framework raw owner·stateful runtime을 실제 app·MeshNode·Spot·Actor·STREAM host 경계에 연결해 해소했다. 전체 C++ framework compile과 focused regression 5/5가 통과했다. .NET descriptor의 ObjectRole·security·placement/capacity configuration과 physical connection identity 기반 duplicate-pipe 판정은 public-contract parity 후속 조건으로 남는다. Sample·E2E source 변경은 0이다. |

Framework service runtime은 제거한 Core heartbeat option을 설정하지 않는다. Raw monitor는 orderly disconnect를
즉시 알리고 Framework liveness probe scheduler가 half-open deadline을 소유한다. Location owner lease와
Framework STREAM heartbeat는 이 deadline을 authority나 application session progress로 재사용하지 않는다.
Fanout subscriber는 publisher별 전용 SUB socket을 사용하며 reserved beacon을 application message로
전달하지 않는다. Publisher 하나의 deadline은 다른 publisher의 ready 상태를 바꾸지 않는다.

### 10.2 M6B — Spot, Actor, STREAM과 Instance Spot

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6B-CPP` | C++ stateful object runtime | C++ lane, `P-DEEP` | `V11-R5A` | 구현 완료 | turn·membership·session·Instance activation과 stale generation contract 통과 | Framework-owned generic object authority에 MeshName과 분리한 global identity, weighted remote create reservation·Ready barrier, concurrent create join, exact-generation destroy, cross-node membership CAS와 application·infrastructure turn 분리를 구현했다. 이동 중 ingress hold와 commit·abort queue handoff, logical timer registration·pending tick 보존, Instance marker cold activation, STREAM connection·binding generation과 exact Actor authority fence를 검증한다. Public raw ROUTER의 exact Spot·Actor route fence send/request/reply와 stale fence terminal-once를 실제 public host에 연결했다. `mesh_node_runtime`, Spot·Actor dispatch, RouteMesh monitoring과 STREAM session binding은 제거된 Core Service owner 대신 Framework raw owner·stateful registry를 사용한다. 11.0 bindings 기준 전체 `zlink_framework` compile과 `raw_route_port`, wire codec, operation registry, M6A·M6B runtime focused regression 5/5가 통과했다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. |
| `V11-M6B-DN` | .NET stateful object runtime | .NET lane, `P-DEEP` | `V11-R5A` | 진행 | turn·membership·session·Instance Task 경쟁 contract 통과 | Framework-owned managed MeshNode에 owner별 application·infrastructure mailbox와 claim, local·remote Spot·Actor exact-generation dispatch, accepted join membership commit, lifecycle, logical multicast, Actor request terminal CAS, relocation seal·abort·commit, STREAM exact Actor binding·relay와 Instance Spot reactivation generation을 구현했다. R5B review에서 확인한 무제한 request operation table은 기본 65,536개 capacity를 ID·deadline 할당 전에 원자적으로 검사하고 request에는 `Backpressured`를 반환하도록 수정했다. Stateful focused regression 10/10이 통과했다. Inbound remote Spot·Actor는 queue admission 전에 target node lifecycle과 object generation을 검사한다. 현재 .NET location projection과 resolved handle에는 Authority Store의 `AuthorityOwnerGeneration`이 없으므로 sender 전달과 target exact 검증은 해당 authority public/runtime seam 구현 뒤 연결해야 한다. 임시 generation이나 상수로 통과시키지 않았다. Actor·Location·session을 포함한 이전 runtime 회귀는 186개가 통과하고 기존 문서 문자열 assertion 1개만 실패했다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6B-JVM` | JVM stateful object runtime | JVM lane, `P-DEEP` | `V11-R5A` | 진행 | Java·Kotlin turn·membership·session·Instance contract 통과 | Framework가 소유하는 Spot mailbox와 lifecycle generation, local·remote Spot send/request single-terminal·timeout, Actor create·join accepted commit·membership epoch·leave lifecycle과 local·remote Actor owning-Spot dispatch를 public raw binding 위에 구현했다. Logical Multicast는 local subscription fan-out과 admitted remote MeshNode별 ROUTER command 23 전송을 연결하고 local·remote target 결과를 `PublishDetail`에 기록한다. Spot resolver와 Actor Location row에서 받은 `AuthorityOwnerGeneration`을 raw route registry와 wire에 전달하며 receiver는 target node lifecycle, object generation과 authority owner generation을 application admission 전에 exact 비교한다. command 39는 closed codec, source·target node lifecycle, 등록된 exact Instance authority fence, object generation을 검증한 뒤 stable-type cold activation barrier와 Spot mailbox로 dispatch한다. M6B binding·Actor focused contract 21/21, JVM core regression 404/404와 Kotlin compile이 통과했다. 남은 gap은 durable Location publication·watch가 command 39 authority intent를 자동 등록하는 연결, ID-first 경로로 대체되기 전 legacy explicit `ActorRef` 호출의 authority generation 공급, Remote STREAM binding이다. 이 seam이 없으면 raw runtime은 추정 generation을 넣지 않고 제출을 거부한다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6B-NODE` | Node stateful object runtime | Node lane, `P-DEEP` | `V11-R5A` | 진행 | turn·membership·session·Instance Promise 경쟁 contract 통과 | public raw binding 위에 Spot·Actor exact-generation dispatch, owner별 serial turn, membership epoch, logical multicast local fan-out·remote node당 1회 전송, STREAM binding generation·delivery와 terminal-once Promise registry를 연결했다. Instance Spot stable type·attempt reservation, command 39 exact route·source·authority fence와 registered intent cold activation generation도 연결했다. Request operation table은 기본 65,536개 capacity를 ID·timer 할당 전에 검사한다. Outbound Spot·Actor route는 object generation을 authority owner generation으로 대체하지 않으며, 실제 Location·lookup fence를 등록한 경우에만 전송하고 누락되면 `NotFound`로 거부한다. Actor lookup reply는 membership epoch와 authority owner generation을 분리해 보존한다. M6B internal contract 11/11, M6A 7/7, M6C 4/4, M5 5/5와 Framework TypeScript compile이 통과했다. Durable Location publication에서 Spot route와 Instance intent를 자동 등록하는 연결은 계속한다. Sample·E2E 변경·실행은 0이다. |
| `V11-R5B` | Stateful runtime slice 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | 수정 진행 | global identity, remote create, mailbox ordering, ownership·fencing·resource와 실행 격리의 I1·I2·I3 review clean | Codex 5.6 sol xhigh review에서 세 managed language의 authority owner generation 미연결, command 39 durable Instance cold activation 미연결, JVM logical multicast no-op, 세 언어의 무제한 request operation table과 JVM mailbox close retained payload를 P1으로 확인했다. JVM mailbox close, JVM·.NET·Node operation capacity 수정과 focused regression은 통과했다. JVM은 정식 authority 결과·mutation·aggregate 계약과 public provider registration seam을 추가했으며, 공식 Redis provider와 durable publication 연결은 M6C에서 계속한다. authority·Instance·logical multicast 연결은 언어별로 병렬 수정한다. Sample·E2E source 변경은 0이다. |

### 10.3 M6C — Maintenance, monitoring과 hosting

M6C는 host barrier, all-or-none preflight, `Retire`·`Shutdown`, durable authority·relocation·recovery,
observability와 C++ host·ASP.NET·Spring·NestJS integration을 구현한다. 공통 Framework는 고정 maintenance HTTP
route를 만들지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6C-CPP` | C++ maintenance·monitoring·hosting | C++ lane, `P-DEEP` | `V11-R5B` | 진행 | preflight·relocation·recovery·terminal observation·bounded host contract 통과 | Framework stateful runtime에 permit-before-seal relocation coordinator를 추가했다. Process gate는 outbound·inbound 기본 64, Capture·Restore 기본 8과 payload in-flight 256 MiB를 queue seal 전에 all-or-none으로 예약하며, oversized unit은 gate가 비어 있을 때만 단독으로 admit한다. Current application turn이 끝난 object만 reversible seal하고 미실행 queue와 logical timer registration을 deterministic envelope로 freeze하며 seal 뒤 application ingress는 hold하고 infrastructure queue는 계속 처리한다. Immutable root를 24시간 retention으로 먼저 저장하고 CRC32C를 검증한 뒤 authority reference·inventory digest를 publish한다. CAS conflict는 orphan root를 정리하고 frozen→held 순서로 source admission을 복원한다. 응답 유실은 authority read로 reconcile하며 publication이 불명확하면 root와 seal을 보존해 recovery가 이어지게 한다. Published root missing·checksum mismatch·inventory mismatch는 source rollback 없이 `data_lost` terminal로 분류하고, valid root는 새 runtime에 queue·timer와 exact generation으로 복원한다. 이번 slice에서 host-wide coordinator를 연결했다. Retire preflight는 create·membership·close를 하나의 structural inventory barrier로 직렬화하되 기존 application queue는 unit seal까지 계속 수락하며, provider가 반환한 unit set이 canonical inventory와 exact match일 때만 `Retiring`을 publish한다. Concurrent Retire waiter는 같은 attempt를 사용하고 preflight 중 Shutdown이 admission seal을 먼저 claim하면 모든 waiter가 `EffectiveIntent=Shutdown` 결과에 합류한다. Blocked Retire는 structural barrier를 해제하고 `Serving`과 normal admission을 복원하며 host terminal result로 저장하지 않는다. Raw public host의 start 전 단일 provider-set seam은 Location authority, aggregate CAS, Relocation payload와 eligible-target preflight capability를 분리 등록하고 start·close lifecycle hook을 host terminal observer에 연결한다. User Spot과 seal 시점의 member Actor는 한 aggregate token으로 함께 freeze하고 immutable aggregate root 저장 뒤 authority prepare·commit 한 번으로 owner와 membership participant를 전환한다. STREAM registry는 accepted inbound completion을 추적하고 Actor relocation barrier 뒤 binding generation을 교체해 stale packet을 거부하며 Shutdown의 host-wide session seal도 같은 barrier를 사용한다. M6C focused contract는 기존 5개에 all-or-none blocker rollback, User Spot 2-participant aggregate·STREAM fence, Retire/Shutdown first-intent race와 post-commit ForceStopped teardown을 추가했고 race를 100회 반복해 모두 통과했다. Raw port·wire codec·operation registry와 M6A~C focused/internal/resource/protocol 6/6, 전체 `zlink_framework` compile도 통과했다. 높은 CPU 부하 구간의 combined run에서는 기존 M6A·M6B raw receive가 반복적으로 timeout성 오류를 냈다. 변경 대상이 아닌 동일 binary의 isolated repeat는 통과했고 clean 6/6 run도 확보했으므로 별도 반복성 이슈를 유지한다. 남은 gap은 public `app_t::retire/shutdown` async facade와 waiter cancellation/deadline, descriptor `Retiring/Draining` publication 및 topology teardown, exact public Location·Relocation provider adapter, target reservation·factory·Restore, aggregate crash recovery·accepted journal/replay·relay ACK이다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. |
| `V11-M6C-DN` | .NET maintenance·monitoring·ASP.NET | .NET lane, `P-DEEP` | `V11-R5B` | 진행 | CAS·lease·Task race·terminal observation·ASP.NET shutdown contract 통과 | `IZLinkFrameworkRuntime`의 host-wide state·snapshot·bounded observer·`RetireAsync`·`ShutdownAsync`와 ASP.NET hosting stop 연결을 유지한다. Retire preflight blocker는 admission seal 전에 `Serving`을 유지하고, Shutdown과 Retire는 first effective intent의 shared operation에 합류하며 waiter cancellation은 shared operation을 취소하지 않는다. Target .NET 계약의 `IZLinkAuthorityStore`, `IZLinkRelocationStore`, Actor·Spot relocation adapter와 policy, 분리된 `AddRelocationStore`를 추가했다. User·Instance Spot과 Actor participant를 함께 담는 deterministic root는 application state, accepted queue sequence·payload와 logical timer cursor·payload를 보존한다. Root를 24시간 retention으로 먼저 저장하고 CRC32C·immutable read를 검증한 뒤 single authority CAS 또는 aggregate prepare·commit으로 publish한다. CAS conflict와 prepare reject는 orphan을 정리하고, commit outcome exception은 authority reference를 읽어 reconcile하며 published root missing·checksum·inventory mismatch는 rollback하지 않는 data-loss로 분류한다. Managed Spot·Actor outbound frame은 object generation이나 상수 `1`을 authority fence로 쓰지 않고 Location row에서 관찰한 `AuthorityOwnerGeneration`만 사용하며 inbound는 local actual generation과 일치할 때만 dispatch한다. In-memory와 Redis Location row는 owner claim generation을 이 field로 materialize한다. 이번 slice에서 `IZLinkLocationStore`가 `IZLinkAuthorityStore`를 직접 상속하도록 연결하고 in-memory provider에 exact read·CAS·snapshot scan·reservation·aggregate state를 추가했다. 공식 Redis package에는 Location과 별도 options·connection lifecycle을 가진 `ZLinkRedisRelocationStore`를 추가했다. Relocation payload는 SHA-256 reference와 CRC32C를 사용하고 Redis `TIME` 기준 retention으로 저장·renew·read·delete한다. `ZLinkRedisLocationStore`는 같은 namespace에서 authority Preserve/Delete CAS, 1분 snapshot scan, reservation과 bounded aggregate prepare·commit·abort를 server-side script로 실행한다. Spot serial queue는 application closure를 accepted sequence·immutable payload·local executor로 분리했다. Turn boundary seal은 pending application record를 capture하고 이후 record를 hold하며 infrastructure continuation은 계속 실행한다. Abort는 infrastructure 뒤 captured→held 순서로 복원하고 commit은 source resource를 해제한 뒤 held record를 relay 입력으로 반환한다. 실제 Spot route ingress는 source RID·Spot RID·request sequence·metadata·message parts를 bounded accepted-journal record로 저장한다. Timer pump는 native handle 대신 registration, delivery·scheduled cursor, next due와 pending tick을 소유한다. Source freeze·abort resume와 target의 새 pump restore를 deterministic logical timer payload로 연결했고 freeze 뒤 timer handler가 실행되지 않도록 admission을 막는다. 정식 `IZLinkMeshObjectServerBuilder`의 stable type·placement·relocation policy 등록을 현재 MeshNode builder가 직접 구현하고 Snapshot adapter type 검증, DI 등록과 typed Capture·Restore invoker를 같은 registration record에 연결했다. User Spot capture는 같은 serial boundary에서 Spot과 canonical member Actor state를 adapter policy에 따라 수집한다. Serial focused 21/21, timer lifecycle 7/7, relocation runtime 12/12, ASP.NET·Framework build warning·error 0이 통과했다. 전체 unit은 709건 중 runtime 702건이 통과했고 실패 7건은 이동 안내 문서와 비활성 E2E fixture를 기대하는 기존 documentation regression이다. Redis 기존 회귀 8건 중 `IReadOnlySet<string>` codec 원인 6건을 수정했고 34/36이 통과했다. 남은 2건은 target fixture의 새 descriptor·authority HASH와 이전 test codec 비교가 충돌하는 contract migration gap이다. `ZLinkAuthorityMutation.Put`의 `NewOwner`·`NewObject`에는 opaque payload와 별도로 target owner token이 필요하다. 이 token을 정식 계약과 provider에 반영하고 in-memory payload decode 우회를 제거하는 작업이 진행 중이다. 실제 Retire scheduler의 target reservation·factory·Restore-before-commit·accepted reply relay ACK·STREAM fence와 aggregate completion이 아직 기존 network Actor drain을 대체하지 않았으므로 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거로 사용하지 않는다. Core·bindings와 Sample·E2E source 변경·test 실행은 0이다. 검증 중 solution 전체 build를 한 번 잘못 호출해 Sample·E2E project compile graph가 시작됐으나 package downgrade에서 중단됐고, 이후 검증은 Framework·ASP.NET과 UnitTests project로 한정했다. |
| `V11-M6C-JVM` | JVM maintenance·monitoring·Spring | JVM lane, `P-DEEP` | `V11-R5B` | 진행 | Java·Kotlin lifecycle·coroutine·Spring metadata와 shutdown contract 통과 | Host-wide `retire`·`shutdown`, runtime state·termination result·observer, 기본 64 unit·256 MiB scheduler, accepted journal queue, logical timer freeze/restore, immutable Relocation Store와 authority publication coordinator를 유지한다. `ZLinkLocationStore`가 `ZLinkAuthorityStore`를 직접 상속하며 runtime은 같은 provider를 별도 fake나 internal port 없이 authority service로 노출한다. In-memory provider도 read·`PRESERVE`/delete CAS·scan·reservation·aggregate 상태를 구현했고 공식 Redis provider의 중복 authority 선언은 제거했다. Exact object role builder와 stable type·placement·explicit relocation policy 등록 표면을 추가했다. Snapshot policy는 Actor와 Spot adapter의 generic 대상 type을 socket 생성 전 검증하며 runtime adapter registry가 stable type을 실제 adapter instance와 capture/restore 호출에 연결한다. Client·Server object role은 Location Store가 필수이고 Recreate·Snapshot policy는 Relocation Store가 필수다. Shutdown은 기존 Actor network handoff를 실행하지 않는다. Actor를 먼저 정리한 뒤 User Spot과 Entry Spot의 `onClosing`에 `HOST_SHUTDOWN`과 deadline을 전달한다. Retire에 active Actor·User Spot이 있으나 새 owner token을 publish할 수 없는 현재 계약에서는 admission seal 전에 `RELOCATION_FAILED` 또는 `RELOCATION_DISABLED`로 종료해 기존 handoff나 payload parse로 우회하지 않는다. Java core 417/417, Spring starter 33/33, Kotlin compile, Redis provider 18건 중 환경 의존 7건 skip·실패 0이 통과했다. 남은 계약 공백은 `ZLinkAuthorityPut`의 `NEW_OWNER`·`NEW_OBJECT`가 target owner token을 전달하지 못하는 점과 aggregate exact owner lease claim/read·capacity fence다. 최소 수정안은 Put에 transition별로 검증하는 optional target owner를 추가하고 Actor 단위 `NEW_OWNER` CAS에 사용하며, 이미 target owner가 있는 aggregate·reservation은 그대로 유지하는 것이다. 이 계약이 확정되기 전에는 User Spot aggregate capture/restore, target replay-before-commit과 실제 Retire relocation을 연결하지 않는다. Spring construction/start 분리도 남아 있다. 아직 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거가 아니며 Sample·E2E·Core·bindings 변경·실행은 0이다. |
| `V11-M6C-NODE` | Node maintenance·monitoring·NestJS | Node lane, `P-DEEP` | `V11-R5B` | 진행 | Promise·event-loop recovery·terminal observation·NestJS cleanup contract 통과 | first-intent-wins Retire·Shutdown barrier, mutation 전 preflight, ready-first bounded relocation scheduler(기본 outbound 64·in-flight 256 MiB), deadline 뒤 force-stop, terminal observer와 published root data-loss recovery 분류를 Framework-owned runtime에 구현했다. Relocation payload는 application state·accepted journal·미실행 queue·logical timer를 deterministic envelope로 만들고 immutable Store에 먼저 기록한 뒤 checksum·inventory digest를 검증하여 Location authority의 단일 preserve CAS로 공개한다. CAS conflict가 발생하면 proven orphan만 삭제한다. CAS 응답이 유실되면 authority exact read로 publication 성공을 reconcile하고, expected version이 유지된 것이 확인될 때만 orphan을 삭제한다. 결과가 불명확하면 retention이 정리하도록 root를 보존한다. Authority reference 해제 뒤 payload 삭제, published payload missing·checksum·inventory mismatch의 non-rollback `RelocationDataLost`를 구현했다. Owner queue는 active claim이 끝난 turn boundary에서 seal하며 기존 미실행 record를 capture하고 이후 ingress를 별도 hold한다. Abort는 captured→held 순서로 admission을 복원하고 commit은 held record만 relay 대상으로 반환하며 infrastructure queue는 계속 진행한다. Spot timer는 native timeout handle을 저장하지 않고 registration option, schedule cursor와 delivery cursor를 freeze하며 target의 동일 registration에 logical schedule을 복원한다. 기존 NestJS host는 application shutdown에서 30초 bounded RouteMesh drain 뒤 idempotent stop을 수행한다. Public `ZLinkLocationStore`와 `ZLinkRelocationStore` 및 Framework·NestJS의 `addLocationStore`·`addRelocationStore` 등록 표면을 각각 분리했다. Redis 전용 또는 두 Store를 묶는 등록 API는 추가하지 않았다. `ZLinkChannelClient`는 global channel name과 classic channel transport를 사용하고 `ZLinkRouteClient`는 globally unique Mesh channel의 MeshName을 내부에서 결정하도록 exact contract에 맞췄다. Raw-only bindings에서 제거된 `createMeshNode`를 요구하던 stale parity test를 제거했고, M6 runtime protocol graph에서 Bingo sample generator를 실행하던 test를 제외했다. Connector protocol test는 각 instance를 명시적으로 close한다. Candidate 41 files에 대한 `M6-RUNTIME` 7 commands, public declaration 32/32, M6C 10/10, protocol 14/14와 Framework TypeScript compile이 통과했다. 별도 Store public declaration regression 26/26과 Framework·NestJS registration 검증도 통과했다. 증거: `.artifacts/v11/evidence/V11-M6C-NODE/result.json`. Durable coordinator를 Spot·Actor restore owner와 session route replacement, multi-mesh NestJS aggregate drain에 연결하는 작업을 계속한다. Sample·E2E source 변경·실행은 0이다. |
| `V11-R5C` | Maintenance runtime slice 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE` | 대기 | lifecycle·authority·handover·recovery·observability·hosting과 실행 격리의 I1·I2·I3 review clean | C++ candidate의 host first-intent barrier·aggregate CAS·STREAM fence 증거와 JVM candidate의 accepted journal, immutable payload publication, Location authority 상속, exact adapter registration/runtime mapping과 shutdown lifecycle 연결을 확인했다. JVM은 기존 network Actor handoff를 제거했지만 `ZLinkAuthorityPut` target owner와 exact owner lease·capacity 계약이 확정되지 않아 active workload Retire를 seal 전에 block한다. 따라서 aggregate capture/restore와 target replay를 연결하기 전에는 review candidate로 승격하지 않는다. 네 언어 M6C candidate와 독립 reviewer가 모두 준비되기 전에는 이 row를 시작하거나 clean으로 판정하지 않는다. |
| `V11-M6-SCAFFOLD-ZERO` | Production placeholder 제거와 runtime 완료 gate | inventory·contract lane, `P-SCAN` | `V11-R5C` | 대기 | production scaffold branch·`RuntimeNotReady` placeholder·fake data 0, 모든 runtime regression 통과, sample·E2E source 삭제·임시 우회 0 | — |

`V11-M6C-CPP` 추가 증거(2026-07-23): 설치 public header에 authority read·CAS·scan, object
reservation·aggregate와 분리된 Relocation Store 계약을 추가하고
`add_relocation_store(...)`를 기존 Location Store 등록과 분리했다. Public Relocation Store task를
M6C immutable payload port에 연결하는 adapter는 payload bytes, CRC32C, missing과 delete 결과를 보존한다.
`app_t::retire(...)`와 `shutdown(...)`은 first intent와 deadline을 공유하고 각
`wait_cancellation`만 취소한다. 실행 중인 host에서는 hosted-service teardown이 끝난 뒤 terminal result를
완료하며 signal, `stop()`과 `request_stop()`도 같은 Shutdown operation을 시작한다.
`test_cpp_framework_m6a_runtime`, `test_cpp_framework_m6b_runtime`,
`test_cpp_framework_m6c_runtime`, `test_cpp_framework_termination_facade` 4/4와 전체
`zlink_framework` compile이 통과했다. 이어서 `location_store_t`가 authority와 object creation
provider를 직접 상속하도록 연결하고 in-memory provider에 exact authority read·CAS·snapshot scan,
reservation과 aggregate prepare·commit·abort를 구현했다. `Preserve`는 owner metadata를 그대로 유지하고
`NewOwner`는 payload를 해석하지 않은 채 Framework가 전달한 exact target owner token을 같은
critical section에서 검증한다. Missing 생성은 `Reserve`만 수행한다. Reservation의 Creating payload와 commit의 Ready payload도 provider가
합성하지 않고 exact bytes를 저장한다. Public authority adapter는 Framework가 소유한 relocation publication
payload만 encode·decode하고 provider payload는 해석하지 않는다. Target preflight가 반환한 owner token은
standalone relocation CAS까지 전달한다. `CA-D46`에 따라 existing object relocation 전용 capacity
fence와 provider를 public header에 추가하고 `location_store_t`가 이 capability를 직접 상속하게 했다.
`NewOwner` CAS는 exact target owner와 relocation capacity fence를 함께 요구하며, 성공한 CAS만 source active와
target pending-to-active를 전환한다. Aggregate prepare는 `NewOwner` participant와 capacity fence를 authority
key 기준 exact 1:1로 검증하고 missing·duplicate·extra fence를 mutation 0인 conflict로 끝낸다. Target preflight와
standalone·aggregate runtime seam도 creation reservation이 아닌 전용 fence를 전달한다. 기존 Location resolver
test double도 새 provider 계약을 위임하도록
갱신했으며 generated protocol include 누락을 바로잡았다. 변경 뒤
`test_cpp_framework_m6c_runtime`, `test_cpp_framework_in_memory_location_store`,
`test_cpp_framework_termination_facade`, `test_cpp_framework_store_location_resolvers` 4/4와 전체
`zlink_framework` compile이 통과했다. 공식 Redis Location provider의 exact authority·reservation 구현,
owner lease claim/read/renew/release, descriptor capacity topology와 실제 target factory·Restore-before-commit
wire dispatch는 남아 있으므로 row 상태와 `V11-R5C`는 계속 `진행`과 `대기`다. Core·bindings와
Sample·E2E source를 변경하거나 실행하지 않았다.

JVM lane은 `CA-D44`~`CA-D46`의 exact provider 계약을 Java runtime과 Kotlin coroutine bridge에
반영했다. `ZLinkAuthorityPut`은 transition별 target owner와 relocation capacity fence 조합을 생성자에서
검증하며 snapshot과 stored 결과는 owner ID·lease generation과 provider-owned placement allocation을
반환한다. Allocation은 Pending·Active 상태, object kind·stable type, descriptor key·lifecycle generation과
capacity delta를 함께 보존한다. Public `NEW_OBJECT` transition과 Missing CAS expectation은 제거했으며 generic
reserve만 Missing에서 Pending을 만들고 commit만 Active로 전환한다. Owner lease는
claim·read·renew·release의 exact token 계약으로 바꾸고 in-memory와 Redis script가 provider의 같은
critical section 또는 Redis transaction에서 lease generation과 expiry를 검증한다. Object creation
reservation은 application이 전달한 Pending payload를 그대로 저장하고 commit에서 Active payload로
교체한다. Commit은 target descriptor·owner lease가 current인지 다시 검증하고, abort는 current liveness와
관계없이 exact Pending reservation을 정리한다. Existing object relocation은 별도 capacity reservation과
fence를 사용하며 `NEW_OWNER`
standalone CAS는 fence가 reserved 상태이고 reservation의 authority key·expected StoreVersion·source owner·target owner가
current Active allocation과 exact match일 때만 CAS를 적용한다. Source owner lease와 descriptor의 current
liveness는 요구하지 않고 target descriptor·owner lease만 reserve, prepare와 commit 직전에 검증한다.
Committed·aborted·stale·mismatch fence는 authority와 capacity mutation 0인 conflict로 끝낸다. In-memory
provider는 creation·delete·relocation·aggregate abort/commit의 pending·active capacity delta를 authority
mutation과 같은 lock에서 갱신한다. Aggregate prepare는 `NEW_OWNER` participant와
capacity fence를 authority key 기준 exact 1:1로 검증하고 missing·duplicate·extra fence를 mutation 0인
conflict로 끝낸다. Reserved fence는 exact `(aggregateId, aggregateGeneration)`에 Prepared 상태로 연결한다.
연결 뒤 direct abort는 stale이고 다른 aggregate의 prepare는 conflict며, exact duplicate만 already-prepared다.
Commit이 target stale로 실패해도 binding과 authority를 유지하고 aggregate abort만 fence와 pending delta를
해제한다. Java·Kotlin exact public inventory, Kotlin의 public capacity bridge와
`ZLinkSuspendingLocationStore` suspend hook도 같은 계약으로 맞췄다.

Creation request와 reservation은 target descriptor lifecycle generation을, relocation capacity request는
source·target descriptor lifecycle generation을 exact field로 보존한다. Commit과 abort는 호출자가 돌려준
reservation이 reserve 당시 record 및 Pending allocation과 정확히 일치하는지 다시 확인한다. Aggregate prepare와
commit도 capacity reservation record의 authority version·current source owner·source Active allocation·aggregate
target owner와 target descriptor·lease를 mutation 전에 다시 검증한다.

검증은 Java core 424/424, Spring starter 33/33, Kotlin 43/43과 Redis provider 18건 중 환경 의존
7건 skip·실패 0으로
통과했다. JVM Redis provider에는 exact MeshNode target capacity descriptor row와 target scheduler/factory가 아직
없으므로 capacity reserve는 `TargetUnavailable`로 끝내며 Redis aggregate도 성공 상태를 합성하지 않는다.
따라서 실제 Retire relocation은 admission seal 전에 계속 차단하고 `V11-M6C-JVM`은 `진행`,
`V11-R5C`는 `대기`로 유지한다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지 않았다.

M6C provider parity checkpoint(2026-07-23)에서 public authority CAS의 Missing expectation과
`NewObject` transition을 제거하고, Missing 생성은 generic reservation만 수행하도록 네 언어 계약과
provider를 맞췄다. C++은 descriptor·capacity fence·aggregate와 StoreRevision exhaustion 회귀에서
in-memory 10/10, Redis 16/16이 통과했고 외부 cross-language 환경 2건만 skip했다. .NET은 exact
MeshNode descriptor CAS, provider-owned capacity projection과 owner generation 전달을 보강해 Redis
44/44가 통과했고 같은 환경 의존 2건만 skip했다. JVM은 descriptor immutable limit, aggregate membership
index, reservation terminal idempotency와 fence 순서 독립성을 보강해 core 428/428, Kotlin 43/43,
실제 Redis 21/21이 통과했고 외부 harness 2건만 skip했다. Node는 exact descriptor, creation·relocation·
aggregate state machine과 capacity projection을 구현해 workspace typecheck, M6C 15/15와 실제 Redis
focused scenario 1/1이 통과했다. 네 lane 모두 `git diff --check`가 통과했다.

독립 audit에서 official Redis authority의 물리 schema가 C++·JVM의 per-authority HASH, .NET의
property별 global HASH, Node의 단일 serialized state로 달라 같은 Redis transaction domain을 공유할 수
없음을 확인했다. Node 방식은 mutation과 scan 비용이 전체 authority cardinality에 비례하므로 production
후보에서 제외한다. Per-authority와 global HASH의 장점을 조합한 hybrid schema까지 포함해
snapshot-consistent bounded scan, Redis Cluster same-slot, 1024-participant aggregate, migration과 hot-key
비용을 독립 review하고 있다. Schema version gate와 공통 physical fixture를 확정해 네 provider를 통일하기
전까지 `V11-M6C-*`는 `진행`, `V11-R5C`는 `대기`로 유지한다. Core·bindings와 Sample·E2E source는
변경하거나 실행하지 않았다.

Hybrid schema implementation checkpoint(2026-07-23)에서 공통 spec과 authority·MeshNode fixture에
literal hash tag, exact current HASH 13개 field, canonical `objectKind`, UTF-8 byte length-prefix capacity
bucket, owner lease 3-field HASH, descriptor·owner-token index, revision-prefixed history·tombstone와 durable
watermark scan을 고정했다. Descriptor immutable digest도 `CA-D54`의 canonical preimage와 fixture vector로
고정했다. Node는 workspace typecheck와 실제 Redis 9/9가 통과했다. JVM은 실제 Redis 27건 중 25건이
통과했고 외부 cross-language 환경 2건만 skip했다. C++은 exact descriptor admission과 aggregate
prepare·commit을 공통 physical schema에 맞췄고 실제 Redis 20건 중 18건이 통과했으며 외부 환경 2건만
skip했다. .NET은 alias·property-map과 legacy scan을 제거하고 schema `KEYS[19]`, scan `KEYS[20]`,
candidate row `KEYS[21...]`인 unified authority script를 직접 사용한다. Provider build는 warning·error
0이고 실제 Redis 49건 중 47건이 통과했으며 외부 harness 2건만 skip했다. 네 provider의 canonical digest
fixture, explicit Lua `KEYS`, exact field·bucket·index 검증과 `git diff --check`가 통과했다. 실제
shared-prefix writer→다른 언어 reader 검증이 끝나기 전까지 `V11-M6C-*`는 `진행`, `V11-R5C`는 `대기`로
유지한다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

Topology contract audit checkpoint(2026-07-23)에서 Automatic RouteMesh는 canonical RID가 작은 쪽만
connect를 시작하고 Manual 양방향 후보와 automatic 경합만 duplicate-pipe admission으로 정리하도록 공통
spec을 수정했다. ClientServer는 Client만 server별 `(RID, lifecycle generation)` intent를 만들고, classic
fanout은 Subscriber만 publisher별 intent를 만들며 automatic·manual subscriber 혼합 등록은 startup
configuration error로 고정했다. .NET planner의 연결 방향과 RouteMesh pairwise ordering은 계약과
일치했다. 다섯 언어 exact interface에는 RouteMesh initiator와 duplicate admission을 직접 고정했고,
ClientServer와 fanout의 비대칭 연결 방향도 공통 계약과 일치시켰다. Initial source audit에서는
C++·JVM·Node intent key의 lifecycle 누락, 세 언어 fanout 혼합 설정의 삭제·병합 처리와 .NET·Node
ClientServer public/runtime surface 누락을 확인했다. 아래 correction checkpoint가 앞의 두 gap을 닫으며,
dedicated ClientServer surface와 언어별 전체 regression은 계속 완료 조건으로 남는다.

Topology source correction checkpoint(2026-07-23)에서 C++ fanout connection intent를 Publisher RID와
lifecycle generation의 tuple로 바꾸고 같은 RID의 새 lifecycle에서 socket과 readiness fence를 교체했다.
C++ M6A runtime과 mixed-mode startup focused test가 통과했다. JVM planner도 role·RID 또는 endpoint와
lifecycle generation을 intent key로 사용하고 manual peer handover에서 stable peer identity로 이전 lifecycle을
교체한다. No-arg subscriber만 automatic mode를 선택하며 manual endpoint와 섞으면 startup error로 끝낸다.
Client와 fanout Subscriber는 descriptor를 게시하지 않고 Server와 Publisher만 게시하도록 role별 source를
분리했다. Descriptor lifecycle은 Store CAS generation과 분리한 nonzero token으로 유지한다. Java core,
Kotlin과 Redis의 Gradle 22 task가 통과했다. 별도 `contractTest` source 두 곳은 제거된
`renewOwnerLease(String, RoutingId, Duration)`를 호출하는 기존 signature gap으로 분리했다. Node planner와
SpotNode executor도 lifecycle generation을 intent와 disconnected 판정에 포함했고 Framework·NestJS builder가
automatic·manual subscriber 혼합을 양쪽 호출 순서에서 거부한다.

Dedicated ClientServer source checkpoint(2026-07-23)에서 .NET과 Node에 role을 정확히 한 번 선택하는
Client·Server builder를 추가하고 process-local unique ChannelName만 받는 send·request 표면을 연결했다.
.NET은 Client 전용 DEALER와 Server 전용 ROUTER, manual endpoint, typed handler dispatch, protocol error
reply와 request terminal 처리를 구현했다. 실제 두 runtime의 send와 request/reply, role 검증과 동일
RID·endpoint의 새 lifecycle에서 disconnect·reconnect readiness reset을 포함한 focused 5/5가 통과했고
Framework build는 warning·error 0이다. Node Framework·NestJS는 Client manual connect, Server bind·actual
port advertise, weight와 manual·group handler 조합을 구현했다. 전용 ClientServer descriptor·key·Store
contract와 in-memory owner lease·lifecycle·revision fence, Server Serving publish→Draining→remove,
automatic Client의 `(ChannelName, Server RID, lifecycle)` intent를 연결했다. Same endpoint/new lifecycle도
transport readiness를 reset하고 polling 오류는 Location runtime status·metric에 기록한다. Full build,
workspace typecheck, M6A runtime 6/6, 관련 contract 48/48와 추가 focused 7/7이 통과했다. 두 언어의 공식
Redis provider operation과 handshake·security admission 뒤 ready 승격, descriptor weight의 outbound 선택
연결은 계속 진행한다. Sample·E2E와 Core·bindings는 변경하거나 실행하지 않았다. 이 checkpoint는
`V11-M6A-DN`·`V11-M6A-NODE`를 완료로 판정하지 않는다.

ClientServer Redis provider checkpoint(2026-07-23)에서 .NET과 Node 공식 Location provider에
`ChannelName`과 Server RID로 만든 key, owner lease·lifecycle·revision·immutable field CAS, 만료된 owner의
takeover, exact owner token cleanup을 구현했다. Descriptor page는 channel별 정렬 index에서 필요한 candidate만
읽으며 기본 100개, 요청 최대 1000개와 encoded 4 MiB 제한을 함께 적용한다. Node의 전체 index
`SMEMBERS`와 page 생략 시 전체 반환은 제거했다. .NET은 `RemoveAllByOwnerAsync`를 owner ID 문자열이 아닌
`ZLinkLocationOwnerToken`으로 바꾸고 runtime, in-memory와 Redis provider가 같은 lease generation을
검증하도록 맞췄다. .NET provider build는 warning·error 0, 실제 Redis focused 3/3과 전체 50/50이
통과했으며 외부 cross-language harness 2건만 skip했다. Node build·workspace typecheck와 실제 Redis를
포함한 ClientServer runtime/provider 17/17이 통과했다.

독립 contract review에서 공통 Redis fixture의 `NormalizedEffectiveMaxMessageBytes`가 잘못된 것으로 판정했다.
이 값은 Location descriptor가 아니라 실제 physical connection의 service admission에서 교환하는 transport
결과다. 다섯 언어 exact descriptor를 확장하지 않고 fixture와 verifier에서 해당 field를 제거했으며, 정식
ClientServer spec에 이 책임 경계를 명시했다. Transport `ConnectionReady`만으로는 ChannelName, lifecycle과
security identity의 service admission을 증명할 수 없다. Framework service hello와 exact
admit/reject 뒤에만 ready target으로 승격하고 descriptor weight selector를 실제 outbound submit에 연결하는
작업이 남아 있다. 따라서 이 checkpoint도 `V11-M6A-DN`·`V11-M6A-NODE`를 완료로 판정하지 않는다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

.NET ClientServer connection completion checkpoint(2026-07-24)에서 automatic descriptor와 manual
endpoint마다 dedicated DEALER·monitor와 exact `hello`·`admit`·`reject`를 연결하고, positive-weight
`Serving` target의 deterministic weighted selection을 실제 send·request에 사용한다. 같은 Server
RID·lifecycle의 두 source는 하나의 physical connection을 공유하며 source 제거 순서와 lifecycle successor
admission을 reference·physical generation·attempt token으로 fence한다. Client와 Server는 5초 probe·15초
deadline, exact outstanding ACK, server-pushed higher-revision update와 malformed control 뒤 reconnect
readmission을 구현한다. TCP endpoint의 disconnect 뒤 즉시 reconnect하지 않고 25ms asynchronous handover를
사용하며 중복 reconnect와 disposal을 같은 tracked task로 직렬화한다.

이 검증에서 public DEALER `Request→Reply` 뒤 ROUTER가 보낸 unsolicited raw frame을 `Recv`가 받지 못하는
Core 결함을 재현했다. Request dispatcher는 frame을 internal queue로 옮겼지만 DEALER part receive는 native
pipe만 읽고 있었다. Dispatcher 설치 전에 queue를 준비하고 활성 dispatcher가 있으면 DEALER receive와
multipart continuation이 그 queue를 사용하도록 Core를 최소 수정했으며 .NET binding에 TCP
request·unsolicited receive·disconnect 회귀를 추가했다. Core request/reply 14/14와 관련 CTest 9/9,
binding request/reply 12/12, Framework ClientServer 12/12 clean exit, Framework build warning·error 0,
`git diff --check`가 통과했다. Framework 전체 unit 731건 중 runtime 724건이 통과했고 실패 7건은 기존
documentation regression의 ledger·E2E ID·public symbol 문자열 불일치다. Sample·E2E source는 변경하거나
실행하지 않았다. 새 Core candidate와 독립 review provenance를 만든 뒤 official Core·.NET local package를
11.0.0 위치에 다시 배포해야 하며, 다른 M6A·public-contract parity 잔여가 있으므로 `V11-M6A-DN` 상태는
계속 `수정 진행`으로 유지한다.

Node ClientServer service admission checkpoint(2026-07-24)에서 automatic descriptor lifecycle마다
전용 DEALER와 monitor를 만들고 transport identity 확인 뒤 Framework `hello`와 exact `admit`·`reject`를
교환하도록 연결했다. Admission 전 connection은 outbound target에 포함하지 않으며, admitted
positive-weight target만 기존 weighted selector가 실제 send·request마다 선택한다. 같은 Server RID의 새
lifecycle이 ready가 되면 이전 lifecycle connection을 제거하고, 종료되었거나 오래된 connection의 event와
reply는 connection ID fence를 통과하지 못한다. Server의 reserved service control frame은 application
handler에 전달하지 않는다. Framework build·workspace typecheck, M6A runtime 6/6, ClientServer와 Redis
focused 20/20, changed-source ESLint와 `git diff --check`가 통과했다. Manual endpoint-only connection의
동일 service admission, liveness probe·ACK와 server-pushed descriptor update는 남아 있으므로
`V11-M6A-NODE`를 완료로 판정하지 않는다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

Node ClientServer connection completion checkpoint(2026-07-24)에서 manual endpoint마다 shared socket이
아닌 전용 DEALER와 monitor를 만들고 transport reconnect마다 새 `hello`·`admit`을 수행하도록 연결했다.
Automatic과 manual source가 같은 Server RID·lifecycle을 승인하면 하나의 ready target만 유지한다. 이전
physical connection의 늦은 admission callback은 physical token과 attempt token으로 차단하며 새 connection은
이전 callback 완료를 기다리지 않는다. Client와 Server는 application traffic과 무관하게 5초마다
`livenessProbe`를 보내고 current physical connection의 outstanding ID와 일치하는 `livenessAck`만 15초
deadline을 갱신한다. Server가 보낸 더 큰 revision의 descriptor update는 immutable identity가 일치할 때만
weight·state를 교체하며 reserved control은 application dispatch에 전달하지 않는다. Framework build,
workspace typecheck, changed-source ESLint, focused ClientServer 15/15, M6A 6/6과 `git diff --check`가
통과했다. 기존 `channel-client` 전체 runner의 실패는 admission 뒤 packet name을 요구하는 별도 current
contract migration으로 분리했고 timeout이 남긴 자식 process는 정리했다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로
`V11-M6A-NODE` 상태는 계속 `수정 진행`으로 유지한다.

Node ClientServer physical ownership follow-up checkpoint(2026-07-24)에서 automatic과 manual source가
같은 Server RID·lifecycle을 승인하면 ready target만 중복 제거하는 대신 실제 DEALER와 monitor 하나를
ref/alias로 공유하도록 보완했다. 어느 source를 먼저 제거해도 남은 alias가 physical connection과
discovery readiness를 유지하며 마지막 alias에서만 resource를 닫는다. Monitor termination은 physical
readiness를 먼저 제거한 뒤 source별 callback을 호출해 stale alias가 ready 상태를 다시 게시하지 못하게
한다. Server peer가 15초 deadline을 넘으면 ROUTER의 `disconnectPeer`로 physical connection을 닫고,
probe send가 backpressure를 반환해도 deadline 전에는 연결 종료로 판정하지 않으며 5초 뒤 같은 probe ID를
재전송한다. Workspace typecheck·build, focused ClientServer 16/16, M6A 6/6, changed-source ESLint와
`git diff --check`가 통과했다. 전체 Node gate는 병렬 Actor runtime의 별도 public method gap 2건에서
중단됐으며 이 focused 변경의 실패는 없다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지
않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로 `V11-M6A-NODE` 상태는 계속 `수정 진행`으로
유지한다.

Node dedicated fanout checkpoint(2026-07-24)에서 generic peer descriptor와 shared SUB 경로를 제거하고
exact `ZLinkFanoutPublisherDescriptor`·key·`ZLinkFanoutLocationStore`를 public contract, in-memory와 공식
Redis provider에 연결했다. Store는 lifecycle·revision·immutable identity·owner token fence, exact owner
cleanup과 bounded page를 구현하며 공식 fixture의 key·row와 일치한다. Publisher는 실제 bind endpoint를
전용 descriptor에 게시하고 5초마다 reserved beacon을 보낸다. Automatic subscriber는 같은 ChannelName의
publisher RID·lifecycle마다 SUB와 monitor 하나를 소유하며 first valid beacon/application payload 뒤에만
ready가 되고 15초 deadline은 해당 publisher connection만 교체한다. Reserved beacon은 application
dispatch에 전달하지 않는다. Protocol error와 deadline 뒤에는 새 physical SUB를 만들며 target identity와
controller attempt를 함께 fence해 늦은 termination이 successor를 닫거나 제거된 descriptor를 다시 열지
못한다. Manual endpoint별 SUB 경로는 유지하고 automatic과 함께 구성하면 startup에서 거부한다. Workspace
typecheck·build, focused fanout 7/7 clean exit, 실제 Redis fanout fixture/fence, ClientServer 16/16,
M6A 6/6, changed-source ESLint와 `git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를
변경하거나 실행하지 않았다. 다른 M6A·review 잔여가 있으므로 `V11-M6A-NODE` 상태는 계속
`수정 진행`으로 유지한다.

JVM ClientServer provider checkpoint(2026-07-24)에서 Java public source에 exact
`ZLinkClientServerServerDescriptor`, key와 선택 capability인 `ZLinkClientServerLocationStore`를 추가하고
공식 Redis provider가 이를 직접 구현하도록 연결했다. Redis row는 fixture와 같은 field 순서·상태 이름을
사용하며 ChannelName과 Server RID key, owner lease·lifecycle·revision·immutable field fence, channel별
정렬 index, 기본 100개·최대 1000개·encoded 4 MiB page와 exact remove를 구현한다.
`removeAllByOwner`는 Java·Kotlin public source, in-memory, runtime과 Redis에서 owner ID 문자열 대신 exact
`ZLinkLocationOwnerToken`을 사용하고 lease generation·expiry를 확인한다. 제거된 예전
`renewOwnerLease(String, RoutingId, Duration)`를 호출하던 JVM contract test도 current claim contract로
수정했다. Java core compile·test와 focused Location contract, Kotlin test가 통과했다. 실제 Redis는
ClientServer fixture·operation을 포함해 25/25가 통과했고 외부 cross-language harness 2건만 skip했다.
현재 JVM automatic ClientServer runtime은 아직 generic peer enumeration을 사용하므로 dedicated Store
publication·discovery와 service admission으로 교체하기 전에는 `V11-M6A-JVM`을 완료로 판정하지 않는다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

JVM ClientServer runtime checkpoint(2026-07-24)에서 generic `ZLinkPeerLocation` ClientServer loop를
제거하고 dedicated `ZLinkClientServerLocationStore` publication·bounded list·reconcile로 교체했다.
Automatic descriptor lifecycle과 manual endpoint마다 전용 DEALER를 만들고 exact
`hello`·`admit`·`reject` 뒤에만 ready target으로 승격한다. RID는 service wire의 opaque `bytes8`을
보존하며 human-readable `toString()`이 같은 서로 다른 RID도 raw hex connection key로 구분한다.
Positive-weight `Serving` target만 deterministic weighted selector가 실제 send·request에 사용하고, 같은
RID의 새 lifecycle은 admission 성공 전까지 이전 ready connection을 유지한 뒤 connection ID fence로 늦은
callback을 차단한다. Reserved control frame은 application dispatch에 전달하지 않는다. Host 내부
configuration dependency가 owner token supplier와 start·drain·stop lifecycle을 연결하며 application이
호출할 새 public ClientServer API나 service locator는 추가하지 않았다. JVM core assemble과 unit
436/436, 신규 focused 6/6, `JavaTargetContractGapTest`와 `git diff --check`가 통과했다.
`livenessProbe`·`livenessAck`, server-pushed descriptor update와 manual endpoint reconnect retry는 남아
있으므로 `V11-M6A-JVM`을 완료로 판정하지 않는다. Core·bindings와 Sample·E2E source는 변경하거나
실행하지 않았다.

JVM ClientServer connection completion checkpoint(2026-07-24)에서 manual endpoint와 automatic
descriptor의 physical lifecycle을 monitor event와 admission attempt token으로 fence했다. 같은
Server RID·lifecycle을 가리키는 두 source는 ref/alias로 실제 DEALER와 monitor 하나만 공유하며 어느
source를 먼저 제거해도 남은 source가 ready connection을 유지하고 마지막 alias에서만 resource를
닫는다. Client와 Server는 application traffic과 무관하게 5초 probe·15초 deadline을 적용한다.
Outstanding probe가 있으면 같은 ID를 재전송하고 current physical connection의 exact ACK만 deadline을
갱신한다. ROUTER의 peer deadline은 raw `disconnectRid`로 physical connection을 닫으며 send
backpressure는 deadline 전 disconnect 증거로 사용하지 않는다. Server-pushed higher-revision update는
immutable identity가 일치할 때만 적용하고 malformed·unsolicited control은 reconnect 뒤 새 admission을
요구한다. Production shared client fallback도 제거했다. JVM focused ClientServer 11/11, Java core
test·assemble과 Kotlin main·test compile, `git diff --check`가 통과했다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. Dedicated fanout과 다른 M6A 잔여가 있으므로
`V11-M6A-JVM` 상태는 계속 `수정 진행`으로 유지한다.

JVM dedicated fanout checkpoint(2026-07-24)에서 exact
`ZLinkFanoutPublisherDescriptor`와 key, 선택 capability인 `ZLinkFanoutLocationStore`를 public source에
추가하고 in-memory와 공식 Redis provider가 이를 구현하도록 연결했다. Publisher는 raw PUB가 실제 bind한
endpoint를 게시하며 automatic subscriber는 generic peer planner를 사용하지 않고 publisher
RID·lifecycle별 전용 SUB와 monitor를 만든다. Native connection과 최초 valid beacon 또는 application
record를 모두 확인한 뒤 ready로 판정하고, reserved beacon은 application handler에 전달하지 않는다.
5초 beacon·15초 deadline은 publisher connection별로 격리하며 manual publisher·subscriber 경로는
유지한다. 같은 connection ID의 이전 monitor callback은 current connection identity로 fence하고,
pending Store reconcile은 lifecycle epoch를 확인해 stop·close 뒤 connection을 다시 만들지 못한다.
Redis list는 server time과 exact owner ID·lease generation·expiry를 한 번에 검사해 만료되거나 해제된
owner의 descriptor를 반환하지 않는다. 독립 Codex 5.6 sol xhigh review에서 발견한 이 세 P1을 수정한 뒤
재리뷰가 `APPROVE`로 끝났다. JVM core 전체 test·assemble, Redis provider 전체 test, Kotlin main·test
compile과 `git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지
않았다. 다른 M6A·review 잔여가 있으므로 `V11-M6A-JVM` 상태는 계속 `수정 진행`으로 유지한다.

C++ ClientServer runtime checkpoint(2026-07-24)에서 exact descriptor·key·선택
`client_server_location_store_t`와 공식 Redis capability를 추가하고, generic ClientServer peer
publication·list를 dedicated Store runtime으로 교체했다. Discovery Server는 raw ROUTER가 bind한 실제
`last_endpoint`를 exact owner token으로 게시하고, Client는 descriptor lifecycle마다 raw DEALER와
`hello`·`admit` service admission, liveness와 ready fence를 소유한다. Positive-weight `Serving`
connection만 실제 send·request weighted selection에 사용하며 같은 RID의 새 lifecycle이 ready가 된 뒤
이전 connection을 제거한다. Service runtime state는 enum 수치를 cast하지 않고 explicit mapper를 사용해
`Retiring`·`Draining`을 wire `draining`으로 통일했고, 설정되지 않은 message bound는 nonzero
`0x7fffffff`로 정규화했다. Owner lease public surface와 runtime은 legacy owner-ID renew·remove·전체
lease list를 제거하고 `claim(ownerId)→renew(token)→removeAll(token)→release(token)` exact 경로만
사용한다. In-memory와 Redis provider, live reader와 contract test도 같은 계약으로 바꿨다. Focused
M6A raw·contract header·target contract·in-memory·location runtime·resolver·Redis 7/7, resolver
27/27, in-memory 10/10, location runtime 4/4가 통과했다. Redis는 20건이 통과하고 외부 환경 의존
2건만 skip했으며 endpoint·default admission·전체 state mapping 3/3과 `git diff --check`가 통과했다.
Exact 문서에 있는 dedicated fanout descriptor·Store·공식 Redis capability가 C++ source에 아직 없고
generic peer 재사용 여부를 다음 audit에서 닫아야 하므로 `V11-M6A-CPP`를 완료로 판정하지 않는다.
Runtime gate에서 제외한 기존
`framework/languages/cpp/e2e/ObservabilityOps/Server/main.cpp`도 제거된 `list_owner_leases()`를 호출하므로,
E2E source를 다시 활성화하는 단계에서 exact read 기반 관측 시나리오로 migration해야 한다.
Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

C++ dedicated fanout checkpoint(2026-07-24)에서 exact `fanout_publisher_descriptor_t`와 key,
`fanout_location_store_t`를 추가하고 in-memory와 공식 Redis provider가 new claim·renew·higher-lifecycle
takeover, immutable identity·revision fence, exact owner cleanup과 1..1000개·encoded 4 MiB bounded page를
구현하도록 연결했다. Redis page는 store time으로 `updated_at`을 교체한 최종 JSON 크기를 기준으로
4 MiB 경계를 계산하며 공식 fixture의 kind·key·field order·hash를 byte 단위로 검증한다. Runtime은
generic peer publication·reconcile을 사용하지 않고 publisher의 실제 bind endpoint를 전용 Store에
게시한다. Automatic subscriber는 publisher RID·lifecycle별 SUB와 monitor를 소유하고 first
beacon/application payload 뒤에만 ready가 되며, lifecycle 교체와 5초 beacon·15초 deadline은 다른
publisher의 connection 상태를 변경하지 않는다. Manual fanout 경로는 그대로 유지한다. 관련 6개 target
build와 CTest 6/6, 실제 Redis focused 4/4, in-memory 1000-row·4 MiB page, scoped
`git diff --check`가 통과했다. Core·bindings와 Sample·E2E source를 변경하거나 실행하지 않았다.
다른 M6A·review 잔여가 있으므로 `V11-M6A-CPP` 상태는 계속 `수정 진행`으로 유지한다.

Documentation verifier checkpoint(2026-07-23)에서 service wire schema 37 commands·157 types와 186개
negative self-test는 통과했다. 최초 v11-first candidate는 Codex 5.6 sol xhigh review에서 C# semicolon-only
record span, non-export TypeScript brand, computed symbol property와 C++ default argument `{}`의 false
member를 발견해 세 차례 거부됐다. Extractor가 owner span, package-private symbol, 괄호 안 brace를 정확히
구분하도록 수정하고 stale override보다 exact baseline·target overload를 먼저 보존하는 refresh 순서를
추가했다. Parser·stale-first overload 회귀 6건을 추가한 최종 trace는 56 documents, 177 code blocks,
1690 declaration owners, 6496 members이며 reviewed-new member는
`4367 / 40a99f2735193c902343d653457da5c3e038647ca8c8ef6118e744dc7b113d30`, owner는
`1000 / ea545f28a93075107baf897c404706c5278c2bc541eebb15a761cc647fe0a046`이다.
Override 297개, intentional removal 1188개이며 unclassified·ambiguous·unknown/unowned는 모두 0이다.
`--write`·`--check`·`--self-test`, `verify-framework-doc-contracts.sh`와 `git diff --check`가 통과했다.
Codex 5.6 sol xhigh와 Claude Sonnet은 수정 뒤 독립 재계산과 refresh idempotence를 확인하고 모두
`ACCEPT`로 판정했다.

### 10.4 Runtime 완료 후 E2E·sample spec 확정과 단계별 활성화

`V11-M6-SCAFFOLD-ZERO` 뒤 공통 E2E와 sample spec을 amended contract와 실제 runtime에 맞춰 최종 확정한다.
Runtime과 spec 사이에 gap이 있으면 public contract를 구현 편의에 맞춰 축소하지 않고 runtime owner로 돌려보낸다.
영향받은 source와 registration은 impact manifest가 승인한 old→new hash로만 바꾸며, 영향받지 않은 항목은
diff 0을 유지한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-E2E-SPEC-FINAL` | Amended contract 기준 공통 E2E spec 확정 | E2E·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 대기 | scenario·negative·race·directional matrix와 approved hash·runtime owner 누락 0 | Relocation 용어와 readiness-first 이전, queue·timer 자동 복원, count·callback·byte gate, precommit abort 복구를 `RL-F11~RL-F14`와 `M75~M78`에 추가했다. 이전 review candidate hash는 이 계약 변경으로 효력을 잃었다. Config 1~14의 기존 contract finding과 새 scenario를 수정한 뒤 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet의 최종 독립 review, runtime owner·approved hash를 새로 기록해야 완료로 전환한다. |
| `V11-SAMPLE-SPEC-FINAL` | Amended contract 기준 공통 sample spec 확정 | sample·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 대기 | public 사용 흐름·역할·message·marker와 다섯 언어 owner 누락 0 | — |
| `V11-R5D` | Final E2E·sample spec 독립 review | Codex xhigh review lane, `P-XHIGH` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-E2E-SPEC-FINAL`, `V11-SAMPLE-SPEC-FINAL` | 대기 | public contract 일치, assertion 약화·coverage 손실·미분류 impact 0, 대체 scenario와 다섯 언어 sample parity의 I1·I2·I3 review clean | E2E pre-final 범위의 두 reviewer round와 post-fix focused review는 완료했다. `V11-E2E-SPEC-FINAL`의 runtime·approved hash와 `V11-SAMPLE-SPEC-FINAL`이 아직 대기이므로 이 combined final gate는 시작하지 않았다. |
| `V11-M6A-E2E` | Topology·liveness E2E 활성화와 gap 해소 | E2E·topology runtime lanes, `P-DELIVERY` | `V11-R5D` | 대기 | Config 3 `PS-F1~F5`, Config 5 `RL-E1~E5`, cross-MeshNode `ToChannel`(`M73`)와 amended placement scenario를 현재 candidate에서 실행, required skip·runtime gap 0 | — |
| `V11-M6B-E2E` | Stateful object E2E 활성화와 gap 해소 | E2E·stateful runtime lanes, `P-DELIVERY` | `V11-M6A-E2E` | 대기 | Spot·Actor·bound STREAM·Instance·remote create·global identity·cross-MeshNode `ToSpot`(`M72`)·stale owner scenario required skip·runtime gap 0 | — |
| `V11-M6C-E2E` | Maintenance·hosting E2E 활성화와 gap 해소 | E2E·maintenance runtime lanes, `P-DELIVERY` | `V11-M6B-E2E` | 대기 | Zero-downtime patch(`M69`), same-version maintenance(`M70`), Shutdown closing(`M71`), no-target blocker(`M74`), aggregate relocation·crash recovery·remote fencing·hosting scenario required skip·runtime gap 0 | — |

`V11-R5D`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet은 같은 candidate를 독립적으로 검토하고 finding을 §18 규칙으로
수렴한다. 두 reviewer가 clean을 기록하고 post-review `DOC`, `TRACE`, `AMENDMENT-IMPACT --mode finalized`가
통과하기 전에는 E2E source·registration을 변경하거나 `V11-M6A-E2E`를 시작하지 않는다.

각 E2E row는 approved scenario ID를 한 번에 하나만 활성화한다. 먼저 한 언어의 same-language cell에서
scenario를 실행하고, 실패 원인이 runtime gap이면 다음 scenario를 활성화하지 않는다. 해당 M6 language owner가
runtime을 수정하고 internal regression을 추가한 뒤 같은 scenario를 다시 실행한다. Same-language cell이
통과하면 필요한 cross-language directional cell을 실행하고, 모든 required cell과 evidence가 통과한 뒤에만
ledger에 해당 ID를 완료로 기록하고 다음 ID를 활성화한다. Scenario 묶음을 한꺼번에 활성화하거나 scenario·sample을
삭제하거나 assertion을 약화해 통과시키지 않는다. `V11-M6C-E2E`가 끝나기 전에는 full matrix, race·crash와
sample 실행을 시작하지 않는다.

## 11. M7 — 전체 correctness, E2E, sample과 smoke

M7은 service 의미가 Core 10.x 구현 없이 네 Framework runtime에서 성립하는지 검증한다. Oracle은 별도 process의
normalized trace 비교에만 사용하며 candidate process의 dependency가 될 수 없다.

M7은 M6 후반에 단계별 활성화를 마친 E2E를 full `4 x 4` matrix와 race·crash 조건으로 다시 검증한다. 그 결과가
통과한 뒤 sample을 처음 실행한다. 영향받은 E2E·sample source와 registration은 finalized impact manifest의
approved hash와 일치해야 하며, 영향받지 않은 항목은 baseline hash를 유지해야 한다. Required registration이나
대체 coverage가 줄어든 candidate는 실행 전에 실패한다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M7-CONTRACT` | 다섯 amended public contract와 네 runtime contract test | contract·language lanes, `P-DELIVERY` | `V11-M6-SCAFFOLD-ZERO`, `V11-R4A` | 대기 | exact interface·source·test·public declaration 일치, required contract skipped 0 | — |
| `V11-M7-RACE-CRASH` | 동시성·수명·crash·recovery suite | race·recovery lanes, `P-DEEP` | `V11-M6C-E2E` | 대기 | §14.2 race, reservation·aggregate transfer phase crash, pause fencing과 resource cleanup 통과 | — |
| `V11-M7-E2E-4X4` | Final directional cross-language `4 x 4` E2E | E2E lane, `P-DELIVERY` | `V11-M6C-E2E`, `V11-M7-CONTRACT` | 대기 | approved source·scenario·registration hash 일치, required service·maintenance caller→server cell, negative frame와 crash scenario skipped 0 | — |
| `V11-M7-SAMPLES` | C++·.NET·Java·Kotlin·Node sample 활성화·검증 | sample·language lanes, `P-DELIVERY` | `V11-M7-E2E-4X4`, `V11-M7-RACE-CRASH`, `V11-SAMPLE-SPEC-FINAL` | 대기 | approved source·registration hash 일치, public API만 사용, remote placement·typed JSON·hosting startup와 cleanup 통과 | 잘못 삭제된 언어 sample 75개와 수정된 Node sample support/runner 3개를 Git에서 복구했다. C++/.NET/JVM/Node registration audit도 손실 0이며 실제 source 변경·compile·run은 runtime 완료, sample spec 확정과 전체 E2E 통과 뒤 시작한다. 증거: `.artifacts/v11/evidence/V11-M4-SAMPLE-RECOVERY/` |
| `V11-M7-SMOKE-FUNCTIONAL` | Liveness·maintenance functional smoke | integration lane, `P-DELIVERY` | `V11-M7-CONTRACT`, `V11-M7-E2E-4X4`, `V11-M7-SAMPLES` | 대기 | reconnect·Retire·Shutdown·transfer·recovery와 resource cleanup 통과 | — |
| `V11-M7-SMOKE-PERF` | Service·Core raw performance smoke-only | perf lane, `P-DELIVERY` | `V11-M7-CONTRACT`, `V11-M7-E2E-4X4`, `V11-M7-SAMPLES` | 대기 | 네 runtime·Kotlin consumer·Core raw 최소 workload, provenance와 cleanup 통과; 수치 판정 없음 | — |
| `V11-M7-JOIN` | 전체 correctness 합류 | release test coordinator, `P-DEEP` | `V11-M7-RACE-CRASH`, `V11-M7-SMOKE-FUNCTIONAL`, `V11-M7-SMOKE-PERF` | 대기 | contract·race·crash·`4 x 4`·sample·functional·perf smoke 누락과 skip 0 | — |

## 12. M8 — Framework POSD·DDD와 unused cleanup

M8은 Framework에 남은 pass-through adapter, 중복 state machine과 사용되지 않는 source·test·sample·generated
output·build·package 입력을 정리하고 production scaffold·placeholder·fake data의 잔여와 재발이 0인지 다시
검증한다. Core와 bindings 제거를 이 단계로 미루지 않는다. Production placeholder 제거도 M6 gate 이후로
미루지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M8-INVENTORY` | Framework 제거·unused 항목 전수 목록 | inventory lane, `P-SCAN` | `V11-M7-JOIN` | 대기 | 네 runtime·공통 protocol·test·sample·build·package 입력의 미분류 0 | — |
| `V11-M8-CLEAN-CPP` | C++ Framework POSD·DDD·unused·package tooling 정리 | C++ cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·compat·pass-through·unused target 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-DN` | .NET Framework POSD·DDD·unused·package tooling 정리 | .NET cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·reflection·compat·unused project 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-JVM` | JVM Framework POSD·DDD·unused·package tooling 정리 | JVM cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | Java·Kotlin scaffold·compat·unused source set 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-NODE` | Node Framework POSD·DDD·unused·package tooling 정리 | Node cleanup lane, `P-DEEP` | `V11-M8-INVENTORY` | 대기 | scaffold·compat·unused export·handle 0, contract 회귀와 package metadata·consumer tooling self-test 통과 | — |
| `V11-M8-CLEAN-COMMON` | Protocol·fixture·E2E·CI·manifest·Framework package runner 정리 | coordinator, `P-SCAN` | `V11-M8-INVENTORY` | 대기 | unused generated constant·fixture·scenario·shared manifest·CI 입력 0, persistent Framework package runner self-test 통과 | — |
| `V11-M8-CLEAN-JOIN` | Framework cleanup 합류 | cleanup coordinator, `P-DELIVERY` | `V11-M8-CLEAN-CPP`, `V11-M8-CLEAN-DN`, `V11-M8-CLEAN-JVM`, `V11-M8-CLEAN-NODE`, `V11-M8-CLEAN-COMMON` | 대기 | 네 clean build·contract test와 scaffold·service projection·oracle link/load 0 | — |
| `V11-R6` | Framework POSD·DDD·제거 범위 독립 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M8-CLEAN-JOIN` | 대기 | domain boundary, dead code·compat helper·build·package 제거 review clean | — |

Cleanup은 문자열 검색만으로 완료하지 않는다. Export, include, build graph, generated 산출물, test discovery,
package manifest와 clean consumer로 사용 여부를 증명한다. 빈 wrapper, forwarding helper와 deprecated alias를
남기지 않는다.

## 13. M9 — Final local package, raw 재검증, E2E와 smoke

M9는 최종 Framework source와 review를 통과한 Core·bindings 조합으로 모든 증거를 다시 만든다. M9의 모든
package는 local/internal 위치에만 생성한다. 외부 registry에는 배포하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M9-RAW-FINAL` | Final Core·bindings raw capability 재검증 | Core·binding lanes, `P-DEEP` | `V11-R6` | 대기 | Core full raw suite·sanitizer, 네 public raw proof, ZMP heartbeat projection과 oracle link·load 0 재통과 | — |
| `V11-M9-PKG-CPP` | C++ final local/internal package 실행 | C++ package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Core·binding·Framework provenance, clean CMake consumer 통과 | — |
| `V11-M9-PKG-DN` | .NET final local/internal package 실행 | .NET package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Core·binding·Framework provenance, clean NuGet consumer 통과 | — |
| `V11-M9-PKG-JVM` | Java·Kotlin final local/internal package 실행 | JVM package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, Maven Java·Kotlin JAR·metadata·Spring consumer 통과 | — |
| `V11-M9-PKG-NODE` | Node final local/internal package 실행 | Node package lane, `P-DELIVERY` | `V11-M9-RAW-FINAL` | 대기 | R6가 review한 source·tooling 수정 0, packed tgz, ESM·CJS·`.d.ts`·NestJS consumer 통과 | — |
| `V11-M9-E2E-4X4` | Final 방향성 cross-language matrix | E2E lane, `P-DELIVERY` | `V11-M9-PKG-CPP`, `V11-M9-PKG-DN`, `V11-M9-PKG-JVM`, `V11-M9-PKG-NODE` | 대기 | required service·maintenance caller→server cell, negative frame와 crash scenario skipped 0 | — |
| `V11-M9-SMOKE-FUNCTIONAL` | Final liveness·maintenance functional smoke | integration lane, `P-DELIVERY` | `V11-M9-E2E-4X4` | 대기 | liveness, Retire·Shutdown·transfer·recovery·resource cleanup 통과 | — |
| `V11-M9-SMOKE-PERF` | Final service·Core raw perf smoke-only | perf lane, `P-DELIVERY` | `V11-M9-E2E-4X4` | 대기 | 네 runtime·Kotlin consumer·Core raw 최소 workload, provenance와 cleanup 통과; 수치 판정 없음 | — |
| `V11-M9-DOCS` | 정식 spec·guide·internals·migration·package 문서 확정 | docs lanes, `P-DEEP` | `V11-M9-SMOKE-FUNCTIONAL`, `V11-M9-SMOKE-PERF` | 대기 | 정식 문서와 public API·source·test·package 불일치 0 | — |
| `V11-R7` | 11.0 최종 통합 review | Codex review lane, `P-DEEP` + Claude `claude-sonnet-5` 병렬 reviewer | `V11-M9-DOCS` | 대기 | 전체 review clean 뒤 final raw·E2E·consumer·smoke 재통과 | — |

## 14. Required correctness와 functional smoke

Correctness gate는 최소한 다음 영역을 포함한다.

| 영역 | 필수 결과 |
|---|---|
| Topology | node direct, Channel select-one, ClientServer, fanout, same-name topology, route 없음과 reconnect |
| Request | reply, backpressure, timeout, cancellation, stale reply와 shutdown 경쟁의 terminal 결과 하나 |
| Spot·Actor | Entry·User·Instance, same-ID 동시 요청, 순차 turn, membership와 close 경쟁 |
| STREAM | bound session, disconnect, Actor 이동 barrier와 stale generation 거부 |
| Maintenance control | empty·blocked·multi-Mesh preflight, `Retire`·`Shutdown`, deadline과 waiter cancellation |
| Maintenance transfer | Actor·User Spot·Instance Snapshot·Recreate, Entry Spot callback, timer·session ordering |
| Recovery | commit 전후 crash, store outage, process pause fencing, target replacement와 orphan cleanup |
| Liveness | store-backed·manual SIGKILL·blackhole·pause·reconnect, frame·timer와 cleanup |
| Protocol | malformed, unknown version·field, oversize, stale owner·generation과 partial-order 거부 |
| Hosting | C++ host, ASP.NET, Spring과 NestJS bounded shutdown |
| Interop | C++·.NET·JVM·Node.js의 방향이 있는 `4 x 4` caller→server 필수 조합 |

### 14.1 Stateful·maintenance E2E catalog

아래 ID는 공통 E2E catalog에서 같은 이름으로 구현한다. 언어별 runner가 임의로 ID를 바꾸거나 일부 항목을
단위 test 통과로 대신하지 않는다.

이 표는 amended formal spec을 반영한 pre-final catalog다. Runtime 구현 중 ID를 삭제하거나 assertion을
약화하지 않는다. `V11-CA-IMPACT`가 각 ID를 분류하고 `V11-E2E-SPEC-FINAL`이 runtime gap을 확인한 뒤 완료 조건과
approved hash를 확정한다. Global identity, remote placement, Instance Spot fluent activation과 Relocation vocabulary로
바뀐 항목은 이전 Mesh별 identity·local-only create·address wrapper·이전 payload vocabulary 가정을 다시 도입하지 않는다.

| ID | Scenario | 완료 조건 |
|---|---|---|
| `V11-E2E-M01` | Empty host retire | 모든 local component가 `Stopped`가 되고 host-owned resource가 정리됨 |
| `V11-E2E-M02` | Preflight blocked | admission과 descriptor state 변화 없이 `Blocked` 반환 |
| `V11-E2E-M03` | Mixed-version target | source보다 낮은 application version, stable type capability·Snapshot adapter capability가 맞지 않는 target 제외 |
| `V11-E2E-M04` | Actor Snapshot transfer | application-owned opaque state bytes, accepted journal과 Actor generation 유지 |
| `V11-E2E-M05` | Actor Recreate relocation | Application state와 Restore 없이 accepted journal·recovery payload를 Relocation Store에 먼저 보존하고, Location Store가 canonical participant set·target reservation·positive capacity를 검증한 뒤 target factory·journal staging·aggregate commit·replay 순서로 복구를 완료함 |
| `V11-E2E-M06` | Instance Spot transfer | global Spot RID, object·authority owner generation과 순차 request 유지 |
| `V11-E2E-M07` | Concurrent cold Instance request | 100개 caller가 owner, activation과 factory 하나로 수렴 |
| `V11-E2E-M08` | User Spot aggregate transfer | User Spot state와 bounded canonical Actor participant set을 한 aggregate로 capture하고 target Spot·Actor restore와 participant digest 검증 뒤 authority·membership을 한 commit으로 전환함 |
| `V11-E2E-M09` | Source crash before commit | `Preparing` 또는 unlinked Transfer Put 중 crash는 transfer를 fenced abort하고 원 request가 일반 connection failure·timeout terminal을 따르며 hidden replay를 수행하지 않음. `Captured`·`Prepared` crash는 Location authority에 연결된 immutable Transfer root와 accepted journal을 사용해 current target commit 또는 replacement로 수렴함 |
| `V11-E2E-M10` | Source crash after commit | target activation과 source cleanup recovery 재개 |
| `V11-E2E-M11` | Store outage | seal 전 `Blocked`, seal 뒤 bounded `ForceStopped` reason 일치 |
| `V11-E2E-M12` | Request terminal race | reply·timeout·cancellation·transfer·shutdown 중 결과 하나 |
| `V11-E2E-M13` | Timer race | 이전 owner와 generation의 tick이 application callback에 도달하지 않음 |
| `V11-E2E-M14` | Full rolling operation | accepted request 완료, moving 결과 뒤 application의 명시적 retry 성공 |
| `V11-E2E-M15` | Maintenance wave | source wave node 사이의 반복 transfer 0건 |
| `V11-E2E-M16` | Manual·automatic classic fanout | publisher 자동 게시와 subscriber 자동 발견, manual mode 회귀 통과 |
| `V11-E2E-M17` | Spot Logical Multicast | Draining target과 stale generation 제외, partial admission detail 일치 |
| `V11-E2E-M18` | Remote owner fencing | 지연된 이전 owner message·timer·phase update가 새 owner에 적용되지 않음 |
| `V11-E2E-M19` | SIGTERM integration | hosting lifecycle이 bounded `Shutdown`을 사용하고 `Blocked`를 반환하지 않음 |
| `V11-E2E-M20` | Multi-Mesh host | host barrier 하나로 all-or-none preflight와 seal 수행 |
| `V11-E2E-M21` | Retire blocked then shutdown | 차단된 `Retire` 뒤 `Shutdown`이 shared deadline 계약으로 terminal 완료 |
| `V11-E2E-M22` | Process pause fencing | lease-derived monotonic deadline 뒤 stale admission과 CAS 거부 |
| `V11-E2E-M23` | Prepared·Committed target replacement | Current coordinator fence가 stable transfer ID의 immutable Transfer manifest로 replacement reservation ACK를 얻고 target·attempt generation·reservation을 한 CAS로 교체해 successor 하나로 activation을 수렴함. Transfer root·journal·terminal key는 attempt 교체에도 바뀌지 않음 |
| `V11-E2E-M24` | Close versus transfer | Instance `Close`와 `Prepared` CAS 승자에 맞는 결과, hidden retry 0건 |
| `V11-E2E-M25` | Cold Instance one-way and request | Global Spot RID의 Instance intent가 eligible target을 선택하고 first message·operation identity·reply correlation·deadline을 activation envelope에 포함해 target transport에 제출함. Source claim은 0건이고 target CAS winner가 `Creating` reservation을 만든다. One-way는 envelope outbound admission에서 완료되고 request만 target activation·handler terminal을 기다림 |
| `V11-E2E-M26` | STREAM binding atomicity | bind·rebind·unbind 실패 시 기존 binding 유지와 terminal result 하나 |
| `V11-E2E-M27` | Observer terminal lane | 일반 event admission을 닫은 뒤 final snapshot·terminal event가 한 번 전달됨 |
| `V11-E2E-M28` | Completion reserve saturation | infrastructure queue 포화에서도 accepted request가 terminal 완료됨 |
| `V11-E2E-M29` | Cross-topology ChannelName call | Spot이 다른 RouteMesh·ClientServer Channel을 호출하고 completion은 원래 owner로 복귀 |
| `V11-E2E-M30` | ChannelName collision | 서로 다른 physical topology의 같은 process-local ChannelName이 startup에서 거부됨 |
| `V11-E2E-M31` | Global object identity collision | 서로 다른 initial Mesh intent로 같은 Actor ID·Spot RID를 동시에 create해도 provider 전체에서 하나의 authority와 current owner로 수렴하고 direct messaging과 manager `Find`는 MeshName 없이 같은 object를 반환함 |
| `V11-E2E-M32` | Cross-language authority interop | 한 runtime이 `authority-key-v1`과 authority payload를 기록하고 다른 runtime이 steady·cold activation·maintenance state를 같은 logical object로 resolve·recovery |
| `V11-E2E-M33` | Transfer preflight growth | Preflight 뒤 reversible seal까지 수락된 work를 exact inventory에 포함하고, seal 뒤 final reservation ACK와 모든 `Prepared` CAS를 완료한 뒤에만 `Draining`을 게시해 continuity를 잃지 않음 |
| `V11-E2E-M34` | Transfer payload leased retention | Long capture 중 staged manifest tree를 추적하고 `Captured`·`Prepared` CAS 직전에 모든 component의 remaining lease를 12시간보다 길게 verify·renew하며 partial renew failure는 root를 Location authority에 연결하지 않음. Provider 기준 시각을 orphan TTL 이상 이동하면 current reference는 renew로 유지되고 orphan만 제거됨. Published reference의 payload가 24시간 연속 Store 불가 뒤 영구 유실되면 non-retriable `TransferDataLost`, 진행 중 `Retire`는 detail에 해당 오류를 보존한 `ForceStopped/TransferFailed`로 종료 |
| `V11-E2E-M35` | Actor owner ABA fence | Actor가 A→B→A로 이전된 뒤 최초 A owner의 지연 message·journal·forward record가 새 A owner에 적용되지 않음 |
| `V11-E2E-M36` | Cross-language terminal failure | 모든 stable failure code를 네 runtime 조합으로 reply·completion·relay해 같은 typed terminal result로 변환하고 unknown code는 protocol error로 수렴 |
| `V11-E2E-M37` | Authority generation atomicity | Provider-domain global object·authority-owner·store revision counters와 Missing/Found expectation으로 create·owner change·preserve·delete·재생성을 원자 처리하고 concurrent winner만 값을 소비하며 per-key version·tombstone을 남기지 않음 |
| `V11-E2E-M38` | ClientServer command isolation | ClientServer role·direction allowlist 밖의 RouteMesh·Spot·Actor·transfer·server-originated application command가 handler·authority에 도달하지 않고 offending connection만 protocol error로 종료 |
| `V11-E2E-M39` | Global Actor creation parity | 다섯 public 언어가 global Actor ID·explicit stable type과 optional initial Mesh intent로 create·GetOrCreate를 수행하고 manager `Find`는 existing-only이며 caller가 physical owner를 선택하거나 hidden create를 시작하지 않음 |
| `V11-E2E-M40` | Spot kind atomic collision | 같은 global Spot RID의 Entry·User create와 Instance fluent cold activation이 하나의 authority CAS에서 경쟁해 kind 하나만 성공하고 close 뒤 다른 kind로 재생성해도 object generation을 재사용하지 않음 |
| `V11-E2E-M41` | Owner lease stale token | Process pause와 lease expiry 뒤 이전 token의 renew·release가 거부되고 같은 owner ID의 새 claim도 global lease generation이 증가한 새 token을 사용해 이전 descriptor·ready routing을 복구하지 않음 |
| `V11-E2E-M42` | Durable authority after owner lease expiry | Owner lease가 만료되어도 `Creating`·`Committed` authority의 phase·Transfer reference·replay cursor·object generation이 유지되고 successor의 `new_owner` CAS만 recovery를 계속함 |
| `V11-E2E-M43` | Bound-session transfer barrier | Session owner ID·host lease generation·node RID·lifecycle, session RID·binding generation을 보존하고 binding ingress를 reversible seal해 last accepted sequence를 확정한 뒤 source journal·target replay·current host lease를 다시 확인한 session owner route publication ACK와 unseal로 transfer 경계의 packet을 순서대로 한 번 처리함. Abort는 durable Aborted CAS 뒤 source-route ACK와 steady normalization을 마친 후에만 source admission을 다시 엶 |
| `V11-E2E-M44` | Snapshot-consistent authority recovery scan | Opaque watermark 시점의 authority key incarnation을 pages 전체에서 정확히 한 번 열거하고 exact Read+CAS로 재검증하며 initial scan 전 Serving을 게시하지 않고 post-watermark mutation은 다음 scan에서 복구함 |
| `V11-E2E-M45` | Negotiated message bound | 양쪽 32 MiB 설정에서 17 MiB payload가 성공하고 sender·receiver 설정이 다르면 작은 effective bound를 allocation 전에 적용하며 `MaxMessageSize=0`이 숨은 16 MiB 상한 없이 normalize됨 |
| `V11-E2E-M46` | Chunked Transfer manifest | 64 MiB보다 큰 journal·Snapshot을 ordered immutable chunks와 Transfer root manifest로 보존·renew·streaming replay하고 Location authority CAS 전 orphan과 reference 제거 뒤 cleanup을 안전하게 처리함 |
| `V11-E2E-M47` | Host lease and routing slots | 한 host가 process lifecycle owner lease를 한 번 claim하고 같은 token을 여러 allocation group에 전달해 component별 slot을 얻으며 stale token과 rollback이 slot·lease를 누출하지 않음 |
| `V11-E2E-M48` | Framework JSON v1 interop | Typed application message의 property·enum·integer·bytes·null 의미를 다섯 언어가 같이 복원하고 unknown field는 무시하되 duplicate·missing required field는 거부함. Transfer adapter의 Snapshot bytes는 application-owned opaque payload로 유지하며 Framework JSON이나 state contract ID를 적용하지 않음 |
| `V11-E2E-M49` | Paused session-owner fence | Transport I/O가 유지되어도 만료된 session owner host lease의 seal·route ACK가 binding route와 unseal을 바꾸지 않고 successor의 exact owner token만 적용됨 |
| `V11-E2E-M50` | Transfer target reservation fence | Paused target의 만료된 host lease에 묶인 offer·reservation ACK·activation을 거부하고 current target owner token을 검증한 replacement reservation 하나로 수렴함 |
| `V11-E2E-M51` | Owner-token bulk cleanup | 같은 owner ID를 다시 claim한 뒤 이전 owner token의 bulk cleanup이 새 descriptor·authority를 삭제하지 않고 exact lease generation만 정리함 |
| `V11-E2E-M52` | Activated-to-Completed admission seal | Target restore·replay와 session route staging 뒤에도 authenticated source cleanup 또는 exact source lease-expiry fence와 authority `Completed` 전에는 application/session ingress를 열지 않고, steady normalization 뒤에만 `Ready`·unseal·Transfer payload release를 수행함 |
| `V11-E2E-M53` | Bounded descriptor reconcile | MeshName·ChannelName descriptor를 최대 1,000개 page와 opaque continuation으로 읽고 scope change stamp가 처음과 끝에 같을 때만 full snapshot을 적용하며 routing group 상한을 검증함 |
| `V11-E2E-M54` | Compact authority completion | Location authority row를 1 MiB compact metadata로 제한하고 full journal·reply completion은 Transfer chunks만 소유하며 late completion root CAS와 recovery scan 4 MiB page cap에서 누락·중복 없이 수렴함 |
| `V11-E2E-M55` | Admitted descriptor update fence | Current connection의 더 높은 revision에서 제한된 mutable field만 적용하고 same-revision conflict·immutable identity/capability mutation은 protocol error, lower revision은 stale ignore로 처리함 |
| `V11-E2E-M56` | Transferred reply ACK barrier | Lease-backed Node·Channel·Spot·Actor·Instance·bound-session accepted request의 exact original reply route를 Transfer payload에 보존하고 stable transfer·operation ID로 relay를 재전송하며 authenticated terminal ACK 또는 exact request-source host lease expiry 전에는 `Completed`·payload release를 허용하지 않음. Operation ID와 connection close를 reply route·terminal 증거로 대신하지 않음 |
| `V11-E2E-M57` | Instance claim generation | Generic Reserve의 Missing→Pending `Creating` claim이 nonzero object·authority owner generation을 한 번 발급하고 exact Commit의 `Ready`·`Closing`·failure completion이 같은 generation을 유지하며 Commit이 새 generation을 만들지 않음 |
| `V11-E2E-M58` | Descriptor registration bound | Fully derived descriptor 1 MiB, stable type·Snapshot adapter·placement capability vector 1,024 상한을 startup에서 원자 검증하고 초과 시 partial publish 없이 모든 언어가 같은 configuration failure로 종료함 |
| `V11-E2E-M59` | Store grace and stateful owner fence | Store failure grace 동안 마지막 stable discovery set과 ready transport는 유지하지만 owner lease의 monotonic deadline 뒤 stateful message·timer·factory completion·CAS admission은 닫히며, 복구 뒤 stable page set과 exact owner token을 다시 확인한 후에만 diff와 admission을 재개함 |
| `V11-E2E-M60` | Provider ambiguous completion | Location authority CAS·Relocation Put invocation 뒤 cancellation·timeout·response loss를 no-commit으로 가정하지 않고 exact read·expected fence 또는 content reference로 reconcile하며 unlinked Put은 orphan cleanup으로 수렴함. Async input은 완료까지 불변이고 success result bytes는 provider 재사용과 mutable adapter 뒤에도 stable함 |
| `V11-E2E-M61` | Connection-bound maintenance source | Manual/no-Store peer의 request와 one-way send를 모두 `Captured` 전에 terminal 완료하고 durable journal·relay barrier에 넣지 않으며, 하나라도 deadline 내 완료하지 못하면 pre-`Captured` abort와 `Blocked/DeadlineExceeded`로 admission을 복원함. Durable journal은 lease-backed accepted work만 기록하고 manual lifecycle token은 opaque equality와 current connection handover로 fence하며 numeric ordering을 사용하지 않음 |
| `V11-E2E-M62` | Cold activation crash boundary | Source가 activation envelope admission 전에 종료되면 authority와 factory가 0건임. Target이 envelope를 수락하고 `Creating` reservation을 획득한 뒤 종료되면 exact target owner의 initial·background bounded scan이 durable first-message reference와 같은 object·authority generation의 factory·`Ready` barrier를 idempotent하게 재개함 |
| `V11-E2E-M63` | Cold activation failure release | Factory·initialize·Ready 실패가 local barrier를 failed·sealed로 유지하고 request terminal·one-way drop을 한 번 확정한 뒤 exact Store version·object generation·authority owner generation·owner lease의 fenced delete로 row를 제거함. 응답 손실은 exact read로 수렴하며 Missing 뒤 다음 caller만 새 generation으로 activation하고 기존 registry는 hidden rerun하지 않음 |
| `V11-E2E-M64` | Store 없는 object role 거부 | Location Store 없이 Object Client 또는 Object Server 역할·factory·manager를 구성하면 socket bind 전에 startup configuration error로 끝나며 hidden process-local object runtime을 만들지 않음. Store 없는 manual `None` 역할은 explicit peer의 Node·Channel direct만 제공하고 Actor·Spot manager, factory, placement와 session Actor dispatch를 노출하지 않음 |
| `V11-E2E-M65` | User Spot close with membership | Current Actor membership이 있는 User Spot의 normal close는 `false`를 반환하고 admission·authority·membership과 closing callback을 바꾸지 않으며, hidden move·destroy 없이 explicit leave/destroy 뒤 close만 한 번 성공함. Host Shutdown·Retire는 Actor barrier를 Spot cleanup보다 먼저 완료함 |
| `V11-E2E-M66` | Store-backed local object publication barrier | Generic Reserve가 User Spot·Actor의 final generation과 Pending `Creating` row를 발급한 뒤 factory·initialize와 같은 owner fence의 exact Commit이 Active `Ready`를 게시할 때까지 resolver·remote messaging이 object를 공개하지 않음. Actor는 initial Entry membership도 barrier 안에서 완료함. Failure는 exact Abort와 read reconcile로 Pending row·scope·membership을 제거하며 다음 caller만 새 generation으로 create함 |
| `V11-E2E-M67` | Retire preflight deadline | Seal 전 preflight가 host deadline을 넘으면 `Blocked/DeadlineExceeded`와 host·descriptor·admission unchanged로 끝나고, seal 뒤 teardown이 deadline을 넘긴 경우에만 `ForceStopped/DeadlineExceeded`로 bounded cleanup을 수행함 |
| `V11-E2E-M68` | Retire Actor target Entry membership | Source Entry member Actor는 target factory·필요한 Restore·journal staging을 commit 전에 끝내고, NewOwner CAS가 ObjectGeneration은 유지한 채 owner·AuthorityOwnerGeneration·current Spot을 target initialized Entry identity로 atomic 교체함. Commit 뒤 target Entry `OnActorRelocated`·journal replay, source `OnLeaveActor`·old membership durable cleanup, `Completed`, route ACK·steady normalization을 끝낸 뒤 Ready를 공개함. User Spot member Actor는 enclosing User Spot aggregate의 participant로 함께 이동함 |
| `V11-E2E-M69` | Zero-downtime application patch | Application version N source의 `Retire`가 version N+1 compatible target만 선택해 User Spot·Actor·Instance와 accepted work를 이전하고 continuous request·bound session을 유지한 뒤 source를 `Stopped/None`으로 종료함 |
| `V11-E2E-M70` | Same-version planned maintenance | Application version N source와 target만 있어도 compatible capacity가 있으면 `Retire`가 정상 완료되어 새 version이 same-version maintenance의 필수 조건이 아님을 증명함 |
| `V11-E2E-M71` | Shutdown Spot closing lifecycle | `Shutdown`이 새 relocation·reservation 없이 Entry·User·Instance Spot에 `OnClosing(HostShutdown, Deadline)`을 한 번 호출하고 callback 동안 membership을 유지한 뒤 bounded cleanup과 `Stopped/None` 또는 deadline의 `ForceStopped/DeadlineExceeded`로 끝남 |
| `V11-E2E-M72` | Cross-MeshNode ToSpot | 서로 다른 process·MeshNode에 있는 caller가 MeshName·owner RID·endpoint 없이 global Spot RID만으로 `SendToSpot`·`RequestToSpot`을 호출해 current Ready Spot handler에 정확히 한 번 도달함 |
| `V11-E2E-M73` | Cross-MeshNode ToChannel | 서로 다른 process·MeshNode에 있는 caller가 MeshName·RID·endpoint 없이 unique ChannelName만으로 `SendToChannel`·`RequestToChannel`을 호출해 ready remote member를 선택하고 terminal result를 한 번 반환함 |
| `V11-E2E-M74` | Retire without eligible target | Compatible target이 없으면 `Retire`가 `Draining` publication과 capture 전에 `Blocked/TargetUnavailable` 또는 capability 불일치의 `Blocked/StateIncompatible`로 끝나고 source authority·admission·payload reference를 바꾸지 않음 |
| `V11-E2E-M75` | Readiness-first relocation | Retire notification이 standalone Actor, Instance Spot과 User Spot aggregate queue의 turn boundary에 도달하고, ready unit부터 bounded sliding permit으로 즉시 relocation하며 느린 current turn이 다른 unit을 막지 않음. Permit 전 seal은 0건임 |
| `V11-E2E-M76` | Queue·timer relocation | Current turn 하나만 source에서 완료하고 미실행 message·accepted journal·logical timer registration·pending tick을 immutable relocation payload로 저장함. Target은 application 재등록 없이 timer를 복원하고 frozen queue→seal 중 hold→Ready 이후 message 순서를 보존함 |
| `V11-E2E-M77` | Relocation concurrency gates | 기본 active outbound·inbound 64, Capture·Restore 8, encoded payload in flight 256 MiB를 독립 high-water로 검증하고, byte 한도를 넘는 단일 User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행함 |
| `V11-E2E-M78` | Relocation precommit abort queue recovery | Target reservation·Restore의 commit 전 실패에서 durable abort와 source normalization 뒤 frozen queue·hold queue·timer schedule·operation identity를 source에 같은 순서로 복원하고 target staging과 relocation payload를 정리함 |

| ID | Liveness scenario | 완료 조건 |
|---|---|---|
| `V11-E2E-L01` | Store-backed peer orderly disconnect | FIN·RST·raw monitor event에서 ready 상태는 intentional delay 없이 즉시 바뀌고 test는 5초 observation budget 안에 결과를 확인 |
| `V11-E2E-L02` | Manual peer asymmetric blackhole | Admission이 initial Ready와 15초 deadline을 시작하고 connection당 outstanding probe 하나를 5초마다 재전송하며, 반대 방향 application traffic이 계속되어도 current matching ACK가 없으면 transport target에서 제외 |
| `V11-E2E-L03` | Store-backed process pause | owner lease TTL과 polling 상한 안에 신규 routing에서 제외 |
| `V11-E2E-L04` | Peer restart | Service admission을 다시 수행하고 store-backed exact owner token 또는 manual CSPRNG lifecycle nonce를 equality로 검증하며 current connection handover 뒤 stale event가 successor를 제거하지 않음. Lifecycle token의 숫자 대소 비교는 사용하지 않음 |
| `V11-E2E-L05` | Liveness cleanup | liveness probe·reconnect timer, monitor handle과 child process가 terminal 뒤 남지 않음 |
| `V11-E2E-L06` | Manual·automatic classic fanout publisher failure | Config 3 `PS-F1~F5`: descriptor·manual endpoint별 publisher 전용 SUB socket, publisher→subscriber periodic one-way exact 2-frame beacon, application filter와 reserved subscription 분리, unrelated-topic traffic 중 false timeout 0, 첫 valid receive 전 ready 0과 application delivery 0. Public exact reserved topic은 거부하고 같은 prefix+추가 byte topic은 허용하며 malformed reserved record는 protocol error와 해당 publisher 즉시 not-ready로 수렴. M5 codec negative fixture와 같은 bytes 사용 |

### 14.2 필수 race·회귀 test

- `V11-RACE-01`: 같은 global Spot RID에 Instance intent를 지정한 100개 caller가 최초 요청해 authority owner와 factory execution 하나로 수렴하는지 확인한다.
- `V11-RACE-02`: reply, timeout, cancellation, transfer와 shutdown 순서를 무작위화해 terminal completion이
  하나인지 확인한다.
- `V11-RACE-03`: Transfer의 `Preparing`, Transfer Put, `Captured`, `Prepared`, `Committed`, `Activated`에서
  source·target·coordinator process를 각각 종료한다. `Captured` 전에는 unlinked Put을 orphan으로 정리하고
  transfer를 fenced abort하며 accepted work replay를 주장하지 않는다. `Captured` 이후에는 current coordinator
  owner·lease·node·store-version fence와 replacement reservation ACK를 가진 writer 하나만 linked immutable root로
  durable authority가 정한 방향에 수렴한다. Replacement는 target attempt generation만 증가시키며 stable transfer
  ID의 transfer·journal·replay cursor와 terminal record를 새 attempt key로 복제하거나 잃지 않아야 한다.
- `V11-RACE-04`: reconnect 중 이전 connection의 disconnect, route update와 reply가 successor connection에
  적용되지 않는지 확인한다.
- `V11-RACE-05`: application·infrastructure queue가 각각 포화된 상태에서 admission 결과, completion reserve와
  memory 상한을 확인한다.
- `V11-RACE-06`: timer callback과 Spot close가 경쟁할 때 terminal state 뒤 새 application turn이 시작되지
  않는지 확인한다.
- `V11-RACE-07`: process pause가 owner·coordinator deadline을 넘은 뒤 message, timer, factory completion과
  phase CAS가 모두 거부되는지 확인한다.
- `V11-RACE-08`: public binding monitor·poller callback과 runtime shutdown 경쟁 뒤 use-after-free, 늦은 callback과
  금지된 재진입이 없는지 확인한다.
- `V11-RACE-09`: 반환하지 않는 application·observer callback이 tombstone으로 분리돼 host `ForceStopped`가
  유한 완료되고, callback 반환 뒤 terminal host state를 바꾸지 못하는지 확인한다.
- `V11-RACE-10`: STREAM bind·rebind·unbind와 disconnect가 경쟁해 current token만 변경하고 기존 binding을
  잘못 제거하지 않는지 확인한다.
- `V11-RACE-11`: Actor owner가 A→B→A로 변경되는 순서와 최초 A의 지연 message·journal·forward record
  도착을 임의로 배치해 current membership fence와 다른 record가 모두 거부되는지 확인한다.
- `V11-RACE-12`: Maintenance preflight, 신규 application admission, reversible seal과 capacity reservation을
  경쟁시켜 preflight 성공 뒤 reservation 부족으로 accepted work가 유실되지 않는지 확인한다.
- `V11-RACE-13`: Authority object·owner generation을 signed 64-bit 최댓값에서 증가시키고 stable exhaustion으로
  실패하며 0·음수 wrap, generation 재사용과 partial authority write가 없는지 확인한다.
- `V11-RACE-14`: 같은 global Spot RID에 User Spot create와 Instance fluent cold request를 동시에 제출해
  authority CAS winner 하나만 factory와 local object를 만들고 loser가 별도 typed location row나 generation을
  남기지 않는지 확인한다.
- `V11-RACE-15`: Owner lease expiry, successor claim, 같은 owner ID의 더 높은 lease generation claim,
  이전 process resume와 지연 renew·release 순서를 무작위화해 current token만 lease와 owner descriptor를
  변경하는지 확인한다.
- `V11-RACE-16`: Durable authority phase CAS, owner lease expiry와 successor `new_owner` claim 순서를
  무작위화해 authority payload가 TTL로 삭제되지 않고 current store version의 writer 하나만 phase를
  변경하는지 확인한다.
- `V11-RACE-17`: Bound-session packet acceptance, session owner seal, source journal capture, target replay와
  route publication ACK 순서를 무작위화해 각 session sequence가 source 또는 target에서 한 번만 처리되고
  binding FIFO가 유지되는지 확인한다.
- `V11-RACE-18`: Authority recovery scan watermark와 key create·update·delete·recreate를 경쟁시켜 watermark
  시점의 incarnation이 누락·중복되지 않고 post-watermark incarnation은 다음 scan에서만 처리되는지 확인한다.
- `V11-RACE-19`: Transfer data chunk·manifest Put, authority reference CAS, renew와 cleanup 사이에 process를
  종료해 current manifest의 모든 chunk만 유지되고 orphan·교체된 chunk가 정리되며 partial payload를 replay하지
  않는지 확인한다.
- `V11-RACE-20`: Session owner의 application runtime을 pause하되 transport I/O를 유지하고 lease expiry,
  `sessionTransferSealed`, `sessionTransferRouted`와 successor claim 순서를 무작위화해 current owner token의
  route ACK만 binding을 변경하는지 확인한다.
- `V11-RACE-21`: Transfer target의 offer·reservation ACK 뒤 process pause, host lease expiry, same-owner-ID
  reclaim, `Prepared`·`Committed` CAS와 activation 순서를 무작위화해 exact target owner token과 current
  reservation을 가진 writer 하나만 authority를 변경하는지 확인한다.
- `V11-RACE-22`: 같은 owner ID의 이전·현재 lease token으로 descriptor 게시와 bulk cleanup을 경쟁시켜 이전
  token이 현재 generation의 descriptor·authority·owner index entry를 제거하지 않는지 확인한다.
- `V11-RACE-23`: Target `Activated`, source cleanup terminal, authority `Completed`, public Ready와 session
  unseal 사이에 source·target을 종료하고 조기·재정렬 `transferComplete`도 주입한다. Authenticated source
  cleanup 또는 exact lease-expiry fence 전에는 Completed와 새 application work가 0건이고 immutable transfer
  replacement가 안전하며, Completed 뒤에는 종료된 transfer를 다시 사용하지 않는지 확인한다.
- `V11-RACE-24`: Descriptor page 사이 create·update·delete와 scope change stamp를 경쟁시켜 stamp가 달라진
  partial snapshot이 connect/disconnect diff에 적용되지 않고 stable page set 하나만 desired state를 바꾸는지
  확인한다.
- `V11-RACE-25`: 같은 transfer root에 late terminal completion을 추가하는 writer를 경쟁시켜 새 immutable
  chunk·manifest와 authority root-reference CAS winner 하나만 current가 되고 loser payload가 orphan으로
  정리되며 reply가 누락·중복되지 않는지 확인한다.
- `V11-RACE-26`: 같은 connection의 descriptor update revision을 재정렬하고 equal-revision same·different
  payload와 reconnect를 경쟁시켜 current immutable identity가 바뀌지 않고 가장 높은 valid mutable snapshot
  하나만 selection에 적용되는지 확인한다.
- `V11-RACE-27`: Reply relay, relay ACK, caller timeout·cancellation, request-source connection 종료·재연결,
  exact source lease expiry와 transfer Completed를 무작위화한다. 서로 다른 request kind와 correlation의
  frozen record가 exact original reply route를 유지하며 operation ID나 reconnect route로 바뀌지 않는지 확인한다.
  Connection close만으로 accounted 처리하지 않고 application terminal completion이 한 번이며 ACK·lease expiry
  전 transfer가 해제되지 않고 lost ACK가 idempotent 재전송으로 수렴하는지 확인한다.
- `V11-RACE-28`: Instance `Creating` claim, factory failure, `Ready` CAS와 close를 경쟁시켜 claim winner가
  발급한 nonzero object generation 하나만 operation lifetime에 사용되고 failure·cleanup이 0이나 새 generation을
  기록하지 않는지 확인한다.
- `V11-RACE-29`: Store failure grace, owner lease 갱신 실패와 transport liveness를 경쟁시켜 discovery transport가
  유지되어도 마지막 valid owner deadline 뒤 stateful message·timer·factory completion·CAS가 모두 거부되고,
  복구한 stable page set과 exact owner token 확인 전에는 신규 connect와 stateful admission이 재개되지 않는지
  확인한다.
- `V11-RACE-30`: Post-commit target replacement와 이전 attempt의 factory·Snapshot restore callback 완료를
  경쟁시킨다. 같은 stable transfer에서 callback이 attempt별 한 번 이상 실행되고 겹쳐도 current exact target
  owner와 target attempt만 completion commit·application admission을 수행하며 stale callback은 authority나
  public Ready를 변경하지 않는지 확인한다.
- `V11-RACE-31`: Authority CAS·transfer Put의 invocation, commit, cancellation·timeout과 response delivery를
  무작위화한다. Invocation 전 cancellation만 no-commit이고 이후 불명확한 결과는 exact read·expected fence 또는
  content reference로 reconcile해 duplicate generation transition과 linked partial transfer가 없으며 lost Put은
  bounded orphan cleanup으로 수렴하는지 확인한다.
- `V11-RACE-32`: Prepared session transfer의 durable `Aborted` CAS, source route 복귀 command·ACK,
  reservation·transfer cleanup, steady source normalization과 admission reopen 경계마다 coordinator를 종료한다.
  `Aborted` 결정 전 abort route가 전송되지 않고 source는 sealed를 유지하며, recovery가 ACK를 재전송한 뒤 steady
  normalization까지 완료한 경우에만 source admission이 다시 열리는지 확인한다.
- `V11-RACE-33`: Manual/no-Store peer의 connection-bound request·one-way send terminal, reversible seal,
  `Captured` CAS와 connection restart·handover를 경쟁시킨다. 두 accepted work가 terminal 전 durable journal에
  들어가지 않고 capture gate를 넘지 않으며, 하나라도 timeout이면 pre-Captured abort와
  `Blocked/DeadlineExceeded`로 복원된다. 새 opaque lifecycle token과 current connection만 유효하고 이전
  connection event가 같은 RID successor를 제거하지 않는지 확인한다.
- `V11-RACE-34`: Instance factory·initialize·Ready 실패, fenced delete 응답 손실, owner lease expiry와 다음
  caller claim을 경쟁시킨다. 이전 registry는 current row가 Missing 또는 replacement임을 exact read로 확인하기
  전까지 failed·sealed를 유지하고 hidden factory 재실행이나 handler admission을 하지 않는다. 다음 caller만
  더 높은 object·authority owner generation으로 새 activation을 시작하고 request·one-way terminal evidence가
  중복되지 않는지 확인한다.
- `V11-RACE-35`: User Spot normal close와 Actor join·leave·destroy를 경쟁시킨다. Close의 membership 확인과
  admission 변경은 같은 serialized boundary에서 결정하고 current membership이 있으면 `false`와
  state unchanged로 끝난다. Leave 또는 destroy가 먼저 완료된 경우에만 close와 closing callback이 한 번
  실행되며 dangling Actor authority와 hidden move·destroy가 없는지 확인한다.
- `V11-RACE-36`: Store-backed User Spot·Actor의 Missing→Pending `Creating` Reserve, factory·initialize와 Actor initial
  membership completion, Pending→Active `Ready` Commit, remote resolve·request와 failure Abort를 경쟁시킨다. `Ready` 전에는
  handle·ActorRef와 handler가 외부에 공개되지 않고, 성공하면 처음 발급한 final generation을 유지한다. 실패와
  delete 응답 손실은 exact read로 수렴하며 partial scope·membership·runtime object와 같은 registry의 hidden
  factory 재실행이 없는지 확인한다.
- `V11-RACE-37`: Host deadline, preflight completion과 first admission seal을 경쟁시킨다. Deadline이 seal보다
  먼저면 `Blocked/DeadlineExceeded`와 state unchanged, seal이 먼저면 teardown을 진행하고 deadline 초과 시
  `ForceStopped/DeadlineExceeded`로 terminal-once가 되는지 확인한다. 같은 reason을 outcome 전이의 증거로
  대신 사용하지 않는다.
- `V11-RACE-38`: Actor NewOwner CAS, target Entry `OnTransfer`, journal replay, source Entry
  `OnLeaveActor`·membership cleanup과 target replacement를 경쟁시킨다. Current owner·attempt만 target callback과
  replay를 commit하고 authority의 owner·AOG·current Spot이 한 snapshot으로 바뀌며 source cleanup failure가 target
  admission을 Completed·route ACK·steady normalization 전에 열지 않는지 확인한다.

각 E2E cell은 protocol·fixture revision, 양쪽 Framework·binding package와 Core payload의 version·절대
경로·SHA-256을 기록한다. Java와 Kotlin은 JVM runtime을 공유하지만 Java `CompletionStage`와 Kotlin
coroutine·DSL 진입점을 각각 검증한다.

## 15. Performance smoke-only 판정

11.0에서는 성능 수치를 판정하거나 개선하지 않는다. C++·.NET·JVM·Node.js service runtime, Kotlin
consumer와 Core raw runner에 대해 다음 smoke만 release gate로 사용한다.

| Pattern | 최소 동작 |
|---|---|
| `FRAMEWORK_SPOT_PUBSUB` | Hub Spot이 Channel-scoped Logical Multicast로 peer Spot에 한 번 이상 전달함 |
| `FRAMEWORK_SPOT_REQREP` | Peer와 hub 사이의 Spot request와 reply가 한 번 이상 완료됨 |
| `FRAMEWORK_SPOT_SENDSEND` | Peer와 hub가 양방향 one-way submit을 각각 한 번 이상 완료함 |

네 언어 runner는 같은 workload fixture를 사용하지만 service state machine을 공유하지 않는다. Kotlin은 별도
runtime runner를 만들지 않고 Kotlin public API로 JVM runtime startup, 최소 operation과 cleanup을 검증한다.
Loopback, peer 한 개, 작은 고정 payload와 짧은 유한 operation 수만 사용한다.

- runner와 application이 clean build된다.
- server와 client가 시작되고 ready 조건을 충족한다.
- Spot publish, request/reply와 양방향 send가 작은 local topology에서 각각 한 번 이상 완료된다.
- Core raw socket의 대표 send·receive workload가 최소 한 번 완료된다.
- 결과가 versioned schema로 기록되고 Framework package, binding package, Core payload, source revision,
  protocol·fixture revision, build mode, 절대 경로와 SHA-256을 포함한다.
- `RAW-PERF-SMOKE`는 `core/build` runtime을 먼저 다시 build하고 runner가 실제로 load한
  `libzlink` 절대 경로를 출력한다. `core/build` runtime이 `core/src`·`core/include`보다 오래되었거나
  예상한 경로와 실제 load 경로가 다르면 workload 시작 전에 실패한다.
- 잘못된 artifact, 오래된 runtime과 남은 이전 process가 있으면 시작 전에 실패한다.
- timeout, assertion과 child process 비정상 종료가 없다.
- 종료 뒤 process, thread·event-loop handle, timer, pending operation과 endpoint resource가 남지 않는다.

결과 schema는 pattern과 schema version, 닫힌 결과·failure reason, 성공·오류 operation 수, 실행 시각과 child
exit status, platform·architecture·compiler·build mode, runtime 언어, dependency manifest, transport·payload·
peer·operation 수를 기록한다. Framework·binding·Core payload의 version, source revision, 실제 절대 경로와
SHA-256, protocol·workload fixture revision도 포함한다. 처리량이나 latency가 진단용으로 기록되어도 성공
판정에는 사용하지 않는다. Artifact load 경로, provenance, schema·fixture version을 해석할 수 없거나 같은 output
directory의 이전 child process가 남아 있으면 workload를 시작하기 전에 실패한다.

`V11-M2-ORACLE`이 10.x normalized trace와 읽기 전용 archive를 봉인하면 `V11-M3-PERF-LEGACY`에서
`bindings/c/perf`의 다음 active Spot 항목을 제거한다. 이후 같은 workload의 최소 동작은 Core target을
복구하지 않고 `scripts/v11/run-smoke.sh --kind perf`가 네 Framework runtime과 Kotlin consumer에서 검증한다.

- `single/src/perf_spot_pubsub.cpp`, `multi/src/perf_multi_spot_client.cpp`와
  `multi/src/perf_multi_spot_server.cpp`
- `perf_spot_pubsub`, `comp_src_spot_pubsub_*`, `comp_src_spot_reqrep_*`와
  `comp_src_spot_sendsend_*` CMake target
- Bash·PowerShell runner의 `SPOT_PUBSUB`, `SPOT_REQREP`, `SPOT_SENDSEND` pattern
- Spot paired gate, result·metric parser의 Spot mapping, fixture·assertion, CI target와 active README 안내

PAIR, DEALER, ROUTER, classic PUB/SUB와 STREAM raw perf target·result schema는 유지한다. Core 10.x Spot baseline과
result archive도 읽기 전용으로 보존하되 11.0 active result나 latest baseline으로 자동 선택되지 않게 한다.
Default와 glob을 포함한 모든 결과 선택 규칙에서 archive 경로를 명시적으로 제외한다.
Active source·build·runner·test·README에서는 `zlink_spot_`, `zlink_mesh_node_`, `perf_spot`, `SPOT_*`와
`MULTI_SPOT` 잔여를 machine gate로 확인하며 archive 제외 경로를 검사기에 명시한다.

처리량, latency, CPU, RSS, raw ROUTER 대비 비율, 대규모 peer·object·payload matrix, 반복성과 data path
개선은 11.0 완료 조건이 아니다. 별도 성능 작업에서 수행한다.

## 16. Package와 version

- Core service 제거와 raw ABI 변경은 Core `11.0.0` candidate에 반영한다.
- C++, .NET, Java와 Node bindings는 Core 11 raw surface와 같은 `11.0.0` major line을 사용한다.
- C++·.NET·JVM·Node.js server Framework package는 같은 Framework version을 사용한다.
- Java와 Kotlin은 JVM runtime을 공유하지만 Java JAR과 Kotlin JAR·metadata를 각각 생성하고 검증한다.
- Core version과 package는 `V11-R2`, bindings version과 package는 `V11-R3`가 각각 clean을 기록한 뒤에만
  확정한다.
- Framework final package는 `V11-R6` cleanup review 뒤 만들고 `V11-R7`에서 전체 조합을 최종 승인한다.
- Core candidate가 바뀌면 네 bindings native payload, Framework package와 이를 사용한 증거를 다시 만든다.
- 모든 package는 local/internal 위치에만 생성하며 외부 배포를 수행하지 않는다.
- Package manifest는 Framework, binding과 Core payload의 검증된 정확한 조합을 고정한다.

## 17. Candidate 실패와 되돌림

M2의 10.x oracle는 읽기 전용 비교 artifact로 유지한다. 제거 작업이 실패해도 새 candidate가 oracle library를
직접 호출하거나 load하는 fallback을 만들지 않는다.

M3의 Core 제거나 raw 회귀가 실패하면 service symbol 일부에 fake 구현을 추가하지 않는다. M3 candidate 전체를
review 이전 상태로 되돌린 뒤 제거 범위나 raw 보존 코드를 수정한다. `V11-R2`가 clean을 기록하기 전에는 Core
11 package를 배포하지 않는다.

M4의 한 binding lane이 실패하면 해당 lane candidate만 폐기할 수 있다. 다른 binding 결과로 public capability를
대체하지 않으며 Core service projection을 되살리지 않는다. 네 lane이 `V11-R3`를 통과하기 전에는 binding
package를 배포하지 않는다.

M5·M6의 중간 candidate는 public E2E·sample·release 대상으로 사용하지 않는다. 아직 완성되지 않은 operation은
internal contract에서 명시적인 실패로 유지하되 production public path에 `RuntimeNotReady` placeholder를 넣지 않는다.
Test를 통과시키기 위한 fake success·data, shadow backend, private binding 접근이나 compatibility facade로 전환하지 않는다.
Local/internal package 조합이 섞인 상태를 release candidate로 사용하지 않는다.

Protocol schema나 public contract 의미가 바뀌면 영향받는 생성물, runtime lane, fixture와 방향성 E2E만 다시
검증한다. Core 또는 binding payload가 바뀌면 그것을 포함한 package·consumer·E2E·smoke 증거는 다시 만든다.
문서 hash만 바뀐 경우에는 candidate와 review를 되돌리지 않는다.

## 18. 독립 review와 수렴 규칙

Codex와 Claude reviewer는 서로의 finding을 보지 않고 같은 Git base와 candidate manifest의 누적 변경을
독립적으로 검토한다. Candidate의 commit 여부는 review 선행 조건이 아니다.
Review 상태와 finding, 수정 revision, 영향 ID와 재검증 증거는 해당 review 행의 증거 칸에만 기록한다.

Review 축은 다음과 같다.

| 축 | 검토 내용 |
|---|---|
| `I1` | Core raw spec, Framework spec, protocol, 네 runtime, 다섯 public API, sample, E2E와 package의 계약 일치 |
| `I2` | POSD·DDD의 깊은 모듈, 정보 은닉, caller 부담, pass-through, aggregate 경계와 중복 상태 기계 |
| `I3` | 제거 API·source·test·문서·compat helper·dead code, build·package 입력과 참조가 끊긴 파일 |
| `D1` | documentation principles, 현재·목표 상태 구분, 독자와 디렉터리 책임 |
| `D2` | 정식 spec, public API, source, test, diagram과 package의 실제 일치 |

수렴 규칙은 다음과 같다.

1. 1~4회차에는 `Critical`, `High`, `Medium`, `Low`의 모든 유효 finding을 반영한다.
2. 각 회차는 두 reviewer가 같은 기준 revision과 그 회차의 누적 변경을 각각 확인한 한 쌍이다.
3. 5회차부터 `Critical`, `High` 또는 `Medium` finding이 하나라도 있으면 gate를 통과하지 못한다.
4. 5회차 이후 `Low` finding만 남으면 위치, 영향과 처리 결정을 증거 칸에 기록하고 clean으로 종료할 수 있다.
5. 정확성, 공개 계약, data loss, race, security, package mismatch와 제거 누락을 선호 문제로 낮추지 않는다.
6. 두 reviewer가 clean을 기록한 뒤 해당 stage가 소유한 required gate를 다시 실행한다. M5 review는 internal
   contract와 기존 pending catalog를, `V11-R4A`는 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet으로 amended formal
   contract·protocol·impact manifest를 재검증한다.
   M6 runtime review는 internal regression과 execution quarantine만 재실행한다. `V11-R5D`는 Codex `gpt-5.6-sol xhigh`와
   Claude Sonnet이 final E2E·sample spec, impact disposition과 coverage를 독립 review하고 post-review gate를
   재실행한다. 최초 실제 E2E는 `V11-R5D` 뒤 `V11-M6A-E2E`가 시작하고, full matrix는 M7, final package
   재검증은 M9가 소유한다.
7. 이후 source가 바뀌면 의미와 직접 의존 범위만 다시 review한다. Hash 변화만으로 전체 review를 처음부터
   시작하지 않는다.

## 19. 전체 완료 조건

- [x] `SPEC-01~06`과 `V11-R1`이 구현 전에 완료됐다.
- [ ] Framework 공통 정식 spec, Core raw 정식 spec과 다섯 exact interface가 실제 구현과 일치한다.
- [ ] Global identity·remote placement amendment의 미결정이 0이고 정식 spec·다섯 exact interface·protocol에
  모두 흡수됐다.
- [ ] Amendment 독립 review 뒤 두 임시 변경 제안과 repository link가 제거됐으며, 이후 작업이 정식 spec과
  이 ledger의 입력만으로 수행됐다.
- [ ] 영향받은 public member·E2E·sample·registration·regression이 impact manifest에 빠짐없이 분류됐고,
  영향받지 않은 source와 registration은 baseline hash를 유지했다.
- [ ] Core 10.x oracle는 별도 process와 normalized trace로만 사용됐고 새 candidate의 compile·link·load 입력이 아니다.
- [ ] Core service 제거와 POSD·DDD review 뒤에만 Core 11 version과 internal package를 만들었다.
- [ ] 네 bindings service projection 제거와 POSD·DDD review 뒤에만 binding 11 package를 만들었다.
- [ ] Core와 네 bindings에서 ZMP heartbeat option·frame·engine timer projection이 제거됐다.
- [ ] 네 runtime의 application traffic과 독립된 5초 livenessProbe·current connection의 matching ACK 15초
  deadline과 raw monitor·owner lease 책임이 분리됐다.
- [ ] 네 public raw binding과 Store capability proof가 final candidate에서 통과했다.
- [ ] C++·.NET·JVM·Node.js runtime이 해당 언어 public binding API만 사용한다.
- [ ] Java와 Kotlin 공개 surface·ABI·coroutine·metadata를 JVM lane 하나가 각각 검증했다.
- [ ] Topology, messaging, Spot, Actor, Instance Spot, STREAM과 stateful maintenance가 네 runtime에 구현됐다.
- [ ] Runtime 구현 중 E2E·sample은 실행 graph에서만 격리됐고 source 삭제·임시 우회·skip·성공 처리가 없었다.
- [ ] Runtime 완료 뒤 공통 E2E·sample spec을 확정하고 topology→stateful object→maintenance 순서로 E2E를
  활성화해 각 묶음의 runtime gap을 0으로 만들었다.
- [ ] Final E2E·sample spec이 `V11-R5D`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet의 독립 review와 post-review gate를
  통과한 뒤에만 E2E source·registration 변경과 실행을 시작했다.
- [ ] Required contract·race·crash·recovery와 방향이 있는 `4 x 4` E2E가 skipped 없이 통과했다.
- [ ] 전체 E2E 뒤 다섯 언어 sample이 approved source·registration과 public API만 사용해 compile·run됐다.
- [ ] Core service와 네 bindings service projection이 제거됐다.
- [ ] Production scaffold, `RuntimeNotReady` placeholder와 fake success·data branch가 production code에 남지 않았다.
- [ ] Core generic timer·monitor와 raw transport 회귀는 유지되고 제거한 ZMP heartbeat 잔여가 없다.
- [ ] 제거 API 때문에 참조가 끊긴 source·test·sample·build·package 입력이 남지 않았다.
- [ ] Final local/internal package와 clean consumer가 통과했고 외부 배포가 없다.
- [ ] Functional smoke와 performance smoke-only gate가 final package로 통과했다.
- [ ] 처리량·latency·CPU·memory 판정과 성능 개선이 11.0 gate에 포함되지 않았다.
- [ ] 모든 필수 review가 수렴 규칙에 따라 clean으로 끝났다.
- [ ] 모든 상태와 증거가 이 ledger의 담당 행에만 기록됐다.
