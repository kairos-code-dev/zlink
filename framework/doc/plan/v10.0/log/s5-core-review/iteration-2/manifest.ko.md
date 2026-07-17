# S5 Core 구현 리뷰 manifest — iteration 2 (최종 전체 pass, P4)

## 1. 목적

iteration 1의 finding 12건(R1 9 + R2 3)을 모두 수정한 새 snapshot에 대한
**마지막 전체 pass**다. 두 리뷰어는 delta와 직접 영향 범위를 깊게 검토하되,
§2.1 규칙대로 최신 snapshot의 전체 campaign scope를 처음부터 다시 검토하고
I1·I2·I3 세 축을 각각 재판정한다.

## 2. Snapshot

| 항목 | 값 |
|---|---|
| Stage / iteration | S5 / 2 (최종 전체 pass) |
| Acceptance candidate commit | `a01b537f8ce36d24db44d611b9d9dce4e263306e` |
| 직전 candidate | `8206fd44dcd` (iteration 1) |
| Delta | 32 files, +3436/−1722 (`git diff 8206fd44d..a01b537f8`) |
| Scope 파일 수 | 630 |
| Scope aggregate SHA-256 | `fa95152dcc7aecf633a79405f35f3613a5cc833824052bde22775aa71ae370c6` |

Scope hash 재계산은 iteration 1 manifest §2와 같은 명령을 사용한다.

## 3. 반영된 finding과 수정 요지

[finding ledger](../iteration-1/finding-ledger.ko.md)의 12건 전부. 수정 요지는
commit `a01b537f8ce` 메시지와 다음 delta 파일에 있다.

- NODROP 원자 reserve: `socket_base`에 비소비 probe `routed_target_writable()`
  신설(`socket_base.hpp`, `socket_base_msg.cpp`, `socket_base_routing.cpp`),
  `mesh_wire.cpp`의 publish가 reserve→commit 2단계, `mesh_messaging_api.cpp`가
  zero-commit backpressure를 SNDTIMEO까지 재시도
- Spot 수명·timer: `maybe_end_spot_locked`(runtime), spot timer immortal
  registry+seam(`mesh_api.cpp`, `mesh_api_internal.hpp`), timer 기계 hook
  (`timer_api.cpp`, `timer_scheduler_backend.cpp`), take_claim의 timer-turn 배제
- actor destroy drain(`mesh_actor_api.cpp`), draining actor ESHUTDOWN(단 fence는
  EAGAIN 유지)
- shutdown detach·재진입(`mesh_node_api.cpp`, `shutdown_active`)
- advertised endpoint·MIXED 병합·generation 교체 DRAINING entry
  (`mesh_wire_admission.cpp`, descriptor codec)
- query 2-pass 검증(`mesh_node_api.cpp`, `mesh_stream_session_api.cpp`)
- strict UTF-8 단일 validator(`mesh_runtime.cpp`의 `valid_utf8`, `check_name`,
  topic·filter 경로)
- claim serial 프로세스 전역화+immortal side table(`mesh_dispatch_api.cpp`)
- monitor handler_active 가드 실장착(`mesh_runtime.cpp`, depth 카운터)
- F3 삼항 제거
- I2: `mesh_wire`를 4모듈로 분해(`mesh_wire_codec/admission/ingress/wire` +
  `mesh_wire_internal.hpp`), 공개 표면 불변
- internals 문서 동기(`services-internals.{ko.md,md}`)

## 4. 신규 검증

- `test_mesh_lifecycle_contracts`(7 case): 다중 node claim 유일성, monitor
  handler 재진입 EDEADLK, Spot 수명 종료·generation 증가, actor destroy drain,
  shutdown detach(TERMINATED/ESHUTDOWN)와 순차 재-shutdown, query 출력 불변,
  UTF-8 거부 matrix
- `test_mesh_peer_admission`에 2-process MIXED 병합 case 추가(11 case)
- 전체 suite 85/85, ASAN clean, TSAN mesh 신규 race 0 (기존 기계 2계열 유지:
  auto-HWM lock-order, raw socket command mailbox ypipe)

## 5. 리뷰어 지시

- iteration 1과 같은 실행 계약(읽기 전용, 결과 파일 2개, scope hash 시작·종료
  기록). 결과 파일은 이 디렉토리(`iteration-2/`)에 쓴다.
- 세 축 각각 finding 또는 `없음` + `CLEAN`/`NOT CLEAN`.
- blocker·high가 없고 세 축 모두 CLEAN이면 마지막 줄에 정확히
  `CORE REVIEW CLEAN`.
- 남은 known risk(기존 기계 TSAN 2계열)와 다음 관찰을 명시 판정한다:
  ① NODROP commit 단계에서 peer 사망 시 잔여 dropped 처리(reserve 뒤 pipe
  소실은 admitted>0으로 OK 반환) ② Spot timer handler 대기가 전역 timer
  scheduler 스레드를 점유하는 head-of-line 특성 ③ ctx_term이 상대 종료 후
  linger로 지연될 수 있는 기존 raw 소켓 특성(신규 MIXED test에서 관측, 테스트는
  종료 순서로 회피).
