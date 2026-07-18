# S8 CPP bindings 전환 리뷰 — iteration 3 — R2 (Claude Sonnet)

## 1. Scope 확인

- 대상 commit: `f9b6ba50c`
- 검토 방식: 정적 소스 대조만 수행. build, 테스트, sanitizer, package 생성은 수행하지 않았다.
  실행 증거는 manifest §2(라이브러리 + 15 samples compile+link+run green, no-hit ZERO)만 사용했다.
- `git ls-files bindings/cpp/include bindings/cpp/src bindings/cpp/samples
  bindings/cpp/CMakeLists.txt | grep -v native/ | LC_ALL=C sort | xargs sha256sum | sha256sum`
  결과: **121 files, `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be`** — 시작·종료
  모두 prompt 명시 값과 일치.
- 현재 HEAD(`cf7027b26`)와 대상 commit `f9b6ba50c` 사이 scope 4경로에 diff 없음(`git diff
  f9b6ba50c cf7027b26 --stat -- <scope>` 무출력), `git status --porcelain -- <scope>`도 무출력 →
  현재 checkout을 `f9b6ba50c` 스냅샷과 동일하게 취급해 정적 대조에 사용했다. 리뷰 중 scope 파일
  수정 없음.

## 2. iteration-2 finding 해소 판정

| ID | 판정 | 근거 |
|---|---|---|
| C2-0 (CMakeLists VERSION) | **해소** | `bindings/cpp/CMakeLists.txt:2` `project(zlink_cpp VERSION 10.0.0 LANGUAGES CXX)` 확인. |
| C2-1 (zlink_errno.h 구버전 중복) | **해소** | `bindings/cpp/include/zlink_errno.h` 파일 삭제 확인(`ls` ENOENT). 부수적으로 iter-2 diff에서 `pimpl_move.hpp`(45줄)도 함께 삭제됨을 확인 — scope 파일 수 123→121과 일치. |
| C2-2 (dangling fwd-decl·미호출 헬퍼·dead 멤버) | **해소** | `routing_id.hpp:95-108`(구 103-112)의 `actor_*_operation_t` 전방선언 10개 전부 제거 확인(현재 해당 위치는 `pub_socket_t`/`xpub_socket_t`/`timer_t`/`service::spot_t`/`send_operation_t`/`send_submit_operation_t`/`request_operation_t`/`request_submit_operation_t`/`request_callback_submit_operation_t`/`reply_operation_t`/`reply_submit_operation_t`만 남음, 전부 scope 내 실제 정의 존재). 미호출 헬퍼 6개(`invalidate_claim`, `make_spot_request_progress`, `move_assign_pimpl_and_close`, `native_request_timeout_ms`, `set_spot_spot_send_context`, `cache_second_rid_native`/`target_second_rid_native`) 전부 scope 전체 grep 0건. `spot_t::impl::last_error`(`spot_impl.hpp`)도 제거되어 `impl`은 `handle` 필드 하나만 남음. `make_request_progress_callback`(제거된 `make_spot_request_progress`가 감싸던 대상)은 `operation_detail.hpp:23`의 다른 래퍼(`native_request_timeout_ms`가 아닌 소켓 경로 전용)에서 여전히 사용 중임을 확인해 과잉 제거가 아님을 검증했다. |
| C2-3 (`spot_operation_state_t` dead 필드) | **해소** | `spot_state.hpp:93-98` `spot_command_t`가 `uint64_t request_seq`만 남고 `spot/topic/channel_name/target` 4필드 전부 제거. `routing_target_t::second_rid`/`second_rid_native_cache`/`has_second_rid_native_cache` 3인조와 접근 헬퍼 2개(`cache_second_rid_native`/`target_second_rid_native`) 전부 제거. `spot.reset()` 호출부(`spot_state.hpp:234`)는 여전히 유효(라이브 `request_seq` 필드에 대해 동작). |
| C2-4 (소멸자 close-busy 무신호) | **해소** | `mesh_node_t`(`mesh_node.cpp:109-114`), `spot_t`(`spot.cpp:74-79`), `mesh_node_publisher_t`(`mesh_node.cpp:675-681`), `stream_session_service_t`(`stream_session.cpp:59-65`) 4개 소멸자 전부 `(void) zlink_*_destroy(...)` 폐기 패턴에서 `detail::report_close_on_destroy(type_name, close_result_t)` 호출로 전환. `detail.hpp:104-115`의 구현은 `close_result_t::ok`가 아니면 `fprintf(stderr, ...)`(모든 빌드에서 fail-loud) + `assert(false)`(debug 빌드에서 즉시 중단)로 신호를 남긴다. Ledger가 제시한 두 해소 경로(부모/자식 제어블록 재설계 vs. 계약을 깨지 않는 최소 침습 신호) 중 후자를 택했고, "무신호 누수" 문제(silent leak)는 확실히 제거됐다 — non-ok 결과가 이제 관측 가능(stderr 로그 + debug assert)하다. 실제 자원 회수 자체(누수 방지)는 여전히 선언 순서 관례에 의존하지만, 이는 ledger가 명시적으로 수용한 최소 침습 대안의 성격상 당연한 트레이드오프이며 재개 사유가 아니다. |

