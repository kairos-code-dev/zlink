# RouteMesh 10.0.0 S8 CPP bindings 전환 리뷰 — iteration 4 공통 prompt

너는 S8 CPP bindings 전환 리뷰 iteration 4의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `50faf28fd` (iter-3 dead-code 수정 반영)
- Scope: `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples bindings/cpp/CMakeLists.txt` 중 `native/` 제외
- 파일 수: 120 (iter-3의 121에서 native_send_result.hpp 삭제)
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `e2190823b037b438a53de285a16be3ce40e92499a7ea06dcc6a73b99291b3bdb`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-3 finding 해소
`../iteration-3/finding-ledger.ko.md`(C3-1 router_spot 사멸 제거·C3-2 spot_spot orphan·C3-3 orphan 연쇄)를 commit `50faf28fd`에 소스 대조로 해소 판정. iter-1·iter-2 finding은 이전 iteration에서 해소·확인됨. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1 계약 일치, I2 POSD·DDD, I3 정리(폐기 no-hit·dead code·전이적 제거로 새로 생긴 orphan 없는지).

## 절차 (4회차 규칙)
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(라이브러리+15 samples green, no-hit ZERO)만. **iteration 4이므로 각 축의 CLEAN은 blocker·high·medium finding 0을 뜻한다. low finding은 별도로 기록하되 CLEAN을 막지 않는다.**

## 출력
1. Scope 확인 2. iter-3 해소 판정 3. I1/I2/I3 Finding(심각도 명시)·Evidence·Verdict 4. low finding 목록(있으면) 5. 폐기 no-hit 판정 6. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN`(세 축 blocker/high/medium 0) 또는 `BINDINGS REVIEW NOT CLEAN`.
