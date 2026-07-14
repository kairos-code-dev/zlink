# C++ G0 공개 계약 및 구현 gap ledger

이 문서는 C++ 정식 공개 계약의 규범 항목을 실제 public symbol, 구현 동작, 실패 test와
연결한다. 정식 계약은 현재 구현의 최소 공통분모로 축소하지 않는다. 현재 상태가 `GAP`인
행은 G1 이후 구현·삭제·검증 작업의 입력이며, 호환 alias를 남기지 않는다.

## 1. 기준

- 기준일: 2026-07-12
- bindings dependency: `zlink_cpp 8.6.4` (`ZLINK_FRAMEWORK_CPP_ZLINK_CPP_VERSION`,
  `find_package(zlink_cpp 8.6.4 EXACT CONFIG REQUIRED)`, local prefix
  `.artifacts/wsl/install/zlink-cpp/8.6.4`, `libzlink_cpp.a` SHA-256
  `e1ca346857ccf550dd17150a8a08dd7fcf4d31bce72558844bb13da75ebb6a43`)
- production 분모: framework include/src 163개, connector/core 44개, extensions 5개 파일
- 정식 언어 문서: 13개 (interface 분모 12 + guide `02-framework-interfaces.ko.md` 제외)
- 공통 E2E 분모: Config 1~11
- baseline 명령: plan §13.2의 configure/build/ctest
  (`build-public-contract-gap`, TESTS/SAMPLES/E2E ON)
- baseline 결과: configure/build exit 0, `ctest` exit 8 (48개 중 10개 실패)
  - `test_cpp_framework_layout_contract`: 공통 `31-session-actor-dispatch.ko.md`가 개정되어
    테스트의 고정 needle(`leaveActor` camelCase 문구)이 더 이상 없음. 정식 spec 경로
    fail-closed 검증 자체는 동작하며, needle을 개정 spec에 맞추는 작업이 G0 항목이다.
  - `test_cpp_framework_http_perf_policy`: HTTP perf 정책 문서 누락 메시지로 실패.
  - `test_cpp_framework_locations_redis`, sample smoke 6개: 이 빌드에 redis-plus-plus가
    없어서 실패/스킵(`redis-plus-plus client is not available in this build`).
    `RowCodecMatchesCommonFixtureBytes`는 redis 없이도 실패하므로 G2에서 원인 분류.
  - `test_cpp_stream_connector`: 배치 실행에서만 실패, 단독 재실행 PASS(transient).
- public contract 변경은 정식 C++ spec의 시그니처를 그대로 구현하며 deprecated/legacy
  adapter를 정식 header에 남기지 않는다.

## 2. 계약 coverage

| ID | 정식 계약 문서 | 적용 범위 | 상태 |
|----|----------------|-----------|------|
| CPP-DOC-001 | `README.ko.md` | 계약 색인, 취소 인자 정책 | 검토 완료 |
| CPP-DOC-002 | `02-framework-interfaces.ko.md` | 전체 public surface 권위 | 검토 완료 |
| CPP-DOC-003 | `02-framework-interfaces.ko.md` | handler 정렬 규칙과 §8.1 gap 표 | 검토 완료 |
| CPP-DOC-004 | `01-system-structure.ko.md` | host/DI/config/lifecycle | 검토 완료 |
| CPP-DOC-005 | `01-system-structure.ko.md` | channel/route/fanout | 검토 완료 |
| CPP-DOC-006 | `02-framework-interfaces.ko.md` | Spot/actor/timer | 검토 완료 |
| CPP-DOC-007 | `02-framework-interfaces.ko.md` | STREAM/session/connector 정합 | 검토 완료 |
| CPP-DOC-008 | `02-framework-interfaces.ko.md` | session relay/bound session | 검토 완료 |
| CPP-DOC-009 | `02-framework-interfaces.ko.md` | monitoring/flow/metrics/drain 표면 | 검토 완료 |
| CPP-DOC-010 | `01-system-structure.ko.md` | Registry 표면 제거 선언 | 검토 완료 (규범 위반 없음) |
| CPP-DOC-011 | `60-http-hosting.ko.md` | HTTP hosting | 검토 완료 |
| CPP-DOC-012 | `61-embedded-http-server.ko.md` | embedded HTTP server | 검토 완료 |
| CPP-DOC-013 | `02-framework-interfaces.ko.md` | 상위 guide; interface 분모 비적용, G7 정합성 검토 | 검토 완료 |

공통 spec 19개는 모든 행의 동작 근거로 함께 적용한다. E2E 문서는 새 public API의 근거가
아니며, 정식 계약을 검증할 scenario와 누락을 식별하는 데만 사용한다.

### 2.1 spec 내부 drift (구현 gap 아님, 문서 정합 항목)

