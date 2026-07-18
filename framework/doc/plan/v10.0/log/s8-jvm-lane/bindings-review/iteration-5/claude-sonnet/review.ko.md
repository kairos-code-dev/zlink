# S8 JVM bindings 전환 리뷰 — iteration 5 — R2(Claude Sonnet)

독립 리뷰. R1(opus)·coordinator 해석을 판정 근거로 사용하지 않음. 정적 소스 대조만 수행(build/실행 없음).

## 1. Scope 확인
- 대상 commit: `fbd35a1ef`
- Scope 명령: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/(linux|darwin|win)`·`resources/native`·`/build/` 제외
- 파일 수: **251** (일치)
- 현재 `HEAD`(`2e8164c17`)는 `fbd35a1ef`의 후손이며, `git diff fbd35a1ef HEAD -- <scope>` = empty → working tree scope가 대상 commit과 동일. working tree 파일로 aggregate SHA-256 재계산:
  파일목록 `LC_ALL=C sort` → 파일별 `sha256sum "$f"`(파일명 포함) → 그 출력 자체를 `sha256sum`
  = **`1351d5dabce124a8cefea369437cc23bd9f1eecf0ce59079056b6add2ad54192`** — prompt 기대값과 **일치**.
- `git log fbd35a1ef..HEAD -- core/include/` = empty → 대조에 사용한 Core 헤더(working tree)는 대상 commit 시점과 동일.

## 2. iter-4 finding 해소 판정

### JV4-1 (blocker) — 해소 확인
`git diff 41660e37b fbd35a1ef -- bindings/java/native/src/zlink_java_reqrep_bridge.c`로 diff 실측:
- dead+broken `zlink_java_router_recv`(6-param Core `zlink_router_recv_part`를 7인자로 오호출하던 함수, 헬퍼 `zlink_java_recv_result_from_errno`/`zlink_java_close_router_recv_parts` 포함) 전체 삭제.
- 전용 `extern "C" int zlink_router_enable_spot_receive (void *router_);` 선언(Core에 대응 정의 없음) 삭제.
- 파일 127줄 → 39줄.
- scope 전체(native/src 포함) `zlink_java_router_recv`·`zlink_router_enable_spot_receive`·`zlink_router_recv_part`(native/src 내) grep = **0건**.
- native/src에 남은 Core 호출 2건(`zlink_msg_data`, `zlink_send_part_rid`) 모두 `core/include/zlink/{message,socket}/api.h` 시그니처와 arity/타입 일치 확인(§5 참조).

**판정: 해소.**

### 3 low (SPOT_TOPIC/SPOT_PAYLOAD/ERRNO_EFSM) — 해소 확인
`git diff 41660e37b fbd35a1ef`로 diff 실측:
- `SampleSupport.java`: `SPOT_TOPIC`/`SPOT_PAYLOAD` 상수 2줄 삭제.
- `NativeSocketRuntime.java`: `ERRNO_EFSM` 상수 1줄 삭제.
- scope 전체 grep = **0건**(참고: `bindings/java/src/test/...SocketContractTest.java`에 독립적인 로컬 `ERRNO_EFSM` 선언이 있으나 `src/test`는 scope 밖이며 삭제된 상수를 참조하지도 않음 — 무관).

**판정: 해소.** 두 finding 모두 새 반례 없음, 재개하지 않음.

## 3. I1/I2/I3 Finding

### I1 — FFI descriptor ↔ callsite ↔ Core arity/시그니처, ServiceLayouts, upcall, 제거·부재 심볼
- **native/src C bridge**: 잔존 Core 호출 2건(`zlink_msg_data`(1-arg), `zlink_send_part_rid`(5-arg)) 전부 Core 헤더와 arity/타입 일치. 사용된 상수(`ZLINK_SUBMIT_OK`/`ZLINK_SUBMIT_INVALID_STATE`/`ZLINK_PART_MORE`/`ZLINK_PART_FINAL`)와 구조체(`zlink_routing_id_t`) 전부 Core에 실존.
- **`Native.java`**(85개 downcall): 버전/컨텍스트/소켓/송수신/옵션/구독/모니터/TLS/proxy/atomic counter/timer/stopwatch/thread/router 계열 전부 `core/api.h`·`socket/api.h`·`eventing/api.h`와 1:1 대조 완료, 불일치 0. `invokeExact` 호출부 인자수도 디스크립터와 85/85 일치.
- **`NativePollerSymbols.java`**(13개), **`NativeMessage.java`**(12개): 전부 Core `eventing/api.h`·`message/api.h`와 일치.
- **`NativeServiceSymbols.java`**(877줄, 75개 downcall): 정규식 기반 스크립트로 `mesh_node.h`/`actor.h`/`spot.h`/`dispatch.h`/`stream_session.h`/`common.h`에서 추출한 Core 함수 arity와 전수 대조 — **불일치 0, Core 부재 심볼 0**. `invokeExact` 호출부 인자수도 75/75 디스크립터와 일치, dead 디스크립터 0.
- **Upcall(콜백) 디스크립터** 6곳(`NativeZlinkThread`/`RoutedRequestSupport`/`SocketCore`/`NativeRouterReceiveSupport`/`NativeTimer`/`NativeMonitorSocket`) + `NativeServiceSymbols.READY_HANDLER_DESCRIPTOR`: 전부 Core `zlink_thread_fn`/`zlink_reply_handler_fn`/`zlink_socket_msg_handler_fn`/`zlink_send_ready_handler_fn`/`zlink_stream_packet_handler_fn`/`zlink_timer_handler_fn`/`zlink_socket_monitor_handler_fn`/`zlink_mesh_ready_handler_fn` typedef와 파라미터 타입·개수 일치.
- **`ServiceLayouts.java`/`NativeLayouts.java`**: 대표 구조체(`MESH_NODE_OPTIONS`/`MESH_PEER_CONNECTION_OPTIONS`/`MESH_NODE_STATUS`/`ACTOR_REF_LAYOUT`/`ROUTING_ID_LAYOUT`/`MONITOR_EVENT_LAYOUT`) 필드 순서·타입·패딩을 Core 구조체와 대조, 매크로 상수(`ZLINK_ACTOR_ID_MAX`=255→256바이트, `ZLINK_MESH_NAME_MAX`=255→256바이트, `ZLINK_MESH_ENDPOINT_MAX`=511→512바이트) 일치.
- **no-hit**: `Native.java`(85)·`NativePollerSymbols.java`(13)·`NativeMessage.java`(12)·`NativeServiceSymbols.java`(75) 전부 선언=호출 1:1, 미사용 디스크립터 **0**.
- **제거·부재 심볼 게이트**: `zlink_java_router_recv`/`zlink_router_enable_spot_receive`/native/src 내 `zlink_router_recv_part` 오용 = 0건. 별도 Core-부재 심볼 신규 발견 없음.

severity: 없음(blocker/high/medium 0).

**Finding I1-L1 (low, 신규)**: JV4-1 삭제로 `zlink_java_reqrep_bridge.c` 상단의 `#include <errno.h>`/`<stdlib.h>`/`<vector>` 3개가 고아 include로 남음(삭제된 함수가 `errno`/`EAGAIN`/`std::vector`를 사용했었음). 현재 파일에 `errno`/`std::vector`/`malloc`류 참조 0건. 컴파일은 깨지지 않음(coordinator manifest: `buildZlinkJavaBridge` ALL GREEN) — 순수 정리 대상.
Evidence: `bindings/java/native/src/zlink_java_reqrep_bridge.c` 1-4행, 파일 내 `errno`/`std::vector`/`malloc|free|realloc|calloc` grep 결과 include 라인 자체 외 0건.
Verdict: non-blocking low.

