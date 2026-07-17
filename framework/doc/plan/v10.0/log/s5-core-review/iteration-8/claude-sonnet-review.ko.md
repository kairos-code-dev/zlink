# S5 Core 구현 독립 리뷰 R2 (Claude Sonnet) — iteration 8 (연장 4회차, 전체 pass)

## 결론

**CORE REVIEW NOT CLEAN**

- iteration 7 수정 4건: **해소 2건**(N7-C1, CS7-I3-01/N7-I3-01), **부분 해소
  2건**(CS7-I1-02/N7-I1-01, CS7-I1-01/N7-I1-02 — 각각 명시적으로 겨냥한 범위는
  막혔지만 같은 결함 계열의 잔여 창이 남아 있다)
- 신규/계속 finding: **3건**(`high 1`, `medium 2`)
- I1 계약 구현 일치: **NOT CLEAN**(high 1, medium 2)
- I2 POSD·DDD: **CLEAN**
- I3 정리 완결성: **CLEAN**

iteration 7의 registry lifecycle pinning은 그 finding이 정확히 겨냥한
shutdown/destroy 재진입 5종 interleaving을 전부 닫았고, `complete_operation`
nothrow 장벽·25개 공개 submit 진입점 외곽 장벽·ingress 메시지 단위 장벽도
설계대로 동작한다 — 이 부분은 코드 추적과 재실행으로 확인했다. 그러나 전체
scope를 다시 훑으며 세 갈래의 잔여/신규 결함을 발견했다. 첫째, registry
lifecycle pin은 `zlink_mesh_node_shutdown`과 `zlink_mesh_node_destroy` 두
함수에만 적용됐을 뿐, 같은 handle을 쓰는 나머지 58개 `as_mesh_node()` 호출
지점(§11이 "thread-safe"라고 명시한 send/request/publish/peer
intent/query를 포함해 사실상 모든 다른 공개 API와 timer 콜백)은 여전히
registry-lock 검사와 실제 사용 사이에 아무 pin도 없는 창을 그대로 갖고 있다
— 이는 CS7-I1-02가 진단한 근본원인("as_mesh_node가 반환하는 raw pointer에
수명 보장이 없다")이 shutdown 하나에만 좁게 적용됐다는 뜻이다(§4.1
CS8-I1-01, high). 둘째, 공개 `zlink_submit_result_t` 진입점은 27개인데
외곽 `bad_alloc` 장벽은 25개뿐이라 2곳이 여전히 무장벽이다(§4.1 CS8-I1-02,
medium). 셋째, wire ingress의 `handle_reply`는 ACTOR_JOIN/ACTOR_LOOKUP
분기에서 operation bookkeeping을 지운 뒤에야 `kind_data` 벡터를 구성하는데,
이 구성이 OOM으로 던지면 iteration 8이 새로 두른 메시지 단위 장벽이 process
종료는 막아도 그 operation의 완료 통지 자체가 영구히 유실되고, ACTOR_JOIN
accepted 분기는 이미 커밋한 actor 소속 변경과 유실된 통지 사이에 불일치를
남긴다(§4.1 CS8-I1-03, medium). 세 건 모두 I1을 다시 NOT CLEAN으로 만든다.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Sonnet |
| Candidate commit / HEAD | `ee8036a09e951e89db5730426d6a91a44afdac85` — 시작·종료 모두 동일 |
| Scope hash (시작) | `269b6c1b17aab31c4b74979d2eb8c61482ce102d49da63de3f06c2cba7c632ff` (631 files) — manifest §2와 일치 |
| Scope hash (종료) | 동일 값 — 일치, `git status core/` clean |
| iteration 7→8 delta | `f8c35e6fe..ee8036a09`, 10 files(+548/−62): `mesh_runtime.{hpp,cpp}`(pin/claim/unregister-wait, ready-handler thread tracking, nothrow `complete_operation`, alloc-fault test hook), `mesh_node_api.cpp`(shutdown/destroy를 pin/claim 경유로 재작성, pause hook), `mesh_dispatch_api.cpp`(ready-handler EDEADLK 정정, `zlink_mesh_reply` 외곽 장벽), `mesh_actor_api.cpp`(8개 진입점 외곽 장벽, ENOMEM 매핑), `mesh_messaging_api.cpp`(6개 외곽 장벽, alloc-fault hook 삽입), `mesh_stream_session_api.cpp`(5개 외곽 장벽, stream complete ENOMEM 매핑), `mesh_wire_ingress.cpp`(ingress 메시지 단위 장벽), `test_mesh_lifecycle_contracts.cpp`(신규 2 test), `CHANGELOG.md` |
| `check_public_surface.py . core/build/lib/libzlink.so.10.0.0` | **PASS**, 196 exports 정확 일치, 제거 identifier 없음 |
| `unittest_public_contract_headers` | 1/1 PASS |
| `cmake --build core/build -j20` | 이미 최신, 성공 |
| `ctest --test-dir core/build -j8` | **100% tests passed, 0 failed out of 85** |
| ASAN 5 mesh 바이너리(재빌드·재실행) | lifecycle 12·peer_admission 12·stress 3·monitor_matrix 6·node_basic 8 = 41 case, 전부 리포트 0 |
| TSAN lifecycle(`setarch $(uname -m) -R`) | 12/12 PASS, 경고 17건(auto-HWM lock-order 13 + `mailbox_t::recv` 1 + `pipe_t::detach_peer_backref` 1 + asio `blob_t` 2) — manifest §4의 수치와 정확히 일치, 전부 기존 계열. `test_destroy_waits_for_pinned_shutdown`·`test_submit_alloc_failure_maps_to_out_of_memory`를 포함한 신규 mesh 코드 경로에서 새 race 프레임 0 |
| TSAN stress(2회 연속) | 3/3 PASS 2회 — `test_ready_handler_churn_under_load` 간헐 실패 재현 소멸(N7-C1 수정 확인) |
| `git diff --check f8c35e6fe ee8036a09` | clean |
| 0-byte·merge marker | scope 631개 파일 전수 재확인: 0건 |
| CMake·package | mesh wire 4 TU(`core/CMakeLists.txt:887-890`), mesh test 5개(`core/tests/CMakeLists.txt:93-97`) 등록 유지 |
| CHANGELOG lifecycle 수치 | `grep -c "RUN_TEST (" core/tests/integration/test_mesh_lifecycle_contracts.cpp` = 12, `CHANGELOG.md:66-68`도 12로 갱신 — 일치 |
| known risk 관련 raw 파일 diff | `f8c35e6fe..ee8036a09`에 `socket_base.cpp`·`pipe.cpp`·`mailbox.cpp`·`ctx_auto_hwm_recalc.cpp`·`mesh_wire_admission.cpp` 없음(diff --stat 10개 파일 목록에 부재) — known risk 4건과 TSAN 2-process admission 3건 모두 이번 delta와 무관 |
| 정적 hygiene | delta 10개 파일 전수 재확인: TODO/FIXME/디버그 출력 없음 |
| 동적 UAF 탐색(신규, §4.1 CS8-I1-01 보강) | scratchpad에서 공개 API만으로 `zlink_mesh_node_status()`(§11 thread-safe query)를 `zlink_mesh_node_destroy()`와 경합시키는 독립 ASAN 하네스를 작성해 20,000회 단발 경합 + 800회×6 racer×3,000회 burst(약 1,440만 호출) 실행 — 크래시 미관측. shutdown/destroy 창도 결정적 pause hook 없이는 기존에 재현되지 않았던 것과 동일하게, 이 창들도 결정적 hook 없이는 blackbox 경합만으로 안정적으로 맞히기 어렵다(§4.1에서 코드 추적 근거로 판정, 동적 미탐지를 안전 증거로 사용하지 않음) |

