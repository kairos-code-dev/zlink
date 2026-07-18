# S8 JVM bindings 전환 리뷰 iteration 2 — R2 (Claude Sonnet) progress

## Scope 확인
- 명령: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle | grep -Ev '(^|/)native/(linux|darwin|win)|(^|/)resources/native/|/build/' `
- 시작: 251 files. `sha256sum <each file> | sha256sum`(재정렬 없이, `git ls-files`가 이미 정렬 출력) → `ec842c6a8947840a2edb35983f69bde59cd43198f097fb26da730d57d52fed61`(manifest·prompt 일치).
- HEAD(`e86213d3a`)와 대상 commit `50faf28fd` 사이 scope 내 diff 없음(`git diff 50faf28fd HEAD --stat -- <scope paths>` → empty) 확인 — 현재 워킹트리 = 대상 commit.
- 종료 시 동일 명령 재실행 → 동일 251 files / 동일 hash `ec842c6a...`. 리뷰 중 scope 파일 미수정(수정 없음, read-only).

## 방법
1. iter-1 finding-ledger(`../iteration-1/finding-ledger.ko.md`)의 JV-F1~JV-F5를 각각 소스 대조로 재검증.
   - JV-F2/F3/F4/F5: 제거 심볼·타입 문자열을 scope 전체에서 grep(`zlink_stream_bind_actor`, `_unbind_actor`, `_bound_actors`, `_send_bound_actor_part`, `zlink_get/set_spot_option`, `zlink_stream_detach`, `zlink_stream_attach`, `zlink_subscribe_handler`, `subscribe_handler_fn`, dead `NativeLayouts` 타입, `router_handler` 중복) → 전부 0 hit.
   - JV-F1: `Native.java`의 `MH_ROUTER_RECV_PART`/`MH_ROUTER_RECV_PART_CRITICAL` `FunctionDescriptor`를 직접 읽고, 정규식 파서로 파라미터 개수를 프로그램적으로 세었으며, `git diff 5c2eb2acc 50faf28fd -- Native.java`로 실제 편집분을 확인.
2. 전체 scope의 모든 `"zlink_..."` 문자열 리터럴(179개, `grep -rhoE '"zlink_[a-zA-Z0-9_]*"'`)을 `core/tests/contract/removed-identifiers-10.0.0.json`(FUNC 76·TYPE 28·REUSED 1·ENUM_TYPE 15·ENUMERATOR 65·MACRO 10·FIELD 109 = 303개 합집합)와 python으로 전수 대조.
3. **전체 downcall arity 자동 검증**: scope 내 모든 `MethodHandle` 선언(`downcall`/`downcallCritical`/`optionalDowncall` + `FunctionDescriptor.of/ofVoid`)을 정규식으로 파싱해 선언 파라미터 수를 추출하고, 같은 핸들에 대한 모든 `invokeExact(...)` 호출부의 실제 인자 개수를 괄호 깊이 추적 파서로 추출해 1:1 대조(98개 핸들, 110개 호출부 스캔).
4. `ServiceLayouts.java`(15개 struct)의 필드 순서·타입을 `core/include/zlink/service/{common,dispatch}.h`와 대조(`ROUTING_ID`/`ACTOR_REF` 공용 layout 포함, `RECEIVE_RECORD` 전체 필드 순서 1:1 검증 — `operation_kind` 뒤 4바이트 padding까지 일치).
5. `NativeReceiveBatch.java` 등 pull-dispatch 배치 API의 `Arena` 스코프 확인(try-with-resources, retain 후 arena close).
6. build/test/run 미실행(정적 대조만). 실행 증거는 manifest의 coordinator 증거(compileJava+samples+kotlin-samples green, no-hit 8종 0, 제거심볼 게이트 EMPTY)를 그대로 인정하되, 이번 리뷰에서 발견한 결함은 **컴파일 시점에 검출 불가능**(MethodHandle `invokeExact`의 타입 일치는 런타임 링크 시점 검사)함을 확인.

## 결과 요약
- I1: **NOT CLEAN** — JV-F1이 실제로는 해소되지 않고 다른 형태로 재발함(신규 반례, 아래 상세).
  - `zlink_router_recv_part`용 `FunctionDescriptor`가 5-param(ADDRESS×4 + INT×1)으로 선언됐으나, 동일 `MethodHandle`의 모든 `invokeExact` 호출부(`Native.routerRecvPart`, `Native.routerRecvPartNoWaitCritical`)는 6-arg(ADDRESS×5 + INT×1)로 호출 — MethodHandle 타입과 호출부 타입 불일치로 **모든 호출에서 런타임 `WrongMethodTypeException`** 발생(라우터 recv 완전 불능, `RouterSocket.recv()` 공개 경로 직결).
  - Core 진짜 계약(`socket/api.h:271-276`)은 6-param(ADDRESS×5+INT×1)이며, 호출부(`Native.java:1350-1352`, `1367-1369`, 및 `NativeRouterReceiveSupport.java`가 넘기는 인자)는 올바르게 6-arg로 수정되어 있음. `FunctionDescriptor` 쪽만 7→5로 과도 삭제(diff에서 `ADDRESS, ADDRESS, JAVA_INT` 두 토큰을 지워야 할 것을 `ADDRESS, ADDRESS, JAVA_INT` 전체 라인이 `JAVA_INT` 한 토큰으로 치환되며 ADDRESS 2개가 삭제됨 — 1개만 지웠어야 함).
  - iter-1 manifest의 "router_recv_part 6param" 확인 및 `:compileJava` green은 이 결함을 검출하지 못함(FFM `invokeExact`의 정확한 타입 일치는 컴파일이 아닌 런타임 링크 검사이므로 javac는 통과함).
  - 전체 downcall(98개 핸들) 자동 arity 대조에서 이 2건(정상/critical variant) 외 추가 불일치 없음.
- I1 나머지: `ServiceLayouts.java` 15개 struct 전량 Core 헤더와 필드 순서·타입 일치(변경 없음, iter-1에서 이미 clean 판정 + 이번에 재대조로 재확인). pull-dispatch batch(`NativeReceiveBatch`/`NativeReadyBatch`) Arena 스코프 이상 없음.
- I2: CLEAN — JV-F5(`router_handler` 중복) 해소 확인, 신규 POSD/DDD 결함 없음.
- I3: CLEAN — JV-F2/F3/F4 전량 해소(제거 심볼·dead layout·raw STREAM attach/detach·subscribe_handler 0 hit). 제거심볼 게이트: scope 내 179개 `zlink_*` 문자열 리터럴 전량 `removed-identifiers-10.0.0.json`과 대조해 0 hit(EMPTY) 확인.

## 최종 판정
BINDINGS REVIEW NOT CLEAN
