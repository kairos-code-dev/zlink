# S8 NODE bindings 전환 리뷰 iteration-2 — R1(opus) review

독립 리뷰어 R1. 다른 리뷰어·coordinator 해석을 판정 근거로 쓰지 않았다. 정적 대조만 수행(build/실행 없음).

## 1. Scope 확인
- 파일 수: 140 (시작·종료 동일).
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c` — prompt/manifest 값과 일치.
- HEAD `3b252dd58`. target `7fead5f17`은 HEAD 조상이고 `git diff 7fead5f17 HEAD -- bindings/node/{src,native/src,samples,binding.gyp,package.json}` 무변경 → scope 내용은 target commit과 byte 동일.
- Coordinator 실행 증거(manifest): addon node-gyp green·tsc(src)·tsc(samples) green·no-hit 9종 0 — 재실행하지 않고 정적 대조로 교차확인.

## 2. iter-1 finding 해소 판정 — 전량 해소(source 대조)

| finding | 판정 | 근거 |
|---|---|---|
| NF1 wire enum 값 | 해소 | `ReceiveKind`(13)=`zlink_mesh_record_kind_t`(1-13), `OperationKind`(11)=`zlink_mesh_operation_kind_t`(1-11), `MeshNodeState`(7)=`zlink_mesh_node_state_t`(1-7) 전값 일치. `OperationKind.ActorJoin=7`(=ACTOR_JOIN), `MeshNodeState.Error=7`/`Stopped=6` 정정. `sample_support.ts:17-18`가 `zlink.ReceiveKind/OperationKind` 공개 enum 사용(raw 상수 제거). |
| NF2 Router spot 메서드 | 해소 | `router_socket.ts:14-42`·`runtime/native/binding_socket.ts` 에 sendToSpot/requestToSpot/replyToSpot·routerSpot* 0 hit. |
| NF3 kind_data | 해소 | `dispatch.ts:108-165` typed `ReceiveKindData` union(5), addon `svc_create_kind_data`(sizeof guard) record.kind/operation_kind 분기, runtime `conversions.ts:kindDataFromRaw`. |
| NF4 ready handler mask+unregister | 해소 | addon `invoke_ready_handler_js` 가 JS 반환 uint32 read-back, JS-thread 재진입 직접호출, 예외 catch→mask 0, `mesh_node_unset_ready_handler` tsfn release. runtime `setReadyHandler` state 저장·교체시 clear 선행·close()에서 unset. |
| NF5 close 결과 | 해소 | `mesh_node_destroy` `!= ZLINK_CLOSE_OK` throw 하고 handle null화 안 함(busy 시 JS 소유 유지). |
| NF6 count 타입 | 해소 | addon `svc_set_size`=`napi_create_double`(JS number), TS `messageCount/partCount/byteCount: number` 정합. |
| NF7 transfer API | 해소 | addon prepare/commit/activate/abort(`actor.h` struct) + `addon_exports.cc:197-200` 등록 + `transfer.ts` contract + `mesh_node.ts:455-497` 4 메서드. |
| NI2-1 Router↔Spot 누출 | 해소 | NF2 root 제거, enum 단일 소스(contracts), dispatch-turn 캡슐화(ready handler state). |
| NI3-1 dead 잔재 | 해소 | spotNodeActorBindRemoteSession/sync_request_callback/wait_sync_request/zlink_msg_gets 0 hit. `create_request_js_state`→`create_request_js_state_impl`(live `create_core_request_js_state` 내부헬퍼). |

해소된 finding은 새 반례 없음 → 재개하지 않음.

## 3. 전체 scope 3축 재검토 — Finding / Evidence / Verdict

### I1 계약 일치
서비스/디스패치 enum·transfer·kind_data·ready·close 는 위 표대로 clean. 그러나 error result 및 monitor enum 에서 Core 대조 불일치 잔존.

**NF2-1 [I1, medium] 공개 result enum이 Core 최신 값 누락 → 역매핑 공백**
- `contracts/errors/results.ts`:
  - `RequestResult`(l.23-37) 가 `NotSupported:112`에서 종료 — Core `ZLINK_REQUEST_BACKPRESSURED=113`(`zlink_errno.h:138`) 누락. request 경로의 현실적 런타임 값.
  - `RecvResult`(l.41-49) 가 `InternalError:206`에서 종료 — Core `BUFFER_TOO_SMALL=207`,`INVALID_STATE=208`(`zlink_errno.h:151-152`) 누락. (recv batch too-small 경로 실사용값)
  - `ConnectResult`(l.86-95) 가 `Busy:607`에서 종료 — Core `AUTH_FAILED=608`(`zlink_errno.h:201`) 누락.
  - `ConfigResult`(l.99-107) 가 `NotFound:706`에서 종료 — Core `CONFLICT=707`,`BUFFER_TOO_SMALL=708`,`BUSY=709`(`zlink_errno.h:216-218`) 누락.
- Evidence: 위 파일/행 대조. Core 값이 그대로 addon 통과되므로 소비자가 해당 코드 수신 시 심볼 매핑 부재.
- Verdict: **NOT CLEAN**. → 4 enum에 누락 Core 값 추가.

**NF2-2 [I1/I3, medium] `MonitorSourceKind` 가 Core 미정의 값 노출(drift)**
- `contracts/eventing/monitor.ts:6` `MonitorSourceKind={Socket:1,SpotPub:3,SpotSub:4}`. Core `zlink_monitor_source_kind_t` 는 `ZLINK_MONITOR_SOURCE_SOCKET=1` 만 정의(`core/include/zlink_enum.h:200-203`).
- addon 은 `snapshot.source_kind` 를 raw uint32 통과(`addon_monitor_status_values.h:12`, `addon_core.cc:2643`) → `SpotPub:3`/`SpotSub:4` 는 생성 불가능한 값(값 2도 건너뜀). 제거된 spot pub/sub monitor 개념 잔재.
- Verdict: **NOT CLEAN**. → Core enum 과 정합(SpotPub/SpotSub 제거) 또는 Core 가 실제 방출한다는 근거 제시.

### I2 POSD·DDD

**NF2-3 [I2, high] socket 바인딩 인터페이스에 미등록·타도메인 `stream*Actor` 4 메서드 누출**
- `runtime/native/binding_socket.ts:128-152` 의 `SocketNativeBinding` 이 `streamBindActor`/`streamBoundActors`/`streamSendBoundActorPart`/`streamUnbindActor` 선언. `addon_exports.cc` 는 `socketStreamAttach`(l.62) 와 stream-session **서비스** 메서드(`streamSessionBindActor` 등, l.207-211)만 등록 — 위 4 명은 어디에도 미등록. 호출 시 `native.streamBindActor` 는 `undefined` → "undefined is not a function".
- 실제 live 경로는 stream-session 서비스(`binding_service.ts` `streamSessionBindActor` 등). socket-level 4 는 다른 도메인(스트림 세션) 개념이 socket 바인딩 표면에 잔존한 것으로, iter-1 NI3-1 이 제거한 dead `native.routerSpot*` 선언과 동류.
- Verdict: **NOT CLEAN**(NF2-4 와 동일 root). → 4 선언 제거.

### I3 정리 완결성

**NF2-4 [I3, high] dead 선언(호출부 0)** — NF2-3 와 동일 대상.
- `binding_socket.ts:128-152` 4 메서드는 scope 전역(src·native/src·samples) grep 시 자신의 선언(128/135/139/146) 외 참조 0. `StreamSocket` contract/runtime 에 actor 메서드 없음. 순수 dead + 미등록.
- Verdict: **NOT CLEAN**.

**NF2-5 [I3, medium] dead enum 멤버** — `MonitorSourceKind.SpotPub/SpotSub`(NF2-2). native 는 항상 1 만 방출하므로 두 멤버는 도달 불가. 유일 소비자는 raw cast(`runtime/eventing/monitor_status.ts`). → NF2-2 와 함께 정리.

## 4. 폐기 no-hit 판정
- manifest no-hit 9종(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/msg_gets/routerSpot) 재확인 전량 0 → **통과**.
- forbidden 토큰(zlink_router_*_spot_part, spotNodeActorBindRemoteSession, sync_request_callback, wait_sync_request, `create_request_js_state`(정확·_impl 제외), subscribe_handler 등) 0. `sendToSpot` 유일 hit 은 Spot 타입(`spot.ts:48`, `runtime/service/spot.ts:73`, → Core `zlink_spot_send_to_spot` 정당) 로 Router 아님 → 위반 아님.
- 단, dead **선언**(NF2-3/4)·dead enum 멤버(NF2-5) 는 no-hit 토큰 목록 밖의 잔재로 I3 완결성 위반.

## 5. 종합
- iter-1 NF1-NF7·NI2-1·NI3-1 전량 해소 확인.
- 신규/잔존 finding: NF2-1(result enum 누락), NF2-2(MonitorSourceKind drift), NF2-3/4(미등록 dead `stream*Actor`), NF2-5(dead enum 멤버).
- iteration-2 판정 기준(각 축 finding 0) 미충족: I1(NF2-1·NF2-2)·I2(NF2-3)·I3(NF2-4·NF2-5) 모두 finding 존재.

BINDINGS REVIEW NOT CLEAN
