# S5 Core 리뷰 finding ledger — iteration 5 병합

Snapshot: `c8d567c64` (631 files, scope hash `53ae8e44…`). 연장 라운드 1회차.

## 1. 판정 요약

| 리뷰어 | 판정 | 축 |
|---|---|---|
| R1 Codex | **NOT CLEAN** | I1 NOT CLEAN(high 1·medium 1) · I2 CLEAN · I3 NOT CLEAN(low 2) |
| R2 Claude Sonnet | **NOT CLEAN** | I1 NOT CLEAN(medium 2) · I2 CLEAN · I3 NOT CLEAN(low 2) |

두 리뷰어가 완전히 같은 4개 지점으로 수렴했다(severity 표기만 상이). 동적
증거는 Sonnet이 독립 재실행: 85/85, ASAN 5종 clean, TSAN baseline 3건 재현
확인, 신규 mesh race 0. scope hash 양쪽 시작·종료 일치.

## 2. iteration-4 수정 해소 판정 병합

| ID | Codex | Sonnet | 병합 |
|---|---|---|---|
| F-I1-03(재재재) | 해소 | 해소(happens-before 논증) | **해소 확정** |
| N3-I1-01(재) | 핵심 해소 | 핵심 해소 | **해소 확정**(파생 1건 신규 분리) |
| F-I1-01 | 부분(§9 잔여) | 부분(§9 잔여) | §7·회계·주석·필드는 **양측 수용**, §9 잔여만 계속 |
| N4-I3-01 | 해소 | 해소 | **해소 확정** |

## 3. 병합 finding 4건과 해결

### 3.1 F-I1-01(잔여) / CS-I1-02 — §9 무조건적 원자성 문구 (Codex high / Sonnet medium)

§7의 peer-departure 예외를 신설했지만 §9 "snapshot 전체의 원자적
reserve/commit까지 보장"이 무제한 문구로 남아 §7과 계속 충돌. **인정, 수정**:
§9(ko/en)에 "이 원자성은 §7의 capacity admission 보장이며 reserve~commit
사이 peer 이탈은 §7 unreachable 규칙을 따른다"를 명시해 §7을 단일 정본
지점으로 고정.

### 3.2 N5-I1-01 / CS-I1-01 — slot_base 확보가 try 밖 (양측 medium)

`std::vector<size_t> slot_base (accepting.size ())` 생성이 catch 범위 밖이라
그 지점의 bad_alloc이 C ABI를 넘을 수 있음. **인정, 수정**: 빈 vector 선언 후
`try` 안에서 `resize` — 선예약 경로의 모든 할당이 ENOMEM 매핑 안으로 들어감.
롤백은 slots_taken 카운터 기반이라 resize 실패 시에도 정합.

### 3.3 N5-I3-01 / CS-I3-01 — CHANGELOG 검증 수치 낡음 (양측 low)

**인정, 수정**: 84/84→85/85, peer admission 10→12 case(신규 unreachable
accounting·MIXED merge 명시), lifecycle contracts 9 case(bind/destroy hammer)
추가, 기계 관찰 3→4건.

### 3.4 N5-I3-02 / CS-I3-02 — race test 반환값 미관측 (양측 low)

**부분 인정, 수정**: 두 binder의 반환값을 캡처해 합법 결과 집합
{OK, INVALID_STATE, NOT_FOUND, BACKPRESSURED} 단정을 추가. 주석은 실제 단정
범위(합법 집합·post-destroy 단조 실패·binding 잔존 0)로 정합화하고,
성공-후-rollback interleaving 자체는 외부 관측점이 없어 설계 논증(양 성공
형태 재검증+단조 staleness, 양 리뷰어가 해소 확정한 F-I1-03)으로 닫힘을
명시. 원 반례를 test 실패로 만들려면 test 전용 pause seam이 필요한데, 낮은
severity 대비 공개 경로에 seam을 추가하는 비용이 크다는 coordinator 판단.

## 4. Known risk

양 리뷰어 4건 모두 수용·추적 유지(신규 finding 없음): TSAN auto-HWM
lock-order, TSAN mailbox ypipe, raw teardown 관찰(detach_peer_backref·asio
blob_t), ctx_term linger.

## 5. 다음 단계

수정 4건 반영 → 전체 검증 → commit → iteration 6(연장 2회차, Codex+Sonnet)
전체 pass. 미해결 medium+ 0건과 세 축 CLEAN까지 계속.
