# S8 JVM bindings 전환 리뷰 — iteration 5 · R1(opus)

독립 리뷰. 다른 리뷰어(R2)·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조만(build/실행 없음).

## 1. Scope 확인
- 대상 commit: `fbd35a1ef` (HEAD `2e8164c17` = 동일 scope freeze).
- 파일 수: **251** (일치).
- aggregate SHA-256: `1351d5dabce124a8cefea369437cc23bd9f1eecf0ce59079056b6add2ad54192` — prompt 값과 **일치**.
- 파일 수정 없음. 시작·종료 hash 동일.

## 2. iter-4 finding 해소 판정
### JV4-1 (blocker) — **해소**
- `bindings/java/native/src/zlink_java_reqrep_bridge.c`가 40줄로 축소. dead+broken `zlink_java_router_recv`, 전용 helper, Core-부재 `zlink_router_enable_spot_receive` 선언 전부 제거.
- native/src의 남은 Core 호출부를 Core 시그니처와 1:1 대조:
  - `zlink_msg_data(msg)` → Core `void* zlink_msg_data(zlink_msg_t*)` = 1-arg. 일치.
  - `zlink_send_part_rid(socket, &rid, &parts[i], (zlink_send_flags_t)flags, part_flag)` = 5-arg → Core `zlink_send_part_rid(void*, const zlink_routing_id_t*, zlink_msg_t*, zlink_send_flags_t, zlink_part_flag_t)` = 5-param. arity·타입 일치.
  - `zlink_routing_id_t{uint8_t size; uint8_t data[255]}` → 브리지의 size=4·data[0..3] 사용과 정합.
- **7-arg `zlink_router_recv_part` 호출부는 native/src에서 완전 소거됨.** Java 측도 descriptor 6-arg + invokeExact 6-arg + Core 6-param 삼자 일치(§4).

### 3 low(SPOT_TOPIC / SPOT_PAYLOAD / ERRNO_EFSM) — **해소**
- src/main·samples·kotlin-samples scope에서 세 심볼 부재. (`ErrorCode.EFSM` enum 항목은 별개의 정상 계약 항목으로 유지.)

해소된 iter-1~4 finding은 새 반례 없이 재개하지 않음.

## 3. 전체 scope 3축 재검토

### I1 — FFI/native 경계 (downcall·upcall·bridge·layout·심볼)
- **C bridge Core 호출부**: §2 대조로 arity/타입 전부 일치.
- **downcall descriptor sweep**: `Native.java`의 85개 `downcall`/`downcallCritical` descriptor 전부 Core 시그니처와 arity·타입클래스 일치(ADDRESS↔pointer/void*/struct*, JAVA_INT↔int/enum/uint32/bool/*_result_t, JAVA_LONG↔size_t/uint64, ofVoid↔void). 모든 `invokeExact` 인자 수 = descriptor 인자 수. 고-arity(`zlink_subscribe_part` 8/8, `zlink_router_request_part` 8/8, `zlink_dealer_request_part` 7/7, `zlink_router_recv_part` 6/6) 및 `void**` out-param(단일 ADDRESS + holder segment) 포함. **불일치 0.**
- **Java-side bridge helper**: `zlink_java_send_u32` descriptor `of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_LONG, JAVA_INT)` = 5-arg → 브리지 정의 5-param 일치(Core header 부재는 정상, native/src 정의).
- **upcall stub**: 독립 대조 3건 정합 —
  - reply handler `ofVoid(JAVA_INT, ADDRESS, JAVA_LONG, ADDRESS)` = Core `zlink_reply_handler_fn(zlink_request_result_t, zlink_msg_t*, size_t, void*)`.
  - mesh ready handler `of(I, A, I, A)` = Core `zlink_mesh_ready_handler_fn` (반환 mask=int, args void*/mask/void*).
- **ServiceLayouts / NativeLayouts**: 21개 struct layout 전부 Core struct와 필드순·타입클래스·배열크기·명시 padding(자연정렬)·총 크기 일치. 대표: MESH_NODE_STATUS(seq(256)=NAME_MAX+1, seq(512)=ENDPOINT_MAX+1, pad(4)×2, 1128B), RECEIVE_RECORD(1192B), ACTOR_REF(520B). **discrepancy 0.**
- **제거·부재 심볼 게이트**: `removed-identifiers-10.0.0.json` 194개(FUNC/TYPE/ENUM_TYPE/ENUMERATOR/MACRO) 전량 java scope 대조 → **hit 0(EMPTY)**. native/src에 `zlink_router_recv_part`·`zlink_router_enable_spot_receive` 잔존 0.
- Verdict: **CLEAN** (blocker/high/medium 0).

### I2 — POSD·DDD
- god-file 없음(최대 `Native.java` 1538줄 = 기계적 FFI 선언 집합, 정당). contracts/runtime/service 계층 분리 유지, InternalAccess/ContractAccess 경계 일관.
- Verdict: **CLEAN**.

### I3 — 정리(dead code·no-hit)
- `Native.java` 85개 MH 핸들 전부 1회+ 호출(선언-only 0). TODO/FIXME/XXX/HACK 0. native/src 파일은 브리지 1개뿐(전량 라이브).
- coordinator manifest: compileJava+buildZlinkJavaBridge+samples+kotlin ALL GREEN, no-hit 0(재실행 안 함, 정적 근거 일치).
- Verdict: **CLEAN**.

## 4. low 목록
없음.

## 5. 제거·부재 심볼·arity 판정
- 제거 심볼 게이트: EMPTY (194/194 no-hit).
- arity: C bridge 2 호출부·85 downcall descriptor·모든 invokeExact 콜사이트·3 upcall·21 layout 전부 Core와 일치. `zlink_router_recv_part` 삼자(Core 6 / descriptor 6 / invokeExact 6) 정합 — 4회 누락됐던 7-arg 결함 소거 확인.

## 결론
iter-4 JV4-1 blocker·3 low 해소. 3축 전부 blocker/high/medium 0, low 0.

BINDINGS REVIEW CLEAN