**Finding I1-L2 (low, 참고)**: `zlink_has`는 Core에서 C++ `bool`(1바이트) 반환(`core/src/api/socket/socket_api.cpp:169`)이지만 `Native.java`의 `MH_HAS` 디스크립터(`FunctionDescriptor.of(JAVA_INT, ADDRESS)`)는 4바이트 `JAVA_INT` 반환으로 선언됨. scope 전체에서 `bool` 반환 Core 함수는 이 1건뿐. x86-64 SysV/Windows x64 ABI 관례상 컴파일러가 bool 반환 시 레지스터 상위 바이트를 통상 clear하여 실무상 오동작 사례는 없으나, FFI 서술의 엄밀성 관점에서 기록.
Evidence: `core/include/zlink/core/api.h:141`(`ZLINK_EXPORT bool zlink_has (...)`), `core/src/api/socket/socket_api.cpp:169`, `Native.java:251-252`.
Verdict: non-blocking low(정보성).

### I2 — POSD·DDD
- 이번 diff(iter-4 fix)는 순수 삭제(dead+broken 함수·미사용 상수)로 POSD/DDD 응집도를 오히려 개선. 신규 위반 없음.
- scope 전체 재검토 범위에서 새로운 POSD/DDD 위반 미발견.

severity: 없음.

### I3 — 정리(dead code·no-hit)
- no-hit 0(§I1 참조).
- I1-L1(고아 include 3개)이 이 축의 유일한 신규 항목.
- 그 외 scope 전체에서 이번 diff로 인한 신규 dead code 없음.

severity: 없음(I1-L1 low만 해당, 위에서 중복 기재하지 않고 low 목록에만 집계).

## 4. Low 목록 (비차단)
1. **I1-L1**: `zlink_java_reqrep_bridge.c`의 고아 include 3개(`<errno.h>`/`<stdlib.h>`/`<vector>`) — JV4-1 삭제 후 정리 누락.
2. **I1-L2**: `zlink_has` FFI 디스크립터가 Core `bool`(1바이트) 반환을 `JAVA_INT`(4바이트)로 서술 — 실무 안전, 서술 정확성 참고.

## 5. 제거·부재 심볼·arity 판정
- `zlink_java_router_recv`/`zlink_router_enable_spot_receive`: scope 전체 0건(완전 제거 확인).
- native/src 내 `zlink_router_recv_part` 오용(7인자 호출): 0건(함수 자체가 삭제되어 호출부 없음).
- `SPOT_TOPIC`/`SPOT_PAYLOAD`/`ERRNO_EFSM`: scope 전체 0건(scope 밖 test의 독립 로컬 선언 1건은 무관).
- Java FFI descriptor(85+13+12+75=185개 downcall + upcall 7개) 전수 재대조: Core 부재 심볼 0, arity 불일치 0.
- Core 헤더(`core/include/zlink/`) 자체는 `fbd35a1ef` 이후 무변경(scope 밖이지만 대조 기준 안정성 확인).

## 6. 최종 판정
I1/I2/I3 3축 모두 blocker/high/medium **0**. low 2건은 비차단으로 기록.

BINDINGS REVIEW CLEAN
