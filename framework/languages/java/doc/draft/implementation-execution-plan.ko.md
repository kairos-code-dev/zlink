<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [Java 묶음](./README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

# Draft -- Java/Kotlin Framework Implementation Execution Plan

> 이 문서는 **구현 전 실행 계획 초안**이다.
> 현재 공개 계약이 아니며, [Java/Kotlin Framework Porting Plan](./java-kotlin-framework-porting-plan.ko.md)을
> 실제 코드 작업 순서로 나누기 위한 계획서다.
>
> 목표는 `.NET` framework와 같은 아키텍처, 기능성, 사용성을 가진 Java/Kotlin
> 포팅 버전을 만드는 것이다. 구현 중 문서와 `.NET` 코드가 어긋나면
> `framework/languages/dotnet/src` 코드가 기능의 최종 기준이다.
>
> **이 문서가 phase 순서의 단일 권위(single source of truth)다.** phase 번호와
> 순서가 [Porting Plan](./java-kotlin-framework-porting-plan.ko.md) §4의 단계 번호와
> 어긋나면 **이 문서의 phase 순서를 따른다.** Porting Plan §4는 모듈별 책임
> 설명용이고, 실제 작업 순서·gate·release 기준은 여기에서 확정한다.

## 0. 정규 모듈·패키지 이름표 (Phase 0 DoD 기준)

아래 표가 Gradle artifact 이름과 Java package 이름의 **단일 정규 매핑**이다. 다른
draft 문서(Porting Plan §2, README)에서 이름이 어긋나면 이 표를 따른다. Phase 0 DoD는
이 표대로 module/package 경계가 만들어졌는지 확인한다. binding group은 `systems.zlink`.
Maven package 좌표도 같은 group을 쓴다. 즉 공개 artifact는
`systems.zlink:<Gradle artifact>:<version>` 형식으로 배포한다.

| Gradle artifact | Java package | `.NET` 대응 | 역할 |
|-----------------|--------------|-------------|------|
| `zlink-framework-core` | `systems.zlink.framework` | `Systems.Zlink.Framework` | public contracts, runtime, handler scanner, backend adapter port |
| `zlink-framework-spring-boot-starter` | `systems.zlink.framework.spring` | `Zlink.Framework.AspNetCore` | auto configuration, `SmartLifecycle`, bean discovery, event bridge |
| `zlink-stream-connector` | `systems.zlink.stream.connector` | `Systems.Zlink.Stream.Connector` | client STREAM connector core (transport/heartbeat/reconnect) |
| `zlink-stream-connector-codecs` | `systems.zlink.stream.connector.codecs` | `Systems.Zlink.Stream.Connector.Codecs` | codec contract + auto codec selector (공유) |
| `zlink-stream-connector-json` | `systems.zlink.stream.connector.json` | `Systems.Zlink.Stream.Connector.Json` | JSON codec |
| `zlink-stream-connector-msgpack` | `systems.zlink.stream.connector.msgpack` | `Systems.Zlink.Stream.Connector.MessagePack` | MessagePack codec |
| `zlink-stream-connector-protobuf` | `systems.zlink.stream.connector.protobuf` | `Systems.Zlink.Stream.Connector.Protobuf` | Protobuf codec |
| `zlink-framework-kotlin` | `systems.zlink.framework.kotlin` | (없음) | coroutine/DSL extension. core runtime을 다시 구현하지 않는다 |
| `zlink-framework-testkit` | `systems.zlink.framework.testkit` | `tests/` fixture | in-process test host, fake backend, contract fixture |

공식 release package URL은 `https://maven.pkg.github.com/kairos-code-dev/zlink` 로
고정한다. release CI는 `MAVEN_REPOSITORY_URL` 에 이 값을 넣어 publish한다. Gradle
스크립트는 fork 검증과 로컬 검증을 위해 URL을 환경변수에서 읽지만, 정식 배포 대상은
문서와 CI에서 같은 URL을 사용해야 한다.

배포 repository URL 결정 순서는 아래와 같다.

1. `MAVEN_REPOSITORY_URL` 이 있으면 그 URL을 사용한다. 사내 Nexus, Artifactory,
   GitHub Packages 외부 조직 저장소처럼 명시 대상이 있을 때 이 값을 쓴다. 공식
   release CI에서는 이 값을 `https://maven.pkg.github.com/kairos-code-dev/zlink` 로
   설정한다.
2. `MAVEN_REPOSITORY_URL` 이 없고 `GITHUB_REPOSITORY` 가 있으면
   `https://maven.pkg.github.com/${GITHUB_REPOSITORY}` 를 사용한다. 이것은 fork나 임시
   CI에서 같은 publish task를 검증하기 위한 fallback이다.
3. 둘 다 없으면 `build/repo` 아래 로컬 Maven repository로 publish한다. 로컬 검증에서
   외부 저장소 credentials가 없어도 publish task를 확인하기 위한 fallback이다.

credentials도 같은 원칙을 따른다. `MAVEN_REPOSITORY_USERNAME` /
`MAVEN_REPOSITORY_PASSWORD` 를 우선 사용하고, 없으면 GitHub Actions의
`GITHUB_ACTOR` / `GITHUB_TOKEN` 을 사용한다. 따라서 package URL은 코드나 문서에
고정하지 않고, CI 환경이 배포 대상을 주입한다.

POM의 project URL과 SCM URL은 저장소를 가리키는 안정 메타데이터이므로
`https://github.com/kairos-code-dev/zlink` 를 사용한다. 이것은 artifact를 publish할
repository URL과 다르다. repository URL은 위 환경변수 규칙으로 정하고, POM URL은
사용자가 artifact의 소스 저장소를 찾기 위한 식별자로 유지한다.

`zlink-framework-core` 내부 package(= `.NET` `Contracts/*` + `Runtime/*` 미러):
`systems.zlink.framework.{channels, spots, actors, streams, registry, monitoring,
configuration}`은 public contract 표면이다. internal runtime은
`systems.zlink.framework.runtime` 아래에서 `.NET` `Runtime/*` 카테고리에 맞춰
`runtime.host`, `runtime.configuration`, `runtime.backend`, `runtime.actors`,
`runtime.channels`, `runtime.spots`, `runtime.streams`, `runtime.registry`,
`runtime.monitoring`, `runtime.messaging`처럼
주제별 package로 분리한다. 공통 직렬 실행 primitive는 **`systems.zlink.framework.execution`(serial
execution queue 등 — §2.3 참조)**에 둔다.

`systems.zlink.framework.runtime` 루트 package는 하위 runtime package를 묶는
namespace 역할만 맡긴다. host 조립, option registration, spot runtime, stream runtime은
각각 `runtime.host`, `runtime.configuration`, `runtime.spots`, `runtime.streams`에 둔다.

> 코드 모듈은 `.NET` `Runtime/Codecs/`(framework 내장 `ZLinkCodecRegistryBuilder`)와
> connector codec 모듈을 구분한다. framework 내장 codec registry는
> `zlink-framework-core` 안에 두고, connector용 codec helper는 위의 4개 connector
> codec 모듈로 분리한다.

## 1. 완료 정의 (North Star — 핵심 5축 동등성 + 샘플 게이트)

이 프로젝트는 **`.NET` framework와 아키텍처, 기능, 사용성, 폴더구조, 파일분류가
동등**해질 때 완료다. P0~P11 phase gate를 모두 통과해도 아래 5축 표가 전부
충족되지 않으면 **미완료**다. 샘플은 별도 release gate로 검증하지만, 샘플 gate도
통과해야 최종 완료다. 기준 대상은 `framework/languages/dotnet`.

| 축 | 동등 기준 | 비교 대상(`.NET`) | 확인 방법 |
|----|-----------|-------------------|-----------|
| **아키텍처(architecture)** | `.NET`과 같은 contract/runtime/backend adapter/testkit 분리. backend 어댑터 한 층으로 격리 | `src/`, `Runtime/*`, `tests/` | architecture evidence table + backend-dependency-policy 회귀 |
| **기능(functionality)** | 모든 서브시스템(channel/spot/actor/stream/registry/monitoring/codec) 동작이 `.NET`과 동일 | `Runtime/*`, `tests/` | regression-test-matrix 전 행 green |
| **사용성(usability)** | 같은 멘탈 모델·동사·등록 흐름. `.NET` `doc/guide` 대응 Spring Boot 가이드 제공 | `doc/guide/01~12` | Java 사용자 가이드 동등 챕터 매핑 |
| **폴더구조(folder layout)** | `.NET` `src/`, `Runtime/*`, `Contracts/*`, `samples/*`, `tests/*` 역할이 Java Gradle module/package/folder로 1:1 추적 가능 | `src/`, `samples/`, `tests/` | folder-layout evidence table |
| **파일분류(file classification)** | `.NET` 파일의 역할(contracts, builders, runtime host, channels, spots, actors, streams, registry, monitoring, codecs, samples)이 Java에서 같은 카테고리 folder/package로 묶임. Java는 같은 카테고리의 여러 파일을 folder/package로 묶고 루트 package에 흩뿌리지 않음 | `src/**`, `samples/**` | file-classification evidence table |

추가로 아래를 모두 만족해야 한다.

- Java framework core, Spring Boot starter, Stream Connector(+codec 모듈), Kotlin
  wrapper, testkit이 빌드된다.
- `.NET`의 아키텍처, 기능, 사용성, 폴더구조, 파일분류가 Java/Kotlin에서 evidence
  table로 1:1 추적된다.
- Java는 `.NET`과 같은 카테고리의 파일을 같은 package/folder 아래에 묶는다. 예를
  들어 channels, spots, actors, streams, registry, monitoring, codecs, configuration,
  runtime host, backend adapter, samples/shared 역할은 서로 섞지 않는다.
- framework는 Java binding의 public API만 호출한다.
- `.NET`과 같은 channel, Spot, actor/session, stream, registry, monitoring 의미를
  제공한다.
- `samples/java/*`와 `samples/kotlin/*` 아래의 `TicTacToe`, `Bingo` sample이 실제
  framework/connector public API만 사용해 실행된다.
- [regression-test-matrix](./internals/regression-test-matrix.ko.md)의 release gate가
  통과한다.

### 1.1 cross-language 상호호출 확인 (언어 중립 wire 계약)

기능 축에는 **언어 간 상호호출**까지 포함한다. Java 서비스가 `.NET`/C++/Node 서비스와
**같은 channel/packet** 위에서 상호 호출되는지 확인한다(언어 중립 wire 계약). 최소
한 경로(예: Java client → `.NET` server channel request/reply, 또는 Java server ←
Node connector)를 release 시나리오에 넣는다. 이것이 통과해야 기능 축이
완료로 인정된다.

Phase 10의 최소 release gate는
`JavaNodeStreamInteropTest.nodeConnector_decodesJavaRequestFrame_andJavaDecodesNodeResponse`로
고정한다. Java가 만든 STREAM request frame을 Node connector protocol이 decode하고,
Node가 만든 response frame을 Java가 다시 decode해야 한다. 이 테스트는 sample 목록을
늘리지 않고 언어 중립 STREAM wire 계약을 검증한다.

### 1.2 완료 판정 프로토콜

이 작업은 긴 포팅 작업이므로 한 번에 전체 완료를 선언하지 않는다. 각 phase는
**audit → implementation → verification → review** 순서로 닫는다. 이 네 단계가 모두
끝나지 않으면 다음 phase로 넘어가지 않는다.

이 프로토콜은 **사용자 승인 대기 없이 진행**하는 실행 규칙이다. 작업자는 각 phase의
audit 결과를 남긴 뒤, `부분`, `미완료`, `미검증` 항목을 같은 phase 안에서 바로
구현하고 검증한다. phase gate가 닫히면 작업자가 evidence table을 기준으로
**스스로 리뷰하고 승인**한 뒤, §1.4 조건에 맞춰 커밋·push하고 다음 phase로 진행한다.
사용자 확인을 기다리는 경우는 아래처럼 실제 작업을 계속할 수 없는 때로 한정한다.

- `.NET` 기준 코드와 이 문서의 요구가 서로 충돌하고, 코드만으로 선택할 수 없는 경우
- public API의 의미를 바꾸는 두 개 이상의 대안이 모두 큰 호환성·사용성 영향을 갖는 경우
- 외부 권한, credential, 원격 저장소 상태처럼 작업자가 해결할 수 없는 상태가 필요한 경우
- unrelated dirty change와 필요한 변경이 같은 파일에서 충돌해 임의 병합이 위험한 경우

1. **audit**: `.NET` 대응 코드와 현재 Java/Kotlin 코드를 비교해 완료 조건을 표로
   만든다. 표에는 `.NET` 파일, Java/Kotlin 파일, 테스트 파일, 판정을 모두 적는다.
2. **implementation**: audit에서 `미완료` 또는 `부분`으로 판정된 항목만 구현한다.
   unrelated 변경은 하지 않는다.
3. **verification**: phase gate의 test와 연결 회귀를 실행한다. 실행하지 못한 검증은
   `미검증`으로 남기고 완료로 세지 않는다.
4. **review**: no-op, fake, sample 우회, public API 누수, POSD 위험 신호가 남아
   있는지 다시 검색한다. 하나라도 남으면 phase는 닫히지 않는다.
5. **self-approval**: 작업자가 phase evidence table, gate 결과, 완료 금지 패턴 검색,
   POSD 리뷰 결과를 근거로 phase 닫힘 여부를 직접 판정한다. 모든 항목이 `완료`이고
   검증 결과가 green일 때만 phase를 승인한다. 사용자 승인 대기를 phase 닫힘 조건으로
   넣지 않는다.

각 phase가 닫힐 때 아래 형식의 **phase evidence table**을 남긴다. 코드 리뷰와
커밋 메시지는 이 표를 기준으로 작성한다.

| 항목 | `.NET` 기준 | Java/Kotlin 구현 | 검증 | 판정 |
|------|-------------|------------------|------|------|
| 예: channel request/reply | `...` 파일/테스트 | `...` 파일/테스트 | 실행한 명령 | 완료/부분/미완료/미검증 |

판정 규칙은 아래와 같다.

- **완료**: `.NET` 대응 기능이 Java/Kotlin에 있고, public API 경로로 실행되며,
  자동 검증이 통과한다.
- **부분**: compile은 되지만 기능 경로나 검증 일부가 빠져 있다.
- **미완료**: public 표면만 있거나 runtime 배선이 없다.
- **미검증**: 구현은 있어 보이지만 test 또는 sample 실행 증거가 없다.

`부분`, `미완료`, `미검증`이 하나라도 있으면 해당 phase는 완료가 아니다.

### 1.3 완료 금지 패턴

아래 패턴은 build가 성공해도 완료로 인정하지 않는다. 발견되면 해당 phase의
verification은 실패로 처리하고 먼저 제거한다.

- public 설정 경로에 남은 `Noop*` builder 또는 상태를 바꾸지 않는 builder method
- sample 통과만을 위한 `Recording*`, `Fake*`, `InMemory*` runtime 우회
  (`testkit`과 명시적 unit test fixture는 제외)
- client나 sample이 framework/connector public API를 거치지 않고 domain object,
  `Catalog`, `Spot`, actor instance를 직접 호출하는 코드
- `UnsupportedOperationException("not needed by sample")`처럼 sample 경로가
  실제 runtime 기능을 생략했음을 숨기는 코드
- readiness 문제를 가리는 sleep, polling delay, 임시 metadata/route store
- framework가 Java binding internal/private member를 reflection으로 호출하는 코드
- Java public API가 제공하는 `await(...)` helper를 쓰지 않고 sample에서
  `toCompletableFuture().join()`을 직접 반복하는 코드
- Kotlin wrapper가 Java runtime과 다른 lifecycle, ordering, error 의미를 만드는 코드

단, `zlink-framework-testkit` 안의 fake backend와 fixture는 허용한다. testkit fixture는
sample이나 production runtime에서 import할 수 없도록 forbidden dependency test로
막는다.

### 1.4 커밋과 push 조건

커밋과 push는 Phase 11까지 끝난 뒤 한 번에 하지 않는다. 각 phase를 닫을 때마다
작고 검증 가능한 단위로 커밋할 수 있지만, 아래 조건을 모두 만족해야 한다.

- 해당 phase evidence table의 모든 항목이 `완료`다.
- phase gate 명령과 결과가 기록되어 있다.
- §1.3 완료 금지 패턴 검색이 통과한다.
- unrelated dirty change가 staged 되지 않았다.
- sample 또는 public API를 바꿨으면 대응 문서와 regression matrix가 함께 갱신됐다.

위 조건을 만족하지 못한 상태에서 커밋하거나 push하지 않는다. 이미 push한 변경에서
완료 금지 패턴이 발견되면, 다음 작업은 새 기능 추가가 아니라 해당 커밋의 교정 또는
revert 여부 판단부터 시작한다.

## 2. 작업 원칙

- 한 단계의 public API를 먼저 compile 가능한 contract로 닫고 runtime을 붙인다.
- backend concrete type은 public API로 내보내지 않는다.
- Java binding에 필요한 기능이 없으면 binding public API를 추가한다.
- compatibility shim은 만들지 않는다.
- sample 통과를 위해 sleep, in-memory route store, metadata store 같은 우회를 만들지
  않는다.
- Kotlin은 Java runtime 위의 thin wrapper로만 구현한다.
- Kotlin `suspend fun` annotation handler는 thin wrapper 원칙의 예외가 아니다. Spring
  scanner가 Kotlin suspend method를 발견하고 framework가 소유하는 coroutine adapter로 Java
  `CompletionStage` handler에 연결해야 하며, 이 경로가 없으면 Kotlin 지원은 부분
  완료다.
- 각 phase는 §1.2의 audit, implementation, verification, review 순서로만 닫는다.
- §1.3 완료 금지 패턴이 남아 있으면 build 성공 여부와 관계없이 미완료다.

## 2.1 POSD 기반 리팩토링 절차

각 phase는 구현 직후 바로 gate로 넘어가지 않는다. 먼저 POSD 기준으로 아래 절차를
수행한다.

1. 해당 phase 코드에서 위험 신호를 열거한다.
2. 각 위험 신호가 어떤 원칙을 어기는지 적는다.
3. 비자명한 구조 결정은 두 가지 이상 대안을 비교한다.
4. 호출자 관점에서 public API가 더 단순해졌는지 확인한다.
5. 리팩토링 뒤 위험 신호가 실제로 사라졌는지 다시 점검한다.
6. **POSD 리팩토링 후 phase gate(DoD·검증·연결 회귀)를 다시 실행해 회귀가 없는지
   확인한다.** 리팩토링은 내부 구조 개선만 하고 public 계약을 바꾸지 않는다.
   계약을 바꿔야 하면 별도 작업으로 분리하고 spec/handler-interfaces를 먼저 고친다.
   gate가 다시 green이고 위험 신호가 0일 때만 다음 phase로 전진한다.

아래 패턴은 모든 phase에서 우선 제거한다.

- shallow module: public method가 내부 구현 복잡도만큼 복잡하다.
- pass-through method: 아무 의미 없이 인자를 그대로 넘긴다.
- information leakage: backend concrete type, native handle, protocol detail이 public
  surface로 새어 나온다.
- temporal decomposition: 실행 순서만 기준으로 class가 나뉘어 호출자가 순서를 외워야
  한다.
- mixed policy/mechanism: validation policy, dispatch policy, backend I/O가 한 class에
  섞인다.
- speculative API: sample이나 `.NET` 동등성에 필요하지 않은 public API가 생긴다.

## 2.2 Phase별 POSD 리팩토링 기준

| Phase | 중점 위험 신호 | 리팩토링 방향 | Gate에 추가되는 확인 |
|-------|----------------|---------------|----------------------|
| Phase 0 | build 구조가 package 의미를 숨김 | source set과 package를 contract/runtime/testkit으로 분리 | module boundary가 설명 가능 |
| Phase 1 | builder와 runtime이 한 class에 섞임 | public contract, validation, runtime state를 분리 | public API가 backend를 모름 |
| Phase 1.5 | binding gap을 framework 우회 코드로 메움 | binding public API를 추가하고 adapter에서 호출 | reflection/internal 접근 없음 |
| Phase 2 | channel type별 중복 submit/correlation | submit operation과 channel transport를 분리 | caller API는 fluent call 하나 |
| Phase 3 | Spring context를 service locator로 사용 | constructor injection과 conditional bean 정책 사용 | context가 DI container를 노출하지 않음 |
| Phase 4 | registry query와 discovery hot path 혼합 | topology query와 runtime discovery view를 분리 | request hot path가 query client를 모름 |
| Phase 5 | Spot, actor, timer, route 정책 혼합 | Spot lifecycle, timer, route egress, monitoring을 내부 모듈로 분리 | Spot public surface가 type/rid 중심 |
| Phase 6 | actor state와 dispatch queue 결합 | actor identity/state, mailbox, Spot location resolver를 분리 | actor 이동 후 dispatch 위치가 명확 |
| Phase 7 | session relay를 route packet으로 흉내 냄 | ActorGateway와 session context를 깊은 모듈로 유지 | application route store 없음 |
| Phase 8 | connector callback, transport, codec 혼합 | transport, frame codec, pending request, dispatch queue를 분리 | connector core가 server framework를 모름 |
| Phase 9 | Kotlin wrapper가 새 의미를 만듦, Kotlin suspend annotation handler가 수동 adapter에만 머묾 | Java API를 호출하는 extension으로 제한하고, Spring scanner에서 발견한 suspend method를 framework가 소유하는 coroutine adapter로만 실행 | Java와 validation/lifecycle 의미 동일, Kotlin suspend annotation handler가 Java annotation handler와 같은 duplicate validation과 dispatch gate 통과 |
| Phase 10 | sample이 readiness 우회를 숨김 | 실제 public API와 observable readiness로 self-check | sleep 기반 masking 없음 |
| Phase 11 | guide/spec/internals 내용 혼합 | 독자별 문서 책임을 분리 | 구현된 계약만 spec으로 승격 |

## 2.3 Serial execution queue (`systems.zlink.framework.execution`)

`.NET` `Runtime/Execution/`(`ZLinkSerialExecutionQueue`, `ZLinkSerialWorkItem`,
`ZLinkRuntimeTaskRunner`, `ZLinkBoundedTaskSet`, `ZLinkPollingBackoff`,
`ZLinkSortedConnectionSet`)에 대응하는 **named module/package**다. node plan §3
`runtime/execution/`과 같은 위치다.

이것은 Phase 5의 **명시적 산출물**이다. Spot의 단일 실행 컨텍스트(모든
packet/timer/subscription/channel-reply continuation 직렬화)가 이 serial execution
queue 위에서 동작한다. **Phase 6(actor mailbox dispatch)과 Phase 7(session serial
dispatch queue)은 이 모듈을 다시 만들지 않고 재사용한다.** Phase 6/7에서 별도
직렬화 primitive를 새로 만드는 것은 변경 증폭이므로 금지한다.

## 2.4 작업 순서 + 의존 그래프 (DAG)

아래가 임계 경로와 병렬 가능 분기를 보여 준다. `→`는 선행 의존, 같은 들여쓰기
형제는 병행 가능을 뜻한다.

```
P0 빌드 골격 (정규 모듈/패키지 표)
 └─ P1 contract + backend adapter port  ★유일한 backend 스왑 지점
     └─ P1.5 Java binding parity
         └─ P2 channel messaging  ← 수직 슬라이스 1 (여기서 dispatch 패턴 확정)
             └─ P3 Spring Boot starter (host/lifecycle/SmartLifecycle)
                 ├─ P4 registry + base monitoring ─┐ (spot 무관 부분은 병렬)
                 │                                  │
                 └─ P5 Spot runtime (serial execution queue 산출)
                     ├─ P6 actor core ──────────────┘ (actor가 registry resolver 사용)
                     │   └─ P7 STREAM session + session relay
                     │       └─ P8 stream connector (+ codec 모듈)
                     └─ (P4의 spot snapshot monitoring source는 P5 이후에만 닫힘)
         P9 Kotlin wrapper  (P2~P8의 Java 표면이 닫히는 대로 점진 가능)
         P10 samples + release gate (P2~P9 필요)
         P11 documentation promotion (P10 이후)
```

임계 경로: **P0 → P1 → P1.5 → P2 → P3 → P5 → P6 → P7 → P8 → P10 → P11**.

병렬 분기:

- **P4 registry/monitoring**은 P3 이후 P5(spot)과 **병행** 가능하다. 단
  spot snapshot monitoring source는 P5가 끝나야 닫힌다.
- **P9 Kotlin wrapper**는 각 Java 표면이 확정되는 즉시 부분적으로 진행할 수 있다.
- P6는 P5(serial execution queue)에, P7은 P6(actor relay)에 의존하므로 직렬이다.

## 3. Phase 0 -- 기준 고정과 빌드 골격

### 산출물

- Gradle 또는 Maven multi-module 골격
- package/module name 확정
- public API source set과 internal runtime source set 분리
- test source set 분리: unit, contract, fake backend, integration, sample

### 작업

1. Java framework module 구조를 **§0 정규 모듈·패키지 이름표 그대로** 만든다.
   (Gradle artifact ↔ Java package 매핑은 그 표가 유일 기준이다.)
2. package naming을 [표면 매핑 정책](./internals/dotnet-to-java-surface-mapping.ko.md)과
   §0 표에 맞춘다.
3. API compatibility check 또는 public surface dump test를 준비한다.
4. Java binding public API gap 후보를 목록화한다.
5. **Kotlin/JVM toolchain을 고정한다**: Kotlin `2.1.0`, JVM toolchain target을 build
   skeleton에 핀으로 박는다(`:kotlin` 서브모듈 포함).

### Gate

- 빈 module build 성공 (§0 표의 9개 module이 모두 빈 빌드 통과)
- public/internal package 경계가 §0 표와 일치
- forbidden dependency test 초안 추가
- **Kotlin 2.1.0 / JVM toolchain이 build skeleton에 핀으로 고정**되어 있음
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §1
  (테스트 계층)의 source set 분리 기준과 일치
- Phase 0 POSD 리팩토링 체크 통과

## 4. Phase 1 -- Contract와 backend adapter

### 산출물

- `ZLinkFrameworkOptions`
- channel, spot, stream, registry, monitoring builder
- `ZLinkBackendAdapterFactory`
- backend adapter port interface
- JSON 기본 codec과 codec registry
- error/exception hierarchy

### 작업

1. [handler-interfaces](./handler-interfaces.ko.md)의 public type을 compile 가능한
   Java 코드로 옮긴다.
2. backend adapter interface를 만든다.
3. Java binding wrapper 구현을 adapter 내부로 제한한다.
4. builder validation framework를 만든다.
5. fake backend를 testkit에 만든다.

### Gate

- contract compile
- public API에 binding concrete type 없음
- **adapter 경계 허용 primitive만 교차**: backend adapter port를 가로질러 나갈 수
  있는 타입은 `RoutingId`, `Message`, `SendFlags`로 한정한다. native binding
  handle(socket/context/spotNode 등 concrete type)은 adapter 밖으로 새지 않는다.
- fake backend로 context/socket/registry/spot/stream wrapper 생성 test 통과
- reflection으로 non-public binding member를 호출하지 않는 test 통과
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md)의
  backend isolation 항목(public surface에 binding concrete type 없음) 미러
