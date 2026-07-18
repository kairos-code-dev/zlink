# S8 JVM bindings 전환 리뷰 — iteration 4 — R2 (Claude Sonnet)

독립 리뷰. R1(codex)·coordinator 해석 미참조. 정적 소스 대조만(빌드·실행·수정 없음). 4회차 규칙 적용: 각 축은 blocker/high/medium finding 0일 때만 CLEAN, low는 별도 기록하되 CLEAN을 막지 않는다.

## 1. Scope 확인
- 대상 commit: `7403cb5c9`.
- `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/(linux|darwin|win)`·`resources/native`·`/build/` 제외 → **251 files**(기대값 일치).
- aggregate SHA-256(`LC_ALL=C sort` 파일목록 → 파일별 sha256sum → 그 출력을 다시 sha256sum) → `fa9c1de94a291e3dad452c648896eee9620ad7815050ea6a91dd096ab07837ff` — **prompt 기대값과 일치**.
- 워킹트리 HEAD(`41660e37b`)와 대상 commit(`7403cb5c9`) 사이 scope 파일 diff → empty(HEAD는 iteration-4 log 문서만 추가한 freeze 커밋). 리뷰 시작·종료 시 251 / 동일 hash 재확인, 파일 미수정.

## 2. iter-3 finding 해소 판정

### JV3-1 (R1, `SERVICE_EVENT_LAYOUT` + 22 offset 상수) — **RESOLVED**
- `git show 7403cb5c9 -- NativeLayouts.java`: `SERVICE_EVENT_LAYOUT` MemoryLayout 및 `SERVICE_EVENT_*_OFFSET` 11개(정정: 실제 오프셋 상수는 10개 + layout 1개 = iter-3 보고서상 "23 심볼") 총 45줄 삭제.
- scope 전체 `SERVICE_EVENT` grep → **0건**.
- 삭제 경계(`NativeLayouts.java:143-176`) 육안 확인 — `MONITOR_EVENT_LAYOUT` 블록과 `ACTOR_REF_LAYOUT` 블록 사이 공백 1줄만 남고 구문·참조 손상 없음.

### R2 low (`optionalDowncall` 미사용 헬퍼) — **RESOLVED**
- `Native.java:34-36`(래퍼)·`NativeSymbols.java:42-47`(구현) 둘 다 삭제 확인.
- scope 전체 `optionalDowncall` grep → **0건**. import 정리 불필요(모든 import 여전히 사용 중, `FunctionDescriptor`/`MethodHandle` 등).

두 finding 모두 소스 대조로 해소 확인, 신규 반례 없음.

## 3. 전체 scope 재검토(3축)

### I1 — FFI downcall descriptor arity vs invokeExact vs Core 시그니처, ServiceLayouts, 콜백 upcall, 제거·부재 심볼 — **NOT CLEAN (JV4-1)**

**정합성 확인(mismatch 0)**
- 독립 파서(괄호깊이 기반, iter-3 스크립트와 별도 재작성)로 scope 전체 `MethodHandle X = ...FunctionDescriptor.of[Void](...)` 선언 183개 추출 → `invokeExact` 호출 184개 distinct target/186개 call site와 인자 수 대조 → **불일치 0건**(`MH_FREE`는 `NativeSymbols.freeDowncall()` 간접 헬퍼라 정적 패턴 매칭에서 누락됐으나 육안 확인 시 `ofVoid(ADDRESS)`=1파라미터 vs `MH_FREE.invokeExact(parts)`=1인자로 일치).
- `zlink_mesh_ready_handler_fn` 콜백 육안 재대조: Core `service/dispatch.h:138-141`(반환 `zlink_mesh_ready_domain_mask_t`, 파라미터 `void*, mask, void*`) vs `NativeServiceSymbols.READY_HANDLER_DESCRIPTOR = of(I,A,I,A)` — 일치.

