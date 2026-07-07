# `core/src` POSD·DDD 리팩토링 계획

> 배경: 2026-07-07 `core/src` 전체(488파일, 8개 하위 영역)를 POSD(deep module,
> information hiding, pass-through 제거, complexity를 아래로) 및 DDD(도메인 경계,
> 단일 책임) 관점에서 전수 감사했다. 이 문서는 그 결과 확정된 42건의 리팩토링
> 후보를 hot-path 위험도와 함께 기록하고, 실행 순서와 검증 게이트를 정의한다.
>
> 목표: 메시지 hot path의 성능 특성(call-graph 깊이, 인라이닝, per-message
> allocation, virtual dispatch 횟수)을 그대로 유지하면서, 남은 god file,
> 중복 로직, shallow wrapper, 누출된 내부 표면을 정리해 core의 구조 복잡도를
> 실제로 줄인다.
>
> 대상: `core/src/api/`, `core/src/runtime/` 전체.
> 제외: `core/src/runtime/services/registry|discovery`(조사 시점 워킹트리에서
> 삭제 대기 중), `core/perf`(별도 완료된 계획), 공개 C API 시그니처(변경 없음).

## 0. 새 컨텍스트 시작 전 확인

새로운 컨텍스트에서 이 문서로 작업을 시작할 때는 아래를 먼저 확인한다.

- 저장소 규칙의 정본은 루트 `AGENTS.md`다. 이 문서와 충돌하면 `AGENTS.md`가
  우선한다.
- 3절의 파일:라인 범위는 2026-07-07 감사 시점 기준이다. 편집 전에 현재 코드로
  위치를 재확인하고, 크게 어긋나면 이 문서를 먼저 갱신한다.
- 이 문서는 **동작 변경 없는 구조 리팩토링** 문서다. 공개 C API 시그니처,
  wire format, errno 의미(단 T1-01의 divergence 해소는 예외로, 정본 확정이
  목적)는 바꾸지 않는다.
- hot path 불변 조건이 최우선이다. 2절의 가드레일을 어기는 변경은 규모가
  아무리 작아도 진행하지 않는다.
- 과거 회귀 전례: thread-local msg pool, fanout add_ref 배칭은 routed/reqrep
  기준 ~7–15% 회귀로 revert됐다(2026-05-30). **재도입 금지.**
- perf 인접 항목(위험도 "조건부", 그리고 code-motion이라도 send/recv 경로에
  닿는 항목)은 반드시 baseline vs patched 벤치 비교 후 커밋한다. 측정 없는
  perf 커밋 금지.
- 커밋 단위: 정책 변경이 포함된 항목(T1-01 등)은 반드시 단독 커밋. 순수
  삭제·중복 제거형(T1-02/04/05/06)은 하나의 정리 커밋으로 묶어도 된다. 그 외
  구조 이동 항목은 항목당 독립 커밋(revert 단위 확보).
- 항목 완료 시 이 문서의 해당 체크박스를 갱신한다.

## 1. 목표와 비목표

목표:

- 같은 도메인 규칙이 한 곳에만 존재하도록 중복을 소거한다(errno 매핑, envelope
  framing, reconnect 백오프, ZMP handshake 등).
- state만 추출되고 동사(연산)가 남은 half-extraction을 마무리한다(spot actor,
  no_bind, ctx bootstrap 등).
- API 표면 파일에서 wire codec / dispatch / lifecycle을 분리해 형제 디렉터리
  (`api/spot/request_reply`의 16파일 분해)와 분해 수준을 맞춘다.
- 추상화를 더하지 않는 pass-through 레이어를 제거한다.

비목표:

- 성능 개선. 이 계획의 종료 기준에 성능 향상은 없다. 기준은 **비회귀**다.
- hot path 본체의 구조 개선. 6절의 제외 판정 항목은 문서화로만 대응한다.
- 공개 계약 변경. binding/framework에 보이는 표면은 그대로다.

## 2. hot-path 가드레일

### 2.1 hot-path 마커 파일 (변경 시 벤치 게이트 필수)

`hot path` 주석이 있는 파일: `runtime/core/pipe.{cpp,hpp}`,
`runtime/core/recv_internal.cpp`, `runtime/core/msg.cpp`(refcount),
`api/socket/socket_message_api.cpp`, `api/socket/socket_message_send_api.cpp`,
`api/socket/socket_message_recv_api.cpp`,
`api/socket/socket_request_reply_router_api.cpp`,
`api/socket/socket_request_reply_runtime_io.cpp`,
`api/socket/request_completion_queue_internal.cpp`,
`api/socket/request_timeout_scheduler_internal.cpp`,
`runtime/sockets/internal/dist.cpp`, `runtime/sockets/internal/lb.cpp`,
`runtime/sockets/common/socket_base_msg.cpp`,
`runtime/sockets/router/router_send_path.cpp`, `runtime/sockets/router/router.hpp`,
`runtime/engine/asio/i_asio_transport.hpp`, `runtime/transports/ws/asio_ws_engine.cpp`.

spot actor 쪽은 마커가 없으므로 추론 기준을 적용한다: `send_actor_gateway_*`
framing, `zlink_spot_node_actor_send/recv/forward` 계열, spot data plane의
`forward_*`/`stage_message`/`flush_staged_messages`/`drain_*`가 hot이다.

### 2.2 위험도 정의

- **없음**: control plane(연결/옵션/lifecycle/에러 경로)만 건드림. 벤치 불요,
  빌드+테스트로 충분.
- **code-motion**: hot 경로가 지나는 코드를 옮기되 명령 수준 동일(파일 분리,
  header-inline 헬퍼 추출, 기존 헬퍼 호출로 교체). 인라이닝을 깨지 않는 형태
  (헤더 inline 또는 동일 TU 유지)를 강제하고, 의심되면 벤치.
- **조건부**: hot 경로의 구조 자체에 닿음(상속 구조, fast-path 분기, per-message
  primitive). 사전 설계 검토 + baseline vs patched 벤치 무회귀 증명 없이는
  진행하지 않는다.

