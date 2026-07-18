# RouteMesh 10.0.0 S8 JVM bindings 전환 리뷰 — iteration 4 공통 prompt

너는 S8 JVM bindings 전환 리뷰 iteration 4의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `7403cb5c9` (iter-3 dead-code 수정 반영)
- Scope: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/linux|darwin|win`·`resources/native`·`/build/` 제외
- 파일 수: 251
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `fa9c1de94a291e3dad452c648896eee9620ad7815050ea6a91dd096ab07837ff`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-3 finding 해소
`../iteration-3/codex/review.ko.md`의 JV3-1(`NativeLayouts.java`의 dead `SERVICE_EVENT_LAYOUT`+22 offset 제거)과 R2의 low(`optionalDowncall` 미사용 helper 제거)가 commit `7403cb5c9`에 해소됐는지. iter-1·iter-2 finding은 이전에 해소·확인됨. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1(FFI downcall descriptor arity vs invokeExact call-site vs Core 시그니처, ServiceLayouts, 콜백 upcall, 제거·부재 심볼 게이트), I2 POSD·DDD, I3 정리(dead code·no-hit).

## 절차 (4회차 규칙)
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(compileJava+samples+kotlin green·no-hit 0·제거심볼 게이트 EMPTY)만. **iteration 4이므로 각 축의 CLEAN은 blocker·high·medium finding 0을 뜻한다. low finding은 별도로 기록하되 CLEAN을 막지 않는다.**

## 출력
1. Scope 확인 2. iter-3 해소 판정 3. I1/I2/I3 Finding(심각도)·Evidence·Verdict 4. low finding 목록(있으면) 5. 제거·부재 심볼 판정 6. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN`(세 축 blocker/high/medium 0) 또는 `BINDINGS REVIEW NOT CLEAN`.
