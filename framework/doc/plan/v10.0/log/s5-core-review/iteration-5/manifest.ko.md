# S5 Core 구현 리뷰 manifest — iteration 5 (연장 라운드, 전체 pass)

## 1. 목적

iteration 4(기본 4회 예산의 마지막 병합)에서 Codex가 high 3·low 1을 유지해
수정 정책의 4회 제한이 해제됐다. 이 iteration은 그 4건의 수정을 반영한
snapshot의 전체 pass이며, 미해결 medium+ 0건이 될 때까지 반복한다.

**R2 교체**: 사용자 지시와 ledger 규정에 따라 이번 iteration부터 R2는
Claude Sonnet이다(이전 Fable 결과는 과거 증거로 보존).

리뷰어 계약: finding은 이슈·근거(file:line)·영향·수정 범위·검증 방향만 제시.
해결 설계 선택·구현은 coordinator 책임.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 5 (연장 전체 pass) |
| Acceptance candidate commit | `c8d567c64` |
| 직전 candidate | `59b3ea940` (iteration 4) + 사용자 ledger commit `472f66a32` |
| Scope 파일 수 | 631 |
| Scope aggregate SHA-256 | `53ae8e44f5085109c684e81423c88342456d4b1ded9fc54c234a226d14b4c140` |
| Scope 정의 | `git ls-files core/include core/src core/tests core/packaging core/CMakeLists.txt core/doc/spec/core core/doc/internals CHANGELOG.md` 각 파일 sha256의 aggregate |

## 3. 반영 사항 (iteration 4 finding 4건)

[iteration-4 finding ledger](../iteration-4/finding-ledger.ko.md) 참조.

- **F-I1-03(재재재)**: bind 사후 재검증을 insert·idempotent 두 성공 형태
  모두로 확장(`mesh_stream_session_api.cpp`). staleness 단조성으로 어떤
  interleaving도 파괴된 generation에 성공을 보고하지 않음.
- **N3-I1-01(재)**: publish local 슬롯 선예약 — mailbox deque placeholder와
  ready-index 키를 remote commit **이전에** 확보(실패 시 전량 롤백+ENOMEM),
  commit 뒤에는 무실패 대입만 남음(`mesh_messaging_api.cpp`).
- **F-I1-01**: coordinator 판정 = spec 내부 긴장(§5 vs §7)의 명료화 + 회계
  투명화. `unreachable_remote_target_count` 공개 필드 신설(detail+monitor
  event, 헤더+spec ko/en+07-monitoring), snapshot 사후 축소 중단(불변식
  snapshot=admitted+dropped+unreachable), wire commit 주석의 사실 왜곡 교정,
  §7에 capacity all-or-none vs §5 peer 이탈 구분 명시.
- **N4-I3-01**: posd-module-structure·architecture ko/en의 mesh wire 4모듈
  inventory 갱신, services-internals에 슬롯 선예약·unreachable 회계 반영.

신규 테스트 2건: `test_stream_session_bind_destroy_race_leaves_no_binding`
(lifecycle contracts, 2-thread bind × destroy hammer),
`test_nodrop_unreachable_target_accounting`(peer admission, fault 주입 결정적).

## 4. 기존 검증 (2026-07-17, candidate `c8d567c64`)

- 전체 suite 85/85 (신규 2 case 포함, 총 87 case 레벨).
- ASAN 5 mesh 바이너리(lifecycle 9·stress 3·monitor 6·basic 8·admission 12)
  리포트 0.
- TSAN 단독 실행: lifecycle 9/9(경고 12=auto-HWM lock-order 10+mailbox 1+raw
  pipe teardown 1), stress 3/3(경고 3). mesh 신규 race 0.
- TSAN 2-process admission 3건 실패(round-robin·MIXED·reconnect)는 **baseline
  `472f66a32`(수정 미포함)에서 동일 재현** — TSAN 감속 하의 기존 시간민감성,
  delta 무관. 부하 병행 시 stress churn EDEADLK·dl_fini SEGV도 관측됐으나
  단독 재실행에서 미재현(부하 오염 분류).

## 5. Known risk (명시 판정 대상)

1. TSAN auto-HWM lock-order 계열(기존).
2. TSAN raw command mailbox ypipe 계열(기존).
3. raw socket teardown 관찰: `pipe_t::detach_peer_backref`(단독 재현)과 부하
   시 asio engine error 경로 `blob_t` 접근 — 신규 raw STREAM 테스트가 노출한
   9.x raw 기계 계열(mesh delta 아님). 추적 유지.
4. ctx_term linger(기존, 2-process 종료 순서로 회피).

## 6. 리뷰어 지시

- 실행 계약 동일: core/ 읽기 전용, scope hash 시작·종료 기록, 결과는
  `iteration-5/`에 R1=`codex-review.ko.md`, R2=`claude-sonnet-review.ko.md`.
- iteration 4 수정 4건의 해소 판정(특히 F-I1-01 coordinator 판정의 수용/반박)
  + 전체 scope 재검토 + I1·I2·I3 축별 판정 + known risk 4건 명시 판정.
- blocker·high·medium 없음 + 세 축 CLEAN이면 마지막 줄 정확히
  `CORE REVIEW CLEAN`.
