# Location Resolver/Store 포팅 진행 — Node (NestJS)

> 이 문서는 [framework-location-resolver-store.ko.md](framework-location-resolver-store.ko.md)
> 계약을 Node framework에 적용하는 작업의 진행 추적 문서다. 계약 내용은 반복하지 않는다.
> 계약이 궁금하면 항상 원본 draft를 보고, 두 문서가 충돌하면 원본 draft가 기준이다.
>
> spot 메시징 표면(캐시 제거, `ZLinkSpotAddress` 반환)은
> [framework-spot-address-messaging.ko.md](framework-spot-address-messaging.ko.md)가 우선한다.
> 두 draft를 함께 읽고 포팅한다. 단, spot 메시징 표면 자체의 전환(rid-only overload 제거,
> stale 주소 fail-fast 분류, bridge relay framing)은 spot-address draft의 별도 작업이며
> (dotnet 포함 미구현) 이 문서의 범위가 아니다. 이 문서 범위에서는 resolver가 spot full
> 주소(`ZLinkSpotAddress`)를 반환하는 조회 표면까지 맞춘다.
>
> 다른 언어 진행 문서: [java(+kotlin)](framework-location-resolver-store-porting-java.ko.md) ·
> [cpp](framework-location-resolver-store-porting-cpp.ko.md).
> 전체 순서는 **node → java(+kotlin) → cpp**이며 node가 첫 대상이다(2.1절 이유 참조).


> **후속(2026-07-04)**: 이 문서의 이식 완료분 위에 POSD 재설계 2차 wave가 예정되어 있다.
> 변경 목록 정본은 `framework/doc/plan/framework-public-contract-posd-redesign.ko.md`,
> 이 언어의 진행 문서는 `framework/doc/plan/framework-public-contract-posd-redesign-node.ko.md`다.
경로: `framework/languages/node/`

## 1. 상태 보드

상태 표기: ⬜ 미착수 · 🟨 진행 중 · ✅ 완료

| 단계 | 상태 | 완료일 |
|------|:---:|--------|
| P1. 공통 모델/계약 (원본 §6–8) | ✅ | 2026-07-03 |
| P1a. key codec — dotnet 바이트 호환 (원본 §6.5) | ✅ | 2026-07-03 |
| P2. in-memory store + 계약 테스트 (원본 §13) | ✅ | 2026-07-03 |
| P3. location runtime — owner lease/lifecycle (원본 §5, 6.6, 9, 15.1, 16.1) | ✅ | 2026-07-03 |
| P4. 자동 연결 reconcile loop (원본 §14) | ✅ | 2026-07-03 |
| P5. spot/actor/route resolver 연결 (원본 §15–17) | ✅ | 2026-07-03 |
| P6. Redis extension (원본 §12) | ✅ | 2026-07-03 |
| P7. 등록 API/option (원본 §20.2, 20.4) | ✅ | 2026-07-03 |
| P7a. monitoring event source 교체 (원본 §20.5) | ✅ | 2026-07-03 |
| P8. 레거시 registry/discovery 제거 + 회귀 가드 (원본 §20.1) | ✅ | 2026-07-04 |
| P9. E2E 전환 (원본 §22) | ✅ | 2026-07-04 |
| P10. sample 전환 — TicTacToe·Bingo·DeliveryDispatch (원본 §22) | ✅ | 2026-07-04 |
| P11. POSD/DDD 리팩토링 루프 (원본 §21) | ✅ | 2026-07-04 |
| G. 완료 게이트 — codex 리뷰 통과 (누락 0건·리팩토링 요소 0건, 6절) | ✅ | 2026-07-04 |

## 2. 현재 상태 (2026-07-03 조사)

### 2.1 왜 node가 첫 대상인가

registry 런타임은 이미 삭제됐는데 location runtime은 아직 없어서 자동 연결·분산 resolve 기능이
공백이고, e2e/sample은 삭제된 API를 여전히 import해서 **현재 깨진 중간 상태**이기 때문이다.

### 2.2 상세

- 커밋 `4398f97d4`가 registry 런타임 배선을 제거했다.
  `packages/framework/src/runtime/registry/`는 빈 디렉토리다. location 계약 코드는 0건
  (`locationStore`/`peerLocation`/`ownerLease` 등 키워드 grep 0건).
- 계약 잔재가 남아 있다:
  - `contracts/Registry/{Models,IZLinkRegistryQuery,IZLinkRegistryOptions}.ts` — enum
    (`ZLinkAutoConnectType`, `ZLinkServiceRole`)과 topology/memberPeers/summary/status 모델.
    location 계약의 전신으로 재편 대상
  - `contracts/Configuration/`의 `useDiscovery().addRegistryEndpoint()` — 소비하는 런타임이
    없는 dead config
  - `contracts/Spots/SpotRoutingContracts.ts` — registry 기반 `ZLinkSpotRemoteAddressResolver`
  - `runtime/diagnostics/index.ts`의 `ZLinkRegistryMonitoringSource`
- 자동 연결 로직이 framework에 없다. `runtime/channels/index.ts`는 manual connection만 수행한다.
- **e2e/sample이 삭제된 `ZLinkRegistryModule`/`ZLINK_REGISTRY_QUERY`를 여전히 import해서 깨진
  상태다** (`e2e/*/Server/Registry/registry-host-factory.ts`, `e2e/DiscoveryRegistryHa/` 등).
- actor generation은 native `ZLinkBackendActorRef {nodeRid, actorId, generation}`에 있다. store 발급
  generation으로 전환할 때 native generation과의 관계 정리가 설계 포인트다.

## 3. 포팅 기준: dotnet 구현 inventory

아래 dotnet 구현을 기능 기준으로 포팅한다. 언어 관용(네이밍, async 모델, 등록 패턴)은 node를
따르되 관찰 가능한 동작과 store에 저장되는 key/값은 같아야 한다.

