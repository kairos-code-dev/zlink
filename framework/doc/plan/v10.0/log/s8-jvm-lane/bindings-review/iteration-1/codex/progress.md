# S8 JVM bindings 전환 리뷰 — R1(opus) progress

## 0. 실행 규칙 확인
- REVIEWER only. build/test/run/modify 금지. 정적 대조 + 국소 grep/read만 수행.
- 실행 증거는 manifest만 인용(compileJava + java/kotlin samples GREEN, 개념 no-hit 8종 0). 재실행 안 함.
- 다른 리뷰어(`claude-sonnet/`) 산출물·coordinator 해석 미참조.

## 1. Scope 확인
- 시작 hash 재계산:
  `git ls-files bindings/java/src/main bindings/java/native/src bindings/java/samples bindings/kotlin/samples bindings/java/build.gradle bindings/kotlin/samples/build.gradle | grep -vE 'native/linux|native/darwin|native/win|resources/native|/build/' | LC_ALL=C sort | xargs sha256sum | sha256sum`
  → `8af1d48c9ebc4c67d6ee90ba48a6350228300e4f819e03c68840611933dcdf12` (기대치 일치, 255 files).
- HEAD: `990f70339` (freeze commit; snapshot 대상 `5c2eb2acc` 내용과 hash 동치).

## 2. 대조 기준(권위 소스)
- Core 10.0.0 공개 C API: `core/include/zlink/service/{common,dispatch,mesh_node,spot,actor,stream_session}.h`, `core/include/zlink/socket/api.h`, `core/include/zlink/message/api.h`.
- **제거 식별자 권위 목록**: `core/tests/contract/removed-identifiers-10.0.0.json` (FUNC/TYPE/ENUM/... 제거 목록). ← 핵심 대조 자산.
- 빌드 산출물 `nm -D core/build-asan/lib/libzlink.so`로 심볼 존재 교차 확인(참고용, 판정은 header+contract 기준).

## 3. 수행한 검토
### I1 서비스-API 계약 일치 (전량 검증)
- `NativeServiceSymbols`의 **모든** service downcall `FunctionDescriptor` 파라미터 수·타입을 C 시그니처와 1:1 대조 → 전부 일치.
  - mesh_node 12, node/channel 4, publisher 4, actor 12, spot 15, dispatch 18, stream_session 12 함수 확인.
  - size_t/uint64→L, 포인터/구조체 out→A, enum/uint32/flags/timeout→I, mask→I 규약 정합.
- `ServiceLayouts` 구조체 레이아웃/수동 패딩을 C 자연 정렬로 손계산 대조 → 전부 일치.
  - 기반 크기: `zlink_routing_id_t`=256B(align1), `zlink_actor_ref_t`=520B(align8) 확인(`NativeLayouts`).
  - MESH_NODE_STATUS(1128), MESH_PEER_ENTRY(824), ACTOR_LOCATION(800), SPOT_STATUS(328), READY_RECORD(792),
    RECEIVE_RECORD(1192; operation_kind 뒤 pad4·reply_token 8정렬), STREAM_SESSION_BINDING(800),
    STREAM_SESSION_STATUS(64) 등 offset/pad 전부 검증.
- `ServiceInterop.stampHeader`: struct_size=layout.byteSize(), version=ABI_VERSION(1). Core `ZLINK_*_ABI_VERSION 1u`와 정합, byteSize=C sizeof 검증됨.
- **manifest §3가 지목한 retained raw 심볼 arity 재검증**:
  - `zlink_router_recv_part`: C 6-param(router, rid**, u64*, msg*, part_flag*, recv_flags) ↔ binding `of(I,A,A,A,A,A,I)` = 6 → **일치**(dotnet router_recv_part 류 결함 없음).
  - `zlink_msg_refcnt`: C 2-param(msg*, error_out*) ↔ binding `of(I,A,A)` = 2 → **일치**.

### I3 정리 완결성 (핵심 발견 축)
- binding `src/main` 내 모든 `"zlink_*"` downcall 심볼 문자열 추출 → `removed-identifiers-10.0.0.json` FUNC/TYPE와 교차.
- Core 10.0.0(headers+src+built .so) 부재 심볼 존재 여부, `downcall`/`downcallAny` 등록 유형(sole vs fallback), 호출 도달성(reachability) 확인.
- `downcallAny(["zlink_multipart_close","zlink_msgv_close"])`은 primary가 현행 심볼 → 결함 아님(오탐 배제).
- 잔재 확인: 미사용 `SpotOptions`(제거 심볼 get/set_spot_option + 제거 enum), 미호출 old stream invoker 4종, 미사용 `NativeLayouts` legacy 레이아웃(제거 TYPE 대응).

### 개념 no-hit
- prompt 폐기 개념(SpotNode/route_bridge/subjects/internal_sockets/pub·sub rid/dispatch_workers/recv_actor_part/msg_gets) 직접 grep → 0(coordinator 8/8과 일치).

## 4. 결론
- I1(service surface)·I2 CLEAN. **I3 NOT CLEAN**: Core 10.0.0이 제거한 함수/타입 식별자에 대한 FFI downcall·레이아웃 잔재가 raw 레이어에 존속.
- 최종: **BINDINGS REVIEW NOT CLEAN** (상세 `review.ko.md`).
