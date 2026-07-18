# S8 CPP bindings 리뷰 iteration-3 — R2 (Claude Sonnet) progress

- Scope hash 확인: 121 files, `dbe1085fb6e612e8ff7013dd8102c0ddd8571ff509c6e147c94bb884f7e426be` — 시작/종료 일치.
  HEAD(`cf7027b26`)와 대상 commit `f9b6ba50c` 사이 scope 4경로 diff 없음(`git diff` 무출력) → 현재
  checkout을 `f9b6ba50c` 스냅샷과 동일하게 취급.
- iter-2 finding(C2-0..C2-4) 5건 전부 소스 직접 대조로 해소 확인:
  - C2-0: `CMakeLists.txt:2` `VERSION 10.0.0` 확인.
  - C2-1: `zlink_errno.h`, `pimpl_move.hpp` 둘 다 파일 삭제 확인(`ls` ENOENT).
  - C2-2: `routing_id.hpp`의 `actor_*_operation_t` dangling fwd-decl 10개 제거, dead helper
    5개(`invalidate_claim`/`make_spot_request_progress`/`move_assign_pimpl_and_close`/
    `native_request_timeout_ms`/`set_spot_spot_send_context`) + second-rid helper 2개 전부
    scope 전체 grep 0건, `spot_t::impl::last_error` 필드 제거 확인.
  - C2-3: `spot_command_t`가 `request_seq`만 남고 `spot/topic/channel_name/target` 제거,
    `routing_target_t::second_rid` 3인조 완전 제거 확인.
  - C2-4: 4개 RAII 타입(`mesh_node_t`/`spot_t`/`mesh_node_publisher_t`/`stream_session_service_t`)
    소멸자가 전부 `detail::report_close_on_destroy()`로 전환. 구현은 `close_result_t::ok` 외
    전부 `fprintf(stderr, ...)` + `assert(false)`(debug만)로 fail-loud 신호 — "무신호 누수" 해소
    확인. Ledger가 명시한 두 옵션(제어블록 재설계 / 최소 침습 신호) 중 후자로 해소, ledger
    문구와 일치.
- iter-2 diff 전체(`git diff f9b6ba50c~1 f9b6ba50c`, 16파일) 라인 단위 재검토: 의도치 않은 부작용
  없음 확인 — 단, `received_access.hpp`에서 `set_spot_spot_send_context`(유일한 setter) 제거 후
  `received.hpp:70`의 `send_context_kind_t::spot_spot` enumerator가 고아로 남은 것 발견(scope
  전체에서 선언 외 0건 참조) → I3 finding.
- fresh 전체 3축 재검토 중 `received_access.hpp`의 `submit_direct_send`/`submit_direct_reply`가
  `send_context_kind_t::router_spot`을 스위치에서 처리하지 않고 `default: break`로 빠지면서도
  함수가 `true`(처리됨)를 반환하는 구조적 결함 발견 — router_spot 컨텍스트로 받은 단일-파트
  메시지의 `received_t::send()`/`.reply()`가 항상 `invalid_argument`로 실패, 원래 존재하는
  멀티파트 폴백 경로로 넘어가지 못함. `router_socket_t::recv()`(raw ROUTER, spot 라우팅 트래픽
  수신 시 `spot_rid` 존재)에서 실제로 도달 가능한 경로임을 `socket.cpp:199-214`,
  `router.cpp:29-46`로 추적 확인. 15-sample 스모크는 raw `router_socket_t`를 쓰는 샘플이 1개뿐이고
  spot_rid 없는 순수 dealer/router 시나리오라 이 경로를 노출하지 않음(기존 evidence로 못 잡힘).
  iter-2 diff와 무관한 기존(pre-existing) 결함 — 오늘 처음 발견. → I1 finding.
- no-hit 10-키워드 게이트(SpotNode/spot_node, route_bridge, subjects, internal_sockets,
  set_pub_routing_id, set_sub_routing_id, dispatch_workers, recv_actor_part, msg_gets) 전부
  재확인 0 hits.
- `TODO/FIXME/XXX` 잔존 0건, `git status --porcelain` scope 무수정.
- 산출물만 작성, build/실행/파일 수정 없음.

## 최종 판정
I1 NOT CLEAN(1 finding, medium — router_spot 단일-파트 send/reply 실패), I2 CLEAN(C2-4 해소로
child-lifetime 논거 소진), I3 NOT CLEAN(1 finding, low — 고아 `spot_spot` enumerator).

BINDINGS REVIEW NOT CLEAN