### 2.3 금지사항

- hot 경로에 out-of-line 함수 호출, 가상 호출, per-message 할당 추가 금지.
- `pipe.cpp` write 변형 통합, `msg.cpp` refcount teardown 헬퍼화 금지(6절).
- thread-local msg pool, add_ref 배칭 재도입 금지(회귀 전례).
- `zlink_poller_wait` drain 루프, `send_activate_write` self-dispatch,
  `part_count==1` fast move, `decode_and_push` 계열은 제자리 유지.

## 3. 작업 목록

각 항목: 대상 → 내용 → (위험 / 규모).

### 3.1 티어 1 — 소형·안전 (10건)

- [x] **T1-01. `zlink_request_result_t → errno` 순방향 매핑 3중 divergence 통일** ⭐
  - `api/actor/spot/service_spot_actor_api.cpp:734`,
    `api/spot/request_reply/service_spot_request_reply_channel_bridge.cpp:29`,
    `api/spot/core/service_spot_route_bridge_channel_reply_internal.cpp:21`
  - 같은 도메인 매핑이 3곳에서 다르게 구현됨(REJECTED→EACCES vs EPERM,
    CONFLICT→ESTALE vs EBUSY, INVALID_STATE→EFSM vs ESHUTDOWN, channel_bridge는
    5-case 부분집합만 처리). 역방향 정본 `api/message/request_result_internal.hpp`
    `from_errno` 옆에 순방향 정본을 추가하고 3곳을 교체. **정본 값 확정이 선행
    과제** — 판정 기준 순서: (1) `from_errno` 역방향 표와의 왕복 일관성,
    (2) `core/doc/spec`의 errno 표, (3) 각 호출처/테스트의 기대값. 확정한 매핑
    표를 이 항목 아래에 기록한 뒤 구현하고, 기준끼리 판정이 갈리면 구현 전에
    사용자 확인을 받는다. (없음 / S)
  - 2026-07-07 확정한 순방향 정본: `OK→0`, `TIMED_OUT→ETIMEDOUT`,
    `NOT_FOUND→ENOENT`, `TERMINATED→ETERM`, `PROTOCOL_ERROR→EPROTO`,
    `INTERNAL_ERROR→EIO`, `REJECTED→EACCES`, `CONFLICT→ESTALE`,
    `BUSY→EBUSY`, `NOT_CONNECTED→ENOTCONN`, `INVALID_ARGUMENT→EINVAL`,
    `INVALID_STATE→EFSM`, `NOT_SUPPORTED→ENOTSUP`. 판정 근거는
    `request_result_internal.hpp`의 `from_errno`, `core/doc/spec/core/errno-map.md`,
    `core/doc/spec/core/errno-map.ko.md`가 같은 값을 가리키는 점이다. 기존
    `service_spot_route_bridge_channel_reply_internal.cpp`의 `EPERM`/`EBUSY`/
    `EAGAIN`/`ESHUTDOWN` 매핑은 정본과 달라 `to_errno` 호출로 교체했다.
- [x] **T1-02. `assign_routing_id_compact` 이중 정의 삭제**
  - `api/socket/socket_message_api.cpp:22-28` 익명 네임스페이스 사본이
    `socket_request_reply_runtime_io_helpers.hpp:19` inline 정본을 가림. 사본
    삭제, 헤더 include. (없음 / S)
- [x] **T1-03. reqrep `_impl` pass-through 병합**
  - `api/socket/socket_request_reply_api.cpp:12-129`,
    `socket_request_reply_pending_api.cpp:15-184`
  - `reqrep::foo(...) { return foo_impl(...); }` 형태 8쌍. 동일 시그니처의 얕은
    레이어이므로 `_impl` 본문을 공개 정의로 병합(호출이 하나 줄어드는 방향).
    공개 C API 진입점 주변이므로 단독 커밋 + 테스트 증거 분리. (없음 / S)
- [x] **T1-04. dealer reply-token 할당 중복 → 기존 헬퍼 호출**
  - `api/socket/socket_request_reply_router_api.cpp:440-457`이
    `socket_request_reply_runtime_io.cpp:82-96` `allocate_dealer_reply_token`
    + target 저장을 verbatim 재인라인. 같은 `state->mutex` 하이므로 헬퍼 호출로
    교체. (code-motion / S)
- [x] **T1-05. 도달 불가 recv fast-path 분기 정리**
  - `api/socket/socket_message_recv_api.cpp:36-39`
    `is_direct_public_routed_recv_fast_type`가 항상 false → `:164-186` 분기 사장.
    ROUTER-fast 후속 의도가 없음을 확인한 뒤 predicate+분기 삭제. (없음 / S)
- [x] **T1-06. `multipart_send_txn.cpp` dead retry scaffolding 삭제**
  - `runtime/core/multipart_send_txn.cpp:238-285`, `.hpp:69,76`
  - 미사용 `attempt_fn` typedef, 1회 호출 trampoline(`attempt_send_*` +
    `*_attempt_arg_t`), `(void)` 처리된 vestigial 파라미터 2개
    (`route_ready_retry_ms_`, `fallback_on_missing_sndtimeo_`) 제거. 제거된
    재시도 루프의 잔재로 순수 삭제. (code-motion / S)
- [x] **T1-07. actor `gateway_parts` framing 3중 복붙 추출**
  - `service_spot_actor_api.cpp:618-637`, `:1824-1842`, `:3485-3505`
  - control-prepend + `zlink_msg_move` 루프 + `close_built_parts` cleanup이 3중
    복제(꼬리가 미묘하게 달라 drift 위험). **header-inline** 헬퍼
    `build_gateway_parts(...)`로 추출 — A/C 블록이 send 경로이므로 out-of-line
    금지. 기존 `resize` 할당 외 추가 할당 없음을 유지. (code-motion / S)
  - 2026-07-07 완료: `service_spot_actor_gateway_parts_internal.hpp`의 inline
    helper로 control prepend, payload move, cleanup을 정본화했다. 호출처별 차이인
    send-frame consume 여부와 no-bind pending erase는 호출처에 남겼다. 사용자
    지시와 CPU 부하 조건에 따라 perf는 생략하고 빌드와 관련 actor/spot 테스트로
    검증했다.
