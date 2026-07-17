# S5 Core 구현 리뷰 — R2 (Claude Fable), iteration 1

리뷰어: Claude Fable (general-purpose agent). 수정 권한 없음(read-only). 다른
리뷰어(Codex) 결과는 열람하지 않음.

## 1. 실행 증거

| 항목 | 값 |
|---|---|
| Reviewer | Claude Fable (general-purpose agent) |
| Acceptance commit | `8206fd44dcd4cdd32e0364d0095631edb7b5118e` |
| 시작 시각 | 2026-07-17 19:43:07 +0900 |
| 종료 시각 | 2026-07-17 19:49:32 +0900 |
| Scope hash (시작) | `281c02e49e016ce567cef652628f512d97495ca4af3b7eacc035361bf964e683` (625 files) |
| Scope hash (종료) | `281c02e49e016ce567cef652628f512d97495ca4af3b7eacc035361bf964e683` — **일치** |

| 실행 명령 | 결과 |
|---|---|
| scope hash 재계산 (manifest §2) | `281c02e4…` / 625 files — manifest 값과 일치 |
| `cmake --build core/build -j && ctest --test-dir core/build -j` | **100% tests passed, 0 failed out of 84** (124.2s) — 기존 결과와 일치 |
| removed-identifier no-hit 스캔 (manifest §5, scoped) | `core/src`·`core/include`·`core/tests` 코드 내 hit 0. hit는 전부 scope 밖(`core/tools/`, `core/study/`, `core/tools/perf/` 역사 로그)과 정당한 absence test(`assert_text_absent`) 1건 |
| `contract_public_surface` (ctest `unittest_public_contract_headers`) | 빌드·통과 (196 export 계약 gate) |
| 공개 header ↔ spec typedef/함수 시그니처 대조 | mesh_node/dispatch/actor/spot/stream_session/monitoring 전부 spec과 일치 |
| 종료 후 working tree | `core/` scope 파일 무변경(hash 동일). `bindings/*` 변경은 build의 `sync-local-core-libs.sh` 부수효과로 review scope 밖 |

주: TSAN/ASAN 재실행은 이 pass에서 수행하지 않았다(시간 예산). ctest 84/84와
scope hash로 baseline 동일성만 확인했다.

## 2. Finding

표현·취향 항목은 §6 editorial note로 분리했다.

| ID | 심각도 | Finding | 근거 (file:line) | 위반 spec | 수정 방향 |
|---|---|---|---|---|---|
| F1 | **High** | claim serial→owner 역해석 표 `g_claim_keys`가 **process 전역이며 serial 하나로만 키잉**된다. 그러나 `next_claim_serial`은 node별 멤버로 1부터 증가한다. 같은 process에 MeshNode가 둘 이상이면(spec §1이 명시적으로 허용) 두 node의 첫 claim이 모두 serial 1이 되어 전역 표에서 충돌한다. `recall_claim_key`/`forget_claim_key`는 `body.node`를 무시하고 serial만 쓴다. 결과: (a) 두 번째 `remember_claim_key`가 첫 node의 owner 매핑을 덮어씀 → 첫 node의 release가 잘못된 owner를 recall해 `owners.find` miss → 해당 mailbox가 `claimed=true`로 영구 고착(dispatch 정지); (b) `forget_claim_key`가 상대 node의 키까지 지워 다른 node release가 `ESTALE`로 실패. node-owner 케이스에서도 두 번째로 release하는 node가 반드시 `ZLINK_CLOSE_INVALID_HANDLE`/`ESTALE`로 실패하고 자기 mailbox를 고착시킨다. | `core/src/api/mesh/mesh_dispatch_api.cpp:77-102`(전역 표), `:108`·`:130`·`:341`·`:536`(serial-only recall/forget/remember); `core/src/runtime/services/mesh/mesh_runtime.hpp:510`·`mesh_runtime.cpp:280`(per-node serial) | dispatch spec §3(“claim은 원래 MeshNode가 destroy된 뒤에도 release”)·§6, mesh spec §1(다중 MeshNode/process). internals doc `services-internals.ko.md:76`은 claim 신원을 “(node generation, owner, domain, serial)”로 기술하나 표는 node 판별을 버린다 | `g_claim_keys`를 `(node 포인터 또는 node_generation, serial)` 복합 키로 바꾸거나 node별 side table로 분리, 또는 serial을 process-global 원자 카운터로 승격 |
| F2 | Medium | MeshNode monitor의 **callback 안 handler 해제 시 `ZLINK_HANDLER_DEADLOCK` 미반환**. `monitor_state_t::handler_active`는 `false`로 초기화된 뒤 core/src 어디에서도 `true`로 설정되지 않는다(`emit_monitor_event`가 handler 호출 전후로 세우지 않음). 따라서 `zlink_mesh_node_monitor_handler(..., NULL, ...)`·`monitor_close`의 `handler_active` 가드 branch는 도달 불가능하고, handler callback 내부에서 handler를 NULL로 해제하면 `EDEADLK` 대신 `ZLINK_HANDLER_OK`를 반환한다. | `core/src/api/mesh/mesh_monitor_api.cpp:60`·`:151`(dead guard); `core/src/runtime/services/mesh/mesh_runtime.cpp:646-695`(emit가 handler_active 미설정) | monitoring spec §3(“같은 callback 안의 해제는 `ZLINK_HANDLER_DEADLOCK`”) | handler 호출 구간에서 `handler_active`(또는 호출 thread id)를 설정/해제해 가드를 실제 동작시킴 |
| F3 | Low | 죽은 무의미 조건식: channel record kind 선택에서 `operation_id_out_ ? (source_spot_rid_ ? CHANNEL_REQUEST : CHANNEL_REQUEST) : CHANNEL_SEND` — 내부 삼항이 양쪽 모두 `CHANNEL_REQUEST`라 항상 같은 값. 동작은 정상이나 refactor 잔재. | `core/src/api/mesh/mesh_messaging_api.cpp:406-409` | (해당 spec 위반 아님; 정리 완결성) | 내부 삼항 제거 |

