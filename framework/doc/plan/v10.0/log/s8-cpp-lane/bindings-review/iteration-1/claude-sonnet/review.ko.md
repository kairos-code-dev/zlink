# S8 CPP bindings 전환 리뷰 — iteration 1 — R2 (Claude Sonnet)

## 1. Scope 확인

- 시작: 129 files, aggregate SHA-256 `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023` (일치)
- 종료: 129 files, aggregate SHA-256 `e1adbf3407a4f1483c9ff87d7dd49dd2f199a1013da6d7cbc34a3086acaff023` (일치, 변경 없음)
- `git status --porcelain`으로 scope 경로에 어떤 수정도 없었음을 재확인.
- 검토는 소스 정적 대조로만 수행. build/테스트/sanitizer/package 생성 실행 없음. 실행 증거는 manifest §2(라이브러리+15 samples compile+link green, rc=0)만 사용.

## 2. 축별 결과

### I1 — 계약 구현 일치

**Finding 1** `[I1][blocker]` `bindings/cpp/samples/sample_common.hpp:318-325` (`mesh_start_single_node`) — Core 10.0.0은 `mesh_node_start()` 전에 non-empty routing_id를 요구하는데(`core/src/api/mesh/mesh_node_api.cpp:373` `if (node->routing_id.empty () || node->bind_endpoint.empty () || node->channels.empty ()) { errno = EINVAL; return ZLINK_CONFIG_INVALID_STATE; }`, spec `core/doc/spec/core/service/01-mesh-node.md` §3 "`start` requires a routing ID, bind endpoint, and at least one ChannelName"), `mesh_start_single_node()`는 `set_bind()`→`add_channel_name()`→`start()` 순서만 밟고 `mesh_node_t::set_routing_id()`를 한 번도 호출하지 않는다. `mesh_node_t::start()`는 실패 시 `config_error_t`를 던진다(`bindings/cpp/src/Runtime/Service/mesh_node.cpp:148-152`). `set_routing_id()`는 정확히 구현·노출되어 있음을 확인했다(`mesh_node.cpp:130-134`, `zlink_set_routing_id(_impl->handle, ...)` 호출) — 문제는 헬퍼가 그것을 쓰지 않는다는 것.
  - 영향 범위: CMake에 등록된 15개 sample 중 `mesh_start_single_node()`를 사용하는 9개 전부 — `spot_pubsub_example.cpp`, `spot_rpc_example.cpp`, `spot_timer_example.cpp`, `actor_queue_example.cpp`, `actor_room_example.cpp`, `actor_room_server_sample.cpp`, `actor_sequential_example.cpp`, `actor_single_player_queue_sample.cpp`, `actor_gateway_relay_sample.cpp`. 이 9개는 첫 `node.start()` 호출에서 uncaught `zlink::config_error_t` 예외로 즉시 종료된다. compile+link만으로는 드러나지 않는 결함이며, coordinator가 기록한 실행 증거 범위(compile+link green)를 벗어난다.
  - 근거: `core/src/api/mesh/mesh_node_api.cpp:360-397` (`zlink_mesh_node_start`), `core/doc/spec/core/service/01-mesh-node.md:163-166`, `bindings/cpp/samples/sample_common.hpp:318-325`, 전체 samples grep(`set_routing_id` 0건).
  - 수정 제안: `mesh_start_single_node()`에 `node_.set_routing_id(...)` 호출 추가(예: 채널명 또는 고정 sample identity 기반 rid).

**Finding 2** `[I1][medium]` `bindings/cpp/include/zlink/Contracts/Service/actor.hpp` — Core의 actor cross-node transfer fence API(`zlink_mesh_node_actor_transfer_prepare/commit/activate/abort`, `core/include/zlink/service/actor.h:214-226`, 타입 `zlink_actor_transfer_prepare_t`/`_result_t`/`_control_t`/`_token_t`/`_id_t`)가 C++ 표면에 전혀 노출되지 않는다. `actor_models.hpp`/`actor.cpp`/`actor.hpp` 전체에 "transfer" 관련 타입·메서드가 0건이다. 이 자체 전환 설계 문서(`framework/doc/plan/v10.0/log/s8-cpp-lane/bindings-transition-design.ko.md:27`)도 "actor.hpp: actor_t가 mesh_node 위임. join/leave/**transfer**"라고 명시했으나 구현되지 않았다.
  - 근거: `core/include/zlink/service/actor.h:214-226`, `bindings/cpp/include/zlink/Contracts/Service/actor.hpp`(전체, transfer 무관), `bindings/cpp/src/Runtime/Service/actor.cpp`(transfer 0건), 설계 문서 자체 진술과의 불일치.
  - 수정 제안: actor 노드 간 transfer fence API를 C++ 표면에 추가하거나, 의도적 축소라면 설계 문서·ledger에 명시적 스코프 결정으로 기록.

