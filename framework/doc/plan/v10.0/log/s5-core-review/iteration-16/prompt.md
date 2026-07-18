# RouteMesh 10.0.0 S5 Core 구현 리뷰 — iteration 16 공통 prompt

너는 S5 Core 구현 리뷰 iteration 16의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `1f247af7a` (`core(control): resolve S5 iteration-15 finding`)
- Scope: 검토 checkout에서 `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`
- Scope 파일 수: 632 (신규 unittest 포함; 이전 iteration의 631에서 증가)
- Scope aggregate SHA-256 (각 파일 sha256sum을 정렬 후 다시 sha256sum): `398ee290bd39d1fb2070b9585cfb86bf63646f2407600c68ca26a4070e7fa993`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일 또는 명령·남은 범위·갱신 시각을 계속 갱신하라.
- `core/doc/internals`는 구현 확정 전 문서이므로 **판정의 근거와 수정 대상에서 제외**한다 (clean 근거로 쓰지 말 것). 최종 internals는 리뷰 clean 뒤 S5-11에서 갱신된다.
- sanitizer·공개 API surface gate·package 실물 생성 검증은 리뷰 종료 뒤 coordinator가 실행한다. 반복 실행하지 마라. 단, 소스 정적 대조와 필요한 국소 빌드·단일 테스트 실행은 자유다.
- 이번은 4회차 이후 iteration이다: 각 축의 `CLEAN`은 해당 축의 blocker·high·medium finding 0건을 뜻한다. low finding은 별도로 기록하되 `CLEAN` 판정을 막지 않는다.
- 재지적 규칙: 이전 iteration에서 resolved로 판정된 finding을 다시 열려면 이전 finding ID와 수정 commit을 명시하고 이전에 없던 구체적 반례를 제시해야 한다. 같은 근본 원인은 새 ID로 쪼개지 말고 하나의 root-cause family로 묶어 보고하라.

## 절차 규정 (ledger §2.2 갱신 반영)

너의 산출물은 review 디렉터리의 `progress.md`와 `review.ko.md` 두 문서뿐이다. **build, 테스트 실행, sanitizer, package 생성 등 어떤 실행 작업도 수행하지 마라.** 실행 증거는 manifest §2에 기록된 coordinator의 결과(일반 build 오류 0, CTest 86/86)만 사용한다. 판정은 소스 정적 대조로만 한다.

## 우선 검증: iteration 15 병합 finding 2건의 해소 여부

[iteration 15 finding ledger](../iteration-15/finding-ledger.ko.md)를 읽고 수정 commit `1f247af7a`를 소스에서 대조하라. 이전 iteration에서 해소 판정된 finding은 새 반례 없이 다시 열지 마라. S5-14-01의 5개 세부는 양 리뷰어 해소 합의로 종결됐다.

1. S5-15-01 (worker lifecycle, `service_control_runtime.cpp` + 신규 `unittest_service_control_runtime.cpp`):
   - 각 `call.fn` 호출의 bad_alloc seal — 실패 tick 폐기 정책이 명시되고 epilogue(`_active_task_id` 해제·broadcast)가 무조건 실행되는지.
   - `run()` 최후 catch의 terminal lifecycle commit(`_stopping`·active 해제·broadcast 한 critical section)이 `remove_task`·`zlink_ctx_term` 대기 계약을 깨우는지.
   - 회귀 unittest가 첫 tick bad_alloc 후 worker 생존(3 tick)·remove 즉시 반환·ctx_term 종료를 검증하고 CMake에 등록됐는지 (전체 suite는 86개가 됐다 — manifest §2).
2. S5-15-02 (low 편승): `schedule_task_locked`의 도달 불가 fallback이 `zlink_assert`로 교체돼 무할당 계약이 명시됐는지 — assert 방식이 caller 계약과 정합적인지.

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