**FINDING JV4-1 — native/src bridge의 Core 시그니처 arity 불일치(컴파일 파괴) [BLOCKER]**
- **위치**: `bindings/java/native/src/zlink_java_reqrep_bridge.c:84-85` (함수 `zlink_java_router_recv`, `:43-99`).
- **내용**: Core `zlink_router_recv_part`(`core/include/zlink/socket/api.h:271-277`)는 **6개 파라미터**(`void*, const routing_id**, uint64_t*, msg_t*, part_flag*, recv_flags`)인데, 이 bridge 함수는 **7개 인자**(`router, source_node_rid_out, source_spot_rid_out, request_seq_out, &part, &has_more, recv_flags`)로 호출한다. C++에서 6-파라미터 함수를 7개 인자로 호출하는 것은 오버로드·매크로가 없는 한 하드 컴파일 오류다. `core/include/zlink/socket/api.h`에는 `zlink_router_recv_part` 선언이 이 1개뿐이며(`grep` 확인), 구현(`core/src/api/socket/socket_request_reply_router_api.cpp:76`)도 정확히 6파라미터로 일치 — 오버로드 여지 없음.
- **근거**: `git log -p -S"source_spot_rid_out_" core/include/zlink/socket/api.h`로 확인한 결과, 이 함수는 과거 7파라미터(`source_node_rid_out_`+`source_spot_rid_out_` 둘 다 보유)였다가 "raw ROUTER는 10.0.0에서 service envelope를 갖지 않는다"는 리팩토링(`c462a13ab`/`c258bbe81` 등)으로 6파라미터로 축소됐다. 이 리팩토링 시 Java FFM 층(`Native.java`의 `MH_ROUTER_RECV_PART`)은 iter-2(JV2-1)에서 정확히 6파라미터로 수정됐으나, **이 native/src C++ bridge 파일의 내부 호출부는 갱신되지 않고 옛 7-인자 형태로 잔존**했다.
- **동반 확인(I3 교차)**: `zlink_java_router_recv`는 Java 측에서 완전히 호출되지 않는 죽은 함수이기도 하다 — scope의 178개 `"zlink_*"` 리터럴 중 `zlink_java_router_recv` 0건, JNI 스타일 `native` 메서드 참조도 0건(`LibraryLoader.java`는 순수 FFM `SymbolLookup` 기반, JNI 없음). 즉 이 함수는 **죽은 동시에 깨진** 코드다. 내부 static 헬퍼 2개(`zlink_java_recv_result_from_errno:16`, `zlink_java_close_router_recv_parts:35`)도 이 함수에서만 쓰여 함께 사문화된다.
- **영향**: `bindings/java/build.gradle:47-91`의 `buildZlinkJavaBridge` Exec 태스크가 `native/src/zlink_java_reqrep_bridge.c` 전체를 `c++ -std=c++17`로 컴파일한다(`processResources`가 이 태스크에 `dependsOn`). 죽은 함수라도 번역 단위 전체가 컴파일되어야 하므로, Gradle 캐시가 무효화되는 clean 빌드·CI에서는 이 한 함수 때문에 **bridge .so 빌드 자체가 실패**한다(태스크 `inputs`에 `core/include`가 포함돼 있어 Core 헤더가 바뀔 때마다 재컴파일 대상이 됨). manifest의 "compileJava+samples+kotlin ALL GREEN"은 아마 캐시된 `.so` 산출물 재사용(Gradle UP-TO-DATE) 또는 별도 clean 미수반 실행이었을 가능성이 높다 — 이 리뷰는 그 원인을 판정하지 않고 정적 소스 사실만 보고한다.
- **권고**: `zlink_java_router_recv` 함수(및 동반 static 헬퍼 2개) 전체 삭제(Java 소비자 0이므로 안전) 또는, 실제로 필요하다면 6-파라미터 Core 시그니처에 맞춰 `source_spot_rid_out` 인자 제거 후 재작성.

**제거·부재 심볼 게이트**
- `removed-identifiers-10.0.0.json`(FUNC 76/TYPE 28/REUSED_IDENTIFIER 1/ENUM_TYPE 15/ENUMERATOR 65/MACRO 10/FIELD 109 = 304개 식별자) vs scope `"zlink_*"` 리터럴 178개(변동 없음, iter-3와 동일 카운트 — 삭제된 dead layout은 심볼 리터럴을 갖지 않았음) 전수 매칭 → **hit 0**.
- `nm -D core/build/lib/libzlink.so.10.0.0`(196개 export) vs 178개 리터럴 → 미발견 3건, 전부 iter-3와 동일 사유로 정당: `zlink_java_msg_data_addr`/`zlink_java_send_u32`(java bridge 자체 export, Core lib 심볼 아님), `zlink_msgv_close`(`downcallAny(["zlink_multipart_close","zlink_msgv_close"])`의 미선택 legacy fallback, primary가 nm에 존재).
- **판정: PASS**(제거·부재 심볼 자체는 0건).

