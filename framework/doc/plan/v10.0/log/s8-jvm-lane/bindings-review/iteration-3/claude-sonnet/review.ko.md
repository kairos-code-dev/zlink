# S8 JVM bindings 전환 리뷰 — iteration 3 (R2 Claude Sonnet)

독립 리뷰. 다른 리뷰어 결과·coordinator 해석 미참조.

## 1. Scope 확인
- 대상 commit: `39b1edee8`
- `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/(linux|darwin|win)`·`resources/native`·`/build/` 제외 → **251 files**(기대값 일치).
- aggregate SHA-256(`git ls-files ... | xargs sha256sum | sha256sum`, 파일 목록은 `git ls-files` 자연 순서=`LC_ALL=C sort`와 동일함을 확인) → `f35dd2fe28be90088b482a799e45389d4fab259c80366b527fa5d9e38a94af95`, **prompt/manifest 기대값과 일치**.
- 현재 워킹트리 HEAD(`f8c8f32aa1`)와 대상 commit(`39b1edee8`) 사이 scope 파일 diff: `git diff 39b1edee8 HEAD --stat -- <251 files>` → **empty**(HEAD는 iteration-3 log 문서 2개만 추가한 freeze 커밋). 워킹트리는 대상 commit과 scope상 동일.
- 리뷰 종료 시 파일 수·hash 재확인: 251 / `f35dd2fe...` 동일(scope 파일 미수정, read-only 정적 대조만 수행).

## 2. iter-2 finding 해소 판정

### JV2-1. `zlink_router_recv_part` descriptor over-correct — **RESOLVED**
- `Native.java:336-349`: `MH_ROUTER_RECV_PART`, `MH_ROUTER_RECV_PART_CRITICAL` 두 변형 모두 `FunctionDescriptor.of(JAVA_INT, ADDRESS×5, JAVA_INT)` = 5 ADDRESS + 1 INT = 6파라미터로 복원됨.
- invokeExact 호출부 `Native.java:1350`(`routerRecvPart`), `:1367`(`routerRecvPartNoWaitCritical`) 둘 다 6인자(`router, sourceNodeRidOut, requestSeqOut, partOut, hasMoreOut, flags`)로 descriptor와 정확 일치.
- Core 시그니처 재대조: `core/include/zlink/socket/api.h:271-277`
  ```c
  zlink_recv_result_t zlink_router_recv_part(
    void *router_, const zlink_routing_id_t **source_node_rid_out_,
    uint64_t *request_seq_out_, zlink_msg_t *part_out_,
    zlink_part_flag_t *has_more_out_, zlink_recv_flags_t flags_);
  ```
  5개 포인터 + 1개 int = 6파라미터, Java descriptor와 완전 일치. `WrongMethodTypeException` 유발 요인 제거 확인.

### JV2-2. `zlink_router_handler` Core 부재 — **RESOLVED**
- scope 전체 grep `router_handler` → 0건(제거 확인).
- `NativeMessage.java:38-41`, `Native.java:81-82`에 `MH_RECV_HANDLER`로 `zlink_recv_handler`(3 ADDRESS) 재정의. Core `socket/api.h:81-83` `zlink_recv_handler(void *s_, zlink_socket_msg_handler_fn handler_, void *userdata_)`와 파라미터 수·타입 일치.
- 콜백 시그니처: `NativeRouterReceiveSupport.java`의 `FD_RECV_HANDLER = ofVoid(ADDRESS, ADDRESS, JAVA_LONG, ADDRESS)`(4파라미터), `SocketCore.java`의 `FD_RECV_CALLBACK`도 동일 4파라미터. Core `socket/api.h:41-44`:
  ```c
  typedef void (*zlink_socket_msg_handler_fn)(
    const zlink_routing_id_t *source_rid_, zlink_msg_t *parts_,
    size_t part_count_, void *userdata_);
  ```
  ADDRESS, ADDRESS, size_t(=JAVA_LONG), ADDRESS = 4파라미터로 정확 일치. `handleReceiveCallback`/`snapshotReceive`/`dispatchReceive`/`CallbackReceivedData` record 전부 `sourceSpotRid`·`requestSequence` 필드 제거로 일관되게 갱신됨(`git diff 50faf28fd..39b1edee8` 확인).
- Router 소켓의 `onReceive()` 실행 경로: Core는 `zlink_recv_handler`를 raw STREAM 외 subject에 ENOTSUP으로 거부 — Router에서 호출 시 `rc != 0` → `ZlinkException.fromLastError(...)`로 정상 전파, arena/executor는 실패 시 `clearExecutorIfCreated`/`closeArena`로 정리됨(누수 없음). 설계 의도("router 콜백 수신은 미지원, 폴링 recv만")와 일치.

