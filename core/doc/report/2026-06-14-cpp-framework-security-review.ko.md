# C++ Framework 계층 보안·버그 검토 리포트

- **작성일**: 2026-06-14
- **대상 범위**: `framework/languages/cpp/{framework,connector,extensions,http-client}` (zlink core 위의 상위 래퍼 계층)
- **상태**: CR1, CR2, H1 종결. H2/H3/M1 이후 항목은 실행 순서에 따라 별도 처리 필요.
- **참고**: 교차언어 공통 결함은 [README.ko.md](README.ko.md) 참조.

> **신뢰 모델**: stream-connector는 원격 서버에 대한 **클라이언트**이며, 인바운드 프레임의 신뢰 불가 주체는 서버다.
> http-client는 응답 서버 **및 리다이렉트 대상**을 신뢰 불가로 취급해야 한다. 프레임워크 런타임은 주로 in-process 신뢰 호출자가 구동한다.

## 요약

| # | 심각도 | 분류 | 위치 | 상태 |
|---|--------|------|------|------|
| CR1 | **Critical** | DoS / 무제한 할당 | `connector/core/.../zlink_stream_calls.cpp:235,307`, `stream_connection.cpp:351` | 수정 완료(2026-06-14) |
| CR2 | **Critical** | 자격증명 유출(CWE-200) | `http-client/src/client.cpp:186,193`, `request_performer.cpp:149`, `url.cpp:110` | 수정 완료(2026-06-14) |
| H1 | High | DoS / 무제한 응답 본문 | `http-client/.../request_performer.cpp:294,302` | 수정 완료(2026-06-14) |
| H2 | High | 동시성 / data race·UAF | `connector/engines/unreal/.../ZLinkStreamConnector.cpp:108,191,268` | 확인 |
| H3 | High | 리소스 누수 / teardown | `connector/engines/unreal/.../ZLinkStreamConnector.cpp:88` | 확인 |
| H4 | ~~High~~ → **Low** | 동시성 / drain-semantics | `framework/src/runtime/dispatch/offload_executor.cpp:65-78`, `runtime/execution/serial_execution_queue.cpp` | ❌ **반박(하향)** |
| M1 | Medium | 쿠키 경로 스코프 | `http-client/.../cookie_jar.cpp:84` | 확인 |
| M2 | Medium | 동시성 / 수명(잠복) | `framework/.../channels/route_handler_invoker.cpp:26` | 확인(잠복) |
| M3 | Medium | data race | `framework/.../spots/spot_runtime.cpp`, `actors/actor_gateway_runtime.cpp` | 확인 |
| M4 | Medium | 수명 / UAF | `connector/engines/axmol/...`, godot 등가 | 확인(좁은 창) |
| M5 | Medium | 동시성 / 코루틴 정지 | `http-client/.../coroutine_scheduler.cpp:28` | 의심 |
| L1~L6 | Low | (부록) | — | — |

> ⚠️ **아래 §Codex 교차검증이 최종 판정이다.** H4는 반박되어 하향됨.

---

## Codex 교차검증 결과 (2026-06-14)

작성 후 Codex에 문서 + 코드 위치를 주고 적대적 대조 리뷰를 요청한 결과:

| # | 초판 | Codex 판정 | 정정 요지 |
|---|------|-----------|-----------|
| CR1 | Critical | **CONFIRMED** | 송신은 `zlink_stream_calls.cpp:26` `max_send_payload_size` 강제, 수신은 `:120/:235/:307` prefix 크기로 직접 할당. 일치 |
| CR2 | Critical | **CONFIRMED** | 최고 심각도 항목 유지. `:149-150`이 교차호스트 홉에 모든 헤더 재적용, `url.cpp:112`가 scheme/host/port 비교·`Authorization` 제거 없이 절대 리다이렉트 추종 |
| H1 | High | **CONFIRMED** | `request_performer.cpp:294,302` `body_limit(uint64 max)` 양쪽 비활성 |
| H2/H3 | High | **CONFIRMED** | IO 스레드 콜백이 비동기화 벡터에 push(`:108→:270`), 게임 스레드 drain(`:191`). 소멸자는 `DetachOwner()`만, `Connector.close()`는 `Close()`(`:130`)에서만 → 명시적 Close 없이 소멸 시 close 누락(`~connector_t()=default`, `connector_runtime.cpp:363`) |
| H4 | ~~High~~ | **❌ REFUTED → 하향** | `offload_executor.cpp:68-70`이 `try_submit`(`:52-53`)과 **동일 `_mutex`** 하에 `_stopping=true` 설정 → "drain 후 enqueue UAF" 추론 성립 안 함. 좁은 nuance(drain 대기 중 `_stopping` 플립 전 submit 수용)만 남음 → **High 아님, Low drain-semantics 노트로 격하**. (경로 주의: `offload_executor.cpp`는 `runtime/dispatch/`가 맞고, `serial_execution_queue.cpp`만 `runtime/execution/`. Codex가 전자까지 `execution/`로 잘못 정정했던 것을 재확인·복구함) |
| M1 | Medium | **CONFIRMED** | `cookie_jar.cpp:84` 순수 prefix 매칭, 경계 없음 |

