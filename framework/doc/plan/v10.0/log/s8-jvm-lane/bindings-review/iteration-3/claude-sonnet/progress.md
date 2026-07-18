# S8 JVM bindings 리뷰 iteration-3 — R2(Claude Sonnet) progress

## Scope
- 대상 commit: `39b1edee8`. 현재 워킹트리 HEAD(`f8c8f32aa1`)는 이 commit 이후 iteration-3 log 파일 2개만 추가(freeze 커밋) — scope 파일 자체는 `git diff 39b1edee8 HEAD --stat -- <scope files>` 결과 empty, 즉 워킹트리 = 대상 commit 상태.
- `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/(linux|darwin|win)`·`resources/native`·`/build/` 제외 → 251 files.
- aggregate hash: `git ls-files ... | grep -Ev ... | xargs sha256sum | sha256sum`(파일 자체는 git ls-files 출력 순서=이미 정렬, LC_ALL=C 재정렬해도 동일 순서 확인) → `f35dd2fe28be90088b482a799e45389d4fab259c80366b527fa5d9e38a94af95`, prompt/manifest 기대값과 일치. 종료 시 미수정이므로 동일.
- 리뷰 중 scope 파일 미수정(read-only, grep/read/nm/python 파싱만 사용).

## iter-2 finding 해소 검증
- **JV2-1** (`zlink_router_recv_part` descriptor 5ADDRESS+1INT 복원): `Native.java:336-349` `MH_ROUTER_RECV_PART`/`MH_ROUTER_RECV_PART_CRITICAL` 둘 다 `ADDRESS×5, JAVA_INT` 확인. invokeExact 호출부(`Native.java:1350`, `:1367`)는 각각 6인자(`router, sourceNodeRidOut, requestSeqOut, partOut, hasMoreOut, flags`)로 descriptor와 정확히 일치. Core `socket/api.h:271-277` `zlink_router_recv_part(void*, const zlink_routing_id_t**, uint64_t*, zlink_msg_t*, zlink_part_flag_t*, zlink_recv_flags_t)` = 5 포인터+1 int로 재대조 일치. **RESOLVED**.
- **JV2-2** (`zlink_router_handler`→`zlink_recv_handler` 재매핑): scope 전체에서 `router_handler` 문자열 grep 0건(`Native.java`/`NativeMessage.java` 모두 제거 확인). `NativeMessage.java:38-41`(`MH_RECV_HANDLER` 3 ADDRESS), `Native.java:81-82`(동일)로 재정의. `NativeRouterReceiveSupport.java`의 `FD_RECV_HANDLER`가 `ofVoid(ADDRESS,ADDRESS,JAVA_LONG,ADDRESS)`=4파라미터로 `zlink_socket_msg_handler_fn(const zlink_routing_id_t*, zlink_msg_t*, size_t, void*)`(`socket/api.h:41-44`)와 일치. `SocketCore.java`의 `FD_RECV_CALLBACK`도 동일 4파라미터로 일치. `handleReceiveCallback`/`snapshotReceive`/`dispatchReceive`/`CallbackReceivedData` 전부 spot-rid·request-sequence 필드 제거로 일관 갱신됨(git diff 확인). Router 소켓에서 `onReceive()` 호출 시 Core가 raw STREAM 외 subject에 ENOTSUP 반환 → `ZlinkException`으로 정상 전파(요청된 설계와 일치, 코드 경로 확인). **RESOLVED**.

