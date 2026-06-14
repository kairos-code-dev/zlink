# 바인딩 공개 계약(Public Contract) 교차 패리티 전수 조사

- **작성일**: 2026-06-14
- **대상**: `bindings/{c,cpp,dotnet,go,java,node,python,rust}` 공개 API 표면
- **기준(canonical)**: `bindings/c/include/zlink/**.h` — C 헤더가 계약의 substrate baseline (총 **182개 공개 `zlink_*` 함수**, 8개 영역)
- **방식**: 언어별 병렬 정밀 매핑(capability 단위, 이름이 아닌 기능 기준) → 교차 대조. 헤드라인 갭은 소스 grep으로 직접 확정.
- **상태**: 조사와 구현 추적 리포트. §8의 확정 구현 항목은 코드와 spec README 반영 완료.

> **읽는 법**: 바인딩은 C의 평면적 `*_part`/`get_set_option` 프리미티브를 빌더·RAII·타입 옵션 facade로 **관용적으로 재구성**한다.
> 그런 재구성은 갭이 아니다. 아래 "갭"은 **실제 capability 누락**만 집계한다.
> 기호: ✓ 제공 · ✗ 누락 · ~ 관용적 대체/부분(아래 주석).

> 🛑 **적용 전 필독 — §8 Spec 정합성 게이트.**
> §1~§6의 패리티는 **C 계약(`bindings/c/include`)을 기준**으로 한다. 그러나 바인딩의 **진짜 target은
> `doc/spec/bindings/` 블루프린트**다(코드가 spec을 따라가는 방향). 2026-06-14 spec 대조 결과,
> 보고된 "갭" **상당수가 바인딩 spec의 의도적 결정**(예: DEALER reply 금지, Python/Rust callback-only,
> 관리언어 zero-copy 금지)이었다. **§1~§6을 그대로 코드에 적용하면 spec 위반이 된다.**
> 반드시 **§8의 spec 게이트로 재분류한 뒤 통과 항목만** 적용할 것.

---

## 0. 요약 — 바인딩-vs-바인딩 패리티

목적: **모든 바인딩이 동일 레벨의 기능을 제공하는가** — 특정 기능이 일부에서 누락됐거나, 일부에만 불필요하게 추가됐는지 교차검증.
기준은 **바인딩 ↔ 바인딩**(C 계약 아님). 실제 "바인딩마다 다른" 항목은 **10개**(§1.1), 세 부류:

**① 누락 → 5개 바인딩에 추가 완료** (사용자 확정: spec이 누락했을 뿐, 추가해야 함)
- **route table**(bind/unbind/resolve_route): **.NET·Java만** 보유 → **C++·Go·Node·Python·Rust에 추가 완료**.
- **connect_router_channel_peer_rid**: **.NET·Java만**(`SpotNode.Channels.cs:81`, `SpotNode.java:37`) → **5개에 추가 완료**.
→ .NET·Java가 정답. 바인딩 spec에 없던 건 spec 누락 → **코드 + spec README 동시 보강**(§8.4 IMPL).

**② 소수 누락 → 전부 추가 확정** (사용자 2026-06-14)
- `atomic_counter`·`thread`: **C++·Go·Rust** 누락 → 추가.
- `stopwatch`: **C++·Rust** 누락 → 추가.
- `subscription_at`: **Python·Rust** 누락 → 추가.
- `poll`(one-shot)·`stream_bound_actors`: **C++** 누락 → 추가.
→ §8.4 IMPL-COREUTIL/IMPL-SINGLE. 네이티브 core 유틸은 std 등가물이 있어도 라이브러리 API 패리티 위해 노출.

**③ 의도된 차이**
- async/future 종료 단자: **Python·Rust** 미제공 — spec 규정 callback-only(§6.6).
- 빌더 part copy/move 선택: **C++·Go만** — 네이티브 소유권 idiom.

**초판 오류 제거(중요)**: ~~DEALER reply~~ 는 **어떤 바인딩도 제공하지 않음** — router/`Received.reply()`를 dealer reply로 오인한 것(§1.2). ~~zero-copy init_data~~ 는 C++ 외 의도적 제외(사용자 확인) → 둘 다 매트릭스에서 제외.

> **적용 기준은 §8 spec 게이트.** ①은 spec 결정, ②는 spec 확인 후 누락분 구현, ③은 현행 유지.

---

## 1. 바인딩-vs-바인딩 패리티 매트릭스 (재작성 2026-06-14)

> **기준 = 바인딩 ↔ 바인딩**(C 계약 아님). "모든 바인딩이 동일 레벨인가"를 보려면 **어떤 바인딩은 제공하고 어떤 건 안 하는 실제 차이**만 본다. 전 바인딩이 똑같이 제공/미제공하면 불일치가 아니므로 제외.
> 초판(C 계약 baseline)의 오류 2건 제거: **dealer reply**(어떤 바인딩도 제공 안 함 — router/`Received.reply` 오인), **zero-copy init_data**(C++ 외 의도적 제외, 사용자 확인). 자세한 사유는 §1.2.
> ✓=공개 제공 · ✗=미제공 · C열은 substrate(참고). 코드 직접 grep으로 전 항목 재확인.

### 1.1 실제 불일치 (binding ↔ binding)

| 항목 | C | C++ | .NET | Go | Java | Node | Python | Rust | 제공/7 | 분류 |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|---|
| **route table** (bind/unbind/resolve_route) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **① 구현 완료** |
| **connect_router_channel_peer_rid** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **① 구현 완료** |
| **atomic_counter** (6종) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **thread** (start/join) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **stopwatch** (start/inter/stop) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **subscription_at** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **poll** (one-shot) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **stream_bound_actors** | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 7 | **② 구현 완료** |
| **async/future 종료 단자** (request) | – | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✗ | 5 | ③ 의도(spec callback-only) |
| **part copy/move 선택** (빌더) | – | ✓ | ✗ | ✓ | ✗ | ✗ | ✗ | ✗ | 2 | ③ 의도된 idiom 차이 |

분류 범례:
- **① 누락(2/7만 보유) → 5개 추가 확정**: .NET·Java만 보유. **바인딩 spec에 없는 건 spec이 누락한 것**(사용자 확정 2026-06-14) — .NET·Java가 정답이고 C++·Go·Node·Python·Rust가 **빠뜨린 것**. → **5개 바인딩에 추가 + spec README 보강**(§8.4 IMPL-ROUTE/IMPL-CONNECTRID).
- **② 누락 → 전부 추가 확정**(사용자 2026-06-14): core 유틸(C++/Go/Rust), `subscription_at`(Python/Rust), `poll`·`stream_bound_actors`(C++) 모두 누락분 추가 + spec 보강(§8.4 IMPL-COREUTIL/IMPL-SINGLE). 네이티브 core 유틸은 std 등가물이 있어도 라이브러리 API 패리티 위해 노출.
- **③ 의도된 차이**: async 단자(Python·Rust callback-only)는 spec 명시(§6.6). copy/move는 네이티브 언어의 소유권 idiom.

### 1.2 매트릭스에서 제외한 항목 (불일치 아님)