- Phase 1 POSD 리팩토링 체크 통과

## 5. Phase 1.5 -- Java binding parity

### 산출물

- Java binding public API gap list
- framework에 필요한 binding public API
- binding smoke test
- framework adapter에서 호출할 public wrapper

### 작업

1. channel, registry, monitoring, spot, stream, ActorGateway, bound session에 필요한
   Java binding 기능을 `.NET` binding/framework 사용 경로와 대조한다.
2. binding에 없는 기능은 Java binding public API로 추가한다.
3. framework에서 binding internal이나 reflection을 호출하지 않도록 adapter 호출
   경로를 고정한다.
4. binding public API 추가분의 unit/smoke test를 만든다.

### Gate

- framework Phase 2~8에서 필요한 binding public API gap이 닫혀 있다.
- Java binding public API 추가분이 compile/test를 통과한다.
- framework adapter가 binding public API만 호출한다.
- stale native artifact로 인한 smoke 실패를 막기 위해 실제 Java binding runtime
  산출물이 source보다 최신인지 확인한다.
- Phase 1.5 POSD 리팩토링 체크 통과

## 6. Phase 2 -- Channel Messaging

### 산출물

- `ZLinkClient`
- `ZLinkFanoutClient`
- `ZLinkRouteClient`
- channel runtime manager
- handler scanner와 annotation dispatcher
- async submit queue와 reply correlation

