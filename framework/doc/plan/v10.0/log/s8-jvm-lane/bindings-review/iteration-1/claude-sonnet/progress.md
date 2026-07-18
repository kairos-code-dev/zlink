# S8 JVM bindings 전환 리뷰 iteration 1 — R2 (Claude Sonnet) progress

## Scope 확인
- 시작: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle | grep -vE 'native/linux|native/darwin|native/win|resources/native|/build/' | LC_ALL=C sort | xargs sha256sum | sha256sum`
  → 255 files, `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` (manifest 일치)
- HEAD(`990f70339`)는 대상 commit `5c2eb2acc` 위의 freeze commit이며 scope 내 diff 없음(`git diff 5c2eb2acc..HEAD --stat -- <scope paths>` → empty) 확인.
- 종료 시 동일 명령 재실행 → 동일 255 files / 동일 hash. 리뷰 중 scope 파일 미수정.

## 방법
- Core 10.0.0 공개 헤더 전량 정독: `core/include/zlink/{common.h,core/api.h,eventing/api.h,message/api.h,socket/api.h,service/{common,mesh_node,dispatch,actor,spot,stream_session}.h}`, `core/include/{zlink_enum.h,zlink_errno.h}`.
- JVM FFI 계층 전량 대조: `runtime/nativeapi/{Native,NativeServiceSymbols,ServiceInterop,ServiceLayouts,NativeLayouts,NativeMessage,NativeSymbols}.java` — 모든 `FunctionDescriptor`를 Core 시그니처와 파라미터 수·타입 단위로 1:1 대조.
- 폐기 심볼(제거 심볼) 대상 scoped grep: `zlink_router_*_spot_part`, `zlink_stream_detach`, `zlink_stream_attach_raw`, `zlink_subscribe_handler`, `zlink_router_recv_part`, `zlink_msg_refcnt`, `zlink_router_handler`.
- 9개 no-hit 토큰(SpotNode/SpotRouteBridge/spot_node/route_bridge/subjects/internal_sockets/dispatch_workers/msg_gets/recv_actor_part) 독립 재확인.
- 발견된 downcall에 대해 `runtime/sockets/*.java`, `contracts/sockets/*.java`를 따라가며 공개 API 도달 가능성(reachability) 확인 — 죽은 코드 vs 실사용 경로 구분.
- core/src(core/include/zlink/service/*.h 대응 구현) 일부 대조로 raw STREAM attach/detach 계열이 core 자체에서도 `ZLINK_INTERNAL_EXPORT`(비-Windows에서는 공개 export 아님)로 강등되어 있음을 교차 확인.
- build/test/run 미실행(정적 대조만). 실행 증거는 manifest의 coordinator 증거(compileJava+samples+kotlin-samples green, no-hit 0)를 그대로 인정.

## 결과 요약
- I1: NOT CLEAN — 2개 독립 root-cause family(모두 blocker급, 공개 API에서 실사용 가능한 raw 계층 경로).
  1. `zlink_router_recv_part` FFI 파라미터 수 불일치(레거시 `spot_rid` out-param 잔존, Core는 6-param인데 JVM은 7-param 선언) — `RouterSocket.recv()` 공개 경로에서 실사용.
  2. 레거시 raw STREAM attach/detach/bind-actor 계열 downcall이 Core 10.0.0에 없는 심볼을 호출 — 그중 `zlink_stream_detach`는 `StreamSocket`(공개 계약) `close()` 경로에서 실사용되어 STREAM 소켓 close 자체가 항상 실패.
- I2: NOT CLEAN(경미) — `zlink_router_handler` downcall이 `Native.java`와 `NativeMessage.java` 두 곳에 중복 선언(하나는 죽은 코드, 하나는 실사용).
- I3: NOT CLEAN — `NativeLayouts.java`에 Core 10.0.0에 대응 심볼이 전혀 없는 레거시 registry/actor-route 와이어 포맷 `MemoryLayout` 16개(및 offset 상수)가 scope 전체에서 0회 참조된 채 잔존. 또한 I1 family 2의 죽은 하위 downcall들(`zlink_stream_attach`, `zlink_stream_attach_len32be`, `zlink_stream_send`, `zlink_stream_send_msg`, `zlink_stream_bind_actor`, `zlink_stream_unbind_actor`, `zlink_stream_send_bound_actor_part`, `zlink_stream_bound_actors`)도 정리 미완료.
- 9개 no-hit 토큰: 전량 0 확인(재검증 완료).
- mesh_node/actor/spot/dispatch/stream_session 서비스 계층 downcall 테이블(`NativeServiceSymbols.java`)은 Core 헤더와 파라미터 수·타입이 전량 일치 — actor-join-reply route(`zlink_actor_join_reply`)도 5-param으로 정확. 이 부분은 findings 없음.

## 최종 판정
BINDINGS REVIEW NOT CLEAN
