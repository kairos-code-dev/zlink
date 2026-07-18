# S5 Core review iteration 14 — R1 progress

- 갱신: 2026-07-18T08:52:31+09:00
- 현재 축: 완료
- 현재 파일/명령: `service_control_runtime.cpp`, `ctx_auto_hwm_recalc.cpp`, `ctx_options.cpp`, `context_api.cpp`
- 확인: detached `26a4cbb81`, scope 631개, aggregate `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969`, checkout 변경 없음
- 확인: S5-13-01a publication/ID commit 직렬화와 단방향 lock order 성립; S5-13-01b `_schedule` bad_alloc 시 `_tasks` rollback·ENOMEM 성립; S5-13-01c queued-event attach/첫 callback self-close 10회와 stray tick 관찰 구간 포함
- 확정 finding: 수정 commit `26a4cbb81`에서 S5-13-01을 새 반례로 재개방. add-time `_tasks.insert`, `wakeup_task` 재삽입은 public auto-HWM C 경로로 bad_alloc을 전파하고, worker의 periodic 재삽입은 예외가 thread entry를 벗어나 process terminate 가능. 동일 error-atomicity family의 high 1건으로 I1/I2에만 기록
- 확인: spec 한국어/영문 pair와 공개 header/error mapper 정적 대조, source literal 174/174, monitor 회귀 test 등록, Debian/RPM/NuGet 10.0.0 및 SOVERSION 10, known risks 4건 current source 재대조
- 종료 확인: scope 631개, aggregate `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969`, checkout 변경 없음
- 결과: I1 NOT CLEAN(high 1), I2 NOT CLEAN(high 1, 동일 family), I3 CLEAN, low 0 — `review.ko.md` 작성 완료
- 남은 범위: 없음
- 실행 제한: build/test/sanitizer/package 생성 미실행; manifest §2의 coordinator 실행 증거만 사용
