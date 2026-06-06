# ZLink Framework for Node.js — 구현 작업 Plan (런북)

> 이 문서 **하나로 구현 완료까지** 진행할 수 있게 만든 제어 문서다. 작업 순서,
> 각 단계의 입력·산출물·완료 기준(DoD)·검증을 모두 담는다. **상세 계약과 동작은
> 각 단계가 가리키는 링크(spec/internals/dotnet 코드)에서 확인**한다.
>
> 사용법: §0 을 먼저 읽고 → §4 의존 그래프로 현재 단계 확인 → §5 의 해당 Phase
> 블록을 펴서 "입력 문서 읽기 → 작업 → DoD 체크 → 검증" 순으로 진행 →
> §7 체크리스트에 표시. 막히면 그 Phase 가 지정한 dotnet 코드를 본다.

---

## 0. 이 문서 사용법 (문서 보는 법)

plan 은 목차·길잡이고, 실제 구현 본체는 `spec/` 에 있다. 흐름:

```
IMPLEMENTATION-PLAN (지금 문서)  ← 순서·참조지도·DoD·함정표·진행추적
   │
   ├─ 시작 전 1회: §1 필독 3개(internals 키스톤/backend/lifecycle)
   │
   └─ 각 Phase 진행 시:
        1) Phase 블록의 "입력 문서" = spec/xxx 를 펴서 계약·동작 확인  ← 구현 본체
        2) 시그니처 더 필요 → spec/handler-interfaces
        3) 동작 모호 → dotnet src/xxx 코드 (기능 최종 기준)
        4) §6 함정표로 드래프트 실수 회피
        5) DoD 체크 + 검증 통과
        6) §5.0 POSD 리팩토링 게이트 → 이슈 0 확인  ← 통과해야만
        7) §7 체크박스 표시 → 다음 Phase
```

> **Phase 전진 규칙:** 어느 Phase 든 (5) DoD·검증 + (6) POSD 게이트를 **둘 다**
> 통과하기 전에는 다음 Phase 로 넘어가지 않는다. 게이트에서 이슈가 나오면 그
> Phase 안에서 리팩토링으로 해소한 뒤 재검증한다.

| 보는 것 | 언제 | 무엇이 들어있나 |
|---------|------|-----------------|
| **이 plan** | 항상 | 순서·참조지도·DoD·함정표·진행추적 |
| internals 필독 3개 | 시작 전 1회 | 번역규칙·backend경계·lifecycle |
| **spec/ 해당 문서** | 각 Phase 구현 중 | **실제 계약·동작(설계도)** |
| dotnet `src/` | 막힐 때 | 기능 최종 기준 |

**기준 3원칙**
1. 의미·동작은 dotnet 과 동일. 표면만 NestJS / TypeScript.
2. **기능의 최종 기준은 dotnet 코드**(`framework/languages/dotnet/src`). 문서가
   코드보다 뒤처질 수 있다 — 막히면 코드를 본다.
3. backend 의존은 어댑터 한 층에만 격리. 나머지는 backend 독립.

**North Star(최종 완료 기준):** P0~P9(P1.5 포함) 통과로 끝이 아니다.
**dotnet framework 와 구조·기능·사용성·샘플 4축이 동등**해야 비로소 완료다.
상세는 §8.

---

## 1. 시작 전 필독 (3개) + 레퍼런스

구현 전에 이 3개로 규칙을 고정한다.

| 문서 | 역할 |
|------|------|
| [internals/dotnet-to-node-surface-mapping](./internals/dotnet-to-node-surface-mapping.ko.md) | **키스톤.** C#→TS / ASP.NET→NestJS / backend 매핑 규칙 |
| [internals/backend-dependency-policy](./internals/backend-dependency-policy.ko.md) | backend 어댑터 경계(유일한 스왑 지점) |
| [internals/lifecycle-and-failure-semantics](./internals/lifecycle-and-failure-semantics.ko.md) | 시동/종료/실패 순서 |

레퍼런스(해당 부분 구현 시 참조):
[di-capability-exposure-policy](./internals/di-capability-exposure-policy.ko.md) ·
[behavior-matrix](./internals/behavior-matrix.ko.md) ·
[implementation-scope-and-nongoals](./internals/implementation-scope-and-nongoals.ko.md) ·
[regression-test-matrix](./internals/regression-test-matrix.ko.md) ·
[sample-implementation-plan](./sample-implementation-plan.ko.md)

---

## 2. 참조 소스 코드 지도 (dotnet — 기능의 최종 기준)

동작이 모호하면 **항상 이 코드를 본다.** 경로는 `framework/languages/dotnet/src`.

