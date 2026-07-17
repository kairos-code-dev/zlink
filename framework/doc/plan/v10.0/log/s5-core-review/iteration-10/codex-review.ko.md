# RouteMesh 10.0.0 S5 Core 리뷰 — iteration 10, R1

## Snapshot 검증

- 대상 commit: `a4e91c01d8e0ac1019c61bc1ef04e0f614f68db3`
- scope 파일 수: 시작/종료 모두 `631`
- SHA-256 aggregate: 시작/종료 모두 `536d62e84fb7f00df811098da619697481ac717e9d626fdb08e27412dc489d3c`
- manifest와 일치
- 종료 시 tracked scope diff 없음
- 요청한 `git worktree add`는 `.git/worktrees`가 read-only라 실패했다. 같은 경로에 local clone을 만들고 detached commit으로 고정해 대체했다.
- 소스 파일은 수정하지 않았다.

## Iteration 9 finding 7건 재판정

1. Request transaction: 원래 결함은 해소됐다. operation·completion storage와 timeout을 전달 전에 준비하고([mesh_node_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_node_api.cpp:69)), 실패 시 transaction 소멸자가 operation·reply route를 제거한다([mesh_node_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_node_api.cpp:121)). 실제 전달 뒤에만 commit한다([mesh_messaging_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_messaging_api.cpp:346), [mesh_messaging_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_messaging_api.cpp:377)).
2. Reply/completion 선소비: 원래 결함은 해소됐다. terminal record가 operation 등록 때 선할당되고([mesh_node_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_node_api.cpp:78)), ready admission과 operation 제거가 같은 lock에서 commit된다([mesh_runtime.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:1049)). Payload/ready-index fault 뒤 token retry 테스트도 존재한다([test_mesh_node_basic.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/tests/integration/test_mesh_node_basic.cpp:224)).
3. Package version·ABI: 해소. CMake `10.0.0`, SOVERSION `10`([CMakeLists.txt](/tmp/claude-1000/zlink-s5-it10-codex/core/CMakeLists.txt:11), [CMakeLists.txt](/tmp/claude-1000/zlink-s5-it10-codex/core/CMakeLists.txt:1368)); Debian `libzlink10`([control](/tmp/claude-1000/zlink-s5-it10-codex/core/packaging/debian/control:18)); RPM `10.0.0/libzlink10`([zlink.spec](/tmp/claude-1000/zlink-s5-it10-codex/core/packaging/redhat/zlink.spec:11)); NuGet `10.0.0/10_0_0`([package.config](/tmp/claude-1000/zlink-s5-it10-codex/core/packaging/nuget/package.config:4)).
4. Dead forward declaration 4개: 소스 선언은 제거됐다. 별도의 stale internals 문서는 신규 finding F7이다.
5. `pending_operation_t::deadline_ms`: 제거됐다. 남은 `deadline_ms`는 Actor transfer용 별도 상태다.
6. 제거 식별자 재사용 gate: 해소([check_public_surface.py](/tmp/claude-1000/zlink-s5-it10-codex/core/tests/contract/check_public_surface.py:238), [removed-identifiers-10.0.0.json](/tmp/claude-1000/zlink-s5-it10-codex/core/tests/contract/removed-identifiers-10.0.0.json:110)).
7. C ABI OOM 정책 중복: 27개 장벽은 `submit_out_of_memory_result()` 정책 helper를 사용한다([mesh_runtime.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:980)).

## Findings

- F1 — `[I1][high] core/src/runtime/services/mesh/mesh_runtime.cpp:321 — MeshNode lifecycle generation이 모든 새 node에서 1로 다시 시작한다 — spec은 같은 RID로 다시 시작한 수명을 generation으로 구분하고 더 높은 generation이 이전 generation을 교체하도록 요구하지만, constructor는 항상 1을 설정하고 새 값을 할당하는 경로가 없다([01-mesh-node.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/spec/core/service/01-mesh-node.ko.md:212), [01-mesh-node.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/spec/core/service/01-mesh-node.ko.md:242)) — RID 재생성에도 중복되지 않는 lifecycle generation allocator를 두고 restart·stale peer contract test를 추가한다.`

