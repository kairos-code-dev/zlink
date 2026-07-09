# `framework/languages/cpp` (C++) POSD·DDD 리팩토링 수정 목록

> 2026-07-09 `framework/languages/cpp` 전수 리뷰. 대상은 core 바인딩(`bindings/cpp`)과 **별개**인 C++ **framework** 코드다:
> `framework`(src+include, ~38.8k줄) + `connector/core`(stream connector, ~7.2k줄) + `connector/engines`(unreal/godot/axmol 게임엔진 어댑터) +
> `http-client`(~2.4k줄) + `extensions`(framework-locations-redis, framework-codec-messagepack, framework-codec-protobuf). 8-에이전트 read-only 병렬 리뷰 + 메인 루프 grep 검증.
> **C++는 레퍼런스 구현**이다 — 여기의 결함은 최고 레버리지(node/java/dotnet로 전파되며, 다른 언어 문서의 "동형 결함"의 원천). 반대로 다른 언어 문서의 다수 결함은 C++에 **이미 부재/해소**돼 있다(§부록 A).
> 파일:라인은 리뷰 시점 기준이므로 편집 전 현재 코드로 재확인한다. 정본 대조 기준은 `{dotnet,node,java-kotlin}-framework-posd-ddd-refactor-list.ko.md`.
> 이 문서는 두 리뷰의 **병합 정본**이다: (1) 8-에이전트 read-only 병렬 POSD/DDD 전수 리뷰(A~D 트랙) + (2) codex 병렬 리뷰(R1~R5 우선순위 + 실행 순서·검증 게이트·tracked 산출물 정리). 겹치는 god-file 분해는 통합했다(R1=§D1, R2=§D2, R3=§D7-redis, R4=§D4/§C9, R5=§D8-http, tracked 산출물 정리=§부록 B).

## dead-code 판정 규칙 (중요)

C++ 런타임 클래스는 `framework/tests`(`Zlink.Framework.UnitTests`/`ContractTests`), `e2e/`, `samples/`, 그리고 connector/extension 자체 테스트가 include로 직접 구동한다. dead 판정은 반드시
**(a) `framework/{src,include}` grep + (b) 전 `tests`/`e2e`/`samples` + connector/extension 테스트 grep**을 하고 `build`/`install`/gcov 산출물은 제외한다.
C++ 특성: **익명 namespace / `static` / `detail::` 심볼이 어디서도 안 쓰이면 가장 안전한 삭제 후보**다. `include/zlink/framework/contracts/**`의 public 계약 심볼은 무참조라도 삭제 금지(보존·"미참조 목록화"만) — 다른 언어 consumer만 있어도 계약 표면이다([[CLAUDE.md]]).
**동명 주의:** 여러 클래스가 같은 메서드명을 가진다(예: `serialize_request`는 `spot_node_manager_t`에서 live, `session_actor_manager_t`에서 dead). 소유 타입별로 분리 확인한다.
`test-only`(production 배선 0, 테스트만 소비)는 dead가 아니라 별도 트랙(dotnet A-SP2식) — 삭제 시 테스트 동반 갱신.

**위험 표기:** **없음**(control plane) / **code-motion**(코드 이동·의미 동일, public 헤더·계약 유지) / **벤치**(per-message/dispatch/reply/publish hot, baseline vs patched 무회귀 필수, [[feedback_perf_measure_before_commit]]).

**가드레일:** public 계약 헤더(`include/zlink/framework/contracts/**`) 유지. native routing/join/bridge/pub-sub 원시연산은 `service::*`로 위임(재구현 아님). codec 불변식(JSON 기본, msgpack/protobuf extension, content-type 판별). **connector는 framework 무의존 독립 유지**(§C0). 게임엔진 어댑터는 얇은 idiomatic wrapper(connector 로직 재구현 금지). RAII/PIMPL/template `if constexpr` 디스패치는 C++ 관용 — parity 갭 아님.

---

## 0. 우선순위 맵

| 항목 | 우선순위 | 상세 |
|---|---|---|
| STREAM 서버 수신 프레임 크기 상한 부재(OOM/DoS) | **P0(보안)** | §B1 |
| channel `outbound_calls` 무한 증가(메모리 누수, hot 뮤텍스) | **P0** | §B2 |
| SPOT god-file(spot_runtime.cpp 3799) 분해 + drain 상태기계 통합 | **P0** | §D1 + §C2 |
| stream wire 포맷 framework↔connector 트윈 + 규칙 drift | **P0** | §C1 |
| connector typed-codec 편의헤더의 framework 결합(독립성) | P1 | §C0 |
| actor busy-wait 30s 폴링 / ordering_log hot alloc / async-log no-op | P1 | §B3~B5 |
| godot/axmol 어댑터 on_packet no-op + 중복 | P1 | §B6 + §C7 |
| channel/actor/stream god-file 분해 | P1 | §D2~D6 |
| 선언 단위 dead 정리 | 선행 | §A |

