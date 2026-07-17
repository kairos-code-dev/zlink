# S5 Core 구현 독립 리뷰 R1 — Codex

## 결론

**CORE REVIEW NOT CLEAN**

- finding: **9건** (`blocker 0`, `high 4`, `medium 5`, `low 0`)
- I1 계약 구현 일치: **NOT CLEAN**
- I2 POSD·DDD: **NOT CLEAN**
- I3 정리 완결성: **CLEAN**

다른 독립 리뷰어의 결과 파일이 실행 중 생성되었지만, 독립성을 유지하기 위해 해당 파일은 읽지 않았다.

## 실행 증거

| 항목 | 결과 |
|---|---|
| 모델 식별자 | Codex (GPT-5) |
| session id | `019f6fac-97bb-7762-a29c-71024b1c131e` |
| companion session id | `8915b088-9549-43cd-a49f-97c1f066b53c` |
| 시작 시각 | `2026-07-17T19:43:55+09:00` |
| 종료 snapshot 시각 | `2026-07-17T19:56:05+09:00` |
| 시작 HEAD | `8206fd44dcd4cdd32e0364d0095631edb7b5118e` — manifest와 일치 |
| 종료 HEAD | `8206fd44dcd4cdd32e0364d0095631edb7b5118e` — 시작값과 일치 |
| 시작 scope hash | 625 files, `281c02e49e016ce567cef652628f512d97495ca4af3b7eacc035361bf964e683` — manifest와 일치 |
| 종료 scope hash | 625 files, `281c02e49e016ce567cef652628f512d97495ca4af3b7eacc035361bf964e683` — manifest·시작값과 일치 |
| tracked working tree | 시작·종료 모두 clean. 시작부터 untracked manifest가 있었고 종료에는 manifest, 이 리뷰의 허용 결과 2개와 다른 리뷰어의 허용 log 2개가 있었다. |
| build | compile/link 성공. post-build bindings 동기화 부수 효과는 HEAD로 복원했고 종료 clean/hash로 확인했다. |
| ctest | `32/84` pass, `52` fail. sandbox가 TCP bind를 막아 mesh start가 result `705`로 실패했으므로 동적 판정 증거로 사용할 수 없음. |
| ASAN | 4개 모두 mesh start `705`로 본문 미실행. LSAN은 ptrace 환경 fatal도 보고. |
| TSAN | 4개 모두 mesh start `705`로 본문 미실행. race clean 증거로 계산하지 않음. |
| public surface | `PUBLIC SURFACE CONTRACT: PASS`, 196 exports 일치, 제거 identifier 없음 |
| C ABI smoke | 재실행하지 않음. manifest의 기존 PASS를 독립 성공 증거로 계산하지 않음. |

manifest §2 명령을 시작과 종료에 그대로 사용했다. 동적 검증 제약과 무관하게 scope hash와 tracked snapshot은 변하지 않았다.

## Finding

