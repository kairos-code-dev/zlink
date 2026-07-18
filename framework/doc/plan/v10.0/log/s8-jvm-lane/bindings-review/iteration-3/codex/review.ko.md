# S8 JVM bindings 전환 리뷰 — iteration 3 — R1 (opus, codex 슬롯)

독립 리뷰. R2·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조만(빌드·실행·수정 없음).

## 1. Scope 확인
- 대상 commit `39b1edee8`(freeze HEAD `f8c8f32aa`의 부모). 파일 수 **251** — 일치.
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum) =
  `f35dd2fe28be90088b482a799e45389d4fab259c80366b527fa5d9e38a94af95` — **일치**.
- 시작·종료 파일 수·hash 동일. 파일 미수정.

## 2. iter-2 finding 해소 판정

### JV2-1 (router_recv_part descriptor over-correct) — RESOLVED
- `Native.java:336-349`: `MH_ROUTER_RECV_PART` 및 `MH_ROUTER_RECV_PART_CRITICAL` 둘 다
  `of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS, ADDRESS, JAVA_INT)` = **5 ADDRESS + 1 INT = 6 파라미터**.
- Core `socket/api.h:271-277` `zlink_router_recv_part(void*, const routing_id**, uint64_t*, msg_t*,
  part_flag*, recv_flags)` = 6 파라미터와 정확 일치.
- invokeExact call site `Native.java:1350, 1367`: `(router, sourceNodeRidOut, requestSeqOut, partOut,
  hasMoreOut, flags)` = **6 인자** → descriptor와 일치. WrongMethodTypeException 제거 확인.
- 반환 `zlink_recv_result_t`는 C enum(int-width, `zlink_errno.h:153`) → JAVA_INT 매핑 정확.

### JV2-2 (zlink_router_handler Core 부재) — RESOLVED
- `zlink_router_handler` 참조 java src/samples 전역 **0건**(재grep 확인).
- `NativeMessage.java:38-41`: `MH_RECV_HANDLER` = `of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS)` = 3 파라미터,
  Core `socket/api.h:81-83` `zlink_recv_handler(void*, zlink_socket_msg_handler_fn, void*)` 일치.
  call site `:148` `(socket, handler, userData)` = 3 인자 일치.
- upcall 콜백: `NativeRouterReceiveSupport.java:43-45` `FD_RECV_HANDLER = ofVoid(ADDRESS, ADDRESS,
  JAVA_LONG, ADDRESS)` + handle MethodType `(MemorySegment, MemorySegment, long, MemorySegment)->void`
  → Core `zlink_socket_msg_handler_fn(source_rid, parts, size_t part_count, userdata)` 4파라미터와 정확 일치.
- onReceive는 STREAM 지원, router=Core ENOTSUP 주석 정합. dotnet `zlink_recv_handler` 패턴과 동형.

## 3. 3축 재검토

### I1 — FFI descriptor/type 정확성 (CLEAN)
- **CRITICAL 전수 대조**: 12개 FFI 파일 모든 downcall MethodHandle에 대해 descriptor 파라미터 수 ==
  invokeExact 인자 수 == Core C 파라미터 수를 대조. **불일치 0건**. 넓은 시그니처 포함 검증:
  `zlink_spot_request_to_spot` 10/10/10, `zlink_subscribe_part` 8/8/8, `zlink_xpub_recv_part` 7/7/7,
  `zlink_router_request_part` 8/8/8, `zlink_dealer_request_part` 7/7/7, `zlink_router_recv_part` 6/6/6.
  모든 `_CRITICAL` 변형은 대응 downcall과 동일 descriptor. `streamPacketHandler` 래퍼(java 2인자)는
  invokeExact에서 NULL userdata를 채워 3파라미터 descriptor와 일치.
- java-native 헬퍼: `zlink_java_send_u32` 5/5/5, `zlink_java_msg_data_addr` 1/1/1 (`zlink_java_reqrep_bridge.c` 대조).
- **upcall 콜백 시그니처 7종 전수** Core typedef와 일치:
  `zlink_reply_handler_fn`(RoutedRequestSupport), `zlink_thread_fn`(NativeZlinkThread),
  `zlink_timer_handler_fn`(NativeTimer), `zlink_monitor_handler_fn`(NativeMonitorSocket),
  `zlink_mesh_ready_handler_fn`(NativeMeshNode, 반환 uint32 mask→JAVA_INT),
  `zlink_socket_msg_handler_fn`·`zlink_send_ready_handler_fn`·`zlink_stream_packet_handler_fn`(SocketCore).