---

## A. 삭제 트랙

### A1. 무참조/도달불가 (src+tests+e2e+samples grep 검증, 없음/code-motion)

- **channels — route_client erased submitter dead 클러스터(~150줄 + 헤더 55):** `channel_runtime.cpp:1139-1199` `submit_request_erased`, `:1257-1319` `submit_spot_request_erased`(둘 다 private, 호출 0 — live는 `submit_request_reply_message_erased`), `channel.hpp:794-857` `submit_request_reply_erased<TReply>` 템플릿. `route_channel_host_service.cpp:335-345` `accepts_spot_routes_from`(무참조 + `attach_spot_route_bridge:347-357`에 재구현).
- **execution — serial_execution_queue 진짜 무참조 3메서드:** `serial_execution_queue.{hpp:35-38,cpp:124-156}` `try_post_async_front`/`post_async`/`run`(전 트리 0; `post`는 test-only).
- **dispatch — `runtime/dispatch/projection.{hpp,cpp}` 파일 전체 test-only(dead 아님, §A2 부류):** `dispatch_projection_t`/`runtime_event_*`/`project` production 배선 0, `test_cpp_framework_runtime_integration.cpp:29-46`만 exercise + `CMakeLists.txt:115` 빌드. **삭제하려면 해당 테스트 + CMake 항목 동반 제거** — "truly unused"가 아니라 테스트가 자기 코드를 살리는 상태(dotnet A-SP2식). 배선 의도 확인 후 판단.
- **spots:** `spot_runtime.hpp:194-202` `record_actor_spot_location_unlocked`(detail inline 무참조, 형제 `record_actor_route_unlocked`는 live), `spot.hpp:289` `spot_node_runtime_state_t` forward 선언(정의/사용 전무).
- **actors:** `actor_client.cpp:347` `create_parts`·`:358` `decode_reply`(private 무참조; live는 `relay_actor_packet`+`decode_envelope_reply`), `actor.hpp:397`/`actor_gateway_runtime.cpp:138` `bound_session_t::send_typed(type_index)` 오버로드(live는 std::function판 :119), `actor.hpp:654`/`actor_gateway_runtime.cpp:706` `session_actor_manager_t::serialize_request`(무참조 — `spot_node_manager_t::serialize_request`는 별개).
- **configuration:** `framework_options_state.hpp:316,341-344` `add_zlink_action`+`deferred_zlink_actions`(죽은 루프, `framework_options.hpp:1426-1428` 항상 no-op), `:93-114,327` `accepted_spot_route_manual_connections_by_node` 필드+`manual_connections_for`/`has_manual_connections`(무참조).
- **codecs/extensions:** `extensions/framework-codec-protobuf/.../protobuf.hpp:52-89` `protobuf_serializers_t`+`serializers_proxy_t`(~38줄 무참조, messagepack엔 대응물 부재), `framework/include/zlink/framework/codecs/json_stream_e2e_client.hpp:7-10` 빈 namespace 블록.
- **redis:** `extensions/framework-locations-redis/.../redis.hpp:159` `redis_location_scripts_t::remove_lease` Lua 상수(완전 dead — production은 `del`+`srem` 직접, `:1277-1300`). dotnet A-ST2 대응.

### A2. test-only(삭제 아님, dotnet A-SP2식 — 삭제 시 테스트 동반) 목록화

`actor_gateway_runtime.cpp:850-869` relayed_frames/bound_session_pushes/actor_bound/actor_disconnected; `route_channel_runtime.cpp:344-352` `complete_request`; `offload_executor` `drained()`/`live_worker_count()`; `native_route_backend` 1-arg ctor(`hpp:28`); redis.hpp:169 `list_leases` Lua(test assert만); `store_location_resolvers.hpp:82-85` `resolve_route`(non-override); `location_value_codec.hpp:52-89` `try_parse_*`; `timer_runtime` sync `dispatch_fire_count`.

### A3. 미참조 public 계약 (⚠️ 삭제 금지, parity 확인 후 목록화만)

`handler_registry.hpp:108,391-399` `send_raw`+`raw_handler_t`, `:258-298` `request` 4오버로드(on_request 별칭), `task.hpp:338/356` `reschedule_task`/`cancelable_task`(detail, test만), `store_actor_directory_t::ensure`(의도적 no-op). **완전 미사용 파일 없음.**

**A 착수 순서:** A1(빌드+테스트로 충분) → A2(테스트 동반) → A3(parity 확인 후).

---

## B. 결함 수정 (correctness)

