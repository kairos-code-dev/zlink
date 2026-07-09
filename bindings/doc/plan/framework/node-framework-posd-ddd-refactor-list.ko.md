# `framework/languages/node` POSD·DDD 리팩토링 수정 목록

> 2026-07-09 `framework/languages/node/packages` 전수 리뷰 **병합 정본**이다. 두 리뷰를 합쳤다:
> (1) 8-에이전트 read-only 병렬 POSD/DDD 전수 리뷰(A~D 트랙, 입자 단위 dead·결함·중복·god-file) +
> (2) codex 병렬 리뷰(R1~R6 우선순위 P0~P2 아키텍처 책임 분리 + `tsc --noUnusedLocals --noUnusedParameters`
> 컴파일러 검증 dead 목록 + 실행 순서·검증 게이트). 겹치는 항목은 통합했다(R1=§C1, R2=§D3, R3=§D1, R4=§D2 route-ops, R5=§D7, R6=§C/§D backend).
> 대상은 core 바인딩(`bindings/node`)과 **별개**인 Node **framework** 코드다:
> `@zlink-systems/framework`(Contracts+Runtime, 108파일 ~25.9k줄) + 위성 6패키지
> (`@zlink-systems/nestjs` 단일 2.9k줄, `@zlink-systems/http-client`, `@zlink-systems/stream-connector`,
> `@zlink-systems/framework-codec-msgpack`, `@zlink-systems/framework-codec-protobuf`,
> `@zlink-systems/framework-locations-redis`). 파일:라인은 리뷰 시점 기준이므로 편집 전 현재 코드로 재확인한다.
> 정본 대조 기준은 `dotnet-framework-posd-ddd-refactor-list.ko.md`(.NET framework)이며 node는 대부분 동형(isomorphic) 결함이거나 이미 더 단순하게 해소돼 있다(§부록 A).

## dead-code 판정 규칙 (중요 — 최초 리뷰에서 오탐 발생, 정정함)

`framework/src/index.ts`는 `export * from './contracts'` 한 줄이고 `package.json`의 `exports`도 `"."` 하나뿐이다.
그러나 **`runtime/**`의 런타임 클래스는 "죽은 코드"가 아니다.** 두 개의 비-`exports` 진입점이 실사용된다:

1. **`framework/src/internal.ts`** — `packages/framework/dist/internal`로 컴파일되어 **33개 계약 테스트
   (`test/contract/*.test.js`)가 `require('.../dist/internal')`로 직접 소비**한다. 즉 `runtime/**`의 대부분 클래스는
   `internal.ts` 배럴을 통해 계약 테스트가 `new framework.X(...)`로 구동하는 **테스트 커버 내부 표면**이다. `internal.ts`는 live.
2. **`framework/src/nest-integration.ts`** — nestjs가 런타임에 `requireFramework(.../dist/nest-integration)`로 deep-require하는 큐레이트 배럴. live.

**→ dead 판정은 반드시 (a) `packages/**/src` `.ts` grep + (b) `test/contract/*.js`·`e2e`·`samples`의
`framework.X`/`internal.X` 사용 + (c) `tsc --noUnusedLocals --noUnusedParameters` 세 가지를 모두 확인한다.**
`tsc --noUnusedLocals --noUnusedParameters`(§A1)가 **가장 안전한 declaration-level 신호**다 — 컴파일러는 export된
심볼이 테스트에서만 쓰여도 unused로 잡지 않으므로, 이 목록에 오르는 것은 전부 진짜 미사용(import/private/local/param)이다.
같은 이유로 **god-file 분해는 "테스트 churn 없는 순수 code-motion"이 아니다** — `dist/internal`/`dist/nest-integration`
export 표면을 유지하고 33개 계약 테스트를 통과시켜야 한다.

**위험 표기:** **없음**(control plane, 빌드+타입체크+계약 테스트로 충분) / **code-motion**(코드 이동·의미 동일, export 표면 유지 필수) /
**벤치**(per-message/dispatch/reply/location-read hot 경로, baseline vs patched 무회귀 증명 필수).

**가드레일(변경 금지 불변식):**
- public 계약(`contracts/**`, nestjs 데코레이터/모듈/토큰 표면)과 `dist/internal`·`dist/nest-integration` export 심볼 유지. "다른 언어에 있다"만으로 삭제/추가 금지.
- 라우팅/조인/브릿지 원시연산은 native 위임(`spot_node_actor_join_spot`·`spot_route_bridge_*`·`sendBoundSession`)으로 정상 — TS 재구현 아님.
- codec 불변식: framework=JSON codec 기본 내장(control-plane JSON framing은 의도된 설계, 버그 아님), protobuf/msgpack=독립 배포 extension, session/joinspot 메시지만 직접 Message(불투명 바이트 보존).
- `stream-connector`는 의도적으로 framework 무의존 독립 패키지(§C1은 "포맷 지식 게이트"로 접근, npm public surface 신설 금지).

---

## 0. 우선순위 맵 (codex R1~R6 ↔ 상세 항목)

| ID | 제목 | 우선순위 | 상세 |
|---|---|---|---|
| R1 | stream wire codec 중복(framework↔stream-connector) | **P0** | §C1 |
| R2 | `ZLinkFrameworkRuntimeHost` 책임 분리 | **P0** | §D3 |
| R3 | SPOT runtime(`spots/index.ts`) 책임 분리 | **P0** | §D1 |
| R4 | Route/SPOT dispatch 전략 분리(`ZLinkChannelRuntimeManager`) | P1 | §D2(route-ops) + §C2/§C6 |
| R5 | NestJS 통합 파일 분할 | P1 | §D7 |
| R6 | native backend adapter Proxy 축소 | P2 | §D-보조 + §C(backend) |
| — | 선언 단위 미사용 정리(tsc) | 선행 | §A1 |