| 서브시스템 | dotnet 소스 경로 | 대응 node spec |
|------------|------------------|----------------|
| backend 포트(계약) | `Zlink.Framework/Runtime/Backend/Contracts/` (10 파일) | [nestjs-overview](./spec/nestjs-overview.ko.md) §5 |
| backend 어댑터(dotnet 구현) | `Zlink.Framework/Runtime/Backend/DotNet/` (16 파일, wrapper 12) | 〃 (node 어댑터의 본보기) |
| 호스트/런타임 | `Zlink.Framework/Runtime/Host/ZLinkFrameworkRuntime*.cs` | nestjs-overview §4 |
| 등록/설정 | `Zlink.Framework/Runtime/Configuration/` (+`Builders/`) | 각 spec 등록 섹션 |
| DI 진입점 | `Zlink.Framework.AspNetCore/ServiceCollectionExtensions.cs` | nestjs-overview §2 |
| 계약 타입 | `Zlink.Framework/Contracts/` (38 파일) | [handler-interfaces](./spec/handler-interfaces.ko.md) |
| channel messaging | `Zlink.Framework/Runtime/Channels/`, `Runtime/Messaging/`, `Runtime/Handlers/` | [nestjs-channel-messaging](./spec/nestjs-channel-messaging.ko.md) |
| spot | `Zlink.Framework/Runtime/Spots/` (최대 서브시스템) | [nestjs-spot](./spec/nestjs-spot.ko.md), [spot-node](./spec/spot-node.ko.md) |
| actor | `Zlink.Framework/Runtime/Actors/` | [nestjs-actor](./spec/nestjs-actor.ko.md) |
| session→actor dispatch | `Runtime/Spots/`+`Runtime/Streams/`+`Runtime/Actors/` (serial executor, mailbox) | [session-actor-dispatch](./spec/session-actor-dispatch.ko.md) |
| stream | `Zlink.Framework/Runtime/Streams/` | [nestjs-stream](./spec/nestjs-stream.ko.md) |
| registry | `Zlink.Framework/Runtime/Registry/` | [nestjs-registry](./spec/nestjs-registry.ko.md) |
| monitoring | `Zlink.Framework/Runtime/Diagnostics/` | [nestjs-monitoring](./spec/nestjs-monitoring.ko.md) |
| stream connector(client) | `Systems.Zlink.Stream.Connector/` (+`.Json/.MessagePack/.Protobuf`) | nestjs-stream §2 |
| 회귀 검증 | `tests/Zlink.Framework.{ContractTests,UnitTests,E2ETests}/` | regression-test-matrix |

**하부 Node 바인딩**(`@zlink-systems/zlink`): 코드 `bindings/node`, 사용 API 요약
[doc/guide/bindings/node](/home/hep7/project/kairos/zlink/doc/guide/bindings/node/index.ko.md).
backend 어댑터가 이 바인딩 위에 dotnet 어댑터와 동일 포트를 구현한다.

---

## 3. 패키지·디렉토리 구성 (Phase 0 산출물)

[surface-mapping §2](./internals/dotnet-to-node-surface-mapping.ko.md) 기준. monorepo
workspace.

| 패키지 | 역할 | dotnet 대응 |
|--------|------|-------------|
| `@zlink-systems/framework` | 코어(런타임·계약·어댑터 포트) | `Systems.Zlink.Framework` |
| `@zlink-systems/nestjs` | NestJS 통합(`ZLinkModule` 등) | `Zlink.Framework.AspNetCore` |
| `@zlink-systems/stream-connector` | 외부 client(독립, `Zlink` prefix) | `Systems.Zlink.Stream.Connector` |
| `@zlink-systems/stream-connector-{json,msgpack,protobuf}` | codec | `.Json/.MessagePack/.Protobuf` |
| `@zlink-systems/zlink` | 하부 바인딩(이미 존재) | `bindings/dotnet` |

`@zlink-systems/framework` 내부 디렉토리(= dotnet `Runtime/` 미러):
`contracts/`, `runtime/backend/{contracts,node}/`, `runtime/host/`,
`runtime/configuration/`, `runtime/channels/`, `runtime/messaging/`,
`runtime/handlers/`, `runtime/spots/`, `runtime/actors/`, `runtime/streams/`,
`runtime/registry/`, `runtime/diagnostics/`, `runtime/codecs/`,
`runtime/execution/`(serial executor 등).

공통 toolchain: TypeScript `strict`, `reflect-metadata`(decorator 메타데이터),
Node 20+, 테스트는 `node:test` 기반 `*.test.js` 파일로 작성한다. 로컬 최종
release gate 는 `npm run verify:release` 이고, CI 는 같은 기준을 OS/ABI matrix 와
cross-language job 으로 나누어 실행한다. `node20` 과 `node22` 양쪽 runtime 은
`npm run verify:runtime-matrix` 로 확인한다.

---

## 4. 작업 순서 + 의존 그래프

