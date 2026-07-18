# S8 JVM(Java/Kotlin) bindings 전환 리뷰 — R1(opus), iteration 1

독립 adversarial 리뷰. 판정 근거: Core 10.0.0 공개 C API(`core/include/zlink/service/*.h`,
`core/include/zlink/socket/api.h`, `core/include/zlink/message/api.h`)와 Core 권위 제거목록
`core/tests/contract/removed-identifiers-10.0.0.json`. 다른 리뷰어·coordinator 해석 미참조.

---

## 1. Scope 확인
- 재계산 hash `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` = 기대치 일치, 255 files.
- 시작·종료 동일(하단 §6). 파일 수정 없음. build/실행 없음.

---

## 2. I1 — 계약 일치

### 2.1 서비스 API 표면 = CLEAN (전량 검증)
전환의 본체인 서비스 downcall/레이아웃은 결함 없음.

- **downcall 시그니처**: `NativeServiceSymbols`의 mesh_node·node/channel·publisher·actor·spot·dispatch·
  stream_session **모든** `FunctionDescriptor`를 C 시그니처와 파라미터 수·타입 1:1 대조 → 전부 일치.
  대표 검증: `zlink_mesh_node_actor_new`(7), `zlink_mesh_node_actor_join_spot`(9),
  `zlink_spot_request_to_spot`(10), `zlink_mesh_node_drain_ready`(5), `zlink_mesh_claim_recv_batch`(4),
  `zlink_mesh_receive_batch_retain_message`(4), `zlink_actor_join_reply`(5), `zlink_stream_session_unbind_actor`(6).
  size_t/uint64→`L`, 포인터/구조체 out→`A`, enum/flags/uint32/timeout→`I`, ready mask→`I` 규약 정합.
  `READY_HANDLER_DESCRIPTOR of(I,A,I,A)` = `zlink_mesh_ready_handler_fn(void*, mask, void*)→mask` 일치.
- **struct 레이아웃/패딩**(`ServiceLayouts`): `JAVA_*_UNALIGNED`+`ADDRESS.withByteAlignment(1)` 위에
  **수동 패딩**으로 C 자연 정렬 재현 → 손계산으로 offset·size 전부 대조, 전부 일치.
  기반: `zlink_routing_id_t`=256B(align1), `zlink_actor_ref_t`=520B(align8).
  검증 사례: `MESH_NODE_STATUS`(sizeof 1128, lifecycle_generation 앞 pad4·last_changed_ms 앞 pad4),
  `RECEIVE_RECORD`(sizeof 1192, `operation_kind` 뒤 pad4로 `reply_token` uint64[4] 8정렬),
  `SPOT_STATUS`(328, spot_kind 뒤·last_error 뒤 pad4), `STREAM_SESSION_STATUS`(64, state 뒤·tail pad4),
  `MESH_PEER_ENTRY`(824, pad 불필요·미삽입 정확), `ACTOR_LOCATION`(800), `READY_RECORD`(792),
  `STREAM_SESSION_BINDING`(800). `operation_id`를 high/low 2×u64로 편 것도 16B/8정렬 동치.
- **ABI 헤더 stamping**: `stampHeader`가 struct_size=`layout.byteSize()`, version=`ABI_VERSION(1)`.
  Core `ZLINK_*_ABI_VERSION 1u`와 정합, byteSize=C sizeof 검증됨.
- **manifest §3 지목 raw arity 재검증(정합 확인)**:
  - `zlink_router_recv_part` — C 6-param ↔ binding `of(I,A,A,A,A,A,I)` = 6. **정합**. dotnet router_recv_part
    인자수 결함(무성 스택 오염)과 같은 문제 **없음**.
  - `zlink_msg_refcnt` — C `(const zlink_msg_t*, zlink_config_result_t*)` 2-param ↔ binding `of(I,A,A)`. **정합**.

### 2.2 retained raw 심볼 중 계약에서 사라진 것 (I1/I3 경계)
manifest §3는 "jvm이 raw path를 유지했으므로 FFI downcall 시그니처가 Core 10.0.0과 정합인지 확인"을 요구한다.
검증 결과, 유지된 raw 심볼 중 **`zlink_subscribe_handler`는 Core 10.0.0에 존재하지 않는다**:
콜백 typedef `zlink_subscribe_handler_fn`이 제거목록 TYPE에 등재, 함수 `zlink_subscribe_handler`는
공개 헤더·`core/src`·빌드 산출물(`nm -D`) 어디에도 없음. binding은 단일(fallback 없음) downcall을 걸고
공개 경로 `NativeSocketRuntime.onSubscribe → SocketCore.onSubscribe → Native.subscribeHandler`가 도달 가능.
정합 대상 계약이 부재하므로 "시그니처 정합" 요구를 충족할 수 없음 → 아래 F2로 분류.