두 finding 모두 소스 대조로 해소 확인, 신규 반례 없음.

## 3. I1/I2/I3 — 전체 scope 재검토

### I1 — FFI downcall arity/type
- 5개 FFI 선언 파일(`Native.java`/`NativeMessage.java`/`NativePollerSymbols.java`/`NativeServiceSymbols.java`/`NativeSymbols.java`) 전체를 정규식+괄호깊이 파서로 스캔(iter-2 R2가 사용한 스크립트를 `MH_` 접두사 제한 없이 일반화 — `NativeServiceSymbols.java`가 `MH_` 접두사 없는 명명 규칙을 쓰는 것을 확인하고 확장).
- **184개 MethodHandle 선언 / 186개 invokeExact 호출부 / 불일치 0건**, 호출부 없는 미사용 핸들 0건. (iter-2 R2가 검증한 범위(98/110, `Native.java` 중심)보다 확장·완전한 전수 대조.)
- 콜백 upcall 시그니처 6종을 Core 헤더 typedef와 1:1 대조(파라미터 수·타입·반환형 포함):
  - `zlink_thread_start` entry — `THREAD_ENTRY = ofVoid(ADDRESS)`(1파라미터), 일치.
  - `zlink_reply_handler_fn` — `RoutedRequestSupport.FD_REPLY_CALLBACK = ofVoid(I,A,L,A)` vs Core `(zlink_request_result_t, zlink_msg_t*, size_t, void*)`, 일치.
  - `zlink_timer_handler_fn` — `NativeTimer.FIRE_HANDLER = ofVoid(A,L,A)` vs Core `eventing/api.h:217`(`void*, uint64_t, void*`), 일치.
  - `zlink_monitor_handler_fn` — `NativeMonitorSocket.FD_MONITOR_CALLBACK = ofVoid(A,A)` vs Core `eventing/api.h:23`, 일치.
  - `zlink_mesh_ready_handler_fn` — `NativeServiceSymbols.READY_HANDLER_DESCRIPTOR = of(I,A,I,A)`(반환 포함) vs Core `service/dispatch.h:138-141`(반환 `zlink_mesh_ready_domain_mask_t`, 파라미터 `void*, mask_t, void*`), 반환형·파라미터 모두 일치.
  - `zlink_socket_msg_handler_fn` — 상기 §2 JV2-2에서 상술(2개 구현체 모두 일치).
- 대표 non-JV2 handle 재대조: `zlink_stream_session_unbind_actor`(`NativeServiceSymbols.java:187-188`, `of(I,A,A,A,L,A,I)`=6파라미터) vs Core `service/stream_session.h:68-74`(`void*, const rid*, const actor_ref*, uint64_t, mesh_operation_id_t*, uint32_t`) — 일치.
- `ServiceLayouts.java`/`NativeLayouts.java`: iter-2 대상~iter-3 대상 사이 `git diff --stat` 결과 두 파일 **무변경**(이번 iteration 변경분은 `Native.java`(4줄)/`NativeMessage.java`(16줄)/`NativeRouterReceiveSupport.java`(46줄) 3개뿐). iter-1·iter-2에서 이미 `ROUTING_ID_LAYOUT`/`ACTOR_REF_LAYOUT`/`RECEIVE_RECORD` 등 Core 구조체 대조 완료·불변. 재확인 차원에서 `MESH_NODE_OPTIONS`/`MESH_PEER_ENTRY`/`ACTOR_LOCATION` 등 필드 순서 육안 재검토 — 이상 없음.
- **Verdict: CLEAN**(blocker/high/medium 0).

### I2 — POSD/DDD
- 변경 파일 3개는 모두 기존 패턴(필드 개수 축소, 콜백 파라미터 단순화)을 그대로 따르는 최소 수정 — 신규 구조 결함 없음.
- `NativeRouterReceiveSupport`가 `SocketCore`의 공용 `SocketCallbackSupport`를 재사용하지 않고 자체 arena/executor 관리를 쓰는 구조적 중복이 존재하나, `git diff 50faf28fd..39b1edee8`로 확인한 결과 이 구조 자체는 JV2-2 수정 이전부터 존재(변경분은 심볼명·파라미터 개수·record 필드뿐) — iter-1/iter-2에서 이미 통과된 pre-existing 영역이며 신규 반례 아님.
- 최대 파일(`Native.java` 1542줄)은 이번 iteration 4줄 diff로 실질 증가 없음 — 구조 재검토 대상 아님.
- **Verdict: CLEAN**(blocker/high/medium 0).