| ID | 축 | 심각도 | file:line 근거 | 위반 계약 | 수정 방향 |
|---|---|---|---|---|---|
| F-I1-01 | I1 | high | `core/src/runtime/services/mesh/mesh_wire.cpp:1911-1929`, `core/src/api/mesh/mesh_messaging_api.cpp:843-853` | `core/doc/spec/core/service/01-mesh-node.ko.md:380-387` — NODROP snapshot all-or-none | remote target을 순차 전송하지 말고 전체 pipe의 실제 capacity를 먼저 reserve한 뒤 일괄 commit한다. 2개 이상 remote target 중 하나만 backpressure인 test에서 zero delivery를 검증한다. |
| F-I1-02 | I1 | high | `core/src/api/mesh/mesh_api.cpp:222-239`, `core/src/api/mesh/mesh_node_api.cpp:85-101`, `core/src/api/mesh/mesh_node_api.cpp:969-992`, `core/src/api/mesh/mesh_node_api.cpp:398-410` | `core/doc/spec/core/service/03-spot.ko.md:53-54,86-89,262-274` — logical lifetime, timer generation, handler 상호배제 | Spot-aware timer release/generation gate와 application-turn serialization을 lifecycle coordinator에 둔다. 마지막 참조 해제 시 Spot/owner를 제거하고 재생성 generation을 증가시킨다. |
| F-I1-03 | I1 | high | `core/src/api/mesh/mesh_actor_api.cpp:609-668` | `core/doc/spec/core/service/04-actor.ko.md:181-182` — active claim·completion·bound session control을 deadline까지 drain | Actor storage를 즉시 지우지 말고 새 admission을 닫은 뒤 세 조건을 deadline까지 drain하는 coordinator를 둔다. 각 held-work timeout test를 추가한다. |
| F-I1-04 | I1 | high | `core/src/api/mesh/mesh_node_api.cpp:308-383`, `core/src/api/mesh/mesh_node_api.cpp:64-78`, `core/src/api/mesh/mesh_messaging_api.cpp:925-936` | `core/doc/spec/core/service/01-mesh-node.ko.md:166-174,306-308` — shutdown terminal completion exactly once | shutdown deadline에서 outstanding operation을 detach하여 exactly-once `ESHUTDOWN` completion으로 만들고 reply route를 정리한다. timeout 0 미응답 request test를 추가한다. |
| F-I1-05 | I1 | medium | `core/src/runtime/services/mesh/mesh_wire.cpp:498-577`, `core/src/api/mesh/mesh_node_api.cpp:475-484` | `core/doc/spec/core/service/01-mesh-node.ko.md:229-251` — 같은 endpoint의 manual/discovery intent는 MIXED로 병합 | inbound connection의 관측 remote endpoint를 peer identity에 기록하고 source ownership을 한 intent에 병합한다. inbound-first 실제 transport test를 추가한다. |
| F-I1-06 | I1 | medium | `core/src/runtime/services/mesh/mesh_wire.cpp:557-585`, `core/src/api/mesh/mesh_node_api.cpp:730-742` | `core/doc/spec/core/service/01-mesh-node.ko.md:246-254,100-120` — old generation drain 및 observable draining count | old generation을 DRAINING entry로 유지하여 새 snapshot에서 제외하고 committed work 종료 뒤 close한다. 새 generation은 별도 lifetime으로 활성화한다. |
| F-I1-07 | I1 | medium | `core/src/api/mesh/mesh_node_api.cpp:759-805`, `core/src/api/mesh/mesh_stream_session_api.cpp:763-803` | `core/doc/spec/core/service/README.ko.md:23-37` — 배열 element 선검증, invalid 시 partial output 금지 | 필요한 모든 element를 첫 pass에서 검증하고 두 번째 pass에서만 출력한다. invalid element 위치별 output 불변 test를 추가한다. |
| F-I1-08 | I1 | medium | `core/src/api/mesh/mesh_node_api.cpp:22-35,136-166,442-467`, `core/src/runtime/services/mesh/mesh_runtime.cpp:36-60`, `core/src/api/mesh/mesh_messaging_api.cpp:939-965` | `core/doc/spec/core/service/01-mesh-node.ko.md:138-140,389-390` — 공개 문자열은 UTF-8 byte sequence | strict UTF-8 scalar validator 하나를 공개 문자열 경로에 재사용한다. overlong, surrogate, U+10FFFF 초과 test를 추가한다. |
| F-I2-01 | I2 | medium | `core/src/runtime/services/mesh/mesh_wire.cpp:1-2255`, `core/src/runtime/services/mesh/mesh_wire.hpp:15-220`, `core/doc/internals/services-internals.ko.md:12-29,107-130` | `doc/principal/software-design-principles.md:59-65,960-969,978-986` — 깊은 모듈, domain boundary, 복잡성 하향 | 단순 ingress/egress 파일 분할보다 codec, peer admission state machine, service ingress router, transport를 각 결정별 깊은 모듈로 분리하는 대안을 선택한다. public API는 유지한다. |

### F-I1-01 상세