5건 전부 새 반례 없이 해소 확인. 재개하지 않는다.

## 3. 전체 scope 재검토 (3축)

### I1 — 계약 구현 일치

**Finding 1** `[I1][medium]` `bindings/cpp/src/Runtime/Messaging/received_access.hpp:59-91`
(`submit_direct_send`) 및 `:93-124`(`submit_direct_reply`) — 두 함수의 `switch
(received_._send_context_kind)`가 `case socket_rid`만 명시적으로 처리하고
`send_context_kind_t::router_spot`은 `default: break`로 빠지는데, 이때 `rc`는 초기값
`ZLINK_SUBMIT_INVALID_ARGUMENT`로 남고 함수는 여전히 무조건 `return true`(= "직접 경로가 처리함")를
반환한다. 호출부인 `send_operations.cpp:84-91`(`send_submit_operation_t::submit()`)와
`reply_operations.cpp`의 대응 위치는 이 `true` 반환을 "네이티브 호출이 실제로 일어났고 결과가
유효하다"는 신호로 해석해, `result == submit_result_t::ok`가 아니면 곧바로
`throw submit_error_t(result, result_errno)`로 예외를 던진다 — 원래대로라면 `submit_direct_send`가
`false`를 반환해 그 아래 멀티파트 폴백 경로(`submit_send`/`submit_reply`, kind에 무관하게 저장된
handle+routing_id로 항상 성공적으로 native 호출을 재구성함)로 넘어가야 하는데, `router_spot`
kind에서는 이 폴백에 도달하지 못하고 항상 `EINVAL`로 실패한다.
  - 재현 조건: `router_spot` context는 이론적 죽은 분기가 아니라 실제로 도달 가능하다.
    `router_socket_t::recv(received_t&, flags_)`(`router.cpp:29-46`)는 raw ROUTER 소켓으로 받은
    메시지에 `spot_rid`가 붙어 있으면(spot 라우팅을 거쳐 전달된 트래픽을 raw router가 받는
    경우) `set_router_spot_send_context`를 호출한다(`socket.cpp:199-214`의
    `socket_t::receive(..., attach_routed_send_context_)` 경로에서 `envelope.source_spot_rid`가
    채워지면 `router_spot`로 태깅). 이렇게 받은 `received_t`에 대해 사용자가 단일-파트
    `.send()`나 `.reply()`(가장 흔한 hot-path 형태)를 호출하면 매번 `invalid_argument`
    예외가 던져진다 — 여러-파트로 보내면 (single-part fast path를 타지 않으므로) 정상 동작한다.
  - coordinator manifest의 15-sample 실행 증거는 이 경로를 노출하지 않는다: raw
    `router_socket_t`를 쓰는 샘플은 `dealer_router_recv_sample.cpp` 1개뿐이고, 그마저 순수
    dealer/router 시나리오(spot_rid 없음, `router_spot` context 미도달)라 이 결함이 컴파일·링크·
    스모크 실행으로는 드러나지 않는다.
  - iteration-2 diff와 무관한 기존 결함이며(`received_access.hpp`의 iter-2 변경분은 별개의 죽은
    `set_spot_spot_send_context` 제거뿐), iteration-1·iteration-2 어느 리뷰에서도 이 파일의 이
    스위치 구조는 지적되지 않았다 — fresh 전체 재검토에서 처음 발견.
  - 근거: `received_access.hpp:59-91,93-124`, `send_operations.cpp:59-98`,
    `router.cpp:29-46`, `socket.cpp:178-214`.
  - 수정 제안: `submit_direct_send`/`submit_direct_reply`가 `router_spot`을 처리하도록 스위치에
    `case router_spot:` 분기를 추가(handle 재사용 자체는 kind 무관하게 이미 저장돼 있으므로
    `socket_rid`와 동일한 native 호출로 처리 가능해 보인다)하거나, 최소한 처리하지 못한
    kind에서는 `return false`로 폴백을 허용해 멀티파트 경로가 항상 정답을 내도록 고친다.

**나머지**: iter-2 diff에 포함된 16개 변경 파일 전체를 라인 단위로 재대조했고(제거된 필드·헬퍼가
scope 내 다른 곳에서 여전히 필요했는지, 대체 경로가 있는지), Core `zlink/service/*.h` 대비
mesh_node/spot/stream_session/actor의 옵션·쿼리·transfer-fence·publish-detail·outbound-metadata
표면은 iteration-2에서 이미 1:1 대조가 끝난 상태로 이번 재검토에서 새 불일치를 추가로 찾지
못했다.