### I3 — 정리(제거·부재 심볼·dead code·no-hit)
- scope 전체 `"zlink_..."` 문자열 리터럴 **178개** 추출 → `core/tests/contract/removed-identifiers-10.0.0.json`(FUNC 76·TYPE 28·REUSED 1·ENUM_TYPE 15·ENUMERATOR 65·MACRO 10·FIELD 109) 전량 대조 → **0건 hit**.
- `nm -D core/build/lib/libzlink.so.10.0.0` 대조: 178개 중 미발견 3건 전부 정상 사유 확인:
  - `zlink_java_msg_data_addr`/`zlink_java_send_u32` — `bindings/java/native/src/zlink_java_reqrep_bridge.c`에 정의된 Java 브릿지 자체 export(별도 helper lib), Core `libzlink.so` 심볼이 아님.
  - `zlink_msgv_close` — `downcallAny(["zlink_multipart_close","zlink_msgv_close"], ...)`의 legacy fallback명. 1순위 `zlink_multipart_close`가 nm에 존재(1건)하여 실제 링크는 항상 전자로 성립, 후자는 미선택 fallback으로 정상.
  - `zlink_router_handler`(JV2-2로 제거) — nm -D 0건, scope grep도 0건 — 일관.
- JV-F2~F5 9개 no-hit 패턴 재검증(`stream_bind_actor`/`_unbind_actor`/`_bound_actors`/`_send_bound_actor_part`/`get_spot_option`/`set_spot_option`/`stream_detach`/`stream_attach`/`subscribe_handler`) — `_unbind_actor` 3건은 전부 유효 API `zlink_stream_session_unbind_actor`(nm 존재, removed-set에 없음)이며 제거 대상인 `zlink_stream_unbind_actor`(session 없는 구명)와 다른 문자열 — 오탐 아님. 나머지 8패턴 0건.
- **Low finding 1건**: `Native.java:34-36`의 `private static MethodHandle optionalDowncall(String, FunctionDescriptor)`(및 그 안에서만 호출되는 `NativeSymbols.optionalDowncall`, `NativeSymbols.java:42-47`)가 scope 전체에서 정의 2곳 외 호출부 0건 — 완전 미사용 사문(死文). `git log -p`로 추적한 결과 과거 `MH_JAVA_ROUTER_RECV = optionalDowncall(...)` 호출부가 존재했으나 이후 리팩토링(라우터 recv 경로 전환)으로 호출부만 제거되고 헬퍼가 잔존한 것 — **JV2 수정과 무관, iter-1 이전부터 존재**, 기능·컴파일 영향 0.
- **Verdict: CLEAN**(blocker/high/medium 0; low 1건 별도 기록, s8-node-lane iteration-4 선례와 동일하게 CLEAN 비차단).

## 4. 제거·부재 심볼 판정
- 제거 심볼 게이트: **EMPTY**(178개 `zlink_*` 리터럴 중 removed-identifiers-10.0.0.json hit 0).
- 부재 심볼: **0**(Core 소유 `zlink_*` 심볼 전량 `nm -D libzlink.so.10.0.0`에 존재). 미발견 3건은 전부 Java 브릿지 자체 export 또는 미선택 legacy fallback명으로 설계상 정상.
- `zlink_recv_handler` ∈ nm(1건), `zlink_router_handler` ∉ nm(0건) — manifest 기록과 일치.

## 5. Low finding 목록
- L [low] `Native.optionalDowncall`(`Native.java:34-36`) + `NativeSymbols.optionalDowncall`(`NativeSymbols.java:42-47`) — 호출부 0건 사문 헬퍼. 과거 `MH_JAVA_ROUTER_RECV` optional-symbol 경로 제거 잔재, JV2와 무관, 기능 영향 0. 후속 정리 시 제거 권장.

## 6. 결론
iter-2 두 finding(JV2-1/JV2-2) 소스 대조로 해소 확인, 신규 반례 없음. I1(184 handle/186 call site 전수 arity·콜백 시그니처 대조)/I2/I3(178 literal 전수 removed-symbol·nm 대조) 세 축 모두 blocker/high/medium 0. low 1건(사문 헬퍼)은 CLEAN 비차단.

BINDINGS REVIEW CLEAN