### 작업

1. client/server channel을 먼저 구현한다.
2. send/request fluent call builder를 구현한다.
3. fanout publisher/subscriber를 구현한다.
4. route mesh channel을 구현한다.
5. handler group, interface handler, annotation handler 등록을 연결한다.
6. 역할별 discovery/manual connection validation을 넣는다.

### Gate

- duplicate channel/handler validation test 통과
- manual client/server request smoke 통과
- discovery client/server request smoke 통과
- fanout publish/subscribe smoke 통과
- route mesh request smoke 통과
- pending request cleanup test 통과
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §2
  (Channel regression) 전 행 미러
- Phase 2 POSD 리팩토링 체크 통과

## 7. Phase 3 -- Spring Boot starter

### 산출물

- `@EnableZLinkFramework`
- Spring auto configuration
- `ZLinkFrameworkConfigurer`
- conditional bean registration
- lifecycle runner

### 작업

1. Spring bean scanner와 handler registration을 연결한다.
2. **`SmartLifecycle` 기반** start/stop을 구현한다. runtime 시동/종료는
   `SmartLifecycle`로 한다(역순 종료를 만족). `ApplicationRunner`는 one-shot
   readiness 신호 용도로만 쓰고 runtime lifecycle에는 쓰지 않는다.
3. [DI 역할 policy](./internals/di-역할-exposure-policy.ko.md)에 맞춰 bean
   노출 조건을 구현한다.
4. Spring `ApplicationEventPublisher` monitoring bridge를 선택 기능으로 둔다.

### Gate

- handler constructor injection smoke 통과
- SpotNode 없는 구성에서 Spot/Actor bean 미등록 확인
- multi-target client의 missing channel configuration error 확인
- Spring context start/stop smoke 통과 (`SmartLifecycle` 역순 종료 확인)
- **연결 회귀**: [lifecycle-and-failure-semantics](./internals/lifecycle-and-failure-semantics.ko.md)
  의 시동/종료 순서 +
  [regression-test-matrix](./internals/regression-test-matrix.ko.md) §1의
  integration host 항목 미러
- Phase 3 POSD 리팩토링 체크 통과

## 8. Phase 4 -- Registry와 base Monitoring

