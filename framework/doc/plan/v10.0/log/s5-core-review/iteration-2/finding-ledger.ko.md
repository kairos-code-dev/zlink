# S5 iteration 2 finding ledger

병합: R1 6건(신규) + R1 미해소 재판정 2건 + R2 4건(신규·부분해소 1 포함).
중복 1건(monitor close UAF: R1 N-I1-01 = R2 N3). editorial note 0건.
snapshot: `a01b537f8ce`.

| ID | 출처 | 축 | 심각도 | 요지 | 상태 |
|---|---|---|---|---|---|
| F-I1-01(재) | R1 | I1 | high | NODROP commit 중 peer 소실 시 admitted>0로 OK+dropped>0 반환 — 성공 dropped=0 계약 위반 | fixed: unreachable 대상은 snapshot 회계에서 제외(경합의 진실=snapshot 시점 이탈), NODROP dropped는 capacity 전용으로 항상 0 |
| F-I1-03(재) | R1 | I1 | high | destroy가 bound session control 미drain | fixed: `session_bindings_pending`로 deadline까지 대기(+node lock 해제 후 검사), commit 후 `session_bindings_remove_actor` |
| N1 | R2 | I1 | high | timer destroy가 scheduler mutex를 쥔 채 busy-refs 대기 → in-flight fire와 영구 데드락(재현 실증) | fixed: busy 대기 전 scheduler lock 해제 + `spot_timer_cancel`로 claim-대기 turn 취소. 회귀 test `test_timer_destroy_overlapping_fire_completes` |
| N-I1-01/N3 | R1+R2 | I1 | high/low | monitor emit vs close UAF(TOCTOU) | fixed: `monitor_emit_refs` 핀 + close가 0까지 대기 |
| N-I1-02 | R1 | I1 | high | actor destroy에서 maybe_end 뒤 무효 iterator 사용 | fixed: `spot_present` 선캡처, erase 뒤 iterator 미사용 |
| N2 | R2 | I1 | medium | `handle_actor_left` maybe_end 누락(zombie Spot) | fixed: LEFT record admit 후 maybe_end |
| N-I2-01 | R1 | I2 | medium | Spot timer 대기가 전역 scheduler head-of-line | fixed(설계는 coordinator 선택): per-Spot backend 대신 **per-MeshNode scheduler** 재사용 — node destroy에서 회수, 전역·타 node 격리 |
| N-I3-01 | R1 | I3 | medium | conandata sha256 미고정 | fixed(설계는 coordinator 선택): release workflow에 sha256 필수 gate 추가(빈 digest 시 실패). digest 값 기입은 tag 생성 직후 S6-05 단계에서 수행(README 절차 갱신) |
| N-I3-02 | R1 | I3 | low | tracked pycache + README 0.6.0/ulala-x 잔재 | fixed: pycache untrack+ignore, README 10.0.0/kairos 경로·sha256 필수 명시 |
| N-I3-03 | R1 | I3 | low | mesh_wire.cpp EOF blank | fixed |
| N4 | R2 | I3 | low | `valid_utf8_public` 사멸 선언 | fixed: 선언 제거. 부수: 9.x 잔재 `zlink_timer_cleanup_spot`/`owner_spot` 필드도 제거(사용처 0) |

검증: 전체 85/85, ASAN clean, TSAN 잔여=기존 2계열(auto-HWM lock-order·command
mailbox ypipe)뿐. 신규 test 1 case(timer destroy overlap+cancel) 추가.
