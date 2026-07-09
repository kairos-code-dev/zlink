# `bindings/cpp` POSD·DDD 리팩토링 통합 수정 목록

> 2026-07-08 cpp 바인딩 전수 POSD/DDD 리뷰와 Codex 재검토 결과를 합친 통합 수정 목록.
> 대상: `src/Runtime`(~11.2k, Service 5.5k 최대) + `include/`(~6.4k 공개 헤더).
> **cpp는 다른 바인딩이 미러링하는 레퍼런스 바인딩**이라 공개 헤더 변경은 전 언어 파급(E 제외).
> 파일:라인은 리뷰 시점 기준이므로 편집 전 재확인한다. hot 항목은 커밋 전 baseline vs patched 벤치 필수.

위험 표기: **없음**(control plane) / **code-motion**(hot 경로 이동이나 명령 동일) / **벤치**(hot 구조에 닿음). 체크박스는 완료 시 갱신.

**공통 주의:** `zlink_cpp`는 소스에서 STATIC 빌드(prebuilt `.a`/`.lib` 미체크인) → 헤더↔`.cpp` 이동은 ABI 우려 없음. `_nowait`/deliberate alias는 의도된 것일 수 있으니 주석 확인. `context_option::socket_limit=3`/`thread_priority=3` 충돌은 core `zlink_enum.h:12-13`을 충실히 미러한 것(binding 결함 아님).

**통합 검토 반영:**
- Codex 리뷰에서 확인한 `spot_operation_state_t`의 범용 state 비대화는 C0으로 채택한다. 다만 `spot_state.hpp:204-210`의 thread-local pool과 capacity 재사용은 성능 계약이므로, 분해는 heap allocation 없이 진행해야 한다.
- public Service 헤더의 include-cycle 매크로는 C9로 채택한다. 공개 class shape 변경 없이 forward declaration/detail header 정리가 목표다.
- `submit_payloadless_request`와 `actor_join_callback_trampoline`은 A9 cleanup 후보로 추가한다.

---

## A. 삭제 트랙 (dead 코드/파일)