- [x] **T1-08. `err.cpp` backtrace 서브시스템 분리**
  - `runtime/utils/err.cpp:371-424` `print_backtrace`(demangle/backtrace 의존)를
    별도 진단 TU로. errno 번역과 무관한 관심사. (없음 / S)
  - 2026-07-07 완료: `print_backtrace` 구현을 `runtime/utils/err_backtrace.cpp`로
    분리하고 `err.cpp`에는 errno 문자열, abort, Windows errno 변환만 남겼다.
- [x] **T1-09. TLS verify-mode 결정 시퀀스 `ssl_context_helper`로 중앙화**
  - `transports/tls/asio_tls_connecter.cpp:400-424` vs
    `transports/ws/asio_ws_connecter.cpp:60-77` + 양쪽 listener의
    `configure_server_verification`/`create_server_context` 시퀀스
  - primitive는 이미 중앙화됨. "options_에서 client/server 컨텍스트 구축" 시퀀스
    자체를 helper로 올려 4곳 복붙 해소. 단 verify-mode/hostname 검증/trust
    store 적용 **순서와 에러 의미는 사실상 계약** — 이동 전 현재 동작을 테스트로
    고정하고 순서를 바꾸지 않는다. (없음 / M)
  - 2026-07-07 완료: `ssl_context_helper_t`에 options 기반 client/server context
    builder를 추가하고 TLS connecter/listener 및 WSS connecter/listener가 같은
    helper를 사용하도록 교체했다. 기존 순서인 CA/trust-system 로딩, verify-mode
    설정, hostname verification 적용 순서를 유지했다. 현재 CTest에 등록된
    TLS/WSS 회귀 테스트는 `test_transport_matrix`이며 `tls`/`wss` 케이스를 포함한다.
- [x] **T1-10. `socket_base.hpp` 정보은닉 강화(축소판)**
  - `sockets/common/socket_base.hpp` — `socket_msg_dispatch_mutex()`의 raw
    `recursive_mutex&` 노출과 핸들러 액세서 9종 축소. protected 사이에 낀
    `public:` 블록 정리. **x-가상함수 표면은 불변**(virtual dispatch 변경 금지).
    액세서 축소는 stream/xsub/router 하위 타입 연쇄 수정을 동반하므로 실제
    규모는 S–M. (code-motion / S–M)

### 3.2 티어 2 — 중형·안전 (23건)

api/socket reqrep 클러스터:

- [x] **T2-01. pending-request + timeout 북키핑 통합**
  - `socket_request_reply_internal.cpp:163-283`,
    `socket_request_reply_pending_api.cpp:47-155`, `dispatch.cpp:87-93`
  - 3개 맵(`pending_sequences`/`pending_request_keys_by_seq`/`pending_requests`)
    동시 erase + `queue_reply_completion(ETIMEDOUT)` 수명주기가 4곳 복제. 동일
    구조 timeout-callback ctx 구조체 재선언(2곳)도 함께 해소. state 메서드로
    통합(인라인 유지 시 indirection 추가 없음). (code-motion / M)
- [x] **T2-02. reqrep envelope 인코딩 정본화**
  - `socket_request_reply_runtime_io.cpp:611-637`,
    `socket_request_reply_submit_api.cpp:56-71, 244-263, 382-398`
  - 4프레임 control prologue(protocol/version/type/seq via `encode_u64_be`) 조립이
    ≥4곳 수작업. decode(`parse_envelope`)는 이미
    `request_reply_protocol_internal.hpp`에 있으므로 encode가 비대칭 갭.
    스트리밍 send 사이트는 형태 유지, 인코딩/타입 선택만 통일. (code-motion / M)
- [x] **T2-03. `errno==EFAULT` 센티널 dispatch → 명시적 resolve**
  - `core/zlink_option.cpp`(5곳), `core/zlink_option_specialized_api.cpp:246-320`,
    `socket/socket_message_handler_api.cpp:97-103`, send/recv service_mode 폴백
  - "service 표면 먼저, EFAULT면 socket" 판정이 errno 값 제어 흐름으로 ~8곳
    반복. 내부 경로가 정당하게 EFAULT를 설정하면 라우팅이 깨지는 취약 구조.
    `resolve_option_target(handle)` enum 반환 하나로. (없음 / M)
- [ ] **T2-04. `part_helper_api.cpp` 저장소 정책 분리**
  - `socket/part_helper_api.cpp:22-106, 226-273, 660-685` vs `:295-658`
  - "어느 레지스트리가 state를 소유하나"(spot 레지스트리 4종 지식 하드코딩,
    cross-domain 누수)와 send/recv 스텝 엔진을 파일 분리. 호출 그래프 불변.
    (code-motion / M)
- [x] **T2-05. `zlink_option.cpp` 매핑 테이블/진입점 분리**
  - `core/zlink_option.cpp:17-227`(순수 데이터, 타 파일이 이미 의존) vs 5개
    진입점 로직. T2-03과 같은 파일이므로 연달아 진행. (없음 / M)

spot actor (api/actor):

- [x] **T2-06. `_locked` pass-through ~30개 인라인**
  - `service_spot_actor_api.cpp:331-360, 716-905` 등 78개 `_locked` 중
    `return actor_runtime().<state>.<method>(...)` 1–3줄 위임자
  - state 추출 후 콜사이트 미재지향의 잔재. 인라인하면 프레임이 하나 제거됨.
    실질 로직 있는 2–3개(`stream_owner_for_actor_ref_locked` 등)만 유지.
    `find_session_binding_locked` 등 send/recv 경유분 포함이므로 code-motion
    원칙 준수. (code-motion / M)