권장 실행 순서(codex): ① §A1 미사용 선언 소커밋 제거 → ② R1(§C1) → ③ R2(§D3) → ④ R3(§D1) → ⑤ R4(§D2) → ⑥ R5(§D7) → ⑦ R6. 결함(§B)은 발견 즉시(수신 정지·누수 우선).

---

## A. 삭제 트랙

### A1. 선언 단위 dead — `tsc --noUnusedLocals --noUnusedParameters` 컴파일러 검증 (없음)

가장 안전한 삭제 트랙. 아래는 `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
출력(리뷰 시점). 파일 삭제가 아닌 **선언 단위 정리**이며 public re-export·테스트 internal 표면을 깨지 않는지 확인 후 제거.

- **contracts:** `Configuration/RegistrationValidators.ts:5` `ZLinkChannelOptions` import · `Spots/Contracts.ts:2` `ZLinkRequestCall` import.
- **actors:** `actor-client.ts:95` `resolveActor` 메서드 · `index.ts:550` `requireJoinCoordinator` 메서드 · `index.ts:1331` `ZLinkActorDispatchRouter`의 `options` 생성자 property(⚠️ **클래스 자체는 test-live**, property만 dead).
- **backend:** `node-backend-adapter-factory.ts:1085` `toNativeTopologyFilter` · `:1095` `toFrameworkRoutingIdEntries`.
- **channels:** `:19` `Type` · `:30` `effectiveMessageFlow` · `:31` `errorLine` · `:33` `writeTraceFile` · `:42` `ZLinkMessageFlowEvent` · `:96` `ZLinkLocationWriteIntent`(imports) · `:454` `ctx` param · `:465` `errorSink` 생성자 property · `:520` `formatDispatchErrorEvent` 함수 · `:1686` `routerChannelId` param.
- **host:** `:11` `ZLinkConfigurationException` · `:16` `Type` · `:27` `ZLinkSpotActorJoinResponse`(imports) · `:2481` `formatErrorMessage`.
- **locations:** `:439` `now` param · `:1620` `actorType` param · `:1631` `actorType` local · `:1716` `actorType` param.
- **spots:** `:100` `ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_ONLY` import · `:165` `tryGetStreamFrameHeader` import · `:346` `ZLinkRemoteActorJoinRequest` interface · `:779` `spotNodeName` param · `:824` `isStreamSessionRelayNode` · `:1545` `receivedFromDispatchEvent` local(=drainRoutes 내부, §A2).
- **streams:** `:37` `ZLinkConfigurationException` import · `:309` `routingId` 생성자 property · `protocol.ts:2` `ZLinkConfigurationException` import.
- **nestjs:** `:5` `Inject` import · `:2883`/`:2885`/`:2886` `createSpotOutbound`의 `registration`/`moduleRef`/`discovery` params.

### A2. 무참조 exported 심볼 — src + `test/contract` + tree 교차확인 완료 (없음)

각 항목 `test=0`(계약 테스트 미사용) 확인. 삭제 시 `dist/internal` 배럴에서도 제거.

- **spots `drainRoutes` 사슬 도달 불가(~80줄):** `runtime/spots/index.ts:1541-1595` `drainRoutes`(유일 호출처가 자기 자신 `:1578` setTimeout, routed 처리는 `dispatchRoutedFromEvent`로 감) + `closeReceivedQuietly`(`:2667-2672`, `test=0`). **주의(정정):** `nativeSpot.recvRoute`는 계약 테스트가 직접 구동(`test=12`)하므로 사슬에서 제외·유지. `isRouteRecvRetryable`은 spots-local과 backend-adapter(`node-backend-adapter-factory.ts:500`)에 동명 2개 — backend판 유지, spots-local판만 drainRoutes와 함께 판단(재확인).
- **channels `formatDispatchErrorEvent`/`formatOptionalDispatchField`(`:520-538`)** — `test=0`, 죽은 dispatch-error 포매터.
- **raw spot-to-spot 사슬:** `ZLinkRuntimeRouteTransport.requestRawFromSpotToSpot`(`channels:290-303`) → `ZLinkChannelRuntimeManager.routeRequestRawFromSpotToSpot`(`channels:1474-1505`) → actor 계약 옵셔널 멤버 `requestRawFromSpotToSpot?`(`actors:158`), `test=0`. **주의:** 단방향 `requestRawToSpot`(non-FromSpot)은 live — 삭제 금지.
- **spots 기타:** `isStreamSessionRelayNode`(`:824`, A1 중복), `ZLinkRemoteActorJoinRequest` interface(`:346`, A1 중복).
- **⚠️ 정정(dead 아님):** `spotRouteResolver` 배선은 **live**다 — `host:595 createSpotManagerOptions`가 `createLocationSpotRouteResolver()`로 주입 → `spots:547 initializeEntrySpot`가 entry spot activation에 전달, nestjs(`:2873`) 보존, 계약 테스트 `location-host.test.js:94,100`이 `spotOptions.spotRouteResolver.resolve(...)`를 직접 assert. 삭제 후보에서 제외.

### A3. contracts 죽은 계약 — **전부 public 계약 표면**(⚠️ test=0만으로 삭제 불가, parity/deprecation 승인 후에만)

아래는 전부 `src/index.ts:1 export * from './contracts'` → `contracts/index.ts:5` → `Configuration/index.ts:1-3`(및 Handlers 배럴)로 재-export되는 **public `@zlink-systems/framework` 계약**이다. tree 전체 미참조(test=0)이나 **public이므로 즉시 삭제 금지** — 공통 spec/guide parity 검토 + deprecation/계약 break 승인 후 제거([[CLAUDE.md]] "Framework public contract parity"). "미참조 public 계약" 목록으로만 관리.

- **public 미참조 제거 후보(parity 확인 후):** `contracts/Configuration/Configs.ts`의 4타입 `ZLinkRouteConfig`(`:14`)/`ZLinkOutboundRouteConfig`(`:19`)/`ZLinkSpotPublisherConfig`(`:24`)/`ZLinkSpotSubscriberConfig`(`:28`)(`ZLinkSocketConfig`:3은 live, 파일 유지). `contracts/Configuration/Builders.ts:32` `ZLinkMetadataPolicyBuilder`(배선 없는 고아 계약). `contracts/Handlers/IZLinkChannelHandlers.ts:22` `ZLinkRouteSendHandler`(형제 `ZLinkRouteRequestHandler`는 live). `contracts/Configuration/Connections.ts`(`ZLinkEndpointConnections` — 파일 전체가 미참조 public).
- **정리 대상 아님(정정 — 삭제 금지):** `runtime/configuration/index.ts`(죽은 배럴로 보였으나 정리 낮은 우선), `runtime/codecs/index.ts`(`DefaultZLinkCodecRegistryBuilder`는 `channel-client.test.js:1925`에서 test-live).

### A4. ⚠️ 최초 리뷰 오탐 정정 — 아래는 **dead 아님**(계약 테스트가 `dist/internal`로 구동)

명시적으로 삭제 대상에서 제외한다(향후 재-리뷰의 재발 방지용 기록):

- `framework/src/internal.ts` — 33개 계약 테스트의 entrypoint. live.
- `runtime/codecs/index.ts` `DefaultZLinkCodecRegistryBuilder` — `channel-client.test.js:1925`. live.
- `ZLinkDealerChannelClientTransport`(`channels:2317`) — `message-packet-name.test.js:26`, `channel-client.test.js:265,269`. live.
- `ZLinkActorDispatchRouter`(`actors:1326`) — `entry-spot-serial-dispatch.test.js:41`, `actor-manager.test.js:502,519`. live(단 `options` ctor property는 dead, §A1).
- `ZLinkLocationReadiness`(`locations:1106`) + `IZLinkLocationReadiness` — `location-runtime.test.js:97,107,114`. live.
- `exposeZLinkHandlers`/`scanZLinkHandlerTypes`/`ZLinkHandlerDescriptor`/`ZLinkHandlerExposurePolicy`(`handlers/index.ts:10-46`) — `handler-runtime.test.js:8,23,26`. live.
- `ZLINK_FRAMEWORK_ERROR_KIND_VALUES`(`contracts/Errors:41`) — `test=1`. live.
- `ZLinkActorRefSnapshot`+`zlinkActorRefSnapshotFrom`/`ToActorRef`(`contracts/Common/ActorRef.ts:17,23,31`) — `test=3`. live.

**A 착수 순서:** A1(컴파일러 검증, 소커밋) → A2(무참조 exported, `dist/internal` 동반 제거) → A3(계약, parity 확인 후).

---

## B. 결함 수정 (correctness — 리뷰 중 발견)

- [ ] **B1. route 디스패치 `requestSeq === null` throw가 수신 루프를 죽임** (correctness, 중)
  - `runtime/channels/index.ts:3071`(seq 검사)/`:3082`(report+throw) — `ZLinkRoutePacketDispatcher.dispatch`가 request인데 seq 없으면 report 후 `throw`. `ZLinkRouteReceiveLoop.runLoop`이 `await task`(`:3220`)로 throw를 받아 **루프 종료** → 잘못된 프레임 하나로 라우트 채널 수신 정지(DoS성). **비대칭 원인 정정:** 채널 dispatcher도 `:2552`에서 동일하게 throw하지만, `ZLinkChannelReceiveLoop.runLoop`은 task를 `:2674`에서 시작하고 `:2676`에서 **await 없이 계속**(fire-and-forget 스케줄링)이라 unhandled rejection에 그침 — dispatcher 라인 차이가 아니라 receive-loop의 await 여부 차이. dotnet B4 계열. skip-and-continue(report+close)로 전환.
- [ ] **B2. Location `AlreadyOwned` claim이 rollback 없이 재-activate로 낙하** (correctness, 낮은 신뢰)
  - `runtime/locations/index.ts:1533-1552` `executeActorClaimThenActivate` — `claimActor`가 `AlreadyOwned` 반환 시 switch가 `Conflict`만 조기 반환, `AlreadyOwned`는 `Claimed`와 동일하게 `activate()` 진입(`:1544-1545`). rollback arm `if (claim.status === Claimed)`(`:1547`)에 `AlreadyOwned` 미포함. 도달: `actors/index.ts:243` `createActor` → 이미 소유 중 actor에 이중 create 위험. dotnet B8 동형. `AlreadyOwned` arm(기존 반환/no-op) 명시.
- [ ] **B3. stream-connector WebSocket 핸드셰이크 실패 시 소켓 누수** (없음)
  - `stream-connector/src/Runtime/Transport/NodeSocketConnector.ts:64` `connectWebSocket`이 소켓 연결 성공 후 `completeWebSocketHandshake` 호출하나 `WebSocketHandshake.ts` 실패 경로(`:33,48,62,75,89,93`)가 `socket.destroy()`를 안 함 → 핸드셰이크 실패 시 열린 TCP/TLS 소켓 방치. (TLS/TCP 직결은 `connectSocket` error 리스너로 정리돼 안전.) try/catch → 실패 시 destroy. dotnet B3 동형(WS 한정).
- [ ] **B4. actor reply 옵션(metadata/compress)이 전면 무시됨** (벤치, silent no-op)
  - `runtime/actors/index.ts:1416-1436` `DefaultZLinkSpotActorReplyOptions.metadata()`/`compress()`는 public 계약(`ZLinkSpotActorReplyOptions`) 구현이나 결과를 읽는 `snapshot()`(`:1430`) 소비처 0. 실제 응답 전송부(`spots/index.ts:3052-3059`, `~4038`)는 metadata에 `new Map()`, compression에 `undefined`를 하드코딩 → 액터 핸들러가 지정한 응답 metadata/압축이 조용히 유실. `dispatchRequestThen`이 `snapshot()`을 actorResponseSender로 배선하거나, 미지원이면 계약 분리(draft 후보).
- [ ] **B5. Spot HandlerNotFound 동일 예외 이중 생성** (없음, 정보 누출)
  - `runtime/spots/index.ts:2953-2965` 및 `3953-3965` — 동일 `ZLinkFrameworkException(ActorDispatchHandlerNotFound, …)`를 `actorErrorSender`용·`throw`용으로 두 번 생성. dotnet B6 동형. 1회 생성 후 양쪽 전달.
- [ ] **B6. native actor-join `admit` reply Message 수명주기 미확정** (없음, 저신뢰)
  - `runtime/spots/index.ts:1462-1502` — accepted 경로가 `encodeFrameworkPayloadMessage`로 native Message를 만들어 submit 후 `reply.close()`가 없음(request/actorCreateRequest만 close). submit 소유이전 여부 확정 필요([[feedback_ownership_all_langs]]).
- [ ] **B7. `closeActorBoundSession`/`destroyActor` signal 드롭** (없음)
  - `runtime/backend/contracts/index.ts:352`가 `signal?: AbortSignal` 선언하나 `node-backend-adapter-factory.ts` Proxy가 미-래핑 → native `(actor, timeoutMs)` 직결, signal 미도달. `streams/index.ts:1134`는 await하므로 sync-over-async는 아니나 close 중 취소 무시. dotnet B9 동형(경미).
- [ ] **B8. http-client 2xx 빈 바디에서 `submit<T>` decode 실패** (없음, 저신뢰)
  - `http-client/src/request-builder.ts:198-208` — 204/304/빈 200에서 `JSON.parse('')` throw → 성공 응답이 `PayloadDecodeFailed`로 뒤집힘. C++ `submit<T>` 계약(빈 바디 null 허용 여부) 대조 후 short-circuit.
- [ ] **B9. nestjs 빌더 순서 함정 + config 검증 대칭성 구멍** (없음, 낮은 신뢰)
  - `nestjs/src/index.ts:1006-1013` `enableRouter`가 `this.spotOptions.router = {...}`로 통째 덮어써 선행 `connectRouter`(`:1015`)의 `manualConnections`/`manualPeerConnections` 유실(`enablePubSub`/`connectPeerPub` 동일). spread 병합. `Registration.ts:817-824` `enableRouter`↔`routingId` 순서 의존, `RegistrationValidators.ts:284` `connectRouter` peerRid 중복 미검증(spotFactory 등은 throw).

**B 착수 순서:** B1 → B3 → B5/B6 → B4(벤치) → B2/B7/B8/B9.

---

## C. 구조 통합 — 지식 중복 소거

- [ ] **C1. ⭐(R1, P0) stream wire 포맷이 core Streams와 stream-connector 위성에 통째 중복** (없음, 최대 유지보수 impact)
  - 바이트 레이아웃 쌍둥이: 6B prefix(`protocol.ts:54-66` ↔ `stream-connector/.../ZlinkStreamFrameCodec.ts:5-30`), 헤더(`protocol.ts:70-179` ↔ `ZlinkStreamHeaderCodec.ts:22-142`), TLV metadata(`protocol.ts:284-356` ↔ `ZlinkStreamMetadataCodec.ts:4-74`), LZ4 pickle(`protocol.ts:239-448` ↔ `Compression/ZlinkStreamCompressionCodec.ts:68-192`, 주석까지 동일), BE 헬퍼(`protocol.ts:450-479` ↔ `ZlinkStreamSupport.ts:51-102`), enum 쌍둥이.
  - **진행형 divergence(잠재 비대칭):** connector `validateHeaderSemantics`(`ZlinkStreamHeaderCodec.ts:169-189`)는 중복 metadata 키를 **거부**(`ZlinkStreamMetadataCodec.ts:63`)하나 core `protocol.ts`는 조용히 덮어씀 → 한쪽이 받는 프레임을 다른 쪽이 거부 가능. stream-connector 단위 테스트 0(현 검증은 e2e `SpotService` round-trip뿐).
  - **방향(codex R1):** wire codec을 소유하는 내부 모듈 1개로 분리 → framework bound-session relay와 connector client가 같은 encode/decode 호출. 오류 타입은 각 패키지 경계에서 감싸고 wire codec은 "어떤 바이트가 유효한가"만 판정. **주의:** stream-connector public API 신설 금지 — workspace 내부 구현 모듈로 시작, 공유 모듈의 npm 노출 여부는 별도 spec/draft. 최소 착지: 두 코덱 상호 참조 주석 + 바이트 레이아웃 단일 spec 왕복 테스트(양측 인코드→상대 디코드). dotnet C1 동형.
- [ ] **C2. ⭐decode→dispatch→report 상태기계 다발(channels 5벌 + spots 2벌)** (벤치, per-message)
  - channels: `ZLinkChannelRequestDispatcher.dispatch` command(`:2474-2515`)/request(`:2516-2623`), `ZLinkChannelPublishDispatcher.dispatch`(`:2751-2835`), `ZLinkRoutePacketDispatcher.dispatch` command(`:3025-3065`)/request(`:3067-3141`). 동일 골격(envelope decode → packetName 검증 → `traceFlow(Received)` → handler lookup → missing report[+reply] → payload decode + filter + handler try/catch → `traceFlow(Dispatched/Replied)` → catch report). `(surface,kind,reason,action)` 튜플 + flow-guard ~15회 복붙.
  - spots: `ZLinkEntrySpotActivation.dispatchActorPacketInsideMailbox`(`:2875-3095`) ≈ `DefaultZLinkSpotManager.dispatchActorPacket`(`:3883-4093`) ~220줄 near-verbatim(차이는 actor 해결 소스·spotRid 소스뿐). error-report 튜플이 spots에서 26회, `flowIfEnabled` 10회.
  - kind/surface/reply-strategy 매개화 dispatch core 1개 + actor/spotRid 훅 주입. dotnet C2 동형.
- [ ] **C3. dispatch reporter/tracer 파이프라인마다 중복 생성** (벤치)
  - `runtime/channels/index.ts:791,813,845` — command/publish/route 루프가 각각 `createDispatchErrorReporter`(→ `ZLinkDispatchErrorReporter`+`ZLinkMessageFlowTracer` 신규) 별 인스턴스 생성. `outboundFlow()`(`:915-923`)가 4번째 tracer. host도 `:233`(start)·`:704`(spotManager) 각각 new reporter. 1회 생성 후 공유 주입(+ B1 항목의 live-mode cell 단일화: `:888,918` cell 독립 생성이 런타임 토글 비대칭). dotnet C3 동형.
- [ ] **C4. actor-client 프레임 프리픽스 인라인 재구현 + response 헤더 다중 생성** (벤치, per-reply)
  - `runtime/actors/actor-client.ts:269-279` `decodeActorReply`가 `protocol.ts:54-66`의 역(6B prefix + slice)을 손코딩(`protocol.ts`엔 `decodeStreamFrame` 부재, connector와 3벌째). `protocol.ts`에 `tryDecodeStreamFrame(frame)` 추가 후 호출.
  - "request→response 헤더(correlationId echo)" 생성 다수: `actor-client.ts:119-138` + `streams/index.ts:2030-2038`·`471-482`·`983-1013`·`1394-1416`. `createResponseFrame(requestHeader, kind, payload, metadata)` factory 1개. dotnet C9 동형.
- [ ] **C5. bound-session send 프레임 패밀리 8중 복붙** (code-motion)
  - `runtime/streams/index.ts` — `sendLocalBoundSession`/`…Response`/`…Error`(`:952-1048`), `sendNativeBoundSession`/`…Response`/`…Error`(`:1051-1126`), `DefaultZLinkBoundSessionResponseTarget.sendResponse`/`sendError`(`:1387-1444`)가 `createJsonFrameMessage → token 재검사 → write/sendNativeFrame → finally frame.close()` 동형. 에러 페이로드 shape `{code:err.constructor.name, message}`도 5중. `(kind, target-writer)` 매개 헬퍼.
- [ ] **C6. (R4) Spot routed reply-submit + remote-join decode/payload 중복** (벤치/code-motion)
  - raw-vs-envelope reply-submit이 `admitRouted`(`:1617-1864`)·`dispatchRoutedSpotPacket`(`:2369-2444`)에 ~7회 → `submitRouteReplyShaped(received, envelope, payload|error)`(dotnet C13). `decodeRemoteActorJoinRequest`(`:2160-2323`) 3블록 ~160줄 동일 필드 추출 → 헬퍼. `wrapRoutedSpotRequestCall` submit/yield(`:4690-4763`), `wrapSendCall`/`wrapPublishCall` 쌍둥이(`:4563-4585`).
- [ ] **C7. remote-join 요청 payload 빌더 3벌** (code-motion)
  - `runtime/actors/index.ts` — `encodeRemoteNativeJoinRequest`(`:859-877`), `joinRemoteSpot` routed(`:907-925`)/fallback(`:955-972`)이 동일 JSON 스키마 손빌드, `boundSession*(+Hex)` 8회 반복 → `buildRemoteJoinRequestPayload(...)`. **주의:** `applyRemoteJoinResult`(`:1058-1094`)는 이미 dedup — payload 방향만 남음.
- [ ] **C8. Location mesh-scan resolver 쌍둥이 + live-row 필터 5곳 재구현** (벤치, per-read)
  - mesh-scan: `ZLinkStoreLocationResolvers.resolveSpotRef`(`:1045-1058`) ≈ `ZLinkLocationSpotRouteResolver.resolve`(`:1137-1153`)(miss 시 undefined vs throw만 다름) → 공유 헬퍼.
  - live-row 필터(owner lease live): `filterLive`(`:809-821`) + resolvers `listLivePeers`(`:1026`)/`resolveRoute`(`:1038`)/`resolveSpotRow`(`:1084`)/`resolveActorRow`(`:1096`). host가 resolver용 `ZLinkOwnerLeaseTracker`를 runtime `queryLeaseTracker`와 별도 인스턴스 생성(`host:562,799,821`) → lease 스냅샷 독립 캐시. `ZLinkLiveRowFilter` 추출 + 단일 tracker 공유. dotnet C12 동형.
  - 부수: `encodeRoutingIdHex` 중복(`locations:64-75` ≡ `key-codec.ts:51-62`), `isKnownAutoConnectType`/`isKnownLocationRole` try/catch 래퍼(`:1904-1920`)를 A1의 dead `tryParse*`로 수렴.
- [ ] **C9. (R5) nestjs discovery 4-함수 + manual/unique 헬퍼 다발 중복** (없음)
  - `discoverProviderRefs`(`:2125-2184`)/`discoverSpotProviderRefs`(`:2186-2245`)/`discoverSpotActorProviderRefs`(`:2247-2306`)/`discoverSpotTimerProviderRefs`(`:2308-2367`) — 2-페이즈 골격 ~60줄×4 = ~240줄 → 제네릭 `collectRefs<TMeta,TRef>(...)`. `createManual*Handlers` 5함수(`:2008-2078`) → 팩토리 1개. `assertUnique*Handler` 6함수(`:1777-1861`) → `assertUnique(existing, next, keyOf, label)`. `createConditionalClientProvider(ForFactory)`(`:2683-2724`) 인자-셔플 중복.
- [ ] **C10. Config 빌더 pass-through 벽 + 내부 플래그 프로토콜 분열** (code-motion)
  - `Registration.ts:743-802` `DefaultSpotMeshBuilder` 10메서드 전부 `this.node.X(...); return this` 벽(+ `Spots/Builders.ts:19` 빈 확장 interface) → `DefaultSpotNodeBuilder` 직접 반환/공유 base. dotnet D5 동형. `DefaultRouteChannelBuilder`(`:634-659`) ≈ `DefaultRouteMeshChannelBuilder`(`:661-686`) near-verbatim → 통합. `RouteMeshInternalState` non-enumerable 플래그 규약이 writer(`Registration.ts:1136-1169`)/reader(`RegistrationValidators.ts:465-484`) 2파일 분열 → 공유 모듈.
- [ ] **C11. http-client 헤더 조회 불필요 스캔** (없음)
  - `http-client/src/runtime/response-body-reader.ts:47,72-79` `findHeader` 선형 스캔인데 입력은 이미 소문자화 dict(`collectHeaders:61-70`) → `headers[name]` 직접 조회. `text.ts:7` `isBlank` 파일-로컬화. dotnet C16 잔재 1건.
- [ ] **C12. (R6) native backend adapter Proxy property-name 분기 축소** (없음/code-motion)
  - `node-backend-adapter-factory.ts:130`부터 native object를 Proxy로 감싸고 `:147-220`이 property 이름으로 backend/spot-node/route-bridge/messaging 분기. Proxy·resolver 분리(surface별)는 **의도된 설계**(가드레일)지만, property-name switch가 커질수록 어떤 native 기능이 어떤 계약으로 매핑되는지 타입 시스템이 못 보여줌(obscurity). socket/spot-node/route-bridge/monitor를 명시적 wrapper class/작은 factory로 분리, property-name switch는 호환 최소 범위로 축소. native binding parity 테스트와 함께 **마지막에**(P2). §A1의 dead 변환 헬퍼(`toNativeTopologyFilter`/`toFrameworkRoutingIdEntries`) 동반 제거 또는 실제 call path 연결.

**C 착수 순서:** C1(spec-test 게이트 먼저) → C4 → C3/C5/C6/C7 → C2/C8(hot, 벤치) → C9/C10/C11 → C12(P2).

---

## D. God-file 분해 (POSD/DDD)

리뷰 시점 대비 framework가 ~50% 성장(spots 3272→4928, channels 2516→3692, host 1340→2489, streams 1789→2228, actors 1528→1811).
공개 표면 + `dist/internal`/`dist/nest-integration` export를 배럴 re-export로 고정한 채 분해하고 **33개 계약 테스트를 통과**시킨다(§dead 규칙 — 순수 churn-free 아님).

- [ ] **D1. (R3, P0) `runtime/spots/index.ts` (4928줄)**
  - **god-class `ZLinkSpotActorJoinDispatch`(1084-2516, ~1432줄) = 최우선.** actor-join admission drain + actor lifecycle drain + actor-packet drain/no-bind reply + remote bound-session decode + remote actor-packet relay + routed actor-join + routed spot packet dispatch를 한 클래스가 소유 → `ZLinkSpotActorLifecycleDrain`/`ZLinkSpotActorPacketDrain`/`ZLinkSpotRoutedAdmission`/`ZLinkSpotRoutePacketDispatch` + 무상태 decode군 → `spot-remote-codec.ts`.
  - `DefaultZLinkSpotManager`(3190-4103, ~913줄) — spot CRUD + actor-packet dispatch(C2로 공통 core 추출 시 ~220줄 감량).
  - 무상태 유닛 즉시 추출: `spot-node-runtime.ts`(`ZLinkSpotNodeRuntimeManager` 357-655, publisher bundle/dispose), `entry-spot-activation.ts`, `spot-activation.ts`(user SPOT + location claim/release), `spot-timer.ts`(`4206-4365,4817-4891`), `spot-serial-executor.ts`(`4471-4561,4621`), `spot-outbound.ts`(`4367-4435,4563-4815`).
  - `ZLinkSpotActorJoinDispatch`의 18-인자 생성자(`:1092-1124`) = primitive-obsession → options 객체/factory(R2 Host 옵션 분리와 연결). (`ZLinkSpotNodeRuntimeManager`는 이미 단일 options 객체 생성자 `:367`이므로 이 항목 대상 아님.)
  - 검증: `spot-manager.test.js`, `entry-spot-dispatch.test.js`, `entry-spot-serial-dispatch.test.js`, `stream-runtime.test.js`.
- [ ] **D2. (R4) `runtime/channels/index.ts` (3692줄)** — 대부분 저위험 물리 분할, 디스패처/route-ops만 벤치

  | 추출 파일 | 라인 | 위험 |
  |---|---|---|
  | `channel-transports.ts` | 132-325 | code-motion |
  | `channel-socket-options.ts` | 327-446 | 없음 |
  | `dispatch-error-reporter.ts` | 448-518 (520-538 dead 삭제) | 없음 |
  | `spot-route-bridge-raw-reply.ts` | 540-639 | 없음 |
  | `channel-runtime-manager.ts` | 641-1859 | code-motion |
  | ↳ `spot-route-dispatch-strategy.ts` (route-ops) | 1030-1599 (~570줄, local/bridge/bound-router/spot-node-router/fallback 전략 통합 = R4) | 벤치 |
  | `channel-autoconnect.ts` | 1861-2045 | 없음 |
  | `channel-socket-registry.ts` | 2047-2315 | code-motion |
  | `channel-dispatchers.ts` | 2370-3181 (**C2/C3/C4 착지점**) | 벤치 |
  | `channel-receive-loops.ts` | 2638-2730,2838-2897,3183-3249 (3루프 공통 base 후보) | 벤치 |
  | `channel-clients.ts` | 3251-3580 | 없음 |

  R4 핵심: route-to-SPOT 전달 방식 선택(`routeRequestToSpot`/`routeRequestRawToSpot`/`routeSendToSpot`의 4~5분기 반복)을 `ZLinkSpotRouteDispatchStrategy` 내부 객체로 분리해 분기 수 감소. raw request reply queue는 bridge 전용 객체 소유. 검증: `YieldDispatch`/`SpotService`/`ToActorMessaging` E2E.
- [ ] **D3. (R2, P0) `runtime/host/index.ts` (2489줄)** — god-class `ZLinkFrameworkRuntimeHost`(125-1840, ~1716줄, 파일 최대 심볼). public 생성자·NestJS 사용 유지, start/stop 조정 + facade만 남김
  - **god-class 내부에서 추출**: `ZLinkBoundSessionRelay`(=RemoteActorDispatchGateway, ~880-1793, ~900줄, 원격 bound-session + actor packet relay 전량) — **벤치**(per-actor-packet hot). `ZLinkActorRuntimeOptionsFactory`(485-718, actor manager/client 옵션), `ZLinkLocationRuntimeOwner`(744-869, store/lifecycle/resolver/event sink), `MeshRouterResolver`(1764-1839).
  - **god-class 종료(1840) 이후 같은 파일의 top-level 자유 함수/클래스(클래스 메서드 아님) — 파일 단위 이동**: route-wire decoders(`sessionActorPacketTargetKey`+`decodeRemote*` 1842-2050) → `spots/route-wire-codec.ts`(encode 짝 이미 거기), `ZLinkNativeFallbackBoundSession(+SendCall)`(2052-2251)→streams, `ZLinkLocalFirstActorJoinCoordinator`+`LazyNative`(2253-2381)→actors, `ZLinkMonitoringRuntime`(2383-2457)→별도 파일.
  - 검증: `test/contract/*runtime*.test.js`, `stream-runtime.test.js`, `channel-client.test.js`, `actor-manager.test.js`, `monitoring-runtime.test.js`.
- [ ] **D4. `runtime/streams/index.ts` (2229줄)** — god-class `ZLinkStreamBindingRuntime`(784-1319)
  - `SessionActorCoordinator`(803-920,1216-1274) / `BoundSessionService`(922-1148,1387-1444, C5와 함께) / `BoundActorRelaySender`(1150-1190) / `ActorSessionBindingRegistry`(1321-1385, 이미 깨끗 → 파일만 분리).
  - 부차: `stream-frame-factory.ts`(1446-1570), `session-context.ts`(1572-1719), `session-requests.ts`(1801-1918), `stream-session-runtime.ts`(`ZLinkStreamSessionNodeRuntime` 531-751 + `SessionRuntime` 298-529 + `SerialExecutor` 753-782).
- [ ] **D5. `runtime/locations/index.ts` (1965줄)** — 6개 도메인(store/write·query/lease/resolver/auto-connect/lifecycle)이 한 TU
  - `in-memory-store.ts`(77-388+1764-1822+1947-1961), `runtime.ts`(390-855), `lease-tracker.ts`(930-1007), `resolvers.ts`(1009-1154, C8 지점), `auto-connect.ts`(857-928+1156-1497+1830-1920), `lifecycle.ts`(1499-1762, B2 수정 + dotnet E1 동형 3책임 재분해: actor/spot claim + actor-session route), `internal-util.ts`.
- [ ] **D6. `runtime/actors/index.ts` (1812줄)** — 선행 리팩토링(`ZLinkActorCreationCoordinator`, `applyRemoteJoinResult`) 후 재성장
  - `actor-remote-joiner.ts`(`ZLinkActorNativeJoinCoordinator` 776-1211 ~435줄, 최대 + C7 payload 빌더), `actor-runtime-state.ts`(`ZLinkActorRuntimeState` 559-774), `spot-actor-dispatch.ts`(1364-1568, B4 수정 경계), `actor-context.ts`(1213-1286,1570-1698), `actor-mailbox.ts`(1288-1309). → 파사드 ~600줄.
- [ ] **D7. (R5, P1) `nestjs/src/index.ts` (2902줄 단일 파일)** — root export만 유지(`exports` 없이 `main`만 있으므로 import 경로 증가 금지), 구현 분해
  - `framework-loader.ts`(67-116 + `nest-integration` lazy load), `tokens.ts`(350-367), `contracts.ts`(118-348), `handler-metadata.ts`(369-375,1114-1239 decorator metadata append/read), `decorators.ts`(377-599), `options-builder.ts`(601-1112 fluent builder), `registration-composer.ts`(1424-1940 discovered→framework registration options), `discovery.ts`(1241-1286,1971-2504, C9 통합 후 provider discovery/scan), `module.ts`(1288-1422), `providers.ts`(2506-2897 Nest provider 배열 + runtime/manager factory). 순환 참조 주의(데코레이터↔메타데이터, 모듈↔프로바이더는 함수 경계). 검증: public export 회귀(`nestjs-module.test.js`) + `sample-regression.test.js`.
- [ ] **D8. `Registration.ts` (1333줄)** — validators는 이미 추출됨, 잔여 4관심사
  - `RegistrationTypes.ts`(47-371 계약 타입 41개 + 예외), 팩토리 진입점 유지(373-416), `RegistrationBuilders.ts`(418-928 빌더 11개 + C10 통합), `RegistrationCodecRegistry.ts`(925-996), `RegistrationNormalizers.ts`(1084-1169,1198-1333 to*/normalize* + C10 플래그 모듈).
