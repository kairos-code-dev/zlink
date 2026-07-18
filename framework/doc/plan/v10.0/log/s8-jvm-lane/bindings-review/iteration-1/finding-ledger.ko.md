# S8 JVM bindings 리뷰 iteration-1 — 병합 finding ledger

두 리뷰어(R1 opus, R2 Sonnet) iteration-1(snapshot `5c2eb2acc`, 255파일 `8af1d48c`). 둘 다
`BINDINGS REVIEW NOT CLEAN`. **service 레이어(NativeServiceSymbols 77함수·ServiceLayouts)는 두 리뷰어
모두 Core 10.0.0과 정합 확인 — mesh 전환은 건전.** 결함은 전부 raw-socket 레이어 드리프트(권위 목록
`core/tests/contract/removed-identifiers-10.0.0.json` 304심볼 대조).

## coordinator 검증: 리뷰어 불일치 해소
- **JV-F1 router_recv_part arity**: R2=7파라미터 blocker, R1=6 정상으로 불일치. coordinator 직접 검증
  (`Native.java:390-393` `FunctionDescriptor.of(JAVA_INT, ADDRESS×6, JAVA_INT)` = 6 ADDRESS+1 INT = **7
  파라미터**; Core `socket/api.h:272-277`은 5 ADDRESS+1 INT = **6 파라미터**). **R2가 옳음**, R1 arity
  오판. 여분 `source_spot_rid` ADDRESS 잔존 → dotnet D2F1과 동일 스택 손상.

## I1/I3 (raw-layer 드리프트)

### JV-F1. router_recv_part 7→6 파라미터 [blocker]
`Native.java:390-393`·`397-402`(critical variant) FunctionDescriptor에서 여분 `source_spot_rid` ADDRESS
제거(6파라미터로), recv 경로 caller(spot_rid 읽는 곳) 정합. RouterSocket.recv() 경로.

### JV-F2. 제거 FUNC downcall — raw STREAM actor·spot option [blocker/high]
권위 목록의 제거 FUNC를 `Native.java`가 downcall:
- `zlink_stream_bind_actor`/`_unbind_actor`/`_bound_actors`/`_send_bound_actor_part`(4) — 10.0.0에서
  stream actor binding은 `zlink_stream_session_*`. dead wrapper. 제거.
- `zlink_get_spot_option`/`zlink_set_spot_option`(2) — dead SpotOptions(제거 enum
  `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS` 하드코딩). 제거.

### JV-F3. stream detach/attach + subscribe_handler [blocker]
- `zlink_stream_detach`가 `NativeStreamSocket.close()`(공개 `Socket.close()` 계약)에서 무조건 호출 →
  실제 10.0.0 lib에서 STREAM 소켓 close마다 IllegalStateException. Core 10.0.0 STREAM 모델은
  packet-handler(close=teardown, detach/attach 개념 없음, dotnet D2F2 참조). detach/attach_raw는
  `ZLINK_INTERNAL_EXPORT`(비공개) — 공개 바인딩이 의존하면 안 됨. 제거·close 경로 정렬.
- `zlink_subscribe_handler`(제거 TYPE `zlink_subscribe_handler_fn`) — `NativeSocketRuntime.onSubscribe`
  reachable → IllegalStateException. 제거/`zlink_xpub_recv_part` 모델로 재배선.

### JV-F4. dead NativeLayouts + 비계약 raw helper [high, I3]
`NativeLayouts.java` 제거 타입 미러 구조체(`zlink_actor_recv_info_t`/`_join_info_t`/`_route_t` + registry/
actor-route 레이아웃 16개, 참조 0) + Core 부재 비계약 helper(`zlink_router_handler`, `zlink_stream_attach*`,
`zlink_stream_send*`). 제거.

### JV-F5. router_handler downcall 중복 [low, I2]
`zlink_router_handler` downcall이 `Native.java`(dead)와 `NativeMessage.java`(live) 중복. dead 제거.

## 처리 방침
coordinator 격리 수정(dotnet iter-2 raw-layer fix와 동형). 모든 FFI downcall 심볼을
`removed-identifiers-10.0.0.json`·`nm -D libzlink.so.10.0.0`와 대조해 제거 심볼 0 확인. router_recv_part
6파라미터, STREAM close 경로 정렬, subscribe_handler 재배선/제거, dead 레이아웃·중복 제거.
:compileJava+samples+kotlin-samples green 유지. 완료 후 iteration-2.

**교훈**: 문자열 키 FFI 바인딩(jvm·dotnet)만 제거 심볼을 조용히 참조 가능(런타임 실패). cpp·node는
컴파일/링크로 방어됨. jvm·dotnet fix·리뷰는 `removed-identifiers-10.0.0.json` 대조를 필수 게이트로.