| 항목 | 실제 제공 바인딩 | 사유 |
|---|---|---|
| ~~DEALER reply~~ | **0개** | 초판 오류 — router/`Received.reply()`를 dealer reply로 오인. 어떤 바인딩의 DEALER에도 `reply` 없음(C++ send/recv, Go Send/Request, Rust send/recv/request, Node request, Python send/request). spec도 dealer reply 금지. → **갭 아님** |
| zero-copy init_data + free_fn | C++ 단독 대상 | **C++ 외 의도적 제외**(사용자: 성능 오버헤드). C++은 복사·zero-copy **두 버전 다 제공** 확정 → zero-copy 버전 추가(§8.4 IMPL-INITDATA-CPP). 바인딩 간 불일치는 아니나 C++ 단독 보강 |
| ctx_set_data / public errno / msg_adopt | 1~2개 산발 | 관리언어는 예외/소유권 이전으로 흡수하는 관용 항목. 네이티브 user-pointer/errno는 거의 안 씀 → 의미 있는 parity 차이 아님(관용적 흡수) |
| spot/spot_node 옵션 튜닝 | 6개(타입 프로퍼티) | Python만 미배선이나 대부분 `ISpotNode` 프로퍼티로 노출 → §6 단일갭과 함께 D-SINGLE에서 확인 |

> **요약**: 진짜 "바인딩마다 다른" 항목은 §1.1의 10개. 이 중 **① 2개(과잉 의심: route table, connect-rid)**, **② 6개(누락)**, **③ 2개(의도)**. 적용 판단은 §8 게이트.

---

## 2. 갭 상세 — 특정 라이브러리에서만 누락

### G1 [최대] Discovery route table — C++·Go·Node·Python·Rust 부재
- C 계약: `zlink_discovery_bind_route` / `zlink_discovery_unbind_route` / `zlink_discovery_resolve_route` (+ `zlink_route_kind_t`, `ZLINK_ROUTE_KIND_*`)
- **제공**: .NET(`IDiscovery.BindRoute/UnbindRoute/ResolveRoute`), Java(`Discovery.bindRoute/unbindRoute/resolveRoute`)
- **부재**: C++, Go, Node, Python, Rust — 소스 grep 0건(FFI 선언도 없음, test/ffi hit만 존재). resolve_actor/resolve_spot은 있으나 일반 route-row API 전체가 빠짐.
- 영향: registry route 테이블에 owner-scoped 라우트를 직접 bind/resolve하는 기능이 5개 언어에서 불가.

### G2 msg_init_data (zero-copy) — **C++ 외 의도적 제외 (갭 아님, §8 게이트 통과)**

> 🟢 **사용자 확인(2026-06-14)**: zero-copy는 **C++을 제외하고 일부러 제외**했다 — 다른 언어는 성능 오버헤드 대비 의미가 없어서다. 따라서 Go·Java·Node·Python·Rust·.NET의 zero-copy 미제공은 **의도된 설계**이며 갭이 아니다. 아래 "부재" 서술은 사실이나 **수정 대상 아님**. **C++만** zero-copy가 의도된 언어이고, `external_message_t::from(span, free_fn, hint)` 보강으로 완료했다(§8 D-INITDATA-CPP).

(이하 초판 분석은 기록 보존 — 단 C++ 외는 "제외가 의도"로 읽을 것)

- C 계약: `zlink_msg_init_data(data, size, free_fn, hint)` — 외부 소유 버퍼를 복사 없이 어댑트 + 해제 콜백.
- **C에만 완전 제공.** 초판은 C++/​.NET을 ✓로 봤으나 Codex 검증에서 정정:
  - **C++ ✓(복사+zero-copy)**: `message_t::from(span)`은 안전 기본값으로 복사하고, `advanced::external_message_t::from(span, free_fn, hint)`는 `zlink_msg_init_data`에 직결해 호출자 버퍼를 복사 없이 메시지에 맡긴다. 메시지가 버퍼를 해제할 때 `free_fn(data, hint)`를 한 번 호출한다.
  - **Rust ✗**: `ffi.rs:983` 선언만, `Message::try_from`은 항상 `copy_nonoverlapping`(`runtime/messaging/message.rs:140`).
  - **Go ✗**(복사 생성자만), **Java ✗**(`PORTING_ISSUES.md #2` `test_msg_ffn` 미해결), **Node ✗**(`close()` no-op, free 콜백 없음).
  - **Python ~**(`from_`로 복사 init_data, free_fn 경로 없음), **.NET ~**(public `init_data`이나 zero-copy/free_fn 의미 미검증).
- 영향: C++의 대용량 zero-copy 송신과 외부 버퍼 소유권 콜백은 보강 완료. C++ 외 바인딩은 사용자 확인에 따라 의도적으로 제외한다. (메모리: Python zero-copy `.data`는 *수신* 경로; *송신* init_data와 별개.)

### G3 DEALER 서버측 recv/reply — **갭 아님: spec가 dealer reply 금지** (§8)

> 🛑 **재분류**: spec(`doc/spec/bindings/python/README.md:470`)이 "Dealer sockets **must not** expose `reply(...)` — peer routing id 없음"을 명시. C++/Node/Python의 dealer reply 부재는 **정상**. 오히려 dealer reply를 가진 바인딩이 있으면 spec 위반(§8 D-DEALERREPLY 역검증). 아래 서술은 무효.

- C 계약: `zlink_dealer_recv_part` / `zlink_dealer_reply_part`
- **부재**: C++(reply 없음), Node(`DealerSocket`는 `request()`만), Python(`request`만, reply는 router 전용).
- **제공**: .NET, Go, Java, Rust(`Received::reply()`).

### G4 connect_router_channel_peer_rid — **spec 미정의 (결정 선행, §8 D-CONNECTRID)**

> 🛑 **재분류**: **모든 바인딩 spec이 connect-rid를 미열거**(disconnect-rid만). 즉 바인딩 계약에 connect-rid는 없으며, .NET/Java가 갖고 있다면 spec 앞섬. 자동 구현 대상 아님 → spec 결정(§8 D-CONNECTRID). 아래 서술은 참고.

- C 계약: `zlink_spot_node_connect_router_channel_peer_rid` (rid 지정 router-channel 연결)
- **부재**: C++, Go, Node, Python, **Rust**(Codex 정정 — `spot_node.rs:64,86`에 endpoint connect + disconnect rid만, connect rid 없음).
- **제공**: .NET(`SpotNode.cs:83`), Java(`SpotNode.java:37`)만. (endpoint 변종 `connect_router_channel_peer`는 전부 제공)

### G5 core 유틸 3종 — C++·Go·Rust가 std로 대체
- `atomic_counter`: **부재** C++, Go, Rust / 제공 .NET, Java, Node, Python
- `stopwatch`: **부재** C++, Rust / 제공 그 외(Go는 `NewStopwatch`로 충족)
- `thread(start/join)`: **부재** C++, Go, Rust(언어 네이티브 스레드 사용) / 제공 .NET, Java, Node, Python
- 성격: 대체로 언어 표준 라이브러리로 대체 가능(실질 기능 손실 적음). 단 spec이 공개 계약으로 열거하므로 패리티상 갭.