## 3. 축별 판정

### I1 — 계약 구현 일치
- F1 (High), F2 (Medium).
- 그 밖의 대조: node/channel/spot/actor/stream_session submit·request·reply,
  errno mapping(`submit_errno_result`), metadata frame 검증, ready/receive
  batch capacity·BUFFER_TOO_SMALL·residue, reply token 소비·EALREADY·ESTALE,
  timeout 후 도착 reply 폐기, channel round-robin(local READY 포함)·zero-weight
  제외, shutdown TIMED_OUT/claim revoke 경로는 모두 spec과 일치.
- 판정: **NOT CLEAN**

### I2 — POSD·DDD
- 없음. `mesh_node_t`는 lifecycle·budget·ordering·generation 규칙을 은닉한
  깊은 모듈이고 C 표면(`api/mesh/`)은 인자 검증 후 위임하는 얇은 계층으로
  경계가 일관된다. header 주석이 “왜”를 설명한다. 패스스루/책임 혼합/의미 있는
  미완 리팩터 잔여 없음.
- 판정: **CLEAN**

### I3 — 정리 완결성
- F3(무의미 삼항). 추가로 F2가 드러낸 `handler_active` dead guard, 그리고
  peer `DRAINING` 관련 vestigial(§4 risk 3 참조: enum 값·`draining_peer_count`
  집계·`disconnect_peer`의 DRAINING branch가 어떤 전이도 설정하지 않아 사실상
  죽은 경로).
- removed-identifier no-hit 스캔은 scope 내 clean. orphan build target·alias·
  호환 잔재는 발견되지 않음.
- 판정: **NOT CLEAN** (모두 low, 기능 영향 없음)

## 4. Known risk 판정 (manifest §7)

1. **TSAN 기존 기계 3계열** — 이 pass에서 TSAN 재실행하지 않음. 신규 mesh
   코드 자체에서는 새 race를 발견하지 못했고 ctest 84/84 green. 3계열은
   9.x 기계의 기존 항목으로 mesh 신규 계약과 무관 — **수용(추적 유지)**.
2. **`ZLINK_MESH_PEER_MIXED` 실도달** — `connect_peer`의 merge는 기존
   peer `source == DISCOVERY` 이면서 `endpoint` 문자열이 정확히 일치할 때만
   MIXED로 승격(`mesh_node_api.cpp:476-484`). in-process discovery adapter가
   없고 inbound peer의 endpoint가 수동 connect endpoint 문자열과 일치하지
   않으므로 현재 코드에서 MIXED는 **사실상 도달 불가(latent)**. 기능 결함
   아님 — **수용(low, 문서화된 한계)**.
3. **peer `DRAINING` 미사용** — 확인: `peer.state = ZLINK_MESH_PEER_DRAINING`
   대입이 코드 전체에 없다. generation 교체는 즉시 치환 + `PEER_DRAINING`
   event만 방출(`mesh_wire.cpp:585`). 따라서 `draining_peer_count`는 항상 0,
   `disconnect_peer`의 DRAINING 분기(`mesh_node_api.cpp:570`)와 status 집계
   (`:737`)는 죽은 경로. spec은 관측 가능한 DRAINING 체류를 요구하지 않으므로
   계약 위반 아님 — **수용**. 단 vestigial 잔재로 I3에 기록.
4. **shutdown deadline 이후 무기한 operation 종료** — deadline 만료 시
   `shutdown`은 outstanding claim을 `revoked=true`로 표시하고 `TIMED_OUT`
   반환(`mesh_node_api.cpp:351-376`); consumer는 revoked claim의 recv에서
   `ESHUTDOWN`으로 해소된다. `destroy`는 `operations.clear()`로 미완 operation을
   버리되(`:429`) node storage를 같은 호출에서 해제하므로 per-operation
   `ESHUTDOWN` completion을 물리적으로 만들지 않아도 관측상 동등(살아남는
   consumer 없음). spec §3 destroy 문구는 문자적으로는 completion 생성을
   말하나 관측 가능한 결과는 claim revoke로 충족 — **수용(low/editorial)**.

## 5. 결론

- Finding: 3건 (High 1, Medium 1, Low 1).
- 축 판정: I1 = NOT CLEAN, I2 = CLEAN, I3 = NOT CLEAN.
- Known risk 4건: 전부 수용(2·3·4는 low 잔재/한계, 1은 추적 유지).
- High finding(F1)이 존재하므로 clean 기준 미충족.

## 6. Editorial note (finding 아님)

- `mesh_messaging_api.cpp:133-136` — `CREATED` 상태 submit이 `EINVAL`→
  `INVALID_ARGUMENT`로 분류된다. spec에 명시적 정의는 없으나 `INVALID_STATE`가
  의미상 더 적합할 수 있음(계약 위반 아님).
- `mesh_runtime.cpp:16-34` handle registry의 “intentionally leaked” 주석과
  `now_ms`의 thread_local clock 주석은 근거를 잘 설명함 — 유지 권장.

NOT CLEAN