## I1 전체 scope 재검토 — FFI downcall arity/type
- 5개 FFI 선언 파일(`Native.java`/`NativeMessage.java`/`NativePollerSymbols.java`/`NativeServiceSymbols.java`/`NativeSymbols.java`) 전체를 정규식+괄호깊이 파서로 스캔(iter-2 R2 스크립트를 `MH_` 접두사 제한 없이 일반화 — `NativeServiceSymbols.java`가 `MH_` 접두사를 안 쓰는 걸 확인하고 수정).
- 결과: **184개 MethodHandle 선언, 186개 invokeExact 호출부, 불일치 0건**, 호출부 없는 미사용 핸들 0건(iter-2의 98/110 스캔보다 범위 확대·완전).
- 대표 샘플 재대조(신규 diff 대상 handle 위주):
  - `zlink_router_recv_part`(6-param) — 상기 JV2-1.
  - `zlink_recv_handler`(3-param, `void*, zlink_socket_msg_handler_fn, void*`) — 상기 JV2-2.
  - `zlink_stream_session_unbind_actor`(`NativeServiceSymbols.java:187-188`, `of(I,A,A,A,L,A,I)`=6파라미터) vs Core `stream_session.h:68-74`(`void*, const rid*, const actor_ref*, uint64_t, mesh_operation_id_t*, uint32_t`) 6파라미터 일치.
- 콜백 upcall 시그니처 6종 전수 Core typedef 대조:
  - `zlink_thread_start` entry(`NativeZlinkThread.java` `THREAD_ENTRY`=`ofVoid(ADDRESS)`) — 1파라미터, Core thread entry 시그니처와 일치.
  - `zlink_reply_handler_fn`(`RoutedRequestSupport.java` `FD_REPLY_CALLBACK`=`ofVoid(I,A,L,A)`) vs Core `message/api.h`(`zlink_request_result_t, zlink_msg_t*, size_t, void*`) — 4파라미터·타입 일치.
  - `zlink_timer_handler_fn`(`NativeTimer.java` `FIRE_HANDLER`=`ofVoid(A,L,A)`) vs Core `eventing/api.h:217`(`void*, uint64_t, void*`) — 일치.
  - `zlink_monitor_handler_fn`(`NativeMonitorSocket.java` `FD_MONITOR_CALLBACK`=`ofVoid(A,A)`) vs Core `eventing/api.h:23`(`const zlink_monitor_event_t*, void*`) — 일치.
  - `zlink_mesh_ready_handler_fn`(`NativeMeshNode.java`/`NativeServiceSymbols.READY_HANDLER_DESCRIPTOR`=`of(I,A,I,A)`) vs Core `service/dispatch.h:138-141`(반환 `zlink_mesh_ready_domain_mask_t`, 파라미터 `void*, mask_t, void*`) — 반환형·파라미터 모두 일치.
  - `zlink_socket_msg_handler_fn` — 상기 JV2-2(2곳: `SocketCore`/`NativeRouterReceiveSupport`).
- `ServiceLayouts.java`/`NativeLayouts.java`: iter-2 target(`50faf28fd`)~iter-3 target(`39b1edee8`) 사이 `git diff --stat` 결과 두 파일 모두 변경 없음(변경분은 `Native.java`/`NativeMessage.java`/`NativeRouterReceiveSupport.java` 3개 파일뿐). iter-1·iter-2에서 이미 `ROUTING_ID_LAYOUT`/`ACTOR_REF_LAYOUT`/`RECEIVE_RECORD` 등 Core 대조 완료(불변). 재확인 차원에서 `MESH_NODE_OPTIONS`/`MESH_PEER_ENTRY`/`ACTOR_LOCATION` 등 필드 순서 육안 재검토 — 이상 없음.
- **Verdict: CLEAN**(blocker/high/medium 0).

## I2 POSD/DDD
- 변경 파일 3개(`Native.java` 4줄, `NativeMessage.java` 16줄, `NativeRouterReceiveSupport.java` 46줄)는 모두 기존 패턴(필드 개수 축소, 콜백 시그니처 단순화)을 그대로 따르는 최소 수정 — 신규 구조적 결함 없음.
- `NativeRouterReceiveSupport`가 자체 `NativeCallbackSupport`/수동 arena 관리를 쓰고 `SocketCore`가 쓰는 `SocketCallbackSupport`(공용 install 헬퍼)를 재사용하지 않는 구조 중복이 존재하나, 이는 JV2-2 수정 이전부터 있던 기존 아키텍처(diff 확인 — 변경분은 심볼명·파라미터 개수뿐)이고 iter-1/iter-2에서 이미 통과된 영역. 신규 반례 아님.
- 최대 파일(`Native.java` 1542줄 등)은 이번 iteration에서 실질 증가 없음(4줄 diff). 구조 재검토 불필요(iter-1/2 clean 유지).
- **Verdict: CLEAN**(blocker/high/medium 0).