### G6 소수 바인딩 누락 (§1.1 ② 갱신)
- **C++만 부재**: `poll`(one-shot array poll; `poller_t` 객체만), `stream_bound_actors`.
- **Python·Rust 부재**: `subscription_at`(직접 grep 재확인 — Rust는 `ffi.rs`에만 있고 공개 계약 미노출). Python은 추가로 SPOT/spot_node 소켓 옵션 get/set(튜닝 프로퍼티 미배선).
- **public `errno` accessor**: C·Java만 노출. 그 외는 예외/result로 흡수(관용적, 실질 갭 아님).
- **C++ discovery route 메서드 caveat**: 조사 에이전트가 `discovery_models.hpp` 일부를 못 열었으나, include grep 0건으로 부재 확정.

---

## 3. 특정 라이브러리만 제공(EXTRA) — C 계약에 없는 것

| 기능 | 제공 바인딩 | 비고 |
|---|---|---|
| **Codec 패키지**(json/msgpack/protobuf) | cpp, dotnet, go, java, node, python, rust (**C 제외 전부**) | 직렬화 헬퍼. 일관된 생태계 add-on(별도 crate/package) |
| **message 직렬화 메서드**(`from_json`/`from_messagepack`/`from_protobuf`) | **C++ 전용** | `message_t`에 직접 |
| **async future API**(`async_result_t` / `.async()`) | **C++ 전용** | 콜백 substrate 위에 future 레이어 |
| **Discovery sync 토글**(`spot_owner_sync_enabled`/`actor_route_sync_enabled`) | cpp, dotnet, go, java, node, python, rust (**관리언어 전부**) | C 공개 헤더에 없음(내부 옵션 backing 추정) |
| **서비스 객체 TLS**(registry/discovery/spot_node `set_tls_*`) | cpp, dotnet, java, go, node | C는 소켓에만 노출 |
| **HWM/dispatch 튜닝 타입 프로퍼티** | 관리언어 대부분 | `spot_node_option` enum의 타입 facade |
| **UnhandledCallbackException 이벤트 / thread-affinity 헬퍼** | **.NET 전용** | 콜백 예외 허브 |
| **Netty ByteBuf 통합 / `readIntLe`·`writeLongLe`** | **Java 전용** | message I/O 헬퍼 |

> EXTRA 중 **네이티브 capability를 새로 추가하는 것은 없음** — 전부 관용적 facade거나 별도 직렬화 패키지다.

### ③ 헤더-바인딩 비대칭 정합성 조사 결과 (해소됨)

초판이 "계약 정합성 확인 필요"로 남긴 **Discovery sync 토글 / 서비스 TLS**를 추적한 결과 — **둘 다 관용적 facade이고 C 공개 헤더 추가는 불필요**:

- **A) Discovery sync 토글**(`spot_owner_sync_enabled`/`actor_route_sync_enabled`): 전용 네이티브 심볼이 **존재하지 않음**. 전 바인딩이 **일반 `zlink_set_option`/`zlink_get_option`**(`socket/api.h:187`) + 옵션 enum(`zlink_enum.h:130-131`: `ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC=0x3035`, `ACTOR_ROUTE_SYNC=0x3036`)으로 라우팅. 코어 처리는 `discovery_state.cpp:158,199`의 옵션 switch. (Rust `discovery_runtime.rs:89`, Node `discovery.ts:128`, Go `discovery_runtime.go:51` 모두 generic setter 경유.) → **헤더 갭 아님, 이미 도달 가능.**
- **B) 서비스 객체 TLS**(registry/discovery/spot_node `set_tls_*`): **소켓용과 동일한** `zlink_set_tls_server/client`(`socket/api.h:199,203`)에 **서비스 핸들을 그대로 전달**. 코어 구현이 이미 서비스 분기를 가짐 — `zlink_option.cpp:377`이 `zlink_service_set_tls_server`를 먼저 시도하고 `EFAULT`일 때만 소켓 fallback. 즉 **서비스 핸들 수용은 기존 public 심볼의 의도된 계약**. → **헤더 갭 아님, 추가 export 불필요.**

**결론**: 두 비대칭 모두 C 공개 헤더에 **새 선언이 필요 없다**. 관리언어 바인딩은 이미 노출된 `zlink_set_option`/`set_tls_*`의 명명된 sugar일 뿐이다. (역으로 C·Go 등 generic setter만 쓰는 바인딩도 enum으로 동일 기능 도달 가능 — 명명 facade 미제공이 "기능 누락"은 아님.)

---

## 4. Codex 교차검증 결과 (2026-06-14)

8개 바인딩 청구항을 Codex(+직접 grep)로 적대적 검증. **정정 2건**, 나머지 전부 확인:

| 청구 | 판정 | 비고 |
|------|------|------|
| C++ route/dealer-reply/rid-connect/poll/stream_bound_actors/유틸 부재 | **CONFIRMED** | grep 0건 + 헤더 확인 |
| **C++ msg_init_data 제공** | **✅ CONFIRMED AFTER FIX** | 초판 검증 당시 `external_message_t::from`은 복사 경로였으나, 후속 구현으로 `zlink_msg_init_data` 직결 zero-copy 오버로드를 추가 |
| Rust route/init_data/유틸 부재 | **CONFIRMED** | src grep 0건, `ffi.rs:983` 선언만 |
| **Rust connect_router_channel_peer_rid 제공** | **❌ REFUTED** | endpoint+disconnect_rid만(`spot_node.rs:64,86`) → 매트릭스 ✗로 정정 |
| Node route/dealer/rid-connect 부재 | **CONFIRMED** | `dealer_socket.ts:33` request만 |
| Node "순수 JS 재구현" | **NEEDS-NUANCE** | `getProperty` 등 메타데이터 접근 있음 → "순수 재구현" 표현 완화(init_data 갭은 유지) |
| .NET route/rid-connect/atomic_counter 제공, ctx_set_data 비공개 | **CONFIRMED** | `Discovery.cs:76`, `SpotNode.cs:83`, `AtomicCounter.cs:10` |
| Java route 제공 + init_data free-fn 부재 + gets 제공(PORTING #3 stale) | **CONFIRMED** | `Discovery.java:26`, `Message.java:835` |
| Go route/atomic_counter/thread/rid-connect 부재, stopwatch/dealer 제공 | **CONFIRMED** | grep 0건 |
| Python route/subscription_at/spot_option/adopt 부재, bound_actors/dealer-reply(router형) 제공 | **CONFIRMED** | grep 0건 |
| ③ sync 토글·서비스 TLS = 헤더 갭 | **REFUTED(=facade)** | §3 참조, 헤더 추가 불필요 |

> 정정 후 당시에는 **init_data zero-copy + free_fn이 C에만 완전 제공**이었으나, 후속 구현으로 C++도 복사 경로와 zero-copy 경로를 모두 제공한다. **rid-connect는 .NET·Java만** 제공하던 상태에서 이후 누락 바인딩에 추가했다.

---

## 5. 이식 계획 (Migration Plan) — 핵심 갭

> ⚠️ **이 절은 C 계약 기준 초안이며 §8 spec 게이트로 대체되었다.** P3(dealer reply)·P4(connect_rid)는 spec상 갭이 아니어서 **무효**이고, P1(route table)·P2(init_data) 등은 §8의 DECIDE 게이트를 먼저 통과해야 한다. **적용은 §8을 따른다.** 아래는 참고용.

레퍼런스 구현이 있는 갭부터, 위험·작업량 순으로.

### P1 [최우선] Discovery route table → C++·Go·Node·Python·Rust
- **무엇**: `bind_route`/`unbind_route`/`resolve_route` + `route_kind`(`ZLINK_ROUTE_KIND_ACTOR/SPOT_NAME/ACTOR_SESSION`) 노출.
- **레퍼런스**: .NET `Contracts/Service/Discovery.cs:76-88`, Java `Discovery.java:26-30` (시그니처·DTO 그대로 매핑).
- **네이티브**: `zlink_discovery_bind_route/unbind_route/resolve_route`는 C 헤더에 이미 export됨 → 바인딩 FFI 선언 + 래퍼만 추가(코어 변경 불필요).
- **작업량**: 바인딩당 소(중) — FFI 3선언 + `DiscoveryRoute`/`route_kind` 타입 + 메서드 3개. Rust는 `ffi.rs`에 선언 자체가 없어 FFI 추가 선행.
- **검증**: 각 바인딩 contract 테스트에 route bind→resolve→unbind 왕복 추가.

### P2 [성능 계약] msg_init_data (zero-copy 외부 버퍼 + free_fn) → 전 바인딩
- **무엇**: 외부 소유 버퍼를 복사 없이 어댑트하고 해제 콜백으로 소유권 반환.
- **주의**: 관리언어(.NET/Java/Node/Python)는 GC·버퍼 수명 때문에 **free_fn 콜백을 GC 핸들/pinning과 연동**해야 안전 — 단순 포팅 아님. C++/Rust는 free_fn을 native로 직결 가능(우선).
- **레퍼런스**: C `message/api.h zlink_msg_init_data`. C++부터(span을 복사 말고 init_data 직결), Rust(`ffi.rs:983` 이미 선언, 호출 래퍼만).
- **작업량**: C++/Rust 소, 관리언어 중(수명 관리). Java는 `PORTING_ISSUES.md #2`에 이미 등재.
- **검증**: free_fn이 정확히 1회 호출되는지(이중 free/누수 없음) 테스트 — 코어 보안 리뷰 #6과 연계.

### P3 [패턴 일관성] DEALER 서버측 recv/reply → C++(reply)·Node·Python
- **무엇**: `dealer_reply_part`(request_seq 기반, rid 불요) — DEALER가 받은 요청에 응답.
- **레퍼런스**: Rust `Received::reply()`, .NET/Java/Go의 dealer reply.
- **작업량**: 소 — 기존 router reply 경로를 dealer에 노출(네이티브 존재).

### P4 [일관성] connect_router_channel_peer_rid → C++·Go·Node·Python·Rust
- **무엇**: rid 지정 router-channel 연결(endpoint 변종은 이미 전부 있음).
- **레퍼런스**: .NET `SpotNode.cs:83`, Java `SpotNode.java:37`.
- **작업량**: 소 — disconnect_rid 변종이 이미 있어 대칭 추가만.

### P5 [단일 바인딩 보완]
- **C++**: `poll`(one-shot array) + `stream_bound_actors`.
- **Python**: `subscription_at` + SPOT/spot_node 옵션 get/set(튜닝 프로퍼티).
- 작업량 각 소. 해당 바인딩 한정.

### P6 [후순위] core 유틸 → C++·Go·Rust
- `atomic_counter`/`stopwatch`/`thread`. 언어 std로 대체 가능 → **spec 패리티가 목표일 때만** 보강. 기능 손실은 사실상 없음.

### 비-작업 항목 (이식 불필요)
- **Discovery sync 토글 / 서비스 TLS**: §3에서 확인된 대로 generic `set_option`+enum / 서비스 핸들 수용 `set_tls_*`로 **이미 도달 가능**. C·Go에 명명 facade가 없을 뿐 기능은 있음 → 선택적 sugar.
- **msg_move / refcnt / adopt 일부**: RAII·소유권 이전으로 흡수된 관용 항목 → 이식 불요(Node `refCount()`만 네이티브 카운터로 교정 고려).

---

## 6. Fluent Builder 레이어 패리티 (2026-06-14 추가 조사)

C는 평면 `*_part` 프리미티브(빌더 없음)이므로 상위 7개 바인딩(cpp/dotnet/go/java/node/python/rust)의 fluent builder를 교차 비교.

### 6.1 결론 한 줄
**빌더 종류·체이닝 구조(type-state)·설정 옵션은 7개 언어 동등.** 종료 단자에서 Python·Rust만 async/future가 없는데, 이는 **spec(`async-coroutine-policy.ko.md`)가 규정한 의도된 설계(callback-only)** — 격차가 아니다(§6.6, §8 D-PYRUST-ASYNC 종결).

### 6.2 동등한 부분 (7개 언어 모두 일치)
- **빌더 종류**: `Send`/`Request`(+Callback 스테이지)/`Reply`/`ActorJoin`(+EntrySpot/Reply)/`ActorLeave`/`Destroy`/`Lookup`/`Bind`/`Unbind` — 전 언어 동일 집합. `publish`는 어디서나 `Send` 빌더 재사용. **`subscribe`는 어느 언어도 빌더가 아님**(직접 호출).
- **fluency 구조**: 전부 **true fluent + type-state 스테이징**(첫 `message()`가 submit 스테이지로 전이, `flags()`가 callback 전용 스테이지로 narrowing). 종료 동사는 전부 `submit`.
- **설정 옵션**:
  - parts: 다부분은 `.message()` 반복 체이닝(variadic 아님; .NET `.Messages(list)`, Python `.messages(*)` 보조 제공).
  - flags: `SendFlags`{None, DontWait}만 — **More/Final 플래그는 어디에도 없음**(다부분=체이닝).
  - timeout: request + actor-request 계열에만, send/reply엔 없음 — 전 언어 일치.
  - **`routing_id`/target는 빌더 체인이 아니라 진입 메서드 인자**(전 언어 일치). **metadata 세터는 어느 빌더에도 없음**(전 언어 일치).

### 6.3 종료 실행(terminal) 단자 — 언어별 (spec 규정대로)

`request`(응답 대기) 계열 빌더의 종료 단자. 아래 차이는 **`async-coroutine-policy.ko.md:55-62`가 명시한 의도된 언어별 규정**이다(미달 아님):

| 바인딩 | send (fire-forget) | request: sync-blocking | request: **async/future** | request: callback |
|--------|:--:|:--:|:--:|:--:|
| C++ | bool `submit()` | — | ✓ `async_result_t<>` (future) | ✓ |
| .NET | bool `Submit()` | — | ✓ `Task<>`/await | ✓ |
| Go | bool `Submit()` | — | ✓ `SubmitAsync()` (channel) | ✓ |
| Java | bool `submit()` | — | ✓ `CompletionStage<>` | ✓ |
| Node | bool `submit()` | — | ✓ `Promise<>`/await | ✓ |
| **Python** | bool `submit()` | — | **✗ 없음** | ✓ `submit(cb)` |
| **Rust** | bool `submit()` | — | **✗ 없음** | ✓ `submit(cb)` |

- **5개 언어(C++/​.NET/Go/Java/Node)**: request/actor-request 계열에 **sync(fire-forget) + async(future/await) + callback** 3종 단자.
- **Python·Rust**: **sync + callback 2종만**. async/await 단자 부재.
  - Python: asyncio 인프라가 있는데도 빌더에 `await op`/`async_()` 없음(`__await__`/`async def submit` 부재 확인). asyncio는 리소스 `__aenter__`/`__aexit__`에만.
  - Rust: `Future`-반환/`async fn` 단자 없음. 응답 대기 op이 전부 `FnOnce` 콜백.
- **결과**: Python·Rust 사용자는 `async/await`로 요청-응답을 쓸 수 없고 콜백 래핑을 직접 해야 함 → **언어 관용성·동등 레벨 격차**.

### 6.4 부차 차이 — 파트 copy/move 의미
- **C++**: `message(&)` 복사 / `message(&&)` 이동 — 오버로드로 선택 노출.
- **Go**: `.Message()` 복사 / `.MoveMessage()` 이동 / `.Bytes()` 차용 — 명시적 3종(Send 한정).
- **.NET·Java·Node·Python·Rust**: 단일 `message()` = **submit 시 consume/move**(복사 변종 없음).
- → C++·Go만 "복사 유지 vs 소유권 이전"을 호출자가 선택. 나머지는 이동 고정(대용량 재사용 패턴 시 차이).

### 6.5 교차검증 결과 (2026-06-14, 직접 grep)

> Codex 호출이 반복적으로 백그라운드 이탈해 결과 미수신 → **직접 grep으로 독립 교차검증**(동등한 사실 확인). 모든 헤드라인·균일성 청구 **CONFIRMED**.

| 청구 | 검증 | 증거 |
|------|------|------|
| Python ops에 async/await/Future 단자 **없음** | ✅ CONFIRMED | `spot_operations.py`에 `async def submit`/`__await__`/`async_()` **0건**; `submit(self)` + `submit(self, callback)`만 |
| Rust ops에 `async fn`/Future 단자 **없음** | ✅ CONFIRMED | `operation_contracts.rs`에 `async fn`/`Future`/`.await` **0건** |
| 5개 언어 async 단자 **존재** | ✅ CONFIRMED | C++ `async_result_t`(9), .NET `Task Async`(1), Go `SubmitAsync`(4), Java `CompletionStage`(2), Node `Promise submit`(1) |
| `subscribe`는 어느 언어도 빌더 아님 | ✅ CONFIRMED | C++ `topic_message_t subscribe()`, Python `subscribe_into(...)`, Node `subscribe(...): boolean` — 직접 호출 |
| copy/move 선택은 **C++·Go만** | ✅ CONFIRMED | C++ `message(message_t&)`+`message(message_t&&)` 오버로드(`:158-159`), Go `MoveMessage`/`Bytes`(`spot_ops.go:14,87`); .NET copy/clone 0건, Java 단일 `message(Message)` |
| 빌더에 metadata 세터 **없음**(전 언어) | ✅ CONFIRMED | cpp/dotnet/node/python/rust 빌더 계약에 `metadata`/`property`/`header` **0건** |
| SendFlags = None/DontWait만(More/Final 없음) | ✅ CONFIRMED | Rust `socket_options.rs:7,10` NONE/DONT_WAIT만; C++ flags에 more/final 0건 |

### 6.6 결론 — Python·Rust callback-only는 **의도된 설계** (수정 대상 아님)

> 🛑 **초판 정정**: 초판은 Python·Rust의 async 단자 부재를 "레벨 미달 → 추가"로 봤으나, **spec `doc/spec/bindings/async-coroutine-policy.ko.md:60-62`가 명시적으로 callback-only를 규정**한다 — "Python/Rust bindings는 `submit_async()`/`async fn submit_async`를 제공하지 않는다." 따라서 **async 단자 추가는 spec 위반**이다. PB1/PB2(async 추가)는 **폐기**.

- **종결(D-PYRUST-ASYNC)**: Python `submit(callback)`, Rust `submit(callback)`/즉시 submit이 **정본**. coroutine/await 연결은 **framework 레이어**가 콜백을 자기 `task`/coroutine으로 감싸 제공한다(spec "Framework에서 coroutine을 붙이는 방법"). bindings 공개 API에 async 단자를 넣지 않는다.
- spec 언어별 종료 단자 표(`async-coroutine-policy.ko.md:55-62`)는 의도된 차이를 규정: C++ `async_result_t`(co_await 미지원), Java `submit()`+`await()`, .NET `Async()`, Node `Promise`, Go `Submit(ctx[,cb])`, **Python·Rust callback-only**. → §6.3의 "격차"는 **격차가 아니라 설계**.

#### 유일한 확인 항목 (PB → §8 게이트로 이관)
- **copy/move 선택 노출**(C++·Go만 보유): 의도된 차이인지 확인 → §8 D-게이트 대상은 아니나, 요구 발생 시 검토(우선순위 낮음, 기능 손실 없음).

#### 비-작업 (이미 spec 정합)
- 빌더 종류·type-state·flags(None/DontWait)·timeout 위치·routing 진입 인자·metadata 부재·subscribe 비-빌더·**Python/Rust callback-only** — 전부 spec 정합. **추가 작업 불요.**

---

## 7. 방법론·한계

- 각 바인딩의 **공개 표면만** 집계(internal/runtime/native 제외). capability 매핑이라 이름이 달라도 동일 기능이면 제공으로 처리.
- 헤드라인 갭(route table, rid-connect, atomic_counter)은 소스 grep으로 재확인 — test/ffi hit 제외 후 확정.
- 미해결 caveat: Node `ctx_set_data`, Go `Message.adopt` 흡수 범위, **.NET `init_data` zero-copy/free_fn 의미**는 추가 확인 권장(매트릭스에 ~로 표기).
- C 계약 자체도 일부 함수가 비대칭 — C 헤더에 `disconnect_peer_rid`는 있으나 `connect_peer_rid`는 없음(프롬프트 오기재였고 매트릭스에서 제외). 계약 설계 의도 확인 권장.
- **2026-06-14 Codex 교차검증 반영**: C++ `init_data`는 당시 복사 경로로 정정했고, 이후 zero-copy 오버로드를 구현했다. Rust `connect_router_channel_peer_rid`는 부재로 정정한 뒤 누락 바인딩에 추가했다. §4 참조.
- **2026-06-14 Spec 정합성 재분류**: §1~§6은 C 계약 기준이라 보고된 "갭" 다수가 바인딩 spec의 의도적 결정과 충돌. 적용 기준은 §8(spec 게이트).

---

## 8. Spec 정합성 게이트 + 구현 Goal (적용 전 필수)

> 이 절이 **적용의 정본**이다. §1~§7은 C 계약 기준 분석(참고). **바인딩의 target은 `doc/spec/bindings/`**(코드가 spec을 따라가는 방향). 아래 게이트로 재분류한 결과만 코드에 반영한다.

### 8.0 원칙
- 바인딩 공개 계약의 기준은 **`doc/spec/bindings/<lang>/README`** 다. C 계약(`bindings/c/include`)은 substrate이지 바인딩이 1:1로 따라야 할 target이 **아니다**.
- 절차: 보고된 갭마다 spec 대조 → 분류 → **`SPEC-필수·미구현`만 코드 작업**.
- 분류: **`SPEC-의도/금지`**(구현 금지) · **`SPEC-미정의`**(spec 소유자 결정 선행) · **`SPEC-필수·미구현`**(즉시 구현 대상).

### 8.1 전수 재분류 (2026-06-14 spec 대조 + 사용자 확인)

| 보고 항목 | 바인딩 spec 증거 | 분류 | Goal |
|-----------|------------------|------|------|
| **DEALER reply** (G3) | python `README.md:470` "Dealer sockets **must not** expose `reply(request_token,parts)` … cannot reply, no peer routing id" | **SPEC-금지** | NONE(역검증 D-DEALERREPLY) |
| **connect_router_channel_peer_rid** (G4) | 바인딩 spec 누락분 보강 완료. .NET·Java 레퍼런스와 맞춰 C++·Go·Node·Python·Rust에 공개 메서드 추가 | **SPEC-필수·구현완료** | **IMPL-CONNECTRID 완료(§8.4)** |
| **Python·Rust async 단자** (§6) | `async-coroutine-policy.ko.md:60-62` "Python/Rust bindings는 `submit_async()`/`async fn submit_async` **제공하지 않는다**" | **SPEC-의도** | NONE(종결) |
| **zero-copy init_data — C++ 외** (G2) | dotnet `README.md:68` ".NET **must not** expose VM-managed zero-copy send paths" + **사용자 확인**: "C++ 제외하고 zero-copy는 일부러 제외(성능 오버헤드)" | **SPEC-의도 제외** | NONE |
| **zero-copy init_data — C++** (G2) | C++만 zero-copy 의도. 복사 `message_t::from(...)`은 유지하고, `advanced::external_message_t::from(span, free_fn, hint)` zero-copy 버전 추가 완료 | **SPEC-필수·구현완료** | **IMPL-INITDATA-CPP 완료(§8.4)** |
| **Discovery route table** (G1) | 바인딩 spec 누락분 보강 완료. core spec엔 정의(`doc/spec/core/service/discovery.ko.md`). .NET·Java 레퍼런스와 맞춰 C++·Go·Node·Python·Rust에 공개 메서드와 반환 DTO 추가 | **SPEC-필수·구현완료** | **IMPL-ROUTE 완료(§8.4)** |
| **core 유틸**(atomic_counter/thread/stopwatch) (G5) | managed 보유, 네이티브(C++/Go/Rust) 누락분 보강 완료. C++·Go·Rust에 라이브러리 API 패리티용 유틸리티 리소스 추가 | **SPEC-필수·구현완료** | **IMPL-COREUTIL 완료(§8.4)** |
| **subscription_at**(Python·Rust), **poll/stream_bound_actors**(C++) (G6) | 소수 누락분 보강 완료. Python·Rust subscriber subscription 조회, C++ one-shot poll, C++ stream-bound actor snapshot 추가 | **SPEC-필수·구현완료** | **IMPL-SINGLE 완료(§8.4)** |

> **핵심**: **모든 결정 게이트 종료(사용자 확정 2026-06-14).** 누락분 전부 추가 — §8.4에 5개 GOAL-IMPL, 코드 태스크 22개 + spec 보강. **손대지 않는 항목**(spec 금지/의도): dealer reply(0개 보유), Python·Rust async(callback-only), 비-C++ zero-copy(의도 제외). 이 셋만 현행 유지.

### 8.2 Goal 카테고리 (goal 실행기용)
- **GOAL-DECIDE-***: spec 소유자 결정 게이트. **코드 작업 아님.** 산출 = spec README 갱신 또는 "현행 유지" 확정 + 근거 1줄.
- **GOAL-IMPL-***: `SPEC-필수·미구현`. **IMPL-ROUTE·CONNECTRID·COREUTIL·SINGLE·INITDATA-CPP 완료** — 5개 GOAL, 코드 태스크 ~22개 + spec 보강. **모든 결정 게이트 종료**(미결정 0).
- **GOAL-VERIFY-***: spec↔코드 일치 검증. 불일치 시 **코드를 spec에 맞춤**.

### 8.3 결정 게이트 체크리스트 (goal 입력)

- [x] **D-ROUTE** — **결정: 추가**(사용자 확정 2026-06-14, "spec이 누락했을 뿐 추가해야 함"). → C++/Go/Node/Python/Rust IMPL + 7개 spec README 보강. **§8.4 IMPL-ROUTE.**
- [x] **D-CONNECTRID** — **결정: 추가**(동일). → 5개 IMPL(기존 disconnect-rid 대칭) + spec 보강. **§8.4 IMPL-CONNECTRID.**
- [x] **D-INITDATA-CPP** — **결정: 복사·zero-copy 두 버전 다 제공**(사용자 확정 2026-06-14). C++만 해당(비-C++은 의도적 제외). `message_t::from`(복사)은 유지하고, `external_message_t::from(span, free_fn, hint)`를 `zlink_msg_init_data` 직결 zero-copy 경로로 보강했다. **§8.4 IMPL-INITDATA-CPP.**
- [x] **D-DEALERREPLY** — **종결.** 8개 바인딩 DEALER를 직접 확인 → **어느 것도 `reply` 없음**(C++ send/recv, Go Send/Request, Rust send/recv/request, Node request, Python send/request). 초판의 "보유"는 router/`Received.reply` 오인. spec 금지와 정합 → **조치 불요.**
- [x] **D-COREUTIL** — **결정: 추가**(사용자 확정 2026-06-14). atomic_counter/thread → C++·Go·Rust, stopwatch → C++·Rust. (언어 std가 있어도 라이브러리 API 패리티 위해 노출.) **§8.4 IMPL-COREUTIL.**
- [x] **D-SINGLE** — **결정: 추가**(동일). `subscription_at` → Python·Rust, `poll`(one-shot) → C++, `stream_bound_actors` → C++. **§8.4 IMPL-SINGLE.**
- [x] **D-PYRUST-ASYNC** — **종결.** spec(`async-coroutine-policy.ko.md`)이 callback-only를 명시 → async 단자 추가 **금지**. framework 레이어가 콜백을 coroutine으로 래핑(spec "Framework에서 coroutine을 붙이는 방법").

### 8.4 GOAL-IMPL (확정 코드 작업 — D-ROUTE·D-CONNECTRID 결정 완료)

> 공통: 네이티브 C export는 모두 존재 → 바인딩 FFI 선언+래퍼만(코어 변경 불요). **Rust만 `ffi.rs`에 선언 선행.** 각 태스크 DoD = 코드 + `doc/spec/bindings/<lang>/README` 갱신 + 표면 테스트(V-SURFACE) 등록 + 해당 바인딩 테스트 green.

#### GOAL-IMPL-ROUTE — discovery route table (5개 바인딩)
- 네이티브: `zlink_discovery_bind_route` / `zlink_discovery_unbind_route` / `zlink_discovery_resolve_route` (+ `zlink_route_kind_t`)
- 레퍼런스: **.NET `Contracts/Service/Discovery.cs:76-88`**, **Java `.../discovery/Discovery.java:26-30`** (시그니처·`DiscoveryRoute`/`route_kind` DTO 그대로).
- [x] **IMPL-ROUTE-cpp** — `bindings/cpp/include/zlink/Contracts/Service/discovery.hpp` (+ `src/Runtime/Service/discovery.cpp`). `bind_route`/`unbind_route`/`resolve_route` + `route_kind` enum + `discovery_route_t` 반환형.
- [x] **IMPL-ROUTE-go** — `bindings/go/internal/native/discovery_runtime.go` (+ `contracts/*.go` 재노출). cgo로 C 직접 호출.
- [x] **IMPL-ROUTE-node** — `bindings/node/src/zlink/contracts/service/discovery/discovery.ts` (+ `runtime/service/discovery/` + native addon `native/src/*.cc` 바인딩).
- [x] **IMPL-ROUTE-python** — `bindings/python/src/zlink/contracts/service/discovery/discovery.py` (+ `_runtime/service/discovery/discovery.py` + `_native/ffi.py` 심볼 3개 등록).
- [x] **IMPL-ROUTE-rust** — `bindings/rust/src/contracts/service/discovery/discovery.rs` (+ `runtime/service/discovery_runtime.rs`). **선행: `runtime/native/ffi.rs`에 3개 `extern` 선언 추가**.
- [x] **IMPL-ROUTE-spec** — `doc/spec/bindings/{cpp,go,node,python,rust,dotnet,java}/README` Discovery 절에 route table 메서드 명시.

#### GOAL-IMPL-CONNECTRID — connect_router_channel_peer_rid (5개 바인딩)
- 네이티브: `zlink_spot_node_connect_router_channel_peer_rid` (이미 export). **기존 `disconnect_router_channel_peer_rid`가 전 바인딩에 있으므로 그 대칭으로 추가** — 가장 단순.
- 레퍼런스: **.NET `Runtime/Service/SpotNode.Channels.cs:81`**, **Java `.../spot/SpotNode.java:37`**.
- [x] **IMPL-CONNECTRID-cpp** — `Contracts/Service/spot_node.hpp` (+ src). `connect_router_channel_peer_rid(channel_name, peer_rid, endpoint)`.
- [x] **IMPL-CONNECTRID-go** — `bindings/go/internal/native/spot_node.go` (disconnect-rid 메서드 옆에 대칭 추가).
- [x] **IMPL-CONNECTRID-node** — `contracts/service/spot/spot_node.ts` (+ runtime + native addon).
- [x] **IMPL-CONNECTRID-python** — `contracts/service/spot/spot_node.py` (+ `_runtime` + `_native/ffi.py`).
- [x] **IMPL-CONNECTRID-rust** — `contracts/service/spot/spot_node.rs` (+ runtime). **선행: `ffi.rs`에 `extern` 선언 추가**.
- [x] **IMPL-CONNECTRID-spec** — 7개 `README` SpotNode Router Channel Peers 절에 connect-rid 추가.

#### GOAL-IMPL-COREUTIL — core 유틸 (atomic_counter/thread/stopwatch)
- 네이티브: `zlink_atomic_counter_{new,set,inc,dec,value,destroy}`, `zlink_thread_{start,join}`(+`zlink_thread_fn`), `zlink_stopwatch_{start,intermediate,stop}` (전부 `core/api.h` export).
- 레퍼런스: **.NET `AtomicCounter.cs`/`ZlinkThread.cs`/`ZlinkStopwatch.cs`**, Java `AtomicCounter.java`/`ZlinkThread.java`/`ZlinkStopwatch.java`.
- 비고: 네이티브 언어는 std 등가물이 있으나 **라이브러리 API 패리티**를 위해 노출(사용자 결정).
- [x] **IMPL-COREUTIL-cpp** — `bindings/cpp/include/zlink/Contracts/Core/`에 `atomic_counter`·`thread`·`stopwatch` 타입 추가(+ src). 3종 전부.
- [x] **IMPL-COREUTIL-go** — `bindings/go/contracts/core.go` + `internal/native/utility.go`. **atomic_counter·thread** 추가(stopwatch는 이미 `NewStopwatch` 보유).
- [x] **IMPL-COREUTIL-rust** — `bindings/rust/src/contracts/core/`에 atomic_counter·thread·stopwatch 추가. **선행: `ffi.rs`에 atomic_counter·thread 선언 추가**.
- [x] **IMPL-COREUTIL-spec** — `doc/spec/bindings/{cpp,go,rust}/README` Core 절에 추가분 명시.

#### GOAL-IMPL-SINGLE — 단일 누락 (subscription_at / poll / stream_bound_actors)
- 네이티브: `zlink_subscription_at`, `zlink_poll`, `zlink_stream_bound_actors` (전부 export; Rust `ffi.rs:1261`에 subscription_at 이미 선언).
- 레퍼런스: subscription_at → C++ `SubSocket::subscription_at`; poll → .NET `ZlinkPoll.cs`/Go `Poll`; stream_bound_actors → 보유 6개 중 아무거나.
- [x] **IMPL-SINGLE-py-subat** — Python `subscription_at`: `contracts/sockets/pubsub_socket_contracts.py` + `_runtime` + `_native/ffi.py`(심볼 등록).
- [x] **IMPL-SINGLE-rust-subat** — Rust `subscription_at`: `contracts/sockets/pubsub_socket_contracts.rs` 래퍼(ffi 이미 선언).
- [x] **IMPL-SINGLE-cpp-poll** — C++ `poll`(one-shot array): `Contracts/Eventing/`에 자유함수 `poll(pollitem*, n, timeout)`(현재 `poller_t` 객체만).
- [x] **IMPL-SINGLE-cpp-boundactors** — C++ `stream_bound_actors`: `Contracts/Sockets/stream_socket.hpp`에 bound actors 조회 추가.
- [x] **IMPL-SINGLE-spec** — `doc/spec/bindings/{python,rust,cpp}/README` 해당 절 보강.

#### GOAL-IMPL-INITDATA-CPP — C++ zero-copy 버전 (복사와 양립)
- 네이티브: `zlink_msg_init_data(data, size, free_fn, hint)` (free_fn = 외부 버퍼 소유권 반환 콜백).
- 현 상태: `message_t::from(span)`은 복사를 유지하고, `external_message_t::from(span, free_fn, hint)` zero-copy 버전은 구현 완료.
- [x] **IMPL-INITDATA-CPP** — `bindings/cpp/include/zlink/Contracts/Messaging/message.hpp` + `src/Runtime/Messaging/message.cpp`:
  - `message_t::from(span)` = **복사 유지**(안전 기본).
  - `external_message_t::from(span, free_fn, hint)` = **진짜 zero-copy**로 `zlink_msg_init_data` 직결. 라이브러리가 버퍼를 해제할 때 `free_fn(data, hint)`를 1회 호출.
  - DoD: free_fn 정확히 1회 호출(이중 free/누수 없음) 단위 테스트 + 복사/zero-copy 둘 다 회귀. 완료.
  - **비-C++은 작업 없음** — zero-copy 의도적 제외(복사 `from()` 유지).

### 8.5 GOAL-VERIFY (결정 불요, 즉시 가능)
- [x] **V-SURFACE** — 각 바인딩에 **공개 표면 테스트**(Go `surface_test.go` 모델)를 두어 spec README의 공개 메서드 집합과 코드를 1:1 대조. C++/Go/Node/Python/Rust는 신규 표면 테스트와 해당 바인딩 테스트로 확인했고, .NET/Java 레퍼런스 표면 테스트도 현재 green으로 확인했다.
- [x] **V-BUILDER-TERMINAL** — §6 fluent builder 종료 단자가 `async-coroutine-policy.ko.md:55-62` 언어별 표와 일치(Python/Rust callback-only 포함). **확인 완료(2026-06-14).**

### 8.6 IMPL 태스크 템플릿 (DECIDE "포함" 통과 후 사용)
각 IMPL 태스크 필드:
- **파일**: `bindings/<lang>/<공개 계약 경로>` (+ 필요 시 `runtime`/`native`/`ffi`)
- **네이티브**: C export 존재 여부 — 존재 시 FFI 선언+래퍼만(코어 변경 불요); Rust는 `ffi.rs` 선언 선행
- **레퍼런스**: 이미 구현한 바인딩(.NET/Java 등)의 시그니처
- **spec**: `doc/spec/bindings/<lang>/README` 갱신(코드와 동시)
- **검증**: `bindings/<lang>` 테스트 + 공개 표면 테스트(V-SURFACE) + 코어 헤더 변경 시만 `bindings/dev_sync_local_core_libs.sh`
- **DoD**: spec 갱신 + 테스트 green + 표면 테스트에 신규 메서드 등록 + 매트릭스(§1) 셀 ✓ 갱신

### 8.7 적용 절차 (goal 실행기)
1. **§8.3 DECIDE 게이트** — 전부 종료(2026-06-14). 결정 반영은 각 IMPL의 spec 보강 단계에서.
2. **§8.4 IMPL** 실행 — 권장 순서: IMPL-CONNECTRID(disconnect-rid 대칭, 최소) → IMPL-ROUTE → IMPL-COREUTIL → IMPL-SINGLE → IMPL-INITDATA-CPP(가장 신중).
3. 각 IMPL 직후 **§8.8 REVIEW**(Codex)로 완료 검증 — 통과해야 다음 진행.
4. 전 IMPL 후 **§8.5 V-SURFACE** + **§8.8 REVIEW-PARITY**로 매트릭스(§1.1) 전 셀 ✓ 확정.

### 8.8 구현 후 Codex 검증 계획 (REVIEW 게이트)

> 각 IMPL 태스크/그룹이 끝나면 Codex로 **적대적 완료 검증**을 돌린다. "구현했다"가 아니라 "공개 계약에 올바르게, 레퍼런스와 동일 레벨로, 회귀/spec위반 없이 들어갔는가"를 판정.
> **Codex 호출 주의**: 본 작업 중 Codex 호출이 반복적으로 백그라운드로 이탈했음 → **동기 실행(`--wait`) 지시**, 결과 미수신 시 **직접 grep 폴백**으로 동일 검증(§4·§6.5 선례).

#### REVIEW 체크 항목 (IMPL 태스크 1건당)
Codex가 대상 바인딩 코드 + 레퍼런스(.NET/Java) + spec README를 열어 각 항목 [CONFIRMED | INCOMPLETE | REGRESSION] 판정:
1. **공개 표면 노출** — 신규 메서드가 `contracts/`(공개 계약)에 있는가? `runtime`/`internal`/`ffi`에만 있으면 INCOMPLETE.
2. **레퍼런스 시그니처 일치** — .NET/Java 대비 인자·반환·DTO(`DiscoveryRoute`/`route_kind` 등)·에러 매핑 동일 레벨인가?
3. **네이티브 바인딩 정확성** — FFI 선언이 C export 시그니처(`core/include`)와 정확히 일치(인자 타입·개수·반환·`free_fn` 시그니처)? 잘못된 marshalling은 REGRESSION.
4. **Rust FFI 선행** — Rust 대상이면 `ffi.rs`에 `extern` 선언이 실제 추가됐는가(route·connect-rid·atomic_counter·thread).
5. **spec README 갱신** — `doc/spec/bindings/<lang>/README` 해당 절에 신규 메서드가 명시됐는가(코드만 들어가고 spec 누락 시 INCOMPLETE).
6. **테스트 커버리지** — 해당 바인딩 테스트 + 표면 테스트(V-SURFACE)에 신규 메서드 왕복/존재 검사가 있는가? green인가?
7. **회귀 없음** — 기존 공개 API 시그니처 변경/제거 없음. (특히 INITDATA: 복사 `message_t::from` 유지 확인.)

#### REVIEW 그룹 (goal 체크리스트)
- [x] **REVIEW-CONNECTRID** — 5개 바인딩 + spec. 추가 포인트: 기존 `disconnect_router_channel_peer_rid`와 시그니처 대칭인지.
- [x] **REVIEW-ROUTE** — 5개 바인딩 + spec. 추가 포인트: `route_kind` enum 값(ACTOR/SPOT_NAME/ACTOR_SESSION)과 `resolve_route` 반환 DTO가 .NET/Java와 동형인지; Rust `ffi.rs` 3선언.
- [x] **REVIEW-COREUTIL** — atomic_counter(6 op)/thread(start·join)/stopwatch(start·inter·stop)가 네이티브 언어에 노출됐는지; Go는 atomic_counter·thread만(stopwatch 기존).
- [x] **REVIEW-SINGLE** — subscription_at(Python·Rust 공개 노출, Rust는 ffi 기존 선언 사용), poll(C++ 자유함수), stream_bound_actors(C++).
- [x] **REVIEW-INITDATA-CPP** — **가장 엄격**: `external_message_t::from`이 진짜 zero-copy(`zlink_msg_init_data` 직결)인가? **free_fn이 정확히 1회 호출**되어 이중 free/누수가 없는가(테스트로)? `message_t::from`(복사)은 그대로인가? — 확인 완료.
- [x] **REVIEW-PARITY (최종)** — 전 IMPL 후 **매트릭스 §1.1 ② 셀이 전부 ✓**로 닫혔는지, 손대지 않기로 한 3항목(dealer reply / Python·Rust async / 비-C++ zero-copy)이 **변하지 않았는지** 교차 재검증. C++/Go/Node/Python/Rust 표면 테스트와 직접 grep으로 확인.

#### REVIEW 산출물
- IMPL 태스크별 판정표(CONFIRMED/INCOMPLETE/REGRESSION) + INCOMPLETE/REGRESSION은 후속 수정 태스크로 환원.
- 최종 REVIEW-PARITY 통과 시 본 리포트 §1.1 매트릭스를 "전 항목 ✓" 상태로 갱신하고 종료.
