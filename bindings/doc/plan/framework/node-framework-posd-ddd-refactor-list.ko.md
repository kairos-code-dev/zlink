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

### A1. 선언 단위 dead — `tsc --noUnusedLocals --noUnusedParameters` 컴파일러 검증 (완료)

가장 안전한 삭제 트랙. 아래는 `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
출력(리뷰 시점). 파일 삭제가 아닌 **선언 단위 정리**이며 public re-export·테스트 internal 표면을 깨지 않는지 확인 후 제거.

- 2026-07-09 적용: 아래 import/private/local/param 정리를 반영했고, 같은 `tsc --noUnusedLocals --noUnusedParameters`
  명령이 통과했다.

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

- [x] **B1. route 디스패치 `requestSeq === null` throw가 수신 루프를 죽임** (correctness, 중)
  - `runtime/channels/index.ts:3071`(seq 검사)/`:3082`(report+throw) — `ZLinkRoutePacketDispatcher.dispatch`가 request인데 seq 없으면 report 후 `throw`. `ZLinkRouteReceiveLoop.runLoop`이 `await task`(`:3220`)로 throw를 받아 **루프 종료** → 잘못된 프레임 하나로 라우트 채널 수신 정지(DoS성). **비대칭 원인 정정:** 채널 dispatcher도 `:2552`에서 동일하게 throw하지만, `ZLinkChannelReceiveLoop.runLoop`은 task를 `:2674`에서 시작하고 `:2676`에서 **await 없이 계속**(fire-and-forget 스케줄링)이라 unhandled rejection에 그침 — dispatcher 라인 차이가 아니라 receive-loop의 await 여부 차이. dotnet B4 계열. skip-and-continue(report+close)로 전환.
  - 2026-07-09 적용: report 후 `return`으로 drop-and-continue 처리했고, `channel-client.test.js`에 `requestSeq`
    없는 route request가 throw 없이 `ReplyPathMissing`/`Drop`으로 보고되는 회귀 테스트를 추가했다.
- [ ] **B2. Location `AlreadyOwned` claim이 rollback 없이 재-activate로 낙하** (correctness, 낮은 신뢰)
  - `runtime/locations/index.ts:1533-1552` `executeActorClaimThenActivate` — `claimActor`가 `AlreadyOwned` 반환 시 switch가 `Conflict`만 조기 반환, `AlreadyOwned`는 `Claimed`와 동일하게 `activate()` 진입(`:1544-1545`). rollback arm `if (claim.status === Claimed)`(`:1547`)에 `AlreadyOwned` 미포함. 도달: `actors/index.ts:243` `createActor` → 이미 소유 중 actor에 이중 create 위험. dotnet B8 동형. `AlreadyOwned` arm(기존 반환/no-op) 명시.
  - 2026-07-09 재검토: 단순 조기 반환은 location lifecycle의 기존 내부 소유 추적과 맞지 않아 별도 설계 검토가 필요하다.
    이번 cleanup 범위에서는 적용하지 않는다.
- [x] **B3. stream-connector WebSocket 핸드셰이크 실패 시 소켓 누수** (없음)
  - `stream-connector/src/Runtime/Transport/NodeSocketConnector.ts:64` `connectWebSocket`이 소켓 연결 성공 후 `completeWebSocketHandshake` 호출하나 `WebSocketHandshake.ts` 실패 경로(`:33,48,62,75,89,93`)가 `socket.destroy()`를 안 함 → 핸드셰이크 실패 시 열린 TCP/TLS 소켓 방치. (TLS/TCP 직결은 `connectSocket` error 리스너로 정리돼 안전.) try/catch → 실패 시 destroy. dotnet B3 동형(WS 한정).
  - 2026-07-09 적용: `connectWebSocket()`이 handshake 실패 시 socket을 `destroy()`하도록 보장했고,
    oversized response header 테스트가 서버 측 socket close까지 확인한다.
- [ ] **B4. actor reply 옵션(metadata/compress)이 전면 무시됨** (벤치, silent no-op)
  - `runtime/actors/index.ts:1416-1436` `DefaultZLinkSpotActorReplyOptions.metadata()`/`compress()`는 public 계약(`ZLinkSpotActorReplyOptions`) 구현이나 결과를 읽는 `snapshot()`(`:1430`) 소비처 0. 실제 응답 전송부(`spots/index.ts:3052-3059`, `~4038`)는 metadata에 `new Map()`, compression에 `undefined`를 하드코딩 → 액터 핸들러가 지정한 응답 metadata/압축이 조용히 유실. `dispatchRequestThen`이 `snapshot()`을 actorResponseSender로 배선하거나, 미지원이면 계약 분리(draft 후보).
  - 2026-07-09 부분 적용: `dispatchRequestThen()`이 reply option snapshot을 후속 전송 콜백에 전달하고,
    Entry Spot activation/default Spot manager의 `actorResponseSender` metadata 인자까지 연결했다. 기존 전송
    인터페이스에는 compression 인자가 없으므로 `compress()` 반영은 새 계약 설계가 필요한 잔여 항목으로 남긴다.
- [x] **B5. Spot HandlerNotFound 동일 예외 이중 생성** (없음, 정보 누출)
  - `runtime/spots/index.ts:2953-2965` 및 `3953-3965` — 동일 `ZLinkFrameworkException(ActorDispatchHandlerNotFound, …)`를 `actorErrorSender`용·`throw`용으로 두 번 생성. dotnet B6 동형. 1회 생성 후 양쪽 전달.
  - 2026-07-09 적용: Entry Spot activation 경로와 default Spot manager 경로 모두 `missingActorError`를 한 번 생성해
    error sender와 throw 경로가 공유하도록 정리했다.
- [ ] **B6. native actor-join `admit` reply Message 수명주기 미확정** (없음, 저신뢰)
  - `runtime/spots/index.ts:1462-1502` — accepted 경로가 `encodeFrameworkPayloadMessage`로 native Message를 만들어 submit 후 `reply.close()`가 없음(request/actorCreateRequest만 close). submit 소유이전 여부 확정 필요([[feedback_ownership_all_langs]]).
- [ ] **B7. `closeActorBoundSession`/`destroyActor` signal 드롭** (없음)
  - `runtime/backend/contracts/index.ts:352`가 `signal?: AbortSignal` 선언하나 `node-backend-adapter-factory.ts` Proxy가 미-래핑 → native `(actor, timeoutMs)` 직결, signal 미도달. `streams/index.ts:1134`는 await하므로 sync-over-async는 아니나 close 중 취소 무시. dotnet B9 동형(경미).
  - 2026-07-09 재검토: binding public surface는 `SpotNode.destroyActor(actorRef)` operation과 `Actor.closeBoundSession(timeoutMs)`를 제공하지만,
    framework backend contract는 `ActorRef`만 받아 `closeActorBoundSession(actorRef, timeoutMs, signal?)`를 요구한다.
    내부 native API를 직접 호출하면 binding public API 우회가 되므로 이번 범위에서는 구현하지 않고 binding/framework
    contract 정렬이 필요한 항목으로 남긴다.
- [x] **B8. http-client 2xx 빈 바디에서 `submit<T>` decode 실패** (없음, 저신뢰)
  - `http-client/src/request-builder.ts:198-208` — 204/304/빈 200에서 `JSON.parse('')` throw → 성공 응답이 `PayloadDecodeFailed`로 뒤집힘. C++ `submit<T>` 계약(빈 바디 null 허용 여부) 대조 후 short-circuit.
  - 2026-07-09 적용: 성공 응답의 body가 빈 문자열이면 `submit<T>()`가 `null` body와 빈 `rawBody`를 반환하도록
    처리했고, 204 회귀 테스트를 추가했다.
- [x] **B9. nestjs 빌더 순서 함정 + config 검증 대칭성 구멍** (없음, 낮은 신뢰)
  - `nestjs/src/index.ts:1006-1013` `enableRouter`가 `this.spotOptions.router = {...}`로 통째 덮어써 선행 `connectRouter`(`:1015`)의 `manualConnections`/`manualPeerConnections` 유실(`enablePubSub`/`connectPeerPub` 동일). spread 병합. `Registration.ts:817-824` `enableRouter`↔`routingId` 순서 의존, `RegistrationValidators.ts:284` `connectRouter` peerRid 중복 미검증(spotFactory 등은 throw).
  - 2026-07-09 적용: NestJS SpotNode builder의 `enableRouter()`/`enablePubSub()`가 기존 manual connection을
    보존하도록 병합하고, router peerRid 중복 검증을 추가했다. `nestjs-module.test.js`에 호출 순서 보존과
    duplicate peerRid 회귀 테스트를 추가했다.

**B 착수 순서:** B1 → B3 → B5/B6 → B4(벤치) → B2/B7/B8/B9.

### 2026-07-09 부분 적용 검증

이번 적용 범위는 A1, B1, B3, B5, B8, B9, C11, C1 최소 게이트, C3 reporter/tracer 공유,
C4 stream frame/reply factory 정리,
C5 bound-session frame send 정리, C6 routed reply/decode wrapper 정리, C7 payload builder 정리,
C8 location live-row 정리, C9 NestJS helper 정리, C10 config builder/internal flag 정리와 B4 metadata 전달이다.
B2는 단순 조기 반환이 기존
location lifecycle의 소유 추적 모델과 맞지 않아 새 POSD 리팩토링 대상을 만들 수 있다고 판단했고,
코드 변경 없이 설계 재검토 항목으로 남겼다. B4의 compression 전달은 기존 전송 인터페이스에 인자가
없어 새 계약 설계가 필요한 잔여 항목으로 남겼다.

- `git diff --check -- bindings/doc/plan/framework/node-framework-posd-ddd-refactor-list.ko.md framework/languages/node` 통과.
- `npm run build` 통과.
- `npm run typecheck` 통과.
- `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- `npm run lint` 통과.
- `node --test test/contract/entry-spot-dispatch.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js` 통과(84 tests).
- `node --test test/contract/http-client.test.js` 통과(32 tests).
- `node --test test/contract/nestjs-module.test.js` 통과(54 tests).
- `node --test test/contract/location-host.test.js` 통과(5 tests).
- `node --test test/contract/actor-client.test.js test/contract/message-flow.test.js test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js` 통과(88 tests).
- `node --test test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js` 통과(127 tests).
- `node --test test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js` 통과(111 tests).
- `node --test test/contract/actor-client.test.js test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js` 통과(77 tests).
- `node --test test/contract/message-flow.test.js test/contract/stream-connector.test.js test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js` 통과(125 tests).
- `node --test --test-name-pattern 'MFLOW-009|DERR-|ZLinkDispatchErrorReporter|ZLinkChannelRequestDispatcher|ZLinkRoutePacketDispatcher drops route requests|ZLinkModule route.*dispatches inbound routed handlers|ZLinkFrameworkRuntimeHost dispatches client-server channel request handlers|ZLinkFrameworkRuntimeHost dispatches client-server send handlers|ZLinkModule channel client uses runtime host channel transport' test/contract/message-flow.test.js test/contract/channel-client.test.js` 통과(16 tests).
- `node --test --test-name-pattern 'store location resolvers|location spot route resolver|DSC-008|DSC-009|requestToChannel traffic survives location|same routing id different endpoint replaces located provider' test/contract/location-runtime.test.js test/contract/channel-client.test.js` 통과(4 tests).
- `node --test test/contract/channel-client.test.js test/contract/stream-connector.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/location-runtime.test.js` 통과(184 tests).
- `npm test`는 최신 C4 적용 후 현재 실행 환경에서 `channel-client.test.js` 28번 이후가 장시간 정지해 중단했다.
  대신 `ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS=test/contract/channel-client.test.js npm test`가 통과했고, `channel-client.test.js`는
  1-34번 구간과 35-56번 구간을 분할 실행해 모두 통과했다.
- C7 적용 후 `ZLINK_NODE_RUNTIME_GATE_SKIP_TESTS=test/contract/channel-client.test.js npm test`는
  `sample-regression.test.js` 내부 sample runner의 Bingo transient 실패로 2회 중단됐지만,
  `node --test test/contract/sample-regression.test.js` 전체 단독 실행은 통과(44 tests)했다.
- `npm run verify:samples`에서 Node 샘플 6개가 모두 `PASS`를 출력했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- C8 적용 후 `./samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`는
  `TicTacToe.Ts`와 `Bingo.Ts`가 `PASS`를 출력한 뒤 `DeliveryDispatch.Ts` 준비 timeout으로 1회 중단됐다.
  이후 `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`를 단독 재실행해 모두 `PASS`를 확인했다.
- C9 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- C10 적용 후 `node --test --test-name-pattern 'builder maps channel and route mesh options|maps route mesh channel options|framework options builder maps dotnet-shaped registration flow into options|validates channel capability endpoints and peer acquisition|preserves route mesh transport options|routeMesh channel option dispatches inbound routed handlers|SpotNode router is not classified as packet route channel|validates and maps SpotNode router and pubSub capability options|derives Spot publisher clients from SpotMesh pubSub capability|framework and NestJS builders register in-memory and integrated location stores|framework runtime host uses one explicit location store|framework runtime host starts spot node auto-connect loops' test/contract/nestjs-module.test.js test/contract/channel-client.test.js test/contract/location-host.test.js`
  통과(12 tests). C10은 config builder/routeMesh/spotMesh/location builder에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- C10 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- C6 적용 후 `npm run build` 통과.
- C6 적용 후 `node --test --test-name-pattern 'remote join|native remote join|routed actor|routed calls|route bridge|SPOT-addressed|routeMesh channel option|route channel dispatches|replies no-bind|routed actor request|bound session|joins remote|routes remote spot-node join' test/contract/spot-manager.test.js test/contract/actor-manager.test.js test/contract/channel-client.test.js test/contract/stream-runtime.test.js`
  통과(32 tests). C6은 routed actor join/route bridge/bound-session에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- C6 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D8 codec registry 분리 적용 후 `npm run build` 통과.
- D8 codec registry 분리 적용 후 `node --test --test-name-pattern 'configuration surface|framework options builder maps dotnet-shaped registration flow into options|zlinkFramework builder maps channel and route mesh options|uses channel serializer registry|uses protobuf codec extension|create request uses configured custom serializer|create request uses binary codec extensions|joinSpot uses configured custom serializer|joinSpot uses binary codec extensions|worker options are accepted' test/contract/contract-surface.test.js test/contract/nestjs-module.test.js test/contract/channel-client.test.js test/contract/spot-manager.test.js test/contract/actor-manager.test.js test/contract/entry-spot-serial-dispatch.test.js`
  통과(10 tests). 설정/codec builder에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D8 codec registry 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D8 normalizer 분리 적용 후 `npm run build` 통과.