### 산출물

- embedded registry lifecycle
- `ZLinkRegistryQuery`
- `ZLinkRegistryQueryClient`
- `ZLinkRuntimeEventHandler<T>`
- socket/discovery/registry event mapper
- monitoring polling runner

### 작업

1. embedded registry와 framework runtime lifecycle 순서를 맞춘다.
2. registry query와 remote query client를 분리한다.
3. monitoring source validation을 구현한다.
4. registry snapshot diff event를 구현한다.
5. handler failure policy를 구현한다.

### Gate

- registry endpoint 누락 validation test 통과
- embedded registry query smoke 통과
- remote registry query smoke 통과
- monitoring source mismatch validation test 통과
- socket/discovery/registry typed event smoke 통과
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §5
  (Registry/Monitoring regression) 행 미러 (단 spot snapshot event는 P5 이후 채움)
- Phase 4 POSD 리팩토링 체크 통과

## 9. Phase 5 -- Spot runtime

### 산출물

- **serial execution queue (`systems.zlink.framework.execution`)** — §2.3. `.NET`
  `Runtime/Execution/`(`ZLinkSerialExecutionQueue` 등) 미러. Spot 단일 실행
  컨텍스트의 기반이며 Phase 6/7이 **재사용**한다(다시 만들지 않는다).
- Spot mesh와 SpotNode runtime
- Entry Spot registry
- user Spot factory
- `ZLinkSpotManager`
- `ZLinkSpotClient` (`IZLinkSpotOutbound` 대응 — outbound 표면)
- `ZLinkSpotPublisherClient`
- Spot timer
- route egress와 route acceptance (`acceptSpotRoutesFromChannel`)
- spot snapshot monitoring event

### 작업

1. local-only SpotNode부터 구현한다.
2. Spot create/getOrCreate/get/list/remove를 구현한다.
3. Entry Spot과 user Spot factory 등록을 구현한다.
4. Spot packet, subscribe, timer descriptor를 구현한다.
5. route mesh 기반 Spot egress를 구현한다.
6. registry-backed Spot remote address resolver를 구현한다.
7. spot snapshot diff monitoring event를 구현한다.
8. timer exception monitoring event를 구현한다.

### Gate

- duplicate Spot factory/Entry Spot validation 통과
- local Spot create/get/list/close smoke 통과
- timer policy test 통과
- spot snapshot typed event smoke 통과
- timer exception monitoring test 통과
- route egress smoke 통과
- registry-backed resolver validation 통과
- **handler/timer가 동일 serial execution queue에서 직렬 실행**(상태 보호) 확인
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §3
  (Spot/Actor regression)의 Spot 행 + §5의 spot snapshot event 행 미러
- Phase 5 POSD 리팩토링 체크 통과

## 10. Phase 6 -- Actor core

### 산출물

- `ZLinkActor`
- `ZLinkActorFactory`
- `ZLinkActorManager`
- actor context
- Entry Spot actor join
- actor mailbox dispatch

### 작업

1. actor factory와 actor manager를 구현한다.
2. actor create/getOrCreate/find semantics를 `.NET`과 맞춘다.
3. Entry Spot actor join과 user Spot 이동을 구현한다.
4. actor mailbox dispatch와 Spot 위치 이동 후 dispatch 위치를 구현한다.
5. actor type mismatch와 duplicate actor create failure를 구현한다.

### Gate

- actor type mismatch test 통과
- actor join dispatch ordering test 통과
- actor duplicate create/getOrCreate type mismatch test 통과
- actor spot 이동 직후 dispatch 위치 test 통과
- **actor mailbox가 Phase 5 serial execution queue를 재사용**(별도 직렬화 primitive
  신설 없음) 확인
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §3
  (Spot/Actor regression)의 actor 행 미러
- Phase 6 POSD 리팩토링 체크 통과

## 11. Phase 7 -- STREAM server session and session relay

### 산출물

- stream node runtime
- header 기반 `ZLinkSession`
- `ZLinkSessionContext`
- `ZLinkSessionClient`
- `ZLinkSessionActors`
- session serial dispatch queue (Phase 5 serial execution queue 재사용 — §2.3)
- borrowed payload policy
- ActorGateway attach
- `ZLinkBoundSession`
- binding token guard

### 작업

1. stream node bind와 session registration을 구현한다.
2. 한 stream node에 session type 하나만 허용한다.
3. connected/disconnected/error/dispatch lifecycle을 구현한다.
4. session client send/reply를 구현한다.
5. session actor bind/find/relay를 구현한다.
6. payload lifetime policy를 test로 고정한다.
7. ActorGateway relay bridge를 구현한다.
8. bound session send/disconnect를 구현한다.
9. stale binding token guard를 구현한다.

### Gate

- duplicate session registration validation 통과
- session connected/dispatch/reply smoke 통과
- same session serial dispatch test 통과
- borrowed payload contract test 통과
- session close/disconnect callback test 통과
- local session actor relay smoke 통과
- remote ActorGateway relay smoke 통과
- stale binding token guard test 통과
- bound session push/disconnect smoke 통과
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §4
  (STREAM/Connector regression)의 session/relay 행 미러
- Phase 7 POSD 리팩토링 체크 통과

## 12. Phase 8 -- Stream Connector

### 산출물

- `zlink-stream-connector` (connector core)
- transport inference: TCP, TLS, WS, WSS
- frame codec
- heartbeat
- reconnect
- manual/immediate dispatch
- typed handler registry
- request pending tracker
- **분리된 codec 모듈** (`.NET` `.Codecs/.Json/.MessagePack/.Protobuf` 미러 — §0 표):
  - `zlink-stream-connector-codecs` — codec contract + auto codec selector (공유)
  - `zlink-stream-connector-json` / `-msgpack` / `-protobuf` — 각 codec 구현
  - connector core는 codec contract(`-codecs`)만 의존하고 구체 codec 모듈은
    선택적으로 얹는다. 단일 connector 모듈에 codec을 합치지 않는다.

### 작업

1. connector core를 server framework와 분리한다.
2. connect/close/dispatch lifecycle을 구현한다.
3. send/request builder와 callback request를 구현한다.
4. heartbeat와 reconnect state machine을 구현한다.
5. typed codec helper를 구현한다.

### Gate

- transport scheme mismatch validation 통과
- header encode/decode roundtrip 통과
- manual dispatch callback test 통과
- request timeout pending cleanup 통과
- reconnect backoff/max attempts test 통과
- JSON/MessagePack/Protobuf typed helper smoke 통과 (각 codec 모듈 분리 빌드)
- connector core가 구체 codec 모듈에 compile 의존하지 않음 확인
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §4
  (STREAM/Connector regression)의 connector/codec 행 미러
- Phase 8 POSD 리팩토링 체크 통과

### Phase 8 audit evidence

아래 표는 Phase 8 진행 중 현재 코드와 `.NET` 기준을 다시 대조한 결과다. 이 표가
모두 `완료`가 되기 전에는 Phase 8을 자체 승인하지 않는다.

| 항목 | `.NET` 기준 | Java/Kotlin 구현 | 검증 | 판정 |
|------|-------------|------------------|------|------|
| TCP frame transport | `ZlinkStreamTransportFactory.ConnectStreamAsync`, `ZlinkStreamFrameSender`, `ZlinkStreamReceiveLoop`, `TransportTests.TcpSendUsesHeaderPayloadFrame` | `DefaultZLinkStreamConnector`가 `AsynchronousSocketChannel`로 `tcp://` endpoint에 연결하고 STREAM frame prefix/header/payload를 실제 socket에 쓰고 읽음 | `gradle :zlink-stream-connector:test --rerun-tasks` (`ZLinkStreamConnectorTest.requestWritesFrameAndCorrelatesResponse`, `ConnectorDispatchTest.dispatch_invokesCallback`) | 완료 |
| request pending correlation | `ZlinkStreamPendingRequests`, `TypedRequestTests.TcpTypedRequestCorrelatesResponse` | request sequence를 생성해 pending map에 등록하고 같은 sequence의 RESPONSE frame으로 `CompletionStage`를 완료함 | `gradle :zlink-stream-connector:test --rerun-tasks` (`requestWritesFrameAndCorrelatesResponse`) | 완료 |
| request timeout cleanup | `ZlinkStreamPendingRequests.WaitAsync`, `ErrorAndModeTests.RequestTimeoutRemovesPendingRequest` | response가 없으면 scheduled timeout으로 pending request를 제거하고 future를 실패시킴 | `gradle :zlink-stream-connector:test --rerun-tasks` (`requestTimeoutFailsPendingRequestsWithTimeoutCause`, `requestWithoutReplyFailsWithTimeoutCause`) | 완료 |
| manual dispatch over transport | `ZlinkStreamConnectorCallbacks`, `DispatchTests.ManualDispatchQueuesCallbackUntilDispatch` | inbound SEND frame을 manual queue에 넣고 `dispatch().submit()`에서 등록 handler를 실행함 | `gradle :zlink-stream-connector:test --rerun-tasks` (`dispatch_invokesCallback`) | 완료 |
| TLS transport | `ZlinkStreamTransportFactory.ConnectStreamAsync` TLS branch, `TransportTests.TlsSendWorksWithSkippedCertificateValidation` | `tls://` endpoint는 Netty `SslHandler` 기반 비동기 transport로 연결하고, `.NET`의 `SkipServerCertificateValidation`에 대응하는 `skipServerCertificateValidation` 옵션으로 self-signed test endpoint를 통과함 | `timeout 180s gradle :zlink-stream-connector:test --rerun-tasks` (`tlsRequestUsesEncryptedFrameAndSkippedCertificateValidation`, `skipServerCertificateValidationDefaultsToFalseAndCanBeEnabled`) | 완료 |
| WebSocket transport | `ZlinkStreamTransportFactory.ConnectWebSocketAsync`, `TransportTests.WebSocketSendUsesBinaryFrames` | `ws://`와 `wss://` endpoint 모두 Java `HttpClient` WebSocket async API로 연결하고 STREAM frame을 binary message로 송수신한다. `wss://`는 `skipServerCertificateValidation`용 SSL context로 self-signed test endpoint를 통과함 | `timeout 180s gradle :zlink-stream-connector:test --rerun-tasks` (`webSocketRequestUsesBinaryFrameAndCorrelatesResponse`, `wssRequestUsesBinaryFrameAndSkippedCertificateValidation`, `skipServerCertificateValidationDefaultsToFalseAndCanBeEnabled`) | 완료 |
| heartbeat control packets | `HeartbeatSendsReservedControlPing`, `InboundHeartbeatPingReceivesPongWhenHeartbeatDisabled`, `HeartbeatTimeoutFailsPendingRequestsWithTimeoutCause` | heartbeat option, ping/pong control frame, heartbeat timeout pending failure를 connector lifecycle에 연결함 | `timeout 180s gradle :zlink-stream-connector:test --rerun-tasks` (`heartbeatSendsReservedControlPing`, `inboundHeartbeatPingReceivesPongWhenHeartbeatDisabled`, `heartbeatTimeoutFailsPendingRequestsWithTimeoutCause`) | 완료 |
| reconnect backoff/max attempts | `ZlinkStreamConnectorLifecycle`, `ReconnectRestoresConnectionAfterTransportClose`, `ConnectWhileReconnectingFailsWhenClosed` | `reconnect().submit()`이 initial delay, backoff factor, max delay, max attempts를 사용해 재시도하고 실패 시 DISCONNECTED로 전환한다. `.NET`의 `MaxAttempts = null`은 Java에서 `UNLIMITED_RECONNECT_ATTEMPTS`로 표현하고, enabled 상태의 zero attempts는 잘못된 설정으로 거부한다. transport read failure도 reconnect 경로로 진입하고, reconnect 지연 중 close가 들어오면 CLOSED 상태를 유지한다 | `timeout 180s gradle :zlink-stream-connector:test --rerun-tasks` (`reconnectRestoresConnectionAfterTransportClose`, `reconnectFailsAfterMaxAttemptsWhenEndpointUnavailable`, `reconnectUnlimitedAttemptsRestoresLateServer`, `reconnectEnabledRejectsZeroMaxAttempts`, `closeWhileReconnectingKeepsConnectorClosed`) | 완료 |
| callback request API | `IZlinkStreamConnectorInternal.RequestEncoded(... callback ...)` | Java public API는 `CompletionStage` submit 표면을 기본으로 제공하고, 같은 call builder에 blocking `await(...)` helper도 둔다. callback request helper는 Java/Kotlin async 정책상 별도 public helper로 추가하지 않고 `CompletionStage.whenComplete(...)`와 Kotlin `await()`를 표준 사용성으로 둔다 | `timeout 300s gradle check`, `timeout 300s ./samples/run_samples.sh` | 완료 |
| codec module separation | `.Codecs`, `.Json`, `.MessagePack`, `.Protobuf` | connector core와 JSON/MessagePack/Protobuf helper 모듈이 분리되어 있고, JSON typed helper는 실제 TCP connector `send/request/on` 표면 위에서 검증됨 | `timeout 120s gradle :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.ConnectorCodecContractTest --rerun-tasks`, `timeout 300s gradle check` | 완료 |
| POSD connector responsibility split | `Runtime/Transport/*`, pending request, callback dispatch, codec 책임 분리 | TCP/TLS/WebSocket transport는 `ZLinkStreamTransportConnection` 구현으로 분리하고, request timeout/correlation은 `ZLinkStreamPendingRequests`, manual dispatch queue는 `ZLinkStreamDispatchQueue`가 맡는다. connector public API는 transport 종류나 pending map을 노출하지 않음 | `timeout 180s gradle :zlink-stream-connector:test --rerun-tasks`, 완료 금지 패턴 검색 | 완료 |