| 구역 | dotnet 위치 (`framework/languages/dotnet/`) | 내용 |
|------|---------------------------------------------|------|
| 계약 | `src/Zlink.Framework/Contracts/Locations/{Stores,Models,Resolvers,Options}.cs` | store 5종 + 통합 `IZLinkLocationStore`, optional watch/change stamp, row/key/filter/page/topology/status 모델, resolver 4종 + `IZLinkLocationRuntimeQuery`, location option |
| 런타임 | `src/Zlink.Framework/Runtime/Locations/` (17개 파일) | `ZLinkLocationRuntime`(owner lease heartbeat), `ZLinkLocationLifecycle`(claim/renew/remove, 소유권 상실), `ZLinkAutoConnect{Planner,Loop,Reconciler}`, `ZLinkOwnerLeaseTracker`, `ZLinkObservedLocationGenerations`, `ZLinkLocationKeyCodec`, `ZLinkStoreLocationResolvers`, `ZLinkLocationAddressResolvers`, `ZLinkInMemoryLocationStore`, `ZLinkLocationRuntimeQueryService`, `ZLinkLocationEventEmitter` 등 |
| actor 소유권 | `src/Zlink.Framework/Runtime/Actors/ZLinkActorSessionLocationOwnership.cs` | `IgnoredStale` → local instance deactivate |
| 등록 API | `src/Zlink.Framework/Contracts/Configuration/Builders.cs:186-216`, `src/Zlink.Framework/Runtime/Configuration/Builders/ZLinkFrameworkOptionsBuilder.cs:93-138` | per-role `Add...LocationStore<T>()` 5종, 인스턴스 통합 등록 `AddLocationStore(store)`, `UseInMemoryLocationStores()`, `ConfigureLocations()`. 혼용은 검증 오류 |
| 호스팅 | `src/Zlink.Framework.AspNetCore/ZLinkLocationHostedService.cs` 외 2 | 시작 시 lease 등록 → advertise, 종료 시 lease 제거 + owner row bulk remove, polling event diff |
| Redis extension | `src/Zlink.Framework.Locations.Redis/` | `ZLinkRedisLocationStore`(통합 store + change stamp), Lua script 원자성, key prefix, row JSON codec |
| 테스트 | `tests/Zlink.Framework.ContractTests/Locations/LocationContracts.cs`(공유 스위트), `tests/Zlink.Framework.UnitTests/Runtime/*Location*·*AutoConnect*·OwnerLeaseTrackerTests.cs`, `tests/Zlink.Framework.Locations.Redis.Tests/` | InMemory·Redis가 같은 계약 스위트를 통과(parity). 원본 §22 필수 테스트의 실체 |
| 회귀 가드 | `tests/Zlink.Framework.UnitTests/Documentation/Regression.cs`, `tests/Zlink.Framework.SampleRegressionTests/Regression.cs` | 레거시 registry/discovery API 재유입 금지 |

### 3.1 언어가 달라도 지켜야 하는 것 (원본 draft 역참조)

- **key 직렬화 바이트 호환** — `ZLinkLocationKeyCodec`의 canonical 문자열(길이 prefix 세그먼트,
  rid는 소문자 hex, auto-connect type/role은 canonical 소문자 문자열)은 언어와 무관하게 같아야
  한다(§6.5). 서로 다른 언어 node가 같은 Redis를 공유하는 것이 전제다.
- **owner lease와 location row는 같은 물리 저장소**(§6.6) — `NewClaim`의 만료 판정 원자성.
- **resolver에 캐시 없음** — 모든 조회가 store에 도달(§8, §10).
- **claim-then-activate** 순서(§17)와 `IgnoredStale` 소유권 상실 규칙(§9).
- **compatibility wrapper 없음** — 기존 registry/discovery API는 한 번에 제거한다(§20.1).
- **레거시 코드는 작업하면서 정리한다** — 각 단계에서 새 location 표면으로 교체한 지점의
  레거시 코드(registry/discovery 계약·런타임·설정, dead config, 미사용 import/헬퍼/테스트)는
  다음 단계로 미루지 않고 그 단계에서 함께 삭제한다. deprecated 표기, 주석 처리, 죽은 코드로
  남기지 않는다. P8은 잔여분 최종 확인과 회귀 가드 추가 단계이지, 정리를 모아두는 단계가
  아니다.
- **wall clock 금지** — lease 만료는 store 기준 시각 + local monotonic 경과로만 판정(§6.6, 7.5).
- 바로 구현할 수 없는 항목은 feature map에 gap으로 남기고, sample에 private helper나 raw 우회
  경로를 넣지 않는다(§21 포팅 순서 4).
- **공통 e2e/sample 문서는 이미 location store 기준으로 변경되어 있다** — 공통 e2e 시나리오
  문서(`framework/doc/framework/common/e2e/` config-1~8)와 공통 sample 문서
  (`framework/doc/framework/common/sample/`)가 검증 기준 정본이다. e2e/sample 구현을 이
  문서들에 맞춰 변경해야 하며, 코드에 맞춰 문서를 되돌리지 않는다.
- **버그는 회피하지 않는다** — 진행 중 버그를 발견하면 우회(workaround, 재시도 감싸기, 테스트
  완화, 시나리오 축소)하지 않는다. 원인을 파악하고, 그 버그를 재현하는 회귀 테스트를 먼저
  작성한 뒤, 버그를 수정하고 나서 하던 작업을 이어간다. 버그가 기존 framework/core에 있어도
  같은 원칙을 적용한다.
- 포팅 완료 판정은 원본 draft **§24 구현 적용 확인표**를 node에 적용해서 한다. 단, §24.3의
  cache 관련 항목("대체 예정" 표기 2건)은 spot-address draft의 캐시 제거 결정에 따라 "구현하지
  않음"이 정답이다 — cache를 만들면 안 된다.

## 4. 작업 항목

- [x] P1. 공통 모델/계약: `contracts/Locations/` 신설 — row/key/filter/page 모델, store 5종 +
      통합 store, optional watch/change stamp, resolver 4종 + runtime query, option.
      기존 `contracts/Registry/Models.ts`의 enum(`ZLinkAutoConnectType`, `ZLinkServiceRole`)과
      topology/summary/status 모델에 대응하는 location 계약을 추가했다. Registry 계약 제거는
      compatibility wrapper 없이 P8에서 일괄 처리한다.
- [x] P1a. key codec — dotnet `ZLinkLocationKeyCodec`와 바이트 호환 검증 테스트 포함
- [x] P2. in-memory store + 계약 테스트 스위트 (`test/contract/`에 store 계약 공유 스위트)
- [x] P3. location runtime: owner lease heartbeat, lifecycle write 훅
      — 완료됨: `ZLinkLocationRuntime` owner lease start/stop/renew, guarded write,
        ownership-lost event, `ZLinkLocationLifecycle` actor claim-then-activate, actor renew,
        spot claim/release, actor-session route bind/rebind/remove
      — 완료됨: `runtime/actors/index.ts` actor manager 생성/삭제 경로의 claim-then-activate,
        actorRef renew, user spot join/leave renew
      — 완료됨: `runtime/spots/index.ts` spot manager 생성/닫기 경로의 claim/release,
        conflict 시 local activation 금지, activation 실패·거절 시 rollback
      — 완료됨: `runtime/host/index.ts` 시작/종료 경로에서 lease를 등록·제거하고, 종료 시
        owner row를 정리한다. P7의 통합 store/in-memory 등록 API로 만든 runtime/lifecycle을
        actor/spot manager option과 native join coordinator에 주입한다.
- [x] P4. 자동 연결 reconcile loop: planner/loop/reconciler 신설, core socket
      `ZLinkBackendConnectableSocket.connect`로 실행. role 허용/target 매칭/pairwise initiator/
      fail-static(원본 §14) 전부
      — 완료됨: `runtime/locations`에 role 허용 정책, target 매칭, pairwise initiator,
        endpoint 없는 dial-only 예외, owner lease join resolver, fail-static reconciler,
        owner 변경 handover, change stamp + live owner set skip을 추가했다.
      — 완료됨: channel runtime host 배선. client/server, route mesh, fanout channel은
        location runtime owner lease 시작 뒤 auto-connect loop를 시작하고, 기존 backend socket
        `connect`/`disconnect`를 실행한다. manual endpoint는 auto executor가 소유하지 않는다.
      — 완료됨: spot node router/pubsub runtime 배선. spot mesh peer row는 router endpoint를
        기준으로 publish하고 pub endpoint는 metadata `pub-endpoint`에 담는다. executor는
        routing id가 있는 peer에 `connectPeerRid`, pub endpoint와 routing id 없는 peer에
        `connectPeer`, 정리 시 `disconnectPeer`를 실행한다. manual endpoint는 auto executor가
        소유하지 않는다.