- D8 normalizer 분리 적용 후 `node --test --test-name-pattern 'configuration surface|framework options builder maps dotnet-shaped registration flow into options|zlinkFramework builder maps channel and route mesh options|validates channel capability endpoints and peer acquisition|maps route mesh channel options|maps stream node options|validates and maps SpotNode router and pubSub capability options|derives Spot publisher clients|worker options are accepted|uses protobuf codec extension|uses channel serializer registry' test/contract/contract-surface.test.js test/contract/nestjs-module.test.js test/contract/channel-client.test.js test/contract/entry-spot-serial-dispatch.test.js`
  통과(11 tests). 설정 normalizer와 registration helper에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D8 normalizer 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D8 builder 분리 적용 후 `npm run typecheck` 통과.
- D8 builder 분리 적용 후 `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D8 builder 분리 적용 후 `npm run lint` 통과.
- D8 builder 분리 적용 후 `npm run build` 통과.
- D8 builder 분리 적용 후 `node --test --test-name-pattern 'configuration surface|framework options builder maps dotnet-shaped registration flow into options|zlinkFramework builder maps channel and route mesh options|validates channel capability endpoints and peer acquisition|maps route mesh channel options|maps stream node options|validates and maps SpotNode router and pubSub capability options|derives Spot publisher clients|worker options are accepted|framework and NestJS builders register in-memory and integrated location stores|creates Spot manager before runtime bootstrap' test/contract/contract-surface.test.js test/contract/nestjs-module.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/location-host.test.js`
  통과(11 tests). 설정 builder와 registration entrypoint에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D8 builder 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D8 type/facade 분리 적용 후 `npm run typecheck` 통과.
- D8 type/facade 분리 적용 후 `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D8 type/facade 분리 적용 후 `npm run lint` 통과.
- D8 type/facade 분리 적용 후 `npm run build` 통과.
- D8 type/facade 분리 적용 후 `node --test --test-name-pattern 'configuration surface|framework options builder maps dotnet-shaped registration flow into options|zlinkFramework builder maps channel and route mesh options|validates channel capability endpoints and peer acquisition|maps route mesh channel options|maps stream node options|validates and maps SpotNode router and pubSub capability options|derives Spot publisher clients|worker options are accepted|framework and NestJS builders register in-memory and integrated location stores|creates Spot manager before runtime bootstrap' test/contract/contract-surface.test.js test/contract/nestjs-module.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/location-host.test.js`
  통과(11 tests). registration public surface와 builder/factory 진입점에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D8 type/facade 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 ActorSessionBindingRegistry 분리 적용 후 `npm run typecheck` 통과.
- D4 ActorSessionBindingRegistry 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 ActorSessionBindingRegistry 분리 적용 후 `npm run lint` 통과.
- D4 ActorSessionBindingRegistry 분리 적용 후 `npm run build` 통과.
- D4 ActorSessionBindingRegistry 분리 적용 후 `node --test --test-name-pattern 'bound session|session binding|stream session|actor session|sendBoundSession|disconnectBoundSession|relay|bindOrGet|single frame reply|session reply|correlationId' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-client.test.js test/contract/actor-manager.test.js`
  통과(45 tests). stream binding/session route에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 ActorSessionBindingRegistry 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 stream-frame-factory 분리 적용 후 `npm run typecheck` 통과.
- D4 stream-frame-factory 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 stream-frame-factory 분리 적용 후 `npm run lint` 통과.
- D4 stream-frame-factory 분리 적용 후 `npm run build` 통과.
- D4 stream-frame-factory 분리 적용 후 `node --test --test-name-pattern 'stream frame|single frame reply|session reply|correlationId|compressed|decompresses|bound session|stream session|actor session|sendBoundSession|relay|packetName' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-client.test.js test/contract/actor-manager.test.js`
  통과(47 tests). stream frame 생성, 압축, session/bound-session에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 stream-frame-factory 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-requests 분리 적용 후 `npm run typecheck` 통과.
- D4 session-requests 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 session-requests 분리 적용 후 `npm run lint` 통과.
- D4 session-requests 분리 적용 후 `npm run build` 통과.
- D4 session-requests 분리 적용 후 `node --test --test-name-pattern 'pending request|request sequence|request timeout|completes pending responses|unmatched response|response frames|session client|stream session|bound session|payloadForHeader|single frame reply' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-client.test.js test/contract/actor-manager.test.js`
  통과(51 tests). stream request/response tracking과 session/bound-session에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 session-requests 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-local-actors 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 session-local-actors 분리 적용 후 `npm run typecheck` 통과.
- D4 session-local-actors 분리 적용 후 `npm run lint` 통과.
- D4 session-local-actors 분리 적용 후 `npm run build` 통과.
- D4 session-local-actors 분리 적용 후 `node --test --test-name-pattern 'bound actor|bound session|session actor|actor binding|bindOrGet|cleanup removes actor bindings|onDisconnected can explicitly notify bound actors|stale tokens|current binding token|stream session actors' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js`
  통과(27 tests). local actor binding과 bound-session에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 session-local-actors 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-calls 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 session-calls 분리 적용 후 `npm run typecheck` 통과.
- D4 session-calls 분리 적용 후 `npm run lint` 통과.
- D4 session-calls 분리 적용 후 `npm run build` 통과.
- D4 session-calls 분리 적용 후 `node --test --test-name-pattern 'session client send|session client reply|send compress|reply compress|bound session send|stream session and bound session require packetName|Client stream send|Client stream reply|pending request|response frame|payloadForHeader' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js`
  통과(18 tests). session send/reply와 bound-session send에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 session-calls 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-calls 분리 적용 후 `node --test --test-skip-pattern 'node run_samples\.sh executes every sample self-check' $(find test -type f -name '*.test.js' -print | sort)`
  통과(520 tests). `sample-regression.test.js`의 전체 샘플 runner 재호출만 제외했고, 해당 샘플 검증은 위의
  6개 샘플 단독 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D4 session-context 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 session-context 분리 적용 후 `npm run typecheck` 통과.
- D4 session-context 분리 적용 후 `npm run lint` 통과.
- D4 session-context 분리 적용 후 `npm run build` 통과.
- D4 session-context 분리 적용 후 `node --test --test-name-pattern 'SessionContext|session context|session client|session actors|stream session|managed stream|bound session|pending request|response frame|payloadForHeader|onConnected|onDisconnected|onDispatch|require provided context|dispatch errors|cleanup removes actor bindings|notify bound actors' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js`
  통과(58 tests). session context, dispatch lifecycle, actor binding, pending response에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 session-context 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-context 분리 적용 후 `node --test --test-skip-pattern 'node run_samples\.sh executes every sample self-check' $(find test -type f -name '*.test.js' -print | sort)`
  통과(520 tests). `sample-regression.test.js`의 전체 샘플 runner 재호출만 제외했고, 해당 샘플 검증은 위의
  6개 샘플 단독 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D4 bound-session-response-target 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 bound-session-response-target 분리 적용 후 `npm run typecheck` 통과.
- D4 bound-session-response-target 분리 적용 후 `npm run lint` 통과.
- D4 bound-session-response-target 분리 적용 후 `npm run build` 통과.
- D4 bound-session-response-target 분리 적용 후 `node --test --test-name-pattern 'bound session response|captureBoundSessionResponseTarget|local bound session error response|bound remote actor request|completes local bound actor request|pending actor request|response frame|session client reply|bound session send and disconnect|current binding token|stale tokens' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js`
  통과(11 tests). bound-session response/error와 pending actor response에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 bound-session-response-target 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후 `npm run typecheck` 통과.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후 `npm run lint` 통과.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후 `npm run build` 통과.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후 `node --test --test-name-pattern 'managed stream|stream session|stream node runtime|SessionRelay|session runtime|provided context|serializes dispatch|does not invoke user callbacks|monitor disconnect|endpointless|receives framed packets|onConnected|onDisconnected|provider|real NestJS application context|session actors bind|bound session|current binding token' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js test/contract/sample-regression.test.js`
  통과(50 tests). managed stream adapter, session lifecycle, monitor disconnect, provider 생성, serial dispatch에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 managed-stream/session-serial-executor/session-provider 분리 적용 후 Node 샘플 6개를 단독 재실행해 최종 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
  첫 `SupportChat.Ts` 실행은 Redis 준비 대기 timeout으로 실패했고, 같은 변경 상태에서 stderr 포함 재실행해 `PASS`를 확인했다.
- D4 stream-session-runtime 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 stream-session-runtime 분리 적용 후 `npm run typecheck` 통과.
- D4 stream-session-runtime 분리 적용 후 `npm run lint` 통과.
- D4 stream-session-runtime 분리 적용 후 `npm run build` 통과.
- D4 stream-session-runtime 분리 적용 후 `node --test --test-name-pattern 'stream session runtime|stream session node runtime|stream node runtime|provided context|serializes dispatch|does not invoke user callbacks|monitor disconnect|endpointless|receives framed packets|managed stream|SessionRelay|onConnected|onDisconnected|dispatch errors|cleanup removes actor bindings|pending responses|compressed dispatch|compressed response|route disconnect error replies|bound session|current binding token|real NestJS application context' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js test/contract/sample-regression.test.js`
  통과(46 tests). session runtime lifecycle, monitor disconnect, endpointless disconnect, dispatch/error/reporting,
  bound-session binding에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 stream-session-runtime 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 stream-session-runtime 분리 적용 후 `node --test --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`
  전체 계약 테스트는 단일 aggregate 실행에서 300초 timeout으로 중단되어 통과로 기록하지 않았다. 같은 변경 상태에서
  `channel-client.test.js`를 제외한 계약 테스트를 `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`
  로 실행해 통과(464 tests)했고, `channel-client.test.js`는 별도 단독 실행으로 통과(56 tests)했다. 샘플 runner
  재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D4 bound-session-service 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 bound-session-service 분리 적용 후 `npm run typecheck` 통과.
- D4 bound-session-service 분리 적용 후 `npm run lint` 통과.
- D4 bound-session-service 분리 적용 후 `npm run build` 통과.
- D4 bound-session-service 분리 적용 후 `node --test --test-name-pattern 'bound session|session binding|sendBoundSession|disconnectBoundSession|bound remote actor request|local bound session|native bound session|current binding token|stale tokens|remote bound session bind|SessionRelay|route disconnect error replies|compressed response|compressed dispatch' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-client.test.js test/contract/actor-manager.test.js`
  통과(24 tests). bound-session local/transport/native 전송, disconnect, `SessionRelay` 재시도, stale token 방지에
  닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 bound-session-service 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 bound-session-service 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js` 단일 aggregate는 300초 timeout으로 중단되어 통과로
  기록하지 않았고, 같은 변경 상태에서 이름 패턴 기준 28 tests, 27 tests, 1 test로 나누어 전부 통과시켰다.
  샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D4 session-actor-coordinator 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 session-actor-coordinator 분리 적용 후 `npm run typecheck` 통과.
- D4 session-actor-coordinator 분리 적용 후 `npm run lint` 통과.
- D4 session-actor-coordinator 분리 적용 후 `npm run build` 통과.
- D4 session-actor-coordinator 분리 적용 후 `node --test --test-name-pattern 'actor session|session actor|session actors|bindOrGet|session binding|bound actor|cleanup removes actor bindings|onDisconnected can explicitly notify bound actors|stale tokens|current binding token|managed stream actor bind|SessionRelay|remote bound session bind|bound remote actor request|local bound actor request|runtime host native bound session retries|stream session actors' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js test/contract/actor-client.test.js`
  통과(18 tests). actor bind/rebind/refresh, stale token 방지, cleanup, `SessionRelay` actor binding에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 session-actor-coordinator 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 session-actor-coordinator 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 28 tests, 27 tests, 1 test로 나누어
  전부 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.
- D4 bound-actor-relay-sender 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D4 bound-actor-relay-sender 분리 적용 후 `npm run typecheck` 통과.
- D4 bound-actor-relay-sender 분리 적용 후 `npm run lint` 통과.
- D4 bound-actor-relay-sender 분리 적용 후 `npm run build` 통과.
- D4 bound-actor-relay-sender 분리 적용 후 `node --test --test-name-pattern 'session actor relay|notify bound actors|notifyDisconnected|onDisconnected can explicitly notify bound actors|bound actor request|bound remote actor request|local bound actor request|SessionRelay|stream session actors|bound session send and disconnect|current binding token|stale tokens' test/contract/stream-runtime.test.js test/contract/stream-session-runtime.test.js test/contract/actor-manager.test.js`
  통과(13 tests). session actor relay, disconnect 알림, `SessionRelay` fallback, stale token 방지에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D4 bound-actor-relay-sender 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D4 bound-actor-relay-sender 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 28 tests, 27 tests, 1 test로 나누어
  전부 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.
- D5 lease-tracker/live-row 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D5 lease-tracker/live-row 분리 적용 후 `npm run typecheck` 통과.
- D5 lease-tracker/live-row 분리 적용 후 `npm run lint` 통과.
- D5 lease-tracker/live-row 분리 적용 후 `npm run build` 통과.
- D5 lease-tracker/live-row 분리 적용 후 `node --test --test-name-pattern 'location|lease|live|resolver|auto-connect|DSC-008|DSC-009|store location resolvers|same routing id different endpoint|requestToChannel traffic survives location|framework and NestJS builders register in-memory and integrated location stores|framework runtime host uses one explicit location store|framework runtime host starts spot node auto-connect loops' test/contract/location-runtime.test.js test/contract/location-host.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js`
  통과(21 tests). owner lease, live-row filter, store resolver, location host, auto-connect에 닿는 시나리오만 실행했고
  full e2e는 실행하지 않았다.
- D5 lease-tracker/live-row 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D5 lease-tracker/live-row 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 27 tests, 1 test로 나누어 전부 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은
  위의 6개 샘플 단독 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 `npm run typecheck` 통과.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 `npm run lint` 통과.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 `npm run build` 통과.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 `node --test --test-name-pattern 'store location resolvers|location readiness|location spot route resolver|SpotRef resolvers|location stores|DSC-008|DSC-009|requestToChannel traffic survives location|same routing id different endpoint|framework runtime host uses one explicit location store|framework runtime host starts spot node auto-connect loops|ZLinkRoutePacketDispatcher forwards SPOT-addressed route frames' test/contract/location-runtime.test.js test/contract/location-host.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js`
  통과(10 tests). store resolver, readiness, SPOT route resolver, location host route에 닿는 시나리오만 실행했고
  full e2e는 실행하지 않았다.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D5 resolvers/readiness/spot-route-resolver 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 1 test를 먼저 통과시켰다. 기존 27-test pattern aggregate는 300초 timeout으로 중단되어 통과로
  기록하지 않았고, 해당 범위를 8 tests, 10 tests, 10 tests matching group으로 다시 나누어 모두 통과시켰다.
  샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.
- D5 auto-connect 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D5 auto-connect 분리 적용 후 `npm run typecheck` 통과.
- D5 auto-connect 분리 적용 후 `npm run lint` 통과.
- D5 auto-connect 분리 적용 후 `npm run build` 통과.
- D5 auto-connect 분리 적용 후 `node --test --test-name-pattern 'auto-connect|starts channel auto-connect loops|starts spot node auto-connect loops|DSC-008|DSC-009|requestToChannel traffic survives location|same routing id different endpoint|location runtime renews owner lease|store location resolvers|framework runtime host uses one explicit location store|framework and NestJS builders register in-memory and integrated location stores' test/contract/location-autoconnect.test.js test/contract/location-runtime.test.js test/contract/location-host.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js`
  통과(13 tests). auto-connect planner/reconciler/loop, location owner lease, store resolver, location host auto-connect에
  닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D5 auto-connect 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D5 auto-connect 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 1 test, 8 tests, 10 tests를 통과시켰다. 기존 `PUB-001|fanout publisher|route client|RoutePacketDispatcher|route channel dispatches|routeMesh channel option`
  aggregate는 300초 timeout으로 중단되어 통과로 기록하지 않았고, 해당 범위는 단독 1 test, 단독 1 test,
  1 test, 4 tests, 3 tests로 다시 나누어 모두 통과시켰다. 마지막 `send-ready|outstanding dealer|FanoutClient|ChannelRequestDispatcher|DERR-006|DERR-009|DispatchErrorReporter`
  group은 10 tests 통과했다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독
  실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D5 lifecycle 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D5 lifecycle 분리 적용 후 `npm run typecheck` 통과.
- D5 lifecycle 분리 적용 후 `npm run lint` 통과.
- D5 lifecycle 분리 적용 후 `npm run build` 통과.
- D5 lifecycle 분리 적용 후 `node --test --test-name-pattern 'location lifecycle|actor claim|claimActor|AlreadyOwned|releaseActor|claimSpot|releaseSpot|bindActorSessionRoute|ownership lost|executeActorClaimThenActivate|location-backed|location route|remote bound session bind|actor session route|framework runtime host uses one explicit location store' test/contract/location-runtime.test.js test/contract/location-host.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js`
  통과(8 tests). actor claim/rollback, spot claim, actor-session route takeover, ownership-lost 처리에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D5 lifecycle 분리 적용 후 Node 샘플 6개를 단독 재실행해 모두 `PASS`를 확인했다:
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`.
- D5 lifecycle 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 1 test, 8 tests, 단독 1 test, 단독 1 test, 단독 1 test, 4 tests, 3 tests, 10 tests로
  나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 단독
  실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D5 runtime/store 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D5 runtime/store 분리 적용 후 `npm run typecheck` 통과.