```text
P0 skeleton
|
+-- P1 backend adapter
    |
    +-- P1.5 node binding parity
        |
        +-- P2 contracts
            |
            +-- P3 host lifecycle
                |
                +-- P4 channel messaging
                    |
                    +-- P5 spot
                    |   |
                    |   +-- P6 actor core
                    |   |   |
                    |   |   +-- P7 stream relay connector
                    |   |
                    |   +-- P8 registry monitoring codecs
                    |
                    +-- P9 parity validation
```

P4 까지가 임계 경로(슬라이스 1). P1.5 는 P2~P8 구현에 필요한 Node binding
public API gap 을 닫는 단계다. framework 는 binding internal/native detail 을
직접 우회하지 않는다. P5~P8 은 P4 패턴을 따르며 일부 병행 가능하나, P6(actor
core)은 P5 에 의존하고, P7(stream actor 바인딩)은 P6 에 의존한다. P8 은 base
registry/monitoring 은 P4 이후 병행 가능하지만, spot monitoring source 는 P5
이후에만 닫을 수 있다.

---

## 5. Phase별 상세

각 Phase 는 **선행 · 입력(읽을 문서) · 산출물 · 작업 · 완료 기준(DoD) · 검증**
형식. DoD·검증을 만족한 뒤 **§5.0 POSD 게이트까지 통과해야** 다음 Phase 로
넘어간다.

### 5.0 Phase 완료 게이트 — POSD 리팩토링 (모든 Phase 공통)

각 Phase 는 "동작하는 코드"에서 끝내지 않는다. DoD·검증이 green 이 되면, 그
Phase 가 만든 코드를 **POSD(Philosophy of Software Design) 원칙으로 리팩토링**하고,
**이슈가 0** 이어야 다음 Phase 로 진행한다. 기준은 repo 의
[POSD 모듈 구조](/home/hep7/project/kairos/zlink/doc/internals/posd-module-structure.ko.md)
와 framework 의
[backend-dependency-policy](./internals/backend-dependency-policy.ko.md) ·
[implementation-scope-and-nongoals](./internals/implementation-scope-and-nongoals.ko.md)
를 따른다.

**POSD 점검 기준:**

- **Deep Module** — 각 모듈이 넓은 기능을 **좁은 인터페이스** 뒤에 숨기는가.
  얕은 wrapper·pass-through 남발이 없는가.
- **정보 은닉** — 계층 간 지식 누수 최소화. backend 바인딩 타입·native
  detail 이 framework public surface 로 새지 않는가(backend-dependency-policy).
- **변경 증폭 억제** — 한 변경이 여러 모듈로 번지지 않는가. 중복 로직·산탄총
  수정(shotgun surgery) 신호가 없는가.
- **공개 표면 유지** — 이번 리팩토링이 spec/handler-interfaces 의 계약을
  바꾸지 않는가(내부만 정리). 바꿔야 하면 spec 을 먼저 고친다.
- **명확성** — 이름·경계가 의도를 드러내는가. "special case → general case"
  로 단순화 여지가 없는가. 깨끗한 per-type API 를 struct+enum 류로 불필요하게
  통합하는 등 과잉 추상화를 넣지 않았는가.
- **POSD 주석 정전화** — 모듈/공개 타입 주석이 repo POSD 주석 정책과
  일치하는가(dotnet contracts 주석 템플릿과 동일 기준).

**Phase별 POSD 중점:**

| Phase | 중점 위험 신호 | 리팩토링 방향 | Gate에 추가되는 확인 |
|-------|----------------|---------------|----------------------|
| P0 | package 구조가 역할을 숨김 | package와 test 계층을 contract/runtime/adapter/sample로 분리 | workspace 경계가 설명 가능 |
| P1 | backend wrapper가 public API로 새어 나옴 | adapter port와 node wrapper를 internal에 격리 | public surface에 binding concrete type 없음 |
| P1.5 | binding gap을 framework 우회 코드로 메움 | binding public API를 추가하고 adapter에서만 호출 | native/internal 직접 호출 없음 |
| P2 | TypeScript 타입이 dotnet 의미를 재정의 | TS 표면만 바꾸고 의미는 dotnet contract에 맞춤 | spec과 contract export가 일치 |
| P3 | NestJS context를 service locator로 사용 | provider token과 constructor injection으로 제한 | handler context가 DI container를 노출하지 않음 |
| P4 | channel type별 submit/correlation 중복 | submit operation, dispatch pipeline, transport adapter를 분리 | caller API는 fluent builder 하나 |
| P5 | Spot lifecycle, timer, route policy 혼합 | Spot activation, timer, route egress, monitoring을 내부 모듈로 분리 | Spot public surface가 type/rid 중심 |
| P6 | actor state와 dispatch queue 결합 | actor identity/state, mailbox, location resolver를 분리 | actor 이동 후 dispatch 위치가 명확 |
| P7 | session relay를 application route store로 우회 | ActorGateway와 session context를 깊은 모듈로 유지 | sample-only route/metadata store 없음 |
| P8 | registry query와 discovery hot path 혼합 | topology query와 runtime discovery view를 분리 | request hot path가 query client를 모름 |
| P9 | guide/spec/sample 책임 혼합 | 사용자 guide, spec, internals, sample README를 분리 | 구현된 계약만 spec/guide에 반영 |