- [x] P5. resolver 연결: spot rid/actor id → `ZLinkSpotAddress`, route resolve.
      `ZLinkStoreLocationResolvers`가 spot address, actor spot address, route row를 store에서
      매번 조회하고 owner lease liveness를 join한다. actor entry/user 위치는 mesh를 포함한
      `ZLinkSpotAddress`로 반환한다. 현재 rid 기반 SPOT outbound API는
      `ZLinkLocationSpotRemoteAddressResolver`가 location row를 읽어 연결하며, custom resolver가
      없고 location store가 설정된 framework/NestJS host에서 이 resolver를 기본값으로 쓴다.
- [x] P6. Redis extension 패키지(`packages/framework-locations-redis`) — 통합
      `ZLinkRedisLocationStore`가 peer/spot/actor/route/owner lease store와 change stamp store를
      함께 구현한다. Redis key prefix로 격리하고, Lua script로 generation claim, owner-guarded
      remove, owner lease TTL, owner bulk remove, change stamp INCR을 처리한다. row JSON은
      dotnet Redis extension과 같은 PascalCase field, routing id hex, route value base64 형식으로
      저장한다.
- [x] P7. 등록 API: `zlinkFramework()` 빌더와 NestJS builder에
      `useInMemoryLocationStores()`, `addLocationStore(instance)`, `configureLocations()`를
      추가했고, framework runtime host가 이 설정으로 location runtime을 만든다.
      `addPeerLocationStore(instance)`, `addSpotLocationStore(instance)`,
      `addActorLocationStore(instance)`, `addRouteLocationStore(instance)`,
      `addOwnerLeaseStore(instance)`도 framework/NestJS builder에 추가했다. per-role store는
      instance와 class provider를 모두 받을 수 있고, runtime host는 provider resolver로 class를
      해석한다. `useDiscovery()` 계열과 registry spot remote address option은 public 등록 표면에서
      제거했고, sample/e2e의 남은 호출부도 location store 등록으로 바꿨다. location option 등록은
      추가했지만, cache option은 spot-address draft의 cache 제거 결정에 따라 추가하지 않았다.
- [x] P7a. monitoring: `ZLinkRegistryMonitoringSource`를 제거하고 location runtime/row event source로
      교체했다. `ZLinkMonitoringOptions`는 runtime polling(`locationRuntime`)과 peer/spot/actor/route
      row event source를 등록한다. runtime polling은 status/topology/service summary diff와
      store unavailable/recovered event를 발행하고, row write/remove·resolve miss·auto-connect
      desired set change는 location runtime/resolver/reconciler에서 같은 event sink로 발행한다.
- [x] P8. 레거시 제거 마무리 + 회귀 가드(documentation-regression 테스트에 금지 API 목록)
- [x] P9. e2e 전환: 공통 시나리오 문서(`framework/doc/framework/common/e2e/` config-1~8 전체)가
      검증 기준이며 dotnet 단계에서 이미 location/store 기준으로 rename됨
      (`config-1-location-messaging.ko.md`, `config-6-store-failure-recovery.ko.md`).
      node e2e 디렉토리는 legacy 이름이므로 전환 시 rename 또는 매핑 —
      `DiscoveryRegistryHa` → store 장애/복구, `RegistryMessaging` → location 기반 messaging,
      `RegistrationCodec`/`RuntimeMonitoring`/`SpotService`/`PubSub`/`ResilienceLifecycle`/
      `YieldDispatch`의 registry/discovery 전제 교체. e2e 구현은 변경된 문서에 맞춰 바꾼다
      (코드에 맞춰 문서를 되돌리지 않는다)
      — 진행됨: `RegistryMessaging`, `DiscoveryRegistryHa`, `ResilienceLifecycle`, `RuntimeMonitoring`을 Redis location
      store 기반으로 전환했다. `DiscoveryRegistryHa`는 `SF-A1`·`SF-B1`·`SF-C1`·`SF-D1`·`SF-D2`
      P0 묶음만 실행하고 legacy `DR-*` client/helper를 제거했다. `ResilienceLifecycle`은
      provider/consumer가 `ZLinkRedisLocationStore`를 등록하고, 기존 registry role은 lease-aware
      public location runtime query를 노출하는 topology probe로만 남긴다. `RuntimeMonitoring`은
      deleted monitoring role을 제거하고 service role에 location runtime monitoring source를
      함께 등록한다.
      — 완료됨: 전체 e2e runner sweep 통과(`DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`,
      `RegistryMessaging`, `ResilienceLifecycle`, `RuntimeMonitoring`, `SpotService`, `YieldDispatch`).
- [x] P10. sample 전환: 공통 sample 문서(`framework/doc/framework/common/sample/`)도 이미
      location store 기준으로 변경되어 있으므로 sample 구현을 문서에 맞춰 변경한다 —
      registry host 제거, store 등록, actor 재연결 흐름, run script, README.
      검증 대상은 `TicTacToe`, `Bingo`, `DeliveryDispatch`다.
- [x] P11. POSD/DDD 리팩토링 루프
- [x] 원본 §24 확인표 점검 (§24.3 cache 항목은 spot-address draft 기준 "캐시 없음"으로 판정)
- [x] cross-language 검증: 같은 Redis instance에 dotnet과 node가 함께 붙어 서로의
      peer/spot/actor/route row를 읽는 검증 (key codec 바이트 호환의 실측 근거)

## 5. 검증

- 단위/계약: `npm test` (= `scripts/run_node_runtime_gate.js`, build+typecheck+lint+`test/**/*.test.js`)
- e2e: 각 `e2e/<Scenario>/run_e2e.sh`
- sample: `npm run verify:samples`

## 6. 완료 처리 기준 — codex 리뷰 게이트

이 문서의 모든 작업 항목이 구현·검증 통과로 체크된 뒤에도, 아래 두 가지 codex 에이전트 리뷰
게이트를 통과해야 문서를 완료 처리한다. codex 리뷰는 한 요청에 한 항목만 담고, 두 리뷰의 병렬
실행은 허용한다.

1. **적용 완결성 리뷰** — 원본 draft 계약(§24 확인표 포함, cache 항목은 3.1의 단서 적용)과 이
   문서의 체크리스트를 기준으로, node 구현에 누락되거나 왜곡 적용된 계약이 없는지 리뷰한다.
2. **POSD/DDD 리팩토링 리뷰** — AGENTS.md의 POSD 원칙(깊은 모듈, 정보 은닉, 복잡성 하향, 오류를
   정의로 제거, 위험 신호 제거)과 DDD 관점(owner lease, claim, fencing, reconcile 같은 location
   도메인 개념이 코드 구조와 이름에 그대로 드러나는가)으로 의미 있는 리팩토링 요소가 남아
   있는지 리뷰한다(원본 §21의 리뷰-리팩토링 루프와 같은 기준).