- [ ] **D9. `framework-locations-redis/src/index.ts` (1130줄 단일, 선택)** — 의도적 격리라 삭제 아님, dotnet E2 동형 구조. 경계 주의(store-instance keyPrefix helper와 row-key codec은 별개):
  - `store.ts`(37-417, `ZLinkRedisLocationStore` 공개 클래스 + connection + write/remove/resolve), `redis-store-key-prefixes.ts`(419-455, `this.keyPrefix` 의존 private helper — 재사용 codec 아님), `redis-options.ts`(458-491, `MutableZLinkRedisLocationOptions`+`configureOptions`), `redis-row-keys.ts`(493-540, PeerKey/SpotKey/… + `encode*Key`/`encodeKeySegments` 실제 row-key codec), `redis-row-codec.ts`(576-1004, `LocationKind`+row JSON codec), `redis-scripts.ts`(1006-1130, Lua). `framework-codec-protobuf`(525줄)는 envelope 정리 후 재평가, `framework-codec-msgpack`(104줄)은 분해 불요.

**D 착수 순서:** 무상태/이미-깨끗 유닛부터(D1 timer/serial/outbound, D4 ActorSessionBindingRegistry, D2 transports/options/clients) → 배럴 고정 후 god-class 도메인 분해(D3 RuntimeHost·D1 JoinDispatch) → hot 경로는 C2/C8 벤치 동반.

