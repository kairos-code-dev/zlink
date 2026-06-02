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

배포 repository URL은 Gradle property가 아니라 환경변수로 결정한다. 기본 순서는
아래와 같다.

1. `MAVEN_REPOSITORY_URL` 이 있으면 그 URL을 사용한다. 사내 Nexus, Artifactory,
   GitHub Packages 외부 조직 저장소처럼 명시 대상이 있을 때 이 값을 쓴다.
2. `MAVEN_REPOSITORY_URL` 이 없고 `GITHUB_REPOSITORY` 가 있으면
   `https://maven.pkg.github.com/${GITHUB_REPOSITORY}` 를 사용한다. 이 저장소의 기본
   CI 배포 경로다.
3. 둘 다 없으면 `build/repo` 아래 로컬 Maven repository로 publish한다. 로컬 검증에서
   외부 저장소 credentials가 없어도 publish task를 확인하기 위한 fallback이다.

credentials도 같은 원칙을 따른다. `MAVEN_REPOSITORY_USERNAME` /
`MAVEN_REPOSITORY_PASSWORD` 를 우선 사용하고, 없으면 GitHub Actions의
`GITHUB_ACTOR` / `GITHUB_TOKEN` 을 사용한다. 따라서 package URL은 코드나 문서에
고정하지 않고, CI 환경이 배포 대상을 주입한다.

`zlink-framework-core` 내부 package(= `.NET` `Runtime/` 미러):
`systems.zlink.framework.{channels, spots, actors, streams, registry, monitoring,
configuration}`, internal `systems.zlink.framework.runtime`(backend adapter 격리),
그리고 **`systems.zlink.framework.execution`(serial execution queue 등 — §0.0
참조)**.

> 코드 모듈은 `.NET` `Runtime/Codecs/`(framework 내장 `ZLinkCodecRegistryBuilder`)와
> connector codec 모듈을 구분한다. framework 내장 codec registry는
> `zlink-framework-core` 안에 두고, connector용 codec helper는 위의 4개 connector
> codec 모듈로 분리한다.

## 1. 완료 정의 (North Star — 4축 동등성)

이 프로젝트는 **`.NET` framework와 4축이 동등**해질 때 완료다. P0~P11 phase gate를
모두 통과해도 아래 4축 표가 전부 충족되지 않으면 **미완료**다. 기준 대상은
`framework/languages/dotnet`.

| 축 | 동등 기준 | 비교 대상(`.NET`) | 확인 방법 |
|----|-----------|-------------------|-----------|
| **구조(structure)** | §0 정규 모듈/패키지 표가 `.NET` `src/`를 미러. backend 어댑터 한 층으로 격리 | `src/`, `Runtime/*` | §0 표 + backend-dependency-policy 회귀 |
| **기능(functionality)** | 모든 서브시스템(channel/spot/actor/stream/registry/monitoring/codec) 동작이 `.NET`과 동일 | `Runtime/*`, `tests/` | regression-test-matrix 전 행 green |
| **사용성(usability)** | 같은 멘탈 모델·동사·등록 흐름. `.NET` `doc/guide` 대응 Spring Boot 가이드 제공 | `doc/guide/01~12` | Java 사용자 가이드 동등 챕터 매핑 |
| **샘플(samples)** | `.NET` 샘플과 동일 시나리오의 실행 가능한 Java 샘플 + 샘플 문서 | `samples/` (TicTacToe, Bingo …) | 샘플 앱 빌드·실행 + self-check |

추가로 아래를 모두 만족해야 한다.

- Java framework core, Spring Boot starter, Stream Connector(+codec 모듈), Kotlin
  wrapper, testkit이 빌드된다.
- framework는 Java binding의 public API만 호출한다.
- `.NET`과 같은 channel, Spot, actor/session, stream, registry, monitoring 의미를
  제공한다.
- `TicTacToe`, `TicTacToe.SessionGateway`, `Bingo`, `StreamingClient` sample이 실제
  framework/connector public API만 사용해 실행된다.
- [regression-test-matrix](./internals/regression-test-matrix.ko.md)의 release gate가
  통과한다.

### 1.1 cross-language 상호호출 확인 (언어 중립 wire 계약)

