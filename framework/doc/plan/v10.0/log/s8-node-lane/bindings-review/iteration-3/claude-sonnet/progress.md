# S8 NODE bindings 리뷰 iteration-3 — R2(claude-sonnet) progress

## 절차 로그
1. prompt.md 원문 로드 (`../prompt.md`), byte 단위 동일 prompt 확인. 다른 리뷰어·coordinator 해석 미참조.
2. scope 확인: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json` (build/node_modules/prebuilds 제외) → 140 파일, aggregate sha256 `4c772436d48795958da6e8cdf8e071962f716b9d33c391a1874c382892ccfdf6` — prompt 명시값과 일치.
3. HEAD(`4f502b174`)가 target `bc409293a`의 후손이고, scope 범위에 대해 `git diff bc409293a HEAD -- <scope>`가 무변화 → scope 내용은 target과 byte 동일 확인.
4. iter-1 finding-ledger(`../../iteration-1/finding-ledger.ko.md`) NF1-NF7·NI2-1·NI3-1 재확인 — 전량 소스 대조로 해소 확인(신규 반례 탐색, 발견 없음).
5. iter-2 R1(opus) 리포트(`../../iteration-2/codex/review.ko.md`) NF2-1~NF2-5 재확인:
   - NF2-1(result enum 누락 RequestResult.Backpressured=113/RecvResult 207,208/ConnectResult 608/ConfigResult 707,708,709) → `results.ts` 전체 8개 enum(SubmitResult/RequestResult/RecvResult/HandlerResult/CloseResult/BindResult/ConnectResult/ConfigResult) core/include/zlink_errno.h 전값 대조, 완전 일치 확인.
   - NF2-2(MonitorSourceKind drift) → `monitor.ts:6` `{Socket:1}` 단일값, Core `zlink_monitor_source_kind_t`(`ZLINK_MONITOR_SOURCE_SOCKET=1`)와 일치.
   - NF2-3/4(미등록 dead `stream*Actor` 4메서드) → scope 전역 grep 0 hit.
   - NF2-5(dead enum 멤버) → NF2-2 해소로 동반 해소.
6. 전체 scope 3축 신규 재검토(과거 발견 재활용 없이):
   - 모든 contracts의 `Object.freeze` enum(28개) 원문 확인 → core/include/zlink_enum.h, zlink_errno.h, core/include/zlink/service/{dispatch,mesh_node,actor,spot,stream_session}.h 전수 대조.
   - `binding_context.ts`/`binding_core.ts`/`binding_socket.ts`/`binding_service.ts`/`binding_eventing.ts` 5개 native 인터페이스 파일에서 선언된 native 메서드명 175개 정밀 추출(파라미터명 오염 없이 `^\s\s이름:` 패턴) 후 `addon_exports.cc`의 등록 문자열 175개와 `comm` 양방향 대조 → 완전 1:1 일치(미등록·orphan export 0건). iter-2가 잡은 버그 클래스(선언은 있는데 등록 없음)가 전체 표면에서 재발하지 않음을 확인.
   - kind_data record(actorControl/actorJoinCompletion/actorLookupCompletion/sendReady/transferControl) 5종 — TS 판별 유니언, addon `svc_create_kind_data` switch, Core 구조체 필드명(actor.h/dispatch.h) 3자 대조 → 필드 단위 완전 일치.
   - MeshNodeStatus/MeshPeerEntry/SpotStatus/StreamSessionStatus/StreamSessionBinding 필드 대 Core 구조체 필드 대조 → 완전 일치.
   - manifest no-hit 토큰 17종 재확인(SpotNode/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/recv_actor_part/zlink_msg_gets/routerSpot/spotNodeActorBindRemoteSession/sync_request_callback/wait_sync_request/create_request_js_state(정확 매치)/streamBindActor/streamBoundActors/streamSendBoundActorPart/streamUnbindActor/zlink_router_*_spot_part) → 전부 0.
   - addon native/src 8개 .cc/.h 파일 dead-code·중복·미사용 include 서브에이전트 조사(read-only) — free/static 함수 미사용 0건, include 미사용 0건. `addon_spot_request_callbacks.cc`는 실제로는 DEALER/ROUTER request-reply tsfn 브리지(파일명이 "spot"이라 오인 소지 있으나 dead 아님, addon_core.cc의 dealer_request/router_request에서 사용). 낮은 심각도 관찰: `stream_release_slot`/`release_socket_send_ready_handler_slot`/`release_socket_monitor_handler_slot`/`release_timer_handler_slot`(4개)와 대응 `*_tsfn_finalize`(4개)가 거의 동일한 lock/find/reset/release 패턴을 반복(기존 `addon_tsfn_slots.h` 템플릿이 find/reset/reserve/create만 일반화하고 release/finalize는 못함) — 기능·계약 영향 없는 스타일 관찰, finding으로 상신하지 않음.
   - **신규 발견**: `bindings/node/src/zlink/runtime/options/option_mapping.ts`의 `SocketOption` 원시 옵션id 테이블(73개 hex값)을 Core `zlink_option_t`(+router/dealer/pub/sub/stream 옵션 블록, 72개 hex값)와 전수 hex diff한 결과, Node에 `DISCOVERY_SPOT_OWNER_SYNC=0x3035`·`DISCOVERY_ACTOR_ROUTE_SYNC=0x3036` 2개의 phantom 값(Core 어디에도 없음)이 존재하고, Core의 살아있는 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES=0x3034`가 누락됨을 확인. 두 phantom 상수는 scope 전역에서 자기 정의 외 참조 0건(dead), `SocketOption`은 public index.ts/contracts에 재수출되지 않는 internal-only 테이블(소비자 직접 노출 없음)이라 즉각적 소비자 파손 위험은 낮으나, 이는 iter-1/iter-2 어느 finding-ledger에도 없는 새로운 I1(raw 옵션id drift)/I3(dead code) 잔재.
7. sample_support.ts·8개 sample 파일에서 공개 enum(`zlink.ReceiveKind`/`zlink.OperationKind`) 사용 확인, raw 매직넘버 우회 0건.
8. RouterSocket 계약(`router_socket.ts`)·런타임(`sockets/router_socket.ts`) 재확인 — spot 관련 메서드 잔재 0건.
9. contracts 디렉터리 → runtime/native import 계층 위반 grep → 0건(POSD 경계 유지).

## 실행 증거
- build/test/run 미실행. 실행 증거는 manifest(coordinator 제공: addon node-gyp green·tsc src/samples green·no-hit 0)만 사용, 본 리뷰는 정적 소스 대조로 교차확인.

## 산출물
- `review.ko.md` (본 디렉터리)