**Finding 3** `[I1][low]` `bindings/cpp/include/zlink/Contracts/Service/mesh_node.hpp` — Core의 `zlink_mesh_node_peer_channels()`(특정 peer의 channel 이름·weight 목록 조회, `core/include/zlink/service/mesh_node.h:241-247`)가 C++에 노출되지 않는다. `mesh_node_t`는 `status()`/`peers()`만 제공하며 `peers()`가 반환하는 `mesh_peer_entry_t`는 `channel_count`만 있고 개별 channel 이름·weight는 없다.
  - 근거: `core/include/zlink/service/mesh_node.h:241-247`, `bindings/cpp/include/zlink/Contracts/Service/mesh_node.hpp` 전체(대응 메서드 없음), `bindings/cpp/src/Runtime/Service/mesh_node.cpp`(`zlink_mesh_node_peer_channels` 호출 0건).
  - 수정 제안: `mesh_node_t::peer_channels(peer_rid, generation)` 추가 또는 의도적 축소로 문서화.

**Finding 4** `[I1][low]` `bindings/cpp/include/zlink/Contracts/Service/mesh_node.hpp` — `spot_t`는 `set_nodrop()`으로 `zlink_spot_set_publish_option`/`get_publish_option`(NODROP)을 노출하지만, 같은 옵션 쌍인 `zlink_mesh_node_publisher_set_option`/`get_option`(`core/include/zlink/service/mesh_node.h:210-219`)은 `mesh_node_publisher_t`에 대응 메서드가 없다. `mesh_node_publisher_t`는 `publish()`/`close()`만 제공한다(`mesh_node.cpp:511-548` 확인, set/get option 구현 없음).
  - 근거: `core/include/zlink/service/mesh_node.h:210-219`, `bindings/cpp/src/Runtime/Service/mesh_node.cpp:511-570`(대응 메서드 부재), `spot.cpp:197-203`(spot 쪽은 존재)와의 비대칭.
  - 수정 제안: `mesh_node_publisher_t::set_nodrop()` 추가로 spot 쪽과 대칭 맞춤.

**Verdict: NOT CLEAN**

### I2 — POSD·DDD 리팩터링

**Finding**: 없음.

Evidence: `bindings/cpp/src/Runtime/Service/*.cpp` 파일 크기는 105~571줄로 God-file 없음(design 문서의 파일군 분해와 일치). `mesh_node_t`/`spot_t`/`actor_t`/`stream_session_service_t`/`claim_t`/`ready_batch_t`/`receive_batch_t`는 모두 pimpl 또는 opaque 핸들로 Core `void*`를 캡슐화하고, 공개 헤더(`Contracts/Service/*.hpp`)에는 `_handle`이 private 멤버로만 존재하며 public getter가 이를 raw로 반환하지 않는다(`dispatch.hpp:236,264` 확인). RAII 소유권(claim/batch move-only, 소멸자에서 release/destroy)이 `dispatch.hpp`/`spot.cpp`/`mesh_node.cpp`에서 일관됨. `actor_t`는 소유 mesh_node에 대한 non-owning view임을 문서화(`actor.hpp:29-30`)하고 `friend class mesh_node_t`로 생성 경로를 제한. `operation_builder_base.hpp`는 `operation_contracts.hpp`(raw socket 요청 계층)와 `spot_state.hpp`에서 실사용 확인, 죽은 템플릿 아님. 반복되는 `rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc` 캐스팅 패턴은 여러 `send_to_*`/`request_to_*` 구현에서 보이나, 이는 얇은 어댑터의 일관된 idiom이며 책임 경계 위반이나 정보 은닉 붕괴로 보지 않음(문체 수준).

