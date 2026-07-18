# RouteMesh 10.0.0 S8 JVM bindings 전환 리뷰 — iteration 2 공통 prompt

너는 S8 JVM bindings 전환 리뷰 iteration 2의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `50faf28fd` (iter-1 raw-layer 수정 반영)
- Scope: `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle` 중 `native/linux|darwin|win`·`resources/native`·`/build/` 제외
- 파일 수: 251 (iter-1의 255에서 dead 파일 4 삭제)
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `ec842c6a8947840a2edb35983f69bde59cd43198f097fb26da730d57d52fed61`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-1 finding 해소
`../iteration-1/finding-ledger.ko.md`(JV-F1 router_recv_part 6param·JV-F2 제거 FUNC downcall·JV-F3 stream detach/attach+subscribe_handler·JV-F4 dead NativeLayouts·JV-F5 dup router_handler)를 commit `50faf28fd`에 소스 대조로 해소 판정. 특히 모든 FFI downcall 심볼을 `core/tests/contract/removed-identifiers-10.0.0.json`(304)과 대조해 제거 심볼 0 확인. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1(FFI downcall 시그니처·arity·타입이 Core 정합, ServiceLayouts 레이아웃, pull dispatch 수명, raw-layer 잔존), I2 POSD·DDD, I3 정리(제거 심볼·dead code·no-hit).

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(compileJava+samples+kotlin green·no-hit 0·제거심볼 게이트 EMPTY)만. iteration 2: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-1 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 제거심볼/폐기 no-hit 판정 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