## 13. Phase 9 -- Kotlin wrapper

### 산출물

- `zlink-framework-kotlin`
- coroutine extensions
- DSL builder
- connector `Flow` wrapper
- Kotlin annotation handler adapter
- Kotlin sample snippets

### 작업

1. Java binding/framework `CompletionStage`를 `suspend`로 감싼다.
2. framework host가 소유하는 `CoroutineScope`와 설정 가능한 dispatcher를 만든다.
   `GlobalScope`와 `runBlocking`은 금지한다.
3. `suspend` handler를 Java handler interface로 변환한다. adapter는
   `scope.future(dispatcher) { ... }`로 `CompletionStage`를 반환한다.
4. Spring bean scanner가 Kotlin `suspend fun` annotation handler를 발견하고 같은
   handler catalog에 등록하도록 한다. Kotlin compiler가 추가하는 continuation
   parameter는 handler shape 검증에서 application parameter로 보지 않는다.
5. `@ZLinkRequest`, `@ZLinkSend`, `@ZLinkPublish`, route handler, Spot actor
   request/send/join/lifecycle, timer handler에 대해 Kotlin suspend method를 Java
   handler interface로 연결한다.
6. Java handler와 Kotlin suspend annotation handler가 같은 mapping을 등록하면 기존
   duplicate validation으로 거부한다.
7. Java builder를 호출하는 DSL을 만든다.
8. connector message stream을 `Flow`로 노출한다.
9. shutdown, request timeout, session close가 coroutine cancellation로 이어지는지
   구현한다.
10. `suspend` handler exception이 Java core의 handler failure policy로 모이는지
   구현한다.
11. Kotlin wrapper가 Java와 다른 error/lifecycle/ordering 의미를 만들지 않도록
   test한다.

### Gate

- Kotlin compile
- coroutine request/send/publish smoke 통과
- connector Flow smoke 통과
- framework가 소유하는 `CoroutineScope` cancellation test 통과
- Spring annotation scanner가 Kotlin `suspend fun` channel handler를 발견하고
  request/send/publish dispatch까지 실행하는 integration test 통과
- Spring annotation scanner가 Kotlin `suspend fun` Spot actor request/send/join/lifecycle
  handler와 timer handler를 발견하고 같은 Spot runtime dispatch 정책으로 실행하는
  fake backend 또는 integration test 통과
- same channel/Spot/actor/session serial ordering test 통과
- suspend handler exception mapping test 통과
- Java handler와 Kotlin handler의 duplicate registration validation test 통과
- Java API와 다른 validation 의미가 없는지 contract test 통과
- Phase 9 POSD 리팩토링 체크 통과

### Phase 9 audit evidence

아래 표는 Kotlin wrapper가 Java runtime 위의 thin wrapper로만 동작하는지 대조한
결과다. Kotlin wrapper는 Java와 다른 lifecycle, ordering, error 의미를 만들지 않는다.

| 항목 | `.NET`/Java 기준 | Kotlin 구현 | 검증 | 판정 |
|------|------------------|-------------|------|------|
| `CompletionStage` suspend wrapper | Java framework/connector async public API는 `CompletionStage`를 반환 | `ZLinkConnectorExtensions.kt`, `ZLinkFrameworkExtensions.kt`가 `await()`로 Java stage를 suspend 함수로 감쌈 | `timeout 180s gradle :zlink-framework-kotlin:test --rerun-tasks` (`suspendWrapperPreservesConnectorSemantics`, `frameworkSubmitAndRequestWrappersAwaitCompletionStage`) | 완료 |
| framework가 소유하는 coroutine runtime | suspend handler는 Java handler interface로 돌아가며 일반 함수처럼 완료 | `ZLinkCoroutineRuntime`이 framework scope의 취소 상태를 포함해 suspend handler를 실행하고, 완료된 뒤 값을 반환하거나 예외를 전달함 | `timeout 180s gradle :zlink-framework-kotlin:test --rerun-tasks` (`coroutineRuntimeMapsSuspendHandlerToJavaHandler`, `coroutineRuntimeMapsSuspendStreamErrorHandlerToCompletionStage`, `closingCoroutineRuntimeCancelsInFlightBlockingHandler`) | 완료 |
| Kotlin suspend annotation discovery | Java annotation handler처럼 Spring bean scanner가 annotated method를 찾아 runtime catalog에 등록해야 함 | `ZLinkHandlerScanner`가 Kotlin compiler의 `Continuation` parameter를 handler parameter에서 제외하고, Spring bean scanner가 같은 catalog에 등록함. Kotlin provider가 있으면 method invoker는 framework가 소유하는 coroutine context에서 suspend handler를 실행함 | `./gradlew :zlink-framework-kotlin:test --tests systems.zlink.framework.kotlin.KotlinSuspendAnnotationHandlerTest --stacktrace` (`scannerTreatsKotlinSuspendChannelAnnotationsLikeJavaMethodHandlers`, `springLifecycleDiscoversKotlinSuspendAnnotationBeanType`, `kotlinSuspendAnnotationRunsInsideFrameworkCoroutineContext`) | 완료 |
| Kotlin suspend Spot/actor annotation dispatch | Java Spot actor annotation handler처럼 actor request/send/join/lifecycle이 같은 method handler invoker로 실행되어야 함 | Spot actor suspend method도 logical parameter와 reply type을 Java method handler와 같은 registration에 보존하고 `ZLinkHandlerMethodInvoker`로 실행함. timer는 annotation 표면이 아니라 `ZLinkSpotTimerHandler` interface wrapper 표면으로 유지함 | `./gradlew :zlink-framework-kotlin:test --tests systems.zlink.framework.kotlin.KotlinSuspendAnnotationHandlerTest --stacktrace` (`scannerTreatsKotlinSuspendSpotActorAnnotationsLikeJavaMethodHandlers`, `kotlinSuspendSpotActorMethodRunsThroughMethodInvoker`) | 완료 |
| duplicate validation parity | Java handler와 Kotlin suspend handler가 같은 mapping을 등록하면 기존 duplicate validation으로 거부해야 함 | Kotlin suspend handler가 Java handler와 같은 channel packet key를 사용하므로 duplicate registration validation에서 startup 실패함 | `./gradlew :zlink-framework-kotlin:test --tests systems.zlink.framework.kotlin.KotlinSuspendAnnotationHandlerTest --stacktrace` (`duplicateValidationRejectsJavaAndKotlinSuspendAnnotationPacketCollision`) | 완료 |
| exception/cancellation mapping | Java handler failure policy는 handler 예외를 받음 | suspend wrapper handler와 suspend annotation handler exception/cancellation이 Java runtime의 handler failure 경로로 전달됨 | `./gradlew :zlink-framework-kotlin:test --stacktrace` (`coroutineRuntimePropagatesSuspendHandlerFailure`, `closingCoroutineRuntimeCancelsInFlightBlockingHandler`, `kotlinSuspendAnnotationExceptionCompletesJavaStageExceptionally`, `kotlinSuspendAnnotationCancellationCompletesJavaStageExceptionally`) | 완료 |
| connector `Flow` wrapper | Java connector `on(...)`, `onErrorReceived(...)`, dispatch mode, handler ordering 의미를 유지 | `messages(packetName)`와 `errors()`가 Java connector handler를 `callbackFlow`로 감싸고 별도 receive loop를 만들지 않음 | `timeout 180s gradle :zlink-framework-kotlin:test --rerun-tasks` (`connectorMessagesFlowUsesJavaManualDispatchSemantics`, `connectorErrorsFlowUsesJavaManualDispatchSemantics`) | 완료 |
| forbidden coroutine scope | runtime 의미를 `GlobalScope`나 runtime `runBlocking`으로 만들지 않음 | production Kotlin wrapper에 `GlobalScope`/`runBlocking` 없음. `runBlocking`은 sample/test entry point에서만 사용 | 완료 금지 패턴 검색 | 완료 |

