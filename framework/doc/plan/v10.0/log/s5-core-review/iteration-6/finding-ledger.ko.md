# S5 Core 리뷰 finding ledger — iteration 6 병합

Snapshot: `b1e6c81fb` (631 files, scope hash `6fa7e8c6…`). 연장 라운드 2회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1·medium 2) · I2 CLEAN · I3 NOT CLEAN(low 1) |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(medium 1) · I2 CLEAN · I3 CLEAN |

iteration 5 병합 4건은 **양측 전부(Codex는 N5-I1-01만 부분) 해소 판정**.
Sonnet은 동적 재검증(85/85·ASAN 5종·TSAN lifecycle) 포함. 병합 유효 finding은
4건(중복 제거): OOM 매핑 계열(양측), shutdown/destroy 수명(high, Codex 단독,
coordinator 코드 검증으로 실재 확정), monitor close errno(Codex), internals
timer 경계(Codex).

## 2. 병합 finding과 해결

### 2.1 N6-I1-02 — shutdown 대기 중 destroy의 node 수명 경쟁 (high, Codex)

coordinator 재현 판정: **실재**. shutdown이 cv wait에서 mutex를 놓는 동안
destroy가 `shutdown_active`를 검사하지 않고 node를 delete할 수 있고, drained/
!drained 양 경로 모두 unlock 후 `wire_stop`/`emit`/`complete_operation`이
node를 계속 사용하는 창이 있었다. 해결:

- destroy가 `shutdown_active`를 검사해 spec §11의 재진입 규칙대로
  `ZLINK_CLOSE_BUSY` + `EDEADLK`로 거부.
- shutdown은 unlock 후 꼬리(이벤트 방출·완료 전달·wire_stop)까지
  `shutdown_active`를 유지하고 마지막에 재획득 후 해제 — destroy가 검사하는
  플래그가 실제 node 사용 구간 전체를 덮는다.
- destroy의 child 검사와 강제 종료를 한 lock 보유로 병합(검사~약속 사이 창
  제거), `unregister_node`를 teardown 앞으로 이동(신규 진입은 EFAULT).
- errno map(ko/en) close 표에 `EDEADLK` = 같은 handle lifecycle 재진입(§11)
  명시(§11과 표의 상호 참조 정합화).
- 검증: 신규 `test_destroy_during_shutdown_wait_is_deadlock_error` — 보유
  claim으로 shutdown을 wait에 파킹 → 동시 destroy가 EDEADLK·node 생존 →
  claim 해제 → shutdown OK → destroy OK. Codex의 검증 방향 그대로.

### 2.2 N6-I1-01 + CS6-I1-01 — submit-family OOM 매핑 (medium, 양측)

Sonnet이 전 지점을 열거(같은 계열의 잔여 전수). 해결 — bad_alloc 장벽을
submit-family 전반에 배치:

- `publish_common`: 상단 문자열 검증·snapshot vector·record 선구축·슬롯
  선예약을 하나의 장벽으로 통합, remote leg(envelope 할당)도 별도 장벽
  + placeholder/ready 롤백.
- `copy_borrowed_parts`/`copy_session_record_parts`: resize 실패 → ENOMEM
  반환, 호출자들은 `errno==ENOMEM ? OUT_OF_MEMORY : INTERNAL_ERROR` 매핑.
- `submit_local_record`·actor send/request·stream session submit의 record
  population 각각 장벽.
- `admit_record`: ready-index 삽입·deque push를 counter 갱신보다 앞에 두고
  장벽으로 감싸 실패 시 mailbox 원상 복구 + ENOMEM(같은 계열의 심층 지점
  선제 해소).
- `emit_monitor_event`: bounded queue push의 bad_alloc은 overflow drop과
  동일한 관측 행동으로 이벤트 폐기(commit 후 오보고 방지).

### 2.3 N6-I1-03 — monitor close errno (medium, Codex)

**인정, 수정**: active handler close를 `EBUSY`로 변경(공식 close 매핑 준수;
EDEADLK는 handler 등록·node lifecycle 재진입 전용).

### 2.4 N6-I3-01 — internals timer 경계 서술 (low, Codex)

**인정, 수정**: services-internals(ko/en) 계층 규칙에 `mesh_api.cpp` seam이
Spot timer registry·turn admission 상태를 직접 소유함을 명시.

## 3. 검증 (수정 후)

- 전체 suite 85/85. 신규 test 포함 lifecycle 10/10.
- ASAN 5 바이너리(39 case) 리포트 0.
- TSAN 단독: lifecycle 10/10(경고 13=auto-HWM 11+mailbox 1+pipe teardown 1,
  전부 기존 계열), stress 3/3(경고 3). 신규 mesh race 0.

## 4. Known risk

양 리뷰어 4건 모두 수용·추적 유지.

## 5. 다음 단계

commit → iteration 7(연장 3회차, Codex+Sonnet) 전체 pass.