```bash
git ls-files core/include core/src core/tests core/packaging \
  core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md \
  | xargs sha256sum | sha256sum
```

TSAN 2-process admission 3건(round-robin·MIXED·reconnect)은 과제 지시와
iteration-5 manifest §4가 이미 baseline `472f66a32` 재현으로 delta-무관을
확정한 사항이며, 이번 delta도 `mesh_wire_admission.cpp`를 건드리지 않아
재실행하지 않았다.

## 2. iteration 7 수정 4건 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| CS7-I1-02 / N7-I1-01(high) — shutdown/destroy registry lifecycle pinning | **부분 해소**(겨냥 범위 완전 해소, 같은 결함 계열의 잔여 창은 신규 CS8-I1-01) | §3에서 5개 interleaving(파킹 중 destroy, pin~lock 사이 destroy 완주, destroy claim 후 shutdown, destroy 완료 후 shutdown, 이중 destroy)을 전부 코드 추적·신규 test로 재확인했다. `as_mesh_node`의 lock 밖 `check_tag`는 shutdown/destroy 경로에서는 `pin_node_lifecycle`/`claim_node_destroy`로 대체되어 두 함수 모두 registry mutex 안에서 `check_tag`를 수행한다(`mesh_runtime.cpp:437,471`). 그러나 이 pin은 shutdown·destroy 두 함수에만 적용됐고, 같은 handle을 쓰는 나머지 58개 `as_mesh_node()` 호출 지점은 여전히 lock 밖 `check_tag`(`mesh_runtime.cpp:512-513`)를 쓰는 옛 패턴 그대로다 — CS7-I1-02가 지목한 근본원인의 나머지 부분이 미해결로 남아 CS8-I1-01로 계속한다 |
| CS7-I1-01 / N7-I1-02(high/medium) — completion·reply OOM 장벽 | **부분 해소**(주요 crash 경로 해소, 잔여 2건은 CS8-I1-02·CS8-I1-03) | `complete_operation`은 nothrow 생성+내부 `bad_alloc` 장벽으로 전환됐고(`mesh_runtime.cpp:960-994`) ingress·timer 스레드에서 더 이상 `std::terminate()` 위험이 없다. `zlink_mesh_reply`(`mesh_dispatch_api.cpp:790,888`)·`zlink_actor_join_reply`(`mesh_actor_api.cpp:1108,1255`)는 외곽 try/catch로 감쌌고, `actor_apply_remote_join_reply`·`handle_reply`의 ACTOR_LOOKUP 분기는 `run_ingress_loop`의 메시지 단위 장벽(`mesh_wire_ingress.cpp:1074-1090`)으로 `std::terminate()`는 막았다. 그러나 (a) 공개 27개 submit 진입점 중 2개가 여전히 외곽 장벽 밖이고(CS8-I1-02), (b) 메시지 단위 장벽이 막는 것은 프로세스 종료뿐이며 그 밑에서 이미 지운 operation bookkeeping과 이미 커밋한 side effect는 그대로 남아 완료 통지가 영구 유실된다(CS8-I1-03) |
| N7-C1(coordinator 발견) — ready handler 해제 spec 정합 | **해소** | `notify_consumer_locked`가 callback 진입 직전 `ready_handler_thread`를 기록하고 반환 뒤 `cv.notify_all()`한다(`mesh_runtime.cpp:686-693`). `zlink_mesh_node_set_ready_handler`는 depth>0이고 같은 thread일 때만 `EDEADLK`, 그 외에는 depth 0까지 `cv.wait`한다(`mesh_dispatch_api.cpp:178-186`) — spec 02-dispatch:176-177("성공한 해제는 이미 시작한 callback이 모두 반환한 뒤 완료되고, handler 안에서의 해제만 EDEADLK")과 정확히 일치. TSAN stress `test_ready_handler_churn_under_load`를 2회 연속 재실행해 간헐 `ZLINK_HANDLER_DEADLOCK` 실패가 재현되지 않음을 확인했다(§1) |
| CS7-I3-01 / N7-I3-01(low) — CHANGELOG lifecycle case 수 | **해소** | `RUN_TEST (` count = `ctest -N`의 lifecycle 항목 수 = `CHANGELOG.md:66-68`의 서술 = 12로 세 값이 일치한다. shutdown/destroy 재진입 양 순서와 OOM 매핑도 같은 절에 서술됐다 |