**Codex ADDITIONAL (초판 누락)**:
- `cookie_jar.cpp:69-70`이 쿠키를 host+name으로만 dedupe(경로 무시) → 다른 경로의 동명 쿠키를 잘못 삭제 가능(M1 연관).
- Unreal **request 콜백**도 state 콜백과 동일한 비동기화 pending-벡터 패턴: `ZLinkStreamConnector.cpp:171` `request.submit([this]…)` → `:176` `PendingRequests.push_back` → `:208` drain. H2 범위 확대.

**Codex 총평**: CR1/CR2/H1/H2/H3/M1은 실제 코드와 일치(반박 불가). 유일한 실질 오류는 **H4** — 실제 락/executor 배선과 모순되고 인용 경로도 틀림 → 재작성/하향 필요.

---

## CRITICAL

### CR1 — 인바운드 스트림 프레임 무제한 할당 → 메모리 고갈 DoS
- **분류**: 신뢰 불가 입력 파싱 / 리소스 고갈 · **확인 · repo 고유**
- **위치**:
  - `connector/core/src/runtime/calls/zlink_stream_calls.cpp:235` — `read_exact_until(state, payload_size, …)` → `std::vector<uint8_t> bytes(size)` (`:120`)
  - `connector/core/src/runtime/transport/stream_connection.cpp:351` — `read_exact` 내 `std::vector<uint8_t> bytes(size)`
  - `connector/core/src/runtime/calls/zlink_stream_calls.cpp:307` — async 경로 `frame_size = 6 + header_size + payload_size`, `state.inbound_buffer`(`connector_runtime.hpp:72`)에 cap 필드 없음

6바이트 prefix가 `header_size`(u16, ≤64KiB)와 `payload_size`(**u32, 최대 4GiB**)를 주는데, 이를 `max_send_payload_size`/`max_metadata_size`와 **대조하지 않는다**(이 상한은 send 경로 `zlink_stream_calls.cpp:24-37`에서만 강제). `frame_codec_t::validate_frame_size`는 존재하나 **encode에서만 호출**된다.

**트리거**: 악의적/탈취 서버(또는 평문 endpoint MITM)가 `payload_size = 0xFFFFFFFF` 전송 → 최대 4GiB 할당/누적 → OOM.
**수정**: 할당 전 `header_size > max_metadata_size`·`payload_size > max_send_payload_size`(또는 별도 `max_receive_*`) 거부, `inbound_buffer` 크기 cap 후 연결 중단.

**처리 기록(2026-06-14)**:
- `connector_options_t`에 `max_receive_payload_size`를 추가했다. 기본값은 64 KiB이며, 큰 서버 push 또는 reply가 필요한 connector는 옵션에서 명시적으로 올린다.
- `frame_codec_t::validate_receive_frame_size`를 추가하고, 동기 request 경로(`read_packet_frame`), async read loop(`try_take_inbound_frame`), 동기 drain 경로(`read_stream_packet`)에서 prefix 파싱 직후 호출하도록 바꿨다.
- `payload_size`가 상한을 넘으면 payload buffer를 만들기 전에 `frame_too_large`로 실패한다. async read loop는 연결을 닫고 pending request를 같은 오류로 완료한다.
- `wait_for`/`receive`가 쓰는 drain 경로도 같은 오류를 `inbound_error`로 보관한 뒤 public 호출자에게 `frame_too_large`를 반환한다.
- 회귀 테스트는 `test_cpp_stream_connector`에 추가했다. 작은 수신 상한으로 동기 request, async request, `wait_for` 경로의 oversize prefix 거부를 검증한다.
- 실행 검증:
  - `cmake --build framework/languages/cpp/build --target test_cpp_stream_connector -j2` 성공.
  - `ctest --test-dir framework/languages/cpp/build -R '^test_cpp_stream_connector$' --output-on-failure` 성공.
  - `cmake --build framework/languages/cpp/build --target test_cpp_framework_contract_headers -j2` 성공.
  - `ctest --test-dir framework/languages/cpp/build -R '^test_cpp_framework_contract_headers$' --output-on-failure` 성공.
