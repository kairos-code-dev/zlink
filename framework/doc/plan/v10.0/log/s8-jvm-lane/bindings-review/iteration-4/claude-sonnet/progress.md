# S8 JVM bindings 리뷰 iteration 4 — R2(Claude Sonnet) progress

## 절차
1. prompt.md 정독(byte-identical, iteration 4 규칙: 각 축 CLEAN = blocker/high/medium 0, low는 별도 기록·비차단).
2. Scope 재현: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` → native/(linux|darwin|win)·resources/native·/build/ 제외 → 251 files.
3. aggregate SHA-256 재계산(`LC_ALL=C sort` 파일목록 → 파일별 sha256sum → 그 출력 자체를 sha256sum) → `fa9c1de94a291e3dad452c648896eee9620ad7815050ea6a91dd096ab07837ff` — prompt 기대값과 일치.
4. HEAD(`41660e37b`)와 대상 commit(`7403cb5c9`) 사이 scope 파일 diff = empty(freeze 커밋만 추가) 확인.
5. iter-3 finding 해소 검증:
   - JV3-1(R1): `git show 7403cb5c9 -- NativeLayouts.java` 확인 → `SERVICE_EVENT_LAYOUT` + 22 offset 상수 45줄 삭제. scope 전체 `SERVICE_EVENT` grep 0건.
   - R2 low(optionalDowncall): `Native.java`/`NativeSymbols.java`에서 `optionalDowncall` 헬퍼 각각 삭제. scope 전체 grep 0건.
   - 삭제 경계 육안 확인(NativeLayouts.java:143-176, Native.java:18-40, NativeSymbols.java:1-60) — 구문 손상·dangling 참조 없음, import 전부 여전히 사용됨.
6. I1 재검증:
   - removed-identifiers-10.0.0.json(FUNC 76/TYPE 28/REUSED 1/ENUM_TYPE 15/ENUMERATOR 65/MACRO 10/FIELD 109=304개) vs scope `"zlink_*"` 리터럴 178개 전수 매칭(Python) → hit 0.
   - `nm -D core/build/lib/libzlink.so.10.0.0`(196 exported zlink_* 심볼) vs 178개 리터럴 → 미발견 3건(`zlink_java_msg_data_addr`/`zlink_java_send_u32`=java bridge 자체 export, `zlink_msgv_close`=downcallAny legacy fallback명, primary `zlink_multipart_close`가 nm에 존재) — 전부 iter-3와 동일 사유로 정당.
   - MethodHandle descriptor(FunctionDescriptor.of/ofVoid) 파서 스크립트 재작성(iter-3와 독립적으로) → 183개 선언 vs invokeExact 184개 distinct target/186개 call site → **mismatch 0**(MH_FREE는 `NativeSymbols.freeDowncall()` 간접 헬퍼라 정적 파서가 직접 못 찾았으나 육안 대조로 1/1 일치 확인).
   - 콜백 upcall 1건(`zlink_mesh_ready_handler_fn`) 육안 재대조: Core `service/dispatch.h:138-141` 반환 `zlink_mesh_ready_domain_mask_t`(uint32)+`(void*, mask, void*)` vs `READY_HANDLER_DESCRIPTOR = of(I,A,I,A)` — 일치.
   - **신규 발견**: `bindings/java/native/src/zlink_java_reqrep_bridge.c:43-99`의 `zlink_java_router_recv`가 Core `zlink_router_recv_part`(6 파라미터, `core/include/zlink/socket/api.h:271-277`)를 **7개 인자**로 호출(`:84-85`, `source_spot_rid_out` 포함) — 컴파일 불가 수준의 arity 불일치. `git log -p api.h`로 이 함수가 과거 7파라미터였다가("raw ROUTER는 10.0.0에서 service envelope 없음" 리팩토링으로) 6파라미터로 축소된 이력 확인 — 이 bridge 파일만 미추종.
   - 해당 함수는 Java 쪽 FFM downcall 어디서도 참조되지 않음(178개 리터럴 중 `zlink_java_router_recv` 0건, JNI 스타일 native 메서드도 0건) → 죽은 동시에 깨진 코드.
7. I2 재검증: iter-3 대상(39b1edee8)→iter-4 대상(7403cb5c9) scope diff가 딱 3파일 삭제뿐(구조 변경 없음) 확인. 최대 파일 `Native.java` 1542→1538줄(순삭제). 구조적 신규 결함 없음.
8. I3 재검증: 배경 에이전트(Explore, read-only)로 scope 전체 미사용 private/package-static 심볼 스윕 → `SampleSupport.java`의 `SPOT_TOPIC`/`SPOT_PAYLOAD`, `NativeSocketRuntime.java`의 `ERRNO_EFSM` 3건 무참조 확인(직접 grep으로 재검증, 각각 선언줄 외 0건). `zlink_java_router_recv`의 정적 헬퍼 2개(`zlink_java_recv_result_from_errno`/`zlink_java_close_router_recv_parts`)는 해당 함수 내부에서만 쓰임 — 함수 자체가 죽으면 동반 사문화.
9. Symbol gate 최종: removed-identifiers 0 hit, nm 미발견 3건 전부 정당 사유 — PASS(§6 참고).

## 결론
I1에 BLOCKER 1건(`zlink_java_router_recv` Core arity 불일치, native/src 컴파일 파괴), I3에 동일 항목 dead-code 교차 기록 + low 3건. iteration 4 규칙(blocker/high/medium 0일 때만 CLEAN)에 따라 I1·I3 축 NOT CLEAN → 전체 NOT CLEAN.
