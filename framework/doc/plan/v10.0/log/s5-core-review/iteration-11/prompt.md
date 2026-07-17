# RouteMesh 10.0.0 S5 Core 구현 리뷰 — iteration 11 공통 prompt

너는 S5 Core 구현 리뷰 iteration 11의 독립 리뷰어다. 이 prompt는 두 리뷰어(R1 Codex, R2 Claude Sonnet)에게 byte 단위로 동일하게 전달된다. 다른 리뷰어의 결과나 coordinator의 해석을 판정 근거로 사용하지 마라.

## Snapshot

- Review 대상 commit: `c1c579ad1` (`core(mesh): resolve S5 iteration-10 findings`)
- Scope: 검토 checkout에서 `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md`
- Scope 파일 수: 631
- Scope aggregate SHA-256 (각 파일 sha256sum을 정렬 후 다시 sha256sum): `56a1b0c135e9357ee3da1666a45084239f9b4a195b5b4d86ee77b471b9a01305`
- 시작과 종료에 scope 파일 수와 aggregate SHA-256을 확인해 기록하고, 검토 중 어떤 파일도 수정하지 마라.

## 절차 규칙 (ledger §2 최신)

- 시간 제한은 없다. 전체 범위를 검토하고 결과 파일을 기록한 뒤 정상 종료해야 완료다.
- 시작할 때 자신의 review 디렉터리에 `progress.md`를 만들고, 3분보다 긴 간격이 생기지 않도록 현재 검토 축·파일 또는 명령·남은 범위·갱신 시각을 계속 갱신하라.
- `core/doc/internals`는 구현 확정 전 문서이므로 **판정의 근거와 수정 대상에서 제외**한다 (clean 근거로 쓰지 말 것). 최종 internals는 리뷰 clean 뒤 S5-11에서 갱신된다.
- sanitizer·공개 API surface gate·package 실물 생성 검증은 리뷰 종료 뒤 coordinator가 실행한다. 반복 실행하지 마라. 단, 소스 정적 대조와 필요한 국소 빌드·단일 테스트 실행은 자유다.
- 이번은 4회차 이후 iteration이다: 각 축의 `CLEAN`은 해당 축의 blocker·high·medium finding 0건을 뜻한다. low finding은 별도로 기록하되 `CLEAN` 판정을 막지 않는다.
- 재지적 규칙: 이전 iteration에서 resolved로 판정된 finding을 다시 열려면 이전 finding ID와 수정 commit을 명시하고 이전에 없던 구체적 반례를 제시해야 한다. 같은 근본 원인은 새 ID로 쪼개지 말고 하나의 root-cause family로 묶어 보고하라.

## 우선 검증: iteration 10 병합 finding 8건의 해소 여부

[iteration 10 finding ledger](../iteration-10/finding-ledger.ko.md) §2·§5를 읽고 각 항목의 수정을 소스에서 대조하라.

1. S5-10-01 scheduler lost-wakeup: `core/src/api/socket/request_timeout_scheduler_internal.cpp` — liveness 판정·thread 기동·task 삽입이 한 critical section인지, 예외 안전 순서와 firing_thread self-cancel이 올바른지.
2. S5-10-02 lifecycle generation: `allocate_lifecycle_generation()` (mesh_runtime.cpp) — wall-clock 앵커 단조 할당이 01-mesh-node §4·§5의 재시작 구분·교체 계약을 충족하는지.
3. S5-10-03 timeout task 소유: `pending_operation_t::timeout_task`, guard commit의 task 인계, 완료·destroy 경로의 node-mutex 밖 cancel, callback의 full operation ID 검증 — ABA가 닫혔는지, cancel 경로에 데드락·누락이 없는지 (완료 경로 전수: complete_pending_operation_with_commit의 정상/owner-미존재 분기, commit_prepared_pending_operation, destroy, shutdown).
4. S5-10-04 monitor registry pin: `monitor_api.cpp`/`monitor_api_internal.hpp` — reader 진입점 5개(zlink_monitor_status, require_monitor_recv_model, zlink_socket_monitor_handler 등록, zlink_close 검사 경로, zlink_monitor_close)가 전부 pin 계약을 쓰는지, unregister·self-close finalizer의 pin drain 대기가 이중 delete·self-deadlock 없이 올바른지, 놓친 reader가 없는지 독립적으로 전수 재조사하라.
5. S5-10-05 join reply flags: coordinator는 "로컬 completion은 선예약 설계로 수락이 항상 성공하므로 EAGAIN 도달 불가, 실결함은 wire 경로 flags 무시"로 판정하고 `wire_submit_join_reply`에 flags를 관통시켰다. 이 판정이 04-actor §3 계약을 충족하는지 독립 판정하라.
6. S5-10-06 acceptor errno: `asio_tcp_acceptor_config.hpp`의 `acceptor_error_to_errno()` — EADDRINUSE가 실제 충돌에만 쓰이는지.
7. S5-10-07 테스트 단위: `unittest_request_timeout_scheduler.cpp`의 µs 상한.
8. S5-10-08: S5-11로 이관됨(이번 검토 대상 아님, internals 제외 규칙).

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
