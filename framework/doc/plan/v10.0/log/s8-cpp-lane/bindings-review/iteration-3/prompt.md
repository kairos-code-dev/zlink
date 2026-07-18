# RouteMesh 10.0.0 S8 CPP bindings 전환 리뷰 — iteration 3 공통 prompt

너는 S8 CPP bindings 전환 리뷰 iteration 3의 독립 리뷰어다. byte 단위 동일 prompt(R1·R2). 다른 리뷰어 결과·coordinator 해석을 판정 근거로 쓰지 마라.

## Snapshot
- 대상 commit: `f9b6ba50c` (iter-2 finding 수정 반영)
- Scope: `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples bindings/cpp/CMakeLists.txt` 중 `native/` 제외
- 파일 수: 121 (iter-2의 123에서 zlink_errno.h·pimpl_move.hpp 삭제로 감소)
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum): `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be`
- 시작·종료 파일수·hash 확인·기록, 파일 수정 금지.

## 우선 검증: iter-2 finding 해소
`../iteration-2/finding-ledger.ko.md`(C2-0 CMakeLists 10.0.0·C2-1 zlink_errno.h 삭제·C2-2 dangling decl/dead helper·C2-3 spot_operation_state_t dead 필드·C2-4 소멸자 close-busy 신호)를 commit `f9b6ba50c`에 대조해 해소 판정. iter-1 finding(F1-F11/I2/I3)은 iter-2에서 이미 해소·확인됨. 해소된 finding은 새 반례 없이 재개 금지.

## 전체 scope 재검토(3축)
I1 계약 일치, I2 POSD·DDD, I3 정리 완결성(폐기 no-hit·dead code). 특히 iter-2 수정으로 새로 생긴 잔재(예: 제거된 helper의 orphan enum 값·미사용 inline)가 없는지.

## 절차
산출물 progress.md·review.ko.md만. build/실행 금지(정적 대조). 실행 증거는 manifest(라이브러리+15 samples green, no-hit ZERO)만. iteration 3: 각 축 CLEAN=finding 0.

## 출력
1. Scope 확인 2. iter-2 해소 판정 3. I1/I2/I3 Finding·Evidence·Verdict 4. 폐기 no-hit 판정 5. 마지막 줄 정확히 `BINDINGS REVIEW CLEAN` 또는 `BINDINGS REVIEW NOT CLEAN`.