기능 축에는 **언어 간 상호호출**까지 포함한다. Java 서비스가 `.NET`/C++/Node 서비스와
**같은 channel/packet** 위에서 상호 호출되는지 확인한다(언어 중립 wire 계약). 최소
한 경로(예: Java client → `.NET` server channel request/reply, 또는 Java server ←
Node connector)를 release 시나리오에 넣는다. 이것이 통과해야 4축 중 기능 축이
완료로 인정된다.

## 2. 작업 원칙

- 한 단계의 public API를 먼저 compile 가능한 contract로 닫고 runtime을 붙인다.
- backend concrete type은 public API로 내보내지 않는다.
- Java binding에 필요한 기능이 없으면 binding public API를 추가한다.
- compatibility shim은 만들지 않는다.
- sample 통과를 위해 sleep, in-memory route store, metadata store 같은 우회를 만들지
  않는다.
- Kotlin은 Java runtime 위의 thin wrapper로만 구현한다.

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
| Phase 9 | Kotlin wrapper가 새 의미를 만듦 | Java API를 호출하는 extension으로 제한 | Java와 validation/lifecycle 의미 동일 |
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
6. capability별 discovery/manual connection validation을 넣는다.

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
- `ZLinkFrameworkOptionsCustomizer`
- conditional bean registration
- lifecycle runner

### 작업

1. Spring bean scanner와 handler registration을 연결한다.
2. **`SmartLifecycle` 기반** start/stop을 구현한다. runtime 시동/종료는
   `SmartLifecycle`로 한다(역순 종료를 만족). `ApplicationRunner`는 one-shot
   readiness 신호 용도로만 쓰고 runtime lifecycle에는 쓰지 않는다.
3. [DI capability policy](./internals/di-capability-exposure-policy.ko.md)에 맞춰 bean
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
- local Spot create/get/list/remove smoke 통과
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

## 13. Phase 9 -- Kotlin wrapper

### 산출물

- `zlink-framework-kotlin`
- coroutine extensions
- DSL builder
- connector `Flow` wrapper
- Kotlin sample snippets

### 작업

1. Java `CompletionStage`를 `suspend`로 감싼다.
2. Java builder를 호출하는 DSL을 만든다.
3. connector message stream을 `Flow`로 노출한다.
4. Kotlin wrapper가 Java와 다른 error/lifecycle 의미를 만들지 않도록 test한다.

### Gate

- Kotlin compile
- coroutine request/send/publish smoke 통과
- connector Flow smoke 통과
- Java API와 다른 validation 의미가 없는지 contract test 통과
- Phase 9 POSD 리팩토링 체크 통과

## 14. Phase 10 -- Samples와 release gate

### 산출물

- `framework/languages/java/samples/TicTacToe`
- `framework/languages/java/samples/TicTacToe.SessionGateway`
- `framework/languages/java/samples/Bingo`
- `framework/languages/java/samples/StreamingClient`
- `framework/languages/java/samples/run_samples.sh`

### 작업

1. `StreamingClient`로 connector 단독 사용성을 먼저 검증한다.
2. `TicTacToe` direct sample을 구현한다.
3. `TicTacToe.SessionGateway`로 ActorGateway relay와 reconnect를 검증한다.
4. `Bingo`로 matching room, timer, bound push를 검증한다.
5. sample regression self-check를 만든다.

### Gate

```bash
./framework/languages/java/samples/run_samples.sh
```

sample gate는 아래를 자동 확인해야 한다.

- sample이 framework/connector public API만 사용한다.
- session sample에 route/metadata store가 없다.
- Session server가 ActorGateway attach를 사용한다.
- connector client가 manual dispatch mode에서 request/reply와 notification을 처리한다.
- Bingo deterministic scenario가 같은 sequence winner를 만든다.
- TicTacToe SessionGateway reconnect가 같은 actor id로 새 binding을 만든다.
- **연결 회귀**: [regression-test-matrix](./internals/regression-test-matrix.ko.md) §6
  (Sample release gate)의 모든 sample 행 미러
- Phase 10 POSD 리팩토링 체크 통과

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

## 16. 작업 중 점검 체크리스트

각 phase가 끝날 때 아래를 확인한다.

- 문서의 public 이름과 코드 이름이 일치한다.
- 새 public API가 생기면 guide, contract, regression 문서가 함께 갱신된다.
- validation failure와 runtime event의 경계가 behavior matrix와 일치한다.
- backend concrete type이 public API에 새지 않는다.
- `.NET` test에서 같은 의미를 검증하는 항목이 있으면 Java regression matrix에
  대응 항목이 있다.

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