`wire_publish_remote_locked()`은 target을 순서대로 전송하고 성공 수를 즉시 증가시킨다. 중간 target이 실패하면 앞 target의 전달을 되돌리지 않은 채 `BACKPRESSURED`를 반환한다. 따라서 호출자는 실패를 관측하지만 일부 target은 메시지를 받는다. 이는 원자적 commit이 아니라 send 호출 직렬화일 뿐이다.

### F-I1-02 상세

`zlink_spot_timer_new()`은 generic timer를 만든 뒤 `timer_count`만 증가시킨다. scoped search에서 감소 경로와 `spots.erase`가 없었다. 이 때문에 generic timer를 정상 destroy해도 MeshNode destroy가 `live_timers`로 `EBUSY`가 될 수 있으며, logical Spot 종료·같은 RID 재생성·이전 generation tick 차단·application claim과 timer handler 상호배제도 연결되지 않는다.

### F-I1-03 상세

local destroy는 `draining = true`를 설정한 직후 owner와 actor를 삭제하고 operation을 `OK`로 완료한다. `timeout_ms`는 실제 drain 대기에 쓰이지 않는다. held claim의 storage와 bound session control을 보존해야 하는 계약을 충족하지 못한다.

### F-I1-04 상세

`timeout_ms == 0` operation은 timeout task가 없다. shutdown deadline에서 operations가 남으면 claim만 revoke하고 `TIMED_OUT`을 반환하며 node는 `DRAINING`에 남는다. 같은 shutdown을 다시 호출하면 `EDEADLK`다. 따라서 destroy 전까지 operation이 무기한 남을 수 있다.

## Known risk 판정

| manifest §7 항목 | 판정 | 근거 |
|---|---|---|
| TSAN: `part_helper_state` check-then-set | **정적 반박 / finding 없음** | `core/src/runtime/sockets/common/socket_base_request_reply_bridge.cpp:41-64`는 shared state에 atomic load/CAS/store를 사용한다. |
| TSAN: auto-HWM lock-order inversion | **동적 판정 불완전 / 확정 finding 없음** | `core/src/runtime/core/ctx_auto_hwm_recalc.cpp:80-116`의 `_slot_sync` → socket monitor sync 순서는 확인했으나 반대 순서의 확정 경로는 찾지 못했다. TSAN은 sandbox bind 제한으로 본문 미실행이다. |
| TSAN: mailbox ypipe | **동적 판정 불완전 / finding 없음** | mailbox ypipe 접근의 mutex 보호를 정적으로 확인했다. TSAN 본문은 실행되지 않아 기존 경고의 동적 소거 증거는 없다. |
| `ZLINK_MESH_PEER_MIXED` 실도달 | **결함 확인** | F-I1-05 |
| peer `DRAINING` 전이 | **결함 확인** | F-I1-06 |
| shutdown 뒤 timeout 없는 operation | **결함 확인** | F-I1-04 |

## 축별 판정

### I1 계약 구현 일치 — NOT CLEAN

- high 4건, medium 4건.
- 관찰 가능한 atomicity, lifetime, deadline 종료, peer state/source, versioned output, UTF-8 계약 불일치가 남아 있다.

### I2 POSD·DDD — NOT CLEAN

- medium 1건.
- `mesh_wire`가 protocol codec, transport, admission state machine과 여러 domain service dispatch를 동시에 소유한다. 이 결합은 I1의 NODROP와 peer drain 오류에서도 실제 변경 이유의 분산으로 드러난다.
- 대안 A인 ingress/egress 기준 파일 분리는 domain 지식을 반복시킨다. 대안 B인 결정별 깊은 모듈 분리를 선택해야 한다.

### I3 정리 완결성 — CLEAN

finding 없음.

- 공개 surface contract: PASS, 196 exports 일치, 제거 identifier 없음.
- manifest §5 제거 pattern scoped word-boundary scan: no hit.
- 대상 source/header/test/package 0-byte file scan: no hit.

동적 sanitizer 결과는 환경 제약으로 clean 증거가 아니지만, 실제 dead code·file·target·alias·adapter·호환 잔재는 정적 범위에서 발견하지 못했다.
