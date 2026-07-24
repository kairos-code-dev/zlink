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
| `P-HIGH` | `gpt-5.6-sol high` | Final E2E·sample spec처럼 public contract와 다섯 언어 coverage를 함께 판단하는 교차 영역 독립 review |
| `P-DELIVERY` | `gpt-5.6 medium` | 일반 기능 구현, E2E, package, smoke와 consumer 검증 |
| `P-SCAN` | `gpt-5.6-terra low` 또는 `gpt-5.6-terra medium` | manifest·링크·inventory, 범위 분류와 read-heavy scan |

`P-SCAN`은 단순 수집·no-hit 검사에 `low`를 사용하고 분류 판단이 필요하면 `medium`을 사용한다.
향후 독립 review는 `P-HIGH`를 사용한다. 이미 완료된 row의 `xhigh` evidence는 당시 실제 실행 이력이므로
변경하지 않는다. 재현되지 않은 race나 protocol·ABI 판단을 high effort로 종료할 수 없을 때만 별도 상향
사유와 검토 범위를 해당 행의 증거 칸에 기록한다.

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
| `V11-R7` | final candidate revision, 직접 선행 row의 evidence, §18; Claude `claude-sonnet-5` 병렬 reviewer | source 수정 없음; reviewed base→final candidate 전체 snapshot | §18 독립 review, `DOC`, `INV`, `WIRE`, `DIFF-SNAPSHOT` | final review row, 두 reviewer 결과와 `<ID>` result |
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
| `V11-CA-SPOT-FLUENT` | 승인한 Instance Spot direct messaging 결정, 공통 Spot spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node exact interface, document inventory·trace와 검증 script; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | global SpotId·Spot 전용 fluent call·User-only manager·type inference·internal close parity와 `<ID>` result |
| `V11-CA-RELOCATION-LIFECYCLE` | `CA-D37~CA-D43`, 공통 Spot Actor·Location·maintenance spec과 다섯 언어 exact interface | 공통 Framework spec, C++·.NET·Java·Kotlin·Node Actor·Spot·configuration·Location exact interface, document inventory·trace; runtime·sample·E2E source 변경 0 | `DOC`, `TRACE --refresh-review`, `TRACE --write`, `TRACE --check`, Instance Spot contract self-test, `ROW-GATE`, 공통 | opaque bytes·adapter kind·Snapshot invocation·Restore-before-commit·Entry callback·queue·timer·bounded concurrency parity와 `<ID>` result |
| `V11-CA-USER-SPOT-SPEC`, `V11-CA-USER-SPOT-CORE-RID`, `V11-CA-USER-SPOT-IFACE-CPP`, `V11-CA-USER-SPOT-IFACE-DN`, `V11-CA-USER-SPOT-IFACE-JVM`, `V11-CA-USER-SPOT-IFACE-NODE`, `V11-CA-USER-SPOT-E2E-SPEC`, `V11-CA-USER-SPOT-REVIEW`, `V11-CA-USER-SPOT-DRAFT-RETIRE` | User Spot execution·typed capacity·Framework-issued Spot identity·공통 weight 변경 요청, 원문 §11 UUID v4와 §12 weight 대체 조항 | Core socket spec, Framework 공통·server·HTTP client spec, 다섯 exact interface, common E2E, trace와 ledger; proposal은 review clean 뒤 제거 | `DOC`, `TRACE --refresh-review`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | 원문 요구 추적 100%, 상충·미소유 0, 두 reviewer 결과, proposal·link 0과 각 `<ID>` result |
| `V11-CA-SPOT-ID-SPEC`, `V11-CA-SPOT-ID-WIRE`, `V11-CA-SPOT-ID-IFACE`, `V11-CA-SPOT-ID-E2E`, `V11-CA-SPOT-ID-REVIEW` | Spot transport RID와 global logical Spot ID 분리 결정 | 공통 Spot·Location·Redis spec, service wire schema·generated asset, 다섯 exact interface, common E2E, trace와 ledger | `DOC`, `WIRE-GEN --write`, `WIRE`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | Spot 파생 field에 RoutingId 사용 0, UTF-8 1..255-byte exact string·global namespace·UUID 발급·v3 Store schema·binary legacy 거부와 각 `<ID>` result |
| `V11-CA-ACTOR-CREATE-SPEC`, `V11-CA-ACTOR-CREATE-STORE-WIRE`, `V11-CA-ACTOR-CREATE-IFACE`, `V11-CA-ACTOR-CREATE-E2E`, `V11-CA-ACTOR-CREATE-REVIEW` | Actor 생성 승인·거절과 Entry Spot lifecycle 분리 결정 | 공통 Actor·Spot·Location spec, creation operation record·service wire, 다섯 exact interface, common E2E, trace와 ledger | `DOC`, `WIRE-GEN --write`, `WIRE`, `TRACE --write`, `TRACE --check`, §18 독립 review, `ROW-GATE`, 공통 | Created·Rejected·Existing terminal result, durable reply, Entry admission callback 제거, 일반 복귀·maintenance callback 순서와 각 `<ID>` result |
| `V11-CA-ONE-WAY-SPEC`, `V11-CA-ONE-WAY-IFACE-DN`, `V11-CA-ONE-WAY-IFACE-JVM`, `V11-CA-ONE-WAY-IFACE-NODE`, `V11-CA-ONE-WAY-IFACE-CPP`, `V11-CA-ONE-WAY-PACKAGE-IFACE`, `V11-CA-ONE-WAY-E2E`, `V11-CA-ONE-WAY-REVIEW`, `V11-CA-ONE-WAY-DRAFT-RETIRE`, `V11-M6-ONE-WAY-CPP`, `V11-M6-ONE-WAY-DN`, `V11-M6-ONE-WAY-JVM`, `V11-M6-ONE-WAY-NODE`, `V11-M6-ONE-WAY-PACKAGES`, `V11-CA-ONE-WAY-RUNTIME-JOIN` | One-way submission 변경 요청과 정식 계약 | Server Framework·HTTP Client·Stream Connector spec, 다섯 exact interface, 네 runtime, package adapter, E2E와 유지 중인 request 문서 | `DOC`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework·package test, `ROW-GATE`, 공통 | result-free terminal, bounded admission, package별 naming, runtime·E2E parity. Draft retire 뒤 request와 `temporary_review_documents` 항목 0 |
| `V11-CA-DEFERRED-JOIN-SPEC`, `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E`, `V11-CA-DEFERRED-JOIN-REVIEW`, `V11-CA-OBJECT-CONTEXT-REVIEW`, `V11-CA-MESSAGE-CONTEXT-REVIEW`, `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE`, `V11-M6-DEFERRED-JOIN-CPP`, `V11-M6-DEFERRED-JOIN-DN`, `V11-M6-DEFERRED-JOIN-JVM`, `V11-M6-DEFERRED-JOIN-NODE`, `V11-M6-OBJECT-CONTEXT-CPP`, `V11-M6-OBJECT-CONTEXT-DN`, `V11-M6-OBJECT-CONTEXT-JVM`, `V11-M6-OBJECT-CONTEXT-NODE`, `V11-M6-MESSAGE-CONTEXT-CPP`, `V11-M6-MESSAGE-CONTEXT-DN`, `V11-M6-MESSAGE-CONTEXT-JVM`, `V11-M6-MESSAGE-CONTEXT-NODE` | Deferred Actor Join·Object Context·MessageContext 변경 요청 | 공통 Actor·Spot·message spec, 다섯 exact interface, Config 8·10 E2E, 네 runtime과 유지 중인 request 문서 | `DOC`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework test, `ROW-GATE`, 공통 | process-local barrier, durable accepted 경계, Context composition·field parity. Draft retire 뒤 request와 `temporary_review_documents` 항목 0 |
| `V11-CA-SESSION-BINDING-SPEC`, `V11-CA-SESSION-BINDING-PROTOCOL`, `V11-CA-SESSION-BINDING-IFACE-DN`, `V11-CA-SESSION-BINDING-IFACE-JVM`, `V11-CA-SESSION-BINDING-IFACE-NODE`, `V11-CA-SESSION-BINDING-IFACE-CPP`, `V11-CA-SESSION-BINDING-E2E`, `V11-CA-SESSION-BINDING-REVIEW`, `V11-CA-SESSION-BINDING-DRAFT-RETIRE`, `V11-M6-SESSION-BINDING-CPP`, `V11-M6-SESSION-BINDING-DN`, `V11-M6-SESSION-BINDING-JVM`, `V11-M6-SESSION-BINDING-NODE` | Session–Actor stored route·disconnect 변경 요청 | Session·Actor·Location·maintenance spec, common internals·wire schema, 다섯 exact interface, Config 2·10, 네 runtime과 유지 중인 request 문서 | `DOC`, `WIRE-GEN --check`, `TRACE --write`, `TRACE --check`, §18 독립 review, 대응 Framework test, `ROW-GATE`, 공통 | no-Store relay, physical automatic all-settled 통지, same-generation Completed-only route switch parity. Draft retire 뒤 request와 inventory 항목 0 |
| `V11-M6B-EXEC-CPP`, `V11-M6B-EXEC-DN`, `V11-M6B-EXEC-JVM`, `V11-M6B-EXEC-NODE` | User Spot execution mode·Yield·same-gate 정식 계약과 Config 8 | 각 Framework Actor·Spot mailbox, worker·request call, scheduler와 deterministic contract test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | SpotWide·PerActor ordering, Yield allowlist, same-gate 선거부와 각 `<ID>` result |
| `V11-M6C-BARRIER-CPP`, `V11-M6C-BARRIER-DN`, `V11-M6C-BARRIER-JVM`, `V11-M6C-BARRIER-NODE` | execution mode별 all-lane barrier 계약과 Config 10 | 각 Framework close·snapshot·relocation seal/barrier/abort source와 race test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | yielded continuation·Actor·Spot·timer lane quiescence와 각 `<ID>` result |
| `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE`, `V11-M6C-CAPACITY-MONITORING` | typed capacity·Redis v3·monitoring 정식 계약과 Config 6·7·10 | 네 runtime·공식 Location provider, 다섯 monitoring projection, cross-language Redis v3 fixture와 atomic contract test | 대응 Framework `M6-RUNTIME`, Redis provider test, `DOC`, `ROW-GATE`, 공통 | creation·relocation·aggregate·abort·destroy bundle 원자성, monitoring parity와 각 `<ID>` result |
| `V11-M6B-ENTRY-IDENTITY-CPP`, `V11-M6B-ENTRY-IDENTITY-DN`, `V11-M6B-ENTRY-IDENTITY-JVM`, `V11-M6B-ENTRY-IDENTITY-NODE` | Framework-issued Spot UUID v4·global identity claim·descriptor mapping 계약과 Config 2 | 각 Framework identity generator, User Spot automatic `Create`, descriptor `NewClaim`·cleanup, 공식 Location provider와 contract test | 대응 Framework `M6-RUNTIME`, Redis provider test, `ROW-GATE`, 공통 | UUID version·variant, User·Entry first-conflict 즉시 실패, exact lifecycle claim·immutable digest·cleanup과 각 `<ID>` result |
| `V11-M6A-CORE-RID-UUID` | Core raw socket automatic RID 정식 계약 | Core raw socket RID 생성 regression과 caller-fixed·STREAM 경계 test | `CORE`, `ROW-GATE`, 공통 | exact 16-byte UUID v4 version·variant와 `<ID>` result |
| `V11-M6A-WEIGHT-CPP`, `V11-M6A-WEIGHT-DN`, `V11-M6A-WEIGHT-JVM`, `V11-M6A-WEIGHT-NODE` | 공통 signed weight 0..10000 계약과 Config 1·2·12 | 각 Framework RouteMesh·ClientServer·placement configuration, runtime option, descriptor·selection과 contract test | 대응 Framework `M6-RUNTIME`, `ROW-GATE`, 공통 | boundary·runtime revision·100:300 ratio·weight 0·capacity-first·64-bit 합산·multicast once와 각 `<ID>` result |
| `V11-CA-USER-SPOT-RUNTIME-JOIN` | execution·capacity·Entry identity·weight sub-ID 전체 evidence | source 수정 없음; sub-ID candidate·evidence와 기존 M6A·M6B·M6C row 상태 reconcile | `ROW-GATE`, `DOC`, `TRACE --check`, `DIFF-OWNED`, 공통 | runtime·provider·monitoring·identity·weight gap 0과 `<ID>` result |
| `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | amended formal spec·exact interface, 공통 topology·dispatch·liveness internals, 보존된 Core service 구현·test | 각 Framework topology·dispatch·Location·liveness source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | amended formal spec·exact interface, 공통 mailbox·stateful object·STREAM·resource internals, 보존된 Core service 구현·test | 각 Framework Spot·Actor·STREAM·Instance source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE` | amended formal spec·exact interface, 공통 maintenance·monitoring·resource internals, 보존된 Core service 구현·test | 각 Framework maintenance·monitoring·hosting source와 deterministic internal contract test | 대응 `M6-RUNTIME`, `ROW-GATE`, 공통 | E2E·sample 실행 0, internal regression 누락 0과 각 `<ID>` result |
| `V11-M6-SCAFFOLD-ZERO` | 네 Framework runtime과 amendment impact manifest | production scaffold·placeholder·fake data와 quarantine 실행 graph 검사 결과 | Framework scope 네 `REMOVE`, `AMENDMENT-IMPACT --mode quarantine`, `INV`, `ROW-GATE`, 공통 | production 금지 count 0, sample·E2E source 삭제·임시 우회 0과 `<ID>` result |
| `V11-E2E-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 E2E 계약 | `framework/doc/framework/common/e2e/`, §14와 impact manifest의 approved scenario·registration hash | `DOC`, `TRACE --write`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | required scenario·negative·race·matrix owner와 acceptance 누락 0 |
| `V11-SAMPLE-SPEC-FINAL` | approved amendment, completed runtime, impact manifest, 공통 sample spec | `framework/doc/framework/common/sample/`과 impact manifest의 approved sample·registration hash | `DOC`, `AMENDMENT-IMPACT --mode finalized`, `ROW-GATE`, 공통 | 다섯 언어 sample의 public 흐름·marker·owner 누락 0 |
| `V11-R5D` | final E2E·sample spec candidate, finalized impact manifest, §18의 I1·I2·I3·D1·D2; Claude `claude-sonnet-5` 병렬 reviewer | source 수정 없음; Codex `gpt-5.6-sol high`·Claude Sonnet 독립 finding과 수렴 evidence | §18 독립 review, `DOC`, `TRACE --check`, `AMENDMENT-IMPACT --mode finalized`, `INV`, `DIFF-OWNED` | assertion 약화·coverage 손실·언어 parity gap 0, 두 reviewer 결과와 `<ID>` result |
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
            -> final E2E/sample spec -> gpt-5.6-sol high/Sonnet independent review
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
| `V11-R1` | SPEC 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `SPEC-01`, `SPEC-02`, `SPEC-03`, `SPEC-04`, `SPEC-05`, `SPEC-06` | 완료 | 두 독립 reviewer의 finding을 모두 반영하고 post-fix machine gate 통과 | Base `86258cb9a3ec`, candidate `.artifacts/v11/spec-review-candidate.json`은 repository path·mode·base/current SHA-256을 가진 255개 파일과 direct fixture 19개를 고정한다. 1회차 finding 16건과 2회차 고유 finding 5건을 모두 반영한 뒤 `DOC`, `INV`, `WIRE`, `TRACE`, `INSTANCE`, `SUBMIT`, candidate check와 `git diff --check`가 모두 exit 0이었다. 2026-07-21 사용자가 추가 review를 종료하고 구현 전 계약을 승인했으므로 3회차는 실행하지 않았다. Aggregate와 승인 기록은 candidate와 `.artifacts/v11/evidence/V11-R1/`이 소유하며 ledger 본문에 복제하지 않는다. |

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
| Instance activation 공개 경계 | target·token과 begin/commit/close lifecycle 공개 / global Spot ID fluent call과 factory 등록만 공개 | 후자. Owner claim·fencing·activation barrier는 언어별 runtime이 소유하고 caller는 owner ID, generation, epoch와 native token을 전달하지 않음 |
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
| `V11-R2` | Core 제거·POSD·DDD 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M3-CORE-VERIFY` | 완료 | I1·I2·I3, raw 보존과 제거 범위 review clean | 첫 C++ installed-package configure가 exported OpenSSL dependency 선언 누락을 찾아 Core config에 conditional `find_dependency(OpenSSL)`을 추가했다. 최종 SHA `06d72dab…`에 대해 두 reviewer가 blocking finding 0·clean/approved를 기록했고 package audit은 isolated build·C consumer와 외부 CMake `find_package(zlink)`·OpenSSL target·link·run을 통과했다. R2 `ROW-GATE` 3 files·5 commands 통과. 증거: `.artifacts/v11/evidence/V11-R2/result.json` |
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
| `V11-R3` | Bindings 제거·POSD·DDD 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M4-BIND-JOIN` | 완료 | public raw 경계, generated output, package input과 제거 범위 review clean | 두 독립 reviewer와 package audit가 C++ `b765413a…`, .NET `70aca597…`, JVM `00e3248f…`, Node `9cbfaee1…`, join `a3b8a1a0…`를 승인했다. Actual CMake·NuGet·Maven consumer, Node package consumer tooling, Core provenance·candidate·runtime SHA·SONAME 11, sample/E2E diff 0과 row freshness를 확인했고 blocking finding은 0이다. R3 candidate `8ce37cf7…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R3/result.json` |
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
| `V11-R4` | Runtime foundation 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M5-FOUND-JOIN` | 완료 | schema, public binding 경계, production placeholder 0과 resource ownership review clean | C++/.NET과 JVM/Node·protocol을 나눈 독립 review에서 발견한 no-op·synthetic success·wire frame·terminal cleanup 문제를 모두 제거하고 재검토했다. Blocking finding 0, sample/E2E diff 0이며 candidate `3a7f6399…`의 `ROW-GATE`가 통과했다. 증거: `.artifacts/v11/evidence/V11-R4/result.json`, `.artifacts/v11/evidence/V11-R4-M5/` |

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
| `V11-CA-IMPACT` | E2E·sample·regression 영향 목록과 실행 격리 | E2E·sample·regression lanes, `P-DELIVERY` | `V11-CA-DECISION` | 완료 | 모든 항목 baseline hash·disposition·owner·activation stage 분류, `pending-disabled-by-contract-amendment`, executed·skipped 0 | Approved base `1f5b979675`와 현재 trace의 exact delta에서 public member 추가 2,066·제거 1,481을 생성했다. 제거 member는 exact signature replacement 316개와 closed decision·behavior replacement 1,165개로 분류했다. `CA-D72~CA-D76`의 one-way·Deferred Join·Object Context·MessageContext removal과 `CA-D77`의 publish monitoring removal 61개를 서로 겹치지 않는 독립 규칙으로 추적한다. Cross-language 150개 group과 Kotlin source/JVM 51개 group을 재검증한 결과 unmatched·ambiguous·parity mismatch는 0이다. 전체 4,805개에 대한 `--check`와 quarantine self-test는 pending 4,151, reviewed-source disabled 51, executed·skipped 0, negative mutation 14개로 통과했다. RuntimeMonitoring runner 변경은 이번 amendment의 temporary reviewed source로 소유한다. |
| `V11-CA-JOIN` | Contract amendment 합류 | amendment coordinator, `P-DELIVERY` | `V11-CA-SPEC`, `V11-CA-IFACE-CPP`, `V11-CA-IFACE-DN`, `V11-CA-IFACE-JVM`, `V11-CA-IFACE-NODE`, `V11-CA-PROTOCOL`, `V11-CA-IMPACT` | 완료 | 정식 spec·다섯 interface·wire·impact manifest·trace의 미분류와 semantic drift 0 | Review finding 반영 뒤 `DOC`, `INV`, `TRACE --check`, `WIRE`, `WIRE-GEN --check`, decoder fixture, impact generator `--check`, quarantine self-test, Instance Spot contract와 diff check가 통과했다. Trace는 documents 56, owners 1622, members 6199, unclassified·ambiguous·unknown 0이다. E2E·sample source diff는 0이며 Core PGM·perf는 사용자 확인에 따라 별도 병행 작업으로 제외했다. |
| `V11-R4A` | Contract amendment 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex high review lane, `P-HIGH` | `V11-CA-JOIN` | 완료 | public contract·protocol·영향 disposition·대체 coverage의 I1·I2·I3 review clean | Candidate `ddf7747688`, `9f5a9caa34`, `46da38f29b`, `d5560840f5`, `4a566820c8`의 finding을 모두 반영한 `edc361796a`를 재검토했다. Codex 5.6 sol xhigh와 Claude Sonnet은 blocking finding 0으로 판정했고 별도 policy audit도 clean이다. 제거 member 805개의 replacement는 exact signature 147개와 closed decision·behavior 658개이며, cross-language 76개 group과 Kotlin source/JVM 43개 group의 unmatched·ambiguous·parity mismatch가 0이다. Core PGM·perf는 별도 작업으로 제외했다. |
| `V11-CA-DRAFT-RETIRE` | 임시 contract 변경 제안 흡수 확인과 삭제 | contract coordinator, `P-DELIVERY` | `V11-R4A` | 완료 | 채택 내용은 정식 spec·exact interface·protocol에 모두 존재하고 E2E·sample 영향은 manifest에 분류되며, 미채택·수정 결정은 ledger에 이유가 기록되고 두 proposal과 link가 repository에서 0 | R4A clean 뒤 임시 입력 두 개를 삭제하고 README·ledger·document inventory의 link와 허용 목록을 제거했다. 정식 spec·internals·다섯 exact interface·protocol·impact manifest와 이 ledger만으로 M6 입력을 구성하며 `DOC`, `TRACE --check`, impact quarantine self-test와 repository link 0 검증이 통과했다. 증거: `.artifacts/v11/evidence/V11-CA-DRAFT-RETIRE/result.json` |
| `V11-CA-SPOT-FLUENT` | Instance Spot fluent cold activation 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | global SpotId 시작 method, Spot 전용 fluent call, User-only manager, type inference·initial Mesh·internal close가 공통 spec과 다섯 exact interface에서 일치하고 source·sample·E2E 변경 0 | `InstanceSpotAddress`를 복구하지 않고 Spot direct fluent call에 explicit Instance intent를 고정했다. 후속 `CA-D47`은 Missing activation의 source-side reservation을 제거했다. Source는 first-message activation envelope를 target에 제출하고 target CAS winner가 generic reservation으로 authority와 pending capacity를 함께 확보하며 Ready commit 뒤 envelope message를 local queue에 한 번 제출한다. Spot create terminal result는 `SpotRef`, 세 state(`Existing`, `Created`, `Rejected`)와 optional reply를 반환한다. 최초 contract self-test와 R4B review는 source-side reservation 문구를 대상으로 했으므로 target-owned activation 변경은 `V11-R5B`와 최종 E2E review에서 다시 검증한다. |
| `V11-CA-RELOCATION-LIFECYCLE` | Actor·Spot relocation adapter와 Entry Spot lifecycle 계약 교정 | contract·language lanes, `P-DEEP` | `V11-CA-DRAFT-RETIRE` | 완료 | opaque bytes adapter, Snapshot invocation scope, Restore-before-commit, Entry Spot callback, queue·timer와 bounded concurrency 규칙이 공통 spec·다섯 exact interface에서 일치하고 이전 `Transfer*` 공개 이름 0 | `CA-D37~CA-D43`을 공통 spec과 다섯 언어 exact interface에 반영했다. Public contract와 protocol vocabulary를 `Relocation`으로 변경하고 호환 alias는 두지 않았다. Adapter state는 최대 64 MiB opaque bytes이며 factory·Restore와 journal validation·staging을 authority commit 전에 끝낸다. Entry Spot에는 commit 뒤 `OnActorRelocated`를 알린다. Current turn 하나만 source에서 완료하고 미실행 queue, accepted journal, logical timer registration과 pending tick은 Framework가 payload에 포함해 복원한다. Permit을 얻은 ready unit부터 이전하며 process 기본값은 outbound·inbound 64, Capture·Restore 8, payload in-flight 256 MiB다. Spot closing reason은 `RelocationOut`으로 통일한다. Runtime 구현과 최종 독립 review는 `V11-R4B`에서 clean 판정을 받았다. |
| `V11-R4B` | Instance Spot fluent·relocation lifecycle 계약 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex high review lane, `P-HIGH` | `V11-CA-SPOT-FLUENT`, `V11-CA-RELOCATION-LIFECYCLE` | 완료 | global identity·cold activation·type inference·User-only manager, relocation adapter·callback·failure와 다섯 언어 parity의 I1·I2·I3 review clean | Codex 5.6 sol xhigh가 찾은 standalone Actor old Entry cleanup-before-replay, User Spot aggregate participant cardinality, exact request-source terminal identity, sequence domain, Java·Kotlin Instance timer, 이전 용어·metric과 impact hash 불일치를 모두 수정했다. Wire는 37 commands·157 types·36 bounds와 negative self-test 186개, DOC은 formal 137·exact 56, trace는 owner 1,650·member 6,389·미분류 0, impact quarantine은 3,458개 중 pending 2,850·executed/skipped 0으로 통과했다. 최종 Codex 재검토와 Claude Sonnet focused review는 모두 `CLEAN`이다. |

#### 9.1.1 User Spot execution·capacity·Entry identity 후속 amendment

