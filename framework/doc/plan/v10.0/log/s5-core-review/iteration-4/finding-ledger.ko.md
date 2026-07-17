# S5 Core 리뷰 finding ledger — iteration 4 병합

Snapshot: `59b3ea940` (631 files, scope hash `d9621658…`). 리뷰 계약(수정본):
리뷰어는 이슈·근거·영향·수정 범위·검증 방향만 제시하고, 해결 설계 선택과
구현은 coordinator 책임이다.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **CORE REVIEW NOT CLEAN** | I1 NOT CLEAN(high 3) · I2 CLEAN · I3 NOT CLEAN(low 1) |
| R2 Claude Fable | **CORE REVIEW CLEAN** | I1/I2/I3 모두 CLEAN, 신규 finding 없음(editorial 4) |

기본 4회 예산의 마지막 병합 시점에 미해결 high가 남았으므로, 수정 정책에 따라
4회 제한을 해제하고 미해결 medium+ 0건까지 수정·재리뷰를 계속한다(iteration 5).

## 2. iteration-3 수정 4건 해소 판정 병합

| ID | Codex | Fable | 병합 판정 |
|---|---|---|---|
| F-I1-03(재재) | 미해소(high) | 해소 | **미해소 유지** — Codex의 idempotent 경로 반례가 유효(아래 §3.1) |
| N3-I1-01 | 미해소(high) | 해소 | **미해소 유지** — deque/set post-commit 할당은 사실(아래 §3.2) |
| N5 | 해소 | 해소 | 해소 확정 |
| N6/N3-I3-01 | 해소 | 해소 | 해소 확정 |

## 3. Coordinator 판정과 해결 설계

### 3.1 F-I1-03(재재) — 인정, 수정

Codex 반례: 삽입 호출만 사후 재검증하므로, 동시 두 번째 호출이 첫 호출의
임시 binding을 idempotent 성공으로 관측하면 재검증을 건너뛴다. destroy 제거
pass 이후 A 삽입 → B idempotent 성공 → A stale 롤백이면 B는 성공을 보고했지만
binding이 없다.

해결 설계(coordinator): **성공 두 형태(insert·idempotent) 모두 사후
재검증**한다. staleness는 단조(파괴된 generation은 되살아나지 않음)이므로
어느 순서로 재검증해도 두 호출 모두 ESTALE로 수렴하고 binding이 남지 않는다.
`mesh_stream_session_api.cpp` bind 경로에서 `if (!idempotent)` 게이트 제거.

검증: `test_stream_session_bind_destroy_race_leaves_no_binding`
(test_mesh_lifecycle_contracts) — 2-thread bind × destroy 8라운드 hammer,
라운드마다 (a) destroy 반환 후 bind는 반드시 실패, (b) 파괴된 generation의
binding 잔존 0 을 단정. 결정적 interleaving 주입 seam은 없어 hammer +
단조성 논증으로 갈음(리뷰어 재판정 대상).

### 3.2 N3-I1-01 — 인정, 수정

Codex 지적대로 remote commit 뒤 `deque::push_back`/`set::insert`는 할당
가능하다. publish_common은 local capacity 판정부터 local commit까지 node
mutex를 연속 보유하므로, 해결 설계는 **local container 슬롯 선예약**:

- record 선구축 후, remote commit **이전에** mailbox deque에 placeholder
  슬롯 push + ready-index 키 선삽입(신규 삽입분 추적). 여기가 마지막 fallible
  지점이며 실패 시 전량 롤백 후 ENOMEM (아무것도 commit되지 않은 시점).
- placeholder·선삽입 키는 mutex 보유 중에만 존재 — remote 실패 경로(재시도
  cv wait, timeout 반환)는 unlock 전에 롤백.
- remote commit 뒤에는 슬롯 대입(move)·counter 증가만 남음 — 무실패.

검증: 기존 multicast 전 suite + 신규 unreachable 테스트가 이 경로를 통과.

### 3.3 F-I1-01 — 부분 인정: 회계·주석·spec 명료화로 해소

두 리뷰어의 해석이 갈렸다(Codex: §7/§9의 snapshot 전체 all-or-none 위반;
Fable: §9 "reserve/commit**까지** 보장"과 §5 commit 취소 불가로 구현이 유일
정합 해석). 해석 분열 자체가 spec 문구의 내부 긴장(§5 vs §7)의 증거다.

coordinator 판정: reserve와 commit 사이 pipe 소실은 분산 환경에서 회수 불가
사건이며(§5 "이미 commit한 message를 취소하지 않는다"와 동일 계열), commit을
N개 독립 peer에 대해 회수 없이 원자화하는 것은 불가능하다. 따라서 계약이
제공 가능한 보장을 정확히 말하도록 spec을 명료화하고, 구현은 아무것도
숨기지 않도록 회계를 투명화한다. Codex가 정확히 지적한 두 결함은 수정한다:

1. **주석의 사실 왜곡** — "snapshot 시점에 이미 떠났다"는 관찰 순서와 다름
   → 실제 순서(reserve 후 commit 전 이탈 = peer departure)로 재서술.
2. **snapshot 사후 축소 회계** — snapshot에서 unreachable을 빼서 보고하던
   것을 중단. detail/monitor event에 `unreachable_remote_target_count` 필드
   신설(공개 구조체, 10.0.0 pre-RC), snapshot은 항상 실측값. 불변식:
   `snapshot_remote == admitted + dropped + unreachable`.
3. **spec 명료화**(01-mesh-node ko/en §7 + 07-monitoring 구조체): NODROP
   all-or-none은 capacity admission 보장이고, reserve~commit 사이 pipe 종료는
   §5 peer 이탈로 분류되어 drop이 아닌 unreachable로 보고되며, 나머지
   snapshot target에는 그대로 전달됨을 명시.

검증: `test_nodrop_unreachable_target_accounting`(test_mesh_peer_admission,
fault 주입 결정적) — Codex의 검증 방향("reserve와 commit 사이 제거 후 public
결과·회계 검증") 그대로: snapshot=1 유지, dropped=0, unreachable=1, local leg
전달 유지, 호출 OK.

### 3.4 N4-I3-01 — 인정, 수정

posd-module-structure ko/en + architecture ko/en의 mesh wire inventory를
실제 4모듈(`mesh_wire`, `_codec`, `_admission`, `_ingress` +
`mesh_wire_internal.hpp`)로 갱신.

## 4. Fable editorial 4건 처리

- 빈 줄 잔재: 기존 허용 형상과 동종, 미조치.
- post-commit 컨테이너 할당: §3.2로 해소(editorial을 넘어 수정됨).
- NODROP 재시도 재구축 churn: 성능 트랙(10.0.0 게이트 제외), 미조치.
- raw pubsub monitor test slow-joiner 창: 9.x 시계열 관찰(E12), 미조치.

## 5. 다음 단계

수정 5건 반영 후 전체 검증(85/85+2, ASAN, TSAN) → commit → iteration 5
(수정·재리뷰 4회차 이후 연장 라운드) 기동. **R2는 사용자 지시로 Claude
Fable → Claude Sonnet 교체.** clean 조건 동일: blocker·high·medium 0 + 세 축
CLEAN + 마지막 줄 `CORE REVIEW CLEAN`.
