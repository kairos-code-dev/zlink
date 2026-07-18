# S8 CPP bindings 리뷰 iteration-2 — R2 (Claude Sonnet) progress

## 절차
1. prompt.md 정독 (byte-identical, iteration-2 공통).
2. scope 확인: 시작 123 files, aggregate SHA-256
   `c0cfcd3d7c45af4e7b089ef74dc83b5a8d02fcee41bc8242c6de2df785b73a12` — 일치.
   HEAD(680ea049f)와 review 대상 commit(de299e184) 사이에 scope 경로 변경 없음(git log 확인,
   working tree도 scope 경로에 diff 없음) → 현재 checkout을 그대로 정적 대조에 사용.
3. iteration-1 finding-ledger.ko.md + 두 리뷰(`codex`, `claude-sonnet`) 정독, F1~F11/I2-1~3/I3-1
   각각의 원래 위치·근거 파악.
4. commit de299e184 소스를 직접 읽어 각 finding을 하나씩 대조:
   - F1(native_message_parts.hpp `with_borrowed_native_parts`/`submit_borrowed_message_array`
     신설, mesh_node/spot/actor/stream_session/dispatch 전 소비처가 이걸 사용) → 해소
   - F2(dispatch.cpp claim_t::recv_batch가 더 이상 성공 시 무효화 안 함, release()만
     ZLINK_CLOSE_OK에서 무효화) → 해소
   - F3(sample_common.hpp `mesh_drain_claim`이 batch 전체 순회) → 해소
   - F4(dispatch.hpp에 kind_data + typed variant 5종, dispatch_access.hpp가 배치 독립 복사) → 해소
   - F5(mesh_node.hpp send/request/publish에 mesh_metadata_t 인자 + publish_detail_t* out,
     store_publish_detail이 네이티브 7필드 전부 복사 확인) → 해소
   - F6(actor.hpp에 4상태 전이 RAII 토큰 + prepare/commit/activate/abort 전체 구현) → 해소
   - F7(mesh_node.hpp peer_channels/set_nodrop(publisher)/set_router_hwm 등 옵션 대칭) → 해소
   - F8(sample_common.hpp `mesh_start_single_node`가 set_routing_id 호출) → 해소
   - F9(mesh_node.cpp set_ready_handler가 unregister-then-swap, trampoline이 try/catch로
     예외 흡수) → 해소
   - F10/I2-3(mesh_node.cpp/spot.cpp/stream_session.cpp move-assignment는 swap으로 수정됐으나
     **소멸자는 원본 코드와 byte-identical하게 busy 결과를 계속 버림** — 4개 타입(mesh_node_t,
     spot_t, mesh_node_publisher_t, stream_session_service_t) 전부 동일 패턴. git diff로
     소멸자 코드가 2f34aacf2 대비 미변경임을 확인. → **부분 해소** — move-assign 경로는
     고쳤지만 원 finding이 지목한 "결과 버리고 파괴" 소멸자 패턴 자체는 남음.
   - F11(mesh_node.cpp remote_actor_ref가 empty/255 초과를 invalid_argument로 거부) → 해소
   - I2-1(dispatch-turn: 전용 클래스는 아니지만 claim_t RAII + 전체 batch 순회로 근본 결함
     해소, 새 반례 없음) → 해소로 판단(재오픈 안 함)
   - I2-2(spot_state.hpp의 spot_operation_kind_t가 raw_*/received_*만 남고 죽은 service
     variant·spot_node_t* 필드 제거) → 해소
   - I3-1(zlink.h/common.h/service/actor.h/service/common.h/service/spot.h/zlink_enum.h 6개
     구 vendor 헤더 삭제 확인, option_ids.hpp의 spot_node_option_id 제거, spot_state.hpp의
     죽은 spot_node_t fwd-decl/actor_command_t::node 제거) → 해소
5. no-hit gate 8개 항목(SpotNode/spot_node, route_bridge, subjects, internal_sockets,
   pub/sub 별도 rid, dispatch_workers, recv_actor_part, msg_gets) scoped grep 재실행 —
   전부 0 hit.
6. 전체 scope 신선 재검토(3축):
   - Service 계층 파일 전수 재독(mesh_node.cpp/hpp, spot.cpp/hpp, actor.cpp/hpp,
     stream_session.cpp/hpp, dispatch.cpp/hpp, dispatch_access.hpp, spot_state.hpp,
     detail.hpp, native_message_parts.hpp, mesh_node_models.hpp, actor_models.hpp).
   - 파일 크기 감사(God-file 없음, 최대 747줄 mesh_node.cpp).
   - CMakeLists.txt 재검토 중 `project(zlink_cpp VERSION 9.0.4 ...)` 발견 — core는
     10.0.0(core/include/zlink/common.h 확인). `write_basic_package_version_file(...
     COMPATIBILITY SameMajorVersion)`가 이 잘못된 9.0.4/major=9를 그대로 패키징에 사용 →
     신규 I3 finding.
   - `bindings/cpp/include/zlink_errno.h`를 `core/include/zlink_errno.h`와 diff —
     EALREADY/EDEADLK/ESHUTDOWN, ZLINK_REQUEST_BACKPRESSURED, ZLINK_RECV_BUFFER_TOO_SMALL,
     ZLINK_RECV_INVALID_STATE, ZLINK_CONNECT_AUTH_FAILED, ZLINK_CONFIG_CONFLICT/
     BUFFER_TOO_SMALL/BUSY 8개 최신 errno 누락된 구버전 복제본. 어떤 bindings/cpp 소스도
     직접 include 안 함(전부 core 버전이 include-order로 승리) — I3-1이 지목한 6개
     stale vendor header와 동일 패턴의 **누락된 7번째 파일**. → 신규 I3 finding.
   - 15개 샘플 재확인: node를 spot/publisher/session보다 먼저 선언(역순 파괴로 안전),
     명시적 close()/busy 처리 호출 0건 → F10 잔여 결함이 현재 샘플에서는 발현되지 않음
     (정상 사용 패턴에서는 숨겨짐)을 확인.
7. 최종 판정 작성.

## 결론
I1: finding 1건(F10/I2-3 부분 미해소). I2: finding 1건(동일 root-cause, 관점만 다름).
I3: finding 2건(zlink_errno.h stale dup, CMakeLists.txt version 9.0.4).
BINDINGS REVIEW NOT CLEAN.