## 3. registry lifecycle pinning 5종 interleaving 재검증

`mesh_node_api.cpp:341-527`, `mesh_runtime.cpp:425-501`을 다시 추적했다.

1. **파킹된 shutdown 중 destroy**: shutdown이 pin 후 `shutdown_active=true`로
   drain wait에 파킹(:373-398)되면, destroy는 `node->mutex`를 잡는 즉시
   `shutdown_active`를 보고 `EDEADLK`+`ZLINK_CLOSE_BUSY`로 반환한다(:479-483,
   claim은 `release_node_destroy_claim`으로 되돌림) — node 미삭제.
   `test_destroy_during_shutdown_wait_is_deadlock_error`
   (`test_mesh_lifecycle_contracts.cpp:662-`)가 실제 동시성으로 확인한다.
2. **pin~lock 사이 destroy 완주**: `pin_node_lifecycle`(:348)이 성공한 직후
   테스트 hook으로 정지시키면(:355-361), destroy는 claim(:469)·child
   검사·강제 STOPPED 커밋(:473-514)까지 끝내고
   `unregister_node_and_wait_lifecycle_quiesced`(:519)에서
   `lifecycle_pins != 0`을 보고 대기한다(`mesh_runtime.cpp:489-501`) — node
   storage가 살아있는 채로 shutdown이 재개되어 `node->mutex`를 안전하게
   잡고 `state==STOPPED`를 관측해 `ZLINK_REQUEST_OK`로 반환한다(:370-371).
   shutdown의 `lifecycle_pin_guard_t` 소멸자가 `unpin_node_lifecycle`을
   호출해야 destroy의 대기가 풀리고 `delete node`가 진행된다(:454-457) —
   **신규 `test_destroy_waits_for_pinned_shutdown`**
   (`test_mesh_lifecycle_contracts.cpp:574-604`)가 이 순서를 정확히
   재현하며, 뒤이은 shutdown이 `EFAULT`로 실패함까지 확인한다(:599-600,
   interleaving (d)와 동일 관측).