- D5 runtime/store 분리 적용 후 `npm run lint` 통과.
- D5 runtime/store 분리 적용 후 `npm run build` 통과.
- D5 runtime/store 분리 적용 후 `node --test --test-name-pattern 'location|lease|live|resolver|auto-connect|lifecycle|actor claim|claimSpot|bindActorSessionRoute|DSC-008|DSC-009|same routing id different endpoint|requestToChannel traffic survives location|framework and NestJS builders register in-memory and integrated location stores|framework runtime host uses one explicit location store|framework runtime host starts channel auto-connect loops|framework runtime host starts spot node auto-connect loops' test/contract/location-store.test.js test/contract/location-runtime.test.js test/contract/location-autoconnect.test.js test/contract/location-host.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js test/contract/actor-manager.test.js test/contract/spot-manager.test.js`
  통과(42 tests). location store/runtime/lifecycle/resolver/auto-connect와 host wiring에 닿는 시나리오만 실행했고
  full e2e는 실행하지 않았다.
- D5 runtime/store 분리 적용 후 Node 샘플 6개를 재실행했다. 전체 루프에서 `TicTacToe.Ts`, `Bingo.Ts`,
  `DeliveryDispatch.Ts`, `SupportChat.Ts`는 `PASS`를 확인했다. `GameQuest.Ts`는 첫 루프에서
  `api-b-route` 준비 timeout으로 1회 중단됐고, 이후 `GameQuest.Ts`, `ShoppingMall.Ts`를 단독 재실행해
  모두 `PASS`를 확인했다.
- D5 runtime/store 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 1 test, 8 tests, 단독 1 test, 단독 1 test, 단독 1 test, 4 tests, 3 tests, 10 tests로
  나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로
  대체했다. full e2e는 실행하지 않았다.
- D6 actors 분리 적용 후
  `node ../../node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D6 actors 분리 적용 후 `npm run typecheck` 통과.
- D6 actors 분리 적용 후 `npm run lint` 통과.
- D6 actors 분리 적용 후 `npm run build` 통과.
- D6 actors 분리 적용 후 `node --test --test-name-pattern 'ActorManager|actor manager|actor context|joinSpot|joinEntrySpot|remote actor|routed actor|bound session|actor packet|location claim|ZLinkActorDispatchRouter|mailbox|SpotActorDispatcher|actor reply|HandlerNotFound|routes remote spot-node join|native remote join|remote join|joins remote|routed calls' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(64 tests). actor 생성/context, remote join, actor packet dispatch, mailbox, bound-session route에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D6 actors 분리 적용 후 Node 샘플 6개를 재실행했다. `TicTacToe.Ts`, `Bingo.Ts`,
  `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`는 `samples/run_samples.sh` 경로에서 `PASS`를
  확인했다. `ShoppingMall.Ts`는 wrapper 실행이 SIGTERM(143)으로 중단됐지만, 같은 변경 상태에서
  `samples/ShoppingMall.Ts/run_sample.sh`를 단독 실행해 `PASS ShoppingMall.Ts`를 확인했다.
- D6 actors 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 `pipefail`을 켠 상태에서 이름 패턴 기준 14 tests,
  14 tests, 1 test, 8 tests, 단독 1 test, 단독 1 test, 단독 1 test, 4 tests, 3 tests, 10 tests로
  나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로
  대체했다. full e2e는 실행하지 않았다.
- D1 spots 부분 분리 적용 후
  `node ../../node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots 부분 분리 적용 후 `npm run typecheck` 통과.
- D1 spots 부분 분리 적용 후 `npm run lint` 통과.
- D1 spots 부분 분리 적용 후 `npm run build` 통과.
- D1 spots 부분 분리 적용 후
  `node --test --test-name-pattern 'timer|serial|outbound|publish|sendToChannel|requestToChannel|sendToSpot|requestToSpot|SPOT outbound|SpotNode router|routed calls|Entry Spot timer|Spot timer|worker|ZLinkSpotActorDispatcher|actor packet|join actor|remote bound session|no bind|auto connect|publisher' test/contract/spot-manager.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js`
  통과(35 tests). timer/serial/outbound, SpotNode router/pubsub, actor packet, remote bound-session/no-bind,
  auto-connect/publisher wiring에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots remote codec 분리 적용 후
  `node --test --test-name-pattern 'remote actor|native remote join|remote join|routes remote spot-node join|actor packet|remote bound session|no bind|routed actor|join actor|Entry Spot routed actor packet|routed bound session' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(25 tests). remote join, routed actor packet, bound-session/no-bind decode 경로만 실행했고 full e2e는 실행하지 않았다.
- D1 spots actor lifecycle/packet drain 분리 적용 후
  `node ../../node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots actor lifecycle/packet drain 분리 적용 후 `npm run typecheck` 통과.
- D1 spots actor lifecycle/packet drain 분리 적용 후 `npm run lint` 통과.
- D1 spots actor lifecycle/packet drain 분리 적용 후 `npm run build` 통과.
- D1 spots actor lifecycle/packet drain 분리 적용 후
  `node --test --test-name-pattern 'actor lifecycle|ActorLifecycle|disconnect|leave actor|actor packet|no bind|remote bound session|Entry Spot routed actor packet|routed bound session|mailboxes|entry spot actor packets|native remote join|remote join|routed actor|SpotActorDispatcher' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(30 tests). actor lifecycle, actor packet mailbox, no-bind reply, remote bound-session/routed actor packet에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots route packet dispatch 분리 적용 후
  `node ../../node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots route packet dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots route packet dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots route packet dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots route packet dispatch 분리 적용 후
  `node --test --test-name-pattern 'SpotRoute|spot route|route raw SPOT|routed spot|route packet|SPOT route|SpotNode router|routed calls|sendToSpot|requestToSpot|direct envelope|handler not found|PayloadDecode|actor packet|remote bound session|routed actor' test/contract/spot-manager.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/channel-client.test.js test/contract/actor-manager.test.js test/contract/stream-runtime.test.js`
  통과(22 tests). routed SPOT packet, direct envelope, SpotNode router, route bridge fallback, remote bound-session과
  routed actor packet에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots actor packet relay dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots actor packet relay dispatch 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots actor packet relay dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots actor packet relay dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots actor packet relay dispatch 분리 적용 후
  `node --test --test-name-pattern 'actor packet relay|raw actor relay|remote bound session|routed bound session|actor packet target|Entry Spot routed actor packet|routed actor|remote join|native remote join|bound session receiver|Session command|deferredResponse' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(19 tests). actor packet relay, deferred response, remote bound-session receiver, routed actor packet target에
  닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots routed actor admission 분리 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots routed actor admission 분리 적용 후 `npm run typecheck` 통과.
- D1 spots routed actor admission 분리 적용 후 `npm run lint` 통과.
- D1 spots routed actor admission 분리 적용 후 `npm run build` 통과.
- D1 spots routed actor admission 분리 적용 후
  `node --test --test-name-pattern 'remote actor|native remote join|remote join|routes remote spot-node join|routed actor|join actor|Entry Spot routed actor packet|routed bound session|actor packet target|actor packet relay|remote bound session|deferredResponse' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(24 tests). remote join, routed actor admission, routed actor packet, bound-session relay에 닿는 시나리오만
  실행했고 full e2e는 실행하지 않았다.
- D1 spots actor packet dispatch 공유 적용 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots actor packet dispatch 공유 적용 후 `npm run typecheck` 통과.
- D1 spots actor packet dispatch 공유 적용 후 `npm run lint` 통과.
- D1 spots actor packet dispatch 공유 적용 후 `npm run build` 통과.
- D1 spots actor packet dispatch 공유 적용 후
  `node --test --test-name-pattern 'actor packet|SpotActorDispatcher|HandlerNotFound|missing actor|remote bound session|routed actor|Entry Spot routed actor packet|routed bound session|actor packet target|actor packet relay|deferredResponse|disconnect notification|onDisconnectActor|mailbox|no bind' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(27 tests). actor packet dispatch, missing actor error bridge, mailbox, remote bound-session target에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots handler registration 적용 규칙 분리 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots handler registration 적용 규칙 분리 후 `npm run typecheck` 통과.
