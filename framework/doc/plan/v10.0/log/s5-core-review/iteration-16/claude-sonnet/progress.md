# R2 Claude Sonnet — iteration 16 진행 기록

## 시작
- 2026-07-18 시작. 검토 checkout: `/tmp/claude-1000/zlink-s5-it10-sonnet` (detached `1f247af7a`), git status clean 확인.
- Scope 파일 수 실측: 632 (git ls-files, `git ls-tree -r HEAD`와 일치). manifest 기재값 631과 불일치 —
  원인: 리뷰 대상 commit이 신규 `core/tests/unittest/unittest_service_control_runtime.cpp`를
  scope 내에 추가했으나 manifest 통계가 갱신 전 값(631)을 그대로 사용한 것으로 보임.
  Aggregate SHA-256 실측: `952284938c531e471e34df46709388623778892da4ccc10367af8ba997d62911`
  (manifest 기재값 `398ee290...`와 불일치, 같은 원인). checkout 자체는 clean·정확한 detached HEAD로
  변조 근거 없음 — review.ko.md에 process note로 기록 예정.

## 진행 축
- [완료] 우선 검증: S5-15-01 해소 확인 — call.fn try/catch(bad_alloc) 봉인 + epilogue 무조건 실행
  + run() 최후 catch가 terminal lifecycle commit. 신규 unittest_service_control_runtime 등록·시나리오
  적절함(3 tick 생존·remove_task 즉시 반환·ctx_term 정상 종료).
- [완료] S5-15-02 해소 확인 — schedule_task_locked의 zlink_assert(!cached_node.empty()) 불변식이
  모든 호출 경로(add_periodic_task 최초 삽입 제외, wakeup_task/loop 경유)에서 성립함을 코드 추적으로 확인.
- [완료] iteration-10 8건 회귀 점검: S5-10-01(request_timeout_scheduler schedule() 단일 critical section) ·
  S5-10-02(allocate_lifecycle_generation) · S5-10-03(pending_operation_t::timeout_task 소유) ·
  S5-10-04(monitor_state_pin_t, reader 5개 진입점) · S5-10-05(wire_submit_join_reply flags_ 관통) ·
  S5-10-06(acceptor_error_to_errno) · S5-10-07(unittest µs 단위) · S5-10-08(internals 이관, 제외 대상)
  — 전부 소스상 유지 확인, 회귀 없음.
- [완료] package metadata 대조: CMakeLists VERSION 10.0.0/SOVERSION 10, debian control/changelog
  libzlink10, redhat spec Version 10.0.0/lib_name libzlink10, nuget nuspec 10.0.0 — 전부 정합.
- [완료] known risk 4건: 이번 커밋이 auto-HWM/ypipe/raw socket teardown/ctx_term 코드를 건드리지 않음을
  git show --stat로 확인 후 각 파일 재대조 — 기존 판정과 동일(추적 유지, ctx_term은 finding 아님).
- [완료] I1/I2/I3 전체 scope 적대적 재검토 — I1/I2/I3 모두 CLEAN(finding 0), low finding 0.
- [완료] 종료 scope 재확인: 632 파일 / 952284938c531e471e34df46709388623778892da4ccc10367af8ba997d62911
  (시작값과 동일), checkout git status clean, HEAD 불변(1f247af7a) 확인.
- [완료] review.ko.md 작성 완료 — 최종 판정: CORE REVIEW CLEAN. 정상 종료.

## Scope 검증 이상 (기록용)
- 실측 632 파일 / aggregate SHA-256 952284938c531e471e34df46709388623778892da4ccc10367af8ba997d62911
  (git ls-files와 git ls-tree -r HEAD 일치). manifest 기재값(631 / 398ee290...)과 불일치.
  원인: 이번 commit이 scope 내 신규 파일 core/tests/unittest/unittest_service_control_runtime.cpp를
  추가했으나 manifest 통계가 갱신 전 값을 유지한 것으로 판단됨. checkout은 clean·정확한 detached
  HEAD(1f247af7a)로 변조 근거 없음 — process note로 review.ko.md에 기록, 코드 finding 아님.