**게이트 운영:**

1. DoD·검증 green 확인.
2. 위 항목으로 리팩토링. 발견 이슈는 그 Phase 안에서 해소(다음 Phase 로 미루지 않음).
3. 리팩토링 후 **DoD·검증을 다시 실행**해 회귀 없음 확인.
4. 항목이 전부 충족(이슈 0)되면 §7 의 해당 Phase + 게이트 박스를 체크하고 전진.
5. 이슈가 남으면 전진 금지 — 2~3 반복.

> 리팩토링은 **내부 구조 개선만** 한다. 외부 계약(spec)을 바꾸는 변경은 별도
> 작업으로 분리하고, spec 을 먼저 갱신한 뒤 진행한다(backend 교체 규칙과 동일).

### Phase 0 — 프로젝트 골격
- **선행:** 없음
- **입력:** [surface-mapping §2](./internals/dotnet-to-node-surface-mapping.ko.md), bindings/node 가이드
- **산출물:** §3 의 패키지 스캐폴드, tsconfig, 빌드/테스트 러너, 바인딩 의존 연결
- **작업:**
  - monorepo workspace + 6개 구현 패키지 스캐폴드
  - `tsconfig`(strict, decorators, `emitDecoratorMetadata`), lint, 테스트 러너
  - `@zlink-systems/zlink` 의존 연결, 스모크 테스트(`version()` 호출 등)
- **DoD:** 전체 빌드 통과 / 빈 테스트 스위트 실행 / 바인딩 스모크 1개 green
- **검증:** CI 골격(빌드+테스트) 가동

### Phase 1 — backend 어댑터 포트 ★최우선
- **선행:** P0
- **입력:** [nestjs-overview §5](./spec/nestjs-overview.ko.md), [backend-dependency-policy](./internals/backend-dependency-policy.ko.md) / dotnet `Runtime/Backend/Contracts/`, `Runtime/Backend/DotNet/`
- **산출물:** `runtime/backend/contracts/`(포트 10), `runtime/backend/node/`(wrapper 12 + factory)
- **작업:**
  - 포트 인터페이스 10개 TS 정의: `ZLinkBackendAdapterFactory` + Channel/Spot/Stream/Registry/Monitoring 어댑터 + backend object 계약
  - Node 바인딩 위 wrapper 12 구현: context, dealer/router/publisher/subscriber socket, spotNode, spot, stream socket, registry, registryQueryClient, socket monitor
  - factory 가 5개 어댑터 생성
- **DoD:**
  - [x] factory 가 channel/spot/stream/registry/monitoring 어댑터를 모두 생성
  - [x] 바인딩 객체(DealerSocket/SpotNode/Registry 등)가 public surface 로 새지 않음
  - [x] 허용 primitive(`RoutingId`=string, `Message`=Buffer, `SendFlags`)만 노출
- **검증:** backend-dependency-policy §9 미러 테스트(`backend-contract.test.js`,
  `backend-public-api-only.test.js`)

### Phase 1.5 — Node binding parity
- **선행:** P1
- **입력:** dotnet `Runtime/Backend/DotNet/`, dotnet `Runtime/Streams/`,
  `Runtime/Spots/`, `Runtime/Actors/`, `Systems.Zlink.Stream.Connector/`,
  `bindings/node` public API, [backend-dependency-policy](./internals/backend-dependency-policy.ko.md)
- **산출물:** Node binding public API gap list, framework 에 필요한 binding public
  API 추가분, adapter smoke test, stale native artifact guard
- **작업:**
  - channel/registry/monitoring/spot/stream/ActorGateway/bound session 에 필요한
    binding API 를 dotnet adapter 사용 경로와 대조한다.
  - binding 에 없는 기능은 `@zlink-systems/zlink` public API 로 추가한다.
  - framework adapter 는 binding public API 만 호출한다. native addon symbol,
    generated JS internal, private field, reflection-like 우회는 금지한다.
  - stale native artifact 로 인한 smoke 실패를 막기 위해 실제 native addon
    산출물이 source 보다 최신인지 확인하는 guard 를 둔다.
- **DoD:**
  - [x] P2~P8 에 필요한 binding public API gap list 가 닫힘
  - [x] ActorGateway attach, bound session send/disconnect, stream session,
        registry query, socket monitor smoke 가 binding public API 로 통과
  - [x] framework runtime/adapter 코드가 binding internal 경로를 import 하지 않음
  - [x] stale native artifact guard 통과
- **검증:** `node-binding-parity.test.js`, `backend-public-api-only.test.js`,
  `native-artifact-freshness.test.js`