- D1 spots handler registration 적용 규칙 분리 후 `npm run lint` 통과.
- D1 spots handler registration 적용 규칙 분리 후 `npm run build` 통과.
- D1 spots handler registration 적용 규칙 분리 후
  `node --test --test-name-pattern 'handler registry|registered without actor type|Entry Spot routed actor packet|entry spot actor packets|joined user spot actors|SpotActorDispatcher|actor packet|spot packet|subscribe|subscription|route channel dispatches|routed send and request|remote bound session|routed actor' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(30 tests). Entry/User Spot handler registration, actor packet, subscription, routed Spot packet에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots location claim adapter 분리 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots location claim adapter 분리 후 `npm run typecheck` 통과.
- D1 spots location claim adapter 분리 후 `npm run lint` 통과.
- D1 spots location claim adapter 분리 후 `npm run build` 통과.
- D1 spots location claim adapter 분리 후
  `node --test --test-name-pattern 'SpotManager creates|claims location|rolls location claim|create reject|retry same spotRid|lifecycle failure|rejects unregistered|close rejects|context close|concurrent getOrCreate|creates Spot factories|creates Spot manager|location spot route resolver|claimSpot|releaseSpot|SpotCreateState|Spot manager before runtime bootstrap' test/contract/spot-manager.test.js test/contract/location-runtime.test.js test/contract/location-host.test.js test/contract/nestjs-module.test.js`
  통과(14 tests). Spot create/getOrCreate/close, location claim/release/retry/reject에 닿는 시나리오만 실행했고
  full e2e는 실행하지 않았다.
- D1 spots timer registration 적용 규칙 분리 후
  `node node_modules/typescript/bin/tsc -p tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters` 통과.
- D1 spots timer registration 적용 규칙 분리 후 `npm run typecheck` 통과.
- D1 spots timer registration 적용 규칙 분리 후 `npm run lint` 통과.
- D1 spots timer registration 적용 규칙 분리 후 `npm run build` 통과.
- D1 spots timer registration 적용 규칙 분리 후
  `node --test --test-name-pattern 'timer|Entry Spot timer|Spot timer|awaits async configure|onInitialize|creates lists finds and closes|create reject|registered|handler registry|creates Spot factories|creates Spot manager' test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/nestjs-module.test.js`
  통과(20 tests). Entry/User Spot timer registration, lifecycle configure/initialize, Spot create/close에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots native actor join admission 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots native actor join admission 분리 적용 후 `npm run typecheck` 통과.
- D1 spots native actor join admission 분리 적용 후 `npm run lint` 통과.
- D1 spots native actor join admission 분리 적용 후 `npm run build` 통과.
- D1 spots native actor join admission 분리 적용 후
  `node --test --test-name-pattern 'native remote join|remote join|routes remote spot-node join|local spot join|admitActorJoin|onActorJoin|onJoinedActor|routed actor|join actor|SpotManager replies routed actor request|remote bound session|actor packet target' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(24 tests). native/remote actor join, routed actor admission, bound-session target, actor packet target에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots subscription dispatch 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots subscription dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots subscription dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots subscription dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots subscription dispatch 분리 적용 후
  `node --test --test-name-pattern 'subscribe|subscription|pubsub|pubSub|publish|publisher|subscriber|fanout publisher|Spot publisher|Spot subscriber|message flow|handler registry|route channel dispatches|routed send and request|remote bound session|routed actor' test/contract/spot-manager.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/channel-client.test.js test/contract/message-flow.test.js test/contract/nestjs-module.test.js`
  통과(22 tests). subscription/pub-sub dispatch, fanout publisher, handler registry, route channel dispatch에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots routed bound-session dispatch 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots routed bound-session dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots routed bound-session dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots routed bound-session dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots routed bound-session dispatch 분리 적용 후
  `node --test --test-name-pattern 'remote bound session|routed bound session|bound session receiver|Session command|actor packet target|actor packet relay|deferredResponse|Entry Spot routed actor packet|remote join|native remote join|routes remote spot-node join|routed actor|route bridge|raw actor relay' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(23 tests). routed bound-session send/response/error, actor packet target, route bridge, routed actor packet에
  닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots route-frame dispatch 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots route-frame dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots route-frame dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots route-frame dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots route-frame dispatch 분리 적용 후
  `node --test --test-name-pattern 'remote bound session|routed bound session|bound session receiver|Session command|actor packet target|actor packet relay|deferredResponse|Entry Spot routed actor packet|remote join|native remote join|routes remote spot-node join|routed actor|route bridge|raw actor relay|route packet|SPOT route|SpotNode router|direct envelope|handler not found|PayloadDecode' test/contract/actor-manager.test.js test/contract/actor-client.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/stream-runtime.test.js test/contract/channel-client.test.js`
  통과(27 tests). route-frame dispatch 순서, routed bound-session, actor packet relay, route packet, routed actor
  admission에 닿는 시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots routed SPOT packet dispatch 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots routed SPOT packet dispatch 분리 적용 후 `npm run typecheck` 통과.
- D1 spots routed SPOT packet dispatch 분리 적용 후 `npm run lint` 통과.
- D1 spots routed SPOT packet dispatch 분리 적용 후 `npm run build` 통과.
- D1 spots routed SPOT packet dispatch 분리 적용 후
  `node --test --test-name-pattern 'route packet|SPOT route|routed spot|routed send|routed request|route channel dispatches|routeMesh|SpotNode router|direct envelope|handler not found|PayloadDecode|route bridge|forwards SPOT|bridge frames first|routed actor|remote bound session' test/contract/spot-manager.test.js test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/channel-client.test.js test/contract/nestjs-module.test.js test/contract/stream-runtime.test.js`
  통과(24 tests). routed SPOT packet send/request, route channel dispatch, SpotNode router, route bridge에 닿는
  시나리오만 실행했고 full e2e는 실행하지 않았다.
- D1 spots context 생성 분리 적용 후
  `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`
  통과.
- D1 spots context 생성 분리 적용 후 `npm run typecheck` 통과.
- D1 spots context 생성 분리 적용 후 `npm run lint` 통과.
- D1 spots context 생성 분리 적용 후 `npm run build` 통과.
- D1 spots context 생성 분리 적용 후
  `node --test --test-name-pattern 'context|addTimer|timer|worker|runWorker|destroyActor|creates Spot|create reject|onCreate|onInitialize|creates lists finds and closes|Spot manager|Entry Spot timer|Spot timer|awaits async configure|handler registry|outbound routed send and request' test/contract/entry-spot-dispatch.test.js test/contract/entry-spot-serial-dispatch.test.js test/contract/spot-manager.test.js test/contract/nestjs-module.test.js test/contract/channel-client.test.js`
  통과(32 tests). Entry/User Spot context, timer registration, worker call, Spot create lifecycle에 닿는 시나리오만
  실행했고 full e2e는 실행하지 않았다.
- D1 spots 부분 분리 적용 후 Node 샘플 6개를 확인했다. remote codec 분리 전에는 `samples/run_samples.sh` 경로로
  6개 모두 `PASS`를 확인했다. remote codec 분리 후 재확인에서는 wrapper가 `DeliveryDispatch.Ts` 시작 지점에서
  SIGTERM(143)으로 중단되어 `TicTacToe.Ts`, `Bingo.Ts`는 wrapper에서 `PASS`를 확인했고,
  `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`는 각 `run_sample.sh` 단독
  실행으로 모두 `PASS`를 확인했다.
- D1 spots actor lifecycle/packet drain 분리 적용 후 Node 샘플 6개를 `samples/run_samples.sh` 경로로
  재실행했고 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
  `ShoppingMall.Ts` 모두 `PASS`를 확인했다.
- D1 spots route packet dispatch 분리 적용 후 Node 샘플 6개를 확인했다. `samples/run_samples.sh` 경로에서
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`는 `PASS`를 확인했고, `SupportChat.Ts`는 wrapper에서
  ready marker timeout으로 1회 실패했지만 같은 변경 상태에서 `samples/SupportChat.Ts/run_sample.sh` 단독 실행으로
  `PASS`를 확인했다. `GameQuest.Ts`, `ShoppingMall.Ts`는 병렬 단독 실행 시 SIGTERM(143)으로 중단되어 순차 단독
  실행으로 다시 확인했고 둘 다 `PASS`를 확인했다.
- D1 spots actor packet relay dispatch 분리 적용 후 Node 샘플 6개를 `samples/run_samples.sh` 경로로 재실행했고
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`
  모두 `PASS`를 확인했다.
- D1 spots actor packet relay dispatch 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 단독 실행으로 통과(56 tests)했다. 샘플 runner
  재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots routed actor admission 분리 적용 후 Node 샘플 6개를 다시 확인했다. `samples/run_samples.sh` 경로에서
  `TicTacToe.Ts`, `Bingo.Ts`는 `PASS`를 확인했고, wrapper가 이후 SIGTERM(143)으로 중단되어
  `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`는 순차 단독 실행으로 모두
  `PASS`를 확인했다.
- D1 spots actor packet dispatch 공유 적용 후 첫 `TicTacToe.Ts` wrapper 실행은 user Spot actor handler turn
  capture가 깨져 `yield requires a framework Spot handler turn captured when the call object was created`로 실패했다.
  원인은 공유 dispatcher가 user Spot 전용 `serial: activation.serial` 주입을 빠뜨린 것이었고, `spot-actor-packet-dispatch.ts`
  옵션에 serial을 추가해 수정했다. 수정 후 `samples/TicTacToe.Ts/run_sample.sh` 단독 실행은 `PASS`를 확인했다.
- D1 spots actor packet dispatch 공유 적용 후 Node 샘플 6개를 다시 확인했다. `samples/run_samples.sh` 경로에서
  `TicTacToe.Ts`, `Bingo.Ts`는 `PASS`를 확인했고, wrapper가 `DeliveryDispatch.Ts` 시작 뒤 SIGTERM(143)으로
  중단되어 `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`는 단독 또는 순차 단독
  실행으로 모두 `PASS`를 확인했다.
- D1 spots handler registration 적용 규칙 분리 후 Node 샘플 6개를 `samples/run_samples.sh` 경로로 재실행했고
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`
  모두 `PASS`를 확인했다.
- D1 spots location claim adapter 분리 후 Node 샘플 6개를 `samples/run_samples.sh` 경로로 재실행했고
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`
  모두 `PASS`를 확인했다.
- D1 spots timer registration 적용 규칙 분리 후 Node 샘플 6개를 `samples/run_samples.sh` 경로로 재실행했고
  `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts`
  모두 `PASS`를 확인했다.
- D1 spots native actor join admission 분리 적용 후 Node 샘플 6개를 확인했다. 첫
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  실행은 `Bingo.Ts`의 `api-a` ready marker timeout으로 중단됐지만, 같은 변경 상태에서 `Bingo.Ts` 단독
  재실행은 `PASS`를 확인했다. 이어서 `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
  `ShoppingMall.Ts`를 순차 실행해 모두 `PASS`를 확인했다.
- D1 spots subscription dispatch 분리 적용 후 Node 샘플 6개를
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  경로로 재실행해 모두 `PASS`를 확인했다. wrapper 실행 중 `SupportChat.Ts` 종료 뒤 native segfault 로그가
  한 번 출력됐으나 wrapper는 전체 exit 0이었다. 같은 변경 상태에서 `SupportChat.Ts` 단독 재실행은 segfault
  없이 `PASS`를 확인했다.
- D1 spots routed bound-session dispatch 분리 적용 후 Node 샘플 6개를
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  경로로 재실행했고 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
  `ShoppingMall.Ts` 모두 `PASS`를 확인했다.
- D1 spots route-frame dispatch 분리 적용 후 Node 샘플 6개를
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  경로로 재실행했고 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
  `ShoppingMall.Ts` 모두 `PASS`를 확인했다.
- D1 spots routed SPOT packet dispatch 분리 적용 후 Node 샘플 6개를 확인했다. 첫
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  실행은 `Bingo.Ts`의 `play-b-route` ready marker timeout으로 중단됐지만, 같은 변경 상태에서 `Bingo.Ts`
  단독 재실행은 `PASS`를 확인했다. 이어서 `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
  `ShoppingMall.Ts`를 순차 실행해 모두 `PASS`를 확인했다.
- D1 spots context 생성 분리 적용 후 Node 샘플 6개를 확인했다. 첫
  `samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`
  실행은 `Bingo.Ts` PASS 뒤 native abort 로그가 한 번 출력되고, 이후 `SupportChat.Ts` ready marker timeout으로
  중단됐다. 같은 변경 상태에서 `Bingo.Ts SupportChat.Ts` 재실행은 clean `PASS`였고, 이어서 `GameQuest.Ts`,
  `ShoppingMall.Ts`를 순차 실행해 모두 `PASS`를 확인했다.
- D1 spots routed actor admission 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js` 단일 aggregate는 90초 이상 출력 없이 멈춰 중단했고,
  같은 변경 상태에서 이름 패턴 기준 16 tests, 14 tests, 8 tests, 2 tests, 7 tests, 10 tests로 나누어 모두
  통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.
- D1 spots actor packet dispatch 공유 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 8 tests, 2 tests,
  7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의
  6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots handler registration 적용 규칙 분리 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플
  검증은 위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots location claim adapter 분리 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플
  검증은 위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots timer registration 적용 규칙 분리 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플
  검증은 위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots native actor join admission 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 두 번째 14 tests 묶음은 첫 병렬 실행에서
  `tail` 파이프 조합이 3분 이상 남아 중단했고, 같은 패턴을 `timeout 90s` 단독 명령으로 재실행해 14 tests
  통과를 확인했다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.
- D1 spots subscription dispatch 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 두 번째 14 tests 묶음은 요약 출력 확인을 위해
  `timeout 90s` 단독 명령으로 한 번 더 실행해 14 tests 통과를 확인했다. 샘플 runner 재호출 테스트만 제외했고
  해당 샘플 검증은 위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots routed bound-session dispatch 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은
  위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots route-frame dispatch 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은
  위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots routed SPOT packet dispatch 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은
  위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots context 생성 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 이름 패턴 기준 16 tests, 14 tests, 4 tests, 4 tests,
  2 tests, 7 tests, 10 tests로 나누어 모두 통과시켰다. 샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은
  위의 6개 샘플 실행 결과로 대체했다. full e2e는 실행하지 않았다.
- D1 spots 부분 분리 적용 후 `channel-client.test.js`를 제외한 계약 테스트를
  `node --test --test-reporter=tap --test-skip-pattern 'node run_samples\.sh executes every sample self-check'`로
  실행해 통과(464 tests)했다. `channel-client.test.js`는 단독 실행으로 통과(56 tests)했다.
  샘플 runner 재호출 테스트만 제외했고 해당 샘플 검증은 위의 6개 샘플 실행 결과로 대체했다.
  full e2e는 실행하지 않았다.

---

## C. 구조 통합 — 지식 중복 소거

- [ ] **C1. ⭐(R1, P0) stream wire 포맷이 core Streams와 stream-connector 위성에 통째 중복** (없음, 최대 유지보수 impact)
  - 바이트 레이아웃 쌍둥이: 6B prefix(`protocol.ts:54-66` ↔ `stream-connector/.../ZlinkStreamFrameCodec.ts:5-30`), 헤더(`protocol.ts:70-179` ↔ `ZlinkStreamHeaderCodec.ts:22-142`), TLV metadata(`protocol.ts:284-356` ↔ `ZlinkStreamMetadataCodec.ts:4-74`), LZ4 pickle(`protocol.ts:239-448` ↔ `Compression/ZlinkStreamCompressionCodec.ts:68-192`, 주석까지 동일), BE 헬퍼(`protocol.ts:450-479` ↔ `ZlinkStreamSupport.ts:51-102`), enum 쌍둥이.
  - **진행형 divergence(잠재 비대칭):** connector `validateHeaderSemantics`(`ZlinkStreamHeaderCodec.ts:169-189`)는 중복 metadata 키를 **거부**(`ZlinkStreamMetadataCodec.ts:63`)하나 core `protocol.ts`는 조용히 덮어씀 → 한쪽이 받는 프레임을 다른 쪽이 거부 가능. stream-connector 단위 테스트 0(현 검증은 e2e `SpotService` round-trip뿐).
  - **방향(codex R1):** wire codec을 소유하는 내부 모듈 1개로 분리 → framework bound-session relay와 connector client가 같은 encode/decode 호출. 오류 타입은 각 패키지 경계에서 감싸고 wire codec은 "어떤 바이트가 유효한가"만 판정. **주의:** stream-connector public API 신설 금지 — workspace 내부 구현 모듈로 시작, 공유 모듈의 npm 노출 여부는 별도 spec/draft. 최소 착지: 두 코덱 상호 참조 주석 + 바이트 레이아웃 단일 spec 왕복 테스트(양측 인코드→상대 디코드). dotnet C1 동형.
  - 2026-07-09 최소 게이트 적용: framework decoder도 duplicate metadata key를 거부하도록 맞춰 connector와의
    수신 판정 divergence를 제거했다. `message-flow.test.js`에 full stream frame prefix/payload 교차 왕복과
    duplicate metadata 양쪽 거부 테스트를 추가했다. 공유 내부 모듈 추출은 아직 남은 C1 본작업이다.
- [ ] **C2. ⭐decode→dispatch→report 상태기계 다발(channels 5벌 + spots 2벌)** (벤치, per-message)
  - channels: `ZLinkChannelRequestDispatcher.dispatch` command(`:2474-2515`)/request(`:2516-2623`), `ZLinkChannelPublishDispatcher.dispatch`(`:2751-2835`), `ZLinkRoutePacketDispatcher.dispatch` command(`:3025-3065`)/request(`:3067-3141`). 동일 골격(envelope decode → packetName 검증 → `traceFlow(Received)` → handler lookup → missing report[+reply] → payload decode + filter + handler try/catch → `traceFlow(Dispatched/Replied)` → catch report). `(surface,kind,reason,action)` 튜플 + flow-guard ~15회 복붙.
  - spots: `ZLinkEntrySpotActivation.dispatchActorPacketInsideMailbox`(`:2875-3095`) ≈ `DefaultZLinkSpotManager.dispatchActorPacket`(`:3883-4093`) ~220줄 near-verbatim(차이는 actor 해결 소스·spotRid 소스뿐). error-report 튜플이 spots에서 26회, `flowIfEnabled` 10회.
  - kind/surface/reply-strategy 매개화 dispatch core 1개 + actor/spotRid 훅 주입. dotnet C2 동형.
  - 2026-07-10 부분 적용: spots actor packet 상태기계는 `spot-actor-packet-dispatch.ts`로 모았다. Entry Spot과
    user Spot의 차이는 actor resolver, left-actor disconnect 무시, local router, remote bound-session target
    기록, Spot serial executor 주입 콜백으로만 남겼다. channels dispatch 상태기계 중복은 아직 남아 있으므로 C2는
    완료로 표시하지 않는다.