- [x] **T2-07. no_bind 플로우 모듈화**
  - state는 `service_spot_actor_no_bind_state.cpp`로 분리됐으나 연산이 god file
    5곳 산재(`:686, 734, 1804, 1856, 3439-3520, 3554`). T1-01/T1-07 완료 후
    `service_spot_actor_no_bind.cpp`로 응집. (code-motion / M)

runtime/services (spot):

- [x] **T2-08. spot 패킹 codec 단일 모듈화**
  - encoder: `runtime/services/spot/request_reply/spot_request_reply_local_dispatch.cpp:384-556`
    / decoder+header-init: `api/spot/request_reply/service_spot_routed_codec.cpp`
  - 같은 wire format(packed spot-routed envelope)의 encode가 runtime층, decode가
    api층에 분단 — 포맷 변경 시 두 층 동시 수정 강제. 소유 모듈 하나로 합치고,
    내부 build skeleton 복붙(`build_spot_request_reply_message_into` vs
    `build_spot_routed_message_into`: header init + part move + cleanup 동일)도
    control-part prefix 파라미터화로 dedup. (code-motion / M)
- [x] **T2-09. `spot_request_reply_local_dispatch.cpp` 분해**
  - 670줄 단일 TU에 local dispatch + pending 조회 + reply 전달 + 에러 합성 +
    codec + local-delivery 파이프라인(api쪽 24파일과 극단 비대칭). T2-08 codec
    분리 후 에러 합성/전달을 concern별 분리. (code-motion / M)
- [x] **T2-10. spot 에러-reply 합성/분류 dedup**
  - `spot_request_reply_local_dispatch.cpp:234-266`,
    `service_spot_route_bridge_channel_reply_internal.cpp:213, 234`,
    `service_spot_request_reply_channel_bridge.cpp:29-74`
  - error-reply 조립 + result→errno 분류가 api/runtime 3곳 구현. T1-01 정본을
    소비하는 형태로 통합. (없음 / S)
- [ ] **T2-11. `spot_data_plane_runtime.cpp` 튜닝/lifecycle 분리**
  - `:49-101`(HWM 해석·옵션 정책) + 168줄 `configure_runtime_sockets:231-399` vs
    초기화/해체(`:442-728`). 1회성 경로라 무위험. (없음 / M)
- [ ] **T2-12. `spot_node_handles.cpp` 튜닝/lifecycle 분리**
  - `:28-345`(HWM refresh, 튜닝 옵션 파싱/저장/검증) vs `:346-719`(핸들
    생성/파괴, 소켓 해체). `fast_*` 액세서는 제자리. (없음 / M)
- [ ] **T2-13. `spot_sub_subject_state.cpp` ready-ack 상태기계 분리**
  - `:52-390`(구독/필터 관리·subject 열거) vs `:397-684`(readiness + ready-ack
    handshake + liveness). 구독 control plane. (없음 / M)
- [ ] **T2-14. `spot_node_access.cpp` pass-through 축소 + spot_state 분리**
  - 표면 2/3가 `return node_ ? node_->x() : -1` 개명 레이어(~50메서드), 나머지가
    별개 역할인 논리 spot state 수명주기(`:349-413`). 위임자 축소 + state 관리
    별도 유닛. (없음 / M)
- [ ] **T2-15. data-plane 내부 state 헤더 노출 축소**
  - `spot_data_plane_runtime_state.hpp`(pending/target/backpressure 세부)가
    `runtime/spot_runtime_execution.hpp`로 누출. forward-decl/좁은 인터페이스로.
    (없음 / S–M)
- [ ] **T2-16. `spot_data_plane_forwarding.cpp` 저위험 3책임 추출**
  - 1,065줄 6책임 중 control plane 3개만: mesh sender 소켓 수명주기
    (`:65-144, 532-553`), poller-interest 관리(`:145-203, 574-593`),
    admission plan/backpressure 정책(`:47-63, 214-281, 555-573`).
    forward/stage/flush/drain hot 본체는 남김(6절). db5e30fb7 방향의 연장.
    (없음~code-motion / L)

runtime/core:

- [x] **T2-17. `ctx.cpp` 잔여 3책임 분리**
  - auto-HWM 재계산 엔진(`:128-276`, `ctx.hpp:187-196` — 디바운스/세대 카운터/
    two-mutex 프로토콜)을 기존 `ctx_*_registry` 동급 모듈 `ctx_auto_hwm_recalc`로
    추출 + 컨텍스트 옵션 스위치보드(`:332-550`) 분리 + 동거 중인
    `thread_ctx_t`(`:648-760`) 분리. (없음 / M)
- [x] **T2-18. `socket_poller.cpp` socket/fd 쌍 dedup**
  - `:61-203` add/modify/remove가 `is_socket` vs `is_fd`만 다른 쌍 4벌 +
    `check_socket_events:336-358`가 `check_events:371-388` 재구현. predicate
    파라미터화 헬퍼로. 관리 API라 hot 무관. (없음 / M)
- [x] **T2-19. `ctx_bootstrap`/`ctx_termination` friend 결합 정리**
  - `ctx.hpp:142-143` friend 2개가 private 멤버 ~10개를 직접 조작하는 전권 정적
    프로시저 — 줄만 옮기고 은닉 경계는 못 옮긴 추출. 좁은 인터페이스 경유로
    전환하거나 환원. (없음 / S–M)
- [x] **T2-20. `object.cpp` pipe retain/release 결합 응집**
  - `retain_command_ref()`가 pipe 대상 sender 6곳(`:260-319`)에 수동 살포, 짝
    release는 `command_targets_pipe` 집합(`:17-22, 144-145`)으로 발화 — 불변식이
    두 단절 지점에 분산. `send_to_pipe` 헬퍼로 co-locate.
    `send_activate_write:276-279` self-dispatch는 불변. (code-motion / S)
- [x] **T2-21. `session_base_pipe_io.cpp` trace 추출**
  - `:10-12, 26-50` env-gate된 router 커맨드 trace(stderr)가 `push_msg` 분기에
    인라인. no-op-when-disabled 헬퍼로. (code-motion / S)

