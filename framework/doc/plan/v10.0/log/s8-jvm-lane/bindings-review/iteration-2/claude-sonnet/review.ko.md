# S8 JVM bindings 전환 리뷰 iteration 2 — R2 (Claude Sonnet)

독립 리뷰. 다른 리뷰어 결과·coordinator 해석 미참조.

## 1. Scope 확인
- 명령: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle | grep -Ev '(^|/)native/(linux|darwin|win)|(^|/)resources/native/|/build/'`
- 대상 commit `50faf28fd`, HEAD(`e86213d3a`)와 scope 내 diff 없음(현재 워킹트리 = 대상 commit) 확인.
- 시작·종료 동일: **251 files**, `sha256sum <각 파일> | sha256sum` → `ec842c6a8947840a2edb35983f69bde59cd43198f097fb26da730d57d52fed61` (manifest·prompt 명시 hash와 일치). 리뷰 중 scope 파일 미수정.

## 2. iter-1 finding 해소 판정

| ID | 내용 | 판정 | 근거 |
|---|---|---|---|
| JV-F1 | `router_recv_part` 7→6 파라미터 | **재발(NOT 해소)** — 아래 §3 I1-1 참조 | 신규 반례 발견 |
| JV-F2 | 제거 FUNC downcall(`zlink_stream_bind_actor`/`_unbind_actor`/`_bound_actors`/`_send_bound_actor_part`, `zlink_get/set_spot_option`) | 해소 | scope 전체 grep 0 hit |
| JV-F3 | `zlink_stream_detach`/`attach`·`zlink_subscribe_handler`(+`subscribe_handler_fn`) | 해소 | scope 전체 grep 0 hit |
| JV-F4 | dead `NativeLayouts`(`zlink_actor_recv_info_t`/`_join_info_t`/`_route_t`, registry/actor-route 16개) + 비계약 helper(`zlink_router_handler`/`zlink_stream_attach*`/`zlink_stream_send*`) | 해소 | `NativeLayouts.java` 대상 grep 0 hit |
| JV-F5 | `zlink_router_handler` 중복 downcall(`Native.java` dead + `NativeMessage.java` live) | 해소 | `Native.java`에 `router_handler` 참조 0, `NativeMessage.java`에만 존재(정상) |

JV-F2~F5는 소스 대조로 해소 확인. **JV-F1은 새 반례로 재개**(coordinator manifest의 "router_recv_part 6param" 확인 주장과 배치되는 직접 증거 확보 — 아래 §3).

## 3. I1/I2/I3 Finding · Evidence · Verdict

### I1 — FFI downcall 정합 / ServiceLayouts / pull dispatch 수명 / raw-layer 잔존

**I1-1 [blocker, 신규] `zlink_router_recv_part` FunctionDescriptor arity(5) ≠ 호출부 arity(6) — MethodHandle 타입 불일치, 런타임 100% 실패**

- 위치: `bindings/java/src/main/java/systems/zlink/runtime/nativeapi/Native.java:336-340`(`MH_ROUTER_RECV_PART`), `:344-349`(`MH_ROUTER_RECV_PART_CRITICAL`), 호출부 `:1350-1352`, `:1367-1369`.
- 현재 선언:
  ```java
  FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
      ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
      ValueLayout.JAVA_INT)
  ```
  반환형 제외 파라미터 = `ADDRESS×4 + JAVA_INT×1` = **5개**.
- 동일 핸들의 호출부(`MH_ROUTER_RECV_PART.invokeExact(router, sourceNodeRidOut, requestSeqOut, partOut, hasMoreOut, flags)`)는 `MemorySegment×5 + int×1` = **6개 인자**로 호출.
- Core 실제 계약(`core/include/zlink/socket/api.h:271-276`):
  ```c
  zlink_router_recv_part(void *router_,
                          const zlink_routing_id_t **source_node_rid_out_,
                          uint64_t *request_seq_out_,
                          zlink_msg_t *part_out_,
                          zlink_part_flag_t *has_more_out_,
                          zlink_recv_flags_t flags_)
  ```
  = `ADDRESS×5 + INT×1` = 6-param. **호출부(6-arg)는 Core와 정확히 일치, FunctionDescriptor(5-param)만 불일치.**
- 근본원인: `git diff 5c2eb2acc 50faf28fd -- Native.java`로 실제 편집 확인 — iter-1 이전엔 `ADDRESS×6 + INT×1`(7-param, `source_spot_rid` 잔존분 포함)이었고, 마지막 줄 `ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));`를 `ValueLayout.JAVA_INT));`로 치환하며 **ADDRESS 2개가 삭제**됨(1개만 지워야 했음 — off-by-one). 호출부(`NativeRouterReceiveSupport.java`, `Native.java` 양쪽 invokeExact 인자 목록)는 정확히 `sourceSpotRidOut` 1개만 제거되어 올바르게 6-arg로 수정됨. 즉 두 반쪽 수정이 서로 어긋난 상태.
- 영향: `java.lang.foreign.Linker.downcallHandle(addr, fd)`로 생성된 `MethodHandle`의 실제 타입은 `(MemorySegment,MemorySegment,MemorySegment,MemorySegment,int)int`(5-param)인데, 호출부는 컴파일 시 `(MemorySegment,MemorySegment,MemorySegment,MemorySegment,MemorySegment,int)int`(6-param) 서명으로 `invokeExact`를 호출 — `invokeExact`는 정확한 타입 일치를 요구하므로 **`Native.routerRecvPart()`/`Native.routerRecvPartNoWaitCritical()`가 호출될 때마다 런타임 예외**(WrongMethodTypeException류) 발생. `RouterSocket.recv()`(공개 API)가 이 경로를 사용하므로 **router 기반 수신 전체가 불능** — iter-1 ledger가 "mesh 전환은 건전"이라 판정한 mesh 라우팅의 핵심 경로.
- 컴파일 시점 미검출 이유: FFM `invokeExact`의 서명 다형(polymorphic signature) 특성상 javac는 호출부 인자의 정적 타입으로 디스크립터를 합성할 뿐, 대상 `MethodHandle`의 실제 타입과 대조하지 않음 — 불일치는 링크(첫 호출) 시점에만 드러남. 따라서 manifest의 `:compileJava :samples:compileJava :kotlin-samples:compileKotlin ALL GREEN` 증거는 이 결함을 배제하지 못함.
- 전수 검증: scope 내 모든 `MethodHandle` 선언(98개, `downcall`/`downcallCritical`/`optionalDowncall`)에 대해 선언 파라미터 수와 모든 `invokeExact` 호출부 인자 수를 정규식+괄호깊이 파서로 1:1 대조 — **이 2건(정상/critical variant, 둘 다 `zlink_router_recv_part`) 외 불일치 없음**.
- **Verdict: NOT CLEAN(blocker)**. JV-F1 재개.

**I1-2 `ServiceLayouts.java` 15개 struct — Core 헤더 대조**

- `git diff 5c2eb2acc 50faf28fd -- ServiceLayouts.java` → 변경 없음(iter-1에서 두 리뷰어 모두 clean 판정, 무변경 파일).
- 재확인으로 `ROUTING_ID_LAYOUT`/`ACTOR_REF_LAYOUT`(`NativeLayouts.java`)을 `zlink/message/api.h:31-35`, `zlink/service/common.h:18-22`와, `RECEIVE_RECORD`(가장 복잡한 struct)를 `zlink/service/dispatch.h:102-125`의 `zlink_mesh_receive_record_t`와 필드 순서·타입 1:1 대조 — `operation_kind`(I32) 뒤 `pad(4)` 삽입까지 정확히 일치(8-byte aligned `reply_token` 배열 정렬 반영). 불일치 없음.
- **Verdict: CLEAN**.

**I1-3 pull-dispatch 배치 수명(`NativeReceiveBatch`/`NativeReadyBatch`)**

- `NativeReceiveBatch.retainMessage()`가 `Arena.ofConfined()`를 try-with-resources로 스코프하고, 네이티브 메시지를 `arena` 해제 전에 `InternalAccess.messageFromOwnedMessageVector`로 복사·소유권 이전 후 반환. 배치 핸들 자체는 `NativeServiceSymbols` lifecycle(`...New`/`...Reset`/`...Destroy`)로 명시적 관리, `close()`에서 null 방어. 이상 없음.
- **Verdict: CLEAN**.

**I1-4 raw-layer 잔존**

- §4의 제거심볼 게이트(179개 리터럴 전수 대조 0 hit)와 §2 JV-F2~F5 재확인으로 raw-layer 잔존 없음 확인.
- **Verdict: CLEAN**.

**I1 총 판정: NOT CLEAN(I1-1 blocker 1건)**

### I2 — POSD/DDD

- JV-F5(`router_handler` 중복 선언) 해소 확인(`Native.java`에 참조 0, `NativeMessage.java`에만 정상 존재).
- iter-1~2 사이 변경분(diff 10파일, 931줄 삭제/17줄 추가)은 전부 제거 심볼·dead code 삭제이며 신규 구조 도입 없음 — 신규 POSD/DDD 결함 소지 없음.
- **Verdict: CLEAN**.

### I3 — 정리(제거 심볼·dead code·no-hit)

- JV-F2/F3/F4 전량 해소(§2).
- scope 내 98개 `MethodHandle` 선언 전부 최소 1개 이상의 `invokeExact` 호출부를 가짐(dead 핸들 0건) — 자동 대조로 확인.
- **Verdict: CLEAN**(제거심볼/dead code 관점). 단 I1-1의 arity 결함 자체는 "정리 누락"이 아니라 "기능 결함"이므로 I3이 아닌 I1로 분류.

## 4. 제거심볼/폐기 no-hit 판정

- scope 내 `"zlink_..."` 문자열 리터럴 179개(`grep -rhoE '"zlink_[a-zA-Z0-9_]*"'`)를 python으로 `core/tests/contract/removed-identifiers-10.0.0.json`(FUNC 76 + TYPE 28 + REUSED_IDENTIFIER 1 + ENUM_TYPE 15 + ENUMERATOR 65 + MACRO 10 + FIELD 109 = 합집합 303개)와 전수 대조.
- **HITS: 0건 — 제거심볼 게이트 EMPTY 확인**(manifest 주장과 일치).

## 5. 최종 판정

I1에서 신규 blocker 1건(`zlink_router_recv_part` FunctionDescriptor/invokeExact arity 불일치로 router 수신 경로 런타임 전량 실패) 확인. JV-F1은 해소되지 않고 다른 형태로 재발했음 — coordinator는 §3 I1-1의 정확한 라인·근본원인(off-by-one 토큰 삭제)을 반영해 재수정 필요.

BINDINGS REVIEW NOT CLEAN
