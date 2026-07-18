# RouteMesh 10.0.0 S8 NODE bindings 전환 리뷰 — iteration 2 공통 prompt

너는 S8 NODE bindings 전환 리뷰 iteration 2의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `7fead5f17` (iter-1 finding 수정 반영)
- Scope: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json` 중 `/build/`·`node_modules`·`prebuilds` 제외
- 파일 수: 140
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `8a47280f86dff6fcbba089f18c22719fd3ef80c0308f2da0b2993eb98ab1970c`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-1 finding 해소
`../iteration-1/finding-ledger.ko.md`(NF1 wire enum 값·NF2 RouterSocket.sendToSpot 제거·NF3 typed kind_data·NF4 ready-handler mask 반환+unregister·NF5 close-busy·NF6 count 타입·NF7 transfer API·NI2-1·NI3-1)를 commit `7fead5f17`에 소스 대조로 해소 판정. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1 계약 일치(wire enum 값이 Core `zlink_mesh_record_kind_t`/`operation_kind_t`/`node_state_t`와 정확히 일치하는지 재확인, pull dispatch 수명, transfer API, kind_data, raw-layer 드리프트 잔존), I2 POSD·DDD, I3 정리(폐기 no-hit·dead code).

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(addon node-gyp green·tsc src+samples green·no-hit 0)만. iteration 2: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-1 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 폐기 no-hit 판정 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