sockets/engine/transports/utils:

- [ ] **T2-22. connecter/listener 공통 base 도입 (각 4중 복제)**
  - `transports/{tcp,ipc,tls,ws}/asio_*_connecter.cpp`: `get_new_reconnect_ivl`
    (verbatim 복사), reconnect/connect 타이머, plug/term, tune, create_engine →
    `asio_connecter_base_t`.
  - `transports/{tcp,ipc,tls,ws}/asio_*_listener.cpp`: accept 루프, 튜닝, 주소
    관리(tls/ws는 서버 인증서 로딩 추가 공유) → `asio_listener_base_t`.
  - 연결/수락은 control plane이라 무위험. (없음 / M+M)
- [ ] **T2-23. `socket_runtime.cpp` 클래스별 분리 + stream/xsub dispatch
  lifecycle 추출 + `ip.cpp` fdpair 분리**
  - `sockets/common/socket_runtime.cpp`(759줄): 독립 코디네이터 5클래스 + RAII
    scope 3종 → 클래스별 TU 분리(순수 파일 분리). (code-motion / M)
  - `sockets/stream/stream.cpp`/`pubsub/xsub.cpp`: 소켓 타입 알고리즘과 앱 콜백
    dispatch 런타임(start/stop/핸들러 등록)이 혼재. hot 함수(xsend/xrecv/
    dispatch_message)는 제자리, 수명주기만 기존 `*_dispatch_context.cpp`
    방향으로 이동. (code-motion / M)
  - `utils/ip.cpp:300-750`: self-pipe/socketpair 구축 엔진(~440줄)은 자립
    서브시스템 → 분리. `create_ipc_wildcard_address`는 IPC 도메인으로. (없음 / M)

### 3.3 티어 3 — 대형 구조 (6건)

- [ ] **T3-01. `service_spot_actor_api.cpp`(3,888줄) 분해 — join 오케스트레이션**
  - join lifecycle ~1,200줄(`:917-1583, 1962-2117, 2570-3170`)이 최대 덩어리.
    `actor_join_state_t`가 이미 데이터를 소유하므로
    `service_spot_actor_join.cpp`가 오케스트레이션을 소유하도록.
    join은 control plane이라 무위험. T2-06/T2-07 선행 시 수월. (없음 / L)
- [ ] **T3-02. `service_spot_actor_api.cpp` 분해 — gateway ingress/codec**
  - egress framing(`:489-686`)과 ingress parse+dispatch(`:1698-2235`)를
    `request_reply`의 codec/ingress 분해와 대칭으로 분리.
    **egress `send_actor_gateway_*`는 send 경로 — header-inline code-motion만
    허용**, out-of-line 이관은 인라이닝 훼손으로 간주. (code-motion~조건부 / L)
- [ ] **T3-03. `poller_api.cpp`(719줄, 5책임) 분해**
  - 등록 테이블 CRUD+인덱스(`:232-418`)와 native↔public 이벤트 변환(`:26-229`)은
    `service_poller_api.cpp`가 이미 내부를 직접 만지는 암묵적 공유 모듈 —
    추출이 기존 결합의 공식화. 일회성 `zlink_poll`(`:420-508`)도 별도 표면.
    `zlink_poller_wait` drain 루프(`:627-719`)는 불변. (code-motion / L)
- [ ] **T3-04. `socket_request_reply_router_api.cpp`(710줄) 분해**
  - 옵션 get/set(`:646-710`)·lifecycle(`:623-644`)은 무위험 추출.
    `recv_dealer_parts_once:375-495`의 envelope 파싱+토큰 할당은
    `runtime_io.cpp` 큐 경로와 로직 중복 — T1-04 선행 후, fast path 인라인 유지
    조건으로 정리. (부분 없음, recv부는 조건부 / L)
- [ ] **T3-05. ZMP handshake/control 중복 통합 (zmp ↔ ws 엔진)**
  - `engine/asio/asio_zmp_engine.cpp` vs `transports/ws/asio_ws_engine.cpp`:
    `receive_hello`/`parse_hello`/`process_handshake_input`/
    `process_ready|error|command_message`/heartbeat 등 ~10쌍이 사실상 동일.
    handshake는 연결당 1회라 안전 — 공유 handshake driver로.
    **`decode_and_push`/`push_one_then_decode`(hot recv)는 제외.** (code-motion / M–L)
- [x] **T3-06. runtime reqrep 층과 api reqrep 층의 분해 대칭화 마무리**
  - T2-08/T2-09 완료 후 남는 dispatch 잔여(`dispatch_spot_request_to_*`,
    local-delivery 파이프라인)를 api쪽 분해 어휘(submit/delivery/completion)와
    맞춰 정리. (code-motion / M)

### 3.4 티어 4 — 조건부 (벤치 게이트 없이는 착수 금지, 3건)

- [ ] **T4-01. recv-part 시퀀싱 4중 중복 통합**
  - `socket_message_api.cpp:39-441`(recv/subscribe) +
    `socket_request_reply_router_api.cpp:98-621`(router/dealer)
  - 동일 상태기계(sequence 활성 판정 → 단일부 fast move → stage/take →
    abort/close 언와인딩) 4벌. 범위 내 최대 API-층 중복이지만 router/dealer recv가
    명시된 hot path. **`part_count==1` fast move는 제자리**, post-recv 꼬리만
    공유 가능한지 설계 검토 후 벤치로 결판. (조건부 / L)
- [ ] **T4-02. transport wrapper 쌍 통합**
  - `tcp_transport.cpp`≈`ipc_transport.cpp`, `ws_transport.cpp`≈`wss_transport.cpp`
  - 주소/소켓 타입만 다른 어댑터 2벌씩. `async_read_some`/`async_write_some`/
    `async_writev`가 per-message I/O primitive — 템플릿화가 non-virtual/인라인
    dispatch를 보존함을 증명해야 진행. (조건부 / M)