- core runtime 또는 `core/include` public header 수정은 없으므로 `bindings/dev_sync_local_core_libs.sh`는 실행 대상이 아니다.
- Claude 리뷰에서 CR1에 대해 "추가 이슈 없음" 판정을 받았다.

### CR2 — 교차 호스트 HTTP 리다이렉트 시 자격증명을 공격자 호스트로 유출
- **분류**: HTTP 리다이렉트 / 자격증명 노출(CWE-200) · **확인 · repo 고유**
- **위치**: `http-client/src/client.cpp:186,193`(auth 헤더 저장), `http-client/src/runtime/request_performer.cpp:149-151`(매 홉 헤더 재적용), `http-client/src/runtime/url.cpp:110-118`(절대 교차호스트 리다이렉트 추종)

`basic_auth`/`bearer_token`이 비밀을 `_headers["authorization"]`에 저장하고, `build_wire_request`가 매 홉마다 **대상 호스트와 무관하게** 모든 `_options.headers`를 재적용한다. `resolve_location`은 절대 교차호스트 리다이렉트를 호스트 비교·`Authorization` 제거 없이 추종한다. 리다이렉트 루프(`request_performer.cpp:92-105`)는 303/POST 메서드 다운그레이드만 하고 자격증명을 제거하지 않는다.

**트리거**: bearer/basic auth로 `https://api.legit/...` 요청 → 서버(또는 평문 홉의 네트워크 공격자)가 `302 Location: https://attacker/` 반환 → `Authorization`이 attacker로 재전송. (쿠키는 호스트 스코프 jar라 영향 없음.)
**수정**: (scheme,host,port)가 원본과 다른 홉에서는 `Authorization`(및 사용자 지정 auth 헤더)을 제거, 명시적 opt-in 시에만 전달.

**처리 기록(2026-06-14)**:
- redirect hop의 scheme, host, port가 최초 요청 origin과 다르면 default header와 request header
  양쪽의 `Authorization`을 재적용하지 않도록 `request_performer`를 수정했다.
- 같은 origin redirect는 기존처럼 인증 헤더를 유지한다. `Proxy-Authorization`은 proxy 연결
  인증이므로 기존 proxy 경로에만 남긴다.
- 이름이 다른 custom 비밀 헤더는 일반 header와 구분할 수 없으므로 자동 제거 범위에 넣지
  않는다. 문서에는 `default_header`나 요청 단위 `header`에 비밀 값을 직접 넣지 말고
  `basic_auth`/`bearer_token`을 쓰라고 명시했다.
- 회귀 테스트는 `test_cpp_http_client`에 추가했다. default `bearer_token`과 request 단위
  `authorization` header가 교차 host redirect 후 `/echo-auth`에 전달되지 않는지, 같은 origin
  redirect에서는 `Authorization`이 유지되는지 검증한다.
- 실행 검증:
  - `cmake --build framework/languages/cpp/build --target test_cpp_http_client test_cpp_framework_contract_headers -j2` 성공.
  - `ctest --test-dir framework/languages/cpp/build -R '^test_cpp_http_client$' --output-on-failure` 성공.
  - `ctest --test-dir framework/languages/cpp/build -R '^test_cpp_framework_contract_headers$' --output-on-failure` 성공.
- Claude 재리뷰에서 CR2/H1 모두 코드·테스트·문서가 일치하며, "추가 이슈 없음" 판정을
  받았다.

---

## HIGH

### H1 — HTTP 응답 본문 무제한 (body_limit 비활성)
- `http-client/src/runtime/request_performer.cpp:294,302` · **확인 · repo 고유**

`parser.body_limit(std::numeric_limits<uint64_t>::max())` — Beast 내장 응답 크기 가드를 버퍼/스트림 파서 양쪽에서 비활성. `max_response_size` 옵션도 없음. 악의 서버가 무제한 바이트 스트림 → 메모리 고갈.
**수정**: 설정 가능한 상한으로 `body_limit` 지정, 합리적 기본값.

**처리 기록(2026-06-14)**:
- `client_builder_t::max_response_body_size(bytes)`를 추가했다. 기본값은 16 MiB이며, 0 bytes는
  `request_protocol_error`로 거부한다.
- buffered 응답과 `download(sink)` streaming 응답 모두 Beast parser의 `body_limit`에 같은
  상한을 적용한다.
- 회귀 테스트는 작은 body 상한으로 `/big` buffered 응답과 streaming download가
  `request_failed`로 실패하는지 검증한다.
