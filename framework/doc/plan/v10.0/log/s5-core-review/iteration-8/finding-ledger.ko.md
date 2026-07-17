# S5 Core 리뷰 finding ledger — iteration 8 병합

Snapshot: `ee8036a09` (631 files, scope hash `269b6c1b…`). 연장 라운드 4회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(medium 1) · I2 CLEAN · I3 CLEAN |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(high 1·medium 2) · I2 CLEAN · I3 CLEAN |

iteration 7 수정분: N7-C1·N7-I3-01 해소(양측), lifecycle pinning과 OOM 장벽은
목표 범위는 해소이나 같은 근본원인의 더 넓은 표면이 신규로 지정됨.

## 2. 병합 finding과 Coordinator 해결

### 2.1 CS8-I1-01 (high) — pinning의 일반화: 나머지 57개 as_mesh_node 사이트

Sonnet 논거: §11이 send/request/publish/query를 thread-safe로 규정하므로
destroy 동시성의 UAF 창은 shutdown만이 아니라 모든 공개 진입점에 존재
(58 사이트 + timer 스레드 콜백). coordinator 수용 — **데이터패스 일반
pinning 구현**:

- `pin_node_data_path` + RAII `mesh_node_pin_t`: registry mutex 아래
  membership+tag 검증과 pin을 원자화. 공개 진입점 57개 사이트 전부 변환
  (timer 콜백 `on_operation_timeout`·claim 경로 포함 — claim도
  as_mesh_node 경유라 일괄 커버).
- 데이터패스 pin은 destroy claim(미커밋)을 거절하지 않는다 — 커밋된
  destroy는 unregister가 먼저이므로 registry에서 자연 차단(EFAULT).
  admission 단계에서 EBUSY로 실패할 destroy가 동시 send를 일시적으로
  깨뜨리는 회귀를 방지.
- destroy는 unregister 후 pin 0까지 대기(§11 기존 대기와 동일 cv).
  blocking 경로(admit cv·drain_ready 100ms 슬라이스·publish 재시도)는
  forced-stop 통지+상태 검사로 유한 시간 안에 pin을 놓음을 확인.
- `as_mesh_node` 자체도 tag 검증을 registry lock 안으로 이동(잔여 사용은
  membership 프로브 1곳).
- spec §11(ko/en)에 실제 계약 명문화: destroy는 진입한 호출 반환까지
  storage 유지, unregister 후 진입은 EFAULT, 이중 destroy는 ESTALE.
  internals(ko/en)에 pinning 메커니즘 기록.
- `CHANGELOG.md`의 lifecycle runner 수치를 실제 13개 case에 맞추고,
  concurrent submit/destroy lifetime pinning 검증을 명시.
- 검증: `test_destroy_waits_for_concurrent_submits` — 4-thread submit
  hammer × destroy, ASAN에서 UAF 0·post-destroy EFAULT 확인.

### 2.2 CS8-I1-02 = N8-I1-01 (medium, 양측) — 외곽 장벽 2개 누락

원인: 반환형이 별줄인 시그니처 2개(`zlink_mesh_node_request_to_channel`,
`zlink_stream_session_request_to_actor`)가 삽입 스크립트 패턴에서 빠짐.
27/27로 보완(별줄 형식 전수 감사로 잔여 0 확인).

### 2.3 CS8-I1-03 (medium) — handle_reply의 bookkeeping 선소거

operation 소거 후 tail 구성 중 OOM이면 ingress 장벽이 메시지를 버려
completion이 영구 소실. 해결: tail 처리를 `handle_reply_tail`로 분리하고
bad_alloc 시 `ZLINK_REQUEST_INTERNAL_ERROR`/`ENOMEM` terminal completion으로
강등(exactly-once 유지; completion 저장 자체의 OOM 드롭은 문서화된 강등
행동).

## 3. 검증 (수정 후)

- 전체 suite 85/85 (lifecycle 13/13 — hammer 추가).
- ASAN 5 바이너리(13·3·6·8·12 = 42 case) 리포트 0 — pinning hammer 포함.
- TSAN 단독: lifecycle 13/13(경고 18=auto-HWM 14+mailbox 1+pipe 1+asio
  blob 2, 전부 기존 계열), stress 3/3(경고 3). 신규 mesh race 0.

## 4. Known risk

4건 유지.

## 5. 다음 단계

Codex iter8 결과 병합 → 추가 finding 있으면 수정 → commit → iteration 9.
