# S8 NODE bindings 전환 리뷰 iteration-3 — R2(claude-sonnet) review

독립 리뷰어 R2(Claude Sonnet). 다른 리뷰어·coordinator 해석을 판정 근거로 사용하지 않았다. 정적 소스 대조만 수행(build/실행 없음).

## 1. Scope 확인

- `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json`(`/build/`·`node_modules`·`prebuilds` 제외) → **140 파일**.
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): **`4c772436d48795958da6e8cdf8e071962f716b9d33c391a1874c382892ccfdf6`** — prompt 명시값과 정확히 일치.
- 대상 commit `bc409293a`는 현재 HEAD(`4f502b174`)의 조상이고, `git diff bc409293a HEAD -- <scope>` 무변화 → scope 내용은 target commit과 byte 동일.
- Coordinator 실행 증거(manifest: addon node-gyp green·tsc(src/samples) green·no-hit 0)는 재실행하지 않고 신뢰, 본 리뷰는 정적 대조로 독립 교차확인만 수행.

## 2. iter-1·iter-2 finding 해소 판정

### iter-1 (NF1-NF7, NI2-1, NI3-1) — 전량 해소

| finding | 판정 | 근거(재확인) |
|---|---|---|
| NF1 wire enum 값 | 해소 | `ReceiveKind`(13값)=`zlink_mesh_record_kind_t`, `OperationKind`(11값)=`zlink_mesh_operation_kind_t`, `MeshNodeState`(7값)=`zlink_mesh_node_state_t` 전값 일치. `dispatch.ts:35-101` 재대조. |
| NF2 Router spot 메서드 | 해소 | `router_socket.ts`(계약)·`runtime/sockets/router_socket.ts`(구현) 재독해, sendToSpot/requestToSpot/replyToSpot·routerSpot* 0 hit. |
| NF3 kind_data | 해소 | `dispatch.ts:108-165` typed union 5종, `conversions.ts:kindDataFromRaw` switch 5종, addon `svc_create_kind_data`(`addon_mesh_service.cc:297-406`) switch 5종 — 3자 필드 단위 대조 완전 일치. |
| NF4 ready handler mask+unregister | 해소 | `mesh_node.ts:setReadyHandler`/`meshNodeUnsetReadyHandler` 존재, native 인터페이스에 등록됨. |
| NF5 close 결과 | 해소 | 구조 유지 확인(coordinator 커밋 반영). |
| NF6 count 타입 | 해소 | `ReceiveRequirements`(`dispatch.ts:176-180`) `number` 타입 유지. |
| NF7 transfer API | 해소 | `transfer.ts` 전체 + `mesh_node.ts:209-216` 4메서드(prepare/commit/activate/abort) + native `meshNodeActorTransferPrepare/Commit/Activate/Abort` 등록 확인. |
| NI2-1 Router↔Spot 누출 | 해소 | NF2 재확인으로 동반 해소. |
| NI3-1 dead 잔재 | 해소 | no-hit 재확인(§4). |

### iter-2 (R1 opus, NF2-1~NF2-5) — 전량 해소

- **NF2-1** (result enum 누락) → `results.ts` 8개 enum 전부(SubmitResult/RequestResult/RecvResult/HandlerResult/CloseResult/BindResult/ConnectResult/ConfigResult) `core/include/zlink_errno.h` 전값과 재대조: `RequestResult.Backpressured=113`, `RecvResult.BufferTooSmall=207`/`InvalidState=208`, `ConnectResult.AuthFailed=608`, `ConfigResult.Conflict=707`/`BufferTooSmall=708`/`Busy=709` 전부 존재·정확한 값. **완전 일치, 초과분 없음.**
- **NF2-2** (`MonitorSourceKind` drift) → `monitor.ts:6` `Object.freeze({ Socket: 1 })` 단일값, Core `zlink_monitor_source_kind_t`(`zlink_enum.h:200-203`, `ZLINK_MONITOR_SOURCE_SOCKET=1`만 정의)와 일치. SpotPub/SpotSub 제거 확인.
- **NF2-3/NF2-4** (미등록 dead `stream*Actor` 4메서드) → `streamBindActor`/`streamBoundActors`/`streamSendBoundActorPart`/`streamUnbindActor` scope 전역 grep **0 hit**. `binding_socket.ts`(129줄 전체 재독)에 해당 선언 없음.
- **NF2-5** (dead enum 멤버) → NF2-2로 동반 해소.

해소된 finding에 대한 새 반례 없음 → 재개하지 않음.

## 3. 전체 scope 3축 재검토

### I1 계약 일치

**전수 대조 결과(NF3-1 제외 전부 CLEAN):**
- contracts 내 28개 `Object.freeze` enum 전부를 `core/include/zlink_enum.h`·`zlink_errno.h`·`core/include/zlink/service/{dispatch,mesh_node,actor,spot,stream_session}.h`와 대조 — 값 불일치 0건.
- native 인터페이스(`binding_context/core/socket/service/eventing.ts`) 선언 175개 메서드명을 파라미터 오염 없이 정밀 추출, `addon_exports.cc` 등록 문자열 175개와 `comm` 양방향 대조 — **완전 1:1 일치**(미등록 0건, 미사용 export 0건).
- kind_data 5종 판별 유니언 필드를 addon `svc_create_kind_data` 스위치·Core 구조체(`zlink_actor_control_record_t`/`zlink_actor_join_completion_t`/`zlink_actor_location_t`/`zlink_mesh_send_ready_data_t`/`zlink_actor_transfer_control_t`) 필드명과 3자 대조 — 완전 일치.
- MeshNodeStatus/MeshPeerEntry/SpotStatus/StreamSessionStatus/StreamSessionBinding 5개 status/entry 구조체 필드 대 Core 대조 — 완전 일치.
- transfer token 왕복(`zlink_actor_transfer_token_t` sizeof 기반 Buffer round-trip, `addon_mesh_service.cc:2078-2148`) — 정합.