### Phase 2 — 계약(Contracts) TS 이식 (backend 독립)
- **선행:** P1.5(일부 타입 공유) — 실질 병행 가능
- **입력:** [handler-interfaces](./spec/handler-interfaces.ko.md) / dotnet `Contracts/`
- **산출물:** `contracts/` 의 모든 interface·decorator·context·enum·options·client·builder 타입
- **작업:**
  - handler 계약(request/send/route/publish/spot/actor/stream/session)
  - context 타입, 결과/에러 타입, enum, options
  - decorator 팩토리(`@ZLinkRequest`/`@ZLinkSend`/`@ZLinkPublish`/`@ZLinkPacket`/`@ZLinkHandlerGroup`/spot·actor 계열/stream 계열)
  - client 인터페이스, module options 타입
- **DoD:** [x] 계약 타입 전부 컴파일 / [x] handler-interfaces 의 카탈로그 항목 누락 없음
- **검증:** contract 테스트(`test/contract/**`) — regression-test-matrix 의 ContractSurface 미러

### Phase 3 — 호스트/모듈 부트스트랩 + lifecycle
- **선행:** P1.5, P2
- **입력:** [nestjs-overview §2~4](./spec/nestjs-overview.ko.md), [lifecycle-and-failure-semantics](./internals/lifecycle-and-failure-semantics.ko.md), [di-capability-exposure-policy](./internals/di-capability-exposure-policy.ko.md) / dotnet `Runtime/Host/`, `Runtime/Configuration/`, `AspNetCore/`
- **산출물:** `runtime/host/`, `runtime/configuration/`, `@zlink-systems/nestjs`(`ZLinkModule`)
- **작업:**
  - `ZLinkModule.forRoot(options)` / `forRootAsync({useFactory, inject})` → `@nestjs/common` 실제 `DynamicModule`
  - 등록 검증(forRoot 빌드 시점), provider 토큰 노출(capability→client)
  - 런타임 시동/종료를 `onApplicationBootstrap`/`onApplicationShutdown` 에 연결, **시동 순서·graceful close** 준수
  - 레지스트리/모니터링 모듈 분리(`ZLinkRegistryModule`, `ZLinkRegistryQueryClientModule`)
- **DoD:**
  - [x] 빈 옵션으로 모듈 부트/셧다운이 lifecycle 순서대로 동작
  - [x] `@nestjs/core` application context 에서 provider 주입과 runtime lifecycle 이 동작
  - [x] 잘못된 등록이 forRoot 빌드 시 검증 예외
  - [x] capability 별 injectable client 토큰 노출 규칙 일치
- **검증:** lifecycle/host e2e 미러(시동순서·실패롤백·종료순서)

### Phase 4 — channel messaging (슬라이스 1 완성)
- **선행:** P3
- **입력:** [nestjs-channel-messaging](./spec/nestjs-channel-messaging.ko.md), [handler-interfaces](./spec/handler-interfaces.ko.md) / dotnet `Runtime/Channels/`, `Runtime/Messaging/`, `Runtime/Handlers/`
- **산출물:** `runtime/channels/`, `runtime/messaging/`, `runtime/handlers/`(scanner/dispatcher/filter), outbound client 구현
- **작업:**
  - 4개 채널 종류: client-server / fanout(pub-sub) / dealer mesh / route mesh
  - handler 발견(NestJS DiscoveryService) ≠ 노출(명시 등록) 규칙
  - request/send/publish handler(interface + decorator 양쪽)
  - dispatch(local ROUTER ingress, outbound DEALER reply correlation), filter pipeline
  - outbound client: `ZLinkChannelClient`/`ZLinkFanoutClient` fluent builder(`requestToChannel(...).submit(...)`, `sendToChannel(...).submit(...)`, `publish(...).submit(...)`), packet key 해석 순서
  - manual vs discovery 연결(같은 capability 에서 혼용 금지)
- **DoD:**
  - [x] 서버 handler + 주입 client 로 **request/reply 1왕복 E2E** 통과
  - [x] send(one-way)·publish(fan-out) 동작
  - [x] scan≠노출 규칙(미등록 handler 가 자동 노출 안 됨)
  - [x] filter 전/후 실행 순서
- **검증:** channels e2e 미러(ClientServer/DealerMesh/Fanout/RouteChannel/HandlerClients)