| ID | 위치 | 내용 | 처리 |
|----|------|------|------|
| CPP-SPECDRIFT-001 | `02-framework-interfaces.ko.md` §5 | `destroyActor(actor_ref, actor)` 표기. interface 권위 문서 2개는 `entry_spot_context_t::destroy_actor(TActor &)`로 고정 | interface 문서가 우선. 구현은 `destroy_actor(TActor &)`로 정렬하고 cpp-spot 문구는 G7 문서 정합에서 갱신 |
| CPP-SPECDRIFT-002 | `02-framework-interfaces.ko.md` §11 본문 | `onLeaveActor` 표기 2곳 잔존. `02-framework-interfaces.ko.md` §8.1은 snake_case(`on_leave_actor`)를 목표로 고정 | §8.1이 우선. 본문 표기는 G7 문서 정합에서 갱신 |
| CPP-SPECDRIFT-003 | `02-framework-interfaces.ko.md` §2/§5 예시 | `add_discovery_events`/`add_registry_events`/`add_registry_check` 예시가 남아 있으나 `01-system-structure.ko.md`는 Registry 표면 제거를 선언, 구현은 `add_location_events`/`add_location_check` | registry 제거 선언이 우선. monitoring 예시는 location 계열로 갱신(G7). 구현에 registry 표면을 새로 만들지 않는다 |
| CPP-SPECDRIFT-004 | `02-framework-interfaces.ko.md` §3.1, `02-framework-interfaces.ko.md` §5 | 매핑 표의 error kind `timeout` 행과 write-after-close의 `disconnected` 예외 문구가 §8 enum 목록·§8.1 삭제 표·.NET 기준 enum(0~21)과 상충 | §8.1과 .NET 정합이 우선. timeout/disconnected는 public enum 값이 아니라 경계 의미(표준 관례/내부 상태)로 구현하고 두 문구는 G7에서 갱신 |
| CPP-SPECDRIFT-005 | `04-async-execution-policy.ko.md` §1 vs `config-8-automatic-turn-dispatch.ko.md` ATD-A2 | 정책 문구 "같은 Spot의 보호 상태는 callback 완료까지 무관 callback 접근 금지"와 ATD-A2의 "같은 spot rid 독립 probe가 await 중 완료" 요구가 표면상 상충 | E2E 계약(ATD-A2/B2/C2)과 .NET 구현(AutomaticTurn_Allows_Later_Work)이 기준: 재진입 금지 보호 단위는 actor/timer mailbox이고 spot serial line은 await 지점에서 양보한다. C++ 구현은 이 해석을 따름. 정책 문구의 보호 단위 명시는 G7 문서 정합 검토로 |
| CPP-SPECDRIFT-006 | `cpp-stream` §2의 `stream_t` 코드 블록(abstract pure virtual + `message_t` 값 전달)이 `handler-interfaces` §8 stream 행(concrete 핸들 + `const zlink::message_t &`)과 상충. handler-interfaces가 목표 선언 정본이므로 cpp-stream 블록을 G7 정합에서 concrete 선언으로 갱신 |

## 3. symbol·동작·test gap

증거의 `INC` = `framework/languages/cpp/framework/include/zlink/framework`,
`SRC` = `framework/languages/cpp/framework/src`.