Phase 9는 위 표의 항목이 실제 구현과 테스트 이름으로 검증될 때만 닫는다.
Kotlin coroutine runtime 단위 테스트만으로는 Spring annotation handler 지원 완료로
보지 않는다.

## 14. Phase 10 -- Samples와 release gate

### 산출물

- `framework/languages/java/samples/java/TicTacToe`
- `framework/languages/java/samples/java/Bingo`
- `framework/languages/java/samples/kotlin/TicTacToe`
- `framework/languages/java/samples/kotlin/Bingo`
- `framework/languages/java/samples/run_samples.sh`

### 작업

1. `TicTacToe` direct sample을 구현한다.
2. `Bingo`로 matching room, timer, bound push를 검증한다.
3. `TicTacToe`와 `Bingo`를 `samples/java/*`, `samples/kotlin/*` 양쪽에 배치한다.
4. `.NET`의 `Client`, `Server/Api`, `Server/Play`, `Server/Registry`,
   `Server/Session`, `Shared/*` 역할을 Java/Kotlin package와 파일로 나누어 둔다.
   독립 실행이 필요한 sample은 같은 역할을 Gradle 하위 프로젝트로도 나누어 둔다.
   `Bingo`는 actor joined/left, Spot created, room model, player client 역할 파일을
   생략하지 않는다.
5. sample regression self-check를 만든다.

### Gate

```bash
./framework/languages/java/samples/run_samples.sh
```

sample gate는 아래를 자동 확인해야 한다.

- sample이 framework/connector public API만 사용한다.
- `TicTacToe`, `Bingo`가 Java/Kotlin 양쪽에서 `.NET`
  sample의 역할 package와 주요 handler/model/player-client 파일을 가진다.
- session sample에 route/metadata store가 없다.
- connector client가 manual dispatch mode에서 request/reply와 notification을 처리한다.
- Bingo deterministic scenario가 같은 sequence winner를 만든다.
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §6
  (Sample release gate)의 모든 sample 행 미러
- Phase 10 POSD 리팩토링 체크 통과

### Phase 10 audit evidence

아래 표는 `.NET` sample의 역할 구조와 Java/Kotlin sample release gate를 다시 대조한
결과다. Java/Kotlin sample은 같은 sample set을 가지며, sample runner와 contract test가
역할 package, public API 경로, forbidden pattern을 함께 검사한다.

| 항목 | `.NET` 기준 | Java/Kotlin 구현 | 검증 | 판정 |
|------|-------------|------------------|------|------|
| sample set | `samples/TicTacToe`, `samples/Bingo` | `samples/java/*`와 `samples/kotlin/*`에 `TicTacToe`, `Bingo` 배치 | `SampleReleaseGateContractTest.requiredSamplesExposeExecutableEntryPoints` | 완료 |
| direct TicTacToe 역할 구조 | `Client`, `Server/Api`, `Server/Play`, `Shared/Contracts` | Java/Kotlin `Client`, `Server`, `Shared` Gradle 하위 프로젝트와 `client`, `server/api`, `server/play`, `shared/contracts` package로 분리 | `ticTacToeDirectSampleUsesFrameworkRuntimePublicFacade`, `ticTacToeKotlinSampleMirrorsJavaRoleLayout` | 완료 |
| Bingo 역할 구조 | `Client`, `Server/Api`, `Server/Play`, `Server/Registry`, `Server/Session`, `Shared/*` | Java/Kotlin Bingo sample이 matching, room, actor, session, notification 역할 파일을 가짐 | `bingoMirrorsFourClientMatchingTimerAndBoundPushGate`, `bingoKotlinSampleMirrorsJavaRoleLayout` | 완료 |
| release gate forbidden pattern | sample이 public framework/connector API만 사용 | aggregate runner와 contract test가 internal import, route/metadata store, recording/fake, direct `Catalog`, direct `new Spot`, readiness sleep을 금지 | `sampleSourcesUseOnlyPublicFrameworkAndConnectorApi`, `./samples/run_samples.sh` forbidden search | 완료 |

## 15. Phase 11 -- Documentation promotion

### 산출물

- 구현된 Java public API와 일치하는 정식 spec 문서
- 사용자 guide 문서
- internals 문서
- sample README와 실행 문서
- draft 잔여 항목 정리

### 작업

1. 구현된 public API, Java binding public API, regression test를 기준으로 draft 내용을
   검토한다.
2. 현재 구현과 테스트에 존재하는 계약만 정식 spec으로 승격한다.
3. 사용법 설명은 guide로 나누고, backend adapter/lifecycle/behavior matrix는
   internals로 나눈다.
4. draft에 남길 후속 편의 기능과 정식 문서로 승격할 내용을 분리한다.
5. README와 sample 문서 링크를 새 구조에 맞춘다.

### Gate

- 정식 spec에 구현되지 않은 API가 없다.
- guide에 backend internal 설명이 섞이지 않는다.
- internals에 사용자 사용법이 섞이지 않는다.
- draft에 남은 항목은 구현 전 또는 후속 편의 기능으로 명확히 표시된다.
- 문서 링크와 sample README 링크가 깨지지 않는다.
- Phase 11 POSD 리팩토링 체크 통과

### Phase 11 audit evidence

아래 표는 구현 완료 기준으로 승격한 문서와 남긴 draft 범위를 대조한 결과다.

| 항목 | `.NET` 기준 | Java/Kotlin 문서 | 검증 | 판정 |
|------|-------------|------------------|------|------|
| 사용자 guide | `framework/languages/dotnet/doc/guide/*` | `framework/languages/java/doc/guide/*`에 01~12 guide와 `guide/samples/*` 배치 | official 문서 링크 check, guide/internal 표현 검색 | 완료 |
| 공개 계약 spec | `framework/languages/dotnet/doc/spec/*` | `framework/languages/java/doc/spec/*`에 handler, Spring Boot channel/spot/actor/stream/registry/monitoring, connector 계약 배치 | `spec/README.ko.md`, 초안 표현 검색 | 완료 |
| internals | `framework/languages/dotnet/doc/internals/*` | `framework/languages/java/doc/internals/*`에 backend policy, behavior/lifecycle/regression matrix 배치 | official 문서 링크 check, internals 사용법 표현 검색 | 완료 |
| draft 잔여 항목 | `.NET` `doc/draft/*` | `draft/README.ko.md`가 실행 계획, 포팅 계획, 후속 초안만 안내 | draft README 링크 check | 완료 |
| sample 실행 문서 | `.NET` `doc/guide/samples/*`, `samples/run_samples.sh` | `samples/README.md`, `guide/samples/*`, Java/Kotlin sample runner | `./samples/run_samples.sh`, official 문서 링크 check | 완료 |

## 16. 작업 중 점검 체크리스트

각 phase가 끝날 때 아래를 확인한다.

- §1.2 phase evidence table이 작성되어 있고 모든 항목이 `완료`다.
- `.NET` 기준 파일, Java/Kotlin 구현 파일, 검증 파일/명령이 각각 비어 있지 않다.
- §1.3 완료 금지 패턴 검색 결과가 green이다.
- 문서의 public 이름과 코드 이름이 일치한다.
- 새 public API가 생기면 guide, contract, regression 문서가 함께 갱신된다.
- validation failure와 runtime event의 경계가 behavior matrix와 일치한다.
- backend concrete type이 public API에 새지 않는다.
- `.NET` test에서 같은 의미를 검증하는 항목이 있으면 Java regression matrix에
  대응 항목이 있다.
- sample이 framework/connector public API를 통하지 않고 domain object, `Catalog`,
  `Spot`, actor instance를 직접 호출하지 않는다.
- `Noop*`, `Recording*`, `UnsupportedOperationException("not needed by sample")`가
  production runtime이나 sample에 남아 있지 않다.

## 16.1 드래프트 함정 정정표 (code-vs-draft)

이전 Java/Kotlin 초안에서 `.NET` 코드로 확인해 **정정한 항목**이다. 구현 중 아래
"틀림" 쪽을 다시 끌어들이지 않는다. 충돌 시 "코드(맞음)" 쪽을 따른다.

| 주제 | 드래프트(틀림) | 코드(맞음) |
|------|----------------|------------|
| publish handler | `@ZLinkEventMapping`/`ZLinkEventHandler` | `@ZLinkPublish`/`ZLinkPublishHandler` (publish, not Event) |
| annotation 이름 | `@ZLinkRequestMapping`/`@ZLinkSendMapping` (`Mapping` 접미사) | `@ZLinkRequest`/`@ZLinkSend`/`@ZLinkPublish` (`Mapping` 없음) |
| spot outbound 표면 | 별도 이름 | `ZLinkSpotOutbound`(`IZLinkSpotOutbound` 대응) — `ZLinkSpotClient`가 구현 |
| monitoring discovery | discovery monitoring source 별도 등록 | 없음. discovery는 registry query로 관찰 |
| backend 포트 | spot/monitor 광범위 포트 | backend = factory + 5 adapter (context/socket·channel/spot/stream/registry/monitoring). spot 포트는 spot node 생성만, monitor 포트는 socket monitor만 |
| registry query 이름 | 임의 query 이름 | `status`/`serviceSummary`/`topology`/`memberPeers` (`.NET` `StatusAsync`/`ServiceSummaryAsync`/`TopologyAsync`/`MemberPeersAsync` 대응) |
| fanout handler 등록 | `addEventHandler` | `addPublishHandler` |
| spot route 수락 | 임의 이름 | `acceptSpotRoutesFromChannel` (`.NET` `AcceptSpotRoutesFromChannel` 대응) |
| actor context → spot | 직접 spot 참조 만들기 | `actorContext.getSpot()` (`.NET` `GetSpot`) |
| shutdown 순서 | 임의/시동과 같은 순서 | 시동의 역순. `SmartLifecycle`로 보장 (monitoring detach → channel/spot/stream stop → registry stop → binding context close) |
| stream session | session type 분리 모델 | header 기반 `ZLinkSession` 하나. 단일 `onDispatch` |

