# RouteMesh 10.0.0 S8 NODE bindings 전환 리뷰 — iteration 4 공통 prompt

너는 S8 NODE bindings 전환 리뷰 iteration 4의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `006d34f97` (iter-3 finding 수정 반영)
- Scope: `git ls-files bindings/node/src bindings/node/native/src bindings/node/samples bindings/node/binding.gyp bindings/node/package.json` 중 `/build/`·`node_modules`·`prebuilds` 제외
- 파일 수: 140
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `967205f1af41bf948f105b576167f21536d3a59094cb06ed675c9f4674fa3963`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-3 finding 해소
- R2(NF3-1): `option_mapping.ts`의 phantom `DISCOVERY_SPOT_OWNER_SYNC=0x3035`/`DISCOVERY_ACTOR_ROUTE_SYNC=0x3036` 제거 + Core `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES=0x3034`(`AUTO_HWM_MSG_UNIT_BYTES`) 추가 → SocketOption 테이블이 Core `zlink_option_t`와 정확 일치하는지.
- R1: `monitor.ts` `MonitorSourceKind` JSDoc이 제거된 spot pub/sub source를 더 이상 서술하지 않는지.
소스 대조로 해소 판정. 해소된 finding은 새 반례 없이 재개 금지. iter-1·iter-2 finding은 이전에 해소·확인됨.

## 전체 scope 재검토(3축)
I1 계약 일치(enum·옵션id 테이블 Core 정확 일치·pull dispatch·native decl==등록), I2 POSD·DDD, I3 정리(폐기 no-hit·dead code).

## 절차 (4회차 규칙)
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(addon node-gyp green·tsc src+samples green·no-hit 0)만. **iteration 4이므로 각 축의 CLEAN은 blocker·high·medium finding 0을 뜻한다. low finding은 별도로 기록하되 CLEAN을 막지 않는다.**

## 출력
1. Scope 확인 2. iter-3 해소 판정 3. I1/I2/I3 Finding(심각도)·Evidence·Verdict 4. low finding 목록(있으면) 5. 폐기 no-hit 판정 6. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN`(세 축 blocker/high/medium 0) 또는 `BINDINGS REVIEW NOT CLEAN`.