- [x] **C3. dispatch reporter/tracer 파이프라인마다 중복 생성** (벤치)
  - `runtime/channels/index.ts:791,813,845` — command/publish/route 루프가 각각 `createDispatchErrorReporter`(→ `ZLinkDispatchErrorReporter`+`ZLinkMessageFlowTracer` 신규) 별 인스턴스 생성. `outboundFlow()`(`:915-923`)가 4번째 tracer. host도 `:233`(start)·`:704`(spotManager) 각각 new reporter. 1회 생성 후 공유 주입(+ B1 항목의 live-mode cell 단일화: `:888,918` cell 독립 생성이 런타임 토글 비대칭). dotnet C3 동형.
  - 2026-07-09 적용: channel runtime은 같은 runtime error sink에 대해 dispatch reporter를 WeakMap으로
    재사용하고, inbound reporter와 outbound tracer가 같은 diagnostics context/live-mode cell을 읽도록
    묶었다. host도 diagnostics context와 dispatch reporter를 sink별로 캐시해 start/stream/spot-node와
    spot-manager 옵션 경로가 같은 message-flow 상태를 공유한다.
- [x] **C4. actor-client 프레임 프리픽스 인라인 재구현 + response 헤더 다중 생성** (벤치, per-reply)
  - `runtime/actors/actor-client.ts:269-279` `decodeActorReply`가 `protocol.ts:54-66`의 역(6B prefix + slice)을 손코딩(`protocol.ts`엔 `decodeStreamFrame` 부재, connector와 3벌째). `protocol.ts`에 `tryDecodeStreamFrame(frame)` 추가 후 호출.
  - "request→response 헤더(correlationId echo)" 생성 다수: `actor-client.ts:119-138` + `streams/index.ts:2030-2038`·`471-482`·`983-1013`·`1394-1416`. `createResponseFrame(requestHeader, kind, payload, metadata)` factory 1개. dotnet C9 동형.
  - 2026-07-09 적용: framework stream protocol에 `decodeStreamFrame()`/`tryDecodeStreamFrame()`을 추가하고,
    actor-client의 단일 frame reply 해석을 이 함수로 이동했다. `actor-client.test.js`에 단일 frame reply 회귀
    테스트를 추가했다. `createStreamReplyHeader()`와 `createJsonReplyFrameMessage()`로 request header의
    `name`/`requestSeq`/`correlationId` echo 규칙을 한 곳에 모았고, session reply 테스트가 correlationId 보존을
    검증한다.
- [x] **C5. bound-session send 프레임 패밀리 8중 복붙** (code-motion)
  - `runtime/streams/index.ts` — `sendLocalBoundSession`/`…Response`/`…Error`(`:952-1048`), `sendNativeBoundSession`/`…Response`/`…Error`(`:1051-1126`), `DefaultZLinkBoundSessionResponseTarget.sendResponse`/`sendError`(`:1387-1444`)가 `createJsonFrameMessage → token 재검사 → write/sendNativeFrame → finally frame.close()` 동형. 에러 페이로드 shape `{code:err.constructor.name, message}`도 5중. `(kind, target-writer)` 매개 헬퍼.
  - 2026-07-09 적용: local bound-session 전송은 `writeLocalBoundSessionFrame()`으로, native 전송은
    `sendNativeBoundSessionPayload()`로, session response target은 내부 `send()`로 모았다. 에러 payload shape는
    `boundSessionErrorPayload()` 하나가 소유한다. public 계약과 frame wire format은 변경하지 않았다.
- [x] **C6. (R4) Spot routed reply-submit + remote-join decode/payload 중복** (벤치/code-motion)
  - raw-vs-envelope reply-submit이 `admitRouted`(`:1617-1864`)·`dispatchRoutedSpotPacket`(`:2369-2444`)에 ~7회 → `submitRouteReplyShaped(received, envelope, payload|error)`(dotnet C13). `decodeRemoteActorJoinRequest`(`:2160-2323`) 3블록 ~160줄 동일 필드 추출 → 헬퍼. `wrapRoutedSpotRequestCall` submit/yield(`:4690-4763`), `wrapSendCall`/`wrapPublishCall` 쌍둥이(`:4563-4585`).
  - 2026-07-09 적용: raw JSON reply와 channel envelope reply 선택은 `submitRoutePayloadReply()`/
    `submitSpotRouteBridgeReply()`/`submitRoutedActorJoinReply()`/`submitRoutedActorJoinError()`가 소유한다.
    remote actor join의 `actorRef`, actor create request, remote bound-session target 해석은
    `decodeRemoteActorJoinPayload()`로 모아 raw 단일 payload, channel envelope payload, legacy header+body 경로가
    같은 필드 규칙을 쓴다. `wrapSendCall()`과 `wrapPublishCall()`은 overload 기반
    `wrapFireAndForgetPacketCall()`을 공유한다. 큰 전략 객체는 D1/D2 분해 전 단계에서 새 얕은 모듈이 되기 쉬워
    이번 C6에서는 만들지 않았다.
- [x] **C7. remote-join 요청 payload 빌더 3벌** (code-motion)
  - `runtime/actors/index.ts` — `encodeRemoteNativeJoinRequest`(`:859-877`), `joinRemoteSpot` routed(`:907-925`)/fallback(`:955-972`)이 동일 JSON 스키마 손빌드, `boundSession*(+Hex)` 8회 반복 → `buildRemoteJoinRequestPayload(...)`. **주의:** `applyRemoteJoinResult`(`:1058-1094`)는 이미 dedup — payload 방향만 남음.
  - 2026-07-09 적용: `buildRemoteActorJoinRequestPayload()`를 추가해 packetName, actor ref, create request,
    source/bound-session target, request base64/hex 필드 구성을 한 곳으로 모았다. native fallback의 기존 필드 차이는
    유지했고, routed/raw-to-spot/fallback 호출부는 같은 payload builder를 사용한다.
- [x] **C8. Location mesh-scan resolver 쌍둥이 + live-row 필터 5곳 재구현** (벤치, per-read)
  - mesh-scan: `ZLinkStoreLocationResolvers.resolveSpotRef`(`:1045-1058`) ≈ `ZLinkLocationSpotRouteResolver.resolve`(`:1137-1153`)(miss 시 undefined vs throw만 다름) → 공유 헬퍼.
  - live-row 필터(owner lease live): `filterLive`(`:809-821`) + resolvers `listLivePeers`(`:1026`)/`resolveRoute`(`:1038`)/`resolveSpotRow`(`:1084`)/`resolveActorRow`(`:1096`). host가 resolver용 `ZLinkOwnerLeaseTracker`를 runtime `queryLeaseTracker`와 별도 인스턴스 생성(`host:562,799,821`) → lease 스냅샷 독립 캐시. `ZLinkLiveRowFilter` 추출 + 단일 tracker 공유. dotnet C12 동형.
  - 부수: `encodeRoutingIdHex` 중복(`locations:64-75` ≡ `key-codec.ts:51-62`), `isKnownAutoConnectType`/`isKnownLocationRole` try/catch 래퍼(`:1904-1920`)를 A1의 dead `tryParse*`로 수렴.
  - 2026-07-09 적용: owner lease 판정은 `ZLinkLiveRowFilter`가 소유하고, runtime query와 store resolver가
    같은 필터를 사용한다. host는 같은 location store 세트에 대해 `ZLinkOwnerLeaseTracker`를 캐시해 runtime,
    actor resolver, spot route resolver가 같은 lease snapshot을 공유한다. spot mesh scan은
    `resolveSpotRowInMeshes()` 하나를 `resolveSpotRef()`와 `ZLinkLocationSpotRouteResolver.resolve()`가 함께
    사용한다. routing id hex 변환은 `ZLinkLocationKeyCodec.encodeRoutingIdHex`로 수렴했고, auto-connect type/role
    유효성 검사는 canonical codec의 map 조회 함수로 이동했다.
- [x] **C9. (R5) nestjs discovery 4-함수 + manual/unique 헬퍼 다발 중복** (없음)
  - `discoverProviderRefs`(`:2125-2184`)/`discoverSpotProviderRefs`(`:2186-2245`)/`discoverSpotActorProviderRefs`(`:2247-2306`)/`discoverSpotTimerProviderRefs`(`:2308-2367`) — 2-페이즈 골격 ~60줄×4 = ~240줄 → 제네릭 `collectRefs<TMeta,TRef>(...)`. `createManual*Handlers` 5함수(`:2008-2078`) → 팩토리 1개. `assertUnique*Handler` 6함수(`:1777-1861`) → `assertUnique(existing, next, keyOf, label)`. `createConditionalClientProvider(ForFactory)`(`:2683-2724`) 인자-셔플 중복.
  - 2026-07-09 적용: Nest provider discovery는 `discoverDecoratedProviderRefs()`와 class-provider wrapper로 모아
    wrapper scan과 token metadata fallback을 한 곳에서 수행한다. manual channel/route handler 등록은
    `createManualHandlerRegistrations()`로, SPOT handler 중복 검사는 `assertUniqueRegistration()`으로 모았다.
    conditional client provider 생성은 `createConditionalClientProviderFromSpec()`로 통합해 registration 주입 경로와
    closure registration 경로가 같은 인자 해석을 사용한다. public decorator/DI token 표면은 변경하지 않았다.
- [x] **C10. Config 빌더 pass-through 벽 + 내부 플래그 프로토콜 분열** (code-motion)
  - `Registration.ts:743-802` `DefaultSpotMeshBuilder` 10메서드 전부 `this.node.X(...); return this` 벽(+ `Spots/Builders.ts:19` 빈 확장 interface) → `DefaultSpotNodeBuilder` 직접 반환/공유 base. dotnet D5 동형. `DefaultRouteChannelBuilder`(`:634-659`) ≈ `DefaultRouteMeshChannelBuilder`(`:661-686`) near-verbatim → 통합. `RouteMeshInternalState` non-enumerable 플래그 규약이 writer(`Registration.ts:1136-1169`)/reader(`RegistrationValidators.ts:465-484`) 2파일 분열 → 공유 모듈.
  - 2026-07-09 적용: `addSpotMesh()`는 별도 pass-through 클래스 없이 `DefaultSpotNodeBuilder`를 직접 반환한다.
    route channel과 route mesh channel builder는 `DefaultRouteChannelOptionsBuilder` 하나를 공유한다.
    non-enumerable route 내부 플래그의 writer/reader/copy 규약은 `RouteChannelInternalState.ts` 한 파일로 모았다.
    빈 subclass나 새 public API를 만들지 않아 C10 결과가 다시 얕은 POSD 대상이 되지 않게 했다.
- [x] **C11. http-client 헤더 조회 불필요 스캔** (없음)
  - `http-client/src/runtime/response-body-reader.ts:47,72-79` `findHeader` 선형 스캔인데 입력은 이미 소문자화 dict(`collectHeaders:61-70`) → `headers[name]` 직접 조회. `text.ts:7` `isBlank` 파일-로컬화. dotnet C16 잔재 1건.
  - 2026-07-09 적용: `content-encoding` 조회를 소문자 header dict의 직접 조회로 바꾸고, 무참조 public helper였던
    `isBlank`는 `text.ts` 파일 내부 함수로 낮췄다.
- [ ] **C12. (R6) native backend adapter Proxy property-name 분기 축소** (없음/code-motion)
  - `node-backend-adapter-factory.ts:130`부터 native object를 Proxy로 감싸고 `:147-220`이 property 이름으로 backend/spot-node/route-bridge/messaging 분기. Proxy·resolver 분리(surface별)는 **의도된 설계**(가드레일)지만, property-name switch가 커질수록 어떤 native 기능이 어떤 계약으로 매핑되는지 타입 시스템이 못 보여줌(obscurity). socket/spot-node/route-bridge/monitor를 명시적 wrapper class/작은 factory로 분리, property-name switch는 호환 최소 범위로 축소. native binding parity 테스트와 함께 **마지막에**(P2). §A1의 dead 변환 헬퍼(`toNativeTopologyFilter`/`toFrameworkRoutingIdEntries`) 동반 제거 또는 실제 call path 연결.

**C 착수 순서:** C1(spec-test 게이트 먼저) → C4 → C3/C5/C6/C7 → C2/C8(hot, 벤치) → C9/C10/C11 → C12(P2).

---

## D. God-file 분해 (POSD/DDD)

리뷰 시점 대비 framework가 ~50% 성장(spots 3272→4928, channels 2516→3692, host 1340→2489, streams 1789→2228, actors 1528→1811).
공개 표면 + `dist/internal`/`dist/nest-integration` export를 배럴 re-export로 고정한 채 분해하고 **33개 계약 테스트를 통과**시킨다(§dead 규칙 — 순수 churn-free 아님).