리뷰에서 나온 이슈는 반영하고(계약 변경이 필요해 보이면 구현 우회가 아니라 원본 draft 수정으로
분리) 전체 테스트 green을 확인한 뒤 재리뷰한다. 두 리뷰 모두 **남은 이슈 0건**이 될 때까지
반복하며, 0건이 되면 1절 상태 보드의 완료 게이트(G)를 ✅로 바꾸고 이 문서를 완료 처리한다.
P11(POSD/DDD 리팩토링 루프)의 완료 판정도 이 게이트의 리뷰 2 통과가 기준이다. 리뷰 라운드마다
지적·반영 요지를 진행 기록에 남긴다.

## 7. 진행 기록

| 날짜 | 내용 |
|------|------|
| 2026-07-03 | 진행 문서 작성. 현황 조사 완료 — registry 런타임 제거 상태에서 location 공백 확인 |
| 2026-07-03 | codex 리뷰 반영: P9에 `RegistrationCodec` 추가·공통 e2e 문서 rename 반영, §24 cache 단서, P7a 상태 보드 추가, dotnet 등록 API 경로 수정, spot 메시징 범위 주석 |
| 2026-07-03 | P1/P1a 완료: `packages/framework/src/contracts/Locations/`와 `runtime/locations/ZLinkLocationKeyCodec` 추가. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-key-codec.test.js` 통과. `npm test`는 기존 registry 제거 중간 상태 때문에 `test/contract/channel-client.test.js` DSC-008/DSC-009에서 `framework.ZLinkRegistryRuntime is not a constructor`로 실패(P2 이후 location runtime/store 전환 대상). |
| 2026-07-03 | P2 완료: `ZLinkInMemoryLocationStore` 추가. owner lease와 row가 같은 store instance에 있고, generation counter는 row 제거 뒤에도 재사용하지 않는다. `NewClaim`/`Renew`/`Takeover`, owner-token remove, owner bulk remove, list paging, change stamp를 `test/contract/location-store.test.js`로 검증. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-key-codec.test.js test/contract/location-store.test.js` 통과. |
| 2026-07-03 | P3 진행 중: `ZLinkLocationRuntime`과 `ZLinkLocationLifecycle` 기초 구현 추가. owner lease start/stop/renew, owner id stamping, `StoreUnavailable` guard, `IgnoredStale` ownership-lost event, actor claim-then-activate/rollback, actor spot renew, spot claim/release, actor-session route takeover/remove를 `test/contract/location-runtime.test.js`로 검증. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 통과. `npm test`는 여전히 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조에서 실패. |
| 2026-07-03 | P3 진행 중: actor/spot manager 호출 경로에 lifecycle option 배선. actor 생성은 location claim 뒤 활성화하고 actorRef를 renew하며, destroy와 user spot join/leave가 row를 갱신한다. spot 생성은 mesh와 owner node를 가진 location claim 뒤 활성화하고, conflict·실패·거절·close에서 local activation/row 상태를 정리한다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 통과. `npm test`는 여전히 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조에서 실패. P3 완료에는 host start/stop 및 P7 등록 API에서 runtime/lifecycle을 실제 manager option으로 주입하는 배선이 남아 있다. |
| 2026-07-03 | P3 완료 및 P7 착수: framework/NestJS builder에 in-memory location store, 통합 store instance, location option 등록 API를 추가했다. runtime host는 시작 시 location runtime owner lease를 등록하고, 종료 시 lease와 owner row를 정리하며, pre-start manager option 생성에도 같은 lifecycle을 주입한다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-host.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 통과. `npm test`는 `test/contract/channel-client.test.js` DSC-008/DSC-009에서 삭제된 `ZLinkRegistryRuntime` 참조 때문에 실패한다. 같은 `npm test` 실행에서 뒤따라 나온 route-client 실패는 단독 재실행(`node --test --test-name-pattern "ZLinkModule route client uses runtime host route transport after bootstrap" test/contract/channel-client.test.js`)에서 통과했다. |
| 2026-07-03 | P4 착수: `ZLinkAutoConnectPlanner`, `ZLinkStoreLocationResolvers`, `ZLinkOwnerLeaseTracker`, `ZLinkAutoConnectReconciler`, `ZLinkAutoConnectLoop`를 추가했다. planner는 role 허용 정책, target 매칭, pairwise initiator, endpoint 없는 dial-only 예외를 계산한다. resolver는 owner lease liveness를 매 조회에 join하고 캐시하지 않는다. reconciler는 local peer row를 publish하고 desired/active diff, owner handover, fail-static store failure, shutdown disconnect를 처리한다. loop는 change stamp와 live owner set version이 모두 같을 때만 list 조회를 건너뛴다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 통과. `npm test`는 `test/contract/channel-client.test.js` DSC-008/DSC-009에서 삭제된 `ZLinkRegistryRuntime` 참조 때문에 실패한다. 같은 `npm test` 실행에서 뒤따라 나온 route-client 실패는 단독 재실행(`node --test --test-name-pattern "ZLinkModule route client uses runtime host route transport after bootstrap" test/contract/channel-client.test.js`)에서 통과했다. 남음: channel/spot socket runtime에 executor/loop를 연결해 실제 core connect/disconnect를 수행한다. |
| 2026-07-03 | P7 진행: framework/NestJS builder에 per-role location store instance 등록 API 5종을 추가했고, all-or-nothing/mixing 검증을 유지했다. public `useDiscovery()` builder와 registry spot remote address option을 제거했으며 runtime host의 routed actor/entry target fallback도 registry option 대신 spot route/default route channel을 사용한다. TypeScript sample/e2e의 남은 `.useDiscovery().addRegistryEndpoint(...)` 호출과 registry spot option은 location store 등록으로 바꿔 build 표면의 삭제 API 참조를 없앴다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-host.test.js test/contract/nestjs-module.test.js test/contract/entry-spot-dispatch.test.js` 통과. 남음: DI token 또는 class 기반 store 등록 필요 여부와 monitoring option 정리. |
| 2026-07-03 | P4 channel runtime 배선: `ZLinkChannelRuntimeManager`가 location runtime/stores/options를 받아 client/server, route mesh, fanout channel별 auto-connect loop를 시작한다. channel executor는 기존 backend socket `connect`/`disconnect`만 호출하고, manual endpoint는 auto connection으로 소유하지 않는다. location store가 configured peer source이면 channel client/subscriber/route client validation을 통과하도록 조정했다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 통과. `npm test`는 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조 2건만 실패한다. 남음: spot node router/pubsub auto-connect executor/loop 배선. |
| 2026-07-03 | P4 완료: `ZLinkSpotNodeRuntimeManager`가 location runtime/stores/options를 받아 spot node별 auto-connect loop를 시작한다. spot mesh peer row는 router endpoint와 pub endpoint metadata(`pub-endpoint`)를 함께 publish하고, executor는 remote router에 `connectPeerRid`, pub endpoint에 `connectPeer`, shutdown 정리에 `disconnectPeer`를 호출한다. channel host 계약 테스트는 client/server, route mesh, fanout 자동 연결을 함께 검증하고, spot host 계약 테스트는 router/pub endpoint 연결과 manual endpoint 미소유를 검증한다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-host.test.js test/contract/location-autoconnect.test.js` 8/8 통과, `node --test test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js` 89/89 통과. `npm test`는 build/typecheck/lint 통과 뒤 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조 2건만 실패한다. |
| 2026-07-03 | P5 완료: `ZLinkStoreLocationResolvers`에 spot address, actor spot address, spot/actor row resolve를 추가했고 route resolve와 함께 owner lease liveness를 join한다. `ZLinkLocationLifecycle`은 entry actor row에 entry mesh name을 기록해 ENTRY_SPOT/USER_SPOT 주소가 모두 mesh를 포함한다. `ZLinkLocationSpotRemoteAddressResolver`는 현재 rid 기반 SPOT outbound API를 location row 기반으로 연결하며, framework/NestJS host는 custom resolver가 없고 location store가 설정된 경우 이 resolver를 기본값으로 쓴다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-runtime.test.js test/contract/location-host.test.js` 12/12 통과, `node --test test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/location-runtime.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-key-codec.test.js test/contract/location-store.test.js` 91/91 통과. `npm test`는 build/typecheck/lint 통과 뒤 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조 2건만 실패한다. |
| 2026-07-03 | P6 완료: `@zlink-systems/framework-locations-redis` 패키지를 추가하고 root build/path/workspace lock에 포함했다. `ZLinkRedisLocationStore`는 하나의 Redis prefix 아래 row hash, generation counter, kind index, owner index, lease TTL key, lease index, change stamp key를 관리한다. Lua script가 `NewClaim` live-owner conflict, `Renew` owner/generation guard, `Takeover`, owner-token remove, owner bulk remove, lease renew/remove/list, stamp bump를 처리한다. row JSON은 dotnet Redis extension과 같은 PascalCase/routing-id-hex/base64 route value 형식이다. 검증: `npm run build`, `npm run typecheck`, `npm run lint` 통과, `node --test test/contract/location-redis-store.test.js` 2/2 통과(실제 Redis 연결), `node --test test/contract/location-key-codec.test.js test/contract/location-store.test.js test/contract/location-runtime.test.js test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/location-redis-store.test.js` 23/23 통과. `npm test`는 build/typecheck/lint 통과 뒤 `test/contract/channel-client.test.js` DSC-008/DSC-009의 삭제된 `ZLinkRegistryRuntime` 참조 2건만 실패한다. |
| 2026-07-03 | P7 완료 및 P8 진행: per-role location store 등록 API가 instance와 class provider를 모두 받도록 확장했고, runtime host가 provider resolver로 class provider를 해석한다. `useDiscovery()` builder와 registry spot remote address option을 제거한 뒤 channel client validation은 location store를 peer source로 인정한다. `DSC-008`은 삭제된 registry runtime 대신 shared in-memory location store와 public location polling option으로 scale-out/scale-in을 검증하고, socket teardown은 endpoint disconnect 전에 linger 0을 적용하도록 고쳐 located provider 제거 뒤 client context close가 멈추지 않게 했다. `DSC-009`는 같은 routing id의 endpoint replacement를 store 계약으로 검증한다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test --test-name-pattern "DSC-008|DSC-009" test/contract/channel-client.test.js` 통과. P8은 남은 legacy registry/discovery 테스트·e2e·sample 참조 정리 때문에 진행 중이다. |
| 2026-07-03 | P8/P10 진행: Bingo.Ts Unix runner와 active Api/Play/Session module을 Redis location store 기준으로 전환했고, `sample-regression.test.js`의 stale Bingo Registry role 기대값을 active location-store topology 기준으로 고쳤다. actor remote native ref adoption이 actor location row를 claim하지 않도록 runtime 회귀를 추가해 Bingo remote room join conflict를 고쳤고, Redis store owner lease routing id string 처리를 수정했다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/actor-manager.test.js test/contract/location-redis-store.test.js` 통과, `./samples/Bingo.Ts/run_sample.sh` 통과, `ZLINK_SKIP_NODE_SAMPLE_SELF_CHECK=1 node --test test/contract/sample-regression.test.js` 통과. 미완료: `node --test test/contract/sample-regression.test.js`의 실제 `run_samples.sh` self-check는 TicTacToe stream request timeout 재현과 DeliveryDispatch의 삭제된 registry API build failure 때문에 아직 실패한다. Bingo README·PowerShell runner·sample inventory에는 Registry 설명과 실행 경로 잔재가 남아 있어 P10은 진행 중이다. |
| 2026-07-03 | P10 진행: TicTacToe stream request timeout 원인을 framework local bound actor relay 경로에서 줄였다. `ZLinkFrameworkRuntimeHost`가 local joined actor request를 native SessionRelay 응답에만 맡기지 않고 Spot manager로 직접 dispatch한 뒤 captured stream response target으로 응답/오류를 완료하게 했고, `stream-runtime.test.js`에 local bound actor request 회귀를 추가했다. Bingo README, sample inventory, PowerShell runner의 active 경로를 Redis location store 기준으로 정리했고, PowerShell runner도 Registry readiness 대신 Redis peer row + owner lease readiness를 확인한다. 검증: `npm run build`, `npm run typecheck`, `npm run lint`, `node --test test/contract/stream-runtime.test.js test/contract/actor-manager.test.js test/contract/location-redis-store.test.js` 통과, `./samples/TicTacToe.Ts/run_sample.sh` 3회 연속 통과, `./samples/Bingo.Ts/run_sample.sh` 통과, `pwsh -File ./samples/Bingo.Ts/run_sample.ps1` 통과(Unix PowerShell 분기), `ZLINK_SKIP_NODE_SAMPLE_SELF_CHECK=1 node --test test/contract/sample-regression.test.js` 통과. `npm run verify:samples`는 TicTacToe/Bingo 통과 뒤 DeliveryDispatch의 삭제된 registry API build failure 때문에 아직 실패한다. P10의 TicTacToe location-store 적용 여부, Windows PowerShell 분기 실측, DeliveryDispatch를 포함하는 broad sample gate 정리는 남아 있다. |
| 2026-07-03 | P7a 완료: Node monitoring public contract에서 registry source를 제거하고 location runtime polling source와 peer/spot/actor/route row event source를 추가했다. `ZLinkLocationRuntime`은 `IZLinkLocationRuntimeQuery`를 구현하고 row write/remove event를 발행하며, resolver는 resolve miss, auto-connect reconciler는 desired set change를 발행한다. framework host는 같은 location event sink를 runtime/resolver/channel/spot auto-connect에 주입한다. 검증: `npm run build`, `node --test test/contract/monitoring-runtime.test.js test/contract/location-runtime.test.js`, `npm run typecheck`, `npm run lint`, `node --test test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/nestjs-module.test.js` 통과. `npm test`는 build/typecheck/lint와 앞선 contract suites 통과 뒤 `test/contract/sample-regression.test.js`의 `node run_samples.sh executes every sample self-check`에서 DeliveryDispatch runner 재시도 2회 지점 실패로 종료했다. 남음: P8의 registry/discovery 계약 잔재와 P10 DeliveryDispatch sample failure 정리. |
| 2026-07-03 | P8/P10 추가 진행: DeliveryDispatch active compile failure를 막던 삭제 registry API 참조를 제거했다. `Server/Registry/registry-module.ts`와 `--role registry` 경로를 제거하고, probe는 runner의 endpoint readiness 확인 뒤 `topology=ready` marker만 출력한다. registry discovery가 담당하던 process 간 channel 연결은 public manual endpoint 설정으로 바꿔 dispatch-api, dispatch-center, courier-session, session role에 실제 endpoint를 넘긴다. 검증: `samples/DeliveryDispatch.Ts npm run build` 통과, workspace `npm run typecheck`, `npm run lint`, `ZLINK_SKIP_NODE_SAMPLE_SELF_CHECK=1 node --test test/contract/sample-regression.test.js` 통과. `./samples/DeliveryDispatch.Ts/run_sample.sh`는 bind/dispatch/tracking 요청까지 진행하지만 tracking의 `DeliveryStatusNotify` publish가 session subscriber에 도달하지 않아 client 전체 timeout에서 `Operation canceled`로 실패한다. |
| 2026-07-03 | P8/P10/P9 진행: fanout publisher가 첫 publish 전 bind되지 않아 DeliveryDispatch tracking publish가 session subscriber에 도달하지 않는 문제를 framework channel runtime에서 수정했고, stream session monitor disconnect endpoint 판정도 회귀 테스트와 함께 고쳤다. route mesh request submit 전 native `SubmitError`는 channel request와 같은 retriable `RouteNotConnected` public exception으로 매핑했다. 검증: `npm test` 통과, `npm run verify:samples` 통과(`PASS TicTacToe.Ts`, `PASS Bingo.Ts`, `PASS DeliveryDispatch.Ts`), `./e2e/PubSub/run_e2e.sh` 통과(`pubsub e2e result=passed`). PubSub e2e는 삭제된 `ZLinkRegistryModule` role을 제거하고 publisher endpoint manual 연결만 사용한다. P9 전체는 다른 e2e runner의 registry/discovery 이름·옵션 잔재 때문에 계속 진행 중이다. |
| 2026-07-03 | P9 진행: `RegistrationCodec`와 `SpotService` runner는 registry 제거 후 통과했고, `YieldDispatch`는 삭제 registry role 대신 전용 Redis container의 `ZLinkRedisLocationStore`를 Play/Session host에 등록하도록 전환했다. 검증: `npm run build` 통과, `./e2e/RegistrationCodec/run_e2e.sh` 통과(`registration-codec e2e result=passed`), `./e2e/SpotService/run_e2e.sh` 통과(`spot-service e2e result=passed`), `./e2e/YieldDispatch/run_e2e.sh YD-A1` 통과(`yield-dispatch e2e result=passed`, Play evidence에 `hold-started`~`probe-completed` 기록). 미완료: `./e2e/YieldDispatch/run_e2e.sh` 전체는 YD-B1에서 actor B fast marker가 actor A resume 뒤에 기록되어 `YD-B1 marker order mismatch`로 실패한다. |
| 2026-07-03 | P9 조사: `npm run build`는 계속 통과한다. `./e2e/YieldDispatch/run_e2e.sh YD-B1` 재현 로그 `logs/20260703-191259-24088`에서 session은 `ActorYieldReq`와 `ActorFastReq`를 모두 수신·dispatch하지만, Play evidence는 `actor-yield-started` → `actor-yield-released` → `actor-yield-resumed` → `actor-yield-completed` → `actor-fast-started` → `actor-fast-completed` 순서다. local entry actor dispatch 우회와 route-drain 비동기화 후보는 이 실패를 고치지 못해 적용하지 않았다. 다음 조사는 Session→Play remote actor packet relay가 첫 request reply를 기다리는 동안 두 번째 actor relay 송신 또는 Play 수신이 어디서 직렬화되는지 확인해야 한다. |
| 2026-07-03 | P9 조사: 별도 remote bound-session reply packet 방식은 Play가 Session spot route endpoint에 직접 연결하지 않는 현재 topology와 맞지 않아 적용하지 않았다. route request/reply를 유지한 채 Play route drain만 비동기로 바꾸는 후보도 `./e2e/YieldDispatch/run_e2e.sh YD-B1` 로그 `logs/20260703-193846-23098`에서 기존 순서 실패를 고치지 못해 적용하지 않았다. 현재 유효 상태는 `npm run build` 통과, YD-B1 실패 유지(`actor-yield-resumed`가 `actor-fast-started`보다 먼저 기록)다. 다음 조사는 core/native route request가 reply 전 다음 actor packet을 같은 target spot에 전달하지 않는지, 아니면 Session 쪽 relay를 public response semantics를 유지한 별도 ack 경로로 바꿀 수 있는지 확인해야 한다. |
| 2026-07-03 | P9 수정: raw remote actor packet relay가 actor request handler 완료까지 route request reply를 붙잡아 같은 target spot의 다음 actor packet을 늦게 전달하던 문제를 고쳤다. receiver가 local actor instance를 가진 request는 즉시 ack하고 실제 response/error는 내부 bound-session response/error packet으로 돌려보낸다. handler 완료 뒤 actor target이 바뀌는 Bingo 같은 흐름을 위해 response packet에 최신 actor packet target을 함께 실어 Session 쪽 actor-id cache를 갱신한다. 검증: `npm run build` 통과, `node --test test/contract/spot-manager.test.js` 통과, `./e2e/YieldDispatch/run_e2e.sh YD-B1` 통과(`logs/20260703-200810-27345`, `yield-dispatch e2e result=passed`), `npm run verify:samples` 통과(`PASS TicTacToe.Ts`, `PASS Bingo.Ts`, `PASS DeliveryDispatch.Ts`; DeliveryDispatch 첫 시도 readiness timeout 후 runner 재시도 통과), 전체 `npm test` 통과(`/tmp/zlink-node-npm-test-final2.log`). |
| 2026-07-03 | P9 진행: `RegistryMessaging` e2e의 삭제된 registry runtime 의존을 Redis-backed location store로 전환했다. runner가 전용 Redis container와 key prefix를 만들고 provider/workflow/location consumer/dynamic provider가 같은 `ZLinkRedisLocationStore`를 등록한다. 기존 registry HTTP role은 registry module 대신 location store의 peer rows를 조회해 RM-A1 topology evidence만 제공한다. 자동 연결 전파는 public `configureLocations()` polling/lease 옵션으로 E2E 범위에 맞췄다. 검증: `rg -n "ZLinkRegistry|ZLINK_REGISTRY|ZLinkRegistryModule|registry-router-endpoint|registry-pub-endpoint|useDiscovery\\(" framework/languages/node/e2e/RegistryMessaging -S` no-hit, `./e2e/RegistryMessaging/run_e2e.sh RM-A1` 통과(`logs/20260703-201649-53920`), `./e2e/RegistryMessaging/run_e2e.sh RM-B1` 통과(`logs/20260703-201855-61648`), `./e2e/RegistryMessaging/run_e2e.sh` 전체 통과(`logs/20260703-201941-66774`, RM-A1/RM-A2/RM-A4/RM-A6/RM-B1/RM-B2/RM-C1/RM-C2/RM-C3/RM-C4/RM-C5/RM-C7/RM-C8/RM-C9 및 `registry-messaging e2e result=passed`). |
| 2026-07-03 | P9 진행: `DiscoveryRegistryHa`는 공통 문서 rename에 맞춰 store failure/recovery 시나리오로 전환을 시작했다. 기존 `ZLinkRegistryModule` 기반 registry role을 Redis location store probe로 바꾸고, provider/consumer가 같은 `ZLinkRedisLocationStore`와 public `configureLocations()` polling/heartbeat/lease/grace 옵션을 등록하게 했다. runner는 명시 시나리오 `SF-A1`에서 전용 Redis container/key prefix를 만들고 provider 2개 + consumer baseline을 검증한다. 검증: `Server/Registry`, `Server/Provider`, `Server/Consumer`, `Client` 패키지 `npm run build` 통과, `git diff --check -- framework/languages/node/e2e/DiscoveryRegistryHa` 통과, `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-A1` 통과(`logs/20260703-202810-3815`, `scenario SF-A1 passed`, `store-failure-recovery scenario result=passed`). 미완료: `SF-B1`, `SF-C1`, `SF-D1`, `SF-D2` 등 store 장애/복구 P0 시나리오와 legacy `DR-*` client/helper 정리. |
| 2026-07-03 | P9 진행: `SF-B1` store 장애 중 fail-static baseline을 추가했다. NestJS public DI 표면에 `ZLINK_LOCATION_RUNTIME_QUERY`를 추가해 store가 등록된 module에서 `IZLinkLocationRuntimeQuery`를 조회할 수 있게 했고, consumer는 `/location/status`와 `/location/peers`를 이 public query로 노출한다. Redis store extension은 Redis socket close가 process crash로 번지지 않도록 client `error` event를 처리하고, 기본 offline queue와 reconnect loop를 끄되 `clientOptions` override는 허용하게 했다. `SF-B1` runner는 `SF-A1` warmup으로 자동 연결을 만든 뒤 Redis container를 정지하고, 같은 consumer가 기존 연결로 request를 계속 처리하며 runtime status가 unhealthy/lastError를 보고하는지 검증한다. 검증: workspace `npm run build` 통과, `Server/Consumer`와 `Client` 패키지 `npm run build` 통과, `git diff --check -- framework/languages/node/packages/framework/src/runtime/host/index.ts framework/languages/node/packages/nestjs/src/index.ts framework/languages/node/packages/framework-locations-redis/src/index.ts framework/languages/node/e2e/DiscoveryRegistryHa ...` 통과, `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-B1` 통과(`logs/20260703-203314-20186`, `scenario SF-A1 passed`, `scenario SF-B1 passed`, `store-failure-recovery scenario result=passed`). 미완료: `SF-C1`, `SF-D1`, `SF-D2`와 legacy `DR-*` client/helper 정리. |
| 2026-07-03 | P9 진행: `DiscoveryRegistryHa` store failure/recovery P0 묶음을 `SF-D2`까지 확장했다. runtime query의 peer/spot/actor/route list는 owner lease live 여부를 join해 stale row를 성공 결과에서 제외한다. `SF-C1`은 api-b를 SIGKILL한 뒤 owner lease TTL 경과 후 `/location/peers`가 live provider 1개만 반환하고 후속 request가 api-a로만 가는지 검증한다. `SF-D1`은 Redis container를 짧게 `pause/unpause`하면서 retry 없는 public request window가 전 구간 성공하고 status/peer list가 회복되는지 검증한다. `SF-D2`는 Redis 장기 pause 중 api-b를 SIGKILL하고, 복구 뒤 api-a 재등록, api-b 제외, 후속 request api-a 단독 routing을 검증한다. 검증: workspace `npm run build` 통과, `Server/Consumer`와 `Client` 패키지 `npm run build` 통과, `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-C1` 통과(`logs/20260703-204014-50300`), `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-D1` 통과(`logs/20260703-204208-60437`), `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-D2` 통과(`logs/20260703-204434-71368`), `./e2e/DiscoveryRegistryHa/run_e2e.sh` 현재 SF P0 전체 sweep 통과(`logs/20260703-204458-72912`, 각 child log에서 `SF-A1`·`SF-B1`·`SF-C1`·`SF-D1`·`SF-D2` pass, `discovery-registry-ha partial e2e result=passed`). 미완료: legacy `DR-*` client/helper와 디렉토리 이름 정리, config-6 P1 시나리오(`SF-A2`, `SF-B2`, `SF-C2`, `SF-D3`) 필요 여부 판단, 다른 e2e runner의 registry/discovery 잔재 정리. |
| 2026-07-03 | P9 진행: `DiscoveryRegistryHa` 내부 legacy `DR-*` code path를 제거했다. client는 `SF-A1`·`SF-B1`·`SF-C1`·`SF-D1`·`SF-D2`만 dispatch하고, `DR-*` scenario 파일, registry cluster support helper, `Server/Embedded`, `Server/Probe`, runner의 registry pub/router cluster harness를 삭제했다. `run_e2e.sh`는 Redis location store 기반 SF P0 전용 runner로 축소했고, feature-map/porting-inventory도 store failure/recovery 기준으로 갱신했다. 검증: `rg -n "DR-|Dr[A-D]|ZLinkRegistry|ZLINK_REGISTRY|ZLinkRegistryModule|useDiscovery|registry-router-endpoint|registry-pub-endpoint|Server/Embedded|Server/Probe|Embedded|Probe|managed-process|discovery-scenario-support|BasicDiscoveryScenario" framework/languages/node/e2e/DiscoveryRegistryHa -S` no-hit. workspace `npm run build`, `Client`·`Server/Registry`·`Server/Provider`·`Server/Consumer` 패키지 `npm run build` 통과, `git diff --check -- framework/languages/node/e2e/DiscoveryRegistryHa framework/doc/framework/common/draft/framework-location-resolver-store-porting-node.ko.md` 통과, `./e2e/DiscoveryRegistryHa/run_e2e.sh` 통과(`logs/20260703-205114-96377`, `store-failure-recovery e2e result=passed`). 미완료: 디렉토리 이름 자체의 rename 여부, config-6 P1 시나리오(`SF-A2`, `SF-B2`, `SF-C2`, `SF-D3`) 필요 여부 판단, 다른 e2e runner의 registry/discovery 잔재 정리. |
| 2026-07-03 | P9 진행: `ResilienceLifecycle` e2e의 삭제 registry runtime 의존을 Redis-backed location store로 전환했다. runner가 전용 Redis container/key prefix를 만들고 provider/consumer가 같은 `ZLinkRedisLocationStore`와 public `configureLocations()` polling/heartbeat/lease 옵션을 등록한다. 기존 `Server/TopologyProbe` role은 registry module 없이 `ZLINK_LOCATION_RUNTIME_QUERY`로 lease-aware peer topology만 노출하는 probe가 됐다. RL-C4는 registry restart 대신 Redis container pause/unpause로 store outage 중 기존 channel 유지와 recovery follow-up을 검증한다. 검증: `rg -n "ZLinkRegistry|ZLINK_REGISTRY|ZLinkRegistryModule|registry-router-endpoint|registry-pub-endpoint|registryMain|registryPubEndpoint|registryRouterEndpoint|managed-registry|RegistryProcess|registryProcess|useDiscovery\\(" framework/languages/node/e2e/ResilienceLifecycle -S` no-hit, `Server/TopologyProbe`·`Client` 패키지 `npm run build` 통과, `./e2e/ResilienceLifecycle/run_e2e.sh RL-B2` 통과(`logs/20260703-211102-53896`), `./e2e/ResilienceLifecycle/run_e2e.sh RL-B3` 통과(`logs/20260703-211321-60769`), `./e2e/ResilienceLifecycle/run_e2e.sh` 전체 통과(`logs/20260703-211853-78546`, RL-A1~RL-D5 및 `resilience-lifecycle e2e result=passed`). |
| 2026-07-03 | P9 진행: `RuntimeMonitoring` e2e의 삭제된 별도 monitoring role을 제거하고 Redis-backed location runtime monitoring으로 전환했다. runner가 전용 Redis container/key prefix를 만들고 Service/FilteredService/ThrowingService가 `ZLinkRedisLocationStore`를 등록한다. MON-A2는 service의 location runtime source에서 `TopologyChanged`와 `ServiceSummaryChanged`를 관찰하고, MON-A4/A5/D1도 location runtime evidence를 검증한다. 검증: `Server/Service`·`Server/FilteredService`·`Server/ThrowingService`·`Server/Trigger`·`Client` 패키지 `npm run build` 통과, `./e2e/RuntimeMonitoring/run_e2e.sh MON-A2` 통과(`logs/20260703-215553-91656`), `./e2e/RuntimeMonitoring/run_e2e.sh MON-B2` 통과(`logs/20260703-215635-94691`), `./e2e/RuntimeMonitoring/run_e2e.sh MON-D1` 통과(`logs/20260703-220318-16504`), `./e2e/RuntimeMonitoring/run_e2e.sh` 전체 통과(`logs/20260703-220339-17357`, MON-A1~MON-D1 및 `runtime-monitoring e2e result=passed`). |
| 2026-07-04 | P8 완료: active Node packages/e2e/samples 범위에서 삭제된 registry/discovery 표면이 다시 남지 않았는지 확인했다. `ZLinkLocationSpotRemoteAddressResolver`는 location row의 spot mesh를 route channel id로 그대로 쓰지 않고, 같은 mesh의 `${mesh}.route` 또는 같은 routing id를 가진 유일한 route mesh로 해석한다. `YieldDispatch` Session host는 legacy manual route connection을 제거하고 Redis location store 자동 연결만 사용한다. 검증: `rg -n "registrySpotRemoteAddresses|useDiscovery\\(|ZLinkRegistryModule|ZLINK_REGISTRY_QUERY|runtime/registry|ZLinkRegistryRuntime|addRegistryEndpoint|RegistryRemoteAddressStore" framework/languages/node/packages framework/languages/node/e2e framework/languages/node/samples -S` no-hit, `npm test` 통과, `npm run verify:samples` 통과(`PASS TicTacToe.Ts`, `PASS Bingo.Ts`, `PASS DeliveryDispatch.Ts`), `./e2e/YieldDispatch/run_e2e.sh` 통과(`logs/20260704-012623-47567`, YD-A1~YD-E4와 shutdown recovery 포함, `yield-dispatch e2e result=passed`). |
| 2026-07-04 | 리뷰 반영: 적용 완결성 리뷰가 지적한 DeliveryDispatch sample의 독립 in-memory location store와 stale RegistryMessaging README를 수정했다. DeliveryDispatch.Ts는 runner가 전용 Redis container와 sample key prefix를 만들고 모든 active role이 `ZLinkRedisLocationStore`를 등록한다. endpoint-less client/subscriber는 public location auto-connect를 사용하고, route mesh client만 현재 public API 한계 때문에 명시 endpoint를 유지한다. `RegistryMessaging/README.ko.md`는 config-1 location messaging 문서명과 현재 역할 설명에 맞췄다. POSD/DDD 리뷰가 지적한 SPOT claim-before-activate 순서도 수정했다. spot manager는 location claim이 `Stored`일 때만 provider/context를 만들고, conflict면 provider를 만들지 않으며, provider 생성 실패는 location spot claim을 rollback한다. 회귀 테스트는 loser provider constructor가 호출되지 않는지 확인한다. |
| 2026-07-04 | cross-language Redis row 검증을 추가하고 통과했다. Node smoke가 같은 Redis instance에 Node row를 쓰고 dotnet Redis store가 peer/spot/actor/route row를 읽는지 검증하며, dotnet이 쓴 row도 Node `ZLinkRedisLocationStore`가 읽는다. dotnet actor location row에는 Node JSON codec이 요구하는 `SpotMeshName` 필드를 추가했다. 검증: `./cross-language/run_cross_language_smoke.sh` 통과(`Node Redis location rows -> dotnet location store`, `dotnet Redis location rows -> Node location store` 포함). |
| 2026-07-04 | P9/P10/§24/cross-language 확인 완료. ResilienceLifecycle의 restored/recovered traffic 판정은 topology row 확인 뒤 단일 batch로 판단하지 않고 public request traffic이 실제 expected provider에 도달할 때까지 bounded wait하는 공통 helper로 정리했다. 최신 검증: `npm run build` 통과, `npm run typecheck` 통과, `npm test` 통과, `npm run verify:samples` 통과(`PASS TicTacToe.Ts`, `PASS Bingo.Ts`, `PASS DeliveryDispatch.Ts`), 전체 e2e runner sweep 통과(`DiscoveryRegistryHa`, `PubSub`, `RegistrationCodec`, `RegistryMessaging`, `ResilienceLifecycle`, `RuntimeMonitoring`, `SpotService`, `YieldDispatch`), legacy grep no-hit, `./cross-language/run_cross_language_smoke.sh` 통과. |
| 2026-07-04 | 완료 게이트 통과: POSD/DDD 재리뷰는 `NO ISSUES`로 종료했다. 적용 완결성 재리뷰는 `RegistryMessaging` feature-map/porting-inventory의 예전 Config 1 문서명과 sample README의 DeliveryDispatch 예전 registry 기반 설명을 지적했고, 이를 `config-1-location-messaging.ko.md`와 `Redis location store` 설명으로 수정했다. scoped stale-string grep no-hit 확인 뒤 재리뷰도 `NO ISSUES`로 종료했다. |

> 갱신 규칙: 단계 착수/완료 때 1절 상태 보드와 4절 체크리스트를 갱신하고, 이 표에 한 줄 기록을
> 남긴다. 계약이 바뀌면 이 문서가 아니라 원본 draft를 고치고 여기서는 참조만 한다.