### Phase 5 — spot
- **선행:** P4
- **입력:** [nestjs-spot](./spec/nestjs-spot.ko.md), [spot-node](./spec/spot-node.ko.md), [stage-wrapper-on-spot](./spec/stage-wrapper-on-spot.ko.md) / dotnet `Runtime/Spots/`
- **산출물:** `runtime/spots/`(activation, serial executor, dispatch router, timer, manager)
- **작업:**
  - SpotNode 등록(router/pubSub bind, attached channel clients, spot publishers)
  - **type-keyed** spot factory(§6 함정), `ZLinkSpotManager`(create/getOrCreate/find/list/remove)
  - lifecycle: configure → onCreate → onInitialize → onClosing (Entry Spot 은 onCreate 없음)
  - handler 등록(`context.handlers.*`), timer(`context.addTimer`)
  - **단일 spot 실행 컨텍스트**(serial executor — 모든 packet/timer/subscription/channel-reply continuation 직렬화)
  - outbound(`context.outbound.*`: sendToSpot/requestToSpot/publish/sendToChannel/requestToChannel)
- **DoD:**
  - [x] spot 생성/조회/제거 + lifecycle 콜백 순서
  - [x] handler·timer 가 동일 직렬 컨텍스트에서 실행(상태 보호)
  - [x] requestToChannel completion 이 같은 spot 컨텍스트에서 실행
- **검증:** spot e2e 미러(Manager/Timer/Route*/Entry*)

### Phase 6 — actor core
- **선행:** P5
- **입력:** [nestjs-actor](./spec/nestjs-actor.ko.md), [session-actor-dispatch](./spec/session-actor-dispatch.ko.md) / dotnet `Runtime/Actors/`, `Runtime/Spots/`(serial executor·mailbox)
- **산출물:** `runtime/actors/`(manager/context/factory/mailbox/dispatch router)
- **작업:**
  - actor factory/manager/context, 생명주기(create→join→leave→disconnect)
  - Entry Spot vs user Spot handler 분리, join/leave/disconnected lifecycle handler
  - **dispatch 순서 보장**: actor별 mailbox 턴 → location 스냅샷 후 Entry/user Spot 큐 선택
  - actor type mismatch, duplicate create, actor location 재확인 정책
- **DoD:**
  - [x] 같은 actor 패킷 순서 보장, 서로 다른 actor 병행
  - [x] join 직후 패킷이 새 user Spot location 으로 라우팅(location 재확인)
  - [x] actor create/getOrCreate/find semantics 가 dotnet 과 일치
- **검증:** spot ActorLifecycle / actor dispatch ordering e2e 미러

### Phase 7 — stream + session relay + stream connector
- **선행:** P6
- **입력:** [nestjs-stream](./spec/nestjs-stream.ko.md) / dotnet `Runtime/Streams/`, `Systems.Zlink.Stream.Connector/`
- **산출물:** `runtime/streams/` + `@zlink-systems/stream-connector` +
  connector codec 패키지(`@zlink-systems/stream-connector-{json,msgpack,protobuf}`)
- **작업:**
  - stream node 등록(`session: T`, `attachActorGateway`)
  - session lifecycle(`onConnected/onDisconnected/onError/onDispatch`), session I/O(client.send/reply, stream.write/close)
  - session→actor bind/relay
  - bound session(`ZLinkBoundSession`) send/disconnect, message metadata policy, stale binding token guard
  - 독립 client `ZlinkStreamConnector`(create→on→connect→send/request→dispatch pump)
  - connector codec 패키지(json/msgpack/protobuf)는 connector 전용으로 분리한다.
    framework runtime codec registry 와 섞지 않는다.
- **DoD:**
  - [x] 외부 client 연결 → session onDispatch 수신 → reply 왕복
  - [x] session→actor relay 동작
  - [x] bound session send / disconnect 동작
  - [x] stale binding token guard 동작
  - [x] connector 재연결/heartbeat 옵션 동작
- **검증:** stream e2e 미러(Protocol/Disconnect/SessionRelay/ActorBinding) + connector 테스트

### Phase 8 — registry + monitoring + codecs
- **선행:** P4. 단, spot monitoring source 는 P5 이후
- **입력:** [nestjs-registry](./spec/nestjs-registry.ko.md), [nestjs-monitoring](./spec/nestjs-monitoring.ko.md) / dotnet `Runtime/Registry/`, `Runtime/Diagnostics/`, `Runtime/Codecs/`
- **산출물:** `runtime/registry/`, `runtime/diagnostics/`, `runtime/codecs/`
  (framework runtime codec registry)
- **작업:**
  - 임베디드 registry 시동(heartbeat 5000/timeout 15000/broadcast 30000 ms), discovery 등록
  - in-process query(status/serviceSummary/topology/memberPeers) + remote query(topology only)
  - monitoring source 등록(socket/registry), typed runtime event + handler provider (discovery 는 query 로 관찰 — §6)
  - P5 이후 spot snapshot polling-diff monitoring source 연결
  - framework runtime codec registry(json/msgpack/protobuf). connector codec package 와
    책임을 섞지 않는다.
- **DoD:**
  - [x] registry 시동 + in-process/remote topology 조회
  - [x] socket/registry 이벤트가 handler 로 전달
  - [x] P5 이후 spot 이벤트가 handler 로 전달
  - [x] codec 등록·직렬화 왕복
