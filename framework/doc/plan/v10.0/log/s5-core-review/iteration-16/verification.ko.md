# S5 종료 검증 — iteration 16 (acceptance candidate `1f247af7a`)

두 리뷰어가 `CORE REVIEW CLEAN`을 남긴 뒤 coordinator가 실행한 stage 종료
검증. reviewer는 이 검증을 실행하지 않았다(ledger §2.2).

## 결과

| 검증 | 명령 | 결과 |
|---|---|---|
| 전체 CTest | `ctest --test-dir core/build -j4` | **86/86 passed, 0 failed** |
| ASAN mesh+scheduler | build-asan의 mesh 5타깃 + `unittest_request_timeout_scheduler` + `unittest_service_control_runtime` | **7/7 통과, report 0** |
| 공개 표면 gate | `contract_public_surface` | **PASS** (formal public surface 196, 제거 identifier 부재, package metadata 10.0.0/SOVERSION 10) |
| `git diff --check` | working tree | **통과** |
| TSAN lifecycle | build-tsan `test_mesh_lifecycle_contracts` (`setarch -R`) | 테스트 **13/13 PASS (0 Failures, OK)**. warning 15건 = auto-HWM lock-order 14 + raw mailbox recv 1 (`mailbox.cpp:66`) — 전부 기존 known risk 계열, **신규 Mesh operation/monitor race 0** |
| TSAN stress | build-tsan `test_mesh_stress` (`setarch -R`) | 테스트 PASS, warning 3건 전부 기존 auto-HWM lock-order 계열 |
| TSAN scheduler | build-tsan `unittest_service_control_runtime` (`setarch -R`) | **PASS, warning 0** (신규 worker lifecycle 수정에 race 없음) |

## Known risk 최종 상태

1. TSAN auto-HWM lock-order — TSAN에서 14+3건으로 기존과 동일하게 재현.
   정적 검토로 역방향 확정 경로 없음, 신규 아님. **추적 유지** (S6 이후).
2. raw command mailbox ypipe — TSAN mailbox recv 1건 기존과 동일. **추적 유지.**
3. raw socket teardown 관찰 — 이번 candidate diff 무관. **추적 유지.**
4. `ctx_term` linger — 계약 일치, finding 아님. S5-15 수정으로 worker
   bad_alloc 뒤에도 term hang 없음을 리뷰어가 추적 확인.

## 판정

S5 완료 gate의 종료 검증 항목 전부 통과. 신규 sanitizer race 0.
다음: S5-11/12 (최신 source 기준 internals 확정·검사).