**Verdict: NOT CLEAN**

### I2 — POSD·DDD

iteration-2가 지목한 I2 finding(=I1 finding 1, `mesh_node_t`가 child lifetime을 추적하지 않는 문제)은
C2-4 해소로 소진되었다: ledger는 두 해소 경로(제어블록 재설계 또는 계약을 깨지 않는 최소 침습
신호) 중 하나를 명시적으로 수용했고, coordinator는 후자를 택해 "무신호 누수"라는 관측 가능한
결함 자체를 제거했다(§2 C2-4). 남은 것은 "선언 순서를 API가 강제하지 않는다"는 설계 선택인데,
이는 ledger가 이미 대안으로 수용한 트레이드오프이지 새로운 결함이 아니다. §3 I1 finding 1
(`received_access.hpp`의 스위치 미완결)은 캡슐화·소유권 경계 위반이 아니라 단순 로직 누락이라
I2가 아닌 I1로 분류했다.

fresh 재검토에서 파일 크기(`Runtime/Service/*.cpp` 105~575줄 범위 유지, God-file 없음), pimpl/opaque
handle 캡슐화, RAII move-only 소유권 규칙은 iteration-1·2와 동일하게 일관됨을 재확인했다.

**Verdict: CLEAN**

### I3 — 정리 완결성

**Finding 1** `[I3][low]` `bindings/cpp/include/zlink/Contracts/Messaging/received.hpp:65-70`
— `enum class send_context_kind_t { none, socket_rid, router_spot, spot_spot }`의 `spot_spot`
enumerator가 iter-2에서 그 유일한 setter(`received_access.hpp`의 `set_spot_spot_send_context`,
C2-2로 제거)를 잃고도 enum 자체에는 남아 고아가 됐다. scope 전체 grep(`spot_spot`) 결과 이
선언 한 줄(`received.hpp:70`) 외 0건 — 어디서도 이 값을 설정(setter 없음)하거나 스위치에서
명시적으로 처리(두 `submit_direct_*` 스위치 모두 `case`가 없어 `default`로 흡수)하지 않는다.
이것이 정확히 iteration-3 prompt가 지목한 "iter-2 cleanup이 새로 만든 잔재"의 사례다(제거된
헬퍼의 orphan enum 값).
  - 근거: `received.hpp:65-70`, `received_access.hpp` 전체 grep(`spot_spot` 0건),
    iter-2 diff(`received_access.hpp` -5줄, `set_spot_spot_send_context` 제거).
  - 수정 제안: `spot_spot` enumerator를 삭제(3-value enum으로 축소)하거나, 향후 실제로 이
    context를 생성하는 경로를 추가할 계획이 있다면 그 계획을 문서화하고 최소한 두
    `submit_direct_*` 스위치에서 명시적으로 처리(또는 명시적 미지원으로 주석 처리)한다.

no-hit 게이트(§4)는 8/10(원 목록 8개 + pub/sub 세분 2개 포함 10키워드) 전부 통과 — 이 finding은
no-hit 키워드 매칭 대상이 아니라 별도 발견.

**Verdict: NOT CLEAN**

## 4. 폐기 개념 no-hit 판정 (전체 121-file scope, scoped grep, `native/` 제외)

| 개념 | 결과 | 근거 |
|---|---|---|
| `SpotNode` | no-hit | 0 hits |
| `spot_node` | no-hit | 0 hits |
| `route_bridge` | no-hit | 0 hits |
| `subjects` | no-hit | 0 hits |
| `internal_sockets` | no-hit | 0 hits |
| `set_pub_routing_id` | no-hit | 0 hits |
| `set_sub_routing_id` | no-hit | 0 hits |
| `dispatch_workers` | no-hit | 0 hits |
| `recv_actor_part` | no-hit | 0 hits |
| `msg_gets` | no-hit | 0 hits |

10개 게이트 전부 통과(zero hits).

## 5. 최종 판정

iter-2 finding(C2-0..C2-4) 5건 전부 해소 확인, 재개 없음. fresh 전체 재검토에서 신규 finding
2건 발견: I1 medium 1건(`received_access.hpp`의 `router_spot` 단일-파트 send/reply가 항상
실패 — 스위치 미완결로 폴백 경로 도달 불가, iter-2와 무관한 기존 결함의 첫 발견) + I3 low 1건
(`send_context_kind_t::spot_spot`이 iter-2 cleanup으로 유일한 setter를 잃고 남은 고아
enumerator). I2는 C2-4 해소로 CLEAN.

I1 NOT CLEAN(1 finding, medium), I2 CLEAN, I3 NOT CLEAN(1 finding, low). no-hit 게이트 10/10 통과.

BINDINGS REVIEW NOT CLEAN