- [ ] **B1. ⭐STREAM 서버 수신 프레임 크기 상한 부재 (벤치/보안, P0)**
  - `framework/src/runtime/streams/stream_host_service.cpp` — 프레임 prefix의 u32 `payload_size`(최대 ~4GiB)로 **검증 없이** 즉시 대용량 할당. **두 경로 모두 취약**: Boost.Asio `read_frame`(`:515` `read_exact(socket, payload_size)`) + native-fd `read_frame_native`(`:566` `read_exact_native`). connector는 `frame_codec_t::validate_receive_frame_size` + `connector_options_t::max_receive_payload_size`(`framing.cpp:121`)로 방어하나 **서버엔 대응 옵션 부재**(계약 `stream.hpp`엔 `max_decompressed_size`만). 악의적/버그 클라이언트로 OOM/DoS. 서버에 수신 payload 상한 옵션 추가 + 양 경로 검증. (레퍼런스 결함이라 전 언어에 전파 위험.)
- [ ] **B2. ⭐channel `outbound_calls` 무한 증가 + hot-path 뮤텍스 (correctness/벤치, P0)**
  - `channel_outbound_exchange.cpp:735,882,957` — 매 `submit_request`/`submit_send`/`submit_publish`마다 `{kind,channel,topic,packet,timeout,metadata(map 전체복사)}`를 `_state->mutex` 잡고 push_back. clear/trim 전무, 유일 reader는 `outbound_calls()` getter의 테스트 소비뿐(`channel_runtime.cpp:531`). production에서 호출당 무한 메모리 누적 + 뮤텍스 경합인데 목적은 진단뿐. 링버퍼/`#ifdef` 진단화/제거(테스트 훅 분리). dotnet A-SP3 계열.
- [ ] **B3. actor `request_spot_mesh_parts` 30초 busy-wait (벤치)**
  - `host/actor_gateway_spot_bridge.cpp:152-166` — routed-peer readiness를 30s deadline `while` + `sleep_for(10ms)`로 **호출 스레드에서 동기 폴링**. join·packet-relay 양 경로가 태움 → 원격 spot 미준비 시 dispatch 스레드 최대 30s 점유. condvar/이벤트 대기로 전환. (참고: `native_route_backend.cpp:103-147`은 이미 `condition_variable` 기반(순수 busy-wait 아님, progress 보조 폴링만) — B3와 다른 형태. `app.cpp:670-676` run() 1ms 폴링은 유휴-메인스레드 red-flag.)
- [ ] **B4. spot `ordering_log` hot-path 문자열 적재 (벤치)**
  - `spot_runtime.cpp:1106,1179,1213,1260,1295` — `publish/send_to/request_to_erased` **매 호출마다** 문자열 연결 push_back. getter(`:2952`) 유일 소비 = 단위 테스트. dotnet A-SP3 동형(테스트 전용 bookkeeping이 hot 경로 상시 실행). 빌드 플래그 게이트/제거.
- [ ] **B5. async/backend 로깅 설정이 순수 no-op (correctness, 기능 갭)**
  - `diagnostics/logging.cpp` — `logging_builder_t::use_async`(285)/`use_backend`(293)가 값을 저장하지만 방출 경로 `emit_log`(143-199)가 **참조하지 않음**(항상 동기·builtin). 즉 async/비-builtin backend가 수락되나 무효. 동작시키거나 미지원이면 검증에서 거부/문서화. (파일 싱크는 `:189-195`에서 레코드마다 ofstream 재오픈+오류 무시 — 견고성 소.)
- [ ] **B6. godot/axmol 게임엔진 어댑터 `on_packet()` silent no-op (correctness)**
  - `connector/engines/godot/src/zlink_godot_stream_connector.cpp:35,180-183` + axmol 동일 — `on_packet()`가 `packet_callback`에 저장만 하고 connector에 핸들러 미등록(`connector.on<T>(...)` 부재), `packet_callback`는 전 파일에서 read 0 → 서버 push 패킷 절대 미전달. Unreal은 정상(`ZLinkStreamConnector.cpp:235-238`). connection state도 미구독(reconnecting enum 죽은값). 핸들러 배선 추가.
- [ ] **B7. http-client `submit<T>` 2xx 빈 바디 decode 실패 (없음, 저신뢰·계약)**
  - `http-client/include/zlink/http_client/contracts/client.hpp:176` — `status<400`이면 무조건 `message_t::from(raw.body).parse_json<T>()` → 204/304/빈 200에서 throw, 성공이 `payload_decode_failed`로 뒤집힘. node B8/java B7이 "C++ 계약 대조" 지목한 지점 = C++도 동일 갭. 빈 바디 short-circuit 계약 확정.
- [ ] **B8. spot `add_spot_resolver` 무동작 + routed reply 이중 submit 엣지 (없음)**
  - `spot_runtime.hpp:98`/`cpp:1756` `resolvers` map write-only(조회 `resolve_spot:2848` 미참조) → 등록 resolver 조용히 무시. public 계약(`add_spot_resolver`)은 유지하되 실배선 또는 문서화. `spot_runtime.cpp:3510-3535` routed reply fallback: 첫 submit 예외를 빈 catch로 삼킨 뒤(`:3519`) 재제출 → 부분전송 후 throw 시 이중 응답 가능(저위험, 삼킨 예외 관측 부재).