**Verdict: CLEAN**

### I3 — 정리 완결성

**Finding (root-cause family)** `[I3][high]` — S8 CPP 전환에서 pre-10.0.0(SpotNode 시대, v9.0.4) 잔존 코드가 `bindings/cpp/include`와 `bindings/cpp/src` 전반에 정리되지 않고 남아 있다. 이번 전환 커밋(2f34aacf2) 및 그 이전 S8 lane 커밋들은 `mesh_node.h`/`dispatch.h`/`stream_session.h`(core와 byte-identical)를 갱신했지만, 같은 디렉터리의 다음 파일들은 갱신되지 않았다:

  - `bindings/cpp/include/zlink.h` — `ZLINK_VERSION_MAJOR 9`/`MINOR 0`/`PATCH 4`(core는 10.0.0), `#include <zlink/service/actor.h>`/`spot.h`/`common.h`만 포함하고 `mesh_node.h`/`dispatch.h`/`stream_session.h`는 누락된 구버전 umbrella 헤더.
  - `bindings/cpp/include/zlink/common.h` — `ZLINK_VERSION_MAJOR 9`/`PATCH 4` (core `common.h`는 10/0).
  - `bindings/cpp/include/zlink/service/actor.h`(109줄, core 232줄과 완전히 다른 내용) — `zlink_actor_join_info_t`, `zlink_actor_route_t`, `ZLINK_ACTOR_ID_MAX 256`(core는 `255u`+1 규약) 등 구버전 전용 타입.
  - `bindings/cpp/include/zlink/service/common.h`(24줄, core 28줄과 다른 내용) — `zlink_actor_ref_t` 대신 `zlink_spot_kind_t`를 정의하는 구버전.
  - `bindings/cpp/include/zlink/service/spot.h`(455줄, core 139줄) — SpotNode 시대 전체 표면 잔존: `zlink_spot_node_new/destroy/entry_spot/spot_lookup/spot_get_or_new/actor_new/actor_lookup/actor_destroy/actor_join_spot/actor_join_entry_spot/actor_leave_spot/actor_recv_part/actor_send_bound_session_msg/actor_forward_bound_session_part/send_to_actor/request_to_actor/actor_reply_no_bind/actor_bind_remote_session/actor_close_bound_session/set_router_bind/set_pub_bind/**set_pub_routing_id/set_sub_routing_id**/connect_peer/connect_peer_rid/disconnect_peer/disconnect_peer_rid`, `zlink_spot_route_bridge_*`(**route_bridge**), `zlink_spot_node_subjects`(**subjects**), `zlink_spot_node_internal_sockets`(**internal_sockets**), `zlink_spot_node_publisher_*`, `zlink_spot_node_status_t`/`peer_entry_t`/`spot_entry_t`/`actor_entry_t`.
  - `bindings/cpp/include/zlink_enum.h` — `zlink_spot_node_mode_t`, `zlink_spot_node_socket_owner_t`, `zlink_spot_node_option_t`(`ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN/MAX` 포함, **dispatch_workers**), `zlink_spot_node_state_t`. core의 신규 errno(`ZLINK_REQUEST_BACKPRESSURED`, `ZLINK_RECV_BUFFER_TOO_SMALL`, `ZLINK_RECV_INVALID_STATE`, `EALREADY`/`EDEADLK`/`ESHUTDOWN`)는 반영되지 않음.
  - `bindings/cpp/src/Runtime/Options/option_ids.hpp:91-99` — 사용처 0건인 죽은 `enum class spot_node_option_id`(`dispatch_workers_min = 13842`, `dispatch_workers_max = 13843` 포함).
  - `bindings/cpp/src/Runtime/Service/spot_state.hpp:22,50,138` — 정의된 곳이 scope 전체에 없는 죽은 `class spot_node_t;` 전방선언과, 그 타입을 쓰는 죽은 멤버 `actor_command_t::node`(`spot_node_t *node`, 전체 scope에서 대입·역참조 0건).
  - `bindings/cpp/include/zlink/Contracts/Core/routing_id.hpp:95`, `Eventing/poller.hpp:20`, `Messaging/message.hpp:23,113` — 공개 계약 헤더에 남은 죽은 `class spot_node_t;` 전방선언·`friend class service::spot_node_t;`(정의 없음, 실사용 없음).
  - `bindings/cpp/include/zlink/Contracts/Eventing/events.hpp:56-61` — scope 전체에서 완전히 미사용인 `enum class monitor_target_kind_t`(선언 외 참조 0건)에 `spot_node = 3` 열거자.

  이 파일들은 CMake `target_include_directories`가 `${ZLINK_CORE_DIR}/include`를 `${CMAKE_CURRENT_LIST_DIR}/include`보다 먼저 PUBLIC으로 추가하므로(`bindings/cpp/CMakeLists.txt`) 실제 컴파일에서는 core의 최신 헤더가 우선 채택되어 컴파일 자체는 green이지만, 129-file scope 안에 여전히 존재하는 소스 파일이며 `install(DIRECTORY include/ ... PATTERN "*.hpp")`가 `*.h`를 걸러내 패키징에서는 빠지더라도 저장소·git scope에는 남아 향후 직접 include나 IDE 인덱싱, 다른 소비자의 오해를 유발할 수 있다.

  - 근거: scoped grep(전체 129-file scope) — `SpotNode|spot_node` HIT(다수), `route_bridge` HIT, `subjects` HIT, `internal_sockets` HIT, `dispatch_workers` HIT. 파일별 diff/개별 읽기로 확인.
  - 수정 제안: 위 파일들을 core의 현재 헤더와 재동기화하거나(umbrella/vendor 헤더 정책이 필요하면), 더 이상 필요 없다면 삭제. `spot_state.hpp`의 죽은 `spot_node_t` 전방선언·`actor_command_t::node` 멤버, `option_ids.hpp`의 `spot_node_option_id`, `events.hpp`의 `monitor_target_kind_t`(또는 그 `spot_node` 열거자만) 제거.