**신규 발견 NF3-1 [I1, low-medium] `SocketOption` 원시 옵션id 테이블이 Core `zlink_option_t`와 어긋남(2 phantom + 1 누락)**
- Evidence: `bindings/node/src/zlink/runtime/options/option_mapping.ts:3-39`의 `SocketOption`(73개 hex값)을 `core/include/zlink_enum.h:48-116`(`zlink_option_t`) + `:130-174`(router/dealer/pub/sub/stream 옵션 블록, 총 72개 hex값)와 전수 hex diff:
  - Node에만 있고 Core에 없음(phantom): `DISCOVERY_SPOT_OWNER_SYNC: 0x3035`, `DISCOVERY_ACTOR_ROUTE_SYNC: 0x3036`(`option_mapping.ts:24`).
  - Core에만 있고 Node에 없음(누락): `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0x3034`(`zlink_enum.h:115`, 소켓 레벨 옵션. 컨텍스트 레벨 `ZLINK_CTX_OPT_AUTO_HWM_MSG_UNIT_BYTES=18`은 `context.ts`에 별도 존재하며 이것과 무관).
  - 나머지 71개 값은 완전 일치.
- 두 phantom 값은 discovery/location-store 관련 제거된 개념의 잔재로 추정(§4 dead code 참조). 완결성 관점에서는 소켓 레벨 auto-hwm 오버라이드가 Node에서 아예 노출되지 않음(문서화된 설계 결정인지 누락인지 본 리뷰 범위에서는 판별 불가).
- Verdict: **NOT CLEAN**. → phantom 2건 제거 또는 Core 재추가 근거 제시, `AUTO_HWM_MSG_UNIT_BYTES` 노출 여부 결정 필요.

### I2 POSD·DDD

- contracts → runtime/native import 방향 검사 — 계층 위반 0건(contracts는 순수 타입만, native/runtime 참조 없음).
- RouterSocket이 Spot 개념을 재누출하지 않음(NF2 재확인).
- 낮은 심각도 관찰(finding 미상신): addon native/src의 tsfn release/finalize 헬퍼 4쌍이 거의 동일 패턴을 반복하나(§progress.md 참조), 기존 `addon_tsfn_slots.h` 템플릿이 find/reset/reserve/create만 일반화하고 release/finalize는 미적용 — 기능·계약 영향 없는 스타일 이슈로 finding 등급에 미달 판단.
- Verdict: **CLEAN**.

### I3 정리 완결성

- manifest no-hit 토큰 17종(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/zlink_msg_gets/routerSpot/spotNodeActorBindRemoteSession/sync_request_callback/wait_sync_request/create_request_js_state(정확 매치)/streamBindActor/streamBoundActors/streamSendBoundActorPart/streamUnbindActor/zlink_router_*_spot_part) 전부 **0 hit**.
- native/src 8개 .cc/.h 파일 dead-code 조사 — 미사용 free/static 함수 0건, 미사용 include 0건.
- **신규 발견(NF3-1과 동일 root) [I3, low-medium]**: `DISCOVERY_SPOT_OWNER_SYNC`(`option_mapping.ts:24`)·`DISCOVERY_ACTOR_ROUTE_SYNC`(동일 라인)는 scope 전역에서 자기 정의(1곳) 외 참조 **0건**(순수 dead). `SocketOption`은 `socket_options.ts`에서만 import되며 public `index.ts`/`contracts`에 재수출되지 않는 internal-only 테이블이라 직접적 소비자 파손 위험은 낮으나, Core에 대응 값이 아예 없는 상태로 방치된 dead 상수임.
- Verdict: **NOT CLEAN**.

## 4. 폐기 no-hit 판정

- manifest 명시 no-hit 17종 전량 통과(§3 I3).
- 단, manifest no-hit 목록 밖에서 신규로 dead 상수 2건(`DISCOVERY_SPOT_OWNER_SYNC`/`DISCOVERY_ACTOR_ROUTE_SYNC`)을 발견 — no-hit 목록에 없었던 이유는 이들이 "제거 대상으로 이미 알려진 토큰"이 아니라 그 자체로 이번에 처음 식별된 잔재이기 때문. manifest 판정 자체는 유효하나 완결성 관점에서 이 2건은 추가 정리 대상.

## 5. 종합

- iter-1 NF1-NF7·NI2-1·NI3-1, iter-2 NF2-1~NF2-5 전량 해소 확인(신규 반례 없음).
- I1 계약 일치: NF3-1(옵션id 테이블 phantom 2건 + 누락 1건)로 **NOT CLEAN**.
- I2 POSD·DDD: finding 0건, **CLEAN**.
- I3 정리 완결성: NF3-1과 동일 root의 dead 상수 2건으로 **NOT CLEAN**.
- 전체 native 바인딩 표면(175개 native 메서드 선언 vs 등록)에 대해서는 iter-2가 발견한 버그 클래스(미등록 dead 선언)가 완전히 재발하지 않음을 정량적으로 확인했으나, 별도의 원시 옵션id 매핑 테이블에서 대응되는 성격의 잔재(Core에 없는 값·미참조 상수)가 새로 발견됨.

BINDINGS REVIEW NOT CLEAN