- **Verdict: NOT CLEAN**(blocker 1건 = JV4-1).

### I2 — POSD/DDD — **CLEAN**
- iter-3 대상(`39b1edee8`) → iter-4 대상(`7403cb5c9`) scope diff는 3파일(`Native.java`/`NativeLayouts.java`/`NativeSymbols.java`)의 순수 삭제 55줄뿐 — 신규 구조 도입 없음.
- 최대 파일 `Native.java` 1542→1538줄(순삭제, 재검토 대상 아님).
- `NativeRouterReceiveSupport`가 `SocketCore`의 공용 콜백 지원을 재사용하지 않는 구조적 중복은 iter-1~3에서 이미 확인된 pre-existing 영역이며 이번 diff와 무관 — 재론하지 않음.
- **Verdict: CLEAN**(blocker/high/medium 0, low 0).

### I3 — 정리(dead code·no-hit) — **NOT CLEAN (JV4-1 교차 + low 3건)**
- §I1의 JV4-1(`zlink_java_router_recv` + 동반 static 헬퍼 2개)은 죽은 코드이기도 하므로 이 축에도 교차 기록한다. severity는 I1과 동일하게 상위 축(blocker)을 따른다 — 이 축의 CLEAN 여부에도 반영.
- 배경 스윕(Explore 에이전트, read-only 정적 grep) + 직접 재검증으로 scope 전체 private/package-static 심볼 무참조 사례 3건 확인:
  - `bindings/java/samples/Zlink.Samples/.../SampleSupport.java:52` `SPOT_TOPIC = "room:lobby"` — 선언 외 0건. (`SpotPubSubExample.java:25`가 참조 대신 동일 문자열을 인라인 중복.)
  - `SampleSupport.java:53` `SPOT_PAYLOAD = "hello-spot"` — 선언 외 0건.
  - `bindings/java/src/main/.../sockets/NativeSocketRuntime.java:40` `ERRNO_EFSM = 156384763` — 선언 외 0건(형제 상수 `DEFAULT_IO_BUFFER_SIZE`/`TOPIC_CAPACITY`는 사용 중과 대조적).
- 위 3건은 순수 미사용 상수(할당·컴파일 영향 없음) → **low**로 분류.
- 그 외 328개 private/package static 선언(모든 `MH_*`/layout-offset/callback-descriptor 상수 포함) 전수 대조 결과 추가 dead code 없음. `SERVICE_EVENT_*`/`optionalDowncall` 잔존 0(§2 재확인).
- **Verdict: NOT CLEAN**(JV4-1 blocker 교차 1건; low 3건 별도 기록·CLEAN 비차단 대상이었으나 blocker 존재로 축 자체가 NOT CLEAN).

## 4. low finding 목록
- L1 `SampleSupport.java:52` `SPOT_TOPIC` — 무참조 사문 상수.
- L2 `SampleSupport.java:53` `SPOT_PAYLOAD` — 무참조 사문 상수.
- L3 `NativeSocketRuntime.java:40` `ERRNO_EFSM` — 무참조 사문 상수.

## 5. 제거·부재 심볼 판정
- 제거 심볼 게이트: **EMPTY**(178개 `zlink_*` 리터럴 중 removed-identifiers-10.0.0.json hit 0).
- 부재 심볼: **0**(Core 소유 `zlink_*` 심볼 전량 nm resolve). 미발견 3건은 전부 java bridge 자체 export 또는 미선택 legacy fallback명으로 정상.
- 단, JV4-1은 "제거된 식별자 재참조"가 아니라 "**Core가 파라미터를 줄인 기존 함수를 옛 arity로 계속 호출**"하는 별개 결함 — 심볼 게이트 통과와 무관하게 별도 finding으로 보고.

## 6. 결론
iter-3 두 finding(JV3-1, R2 low) 소스 대조로 해소 확인, 신규 반례 없음. I2는 CLEAN(변경 없음). I1·I3에 신규 BLOCKER 1건(JV4-1: `zlink_java_reqrep_bridge.c`의 `zlink_java_router_recv`가 Core `zlink_router_recv_part`를 6파라미터 시그니처에 7개 인자로 호출 — 컴파일 파괴 + 완전 무참조 dead code) 발견. iteration 4 규칙(각 축 CLEAN = blocker/high/medium 0)에 따라 I1·I3 NOT CLEAN → 전체 NOT CLEAN.

BINDINGS REVIEW NOT CLEAN
