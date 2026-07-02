# SPOT ↔ channel 암묵 와이어링 재설계 계획 (draft)

## 한 줄 요약

명시적 와이어링 함수(`EnableSpotRouteEgress`, `AcceptSpotRoutesFromChannel`,
`AttachSpotPublisherClient`)를 제거하고 **같은 프로세스의 `SpotNode`와 channel을 런타임이
자동으로 잇는다.** 사용자는 `SpotNode`와 채널만 설정한다. *원격* 도달성은 호출 시점에
해석하고(없으면 런타임 예외), *로컬* 설정 오류는 시작 시점 Strict 검증으로 잡는다.

## 결정 배경 (확정 전제 — 다시 열지 않음)

1. **복잡도 이동은 가치다.** framework 사용자는 C-API를 만지지 않는다. C-API bridge 복잡도를
   깨끗한 framework 표면 뒤로 숨기는 것은 framework의 본분이다.
2. **토폴로지 경계는 (원격은) 런타임 검증으로 충분.** 신뢰된 서버 간 통신이라 "어느 channel이
   어느 노드로 가는가"를 시작 시점 명시 opt-in으로 강제할 필요가 없다. 단 *로컬*에서 싸게 아는
   오류는 Strict로 시작 시점에 잡는다(아래 검증 정책).
3. **지금이 제일 싸다.** SPOT 경로는 draft·parity 빌드아웃 단계라 외부 소비자가 없다.

## 확정 결정 로그 (2026-06-24)

| # | 결정 | 핵심 |
|---|------|------|
| Q7 | **A. 전면 암묵(0 와이어링)** | B(단일 opt-in)·C(convention) 기각. 대신 가드레일(아래) 의무화 |
| Q1 | **무게중심 = 코어(대부분 재사용) + framework thin handoff** | 코어 bridge가 채널명 키잉·relay·reply 이미 보유. framework는 채널명→소켓 handoff |
| Q9 | **프로세스당 단일 `SpotNode`** (요청1) | `AddNode` 제거, `AddSpotMesh(name)`가 곧 노드. ~~owner-selection 자명화~~ → **정당화 정정**: 모호성 제거가 아니라 **구현 단순화**다(맨 끝 "재검토" 단락). 다중 노드 대안 있음 |
| Q2 | **owner-selection = 자명(Q9 결과)** | 한 프로세스에 노드 1개 → route·pub 소유 노드 모호성 없음. 코어 전달 키 변경 불필요 |
| Q8 | **타입별 namespace + 외부→spot route는 RouteMesh 단일 + DealerMesh 제거** | 이름 타입 넘어 재사용. straddle 소멸 |
| Q6′ | **`AddRouteMeshChannel`→`AddRouteMesh` 리네임** (요청2) | mesh-계열은 "Channel" 접미사 제거(`AddSpotMesh`와 짝). ClientServer/Fanout은 유지 |
| — | **clean cutover** | no-op 셤 없이 제거·재작성. **core→dev_sync→bindings→framework**(dotnet 먼저)→java→kotlin→node |
| — | **코어: 신규 동작 1건 + 표면 축소** | 신규=pending-reply detach 취소. 그 외는 **제거 6 + 시그니처 2**(단순화, 새 계약 아님) |

미해결 질문 **전부 결정 완료**(Q1–Q9 + 구 Q3–Q6): Q3=eager(lazy 폐기), Q4=opt-out 없음(v1),
Q5=에러 유지(spotRid 유일), Q6=언어 idiom 보존. 상세는 맨 끝 "결정된" 섹션.

### A(전면 암묵)의 의무 가드레일

capability anchor가 없으므로 아래를 의무화한다(없으면 A는 위험):
- **노출 범위 축소**: 외부→spot route는 **RouteMesh 채널만** 자동 브릿지(모든 server 채널이
  아님). 일반 ClientServer/Fanout은 spot로 안 샌다.
- **단일 노드/프로세스(Q9)**: route·pub 소유 노드가 자명. (다중 노드가 하드 요구로 올라오면
  channel→node 링크 = codex B로 되돌아가야 함 — 그때 재개.)
- **로컬 Strict 검증**: 아래 "검증 정책".
- **pending-reply 안전**: detach/shutdown 시 pending 취소 + generation ownership.

## Q9 비용 — 다중 노드/프로세스 재구성 (요청1의 대가)

현재 **다중 `SpotNode`/프로세스가 실사용 중**이라 Q9는 이들 재구성을 동반한다:
- `SpotService` e2e Server: `PlaySpotNode` + `SessionSpotNode` 2노드 → 프로세스 분리.
- `SpotService` e2e Client: `EdgeSpotNode` + `EdgePublisherNode` 2노드 → 분리.
- `SupportChat` 샘플: `SupportSpotNode` + `SupportConversationSpotNode` 2노드 → 분리.
- **단일 노드 프로세스**(Bingo Play, GameQuest QuestMission, ShoppingMall OrderWorkflow,
  DeliveryDispatch, 각 언어 Monitoring e2e 등 다수): **프로세스 분리는 불필요**하나, `AddNode`
  호출 제거 + 노드 이름·RoutingId를 `AddSpotMesh` 설정으로 옮기는 **API 마이그레이션은 전 언어
  모든 `AddNode`/`addNode` 호출처에 필요**(샘플·e2e 전수).

→ Q9를 거부하고 다중 노드를 유지하려면 full-implicit가 불가능(owner 모호)하고 channel→node
명시 링크(codex B)가 필요하다. **A를 고른 이상 Q9가 일관된 선택**이라 권고한다. (되돌릴 경우
Q7을 B로 재검토.)

## 메시지 흐름 정의 (용어 고정)