이 amendment는 M6 구현 중 확인한 User Spot 실행 경계, capacity 계층과 Entry Spot identity의 계약 공백을
정식 입력으로 닫는다. 기존 M6 증거는 폐기하지 않지만, 아래 계약에 직접 영향을 받는 scheduler, Location
Store, provider와 monitoring 결과는 새 계약을 구현하고 재검증하기 전까지 완료 근거로 사용하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-CA-USER-SPOT-SPEC` | User Spot execution mode·Yield·capacity·Entry RID·공통 weight 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-R4B` | 완료 | 공통·server spec에 `CA-D57~CA-D64`와 원문 §11 UUID v4·§12 weight 대체 조항 누락 0, 기존 상충 문구 0 | 변경 요청의 23개 반영 항목과 §11·§12 추가 항목을 execution·capacity·identity·weight owner에 나누어 정식 spec에 반영했다. Positive weight 범위의 원문 오기는 공통 허용 범위와 같은 `1..10000`으로 확정했다. |
| `V11-CA-USER-SPOT-CORE-RID` | Core raw socket automatic RID 계약 확인 | Core contract lane, `P-DEEP` | `V11-R4B` | 완료 | 미지정 raw socket RID가 RFC 4122 UUID v4 bit layout의 16-byte binary이고 Framework 문자열 형식과 혼합되지 않음 | Core socket 정식 spec의 `set/get routing ID` 계약에 caller 미지정 기본값과 raw binary 표현을 한국어·영어로 고정했다. |
| `V11-CA-USER-SPOT-IFACE-CPP` | C++ exact interface 갱신 | C++ contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | C++ declaration·example·provider request·monitoring shape를 공통 계약에 맞췄고 placement·descriptor·monitoring weight를 signed `int`로 통일했다. |
| `V11-CA-USER-SPOT-IFACE-DN` | .NET exact interface 갱신 | .NET contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | .NET exact declaration을 공통 계약에 맞췄고 `CancellationToken`은 .NET 표현에만 유지했다. Weight monitoring도 signed `int`로 통일했다. |
| `V11-CA-USER-SPOT-IFACE-JVM` | Java·Kotlin exact interface 갱신 | JVM contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | Java ABI와 Kotlin suspend surface가 같은 실행·capacity·weight 의미를 제공 | Java ABI와 Kotlin coroutine 표현에 같은 execution·capacity·identity·weight 의미를 고정했다. Kotlin execution permit 연결은 runtime sub-ID가 검증한다. |
| `V11-CA-USER-SPOT-IFACE-NODE` | Node.js exact interface 갱신 | Node.js contract lane, `P-DEEP` | `V11-CA-USER-SPOT-SPEC` | 완료 | execution mode, 제한된 Yield, typed capacity, Entry RID와 signed weight 0..10000이 공통 계약과 일치 | Node.js exact declaration에 Actor claim·Spot gate, typed capacity, Framework-issued Spot identity와 signed weight 계약을 반영했다. Runtime mailbox 연결은 별도 sub-ID로 유지한다. |
| `V11-CA-USER-SPOT-E2E-SPEC` | 공통 E2E scenario와 impact disposition 갱신 | E2E coordinator, `P-DELIVERY` | `V11-CA-USER-SPOT-SPEC` | 완료 | Config 1·2·5·6·7·8·9·10·11·12·13·14와 catalog에 실행·capacity·identity·weight scenario, race와 언어 parity 누락 0 | Config 1·2·5·6·7·8·9·10·11·12·13·14와 catalog에 ordering·barrier·typed capacity·UUID collision·descriptor cleanup·weight boundary/revision/selection 검증을 반영했다. |
| `V11-CA-USER-SPOT-REVIEW` | 정식 흡수 완전성·교차 일관성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-USER-SPOT-CORE-RID`, `V11-CA-USER-SPOT-IFACE-CPP`, `V11-CA-USER-SPOT-IFACE-DN`, `V11-CA-USER-SPOT-IFACE-JVM`, `V11-CA-USER-SPOT-IFACE-NODE`, `V11-CA-USER-SPOT-E2E-SPEC` | 완료 | 원문 23개 항목과 §11 UUID v4·§12 weight 추가 항목의 spec·exact interface·E2E·runtime row 추적 100%, 상충·미소유 0 | Codex 5.6 sol xhigh가 최종 candidate를 `CLEAN`으로 판정했다. Claude Sonnet focused review는 execution·capacity와 identity·weight를 각각 `APPROVE`했다. Broad budget-limited review가 제기한 User Spot automatic UUID 미소유 주장은 `24-spot-address-messaging`과 다섯 Spot exact interface의 명시 계약으로 해소했고, flow tracing ID 제안은 원문 요구가 아니므로 범위에 추가하지 않았다. Trace는 owners 1734, members 6637, unclassified·ambiguous·unknown 0이다. |
| `V11-CA-USER-SPOT-DRAFT-RETIRE` | 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-USER-SPOT-REVIEW` | 완료 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | 두 독립 reviewer 결과와 자동 검증을 확보한 뒤 proposal과 README link를 제거했다. 이후 구현은 정식 spec·exact interface·E2E·아래 runtime sub-ID만 사용한다. |
| `V11-CA-SPOT-ID-SPEC` | SpotId logical identity 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Entry·User·Instance가 global UTF-8 string SpotId를 사용하고 NodeRid만 RoutingId를 사용하며 network identity 문서의 Spot identity 소유 0 | 공통 Spot·Location·Redis·monitoring·relocation 문서의 `SpotRid` 표면을 `SpotId`로 전환하고 identity 규칙을 `24-spot-address-messaging`으로 이동했다. UTF-8 1..255 bytes, case-sensitive exact match, no normalization과 global namespace를 정식 owner에 고정했다. |
| `V11-CA-SPOT-ID-WIRE` | SpotId wire·Store schema clean break | protocol·Location contract lane, `P-DEEP` | `V11-CA-SPOT-ID-SPEC` | 완료 | 모든 Spot field가 `text8`·`optional-text8`, generated drift 0, Redis `location-authority-hybrid-v3` fixture와 legacy arbitrary binary 거부 | Service wire target field를 `spotId` 계열과 UTF-8 text로 바꾸고 Redis v3 key·claim field fixture를 작성했다. Candidate `342cd38221`에서 WIRE는 40 commands·167 types와 negative self-test 233건을 통과했다. |
| `V11-CA-SPOT-ID-IFACE` | 다섯 언어 exact interface SpotId 전환 | public contract lanes, `P-DEEP` | `V11-CA-SPOT-ID-SPEC` | 완료 | .NET·Java·Kotlin·Node는 string, C++는 `std::string`이며 Spot 파생 public member에 RoutingId 사용 0 | 공통 이름을 `SpotId` 계열로 바꾸고 다섯 언어의 SpotRef·messaging·manager·context·Location·monitoring 선언을 string projection으로 전환했다. 후속 audit에서 Java generated inventory의 `SpotRef`, manager `getOrCreate`·`find`, Actor `joinSpot`과 Kotlin Spot send/request extension에 남은 RoutingId signature 6건을 발견해 `String`으로 교정했다. Node transport identity의 NodeRid만 RoutingId를 유지한다. |
| `V11-CA-SPOT-ID-E2E` | SpotId E2E 계약과 compatibility negative 갱신 | E2E coordinator, `P-DELIVERY` | `V11-CA-SPOT-ID-SPEC`, `V11-CA-SPOT-ID-WIRE` | 완료 | cross-node global ID, UTF-8 boundary·case exact·no normalization, reserved Entry 형식, legacy binary reject scenario 누락 0 | Common E2E `SM-A13`과 catalog `M93`에 1·255-byte 성공, 256-byte 선거부, case·Unicode exact distinction, reserved Entry 형식과 invalid UTF-8 legacy frame의 side-effect 없는 거부를 고정했다. |
| `V11-CA-SPOT-ID-REVIEW` | SpotId 흡수 완전성·교차 일관성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-SPOT-ID-WIRE`, `V11-CA-SPOT-ID-IFACE`, `V11-CA-SPOT-ID-E2E` | 완료 | spec·wire·Store·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | Candidate `342cd38221`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet focused review가 actionable finding 0으로 `CLEAN`을 반환했다. Runtime 전환은 `V11-M6B-ENTRY-IDENTITY-*`가 소유하며 계약 미소유 항목은 0이다. |
| `V11-CA-ACTOR-CREATE-SPEC` | Actor 생성 승인·거절과 Entry Spot lifecycle 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | 최초 생성·User→Entry 복귀·maintenance 복원이 서로 다른 callback을 사용하고 Entry Spot에 admission callback 노출 0 | Shared membership lifecycle, User Spot admission lifecycle과 Entry Spot creation·relocation lifecycle을 분리했다. 최초 create callback의 `Rejected`는 해당 attempt만 cleanup하고 별도 operation은 새 reservation과 callback을 실행한다. |
| `V11-CA-ACTOR-CREATE-STORE-WIRE` | Actor creation terminal result와 durable reply | protocol·Location contract lane, `P-DEEP` | `V11-CA-ACTOR-CREATE-SPEC` | 완료 | Created·Rejected terminal result와 opaque reply envelope가 exact source lifecycle·OperationId에 고정되고 Ready publication·rejection cleanup과 원자적으로 닫히며 다른 operation 공유 0 | Service wire command 49와 Actor create terminal union을 추가했다. 같은 source lifecycle·OperationId만 retained semantic terminal을 replay하고 다른 operation은 callback 결과를 공유하지 않도록 Store와 schema에 고정했다. Candidate `342cd38221`의 WIRE 233개 negative self-test가 통과했다. |
| `V11-CA-ACTOR-CREATE-IFACE` | 다섯 언어 Actor manager·Spot lifecycle exact interface 갱신 | public contract lanes, `P-DEEP` | `V11-CA-ACTOR-CREATE-SPEC` | 완료 | Existing·Created·Rejected union과 callback 분리가 다섯 언어에서 같은 의미를 제공 | C++·.NET·Java·Kotlin·Node exact interface에 single-use Create·GetOrCreate call, Existing·Created·Rejected result와 분리된 Entry/User Spot lifecycle callback을 반영했다. DOC·trace의 unclassified·ambiguous·unknown은 0이다. |
| `V11-CA-ACTOR-CREATE-E2E` | Actor creation rejection·Entry lifecycle E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-ACTOR-CREATE-SPEC`, `V11-CA-ACTOR-CREATE-STORE-WIRE` | 완료 | Actor별 callback 직렬화, rejection 뒤 독립 request 재시도, 동일 OperationId terminal replay, rejection cleanup, 일반 복귀와 maintenance callback sequence 누락 0 | Common E2E `SM-A14`와 catalog에 concurrent distinct operation, rejection cleanup·재경쟁, same OperationId replay, Ready visibility와 Entry lifecycle sequence를 추가했다. |
| `V11-CA-ACTOR-CREATE-REVIEW` | Actor creation·Entry lifecycle 흡수 완전성 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-ACTOR-CREATE-STORE-WIRE`, `V11-CA-ACTOR-CREATE-IFACE`, `V11-CA-ACTOR-CREATE-E2E` | 완료 | spec·Store·wire·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | Candidate `342cd38221`에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet focused review가 command 49, operation-scoped replay, staging visibility, local·remote 동일 후보 규칙과 absolute deadline을 대조해 actionable finding 0으로 `CLEAN`을 반환했다. |
| `V11-CA-ONE-WAY-SPEC` | One-way 반환·오류·terminator naming 정식 계약 통합 | contract coordinator, `P-DEEP` | `V11-CA-ACTOR-CREATE-REVIEW` | 완료 | `CA-D72~CA-D73`이 Server Framework·Stream Connector·HTTP Client 정식 spec에 반영되고 result status·`SubmitAsync`·generic `async` terminator 상충 0 | 변경 요청 827행을 검토해 Server, HTTP Client와 Stream Connector 정식 spec에 result-free one-way completion, send timeout까지 capacity 대기와 exceptional completion을 반영했다. Logical Multicast target count의 monitoring 소유권은 후속 `CA-D77`이 제거했다. 미결정 오류는 operation별 기존 not-found kind와 `RuntimeShutdown=36`으로 닫고 세 package naming을 같은 snapshot으로 고정했다. `verify-framework-submit-api.sh --contract`가 languages=5, result_free=1, fanout_overloads=10으로 통과했다. |
| `V11-CA-ONE-WAY-IFACE-DN` | .NET one-way·worker exact interface | .NET contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | Messaging one-way `ValueTask Async`, direct relay `ValueTask`, request·worker·create 결과 유지와 제한된 `Yield`가 일치 | Canonical .NET Server exact interface 8개에서 one-way builder를 `ValueTask Async`, direct relay를 `ValueTask RelayAsync`로 고정하고 submit·publish result/status/detail public type을 제거했다. Actor·Spot create/get-or-create의 제한된 `Yield`와 `RuntimeShutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring 소유도 제거했다. 대상 old symbol 0, code fence 짝수와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-JVM` | Java·Kotlin one-way·worker exact interface | JVM contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | Java `CompletionStage<Void> submit`, Kotlin 전용 `await`·`yield` wrapper와 SpotId String parity가 일치 | Canonical Java 6개와 Kotlin 5개 exact interface에서 one-way를 `CompletionStage<Void> submit`과 Kotlin `await(): Unit`으로 고정하고 public submit·publish result/status/detail type을 제거했다. Request·worker·create result와 제한된 `yield`를 유지하고 Spot address·descriptor·location은 String SpotId, RoutingId는 NodeRid로만 사용하도록 정리했다. `RUNTIME_SHUTDOWN=36`, operation별 기존 not-found mapping, old result·Kotlin alias·Spot RoutingId misuse 0과 code fence·`git diff --check` 통과를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-NODE` | Node.js one-way·worker exact interface | Node.js contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | One-way `Promise<void> submit`, worker `submit`, request·create 결과와 제한된 `yield`가 일치 | Canonical Node.js Server exact interface 5개에서 one-way `submit`·direct `relay`를 `Promise<void>`로 고정하고 public submit·publish result/status/detail type을 제거했다. Actor·Spot create/get-or-create의 제한된 `yield`와 `RuntimeShutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring count도 제거했다. 대상 old symbol 0, code fence 짝수와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-IFACE-CPP` | C++ one-way·worker exact interface | C++ contract lane, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | One-way `task_t<void> submit`, request·worker `submit`, create 결과와 제한된 `yield`가 일치 | Canonical C++ Server exact interface 4개에서 one-way `submit`·direct `relay`를 `task_t<void>`로 고정하고 public submit·publish result/status/detail type을 제거했다. Actor·Spot create/get-or-create의 제한된 `yield`와 `runtime_shutdown=36`을 반영했으며 후속 `CA-D77`에 따라 Logical Multicast monitoring count도 제거했다. 대상 old symbol 0, result-bearing `submit`은 create 2건만 남았고 code fence와 `git diff --check`가 통과했다. |
| `V11-CA-ONE-WAY-PACKAGE-IFACE` | Stream Connector·HTTP Client terminator naming exact interface | package contract lanes, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | .NET `Async`, Kotlin `await`, Java·C++ `submit`, Node package별 충돌 없는 naming과 기존 실행 경계가 일치하고 실제 runtime signature 충돌 0 | HTTP Client와 Stream Connector canonical exact interface 전 언어를 같은 naming snapshot으로 정렬했다. .NET은 `Async`·`AsyncRaw`, Kotlin wrapper는 `await`, Java·C++와 Node Stream Connector는 `submit` 계열을 사용한다. One-way는 .NET `ValueTask`, Kotlin `Unit`, Java `CompletionStage<Void>`, Node.js `Promise<void>`로 결과값 없이 완료한다. C++ HTTP Client는 `task_t<void>`를 사용하지만 Stream Connector core는 기존 no-exception·no-coroutine 경계를 보존해 `void submit()`을 유지하고 error event로 실패를 보고한다. Node HTTP Client는 TypeScript generic erasure와 Server builder 상속 때문에 no-argument typed response와 one-way `submit()`을 구분할 수 없으므로 typed response에 `async<T>()`를 유지한다. Request·response와 callback result API는 유지했다. |
| `V11-CA-ONE-WAY-E2E` | One-way admission·error·partial publish E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-SPEC` | 완료 | immediate admission, delayed capacity, timeout·cancellation·shutdown race, target·route error, zero target와 partial multicast scenario 누락 0 | Config 13의 기존 SA-E2E-01~20·SA-REG-01~04를 유지하면서 result-free completion, immediate·delayed admission, bounded pending, timeout·cancellation·shutdown race와 late admission 차단, target·route exception, classic fanout subscriber 0과 Logical Multicast target 0 정상 완료, partial publish rollback·retry 금지를 반영했다. 후속 `CA-D77`에 따라 Config 7·13은 publish 전용 snapshot·metric·runtime event·message-flow count 부재를 검증한다. Legacy status/result 표현 0, scenario 20+4 유지와 `git diff --check`를 확인했다. |
| `V11-CA-ONE-WAY-REVIEW` | One-way 정식 흡수와 package parity 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-ONE-WAY-IFACE-DN`, `V11-CA-ONE-WAY-IFACE-JVM`, `V11-CA-ONE-WAY-IFACE-NODE`, `V11-CA-ONE-WAY-IFACE-CPP`, `V11-CA-ONE-WAY-PACKAGE-IFACE`, `V11-CA-ONE-WAY-E2E` | 진행 | 원문 항목의 spec·interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | 1차 review의 bounded pending, multicast timeout, Kotlin wrapper, Java·Node monitoring, duplicate terminal, Java public helper와 Node HTTP naming 지적을 수정했다. 2차 Codex xhigh review에서 Logical Multicast committed partial 결과, 세 runtime의 unbounded waiter, Kotlin metadata·Actor error와 Java helper 재노출을 추가 확인했고 Claude Sonnet은 Node Actor·bound-session의 고정 1초 timeout을 확인했다. .NET 수정과 C++ direct handoff·saturation gate, Node bounded waiter·configured timeout, JVM bounded handoff·Kotlin parity를 통과했으며 Java 내부 helper 비공개화 뒤 Codex high와 Claude Sonnet의 최종 clean 재검토가 남았다. |
| `V11-CA-ONE-WAY-DRAFT-RETIRE` | One-way 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-REVIEW` | 대기 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | — |
| `V11-M6-ONE-WAY-CPP` | C++ Server runtime의 result-free one-way admission | C++ runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-CPP` | 진행 | 반환 데이터 0, bounded admission·single terminal contract test 통과 | Direct handoff와 saturation·timeout·recovery를 유지하면서 Logical Multicast caller를 worker dequeue 시점에 완료하고 target 지연·실패는 내부에서 관측하도록 분리했다. Publish message-flow trace를 제거하고 classic fanout metric은 channel 경로에서만 기록한다. Async task·messaging·message-flow focused executable이 통과했으며 독립 final review 전에는 완료로 전환하지 않는다. |
| `V11-M6-ONE-WAY-DN` | .NET Server runtime의 result-free one-way admission | .NET runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-DN` | 진행 | 반환 데이터 0, bounded admission·single terminal contract test 통과 | Bounded capacity·multicast timeout·single-use 48개를 유지하면서 Logical Multicast caller를 worker commit 직후 완료하고 target 지연·실패를 generic runtime error sink로 분리했다. Logical Multicast focused 6/6과 Framework build가 통과했으며 publish message-flow trace는 없다. 독립 final review 전에는 완료로 전환하지 않는다. |
| `V11-M6-ONE-WAY-JVM` | JVM Server runtime의 result-free one-way admission | JVM runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-JVM` | 진행 | 반환 데이터 0, bounded admission·Kotlin parity contract test 통과 | Bounded multicast handoff·Kotlin Spot/metadata/error parity를 유지하면서 caller를 worker commit 직후 완료하고 target 처리 실패는 caller terminal과 분리했다. Target별 monitoring projection과 Logical Multicast·classic fanout Publish trace를 제거했으며 publisher focused test가 통과했다. 독립 final review 전에는 완료로 전환하지 않는다. |
| `V11-M6-ONE-WAY-NODE` | Node.js Server runtime의 result-free one-way admission | Node.js runtime lane, `P-DEEP` | `V11-CA-ONE-WAY-IFACE-NODE` | 진행 | 반환 데이터 0, bounded admission·configured timeout contract test 통과 | Bounded waiter·payload 없는 overflow deadline·mesh별 configured timeout을 유지하면서 publish worker slot 확보 직후 caller를 완료하고 target Promise의 지연·실패와 late abort를 분리했다. Logical Multicast focused 9/9와 build가 통과했고 Publish success trace를 제거했다. 독립 final review 전에는 완료로 전환하지 않는다. |
| `V11-M6-ONE-WAY-PACKAGES` | Stream Connector·HTTP Client naming·adapter 구현 | package implementation lanes, `P-DELIVERY` | `V11-CA-ONE-WAY-PACKAGE-IFACE` | 완료 | exact interface·source·package consumer에서 제거된 terminal과 실제 runtime signature 충돌 0 | .NET HTTP Client·Connector test 63개·141개, Java·Kotlin package 4개 Gradle test, C++ HTTP 57개·sample 4개 build가 통과했다. Node Connector와 HTTP server one-way는 `Promise<void>`, raw response는 `submitRaw`, typed response·callback은 CA-D73 예외의 `async<T>`로 맞췄으며 전체 build, HTTP 36/36과 packaged contract 7개가 통과했다. C++ Connector는 no-coroutine `void submit()`을 유지한다. |
| `V11-CA-ONE-WAY-RUNTIME-JOIN` | One-way contract·runtime·package 합류 | amendment coordinator, `P-DELIVERY` | `V11-CA-ONE-WAY-DRAFT-RETIRE`, `V11-M6-ONE-WAY-CPP`, `V11-M6-ONE-WAY-DN`, `V11-M6-ONE-WAY-JVM`, `V11-M6-ONE-WAY-NODE`, `V11-M6-ONE-WAY-PACKAGES` | 대기 | 정식 계약, 네 runtime, Kotlin wrapper와 Connector·HTTP package consumer gap 0 | — |
| `V11-CA-DEFERRED-JOIN-SPEC` | Deferred Actor Join 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 수정 진행 | Defer barrier, Yield 구분, completion durability, bounded aggregate commit과 오류 37..39 상충 0 | High review finding을 반영해 process-local activation, outcome별 durable 범위, 64 Join·8 MiB cap, transition race와 bounded aggregate commit을 확정했다. Context composition과 MessageContext는 별도 review/runtime gate가 소유한다. |
| `V11-CA-DEFERRED-JOIN-IFACE-DN` | .NET deferred Join·Context exact interface | .NET contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 검토 중 | Context-only factory, `Defer`, completion과 MessageContext target signature 누락 0 | P0 durable·scope 결정이 끝날 때까지 signature candidate를 완료로 판정하지 않는다. |
| `V11-CA-DEFERRED-JOIN-IFACE-JVM` | Java·Kotlin deferred Join·Context exact interface | JVM contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 검토 중 | Java 동기 `defer`, Kotlin coroutine wrapper 0, Context·completion·error parity 누락 0 | Kotlin coroutine wrapper를 만들지 않는 방향은 유지하되 P0 결정 전 완료로 판정하지 않는다. |
| `V11-CA-DEFERRED-JOIN-IFACE-NODE` | Node.js deferred Join·Context exact interface | Node.js contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 검토 중 | Promise를 반환하지 않는 `defer`, containing Spot handler와 Context·completion parity 누락 0 | P0 durable·scope 결정이 끝날 때까지 signature candidate를 완료로 판정하지 않는다. |
| `V11-CA-DEFERRED-JOIN-IFACE-CPP` | C++ deferred Join·Context exact interface | C++ contract lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-SPEC` | 검토 중 | `void defer()`, Context-only factory, completion variant와 message context parity 누락 0 | P0 durable·scope 결정이 끝날 때까지 signature candidate를 완료로 판정하지 않는다. |
| `V11-CA-DEFERRED-JOIN-E2E` | Deferred Join·queue·Context·error E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-DEFERRED-JOIN-SPEC` | 검토 중 | handler terminal·failure, Yield, queue hold, completion, same/cross-node Context와 오류 parity scenario 누락 0 | Config 8·10 candidate를 반영했지만 P0 durable·scope 결정에 맞춘 crash·recovery scenario 보강이 남았다. |
| `V11-CA-DEFERRED-JOIN-REVIEW` | Deferred Join 변경 요청 흡수 완전성 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E` | 수정 대기 | 변경 요청 항목의 spec·다섯 interface·E2E·runtime owner 추적 100%, 상충·미소유 0, 두 reviewer clean | P0 세 항목의 계약 결정을 먼저 닫아야 한다. 변경 요청 문서는 review가 끝날 때까지 유지한다. |
| `V11-CA-OBJECT-CONTEXT-REVIEW` | Actor·Spot Context composition 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP` | 대기 | Context-only factory, exact identity, same/cross-node generation·fence와 containing Spot thread-safety review clean | Deferred Join durability 판단과 분리해 object Context 계약만 검토한다. |
| `V11-CA-MESSAGE-CONTEXT-REVIEW` | MessageContext naming·field parity 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-HIGH` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-E2E` | 대기 | nullable Mesh·Channel, correlation, cancellation ownership, specialized context와 제거 surface review clean | Universal MessageContext에는 connection cancellation을 포함하지 않는다. |
| `V11-M6-DEFERRED-JOIN-CPP` | C++ runtime deferred Join 전환 | C++ runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-CPP`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Result-bearing Join을 `defer()`로 전환하고 serial turn의 정상 terminal 뒤 등록 순 activation, handler failure 폐기, 최대 64개, detached·duplicate·concurrent transition 차단과 remaining absolute timeout 전달을 구현했다. Framework·header contract build/run은 통과했다. Cross-node Accepted OperationId를 durable relocation record와 target Actor mailbox callback에 연결하는 작업, callback 기반 sample/E2E 전환과 execution baseline 첫 assertion 실패가 남았다. Compatibility alias는 추가하지 않았다. |
| `V11-M6-DEFERRED-JOIN-DN` | .NET runtime deferred Join 전환 | .NET runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-DN`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Public Join을 동기 `Defer`와 `OnJoinCompletedAsync` completion으로 전환했다. Cross-node Accepted completion은 Relocation Store immutable root에 operation ID·raw reply·ActorRef·generation과 `Prepared→Committed→Delivered` cursor를 기록하고 target Actor mailbox에서 callback을 실행한다. Actor bind recovery, callback retry, authority reference 해제 뒤 root delete를 검증했으며 focused durability·mailbox·contract 6/6과 Framework build가 통과했다. 전체 767 pass/17 fail의 실패는 다른 진행 항목이다. 전체 cross-node Actor state·queue·timer materialization과 callback 기반 sample/E2E 전환은 상위 relocation gap으로 남아 있다. |
| `V11-M6-DEFERRED-JOIN-JVM` | JVM runtime deferred Join 전환 | JVM runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-JVM`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Java public Join을 `timeout`+동기 `defer`로 전환했다. User·Entry Spot와 timer handler의 여러 member Actor intent를 한 terminal scope로 묶고 active turn 직후 기존 queued application turn보다 먼저 Actor mailbox barrier를 실행한다. Routed cross-node User Spot Accepted는 Relocation Store root와 commit wire에 operation ID·raw reply·generation·cursor를 보존하고 target mailbox callback, retry·dedupe와 backlog 순서를 적용했다. Java core 517/517와 Kotlin projection test가 통과했다. Core-native `JoinEntrySpot`·native Spot path는 Framework operation ID와 manifest를 전달할 wire field가 없어 Core·bindings 변경 없이 durable target replay를 닫을 수 없으며 callback 기반 sample/E2E 전환도 남아 있다. |
| `V11-M6-DEFERRED-JOIN-NODE` | Node.js runtime deferred Join 전환 | Node.js runtime lane, `P-DEEP` | `V11-CA-DEFERRED-JOIN-IFACE-NODE`, `V11-CA-DEFERRED-JOIN-REVIEW` | 진행 | process-local barrier·completion durability·queue ordering test 통과 | Public Join을 result-free `defer`로 전환하고 handler-local scope의 정상 terminal 뒤 순서 실행, exception 폐기, 64개·8 MiB, single-use·moving·absolute deadline과 same-node completion callback을 구현했다. Build와 focused contract/deferred 31/31이 통과했다. Cross-node Accepted OperationId의 durable target Actor mailbox callback과 submit/result 기반 기존 actor-manager scenario 전환이 남아 있다. Source에서 잘못된 remote success callback을 대신 호출하지 않으며 compatibility alias도 추가하지 않았다. |
| `V11-M6-OBJECT-CONTEXT-CPP` | C++ object Context composition | C++ runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 대기 | Context-only factory·identity·source fence test 통과 | Deferred Join과 별도 gate로 진행한다. |
| `V11-M6-OBJECT-CONTEXT-DN` | .NET object Context composition | .NET runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 대기 | Context-only factory·identity·source fence test 통과 | Deferred Join과 별도 gate로 진행한다. |
| `V11-M6-OBJECT-CONTEXT-JVM` | JVM object Context composition | JVM runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 대기 | Context-only factory·identity·source fence test 통과 | Deferred Join과 별도 gate로 진행한다. |
| `V11-M6-OBJECT-CONTEXT-NODE` | Node.js object Context composition | Node.js runtime lane, `P-DEEP` | `V11-CA-OBJECT-CONTEXT-REVIEW` | 대기 | Context-only factory·identity·source fence test 통과 | Deferred Join과 별도 gate로 진행한다. |
| `V11-M6-MESSAGE-CONTEXT-CPP` | C++ MessageContext 통일 | C++ runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 대기 | context field·marker 제거와 dispatch test 통과 | Object Context와 별도 gate로 진행한다. |
| `V11-M6-MESSAGE-CONTEXT-DN` | .NET MessageContext 통일 | .NET runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 대기 | context field·marker 제거와 dispatch test 통과 | Object Context와 별도 gate로 진행한다. |
| `V11-M6-MESSAGE-CONTEXT-JVM` | JVM MessageContext 통일 | JVM runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 대기 | context field·marker 제거와 dispatch test 통과 | Object Context와 별도 gate로 진행한다. |
| `V11-M6-MESSAGE-CONTEXT-NODE` | Node.js MessageContext 통일 | Node.js runtime lane, `P-DEEP` | `V11-CA-MESSAGE-CONTEXT-REVIEW` | 대기 | context field·marker 제거와 dispatch test 통과 | Object Context와 별도 gate로 진행한다. |
| `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE` | Actor Join 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-DEFERRED-JOIN-REVIEW`, `V11-CA-OBJECT-CONTEXT-REVIEW`, `V11-CA-MESSAGE-CONTEXT-REVIEW` | 대기 | 요청 문서와 repository link 0, 정식 spec·exact interface·E2E·ledger만으로 구현 가능 | 세 독립 review 전에는 요청 문서를 삭제하지 않는다. |
| `V11-CA-SESSION-BINDING-SPEC` | Session–Actor stored route·disconnect 정식 계약 | contract coordinator, `P-DEEP` | `V11-CA-ONE-WAY-SPEC` | 완료 | no-Store relay, physical automatic all-settled 통지, logical notification과 same-generation route update가 정식 spec에 일치 | `CA-D78`을 30·31·22·23·40·41·42·54장과 implementation gap에 반영하고 목표 계약과 runtime 차이를 분리했다. |
| `V11-CA-SESSION-BINDING-PROTOCOL` | Bound-session route·disconnect formal invariant | protocol lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | command 44·45 completed-only 유지, stored route·lease·generation·disconnect 의미와 schema drift 0 | Service wire internals에 의미를 보강하고 기존 schema의 completed-only command invariant를 대조했다. Wire field와 새 command는 추가하지 않았다. |
| `V11-CA-SESSION-BINDING-IFACE-DN` | .NET exact interface 의미 보강 | .NET contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, automatic·logical disconnect와 route update 의미 일치 | STREAM exact interface를 갱신하고 존재하지 않는 `ActorRef.MeshName` 설명도 교정했다. |
| `V11-CA-SESSION-BINDING-IFACE-JVM` | Java·Kotlin exact interface 의미 보강 | JVM contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | Java public API와 Kotlin projection의 automatic·logical disconnect 의미 일치 | Java·Kotlin STREAM exact interface에 같은 동작을 반영했다. |
| `V11-CA-SESSION-BINDING-IFACE-NODE` | Node.js exact interface 의미 보강 | Node.js contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, stored route와 disconnect 의미 일치 | Node.js channel messaging exact interface에 반영했다. |
| `V11-CA-SESSION-BINDING-IFACE-CPP` | C++ exact interface 의미 보강 | C++ contract lane, `P-DEEP` | `V11-CA-SESSION-BINDING-SPEC` | 완료 | public signature 변경 0, stored route와 disconnect 의미 일치 | C++ STREAM exact interface에 반영했다. |
| `V11-CA-SESSION-BINDING-E2E` | Session disconnect·relocation E2E 계약 | E2E coordinator, `P-DELIVERY` | `V11-CA-SESSION-BINDING-SPEC`, `V11-CA-SESSION-BINDING-PROTOCOL` | 진행 | all-settled failure·dedupe·no-Store, same-generation·Completed-only sequence scenario 누락 0 | Config 2 SM-D4A·D4B·D5·D5A와 Config 10 ST-E1A 계약을 갱신했다. 다섯 언어 feature map은 신규 runner가 없는 항목을 미구현으로 표시했고 C++·Node application disconnect loop를 제거했다. 최신 focused runner 증거가 모두 확보되기 전에는 완료로 전환하지 않는다. |
| `V11-CA-SESSION-BINDING-REVIEW` | 정식 흡수와 runtime gap 독립 review | Codex review lane, `P-HIGH` | `V11-CA-SESSION-BINDING-IFACE-DN`, `V11-CA-SESSION-BINDING-IFACE-JVM`, `V11-CA-SESSION-BINDING-IFACE-NODE`, `V11-CA-SESSION-BINDING-IFACE-CPP`, `V11-CA-SESSION-BINDING-E2E` | 진행 | spec·schema·interface·E2E·runtime owner 추적 100%, 상충·미소유 0 | Codex 5.6 sol high와 Claude Sonnet의 formal contract review는 모두 CLEAN이다. Relocation barrier의 timer replay와 네 exact Spot 상세 설명, C++ guide의 snapshot·all-settled 표현을 교정했고 DOC gate와 TRACE check가 통과했다. E2E runner 증거가 채워질 때 완료로 전환한다. |
| `V11-CA-SESSION-BINDING-DRAFT-RETIRE` | Session–Actor 변경 요청 문서 제거 | contract coordinator, `P-DELIVERY` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | 요청 문서와 inventory link 0, 정식 문서만으로 구현 가능 | Formal contract clean review 뒤 임시 요청 문서와 inventory link는 제거했다. 선행 E2E·review row가 완료되면 상태만 완료로 전환한다. |
| `V11-M6-SESSION-BINDING-CPP` | C++ stored route·disconnect runtime | C++ runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1~4 C++ 항목과 contract test gap 0 | Connection별 Actor map과 global Actor→single current binding index를 사용해 multi-Actor binding을 지원하고 inbound relay의 authority 재조회를 제거했다. Binding token, same-ObjectGeneration route update, stale token 차단과 physical disconnect all-settled cleanup을 구현했다. M6B runtime·Actor gateway focused target build와 실행이 통과했으며 resolver read count는 bind 2회 뒤 admit 2회에서도 2로 유지된다. Review clean 전에는 완료로 전환하지 않는다. |
| `V11-M6-SESSION-BINDING-DN` | .NET stored route·disconnect runtime | .NET runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1~4 .NET 항목과 contract test gap 0 | Relay의 `IZLinkActorResolver.FindWithPresenceAsync`와 hidden rebind를 제거하고 bind 때 고정한 route만 사용한다. Physical close는 exact binding snapshot 전체를 병렬 통지하고 같은 binding notification은 completion을 공유하며 callback timeout·failure 뒤에도 tombstone cleanup을 완료한다. 다른 ObjectGeneration은 explicit bind만 허용한다. 수동 E2E disconnect loop를 제거했고 focused 9/9가 통과했다. Review clean 전에는 완료로 전환하지 않는다. |
| `V11-M6-SESSION-BINDING-JVM` | JVM stored route·disconnect runtime | JVM runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1~4 Java·Kotlin 항목과 contract test gap 0 | Relay의 `ZLinkActorDirectory.find`와 hidden native unbind/rebind를 제거하고 stream에서 session actor runtime으로 전달하던 dead directory plumbing도 삭제했다. Exact binding snapshot all-settled·dedupe·deadline cleanup과 same-ObjectGeneration route fence를 구현했다. Focused route contract와 core compile이 통과했으며 전체 fake backend compile은 선행 SpotId·ClientServer signature drift 89건에서 차단된다. Review clean 전에는 완료로 전환하지 않는다. |
| `V11-M6-SESSION-BINDING-NODE` | Node.js stored route·disconnect runtime | Node.js runtime lane, `P-DEEP` | `V11-CA-SESSION-BINDING-REVIEW` | 진행 | IMP-SA-1·4 Node.js 항목과 contract test gap 0 | Internal route refresh·rebind는 같은 ObjectGeneration에서만 route를 갱신하고 새 incarnation의 explicit bind는 새 handle·token을 발급한다. Physical disconnect는 exact snapshot all-settled·deadline·cleanup을 수행하고 explicit notification과 token으로 중복을 막는다. TypeScript build와 CA-D78 focused 6/6이 통과했다. 전체 stream test 15건은 병렬 one-way·Deferred Join fixture drift로 별도 정리 중이며 review clean 전에는 완료로 전환하지 않는다. |

