# RouteMesh 10.0.0 S5 Core 구현 리뷰 — iteration 14 공통 prompt

너는 S5 Core 구현 리뷰 iteration 14의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `26a4cbb81` (`core(monitor): resolve S5 iteration-13 finding`)
- Scope: 검토 checkout에서 `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`
- Scope 파일 수: 631
- Scope aggregate SHA-256 (각 파일 sha256sum을 정렬 후 다시 sha256sum): `ba0b6c526ee2193046b0a0e78b2e5ace6b5ba384fd23eff7e1f43b5566327969`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일 또는 명령·남은 범위·갱신 시각을 계속 갱신하라.
- `core/doc/internals`는 구현 확정 전 문서이므로 **판정의 근거와 수정 대상에서 제외**한다 (clean 근거로 쓰지 말 것). 최종 internals는 리뷰 clean 뒤 S5-11에서 갱신된다.
- sanitizer·공개 API surface gate·package 실물 생성 검증은 리뷰 종료 뒤 coordinator가 실행한다. 반복 실행하지 마라. 단, 소스 정적 대조와 필요한 국소 빌드·단일 테스트 실행은 자유다.
- 이번은 4회차 이후 iteration이다: 각 축의 `CLEAN`은 해당 축의 blocker·high·medium finding 0건을 뜻한다. low finding은 별도로 기록하되 `CLEAN` 판정을 막지 않는다.
- 재지적 규칙: 이전 iteration에서 resolved로 판정된 finding을 다시 열려면 이전 finding ID와 수정 commit을 명시하고 이전에 없던 구체적 반례를 제시해야 한다. 같은 근본 원인은 새 ID로 쪼개지 말고 하나의 root-cause family로 묶어 보고하라.

## 절차 규정 (ledger §2.2 갱신 반영)

너의 산출물은 review 디렉터리의 `progress.md`와 `review.ko.md` 두 문서뿐이다. **build, 테스트 실행, sanitizer, package 생성 등 어떤 실행 작업도 수행하지 마라.** 실행 증거는 manifest §2에 기록된 coordinator의 결과(일반 build 오류 0, CTest 85/85)만 사용한다. 판정은 소스 정적 대조로만 한다.

## 우선 검증: iteration 13 병합 finding 1건의 해소 여부

[iteration 13 finding ledger](../iteration-13/finding-ledger.ko.md)를 읽고 수정 commit `26a4cbb81`를 소스에서 대조하라. 이전 iteration에서 해소 판정된 finding(특히 generation ruling — 양 리뷰어가 spec 근거로 수용해 종결됨)은 새 반례 없이 다시 열지 마라.

1. S5-13-01a: `set_monitor_handler_state`가 `state->dispatch_sync`를 publication부터 `dispatch_task_id` 저장까지 보유하고, `monitor_handler_task`가 handler load 전 같은 lock을 취하므로 immediate tick·self-close finalizer의 ID snapshot이 등록 commit 이후로 직렬화되는지 (`monitor_api.cpp`). lock order(`dispatch_sync`→scheduler `_sync` 단방향, scheduler는 `_sync` 해제 후 task 호출)가 안전한지 독립 검증하라.
2. S5-13-01b: `add_periodic_task`의 `_schedule` insert bad_alloc 시 `_tasks` entry 제거(strong rollback)와 `ENOMEM` (`service_control_runtime.cpp`).
3. S5-13-01c: 회귀 테스트 `test_monitor_handler_attach_with_queued_events_and_self_close`가 큐 적재 후 attach→첫 callback self-close를 반복 검증하는지 (`test_monitor_socket_contract.cpp`).

## 이후: 최신 Core 전체 scope 재검토

iteration 10 finding 해소 판정 뒤, 전체 scope를 처음부터 적대적으로 재검토하라.

- I1 계약 구현 일치: `core/doc/spec/core` 전체(공통 8·service 5·socket 9)와 source의 계약 일치, 관찰 가능한 동작·오류·수명·동시성.
- I2 POSD·DDD: 깊은 모듈·정보 은닉·복잡성 하향 이동, MeshNode·Spot·Actor·session 책임 경계.
- I3 정리 완결성: 죽은 code·선언·test·build target·호환 잔재 (internals 문서는 제외).
- Known risk 4건을 소스 대조로 명시 판정: TSAN auto-HWM lock-order, raw command mailbox ypipe, raw socket teardown 관찰, ctx_term linger.
- package metadata(debian/redhat/nuget 10.0.0·SOVERSION 10) 정적 대조.

## 출력 계약

자신의 review 디렉터리에 `review.ko.md`를 작성하고, 같은 내용을 최종 결과로 반환하라. 형식:

1. Scope 확인 (시작·종료 파일 수와 aggregate SHA-256)
2. iteration 10 finding 8건 각각의 해소 판정
3. I1 / I2 / I3 각각: Finding(`[축][심각도] file:line — 문제 — 근거 — 수정 제안` 형식, 없으면 "없음"), Evidence, Verdict(CLEAN 또는 NOT CLEAN — 4회차 이후 규칙 적용)
4. low finding 목록 (있으면; CLEAN을 막지 않음)
5. Known risk 4건 판정
6. 마지막 줄: 세 축 모두 CLEAN이면 정확히 `CORE REVIEW CLEAN`, 아니면 정확히 `CORE REVIEW NOT CLEAN`

문체 교정·취향 차이는 finding으로 등록하지 마라. finding은 공개 계약, 관찰 가능한 동작, concurrency·resource, package·artifact, 검증 누락에 구체적 영향을 주는 것만 등록한다.