---

## 3. I3 — 정리 완결성 (NOT CLEAN)

**Root-cause family(단일 근본원인).** 서비스 계층은 신규 `NativeServiceSymbols` 경로로 올바르게 전환됐으나,
**raw-socket 레이어를 통째로 유지하면서 Core 10.0.0이 제거한 함수/타입 식별자에 대한 FFI downcall과 레이아웃
정의를 삭제하지 않았다.** 신규 서비스 경로가 이들을 대체(supersede)하여 잔재는 대부분 dead지만,
`removed-identifiers-10.0.0.json`이 권위적으로 "제거"로 규정한 식별자를 여전히 참조한다. iteration 1
기준(축 CLEAN=finding 0)에서 제거-심볼 FFI downcall 잔재는 finding이다.

### F1 (I3) — 제거된 함수 심볼에 대한 FFI downcall 잔존
`removed-identifiers-10.0.0.json` FUNC 등재 심볼을 `Native.java`가 계속 downcall:

| 심볼(제거목록 FUNC) | binding 위치 | 대체(현행) | 상태 |
|---|---|---|---|
| `zlink_stream_bind_actor` | `Native.java:149` MH + `streamBindActor` invoker | `zlink_stream_session_bind_actor`(NativeServiceSymbols) | wrapper 미호출(dead) |
| `zlink_stream_unbind_actor` | `Native.java:155` + `streamUnbindActor` | `zlink_stream_session_unbind_actor` | dead |
| `zlink_stream_bound_actors` | `Native.java:167` + `streamBoundActors` | `zlink_stream_session_bindings` | dead |
| `zlink_stream_send_bound_actor_part` | `Native.java:161` + `streamSendBoundActorPart` | `zlink_mesh_node_actor_send_bound_session` | dead |
| `zlink_get_spot_option` | `Native.java:201` MH_GET_SPOT_OPTION | (spot는 service화; 해당 소켓옵션 폐기) | `SpotOptions`에서 사용, 그러나 `SpotOptions` 자체 미사용(dead) |
| `zlink_set_spot_option` | `Native.java:196` MH_SET_SPOT_OPTION | 동상 | dead |

- 증거(대체 경로 정상): `nm -D`에서 `zlink_stream_session_bind_actor`·`zlink_multipart_close` EXPORTED 확인.
- 증거(잔재 미호출): `streamBindActor/streamUnbindActor/streamBoundActors/streamSendBoundActorPart`의
  `Native.java` 외 호출자 0. `SpotOptions`의 외부 참조 0.
- `SpotOptions`는 추가로 제거 enum `ZLINK_SPOT_OPT_REQUEST_TIMEOUT_MS`(제거목록 ENUMERATOR)를
  `0x3701` 하드코딩으로 참조.

### F2 (I3/I1 latent) — 제거된 subscribe 콜백 심볼 참조 + 도달 가능 경로
- `zlink_subscribe_handler`(함수) Core 부재, typedef `zlink_subscribe_handler_fn` 제거목록 TYPE 등재.
- `Native.java:85` 단일 downcall(fallback 없음). 공개 경로 `NativeSocketRuntime.onSubscribe`(public)까지 도달.
- 현행 Core는 SUB 콜백 수신 심볼을 제공하지 않음(`zlink_recv_handler`는 raw STREAM 전용, SUB는 polling
  `zlink_subscribe_part`). 실제 10.0.0 라이브러리 링크 시 이 경로 호출은 `missingDowncall`의
  `IllegalStateException("Missing native symbol ... incompatible")`로 귀결. samples가 green인 것은 이 경로가
  샘플에서 미행사되거나 bundled legacy `.so`(scope 제외 `resources/native`)에 잔존 심볼이 있기 때문으로 판단.

### F3 (I3, 관찰·보강) — 미사용 legacy 레이아웃/부재 raw 헬퍼
- `NativeLayouts`의 `ACTOR_RECV_INFO_LAYOUT`·`ACTOR_JOIN_INFO_LAYOUT`·`ACTOR_ROUTE_LAYOUT`은 각각
  제거목록 TYPE `zlink_actor_recv_info_t`·`zlink_actor_join_info_t`·`zlink_actor_route_t`를 미러링하며
  외부 참조 0(정의만 존재). `ACTOR_CREATE_RESULT_LAYOUT`·`SPOT_ROUTE_LAYOUT`·`ACTOR_JOIN_RESULT_LAYOUT`도
  미사용 잔재.