---

## 부록 A. dotnet 정본 대조 요약

node에서 **부재/이미 해소**된 dotnet 항목(재도입 금지): A-SP1/A-SP2/A-SP4(peer disconnect·discovered-router·descriptor
`LeaveAsync`, native/데코레이터 위임), A-CH1(bridge live `channels:861-885`), A-LO1(const-false param 없음, 진짜 술어 `RegistrationValidators:395`),
A-ST1/A-ST2(죽은 encoder·단수 Lua), B1/B2(빈 metadata·이중 압축, 대칭·1회), C6/C7(단일 outbound·데코레이터),
C14(단일 message-flow tracer), C16 대부분/D7(헤더 조회 1건·단일 handler 래퍼), D6(고정율 drain 없음, event-driven+`queueMicrotask`).

node에 **동형 존재** → 위 A~D 반영: A-LO2(빈 no-op) / B4·B6·B8·B9(→B5·B1·B2·B7) / C2·C3·C9·C12·C13(→C2·C3·C4·C8·C6) / D5(SpotMesh pass-through→C10) / E1(location lifecycle 3책임→D5) / E2(redis 단일 클래스→D9).

## 부록 B. 미사용 빌드 산출물(추적 대상 아님, grep 노이즈)

`git status --ignored --short -- framework/languages/node`의 `!!` 항목: `node_modules/`, `packages/*/dist/`, `samples/*/dist/`, `e2e/*/*/dist/`.
커밋 대상 아니나 grep 결과를 흐릴 수 있어 필요 시 삭제 가능(`npm run build`/각 runner가 재생성). **주의:** `packages/framework/dist/internal`·`dist/nest-integration`은 계약 테스트/nestjs가 로드하므로 빌드 후 존재해야 함.