- [x] **D1. (R3, P0) `runtime/spots/index.ts` (4928줄)**
  - **god-class `ZLinkSpotActorJoinDispatch`(1084-2516, ~1432줄) = 최우선.** actor-join admission drain + actor lifecycle drain + actor-packet drain/no-bind reply + remote bound-session decode + remote actor-packet relay + routed actor-join + routed spot packet dispatch를 한 클래스가 소유 → `ZLinkSpotActorLifecycleDrain`/`ZLinkSpotActorPacketDrain`/`ZLinkSpotRoutedAdmission`/`ZLinkSpotRoutePacketDispatch` + 무상태 decode군 → `spot-remote-codec.ts`.
  - `DefaultZLinkSpotManager`(3190-4103, ~913줄) — spot CRUD + actor-packet dispatch(C2로 공통 core 추출 시 ~220줄 감량).
  - 무상태 유닛 즉시 추출: `spot-node-runtime.ts`(`ZLinkSpotNodeRuntimeManager` 357-655, publisher bundle/dispose), `entry-spot-activation.ts`, `spot-activation.ts`(user SPOT + location claim/release), `spot-timer.ts`(`4206-4365,4817-4891`), `spot-serial-executor.ts`(`4471-4561,4621`), `spot-outbound.ts`(`4367-4435,4563-4815`).
  - `ZLinkSpotActorJoinDispatch`의 18-인자 생성자(`:1092-1124`) = primitive-obsession → options 객체/factory(R2 Host 옵션 분리와 연결). (`ZLinkSpotNodeRuntimeManager`는 이미 단일 options 객체 생성자 `:367`이므로 이 항목 대상 아님.)
  - 2026-07-10 부분 적용: Entry/User Spot serial 실행 정책은 `spot-serial-executor.ts`,
    provider 생성 규칙은 `spot-provider.ts`, timer validation/overrun/diagnostics는 `spot-timer.ts`,
    channel/fanout/routed outbound와 `SpotRef` route target 정규화는 `spot-outbound.ts`로 분리했다.
    outbound 이동 후 `submit()`/`yield()`가 같은 routed request begin 로직을 반복하는 새 POSD 대상을 확인해
    `begin<TReply>()` helper로 즉시 수렴했다.
  - 2026-07-10 부분 적용: handler registration snapshot과 actor handler registry 연결은
    `spot-handler-registry.ts`가 소유한다. SpotNode auto-connect local row/manual endpoint 제외/peer
    connect-disconnect 정책은 `spot-node-autoconnect.ts`, native SpotNode option wiring과 publisher bundle 생성은
    `spot-node-connector.ts`, runtime publisher transport는 `spot-publisher-transport.ts`로 분리했다. 새 파일들은
    `ZLinkSpotNodeRuntimeManager` 전체를 import하지 않고 필요한 capability만 받도록 두어 manager mirror나
    단순 pass-through 파일로 번지지 않았는지 확인했다.
  - 2026-07-10 부분 적용: `ZLinkSpotActorJoinDispatch`의 positional 18-인자 생성자를
    `ZLinkSpotActorJoinDispatchOptions`로 바꿔 internal Entry route, Entry Spot, user Spot 호출부의 capability를
    이름으로 드러냈다. 큰 class 본문은 아직 남아 있으므로 D1은 완료로 표시하지 않는다. 다음 분리 대상은
    routed admission/route packet dispatch이다. 부분 적용 후 `runtime/spots/index.ts`는 3885줄이었다.
  - 2026-07-10 추가 적용: remote actor join packet 상수, wire payload 타입, identity 판정, actor ref decode,
    bound-session target decode, actor join payload decode는 `spot-remote-codec.ts`로 분리했다. 새 파일은 wire
    shape와 routing id decode만 소유하고 dispatch/admission 정책을 가져오지 않는다. 이로써 다음 단계에서
    `ZLinkSpotActorJoinDispatch`의 drain/admission 책임을 나눌 때 무상태 decode군을 다시 함께 끌고 가지 않게 했다.
  - 2026-07-10 추가 적용: actor lifecycle drain은 `spot-actor-lifecycle-drain.ts`, actor packet multipart 수집,
    remote bound-session bind 소비, no-bind reply frame 작성은 `spot-actor-packet-drain.ts`로 분리했다. native
    non-blocking recv flag는 `spot-native-flags.ts`로 모아 lifecycle/packet/join/route drain이 같은 계약 값을
    공유한다. 새 drain 파일은 routed admission, routed spot packet dispatch, subscription dispatch를 가져오지
    않으므로 분리 결과가 다시 큰 coordinator가 되지 않는지 확인했다. 이 시점의 `runtime/spots/index.ts`는 3536줄이다.
  - 2026-07-10 추가 적용: route reply 제출 규칙은 `spot-route-replies.ts`, routed SPOT packet/direct envelope
    dispatch는 `spot-route-packet-dispatch.ts`로 분리했다. route packet dispatcher는 packet handler lookup,
    channel/direct envelope decode, handler 실행, route reply/error report만 소유하고 routed actor admission이나
    bound-session relay decode를 가져오지 않는다. reply helper는 actor join reply와 route packet reply가 같은
    native bad-address 처리와 multipart reply 규칙을 공유하게 해, 분리 결과가 또 다른 중복 POSD 대상이 되지 않게
    했다. 이 시점의 `runtime/spots/index.ts`는 3195줄이다.
  - 2026-07-10 추가 적용: remote bound-session send/response/error와 actor packet relay wire decode는
    `spot-remote-route-codec.ts`, actor packet relay 실행은 `spot-actor-packet-relay-dispatch.ts`로 분리했다.
    relay dispatcher는 remote bound-session target 계산, stream frame header 판정, deferred response bridge reply,
    actor packet target 회신만 소유한다. routed admission, route packet dispatch, wire decode를 가져오지 않으므로
    분리 결과가 새 god-class나 단순 pass-through 모듈로 변하지 않았는지 확인했다. 이 시점의
    `runtime/spots/index.ts`는 2886줄이다.
  - 2026-07-10 추가 적용: route-frame 기반 routed actor join admission은 `spot-routed-actor-admission.ts`로
    분리했다. 새 파일은 remote actor join 요청 decode, local actor 재입장 또는 routed actor provider 호출,
    Spot `onActorJoin`/`onJoinedActor` 실행, actor join route reply 제출만 소유한다. native actor join drain,
    route packet dispatch, actor packet relay를 가져오지 않으므로 새 coordinator가 되지 않는지 확인했다.
    native actor join admission은 같은 provider/commit 옵션을 공유하므로 `ZLinkSpotActorJoinDispatch` 안에
    남겼다. 이 시점의 `runtime/spots/index.ts`는 2732줄이다.
  - 2026-07-10 추가 적용: Entry Spot과 user Spot의 actor packet decode/dispatch/report/reply 상태기계는
    `spot-actor-packet-dispatch.ts`로 통합했다. 새 파일은 stream frame header decode, missing actor/error
    report, payload decode, actor send/request handler dispatch, async response/error sender bridge만 소유한다.
    Spot 생성/종료, actor join admission, route packet dispatch는 가져오지 않아 새 manager mirror가 되지 않도록
    경계를 확인했다. user Spot handler turn capture에 필요한 Spot serial executor를 옵션으로 넘긴다. 이 시점의
    `runtime/spots/index.ts`는 2355줄이다.
  - 2026-07-10 추가 적용: Entry Spot과 user Spot의 handler registration 적용 규칙은
    `spot-handler-registry.ts`의 `applyEntrySpotHandlerRegistrations()`와 `applySpotHandlerRegistrations()`로
    모았다. actor send/request, packet, subscription 등록 필터링과 actor handler registry 반영 지식이 두
    생성 경로에 반복되던 것을 registry 모듈 내부 규칙으로 수렴했다. 이 helper는 생성 lifecycle이나 Spot
    activation 상태를 받지 않으므로 새 activation builder나 manager mirror가 되지 않는지 확인했다. 이 시점의
    `runtime/spots/index.ts`는 2308줄이다.
  - 2026-07-10 추가 적용: user Spot location claim/release 상태 판정은 `spot-location-claim.ts`로 분리했다.
    새 파일은 mesh/node rid 확인, `claimSpot()` 결과를 Created/Existing/SpotCreateFailed로 해석하는 규칙,
    tracked release 호출만 소유한다. Spot provider 생성, native Spot wiring, lifecycle callback 실행은
    `DefaultZLinkSpotManager`에 남겨 activation builder mirror가 되지 않도록 했다. 이 시점의
    `runtime/spots/index.ts`는 2273줄이다.
  - 2026-07-10 추가 적용: Entry Spot과 user Spot의 configuration timer registration 적용 규칙은
    `spot-timer.ts`의 `addEntrySpotTimerRegistrations()`와 `addSpotTimerRegistrations()`로 모았다. timer
    필터링, handler type cast, diagnostics 생성 지식이 생성 경로에 반복되던 것을 timer 모듈 내부 규칙으로
    수렴했다. 수동 `context.addTimer()`와 Spot lifecycle 실행은 그대로 남겨 새 lifecycle builder가 되지 않도록
    했다. 이 시점의 `runtime/spots/index.ts`는 2257줄이다.
  - 2026-07-10 추가 적용: native `recvActorJoin()`으로 들어오는 actor join admission과 reply 제출은
    `spot-native-actor-join-admission.ts`로 분리했다. 새 파일은 native actor join payload decode, routed actor
    provider 호출, Spot `onActorJoin`/`onJoinedActor` 실행, native `replyActorJoin()` 제출만 소유한다.
    `ZLinkSpotActorJoinDispatch`는 native dispatch handler wiring, actor join drain loop, subscription drain,
    routed route-frame dispatch 조정만 남겼다. 추출 직후 소비되지 않는 `actorRef` decode와 불필요한
    `actorId` 파라미터를 제거해 새 admission 파일이 다시 decode 잡동사니나 pass-through 모듈이 되지 않게
    확인했다. 이 시점의 `runtime/spots/index.ts`는 2143줄이고, 새 native admission 파일은 164줄이다.
  - 2026-07-10 추가 적용: SPOT subscription(pub/sub) dispatch는 `spot-subscription-dispatch.ts`로 분리했다.
    새 파일은 subscribe registration 적용, native subscribe drain, publish envelope decode, subscription handler
    실행과 flow/error report만 소유한다. native actor join, route-frame dispatch, actor packet relay를 가져오지
    않으며, 생성자는 options 객체로 받아 positional 인자 목록을 새로 만들지 않게 했다.
    `ZLinkSpotActorJoinDispatch`는 이제 native dispatch handler wiring과 drain 조정만 남는다. 이 시점의
    `runtime/spots/index.ts`는 1971줄이고, 새 subscription dispatch 파일은 189줄이다.
  - 2026-07-10 추가 적용: routed bound-session send/response/error dispatch는
    `spot-routed-bound-session-dispatch.ts`로 분리했다. 새 파일은 bound-session route frame 판정, receiver
    callback 호출, replyable route에 대한 `{ ok: true }` 응답 제출만 소유한다. actor packet relay, route packet
    dispatch, routed actor admission을 가져오지 않으므로 route dispatcher mirror가 되지 않는다. 기존 내부
    callback 시그니처와 같은 `unknown` payload/metadata 계약을 유지해 public surface를 넓히지 않았다.
    이 시점의 `runtime/spots/index.ts`는 1919줄이고, 새 bound-session dispatch 파일은 101줄이다.
  - 2026-07-10 추가 적용: route-frame drain과 dispatch 순서는 `spot-routed-frame-dispatch.ts`로 분리했다.
    새 파일은 `recvRoute()` drain/retry, route-frame 처리 순서(bound-session → actor packet relay → routed
    SPOT packet → routed actor admission), packet handler registration map만 소유한다. native actor join,
    subscription, actor lifecycle/actor packet native drain은 가져오지 않아 `ZLinkSpotActorJoinDispatch` mirror가
    되지 않게 했다. `ZLinkSpotActorJoinDispatch`는 이제 native dispatch handler wiring과 native actor join
    drain 조정만 남는다. 이 시점의 `runtime/spots/index.ts`는 1786줄이고, 새 route-frame dispatch 파일은
    240줄이다.
  - 2026-07-10 추가 적용: `DefaultZLinkSpotManager`의 routed SPOT packet send/request 실행은
    `spot-routed-spot-packet-dispatch.ts`로 분리했다. 새 파일은 active Spot 조회 결과에 대한 packet handler
    selection, serial 실행, missing/exception error report만 소유한다. Spot 생성/종료, location claim,
    actor join/leave 상태를 가져오지 않으므로 manager mirror가 되지 않게 했다. 이 시점의
    `runtime/spots/index.ts`는 1716줄이고, 새 routed SPOT packet dispatch 파일은 135줄이다.
  - 2026-07-10 추가 적용: Entry Spot과 user Spot context 생성은 `spot-context.ts`로 분리했다. 새 파일은
    context 객체 조립, `addTimer()`의 timer registry wiring, `runWorker()` call 생성, Entry Spot `destroyActor`
    guard만 소유한다. activation map, Spot 생성/종료, native dispatch, actor state를 가져오지 않으므로 activation
    manager mirror가 되지 않게 했다. 이 시점의 `runtime/spots/index.ts`는 1643줄이고, 새 context 파일은
    142줄이다.
  - 2026-07-10 추가 적용: native Spot dispatch handler fan-in은 `spot-actor-join-dispatch.ts`로 분리했다.
    새 파일은 native dispatch event를 actor join, actor lifecycle, actor packet, subscription, route-frame drain에
    연결하고, `recvActorJoin()` drain의 idle 대기 규칙만 소유한다. Entry/user Spot activation map, provider 생성,
    location claim, lifecycle 실행은 가져오지 않으므로 activation manager mirror가 되지 않게 했다. 기존
    `ZLinkSpotActorJoinDispatch`가 이미 각 drain 모듈을 조정하던 내부 객체였기 때문에 pass-through wrapper가
    아니라 native dispatch fan-in의 단일 소유자로 남는다. 이 시점의 `runtime/spots/index.ts`는 1429줄이고,
    새 dispatch 파일은 270줄이다.
  - 검증: `node node_modules/typescript/bin/tsc -p packages/framework/tsconfig.json --noEmit --noUnusedLocals --noUnusedParameters`,
    `npm run typecheck`, `npm run lint`, `npm run build` 통과. 관련 계약 테스트는 Spot/Entry/channel/stream route
    dispatch 시나리오 77개가 통과했다. 샘플은 `./samples/run_samples.sh TicTacToe.Ts Bingo.Ts DeliveryDispatch.Ts
    SupportChat.Ts GameQuest.Ts ShoppingMall.Ts`로 6개 모두 `PASS`를 확인했다. unit/contract는
    `channel-client.test.js` 제외 전체 464개가 통과했고, `channel-client.test.js`는 전체 단일 실행 대신 route,
    codec/fanout, CH/DERR/REG/DSC, backpressure, request dispatcher/error reporter 묶음을 분할 실행해 통과했다.
    사용자가 요청한 대로 full e2e는 실행하지 않았다.
  - 2026-07-10 추가 적용: Entry Spot activation은 `spot-entry-activation.ts`로 분리했다. 새 파일은 Entry Spot
    provider 생성, context 주입, timer/handler registration 적용, native join dispatch attach, Entry actor packet
    mailbox dispatch만 소유한다. node runtime manager의 node map, publisher bundle, start/stop orchestration은
    가져오지 않으므로 runtime manager mirror가 되지 않는다. 새 파일은 366줄로 작지는 않지만 기존 activation
    객체의 물리 이동이며, activation 내부 책임을 넘어선 새 public surface나 helper cascade를 만들지 않았다.
    이 시점의 `runtime/spots/index.ts`는 1167줄이다.
  - 2026-07-10 보정: `contract-surface.test.js`가 spec catalog의 `ZLinkActorJoinAdmission`과
    `ZLinkActorTransfer<TActor>` 선언 누락을 잡아 `contracts/Spots/Contracts.ts`에 선언만 추가했다. 이 변경은
    spec 문서에 이미 있는 public 타입 이름을 declarations에 맞춘 것이며, actor transfer 동작이나 새 runtime
    경로를 구현하지 않았다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`,
    `npm run build`, `contract-surface.test.js` 통과. Entry/Spot/channel/stream 관련 계약 테스트 98개가 통과했다.
    샘플은 6개 모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개가 통과했고,
    `channel-client.test.js`는 단일 파일 실행 대신 route(22), codec/fanout(15), CH/DERR(4), REG/DERR/CH(3),
    DSC/backpressure(6), request dispatcher/error reporter(5), direct client/module(3) 묶음으로 분할 실행해
    모두 통과했다. full e2e는 실행하지 않았다.
  - 2026-07-10 추가 적용: SpotNode runtime orchestration은 `spot-node-runtime-manager.ts`로 분리했다. 새 파일은
    SpotNode 생성/설정, Entry activation 시작/폐기, internal Entry route dispatch, publisher bundle 제출,
    location auto-connect loop 시작/정지만 소유한다. user Spot activation map, user Spot provider 생성, user
    Spot lifecycle 실행은 가져오지 않으므로 `DefaultZLinkSpotManager` mirror가 되지 않는다. 기존 독립 클래스의
    파일 이동에 가깝지만 SpotNode lifecycle과 Entry orchestration 지식을 `index.ts`에서 제거해 남은 user Spot
    manager 책임을 더 좁혔다. 이 시점의 `runtime/spots/index.ts`는 834줄이고, 새 SpotNode runtime manager 파일은
    420줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. SpotNode/Entry/location/route/channel/stream 관련 계약 테스트 89개가 통과했다. 샘플은
    `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts` 6개
    모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개가 통과했고,
    `channel-client.test.js`는 route(22), codec/fanout(15), CH/DERR 단건 4개, REG/DERR/CH(3),
    DSC/backpressure(6), request dispatcher/error reporter(5), direct client/module(3) 묶음으로 분할 실행해
    모두 통과했다. full e2e는 실행하지 않았다.
  - 2026-07-10 추가 적용: user Spot active/pending activation 상태는 `spot-activation-registry.ts`로 분리했다.
    새 파일은 active map, pending getOrCreate 공유, type mismatch 판정, Created→Existing 결과 변환,
    close 가능 여부 판정, spot rid 발급 규칙을 함께 소유한다. 단순 Map wrapper가 아니라 `getOrCreate` 동시성
    규칙과 public create result 해석을 감추므로 shallow helper가 되지 않는다. user Spot provider 생성,
    lifecycle callback 실행, native dispatch wiring은 여전히 `DefaultZLinkSpotManager`에 남겨 activation builder
    mirror를 만들지 않았다. 이 시점의 `runtime/spots/index.ts`는 772줄이고, 새 registry 파일은 131줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. Spot manager create/getOrCreate/find/list/close/type mismatch 및 관련 actor/route/stream 계약 테스트
    127개가 통과했다. 샘플 6개는 모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개가
    통과했고, `channel-client.test.js`는 route(22), codec/fanout(15), CH/DERR(4), REG/DERR/CH(3),
    DSC/backpressure(6), request dispatcher/error reporter(5), direct client/module(3) 묶음으로 분할 실행해
    모두 통과했다. full e2e는 실행하지 않았다.
  - 2026-07-10 추가 적용: user Spot actor membership 전이는 `spot-actor-membership.ts`로 분리했다. 새 파일은
    actor join admission commit, joined actor map 갱신, Entry Spot leave 알림, user Spot leave/disconnect callback,
    routed actor leave commit, Entry Spot 재조인 조건을 함께 소유한다. activation map, provider 생성, native dispatch
    wiring, routed packet dispatch는 가져오지 않으므로 `DefaultZLinkSpotManager`나 activation builder mirror가 되지
    않는다. 이 시점의 `runtime/spots/index.ts`는 695줄이고, 새 membership 파일은 169줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. actor join/leave/disconnect, routed actor, Entry Spot, bound-session, route 관련 계약 테스트 97개가
    통과했다. 샘플은 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
    `ShoppingMall.Ts` 6개 모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개가 통과했고,
    `channel-client.test.js`는 route/routeMesh(22), codec/fanout/client/request-dispatch(7),
    CH/DERR/REG/DSC/backpressure(14) 묶음으로 분할 실행해 모두 통과했다. 사용자가 요청한 대로 full e2e는
    실행하지 않았다.
  - 2026-07-10 추가 적용: user Spot activation lifecycle은 `spot-activation.ts`로 분리했다. 새 파일은 user Spot
    provider/context 생성, handler/timer 적용, location claim rollback, create/reject reply 정규화, close/release,
    native actor join dispatch wiring, user Spot actor packet dispatch를 소유한다. `DefaultZLinkSpotManager`에는
    factory 등록 검증, create/getOrCreate 요청 소유권, active/pending registry orchestration, public manager facade만
    남겼다. 이 시점의 `runtime/spots/index.ts`는 508줄이고, 새 activation lifecycle 파일은 417줄이다.
    `spot-activation.ts`는 크기가 있어 계속 주시해야 하지만, activation lifecycle/rollback/native join wiring을 한
    곳에 숨기고 manager public orchestration을 복제하지 않으므로 이번 분리 결과가 즉시 새 POSD 리팩토링 대상이
    되지는 않는다고 판정했다.
  - 2026-07-10 보정: `contract-surface.test.js`가 spec catalog의 `ZLinkActorTransferAdapter<TActor>` 선언 누락을
    잡아 `contracts/Spots/Contracts.ts`에 선언만 추가했다. 이 변경은 spec 문서에 이미 있는 public 타입 이름을
    declarations에 맞춘 것이며, actor transfer 동작이나 새 runtime 경로를 구현하지 않았다.
  - 최종 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`,
    `npm run build` 통과. Spot manager create/getOrCreate/location rollback/close, actor join/leave/disconnect,
    routed actor, Entry Spot, bound-session, route, stream-bound actor 관련 계약 테스트 109개가 통과했다. 샘플은
    `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`, `ShoppingMall.Ts` 6개
    모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개가 통과했고,
    `channel-client.test.js`는 route/routeMesh(22), codec/fanout/client/request-dispatch(7),
    CH/DERR/REG/DSC/backpressure(14) 묶음으로 분할 실행해 모두 통과했다. full e2e는 실행하지 않았다.
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
  - 2026-07-10 부분 적용: channel runtime transport surface는 `channel-transports.ts`로 분리했다.
    `ZLinkChannelClientTransport`, `ZLinkSpotPublisherClientTransport`, `ZLinkRouteClientTransport`,
    `ZLinkRuntimeChannelTransport`, `ZLinkRuntimeRouteTransport`를 이동했고, `channels/index.ts`는 기존 public import
    경로를 유지하도록 re-export만 남겼다. 새 파일은 `ZLinkChannelRuntimeManager` 구체 클래스를 import하지 않고
    send/request/publish/route-to-Spot에 필요한 structural capability만 받으므로 runtime manager mirror가 되지
    않는다. 이 시점의 `runtime/channels/index.ts`는 3499줄이고, 새 transport 파일은 271줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. channel/route transport 관련 `channel-client.test.js` route/routeMesh(22), codec/fanout/publish(7),
    direct client/NestJS route transport(49), CH/DERR/REG/DSC/backpressure(14), bound-session/route 주변(29) 묶음이
    통과했다. 샘플은 `TicTacToe.Ts`, `Bingo.Ts`, `DeliveryDispatch.Ts`, `SupportChat.Ts`, `GameQuest.Ts`,
    `ShoppingMall.Ts` 6개 모두 `PASS`를 확인했다. `channel-client.test.js` 제외 unit/contract 464개도 통과했다.
    full e2e는 실행하지 않았다.
  - 2026-07-10 부분 적용: live socket option adapter와 runtime option facade는 `channel-socket-options.ts`로
    분리했다. 새 파일은 channel name 검증, socket weight/high-water-mark/send-timeout/max-message-size 검증,
    backend router socket에 대한 public `ZLinkSocketConfig` adapter만 소유한다. `ZLinkChannelRuntimeManager`
    구체 클래스를 import하지 않고 `clientServerServerSocket()`/`routeMeshSocket()` capability만 요구하므로
    runtime manager mirror가 되지 않는다. 이 시점의 `runtime/channels/index.ts`는 3375줄이고, 새 socket
    options 파일은 138줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. socket option/channel transport 관련 `channel-client.test.js`/`nestjs-module.test.js` 묶음 49개,
    DSC/backpressure 묶음 5개, codec/fanout/publish/request-dispatch 묶음 9개, DERR-002/DERR-007/REG-003 단건,
    그리고 넓은 CH/DERR 묶음에서 timeout 전에 PASS가 확인된 CH-001/CH-006/DERR-001을 확인했다. 샘플 6개는 모두
    `PASS`였고, `channel-client.test.js` 제외 unit/contract 464개도 통과했다. full e2e는 실행하지 않았다.
  - 2026-07-10 부분 적용: dispatch error reporting은 `dispatch-error-reporter.ts`로 분리했다. 새 파일은
    `ZLinkDispatchErrorSink`, `ZLinkDispatchErrorReporter`, error object에서 `errorType`/`errorMessage`를 뽑는
    formatting 규칙만 소유한다. `channels/index.ts`는 기존 import 경로를 유지하도록 re-export하고, channel runtime
    lifecycle/state는 새 파일로 넘기지 않았다. 이 시점의 `runtime/channels/index.ts`는 3306줄이고, 새 reporter
    파일은 83줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. dispatch error/message-flow 관련 focused 묶음 11개가 통과했다. 넓은 error/flow 묶음은 중복 범위가 크고
    오래 걸려 중단했으며, 중단 전 DERR-001/DERR-002는 PASS를 확인했다. 샘플은 6개 모두 `PASS`였고,
    `SupportChat.Ts`에서 한 번 segfault 로그가 나와 단독 재실행했을 때 segfault 없이 `PASS`를 확인했다.
    `channel-client.test.js` 제외 unit/contract 464개도 통과했다. full e2e는 실행하지 않았다.
  - 2026-07-10 부분 적용: raw SPOT route bridge reply queue는 `spot-route-bridge-raw-reply.ts`로 분리했다. 새 파일은
    route channel별 pending raw request FIFO, timeout/abort handler 정리, 완료 후 dequeue, 이미 완료된 reply payload
    close, raw bridge reply marker 판정을 함께 소유한다. channel runtime manager는 queue 인스턴스를 호출만 하며,
    route lifecycle이나 socket registry 상태는 새 파일로 넘기지 않았다. 이 시점의 `runtime/channels/index.ts`는
    3193줄이고, 새 raw reply 파일은 121줄이다.
  - 검증: strict tsc(`--noUnusedLocals --noUnusedParameters`), `npm run typecheck`, `npm run lint`, `npm run build`
    통과. route bridge/raw SPOT/accepted Spot route 관련 묶음 13개와 route/bound-session 주변 묶음 52개가 통과했다.
    샘플은 6개 모두 `PASS`였고, `channel-client.test.js` 제외 unit/contract 464개도 통과했다. full e2e는 실행하지
    않았다.
