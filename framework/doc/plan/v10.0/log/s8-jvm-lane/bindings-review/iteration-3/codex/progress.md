# S8 JVM bindings review — iteration 3 — R1 (opus) progress

- Scope hash verified: `f35dd2fe28be90088b482a799e45389d4fab259c80366b527fa5d9e38a94af95` (251 files) — MATCH.
- HEAD `f8c8f32aa` (iter-3 freeze; parent `39b1edee8` = target commit).

## iter-2 finding resolution (source-diff verdict)
- JV2-1 (router_recv_part descriptor): RESOLVED. Both `MH_ROUTER_RECV_PART` and
  `MH_ROUTER_RECV_PART_CRITICAL` = `of(JAVA_INT, ADDRESS×5, JAVA_INT)` = 6 params.
  Matches Core `zlink_router_recv_part` (socket/api.h:271-277: void* + 4 ptr-out + 1 flags) and
  the 6-arg `invokeExact` call sites (Native.java:1350, 1367).
- JV2-2 (router_handler absent): RESOLVED. `zlink_router_handler` fully gone from NativeMessage.
  Rewired to `MH_RECV_HANDLER` = `of(JAVA_INT, ADDRESS×3)` matching Core `zlink_recv_handler`
  (socket/api.h:81-83); call site passes 3 args (NativeMessage.java:148). Upcall callback
  `FD_RECV_HANDLER = ofVoid(ADDRESS, ADDRESS, JAVA_LONG, ADDRESS)` + MethodType
  `(MemorySegment,MemorySegment,long,MemorySegment)->void` matches 4-param
  `zlink_socket_msg_handler_fn(source_rid, parts, part_count/size_t, userdata)`.

## Symbol gate (I3)
- 178 distinct `zlink_*` FFI literals across 12 FFI files.
- removed-identifiers-10.0.0.json hits: 0.
- nm -D libzlink.so.10.0.0: all resolve except 3 explained:
  - `zlink_msgv_close` — legacy fallback alias in `downcallAny(["zlink_multipart_close","zlink_msgv_close"])`;
    primary `zlink_multipart_close` IS in nm, fallback never reached.
  - `zlink_java_msg_data_addr`, `zlink_java_send_u32` — binding-native helpers, exported by
    `libzlink_java_bridge.so` (defined in bindings/java/native/src/zlink_java_reqrep_bridge.c).
- Verdict: no removed symbol, no unresolvable symbol.

## Critical arity sweep (I1) — CLEAN
- Exhaustive descriptor↔callsite↔Core cross-check across all 12 FFI files: NO MISMATCH.
  Every downcall: descriptor param count = invokeExact arg count = Core C param count, with
  matching layout/Java types and return casts. Wide sigs verified: spot_request_to_spot 10/10/10,
  subscribe_part 8/8/8, xpub_recv_part 7/7/7, router_request_part 8/8/8, dealer_request_part 7/7/7,
  router_recv_part 6/6/6. All *_CRITICAL variants share their twin's descriptor.
- Java-bridge helpers: zlink_java_send_u32 5/5/5, zlink_java_msg_data_addr 1/1/1 (verified vs
  zlink_java_reqrep_bridge.c). iter-2 defect class (descriptor N vs callsite N±1) absent.
- 7 upcall callback stubs independently verified vs Core typedefs (all match):
  reply_handler_fn, thread_fn, timer_handler_fn, monitor_handler_fn, mesh_ready_handler_fn,
  socket_msg_handler_fn (recv), send_ready_handler_fn, stream_packet_handler_fn.

## Struct layout (I1) — ServiceLayouts vs Core structs — CLEAN
- All 17 ServiceLayouts + ROUTING_ID/ACTOR_REF nested layouts match C ABI exactly, including
  every interior/trailing pad and total size (MESH_NODE_STATUS 1128, RECEIVE_RECORD 1192, etc.).
- NativeLayouts monitor layouts spot-checked: MONITOR_EVENT_LAYOUT (784, used by Native/NativeMonitorSocket)
  and MONITOR_SNAPSHOT_LAYOUT (used) MATCH; MONITOR_EVENT matches zlink_monitor_event_t.

## I3 dead code — FINDING JV3-1 (LOW)
- `SERVICE_EVENT_LAYOUT` + 22 `SERVICE_EVENT_*` offset constants (NativeLayouts.java:160-206,
  23 symbols total) are ORPHANED: zero consumers repo-wide (java/kt, excl build), no live Core
  struct (service_kind/subject_kind = 0 hits in core/include). Residue of the removed public
  service-monitor surface (commit 65a11b2b7 deleted core/src/api/monitor_service_api.cpp +
  ServiceMonitor across all bindings). The S8 cleanup commit 37851c072 ("remove deprecated FFI
  residue") missed it. No runtime/ABI impact (never allocated/read) → LOW.

## Verdict
- iter-2 JV2-1/JV2-2: RESOLVED. I1 (arity/upcall/layout): CLEAN. I3 symbol gate: CLEAN.
- I3 dead-code: 1 finding (JV3-1) → axis NOT clean → overall BINDINGS REVIEW NOT CLEAN.