아래 sub-ID는 이 amendment가 추가한 runtime 구현 범위를 독립적으로 할당하고 완료 판정하기 위한
안정된 작업 단위다. 각 sub-ID가 완료되기 전에는 대응하는 기존 `V11-M6A-*`·`V11-M6B-*`·`V11-M6C-*` row를 완료로
전환하지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6B-EXEC-CPP` | C++ User Spot execution mode와 Yield scheduler | C++ runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | `test_cpp_framework_m6b_runtime`, `test_cpp_framework_execution`, `test_cpp_framework_contract_headers`를 root가 clean rebuild 뒤 재실행해 3/3 통과했다. SpotWide Actor FIFO double claim, PerActor Actor·Spot·timer lane, unsupported Yield와 self·same-gate request의 submit 전 거부를 검증했다. |
| `V11-M6B-EXEC-DN` | .NET User Spot execution mode와 Yield scheduler | .NET runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | Framework build error·warning 0. `UserSpotExecutionSchedulerTests` 5/5, Worker·serial focused 회귀를 포함해 37/37 통과했고 root 재검증은 execution·worker·serial 15/15를 통과했다. Config 8 E2E는 이 row의 gate가 아니며 `V11-R5D` 뒤 별도 활성화한다. |
| `V11-M6B-EXEC-JVM` | JVM User Spot execution mode와 Yield scheduler | JVM runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Java·Kotlin continuation이 같은 claim을 유지하며 실행 mode·Yield·same-gate contract test 통과 | Root가 Java core·Kotlin unit suite를 `--rerun-tasks`로 clean 재실행해 각각 494/494와 46/46을 통과했다. SpotWide double claim, PerActor Actor·Spot·timer lane, Java CompletionStage·Kotlin coroutine continuation claim, exact Yield allowlist와 same-gate 사전 거부를 검증했다. |
| `V11-M6B-EXEC-NODE` | Node.js User Spot execution mode와 Yield scheduler | Node.js runtime lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | SpotWide 이중 claim, PerActor Actor·Spot·timer lane, Yield allowlist와 same-gate 선거부 contract test 통과 | Root가 `npm run verify:m6b-runtime`을 재실행해 39/39를 통과했고 typecheck와 production·browser build도 통과했다. SpotWide shared gate+Actor claim, PerActor Actor·Spot·timer lane, Promise continuation 유지, unsupported Yield의 worker admission 전 거부와 Actor join Yield surface 제거를 검증했다. |
| `V11-M6C-BARRIER-CPP` | C++ all-lane lifecycle barrier | C++ maintenance lane, `P-DEEP` | `V11-M6B-EXEC-CPP` | 완료 | yielded continuation과 Actor·Spot·timer lane을 모두 quiesce한 뒤 close·snapshot·relocation을 시작하고 abort가 같은 generation seal만 복원 | Root 검토에서 lifecycle seal이 application·infrastructure active lane과 yielded continuation 종료를 기다린 뒤 queue·timer를 capture하는 것을 확인했다. Seal 동안 timer 변경과 ingress 실행을 차단하고 held ingress를 FIFO로 보존한다. Abort는 token·captured reference·barrier generation이 모두 같은 경우에만 같은 generation으로 복원하며 commit 뒤 이전 generation ingress는 stale로 거부한다. CMake build와 M6B·M6C runtime binary가 모두 exit 0이고 targeted `git diff --check`가 통과했다. |
| `V11-M6C-BARRIER-DN` | .NET all-lane lifecycle barrier | .NET maintenance lane, `P-DEEP` | `V11-M6B-EXEC-DN` | 완료 | yielded continuation과 Actor·Spot·timer lane을 모두 quiesce한 뒤 close·snapshot·relocation을 시작하고 abort가 같은 generation seal만 복원 | `ZLinkSpotSerialExecutor`의 generation-scoped barrier가 SpotWide·PerActor의 Spot·Actor·timer claim, caller cancellation 뒤 실제 callback과 yielded terminal continuation을 모두 기다린다. Relocation은 accepted journal과 seal 뒤 held ingress를 같은 barrier에 결합하고 abort는 exact generation만 reopen한다. Close도 all-lane barrier 뒤 callback을 실행하고 성공 시 admission seal을 유지한다. Focused scheduler 11/11, scheduler+serial 32/32와 Framework build warning·error 0이 통과했다. |
| `V11-M6C-BARRIER-JVM` | JVM all-lane lifecycle barrier | JVM maintenance lane, `P-DEEP` | `V11-M6B-EXEC-JVM` | 진행 | coroutine continuation을 포함한 모든 lane의 barrier와 generation-fenced abort contract test 통과 | Queue primitive는 yielded continuation을 active claim으로 추적하고 seal 뒤 Actor·Spot·timer·infrastructure 실행을 hold하며 exact seal object와 generation이 일치하는 abort만 captured→held FIFO를 복원한다. `ZLinkAsyncSerialQueueTest` 16/16과 Actor dispatch·Spot timer focused test가 통과했다. 다만 production maintenance coordinator가 User Spot participant queue 전체를 하나의 barrier generation으로 묶고 adapter capture를 호출하는 연결이 없어 aggregate all-lane gate는 아직 완료가 아니다. |
| `V11-M6C-BARRIER-NODE` | Node.js all-lane lifecycle barrier | Node.js maintenance lane, `P-DEEP` | `V11-M6B-EXEC-NODE` | 완료 | Promise continuation을 포함한 모든 lane의 barrier와 generation-fenced abort contract test 통과 | Root가 typecheck, production·browser build, M6C 40/40과 M6B 회귀 39/39를 재실행했다. SpotWide Yield continuation은 기존 claim으로 seal 안에서 재개되고 PerActor Actor·Spot·timer lane은 같은 generation barrier에서 quiesce한다. Close·relocation capture는 barrier 뒤에 시작하며 exact-generation abort만 seal과 timer state를 복원하고 stale abort와 commit 이후 turn은 상태를 바꾸지 못한다. |
| `V11-M6C-CAPACITY-CPP` | C++ runtime·Redis typed capacity | C++ Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 production target을 재빌드하고 실제 Redis 전체 27건을 실행해 25건 통과, 별도 cross-language fixture 환경이 필요한 2건만 skip임을 확인했다. Creation·terminal·standalone relocation·aggregate direct reserve/commit/abort·destroy가 Actor total·Spot total·Spot stable-type의 여섯 HASH를 단일 v3 bundle로 변경한다. Legacy scalar `capacityDelta`, participant relocation reservation과 비활성 Lua scaffold는 production source에서 0건이다. |
| `V11-M6C-CAPACITY-DN` | .NET runtime·Redis typed capacity | .NET Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | .NET public capacity vector는 Actor total, Spot total과 optional Spot stable-type delta를 함께 표현한다. InMemory·Redis creation, standalone relocation, aggregate prepare·commit·abort와 delete가 같은 typed bundle을 원자적으로 적용하고 Redis schema는 `zlink-capacity-bundle-v2`를 사용한다. Root 재검증에서 `RelocationRuntimeTests` 23/23과 실제 Redis `RedisAuthorityRelocationTests` 11/11이 통과했다. |
| `V11-M6C-CAPACITY-JVM` | JVM runtime·Redis typed capacity | JVM Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 Core와 Redis module 전체를 `--rerun-tasks`로 재실행해 build 성공을 확인했다. Core 494/494, Redis module 35건 실패 0이며 환경 비대상 2건만 skip했다. Descriptor v2 fixture와 creation·standalone relocation·aggregate·abort·destroy의 Actor total·Spot total·Spot stable-type bundle 원자성을 실제 Redis 7.0.15에서 검증했고 legacy scalar aggregate Lua와 relocation helper는 production source에서 제거했다. |
| `V11-M6C-CAPACITY-NODE` | Node.js runtime·Redis typed capacity | Node.js Location provider lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | creation·relocation·aggregate·abort·destroy가 단일 typed bundle을 원자적으로 처리하고 cross-language Redis fixture 통과 | Root가 production typecheck·Node/browser build와 실제 Redis filtered 3/3을 재실행했다. Descriptor v2·authority fixture, exact 14-field admission과 creation·standalone relocation·aggregate·abort·destroy의 Actor total·Spot total·Spot stable-type bundle 원자성을 검증했다. 전체 M6C 36건 중 capacity와 무관한 Entry/User Spot collision 정책 2건은 `V11-M6B-ENTRY-IDENTITY-NODE` gap으로 분리한다. |
| `V11-M6C-CAPACITY-MONITORING` | 다섯 언어 capacity 관측 구현 | monitoring lanes, `P-DELIVERY` | `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE` | 진행 | Actor total·Spot total·Spot stable type별 active·reserved·limit, activation active·limit과 unlimited 표현의 parity test 통과 | C++는 Location Store descriptor의 Actor·Spot·stable type별 active·reserved·limit과 activation active·limit을 snapshot에 투영하고 store 불가 시 registration limit으로 fallback한다. .NET은 descriptor cache를 snapshot에 투영하고 capacity 변경 시 placement event를 발행한다. .NET focused 2건과 public contract snapshot, C++ framework·contract header·runtime integration build/run이 통과했다. JVM·Node projection과 cross-language parity가 남아 있다. Java RuntimeMonitoring E2E는 public multicast snapshot delta를 검증하도록 준비됐지만 `ZLinkRouteMeshRuntimeService`가 remote·local 세부 counter 6개를 아직 0으로 투영하므로 live E2E 완료 증거로 사용하지 않는다. C++ vertical target은 기존 generated include와 stale service API 오류로 전체 compile evidence를 만들지 못했다. |
| `V11-M6B-ENTRY-IDENTITY-CPP` | C++ global SpotId와 Framework-issued lifecycle identity | C++ runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 대기 | 모든 Spot path가 UTF-8 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | `<ID>/result.json` |
| `V11-M6B-ENTRY-IDENTITY-DN` | .NET global SpotId와 Framework-issued lifecycle identity | .NET runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 대기 | 모든 Spot path가 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | `<ID>/result.json` |
| `V11-M6B-ENTRY-IDENTITY-JVM` | JVM global SpotId와 Framework-issued lifecycle identity | JVM runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 완료 | Java·Kotlin 모든 Spot path가 String SpotId를 사용하고 Entry·User UUID v4·v3 provider parity test 통과 | Root가 Core 497/497, Kotlin 46/46과 실제 Redis module 35건 실패 0을 `--rerun-tasks`로 재실행했다. Public `joinEntrySpot(Object)`는 target RID를 받지 않고 Actor stable type·capacity·weight로 eligible descriptor를 선택한 뒤 exact `entrySpotId`를 사용한다. Entry·User Spot UUID v4, 첫 충돌 즉시 실패, descriptor kind·Mesh·owner mapping과 v3 identity claim을 검증했다. 비활성 integration source의 이전 public 호출은 `V11-R5D` 뒤 E2E 활성화 단계에서 current contract로 함께 갱신하며 이 runtime sub-ID gate에는 포함하지 않는다. |
| `V11-M6B-ENTRY-IDENTITY-NODE` | Node.js global SpotId와 Framework-issued lifecycle identity | Node.js runtime·Location lane, `P-DEEP` | `V11-CA-SPOT-ID-REVIEW` | 완료 | 모든 Spot path가 string SpotId를 사용하고 Entry·User UUID v4, exact descriptor mapping·v3 provider·cleanup contract test 통과 | Root가 typecheck, production/browser build, M6B 39/39, M6C 36/36과 Unicode Redis SpotId·public contract focused 2/2를 재실행했다. Public/runtime/Location/wire의 Spot identity는 UTF-8 string이고 hex side field·NodeRid==SpotId fallback은 0이다. Entry descriptor exact match, UUID v4 첫 충돌의 즉시 SpotIdConflict와 추가 UUID·reservation 0을 검증했다. |
| `V11-M6A-CORE-RID-UUID` | Core raw automatic RID regression | Core regression lane, `P-DELIVERY` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | caller가 RID를 지정하지 않은 raw socket의 ID가 exact 16 bytes이고 UUID v4 version·variant bit를 만족하며 caller 지정 binary RID와 STREAM 4-byte RID 계약을 바꾸지 않음 | `.artifacts/v11/evidence/V11-M6A-CORE-RID-UUID/result.json`: targeted build, automatic UUID v4·caller binary contract와 STREAM 4-byte regression 2/2 통과 |
| `V11-M6A-WEIGHT-CPP` | C++ 공통 public weight 범위와 selection | C++ topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, 64-bit 합산과 multicast contract test 통과 | C++ public builder와 runtime option은 RouteMesh channel·ClientServer·placement weight를 signed `0..10000`으로 검증한다. Topology·ClientServer selection은 `uint64_t` 누적치를 사용하고 weight 0을 새 선택에서 제외하며 Logical Multicast는 positive remote node를 한 번씩 포함한다. Runtime 변경은 descriptor revision을 증가시켜 channel·placement weight를 다시 게시한다. Root가 `test_cpp_framework_m6a_runtime`과 `test_cpp_framework_contract_headers`를 rebuild·실행해 exit 0을 확인했으며 M6A test는 4,300,000,000 누적, 1:3 selection, zero exclusion, runtime revision과 multicast를 검증한다. |
| `V11-M6A-WEIGHT-DN` | .NET 공통 public weight 범위와 selection | .NET topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 대기 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, 64-bit 합산과 multicast contract test 통과 | `WeightContractTests`의 capacity-first·weight 0 Actor/User Spot eligibility 7/7, runtime placement/channel revision 1/1, ClientServer 100:300 1/1, same-process weight 0 1/1, Logical Multicast positive remote once 1/1이 통과했다. Evidence artifact와 `ROW-GATE` 전이므로 완료로 전환하지 않는다. |
| `V11-M6A-WEIGHT-JVM` | JVM 공통 public weight 범위와 selection | JVM topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | Java·Kotlin RouteMesh·ClientServer·placement weight parity와 overflow-safe selection test 통과 | Root가 누락된 `ZLinkRouteMeshRuntimeOptions` production 구현을 연결해 Mesh·Channel의 live weight 변경을 Framework-owned node와 Location descriptor 새 revision에 함께 반영했다. Weight는 mutation 전에 signed `0..10000`을 검증하고 0은 selection에서 제외하며 topology 합산은 `long`과 `Math.addExact`를 사용한다. RouteMesh M6A 5/5, ClientServer M6A 11/11, live option 3/3과 Core 497/497·Kotlin 46/46 전체 회귀가 통과했다. |
| `V11-M6A-WEIGHT-NODE` | Node.js 공통 public weight 범위와 selection | Node.js topology·placement lane, `P-DEEP` | `V11-CA-USER-SPOT-DRAFT-RETIRE` | 완료 | RouteMesh·ClientServer·placement signed 0..10000, runtime revision, safe-integer 합산과 multicast contract test 통과 | Root가 M6A 9/9, Logical Multicast focused 6/6과 typecheck를 재실행했다. Public runtime option의 Mesh placement·Channel weight mutation은 `0..10000`을 검증하고 raw descriptor revision과 peer announcement를 동기 갱신한 뒤 Location descriptor republish를 직렬화한다. Weight 0 제외, capacity-first placement, safe-integer 합산, ClientServer selection과 Logical Multicast admission·backpressure 경계를 검증했다. |
| `V11-CA-USER-SPOT-RUNTIME-JOIN` | execution·capacity·Entry identity·weight runtime amendment 합류 | amendment coordinator, `P-DELIVERY` | `V11-M6B-EXEC-CPP`, `V11-M6B-EXEC-DN`, `V11-M6B-EXEC-JVM`, `V11-M6B-EXEC-NODE`, `V11-M6C-BARRIER-CPP`, `V11-M6C-BARRIER-DN`, `V11-M6C-BARRIER-JVM`, `V11-M6C-BARRIER-NODE`, `V11-M6C-CAPACITY-CPP`, `V11-M6C-CAPACITY-DN`, `V11-M6C-CAPACITY-JVM`, `V11-M6C-CAPACITY-NODE`, `V11-M6C-CAPACITY-MONITORING`, `V11-M6B-ENTRY-IDENTITY-CPP`, `V11-M6B-ENTRY-IDENTITY-DN`, `V11-M6B-ENTRY-IDENTITY-JVM`, `V11-M6B-ENTRY-IDENTITY-NODE`, `V11-M6A-CORE-RID-UUID`, `V11-M6A-WEIGHT-CPP`, `V11-M6A-WEIGHT-DN`, `V11-M6A-WEIGHT-JVM`, `V11-M6A-WEIGHT-NODE` | 대기 | 모든 sub-ID evidence와 대응 `V11-M6A-*`·`V11-M6B-*`·`V11-M6C-*` 상태를 대조해 runtime·provider·monitoring·identity·weight gap 0 | `<ID>/result.json` |

- 이 변경의 runtime contract test가 모두 통과하기 전에는 해당 언어의 M6A·M6B·M6C row와 `V11-R5A`·
  `V11-R5B`·`V11-R5C`를 완료로 전환하지 않는다.

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

#### 9.1.2 Contract amendment decision register

아래 결정은 기존 proposal, Store amendment와 후속 execution·capacity·identity 교정의 open item을 합친 정식 spec 작성
입력이다. `CA-D01~D63`에 미결정 상태를
허용하지 않는다. 각 결정은 caller가 target node, owner fence, Store transaction과 retry state machine을
조합하지 않게 하는 방향을 선택했다. 언어별 표현은 달라도 identity, option, deadline, closed result와
failure 의미는 같아야 한다.

| ID | 비교한 대안 | 선택한 계약 | 정식 owner |
|---|---|---|---|
| `CA-D01` | `(MeshName, ActorId)` key / Store namespace global `ActorId` | `ActorId` 하나를 global key로 사용한다. UTF-8 1..255 bytes, case-sensitive exact equality이며 normalization과 case folding을 하지 않는다. MeshName은 initial placement attribute다. `ActorRef`는 `{ActorId, ObjectGeneration, MeshName, NodeRid}` location snapshot이고 message target이 아니다. JSON generation은 decimal string이다. | Framework API, Actor model, Location runtime, 다섯 Actor·serialization interface |
| `CA-D02` | Mesh별 Spot ID / global Spot ID | Global namespace와 `SpotRef` 결정은 유지한다. Spot의 Core RoutingId 재사용과 RID 명칭은 후속 `CA-D65`가 string `SpotId`로 대체했다. | Spot messaging, Spot Actor, Location runtime, 다섯 Spot·serialization interface |
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
| `CA-D14` | 자유 형식 selector / 작은 typed placement surface | `CA-D70`이 대체한다. 최초 배치는 stable type·Serving·capacity와 node-wide weight만 사용하며 caller-defined selector를 받지 않는다. | Framework API, MeshNode, Location runtime, manager·configuration interface |
| `CA-D15` | participant별 visible CAS / bounded aggregate commit | User Spot과 member Actor는 non-zero 128-bit aggregate ID, 최대 1024 participant와 encoded 최대 1 MiB의 aggregate record를 사용한다. Generic Store transaction이 owner·membership visibility를 한 commit generation으로 전환한다. Commit 전 partial owner를 resolve하지 않으며 commit 뒤에는 전체 target recovery만 허용한다. | Spot Actor, Location runtime·Store, maintenance, protocol |
| `CA-D16` | cache를 hidden fixed profile로 고정 / 운영 가능한 두 public duration | `RouteCacheMaxAge` 기본 15초와 `RelocationForwardingWindow` 기본 30초를 공통 Location option으로 공개한다. 둘 다 0이면 cache·forwarding을 끈다. 양수이면 cache age가 forwarding window보다 최소 5초 작아야 한다. Runtime 변경은 새 entry와 새 relocation에만 적용한다. | Framework API, Location runtime, configuration interface |
| `CA-D17` | stale hop마다 Store 조회 / committed mapping chain | Relay는 committed source→target mapping만 사용하고 Store를 읽지 않는다. AuthorityOwnerGeneration은 hop마다 증가해야 하며 최대 8 hops다. Mapping 하나의 대기열은 1024 message·16 MiB 이하이고 negotiated message bound도 함께 지킨다. Original operation ID, generation, payload와 reply route를 보존하고 loop·bound 초과는 stale-route error다. | Actor·Spot messaging, Location runtime, protocol·monitoring |
| `CA-D18` | relocation policy 생략 overload / explicit policy | 모든 object Server factory는 policy를 명시한다. 생략 overload와 compatibility default를 두지 않는다. 이동을 지원하지 않아도 `Disabled`를 등록한다. | Framework API, 다섯 configuration interface |
| `CA-D19` | fixed RID 전면 제거 / manual topology에 한정 | Fixed Routing ID는 Location Store descriptor와 automatic discovery를 사용하지 않는 explicit manual topology에서만 허용한다. Object Client·Server 또는 automatic mode와 함께 설정하면 startup 오류다. | MeshNode, network identity, 다섯 routing configuration interface |
| `CA-D20` | slot allocation 유지 / prefix+UUID v4 lifecycle RID | Public slot count·group, allocation Store·provider와 result type을 제거한다. Core raw socket의 미지정 RID는 RFC 4122 UUID v4 bit layout의 16-byte binary다. Framework automatic RID의 random identity도 UUID v4다. Diagnostic prefix를 제공하는 RID는 lowercase canonical 문자열을 prefix 뒤에 붙인 UTF-8 값을 Core socket에도 명시하고, User Spot `Create`의 logical RID도 active authority 충돌에서 새 UUID로 재시도하지 않는다. | Core socket, MeshNode, Spot messaging, Location Store, 다섯 routing interface |
| `CA-D21` | 별도 RID provider / descriptor owner CAS 재사용 | Prefix는 ASCII `[A-Za-z0-9._-]` 1..64자이고 full RID는 `prefix-<lowercase-canonical-uuid-v4>`이며 255 bytes 이하이다. Descriptor owner CAS가 `(MeshName, RID)` active conflict를 확인한다. 충돌은 기존 record를 바꾸거나 새 UUID로 재시도하지 않고 즉시 `RoutingIdConflict`로 startup을 실패한다. Replacement lifecycle은 새 RID를 사용한다. | MeshNode, Location Store, protocol·monitoring |
| `CA-D22` | Channel weight 합성 / node-wide Router weight | Placement weight는 Channel weight와 분리한다. 범위는 후속 `CA-D64`가 공통 signed `0..10000`, 기본 `100`으로 확대했다. `0`은 신규 placement·relocation target에서만 제외하고 reservation을 얻은 attempt와 existing traffic은 이후 변경으로 취소하지 않는다. Startup builder, runtime option, descriptor와 snapshot이 같은 값을 사용한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D23` | capacity 미설정 / finite node·type capacity | `CA-D61`로 대체했다. Population 기본값은 Actor total·Spot total·Spot stable type 모두 `0`(unlimited)이고 activation concurrency만 별도 기본값 128을 사용한다. | MeshNode, Location runtime·Store, configuration·monitoring interface |
| `CA-D24` | 두 CAS와 compensation / generic atomic reservation | Generic `Reserve`, `Commit`, `Abort` operation은 유지하되 `CA-D62`가 scalar 값을 typed capacity bundle로 대체했다. TTL 없이 authority와 exact reservation fence로 recovery·takeover·abort한다. | Location runtime·Store, Redis provider, 다섯 provider interface |
| `CA-D25` | `InstanceSpotAddress` overload / Manager explicit create / Spot direct fluent activation | `InstanceSpotAddress`와 Instance manager create를 모두 제공하지 않는다. `SendToSpot`·`RequestToSpot`은 global SpotId를 받고 Spot 전용 fluent call을 반환한다. Marker 없는 call은 existing-only다. Instance marker가 있는 Missing call만 optional stable type과 initial Mesh로 cold activation한다. Selected Mesh의 distinct type이 하나면 생략한 type을 자동 선택하고 여러 type이면 명시를 요구한다. Existing authority는 저장된 kind·type과 current Mesh를 사용한다. | Spot address messaging, Location runtime, 다섯 Spot·messaging interface |
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
| `CA-D45` | Reservation provider가 Creating·Ready authority payload를 합성 / opaque payload와 provider allocation metadata 분리 | `Reserve` request는 Framework가 encode한 Creating authority payload를 받고, `Commit`은 Ready authority payload를 받는다. Provider는 payload body를 해석하거나 합성하지 않고 exact bytes를 저장한다. 별도 current placement allocation은 `Reserved`·`Active` state, kind, stable type, descriptor key·lifecycle generation과 typed capacity bundle을 저장한다. Reserve는 Missing→Reserved, exact Commit은 Reserved→Active, exact Abort는 Reserved→Missing만 수행하며 snapshot·stored·scan result가 allocation을 반환한다. Owner metadata는 allocation과 분리한다. | Location runtime·Store, Redis provider, 다섯 provider exact interface |
| `CA-D46` | Missing create reservation을 relocation에도 재사용 / existing object용 relocation capacity fence 분리 | Existing Actor·Spot relocation은 create reservation을 재사용하지 않는다. Standalone relocation fence는 current authority·source Active allocation과 typed bundle을 exact-match하고 target의 bundle 전체를 Reserved로 만든다. Standalone `NewOwner`만 이 fence를 소비한다. User Spot aggregate는 participant별 fence 없이 Spot total 1, Spot stable type 1과 Actor total N의 단일 typed bundle을 prepare하고 aggregate commit·abort가 all-or-none으로 finalize한다. Delete와 recovery도 exact bundle·authority·allocation fence를 사용하며 TTL에 의존하지 않는다. | Location runtime·Store, maintenance, Redis provider, 다섯 provider exact interface |
| `CA-D47` | Source가 Instance owner claim 뒤 target에 first message 전송 / target-owned first-message activation envelope | Ready authority는 source가 current owner로 일반 direct call을 보낸다. Missing+Instance intent에서는 source가 target만 선택하고 global Spot ID·stable type·descriptor fence·operation identity·reply correlation·deadline과 first message를 activation envelope로 target transport에 제출하며 owner claim과 reservation을 만들지 않는다. Target은 current authority와 local exact instance를 확인하고 Missing이면 자신을 owner로 generic Reserve를 수행한다. CAS winner만 factory·initialize·Commit을 실행하고 Ready barrier 뒤 envelope message를 local queue에 exactly once 제출한다. CAS loser는 local instance를 만들지 않고 Ready winner로 original operation을 한 번 redirect하거나 Creating completion에 합류한다. Authority와 일치하지 않는 local instance는 fence한다. | Framework API, Spot messaging, Location runtime, 다섯 Spot exact interface, Instance E2E |
| `CA-D48` | 언어별 Redis authority layout 중 하나를 복사 / 장점을 결합한 공통 hybrid schema | Authority current state와 active-scan history는 authority별 HASH에 두고 global counter·capacity·membership·versioned index만 shared HASH/ZSET에 둔다. Creation reservation·relocation fence·aggregate는 operation별 HASH다. `CA-D62`의 typed capacity schema부터 provider transaction domain 전체가 literal `{zlink-location-v2}` hash tag를 공유하고 모든 Lua key를 `KEYS`로 전달한다. v1과 v2 key를 한 transaction에서 혼합하거나 in-place 해석하지 않는다. Public descriptor HASH와 admission metadata HASH를 분리한다. Watermark·immutable history·tombstone·durable cursor로 1000 item·4 MiB snapshot page를 만들며 전체 materialization과 numeric revision score를 금지한다. | Redis Location Store 공통 spec·fixture, C++·.NET·JVM·Node provider와 cross-language Redis test |
| `CA-D49` | Capacity bucket과 `objectKind`를 언어별 enum 표현에 맡김 / Redis physical encoding을 고정 | Current authority의 `objectKind`는 `actor`, `user_spot`, `instance_spot` token만 사용한다. Capacity node bucket은 canonical descriptor key와 lifecycle generation decimal을 UTF-8 byte length-prefix로 encode하고, type bucket은 같은 값 뒤에 canonical `objectKind` token과 stable type을 같은 방식으로 붙인다. 이 규칙은 Unicode, enum 이름과 숫자값 차이에도 네 provider가 같은 field를 갱신하게 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider physical schema test |
| `CA-D50` | Owner lease를 언어별 string·HASH·dual-write로 유지 / 하나의 HASH 계약으로 고정 | `owner-lease:D`는 `ownerId`, `generation`, `expiresAt` 세 field와 key TTL만 사용한다. Descriptor·authority·RoutingId allocation은 이 HASH를 직접 검증·갱신하며 별도 legacy lease value나 owner lease index를 쓰지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider owner lease test |
| `CA-D51` | Descriptor owner index를 owner ID raw suffix로 구성 / exact owner token digest로 구성 | Descriptor index는 canonical descriptor key member를 저장하는 `descriptor:mesh:index` SET 하나를 사용한다. Cleanup index는 `ownerId + NUL + LeaseGeneration decimal`의 SHA-256 lower-hex suffix를 사용하고 같은 canonical key를 member로 저장한다. Owner ID만 일치하는 다른 host lifecycle descriptor는 제거하지 않는다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider descriptor cleanup test |
| `CA-D52` | Authority history를 language별 JSON·field grouping으로 저장 / revision-prefixed field encoding으로 고정 | Revision hex `R`마다 Active full snapshot은 `R:deleted=0`과 exact current 13개 `R:<field>`를 저장한다. Pending full snapshot은 provider-issued creation reservation ID, reference, SHA-256과 encoded size 네 field까지 current와 history에 모두 저장해 17개 field를 복원한다. Pending에는 네 field가 필수이고 Active에는 금지한다. Tombstone은 `R:deleted=1`, `R:authorityKey`만 저장한다. Membership history는 `R` field에 immutable bytes를 저장한다. 어느 언어가 만든 watermark snapshot도 다른 언어가 복원할 수 있어야 한다. | Redis Location Store 공통 spec·authority fixture, 네 provider concurrent scan test |
| `CA-D53` | Automatic RouteMesh initiator와 duplicate-pipe admission을 한 문장으로 설명 / 시작 규칙과 안전장치를 분리 | Automatic RouteMesh는 canonical RID가 더 작은 MeshNode만 pairwise connect를 시작한다. Manual topology의 양방향 connect와 automatic의 경합·stale discovery 후보만 공통 duplicate-pipe admission에서 RID·lifecycle generation을 확인해 하나의 ready connection으로 수렴한다. ClientServer는 Client가 server별 intent를 만들고 classic fanout은 Subscriber가 publisher별 intent를 만드는 비대칭 topology다. | `10-channel-topology.ko.md`, `12-client-server-channel.ko.md`, `21-mesh-node.ko.md`, topology regression |
| `CA-D54` | Immutable digest를 언어별 descriptor serialization hash로 계산 / 공통 canonical preimage hash | Admission HASH의 `immutableDigest`는 `zlink-mesh-node-immutable-v2` domain부터 Entry Spot ID를 포함한 immutable descriptor·capability field를 UTF-8 byte length-prefix segment로 연결한 preimage의 SHA-256 lower-hex다. Channel name과 capability는 unsigned UTF-8 byte lexical order로 정렬한다. Descriptor revision, weight 값, maintenance wave, runtime state, owner token, timestamp와 usage count는 제외한다. | Redis Location Store 공통 spec·MeshNode fixture, 네 provider byte-level contract test |
| `CA-D55` | Instance first message를 process-local reservation·queue로만 유지 / complete activation recovery envelope와 provider-issued Pending creation projection | Target-owned Instance cold activation은 command 39의 optional metadata presence·frame까지 포함한 complete first-message envelope를 Relocation Store에 먼저 저장한다. Location Reserve는 provider-issued reservation ID, reference, SHA-256과 encoded size를 Pending authority current·history row에 함께 기록한다. Pending snapshot은 이 projection을 exact read로 반환하고 Active에서는 제거한다. Factory·initialize 뒤 durable activation inbox first record를 Ready 전에 확정하되 handler는 barrier로 막는다. Ready Instance authority만 recovery root·inbox sequence·replay cursor를 유지하며 queue head restore 뒤 barrier를 연다. 첫 handler terminal completion을 durable하게 기록하고 cursor를 sequence까지 갱신한 뒤에만 Preserve CAS로 pointer를 release하고 root를 삭제한다. Actor·User Spot generic create에는 ZLIA와 durable inbox를 사용하지 않는다. Instance Spot factory가 하나라도 있는 Object Server는 relocation policy와 관계없이 Relocation Store를 정확히 하나 등록한다. | `20`·`24`·`40`·`41`·`42`, protocol schema·golden·validator, 다섯 authority exact interface·registration, Config 14 `IS-F08`·`IS-P07`·`IS-E2E-32/34/35/36`, `V11-M6B-*` |
| `CA-D56` | User Spot remote create·close를 Location polling이나 local manager로 대체 / exact terminal service operation | User Spot create는 Location Store의 generic Pending creation content와 provider-issued reservation을 사용하되 source와 target 사이에 별도 generation-fenced service operation을 둔다. Create request는 operation correlation, source·target node lifecycle, authority key, stable type, exact reservation·StoreVersion과 deadline을 전달한다. Target은 Pending row의 immutable creation content를 exact read한 뒤 factory·initialize·Commit을 실행하고 `Existing`·`Created`·`Rejected`, exact `SpotRef`와 optional application reply를 terminal-once로 반환한다. User Spot close도 current owner로 exact `SpotRef`, authority owner generation과 target lifecycle을 전달하는 별도 operation을 사용하며 active Actor membership, moving state와 generation을 target admission 전에 검증한다. Location row polling은 terminal reply·rejection을 보존하지 못하고 application packet으로 reserved control을 흉내 내면 handler 경계가 누출되므로 사용하지 않는다. Protocol은 command 47 `userSpotCreate`, command 48 `userSpotClose`, reply operation discriminator 13·14로 고정했다. 기존 command 20 reply prefix와 field 순서를 유지하며 create·close 성공 tail cardinality와 source·target lifecycle, reservation·StoreVersion, exact SpotRef·authority generation·deadline 누락을 validator negative mutation으로 거부한다. Schema 39 commands·163 types, negative self-test 215개, generated 4-language constants drift check와 decoder fixture가 통과했다. 네 runtime codec·dispatch·source operation table과 cross-node contract를 같은 command identity로 갱신하기 전에는 remote create·close를 완료로 판정하지 않는다. | Spot manager exact interface, `20`·`23`·`24`·`40`, protocol schema·golden·validator, 네 runtime·M6B contract |
| `CA-D57` | User Spot Actor 독립 실행 / factory별 실행 mode | User Spot factory의 기본 mode는 `SpotWide`다. Actor job은 Actor별 FIFO claim을 먼저 유지한 뒤 User Spot gate를 얻으며 Spot direct handler, member Actor handler, timer와 lifecycle callback을 전체 직렬화한다. Optional `PerActor`는 factory 등록 때만 고정하고 Actor별 FIFO lane, Spot direct·lifecycle lane과 timer별 FIFO lane을 사용한다. 서로 다른 Actor와 서로 다른 timer는 동시에 실행할 수 있다. | Async policy, Actor model, Spot Actor, 다섯 configuration·Spot interface |
| `CA-D58` | 모든 owner turn의 generic Yield / shared Spot gate 전용 Yield | `Yield`는 `SpotWide` User Spot과 Instance Spot에서만 허용한다. Member Actor는 Actor claim을 유지하고 User Spot gate만 반납하며 terminal result 뒤 같은 gate를 다시 얻는다. Entry Spot·Entry Actor·`PerActor`·Node·Channel·owner 밖에서는 operation submission, queue mutation과 gate release 전에 `InvalidConfiguration`으로 끝낸다. RequestToChannel·RequestToSpot·RequestToActor와 RunIoWorker·RunCpuWorker의 제한은 유지한다. Create·get-or-create 제공 여부는 후속 `CA-D73`이 추가하며 join·send·publish·timer·close·destroy에는 제공하지 않는다. | Async policy, messaging, Actor·Spot exact interface |
| `CA-D59` | self request inline·같은 gate 대기 / no-inline과 deadlock 선검증 | Application handler를 inline 또는 reentrant dispatch하지 않는다. 같은 Actor의 awaited request는 Actor claim을 유지한 채 완료될 수 없으므로 `Async`와 `Yield` 모두 submit 전에 거부한다. `SpotWide`에서 target 처리가 현재 User Spot gate를 필요로 하는 `Async`도 submit 전에 거부한다. One-way submit은 queue 순서를 보존할 수 있으므로 별도 완료 대기를 만들지 않는다. | Async policy, Actor·Spot messaging, 다섯 runtime contract test |
| `CA-D60` | 단일 Spot queue seal / execution mode별 all-lane barrier | Close·relocation·snapshot은 새 application admission과 participant 변경을 먼저 seal하고, yielded continuation을 포함한 active Actor·Spot·timer lane이 안전한 turn 경계에 도달한 뒤 진행한다. 실패하면 같은 generation의 seal을 모두 abort한다. Snapshot은 일부 lane만 멈춘 상태를 capture하지 않는다. | Async policy, Spot Actor, maintenance, 네 runtime |
| `CA-D61` | generic object active 10,000·pending 128 / typed population limit과 activation concurrency 분리 | Node Actor total, Node Spot total과 Spot stable type limit만 제공하며 기본값 `0`은 unlimited, 양수는 최대값, 음수는 startup 오류다. Entry Spot은 Spot count에서 제외하고 그 Actor는 Actor count에 포함한다. Actor type별 limit은 두지 않는다. Activation concurrency는 population capacity와 다른 typed option이며 기존 pending 128의 보호 목적을 유지한다. | Framework API, MeshNode, configuration·monitoring exact interface |
| `CA-D62` | scalar capacity delta / typed capacity vector의 atomic reservation | Location Store는 Actor total, Spot total과 optional Spot stable type bucket의 active·reserved·limit을 lifecycle fence 아래 관리한다. Creation·commit·abort·destroy·relocation과 aggregate는 필요한 vector 전체와 authority를 한 transaction에서 바꾼다. User Spot aggregate target은 Spot total 1개, 해당 Spot stable type 1개와 member Actor total N개를 단일 bundle로 all-or-none 예약한다. Descriptor는 stale projection일 뿐 최종 admission이 아니다. | Location runtime·Redis Store, 다섯 provider·monitoring interface |
| `CA-D63` | opaque Framework-issued Entry identity / 진단 prefix와 독립 UUID v4를 가진 lifecycle identity | UUID 발급·lifecycle·collision 규칙은 유지한다. Entry identity의 타입·명칭과 문서 소유권은 후속 `CA-D65`가 string `SpotId`와 Spot identity spec으로 대체했다. | MeshNode, Spot messaging, Location Store, 다섯 Spot interface |
| `CA-D64` | topology별 0..100 weight / 공통 signed 0..10000 weight | RouteMesh Channel Server, ClientServer Server와 node-wide object placement의 public weight는 signed integer `0..10000`, 기본값 `100`이다. `0`은 새 target에서 제외하고 positive 값은 상대 비중이며 capacity·eligibility filter 뒤에 적용한다. Startup과 runtime update의 범위 밖 값은 mutation 전 configuration error다. Runtime update는 descriptor revision으로 순서화하고 이미 제출했거나 reservation을 얻은 operation에는 적용하지 않는다. Logical Multicast는 positive member를 한 번 포함하고 `0`을 제외하며 selection 합계는 최소 64-bit 정수로 계산한다. | Channel topology·ClientServer·MeshNode·monitoring, 다섯 configuration·runtime interface |
| `CA-D65` | Core RoutingId를 Spot에 재사용 / global logical string SpotId | Entry·User·Instance Spot은 Location Store transaction domain 전체에서 global인 UTF-8 1..255-byte, case-sensitive exact string `SpotId`를 사용한다. Unicode normalization과 case folding은 적용하지 않는다. User Spot `Create`는 lowercase canonical UUID v4, Entry Spot은 `<prefix>-entry-<lowercase-canonical-uuid-v4>`를 발급한다. NodeRid만 Core RoutingId를 사용한다. Wire는 Spot field를 text8로 encode하고 arbitrary binary legacy Spot RID를 거부하며 Redis는 `location-authority-hybrid-v3`을 사용한다. | Spot messaging·Actor·Location·Redis·relocation·monitoring, protocol, 다섯 exact interface와 E2E |
| `CA-D66` | Actor creation callback void / 승인·거절과 optional reply | Entry Spot의 Actor creation callback은 `Accepted`와 optional reply를 반환한다. Manager terminal result는 `Existing`, `Created`, `Rejected`의 닫힌 union이며 `Create`는 `Created`·`Rejected`만, `GetOrCreate`는 세 상태를 모두 반환할 수 있다. Callback exception은 application rejection이 아니라 기존 typed creation failure다. | Actor model, Entry Spot interface, 다섯 Actor manager interface |
| `CA-D67` | Entry·User Spot 공통 admission lifecycle / membership과 admission lifecycle 분리 | `OnActorJoin`은 User Spot admission에만 존재한다. Entry·User Spot은 commit 뒤 알림과 source membership 종료·disconnect callback만 공유한다. Actor 최초 생성은 Entry의 creation callback만 사용하고 joined callback을 호출하지 않는다. User→Entry 일반 복귀는 target joined와 source leave를, maintenance 복원은 target relocated와 source leave만 호출한다. | Spot model·Spot Actor, 다섯 Entry·User Spot interface와 E2E |
| `CA-D68` | 서로 다른 concurrent request가 첫 rejection을 공유 / operation-scoped terminal replay | 같은 Actor의 creation callback은 reservation으로 직렬화한다. Creating을 본 다른 operation은 authority 변경 뒤 Ready면 `Existing`, rejection·failure cleanup이면 새 reservation으로 자신의 request를 실행한다. 서로 다른 operation은 `Rejected` reply를 공유하지 않는다. 동일한 source lifecycle과 `OperationId`의 중복 전달만 Location Store의 correlation-free semantic terminal을 재사용하며 command reply는 현재 correlation과 reply route로 다시 encode한다. | Location runtime·Store·Redis v3 fixture, service wire와 다섯 provider interface |
| `CA-D69` | rejection을 Ready 뒤 destroy로 처리 / staging 단계에서 atomic reject | Rejected Actor는 Ready authority와 message admission을 열지 않고 active capacity·destroy lifecycle에 포함하지 않는다. Staging instance를 폐기한 뒤 exact Creating authority와 pending capacity를 정리한다. 다른 operation은 새 reservation으로 진행할 수 있고, 거절한 operation의 terminal record만 retry retention 동안 유지한다. | Actor runtime, Location Store, E2E·failure recovery |
| `CA-D70` | stable type 후보를 placement profile로 다시 분할하고 정의되지 않은 affinity를 전달 / stable type 기반 단일 배치 pool | Actor·User Spot·Instance Spot 최초 배치의 public API, factory option, descriptor capability, immutable digest, creation intent, reservation과 recovery에서 `PlacementProfile`·`AffinityKey`를 제거한다. 선택한 Mesh의 `Serving` Object Server 중 stable type을 등록했고 population capacity가 남은 node만 후보로 만든 뒤 node-wide placement weight를 적용한다. | Framework API·Actor·Spot·MeshNode·Location·Redis, service wire, 다섯 exact interface·runtime·E2E |
| `CA-D71` | publish 결과가 remote subscriber queue 또는 handler 완료까지 확인 / source-local submission 경계 | Logical Multicast의 remote 경계는 고정한 MeshNode route의 local transport queue 제출, local 경계는 일치하는 local Spot queue 제출이다. Remote Spot queue 수락, subscriber handler 시작·완료와 ACK는 기다리지 않는다. 반환 type과 terminator 이름은 후속 `CA-D72`·`CA-D73`이 대체한다. | Spot messaging, 다섯 messaging exact interface·runtime·contract test |
| `CA-D72` | one-way queue admission status·detail 반환 / 반환 데이터 없는 비동기 admission | `CA-D71`의 source-local admission 경계는 유지하되 Send·Publish·STREAM send·reply·bound session send·session Actor relay는 정상 완료 값을 반환하지 않는다. Queue가 가득 차면 operation family의 send timeout까지 기다리고, timeout·cancellation·route 단절·runtime 종료는 exceptional completion으로 전달한다. `Backpressured`는 중간 상태이며 public terminal 결과가 아니다. Logical Multicast의 target별 성공·drop·unreachable count는 public 결과에서 제거하며 monitoring 소유권은 후속 `CA-D77`이 제거한다. Partial submission을 rollback하거나 자동 재시도하지 않는다. Not-found는 Actor·Spot·Mesh·session별 기존 kind를 사용하며 runtime 종료는 공통 `RuntimeShutdown=36`으로 고정한다. | Async policy, messaging·Spot·Actor·STREAM, monitoring·error, 다섯 runtime·E2E |
| `CA-D73` | package별 비동기 terminator 이름과 Java call을 직접 노출하는 Kotlin surface / repository-wide fluent terminator 규칙 | Messaging·Worker call builder의 비동기 종결자는 .NET `Async`, Kotlin 전용 wrapper `await`, Java·Node·C++ `submit`을 사용한다. 즉시 제출은 `Submit`·`submit`, 실제 Spot gate를 반납하는 종결자만 `Yield`·`yield`다. .NET one-way `SubmitAsync`는 `Async`로 바꾸고 Java·Node·C++ one-way는 `Void`·`void`를 반환한다. Request·worker·Actor·Spot create·get-or-create의 application result는 유지하며 create 계열에도 허용된 `SpotWide`·Instance 문맥의 `Yield`를 제공한다. 같은 naming을 Server Framework·Stream Connector·HTTP Client의 exact interface와 package consumer에 한 snapshot으로 적용한다. 단, Node HTTP Client의 no-argument typed response와 Server builder one-way는 TypeScript generic erasure 뒤 같은 상속 signature가 되므로 one-way `submit()`을 우선하고 typed response에 `async<T>()`를 유지한다. | Framework·Stream Connector·HTTP Client 공통 async policy, 다섯 exact interface·runtime·sample |
| `CA-D74` | Actor Join 결과를 handler에서 await / handler terminal 뒤 Deferred Join 실행 | Actor membership Join call은 결과를 기다리는 `Async`·`submit`·`Yield` terminal을 제공하지 않고, 열린 Framework handler scope에 immutable intent와 Actor barrier를 동기 등록하는 `Defer`만 제공한다. Handler가 정상적으로 끝나면 등록 순서대로 활성화하고 exception·cancellation이면 모두 폐기한다. Join completion은 Spot의 `OnJoinCompleted`로 전달하며 same-node는 process lifetime, verified Relocation manifest가 Location authority에 publish된 cross-node `Accepted`만 recovery 뒤 at-least-once completion을 보장한다. Relocation은 `ObjectGeneration`을 유지하고 owner가 바뀔 때만 `AuthorityOwnerGeneration`을 증가시킨다. | Async policy, Actor·Spot lifecycle·Relocation, 다섯 exact interface·runtime·E2E |
| `CA-D75` | lifecycle Context를 callback 인자로 임시 전달 / application object가 exact Context를 composition | Actor·User Spot·Entry Spot·Instance Spot application instance는 Framework가 생성 전에 제공한 exact Object Context를 읽기 전용 member로 보유하고 lifecycle 동안 같은 객체를 반환한다. Actor factory는 ActorId를 별도 인자로 받지 않고 Actor Context 하나를 받으며 Spot `Configure`는 Context 인자를 받지 않는다. Handler는 application object를 받고 object operation은 그 객체의 Context를 통해 호출한다. Source Context는 relocation commit 뒤 fence되어 새 current-owner operation을 시작하지 못한다. | Actor·Spot object model과 factory·handler contract, 다섯 exact interface·runtime·E2E |
| `CA-D76` | send·request·Spot Actor별 중복 context와 handler invocation context 혼용 / 공통 MessageContext와 명시적 specialization | 모든 inbound message의 공통 정보는 `MessageContext`로 통일하고 nullable MeshName·ChannelName·ContentType·CorrelationId, packet name과 immutable metadata를 제공한다. Route는 source node, Publish는 topic·source, STREAM Session은 reply 가능 여부가 필요하므로 각각 `RouteMessageContext`, `PublishMessageContext`, `SessionMessageContext`를 사용한다. Universal context에는 reply operation이나 connection cancellation을 넣지 않는다. Filter chain의 descriptor·payload·next는 message context가 아닌 `HandlerInvocation`으로 분리한다. | Message model, Channel·Actor·Spot·STREAM handler와 filter, 다섯 exact interface·runtime·E2E |
| `CA-D77` | publish target별 monitoring과 publish 전용 metric·event / source-local 시작 뒤 관측 집계 없는 best-effort publish | Publish는 설정한 timeout 안에서 worker와 source-local capacity를 확보해 작업을 시작하면 결과값 없이 정상 완료한다. 시작 전 timeout·cancellation·shutdown만 typed exceptional completion이며, 시작 뒤 target별 수락·실패를 기다리거나 전체 실패·rollback·자동 retry로 바꾸지 않는다. Logical Multicast snapshot type, MeshNode multicast field, target count field, `zlink.mesh_node.multicast.*` metric과 multicast runtime event를 제거한다. Logical Multicast와 classic fanout publish는 message-flow event를 만들지 않으며 peer·transport·mailbox·shutdown과 classic fanout 연결 상태 같은 공통 monitoring은 유지한다. | Spot messaging, runtime monitoring·metrics·tracing, 다섯 exact interface·E2E |
| `CA-D78` | Session callback의 선택적 disconnect 통지와 message별 route 재조회 / Framework automatic all-bound 통지와 stored exact binding route | Bind 성공 때 Session owner가 Actor별 exact route·generation·lease fence를 저장한다. Relay·request relay와 disconnect는 Location Store를 message마다 조회하지 않고 저장 route를 owner lease·local admission deadline 안에서 사용한다. Physical disconnect는 current binding 전체에 automatic all-settled 통지를 수행하며 exact binding identity마다 Spot callback을 최대 한 번 실행한다. Public `NotifyDisconnected`는 연결이 유지된 상태의 logical notification이다. Relocation route update는 같은 ObjectGeneration에만 허용하고 owner·membership commit, callback·journal replay, durable source cleanup, `Completed`, command 44·45 route switch·ACK, steady normalization 뒤 target admission을 연다. | Session Actor dispatch, Actor·Spot·Location·maintenance, wire schema, 다섯 exact interface·runtime·E2E |