- [ ] **T4-03. WS 엔진 base 통합**
  - `asio_ws_engine_t`가 `i_engine` 직접 상속으로 stream fastpath 전층 + ZMP층을
    재구현(3중 ~1,700줄, 범위 내 최대 중복원). 상속 변경은 vtable에 영향.
    **T3-05(handshake 통합) 선행 후**, base 추출은 벤치 무회귀 증명 시에만.
    (조건부 / L)

## 4. 권장 실행 순서

1. **티어 1**: T1-01 → T1-02 → T1-04 → T1-05 → T1-06 → T1-07 → T1-03 →
   T1-08/09/10. T1-01은 정본 값 확정이 선행하며 단독 커밋.
   T1-02/04/05/06은 삭제형이라 하나의 정리 커밋으로 묶어도 된다.
   T1-07(actor framing)과 T1-03(공개 C API 진입점)은 각각 별도 커밋.
2. **T2-03 (EFAULT 센티널 제거)**: 잠재 correctness 함정이므로 티어 2 중
   최우선으로 단독 진행 (T2-05를 같은 파일이라 연달아).
3. **reqrep 클러스터**: T2-01 → T2-02 → T2-10 → T2-08 → T2-09 → T3-06.
   같은 도메인이므로 연속 진행이 컨텍스트 효율적.
4. **spot actor 클러스터**: T2-06 → T2-07 → T3-01 → T3-02.
   각 단계 후 core actor/spot 관련 테스트와 full core CTest를 확인한다. C++
   framework/bindings E2E는 별도 확인 단계에서 수행한다.
5. **services/spot 구조**: T2-11 → T2-12 → T2-13 → T2-14 → T2-15 → T2-16.
6. **runtime/core**: T2-17 → T2-18 → T2-19 → T2-20 → T2-21, T1-06과 독립.
7. **transports/sockets**: T2-22 → T2-23 → T3-05, 이후 T3-03/T3-04.
8. **티어 4**는 위 전부 완료 후 개별 설계 검토 + 벤치 게이트로 별도 판단.

## 5. 검증 게이트

- 매 항목: core 빌드 + 해당 영역 단위/통합 테스트 그린.
- code-motion 항목 중 send/recv 경로 인접(T1-04, T1-07, T2-01, T2-02, T2-06,
  T2-08, T3-02, T3-05): routed/reqrep 벤치 baseline vs patched 비교, 회귀 시
  커밋하지 않고 원인 규명 또는 철회.
- 벤치 실행 방법(정본 러너 = `bindings/c/perf`, 런타임은 `core/build`에서
  해석되므로 반드시 `cmake --build core/build` 후 실행):
  - socket reqrep 인접(T1-04, T2-01, T2-02):
    `./bindings/c/perf/run_benchmarks_multi.sh --pattern DEALER_ROUTER_REQREP,ROUTER_ROUTER_REQREP`
  - spot/actor 인접(T1-07, T2-06, T2-08, T3-02):
    `--pattern SPOT,SPOT_REQREP,SPOT_SENDSEND`
  - 엔진/transport 인접(T3-05, 티어 4 전체): 위 두 묶음 + `STREAM`.
  - 비교 절차: 변경 전 HEAD에서 1회(baseline) → 패치 적용 후 1회(patched) →
    동일 머신·동일 조건에서 throughput/latency 비교, 결과 요지를 커밋 메시지에
    남긴다.
- 티어 4: 착수 전 설계 검토 기록 + 착수 후 벤치 무회귀 증명이 머지 조건.
- 클러스터 완료 시점: full test gate(fail 0) 후 다음 클러스터로.
- 각 항목 완료 시 이 문서 체크박스 갱신. 계획과 실제가 어긋나면 코드가 아니라
  이 문서를 먼저 갱신한다.

## 6. 제외 판정 — 구조 변경 금지, 문서화로만 대응

아래는 감사에서 중복/산재로 식별됐으나 hot path 본체이므로 리팩토링하지
않는다. 필요 시 불변식 주석만 보강한다.

- `pipe.cpp:436-543` write 6변형: {HWM check, recursive, flush} 2×2×2 정책
  매트릭스의 의도적 수전개. `pipe.hpp:200-206` 불변식 주석이 이미 존재.
- `msg.cpp` refcount teardown 5중 인라인(close/rm_refs/slice/init_view 언와인드):
  refcount hot path. TL pool 회귀 전례가 있는 영역.
- `recv_internal.cpp` 가드/INT_MAX 클램프 반복: 명시된 hot path, 저가치.
- `request_timeout_scheduler_internal.cpp` ↔ `timer_scheduler_backend.cpp`
  deadline-큐 엔진 통합: 전자가 hot path 파일. 후순위로도 두지 않고 제외.
- WS/ZMP/stream 엔진의 send/recv/gather 본체(T4-03의 hot 부분),
  `object.cpp`의 send_*/process_* 순수 보일러플레이트.

## 7. 진행 상태

- 2026-07-07: 전수 감사 완료(5영역 병렬), 본 계획 작성. 구현 미착수.
- 2026-07-07: 외부 리뷰 반영 — T1 실행 순서 조정(1→2→4→5→6→7→3), T1-09
  규모 S→M(TLS 계약 고정 테스트 선행), T1-10 규모 S→S–M(하위 타입 연쇄),
  T2-03을 티어 2 최우선 단독 작업으로 상향, 커밋 묶음 규칙 완화(삭제형 한정).
  Discovery/Registry 제거(3cd044e22) 이후 stale 여부 재검증: `core/src`에
  discovery/registry 참조 0건 확인 — 본 계획 42건 중 무효화된 항목 없음
  (감사 자체가 제거 이후 트리 기준이었음).
- 2026-07-07: 외부 에이전트 단독 수행 대비 보강 — AGENTS.md 우선 규칙·라인
  범위 시점 주의(0절), T1-01 정본 판정 기준 3단계 명시(3.1절), 벤치 러너
  실체(`bindings/c/perf/run_benchmarks_multi.sh`)와 패턴 매핑·비교 절차(5절).
