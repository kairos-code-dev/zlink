# S8 NODE bindings 전환 리뷰 iteration-3 — R1(codex) review

독립 리뷰어 R1. 다른 리뷰어·coordinator 해석을 판정 근거로 쓰지 않았다. 정적 대조만 수행(build/실행 없음).

## 1. Scope 확인
- 파일 수: 140 (시작·종료 동일).
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `4c772436d48795958da6e8cdf8e071962f716b9d33c391a1874c382892ccfdf6` — prompt 값과 일치.
- 대상 commit `bc409293a`. 현재 HEAD `4f502b1745`의 조상이며 `git diff bc409293a HEAD -- bindings/node/{src,native/src,samples,binding.gyp,package.json}` 무변경 → scope 내용은 target commit과 byte 동일.
- Coordinator 실행 증거(manifest): addon node-gyp green·tsc(src)·tsc(samples) green·no-hit 0 — 재실행하지 않고 정적 대조로 교차확인.

## 2. iter-1(NF1–NF7)·iter-2 finding 해소 판정 — 전량 해소(source 대조)

| finding | 판정 | 근거 |
|---|---|---|
| NF1 wire enum | 해소 | `dispatch.ts` `ReceiveKind`(1-13)=`zlink_mesh_record_kind_t`, `OperationKind`(1-11)=`zlink_mesh_operation_kind_t`(`ActorJoin=7`=ACTOR_JOIN·`ActorLookup=5`=ACTOR_LOOKUP), `MeshDestinationKind`(1-5)=`zlink_mesh_destination_kind_t`, `mesh_node.ts` `MeshNodeState`(1-7)=`zlink_mesh_node_state_t`(`Stopped=6`·`Error=7`) 전값 일치 |
| NF2 Router spot | 해소 | `routerSpot*`·Router의 `sendToSpot/requestToSpot/replyToSpot` = 0 hit. `sendToSpot` 유일 hit은 Spot 표면(`contracts/service/spot.ts:48`·`runtime/service/spot.ts:73` → Core `zlink_spot_send_to_spot`) |
| NF3 kind_data | 해소 | `dispatch.ts:108` `ReceiveKindData` union(SpotControl/join-completion/actor-location/SendReady/TransferControl) + `kindData: ReceiveKindData \| null` 필드 |
| NF4 ready handler | 해소 | `meshNodeSetReadyHandler`/`meshNodeUnsetReadyHandler` 선언·등록, contract `setReadyHandler(handler)=>number`(JS 반환 mask) |
| NF5 close 결과 | 해소 | iter-2 검증(`mesh_node_destroy != CLOSE_OK` throw) 유지 |
| NF6 count 타입 | 해소 | iter-2 검증(number 정합) 유지 |
| NF7 transfer | 해소 | `meshNodeActorTransfer{Prepare,Commit,Activate,Abort}` 선언·등록, `transfer.ts` contract |
| iter-2 dead `stream*Actor` | 해소 | `binding_socket.ts`의 `streamBindActor/streamBoundActors/streamSendBoundActorPart/streamUnbindActor` = 0 hit. live 경로는 서비스 표면 `streamSessionBindActor` 등(`binding_service.ts`) |
| iter-2 result enum 최신값 | 해소 | 아래 §3 I1 표대로 8종 전값 Core 일치 |
| iter-2 MonitorSourceKind 값 | 해소 | `{ Socket: 1 }`만 — Core `ZLINK_MONITOR_SOURCE_SOCKET=1` 유일값 일치 |

해소된 finding은 새 반례 없음 → 재개하지 않음.

## 3. 전체 scope 3축 재검토 — Finding / Evidence / Verdict

### I1 계약 일치

result enum 8종 Core(`zlink_errno.h`) value-by-value 대조:

| enum | 멤버수 | Core 대조 |
|---|---|---|
| SubmitResult | 14 | 0·1·2·3·4·5·6·7·8·9·10·11·12·13 = `zlink_submit_result_t` 전값 일치 |
| RequestResult | 14 | 0·101–113 = `zlink_request_result_t` 전값 일치(**Backpressured=113** 포함) |
| RecvResult | 9 | 0·201–208 = `zlink_recv_result_t` 전값 일치(**BufferTooSmall=207·InvalidState=208** 포함) |
| HandlerResult | 7 | 0·301–306 = 일치 |
| CloseResult | 5 | 0·401–404 = 일치 |
| BindResult | 6 | 0·501–505 = 일치 |
| ConnectResult | 9 | 0·601–608 = 일치(**AuthFailed=608** 포함) |
| ConfigResult | 10 | 0·701–709 = 일치(**Conflict=707·BufferTooSmall=708·Busy=709** 포함) |