## I3 정리(제거·부재 심볼·dead code·no-hit)
- scope 전체 `"zlink_..."` 문자열 리터럴 178개 추출 → `core/tests/contract/removed-identifiers-10.0.0.json`(FUNC/TYPE/REUSED/ENUM_TYPE/ENUMERATOR/MACRO/FIELD 합집합) 대조 → **0건 hit**.
- 178개 중 `nm -D core/build/lib/libzlink.so.10.0.0` 미발견 3건 검토:
  - `zlink_java_msg_data_addr`/`zlink_java_send_u32` — `bindings/java/native/src/zlink_java_reqrep_bridge.c`에 정의된 Java 브릿지 자체 export(별도 빌드 lib), Core `libzlink.so`의 심볼이 아니므로 정상.
  - `zlink_msgv_close` — `NativeMessage.java` `MH_MSGV_CLOSE`가 `downcallAny(["zlink_multipart_close","zlink_msgv_close"], ...)`로 첫 이름을 우선 시도하는 legacy fallback명. `zlink_multipart_close`는 nm에 존재(1건) → 실제 링크는 항상 전자로 성립, 후자는 미선택 fallback으로 정상.
  - JV2-2로 제거된 `zlink_router_handler` — nm -D에 0건(Core 미제공), scope grep도 0건 — 일관.
- JV-F2~F5 재검증(9개 패턴: `stream_bind_actor`/`_unbind_actor`/`_bound_actors`/`_send_bound_actor_part`/`get_spot_option`/`set_spot_option`/`stream_detach`/`stream_attach`/`subscribe_handler`) → `_unbind_actor` 3건은 전부 `zlink_stream_session_unbind_actor`(현재 유효 API, nm 존재, removed-set에 없음)이며 removed 대상인 `zlink_stream_unbind_actor`(session 없는 구명)와는 다른 문자열 — 오탐 아님. 나머지 8패턴 0건.
- **낮은 심각도 발견 1건**: `Native.java:34-36`의 `private static MethodHandle optionalDowncall(String, FunctionDescriptor)`(및 그 안에서만 호출되는 `NativeSymbols.optionalDowncall`)가 scope 전체에서 정의 2곳 외 호출부 0건 — 완전 미사용 사문(死文). `git log -p`로 추적한 결과 과거 `MH_JAVA_ROUTER_RECV = optionalDowncall(...)` 호출부가 존재했으나 리팩토링으로 제거되고 헬퍼만 잔존(iter-1 이전부터 존재, JV2 수정과 무관, 기능 영향 0, compileJava 영향 없음). node lane iteration-4 선례(`low-followups.ko.md`: 미참조 타입 4건, CLEAN 비차단)와 동일 성격의 low finding으로 분류.
- **Verdict: CLEAN**(blocker/high/medium 0; low 1건 별도 기록).

## 제거·부재 심볼 판정
제거 심볼 게이트: EMPTY(0/178). 부재 심볼: 0(Core 소유 심볼 전량 nm -D 존재, 브릿지 자체 심볼·legacy fallback명 예외는 설계상 정상).

## 결론
iter-2 두 finding(JV2-1/JV2-2) 모두 소스 대조로 해소 확인, 신규 반례 없음. I1/I2/I3 전 축 blocker/high/medium 0. low 1건(사문 헬퍼 메서드)은 CLEAN 비차단.

BINDINGS REVIEW CLEAN