- [ ] **B9. 진단 보고/오류처리 비대칭 (없음, 낮음)**
  - header 디코드 실패가 dispatch-error reporter 미보고: `route_packet_dispatcher.cpp:42-46`·`channel_packet_dispatcher.cpp:30-34`(handler-missing/body는 report). connector 동기수신 `framing.cpp:132-135`는 헤더 실패 시 가짜 `{unknown}` 패킷 반환·조용히 drop(calls 경로 `zlink_stream_calls.cpp:479`는 표면화). handler topic 와일드카드 비대칭(`handler_registry.cpp` 등록 336 vs 조회 254). 무조건 stderr 잔존(`route_channel_host_service.cpp:517-599`, `channel_host_service.cpp:229-233`, `monitoring_runtime.cpp:99-102` — 게이트 없음, 스윕).

**B 착수 순서:** B1(보안)→B2→B3/B4(hot 폴링/alloc)→B6(엔진 no-op)→B5/B7/B8/B9.

---

## C. 구조 통합 — 지식 중복 소거 + 레이어링

- [ ] **C0. connector typed-codec 편의헤더가 framework에 결합 (code-motion, 레이어링)**
  - **connector 런타임 SOURCE는 framework 무의존 확인**(`connector/core/src` framework/nlohmann include 0, 자체 LZ4 소유). Java C0(전체 runtime 의존)과 **다름** — connector 런타임 코어는 독립.
  - **그러나 connector codec PUBLIC 타깃이 framework에 의존**(정정 — "core만 독립"으로 과소평가하면 안 됨): `connector/core/.../codecs/auto_codec.hpp:4`가 `<zlink/framework/codecs/json_stream_connector.hpp>`(framework 트리)를 include → 이 헤더는 `zlink::stream_connector::codecs` 네임스페이스를 채우지만 framework `codecs/json.hpp`에 의존(`message_t::from_json/parse_json`의 out-of-line 정의가 거기에만 존재). CMake `zlink_stream_connector_codecs` INTERFACE 타깃(`CMakeLists.txt:340-350`)이 `framework/include`를 include-dir로 주입 + nlohmann 링크 → **connector typed-codec를 쓰는 소비자가 `framework/include`를 전이 의존**. 실제 소비자는 **게임엔진이 아니라**(godot/axmol/unreal은 raw byte payload만 다뤄 auto_codec 미참조) **samples/e2e 클라이언트(TicTacToe/Bingo/SupportChat/GameQuest/DeliveryDispatch + e2e/DeliveryDispatch/Client) + connector 자체 테스트 스위트**(`test_cpp_stream_connector.cpp`가 auto_codec include → 자기 계약 테스트가 framework/include 전이 의존).
  - **계약 모순:** 패키지 설치 테스트 `install_consumer.cmake:79-83`은 connector 패키지 config가 `zlink_framework_cppTargets.cmake`를 포함하는 것을 **금지**한다(= connector는 framework 타깃 무의존이 계약). 그런데 codecs 타깃은 framework/include를 주입 → 계약과 실배선 불일치. layout-contract 테스트(`test_cpp_framework_layout_contract.cpp:138-147`)는 nlohmann 노출만 화이트리스트하고 **방향(connector→framework)은 미검사**. (framework-side 계약 테스트 `test_cpp_framework_contract_headers.cpp:60,77`도 auto_codec를 소비.)
  - **부수(build-system dead dep):** `connector/engines/godot/CMakeLists.txt:11`·`axmol/CMakeLists.txt:11`이 `zlink::stream_connector_codecs`를 **PRIVATE 링크**하는데 두 엔진 소스는 auto_codec/codecs 헤더를 전혀 include하지 않음(raw byte만) → 미사용 CMake 의존이 엔진 타깃에 framework/include를 끌어옴. C0 수정 시 이 링크도 제거.
  - **권고(가드레일 준수):** connector JSON codec을 connector 트리로 이관하고 `message_t::from_json` 대신 nlohmann 직접 호출로 self-contained화 → connector codec 타깃에서 `framework/include` 제거. framework는 자기 사본 유지(또는 core `zlink/Contracts` 공유). node/dotnet connector 독립성과 정합. (참고: 두 리뷰 에이전트가 "framework→connector 어댑터로 허용" vs "좁은 누수"로 갈렸으나, 방향 게이트 부재 + 전이 의존이 실재하므로 자립화가 안전.)