**Verdict: NOT CLEAN**

## 3. 폐기 개념 no-hit 판정 (전체 129-file scope, scoped grep)

| 개념 | 결과 | 근거 |
|---|---|---|
| SpotNode / spot_node | **HIT** | `bindings/cpp/include/zlink/Contracts/Core/routing_id.hpp:95`, `Eventing/poller.hpp:20`, `Messaging/message.hpp:23,113`, `Eventing/events.hpp:61`, `zlink/service/spot.h`(다수), `zlink_enum.h`(다수), `src/Runtime/Options/option_ids.hpp:91`, `src/Runtime/Service/spot_state.hpp:22,50,138` |
| route_bridge | **HIT** | `bindings/cpp/include/zlink/service/spot.h:257-314` (`zlink_spot_route_bridge_*`, `ZLINK_SPOT_ROUTE_BRIDGE_*`) |
| subjects | **HIT** | `bindings/cpp/include/zlink/service/spot.h:431` (`zlink_spot_node_subjects`) |
| internal_sockets | **HIT** | `bindings/cpp/include/zlink/service/spot.h:436` (`zlink_spot_node_internal_sockets`) |
| pub/sub 별도 routing_id | **HIT** | `bindings/cpp/include/zlink/service/spot.h:228,231` (`zlink_spot_node_set_pub_routing_id`, `zlink_spot_node_set_sub_routing_id`) |
| dispatch_workers | **HIT** | `bindings/cpp/include/zlink_enum.h:199-200` (`ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN/MAX`), `bindings/cpp/src/Runtime/Options/option_ids.hpp:97-98` (`dispatch_workers_min/max`, 죽은 코드) |
| recv_actor_part | no-hit | 전체 scope grep 0건 |
| msg_gets | no-hit | 전체 scope grep 0건 |

모든 HIT 항목은 위 I3 root-cause family의 하위 위치이며, 실제 컴파일 경로(core/include가 include 우선순위에서 승리)에서는 소비되지 않지만 129-file scope 안에 현존하는 소스로서 no-hit 판정을 통과하지 못한다.

## 4. 최종 판정

I1 NOT CLEAN, I2 CLEAN, I3 NOT CLEAN.

BINDINGS REVIEW NOT CLEAN