- `zlink_router_handler`(`Native.java:385` MH_ROUTER_SPOT_HANDLER, `NativeMessage.java:36` MH_ROUTER_HANDLER),
  `zlink_stream_attach`/`zlink_stream_attach_len32be`/`zlink_stream_send`/`zlink_stream_send_msg`는
  Core 10.0.0 공개 헤더·`core/src`·built `.so` 모두 부재(제거목록 미등재 → 과거에도 비공개였을 가능성).
  이 중 `zlink_router_handler`는 `NativeRouterReceiveSupport`의 `onReceive` 콜백 경로로 도달 가능.
  적어도 "현행 Core 공개 계약에 대응 심볼 없음"이므로 비계약 의존으로 확인 필요.
- `zlink_stream_attach_raw`(2-param `(s, on_raw_fn)`)·`zlink_stream_detach`(1-param)는 Core에
  `ZLINK_INTERNAL_EXPORT`로만 존재(공개 계약 아님). binding은 `SocketCore`에서 raw STREAM에 사용.
  공개 STREAM raw 경로는 `zlink_stream_packet_handler`(binding도 보유). 비공개 내부심볼 의존은 관찰로 기록.
  - 부수 관찰: `MH_STREAM_ATTACH_RAW` descriptor는 `of(I,A,A,A)`(3-param)인데 C `zlink_stream_attach_raw`는
    2-param. invoker는 2-arg 전달. 내부심볼 의존 자체가 비계약이므로 F3에 병합(별도 승격 안 함).

> 오탐 배제: `MH_MSGV_CLOSE`는 `downcallAny(["zlink_multipart_close","zlink_msgv_close"])`로 primary가
> 현행 심볼 `zlink_multipart_close`(EXPORTED 확인) → **결함 아님**. `zlink_java_msg_data_addr`/
> `zlink_java_send_u32`는 binding 자체 glue(`bindings/java/native/src/zlink_java_reqrep_bridge.c`) 제공 → 정상.

---

## 4. I2 — POSD·DDD
- 서비스 계층 분리(`NativeServiceSymbols` downcall / `ServiceLayouts` 레이아웃 / `ServiceInterop` marshalling)는
  경계 명확하고 offset을 named path element로 도출하는 등 견고. 신규 표면 자체의 POSD 결함 미발견 → 이 축은 CLEAN.
- 단, §3의 이중 경로(신규 service + 미삭제 old raw wrapper)·dead class(`SpotOptions`)는 DDD 관점 잔재 냄새이나
  근본원인이 정리 미완결이므로 I3(F1~F3)로 귀속. I2 독립 finding으로 별도 계상하지 않음.

---

## 5. 폐기 no-hit 판정
- **prompt 폐기 개념 목록**(SpotNode/route_bridge/subjects/internal_sockets/pub·sub rid/dispatch_workers/
  recv_actor_part/msg_gets): 직접 grep 결과 hit 0 → **PASS**(coordinator 8/8과 일치).
- **보강(권위 제거목록 기준)**: 위 개념 목록은 coordinator no-hit이 커버한 범위이나, `removed-identifiers`
  **FUNC/TYPE 심볼** 차원의 no-hit은 **실패**(F1·F2·F3). 즉 개념-키워드 no-hit은 통과하되 제거-심볼 downcall은
  잔존. iteration 1 축 판정은 후자를 finding으로 계상.

---

## 6. 종료 Scope 재확인
- 종료 hash `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` = 시작 일치(255 files). 무수정.

---

## 7. 축별 Verdict
| 축 | 판정 | 근거 |
|---|---|---|
| I1 계약 일치(service 표면) | CLEAN | downcall arity·layout·padding·ABI stamp 전량 정합. router_recv_part/msg_refcnt arity 정합 |
| I2 POSD·DDD | CLEAN | 신규 표면 구조 견고. 잔재는 I3 귀속 |
| I3 정리 완결성 | **NOT CLEAN** | F1(제거 FUNC 심볼 downcall 6종)·F2(제거 subscribe 심볼+도달경로)·F3(미사용 제거 TYPE 레이아웃·비계약 raw 헬퍼) |
| 폐기 개념 no-hit | PASS | 개념 8종 0 |

권고(비판정): 신규 서비스 경로가 대체한 old raw stream/spot downcall·wrapper·미사용 legacy 레이아웃·SUB
콜백 경로를 삭제하고, raw STREAM은 공개 `zlink_stream_packet_handler` 경로로 일원화. 잔여 부재 심볼
(`zlink_router_handler`, `zlink_stream_attach*`, `zlink_stream_send*`)은 현행 Core 계약 심볼로 재매핑 또는 제거.

BINDINGS REVIEW NOT CLEAN