- [ ] **C1. ⭐stream wire 포맷 framework↔connector 트윈 + 규칙 drift (없음, P0 유지보수)**
  - framework `stream_runtime.cpp:587-906`(`validate_header`/`encode_header`/`decode_header` + u64/u16 헬퍼 + 인라인 metadata) ≡ connector `header_codec.cpp:37-276` + `metadata_codec.cpp` — 바이트 포맷·검증 규칙 트윈. LZ4 프레임도 트윈(`stream_compression.cpp:24-104` ≡ `lz4_compression_codec.cpp:24-109`, 자체 소유 의도).
  - **이미 drift 시작(상호운용 잠복 갭 = §B 결함):** 빈 metadata 값(framework 허용 vs connector 거부, B/D2), 예약 패킷명(`__zlink.` vs `$`), 중복 metadata 키(마지막값 vs 거부). → **정본 부재가 근본 원인.**
  - **권고(코드 병합 금지 — 레이어링):** (1) 언어 중립 wire 스펙 문서 정본화, (2) 양 코덱 상호 참조 주석 + **동일 벡터를 먹이는 spec-conformance 교차 테스트**(현재 framework/connector 코덱 테스트가 각자 존재·교차 없음), (3) 검증 규칙(빈값/예약명/중복키) 정렬. dotnet/node/java C1 동형이나 C++가 정본이므로 여기서 규칙을 확정해야 전 언어가 따른다.
- [ ] **C2. spot/channel decode→dispatch→report 상태기계 + reply 봉투 조립 중복 (벤치, per-message)**
  - spots: `spot_runtime.cpp` reply 봉투 조립 11벌(`create_error_header`/`create_reply_header`/`reply_raw_envelope`) + `report_spot_dispatch_error` 15회 + `trace` 9회가 `drain_actor_packets`/`drain_routed_packets`/`dispatch_subscription`/`relay_actor_packet`에 반복. `reply_no_bind`/`reply_error` 람다 동기/offload 2벌(`:3129·3214` ↔ `3314·3321`).
  - channels: `route_packet_dispatcher.cpp:86-213` ≈ `channel_packet_dispatcher.cpp:21-164`(`message_dispatch_error_event_t` 14필드 brace-init route 4회/channel 3회). node 5벌보다 나으나 `(surface,kind,reason,action)` + reply-strategy 매개화한 dispatch core로 축약.
- [ ] **C3. channels route_client submitter build-parts 골격 + route-to-SPOT spot/non-spot 쌍 (벤치)**
  - `channel_runtime.cpp:1085-1578` erased submitter 6종이 `create_envelope→metadata→trace(sent){11필드}→encode` 골격 복붙(`message_flow_event_t` 11필드 init 7회) → `build_route_envelope_parts` 헬퍼(A1 삭제 후 4종). `route_channel_runtime.cpp` spot/non-spot 쌍(`submit_send_parts:142-175`≈`submit_spot_send_parts:232-266` 등 3쌍, 차이는 `optional<routing_id> spot`) → 매개화 core 3개(dotnet/node R4 route-to-SPOT 전략). `native_route_backend.cpp` `submit_send:169-226` vs `submit_request:228-374` bridge/router 대상 해석도 병렬 중복.
- [ ] **C4. dispatch_error_reporter가 dispatch_options 값 복사 (벤치)**
  - `dispatch_error_reporter.hpp:24` 생성자가 `dispatch_options_t`를 **값**으로 저장(observer/logger/diagnostics 복사); `message_flow_tracer`는 const-ref borrow(대칭 깨짐). report 사이트마다 신규 생성 + `channel_runtime.cpp:543`이 값 반환 → report 1건당 2회 복사. const-ref borrow로 전환.
- [ ] **C5. actor-mesh relay 경로 + stale-retry + fault 분류 중복 (code-motion/벤치)**
  - `actor_client.cpp:278-345 relay_actor_packet` ≈ `actor_gateway_spot_bridge.cpp:363-422 relay_actor_packet_to_remote_actor_mesh`(create_envelope→make_spot_actor_packet_route_request→encode→decode_envelope_reply 골격, origin 직접 vs mesh 경유 차이만) → `remote_actor_packet_relay` 헬퍼. `send_to_actor_erased:96-131` ≈ `request_to_actor_erased:133-173`(resolve→submit→stale면 재resolve→retry) → template retry. 오류 분류(`map_actor_route_reply_error`/`map_native_exception`/`request_result_error_kind`) → 공용 fault-classifier(java C5). relay-kind 키 상수 3분산(`spot_bridge:26`/`spot_route_internal_dispatcher:21`/`actor_client:299` raw) → 공용 헤더.