- F2 — `[I1·I2][high] core/src/api/mesh/mesh_messaging_api.cpp:21 — committed timeout task가 operation/node 수명과 분리되어 stale raw node pointer와 low serial만 보관한다 — guard는 commit 뒤 task를 cancel하지 않고([mesh_messaging_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_messaging_api.cpp:140)), callback은 raw pointer를 pin한 뒤 low serial만 조회한다([mesh_messaging_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_messaging_api.cpp:57)). Destroy는 operation을 지우고 node를 delete하며([mesh_node_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_node_api.cpp:615), [mesh_node_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_node_api.cpp:622)), 새 node는 serial 1부터 다시 시작한다([mesh_runtime.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/services/mesh/mesh_runtime.cpp:342)). 같은 주소가 재사용되면 이전 timer가 새 node의 같은 low operation을 조기 timeout시킬 수 있다 — operation이 timeout handle을 소유해 terminal completion/destroy 시 cancel하고, callback은 full operation ID와 lifecycle generation을 검증한다.`

- F3 — `[I1·I2][high] core/src/api/monitoring/monitor_api.cpp:193 — monitor handler registry가 pin되지 않은 raw state pointer를 반환한다 — status는 registry lock이 풀린 뒤 state를 역참조하지만([monitor_query_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/monitoring/monitor_query_api.cpp:147)), 동시 close는 registry에서 제거한 뒤 같은 state를 delete한다([monitor_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/monitoring/monitor_api.cpp:240), [monitor_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/monitoring/monitor_api.cpp:50)). 이는 monitor control path correctness와 close fail-fast 계약([README.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/spec/core/socket/README.ko.md:22))을 위반하며 UAF/파괴된 mutex 접근이 가능하다 — registry entry를 shared ownership 또는 lifecycle pin으로 반환하고 close가 active reader 종료를 기다리도록 한다.`

- F4 — `[I1][medium] core/src/api/mesh/mesh_actor_api.cpp:1277 — zlink_actor_join_reply가 flags와 formal backpressure 계약을 무시한다 — flags는 즉시 unused 처리되고, local completion은 mailbox budget/SNDTIMEO 확인 없이 splice 후 token과 membership을 commit한다([mesh_actor_api.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/mesh/mesh_actor_api.cpp:1452)). Spec은 DONTWAIT에서 EAGAIN, blocking에서 SNDTIMEO/ETIMEDOUT, 두 실패에서 token retry를 요구한다([04-actor.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/spec/core/service/04-actor.ko.md:227)) — completion reservation에 contract-defined capacity admission을 결합하고 성공한 admission 뒤에만 token·membership을 commit한다.`

- F5 — `[I1][medium] core/src/runtime/transports/asio/asio_tcp_acceptor_config.hpp:33 — acceptor open·option·bind·listen의 모든 오류를 EADDRINUSE로 변환한다 — 실제 address collision이 아닌 EPERM·EMFILE·ENOMEM 등도 ZLINK_BIND_ADDR_IN_USE로 노출된다([zlink_errno.h](/tmp/claude-1000/zlink-s5-it10-codex/core/include/zlink_errno.h:180)). 이번 sandbox의 socket syscall EPERM도 ctest에서 502/EADDRINUSE로 오분류됐다 — Boost error code를 실제 errno/result bucket으로 변환하고 EADDRINUSE는 실제 bind collision에만 사용한다.`

- F6 — `[I3][medium] core/tests/unittest/unittest_request_timeout_scheduler.cpp:38 — scheduler 회귀 테스트의 timeout 단위가 1000배 짧다 — SETTLE_TIME은 millisecond 값이고 timeout_ms는 6000으로 계산하지만([testutil.hpp](/tmp/claude-1000/zlink-s5-it10-codex/core/tests/testutil.hpp:36)), 비교 대상 stopwatch는 microsecond를 반환한다([zlink_utils.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/api/core/zlink_utils.cpp:36)). 따라서 의도한 6초가 아니라 약 6ms scheduling 지연으로 실패한다 — chrono 기반 동일 단위 deadline을 사용하거나 비교값을 microsecond로 변환한다.`

- F7 — `[I3][low] core/doc/internals/multipart-atomicity.ko.md:299 — 제거된 service SPOT PUB/SUB 구현을 current internals로 설명한다 — 존재하지 않는 spot_sub_recv.cpp를 링크하고 제거된 spot_sub_t::recv()를 설명하며 같은 broken link가 문서 후반에도 남아 있다([multipart-atomicity.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/internals/multipart-atomicity.ko.md:1482)) — RouteMesh 현재 구조로 단락을 다시 쓰거나 더 이상 유효하지 않은 예외 설명을 제거한다.`