- HTTP 압축 해제 결과의 출력 상한은 부록 L1 범위이므로 이번 H1 처리에서 닫지 않았다.
- Claude 재리뷰에서 H1이 실제 코드에서 닫혔고 L1을 잘못 닫지 않았음을 확인받았다.

### H2 — Unreal 어댑터: IO 스레드에서 쓰는 pending 큐의 data race + UAF
- `connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp:108-111,191-196,268-271` · **확인 · repo 고유**

`on_connection_state_changed([this]{ EnqueueState(...) })`가 Boost.Asio IO 워커 스레드에서 인라인 실행(`connector_runtime.cpp:217-233`)되어 `PendingStates.push_back`이 게임 스레드 `Dispatch()`의 `std::vector` read+erase와 경쟁. `std::vector`는 thread-safe 아님 → torn read / realloc 중 iterator / UAF. bare `[this]` 캡처가 객체 소멸 후에도 잔존.
**수정**: pending 큐를 뮤텍스 보호(또는 state-changed 핸들러를 `Dispatch()` 큐로 마샬링).

### H3 — Unreal 어댑터: 소멸 시 connector를 `close()`하지 않음
- `connector/engines/unreal/.../ZLinkStreamConnector.cpp:88-99,302-307` · **확인 · repo 고유**

`~UZLinkStreamConnector` → `DetachOwner()`가 큐만 비우고 `Connector.close()`를 호출하지 않음 → 살아있는 소켓·pending async read·`this` 캡처 state_handler가 암묵 소멸, 큐된 IO-스레드 콜백이 소멸 중 발화 가능(H2 유발).
**수정**: `DetachOwner()`에서 큐 정리 전 `Connector.close()`.

### H4 — ~~프레임워크 executor drain 순서 race~~ → **Low (Codex 반박, 하향)**
- `framework/src/runtime/dispatch/offload_executor.cpp:65-78`, `framework/src/runtime/execution/serial_execution_queue.cpp` · **하향 · repo 고유**

> 🔴 **§Codex 교차검증에서 반박됨.** `offload_executor.cpp:68-70`이 `try_submit`(`:52-53`)과 **동일 `_mutex`** 하에서 `_stopping=true`를 설정하므로 초판이 가정한 "drain 완료 후 작업 끼워넣기 → 해제된 큐 UAF"는 성립하지 않는다.
>
> ⚠️ **경로 주의**: `offload_executor.cpp`는 `runtime/dispatch/`에 있고(초판 경로가 정확), `serial_execution_queue.cpp`만 `runtime/execution/`에 있다. Codex가 전자까지 `execution/`로 잘못 정정한 것을 코드 확인 후 복구했다.

**남는 좁은 nuance(Low)**: `drain()`이 대기 중인 동안(`_stopping` 플립 전) `try_submit`이 새 작업을 수용할 수 있는 drain-semantics 경합. UAF는 아니며 종료 지연/잔여 작업 처리 수준.
**수정(선택)**: drain 진입 시 `_stopping`을 먼저 플립해 신규 submit을 즉시 거부하면 의미가 더 명확해짐.

---

## MEDIUM

- **M1 쿠키 경로 스코프** `cookie_jar.cpp:84`: `path.rfind(cookie.path, 0) != 0` 순수 prefix 테스트 → `Path=/admin` 쿠키가 `/administrator`에도 전송(RFC 6265 §5.1.4 경계 누락). 호스트 스코프·Secure는 정확 확인됨. **수정**: 정확 매치 또는 경계(`/`) 검사.
- **M2 코루틴 풀에 by-reference 캡처(잠복 UAF)** `route_handler_invoker.cpp:26`: `[&handlers,&services,…]`를 `handler_coroutine_executor()`에 제출. 현재는 호출부가 즉시 `.result()`로 블록해 안전하나, fire-and-forget 리팩터 시 dangling. **수정**: 소유 복사/shared_ptr 캡처 또는 블로킹 계약 문서화.
- **M3 spot/actor 런타임 비동기화 공유 맵** `spot_runtime.cpp`, `actor_gateway_runtime.cpp`: `std::map`/`std::set`에 뮤텍스 없음. deferred `leave_callback → close_now()`가 offload 풀에서 맵 변형하며 caller 스레드 `create_spot`/`relay_actor_packet`와 경쟁. `callback_mutex`는 depth/thread만 보호. **수정**: 노드 맵 보호 또는 모든 변형을 spot serial 큐로 funnel.
- **M4 엔진 어댑터 dangling 런타임(Axmol/Godot)** `connector/engines/axmol/src/...:120,125`: `runtime = _runtime.get()` raw 캡처, `~stream_connector_t() = default`가 pending 콜백이 stale 포인터 보유한 채 `_runtime` 소멸. Unreal과 달리 close-on-destruct 없음. **수정**: `weak_ptr` 캡처 또는 소멸자 close.
- **M5 코루틴 continuation이 모든 예외 삼킴(코루틴 정지 가능)** `coroutine_scheduler.cpp:28-34`: `try{ continuation(); }catch(...){}`. continuation이 `task_completion_source` 완료 전 throw 시 await 코루틴이 영구히 재개되지 않을 수 있다. **의심** — completion source가 throw 전 항상 resolve되는지 확인 필요.