기타 Core 매핑 enum: `SocketType`(0·0x1001–0x1008)=`zlink_socket_type_t`, `AutoHwmProfile`(0-3)=`zlink_auto_hwm_profile_t`, `RidDuplicatePolicy`(0/1)=`zlink_rid_duplicate_policy_t`, `SubmitRetryMode`(0/1)=`zlink_submit_retry_mode_t`, `Send/RecvFlags`(0/0x1)=`zlink_*_flags_t`, `MonitorEventType`(0x0001–0x8000, 16bit)=`zlink_socket_monitor_event_e` — 전값 일치.

`PollEventFlag`(PollIn=1·PollOut=2·PollErr=4·PollPri=8·PollCompletion=32): 존재 값은 `zlink_poller_event_flag_e`와 일치. `ZLINK_POLLITEMS_DFLT=16`은 poll-readiness 조건이 아닌 내부 기본 플래그로, enum 문서("readable/writable/error" readiness)와 정합하는 의도적 curation(잘못된 값 아님) → 위반 아님.

wire enum(§2 NF1)·transfer·kind_data·ready·close 는 clean.

**Verdict(I1): CLEAN** (finding 0).

### I2 POSD·DDD

- Router↔Spot 도메인 누출(iter-1 NI2-1) 없음: Router 표면에 spot 메서드 0.
- 미등록·타도메인 dead 선언(iter-2 NF2-3) 없음: `binding_socket.ts` 는 socket 도메인 메서드만.
- native 함수선언 ↔ addon 등록 완전 대조: 함수형 native 선언 **175** == `ZLINK_METHOD` 등록 **175**, 양방향 차집합 공집합 → 도메인 표면과 addon 표면 완전 정합, 누출/미등록 0.

**Verdict(I2): CLEAN** (finding 0).

### I3 정리 완결성

no-hit 13토큰(SpotNode·spot_node·route_bridge·subjects·internal_sockets·dispatch_workers·recv_actor_part·zlink_msg_gets·routerSpot·spotNodeActorBindRemoteSession·sync_request_callback·wait_sync_request·subscribe_handler) 전량 0. dead `stream*Actor` 4선언 0. 미등록 native 선언 0(§I2 bijection).

**NF3-1 [I3/I1, low] `MonitorSourceKind` JSDoc가 제거된 "spot's pub/sub side" 개념을 계속 기술 (drift 잔재)**
- `contracts/eventing/monitor.ts:5`: `/** Identifies what a monitored source is (a socket, or a spot's pub/sub side). */`. 바로 아래 enum은 `{ Socket: 1 }`만(iter-2 NF2-2에서 SpotPub/SpotSub 값 제거). Core `zlink_monitor_source_kind_t`도 `SOCKET=1` 유일.
- Evidence: `grep 'pub/sub|SpotPub|SpotSub' src/zlink/contracts` 유일 hit = 이 doc comment. 소비자가 "spot의 pub/sub side" source kind가 존재/방출된다고 오인하도록 기술 — 값은 제거됐으나 값을 서술하던 산문이 잔존(iter-2 NF2-2의 미완 해소).
- Verdict: **NOT CLEAN**. → doc comment를 실제 계약(Socket 단일)에 맞게 정정("a monitored socket").

**Verdict(I3): NOT CLEAN** (finding 1: NF3-1).

## 4. 폐기 no-hit 판정
- no-hit 13토큰 재확인 전량 0 → **통과**.
- forbidden 토큰(`zlink_router_*_spot_part`·`spotNodeActorBindRemoteSession`·`sync_request_callback`·`wait_sync_request`·routerSpot) 0.
- 단, no-hit 토큰 목록 밖의 산문 drift 1건(NF3-1) 잔존 — I3 완결성 위반.

## 5. 종합
- iter-1 NF1–NF7·iter-2 finding(dead stream*Actor·result enum·MonitorSourceKind 값) 전량 해소 확인.
- 신규 finding: NF3-1(MonitorSourceKind doc comment의 removed-concept drift, low).
- iteration-3 기준(각 축 finding 0): I1 CLEAN·I2 CLEAN, **I3 finding 1건**으로 미충족.

BINDINGS REVIEW NOT CLEAN