`CA-D55` review checkpoint(2026-07-24)에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet이
수정된 공통 spec, Redis current·history 13/17 field, 다섯 언어 exact interface, Config 14와
protocol schema·golden을 독립 검토해 clean으로 판정했다. `WIRE` self-test는 37 commands,
161 types, durable fixture 4개와 negative 200건이 통과했고 generated asset 35개와
`git diff --check`도 통과했다. 전체 `DOC` runner는 이 변경이 아닌 병행 C++ public member
trace drift(`expected=4367`, `actual=4386`)에서 중단됐으므로 reviewed trace를 임의로 갱신하지
않았다. Node production runtime은 Pending·Ready startup recovery, exact metadata, application
Instance factory와 handler terminal 뒤 recovery root release까지 연결했지만 public Spot address
transport와 User Spot generic creation coordinator가 남아 있어 `V11-M6B-NODE`는 계속 `진행`이다.

Node Spot address·manager checkpoint(2026-07-24)에서 exact single-use create/get-or-create builder,
authority-first Ready routing, Missing Instance target selection과 command 39 activation, User Spot generic
Reserve·factory·initialize·Commit coordinator를 public host에 연결했다. User Spot creation content는
Location reservation domain만 사용하고 Relocation Store를 요구하지 않는다. 하나의 deadline signal을
store·factory·initialize·commit과 concurrent waiter에 적용하며 `Rejected`는 예외로 바꾸지 않고 Pending을
Abort한 뒤 terminal result로 반환한다. Ready authority는 User·Instance kind와 stable type을 exact
검증하고 ZLIA의 최초 message metadata를 target dispatch까지 보존한다. Handler terminal completion은
replay cursor를 inbox sequence까지 올리는 durable CAS와 recovery pointer를 해제하는 CAS의 두 단계로
분리하며 중간 crash recovery는 handler를 다시 실행하지 않고 pointer release만 재개한다. Public
`SpotHandle`·resolver와 Instance Spot의 Actor·subscription handler surface를 제거하고
`onClosing(context, AbortSignal)`을 exact interface에 맞췄다. Host Shutdown drain은 User·Instance Spot에
`HostShutdown`, explicit close는 `ExplicitClose`를 전달하고 deadline 뒤 callback 대기를 종료한다.
이 checkpoint 당시 구현은 automatic create의 generated RID 충돌을 새 RID로 재시도했다. `CA-D20`이
이를 대체하므로 이 동작과 관련 test는 완료 근거가 아니다. 각 Framework-issued Spot identity sub-ID에서
첫 active collision을 즉시 `RoutingIdConflict`로 끝내고 두 번째 UUID·reservation을 만들지 않도록 수정해야
한다. Explicit get-or-create의 remote owner는 fail-closed한다. Placement와 Store·commit 실패는
`PlacementCapacityExhausted`·`RequestFailed`로 구분한다. TypeScript typecheck·build, M6B 32/32,
M6C 31/31, public contract 27/27, wire validator, scoped ESLint와 `git diff --check`가 통과했다. Codex
`gpt-5.6-sol xhigh`와 Claude Sonnet의 최종 독립 review는 P0·P1·P2 finding 0으로 수렴했다.