## CTest 재현

빌드:

- 요청한 전체 Debug 빌드는 약 82%에서 `/tmp` 용량 부족(`No space left on device`)으로 중단됐다. 20분 초과는 아니었다.
- 그 전에 `test_monitor_socket_contract`와 shared library가 링크됐고, 불필요한 해당 build의 test executable을 제거한 뒤 `unittest_request_timeout_scheduler` target을 별도로 정상 링크했다.
- 생성된 shared library SONAME은 `libzlink.so.10`.
- `contract_public_surface`: 통과.
- NuGet XML 3개: parse 성공.

지정 regex ctest 3회:

| 회차 | scheduler | monitor |
|---|---|---|
| 1 | PASS | FAIL, abort |
| 2 | PASS | FAIL, abort |
| 3 | PASS | FAIL, abort |

Scheduler는 이번 독립 실행에서 3/3 통과했지만 F6의 6ms 단위 오류 때문에 manifest의 부하 상황 실패는 소스로 설명된다.

Monitor는 sandbox가 `socket(AF_INET, …)`을 `EPERM`으로 차단해 매번 wildcard bind 단계에서 먼저 실패했다. F5 때문에 이 값이 `ZLINK_BIND_ADDR_IN_USE(502)/EADDRINUSE`로 표시됐고, forced teardown에서 `mutex.hpp:108 Invalid argument` abort가 이어졌다. 정상 network 환경의 원래 monitor failure를 동일 조건으로 분리 재현했다고 볼 수는 없다. 다만 F3의 unpinned registry state race는 실행 환경과 무관하게 소스로 확정된다. `Invalid argument`의 직접 원인이 F3이라는 주장은 가설로 남긴다.

## Known risk 판정

1. TSAN auto-HWM lock-order: 미해소·추적 유지. `_slot_sync`를 잡은 상태에서 socket plan이 monitor lock을 취한다([ctx_auto_hwm_recalc.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80), [socket_base.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/sockets/common/socket_base.cpp:214)). 반대 순서의 확정 경로는 이번 정적 검토에서도 입증하지 못해 별도 finding으로 올리지 않았다.
2. Raw command mailbox ypipe: 미해소·추적 유지. send/read는 `_sync`로 감싸지만([mailbox.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/core/mailbox.cpp:39), [mailbox.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/core/mailbox.cpp:89)), 기존 TSAN 관찰을 무효화하는 새 ownership 증거가 없다.
3. Raw socket teardown: 미해소·추적 유지. peer back-reference는 원자성·공유 lock 없이 수정되고([pipe.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/core/pipe.cpp:200), [pipe.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/core/pipe.cpp:723)), Asio teardown은 session의 `blob_t` view를 읽은 뒤 session teardown을 진행한다([asio_engine.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/engine/asio/asio_engine.cpp:1845)).
4. `ctx_term` linger: 계약 일치, finding 아님. 기본 blocky context는 linger `-1`을 사용하고([socket_base.cpp](/tmp/claude-1000/zlink-s5-it10-codex/core/src/runtime/sockets/common/socket_base.cpp:129)), spec도 모든 socket close까지 block될 수 있음을 명시한다([01-context.ko.md](/tmp/claude-1000/zlink-s5-it10-codex/core/doc/spec/core/01-context.ko.md:123)).

## 축별 판정

### I1 계약 구현 일치

- Finding: F1, F2, F3, F4, F5
- Evidence: lifecycle generation 고정, stale timeout ABA, monitor state UAF, Actor join reply backpressure 미구현, bind errno 오분류.
- Verdict: **NOT CLEAN**

### I2 POSD·DDD 리팩터링

- Finding: F2, F3
- Evidence: operation과 timeout task 수명이 한 owner에 닫히지 않았고, monitor registry가 raw pointer lifetime 책임을 모든 호출자에게 노출한다.
- Verdict: **NOT CLEAN**

### I3 정리 완결성

- Finding: F6, F7
- Evidence: 잘못된 scheduler test 시간 단위와 제거된 SPOT 구현을 가리키는 stale internals 문서.
- Verdict: **NOT CLEAN**

CORE REVIEW NOT CLEAN