- [ ] **C6. bound-session frame/sink + reconcile 루프 트윈 (code-motion/벤치)**
  - `encode_bound_session_frame`(`spot_runtime.cpp:43-71` ≡ `spot_route_internal_dispatcher.cpp:51-79`, 6B prefix+STREAM 헤더 바이트동일) + `submit_result_error_kind`(`:73-86`≡`:81-94`) + bound-session sink 클로저(`spot_runtime.cpp:3255-3288`≡`spot_route_internal_dispatcher.cpp:172-206`). ⭐**reconcile 루프 통째 트윈:** `location_auto_connect_host_service.hpp` ≡ `spots/spot_node_host_service.cpp`(`key_of`/`target_key`/`is_self`/`local_is_initiator`/`trace_*`/`publish_local`/tick diff 전부 near-verbatim, 문자열까지) → 공유 peer-reconcile 헬퍼(dotnet C12/node C8 대응, 두 host service로 갈라짐).
- [ ] **C7. 게임엔진 어댑터 godot ≡ axmol near-verbatim (code-motion)**
  - `connector/engines/godot/src/...cpp`(196) ≡ `connector/engines/axmol/src/...cpp`(196) 바이트 수준 동일(네임스페이스·`to_*_packet`·dispatcher setter 명명만 다름), 헤더도 동일. 공유 core 래퍼 + 엔진별 얇은 shim으로. **Unreal은 통합 대상 아님**(443줄, dispatch manual/weak ptr/PIE 후크/상태 구독 실질 상이). codec extension `register_payload_serializer` 바디도 msgpack≡protobuf(content_type만, java C10) — **모듈 독립성상 병합 금지, 관찰만**.
- [ ] **C8. handler/config 중복 + C9 프레임 프리픽스 인라인 (code-motion)**
  - `handler_registry.hpp:126-256` `on_request` 4벌 인라인(on_send/on_event는 헬퍼 `:447-603` 위임 — 대칭화). `framework_options.hpp:109-159`(request)≡`181-230`(send) route-installer 4-way if-constexpr. `framework_options_validation.hpp:43-66` 채널 capability 검증 3벌. **C9:** 6바이트 프리픽스 파싱이 6곳 손 인라인(framework `stream_host_service.cpp:509/560/541/614` + connector `framing.cpp:117`/`zlink_stream_calls.cpp:454`) — `decode_prefix` 헬퍼 부재; connector inbound read 이중화(`framing.cpp:114` vs `zlink_stream_calls.cpp:444`); `message_from_bytes` 3중 정의. env-trace gate 파싱 6벌(`app.cpp` 헬퍼 있는데 재인라인).
  - ⭐**(R4) stream host의 Boost.Asio ↔ native-fd 프레임 IO 이중 경로:** `stream_host_service.cpp`가 같은 프레임 규칙을 두 벌로 재구현 — `read_exact`(`:141`)/`read_frame`(`:508`)/`write_frame`(~541)/error-frame(~627) [Boost.Asio 템플릿] vs `read_exact_native`(`:482`)/`read_frame_native`(`:557`)/`write_frame_native`(`:601`)/error-frame(~660) [native fd]. prefix·header·payload·error-frame 조립이 lockstep. read/write byte-span 전송 추상화로 통합해 frame codec 1벌만 유지 → §B1 크기검증도 한 곳에.

**C 착수 순서:** C1(정본 문서+conformance 테스트 먼저)→C0(connector codec 자립화)→C6/C5/C3/C8(code-motion)→C2/C4(hot, 벤치)→C7.

---

## D. God-file 분해 (POSD/DDD)

공개 계약 헤더 + 테스트 통과 유지. hot dispatch/pump 클러스터만 벤치.

- [ ] **D1. (P0) `spots/spot_runtime.cpp` (3799줄)** — 코드베이스 최대. `spot_node_runtime_t` 엔진(2080-3797, ~1700줄)이 3도메인:
  - (a) actor 수명주기(`commit_accepted_actor_join`/`join_actor_to_spot`/`join_remote_actor`/`join_entry_spot`/`relay_actor_packet`/`notify_disconnected` 2172-2642) → actor lifecycle coordinator.
  - (b) ⭐inbound drain 상태기계(hot): `drain_actor_packets`(3090-3391, 301줄)/`drain_routed_packets`(3445-3766, 321줄)/`dispatch_subscription`(3393-3443)/`drain_subscriptions` → `spot_inbound_pump_t`(§C2 + §C6 착지). `find_actor_ref`(3098-3127) O(actors×factories) 선형 스캔(벤치, C10 동형).
  - (c) native attach/detach + peer snapshot(2957-3075).
  - 무상태 클러스터: anon-ns A(38-153 프레임인코딩/executor), anon-ns B(175-590 actor-location codec + request_spot_mesh_parts + report 헬퍼), `spot_context_t` 메시징(729-1350), `spot_handler_registry_t::invoke_erased`(1431-1570). CRUD+location claim → spot-lifecycle.
  - `spot_node_host_service.cpp`(522): 외부 링키지 reconcile 자유함수(:58-298)를 anon-ns 은닉 + "spot mesh auto-connect reconciler" 단일 유닛(§C6).
