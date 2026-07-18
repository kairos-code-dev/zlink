# S8 JVM bindings 전환 리뷰 — iteration 4 — R1 (opus, codex 슬롯)

독립 리뷰. R2·coordinator 해석을 판정 근거로 쓰지 않음. 정적 대조만(빌드·실행·수정 없음).
`../iteration-4/claude-sonnet/` 미열람. 4회차 규칙: 각 축 CLEAN = blocker/high/medium finding 0.
low는 별도 기록하되 CLEAN을 막지 않음.

## 1. Scope 확인
- 대상 commit `7403cb5c9` (freeze HEAD `41660e37b`의 부모). 파일 수 **251** — 일치.
- aggregate SHA-256(`LC_ALL=C sort` 재sha256sum) =
  `fa9c1de94a291e3dad452c648896eee9620ad7815050ea6a91dd096ab07837ff` — **일치** (시작·종료 동일).
- 파일 미수정.

## 2. iter-3 finding 해소 판정

### JV3-1 (dead `SERVICE_EVENT_LAYOUT` + 22 offset 상수) — RESOLVED
- commit `7403cb5c9` diff: `NativeLayouts.java` −45줄 = `SERVICE_EVENT_LAYOUT` + 10개
  `SERVICE_EVENT_*_OFFSET`(byteOffset 상수 22개를 포함한 블록 전체) 삭제.
- 재grep: `SERVICE_EVENT` / `service_kind` / `subject_kind`가 java src·samples·kotlin samples
  전역 참조 **0**. (잔여 매치는 scope 제외 대상인 `resources/native`의 prebuilt
  `libzlink.so.9`·`.dylib` 바이너리뿐 — 소스 아님.)
- 대응 live Core struct 여전히 부재 → 삭제로 인한 ABI/정확성 영향 없음.

### iter-3 R2 low (`optionalDowncall` 미사용 helper) — RESOLVED
- diff: `Native.java` −4줄(`private static optionalDowncall` wrapper), `NativeSymbols.java` −6줄
  (`static optionalDowncall`). 재grep 참조 **0**.

### 삭제로 인한 새 orphan 부재 — CONFIRMED
- `NativeSymbols.java`: 삭제 후 남은 import 8종(FunctionDescriptor/Linker/MemorySegment/
  SymbolLookup/ValueLayout/MethodHandle/MethodHandles/MethodType) 전부 여전히 사용
  (`downcall`·`downcallCritical`·`downcallAny`·`cDowncall`·`freeDowncall`·`require`·`missingDowncall`
  잔존·소비). 새 unused import·helper 없음.
- `Native.java`: `downcall`/`downcallCritical` wrapper 잔존·다수 소비. import orphan 없음.
- `NativeLayouts.java`: 삭제 위치(`MONITOR_REMOTE_OFFSET`와 `ACTOR_ID_MAX` 사이) 인접 심볼
  `MONITOR_EVENT_LAYOUT`+offsets(소비처 `NativeMonitorSocket`·`Native`), `ACTOR_REF_LAYOUT` 정상
  상주·소비. import(MemoryLayout/ValueLayout/PathElement/ROUTING_ID_LAYOUT) 전부 잔여 코드가 사용.

## 3. 3축 재검토 (7403cb5c9)

### I1 — FFI descriptor/type 정확성 — CLEAN
- downcall MethodHandle 전수 대조: descriptor 파라미터 수 == invokeExact 인자 수 == Core C 파라미터 수.
  **총 187 handle 검사(185 downcall + 2 upcall descriptor), 불일치 0.**
  - `Native.java` 85, `NativeServiceSymbols.java` 75, `NativePollerSymbols.java` 13,
    `NativeMessage.java` 12(+`downcallAny`의 `zlink_multipart_close`, default-lookup `free`).
- 넓은 시그니처 a=b=c 명시 검증: `zlink_spot_request_to_spot` 10/10/10(spot.h:92),
  `zlink_subscribe_part` 8, `zlink_xpub_recv_part` 7, `zlink_router_request_part` 8,
  `zlink_dealer_request_part` 7, `zlink_router_recv_part` 6. 모든 `_CRITICAL` 변형은 대응
  non-critical과 동일 descriptor/arity.
- upcall 콜백 vs Core typedef: `FD_REPLY_CALLBACK`=ofVoid(INT,ADDR,LONG,ADDR)=4 vs
  `zlink_reply_handler_fn`(socket/api.h:54) 4; `READY_HANDLER_DESCRIPTOR`=of(I,A,I,A)=3 vs
  `zlink_mesh_ready_handler_fn`(dispatch.h:138) 3.
- bridge 심볼(public 헤더 외, `zlink_java_reqrep_bridge.c` 대조):
  `zlink_java_msg_data_addr` 1/1, `zlink_java_send_u32` 5/5.
- ServiceLayouts: 소비처 다수(MeshCalls·NativeClaim·NativeMeshNode·NativeSpot·NativeReadyBatch·
  NativeReceiveBatch·NativeStreamSessionService 등) — live. iter-3 R1의 17 구조체 ABI 대조 결과
  (필드 순서·폭·padding·총크기) 유지, 이번 삭제 커밋은 ServiceLayouts 미변경.
- Verdict: **CLEAN** (blocker/high/medium 0).

### I2 — POSD·DDD — CLEAN
- 이번 커밋은 순수 dead-code 삭제(3파일 −55줄, 기능 추가 0). 콜백 수명·arena 고정·
  등록 실패 롤백·dead handler 시 parts 1회 해제 등 iter-3 CLEAN 판정 구조에 변화 없음.
- 삭제가 캡슐화·책임 경계·이름 일관성을 훼손하지 않음(제거 대상이 소비처 없는 orphan이었음).
- Verdict: **CLEAN** (blocker/high/medium 0).

### I3 — 정리(dead code·no-hit·제거/부재 심볼) — CLEAN
- iter-3 유일 finding JV3-1(dead layout) 및 R2 low(optionalDowncall) 해소, 새 orphan 0(§2).
- 제거·부재 심볼 게이트(독립 재계산): FFI 층 distinct `zlink_*` string literal **178**,
  `core/tests/contract/removed-identifiers-10.0.0.json`의 removed 식별자 **119~120**,
  교집합 **0**. subagent sweep도 동일(0 hit). 부재(unresolvable) 심볼 iter-3 근거 유지
  (`zlink_msgv_close` legacy fallback 미도달, java bridge export 2종) — 전부 정당.
- Verdict: **CLEAN** (blocker/high/medium 0).

## 4. low finding 목록
- 없음.
- (정보성, finding 아님) 삭제 자리에 이중 공백 라인 3곳(`Native.java`, `NativeSymbols.java`,
  `NativeLayouts.java`). 순수 whitespace로 식별자·dead code가 아니며 컴파일·동작 무영향. 기록만.

## 5. 제거·부재 심볼 판정
- removed-identifiers 적중 **0**, 부재(unresolvable) FFI 심볼 **0**. **PASS**.

## 6. 종합
- iter-3 JV3-1 + R2 optionalDowncall 해소 확인, 새 orphan 0.
- I1·I2·I3 세 축 모두 blocker/high/medium 0. low 0.

BINDINGS REVIEW CLEAN