| ID | 계약 영역 | 현재 증거 | 필요한 구현·삭제 | 먼저 고정할 검증 | 상태 |
|----|-----------|-----------|------------------|------------------|------|
| CPP-G0-ASYNC-001 | one-way `void submit()` 종결자 | `send_call_t`/`route_send_call_t`/`stream_write_call_t`/`bound_session_send_call_t::submit()`이 `result_t<void>` 반환(`INC/contracts/channels/call.hpp:351,396,431`, `channel.hpp:633`); actor send는 `actor_send_call_t::async()`(`INC/contracts/actors/actor.hpp:93`) | 네 send 종결자를 `void submit()`으로, actor send를 `void submit()`으로 교체. local queue 수락 실패는 동기 framework error, 수락 후 실패는 monitoring 관측 | submit 반환형 contract test, bounded queue 수락/거부 unit test | GAP |
| CPP-G0-ASYNC-002 | relay/disconnect `task_t<void>` 완료 계약 | `session_actor_t::relay/notify_disconnected`가 `relay_call_t` 반환(`actor.hpp:569,573`); `bound_session_t::disconnect()`가 send-call 반환(`actor.hpp:381`)이라 `co_await` 불가 | `task_t<void> relay(const zlink::message_t &)`, `task_t<void> notify_disconnected()`, `task_t<void> disconnect()`로 교체. `relay_request` 등 spec 외 멤버는 삭제 목록으로 | 시그니처 contract test, relay/disconnect 완료 의미 unit test | GAP |
| CPP-G0-ASYNC-003 | request/join/worker 단일 `async()` terminator | `request_call_t::yield()`(+token), `channel_yield_request_call_t`, `actor_yield_join_call_t::yield()`, `worker_call_t::yield()/submit(callback)`, `capture_current_serial_yield_turn` 공개(`call.hpp:89,242,270`, `actor.hpp:276`, `INC/contracts/workers/worker.hpp`) | yield/callback 실행 방식 선택 표면 전체 삭제. automatic turn은 runtime이 소유(YD-C1의 detached offload 정렬 유지) | yield symbol 부재 검색 test, ATD 계약 unit test(자기 데드락 회피·직렬성) | GAP |
| CPP-G0-CANCEL-001 | framework 전용 cancellation token 부재 | `cancellation_token_t`/`cancellation_token_source_t` 공개(`INC/contracts/cancellation.hpp:66,96`), yield overload가 token 수락 | 두 타입과 token 수락 overload를 public contract에서 제거. 대체 중단 API는 별도 계약 승인 전 추가 금지 | header export 부재 검증 test | GAP |
| CPP-G0-BLOCK-001 | lifecycle/transfer blocking bridge 제거 | `.result()` bridge: `SRC/runtime/host/actor_gateway_spot_bridge.cpp` 9회, `SRC/runtime/spots/spot_runtime.cpp` 8회+`future.get()`(3663), `SRC/runtime/locations/location_runtime.hpp` 10회, `store_location_resolvers.hpp` 12회; transfer adapter invoker `.result()`(`INC/contracts/spots/spot.hpp:1513,1517`) | coroutine chain `co_await`로 정렬. `.result()`는 C core dispatch callback 최외곽 경계만 허용 | 경계 밖 `.result()` no-hit 검사, transfer/lifecycle async 완료 test | GAP |
| CPP-G0-NAME-001 | Spot lifecycle callback snake_case | detection이 `onCreateActor`/`onLeaveActor`/`onDisconnectActor` 탐색(`spot.hpp:1038,1059,1067`, `spot.hpp:309-310`); `entry_spot_context_t::destroyActor`(`spot.hpp:836`) | `on_create_actor`/`on_leave_actor`/`on_disconnect_actor`/`destroy_actor(TActor &)`로 교체, camelCase 탐지 경로 삭제(이중 탐지 금지) | callback concept contract test, camelCase no-hit 검색 | GAP |
| CPP-G0-ERROR-001 | 공통 계약 오류 집합만 public | `framework_error_kind_t` 22~27 여섯 개(`actor_stale_generation`~`cancelled`)가 C++ 전용 공개값(`INC/contracts/errors/error.hpp:36` 주석 "common promotion is pending"). .NET 기준 enum(`ZLinkFrameworkErrorKind`)은 0~21로 확정이라 17~21(`worker_*`,`actor_location_stale`,`actor_create_rejected`)은 공통과 일치·유지 | 여섯 enumerator를 public enum에서 제거하고 내부 상태로 이동. 경계 의미는 .NET 정합으로 확정: request timeout은 .NET이 언어 표준 `TimeoutException`을 쓰므로 C++도 framework kind가 아닌 표준 관례로 매핑. G1 확정 설계: 내부 `detail::boundary_error_t`(timed_out/shutdown/disconnected/closed/cancelled/stale_generation)를 `framework_exception_t` 비공개 멤버로 보존하고, 공개 경계는 단일 예외형 유지 + `std::error_code code()` 파셋(std::errc 매핑: timed_out→timed_out, shutdown·cancelled→operation_canceled, disconnected→not_connected, closed→connection_aborted, stale_generation→operation_not_permitted; 빈 code=일반 framework 오류). `std::system_error` 이중 throw 대안은 내부 catch(framework_exception_t) 관통으로 폐기. `cpp-framework-interfaces` §3.1 표의 `timeout` kind 행과 `cpp-stream` §5의 `disconnected` 예외 문구는 §8.1·.NET과 상충하는 문서 drift로 G7 정합에서 갱신(CPP-SPECDRIFT-004) | enum 값 목록 contract test(여섯 개) | GAP |
| CPP-G0-SPOTHANDLE-001 | opaque `spot_handle_t`와 두 resolver | public `spot_ref_t` 주소 snapshot 잔존(`INC/contracts/locations/spot_ref.hpp:12`), `spot_handle_t`/`spot_handle_resolver_t`/`actor_spot_handle_resolver_t` 0건; `route_client_t`에 `send_to_spot/request_to_spot` 없음, `spot_context_t::request_to(spot_ref_t)`(`spot.hpp:557`)는 spec의 2-rid 시그니처와 다름 | `spot_handle_t`(spot_rid만 노출, snapshot/refresh 내부) + 두 resolver + route client `send_to_spot/request_to_spot` 구현, context는 `send_to/request_to(node_rid, spot_rid, ...)`로 정렬, `spot_ref_t` 삭제 | handle 표면 contract test, stale→1회 refresh retry unit test, `spot_ref_t` 부재 검색 | GAP |
| CPP-G0-ACTOR-001 | nullable Spot 식별자 단일 기준 | `actor_context_t::is_joined()`(`actor.hpp:417`)만 공개, `std::optional<spot_rid_t> spot_rid()` 없음 | `is_joined()` 삭제, `spot_rid()` 구현 | 시그니처 contract test, membership 전이 unit test | GAP |
| CPP-G0-ACTOR-002 | join 결과 승인/거절 variant | `actor_join_result_t{int result_code; optional<actor_ref_t>; message_t}` 독립 필드(`actor.hpp:180`) | `std::variant<actor_join_accepted_t<T>, actor_join_rejected_t<T>>`로 교체(승인만 필수 actor ref), `actor_join_call_t` 단일 `async()` | variant exhaustiveness test, 모순 상태 표현 불가 검증 | GAP |
| CPP-G0-SPOTCTX-001 | 역할별 context 정리 | `spot_context_t`/`entry_spot_context_t` 분리는 존재(`spot.hpp:517,822`)하나 `run_worker`가 `worker_completion_mode_t` 등 실행 방식 선택을 노출(`spot.hpp:607-681`), Entry 전용 `destroyActor`는 camelCase, `manager()`/`outbound()` 표면은 handler-interfaces §8 표와 대조 검증 필요 | 공통 context worker는 `timeout`+`async()`만, 역할별 유효 작업만 노출 | context member contract test | 닫힘(G1 — NAME-001이 `destroy_actor` snake_case화, ASYNC-003이 worker 실행 방식 선택 제거로 `run_worker`는 `timeout`+`async()`만 노출, context/entry 분리는 기존 구조 확인) |
| CPP-G0-SPOTMGR-001 | async `find_spot`/`list_spots` | `find_spot`/`list_spots` 동기 반환(`spot.hpp:1186-1187`), `close_spot`만 `task_t<bool>` | `task_t<std::optional<spot_info_t>>`/`task_t<std::vector<spot_info_t>>`로 비동기화 | 시그니처 contract test, close 중 조회 ordering test | GAP |
| CPP-G0-CONN-001 | capability별 `endpoint_connections_t` | 심볼 0건, startup endpoint 등록만 존재 | `connect/disconnect/list_connections` runtime handle을 manual 역할 builder에서 반환 | handle contract test, 실행 중 add/remove/snapshot unit test | GAP |
| CPP-G0-DISPATCH-001 | dispatch 최적화 비공개, typed packet identity 단일 소유 | `dispatch_mode_t{compiled,dynamic}`, `spot_dispatch_mode`/`stream_dispatch_mode`(`INC/contracts/dispatch/execution.hpp:24,193-194`); `packet_name(...)` override가 `request_call_t`/`send_call_t`/`actor_send_call_t`/`actor_request_call_t`에 공개(`call.hpp:59,339`, `actor.hpp:92,110`) | enum과 두 option, typed call의 `packet_name` override 삭제. identity는 registration descriptor 소유(`stream_write_call_t::packet_name`은 spec상 유지) | 부재 검색 test, descriptor identity unit test | GAP |
| CPP-G0-STREAM-001 | typed session handler와 raw 경계 | typed invoker/concept(`typed_session_packet_handler_for`) 0건, raw `on_packet(message_t)`만(`INC/contracts/streams/stream.hpp:214`); `stream_header_t`/`stream_header_flags_t`/`stream_message_kind_t` 등 wire 타입이 public header에 공존(`stream.hpp:30-46,124`) | serializer registry 이후 typed invoker 추가, raw wire 타입은 runtime 경계로 이동 | typed handler contract test, wire 타입 비노출 layout test | GAP |
| CPP-G0-STREAM-002 | `stream_t` 계약 시그니처 | spec은 abstract `stream_t`(`session_id/close/write_packet/reply_packet` pure virtual, `message_t` 값 전달), 구현은 concrete 핸들+const 참조(`stream.hpp:180-205`); `stream_write_call_t::submit()`은 `result_t<void>`(CPP-G0-ASYNC-001) | **G1 판정 완료: 구현 유지.** 두 spec이 상충한다 — `handler-interfaces` §8 표(목표 C++ 선언)는 concrete 핸들+`const zlink::message_t &`를 명시하고, `cpp-stream` §2 블록은 abstract+값 전달을 보인다. 목표 선언 정본인 handler-interfaces가 우선하며(다른 context 핸들 전부 concrete value-handle인 C++ 계약 일관성과도 부합) `cpp-stream` 블록은 문서 drift로 CPP-SPECDRIFT-006에 기록해 G7에서 갱신한다. `submit()`은 ASYNC-001에서 `void`로 정렬 완료 | stream_t 시그니처 contract test(contract_headers) | 닫힘(코드 무변경) |
| CPP-G0-ROUTEMESH-001 | route-mesh 등록·runtime options | 등록명 `add_route_mesh_channel`(`INC/contracts/configuration/framework_options.hpp:1361`) vs spec `add_route_mesh`; `channel_runtime_options_t::route_mesh_channel(name)`/`route_mesh_channel_runtime_options_t` 없음(`channel.hpp:617`은 `client_server_channel`만) | `add_route_mesh`로 정렬(구명 삭제), route-mesh runtime options 표면 추가 | options 표면 contract test | GAP |
| CPP-G0-EXPORT-001 | installed header에서 runtime state 비노출 | `install(DIRECTORY framework/include/ ...)`가 detail 전체 설치(`framework/languages/cpp/CMakeLists.txt:593`); `configuration/detail/framework_options_state.hpp:329-331`에 runtime state 노출; `task.hpp`의 scheduler/serial-yield detail 공개 | **G1 판정 완료: 설치 트리 fail-closed 검증으로 대체.** C++ contract layer는 헤더 구현이라 detail namespace의 builder state 정의(옵션 수집·헤더 template이 인라인 조작)는 설치 헤더에 남는다 — `handler-interfaces` §8.1 public/runtime 경계 행이 detail state 선언을 "일치"로 명시(계약 문서화 대상 아님). 실제 위험(삭제된 계약·yield/cancellation/dispatch mode·구 spot_ref 표면의 재유입)은 `verify_packaged_contract.sh`가 설치 트리 기준으로 금지 토큰/헤더를 fail-closed 검사. `task.hpp`의 serial-yield detail은 ASYNC-003에서 `serial_turn`으로 정리 완료 | verify_packaged_contract.sh 금지 목록(설치 트리) | 닫힘(검증 강화) |
| CPP-G0-CMAKE-001 | install component/export 분리 | install `COMPONENT` 지정 0건. `zlink_framework_cppTargets`에 `zlink_http_client` 포함(HttpClient 분리 안 됨), Framework/StreamConnector/FrameworkDependency 분리는 export set 2개뿐 | **G1 구현 완료.** 모든 install에 `Framework`/`StreamConnector`/`FrameworkDependency`/`HttpClient` COMPONENT 지정, `zlink_http_client`를 전용 export set `zlink_http_client_cppTargets`+`zlink_http_client_cppConfig.cmake`로 분리(framework export에서 제거), `scripts/verify_packaged_contract.sh` 작성(3 component만 빈 prefix 설치→manifest/금지 토큰 비교→out-of-tree clean consumer configure/build/run, HttpClient 누출·repo source fallback 시 실패) | packaged-contract script PASS + `test_cpp_framework_install_consumer` PASS | 닫힘 |
| CPP-G0-FLOW-001 | flow correlation 전체 | `flow_id`/`flow_origin_t`/`0xF2`/`has_flow_id=0x10` 전부 0건; `message_flow_event_t`(`execution.hpp:166-182`)와 `message_dispatch_error_event_t`(`:150-164`)에 flow 필드 없음 | UUIDv7 자동 생성(create-if-absent), 네 origin, envelope/stream wire 마커 일괄 교체(구형 decoder 병행 금지, unknown mandatory flag=protocol error), coroutine 문맥 전파·정리, connector 동일 codec | flow codec unit test(바이트 동일), 문맥 누수 test, MFLOW-EXT 회귀 | GAP |
| CPP-G0-METRIC-001 | runtime metric catalog | `metric_event_payload_t{name,value,tags}`만(`INC/contracts/eventing/events.hpp:128-133`); spec 요구 `unit`/`instrument_kind`/`temporality` 필드와 두 enum 없음; catalog 계기명(`zlink.stream.connections.*` 등) src 전체 0건, `record_runtime_metric` 내부 호출자 0건 | payload 확장 + §4 catalog 방출(계기 소유권: connector가 `reconnects`), 닫힌 label, 비구독 시 방출 접힘, test collector 집계 | payload/catalog contract test, 무구독 비적재 test | GAP |
| CPP-G0-DRAIN-001 | graceful drain & handoff 전체 | `drain_result_t`/`drained_t`/`force_stopped_t`/`spot_drain_policy_t`/`drain_event_t`/`stream_close_reason_t` 전부 0건; `app_t`는 `stop/request_stop`만(`INC/contracts/configuration/app.hpp:85-86`); peer row에 `Draining` 필드 없음(`INC/contracts/locations/rows.hpp:23-31`); `session-closing` 프레임/connector close reason 0건 | `app.drain(deadline)/drain()(30s)/await_drained()/is_ready()`, 단일 terminal result·멱등·waiter 취소 분리, typed `Draining` row, readiness/admission, owner lease 유지, versioned `session-closing`(v1, reason 1..6) + connector `close_reason`, `zlink.drain.*` 계기 | drain 상태기계 unit test, session-closing codec test, Config 11 Track C | GAP |
| CPP-G0-HTTP-001 | HTTP serializer 등록과 서버 관측 | `register_json_serializer<T>()` no-op(`INC/contracts/http/http.hpp:644`), lazy `get<T>()` 의존; embedded server §8 component(`http_server_t`/`http_connection_t`/`http_error_mapper_t`/`http_server_metrics_t`) 미분리, §15 metrics 대부분 미노출 | `map_*` 등록 시 serializer 등록 이행, §8/§15는 G4 리팩터링과 metric catalog 연계로 구현 | 등록 시점 검증 unit test, server metrics 관측 test | GAP |
| CPP-G0-DI-001 | `service_provider_t::get<T>()` optional 조회 | `get_required<T>()`만 존재(`INC/contracts/configuration/services.hpp:54`) | spec의 `std::optional<std::reference_wrapper<T>> get<T>()` 추가 | DI contract test | GAP |
| CPP-G0-REGTEST-001 | 정식 spec 경로 regression fail-closed | `test_cpp_framework_layout_contract.cpp:327-396`의 needle이 개정 전 공통 spec 문구(`leaveActor`)로 고정되어 baseline 실패; `:232` 코드 scan root는 `if(!exists) continue`(fail-open) | needle을 개정 spec 문구로 갱신, scan root 존재를 fail-closed로 | baseline 재실행 green + fail-closed 검증 | GAP |