- **검증:** registry e2e(Discovery/EmbeddedRegistry) + monitoring/events e2e 미러

### Phase 9 — 동등성 검증 + 사용성·샘플 동등성
- **선행:** P4~P8
- **입력:** [regression-test-matrix](./internals/regression-test-matrix.ko.md),
  [sample-implementation-plan](./sample-implementation-plan.ko.md), §8 최종 완료 기준,
  dotnet `tests/`, `samples/`, `doc/guide/`
- **산출물:** 전체 regression suite, NestJS 사용자 guide, NestJS sample apps, sample 실행 스크립트, 문서 링크 회귀 테스트
- **작업:**
  - contract/unit/e2e/multi-process 미러를 채워 dotnet 동작 동등성 고정. 문서별 `회귀 테스트` 단락이 실제 테스트로 연결되는지 확인.
  - **사용성 계층 동등화**: dotnet `doc/guide` 에 대응하는 node 사용자 가이드(NestJS)를
    `sample-implementation-plan` 의 장 매핑대로 작성한다.
  - **샘플 동등화**: stream connector 단독 sample 과 TypeScript NestJS sample 을
    같은 시나리오로 구현한다. NestJS sample 은 TypeScript 를 기준으로 제공하고,
    JavaScript NestJS sample 은 별도로 유지하지 않는다.
  - sample smoke command 를 만들고 CI release gate 에 연결한다.
  - `npm run verify:cross-language` 를 release gate 에 연결한다(Node↔dotnet,
    Node↔C++/Java 중 최소 지정 경로).
  - guide/spec/internals/sample 문서 링크가 깨지지 않는지 문서 회귀 테스트를 추가한다.
- **DoD:**
  - [x] regression matrix 의 모든 행이 green / multi-process topology 시나리오 통과
  - [x] NestJS sample smoke command 가 모든 필수 sample 을 실행하고 self-check 통과
  - [x] 사용자 guide 가 dotnet guide 의 주요 장과 1:1 대응한다
  - [x] cross-language smoke 가 Node↔dotnet request/reply, stream connector 왕복,
        actor/session relay 중 최소 필수 경로를 통과한다
  - [x] 문서 링크 회귀 테스트 통과
  - [x] §8 의 4축(구조·기능·사용성·샘플) 동등성 표가 전부 충족
- **검증:** 전체 회귀 스위트 + §8 동등성 점검

> 2026-06-02 최종 재검토: `npm run verify:release` 로 ABI 선언 gate, P0 전체
> 회귀, sample smoke, Node 20/22 runtime matrix, cross-language smoke 를 통과했다.
> 샘플 host 는 ready 이후 역할 process 종료와 orphan process 를 실패로 드러내도록
> 보강했으며, P9 POSD gate 에서 추가 red flag 는 남기지 않았다.

---

## 6. 반드시 지킬 코드-검증 결정 (드래프트 함정)

문서 이식 중 **dotnet 코드로 확인해 기존 node `draft/` 를 정정**한 항목. 드래프트가
아니라 **spec/ 을 따른다.**

| 주제 | 드래프트(틀림) | spec/코드(맞음) |
|------|----------------|-----------------|
| publish | `@ZLinkEvent`/`ZLinkEventHandler` | `@ZLinkPublish`/`ZLinkPublishHandler` |
| stream session | `onPacket`/`onRaw`, `writePacket` | 단일 `onDispatch(header,payload)`, raw 는 `stream.write` 뿐 |
| spot factory | `spotName` 문자열 키 | **타입 키**(`spotFactories:[StageSpot]`, `manager.create(StageSpot)`) |
| spot 조회 | `spotRid→spotName` | 없음. `ZLinkSpotInfo` 는 `spotRid` 만 |
| spot handler 등록 | `context.addHandler(...)` | `context.handlers.addHandler(...)` |
| spot outbound | context 직접 | `context.outbound.*` |
| monitoring | `addDiscoveryEvents`/`monitoring.discovery` | 없음. discovery 는 registry query 로 |
| backend 포트 | spot/monitor 광범위 | spot 포트는 `createSpotNode` 만, monitor 포트는 `openSocketMonitor`(socket) 만 — registry/spot 이벤트는 호스트가 polling-diff 로 합성 |
| client 호출 | flat(`await client.request(ch, req, opts)`) | fluent builder(`client.requestToChannel(...).timeout(...).submit()`) |

TS 고유 제약(표면 한계): 런타임 타입 소거 → packet key 는 **생성자 이름** 또는
`@ZLinkPacket`/`packetName` 명시. `ulong`→`bigint`, `TimeSpan`→ms `number`,
`CancellationToken`→`AbortSignal?`.

---

## 7. 진행 추적 체크리스트

각 Phase 는 **(구현+DoD) → (POSD 게이트)** 두 박스를 모두 체크해야 완료다(§5.0).

