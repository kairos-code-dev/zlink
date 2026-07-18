# RouteMesh 10.0.0 S8 JVM bindings 전환 리뷰 — iteration 5 공통 prompt

너는 S8 JVM bindings 전환 리뷰 iteration 5의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `fbd35a1ef` (iter-4 finding 수정 반영)
- Scope: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/linux|darwin|win`·`resources/native`·`/build/` 제외
- 파일 수: 251
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `1351d5dabce124a8cefea369437cc23bd9f1eecf0ce59079056b6add2ad54192`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-4 finding 해소
- JV4-1(blocker): `bindings/java/native/src/zlink_java_reqrep_bridge.c`의 dead+broken `zlink_java_router_recv`(6-param `zlink_router_recv_part`를 7인자 호출) 및 전용 helper·Core-부재 `zlink_router_enable_spot_receive` 제거로 C bridge가 컴파일되는지(**native/src의 모든 `zlink_router_recv_part`·`zlink_*` 호출부 arity가 Core 시그니처와 일치하는지 native/src까지 대조**).
- 3 low(SPOT_TOPIC/SPOT_PAYLOAD/ERRNO_EFSM) 제거.
소스 대조로 해소 판정. iter-1~3 finding은 이전에 해소·확인됨. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1(FFI downcall descriptor arity vs invokeExact vs Core, **native/src C bridge의 Core 함수 호출부 arity/시그니처**, ServiceLayouts, 콜백 upcall, 제거·부재 심볼), I2 POSD·DDD, I3 정리(dead code·no-hit). native/src(C bridge)를 반드시 포함해 검토하라.

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(**compileJava+buildZlinkJavaBridge+samples+kotlin ALL GREEN**·no-hit 0·제거심볼 게이트 EMPTY)만. iteration 5(4회차+): 각 축 CLEAN=blocker/high/medium 0, low는 별도 기록·미차단.

## 출력
1. Scope 확인 2. iter-4 해소 판정 3. I1/I2/I3 Finding(심각도)·Evidence·Verdict 4. low 목록(있으면) 5. 제거·부재 심볼·arity 판정 6. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