## 4. E2E·sample inventory

| ID | 영역 | 현재 차이 | 필요한 작업 | 상태 |
|----|------|-----------|-------------|------|
| CPP-G0-E2E-001 | Config 8 | `e2e/YieldDispatch` 디렉터리, `YD-*` ID, `yield-*` marker, `run_e2e_all.sh:26` 등록명 | `AutomaticTurnDispatch`/`ATD-A1~E5`/`await-*` marker로 이관. YD-A1~E5는 ATD와 1:1 대응(D1/E4/E5는 runner gate). 단 framework의 yield 표면 제거(CPP-G0-ASYNC-003)가 선결이며, ATD-A1/E4는 "Yield 계열 부재" 검증으로 의미가 뒤집힌다 | GAP |
| CPP-G0-E2E-002 | Config 11 | `ObservabilityOps` fixture 부재, OBS-A1~C5 15개 전체 누락(기존 `RuntimeMonitoring`은 Config 7 소관) | flow/metrics/drain 구현 후 public 표면만 쓰는 fixture/runner/`/evidence` 구현 | GAP |
| CPP-G0-E2E-003 | all runner 등록 | `run_e2e_all.sh` CONFIGS 10개, `SpotActorTransfer`는 개별 실행 전용(디렉터리 11개 중 10개 등록) | G6에서 개별 runner 증거 필수(plan §8.6), ATD/OBS 추가 시 CONFIGS 갱신 | GAP |
| CPP-G0-E2E-004 | cross-language | C++↔이전 G7 통과 언어(.NET) 방향별 행 없음 | G6에서 store/codec/messaging/flow-wire/draining-row/session-closing 방향 조합 구현 | GAP |
| CPP-G0-SAMPLE-001 | Bingo §17 | flow 로그는 기존 시연 존재(cpp-monitoring §7.5), metric 계기·`DrainNatural`·drain 시연 없음 | G5에서 flow/metrics/drain 예제와 관측 켠 smoke `bingo=completed` 확인 | GAP |