- **ServiceLayouts 17개 구조체** + ROUTING_ID/ACTOR_REF nested layout이 Core C ABI를 정확 재현
  (필드 순서·폭·내부/말미 padding·총크기 전부 일치: MESH_NODE_STATUS 1128, MESH_PEER_ENTRY 824,
  RECEIVE_RECORD 1192, SPOT_STATUS 328, STREAM_SESSION_STATUS 64 등 padding 체크포인트 검증).
  MONITOR_EVENT_LAYOUT(784)·MONITOR_SNAPSHOT_LAYOUT은 사용처 있고 Core 일치.
- Verdict: **CLEAN**(finding 0).

### I2 — POSD·DDD (CLEAN)
- 콜백 수명: upcall stub은 `Arena.ofShared()`에 고정, 등록 실패 시 arena 정리(NativeRouterReceiveSupport
  onReceive 예외 경로, SocketCore installCallback 실패 롤백) 정합. router receive는 stub 1개 상주 +
  Java handler 참조 교체 패턴으로 안정. dead handler 시 parts vector를 multipartClose로 정확히 1회 해제.
- Verdict: **CLEAN**(finding 0).

### I3 — 정리(제거·부재 심볼·dead code·no-hit) — **NOT CLEAN (JV3-1)**
- 제거·부재 심볼: FFI `"zlink_*"` literal **178종** 중 `removed-identifiers-10.0.0.json` 적중 0.
  nm -D `libzlink.so.10.0.0`에서 175종 resolve; 잔여 3종 전부 정당 —
  `zlink_msgv_close`(=`downcallAny(["zlink_multipart_close","zlink_msgv_close"])`의 legacy fallback,
  primary `zlink_multipart_close`가 nm에 존재해 미도달), `zlink_java_msg_data_addr`·`zlink_java_send_u32`
  (바인딩 native bridge `libzlink_java_bridge.so`가 export). 제거·부재 심볼 **0**. → 이 부분은 clean.

#### FINDING JV3-1 — 죽은 layout 상수 잔존 [LOW]
- **위치**: `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/NativeLayouts.java:160-206`.
- **내용**: `SERVICE_EVENT_LAYOUT` + 22개 `SERVICE_EVENT_*_OFFSET` 상수(총 23 심볼)가 **어디에서도
  참조되지 않음**(java/kt 전역, build 제외, 정의 파일 제외 = 0건).
- **근거**: 이 layout은 제거된 public service-monitor surface의 Java ABI 미러. 커밋 `65a11b2b7`
  ("remove public service monitor surface")가 Core `core/src/api/monitor_service_api.cpp` 및 전 바인딩
  ServiceMonitor(cpp/dotnet/구java `dev.kairoscode.zlink`/rust)를 삭제했으나, 신규 java 바인딩의
  `SERVICE_EVENT_LAYOUT`는 orphan으로 남음. 대응 live Core struct 부재(`service_kind`·`subject_kind`가
  core/include 전역 0건). S8 정리 커밋 `37851c072`("remove deprecated FFI residue")가 이 잔재를 놓침.
- **영향**: 순수 dead code. 할당·read 경로 없음 → 런타임/ABI/정확성 무영향. 그러나 I3(정리 축)는
  dead code를 명시 대상으로 하며, iteration 3의 축 CLEAN 기준(finding 0)을 위반.
- **권고**: NativeLayouts.java:160-206의 `SERVICE_EVENT_LAYOUT`+`SERVICE_EVENT_*` 23 심볼 삭제.

## 4. 제거·부재 심볼 판정
- removed-identifiers 적중 0, 부재(unresolvable) FFI 심볼 0. **PASS**.

## 5. 종합
- iter-2 JV2-1/JV2-2 해소 확인. I1·I2 CLEAN, 제거심볼 게이트 CLEAN.
- I3 축에 dead-code finding **JV3-1(LOW)** 1건 → I3 축 not clean → 전체 not clean.

BINDINGS REVIEW NOT CLEAN