- [ ] **D2. (R2) `channels/channel_runtime.cpp` (1691줄)** — builders(625-855→`channel_builders.cpp`), message_bus(857-964), options(966-1029), route_client_t submitters(1071-1578 ~500줄, §C3 착지→`route_client_calls.cpp`), zlink_builder(1580-1691). `pending_operation_controller_t`(admission/timeout/retry/dead-letter hook ~143-278)는 **익명 ns 헬퍼가 아니라 테스트 가능한 internal class로** — pending limit·retry는 framework reliability 정책이라 한 곳에서 검증 가능해야(codex R2). `channel_outbound_exchange.cpp`(1021): `channel_native_client_t`(270-624 ~355→`channel_native_client.cpp`), native_publisher(626-714), transport util(133-266), submit 3종(716-1019, §C4).
- [ ] **D3. `actors/actor_gateway_runtime.cpp` (1096줄)** — 6타입 절단: stream_relay_header(22-46), bound_session_t(84-227), actor_context_t(229-397), session_actor_t(399-583), session_actor_manager_t(585-775), actor_gateway_t+detail(777-1096). `host/actor_gateway_spot_bridge.cpp`(917): actor_join_native(65-138), remote_spot_relay(140-422, §C5 착지), route 결정 계층(424-615), binding-selector(617-693), configure orchestrator(738-915 유지). **D-Cross:** `configure_actor_gateway_spot_bridge`가 actor↔spot 양방향 콜백 배선(back-door 결합) → binding-selector로 격리(java D-Cross 동형).
- [ ] **D4. streams/connector god-files** — `stream_runtime.cpp`(1042)→dispatch_executor / dispatch(65-190) / types(225-542) / **header_codec(587-906, §C1 정본)** / runtime. `connector/core/.../zlink_stream_calls.cpp`(1564)→support(34-207) / frame_io(209-497, §C9) / heartbeat(499-540) / inbound_pump(542-796) / outbound_pump(798-1035) / calls(1041-1564). `connector_runtime.cpp`(1043)→shared_runtime(28-198) / contracts(313-512) / connect(514-808) / runtime. `stream_host_service.cpp`(957, §B1 착지).
- [ ] **D5. `host/app.cpp` (732줄)** — `add_zlink_framework`(358-637 ~280줄) composition root: location 배선(439-533 ~95줄 최대)/channel(544-560)/spot(561-576)/actor(577-590)/stream-http(624-633) → `wire_*_services()` 스테이지 헬퍼(각 번들이 자기 add 소유). teardown cascade 2벌(catch 678-685 ≡ 정상 688-718) → `teardown()` 단일화. (backend obscurity·이중 파사드는 C++ 부재.)
- [ ] **D6. 계약 헤더 `configuration/framework_options.hpp` (1470줄, 최대 계약 헤더)** — `configuration/builders/` 하위로: `channel_builders.hpp`(328-819), `spot_builders.hpp`(821-1125), `stream_builders.hpp`(1127-1273), `handler_group_builder.hpp`(36-282), `codec_builder.hpp`(284-326); `framework_options.hpp`는 `zlink_framework_options_t` aggregate + 재-export만. `handler_registry.hpp`(612): §C8 헬퍼화 + 무거운 template 구현부(447-603)를 `detail/*.inl` 분리. **주의:** `contracts/spots/spot.hpp`(1570)는 public 계약 → 분해 대상 아님.
- [ ] **D7. (R3) `extensions/framework-locations-redis/.../redis.hpp` (1581줄, header-only)** — 논리적으론 이미 클래스 분해됨(`redis_location_scripts_t:47`, `key_schema_t:213`, `row_codec_t:479`, `worker_t:804`, `store_t:881`, script_result). 그러나 **물리적으로 1581줄 단일 헤더가 impl 전부를 include 소비자에게 노출**(Lua script/worker threading/Redis client lifecycle까지). 빌드·배포 정책 확인 후 구현을 `src`로 이관하거나 `detail/*.hpp`로 나누고 public include에는 최소 facade만 남김(codex R3). **JSON row format·Redis key schema는 cross-language parity 대상이므로 테스트 먼저 고정.** §A1 죽은 `remove_lease` Lua 동반 정리. 위험: code-motion(header-only 유지 시)·계약(테스트 고정 후).
- [ ] **D8. (R5, P2) `http/http_request_pipeline.cpp` (776줄)** — 이미 작은 함수로 나뉘어 급하진 않으나 HTTP 기능 추가 전 경계 정리: URL/query/route match(96-244,299-348→`http_route_matcher`), request/header/context/body binding(401-482→`http_request_binder`), health/readiness/liveness(350-399,568-599→`http_system_routes`), framework error→status + route-miss(510-566→`http_response_mapper`), middleware before/after + route invocation(624-748→`http_middleware_runner`). 기능 추가 없으면 R1~R4 이후. (`http_listener.cpp` fd_stream+listener는 얇음, 손대지 않음.)

