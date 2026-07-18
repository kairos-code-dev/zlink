# S8 JVM bindings 전환 리뷰 — iteration-2, R1(codex)

독립 adversarial 리뷰어 R1. R2(claude-sonnet) 산출물·coordinator 해석을 판정 근거로 쓰지 않음.
정적 소스 대조만 수행(build/실행 금지, 실행 증거는 manifest).

## 1. Scope 확인
- commit `50faf28fd`(frozen `e86213d3a`, ec842c6a 동결).
- 시작·종료 aggregate SHA-256 = `ec842c6a8947840a2edb35983f69bde59cd43198f097fb26da730d57d52fed61` = 기대값. 파일 251. 파일 수정 없음.

## 2. iter-1 finding 해소 판정 (@ 50faf28fd)

| ID | iter-1 요지 | 판정 | 근거 |
|---|---|---|---|
| JV-F1 | router_recv_part 7→6 param | **미해소(over-correction, 신규 blocker)** | 아래 JV-R1-1 |
| JV-F2 | 제거 FUNC downcall(stream actor-bind ×4, get/set_spot_option ×2) | 해소 | 6개 심볼 문자열 리터럴 0건. `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS` 0건 |
| JV-F3 | stream detach/attach + subscribe_handler | 해소 | `zlink_stream_detach/attach` 0건, `zlink_subscribe_handler` 0건. STREAM close 경로는 `zlink_stream_packet_handler` 모델(MH_STREAM_PACKET_HANDLER) |
| JV-F4 | dead NativeLayouts 미러 + 비계약 raw helper | **부분 해소** | actor_recv/join/route·registry 레이아웃 미러 제거 확인. 단 비계약 helper `zlink_router_handler` 잔존 → JV-R1-2 |
| JV-F5 | router_handler downcall 중복(Native.java dead) | 해소 | `zlink_router_handler`가 Native.java에서 제거, NativeMessage.java 1곳만 잔존 |

제거심볼 게이트: JVM 소스의 모든 `"zlink_*"` 리터럴(179) ∩ removed-identifiers FUNC/TYPE(104) = **∅**. 통과.

## 3. 3축 Finding / Evidence / Verdict

### I1 — FFI downcall 시그니처·arity·타입 Core 정합

#### JV-R1-1 [BLOCKER] `zlink_router_recv_part` 디스크립터 arity 부족 (4 ADDRESS, 필요 5) — iter-1 JV-F1 over-correction

- **Core 계약** `core/include/zlink/socket/api.h:271-277`:
  `zlink_router_recv_part(void*, const zlink_routing_id_t**, uint64_t*, zlink_msg_t*, zlink_part_flag_t*, zlink_recv_flags_t)`
  = **5 ADDRESS + 1 INT = 6 param**, 반환 `zlink_recv_result_t`(INT).
- **JVM 디스크립터** `Native.java:336-340`(및 critical 변형 `344-349`):
  `FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS, JAVA_INT)`
  = 반환 INT + **4 ADDRESS + 1 INT = 5 param**. → Core보다 ADDRESS 1개 부족.
- **wrapper 불일치**(자기모순): `Native.routerRecvPart(...)` `1343-1356`은
  `MH_ROUTER_RECV_PART.invokeExact(router, sourceNodeRidOut, requestSeqOut, partOut, hasMoreOut, flags)`
  = **6 인자(MemorySegment×5 + int)**를 넘긴다. 디스크립터 MethodType은 `(MemorySegment×4,int)int`(5 인자).
  → `invokeExact`는 call-site MethodType == handle MethodType 정확 일치를 요구하므로 **첫 호출 시 `WrongMethodTypeException`**. critical 변형(`1360-1373`)도 동일.
- **git 근거**: 수정 커밋 `4205f73f1`은 iter-1의 7-param 디스크립터(6 ADDRESS+INT)에서 `ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT` 줄을 `ValueLayout.JAVA_INT`로 치환 → **ADDRESS 2개 제거**(7→5 param). 올바른 수정은 여분 `source_spot_rid` ADDRESS **1개만** 제거(7→6 param). wrapper는 인자 1개(`sourceSpotRidOut`)만 제거해 6-인자로 정상 축소했으나 디스크립터는 2개를 제거해 **디스크립터와 wrapper가 어긋남**.
- **원인 유형**: `zlink_recv_part`(정당히 4 ADDRESS: s, source_rid_out, part_out, has_more_out, flags)와 `zlink_router_recv_part`(5 ADDRESS: router, source_node_rid_out, **request_seq_out**, part_out, has_more_out, flags)를 혼동. 여분 `request_seq_out_` ADDRESS까지 제거됨.
- **실행경로(latent 아님)**: 공개 `RouterSocket.recv(received, RecvFlags.NONE)`
  → `NativeRouterSocket.recv:58-72`(non-DONT_WAIT 분기 `routerRecv`)
  → `NativeRouterReceiveSupport.recvDirectOnceIntoImpl:289`
  → `routerRecvPart:511-516` → `Native.routerRecvPart` → **위 invokeExact 폭발**.
  `bindings/{java,kotlin}/samples/.../DealerRouterRecvSample`가 정확히 이 경로(`router.recv(...)`)를 실행. DONT_WAIT 경로(`routerRecvInto`)도 `routerRecvPart` 경유라 동일.