sample manifest는 TicTacToe, Bingo, DeliveryDispatch, SupportChat, GameQuest, ShoppingMall
여섯 개다. 실행 성공은 G5에서 새로 검증한다.

## 5. §7.2 bindings 공개 기능 audit

framework와 connector는 `zlink::cpp` target을 `target_link_libraries(... PUBLIC zlink::cpp)`로만
사용한다. bindings source/서브디렉터리 직접 참조와 private header include는 없다
(`find_package EXACT CONFIG`만 사용).

| 필요한 기능 | bindings public capability | framework owner | 판정 |
|-------------|----------------------------|-----------------|------|
| context/socket 수명 | `zlink::context_t`, router/dealer/pub/sub/stream socket | `src/runtime/backend/*` | 충족 |
| SPOT node/route bridge | `service::spot_node_t::create_route_bridge()`/`spot_route_bridge_t`, spot 생성·dispatch·transfer callback | `src/runtime/spots/*`, route bridge 자동 배선 | 충족 |
| CAPI timer | timer 등록·`fire_count`·stop/destroy | `src/runtime/timers/*` | 충족 |
| STREAM transport | `stream_socket_t` lifecycle/session callback | `src/runtime/streams/*` | 충족 |
| flow wire 확장(0xF2/flow_id) | wire는 framework/connector envelope·header codec 소유(bindings 확장 불필요) | framework stream/channel codec | 충족(선행 절차 불요) |
| drain의 `Draining` row | location store는 framework 계층 소유(bindings 무관) | `contracts/locations/*` | 충족(선행 절차 불요) |