- 2026-07-07: T1-01 완료 — `request_result_internal.hpp`에 순방향 정본
  `to_errno`를 추가하고 actor no-bind callback, spot request-reply channel
  bridge, spot route-bridge channel reply의 로컬 매핑 3개를 제거했다. 정본 값은
  `from_errno` 왕복 일관성과 `core/doc/spec/core/errno-map(.ko).md`의 callback
  errno 표를 기준으로 확정했다.
- 2026-07-07: T1-02/T1-04/T1-05/T1-06 구현 — `assign_routing_id_compact`
  사본 제거, dealer reply-token 할당의 기존 helper 재사용, 항상 false인 routed
  recv fast-path 분기 삭제, multipart send retry 잔재 삭제를 묶어 적용했다.
  T1-04는 원래 reqrep 벤치 게이트 대상이지만, 사용자 지시로 perf 실행은
  생략하고 빌드와 테스트로 검증한다.
- 2026-07-07: T1-07 구현 — actor gateway multipart 조립의 3중 복제를 header
  inline helper로 통일했다. send 경로의 기존 frame consume 정책과 no-bind pending
  cleanup 정책은 호출처별로 유지했다.
- 2026-07-07: T1-08 구현 — `err.cpp`에서 libunwind/demangle 기반 backtrace 출력을
  `err_backtrace.cpp`로 분리해 errno 변환과 진단 출력 책임을 나눴다.
- 2026-07-07: T1-09 구현 — TLS/WSS client/server SSL context 생성 정책을
  `ssl_context_helper_t`의 options 기반 builder 두 개로 중앙화했다. `test_asio_ssl`,
  `test_asio_ws`, `test_zmp_ws_wss`는 현재 CTest 미등록 allowlist 항목이고, 등록된
  TLS/WSS 검증은 `test_transport_matrix`로 수행했다.
- 2026-07-07: T1-10 구현 — socket message dispatch mutex의 raw reference 노출을
  `lock_socket_msg_dispatch()`로 좁히고, 하위 타입이 직접 쓰지 않는 handler
  subject/userdata accessor를 private 영역으로 내렸다. x-가상함수 표면은 변경하지
  않았다.
- 2026-07-07: T1-03 구현 — socket reqrep api/pending api의 파일-local `_impl`
  함수 본문을 `reqrep::...` 정의로 직접 병합하고 1줄 pass-through wrapper를
  삭제했다. 공개 C API 및 내부 헤더 시그니처는 유지했다.
- 2026-07-07: T2-03 구현 — `resolve_option_target()`을 추가해 core option
  진입점이 서비스/소켓/invalid handle을 먼저 판정하도록 바꿨다. common option,
  routing-id, TLS helper, pub/sub specialized option, send-ready handler, poller
  add/modify/remove에서 service 실패의 `errno == EFAULT`를 socket fallback 신호로
  쓰던 분기를 제거했다.
- 2026-07-07: T2-05 구현 — public option 매핑 테이블과 `map_*`/`lookup_common_option`
  함수를 `zlink_option_mapping.cpp`로 분리했다. `zlink_option.cpp`는 handle resolve,
  socket option checked access, 공개 option 진입점 로직만 남겼다.
- 2026-07-07: T2-01 구현 — socket reqrep pending request의 3개 map 삽입/삭제와
  timeout task 생성/timeout 완료 큐잉을 공통 helper로 모았다. dispatch hot path의
  잠금 범위와 key→sequence fallback 조회 순서는 유지했다.
- 2026-07-07: T2-02 구현 — reqrep envelope control frame의 protocol/version/type/seq
  인코딩을 `request_reply_protocol_internal.hpp`의 공통 helper로 모았다. streaming
  send 경로는 기존 4프레임 전송 형태를 유지하고, control bytes 생성만 정본화했다.
- 2026-07-07: T2-10 구현 — spot request/reply error-reply의 errno payload
  생성을 `request_reply_protocol_internal.hpp`의 공통 helper로 모았다. result→errno
  분류는 T1-01의 `request_result_internal::to_errno` 정본을 계속 사용한다.
- 2026-07-07: T2-08 구현 — spot packed routed envelope의 build/part-count
  함수를 `service_spot_routed_codec.cpp`로 옮겨 decode/header-init과 같은 모듈에
  모았다. request/reply와 routed build의 header-prefix + payload move skeleton도
  공통 helper로 묶었다.
- 2026-07-07: T2-09 구현 — `spot_request_reply_local_dispatch.cpp`에서 reply
  completion/pending lookup과 request/direct local delivery를 각각
  `spot_request_reply_local_reply.cpp`, `spot_request_reply_local_request.cpp`로
  분리했다. 원래 파일은 combined message parsing과 local-delivery orchestration만
  남긴다.
- 2026-07-07: T3-06 구현 — runtime spot reqrep local delivery를 api쪽
  `local_reply`/`local_request`/`local_direct` 분해 어휘와 맞췄다.
  `dispatch_spot_request_to_*` 잔여 이름은 `deliver_request_to_*`로 바꾸고,
  direct payload delivery와 공통 local queue 진입점을 별도 TU로 분리했다.
- 2026-07-07: T2-06 구현 — actor runtime state 추출 뒤 남아 있던 `_locked`
  단순 위임자 중 registry/session/route 접근만 제거하고 callsite를 state
  메서드로 직접 연결했다. 조건 분기나 상태 갱신 의미가 있는 helper
  (`stream_owner_for_actor_ref_locked`, `clear_actor_bound_session_locked`,
  `next_*_locked` 등)는 유지했다.
- 2026-07-07: T2-07 구현 — no-bind pending key/callback, submit, reply-frame
  처리, 실패 응답 준비를 `service_spot_actor_no_bind.cpp`로 모았다. actor queue
  삽입은 `actor_handle_t` 내부 구조와 강하게 결합된 locked 구간이라
  `service_spot_actor_api.cpp`에 남기고, dispatcher는 `actor_no_bind_reply_t`를
  통해 no-bind 응답 필드 지식을 직접 들고 있지 않게 줄였다. Core no-bind/spot
  관련 테스트와 C++ ToActorMessaging E2E로 확인했다.
