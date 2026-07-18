# RouteMesh 10.0.0 S5 Core 구현 리뷰 — iteration 15 공통 prompt

너는 S5 Core 구현 리뷰 iteration 15의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `7b580a520` (`core(control): resolve S5 iteration-14 finding`)
- Scope: 검토 checkout에서 `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`
- Scope 파일 수: 631
- Scope aggregate SHA-256 (각 파일 sha256sum을 정렬 후 다시 sha256sum): `cf26306098cb19579b2759b3c82df89809ece3ddb5d4aebac23372da02eefdc5`
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

## 우선 검증: iteration 14 병합 finding 1건의 해소 여부

[iteration 14 finding ledger](../iteration-14/finding-ledger.ko.md)를 읽고 수정 commit `7b580a520`를 소스에서 대조하라. 이전 iteration에서 해소 판정된 finding은 새 반례 없이 다시 열지 마라.

S5-14-01 (scheduler allocation 봉인, `service_control_runtime.{cpp,hpp}` + `context_api.cpp`):

1. `task_entry_t::cached_node` — schedule node가 add 시 1회 할당되고, `deschedule_task_locked`의 extract와 `schedule_task_locked`의 rekey·move 재삽입으로 wakeup·periodic reschedule이 무할당·no-throw인지.
2. `add_periodic_task`가 emplace부터 최초 schedule insert까지 하나의 sealed transaction(실패 시 완전 rollback + ENOMEM/0)인지.
3. loop가 pass당 단일 due call 처리로 vector 할당을 제거했는지, `_active_task_id` 설정·해제 의미(동시 remove_task 대기 계약 포함)가 기존과 동등한지.
4. `run()`의 최후 방어 seal과 `zlink_ctx_set_ext`·`zlink_ctx_auto_hwm_recalculate`의 bad_alloc→ENOMEM barrier.
5. multimap node handle 재사용의 정확성: extract 후 `key()` 변경·재삽입이 iterator/소유권 규칙을 위반하지 않는지, remove_task 시 cached_node가 entry와 함께 해제되는지.

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