- [x] **P0** 골격 — 빌드/테스트 러너/바인딩 스모크 · [x] POSD 게이트
- [x] **P1** backend 어댑터 포트 — factory 5어댑터 + wrapper 12 + 누수 0 · [x] POSD 게이트
- [x] **P1.5** Node binding parity — public API gap 0 + internal 우회 0 · [x] POSD 게이트
- [x] **P2** 계약 TS 이식 — contract 테스트 green · [x] POSD 게이트
- [x] **P3** 호스트/모듈/lifecycle — forRoot/forRootAsync + 시동·종료 순서 · [x] POSD 게이트
- [x] **P4** channel messaging — request/reply E2E + send/publish + filter · [x] POSD 게이트
- [x] **P5** spot — lifecycle + 단일 실행 컨텍스트 + manager · [x] POSD 게이트
- [x] **P6** actor core — 순서 보장 + actor lifecycle · [x] POSD 게이트
- [x] **P7** stream + session relay + connector — bound session + 외부 client 왕복 · [x] POSD 게이트
- [x] **P8** registry/monitoring/codecs — query + typed event + codec · [x] POSD 게이트
- [x] **P9** 동등성 검증 + 사용성·샘플 동등 — regression green + 가이드 + 샘플 · [x] POSD 게이트(전체 정리)
- [x] **최종 완료** — §8 4축(구조·기능·사용성·샘플) 동등성 표 전부 충족 + cross-language 상호호출 확인

---

## 8. 최종 완료 기준 — dotnet framework 동등성 (North Star)

이 프로젝트는 **dotnet framework 와 4축이 동등**해질 때 완료다. P9 까지 통과해도
아래 표가 전부 충족되지 않으면 완료로 판정하지 않는다. 기준 대상은
`framework/languages/dotnet`.

| 축 | 동등 기준 | 비교 대상(dotnet) | 확인 방법 |
|----|-----------|-------------------|-----------|
| **구조(structure)** | 패키지·모듈 경계가 dotnet 을 미러. backend 어댑터 한 층으로 격리(나머지 backend 독립) | `src/` 디렉토리, `Runtime/*` | §3 패키지 구성 + backend-dependency-policy 회귀 |
| **기능(functionality)** | 모든 서브시스템(channel/spot/actor/stream/registry/monitoring/codec)의 동작이 dotnet 과 동일 | `Runtime/*`, `tests/` | regression-test-matrix 전 행 green + cross-language(같은 channel/stream/session) 상호호출 |
| **사용성(usability)** | 같은 멘탈 모델·동사·등록 흐름. dotnet `doc/guide` 대응 NestJS 가이드 제공 | `doc/guide/01~12`, 케이스/샘플 가이드 | node 사용자 가이드 작성·동등 챕터 매핑 |
| **샘플(samples)** | dotnet 샘플과 동일 시나리오의 실행 가능한 TypeScript NestJS 샘플 + 샘플 문서 | `samples/StreamingClient`, `samples/Bingo.Ts`, `doc/guide/samples/` | 샘플 앱 빌드·실행 + 샘플 문서 동등 |

운영 규칙:

- **사용성·샘플 축은 P9 의 필수 산출물**이다. 세부 대상과 실행 기준은
  [sample-implementation-plan](./sample-implementation-plan.ko.md) 이 소유한다.
- 4축 모두 충족 + §7 의 모든 Phase·게이트 박스 체크 = **최종 완료**.
- cross-language 동등성: node 서비스가 dotnet/C++/Java 와 **같은 channel,
  stream header, session/actor packet** 위에서 상호 호출되는지까지 확인한다
  (언어 중립 wire 계약).

## 9. 회귀 테스트

이 plan 문서는 아래 문서 회귀 테스트와 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `documentation-regression.test.js › node documentation relative markdown links resolve` | `IMPLEMENTATION-PLAN.ko.md`의 상대 링크가 모두 유효하다. |
| `release-gate.test.js › node framework release gate declares required ABI and runtime matrix` | toolchain 기준이 `node20`/`node22` release gate와 일치한다. |
| `sample-regression.test.js › node cross-language smoke covers channel send publish and stream connector paths` | P1.5, P9, §8 이 binding public API gap과 cross-language smoke를 완료 기준으로 둔다. |

## 10. 현재 문서 상태

- ✅ 구현 기준 문서 완비: `spec/`(11) + `internals/` + `guide/` + 이 plan — 모두 dotnet 코드 검증 기반.
- ✅ 사용성(가이드)·샘플 실행 기준: [sample-implementation-plan](./sample-implementation-plan.ko.md) 에서 P9 산출물로 고정.
- 참고: 기존 `draft/`는 초기 설계 기록이다. 구현 기준은 `spec/`, `internals/`,
  `guide/`, `sample-implementation-plan` 이다.
- 전체 인덱스: [README](./README.ko.md).