Kotlin/JVM 표면 제약: `CompletionStage`→`suspend`, monitoring stream→`Flow`,
Java builder를 호출하는 thin DSL만 둔다. Kotlin 전용 runtime 의미를 만들지 않는다.
Java public API는 call builder에 `await(...)` helper를 제공한다. 이 helper는
`submit(...)`과 같은 작업을 현재 thread에서 기다리는 편의 표면이며, Kotlin 전용
runtime 의미를 만들지 않는다.
Kotlin handler 실행은 framework가 소유하는 coroutine adapter에서만 이루어지고, Java core의
serial execution queue와 lifecycle을 우회하지 않는다.

## 17. 권장 구현 순서 요약 (phase 1:1 매핑)

아래는 §3~§15의 phase 블록과 **1:1로 대응**한다. (이전 판의 15-항목 목록은 phase
번호와 어긋나 정정함.) full regression gate는 별도 단계가 아니라 §15 Phase 11과
§14 Phase 10 release gate에 포함된다.

| # | Phase | 핵심 산출물 |
|---|-------|-------------|
| 1 | **Phase 0** | module skeleton + 정규 모듈/패키지 표 + Kotlin 2.1.0 toolchain |
| 2 | **Phase 1** | public contract + backend adapter port (fake backend) |
| 3 | **Phase 1.5** | Java binding parity (binding public API gap 닫기) |
| 4 | **Phase 2** | channel messaging (수직 슬라이스 1) |
| 5 | **Phase 3** | Spring Boot starter (`SmartLifecycle`) |
| 6 | **Phase 4** | registry + base monitoring (P5와 병행 가능) |
| 7 | **Phase 5** | Spot runtime + serial execution queue 산출 |
| 8 | **Phase 6** | actor core (serial execution queue 재사용) |
| 9 | **Phase 7** | STREAM server session + session relay |
| 10 | **Phase 8** | stream connector + 분리 codec 모듈 |
| 11 | **Phase 9** | Kotlin wrapper |
| 12 | **Phase 10** | samples + release gate (full regression 포함) |
| 13 | **Phase 11** | documentation promotion |

## 18. 실행 요청 프롬프트

이 문서를 실제 구현 작업에 사용할 때는 아래 프롬프트를 그대로 사용한다. 핵심은
전체 작업을 한 번에 완료 선언하지 않고, phase마다 audit과 evidence table을 먼저
작성한 뒤 구현, 검증, 재검토, 커밋, push까지 순서대로 닫는 것이다.

### 18.1 Goal 시작 프롬프트

Codex goal을 시작할 때는 아래 블록을 그대로 사용한다. `Goal objective`는 goal
도구가 추적할 최종 상태만 담는다. 구현 순서, 감사 방식, 검증, 자체 승인, 커밋과
push 규칙은 `Goal start prompt`에 둔다. `Goal start prompt`는 goal 생성 직후 바로
실행할 첫 지시이며, 단순 안내문이 아니다.

이미 같은 objective의 goal이 있으면 새 goal을 만들지 않고 기존 goal을 이어서
진행한다. goal 도구가 없는 환경에서는 `Goal objective`를 장기 작업의 고정 목표로
삼고, `Goal start prompt`를 실행 지시로 해석한다.

이 프롬프트는 **사용자 개입 없이 끝까지 진행**하는 작업을 전제로 한다. 작업자는 phase
전환, 구현 착수, 테스트 실행, 리뷰, 자체 승인, 커밋, push를 위해 사용자 확인을
기다리지 않는다. 질문은 §1.2의 예외처럼 작업자가 해결할 수 없는 실제 충돌이나 외부
상태가 있을 때만 허용한다. 이전 응답, 이전 커밋 메시지, 이전 문서의 완료 표현은 현재
코드와 검증 증거로 다시 확인하기 전에는 완료 근거로 쓰지 않는다.