Remote User Spot create와 remote Spot close는 현재 service wire에 generation-fenced create·close operation과
terminal correlation이 없어 local-only 구현을 remote 성공으로 확장하지 않았다. Location polling은
`Rejected`·application reply를 보존하지 못하므로 금지하고 두 operation은 명시적으로 실패한다. 이 gap은
`CA-D56`의 protocol schema·네 runtime owner가 소유하며, 해당 command와 cross-node contract가 구현되기
전에는 `V11-M6B-NODE`를 완료로 판정하지 않는다. Sample·E2E source는 변경하거나 실행하지 않았다.

`CA-D16`은 두 값을 공개하지만 invalid 조합을 runtime에 넘기지 않는다. `CA-D61`은 `CA-D23`의 generic
population capacity를 대체한다. Population 기본값은 unlimited지만 activation concurrency는 별도 admission
option으로 bounded default를 유지한다. Descriptor와 Store reservation은 `CA-D62`의 typed capacity vector를
같은 의미로 게시·검증한다. `CA-D11`, `CA-D15`, `CA-D17`, `CA-D21`의 byte·count bound는 protocol schema의
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
채택하지 않았거나 표현을 바꾼 항목의 결론과 이유는 `CA-D01~CA-D63`이 소유한다. 임시 설계 입력은
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
| `V11-M6A-CPP` | C++ topology·dispatch·Location·liveness runtime | C++ lane, `P-DEEP` | `V11-R4B` | 수정 진행 | node·Channel·ClientServer·manual·automatic classic fanout, remote placement, mailbox·CAS·reconnect·liveness internal contract 통과 | Public raw ROUTER·DEALER·PUB·SUB API로 node·Channel send/request, ClientServer 독립 admission·send/request, Location descriptor publish/watch/CAS, manual·automatic classic fanout과 publisher별 reconnect, bounded mailbox, terminal-once registry, 5초/15초 liveness를 구현했다. 실제 `mesh_node_runtime` public host가 Framework-owned raw owner를 생성하고 app·host dispatch를 이 경계로 연결한다. ClientServer public configuration은 exact `client()`·`server()` role builder로 분리했고 legacy `enable_client`·`enable_server`와 parent의 role별 socket option을 제거했다. 같은 ChannelName의 dual role과 서로 다른 ChannelName의 동일 역할은 허용하고 같은 ChannelName·role 중복은 socket bind 전에 거부한다. Server RID는 public builder가 아니라 runtime lifecycle identity 경계에서 생성한다. `listen(0)`은 실제 bound port를 보존하면서 advertise host만 descriptor에 반영하며, 같은 process의 local Server도 기존 DEALER→ROUTER admission·request/reply 경계를 사용한다. Contract headers, M6A runtime과 `SameProcessClientServerUsesLocalReadyServerWithoutExternalStoreOrManualEndpoint`, `ClientServerPortZeroPublishesAdvertiseHostWithBoundPort`, 역할 중복·다중 ChannelName을 포함한 resolver 36/36이 통과했다. Compatibility header나 Core·bindings 수정은 없고 Sample·E2E 변경·실행은 0이다. Sample·E2E의 legacy builder callsite는 재활성화 단계 migration gap으로 남긴다. |
| `V11-M6A-DN` | .NET topology·dispatch·Location·liveness runtime | .NET lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·mailbox·CAS·Task terminal winner·liveness internal contract 통과 | 최신 `Systems.Zlink` 11.0.0 package의 public raw API로 managed MeshNode를 구현했다. 실제 두 node admission·remote `ToChannel`을 포함한 foundation 11/11과 backend·monitor·dispatch·Location 62/62가 통과했다. R5A 수정에서 descriptor extension의 필수·unknown TLV와 원본 descriptor bytes를 보존하고, 같은 lifecycle의 revision 증가·같은 revision exact-byte idempotence·immutable field 변경 거부를 mutation 전에 검사했다. Foundation focused regression 13/13이 통과했다. .NET source에는 target exact interface의 object role·placement weight·active/pending capacity public builder가 아직 없어 실제 application configuration 연결과 physical connection identity 기반 duplicate-pipe 판정은 후속 public-contract parity가 필요하다. 상수로 완료 처리하지 않았다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6A-JVM` | JVM topology·dispatch·Location·liveness runtime | JVM lane, `P-DEEP` | `V11-R4B` | 수정 진행 | Java·Kotlin API, remote placement, CAS·executor·coroutine·reconnect internal contract 통과 | 최신 `systems.zlink:zlink:11.0.0` public raw binding만 사용해 Framework ROUTER owner와 exact hello·admit·update, Node·Channel send/request/reply, bounded mailbox, Location CAS/watch, placement selector, reconnect와 5초/15초 liveness를 구현했다. Classic fanout connection fence·beacon·timeout contract를 포함한 service·binding regression과 M5 foundation, Java·Kotlin compile이 통과했다. 전체 core test 383개 중 M6B stateful Spot·Actor 구현을 요구하는 기존 6개만 격리됐다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6A-NODE` | Node topology·dispatch·Location·liveness runtime | Node lane, `P-DEEP` | `V11-R4B` | 수정 진행 | topology·remote placement·CAS·Promise·event-loop·reconnect internal contract 통과 | Public raw binding만 사용하는 owner에 admission, node·Channel send/request, mailbox, topology·placement, Location CAS, liveness와 전용 ClientServer·fanout registry를 구현하고 public host factory를 연결해 제거된 `createMeshNode` 의존을 없앴다. Framework TypeScript compile, M6A 7/7, M5 4/4와 changed-source ESLint가 통과했다. M6B 기능은 가짜 성공 없이 `NotSupported`로 유지하며 Sample·E2E 변경·실행은 0이다. |
| `V11-R5A` | Topology runtime slice 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M6A-CPP`, `V11-M6A-DN`, `V11-M6A-JVM`, `V11-M6A-NODE` | 수정 진행 | topology·dispatch·placement·authority·liveness와 실행 격리의 I1·I2·I3 review clean | Codex 5.6 sol xhigh review에서 확인한 C++ public host의 삭제된 Core Service header·owner 잔존은 Framework raw owner·stateful runtime을 실제 app·MeshNode·Spot·Actor·STREAM host 경계에 연결해 해소했다. 전체 C++ framework compile과 focused regression 5/5가 통과했다. .NET descriptor의 ObjectRole·security·placement/capacity configuration과 physical connection identity 기반 duplicate-pipe 판정은 public-contract parity 후속 조건으로 남는다. Sample·E2E source 변경은 0이다. |

Framework service runtime은 제거한 Core heartbeat option을 설정하지 않는다. Raw monitor는 orderly disconnect를
즉시 알리고 Framework liveness probe scheduler가 half-open deadline을 소유한다. Location owner lease와
Framework STREAM heartbeat는 이 deadline을 authority나 application session progress로 재사용하지 않는다.
Fanout subscriber는 publisher별 전용 SUB socket을 사용하며 reserved beacon을 application message로
전달하지 않는다. Publisher 하나의 deadline은 다른 publisher의 ready 상태를 바꾸지 않는다.

### 10.2 M6B — Spot, Actor, STREAM과 Instance Spot

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6B-CPP` | C++ stateful object runtime | C++ lane, `P-DEEP` | `V11-R5A` | 수정 진행 | turn·membership·session·Instance activation과 stale generation contract 통과 | Framework-owned generic object authority에 MeshName과 분리한 global identity, weighted remote create reservation·Ready barrier, concurrent create join, exact-generation destroy, cross-node membership CAS와 application·infrastructure turn 분리를 구현했다. 이동 중 ingress hold와 commit·abort queue handoff, logical timer registration·pending tick 보존, Instance marker cold activation, STREAM connection·binding generation과 exact Actor authority fence를 검증한다. Public raw ROUTER의 exact Spot·Actor route fence send/request/reply와 stale fence terminal-once를 실제 public host에 연결했다. `mesh_node_runtime`, Spot·Actor dispatch, RouteMesh monitoring과 STREAM session binding은 제거된 Core Service owner 대신 Framework raw owner·stateful registry를 사용한다. `CA-D56`은 exact `spot_manager_t` fluent surface와 `spot_ref_t`, Location `reserve()`에서 command 47·48로 이어지는 source·target production 경로, concurrent Creating join, `inline-v1` creation reference 검증과 same-node raw command loopback을 구현했다. 공식 Redis provider도 provider-issued reservation ID, content reference, SHA-256과 encoded size를 Pending current·history snapshot에 저장하고 Commit에서 current projection을 제거하며, runtime은 provider별 side interface 없이 public `authority_snapshot_t::pending_creation`을 사용한다. 이 checkpoint의 `Create` generated RID active-collision retry는 `CA-D20`이 대체했으므로 완료 근거가 아니다. `V11-M6B-ENTRY-IDENTITY-CPP`에서 첫 collision 즉시 `RoutingIdConflict`, 추가 UUID·reservation 0건으로 수정해야 한다. Caller RID를 받은 `GetOrCreate`는 같은 Ready incarnation을 `Existing`으로 반환하며 type mismatch 의미를 유지한다. Command 47·48 source는 transport timeout, typed wire failure와 target conflict를 기존 public error kind인 `DeadlineExceeded`, `SpotGenerationStale`, `SpotMoving`, `SpotTypeMismatch`, capacity와 rejected 의미로 보존한다. Target은 type mismatch·stale generation·moving terminal을 exact code로 저장하고 같은 operation replay에도 그대로 반환한다. Command 47 source는 자신이 만든 Pending reservation만 exact key·reservation fence로 추적하고 submit throw·not-admitted와 authoritative failure/rejected terminal에서 즉시 abort/reconcile한다. Concurrent Creating join과 timeout·disconnect처럼 target outcome이 불명확한 terminal은 cleanup하지 않으며, 이미 Commit된 authority에는 exact abort가 stale로 끝나므로 successor를 변경하지 않는다. Redis suite 25건 중 endpoint round-trip을 포함한 23건이 통과했고 cross-language 전용 prefix가 필요한 2건만 skip됐다. resolver 36/36, service wire codec과 M6B runtime, contract headers가 통과했다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. |
| `V11-M6B-DN` | .NET stateful object runtime | .NET lane, `P-DEEP` | `V11-R5A` | 진행 | turn·membership·session·Instance Task 경쟁 contract 통과 | Framework-owned managed MeshNode에 owner별 application·infrastructure mailbox와 claim, local·remote Spot·Actor exact-generation dispatch, accepted join membership commit, lifecycle, logical multicast, Actor request terminal CAS, relocation seal·abort·commit, STREAM exact Actor binding·relay와 Instance Spot reactivation generation을 구현했다. R5B review에서 확인한 무제한 request operation table은 기본 65,536개 capacity를 ID·deadline 할당 전에 원자적으로 검사하고 request에는 `Backpressured`를 반환하도록 수정했다. Inbound remote Spot·Actor는 queue admission 전에 target node lifecycle, object generation과 Location row에서 받은 authority owner generation을 검사한다. Object Server가 Recreate·Snapshot policy를 하나라도 등록하거나 Instance Spot factory를 하나라도 등록하면 Relocation Store 누락을 socket bind 전 configuration validation에서 거부한다. Disabled Actor·Spot만 등록하고 Instance Spot factory가 없으면 요구하지 않는다. `CA-D56`은 exact `IZLinkSpotManager` fluent surface, `SpotRef`, public Create·GetOrCreate·Find·Close, command 47·48, client-only source, concurrent Creating join, terminal replay, Ready staging, close rollback과 InMemory·Redis Pending projection을 production path에 연결했다. Generic User Spot request는 Relocation Store가 아니라 Location reservation의 inline envelope를 사용한다. Stateful 16/16, Redis authority 9/9와 Framework·Unit build warning·error 0이 통과했다. 전체 unit에는 기존 documentation regression 7건과 ClientServer timing·liveness 2건이 별도 실패로 남아 있고, fixed packaged verifier는 기존 package snapshot의 Systems.Zlink 10.1.0/11.0.0 및 XML hash 불일치에서 중단된다. 실제 Instance durable activation 연결을 완료하기 전에는 이 row를 완료로 판정하지 않는다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6B-JVM` | JVM stateful object runtime | JVM lane, `P-DEEP` | `V11-R5A` | 진행 | Java·Kotlin turn·membership·session·Instance contract 통과 | Framework가 소유하는 Spot mailbox와 lifecycle generation, local·remote Spot send/request single-terminal·timeout, Actor create·join accepted commit·membership epoch·leave lifecycle과 local·remote Actor owning-Spot dispatch를 public raw binding 위에 구현했다. Logical Multicast는 local subscription fan-out과 remote MeshNode별 ROUTER command 23 전송을 연결한다. Executor가 작업을 시작한 뒤에는 target별 수락 결과를 집계하거나 public·internal monitoring에 기록하지 않고, 일부 target 전달 실패를 전체 operation 실패나 자동 retry로 바꾸지 않는다. Spot resolver와 Actor Location row에서 받은 `AuthorityOwnerGeneration`을 raw route registry와 wire에 전달하며 receiver는 target node lifecycle, object generation과 authority owner generation을 application admission 전에 exact 비교한다. command 39는 closed codec, source·target node lifecycle, 등록된 exact Instance authority fence, object generation을 검증한 뒤 stable-type cold activation barrier와 Spot mailbox로 dispatch한다. Remote bound STREAM은 command 36·38 closed codec과 exact Actor·object·authority·node·binding·session generation fence를 사용해 bind·send·close를 owner node로 전달한다. `CA-D56`은 canonical stable-type Spot manager call builder, startup descriptor publication, Location inline creation envelope, command 47·48 actual peer transport, semantic fingerprint·terminal replay, factory-once Ready barrier, close rollback과 InMemory·Redis Pending projection을 연결했다. Durable authority runtime은 startup scan·watch subscription·재scan 순서로 `PENDING + CREATING Instance`를 command 39 intent에 자동 등록하고, `ACTIVE + READY`가 되기 전에는 일반 Spot route로 공개하지 않는다. Watch는 재scan 신호로만 사용하며 authority generation을 event나 legacy explicit `ActorRef`에서 추정하지 않는다. Core 470 tests, actual peer M6B 19/19, Kotlin compile·test와 Redis unit·fixture가 통과했고 post-fix review는 `APPROVE`했다. Redis endpoint 의존 12건은 환경변수 부재로 skip됐고 전체 Spring suite의 기존 monitoring·handler 환경성 6건은 별도 실패지만 이관한 canonical Spot manager bean test는 통과했다. Sample·E2E 변경·실행은 0이다. |
| `V11-M6B-NODE` | Node stateful object runtime | Node lane, `P-DEEP` | `V11-R5A` | 진행 | turn·membership·session·Instance Promise 경쟁 contract와 public command 47·48 native two-process contract 통과 | public raw binding 위에 Spot·Actor exact-generation dispatch, owner별 serial turn, membership epoch, logical multicast local fan-out·remote node당 1회 전송, STREAM binding generation·delivery와 terminal-once Promise registry를 연결했다. Instance Spot stable type·attempt reservation, command 39 exact route·source·authority fence와 registered intent cold activation generation도 연결했다. Request operation table은 기본 65,536개 capacity를 ID·timer 할당 전에 검사한다. `CA-D55`는 complete activation envelope, Pending projection, Ready recovery root·cursor, startup exact scan·queue-head restore, descriptor mismatch abort·orphan cleanup을 연결했다. `CA-D56`은 public manager와 host의 command 47·48 source·target, inline Location creation envelope, exact Pending integrity, Ready staging, concurrent reservation completion join, close rollback과 deadline+5분 terminal replay를 연결했다. Framework startup은 공식 Location Store에 owner lease로 MeshNode endpoint·object role·User Spot·Instance Spot·Actor capability를 durable publication하며, 빈 Redis authority page도 정상 처리한다. 별도 process 두 개가 public Nest builder와 `ZLINK_SPOT_MANAGER`, 공식 Redis provider를 사용해 remote User Spot Create/GetOrCreate/Find/Close를 실제 native socket command 47·48로 완료했다. Post-review P1 수정에서 MeshNode descriptor publication을 Location runtime 경계로 모으고 Core가 확정한 endpoint와 exact·legacy effective capability를 사용해 Preparing revision 1, Serving revision 2, Draining revision 3 이상을 strict하게 게시하도록 고쳤다. Command 48의 Closing CAS·local close·authority delete는 target만 소유하며 terminal reply 유실 뒤에도 Ready authority를 복원하지 않는다. `ZLinkActorFactory.create`는 exact optional AbortSignal을 받고 일반 actor creation이 같은 signal을 전달한다. 후속 독립 review에서 RouteMesh listener identity의 bind·advertise host exact surface, application version·maintenance wave·security identity 설정, command 48 membership seal과 bounded cleanup signal, 실제 transport terminal replay 증거가 남아 있음을 확인했다. 이 항목을 수정하고 focused regression이 통과하기 전에는 완료로 판정하지 않는다. 공통 E2E·sample source 변경·실행은 0이다. |
| `V11-R5B` | Stateful runtime slice 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M6B-CPP`, `V11-M6B-DN`, `V11-M6B-JVM`, `V11-M6B-NODE` | 수정 진행 | global identity, remote create, mailbox ordering, ownership·fencing·resource와 실행 격리의 I1·I2·I3 review clean | Codex 5.6 sol xhigh review에서 세 managed language의 authority owner generation 미연결, command 39 durable Instance cold activation 미연결, JVM logical multicast no-op, 세 언어의 무제한 request operation table과 JVM mailbox close retained payload를 P1으로 확인했다. JVM mailbox close, JVM·.NET·Node operation capacity 수정과 focused regression은 통과했다. JVM은 정식 authority 결과·mutation·aggregate 계약과 public provider registration seam을 추가했으며, 공식 Redis provider와 durable publication 연결은 M6C에서 계속한다. authority·Instance·logical multicast 연결은 언어별로 병렬 수정한다. Sample·E2E source 변경은 0이다. |