현재 확인 범위에서 §7.2 조건부 선행 절차(bindings 공개 기능 추가)가 필요한 gap은 없다.
구현 중 새 필요가 확인되면 이 표에 행을 추가하고 선행 절차를 먼저 닫는다.

## 6. §7.3 core/bindings 재사용 감사

| core 기능/symbol | bindings public symbol | framework 중복 후보 | 판정과 책임 차이 | 상태 |
|------------------|------------------------|----------------------|------------------|------|
| SPOT route relay 분류 | `spot_node_t::create_route_bridge()` | 자체 relay packet 분류 없음(bridge 사용, cpp-spot 계약 기준) | bindings 위임 유지 | 완료 |
| stream wire header codec | 해당 없음(core는 raw stream) | framework `stream_runtime`과 connector `header_codec`이 각자 인코딩 | 스펙(cpp-monitoring §7.4/§9)이 "바이트 동일 이중 구현"을 명시 요구하는 mirror. 책임이 달라 유지하되 바이트 동일성 test로 고정 | 유지(검증 필요) |
| LZ4 compression codec | 해당 없음 | framework `lz4_stream_compression_codec()`와 connector `lz4_compression_codec()` | 별도 배포 라이브러리 간 의도된 mirror(연결 금지 계약). 유지, 동작 동일성 test로 고정 | 유지(검증 필요) |
| CAPI timer scheduling | timer `fire_count` | framework 자체 timer scheduler 없음(투영만) | bindings 위임 유지 | 완료 |

G4/G7에서 재감사한다. "현재 코드가 이미 있음"은 유지 근거로 쓰지 않는다.

## 7. 삭제 목록

호환 계층 없이 삭제한다.

- `cancellation_token_t`, `cancellation_token_source_t`, token 수락 yield overload
- `request_call_t::yield()`, `channel_yield_request_call_t`, `actor_yield_join_call_t`,
  `worker_call_t::yield()/submit(callback)`, `worker_completion_mode_t` 공개 표면,
  `capture_current_serial_yield_turn` 공개 detail
- `dispatch_mode_t`, `dispatch_options_t::spot_dispatch_mode/stream_dispatch_mode`
- `request_call_t/send_call_t/actor_send_call_t/actor_request_call_t`의 `packet_name(...)` override
- `actor_context_t::is_joined()`
- `actor_join_result_t`의 `result_code`+optional 독립 필드 형태
- `spot_ref_t`와 `spot_context_t::request_to(spot_ref_t)`/`route_client_t`의 spot_ref overload
- `framework_error_kind_t`의 공통 계약 밖 enumerator 11개(내부 상태로 이동)
- camelCase lifecycle 탐지(`onCreateActor`/`onLeaveActor`/`onDisconnectActor`)와 `destroyActor`
- `session_actor_t`의 spec 외 공개 멤버(`relay_request` 등)는 정식 계약 대조 후 삭제 또는
  spec 변경 절차로 분리
- `add_route_mesh_channel` 구명(정식 `add_route_mesh`로 정렬 후)
- e2e `YieldDispatch` 디렉터리·`YD-*` ID·`yield-*` marker·`yield_*` 메시지 타입/네임스페이스

## 8. 문서 snapshot