| # | 방향 | 시작 주체 | 쓰는 API | 쓰는 소켓 | 이름 모호성 |
|---|------|----------|---------|----------|-----------|
| 1 | spot → spot | spot | `outbound.SendToSpot/RequestToSpot(spotRid)` | SpotNode router(mesh) | 없음(spotRid) |
| 2 | spot → 일반 channel | spot | `outbound.SendToChannel/RequestToChannel(name)` | 그 채널 client | 없음(API가 타입 결정) |
| 3 | spot → publish | spot | `outbound.Publish(topic)` | SpotNode pub | 없음 |
| **4** | **외부 → spot (routed)** | **비-spot 코드** | **`routeClient.Send/Request(name, spotRid)`** | **그 RouteMesh 채널의 route bridge** | **straddle → Q8로 해소** |
| 5 | 외부 → spot (publish) | 비-spot 코드 | `publisherClient.PublishSpot(name, topic)` | SpotMesh pub | 없음 |

**"spot route egress" = #4.** spot 바깥 코드가 routed request를 spotRid로 보내 RouteMesh 채널
bridge를 타고 spot으로 *들어가는* inbound 경로. spot→spot(#1)도, spot의 outbound(#2)도 아니다.
**target은 #4가 spotRid(`RoutingId`), #5(publish)는 SpotMesh 채널명+topic** (publish엔 spotRid 없음).

## 목표 모델 (사용자 표면)

```csharp
// === 받는 노드 (spot 호스팅 + 외부 route 수신) — 프로세스당 노드 1개(Q9) ===
builder.Services.AddZLinkFramework(options =>
{

    // 외부→spot route용 RouteMesh 채널만 설정 — accept/egress/attach 호출 없음
    options.AddRouteMesh("api").EnableServer("tcp://0.0.0.0:9001");   // (구 AddRouteMeshChannel)

    // SpotMesh가 곧 단일 노드(Q9) — AddNode 없음
    var node = options.AddSpotMesh("game.stage");
    node.EnableRouter("tcp://0.0.0.0:9101");   // spot mesh 전용(spot↔spot)
    node.EnablePubSub("tcp://0.0.0.0:9100");
    node.AddSpotFactory<StageSpot>();
    // ← AcceptSpotRoutesFromChannel("api") 없음. 같은 프로세스 RouteMesh + 단일 노드가 자동 연결.
});

// === 보내는 노드 (외부, local spot 없음) ===
builder.Services.AddZLinkFramework(options =>
{
    options.AddRouteMesh("api").EnableClient("tcp://play-node-1:9001");
    // ← EnableSpotRouteEgress 없음. routeClient.Request("api", spotRid, …) 호출이 곧 의도.
});
```

```csharp
// #4 외부 → spot : RouteMesh 채널명으로. "api"가 없거나 RouteMesh가 아니면 런타임 예외.
await routeClient.Request("api", spotRid, new GetStageStateRequest()).Async<…>(ct);
// #5 외부 → spot publish : SpotMesh 채널명 + topic.
await spotPublisher.PublishSpot("game.stage", "stage.updated", evt).Async(ct);
// #2 spot → 외부 channel : 변경 없음. 이 "orders"는 ClientServer여도 됨.
await spot.Context.Outbound.RequestToChannel("orders", req).Async<…>(ct);
```

### 핵심 규칙

1. **Colocation 자동 브릿지(RouteMesh 한정).** 같은 프로세스에 **RouteMesh 채널** + `SpotNode`
   (Q9로 단 하나)가 있으면 런타임이 그 채널 ROUTER에 route bridge를 자동 attach. 들어온 프레임은
   relay-kind로 demux — relay면 spot plane(그 노드에서 spotRid 조회), 아니면 일반 RouteMesh handler.
2. **publish 자동 부착.** 외부 코드의 `PublishSpot(meshName, …)`는 그 SpotMesh의 pub로 자동
   연결(소유 노드 = 그 프로세스의 단일 노드, Q9).
3. **호출 시점 lookup.** route/publish는 인자 채널명으로 대상을 찾는다. 없거나 타입이 안 맞으면
   (RouteMesh 아님/SpotMesh 아님) **명확한 런타임 예외**(timeout 아님).
4. **eager bridge(시작 시점, Q3).** colocated RouteMesh 채널의 bridge를 시작 시점에 붙인다
   (ingress 서버 + egress 클라이언트). lazy 경로 없음 → 첫 호출 race 없음.

### 검증 정책 (`ValidateImplicitSpotRoutes = Strict | Warn | Off`, 기본 Strict)

- **시작 시점(Strict)**: **설정만으로 아는 사실**만 검증한다 — (Q9 위반인) 프로세스 다중
  `SpotNode`, route/pub capability를 선언했는데 그 소켓(server ROUTER / pub)이 없음,
  discovery/manual 경로 부재. (호출 인자 채널명은 **동적**이라 여기서 못 본다.) Warn=로그, Off=동적.
- **호출 시점(런타임 예외)**: **호출 인자 채널명**의 미존재 / 타입 불일치(RouteMesh·SpotMesh
  아님) + 원격 도달성(대상 노드/spot 실재). 즉시 명확한 예외(timeout 아님).

## 무엇이 바뀌나 (API 델타 — dotnet 기준, 4언어 동일)

| 항목 | 현재 | 목표 |
|------|------|------|
| `EnableSpotRouteEgress` / `AcceptSpotRoutesFromChannel` / `AttachSpotPublisherClient` | 명시 필수 | **제거**(자동) |
| `AddNode(name)` | 명시 | **제거**(Q9: SpotMesh가 곧 단일 노드) |
| `AddRouteMeshChannel` / 빌더 | 이름 | **`AddRouteMesh`로 리네임**(Q6′) |
| `AddDealerMeshChannel` + 런타임·빌더·enum·테스트 | 존재 | **제거**(기능테스트 전용) |
| 외부→spot route 전송 | ClientServer DEALER + RouteMesh 둘 다 | **RouteMesh 단일** |
| client-server spot egress/ingress (`ClientEgressTarget`/server-bundle attach) | 있음 | **제거** |
| 채널 namespace | ClientServer/Fanout/DealerMesh 공유 버킷 | **타입별 분리**(이름 재사용 가능) |
| 시작 시점 accept 검증 | 있음 | `ValidateImplicitSpotRoutes`(로컬 Strict) + 원격은 런타임 예외 |
| `Outbound.*` / `IZLinkRouteClient` / `IZLinkSpotPublisherClient` | 존재 | **유지**(시그니처 불변) |
| opt-out | 없음 | **없음**(Q4 — RouteMesh 한정+demux+신뢰로 불필요. 필요 시 후일 추가) |

## 언어별 호출 인터페이스 (유지 — 시그니처 불변)

재설계는 이 호출이 "명시 와이어링 없이도 동작"하게 만드는 것이지 인터페이스를 바꾸지 않는다.

### #4 + #2/#1/#3 — spot outbound 및 route client

**dotnet** — `IZLinkSpotOutboundSink` via `Context.Outbound` (`Runtime/Spots/ZLinkSpotContextSurfaces.cs:31`):
`SendToChannel<T>(name,msg)`·`RequestToChannel<T>(name,req)`·`SendToSpot<T>(spotRid,msg)`·
`RequestToSpot<T>(spotRid,req)`·`Publish(topic,msg)` → `.Async(ct)`/`.Async<TReply>(ct)`.
route client `IZLinkRouteClient` (`Contracts/Channels/RouteCalls.cs:3`):
`Send<T>(routerChannelId, RoutingId target, msg)`·`Request<T>(routerChannelId, RoutingId target, req)`.

**java** — `ZLinkSpotOutbound` via `context.outbound()` (`.../spots/ZLinkSpotOutbound.java:8`):
`sendToChannel`/`requestToChannel`/`sendToSpot`/`requestToSpot`/`publish` → `submit(Class<TReply>)
: CompletionStage`.
route `ZLinkRouteClient` (`.../channels/ZLinkRouteClient.java:5`): `sendTo(name,RoutingId,msg)`·
`requestTo(name,RoutingId,msg)`.

**kotlin** — 전용 인터페이스 없음(java 재사용 + coroutine 확장 `awaitReply<T>()`,
`ZLinkRouteClient.send/request`), pkg `systems.zlink.framework.kotlin`.

**node** — `ZLinkSpotOutbound` via `context.outbound` (`packages/framework/src/contracts/Spots/Contracts.ts:122`),
token `ZLINK_SPOT_OUTBOUND`; route `ZLinkRouteClient` (`.../Channels/RouteCalls.ts:4`), token
`ZLINK_ROUTE_CLIENT`. 종결 `submit<TReply>(signal?) : Promise<TReply>`.

### #5 — publish client (target = SpotMesh 채널명+topic, spotRid 아님)

- dotnet `IZLinkSpotPublisherClient.PublishSpot<T>(channelName, topic, msg)` (`Contracts/Spots/Contracts.cs:96`)
- java `ZLinkSpotPublisherClient.publishSpot(channelName, topic, msg)` (`.../spots/ZLinkSpotPublisherClient.java:5`)
- node `ZLinkSpotPublisherClient.publishSpot(channelName, topic, event)` (`.../Channels/RouteCalls.ts:9`), token `ZLINK_SPOT_PUBLISHER_CLIENT`
- kotlin: java 호출 + `.submit().await()`

**naming 비대칭(Q6)**: dotnet `Send/Request` vs java `sendTo/requestTo` vs node `send/request`;
route target 파라미터 `targetNodeRid`(dotnet/node) vs `target`(java) — 의미는 모두 target spotRid.

## 코어 영향 — C-API 전체 변경 리스트 (확정)

RouteMesh 단일화(Q8) + 함수 병합으로 코어는 **신규로 늘기보다 크게 줄어든다**(현재 ~13 →
~8). `send`/`request`만으로는 부족하다 — 그것들은 **egress(나가는 방향)**이고, **ingress
(외부→spot, flow #4 받는 쪽)**는 `handle_router_received`가, **셋업**은 `new`+`attach`가 맡는다.
bridge·publisher C-API 전수(`core/include/zlink/service/spot.h:238-347`):

| C-API 함수 | 조치 | 이유 |
|-----------|------|------|
| `zlink_spot_route_bridge_new` | 유지 | bridge 객체 생성 |
| `zlink_spot_route_bridge_attach_router_channel` | 유지 | ROUTER 소켓 부착(없으면 송수신 불가) |
| `zlink_spot_route_bridge_send` / `_request` | **유지(시그니처 변경)** | egress. `target_node_rid` 파라미터 추가로 `set_target_node` 흡수 |
| `zlink_spot_route_bridge_handle_router_received` | **유지(병합)** | **ingress** demux. `request_seq`(0=없음) 받아 `_with_metadata` 흡수 |
| `zlink_spot_route_bridge_close` | **변경(동작 신규)** | detach/shutdown 시 pending-reply 취소 + generation ownership |
| `zlink_spot_node_publisher_new` / `_publish` / `_close` | 유지 | pub flow #5 |
| **`zlink_spot_route_bridge_set_target_node`** | **제거(병합)** | send/request 직전 호출 패턴 → 파라미터로 흡수 |
| **`zlink_spot_route_bridge_handle_router_received_with_metadata`** | **제거(병합)** | `handle_router_received`로 흡수(바인딩은 이미 1개로 접음) |
| **`zlink_spot_route_bridge_summary`** | **제거** | framework 미사용(monitoring 미연결, grep 확인) |
| `zlink_spot_route_bridge_drain` | **유지(흡수 검토)** | receive 루프·shutdown에서 **능동 호출 중**(`ZLinkChannelReceiveLoop.cs:82` 등) — load-bearing. handle/send 흡수는 구현 시 별도 확인, 그 전엔 유지 |
| **`zlink_spot_route_bridge_attach_dealer_channel`** | **제거** | client-server DEALER spot egress 폐지(Q8 C) |
| **`zlink_spot_route_bridge_handle_dealer_received` / `_with_metadata`** | **제거** | 〃 |

요약: **제거 6**(dealer 3 + set_target_node·handle 변종·summary) + **시그니처 변경 2**(send/request,
handle_router_received) + **동작 변경 1**(close). drain은 유지(검토). bridge ~13 → **~7**(+publisher 3).

**최소 기능 집합**: `new` · `attach_router_channel` · `send` · `request` · `handle_router_received`
· `drain`(검토) · `close` (+ publisher 3). 송신·수신·셋업·flush·종료의 최소 축.

**코어 구현 동반 제거(불필요 코드 정리)**: `service_spot_route_bridge_api.cpp`의 DEALER
attach/receive 분기 + summary/set_target_node/handle 변종 코드(drain 제외), `..._channel_reply_internal.cpp`
DEALER reply 분기, dealer 전용 endpoint capability.

### bridge 동작 (유지 — 재사용)

- 채널명 키잉 attach/send, 내부 `endpoints[channel_name]`, relay codec, demux, **외부 호출자
  transport routing-id 캡처 + pending-reply 테이블**(`...channel_reply_internal.cpp:71-141,171-241`),
  reply 모델, spotRid resolver, discovery.
- **local-only delivery**: `(own_node_rid, spot_rid)`로만 조회, miss=ENOENT, forward 안 함
  (`service_spot_route_bridge_api.cpp:160-170,205-216`). Q9(단일 노드)이라 정확한 동작 — 전달 키 변경 불필요.

### 신규 동작 (1건) — 표면 축소와 구분

- **pending-reply detach/shutdown 취소 + generation ownership.** bridge close
  (`service_spot_route_bridge_api.cpp:721-730`)는 현재 단순 삭제 — 채널 dispose/shutdown 시 pending이
  stale socket을 때리지 않도록 취소·세대 검증 추가. 코어 테스트로 닫는다.
- **새 계약을 추가하는 건 이 1건(동작)뿐.** 단 위 표면 축소(제거 6 + 시그니처 2)는 전 바인딩·
  framework wrapper에 **기계적으로 파급**되므로 "작은 변경"으로 오해하면 안 된다(작업량은 넓다).

## 작업 순서 (확정 — 각 단계 그린으로 닫기)

> 코어를 건드리면 반드시 **core 빌드 → `bindings/dev_sync_local_core_libs.sh`(빌드된 core
> 라이브러리를 bindings로 배포) → bindings 수정 → framework** 순으로 내려간다.
> **각 단계에서 그 변경으로 죽은 코드(미사용 분기·orphan 테스트·dead path)를 반드시 동반
> 제거**한다 — clean cutover의 일부다.
>
> 또한 **각 레이어(코어 / bindings / framework)의 기능 변경이 그린이 된 직후 POSD 기반
> 리팩토링 패스를 한 번 돈다**(§1R·§2R·§5R). 방식은 일관되게 — **codex 에이전트로 그 변경분의
> POSD 위반 요소를 리스트업 → 항목별 리팩토링(동작 불변) → 테스트 재그린**.

### D. DealerMesh 채널 타입 제거 (framework-level, 독립 — 먼저 가능)

C-API와 무관(코어 DEALER 소켓은 유지). namespace 분리와 묶는다. 언어별:
- 빌더·계약: dotnet `AddDealerMeshChannel`/`IZLinkDealerMeshChannelBuilder`, node
  `addDealerMeshChannel`(framework+nestjs), java `addDealerMeshChannel`. `AutoConnectType.DealerMesh`
  enum(`Contracts/Registry/Models.cs`), `IZLinkDispatchOptions`의 DealerMesh 항목.
- 런타임: `ZLinkDealerMeshPendingRequests` + DealerMesh 분기(`ZLinkChannelBundleFactory`/
  `ZLinkChannelRuntimeManager`/`ZLinkChannelRegistrationValidator`).
- 테스트: `DealerMeshReceiveLoopTests`, `BuilderContracts`/`HandlerExposure`, node `contract-surface.test.js`.
- e2e/문서: `RegistryMessaging` `profile.mesh`(RmC6) 제거(ClientServer 멀티-endpoint로 흡수),
  `common/e2e/config-1-registry-messaging`·`samples/README.md`·cpp 가이드 갱신.

### 1. 코어 변경 + 빌드

- **pending-reply detach/shutdown 취소 + generation ownership**(`...bridge_api.cpp:721-730`).
- **C-API 축소**("코어 영향" 표): dealer 3함수 제거 + `set_target_node`(→send/request 파라미터)·
  `handle_router_received_with_metadata`(→`handle_router_received` 병합)·`summary` 제거 + `drain`
  제거 검토. `send`/`request`에 `target_node_rid`, `handle_router_received`에 `request_seq` 추가.
- 구현 분기 정리(dealer attach/receive·reply, summary/drain/set_target/handle 변종), dealer endpoint capability.
- 코어 테스트(detach 중 late reply, 제거 경로 회귀). **core 빌드.**

### 1R. POSD 리팩토링 — 코어 변경분 (codex 주도)

코어가 그린이 된 직후 1패스:
1. **codex 에이전트로 리팩토링 요소 리스트업** — 이번 변경 범위(bridge 표면 축소·pending-reply·
   dealer/summary/set_target 제거·`...bridge_api.cpp`/`..._channel_reply_internal.cpp`)에서
   god-file·과결합·얕은 모듈·중복·명명/주석 등 POSD 위반을 항목화.
2. 항목별 리팩토링(**동작 불변**, 코어 테스트 그린 유지).
3. 코어 테스트 재그린 + 재빌드로 닫는다. (이후 §2 dev_sync는 이 리팩토링까지 반영된 core를 배포.)

### 2. bindings 동기화·수정 (framework 전에)

**bindings는 C-API를 1:1 미러링**한다. 따라서 "코어 영향" 표의 모든 변경을 각 바인딩 FFI 표면에
그대로 옮기는 **기계적 작업**이다. 변경 → 바인딩 조치 매핑:

| C-API 변경 | 바인딩 조치 |
|-----------|------------|
| `attach_dealer_channel` · `handle_dealer_received`(+`_with_metadata`) 제거 | FFI export·래퍼에서 삭제 |
| `set_target_node` 제거(병합) | 삭제 + 호출부를 `send`/`request` 인자로 이동 |
| `handle_router_received_with_metadata` 제거(병합) | 삭제 |
| `summary` 제거 | 삭제 |
| `send` / `request` 시그니처(`target_node_rid` 추가) | FFI 시그니처·마샬링 갱신 |
| `handle_router_received` 시그니처(`request_seq` 추가) | 〃 |
| `new` · `attach_router_channel` · `drain` · `close` · publisher 3 | 유지(시그니처 불변; close는 코어 내부 동작만 변경) |

- **순서**: `bindings/dev_sync_local_core_libs.sh` 실행(빌드된 core 배포) → 위 표 적용 → 빌드.
- **적용 대상(확인됨, C-API 직접 노출)**: c/cpp/go/java/dotnet/python/node/rust —
  `bindings/dotnet/.../SpotRouteBridge.cs`+`NativeMethods.SpotRouteBridge.cs`,
  `bindings/java/.../SpotRouteBridge.java`, `bindings/go/.../spot_route_bridge.go`,
  `bindings/cpp/.../spot_node.hpp`, `bindings/python/.../{ffi.py,spot_route_bridge.py}`, node
  `native/src/addon_spot*`, rust `spot_route_bridge.rs`. kotlin/javascript는 java/node 공유.
- 각 바인딩 샘플·테스트(`test_core_api_alignment`, `test_socket_surface`, python 샘플 등) 갱신·그린.

### 2R. POSD 리팩토링 — bindings 변경분 (codex 주도)

bindings 정렬이 그린이 된 직후 1패스:
1. **codex로 리팩토링 요소 리스트업** — 변경된 각 언어 바인딩 FFI/래퍼(spot_route_bridge 표면)에서
   god-file·반복 마샬링·얕은 래퍼·명명/주석 등 POSD 위반 항목화.
2. 항목별 리팩토링(동작 불변, 바인딩 테스트 그린 유지).
3. 바인딩 테스트 재그린으로 닫는다.

### 3. dotnet framework cutover (레퍼런스 — bindings 위에서)

- **backend wrapper를 축소된 C-API 표면에 맞춤(dealer만이 아니라 전체 반영)**:
  `IZLinkBackendSpotContracts`/`ZLinkBackendSpotRouteBridgeWrapper`에서 dealer 메서드 + `SetTargetNode`
  제거, `Send`/`Request`(`target_node_rid`)·`HandleRouterReceived`(`request_seq`) 시그니처 갱신,
  `Summary` 제거(노출 시), `Drain` 유지. 호출부 갱신: `ZLinkSpotRouteBridgeProvider`(client-server
  egress 제거), `ZLinkRouteSpotChannelCalls`(SetTargetNode→send/request 인자), receive pump
  (`ZLinkChannelReceiveLoop`/`ZLinkRouteReceivePump`)의 handle 호출.
- **RouteMesh 단일화**: `ZLinkSpotRouteEgressDispatcher`의 `ClientEgressTarget` 제거(RouteMesh만),
  `ZLinkSpotNodeInitializer.AttachAcceptedSpotRouteServerBundle`의 client-server 분기 제거.
- **egress owner resolver 재배선(codex)**: `EnableSpotRouteEgress(targetSpotNodeChannelName)` 제거로
  egress가 잃는 owner-targeting 입력을 **spotRid → 소유 노드 registry resolver**
  (`ZLinkRegistryRouteResolvers`/`resolve_spot` → RouterChannelId/TargetNodeRid)로 대체. 현재
  `targetSpotNodeChannelName` 기반(`ZLinkSpotRouteEgressResolver`·`ResolveTargetPeerRidAsync`·
  `ResolveAcceptedSpotRouteNodeRid`·`ZLinkRouteRegistrationValidator`의 TargetSpotNodeChannelName) 제거.
- **Q9**: `AddNode` 제거, `AddSpotMesh(name)`가 단일 노드(`ZLinkSpotMeshBuilder.DefaultNode` 활용,
  `IZLinkSpotMeshBuilder`/`IZLinkSpotMeshNodeBuilder` 계약 정리). 프로세스 다중 노드 = 시작 에러.
- **namespace 타입별 분리**: 공유 `Channels` 버킷 → 타입별 또는 `(type,name)` 키. **함께 갱신**:
  `ResolveChannelRequestTimeout`(41-45)·`ResolveRouteRequestTimeout`(48-52)·채널 facade/client
  lookup·validator 충돌 규칙(타입 내).
- **자동 와이어링**: initializer가 accepted 필터 제거 후 colocated RouteMesh를 단일 노드에 handoff;
  eager egress bridge(시작 시점, Q3); publish 콜-시 자동 부착.
- **리네임** `AddRouteMeshChannel`→`AddRouteMesh`(빌더 타입·계약 + 단위테스트
  `Configuration/Registration/Channels.cs`·`RegistryRemoteAddresses.cs`).
- **검증**: `ValidateAcceptedSpotRouteChannel` 제거 → `ValidateImplicitSpotRoutes(Strict|Warn|Off)`.
- **3함수 + 죽은 코드 제거**(no-op 셤 없음): 빌더·registration(`ZLinkSpotRouteEgressRegistration`,
  `AcceptedSpotRouteChannels`, `AttachedSpotPublisherClients`)·validator·initializer.
- opt-out 함수는 두지 않는다(Q4).

### 4. dotnet 테스트·샘플·문서 그린

- e2e `SpotService`(2노드→프로세스 분리, Q9)·`RuntimeMonitoring`; 샘플 와이어링·`AddNode` 호출 제거
  (Bingo는 이미 RouteMesh `PlayChannel`; SupportChat 2노드→분리); `DocumentationRegressionTests`·
  behavior-matrix·regression-test-matrix.
- 문서: `guide/05-spot`(§5 host-배선 교체)·`spec/aspnet-core-spot`.

### 5. java → kotlin → node framework cutover (언어별 원샷)

- 각 언어 PR이 자체 완결: 위 §3 전체(**backend wrapper 축소 표면 반영**(dealer+SetTargetNode 제거,
  Send/Request·Handle 시그니처) + RouteMesh 단일화 + egress resolver + Q9 `AddNode` 제거 + namespace
  + AddRouteMesh + DealerMesh + 자동 경로) + 샘플·e2e·문서. java SpotService(`ClientApplication.java`/
  `PlayApplication.java`의 `enableSpotRouteEgress`/`acceptSpotRoutesFromChannel`)·node contract test
  포함. 언어 간 일시 비대칭 허용, 마지막에 parity 수렴.

### 5R. POSD 리팩토링 — framework 변경분 (codex 주도)

**전 언어 framework 작업(§3~§5)이 완료·그린**이 된 직후 1패스(언어별 또는 일괄):
1. **codex로 리팩토링 요소 리스트업** — 변경된 framework 코드(builder·initializer·validator·
   dispatcher·egress resolver·backend wrapper·namespace 재구성)에서 god-file·과결합·얕은 모듈·
   중복·명명/주석 등 POSD 위반 항목화. (기존 POSD 리팩토링 이력 — node god-file 분해 등 — 참고)
2. 항목별 리팩토링(동작 불변, 빌드·테스트·샘플 그린 유지).
3. 4언어 테스트·샘플 재그린으로 닫는다.

### 6. Q6 naming 통일

- 호출 인터페이스 naming(`Send/Request` vs `sendTo/requestTo`, `targetNodeRid` vs `target`) 통일 결정·적용.

## 문서 변경 계획 (core/doc · framework/doc — 본 plan/draft 제외)

각 변경은 해당 언어 cutover 단계에 묶어 그 PR에서 닫는다. **`.ko.md` 먼저 → `.md` 미러 동기화**.

- **core/doc** (C-API 3함수 제거로 minimal 아님):
  - `spec/core/service/spot.{ko.md,md}` — bridge C-API에서 **dealer 3함수 제거** + endpoint 종류에서
    dealer 제거 + bridge detach 시 pending-reply 취소 계약 추가.
  - `internals/protocol-zmp.{ko.md,md}` — spot route relay 중 **DEALER 프레이밍·reply 경로 기술 제거**
    (router 경로만).
  - `guide/07-3-spot.{ko.md,md}`·`guide/07-0-services.{ko.md,md}` — route bridge 모델에서 DEALER egress
    경로 제거, RouteMesh 단일로 정리.
  - `spec/core/errno-map.{ko.md,md}` — detach 후 늦은 reply errno(필요 시).
  - `spec/core/socket/dealer.{ko.md,md}` — DEALER 소켓 자체는 유지(일반 채널). bridge 관련 언급만 점검.
- **bindings/doc** — 각 언어 바인딩 문서에서 dealer bridge 3함수 표면 제거(C-API 정렬과 짝).
- **dotnet**: `guide/05-spot`(§2 함수표·§2/§5 host 배선 교체·§7), `spec/aspnet-core-spot`(§4.2·§7),
  `spec/aspnet-core-channel-messaging`(EnableSpotRouteEgress·DealerMesh·AddRouteMesh 리네임),
  `guide/11-interface-catalog`(BuilderContracts), `guide/samples/spot-samples`,
  `internals/{behavior-matrix,regression-test-matrix}`, `spec/handler-interfaces`, `README`.
- **java**: `guide/05-spot`, `spec/{spring-boot-spot,spring-boot-channel-messaging,spring-boot-registry}`,
  `guide/samples/{spot-samples,deliverydispatch-sample}`, `internals/{behavior-matrix,regression-test-matrix}`,
  `spec/handler-interfaces`. **kotlin**: `guide/05-spot`, `guide/samples/{spot-samples,deliverydispatch-sample}`
  (spec/internals는 java 링크 공유).
- **node**: `spec/{nestjs-spot,spot-node,nestjs-channel-messaging}`.
- **cpp**: `spec/cpp-framework-interfaces`, `guide/{01-overview,03-concepts,07-channel-messaging,
  13-interface-catalog,15-feature-map,16-grpc-alternative,README}`(DealerMesh), `internals/...refactoring-log`(현행만 정정).
- **common**: `e2e/config-2-spot-service`(3함수·Q9), `e2e/config-1-registry-messaging`(DealerMesh).
- **DealerMesh 제거 문서**: `samples/README.md`, cpp 가이드 7종(위), 각 언어 channel-messaging spec.
- **namespace/RouteMesh 규칙**: 각 언어 channel-messaging spec/guide에 "타입별 namespace(이름 재사용)"
  + "외부→spot route는 RouteMesh 단일" + 메시지 흐름 5종 명시.
- **정본 승격**: dotnet cutover 완료 시 draft의 사용자 모델을 `guide/05-spot`·`spec/aspnet-core-spot`로
  흡수. draft는 설계 근거·결정 로그 archive로 남김.

## 호환성 전략 — clean cutover

deprecate-then-remove(no-op 셤)는 채택 안 한다(draft 단계, 외부 소비자 없음). 명시 함수와 와이어링
코드를 한 번에 깨끗이 들어내고 자동 경로로 재작성한다. bisect 위험은 **언어별 원샷 + 각 단계
그린**으로 흡수(셤 대신). 언어 간 일시 비대칭 허용, 마지막 언어에서 parity 수렴.

**순서는 아래에서 위로**: C-API/코어 변경이 **맨 앞**(core→`dev_sync`→bindings), 그 위에서 framework를
언어별로 cutover. **framework-내장 backend wrapper**(dotnet `ZLinkBackendSpotRouteBridgeWrapper`, java
`ZLinkJavaBackendAdapterFactory`, node `node-backend-adapter-factory`)는 그 언어 binding 표면이 갱신된
뒤 해당 framework 단계(§3/§5)에서 제거한다 — binding보다 먼저 들어내면 빌드가 깨진다.

## 리스크 & 완화

| 리스크 | 완화 |
|--------|------|
| 자동 브릿지가 의도 안 한 채널 노출 | RouteMesh 채널만 브릿지(Q8) + relay/normal demux + 서버 간 신뢰(전제2). opt-out은 v1 미도입(Q4) |
| 와이어링 누락이 런타임까지 안 드러남 | 호출 시 명확한 예외("channel 'api' not found / not a RouteMesh"). timeout 금지 |
| Q9가 다중-노드 배포 차단 | 단일-노드가 지배적. 다중 필요 시 channel→node 링크(codex B) 재검토 |
| 모든 inbound 프레임 demux 비용 | relay-kind 디코드 frame 0 1회 — 측정·이름 충돌 점검 |
| pending-reply가 detach 중 stale socket | 신규 코어 1건(취소+generation) |
| C-API 표면 축소·시그니처 변경의 전 바인딩·4언어 파급 | 신규 *동작*은 1건(pending-reply)이나 *표면*은 크게 축소(제거 6 + 시그니처 2). 순서 체인(core→dev_sync→bindings→framework) + 각 단계 그린으로 흡수. 바인딩은 C-API 1:1 미러라 기계적 |

## 결정된 (구 미해결 질문)

- **Q3. (결정 — eager, lazy 폐기)** lazy egress enable을 두지 않는다. RouteMesh ⟺ spot-route 가능
  (Q8)이므로, colocated RouteMesh 채널의 bridge를 **시작 시점에 eager attach**한다(ingress 서버
  소켓 + egress 클라이언트 소켓 모두). 첫 호출 race 자체가 사라진다. (잔존 GetOrCreate 경로가
  필요해도 기존 `ZLinkSpotRouteBridgeProvider`의 락 기반 GetOrCreate가 idempotent.) → 핵심 규칙의
  "lazy egress" 표현은 "eager(시작 시점)"로 정정.
- **Q4. (결정 — v1 opt-out 없음)** `DisableSpotRouteIngress()`를 두지 않는다. **RouteMesh 채널만**
  자동 브릿지(Q8) + relay/normal demux(비-spot 프레임은 일반 route handler로) + 서버 간 신뢰(전제2)
  로 노출이 이미 무해 수준이다. 비-spot RouteMesh 채널이 auto-bridge돼도 relay 패킷이 오지 않으면
  영향 없음. 실제 "이 RouteMesh는 spot relay를 절대 받으면 안 됨" 요구가 생기면 그때 채널 단위
  opt-out 추가. → 미검증 API 표면을 미리 만들지 않는다.
- **Q5. (결정 — 에러 유지)** spotRid는 전역 유일 UUID라 **소유 노드가 정확히 하나**다. 따라서
  egress의 spotRid→owner resolver가 후보 2개+를 반환하면 그건 **registry 불일치(두 노드가 같은
  spotRid 주장)** 이므로 **에러가 정답**이다(명확한 진단 메시지). 현 동작 유지.
- **Q6. (결정 — 언어 idiom 보존, 강제 통일 안 함)** 프로젝트 원칙
  (`feedback_api_parity_vs_language_idiom`)상 method naming 차이(dotnet `Send/Request` vs java
  `sendTo/requestTo` vs node `send/request`)는 **언어 특성 차이지 능력 갭이 아니다** → 강제 rename
  하지 않는다. 단 route target 파라미터(`targetNodeRid`/`target`)는 **문서에서 일관되게 "target
  spotRid"로 표기**해 의미만 통일한다(시그니처는 언어 idiom 유지).

## 재검토 — 단일 노드(Q9)의 근거 정정 + 다중 노드 대안 (2026-06-24 추가)

> **이 단락은 Q2/Q9의 *정당화*를 정정한다.** 앞에서 단일 노드를 "owner-selection 이 자명해진다
> (모호성 제거)"로 정당화했는데, 검토 결과 **그건 과장**이다. 근본적 모호성은 없다. 단일 노드는
> *정합성* 때문이 아니라 *구현 비용*을 줄이려는 단순화다. (결정 자체를 뒤집는 게 아니라 근거를
> 정직하게 다시 적는다.)

### 어디서도 근본적으로 모호하지 않다

| "모호"라 했던 것 | 실제 — 모호 아님 |
|------------------|------------------|
| 외부→spot ingress: 어느 로컬 노드 spot? | **송신 측 resolver 가 spotRid→소유 노드 rid 로 이미 라우팅**(`ResolveSpot(spotRid).OwnerNodeRid`) → 받는 bridge 는 자기 노드 local 전달이 정답. 모호 아님 |
| actor relay: 어느 노드 actor? | **bind 된 ref 가 actor 의 NodeRid 를 운반** → 그 노드로 |
| ActorGateway return 입구: 어느 로컬 노드? | 로컬 노드 **아무거나** mesh router 면 됨(rid 라우팅) → 기본 1개 자동 선택, 필요 시 이름 override |

core mesh 는 RoutingId 로 라우팅하므로 **로컬 노드와 원격 노드를 다르게 취급하지 않는다.** 채널은
(타입+이름)으로 여러 개 등록되는데 spot 노드는 **전역 유일 rid** 라 오히려 더 깔끔하게 구분된다 —
**다중 채널은 허용하면서 다중 노드만 막는 것은 비일관**이다.

### 코드 검증 결과 (2026-06-24) — 다중 노드 route ingress 는 **framework-only**(core 불변)

코드를 추적해 확인했다. **core 변경은 필요 없고, gap 은 전부 framework 에 있다.**

- **egress(송신)는 이미 됨**: dispatcher 가 `routeChannel.Discovery.ResolveSpot(targetSpotRid).OwnerNodeRid`
  로 **spotRid→소유 노드 rid 를 동적 해석**해 그 노드로 보낸다(`ZLinkSpotRouteEgressDispatcher.cs:103-110`,
  core `discovery_registry_client.cpp:64,191`). 옛 static `EnableSpotRouteEgress(target)` 인자는 이미 없음.
- **core bridge 는 이미 정확**: relay 가 owner 노드에 도착하므로 bridge 가 자기 노드 local 조회
  (`find_spot_state_by_identity(own_node_rid, spot_rid)`, `service_spot_route_bridge_api.cpp:160-168`)
  하는 게 **정답**이다. → **cross-node 전수 조회(이전에 적은 "Choice β")는 불필요.** relay 봉투엔 node rid
  가 없고 spotRid 만 있다(`...codec_internal.cpp:49-68`) — owner 라우팅을 송신이 책임지기 때문.
- **진짜 gap = framework 2곳**:
  1. **single-owner 가드** — 프로세스에 router-가능 spot 노드가 2개 이상이면 시작 시 throw
     (`ZLinkRouteChannelInitializer.cs:88-92`). 다중 노드 자체를 막는다.
  2. **한 채널→한 bridge→한 노드** — route channel 런타임이 `_spotRouteBridge` 하나만 들고
     (`ZLinkRouteChannelRuntime.cs:21,84-95`), 그 채널 inbound 를 그 bridge 로만 보낸다. relay 에 node rid
     가 없으니, **공유 채널 하나는 한 노드만** 서빙 → 다른 노드 spot 은 그 채널로 ENOENT.

### 다중 노드를 지원하려면 (framework 작업만)

1. **노드별 route channel/bridge**: 각 노드가 자기 route channel(router 소켓+routing id+bridge)을 갖게 한다.
   relay 가 node rid 를 안 실으니, "채널 ↔ 노드"가 1:1 이어야 inbound 가 owner 노드 bridge 로 간다.
   egress 의 owner-node-rid → 올바른 per-node 채널 매핑이 discovery 로 풀려야 한다.
2. **single-owner 가드 해제**(`ZLinkRouteChannelInitializer.cs:88-92`) + 빌더 가드 완화
   (`AddSpotMesh` 다회 / `AddNode` 부활, `ZLinkFrameworkOptionsBuilder.cs:153`).
   actor 위치는 bind ref 가 운반하고 actor relay 의 cross-node 는 core 가 이미 지원(분리 토폴로지가 증거).
4. 4언어 빌더·initializer 동일 적용.

**핵심**: ~~core bridge cross-node 조회~~ 는 **불필요**(검증으로 철회). `.cpp` 변경 0. 전부 framework.

### 정리 / 결정 상태

  `AddSpotMesh(name)`를 여러 번 호출해 노드별 route channel/bridge를 갖게 하고, core/bindings 는 바꾸지 않는다.
- Q9(프로세스당 단일 노드)는 **모호성 제거가 아니라, 위 framework 작업(노드별 채널·가드 해제)을
  생략하려는 단순화**다. (이전에 적은 "core 구현 생략"은 틀렸음 — core 는 어차피 안 바뀐다.)
- 다중 노드 허용은 **core 불변 + framework 작업만**으로 되고 **다중 채널과 일관**된다.
- → Q9 의 명시 대안으로 **"다중 노드 + 위 framework 작업"** 을 남긴다. 어느 쪽으로 갈지는 별도 결정
  (단일=빨리·단순 / 다중=프로세스당 다역할·채널과 일관). codex 초기 리뷰의 "capability anchor 유지"
  권고와 같은 결의 트레이드오프다.

## 관계 문서

- `spot-route-channel-bridge-plan.ko.md` — **부분 supersede.** core bridge 객체·relay·pending-reply
  return-path 모델 유지; "명시 attach 트리거"만 자동으로 교체. **작업 필요**: 이 draft가 dealer
  C-API(`attach_dealer_channel` :203-207, `handle_dealer_received` :247-252, API 표 :273-283, C++
  builder :656-664)와 제거/병합 대상 함수를 아직 정본처럼 기술 — 본 재설계의 C-API 축소에 맞춰
  **갱신 또는 archive(supersede 표기)** 한다. (draft라 "문서 변경 계획" 범위 밖이므로 여기에 별도 명시.)
- `spot-publish-data-plane-ingress.ko.md` — 병행. publisher handle(`zlink_spot_node_publisher_*`)
  유지, 여는 트리거를 콜-시 자동으로.
- `framework-route-resolvers.ko.md` — 병행. spotRid→노드 resolver 유지(egress 시 암묵 호출).
- `auto-connect-channel-types.ko.md` — 병행. DealerMesh 제거(Q8) 외 auto-connect 타입·socket role
  검증 유지.