---

## 부록 A. Low

- **L1 HTTP 압축 폭탄(출력 cap 없음)** `compression.cpp:35-49`: `inflate_raw`가 무제한 append. 작은 gzip/deflate가 무제한 팽창. H1 응답 cap과 연동 수정.
- **L2 버퍼 POST의 재사용 연결 실패 시 조용한 재시도** `request_performer.cpp:180-205`: keep-alive 끊김 시 비멱등 POST 이중 실행 가능.
- **L3 쿠키 jar 무제한 증가** `cookie_jar.cpp:67-75`: per-host cap 없음.
- **L4 `change_state`가 `transport_mutex` 없이 핸들러 벡터 순회** `connector_runtime.cpp:217-233`: 핸들러가 connect 후 등록될 때만 race(보통 setup-time).
- **L5 `stream_runtime.cpp` `closed` 플래그가 plain bool** `:310`: 풀 스레드가 세팅. atomic화 권장. **의심**.
- **L6 Axmol/Godot 기본 스레드 디스패처가 콜백 인라인 실행**: integrator가 `set_*_thread_dispatcher` 누락 시 엔진 메인 스레드로 마샬 안 됨(통합 footgun).

---

## 부록 B. 검토 후 "이상 없음" 확인 (오탐 방지)

- **`header_codec.cpp`/`metadata_codec.cpp` decode**: 모든 read가 `bytes.size()-offset < N` 경계 검사 선행, name/metadata/key/value 크기 모두 슬라이스 전 검증, trailing/중복 키 검사, uint8 승격 시프트 well-defined. over-read 없음.
- **`frame_codec.cpp` encode**: u16/u32 ceiling + `validate_frame_size` 정확. (decode 미호출이 CR1.)
- **`lz4_compression_codec.cpp`**: `size>=4` 검증, `original_size` INT32_MAX clamp, `LZ4_decompress_safe`로 정확 사이징·반환 길이 검증. 안전.
- **`connection_opener.cpp` TLS**: `verify_peer` + `host_name_verification` 항상 설정, skip-verify 옵션 없음, SNI/클라이언트 인증서/trust-file 경로 정확. **HTTPS 검증 비활성 아님.**
- **HTTP 연결 풀**: `scheme|host:port[|proxy]` 키 → wrong-host 재사용/smuggling 없음, 뮤텍스 보호, over-cap idle RAII close(fd 누수 없음).
- **`url.cpp` base-url/IPv6 파싱**, **`text.cpp`**: 경계 검사·malformed throw, 인덱싱 underflow/OOB 없음.
- **프레임워크 메시징 코덱**(`envelope_codec.cpp`, `client_call_codec.cpp`): nlohmann/json parse try/catch → 타입 실패, `message_parts_t::operator[]` 경계 검사, `gmtime_r`(thread-safe).
- **`framework_runtime.cpp` teardown**: 멤버/소멸 순서 정확, `drain()` 멱등, 소켓 reset 후 `context.term()`, double-free 없음.
- **`submit_queue.cpp`/`pending_submit.cpp`**: `_gate` 락, 멱등 `dispose_all`, single-shot 해소, 예외 안전.
- **stream-connector send 경로**: `validate_packet_limits`가 encode 전 payload/metadata cap·코덱 활성 검사, pending-request 맵은 `transport_mutex` 보호.

---

## 처리 우선순위
1. **CR2**(교차호스트 리다이렉트 auth 제거) — 원격 트리거. CR1은 2026-06-14에 수정 완료.
2. **H1**(응답 body_limit), **H2/H3**(Unreal IO 스레드 race + close-on-destruct; request 콜백도 동일 패턴 — Codex ADDITIONAL).
3. **M1**(쿠키 경계 + host+name dedupe 경로 무시), **M2/M3**(프레임워크 교차스레드 캡처/맵), **M4**(엔진 dangling 런타임).
4. ~~H4~~ executor drain — **Codex 반박으로 Low 격하**, 선택적 정리.