- [ ] **D3. (R2, P0) `runtime/host/index.ts` (2489줄)** — god-class `ZLinkFrameworkRuntimeHost`(125-1840, ~1716줄, 파일 최대 심볼). public 생성자·NestJS 사용 유지, start/stop 조정 + facade만 남김
  - **god-class 내부에서 추출**: `ZLinkBoundSessionRelay`(=RemoteActorDispatchGateway, ~880-1793, ~900줄, 원격 bound-session + actor packet relay 전량) — **벤치**(per-actor-packet hot). `ZLinkActorRuntimeOptionsFactory`(485-718, actor manager/client 옵션), `ZLinkLocationRuntimeOwner`(744-869, store/lifecycle/resolver/event sink), `MeshRouterResolver`(1764-1839).
  - **god-class 종료(1840) 이후 같은 파일의 top-level 자유 함수/클래스(클래스 메서드 아님) — 파일 단위 이동**: route-wire decoders(`sessionActorPacketTargetKey`+`decodeRemote*` 1842-2050) → `spots/route-wire-codec.ts`(encode 짝 이미 거기), `ZLinkNativeFallbackBoundSession(+SendCall)`(2052-2251)→streams, `ZLinkLocalFirstActorJoinCoordinator`+`LazyNative`(2253-2381)→actors, `ZLinkMonitoringRuntime`(2383-2457)→별도 파일.
  - 검증: `test/contract/*runtime*.test.js`, `stream-runtime.test.js`, `channel-client.test.js`, `actor-manager.test.js`, `monitoring-runtime.test.js`.