- **manifest 대비**: manifest의 "router_recv_part 6param"은 **wrapper**(6 param)에만 성립하고 **디스크립터**(5 param)에는 불성립. manifest의 samples-green은 이 frozen 소스와 모순(DealerRouterRecvSample는 반드시 throw). → 게이트가 실제로 이 소스/재빌드 lib로 그 샘플을 돌렸는지 재확인 필요.
- **대조 결과 정합 확인용**(같은 방식 8종 전수, 모두 OK): router_request_part 8, dealer_request_part 7, router_reply_part 5, recv_part 5, send_part 4, send_part_rid 5, publish_part 5, subscribe_part 8 — **router_recv_part만 불일치**.
- **Verdict**: NOT CLEAN. 수정안 = 두 디스크립터 모두 `ADDRESS`를 5개로(현재 4개 뒤에 `request_seq_out` 자리 ADDRESS 1개 추가), 즉 `of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS, ADDRESS, ADDRESS, JAVA_INT)`.

#### JV-R1-2 [HIGH] `zlink_router_handler` — Core/bridge 부재 심볼을 공개 경로에서 downcall

- `NativeMessage.java:36-39`: `MH_ROUTER_HANDLER = NativeSymbols.downcall("zlink_router_handler", of(JAVA_INT, ADDRESS, ADDRESS, ADDRESS))`.
- `zlink_router_handler`는 **Core 소스·헤더 전무**, **export map `core/src/libzlink.vers` 부재**(동류 `zlink_recv_handler`:132, `zlink_send_ready_handler`:139만 존재), **JVM bridge `zlink_java_reqrep_bridge.c` 부재**. 전 native 트리에서 정의 0건.
- `NativeSymbols.downcall`(`NativeSymbols.java:28-32`)은 심볼 부재 시 `missingDowncall` throwing handle 반환 → **호출 시** `IllegalStateException("Missing native symbol 'zlink_router_handler'")`. class-init은 통과(lazy)라 로드/컴파일은 green.
- **공개 경로**: `RouterSocket.onReceive(handler)` → `NativeDealerRequestSupport.java:70` → `NativeRouterReceiveSupport.onReceive:108-129` → `NativeMessage.routerHandler:142-148` → 폭발. 현재 sample은 onReceive 미사용이라 latent(=samples green과 무모순)지만 공개 API 결함.
- removed-identifiers 게이트 사각지대: 이 심볼은 "제거됨"이 아니라 "애초에 없음"이라 문자열 게이트에 안 걸림. iter-1 JV-F5는 NativeMessage 사본을 "live"로 분류했으나 실제 Core 부재라는 **신규 반례**로 재개.
- **Verdict**: NOT CLEAN. 조치 = `zlink_recv_handler` 모델로 재배선하거나(라우터 수신 콜백을 recv_handler로 통일) 해당 helper·onReceive-handler 경로 제거.

### I2 — POSD / DDD
- raw/service 레이어 경계, ThreadLocal scratch(RECV_OUT_SCRATCH·MULTIPART_RECEIVE_SCRATCH) 재사용, Arena 수명(confined/shared) 처리 관측 범위 내 구조적 결함 없음. critical 변형은 DONT_WAIT 계약 주석과 함께 분리되어 있어 안전 계약 명시적. **CLEAN**.

### I3 — 정리(제거심볼·dead code·no-hit)
- 제거 FUNC/TYPE 문자열 리터럴 0건, dead NativeLayouts 미러 제거 확인.
- 그러나 JV-R1-2의 `zlink_router_handler`는 Core 부재 심볼을 참조하는 dead/broken downcall(제거 대상). **NOT CLEAN**.

## 4. 제거심볼 / 폐기 no-hit 판정
- removed-identifiers-10.0.0.json FUNC/TYPE 대조: 제거 심볼 문자열 참조 **0**. 통과.
- 폐기 raw helper(JV-F2/F3/F4 대상) 개별 grep: stream actor-bind ×4·get/set_spot_option·subscribe_handler·stream detach/attach = **전부 0건**. 통과.
- 예외: `zlink_router_handler`(removed 목록 밖·Core 부재) 1건 잔존 → JV-R1-2.

## 5. 결론
iter-1 raw-layer 수정은 대부분 정착했으나 핵심 항목 JV-F1이 **과교정**되어 `zlink_router_recv_part` 디스크립터가 Core(5 ADDRESS) 및 자기 wrapper(5 segment)와 어긋나는 4-ADDRESS로 남았고(공개 `RouterSocket.recv` 실행 시 `WrongMethodTypeException`), 부수로 Core 부재 심볼 `zlink_router_handler` downcall이 잔존한다. 각 축 CLEAN=finding 0 요건 불충족.

BINDINGS REVIEW NOT CLEAN