`V11-M6B-DN` command 39 checkpoint(2026-07-24):

- command 39 route를 `Ready` authority fence와 target-owned `ColdActivation` descriptor fence의 닫힌
  union으로 고정했다. 기존 Ready route의 version byte `1`과 body는 유지하고 ColdActivation은 version
  byte `2`에서 target Mesh, stable type, descriptor version과 deadline을
  전달한다. ColdActivation route와 `ZLIA`가 이 값을 exact하게 보존해야 한다.
- 공통 schema·golden·validator·decoder fixture와 다섯 언어 exact interface를 같은 계약으로
  갱신했다. unknown route kind, Ready route의 cold field, ColdActivation route의 authority fence와
  route/`ZLIA` authority fence와 deadline 불일치를 negative self-test로 거부한다.
- .NET managed MeshNode는 ColdActivation command 39 source submit, admitted peer와 source·target lifecycle
  검증, metadata와 first-message frame 보존, target dispatch와 request terminal 반환을 연결했다.
  schema validator, generated asset check, decoder fixture verifier와 command 39 codec·actual peer focused
  test가 통과했다.
- durable activation은 아직 완료하지 않았다. target factory·initialize barrier, canonical binary
  `ZLIA` 저장, provider-issued Pending reservation, Ready publication, startup Pending recovery, durable
  terminal·cursor와 Preserve cleanup을 하나의 production coordinator로 연결해야 한다. 이 연결 전에는
  `V11-M6B-DN`을 완료로 판정하지 않는다. 검토 중 작성했던 별도 JSON coordinator는 canonical
  authority payload와 충돌하고 loser·recovery·cleanup correctness를 보장하지 못해 소스에 남기지 않았다.

.NET Actor creation checkpoint(2026-07-24):

- Public `IZLinkActorManager`를 single-use `Create`·`GetOrCreate` fluent call과 `Created`·`Existing`·
  `Rejected` result로 전환하고 별도 public Actor directory와 caller-selected placement를 제거했다.
  Source·sample·E2E 소비 코드는 삭제하지 않고 같은 public manager 계약을 사용하도록 맞췄다.
- Managed MeshNode command 49에 source·target lifecycle fence, semantic `OperationId`, duplicate body 검증,
  target single execution과 retained terminal replay를 연결했다. Backend wrapper는 remote request와
  application reply를 completion pump로 반환하며 local factory로 우회하지 않는다.
- Source manager는 `Serving`, Actor capability, Actor aggregate active·reserved capacity와 positive
  node-wide weight로 target을 선택하고 generic `Reserve`의 capacity race에서 해당 후보를 제외해 다시
  선택한다. Target은 immutable creation intent를 검증하고 Actor factory와 Entry Spot create callback을
  staging 상태에서 실행한 뒤 `CompleteCreation`으로 Ready authority와 `Created` terminal을 함께
  publish하거나 `Rejected`·`Failed` terminal과 reservation cleanup을 함께 확정한다.
- staging Actor는 Ready commit 전 `Find`와 dispatch에서 제외하며 commit 성공 뒤에만 publish한다.
  command reply의 correlation은 retained semantic terminal에 저장하지 않고 현재 전달의 correlation으로
  다시 encode한다.
- Public `FindAsync`와 `FindSpotAsync`는 local registry가 아니라 global Ready Actor authority를 읽어
  remote `ActorRef`와 current `SpotRef`도 반환한다. Public `DestroyAsync`의 remote owner command는 아직
  production 경로가 없으므로 remote Actor를 local miss인 `false`로 숨기지 않고 typed gap으로 실패한다.
  remote destroy와 generic authority Delete가 연결되기 전에는 Actor manager 완료로 판정하지 않는다.
- Checked-in `Zlink.Framework.Locations.Redis.Tests/ActorCreateCommandRuntimeTests`에서 실제 inproc 두
  node를 연결해 첫 `Rejected`, 같은 `OperationId` 재전달의 target 실행 횟수 유지, 별도 operation의
  두 번째 target 실행을 검증했다. 동일 process의 object server도 Actor·User Spot의 일반 placement
  후보에 포함되는 조건을 함께 고정했고 focused test 2/2가 통과했다. Redis authority suite는 Actor
  pending·active population projection과 pending·active capacity overflow를 포함해 12/12가 통과했다.
  저장소 UnitTests project는 기존 SpotId·mock interface 미전환 compile gap 때문에 추가한 정식 codec
  test를 아직 실행하지 못했다.
- Actor와 User Spot source manager는 local RID를 후보에서 제외하지 않는다. 선택한 target이 같은
  process이면 같은 target coordinator를 직접 실행하되 reservation fence, source lifecycle,
  `OperationId`, immutable intent, staging과 Ready publication 규칙을 remote command와 동일하게
  적용한다. Descriptor 조회부터 reserve, Creating 대기와 target callback까지 각 submit 시작 시 계산한
  하나의 absolute deadline과 연결된 cancellation token을 사용하며, remote transport에는 남은 시간만
  전달한다. User Spot의 선행 Creating이 cleanup되면 해당 caller가 새 reservation을 다시 경쟁한다.
  Production InMemory integration은 같은 process server의 local Actor create와 첫 target `Reserve`의
  capacity race 뒤 두 번째 remote node의 factory·Entry Spot callback 성공을 직접 검증한다. 이 과정에서
  Entry Spot row가 node lifecycle generation `0`을 publish해 모든 후보가 fence에서 탈락하던 문제를
  발견했다. Node startup은 non-zero Core lifecycle generation을 bounded wait한 뒤 같은 값을 Entry row,
  descriptor와 command target fence에 사용한다. Reserved Actor staging은 Ready 전 public dispatch만
  차단하고 factory의 context·native Actor binding은 허용한다. Framework build와 command 49
  replay·local target·capacity-race focused test 4/4가 통과했다.
- exact capacity public shape를 `ZLinkPopulationCapacity`, `ZLinkSpotTypeCapacity`,
  `ZLinkPlacementCapacity`로 정리하고 InMemory·Redis projection과 Lua admission을 맞췄다. Framework와
  Redis provider build, packaged contract generation, capacity fixture·admission·projection 6건이 통과했다.
  실제 Actor manager의 target capacity race와 cross-node callback까지 묶은 checked-in integration
  test도 위 focused 4/4에 포함했다.
  서로 다른 concurrent `GetOrCreate` operation은 앞선 `Rejected` reply를 공유하지 않고 cleanup 뒤 새
  reservation을 경쟁하며, 동일한 `OperationId` 재전달만 retained terminal을 재사용한다. 이 coordinator의
  cross-node·rejection·failure·capacity race test가 통과하기 전에는 `V11-M6B-DN`을 완료로 판정하지 않는다.

Node placement·Logical Multicast checkpoint(2026-07-24):

- `CA-D70`에 따라 Node public call, factory option, descriptor capability·digest, creation intent와
  InMemory·Redis admission에서 caller-defined placement selector를 제거했다. 최초 배치는 stable type,
  `Serving`, active·pending capacity와 node-wide weight만 사용한다.
- `CA-D71`의 publish 결과는 remote source outbound transport queue 제출과 origin local Spot application
  queue 제출만 집계한다. Remote Spot queue와 remote·local handler 실행·완료는 기다리지 않는다.
- Node build, InMemory·Redis Location contract 15/15와 관련 Spot·Logical Multicast focused 4/4가 통과했다.
  전체 typecheck의 `sourceSpotRid`→`sourceSpotId` 테스트 상수명 1건은 병렬 SpotId 전환 범위다.
- Root service wire schema·golden에서도 세 selector field를 제거했다. Validator self-test는 40 commands,
  167 types와 negative 233건, generated asset write/check가 통과했다. .NET Framework build는 기존 nullable
  warning 23건 외 성공했고 Redis provider build는 warning·error 0, Redis Location 25/25와 Authority
  Relocation 10/10이 통과했으며 .NET source·test·API snapshot의 해당 identifier 검색 결과는 0건이다.
- Framework overview·API, Instance Spot E2E와 service wire internals의 남은 표현도 `CA-D70`에 맞췄다.
  `framework/doc/framework` 전체에서 제거한 세 selector identifier와 문구의 검색 결과는 0건이며 cold
  route·ZLIA는 target Mesh·stable type·descriptor version·deadline만 보존한다.

Placement option 독립 review checkpoint(2026-07-24):

- Codex 5.6 sol xhigh review에서 제거 대상 identifier가 정식 spec·exact interface·service wire와 현재
  public source에서는 제거된 것을 확인했다. 다만 obsolete Actor placement surface, 언어별 typed capacity
  선필터와 reservation 재선택, JVM direct Spot publish detail, stale public-contract trace와 일부 test
  compile gap을 완료 차단 이슈로 분류했다.
- Common Instance Spot E2E에 남아 있던 profile·affinity 조건 세 곳을 stable type·capacity·node-wide
  weight로 고쳤다. Logical Multicast의 remote capacity는 수신 Spot queue가 아니라 remote target으로 향하는
  source-local outbound transport queue의 capacity임을 common spec과 submit-admission E2E에 명시했다.
- C++에서 obsolete `actor_placement_t`·`preferred_node_rid`·`actor_directory_t::ensure`를 제거하고 Actor
  target surface를 exact interface에 맞췄다. Actor·Spot aggregate와 Spot stable type별
  `active/reserved/limit` projection, Redis codec·immutable digest·Lua capacity gate, Actor·User Spot
  capacity-first weighted selection과 capacity·stale target 실패 후 bounded candidate 재선택을 구현했다.
  C++ framework link, M6B first-fail→second-success test, Redis projection·digest test와 InMemory Location
  Store test 1/1이 통과했다.
- JVM도 같은 exact capacity projection을 InMemory·Redis descriptor, digest와 Lua admission에 적용했다.
  Actor·User Spot은 실제 descriptor usage로 후보를 먼저 거르고 reserve race에서는 다른 lifecycle을
  재선택한다. Direct Spot publish는 backend의 source-local detail 7개를 그대로 반환한다. 실제 Redis
  suite를 포함해 Core 485, Redis 32(+2 skipped), Kotlin 45 test와 exact Location Store contract test가
  통과했다.
- Public-contract review baseline은 제거한 selector와 새 fluent Actor surface를 반영해 refresh했다.
  Trace write·check는 member 6,681개, intentional removal 1,218개, unclassified·ambiguous·unknown 0으로
  통과했다. Contract amendment impact policy에는 `CA-D65` SpotId, `CA-D58~59` Yield,
  `CA-D67` lifecycle과 reviewed exact surface 제거 근거를 닫았고 manifest 4,056개를 write·check했다.
- 전체 framework document verifier는 SpotId 전환 범위의 common internals semantic,
  authority·MeshNode fixture와 .NET·JVM·Node amended object contract를 정식 계약에 맞춘 뒤 다시
  실행했다. Service wire 233개 negative self-test, public trace, exact interface 56개, formal document
  137개와 Redis fixture 5개를 포함한 전체 검증이 통과했다.
- 후속 .NET production 검증에서 MeshNode 시작 직후 Entry Spot row의 owner node generation이 `0`으로
  게시될 수 있는 race를 확인했다. Runtime은 Core가 non-zero lifecycle generation을 게시할 때까지
  bounded wait한 뒤 Entry Spot location을 claim하므로 descriptor, Entry location과 command target fence가
  같은 generation을 사용한다. 실제 manager test는 same-process target 생성과 첫 candidate의 capacity
  reservation 실패 뒤 두 번째 remote node의 factory·Entry callback 성공을 검증했고 command 49 replay
  test와 함께 4/4가 통과했다.
- 최종 focused review에서 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet은 `CA-D70`·`CA-D71`, .NET command 49,
  local·remote 동일 후보 규칙, capacity race 재선택, 하나의 absolute deadline, reservation·lifecycle
  fence, operation-scoped terminal replay와 staging visibility를 다시 대조해 actionable finding 없이
  `CLEAN`으로 판정했다.

Placement·publish 후속 drift 점검(2026-07-24):

- 정식 spec과 통합 중인 common 사본에서 남아 있던 구형 `ZLinkObjectPlacementOptions`,
  `object_placement_options_t`와 `placement option` 표현을 Actor·User Spot·Instance Spot별 factory option
  계약으로 맞췄다. .NET exact interface의 고아 initializer 세 개와 Java `javap`의 제거된
  `Set<String>` constructor도 정리했다.
- `PlacementProfile`·`AffinityKey` 계열은 결정 이력을 제외한 Framework spec·public source·wire·test·E2E에서
  0건이다. Instance Spot verifier에는 두 selector의 재도입을 거부하는 negative mutation을 추가했다.
- Logical Multicast의 다섯 언어 exact interface와 public source는 bounded worker와 source-local capacity를
  확보해 publish transaction을 시작하면 결과값 없이 완료하며 target별 제출 결과를 집계하지 않는다. Node binding 제거 뒤 남아 있던 commit fixture를 Framework
  MeshNode backend로 전환했으며, pre-start cancellation은 mock test, post-start cancellation은 실제 backend
  test가 각각 소유한다.
- 검증은 Instance Spot contract 5개 언어·formal 문서 5개·negative 16건, submit API 5개 언어·scenario
  20개·regression 4건, .NET Logical Multicast 3/3, JVM publish focused test와 Node Logical Multicast
  7/7이 통과했다. Contract amendment manifest 4,056개도 제거된 affinity scenario 이름 없이 재생성하고
  write/check를 통과했다.
- 실제 process E2E `SA-E2E-13`은 .NET·Java·Node·C++ feature map에서 미구현 상태이므로
  `V11-M6A-E2E` 완료 근거로 사용하지 않는다.
- 이번 runtime 합류 뒤 root가 Instance Spot verifier를 다시 실행해 언어 5개·formal 문서 5개,
  required fragment 112개와 forbidden rule 11개가 모두 통과했다. 현재 verifier invocation의
  optional negative mutation 입력은 0개였으며 이를 이전 16건 실행과 합산하지 않는다.

JVM Entry Spot identity checkpoint(2026-07-24):

- Object Server registration은 caller가 Entry Spot ID를 지정하는 표면을 제거하고
  `<diagnostic-prefix>-entry-<lowercase-canonical-uuid-v4>`를 발급한다. Descriptor는 exact
  `entrySpotId`를 immutable digest에 포함하며 In-memory와 Redis provider가 descriptor identity와 global
  Entry identity를 같은 admission에서 claim하고 exact owner cleanup으로 해제한다.
- Redis v3 Entry identity HASH를 `state`, `spotId`, `descriptorKey`,
  `lifecycleGeneration`, `ownerId`, `ownerLeaseGeneration` 여섯 field와 TTL 없음으로
  고정했다. Real Redis test가 schema, active collision, exact cleanup과 stale cleanup 무효를 검증한다.
- User Spot `GetOrCreate`는 Framework Entry ID 예약 형식을 call 생성 시점에
  `InvalidConfiguration`으로 거부하므로 Store read·reservation과 factory 실행 전에 끝난다.
- Java core 488/488과 Redis provider 35건 중 환경에 따른 2건 skip·실패 0이 clean rebuild에서
  통과했고 Kotlin runtime source도 다시 compile했다.
- `V11-M6B-ENTRY-IDENTITY-JVM`은 완료로 전환하지 않는다. Public
  `ZLinkActorContext.joinEntrySpot(...)`이 아직 caller의 target `RoutingId`를 받고, runtime의 일부
  Entry join·membership path가 descriptor의 String SpotId mapping 대신 target node RID를 요구한다.
  Exact `joinEntrySpot(request)`와 eligible Entry descriptor 선택으로 전환하고 Java·Kotlin ABI·runtime
  test를 통과해야 row를 닫을 수 있다.

Node Weight·Entry identity checkpoint(2026-07-24):

- Placement selection의 합계를 `BigInt`로 계산하고 weight `0`을 Logical Multicast remote target에서
  제외했다. Boundary, capacity-first와 100:300 deterministic selection을 포함한 M6A runtime 8/8,
  typecheck와 package build가 통과했다.
- Framework-issued Entry·User Spot UUID v4, in-memory descriptor·global identity claim, first-conflict
  무변경과 exact cleanup을 구현했다. Redis v3 provider는 shared fixture의
  `entry-spot-id:<sha256>` key와 여섯 field만 기록하며 generic User·Instance reservation 충돌도 같은 Lua
  transaction에서 처리한다. In-memory 5/5와 실제 Redis fixture·claim focused test가 통과했다.
- `V11-M6A-WEIGHT-NODE`는 runtime `mesh(meshName).placementWeight` mutation과 descriptor republish가
  아직 없어 완료로 전환하지 않는다. `V11-M6B-ENTRY-IDENTITY-NODE`도 기존 Spot path의
  `SpotRid`·`RoutingId`가 전부 String `SpotId`로 바뀌지 않아 대기 상태를 유지한다.

.NET capacity parity checkpoint(2026-07-24):

- Redis creation·relocation capacity focused test 4건은 typed Actor·Spot·Spot type bundle의 admission,
  commit 재검증과 abort cleanup을 통과했다.
- .NET provider는 descriptor JSON의 `ActivationConcurrency`, admission HASH의 activation concurrency
  limit·Entry Spot ID와 target `zlink-mesh-node-immutable-v2` digest를 게시한다. Digest는 Entry Spot ID
  presence·값, Actor·Spot population limit과 activation concurrency limit을 포함하고 mutable active count는
  제외한다. Shared fixture, 실제 admission HASH, digest fence와 capacity focused test를 함께 실행해
  7/7이 통과했다.
- 같은 canonical fixture에 대한 C++·JVM·Node provider parity와 aggregate·destroy까지 포함한 전체 bundle
  gate는 아직 끝나지 않았다. 따라서 assertion을 약화하지 않고 `V11-M6C-CAPACITY-DN`은 대기 상태를
  유지한다.
- 정식 server Redis spec은 v3 typed capacity schema를 소유하지만
  `framework/common/spec/41-location-store-redis.ko.md`의 나머지 본문에는 v1 active·pending key와 record
  설명이 남아 있다. Provider parity가 확정되면 공통 문서 전체를 v3 본문으로 통일해야 하며, 현재의
  digest 단락 수정만으로 문서 gate를 완료 처리하지 않는다.

C++ capacity parity checkpoint(2026-07-24):

- Public maintenance contract와 in-memory provider를 typed capacity bundle로 전환했다. Creation·relocation·
  aggregate·abort·delete는 Actor total, Spot total과 optional Spot stable-type bucket을 함께 변경하며
  in-memory focused 14/14가 통과했다.
- MeshNode descriptor는 population `Capacity`와 별도 `ActivationConcurrency`를 게시한다.
  `set_actor_limit`, `set_spot_limit`, `set_activation_concurrency`가 descriptor publication까지 연결됐고
  immutable digest v2·Redis v3 physical key·admission field·shared descriptor fixture focused 5/5,
  M6B runtime과 contract header compile이 통과했다.
- C++ Redis Lua의 creation·relocation·aggregate transaction은 아직 scalar counter와 participant별
  relocation reservation을 사용한다. Exact 여섯 capacity HASH와 하나의 typed bundle로
  reserve·commit·abort·destroy를 처리하는 실제 Redis gate가 끝나기 전까지
  `V11-M6C-CAPACITY-CPP`는 대기 상태를 유지한다.

ClientServer dual-role JVM checkpoint(2026-07-24):

- Java와 Kotlin이 공유하는 public configuration에 exact `client()`·`server()` role builder를 추가했다.
  같은 ChannelName의 Client와 Server를 각각 한 번 등록할 수 있고, 서로 다른 ChannelName에는 같은 역할을
  여러 번 등록할 수 있다. Java와 Kotlin focused contract가 이 조합을 검증한다.
- target RouteMesh의 `addRouteMesh(meshName).channelName(channelName)`과 ClientServer ChannelName을 startup
  validation에서 교차 검사한다. 등록 순서와 관계없이 socket bind 전에 configuration error로 실패하는
  Java test가 통과했다.
- JVM production public surface에서는 ClientServer의 기존 `enableClient`·`enableServer`,
  `clientConnections`, channel별 timeout·socket option과 explicit packet-name handler overload를
  제거했다. Active Java core test와 Kotlin focused test는 exact role builder와 framework 기본
  packet-name 규칙으로 옮겼으며 compiler warning 없이 통과했다.
- 기존 Java sample·E2E source의 ClientServer legacy callsite 110개는 이번 단계에서 수정하지 않았다.
  해당 source를 재활성화하기 전에 `client().connect(...)` 또는 `server().listen(...)`으로 옮겨야
  compile할 수 있다. 이 migration은 sample·E2E 활성화 단계의 선행 조건이며 production API에는
  compatibility member를 다시 추가하지 않는다.
- `:zlink-framework-core:assemble`과 Java core focused 99건, Kotlin focused 18건이 통과했다.
  Root `compileTestJava compileTestKotlin`도 통과해 sample·E2E를 제외한 JVM module의 production·test
  callsite가 exact interface로 compile됨을 확인했다. `server()`만 등록하고 `listen()`을 호출하지 않은
  구성은 startup validation에서 실패하며, 4-argument explicit packet-name helper는 ClientServer exact
  interface에 존재하지 않는다.

`V11-M6B-JVM` durable Instance authority checkpoint(2026-07-24):

- Authority runtime은 startup full scan 뒤 optional Location watch를 등록하고 다시 full scan한다. 따라서
  subscribe 전후 publication race를 회수하며, watch event는 row 내용을 신뢰하지 않고 authoritative
  snapshot 재scan만 요청한다. Watch가 없거나 종료되어도 bounded polling을 correctness fallback으로 유지한다.
- `PENDING + INSTANCE_SPOT + CREATING` row는 exact object·authority owner·lease·target node lifecycle과
  StoreVersion fence를 command 39 intent에 등록한다. 이 상태는 일반 Spot route로 공개하지 않는다.
  `ACTIVE + READY` 전환에서만 route를 공개하고 update·remove·shutdown에서는 이전 intent와 route를
  exact fence로 제거한다. Legacy explicit `ActorRef`에서 generation을 추정하는 경로는 추가하지 않았다.