- [x] **D4. `runtime/streams/index.ts` (2229줄)** — god-class `ZLinkStreamBindingRuntime`(784-1319)
  - `SessionActorCoordinator`(803-920,1216-1274) / `BoundSessionService`(922-1148,1387-1444, C5와 함께) / `BoundActorRelaySender`(1150-1190) / `ActorSessionBindingRegistry`(1321-1385, 이미 깨끗 → 파일만 분리).
  - 부차: `stream-frame-factory.ts`(1446-1570), `session-context.ts`(1572-1719), `session-requests.ts`(1801-1918), `stream-session-runtime.ts`(`ZLinkStreamSessionNodeRuntime` 531-751 + `SessionRuntime` 298-529 + `SerialExecutor` 753-782).
  - 2026-07-09 부분 적용: actorId와 session context, actor, binding token의 현재 매핑을 소유하는
    `ZLinkActorSessionBindingRegistry`를 `actor-session-binding-registry.ts`로 분리했다. 새 모듈은 stream
    runtime의 구체 클래스 대신 작은 structural interface와 generic route 타입만 알고, `streams/index.ts`는
    `DefaultZLinkSessionContext`/`DefaultZLinkSessionActor`를 타입 인자로 연결한다. 따라서 registry 분리가
    stream runtime 세부 구현을 새 파일에 누출하거나 단순 pass-through wrapper를 만들지 않는다.
    이어서 JSON frame message 생성, reply header echo, 압축 codec 선택, 기본 binding `Message` 생성을
    `stream-frame-factory.ts`로 분리했다. `zlinkStreamLz4CompressionCodec`은 `streams/index.ts`에서 재-export해
    기존 public import 경로를 유지했고, 새 모듈은 frame 생성 규칙과 compression 경계만 소유한다.
    이후 pending request lifecycle과 request sequence 발급은 `session-requests.ts`로 분리했다.
    `ZLinkPendingSessionRequest`는 기존처럼 `streams/index.ts`에서 re-export해 import 경로를 유지하고,
    response payload 복사 규칙은 `stream-message-utils.ts`가 소유하게 했다.
    session context가 들고 있던 local actor/token map은 `session-local-actors.ts`로 분리했다. 새 모듈은
    `actorId`만 요구하는 generic actor 타입으로 동작하므로 session context/actor 구현을 새 파일에 묶지 않는다.
    session `send()`/`reply()`와 bound-session send call 객체는 `session-calls.ts`로 분리했다. 새 파일은
    `ZLinkSessionCallContext`와 `ZLinkBoundSessionSendRuntime`이라는 작은 structural interface만 요구하므로
    `DefaultZLinkSessionContext`나 `ZLinkStreamBindingRuntime` 구현 세부사항에 결합하지 않는다. 따라서 call 객체
    분리가 단순 pass-through wrapper나 concrete runtime mirror가 되는 새 POSD 대상으로 번지지 않았는지 확인했다.
    이후 session context, session client/actors facade, session actor, bound-session facade를 `session-context.ts`로
    분리했다. 새 파일은 `ZLinkSessionContextRuntime`과 `ZLinkBoundSessionRuntime` capability만 요구하고,
    `ZLinkStreamBindingRuntime` concrete class를 import하지 않는다. context가 소유하는 dispatch header,
    pending request, local actor binding 상태만 한곳에 남겨 runtime god-class의 mirror가 되는 새 POSD 대상을 만들지
    않았는지 확인했다.
    local bound-session response target과 error payload shape는 `bound-session-response-target.ts`로 분리했다.
    새 파일은 frame factory와 `stream.writeRaw()` capability만 요구하므로 session context나 binding runtime 전체를
    다시 비추는 wrapper가 아니며, response/error frame 작성 규칙을 한곳에 묶는다.
    backend stream socket을 public `ZLinkStream` 계약으로 보이게 하는 adapter는 `managed-stream.ts`로 분리했다.
    session callback 직렬 실행 정책은 `session-serial-executor.ts`가 소유하고, provider resolver를 통한 session
    생성 정책은 `session-provider.ts`가 소유한다. session runtime 본체 이동 전 순환 import를 만들지 않기 위한
    선행 분리이며, 각 파일은 adapter/queue/provider 생성이라는 독립 책임을 가져 단순 파일 쪼개기나 pass-through가
    되지 않는지 확인했다.
    이후 `ZLinkStreamSessionRuntime`과 `ZLinkStreamSessionNodeRuntime` 본체를 `stream-session-runtime.ts`로
    분리했다. 새 파일은 session lifecycle, framed packet dispatch, monitor disconnect 처리, endpoint가 없는
    disconnect 지연 처리만 소유하고, `ZLinkStreamBindingRuntime` 구체 클래스 대신 session context를 만들 수 있는
    작은 capability만 요구한다. `streams/index.ts`는 기존 public constructor에서 `bindingRuntime`을 생략할 수
    있던 동작을 유지하기 위한 얇은 호환 wrapper만 남겼다. 따라서 session runtime 이동 결과가 다시 concrete runtime
    mirror나 pass-through 중심의 POSD 대상으로 번지지 않았는지 확인했다.
    이후 bound-session frame 전송, transport 전송, native `SessionRelay` 재시도, disconnect, remote bind relay는
    `bound-session-service.ts`로 분리했다. 새 파일은 route registry와 frame factory만 받아 전송 규칙을 소유하고,
    `ZLinkStreamBindingRuntime` 전체를 import하지 않는다. `ZLinkStreamBindingRuntime`에는 public 계약을 유지하는
    facade 메서드와 actor binding 조정만 남겨, bound-session service가 runtime god-class의 복사본이나 얕은
    wrapper가 되지 않았는지 확인했다.
    이후 session actor bind, `bindOrGet`, actor ref 갱신, remote bound-session bind relay 조정은
    `session-actor-coordinator.ts`로 분리했다. 새 파일은 route registry, remote bind relay capability, session
    actor runtime capability만 받아 actor binding lifecycle을 소유하고, frame 생성이나 bound-session 전송 책임을
    다시 가져오지 않는다. `ZLinkStreamBindingRuntime`에는 기존 public 메서드 표면과 session context/frame facade만
    남겨, coordinator 분리가 새 god-class나 pass-through 파일이 되지 않았는지 확인했다.
    이후 session actor `relay()`와 `notifyDisconnected()` 경로는 `bound-actor-relay-sender.ts`로 분리했다.
    새 파일은 active dispatch header 확인, optional user relay hook, managed stream `SessionRelay` frame 전송,
    disconnect 알림 hook만 소유한다. actor bind/rebind와 bound-session send를 다시 끌어오지 않아 D4 분해 결과가
    또 다른 POSD 대상이 되는 넓은 coordinator로 번지지 않았는지 확인했다.
    D4 완료 시점의 `streams/index.ts`는 public export/facade와 `ZLinkStreamBindingRuntime`의 기존 public 표면을
    유지하는 437줄 파일이 되었다. 계획서가 지목한 `SessionActorCoordinator`, `BoundSessionService`,
    `BoundActorRelaySender`, `ActorSessionBindingRegistry`, frame factory, session context/request/runtime 책임은 모두
    독립 파일로 이동했고, 새 파일들이 다시 `ZLinkStreamBindingRuntime` 전체를 import하는 mirror 구조가 아닌지 확인했다.
- [x] **D5. `runtime/locations/index.ts` (1965줄)** — 6개 도메인(store/write·query/lease/resolver/auto-connect/lifecycle)이 한 TU
  - `in-memory-store.ts`(77-388+1764-1822+1947-1961), `runtime.ts`(390-855), `lease-tracker.ts`(930-1007), `resolvers.ts`(1009-1154, C8 지점), `auto-connect.ts`(857-928+1156-1497+1830-1920), `lifecycle.ts`(1499-1762, B2 수정 + dotnet E1 동형 3책임 재분해: actor/spot claim + actor-session route), `internal-util.ts`.
  - 2026-07-09 부분 적용: owner lease snapshot cache와 live-row 판정은 `lease-tracker.ts`로 분리했다.
    `ZLinkOwnerLeaseTracker`와 `ZLinkLiveRowFilter`는 lease store와 polling option만 알고, location runtime/resolver
    전체를 import하지 않는다. `locations/index.ts`는 기존 public import 경로를 유지하도록 두 class를 re-export한다.
    이 분리는 D5의 resolver/runtime 분해를 준비하는 독립 책임 이동이며, live-row filter가 resolver를 다시 비추는
    얕은 wrapper가 되지 않았는지 확인했다.
    이후 store 기반 peer/route/spot/actor resolver, readiness probe, SPOT route resolver는 `resolvers.ts`로
    분리했다. 새 파일은 location store interface, live-row filter, resolve miss event만 알고, location runtime
    lifecycle이나 auto-connect loop를 import하지 않는다. `locations/index.ts`는 기존 public import 경로를 유지하기
    위해 resolver class들을 re-export한다. 따라서 resolver 분리가 runtime god-file의 축소가 아니라 resolver 전용
    god-class로 번지는지 다시 점검했고, 현재는 store lookup과 live-row 판정만 소유하는 경계로 남겼다.
    이후 auto-connect type/executor/publisher 계약은 `auto-connect-types.ts`, peer 선택 규칙은
    `auto-connect-planner.ts`, store publish와 desired-set 조정은 `auto-connect-reconciler.ts`, polling/watch loop는
    `auto-connect-loop.ts`로 분리했다. 처음에는 `auto-connect.ts` 단일 파일로 옮겼지만, planner/reconciler/loop가
    한 파일에 다시 모여 새 POSD 대상이 되는 것을 확인해 내부 파일을 한 번 더 나눴다. `locations/index.ts`는
    기존 import 경로를 유지하는 re-export만 맡고, 새 auto-connect 파일들은 `ZLinkLocationRuntime` 전체가 아니라
    `writePeer`/`removePeer`만 요구하는 작은 publisher 계약을 사용한다. 남은 `locations/index.ts`는 여전히
    in-memory store, runtime, lifecycle이 함께 있으므로 D5의 다음 분리 후보로 남긴다.
    이후 lifecycle은 public facade를 `lifecycle.ts`에 남기고, actor claim/activation/renew/release는
    `actor-location-claims.ts`, spot claim/release는 `spot-location-claims.ts`, actor-session route bind/remove는
    `actor-session-route-claims.ts`, lifecycle이 runtime에 요구하는 최소 쓰기/삭제/ownership handler 계약은
    `lifecycle-runtime.ts`로 분리했다. 처음 `lifecycle.ts` 단일 파일로만 이동했을 때 actor/spot/route 세 책임이
    다시 한 class에 모여 새 POSD 대상이 되는 것을 확인했고, 내부 책임 파일을 추가로 나눴다. facade는 기존
    public 메서드를 유지하는 얇은 호환 계층으로 남기고, 각 내부 파일은 `ZLinkLocationRuntime` concrete type을
    import하지 않는다. 남은 `locations/index.ts`는 942줄이며 in-memory store와 runtime을 함께 담고 있으므로
    D5의 다음 분리 후보로 남긴다.
    마지막으로 in-memory store 구현은 `in-memory-location-store.ts`, runtime 조정과 query/write API는
    `runtime.ts`로 옮겼고, `locations/index.ts`는 기존 import 경로를 유지하는 6줄 re-export 배럴이 되었다.
    store 파일은 row table, generation fence, paging/filter, owner lease, change stamp를 한 store 구현 안에
    숨기고 runtime concrete type을 import하지 않는다. runtime 파일은 store 구현 세부를 알지 않고 store
    interface와 lease/live-row filter, event sink만 사용한다. 분리 후 가장 큰 파일은 `runtime.ts` 509줄과
    `in-memory-location-store.ts` 427줄이며, 각각 runtime 조정 책임과 in-memory store 구현 책임으로 나뉘어
    D5 결과가 다시 같은 POSD 대상이 되지 않았는지 확인했다.
- [x] **D6. `runtime/actors/index.ts` (1812줄)** — 선행 리팩토링(`ZLinkActorCreationCoordinator`, `applyRemoteJoinResult`) 후 재성장
  - `actor-runtime-state.ts`는 actor state, native ref, create operation, remote target snapshot을 소유한다.
    `actor-context.ts`는 actor context, join call, unbound session 오류를 소유하고 state 파일이 context 생성을
    알지 않도록 `ensureContext(() => ...)` 경계로 연결했다. `actor-mailbox.ts`는 per-actor serial mailbox만
    소유한다.
  - `spot-actor-dispatch.ts`는 actor packet registry, reply option snapshot, SPOT actor dispatcher를 함께 소유한다.
    `actor-remote-joiner.ts`는 native/remote join transport 선택과 accepted join 상태 반영을 소유하고,
    remote packet 상수와 join payload/base64/hex wire shape은 `actor-remote-wire.ts`로 분리했다. 처음 remote
    joiner에 wire builder까지 함께 둔 결과가 다시 600줄대 POSD 대상이 되는 것을 확인해 wire 모듈을 추가로
    나눴다.
  - `actor-creation.ts`는 factory/provider 해석, location claim 뒤 actor 생성, native ref 알림을 소유한다.
    `runtime/actors/index.ts`는 기존 public import 경로를 유지하는 re-export와 `DefaultZLinkActorManager`,
    `ZLinkActorDispatchRouter` facade만 남아 415줄이다. 새 파일 중 가장 큰 `actor-remote-joiner.ts`는 509줄이지만
    wire shape을 알지 않고 join coordinator 정책만 소유하므로 추가 분리는 accessor threading에 가까운 것으로
    판단했다.
- [ ] **D7. (R5, P1) `nestjs/src/index.ts` (2902줄 단일 파일)** — root export만 유지(`exports` 없이 `main`만 있으므로 import 경로 증가 금지), 구현 분해
  - `framework-loader.ts`(67-116 + `nest-integration` lazy load), `tokens.ts`(350-367), `contracts.ts`(118-348), `handler-metadata.ts`(369-375,1114-1239 decorator metadata append/read), `decorators.ts`(377-599), `options-builder.ts`(601-1112 fluent builder), `registration-composer.ts`(1424-1940 discovered→framework registration options), `discovery.ts`(1241-1286,1971-2504, C9 통합 후 provider discovery/scan), `module.ts`(1288-1422), `providers.ts`(2506-2897 Nest provider 배열 + runtime/manager factory). 순환 참조 주의(데코레이터↔메타데이터, 모듈↔프로바이더는 함수 경계). 검증: public export 회귀(`nestjs-module.test.js`) + `sample-regression.test.js`.
- [x] **D8. `Registration.ts` (1333줄)** — validators는 이미 추출됨, 잔여 4관심사
  - `RegistrationTypes.ts`(47-371 계약 타입 41개 + 예외), 팩토리 진입점 유지(373-416), `RegistrationBuilders.ts`(418-928 빌더 11개 + C10 통합), `RegistrationCodecRegistry.ts`(925-996), `RegistrationNormalizers.ts`(1084-1169,1198-1333 to*/normalize* + C10 플래그 모듈).
  - 2026-07-09 부분 적용: codec registry 상태와 content type 검증을 `RegistrationCodecRegistry.ts`로
    분리했고, `ZLinkConfigurationException`은 `ConfigurationException.ts`로 이동한 뒤 `Registration.ts`에서
    re-export해 public import 경로를 유지했다. 이후 registration 정규화 함수와 capability helper를
    `RegistrationNormalizers.ts`로 분리했고, `Registration.ts`에서 기존 helper export를 유지했다.
    이어서 fluent builder 구현과 `createFrameworkOptions()` 조립 책임을 `RegistrationBuilders.ts`로 분리했고,
    public 계약 타입 묶음은 `RegistrationTypes.ts`로 이동했다. builder 분리 결과는
    `DefaultSpotNodeBuilder`/`DefaultRouteChannelOptionsBuilder`처럼 실제 상태 조립을 소유하는 구현 모듈이며,
    단순 pass-through 파일을 새로 만들지 않았다. `Registration.ts`는 public import 경로와 factory 진입점을
    유지하는 68줄 facade가 되었고, 내부 구현 모듈은 type-only dependency를 `RegistrationTypes.ts`로 직접
    가져오도록 정리했다. D8 분해 결과가 다시 POSD 대상이 되는 얕은 wrapper로 남지 않았는지 확인했다.
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