3. **destroy claim 후 shutdown**: `claim_node_destroy`가 `destroy_claimed`를
   먼저 세우면(:479), 이어지는 `pin_node_lifecycle`은 그 플래그를 보고
   `EDEADLK`로 실패한다(:441-446) — shutdown이 node에 전혀 닿지 못한다.
4. **destroy 완료 후 shutdown**: `unregister_node_and_wait_lifecycle_quiesced`
   가 `live_nodes`에서 제거한 뒤(:496)에는 이후의 `pin_node_lifecycle`이
   `live_nodes.count`에서 실패해 `EFAULT`를 반환한다(:432-435) — 위
   2번의 test가 이 경로도 함께 확인한다.
5. **이중 destroy**: 두 번째 `claim_node_destroy`는 `destroy_claimed`가 이미
   true이므로 `ESTALE`로 실패한다(:475-478).

다섯 interleaving 모두 use-after-free 없이 정본이 요구하는 결과(EDEADLK 또는
안전한 순차 완료)로 수렴한다. ASAN·TSAN 재실행(§1)에서도 이 두 test가 포함된
lifecycle 12/12가 리포트 0이다.

## 4. 축별 finding

### I1 계약 구현 일치 — NOT CLEAN (high 1, medium 2)

#### CS8-I1-01 (high) — registry lifecycle pin이 shutdown/destroy 두 함수에만 적용되어, 나머지 58개 `as_mesh_node()` 호출 지점은 destroy와 경합하는 동일 계열의 use-after-free 창을 그대로 갖고 있음