```text
8ffae3ae36f3305e1dfa35d1874a1c2c9c57342f5f2116abbe7f5e432f79f595 README.ko.md
8cf0cac1e46c6086de082d8ad4aeae51f339245d05da1b7bc6175f9b622ec79e server/22-actor-model.ko.md
6614f5efd549442f95ac4f67f8ff1e10bba9c7061ee63a7608ffd91f43fea4bd 04-async-execution-policy.ko.md
5f190e3b4f1b93d4a0e03c9ba23b625a1b8a56c5d79dc361917215be73fa0839 server/10-channel-topology.ko.md
077319afac1aec1aba884853cd172443f5e2563d664b00b0a9e2468a252a196c server/53-flow-correlation.ko.md
06f3d56438301a80afd983475e58c65d3b0e678a32b832c5f13813bf937ffcb6 05-framework-api.ko.md
822ada32199d71d2c4505c561fc4f2f4db6f9c50d49eb2469b202d87dd2bc97f server/54-graceful-drain-handoff.ko.md
125804135ae6cc74c0b6c6e44296662d0aa7cae3157fa8c883d3415aa0aa95a9 90-implementation-gap.ko.md
df441c4de567865658b0b79ded6c840d020ccf60865f58e7990a248e9fa361a0 02-interaction-model.ko.md
dfa08a0db46f59bcd107347c9f02256ff023d7c64c3f8caac42772c37d7b058b server/40-location-runtime.ko.md
f84d4a035cd773d6fe8aa0096151909e92be0743b57445dd51e5b38eeab9376c server/41-location-store-redis.ko.md
0635851f5d9b3cf0fa6f481fb886200e1802f3bda6fe80db3648b35b53e22108 server/52-message-flow-tracing.ko.md
a165665cbb47ef2b69744cfa7614d40c35274154af47439693f811080934f914 03-message-model.ko.md
136b4b2378c404b4728a4e526f985da6303456c294c06e9e425a39abb99d816b 01-overview.ko.md
883e767f3c2b673c9dabc4083fa42a7fc29799d25ef9ad04761d9cbdbc5cb245 00-public-contract-governance.ko.md
d34e9b26860a2ee285b340c5234bb27fc4c82438bbdf375e697f1350a0c1ef1f server/51-runtime-metrics.ko.md
49f5154412ed827496ba50f2e49a0f6bc84f3e1bcfdb4022b561dbded9b64147 server/31-session-actor-dispatch.ko.md
ae0c25c9f67cb397da861e82d8aaf1311472dfe5e28212a88f1e0aa32ec20998 server/23-spot-actor.ko.md
45576c26b8061e0a1965d219d539080a4917c6c06572b5d63bccddfb2f1bbe4d server/24-spot-address-messaging.ko.md
143e0cde846ea1d55812015f23cd6f3afb44679f4ff7fad4f91f54b37eb124be server/languages/cpp/README.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
3ca1c76505c73a0d5e74abbc9d606c3d7bc2d9f12646b72b0c905ebf0b3fe40d server/languages/cpp/61-embedded-http-server.ko.md
e181353da277312bb6dcce2a89129f3552fdb7a2d9e11c2e2bd0fa448cbfd887 server/languages/cpp/60-http-hosting.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
40ecf2131568c46311816e03a5f81c822098972c6a29bf96c6ef57e42e6f0050 server/languages/cpp/01-system-structure.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
40ecf2131568c46311816e03a5f81c822098972c6a29bf96c6ef57e42e6f0050 server/languages/cpp/01-system-structure.ko.md
ca6096255fe7bf8c97c687d29e032d2278cdbbba3a97400f46874da3007548b0 server/languages/cpp/02-framework-interfaces.ko.md
40ecf2131568c46311816e03a5f81c822098972c6a29bf96c6ef57e42e6f0050 server/languages/cpp/01-system-structure.ko.md
d30ea2acfbee45009ee2e0d000f2b37009ccf9f5f134c8ad29dd2035e3b8ab99 server/50-runtime-monitoring.ko.md
fe9072b34809ccc20b489f6a3ebdd093fdd35470d3f1ee291e065f5644cc5f99 stream-connector/32-stream-connector.ko.md
623bca5e070513cc314c2d7f93d00dcdeab8b5f473bdeb883bfb5711eaa028e0 server/30-stream-session.ko.md
d9a36ee80739f7035a0371c703871cea26f952e353d0f123e45b69e44c540088 server/21-spot-node.ko.md
d9546cf37a3f9f34e863ac4a63eda2e2af6f1985269279579fa5b53632978108 server/20-spot-messaging.ko.md
54f7a53bc1ff7cc97ada0a41d28f50678e43d68c3ad46e0beef43466dd8ccf5c server/25-stage-wrapper-on-spot.ko.md
7a1a32c29bc2cfc642ce465f71e5f405741a2a8c23b95d0426b132947fdd0202 server/11-channel-messaging.ko.md
```

## 9. G0 완료 조건

- [x] 정식 문서 inventory와 hash 고정
- [x] public symbol/동작/package/E2E gap을 작업 ID로 등록
- [x] 삭제 목록 고정
- [x] bindings package version과 public-only 사용 감사
- [x] Config 8/11 fixture gap을 실패 테스트로 고정. 물리 이관은 framework yield 표면
  제거(G1)와 함께 수행하고 최종 실행은 G6에서 판정(Java 선례)
- [x] 각 GAP을 재현하는 active failing contract test:
  `test_cpp_framework_target_contract`가 ledger ID 기준 69개 실패를 재현(exit 1)