- 2026-07-07: T3-01 진행 중 — join 완료 callback과 즉시 완료/idempotent 완료
  예약 로직, join live/index/retire/release/timeout bookkeeping을
  `service_spot_actor_join.cpp`로 먼저 분리했다. gateway join request/reply
  helper도 같은 파일로 이동해 gateway ingress dispatcher가 join state 조작을
  직접 소유하지 않게 줄였다. 이어서 승인된 join을 actor 상태와 lifecycle event에
  반영하는 commit helper도 같은 join 모듈로 옮겼다. join recv/reply public
  surface와 `zlink_spot_node_actor_join_spot` submit 진입점까지
  `service_spot_actor_join.cpp`로 이동했다. 이어서
  `zlink_spot_node_actor_join_entry_spot` submit 진입점도 같은 join 모듈로
  이동했다. `service_spot_actor_api.cpp`에는 join queue/spot replacement/stream
  teardown과 gateway ingress dispatch에 필요한 결합부가 아직 남아 있으므로 T3-01
  체크박스는 유지한다. submit 이동 뒤 미사용이 된 join pending actor/queue helper와
  한 줄 pass-through helper는 제거했다. 이어서 spot facade 제거 시 join queue/live
  request를 종료 대상으로 수집하는 로직과 joined-or-pending 조회 로직을 join
  모듈로 옮겨 API 파일의 join state 직접 접근을 더 줄였다. stream binding 해제 시
  queued/live join request를 찾는 로직도 join 모듈 API로 감춰, API 파일은 session
  binding 정리와 완료 콜백 방출만 맡도록 좁혔다. gateway ingress dispatcher의
  entry/spot join request/reply packet kind 분기도 join 모듈 helper로 이동해,
  API 파일은 join packet 세부 종류를 직접 알지 않도록 줄였다. join epoch 증가
  규칙도 join 모듈의 locked helper로 모아 API 파일에 남아 있던 동일 구현을
  제거했다. actor의 현재 spot routing id 조회도 join 모듈 helper 하나로 합쳐
  lifecycle/join 경로가 같은 정의를 쓰게 했다. destroy/leave guard가 보던
  pending join 조회도 join 모듈 helper 뒤로 숨겼다. join 완료 callback과 request
  release의 paired 수명주기도 join 모듈 helper로 모아 API 파일이 두 단계 완료
  순서를 직접 알지 않게 했다. spot facade 제거와 joined-or-pending 조회 진입점도
  join 모듈로 옮겨 API 파일의 join teardown 책임을 더 줄였다. lifecycle info 생성
  규칙도 join 모듈 helper 하나로 합쳐 API 파일의 중복 구현을 제거했다. stream
  binding 해제 시 queued/live join abort 선택, retire, 완료 callback 방출도 join
  모듈 batch helper로 모아 API 파일은 session binding 전체 해제 순서만 맡게 했다.
  gateway join reply 후 lock 밖에서 완료해야 하는 request도 같은 completion batch로
  넘겨 API 파일이 `queued_join_request_t *`를 직접 들고 있지 않게 했다. spot
  introspection의 pending join count 조회도 join 모듈 helper 뒤로 숨겼다. entry/user
  spot membership predicate도 join 모듈 정의 하나로 합쳤다. destroy/leave
  operation도 actor resolution 이후의 join lifecycle 본문을 join 모듈 helper로
  옮겨 API 파일은 비동기 reply operation wiring만 맡도록 줄였다.
- 2026-07-07: 검증 범위 조정 — C++ framework 작업이 별도로 진행 중이므로, 이
  core 리팩토링 루프에서는 framework/bindings E2E를 실행하지 않는다. spot actor
  클러스터도 core build, 관련 core 테스트, full core CTest로만 검증하고,
  bindings/framework 확인은 별도 단계로 넘긴다.
- 2026-07-07: T2-21 완료 — `session_base_pipe_io.cpp`의 env-gate router trace
  `fprintf`와 로그 카운터를 같은 TU의 inline helper로 옮겨 `push_msg` 분기에서
  trace 세부 조립을 제거했다.
- 2026-07-07: T2-20 완료 — pipe 대상 command 판정을 `pipe_command_destination`
  helper 하나로 모으고, retain + send/self-dispatch는 `send_pipe_command`로
  응집했다. `activate_write`의 같은-thread 직접 처리 조건은 유지했다.
- 2026-07-07: T2-17 진행 — `thread_ctx_t` 구현을 `ctx.cpp`에서
  `ctx_thread.cpp`로 분리해 ctx 본문에서 thread option/scheduling switchboard를
  제거했다. 이어서 `ctx_t::set/get` option switchboard를 `ctx_options.cpp`로
  옮겼다. auto-HWM 재계산 엔진도 `ctx_auto_hwm_recalc.cpp`로 분리해 `ctx.cpp`는
  context lifecycle, socket registry, command routing 중심으로 줄였다.
- 2026-07-07: T2-18 완료 — `socket_poller_t`의 socket/fd add/modify/remove
  중복을 private item helper로 합치고, socket event 채우기 로직도
  `collect_socket_event`로 공유했다. poll/select wait 루프와 drain 흐름은
  그대로 유지했다.
- 2026-07-07: T2-19 완료 — `ctx_bootstrap_t`/`ctx_termination_t` friend
  helper를 제거하고 같은 lifecycle 구현을 `ctx_t` private 메서드로 환원했다.
  외부 helper가 ctx 내부 멤버를 직접 조작하던 결합을 없애기 위해 잠금 순서와
  shutdown/reaper wait 동작은 그대로 유지했다.
- 2026-07-07: 검증 배치 조정 — 항목마다 full CTest를 반복하지 않고 티어/클러스터
  단위로 변경을 묶은 뒤 full core CTest를 실행한다. 개별 조각은 core build와
  좁은 관련 테스트(smoke)로 확인하고, framework/bindings E2E는 계속 별도 단계로
  둔다.