- [x] **A1. `zlink::error_code` enum 전체 삭제** (없음, high) — `Contracts/Errors/results.hpp:70-76`(efsm/enocompatproto/eterm/emthread). 전 트리 무참조 + `ZLINK_HAUSNUMERO+51..54` 리터럴 복붙(HDR-001의 drift 원인). public이나 소비자 0.
- [x] **A2. `zlink::protocol_error` enum 삭제** (없음, high) — `results.hpp:79-82`(zmp_malformed_command_hello). 무참조(다른 `protocol_error` 히트는 `request_result.hpp:19`의 무관 enumerator).
- [x] **A3. `spot_service_attachment_stats_t` struct 삭제** (없음, high) — `spot_node_models.hpp:508-517`. 생산자/소비자/테스트 0 → 미배선 stats API 스캐폴딩.
- [x] **A4. `socket_t::try_send_result`/`try_publish_result` dead protected 멤버 삭제** (없음, high) — `socket_contracts.hpp:90-119` 6 오버로드 + `socket.cpp:177-217,284-310` 정의. 전 트리 call site 0(각 파생 소켓/`spot_t`는 자체 try-result 구현). protected라 외부 소비자 도달 불가.
- [x] **A5. dead generic `native_handle(T&)` 템플릿 삭제** (없음, high) — `Native/socket_handle.hpp:101-111`. `handle()`가 protected+비-friend라 도달 불가·인스턴스화 불가.
- [x] **A6. dead 별칭/중복선언 정리** (없음) — `subscription_event.hpp:29` `subscription_entry_t` alias(무참조), `actor_models.hpp:29` `actor_id_string()`(무참조 pass-through), `spot_node_models.hpp:84` 중복 forward-decl(routing_id.hpp에서 이미 정의). 현재 트리에서 중복 forward-decl은 발견되지 않아 무참조 alias/pass-through만 제거했다.
- [x] **A7. dead PUBLIC 오버로드 — 삭제 전 cross-language 확인** (medium) — `socket_t`의 decomposed-output `subscribe(rid_out, topic_out, parts_out, flags)`(`socket_contracts.hpp:136-153`)·`subscription_event(rid_out, subscribed_out, topic_out, flags)`(`:158-161`). 내부 call site 0(struct 기반 형제만 사용)이나 **PUBLIC** → deprecate 후 제거 또는 spec 확인. (2026-07-09 완료: C++ spec는 `topic_message_t&`/`subscription_event_t&` 출력 저장소를 계약으로 설명하고 decomposed overload 사용처 no-hit. struct 기반 `subscription_event(subscription_event_t&)`는 native 수신 로직을 직접 보유하도록 정리하고 decomposed public overload 삭제. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
- [x] **A8. phantom enumerator** (medium, HDR-002/003) — `rid_duplicate_policy_t::replace`(`routing_id.hpp:79`), `monitor_event::connection_ready_changed`(`events.hpp:25`), `monitor_state::send_ready`(`status.hpp:24`) 무참조. "alias" 표기는 forward-compat 의도일 수 있어 문서화-or-삭제 한 번에. (2026-07-09 완료: native/spec는 `REJECT`/`HANDOVER`, `CONNECTION_READY`, `READY`만 계약으로 노출함을 확인. C++-only aliases `rid_duplicate_policy_t::replace`, `monitor_event::connection_ready_changed`, `monitor_state::send_ready` 삭제. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
- [x] **A9. dead internal helper 2건 삭제** (없음, high) — `actor_detail.hpp:237` `submit_payloadless_request`는 정의 외 참조 0. `detail.hpp:122` `actor_join_callback_trampoline`도 정의 외 참조 0이며 실제 request callback은 다른 trampoline을 사용한다. 둘 다 internal inline helper라 public surface 영향 없음.

## B. 결함 수정 (correctness — 리뷰 중 발견)

- [x] **B1. bind/connect/unbind/disconnect가 native 정밀 result를 버리고 coarse errno 재파생** (없음, caller 확인 — **최상위**)
  - `socket.cpp:62-88`이 `zlink_bind/connect/unbind/disconnect`의 rich return(bind 501-505/connect 601-607, C++ enum과 수치 동일)을 버리고 `zlink_errno()`+`*_from_errno`로 재파생 → `internal_error`/`not_found`/`conflict`/`busy` 영구 소실. **바로 아래 `disconnect_rid`(`:90-96`)는 `static_cast<connect_result_t>(rc)`로 올바르게** 함. 같은 파일 내 두 전략. rc 직접 캐스팅으로 통일 + `unbind`/`disconnect` byte-identical 중복을 `detail::throw_native_bind_or_connect_failure<E,R>(rc)` 헬퍼로. **주의**: `.result()` 관찰값이 정밀해짐 → 기존 caller가 coarse 값에 pattern-match하는지 확인.
  - **추천 결정**: 형제 `disconnect_rid`가 이미 `static_cast`로 올바르게 하므로 **의도가 증명된 버그** → 정밀화 진행. 착수 첫 단계로 `bind_result_t`/`connect_result_t`에 `switch`하는 caller(테스트 포함) 감사 후 함께 갱신. 예외 **타입**은 불변(`bind_error_t`/`connect_error_t` 유지, 값만 정밀)이라 cross-language 파급 없음.
- [x] **B2. `poller_t::wait`/`wait_socket_items`가 native error 버리고 `invalid_handle` 하드코딩** (**벤치**, 예외타입 결정)
  - `poller.cpp:307-339,356-390`이 `zlink_poller_wait`의 `error` out-param을 무시하고 non-EINTR/EAGAIN을 항상 `recv_error_t(invalid_handle)`로. **같은 파일 `poll()`(`:487-511`)은 `config_error_t(static_cast<config_result_t>(error))`로 올바르게** 함. → `poller_t::wait` 실패가 항상 "invalid handle"로 오보고. 실패 브랜치만 수정(성공 hot path 불변, 중립). `detail::throw_poll_failure(error, err)` 헬퍼로 3 call site 통일. **주의**: `wait`가 `recv_error_t` 유지 vs `poll()`처럼 `config_error_t` 정렬은 관찰 가능한 예외타입 결정.
  - **추천 결정: (a) `recv_error_t` 유지, 값만 정밀화.** 타입을 `config_error_t`로 바꾸면 레퍼런스 바인딩의 공개 예외 타입이 바뀌어 전 언어 catch 사이트에 파급되고 기존 `catch(recv_error_t&)`를 깨뜨림. 지금은 오분류만 고치고, "poller 오류를 config 도메인으로 볼지"는 E-track(cross-language spec)으로 미룸.
- [x] **B3. routing-id thread-local ring buffer의 미문서화 "≤8 동시" 용량 계약** (**벤치**/저위험 문서화)
  - `Core/routing_id_access.hpp:56-69`의 `thread_local zlink_routing_id_t native[8]`가 send hot path(`socket.cpp:160,172,202,214`)에서 native 포인터를 할당 없이 넘김. Service의 2-인자 call site(`spot_send.cpp:155-156` 등)는 한 표현식에 2개 동시. 매직 `8`이 무문서·무assert → 미래에 ≥9 동시 시 조용한 routing-id 손상. (a) 즉시: 불변식 주석 + debug assert(wraparound 감지). (b) 철저: 각 2-인자 call site가 로컬 `zlink_routing_id_t` 선언해 `&local` 직접 전달(Service 디렉터리 파급, 후속 트랙). (a) 지금, (b) cross-directory 추적.
  - 2026-07-09 완료: `routing_id_native()`에 즉시 단일 native handoff 전용 불변식 주석과 ring capacity 상수 추가. socket send hot path, spot send/request/reply, route bridge, raw request/reply, received send/reply의 다중 routing-id 전달은 `routing_id_native_value()`로 만든 함수 스코프 local native 값의 주소를 넘기도록 변경. 동시 `routing_id_native()` 호출 패턴 검색 no-hit. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS.

## C. 구조 통합 — 지식 중복 소거

- [x] **C0. `spot_operation_state_t`를 역할별 내부 state/command로 분해** (**벤치**, POSD 핵심)
  - `spot_state.hpp:25-54`의 `spot_operation_kind_t`가 raw socket send/publish/request, SPOT send/request/reply, `received_t` send/reply, actor send, stream-bound actor send를 한 enum에 모은다. 같은 state가 `spot`, topic/channel, routing id cache, request seq, raw socket, `received_t`, `spot_node_t`, stream handle, actor, actor id를 함께 가진다(`spot_state.hpp:56-88`).
  - 새 send 계열을 추가할 때 enum, 공용 state 필드, reset, submit switch가 함께 바뀌므로 변경 증폭이 크다.
  - public builder surface는 유지한다. 내부 state를 역할별로 나누되 thread-local pool, in-place reset, message move/restore 정책을 보존한다. per-call heap allocation, `std::function`, virtual dispatch 추가 금지.
  - 2026-07-09 완료: `spot_operation_state_t`를 `message`, `raw`, `spot`, `received`, `actor`, `stream_actor` 역할별 command/state로 분해했다. routing-id native cache는 각 command의 `routing_target_t`에 귀속했고, `router_socket_t::send(target)`의 raw target cache 경로도 별도 검증했다. thread-local pool acquire/release와 in-place reset은 유지했고, hot submit 함수에 `std::function`/virtual dispatch/per-call heap allocation을 추가하지 않았다. `bindings/cpp/tests/run_tests.sh`, `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md` PASS.
- [x] **C1. count-then-fetch ENOBUFS 재시도 루프 7벌** (없음) — `spot_node.cpp`의 peers/peers_query/subjects/internal_sockets/spots/actors(`:365-593`) + `spot.cpp:560-586`(actors). `template<NativeT,FetchFn> fetch_growable_native_array(fetch)`로(신규 `native_array_fetch.hpp` 또는 `service_model_access.hpp`). ENOBUFS-only 재시도 정확히 보존(~150줄).
- [x] **C2. 오퍼레이션 빌더 ctor/dtor/state 접근자 plumbing ~17벌** (**벤치**) — `send/reply/request/actor_operations.cpp`의 17 typestate 빌더가 dtor+move+2 ctor+`state()` 쌍을 verbatim 반복. private header-internal CRTP `operation_builder_base_t<Derived,StateT>`로(공개 메서드 불변). **주의**: `request_submitter.hpp:95-97` "callback/awaitable 경로 분리 유지, promise/future 라우팅 금지" + `spot_state.hpp:204-210` thread-local pool(send/request/reply는 pool, actor-join 계열은 make_unique) → `if constexpr` teardown 정책 스위치 필수, 두 경로 병합 금지. widely-friend 헤더라 재검증. (2026-07-09 완료: `Contracts/Service/operation_builder_base.hpp`에 private base와 teardown policy 추가. send/reply/request builder는 `pooled_operation_state_policy_t`로 `release_state` 경로를 유지하고 fluent stage 전환은 `release_state_ptr()`로 같은 pooled state를 이전한다. actor operation builder는 `unique_operation_state_policy_t`로 기존 `make_unique` 소유권을 유지했다. callback/awaitable submit 함수는 병합하지 않았다. `bindings/cpp/tests/run_tests.sh`, `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md` PASS)
- [x] **C3. pimpl move-construct/assign dance 3벌** (없음) — `spot_node.cpp:120-143`·`spot_route_bridge.cpp:104-127,311-334`의 "close→move→null이면 reseed" 동일. `move_assign_pimpl_and_close(Derived&, Derived&&, int&)` 헬퍼로. **주의**: `spot_t`(`spot.cpp:189-213`)는 `dispatch_state->owner` 재설정이 있어 다르므로 별도 유지(정당한 비대칭).
- [x] **C4. `received_t`/`topic_message_t` "single-or-multi lazy parts" 전체 이중** (**벤치**) — `received.cpp` vs 인터리브된 `topic_message` 본문의 `first_part`/`materialize_parts`/`parts`/`single_part_or_throw`/`close`. `detail::lazy_parts_t` 컴포지션(또는 Runtime-only 공유 base)로, 공개 메서드/시그니처 불변. hot(양쪽 수신 경로). **주의**: public 헤더 private 멤버 레이아웃 변경 → friend(`detail::*_access_t`) 외 의존 없음 확인. (2026-07-09 완료: `Contracts/Messaging/lazy_message_parts.hpp`에 `detail::lazy_message_parts_t` 추가, `received_t`/`topic_message_t`가 private composition으로 공유. public 메서드/시그니처 불변, 기존 friend access 직접 `_parts` 의존 없음 확인. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
- [x] **C5. hot path "invalid handle" 가드 10벌** (code-motion) — `spot_receive.cpp`의 publish/send_channel/subscribe/subscription_event/try_publish_result(`:117-323` 9곳) + `spot_send.cpp:79-82`(throw 변종). `spot_t::impl::require_handle() noexcept` inline 헬퍼로(각 `if(!require_handle()) return -1;`). 동일 codegen, TU 경계 인라인 확인(`spot_impl.hpp` 이미 include). (2026-07-09 완료: `spot_t::impl::require_handle()`와 throw 변종 helper 추가, return 경로 9곳과 throw 경로 1곳 치환. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
- [x] **C6. reply multipart 제출이 기존 공유 헬퍼 미사용 재구현** (code-motion) — `spot.cpp:140-176`(`submit_reply_messages`)가 `zlink::detail::submit_message_parts_close_on_failure`(`native_message_parts.hpp:283-294`, 형제 `send_to_spot`가 이미 사용)를 손수 재구현. 공유 헬퍼로 위임. **주의**: single 변종(`submit_single_reply_message`, by-value)은 소유권 다르니 병합 금지. `throw_if_reply_flags_unsupported` pre-check + reply의 "항상 throw, backpressure-return-false 없음" 보존.
- [x] **C7. `set_routing_id`/`get_routing_id` 3벌** (없음) — `dealer.cpp:47-59`·`router.cpp:59-71`·`stream.cpp:61-73` byte-identical. `Sockets/detail.hpp`에 `set/get_routing_id_or_throw` 헬퍼로(~24줄).
- [x] **C8. per-socket-type option accessor 템플릿 6벌** (code-motion) — `Options/socket_options_detail.hpp:75-155`의 common/router/dealer/pub/sub/stream 6쌍이 enum/native타입/함수포인터만 다른 4줄 어댑터. `option_traits<OptionId>` + generic `get/set_typed_option_value<T>`로. 6 `.cpp` call site 이름치환. (2026-07-09 완료: `socket_options_detail.hpp`에 `option_traits<OptionId>`와 typed get/set/string helper 추가, 5개 옵션 구현 파일 호출부 이름 치환. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
- [x] **C9. public Service 헤더 include-cycle 매크로 제거** (없음, public header 검증) — `CPP_BINDING_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE` 제거. `operation_contracts.hpp`는 필요한 message/actor model 헤더와 `spot_node_t`/`spot_t` forward 선언만 사용하고, `spot_node.hpp`의 public 단독 include 동작은 마지막 `spot.hpp` include로 유지. (2026-07-09 완료: 매크로 검색 no-hit, `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS)
  - `operation_contracts.hpp:4-6`와 `spot.hpp:4-6`이 `CPP_BINDING_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE`를 정의해 `spot_node.hpp` include 동작을 제어하고, `spot_node.hpp:327-329`가 다시 이 매크로에 따라 `operation_contracts.hpp`/`spot.hpp`를 include한다.
  - 공개 계약 헤더가 include 순환 회피 전략을 알아야 하므로 빌드 배선 지식이 public surface에 샌다.
  - forward declaration과 작은 detail header로 cycle을 끊는다. public class layout, inline 함수 위치, aggregate include 테스트를 함께 확인한다.

## D. 구조 개선 — 중간 이하 (기회 될 때)

- [x] **D1. `context.cpp` bare int option ID → named enum** (없음) — 모든 accessor가 `get_option_raw(<bare int>)`(io_threads=1, socket_limit/thread_priority 둘 다 3). `Options/option_ids.hpp` 패턴대로 `enum class context_option_id : int { ... = ZLINK_* }` 도입해 native 충돌을 정의 지점에서 가시화(오해 landmine 제거).
- [x] **D2. `set_send_ready_handler` pass-through 배치 일관화** (없음) — `pair.cpp:36-39`·`router.cpp:54-57`만 out-of-line, 나머지 5 소켓은 헤더 inline. pair/router도 헤더 inline로 이동(STATIC 소스 빌드라 ABI 무관).
- [x] **D3. `error_code` 매직넘버 표현 (A1과 함께)** — 삭제가 1순위지만 유지 시 `static_cast<int>(EFSM)` 등 C 매크로 파생으로. A1에서 `error_code` enum 자체를 삭제해 별도 매직넘버 표현이 남지 않는다.
- [x] **D4. `_t` 별칭 이중명명 8종** (compatible) — `spot_node_models.hpp:94-101`이 8 enum을 bare+`using X_t=X`로 이중선언, 공개 accessor는 `_t`만 사용(bare는 내부 static_cast만). enum 선언에 `_t` 직접 부여 + ~7 내부 cast site 갱신 + `using` 제거. 외부 caller 소스호환(이미 `_t`만 봄).

## E. 교차 언어 계약 결정 필요 (레퍼런스 헤더 변경 = 전 언어 파급)

- [x] **E1. `rid_duplicate_policy_t::replace` C++-only enumerator 제거** — native `zlink_rid_duplicate_policy_t`엔 REJECT/HANDOVER만(`zlink_enum.h:138-142`), REPLACE 없음.
  - 2026-07-09 완료: spec/site/native enum은 `REJECT`/`HANDOVER`만 노출하고 `replace` 참조는 계획서와 C++ enum 선언뿐임을 확인. `routing_id.hpp`에서 `replace = 1` alias 삭제. `git diff --check -- bindings/cpp bindings/doc/plan/cpp-binding-posd-ddd-refactor-list.ko.md`, `bindings/cpp/tests/run_tests.sh` PASS.
- [x] **E2. `spot_t` 314줄 통합 facade** — pub/sub+routed+channel+actor+dispatch를 한 클래스에, `message/pubsub/routed_socket_contracts` base 미컴포즈. 의도된 cross-language SPOT facade일 개연성 → 분해는 공개 class shape 변경, 단독 금지. (2026-07-09 완료: `bindings/doc/spec/cpp/README.ko.md`는 `spot_t`를 구체 facade 계약으로 설명하고, 공통 spec의 Spot facade 절도 publish/send/request/reply/actor/dispatch 시작점을 같은 facade에 둔다. C++ 단독 base 분해는 공개 class shape 변경이라 수행하지 않음. 후속 변경은 공통 framework/spec 설계 트랙 필요.)
- [x] **E3. `context_option` 3=3 충돌 upstream** — core `zlink_enum.h`의 `ZLINK_SOCKET_LIMIT`/`ZLINK_THREAD_PRIORITY` 동값을 cpp가 충실 미러. 결함이면 core spec 차원 — upstream 제기(cpp 단독 수정 아님). (2026-07-09 완료: `core/include/zlink_enum.h`, `bindings/cpp/include/zlink/Contracts/Core/routing_id.hpp`, `bindings/cpp/src/Runtime/Options/option_ids.hpp`, `doc/site/docs/api/context.md`가 모두 `ZLINK_SOCKET_LIMIT = 3`, `ZLINK_THREAD_PRIORITY = 3`을 공개/내부 계약으로 노출함을 확인. D1에서 named enum으로 충돌을 가시화했고, C++ 단독 값 변경은 계약 불일치라 하지 않음.)

---

## 핫패스 보존 게이트 (절대 위반 금지)

poller wait 성공 브랜치 · routed send `routing_id_native` 무할당 포인터 경로 · 단일부 수신 pool adoption · callback/awaitable 요청 경로 분리(promise/future 라우팅 금지) · `spot_operation_state_t` thread-local pool acquire/release · CRTP는 virtual 없음 유지. **hot TU 변경(B2, B3, C2, C4, C5)은 커밋 전 baseline vs patched 벤치 무회귀 증명, per-message allocation/virtual dispatch 추가 금지.**

## 권장 실행 순서

A(삭제, 레퍼런스 표면 축소) → B1·B2·B3(정밀도/손상 결함) → C1·C3·C6·C7(control-plane 통합) → C5·C8·D(기회순) → C2·C4(벤치 게이트) 마지막. E는 별도 계약 트랙(레퍼런스 헤더라 신중). B1/B2는 관찰 가능한 예외값 변화라 caller pattern-match 확인 선행.