## 부록 C. 방법론 / 검증 게이트

- 8-에이전트 read-only 병렬 리뷰(spots / channels+messaging / host+backend / streams+stream-connector / locations+redis / actors+handlers / contracts+config / nestjs+http-client+codecs) + codex 병렬 리뷰(R1~R6 + tsc unused) + 메인 루프 직접 grep 검증.
- **dead 판정 필수 3확인:** `packages/**/src` `.ts` grep + `test/contract/*.js`·`e2e`·`samples`의 `framework.X`/`internal.X` + `tsc --noUnusedLocals --noUnusedParameters`. (최초 리뷰가 `dist/internal` 계약 테스트 사용을 놓쳐 §A4의 오탐 발생 — 정정 완료.)
- **⚠️ 동시 세션 주의:** 병렬 `kairos-code-dev` 세션이 Node 파일을 동시 수정/revert한 이력. 착수 전 `git fetch`+동기화, 검증 즉시 커밋+푸시([[project_node_framework_posd_refactor]]).

각 단계 후 최소 검증:

```bash
cd framework/languages/node
npm run build
npm run typecheck
npm test   # combined `node --test`는 핸들 누수로 hang → 게이트는 파일별 계약 테스트
```

R1/R2/R3/R4 이후 추가:

```bash
cd framework/languages/node
./e2e/YieldDispatch/run_e2e.sh all
./e2e/SpotService/run_e2e.sh all
./e2e/ToActorMessaging/run_e2e.sh all
```

샘플 표면을 건드린 경우: `npm run verify:samples`.

빌드: `node ../../../bindings/node/node_modules/typescript/bin/tsc -b tsconfig.build.json`.