**D 착수 순서:** 무상태/무위험부터(D4 header_codec/frame_io, D2 builders/route_client, D6 계약 헤더 분할) → god-class 엔진 분해(D1 inbound pump, D3 spot bridge)는 §C2/§C5 벤치와 동반.

---

## 부록 A. 다른 언어 대조 (C++ = 정본)

**C++에 부재/이미 해소**(다른 언어의 결함이 여기서 유래하지 않음, 재도입 금지): node B4(수신 루프 throw 종료 — C++는 `result_t` 반환 + detached worker), dotnet/node B6(HandlerNotFound 예외 이중 생성 — 1회 생성), dotnet A-CH1(server-bundle bridge vestigial — 실 attach), B8(AlreadyOwned — `location_write_status_t`에 상태 자체 없음, takeover=bool로 scope 누출 없음), dotnet C12/node C8 live-row 필터·lease tracker·observed guard 중복(단일 `owner_is_live` 집중), mesh-scan resolver 쌍둥이(단건 조회), dotnet C16 http 헤더 조회 3벌(단일 정의), node R6/backend obscurity(타입안전 `native_route_backend_t`), dotnet D1 이중 파사드, **dotnet E2/java D8 Redis store 다책임(이미 `scripts_t`/`key_schema_t`/`row_codec_t`/`worker_t`/`store_t` 클래스 분해 완료)**, java B1 무조건 stdout `[boot]` 계측(전부 env-gate).

**C++에 존재**(전 언어로 전파되는 원천 결함/중복): C1(wire 트윈 + 규칙 drift), C2(dispatch 상태기계), route-to-SPOT 4-way(§C3), actor-mesh relay 중복(§C5), reconcile 루프 트윈(§C6), spot/channel god-file. **C++ 고유**: B1(STREAM 서버 크기 상한 부재), B2(outbound_calls 무한증가), B3(30s busy-wait), B6(godot/axmol on_packet no-op), C7(게임엔진 어댑터 중복), C0(connector typed-codec 결합).

## 부록 B. tracked 산출물 정리 + 방법론 / 검증 게이트

- **⚠️ tracked generated/cache(별도 cleanup PR, `.gitignore` 보강):** `framework/languages/cpp/.cache/clangd/index/**` — **git-tracked 677개**(로컬 clangd index, source 아님), `framework/languages/cpp/Testing/Temporary/CTestCostData.txt`(CTest 산출물). 리팩토링과 섞지 말고 별도 정리.
- **ignored 산출물(로컬 정리 가능):** `build*/`, `Testing/Temporary/LastTest.log`, `e2e/**/logs/*.log`, `samples/**/{build,logs}/`.
- **⚠️ address→ref 등 진행 중 변경:** worktree에 C++ framework runtime/e2e/sample 변경이 이미 있음 — 진행 중 파일을 dead 판정하지 말 것.


- 8-에이전트 read-only 병렬 리뷰(spots / channels+messaging / actors / streams+connector / host+backend / locations+redis+http / handlers+config+contracts / codecs+extensions+engines) + 메인 루프 grep 검증.
- dead 판정: `framework/{src,include}` + 전 `tests`/`e2e`/`samples` + connector/extension 테스트 grep(build/install/gcov 제외). 익명 ns/static/detail 우선, public 계약 헤더 보수적, 동명 소유타입 분리.
- **⚠️ 동시 세션 주의:** 병렬 `kairos-code-dev` 세션이 파일을 동시 수정/revert한 이력. 착수 전 `git fetch`+동기화, 검증 즉시 커밋+푸시.

```bash
# build tree가 stale하면 먼저 CMake configure. (build 디렉터리명은 작업자마다 다를 수 있음)
cmake --build framework/languages/cpp/build --target \
  test_cpp_framework_channel_messaging test_cpp_framework_spot_runtime \
  test_cpp_framework_ActorGateway_actor_session_relay test_cpp_framework_stream_framework \
  test_cpp_framework_message_flow test_cpp_framework_locations_redis \
  test_cpp_framework_contract_headers test_cpp_framework_layout_contract test_cpp_framework_sample_parity
ctest --test-dir framework/languages/cpp/build -R \
  'test_cpp_framework_(channel_messaging|spot_runtime|stream_framework|message_flow|locations_redis|contract_headers|layout_contract|sample_parity|ActorGateway_actor_session_relay)' \
  --output-on-failure
framework/languages/cpp/e2e/run_e2e_all.sh && framework/languages/cpp/samples/run_samples.sh
# connector/http-client 자체 테스트도 별도 실행
```

hot(벤치) 항목은 baseline vs patched 실측 무회귀 후에만 커밋. wire/codec 변경은 §C1 conformance 테스트로 framework↔connector 상호운용 확인. C++는 정본이므로 계약 표면 변경 시 다른 언어 parity 영향 선검토([[CLAUDE.md]]).
