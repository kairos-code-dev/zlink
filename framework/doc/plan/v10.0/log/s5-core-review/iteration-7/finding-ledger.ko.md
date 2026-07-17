# S5 Core 리뷰 finding ledger — iteration 7 병합

Snapshot: `f8c35e6fe` (631 files, scope hash `cdbc1b10…`). 연장 라운드 3회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1·medium 1) · I2 CLEAN · I3 NOT CLEAN(low 1) |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(high 2) · I2 CLEAN · I3 NOT CLEAN(low 1) |

iteration 6 병합분: N6-I1-03·N6-I3-01은 양측 해소, N6-I1-02는 부분 해소
(역순 창 잔존), OOM 장벽은 부분 해소(잔여 지점). 병합 유효 finding 3건:

- **N7-I1-01 = CS7-I1-02 (high, 양측 수렴)**: shutdown의 handle 검증과
  mutex 획득 사이에 destroy가 node를 삭제하는 역순 창.
- **N7-I1-02 (medium, Codex) + CS7-I1-01 (high, Sonnet)**: OOM/예외 매핑
  잔여 — submit의 validation·target 선택·operation/reply map·remote wire
  envelope, 그리고 완료/응답 절반(complete_operation의 plain new,
  ingress/timer 스레드의 무장벽 경로 = std::terminate 위험).
- **N7-I3-01 = CS7-I3-01 (low)**: CHANGELOG lifecycle case 수 불일치.

## 2. Coordinator 해결

### 2.1 역순 수명 창 — registry lifecycle pinning

설계: §11이 규정하는 두 함수(shutdown/destroy)를 registry가 서로 순서화한다.

- `pin_node_lifecycle`: shutdown이 진입 시 registry mutex 아래에서
  유효성 검사(+`check_tag`도 lock 안)와 pin을 원자적으로 수행. destroy가
  이미 claim했다면 §11 재진입 = `EDEADLK`. RAII guard로 모든 return에서
  unpin.
- `claim_node_destroy`: destroy가 진입 시 handle lifecycle을 배타 점유
  (이중 destroy는 `ESTALE`/INVALID_HANDLE). EBUSY·EDEADLK 반환 경로는
  claim 해제.
- `unregister_node_and_wait_lifecycle_quiesced`: destroy commit 후
  registry에서 제거하고 **pin이 0이 될 때까지 대기**한 뒤에만 teardown·
  delete로 진행 — 검증만 통과한 shutdown이 아직 어디에 있어도 storage가
  살아 있다.

모든 interleaving: (a) shutdown 파킹 중 destroy → `shutdown_active` 가드
EDEADLK(기존), (b) shutdown pin~lock 사이 destroy 완주 시도 → destroy가
pin 대기, shutdown은 STOPPED 관측 후 OK, destroy 이후 완료, (c) destroy
claim 후 shutdown → pin이 EDEADLK, (d) destroy 완료 후 shutdown → EFAULT,
(e) 이중 destroy → 둘째 ESTALE.

검증: 신규 test-only hook `zlink_test_set_shutdown_pause_after_pin`으로
역순 interleaving을 결정적으로 재현하는
`test_destroy_waits_for_pinned_shutdown` (Codex의 검증 방향 그대로:
검증 통과 직후 정지 → destroy 완주 → 안전 완료 확인).

### 2.2 OOM/예외 매핑 전면화

- **공개 진입점 외곽 장벽**: submit-family 공개 C 함수 25개 전부에
  function-try-block(`bad_alloc` → `ENOMEM`/`OUT_OF_MEMORY`). validation·
  target snapshot·operation/reply map·remote wire envelope 등 심층 rollback
  장벽이 못 덮는 잔여 할당이 전부 여기서 봉인된다(C ABI 밀봉이 계약의
  핵심 요구; 부분 상태 정합은 심층 장벽이 담당).
- **완료/응답 절반**: `complete_operation`을 nothrow 할당+장벽으로 전환
  (ingress·timer 스레드에서 terminate 불가), ingress dispatch 루프에
  메시지 단위 장벽(OOM 시 해당 inbound 메시지 drop = 전송 손실과 동일
  관측).
- **오매핑 2건 수정**: actor local submit의 admit ENOMEM →
  `OUT_OF_MEMORY`(기존 INTERNAL_ERROR), stream complete 병합 할당 실패 →
  `OUT_OF_MEMORY`.
- **OOM contract test**: test-only hook `zlink_test_set_mesh_alloc_fault`
  (record 준비·publish 준비·operation 등록 3지점)로
  `test_submit_alloc_failure_maps_to_out_of_memory` — 주입 시
  OUT_OF_MEMORY/ENOMEM·예외 0, 해제 후 즉시 성공.

### 2.3 검증 중 발견한 spec 미달 1건 (coordinator, N7-C1)

수정 후 TSAN 검증에서 `test_ready_handler_churn_under_load`가 간헐 실패
(`ZLINK_HANDLER_DEADLOCK`)했다. 근본원인: spec 02-dispatch는 "성공한 해제는
이미 시작한 callback이 모두 반환한 뒤 완료되고, handler **안에서의** 해제만
EDEADLK"라고 규정하는데, 구현은 callback in-flight면 스레드 무관하게
EDEADLK를 반환했다(TSAN 감속이 창을 넓혀 노출). 수정: handler 실행 스레드
id를 기록해 같은 스레드 재진입만 EDEADLK, 다른 스레드의 (해)등록은 진행 중
callback 반환까지 cv 대기 후 적용. TSAN stress 2회 연속 그린으로 재현 소멸
확인 — 이전 iteration들의 "churn EDEADLK 부하 오염" 분류는 이 결함의
간헐 발현이었음을 정정한다.

### 2.4 CHANGELOG

lifecycle contracts 12 case로 갱신(양 lifecycle 재진입 순서·OOM 매핑 case
설명 포함).

## 3. 검증 (수정 후)

전체 suite 85/85(lifecycle 12/12), ASAN 5종 clean, TSAN 단독 lifecycle·
stress 그린·신규 mesh race 0. (세부 수치는 iteration-8 manifest.)

## 4. Known risk

4건 모두 양 리뷰어 수용·추적 유지.

## 5. 다음 단계

commit → iteration 8(연장 4회차, Codex+Sonnet) 전체 pass.