- 이슈·근거: `pin_node_lifecycle`/`claim_node_destroy`(`mesh_runtime.cpp:425-487`)는
  `zlink_mesh_node_shutdown`(`mesh_node_api.cpp:348`)과
  `zlink_mesh_node_destroy`(`mesh_node_api.cpp:469`) 두 곳에서만 호출된다.
  같은 handle을 검증하는 나머지 모든 공개 API·내부 콜백은 여전히 예전
  `as_mesh_node()`(`mesh_runtime.cpp:503-514`)를 쓰는데, 이 함수는 registry
  mutex를 `live_nodes.count (handle_)` 검사 동안만 잡고(:508-510) 그 블록이
  끝난 뒤(:511) 아무 pin도 세우지 않은 채 raw pointer를 반환한다 — 호출자가
  실제로 `node->mutex`를 잡거나 필드를 건드리기까지 어떤 수명 보장도 없다.
  `grep -c "as_mesh_node (" core/src/api/mesh/*.cpp`는 파일별
  mesh_api.cpp 15·mesh_node_api.cpp 16·mesh_actor_api.cpp 9·
  mesh_stream_session_api.cpp 6·mesh_dispatch_api.cpp 5·
  mesh_transfer_api.cpp 4·mesh_messaging_api.cpp 2·mesh_monitor_api.cpp 1 =
  총 58곳을 센다. 대표 사례:
  - `zlink_mesh_node_connect_peer`(`mesh_node_api.cpp:531-558`) — `:536`에서
    `as_mesh_node`, `check_versioned`·endpoint 검증·`std::string endpoint`
    구성을 거친 뒤 `:558`에서야 `node->mutex`를 잡는다.
  - `zlink_mesh_node_status`(`mesh_node_api.cpp:799-809`) — `:801`에서
    `as_mesh_node`, `check_versioned` 뒤 `:809`에서 `lock_guard`.
  - `node_channel_submit`(`mesh_messaging_api.cpp:340-379`, 4개 공개
    send/request 진입점의 공유 body) — `:353`에서 `as_mesh_node`,
    `check_submit_input`·`owner_id_t`/`rid_bytes_t` 구성 뒤에야
    `:372`/`:400`에서 `node->mutex`를 잡는다.
  - `zlink_mesh_node_set_ready_handler`(`mesh_dispatch_api.cpp:172-180`) —
    같은 패턴.
  - `on_operation_timeout`(`mesh_messaging_api.cpp:27-46`) — **detach된 timer
    스레드**에서 실행되는 내부 콜백도 `:33`에서 pin 없는
    `as_mesh_node`를 쓴 뒤 `:38`에서 lock한다. 공개 API 호출자 스레드뿐
    아니라 core 자신의 백그라운드 스레드도 같은 창에 노출된다.
  정본 §11("send, request, publish, weight 변경, peer intent와 query는
  thread-safe다")은 이 함수군이 다른 스레드의 concurrent 호출에 안전해야
  한다고 규정하고, destroy 자체의 계약도 in-flight 호출을 기다리지 않는다
  ("먼저 child handle이 없는지 검사한다" — child로 열거된 것은 publisher·
  facade/timer·monitor·session뿐이며 이 58개 호출은 child가 아니다,
  `01-mesh-node.ko.md:172-183`). shutdown/destroy 자체를 위해 도입한 정교한
  registry pin/claim 메커니즘은 바로 이런 concurrent 파괴를 안전하게
  만들려는 설계 의도를 보여주는데, 그 적용 범위가 두 함수로 좁혀졌다.
- 영향: 한 스레드가 이 58개 진입점 중 하나(또는 timer 콜백)를 실행하는 동안
  다른 스레드가 같은 handle로 `zlink_mesh_node_destroy`를 호출해 완주하면,
  전자는 `as_mesh_node`가 반환한 뒤 이미 `delete`된 `mesh_node_t`의
  `mutex`를 잠그거나 필드를 읽는다 — use-after-free다. 이는 CS7-I1-02/
  N7-I1-01이 shutdown 하나에서 입증한 것과 정확히 같은 결과 클래스이며,
  트리거 조건은 오히려 더 넓다(정상적인 concurrent send/request/query와
  destroy의 통상적인 스레드 스케줄링만 있으면 된다).
- 수정 범위: `as_mesh_node()` 자체를 `pin_node_lifecycle`과 같은 원자적
  registry-lock 보호 pin으로 교체하거나(58개 호출부 모두 RAII unpin 필요),
  destroy가 이 58개 호출 부류도 인지하는 공유 참조 카운트를 두는 방식.
  timer 콜백(`on_operation_timeout`)도 같은 메커니즘을 써야 한다.
- 검증 방향: `zlink_mesh_node_shutdown`에 쓴 것과 같은 결정적 pause hook을
  대표 진입점(`connect_peer`·`status`·`send_to_channel`·
  `set_ready_handler`) 각각의 `as_mesh_node` 직후~lock 직전에 주입해 동시
  `destroy` 완주를 재현하고, ASAN(use-after-free)·TSAN(mutex-vs-delete
  race) 양쪽에서 안전한 `EFAULT`(destroy가 이미 이겼을 때)와 안전한 정상
  완료(pin이 있었다면 destroy가 기다렸을 때)만 관측됨을 확인한다.
  결정적 hook 없는 blackbox 경합(§1, 약 1,440만 호출)은 크래시를
  관측하지 못했다 — 이는 창이 없다는 증거가 아니라, shutdown/destroy
  창도 전용 hook 없이는 재현되지 않았던 것과 같은 한계다.
- severity 근거: CS7-I1-02/N7-I1-01과 동일한 use-after-free 결과 클래스이고
  그 finding이 high였던 근거가 그대로 적용된다. 오히려 트리거 표면이
  58개 호출 지점(+timer 콜백)으로 넓어 재현 가능성이 더 높다.

#### CS8-I1-02 (medium) — 공개 27개 submit 진입점 중 2개가 여전히 외곽 `bad_alloc` 장벽 밖에 있음

- 이슈·근거: `core/include/zlink/service/*.h`의 `ZLINK_EXPORT
  zlink_submit_result_t` 선언은 정확히 27개다. 구현부에서 함수 본문
  시작이 `try {`인지 확인하면 25개만 그렇고, 나머지 2개는 평범한 `{`다:
  `zlink_mesh_node_request_to_channel`(`mesh_messaging_api.cpp:500-517`)과
  `zlink_stream_session_request_to_actor`
  (`mesh_stream_session_api.cpp:1039-1056`). 두 함수는 각각 형제 함수
  `zlink_mesh_node_send_to_channel`·`zlink_stream_session_send_to_actor`와
  똑같이 `node_channel_submit`/`session_to_actor_submit`을 호출하는데, 형제
  쪽은 iteration 8에서 외곽 `try`/`catch (const std::bad_alloc &)`가
  붙었고(`mesh_messaging_api.cpp:481-492`,
  `mesh_stream_session_api.cpp:1020-1031`) 이 두 곳만 빠졌다. 두 helper
  내부에는 `check_name`의 `std::string` 구성, candidate/round-robin 조회,
  `zlink_mesh_node_status_t` 유사 구조체 구성 등 소규모 할당이 여러 곳
  있어(`mesh_messaging_api.cpp:395-408`,
  `mesh_stream_session_api.cpp:855-897,959-983`) `bad_alloc`이 이 두 진입점
  밖으로 그대로 나갈 수 있다.
- 영향: 이 두 request API에서 storage 부족이 발생하면 정식
  `ZLINK_SUBMIT_OUT_OF_MEMORY`/`ENOMEM` 대신 C ABI 밖으로 C++ 예외가
  전파된다 — extern "C" 경계를 넘는 undefined behavior이며, N6-I1-01/
  CS6-I1-01이 애초에 막으려던 위반과 동일한 성격이다.
- 수정 범위: 두 함수 선언을 나머지 25개와 동일한 function-try-block 패턴으로
  맞추고, `OUT_OF_MEMORY`/`ENOMEM` 매핑을 추가.
- 검증 방향: 신규 `zlink_test_set_mesh_alloc_fault` hook을 이 두 진입점의
  내부 helper 할당 지점에도 arm해 `OUT_OF_MEMORY`/`ENOMEM` 반환과 예외 0을
  확인하고, 해제 후 다음 호출이 정상 성공함을 검증한다.
- severity 근거: N6-I1-01/CS6-I1-01이 처음 이 계열을 발견했을 때와 같은
  medium — 이 두 지점은 std::thread 경계가 없는 일반 API 호출 스레드에서
  실행되므로 CS7-I1-01이 high로 올린 근거(ingress/timer 스레드의 확정적
  `std::terminate()`)가 적용되지 않는다.

#### CS8-I1-03 (medium) — `handle_reply`가 operation bookkeeping을 지운 뒤 구성하는 ACTOR_JOIN/ACTOR_LOOKUP reply tail이 OOM으로 던지면 완료 통지가 영구 유실되고, ACTOR_JOIN accepted 분기는 이미 커밋한 상태 변경과 불일치한다

- 이슈·근거: `handle_reply`(`mesh_wire_ingress.cpp:554-627`)는 `op`를
  `node_->operations`에서 찾은 즉시 무조건 erase한다(:571-576, "이미
  완료됨: exactly-once" 주석). 그 뒤 ACTOR_JOIN 분기(:580-598)는
  `actor_apply_remote_join_reply`(`mesh_actor_api.cpp:303-382`)를 호출하고,
  ACTOR_LOOKUP 분기(:599-623)는 `location` struct를 담는 `kind_data`
  벡터를 직접 구성한다(:618-620) — 둘 다 `try`/`catch` 없이. iteration 8이
  `run_ingress_loop`에 추가한 메시지 단위 장벽(`mesh_wire_ingress.cpp:
  1074-1090`)은 이 두 경로에서 `bad_alloc`이 던져져도 프로세스가
  `std::terminate()`하지 않도록 막지만, catch 블록은 단순히 `continue`할
  뿐이다(:1088). 이때 `op`는 이미 `node_->operations`에서 사라진 뒤이므로
  `complete_operation`이 끝내 호출되지 않고, 이 operation은 이후 어떤
  timeout·shutdown force-completion(`node->operations`를 순회하는 드레인,
  `mesh_node_api.cpp:425-431`)도 다시 찾지 못한다 — 요청자는 성공도 실패도
  timeout도 결코 받지 못한다. ACTOR_JOIN accepted 분기는 더 심각하다:
  `actor_apply_remote_join_reply`는 `node_->mutex` 아래에서 actor의
  `spot_rid`/`spot_generation`/`spot_node_rid`/`membership_epoch`를 이미
  커밋한 뒤(:349-353) `kind_data` 벡터를 구성한다(:375-377) — 이 구성이
  던지면 actor 소속 변경은 **이미 확정**됐는데 원 요청자는 그 사실을 통지받지
  못한다. 이는 coordinator가 이 메시지 단위 장벽을 정당화한 근거
  ("OOM 시 해당 inbound 메시지 drop = 전송 손실과 동일 관측",
  iteration-7 finding-ledger.ko.md §2.2)와 어긋난다 — 실제로 유실된 wire
  message는 이런 로컬 상태 커밋을 만들지 않으며, 로컬 `node->operations`
  항목도 그대로 남아 나중에 timeout으로 수렴한다. 여기서는 그 수렴 경로 자체가
  이미 제거됐다.
- 영향: (a) 드문 OOM 조건에서 실질적으로 영구 hang(요청자가 이 operation의
  완료를 영원히 기다림), (b) ACTOR_JOIN accepted의 경우 actor membership이
  요청자 모르게 이미 바뀐 상태로 남는 관측 가능한 불일치. 두 가지 모두 프로세스
  종료나 메모리 손상은 아니지만 이 iteration이 명시적으로 다루는 "operation은
  결국 정의된 결과로 완료된다"는 계약을 어긴다.
- 수정 범위: `handle_reply`가 `op`를 erase하기 전에 kind별 tail 구성을
  먼저 시도하거나(구성 실패 시 erase하지 않고 재시도/timeout에 맡김),
  최소한 구성 실패 시 `complete_operation`을 `OUT_OF_MEMORY`로 직접 호출하는
  fallback 경로 추가. `actor_apply_remote_join_reply`의 membership commit과
  `kind_data` 구성 순서도 재검토(구성을 먼저 하거나, 실패해도 통지가 반드시
  나가도록).
- 검증 방향: `zlink_test_set_mesh_alloc_fault`를 `handle_reply`의
  ACTOR_JOIN/ACTOR_LOOKUP tail 구성 지점에 arm하는 새 fault point를 추가해,
  주입 시에도 원 요청자가 (성공이든 `OUT_OF_MEMORY`든) 결국 completion을
  받는지, ACTOR_JOIN accepted라면 그 completion이 실제 커밋된 membership과
  일치하는지 확인한다.
- severity 근거: crash나 메모리 손상이 아니고 트리거가 OOM으로 좁아 high는
  아니지만, "모든 operation은 결국 완료된다"는 iteration 7 finding-ledger
  §2.2("완료/응답 절반" 수정)의 핵심 주장이 이 두 특정 분기에서는 여전히
  거짓이라 medium으로 계상한다.

### I2 POSD·DDD — CLEAN

- finding 없음. `core/doc/internals/posd-module-structure.ko.md`가 열거하는
  mesh 4모듈이 `git ls-files core/src/runtime/services/mesh/`의 실제 8개
  파일과 여전히 일치하고 iteration 7→8 delta는 새 파일·새 모듈 경계를 만들지
  않았다. registry lifecycle pin은 handle 수명 문제를 registry(기존 책임
  소재) 안에 흡수했고, ready handler thread 판정도 dispatch/runtime 내부
  상태로 유지되어 새 public abstraction이나 층 경계 침범이 없다.
- CS8-I1-01·CS8-I1-02·CS8-I1-03 모두 계약(handle 수명·errno map·completion
  보장) 문제이지 모듈 경계 문제가 아니다 — I1에만 계상하고 I2에 중복
  계상하지 않는다.

### I3 정리 완결성 — CLEAN

- finding 없음. CHANGELOG lifecycle 12 case가 `RUN_TEST` count·`ctest -N`과
  일치하고(§1), shutdown/destroy 재진입 양 순서·OOM 매핑 설명도 반영됐다.
  scope 631개 파일 0-byte·merge marker 전수 0건, `git diff --check` clean,
  CMake mesh wire 4 TU·mesh test 5개 등록 유지, known-risk 관련 raw 파일
  이번 delta 무변경(§1).
- CS8-I1-02(OOM test 커버리지 부족)는 I1 계약 검증 범위 finding으로 계상했고
  독립 I3 finding으로 중복 계산하지 않는다 — iteration 7의 CS7-I3-01과 같은
  분류 원칙.

## 5. Known risk 4건 명시 판정

| 항목 | 판정 | 근거 |
|---|---|---|
| TSAN auto-HWM lock-order 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 경고 13건 전부 top frame이 `prepare_auto_hwm_socket_plan`/`refresh_auto_hwm_policy`/`ctx_t::create_socket`(`socket_base.cpp:225,372`). `f8c35e6fe..ee8036a09` diff에 `socket_base.cpp`·`ctx_auto_hwm_recalc.cpp` 없음 |
| TSAN raw command mailbox ypipe 계열 | **수용·추적 유지, 신규 finding 없음** | lifecycle 재실행 data race 1건이 `mailbox_t::recv`(`mailbox.cpp:66`). `mailbox.cpp` 이번 delta 무변경 |
| raw socket teardown(`pipe_t::detach_peer_backref`·asio `blob_t`) | **9.x raw 기계 risk로 수용·추적 유지** | lifecycle 재실행에서 `pipe.cpp:202` 1건, asio `blob_t`(`blob.hpp:78,81`) 2건 관측 — manifest §4의 "17=13+1+1+2" 수치와 정확히 일치. `pipe.cpp` 이번 delta 무변경 |
| ctx_term linger | **수용·추적 유지** | `socket_base.cpp:129-134`의 blocking 기본 linger `-1`은 이번 delta로 미변경. delta는 mesh API/runtime 계층만 건드렸을 뿐 raw socket termination 정책을 손대지 않았다 |

TSAN 2-process admission 3건(round-robin·MIXED·reconnect)은 baseline
`472f66a32`(수정 미포함) 재현으로 delta-무관이 이미 확정된 사항이며, 이번
delta도 `mesh_wire_admission.cpp`를 건드리지 않아 재실행하지 않았다.

## 6. 최종 판정

blocker 0, high 1(CS8-I1-01), medium 2(CS8-I1-02, CS8-I1-03), low 0 — I1이
`NOT CLEAN`이다. I2·I3는 `CLEAN`이다. clean gate는 blocker·high·medium 0과
세 축 CLEAN을 동시에 요구하므로 충족하지 못한다.

iteration 7의 registry lifecycle pinning은 그것이 정확히 겨냥한
shutdown/destroy 재진입 5종 interleaving을 완전히 막았고(§3, 신규 test
`test_destroy_waits_for_pinned_shutdown` 포함 ASAN·TSAN 전수 재확인), OOM
장벽 확장도 iteration 7이 지목한 completion/reply 경로 대부분과 ingress·
timer 스레드의 `std::terminate()` 위험을 실제로 제거했다(§1, §2). 다만 전체
scope를 다시 훑는 과정에서, 두 수정 모두 "겨냥한 특정 함수/경로"는 닫았지만
"같은 근본원인이 적용되는 나머지 영역"은 좁게 남겨뒀다는 같은 패턴의 잔여
결함을 발견했다: registry pin은 shutdown/destroy 두 함수 밖의 58개 다른
`as_mesh_node()` 호출 지점(§11이 명시적으로 thread-safe라 규정한 API
대부분과 timer 콜백)에는 적용되지 않았고(CS8-I1-01, high), OOM 외곽 장벽은
27개 공개 submit 진입점 중 2개를 놓쳤으며(CS8-I1-02, medium), 메시지 단위
장벽은 프로세스 종료는 막았지만 그 밑에서 operation bookkeeping이 이미
지워진 뒤의 완료 통지 유실은 막지 못했다(CS8-I1-03, medium).

CORE REVIEW NOT CLEAN