- [x] documentation regression fail-closed: layout needle을 개정 spec 문구로 갱신,
  scan root 부재를 fail-closed로 전환, `http_perf_policy` 문서 경로를 정식
  `common/spec/languages/cpp/` 경로로 수정 — 세 항목 재실행 PASS

## 추가 발견 (G2 Config 11 구현 중, 2026-07-13)

| ID | 표면 | 현재 | 목표/대응 | 상태 |
|----|------|------|-----------|------|
| CPP-FANOUT-WIRE-001 | spot fanout wire | cpp는 self-delimited 단일 프레임(`['Z''L''F''E'][u32 BE header_len][header JSON][body]` — G4에서 legacy raw 오분류 방지용 4바이트 매직 추가), .NET은 envelope 2-part. cpp 수신은 양쪽 다 수용하나 cpp 발행은 .NET 구독자가 못 읽을 수 있음. `ZLFE` 프리픽스는 framework 프레임 예약(계약)으로 판정 — 예약 프리픽스로 시작하는 raw payload는 계약 밖(비-framework 발행자를 framework가 escape할 수 없어 in-band 판별의 이론적 한계; Codex MEDIUM 잔여는 이 예약 계약으로 수용) | G6 cross-language fanout에서 .NET 실측 wire 대조 후 정렬(원인인 framework 부착 spot의 multipart 첫 파트만 도달 현상은 core 소유 — core팀 이관 후보). wire 확정 시 예약 프리픽스 계약도 spec 명문화 | 열림(G6) |
| CPP-BINDING-SENDOP-001 | bindings/cpp send 빌더 | `send_submit_operation_t::message(message_t&&)` rvalue 오버로드가 append가 아닌 single_part 교체(다중 파트 조립 시 앞 파트 유실). lvalue 오버로드는 append로 정상 | 바인딩 소유 — 전달 후보. framework는 lvalue 경로만 사용 | 열림(전달) |
| CPP-CONN-PUMP-001 | connector 유지보수 pump | heartbeat/pong 송신이 dispatch/API 호출 구동(passive 대기 앱은 pong 굶김→서버 heartbeat_timeout). .NET은 백그라운드 수신 루프가 상시 응답 | 장수명 idle 클라이언트 시나리오에서 차이 노출 — connector 백그라운드 유지보수 루프 검토 후보(G7 판정) | 열림(판정) |
| CPP-STREAM-LZ4-001 | STREAM 압축 wire | cpp connector/framework가 raw `[u32 BE][LZ4 block]` 프레이밍을 쓰는데 .NET(K4os LZ4Pickler)·Node(lz4-pickle)는 **pickle 프레이밍**이라 압축 payload가 언어 경계를 못 넘었다(G6 실측: .NET 압축 응답을 cpp 커넥터가 receive-limit로 거부) | **닫힘(G6)**: 공용 `lz4_pickle.hpp`로 connector·framework 코덱을 pickle로 교체, 단위 케이스 갱신, cross-language STREAM 양방향 PASS |
| CPP-E2E-CONCURRENCY-001 | E2E fixture 동시성 | cpp `http_client`는 호출 스레드에서 동기로 수행되고 프레임워크 HTTP 서버도 요청을 직렬 처리해, 한 프로세스 안에서 "요청을 띄워 둔 채 다른 요청"을 하려면 별도 스레드가 필요하다(.NET은 un-awaited task로 표현) | **닫힘(G6)**: SM-A8을 전용 스레드+전용 client로 재구성. 동일 패턴이 필요한 시나리오는 같은 방식 사용 |
| CPP-SPOT-REG-TXN-001 | spot actor 등록 트랜잭션 동시성 | G4 Codex가 지적한 3건은 **기존 구조의 취약점**(본 작업 이전 커밋에서도 동일): (a) 동시 join이 같은 actor 인스턴스로 초기화/admission을 중복 수행(단일 initializer 선출 없음), (b) join 설치 후 commit 전에 도착한 destroy가 맵/인덱스를 지워 commit이 인스턴스 없이 성공할 수 있음, (c) relay가 create 이전에 취득한 `actor_spot_rids` iterator를 이후 역참조(동시 destroy 시 무효). 본 작업이 추가한 identity index 헬퍼 자체는 Codex 판정 clean(팩토리 락 밖 실행, 맵·인덱스 동시 갱신, 참조-후-erase 무해) | 등록 트랜잭션을 단일 임계구역/버전 검사로 재설계하는 별도 작업으로 분리(핫 경로 재설계라 회귀 위험 — 실측 기반 리팩터가 필요). 현재는 spot node의 직렬 dispatch 스레드+호스트 브리지 경로가 사실상 직렬화하고 있으며 전 게이트(단위·E2E·샘플)에서 재현되지 않음 | 열림(후속) |
| CPP-SPOTSTOP-001 | spot node 종료 소유권 | 간헐적으로 종료가 `zlink_ctx_term()`(mailbox poll, timeout=-1)에서 무한 대기 → 러너 강제 kill(137)로 config FAIL. **원인은 core가 아니라 프레임워크 소유권 순서**: spot node host service가 컨텍스트를 term 할 때 런타임이 여전히 native SPOT/actor 핸들(`native_spots_by_rid`/`native_actors`/`routed_control_spot`)을 들고 있어 소켓이 살아 있었다 | **닫힘**: `spot_node_runtime_t::release_native_handles()`를 node reset·context term 이전에 호출해 핸들을 먼저 해제 |