```text
Goal objective:
/home/hep7/project/kairos/zlink/framework/languages/java/doc/draft/implementation-execution-plan.ko.md 를 단일 실행 기준으로 삼아, 사용자 승인 대기 없이 framework/languages/dotnet 과 동등한 아키텍처, 기능, 사용성, 폴더구조, 파일분류, 샘플 수준의 Java/Kotlin ZLink framework 포팅을 끝까지 완료한다. 각 phase는 audit, 구현, 검증, POSD 리뷰, 자체 승인, phase 단위 커밋과 push까지 증거 기반으로 닫는다.

Goal start prompt:
/home/hep7/project/kairos/zlink 에서 작업해. 이 요청은 장기 실행 goal이다. 같은 objective의 goal이 없으면 위 Goal objective로 goal을 생성하고, 이미 같은 objective의 goal이 있으면 새 goal을 만들지 말고 기존 goal을 이어서 진행해. goal 도구가 없는 환경에서는 Goal objective를 현재 작업의 고정 최종 목표로 삼아. goal 생성, goal 확인, context 압축, 중단 뒤 재개가 끝난 직후에는 사용자에게 다시 묻지 말고 마지막 evidence table, 테스트 결과, git 상태를 확인한 뒤 첫 미완료 phase부터 계속 진행해.

실행 기준:
1. 단일 실행 기준은 framework/languages/java/doc/draft/implementation-execution-plan.ko.md 이다.
2. 이 문서가 다른 계획 문서, README, draft, 이전 대화, 이전 응답, 이전 커밋 메시지와 어긋나면 이 문서의 phase 순서, gate, 완료 조건을 우선한다.
3. 이 문서가 실제 .NET framework와 어긋나면 framework/languages/dotnet/src, framework/languages/dotnet/tests, framework/languages/dotnet/samples 를 최종 기준으로 삼고 Java/Kotlin 코드와 이 문서를 함께 고친다.
4. AGENTS.md의 문서 디렉토리 책임, 금지 표현, ASCII diagram 규칙, binding public API 사용 규칙, POSD 절차를 함께 적용한다.
5. 완료 근거는 현재 코드, 실제 실행한 검증 명령, phase evidence table만 인정한다. 과거에 완료라고 말했거나 문서에 완료처럼 적힌 내용도 다시 검증하기 전에는 완료로 보지 않는다.

사용자 개입 없는 진행:
1. Phase 0부터 Phase 11까지 사용자 승인 대기 없이 순서대로 진행한다.
2. phase 전환, 구현 착수, 테스트 실행, 리뷰, 자체 승인, 커밋, push를 위해 사용자에게 묻지 않는다.
3. 구현 선택, 리팩토링 선택, 테스트 순서, phase 전환, 자체 승인, phase 단위 커밋과 push는 작업자가 직접 판단한다.
4. 질문은 문서 §1.2의 예외처럼 작업자가 해결할 수 없는 실제 충돌, public API 의미 변경의 큰 선택지 충돌, 외부 권한/credential/원격 저장소 문제, unrelated dirty change와 필요한 변경이 같은 파일에서 위험하게 충돌하는 경우에만 한다.
5. 질문해야 하는 예외가 생기면 phase, 파일, 충돌 내용, 선택지, 지금까지 확보한 증거를 짧게 보고한다.
6. 진행 중 사용자가 새 지시를 주면 최신 지시가 우선이다. 최신 지시가 goal 중단이 아니라면 현재 phase를 계속 진행한다.

최종 완료 조건:
1. Java/Kotlin 포팅은 framework/languages/dotnet 과 동등한 아키텍처, 기능, 사용성, 폴더구조, 파일분류, 샘플 수준을 만족해야 한다.
2. 이름만 비슷하거나 compile만 되는 scaffold는 완료가 아니다.
3. public API 흐름, runtime 배선, lifecycle, 오류 의미, 테스트, sample 동작이 .NET과 다르면 완료가 아니다.
4. Java는 .NET의 Contracts, Runtime, samples, tests 역할을 추적 가능하게 나누고, 같은 카테고리의 여러 파일은 같은 package/folder로 묶는다.
5. Java sample과 Kotlin sample은 각각 samples/java와 samples/kotlin 아래에 Bingo와 TicTacToe를 같은 구조와 같은 기능 수준으로 제공해야 한다.
6. sample이 framework/connector public API를 우회하거나 Java/Kotlin 중 한쪽만 동작하면 완료가 아니다.
7. framework core, Spring Boot starter, connector, connector codec 모듈, Kotlin wrapper, testkit, Java/Kotlin sample, 문서, release gate가 모두 evidence table로 추적되어야 한다.
8. Kotlin `suspend fun` annotation handler가 Spring DI 안에서 자동 발견되고 Java
   handler와 같은 runtime dispatch, duplicate validation, failure policy를 통과해야
   한다. 수동 `ZLinkCoroutineRuntime` wrapper만 있으면 완료가 아니다.

phase 실행 절차:
1. 각 phase는 audit -> implementation -> verification -> review -> self-approval -> commit/push 순서로 닫는다.
2. phase 시작 전에는 코드 수정부터 하지 말고 .NET 기준 파일, Java/Kotlin 대응 파일, 테스트 파일, sample 파일, 5축 동등성 판정(아키텍처, 기능, 사용성, 폴더구조, 파일분류), 완료/부분/미완료/미검증 판정을 phase evidence table로 먼저 남긴다.
3. 기존 구현이 있어도 완료로 가정하지 말고 실제 .NET 기준과 다시 대조한다.
4. 부분, 미완료, 미검증 항목만 구현하고 unrelated 변경은 하지 않는다.
5. phase 중간에 새 gap이 발견되면 같은 phase의 evidence table에 추가하고, 그 항목까지 닫은 뒤에만 자체 승인한다.
6. phase evidence table은 임시 메모로 끝내지 말고 관련 draft 또는 regression matrix에 반영해 다음 재개 시 기준으로 사용할 수 있게 한다.
7. 의존성이 허용해 병행 가능한 작업도 phase evidence를 섞지 말고, 어떤 phase의 어떤 gate를 닫는지 분리해 기록한다.

self-review와 self-approval:
1. phase 구현 뒤에는 .NET 기준과 Java/Kotlin 구현을 다시 코드 리뷰한다.
2. POSD red flag, no-op/fake/sample 우회, public API 누수, 문서와 코드 이름 불일치, 완료 금지 패턴이 남아 있는지 검색한다.
3. phase evidence table의 모든 항목이 완료이고, phase gate test, 연결 회귀, sample self-check, 완료 금지 패턴 검색, POSD 리뷰가 모두 green일 때만 작업자가 스스로 phase를 승인한다.
4. 자체 승인은 단순 선언이 아니라 evidence table, 실행한 명령, 실패 후 수정 내역, 남은 위험 신호 0개를 근거로 판정한다.
5. 부분, 미완료, 미검증, 실행하지 않은 test, 실패 후 재검증하지 않은 test가 하나라도 있으면 승인하지 말고 같은 phase 안에서 수정과 검증을 반복한다.
6. 자체 승인 결과는 관련 draft나 regression matrix에 남겨 다음 작업자가 같은 판단을 재현할 수 있게 한다.

완료 금지 패턴:
1. scaffold, no-op builder, fake runtime 우회, recording sample, in-memory route/store 우회, 직접 객체 호출, Catalog 우회, sample 전용 UnsupportedOperationException, blocking helper, readiness sleep은 완료로 인정하지 않는다.
2. sample은 framework/connector public API만 사용해서 실제 실행되어야 한다.
3. sample이 domain object, Spot, Catalog, route store, metadata store를 직접 호출하면 실패로 처리한다.
4. testkit fixture는 testkit 안에서만 허용하고 sample이나 production runtime에서 import되지 않게 forbidden dependency test로 막는다.
5. Java/Kotlin 코드가 compile은 되지만 실제 네트워크, runtime, dispatch, lifecycle 경로를 타지 않으면 완료가 아니다.

binding과 비동기 API:
1. Java framework는 bindings/java public API만 사용한다. framework 안에서 binding internal/private member를 reflection으로 호출하지 않는다.
2. 필요한 binding 기능이 없으면 bindings/java에 public API를 추가하고 테스트한 뒤 framework adapter에서 그 public API를 호출한다.
3. Java public API는 CompletionStage 기반 비동기 표면을 기본으로 하고, call builder의 `await(...)` helper는 같은 작업을 절차식 코드에서 기다리는 편의 표면으로 제공한다.
4. Kotlin은 Java runtime 의미를 바꾸지 않는 suspend/Flow wrapper만 제공한다.
5. bindings/java 수정이 필요한 경우에는 bindings/java public API, 테스트, 문서까지 함께 닫는다. Java framework 안에서 임시 adapter, reflection, blocking wrapper로 binding gap을 숨기지 않는다.
6. Kotlin coroutine 지원은 Java CompletionStage를 suspend/Flow로 감싸는 thin wrapper로 구현하고, runtime 의미나 callback 실행 순서를 Kotlin wrapper가 새로 정의하지 않게 한다.
7. Kotlin suspend annotation handler는 Spring DI와 framework scanner 안에서 동작해야
   한다. `suspend fun`을 직접 `runBlocking`으로 호출하거나 sample에서 handler를 수동
   생성해서 통과시키는 것은 완료 금지 패턴이다.

검증:
1. 전체 검증은 반드시 실제로 실행한 명령과 결과로만 판단한다.
2. gradle check, sample self-check, forbidden pattern search, git diff --check를 실행하지 않았거나 실패한 상태면 green으로 기록하지 않는다.
3. test hang, killed worker, skipped test, flaky retry 미완료는 미검증 또는 실패로 남긴다.
4. 실패를 고친 뒤에는 같은 명령을 다시 실행해 성공 결과를 남긴다.
5. 넓은 검증 명령은 동시에 여러 개 실행하지 말고, 실패 원인이 섞이지 않게 순서대로 실행한다.
6. 검증 결과를 요약할 때는 실행한 명령, 성공/실패, 실패 후 재실행 여부를 함께 적는다.
7. phase 커밋 전에는 `git diff --check -- framework/languages/java`를 실행해 whitespace 오류를 먼저 제거한다.

문서 동기화:
1. sample, public API, runtime 의미를 바꾸면 관련 guide, draft, regression-test-matrix를 함께 갱신한다.
2. 문서에는 구현된 사실만 적고, 구현되지 않은 계약은 정식 spec에 섞지 않는다.
3. 문서 본문에는 AGENTS.md에서 금지한 표현을 쓰지 않는다.
4. draft 문서에 완료라고 쓰려면 대응 코드와 테스트 명령이 같은 표에 있어야 한다.

커밋과 push:
1. 커밋과 push는 문서 §1.4 조건을 만족하는 phase 단위에서만 수행한다.
2. 커밋 전에는 git status와 staged diff를 확인해서 해당 phase 변경만 포함되었는지 검증한다.
3. unrelated dirty change는 stage하지 않는다.
4. §1.4 조건을 하나라도 만족하지 못하면 커밋하거나 push하지 말고, 남은 항목을 같은 phase 안에서 계속 해결한다.
5. phase 전체가 승인되지 않았는데 일부 구현만 먼저 커밋하지 않는다.
6. push가 원격 권한, 네트워크, 인증 문제로 실패하면 재시도 가능한 범위는 직접 재시도하고, 권한이나 credential 입력이 필요한 상태임을 증거와 함께 보고한다.
7. 커밋 메시지는 phase 번호와 닫은 gap을 드러내고, 검증하지 않은 내용을 포함하지 않는다.
8. 커밋 후 push가 성공하면 해당 phase evidence에 commit hash와 push 결과를 남긴다.

진행 보고:
1. Phase 11까지 모두 닫히고 full regression, sample self-check, .NET 동등성 evidence가 green이 되기 전에는 최종 완료라고 말하지 않는다.
2. 토큰이나 시간이 많이 들었다는 이유로 완료를 선언하지 않는다.
3. 검증하지 못한 것은 미검증으로 남기고 같은 phase 안에서 계속 해결한다.
4. 중간 응답에서는 현재 phase, 닫힌 evidence, 실패 또는 남은 gap만 보고한다.
5. 최종 응답에는 닫힌 phase 목록, 검증 명령, sample 결과, 커밋/push 결과, 남은 위험 0개 판정을 간단히 적는다.
```

### 18.2 일반 실행 요청 프롬프트

```text
/home/hep7/project/kairos/zlink 에서 작업해.

목표:
framework/languages/java/doc/draft/implementation-execution-plan.ko.md 를 기준으로
framework/languages/dotnet 의 ZLink framework와 동일한 아키텍처, 기능, 사용성,
폴더구조, 파일분류, 샘플 수준의 Java/Kotlin framework 포팅을 실제 구현해.

중요:
- scaffold, no-op, fake runtime, recording sample, 직접 객체 호출, Catalog 우회,
  UnsupportedOperationException("not needed by sample")은 완료로 인정하지 마.
- 완료 조건은 .NET framework와 아키텍처, 기능, 사용성, 폴더구조, 파일분류가 모두
  동등해야 해. 하나라도 다르면 완료가 아니야.
- Java는 같은 카테고리의 여러 파일을 같은 package/folder로 묶어. channels, spots,
  actors, streams, registry, monitoring, codecs, configuration, runtime host, backend
  adapter, samples/shared 역할을 루트 package나 임의 폴더에 섞지 마.
- Java는 CompletionStage 기반 비동기 표면을 기본으로 하고, Kotlin은 suspend/Flow
  wrapper만 제공해. Kotlin `suspend fun` annotation handler는 Spring scanner가 자동
  발견하고 framework가 소유하는 coroutine adapter로 실행해야 해. Java public API에
  blocking helper를 추가하지 마.
- framework는 bindings/java public API만 호출해야 해. 필요한 binding 기능이 없으면
  bindings/java에 public API를 추가하고 테스트해.
- .NET framework의 코드와 테스트가 source of truth야. 문서와 코드가 다르면
  framework/languages/dotnet/src, samples, tests를 우선해.
- unrelated dirty change는 건드리지 마.

진행 방식:
1. 사용자 승인 대기 없이 Phase 0부터 Phase 11까지 순서대로 진행해. 의존성이 허용하는 병행 작업도 먼저
   phase별 audit을 분리해서 남겨.
2. 각 phase 시작 전에는 코드 수정하지 말고 .NET 기준 폴더/파일, Java/Kotlin 현재
   폴더/파일, 테스트 파일, 완료/부분/미완료/미검증 판정을 phase evidence table로
   작성해.
3. audit에는 아키텍처, 기능, 사용성, 폴더구조, 파일분류 5축 동등성 판정을 반드시
   포함해.
4. 해당 phase의 부분/미완료/미검증 항목만 구현해. unrelated 변경은 하지 마.
5. phase 구현 후에는 gate test, 연결 회귀, 완료 금지 패턴 검색을 실행해.
6. 검증 후 POSD red flag, no-op/fake/sample 우회, public API 누수, 문서와 코드 이름
   불일치를 다시 리뷰해. 하나라도 남으면 같은 phase 안에서 수정과 검증을 반복해.
7. phase evidence table의 모든 항목이 완료가 되면 스스로 리뷰하고 승인한 뒤 그 phase를
   닫아. 사용자 승인을 기다리지 말고, §1.4 조건에 맞춰 작은 주제 단위로 커밋하고 push해.
8. Phase 11까지 모두 닫히기 전에는 최종 완료라고 말하지 마. 부분/미완료/미검증이
   하나라도 있으면 원인과 다음 수정 범위를 적고 계속 수정해.
9. 최종 완료 선언은 .NET framework와 아키텍처, 기능, 사용성, 폴더구조, 파일분류,
   샘플이 모두 동등하고 full regression과 sample self-check가 통과한 뒤에만 해.
10. 사용자에게 다시 물어보는 것은 §1.2의 예외 상황에 한정해. 단순히 phase 전환,
    구현 착수, 테스트 실행, 리뷰, 승인, 커밋·push를 위해 멈추지 마.

검증:
- 가능한 한 자동 테스트와 sample self-check를 실행해.
- 실행하지 못한 검증은 미검증으로 남겨.
- sleep이나 임시 store로 readiness 문제를 숨기지 마.
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../doc/README.ko.md) | [Java 묶음](./README.ko.md) | [포팅 계획](./java-kotlin-framework-porting-plan.ko.md)
<!-- framework-adapter-nav:bottom:end -->