- 검증은 authority startup·watch·Pending→Ready→Removed 1/1, command 39 actual peer 19/19,
  Framework Location runtime 6/6이 통과했다. Java core assemble과 Kotlin compile도 통과했다.
  Redis provider test는 32건 중 local unit·fixture 18건이 통과하고 외부 endpoint 의존 14건만 skip됐으며
  실패는 0이다. Sample·E2E·Core·bindings source 변경과 실행은 0이다.

`CA-D55`·`CA-D56` 구현 checkpoint(2026-07-24):

- .NET, JVM과 Node는 public User Spot manager에서 Location reservation을 만든 뒤 command 47·48을 실제
  target runtime으로 보내는 production 경로를 연결했다. Generic User Spot creation request는 Relocation
  Store를 사용하지 않고 Location reservation의 `inline-v1` opaque reference에 complete application envelope를
  보관한다. Target은 CRC32C, encoded size와 SHA-256을 factory 실행 전에 검증한다.
- 네 runtime target은 source·target lifecycle, reservation, StoreVersion, owner generation과 close의
  `Ready + UserSpot + Active`를 검사한다. 같은 operation은 semantic fingerprint가 일치할 때만 terminal을
  replay하고, live capacity가 차면 factory 전에 Busy로 거부한다. 완료 terminal은 deadline 뒤 5분 동안
  유지하며 그 뒤의 expired retry도 handler를 다시 실행하지 않는다.
- .NET focused Stateful 16/16와 Redis authority 9/9, JVM core 470과 actual peer M6B 19/19, Node M6B
  35/35·M6C 32/32, C++ M6B binary가 통과했다. C++·JVM post-fix review는 범위 내 추가 P0/P1 없이
  승인됐다. Node post-review P1은 `V11-M6B-NODE` 행의 미완료 조건으로 관리한다. .NET fixed public API
  snapshot은 generator 출력으로 갱신했다.
- C++은 exact `spot_manager_t` call builder·`spot_ref_t` surface, Location Reserve→command 47·48,
  concurrent Creating join, inline reference 검증과 local raw command loopback을 production path에 연결했다.
  `PublicSpotManagerUsesLocationReservationAndMeshCommands`가 Create/GetOrCreate→Find→Close를 검증하며
  resolver 33/33, contract headers와 M6B runtime이 통과했다.
- C++ 공식 Redis provider는 Pending current·history에 provider-issued reservation ID, immutable content
  reference, SHA-256과 encoded size를 기록하고 Commit 뒤 Active current에서는 네 field를 제거한다. Pending
  Reserve loser는 `AlreadyExists`를 반환하지 않고 같은 attempt의 snapshot을 반환한다. 공통 physical schema
  fixture와 Lua script contract 3/3, 실제 Redis endpoint round-trip을 포함한 provider suite 23/23이 통과했다.
  Cross-language 전용 prefix가 필요한 2건은 환경이 없어 skip됐다.
- 독립 C++ review에서 `InMesh` source transport 불일치, 1 MiB creation request 상한 누락, Pending
  reservation을 Ready `AlreadyExists`로 공개하던 오류와 invalid default `spot_ref_t`를 수정했다. Target
  materializer는 Commit 전 reserved facade를 local registry에 남기지 않는다. Post-fix verdict는
  `APPROVE WITH RESIDUAL GAPS`다.
- C++의 기존 `GeneratedUserSpotIdRetriesActiveCollisionWithinCreateCall`은 `CA-D20`과 상충하므로
  폐기 대상이다. 대체 contract test는 User Spot automatic `Create`의 첫 active collision이 기존 authority를
  변경하지 않고 즉시 `RoutingIdConflict`로 끝나며 추가 UUID 생성·reservation·factory 호출이 0건인지
  검증한다. Caller RID `GetOrCreate`의 `Existing`·`SpotTypeMismatch` 의미는 유지한다.
- C++ command 47·48 terminal mapping은 transport timeout과 canonical typed wire failure를 public
  `DeadlineExceeded`, `SpotGenerationStale`, `SpotMoving`, `SpotTypeMismatch`, capacity·rejected error로
  보존한다. Codec exact-code round-trip, target type-mismatch terminal replay와
  `UserSpotTerminalMappingPreservesExactPublicErrors`가 통과했다.
- C++ source cleanup은 source-created reservation과 joined reservation을 구분하고 exact fence에서만
  abort한다. Not-owned·wrong fence·exact abort·already committed 보호와 실제 materializer failure 뒤 source
  reconcile을 focused test로 검증했다.
- 남은 조건은 Node native 두 process E2E, Redis endpoint가 필요한
  JVM live round-trip과 command 39 durable Instance activation의 언어별 나머지 연결이다.
- 전체 회귀의 별도 baseline으로 .NET documentation 7건·ClientServer timing/liveness 2건, JVM Spring
  monitoring·handler 6건과 .NET packaged contract의 기존 package version·XML hash 불일치를 유지한다.
  Sample·E2E source 변경·실행은 0이고 Core·bindings 변경은 0이다.

### 10.3 M6C — Maintenance, monitoring과 hosting

M6C는 host barrier, all-or-none preflight, `Retire`·`Shutdown`, durable authority·relocation·recovery,
observability와 C++ host·ASP.NET·Spring·NestJS integration을 구현한다. 공통 Framework는 고정 maintenance HTTP
route를 만들지 않는다.

