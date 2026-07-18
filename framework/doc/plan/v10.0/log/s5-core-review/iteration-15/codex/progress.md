# S5 Core review iteration 15 — R1 progress

- 갱신: 2026-07-18T09:07:45+09:00
- 현재 축: 완료
- 현재 파일/명령: `service_control_runtime.cpp/.hpp`, `context_api.cpp`, `ctx_auto_hwm_recalc.cpp`, `ctx.cpp`
- 확인: detached `7b580a520`, scope 631개, aggregate `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5`, checkout 변경 없음
- 확인: cached node의 extract/rekey/move와 add transaction은 성립; loop vector 제거 및 active ID 설정 순서도 기존 remove wait 계약을 유지
- 확정 finding: 수정 commit `7b580a520`에서 S5-14-01을 새 반례로 재개방. auto-HWM callback bad_alloc 시 `run()` catch가 active ID cleanup/broadcast 없이 worker를 종료하고, destructor의 `stop_auto_hwm_recalc_task()`→`remove_task()`가 무기한 대기함. 동일 scheduler allocation/liveness family의 high 1건
- 확인: iteration 10 8건 유지, spec ko/en 25/25, source literal 174/174, package 10.0.0/SOVERSION 10, known risks current source 재대조
- 종료 확인: scope 631개, aggregate `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5`, checkout 변경 없음
- 결과: I1 NOT CLEAN(high 1), I2 NOT CLEAN(high 1, 동일 family), I3 CLEAN, low 0 — `review.ko.md` 작성 완료
- 남은 범위: 없음
- 실행 제한: build/test/sanitizer/package 생성 미실행; manifest §2의 coordinator 실행 증거만 사용