| ID | 작업 | 담당·profile | 선행 | 상태 | 완료 gate | 증거 |
|---|---|---|---|---|---|---|
| `V11-M6C-CPP` | C++ maintenance·monitoring·hosting | C++ lane, `P-DEEP` | `V11-R5B` | 진행 | preflight·relocation·recovery·terminal observation·bounded host contract 통과 | Framework stateful runtime에 permit-before-seal relocation coordinator를 추가했다. Process gate는 outbound·inbound 기본 64, Capture·Restore 기본 8과 payload in-flight 256 MiB를 queue seal 전에 all-or-none으로 예약하며, oversized unit은 gate가 비어 있을 때만 단독으로 admit한다. Current application turn이 끝난 object만 reversible seal하고 미실행 queue와 logical timer registration을 deterministic envelope로 freeze하며 seal 뒤 application ingress는 hold하고 infrastructure queue는 계속 처리한다. Immutable root를 24시간 retention으로 먼저 저장하고 CRC32C를 검증한 뒤 authority reference·inventory digest를 publish한다. CAS conflict는 orphan root를 정리하고 frozen→held 순서로 source admission을 복원한다. 응답 유실은 authority read로 reconcile하며 publication이 불명확하면 root와 seal을 보존해 recovery가 이어지게 한다. Published root missing·checksum mismatch·inventory mismatch는 source rollback 없이 `data_lost` terminal로 분류하고, valid root는 새 runtime에 queue·timer와 exact generation으로 복원한다. 이번 slice에서 host-wide coordinator를 연결했다. Retire preflight는 create·membership·close를 하나의 structural inventory barrier로 직렬화하되 기존 application queue는 unit seal까지 계속 수락하며, provider가 반환한 unit set이 canonical inventory와 exact match일 때만 `Retiring`을 publish한다. Concurrent Retire waiter는 같은 attempt를 사용하고 preflight 중 Shutdown이 admission seal을 먼저 claim하면 모든 waiter가 `EffectiveIntent=Shutdown` 결과에 합류한다. Blocked Retire는 structural barrier를 해제하고 `Serving`과 normal admission을 복원하며 host terminal result로 저장하지 않는다. Raw public host의 start 전 단일 provider-set seam은 Location authority, aggregate CAS, Relocation payload와 eligible-target preflight capability를 분리 등록하고 start·close lifecycle hook을 host terminal observer에 연결한다. User Spot과 seal 시점의 member Actor는 한 aggregate token으로 함께 freeze하고 immutable aggregate root 저장 뒤 authority prepare·commit 한 번으로 owner와 membership participant를 전환한다. Aggregate recovery는 bounded strict envelope decode 뒤 모든 participant의 root·checksum·inventory digest, source authority generation과 target owner를 먼저 검증하고 User Spot·member Actor의 queue·logical timer·canonical membership을 `recovering` staging runtime에 원자적으로 복원한다. 검증 실패와 allocation failure에는 live runtime의 partial restore를 남기지 않는다. 같은 target generation의 재실행도 root identity, stable type, queue·timer·membership 전체가 exact match일 때만 idempotent하게 합류한다. Application claim과 timer 실행은 lifecycle restore·accepted journal replay·source cleanup·completion ACK seam이 완료될 때까지 차단하며 현재 slice는 `recovery_required`로 fail closed한다. STREAM registry는 accepted inbound completion을 추적하고 Actor relocation barrier 뒤 binding generation을 교체해 stale packet을 거부하며 Shutdown의 host-wide session seal도 같은 barrier를 사용한다. M6C focused contract는 기존 5개에 all-or-none blocker rollback, User Spot 2-participant aggregate·STREAM fence, Retire/Shutdown first-intent race, post-commit ForceStopped teardown과 aggregate crash recovery를 추가했다. Raw port·wire codec·operation registry와 M6A~C focused/internal/resource/protocol 6/6, 전체 `zlink_framework` compile도 통과했다. 독립 post-fix review는 decode bound·strict ordering, exact idempotency, staging admission과 copy-on-write atomic commit을 확인하고 `APPROVE`했다. 높은 CPU 부하 구간의 combined run에서는 기존 M6A·M6B raw receive가 반복적으로 timeout성 오류를 냈다. 변경 대상이 아닌 동일 binary의 isolated repeat는 통과했고 clean 6/6 run도 확보했으므로 별도 반복성 이슈를 유지한다. 남은 gap은 descriptor `Retiring/Draining` publication과 topology teardown, standalone Actor·Instance Spot target reservation·factory·Restore, User Spot aggregate lifecycle restore·accepted journal replay·source cleanup·completion/relay ACK이다. Core·bindings와 Sample·E2E source 변경·실행은 0이다. |
| `V11-M6C-DN` | .NET maintenance·monitoring·ASP.NET | .NET lane, `P-DEEP` | `V11-R5B` | 진행 | CAS·lease·Task race·terminal observation·ASP.NET shutdown contract 통과 | `IZLinkFrameworkRuntime`의 host-wide state·snapshot·bounded observer·`RetireAsync`·`ShutdownAsync`와 ASP.NET hosting stop 연결을 유지한다. Retire preflight blocker는 admission seal 전에 `Serving`을 유지하고, Shutdown과 Retire는 first effective intent의 shared operation에 합류하며 waiter cancellation은 shared operation을 취소하지 않는다. Target .NET 계약의 `IZLinkAuthorityStore`, `IZLinkRelocationStore`, Actor·Spot relocation adapter와 policy, 분리된 `AddRelocationStore`를 추가했다. User·Instance Spot과 Actor participant를 함께 담는 deterministic root는 application state, accepted queue sequence·payload와 logical timer cursor·payload를 보존한다. Root를 24시간 retention으로 먼저 저장하고 CRC32C·immutable read를 검증한 뒤 single authority CAS 또는 aggregate prepare·commit으로 publish한다. CAS conflict와 prepare reject는 orphan을 정리하고, commit outcome exception은 authority reference를 읽어 reconcile하며 published root missing·checksum·inventory mismatch는 rollback하지 않는 data-loss로 분류한다. Managed Spot·Actor outbound frame은 object generation이나 상수 `1`을 authority fence로 쓰지 않고 Location row에서 관찰한 `AuthorityOwnerGeneration`만 사용하며 inbound는 local actual generation과 일치할 때만 dispatch한다. In-memory와 Redis Location row는 owner claim generation을 이 field로 materialize한다. 이번 slice에서 `IZLinkLocationStore`가 `IZLinkAuthorityStore`를 직접 상속하도록 연결하고 in-memory provider에 exact read·CAS·snapshot scan·reservation·aggregate state를 추가했다. 공식 Redis package에는 Location과 별도 options·connection lifecycle을 가진 `ZLinkRedisRelocationStore`를 추가했다. Relocation payload는 SHA-256 reference와 CRC32C를 사용하고 Redis `TIME` 기준 retention으로 저장·renew·read·delete한다. `ZLinkRedisLocationStore`는 같은 namespace에서 authority Preserve/Delete CAS, 1분 snapshot scan, reservation과 bounded aggregate prepare·commit·abort를 server-side script로 실행한다. Spot serial queue는 application closure를 accepted sequence·immutable payload·local executor로 분리했다. Turn boundary seal은 pending application record를 capture하고 이후 record를 hold하며 infrastructure continuation은 계속 실행한다. Abort는 infrastructure 뒤 captured→held 순서로 복원하고 commit은 source resource를 해제한 뒤 held record를 relay 입력으로 반환한다. 실제 Spot route ingress는 source RID·Spot ID·request sequence·metadata·message parts를 bounded accepted-journal record로 저장한다. Timer pump는 native handle 대신 registration, delivery·scheduled cursor, next due와 pending tick을 소유한다. Source freeze·abort resume와 target의 새 pump restore를 deterministic logical timer payload로 연결했고 freeze 뒤 timer handler가 실행되지 않도록 admission을 막는다. 정식 `IZLinkMeshObjectServerBuilder`의 stable type·placement·relocation policy 등록을 현재 MeshNode builder가 직접 구현하고 Snapshot adapter type 검증, DI 등록과 typed Capture·Restore invoker를 같은 registration record에 연결했다. User Spot capture는 같은 serial boundary에서 Spot과 canonical member Actor state를 adapter policy에 따라 수집한다. Serial focused 21/21, timer lifecycle 7/7, relocation runtime 12/12, ASP.NET·Framework build warning·error 0이 통과했다. 전체 unit은 709건 중 runtime 702건이 통과했고 실패 7건은 이동 안내 문서와 비활성 E2E fixture를 기대하는 기존 documentation regression이다. Redis 기존 회귀 8건 중 `IReadOnlySet<string>` codec 원인 6건을 수정했고 34/36이 통과했다. 남은 2건은 target fixture의 새 descriptor·authority HASH와 이전 test codec 비교가 충돌하는 contract migration gap이다. `ZLinkAuthorityMutation.Put`의 `NewOwner`·`NewObject`에는 opaque payload와 별도로 target owner token이 필요하다. 이 token을 정식 계약과 provider에 반영하고 in-memory payload decode 우회를 제거하는 작업이 진행 중이다. 실제 Retire scheduler의 target reservation·factory·Restore-before-commit·accepted reply relay ACK·STREAM fence와 aggregate completion이 아직 기존 network Actor drain을 대체하지 않았으므로 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거로 사용하지 않는다. Core·bindings와 Sample·E2E source 변경·test 실행은 0이다. 검증 중 solution 전체 build를 한 번 잘못 호출해 Sample·E2E project compile graph가 시작됐으나 package downgrade에서 중단됐고, 이후 검증은 Framework·ASP.NET과 UnitTests project로 한정했다. |
| `V11-M6C-JVM` | JVM maintenance·monitoring·Spring | JVM lane, `P-DEEP` | `V11-R5B` | 진행 | Java·Kotlin lifecycle·coroutine·Spring metadata와 shutdown contract 통과 | Host-wide `retire`·`shutdown`, runtime state·termination result·observer, 기본 64 unit·256 MiB scheduler, accepted journal queue, logical timer freeze/restore, immutable Relocation Store와 authority publication coordinator를 유지한다. `ZLinkLocationStore`가 `ZLinkAuthorityStore`를 직접 상속하며 runtime은 같은 provider를 별도 fake나 internal port 없이 authority service로 노출한다. In-memory provider도 read·`PRESERVE`/delete CAS·scan·reservation·aggregate 상태를 구현했고 공식 Redis provider의 중복 authority 선언은 제거했다. Exact object role builder와 stable type·placement·explicit relocation policy 등록 표면을 추가했다. Snapshot policy는 Actor와 Spot adapter의 generic 대상 type을 socket 생성 전 검증하며 runtime adapter registry가 stable type을 실제 adapter instance와 capture/restore 호출에 연결한다. Client·Server object role은 Location Store가 필수이고 Recreate·Snapshot policy는 Relocation Store가 필수다. Shutdown은 기존 Actor network handoff를 실행하지 않는다. Actor를 먼저 정리한 뒤 User Spot과 Entry Spot의 `onClosing`에 `HOST_SHUTDOWN`과 deadline을 전달한다. Retire에 active Actor·User Spot이 있으나 새 owner token을 publish할 수 없는 현재 계약에서는 admission seal 전에 `RELOCATION_FAILED` 또는 `RELOCATION_DISABLED`로 종료해 기존 handoff나 payload parse로 우회하지 않는다. Java core 417/417, Spring starter 33/33, Kotlin compile, Redis provider 18건 중 환경 의존 7건 skip·실패 0이 통과했다. 남은 계약 공백은 `ZLinkAuthorityPut`의 `NEW_OWNER`·`NEW_OBJECT`가 target owner token을 전달하지 못하는 점과 aggregate exact owner lease claim/read·capacity fence다. 최소 수정안은 Put에 transition별로 검증하는 optional target owner를 추가하고 Actor 단위 `NEW_OWNER` CAS에 사용하며, 이미 target owner가 있는 aggregate·reservation은 그대로 유지하는 것이다. 이 계약이 확정되기 전에는 User Spot aggregate capture/restore, target replay-before-commit과 실제 Retire relocation을 연결하지 않는다. Spring construction/start 분리도 남아 있다. 아직 `M6-RUNTIME` 또는 `V11-R5C` 시작 증거가 아니며 Sample·E2E·Core·bindings 변경·실행은 0이다. |
| `V11-M6C-NODE` | Node maintenance·monitoring·NestJS | Node lane, `P-DEEP` | `V11-R5B` | 진행 | Promise·event-loop recovery·terminal observation·NestJS cleanup contract 통과 | first-intent-wins Retire·Shutdown barrier, mutation 전 preflight, ready-first bounded relocation scheduler(기본 outbound 64·in-flight 256 MiB), deadline 뒤 force-stop, terminal observer와 published root data-loss recovery 분류를 Framework-owned runtime에 구현했다. Relocation payload는 application state·accepted journal·미실행 queue·logical timer를 deterministic envelope로 만들고 immutable Store에 먼저 기록한 뒤 checksum·inventory digest를 검증하여 Location authority의 단일 preserve CAS로 공개한다. CAS conflict가 발생하면 proven orphan만 삭제한다. CAS 응답이 유실되면 authority exact read로 publication 성공을 reconcile하고, expected version이 유지된 것이 확인될 때만 orphan을 삭제한다. 결과가 불명확하면 retention이 정리하도록 root를 보존한다. Authority reference 해제 뒤 payload 삭제, published payload missing·checksum·inventory mismatch의 non-rollback `RelocationDataLost`를 구현했다. Owner queue는 active claim이 끝난 turn boundary에서 seal하며 기존 미실행 record를 capture하고 이후 ingress를 별도 hold한다. Abort는 captured→held 순서로 admission을 복원하고 commit은 held record만 relay 대상으로 반환하며 infrastructure queue는 계속 진행한다. Spot timer는 native timeout handle을 저장하지 않고 registration option, schedule cursor와 delivery cursor를 freeze하며 target의 동일 registration에 logical schedule을 복원한다. 기존 NestJS host는 application shutdown에서 30초 bounded RouteMesh drain 뒤 idempotent stop을 수행한다. Public `ZLinkLocationStore`와 `ZLinkRelocationStore` 및 Framework·NestJS의 `addLocationStore`·`addRelocationStore` 등록 표면을 각각 분리했다. Redis 전용 또는 두 Store를 묶는 등록 API는 추가하지 않았다. `ZLinkChannelClient`는 global channel name과 classic channel transport를 사용하고 `ZLinkRouteClient`는 globally unique Mesh channel의 MeshName을 내부에서 결정하도록 exact contract에 맞췄다. Raw-only bindings에서 제거된 `createMeshNode`를 요구하던 stale parity test를 제거했고, M6 runtime protocol graph에서 Bingo sample generator를 실행하던 test를 제외했다. Connector protocol test는 각 instance를 명시적으로 close한다. Candidate 41 files에 대한 `M6-RUNTIME` 7 commands, public declaration 32/32, M6C 10/10, protocol 14/14와 Framework TypeScript compile이 통과했다. 별도 Store public declaration regression 26/26과 Framework·NestJS registration 검증도 통과했다. 증거: `.artifacts/v11/evidence/V11-M6C-NODE/result.json`. Durable coordinator를 Spot·Actor restore owner와 session route replacement, multi-mesh NestJS aggregate drain에 연결하는 작업을 계속한다. Sample·E2E source 변경·실행은 0이다. |
| `V11-R5C` | Maintenance runtime slice 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M6C-CPP`, `V11-M6C-DN`, `V11-M6C-JVM`, `V11-M6C-NODE` | 대기 | lifecycle·authority·handover·recovery·observability·hosting과 실행 격리의 I1·I2·I3 review clean | C++ candidate의 host first-intent barrier·aggregate CAS·STREAM fence 증거와 JVM candidate의 accepted journal, immutable payload publication, Location authority 상속, exact adapter registration/runtime mapping과 shutdown lifecycle 연결을 확인했다. JVM은 기존 network Actor handoff를 제거했지만 `ZLinkAuthorityPut` target owner와 exact owner lease·capacity 계약이 확정되지 않아 active workload Retire를 seal 전에 block한다. 따라서 aggregate capture/restore와 target replay를 연결하기 전에는 review candidate로 승격하지 않는다. 네 언어 M6C candidate와 독립 reviewer가 모두 준비되기 전에는 이 row를 시작하거나 clean으로 판정하지 않는다. |
| `V11-M6-SCAFFOLD-ZERO` | Production placeholder 제거와 runtime 완료 gate | inventory·contract lane, `P-SCAN` | `V11-R5C`, `V11-CA-ONE-WAY-RUNTIME-JOIN` | 대기 | production scaffold branch·`RuntimeNotReady` placeholder·fake data 0, 모든 runtime regression 통과, sample·E2E source 삭제·임시 우회 0 | — |

Node multi-Mesh host drain checkpoint(2026-07-24)에서 공개 per-Mesh `Drain`·`AwaitDrained`의
multi-Mesh 거부 계약은 유지하면서 NestJS shutdown 전용 aggregate coordinator를 연결했다. Host 경로는
모든 Mesh admission을 같은 deadline으로 seal하고 Mesh별 channel weight·resource를 병렬로 정리한다.
Location owner의 Draining publication과 cleanup은 process 공유 resource이므로 각각 한 번만 실행한다. 실패하면
모든 Mesh를 같은 force-stop 결과로 수렴시킨다. Focused drain contract 7/7, M6C 32/32, workspace
typecheck·build와 changed-source ESLint가 통과했다. M6C 재검증 중 발견한 User Spot Pending join의
factory 중복 시작, operation deadline signal을 rollback에 재사용하던 오류, Ready commit provider 예외의
raw 노출도 수정했다. Cleanup은 독립된 bounded deadline을 사용하고 timeout 뒤 unresolved provider Promise를
기다리지 않는다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다. Spot·Actor restore owner,
session route replacement와 실제 relocation completion gap이 남아 있으므로 `V11-M6C-NODE`는 계속 `진행`이다.

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
실행하지 않았다.

Core request dispatcher refresh checkpoint(2026-07-24)에서 dispatcher 설치·종료의 atomic state,
DEALER generic·typed receive queue ownership, 하나의 `RCVTIMEO` deadline, multipart prefix 선검증과
logical send atomicity, readable pipe 재활성화와 source pipe 보존을 보완했다. Candidate
`.artifacts/v11/candidates/V11-M3-CORE-VERIFY-20260724-request-dispatch-v3-195608.json`은 235개 파일과
aggregate `ba5b66c…`를 봉인한다. Normal 80/80과 ASAN 80/80, Core removal 722 records·violations 0,
package tooling self-test와 `ROW-GATE`가 통과했고 Codex `gpt-5.6-sol xhigh`와 Claude Sonnet의 독립
review에서 blocking finding은 0이다. 승인된 candidate를 isolated Release로 다시 만들어
`.artifacts/wsl/install/zlink-core/11.0.0`에 배포했으며 clean C consumer, runtime 11.0.0·SONAME 11,
service header 0을 확인했다. Core provenance SHA-256은 `2313515b…`, runtime SHA-256은 `f5e184af…`다.
같은 provenance로 `Systems.Zlink.11.0.0.nupkg`를 다시 만들고 isolated .NET consumer를 통과시켰다.
증거는 `.artifacts/v11/evidence/V11-M3-CORE-VERIFY/request-dispatch-refresh-result-20260724.json`,
`.artifacts/v11/evidence/V11-R2/request-dispatch-refresh-result-20260724.json`,
`.artifacts/v11/evidence/V11-M3-CORE-PKG/request-dispatch-refresh-build-20260724.json`과
`.artifacts/v11/evidence/V11-M4-BIND-DN/request-dispatch-refresh-consumer-20260724.json`이 소유한다.
Full inventory check의 Framework semantic record 60개 추가·5개 제거 drift는 concurrent M6 구현에서
발생했으며 Core removal scope와 package candidate 밖이므로 M6 join inventory reconcile 입력으로 기록한다.
다른 M6A·public-contract parity 잔여가 있으므로 `V11-M6A-DN` 상태는 계속 `수정 진행`으로 유지한다.

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

ClientServer dual-role runtime checkpoint(2026-07-24)에서 C++·.NET·JVM·Node.js는 같은
ClientServer ChannelName에 Client와 Server를 각각 한 번 등록할 수 있도록 registration을 합쳤다.
같은 역할을 두 번 등록하면 startup configuration error로 실패하고, RouteMesh와 ClientServer가 같은
ChannelName을 사용해도 startup에서 거부한다. 같은 process의 Server는 local 우선이나 제외 없이
Ready·positive weight·non-draining 조건과 remote Server에 사용하는 weight 계산을 그대로 적용한다.
선택된 local Server도 handler를 직접 호출하지 않고 실제 DEALER→ROUTER admission·request/reply 경계를
통과한다. C++ focused 29/29, .NET 신규 focused 4/4, JVM focused 75/75·전체 core unit 462/462·Kotlin
compile, Node focused 30/30·M6A 6/6·TypeScript build·changed-source ESLint와 `git diff --check`가
통과했다. .NET 전체 unit의 기존 문서 7건과 ClientServer liveness 2건, JVM integration compile의 기존
Stream relay 1건과 Fanout publish 4건은 이 변경과 무관한 선행 API drift로 분리했다. 다섯 언어 exact
interface에 정의된 ClientServer monitoring public surface가 runtime에 아직 없어 `ClientAndServer`
계열 aggregate snapshot projection은 후속 M6 monitoring gap으로 유지한다. Core·bindings와 Sample·E2E
source를 변경하거나 실행하지 않았다. 독립 Codex review에서 발견한 C++ keyed action의 state self-reference
cycle을 merged action value snapshot capture로 수정하고, contract trace에 새 callable helper가 들어가지
않도록 제거했다. Clean-first C++ contract headers·target contract·app host·store/location 4/4와
resolver 29/29가 다시 통과했으며 post-fix review는 `APPROVE`로 끝났다. 따라서 네 `V11-M6A-*` 행은
계속 `수정 진행`으로 유지한다.

ClientServer local-only correction checkpoint(2026-07-24)에서 같은 process의 Client와 Server
registration을 Location Store와 독립된 peer source로 고정했다. Framework는 bound local endpoint를 직접
얻지만 handler를 직접 호출하지 않고 기존 DEALER→ROUTER service admission과 transport를 그대로 사용한다.
Location Store도 있으면 local source와 automatic descriptor를 Server RID·lifecycle generation으로 합쳐
ready target 하나만 유지한다. 공통 spec과 다섯 언어 exact interface를 같은 의미로 수정했다. .NET은
local snapshot의 weight·drain revision을 client connection에 즉시 반영하며 local-only request와 weight
`0` contract 2/2가 통과했다. 전체 ClientServer 15/17이며 나머지 2건은 앞서 분리한 liveness timing
baseline이다. JVM은 local bound endpoint를 기존 admission 경로에 연결했고 configuration과 admission
focused suite가 통과했다. Managed production constructor test는 bound endpoint connect, Hello·Admit,
positive weight 선택, weight `0`·Draining 제외를 검증한다. 기존 manual integration test를 유지하면서
local-only positive·weight `0` 시나리오도 별도로 추가했다. Integration source set은 공유 worktree의
선행 SpotManager·Stream relay·Fanout API 전환 23개 compile error 때문에 실행 전에 중단됐다. Node.js는
local source를 항상 시작하고 automatic alias와 physical connection을
합치며 focused 31/31, M6A 6/6, build·typecheck·changed-source ESLint가 통과했다. C++도
`SameProcessClientServerUsesLocalReadyServerWithoutExternalStoreOrManualEndpoint`에서 Location Store와
manual endpoint 없이 local Ready Server를 정상 후보로 선택하고 기존 DEALER→ROUTER 경계로
request/reply하는 것을 검증했다. 이 test를 포함한 resolver 31/31, contract headers와 M6B runtime이
통과했다. Core·bindings와 Sample·E2E source는 변경하거나 실행하지 않았다.

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
| `V11-E2E-SPEC-FINAL` | Amended contract 기준 공통 E2E spec 확정 | E2E·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 대기 | scenario·negative·race·directional matrix와 approved hash·runtime owner 누락 0 | Relocation 용어와 readiness-first 이전, queue·timer 자동 복원, count·callback·byte gate, precommit abort 복구를 `RL-F11~RL-F14`와 `M75~M78`에 추가했다. 이전 review candidate hash는 이 계약 변경으로 효력을 잃었다. Config 1~14의 기존 contract finding과 새 scenario를 수정한 뒤 Codex `gpt-5.6-sol high`와 Claude Sonnet의 최종 독립 review, runtime owner·approved hash를 새로 기록해야 완료로 전환한다. |
| `V11-SAMPLE-SPEC-FINAL` | Amended contract 기준 공통 sample spec 확정 | sample·contract lanes, `P-DEEP` | `V11-M6-SCAFFOLD-ZERO`, `V11-CA-IMPACT` | 대기 | public 사용 흐름·역할·message·marker와 다섯 언어 owner 누락 0 | — |
| `V11-R5D` | Final E2E·sample spec 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex high review lane, `P-HIGH` | `V11-E2E-SPEC-FINAL`, `V11-SAMPLE-SPEC-FINAL` | 대기 | public contract 일치, assertion 약화·coverage 손실·미분류 impact 0, 대체 scenario와 다섯 언어 sample parity의 I1·I2·I3 review clean | E2E pre-final 범위의 두 reviewer round와 post-fix focused review는 완료했다. `V11-E2E-SPEC-FINAL`의 runtime·approved hash와 `V11-SAMPLE-SPEC-FINAL`이 아직 대기이므로 이 combined final gate는 시작하지 않았다. |
| `V11-M6A-E2E` | Topology·liveness E2E 활성화와 gap 해소 | E2E·topology runtime lanes, `P-DELIVERY` | `V11-R5D` | 대기 | Config 3 `PS-F1~F5`, Config 5 `RL-E1~E5`, cross-MeshNode `ToChannel`(`M73`)와 amended placement scenario를 현재 candidate에서 실행, required skip·runtime gap 0 | — |
| `V11-M6B-E2E` | Stateful object E2E 활성화와 gap 해소 | E2E·stateful runtime lanes, `P-DELIVERY` | `V11-M6A-E2E` | 대기 | Spot·Actor·bound STREAM·Instance·remote create·global identity·cross-MeshNode `ToSpot`(`M72`)·stale owner scenario required skip·runtime gap 0 | — |
| `V11-M6C-E2E` | Maintenance·hosting E2E 활성화와 gap 해소 | E2E·maintenance runtime lanes, `P-DELIVERY` | `V11-M6B-E2E` | 대기 | Zero-downtime patch(`M69`), same-version maintenance(`M70`), Shutdown closing(`M71`), no-target blocker(`M74`), aggregate relocation·crash recovery·remote fencing·hosting scenario required skip·runtime gap 0 | — |

`V11-R5D`에서 Codex `gpt-5.6-sol high`와 Claude Sonnet은 같은 candidate를 독립적으로 검토하고 finding을 §18 규칙으로
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
| `V11-R6` | Framework POSD·DDD·제거 범위 독립 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M8-CLEAN-JOIN` | 대기 | domain boundary, dead code·compat helper·build·package 제거 review clean | — |

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
| `V11-R7` | 11.0 최종 통합 review; Claude `claude-sonnet-5` 병렬 reviewer | Codex review lane, `P-DEEP` | `V11-M9-DOCS`, `V11-CA-USER-SPOT-RUNTIME-JOIN`, `V11-CA-ACTOR-CREATE-REVIEW`, `V11-CA-ONE-WAY-RUNTIME-JOIN`, `V11-CA-DEFERRED-JOIN-DRAFT-RETIRE`, `V11-M6-DEFERRED-JOIN-CPP`, `V11-M6-DEFERRED-JOIN-DN`, `V11-M6-DEFERRED-JOIN-JVM`, `V11-M6-DEFERRED-JOIN-NODE`, `V11-M6-OBJECT-CONTEXT-CPP`, `V11-M6-OBJECT-CONTEXT-DN`, `V11-M6-OBJECT-CONTEXT-JVM`, `V11-M6-OBJECT-CONTEXT-NODE`, `V11-M6-MESSAGE-CONTEXT-CPP`, `V11-M6-MESSAGE-CONTEXT-DN`, `V11-M6-MESSAGE-CONTEXT-JVM`, `V11-M6-MESSAGE-CONTEXT-NODE`, `V11-CA-SESSION-BINDING-DRAFT-RETIRE`, `V11-M6-SESSION-BINDING-CPP`, `V11-M6-SESSION-BINDING-DN`, `V11-M6-SESSION-BINDING-JVM`, `V11-M6-SESSION-BINDING-NODE` | 대기 | 전체 review clean 뒤 final raw·E2E·consumer·smoke 재통과 | — |

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
| `V11-E2E-M06` | Instance Spot transfer | global Spot ID, object·authority owner generation과 순차 request 유지 |
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
| `V11-E2E-M17` | Spot Logical Multicast | Draining target과 stale generation 제외, transaction 시작 뒤 target별 결과·monitoring 집계 없음 |
| `V11-E2E-M18` | Remote owner fencing | 지연된 이전 owner message·timer·phase update가 새 owner에 적용되지 않음 |
| `V11-E2E-M19` | SIGTERM integration | hosting lifecycle이 bounded `Shutdown`을 사용하고 `Blocked`를 반환하지 않음 |
| `V11-E2E-M20` | Multi-Mesh host | host barrier 하나로 all-or-none preflight와 seal 수행 |
| `V11-E2E-M21` | Retire blocked then shutdown | 차단된 `Retire` 뒤 `Shutdown`이 shared deadline 계약으로 terminal 완료 |
| `V11-E2E-M22` | Process pause fencing | lease-derived monotonic deadline 뒤 stale admission과 CAS 거부 |
| `V11-E2E-M23` | Prepared·Committed target replacement | Current coordinator fence가 stable transfer ID의 immutable Transfer manifest로 replacement reservation ACK를 얻고 target·attempt generation·reservation을 한 CAS로 교체해 successor 하나로 activation을 수렴함. Transfer root·journal·terminal key는 attempt 교체에도 바뀌지 않음 |
| `V11-E2E-M24` | Close versus transfer | Instance `Close`와 `Prepared` CAS 승자에 맞는 결과, hidden retry 0건 |
| `V11-E2E-M25` | Cold Instance one-way and request | Global Spot ID의 Instance intent가 eligible target을 선택하고 first message·operation identity·reply correlation·deadline을 activation envelope에 포함해 target transport에 제출함. Source claim은 0건이고 target CAS winner가 `Creating` reservation을 만든다. One-way는 envelope outbound admission에서 완료되고 request만 target activation·handler terminal을 기다림 |
| `V11-E2E-M26` | STREAM binding atomicity | bind·rebind·unbind 실패 시 기존 binding 유지와 terminal result 하나 |
| `V11-E2E-M27` | Observer terminal lane | 일반 event admission을 닫은 뒤 final snapshot·terminal event가 한 번 전달됨 |
| `V11-E2E-M28` | Completion reserve saturation | infrastructure queue 포화에서도 accepted request가 terminal 완료됨 |
| `V11-E2E-M29` | Cross-topology ChannelName call | Spot이 다른 RouteMesh·ClientServer Channel을 호출하고 completion은 원래 owner로 복귀 |
| `V11-E2E-M30` | ChannelName collision | 서로 다른 physical topology의 같은 process-local ChannelName이 startup에서 거부됨 |
| `V11-E2E-M31` | Global object identity collision | 서로 다른 initial Mesh intent로 같은 Actor ID·Spot ID를 동시에 create해도 provider 전체에서 하나의 authority와 current owner로 수렴하고 direct messaging과 manager `Find`는 MeshName 없이 같은 object를 반환함 |
| `V11-E2E-M32` | Cross-language authority interop | 한 runtime이 `authority-key-v1`과 authority payload를 기록하고 다른 runtime이 steady·cold activation·maintenance state를 같은 logical object로 resolve·recovery |
| `V11-E2E-M33` | Transfer preflight growth | Preflight 뒤 reversible seal까지 수락된 work를 exact inventory에 포함하고, seal 뒤 final reservation ACK와 모든 `Prepared` CAS를 완료한 뒤에만 `Draining`을 게시해 continuity를 잃지 않음 |
| `V11-E2E-M34` | Transfer payload leased retention | Long capture 중 staged manifest tree를 추적하고 `Captured`·`Prepared` CAS 직전에 모든 component의 remaining lease를 12시간보다 길게 verify·renew하며 partial renew failure는 root를 Location authority에 연결하지 않음. Provider 기준 시각을 orphan TTL 이상 이동하면 current reference는 renew로 유지되고 orphan만 제거됨. Published reference의 payload가 24시간 연속 Store 불가 뒤 영구 유실되면 non-retriable `TransferDataLost`, 진행 중 `Retire`는 detail에 해당 오류를 보존한 `ForceStopped/TransferFailed`로 종료 |
| `V11-E2E-M35` | Actor owner ABA fence | Actor가 A→B→A로 이전된 뒤 최초 A owner의 지연 message·journal·forward record가 새 A owner에 적용되지 않음 |
| `V11-E2E-M36` | Cross-language terminal failure | 모든 stable failure code를 네 runtime 조합으로 reply·completion·relay해 같은 typed terminal result로 변환하고 unknown code는 protocol error로 수렴 |
| `V11-E2E-M37` | Authority generation atomicity | Provider-domain global object·authority-owner·store revision counters와 Missing/Found expectation으로 create·owner change·preserve·delete·재생성을 원자 처리하고 concurrent winner만 값을 소비하며 per-key version·tombstone을 남기지 않음 |
| `V11-E2E-M38` | ClientServer command isolation | ClientServer role·direction allowlist 밖의 RouteMesh·Spot·Actor·transfer·server-originated application command가 handler·authority에 도달하지 않고 offending connection만 protocol error로 종료 |
| `V11-E2E-M39` | Global Actor creation parity | 다섯 public 언어가 global Actor ID·explicit stable type과 optional initial Mesh intent로 create·GetOrCreate를 수행하고 manager `Find`는 existing-only이며 caller가 physical owner를 선택하거나 hidden create를 시작하지 않음 |
| `V11-E2E-M40` | Spot kind atomic collision | 같은 global Spot ID의 Entry·User create와 Instance fluent cold activation이 하나의 authority CAS에서 경쟁해 kind 하나만 성공하고 close 뒤 다른 kind로 재생성해도 object generation을 재사용하지 않음 |
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
| `V11-E2E-M72` | Cross-MeshNode ToSpot | 서로 다른 process·MeshNode에 있는 caller가 MeshName·owner RID·endpoint 없이 global Spot ID만으로 `SendToSpot`·`RequestToSpot`을 호출해 current Ready Spot handler에 정확히 한 번 도달함 |
| `V11-E2E-M73` | Cross-MeshNode ToChannel | 서로 다른 process·MeshNode에 있는 caller가 MeshName·RID·endpoint 없이 unique ChannelName만으로 `SendToChannel`·`RequestToChannel`을 호출해 ready remote member를 선택하고 terminal result를 한 번 반환함 |
| `V11-E2E-M74` | Retire without eligible target | Compatible target이 없으면 `Retire`가 `Draining` publication과 capture 전에 `Blocked/TargetUnavailable` 또는 capability 불일치의 `Blocked/StateIncompatible`로 끝나고 source authority·admission·payload reference를 바꾸지 않음 |
| `V11-E2E-M75` | Readiness-first relocation | Retire notification이 standalone Actor, Instance Spot과 User Spot aggregate queue의 turn boundary에 도달하고, ready unit부터 bounded sliding permit으로 즉시 relocation하며 느린 current turn이 다른 unit을 막지 않음. Permit 전 seal은 0건임 |
| `V11-E2E-M76` | Queue·timer relocation | Current turn 하나만 source에서 완료하고 미실행 message·accepted journal·logical timer registration·pending tick을 immutable relocation payload로 저장함. Target은 application 재등록 없이 timer를 복원하고 frozen queue→seal 중 hold→Ready 이후 message 순서를 보존함 |
| `V11-E2E-M77` | Relocation concurrency gates | 기본 active outbound·inbound 64, Capture·Restore 8, encoded payload in flight 256 MiB를 독립 high-water로 검증하고, byte 한도를 넘는 단일 User Spot aggregate는 다른 payload 단계와 겹치지 않게 단독 실행함 |
| `V11-E2E-M78` | Relocation precommit abort queue recovery | Target reservation·Restore의 commit 전 실패에서 durable abort와 source normalization 뒤 frozen queue·hold queue·timer schedule·operation identity를 source에 같은 순서로 복원하고 target staging과 relocation payload를 정리함 |
| `V11-E2E-M79` | SpotWide Yield double claim | Member Actor A의 job 1이 User Spot gate를 `Yield`한 동안 Actor B, Spot direct handler와 timer는 진행하지만 Actor A job 2는 job 1 continuation이 같은 gate를 다시 얻어 완료할 때까지 시작하지 않음 |
| `V11-E2E-M80` | SpotWide Async·self-request deadlock prevention | Current User Spot gate가 필요한 target을 `Async`로 기다리거나 같은 Actor 자신에게 awaited request를 제출하면 outbound admission 전에 닫힌 오류로 끝나고 inline·reentrant handler, queue mutation과 gate release가 0건임 |
| `V11-E2E-M81` | Yield context allowlist | 같은 request·worker public call type을 `SpotWide` User Spot과 Instance Spot에서는 정상 실행하고, Entry Spot·Entry Actor·`PerActor`·Node·Channel·owner 밖에서는 operation submission 전에 `InvalidConfiguration`으로 완료함 |
| `V11-E2E-M82` | PerActor lane isolation | Actor A의 장시간 `Async`가 Actor B와 서로 다른 timer를 막지 않으며 Actor A의 다음 job과 같은 timer의 다음 tick은 FIFO·non-overlap을 유지하고 Spot direct·lifecycle callback은 하나의 Spot lane에서 순서대로 실행됨 |
| `V11-E2E-M83` | Multi-lane lifecycle barrier | `SpotWide`의 yielded continuation과 `PerActor`의 Actor·Spot·timer lane이 남아 있는 동안 close·snapshot·relocation이 capture를 시작하지 않고, seal 실패·abort 뒤 같은 generation의 application admission을 정확히 복원함 |
| `V11-E2E-M84` | Typed capacity unlimited and validation | Actor total·Spot total·Spot stable type limit의 `0`은 hidden population cap 없이 동작하고 음수는 socket bind 전에 startup 오류이며 Entry Spot은 Spot count에서 제외되지만 Entry Actor는 Actor count에 포함됨 |
| `V11-E2E-M85` | Typed capacity atomic reservation | 여러 process의 concurrent create가 Actor·Spot total과 Spot stable type limit을 넘지 않고 Active+Reserved+Requested로 판정하며 factory 실패·timeout·abort가 exact reservation vector만 해제함 |
| `V11-E2E-M86` | Aggregate relocation capacity vector | User Spot과 member Actor relocation이 target Spot total 1개, Spot stable type 1개와 Actor total N개 slot을 단일 bundle로 all-or-none 예약하고 어느 bucket이든 부족하면 factory·Restore·partial authority mutation 없이 다른 candidate를 선택하거나 `PlacementCapacityExhausted`로 끝남 |
| `V11-E2E-M87` | Capacity descriptor projection | Stale descriptor가 여유를 표시해도 Location Store atomic reservation이 최종 거부하며 runtime은 다른 candidate를 시도하고 monitoring은 Actor total·Spot total·Spot stable type별 active·reserved·limit과 unlimited를 구분함 |
| `V11-E2E-M88` | Entry Spot ID lifecycle | MeshNode와 Entry Spot이 같은 diagnostic prefix와 독립 RFC 4122 UUID v4 lowercase canonical suffix를 사용하고 같은 lifecycle에서는 exact mapping을 유지하며 replacement lifecycle은 새 MeshNode RID와 Entry Spot ID를 게시함 |
| `V11-E2E-M89` | Entry Spot ID conflict and reserved pattern | Entry global identity claim의 active 충돌은 기존 record 변경, 두 번째 UUID 생성과 두 번째 claim 없이 즉시 `RoutingIdConflict`로 startup을 실패하고 caller User·Instance Spot ID의 reserved Entry 형식은 Location Store·factory 호출 전에 `InvalidConfiguration`으로 거부됨 |
| `V11-E2E-M90` | Cross-language execution·capacity parity | 다섯 public 언어의 동일 fixture가 `SpotWide`·`PerActor`, Yield allowlist, typed capacity와 Entry Spot ID lifecycle에서 같은 ordering·terminal·monitoring 결과를 기록함 |
| `V11-E2E-M91` | User Spot automatic UUID collision | User Spot `Create`가 UUID v4 Spot ID의 첫 active authority 충돌에서 기존 authority를 바꾸지 않고 즉시 `RoutingIdConflict`로 끝나며 두 번째 UUID·reservation과 factory 호출을 만들지 않음 |
| `V11-E2E-M92` | Common public weight range and selection | RouteMesh Channel, ClientServer Server와 node placement가 startup·runtime에서 signed `0..10000`, 기본 `100`을 공유하고 `-1`·`10001`을 mutation 전에 거부하며 100:300 장기 비율, weight 0 제외, capacity-first, multicast once와 64-bit 합산을 검증함 |
| `V11-E2E-M93` | SpotId UTF-8 exact string boundary | 1·255-byte Spot ID 성공, 256-byte 선거부, case·Unicode normalization 없이 distinct authority 유지, invalid UTF-8 legacy binary frame의 protocol rejection과 Store·factory side effect 0을 다섯 언어에서 검증함 |

| ID | Liveness scenario | 완료 조건 |
|---|---|---|
| `V11-E2E-L01` | Store-backed peer orderly disconnect | FIN·RST·raw monitor event에서 ready 상태는 intentional delay 없이 즉시 바뀌고 test는 5초 observation budget 안에 결과를 확인 |
| `V11-E2E-L02` | Manual peer asymmetric blackhole | Admission이 initial Ready와 15초 deadline을 시작하고 connection당 outstanding probe 하나를 5초마다 재전송하며, 반대 방향 application traffic이 계속되어도 current matching ACK가 없으면 transport target에서 제외 |
| `V11-E2E-L03` | Store-backed process pause | owner lease TTL과 polling 상한 안에 신규 routing에서 제외 |
| `V11-E2E-L04` | Peer restart | Service admission을 다시 수행하고 store-backed exact owner token 또는 manual CSPRNG lifecycle nonce를 equality로 검증하며 current connection handover 뒤 stale event가 successor를 제거하지 않음. Lifecycle token의 숫자 대소 비교는 사용하지 않음 |
| `V11-E2E-L05` | Liveness cleanup | liveness probe·reconnect timer, monitor handle과 child process가 terminal 뒤 남지 않음 |
| `V11-E2E-L06` | Manual·automatic classic fanout publisher failure | Config 3 `PS-F1~F5`: descriptor·manual endpoint별 publisher 전용 SUB socket, publisher→subscriber periodic one-way exact 2-frame beacon, application filter와 reserved subscription 분리, unrelated-topic traffic 중 false timeout 0, 첫 valid receive 전 ready 0과 application delivery 0. Public exact reserved topic은 거부하고 같은 prefix+추가 byte topic은 허용하며 malformed reserved record는 protocol error와 해당 publisher 즉시 not-ready로 수렴. M5 codec negative fixture와 같은 bytes 사용 |

### 14.2 필수 race·회귀 test

- `V11-RACE-01`: 같은 global Spot ID에 Instance intent를 지정한 100개 caller가 최초 요청해 authority owner와 factory execution 하나로 수렴하는지 확인한다.
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
- `V11-RACE-14`: 같은 global Spot ID에 User Spot create와 Instance fluent cold request를 동시에 제출해
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
- `V11-RACE-39`: `SpotWide` Member Actor의 Yield terminal completion, 다른 Actor·Spot·timer admission과 같은
  Actor의 다음 job을 경쟁시킨다. Actor claim은 continuation 완료까지 하나이며 User Spot gate만 release·reacquire되고
  같은 Actor handler의 overlap·FIFO 역전과 inline self-dispatch가 없는지 확인한다.
- `V11-RACE-40`: `PerActor` Actor·Spot·timer lane admission과 close·snapshot·relocation seal을 경쟁시킨다. Seal 뒤
  새 application admission은 없고 yielded continuation을 포함한 기존 obligation이 모두 안전 경계에 도달한 뒤에만
  capture하며 abort는 같은 generation의 모든 lane을 다시 여는지 확인한다.
- `V11-RACE-41`: Actor·Spot·stable type limit의 마지막 slot에 여러 creation·relocation을 동시에 예약한다. Typed
  vector 전체가 한 transaction에서 성공하거나 실패하고 aggregate의 Spot total 1개·Spot stable type 1개·
  Actor total N개 중 일부만 reserved·active로 남지 않으며 stale lifecycle cleanup이 successor count를
  변경하지 않는지 확인한다.
- `V11-RACE-42`: Entry Spot ID의 첫 identity claim과 descriptor owner replacement를 경쟁시킨다. Exact active
  claim 충돌은 기존 record mutation, 두 번째 UUID 생성과 두 번째 claim 없이 즉시 `RoutingIdConflict`로
  끝나며 이전 lifecycle의 지연 publication·cleanup이 새 lifecycle의 Entry mapping을 제거하거나 재사용하지
  않는지 확인한다.
- `V11-RACE-43`: Unsupported execution context에서 request·worker `Yield`와 cancellation·timeout을 경쟁시킨다.
  Context validation이 항상 operation ID, outbound admission, worker scheduling과 queue mutation보다 먼저이며
  `InvalidConfiguration` terminal이 한 번만 완료되는지 확인한다.
- `V11-RACE-44`: User Spot automatic UUID 생성과 global authority reservation에 active collision을 주입한다.
  첫 collision이 기존 authority를 바꾸지 않고 즉시 terminal이 되며 두 번째 UUID·reservation·factory가
  생성되지 않는지 확인한다.
- `V11-RACE-45`: RouteMesh·ClientServer·placement runtime weight update와 target selection을 경쟁시킨다.
  Descriptor revision보다 먼저 범위 밖 값이 거부되고 이미 submit·reservation된 operation은 이전 선택을
  유지하며, 많은 `10000` member의 합산이 overflow 없이 current positive candidate 집합을 사용하는지 확인한다.

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
   M6 runtime review는 internal regression과 execution quarantine만 재실행한다. `V11-R5D`는 Codex `gpt-5.6-sol high`와
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
- [ ] Final E2E·sample spec이 `V11-R5D`에서 Codex `gpt-5.6-sol high`와 Claude Sonnet의 독립 review와 post-review gate를
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
