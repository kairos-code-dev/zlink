# C++ SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`

이 문서는 C++ Config 10 E2E의 현재 구현 상태를 공통 시나리오별로 기록한다. 모든 시나리오 ID가
client와 `run_e2e.sh all`에 등록되어 있지만, 현재 배치와 일부 검증은 공통 완료 기준을 충족하지
않는다. 따라서 실행 코드가 있다는 이유만으로 `implemented`로 표시하지 않는다.

현재 runner는 actor 노드 세 개만 시작하고 각 노드가 HTTP 제어 endpoint와 stream endpoint를 함께
제공한다. 공통 문서가 요구하는 actor 노드 두 개, session gateway 두 개, transfer controller 한 개의
역할 분리는 아직 구현되지 않았다(`E2E-CP-56`). 이 배치 차이를 해소하기 전까지 Config 10 전체는
완료로 판정할 수 없다.

| 시나리오 | 상태 | 현재 구현과 남은 gap |
|----------|------|----------------------|
| ST-A1 | `deferred` | local admission부터 성공 응답까지의 순서를 client가 검사한다. 역할별 배치가 공통 구성과 다르다(`E2E-CP-56`). |
| ST-A2 | `deferred` | local admission 거절과 joined side effect 부재를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-A3 | `deferred` | joined gate가 유지되는 동안 actor packet이 완료되지 않는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-B1 | `deferred` | stateful remote transfer와 target state를 검사하고, source·target 역할 evidence의 `commit_request`, `location_committed`, `commit_ack`, `source_cleanup`이 같은 transfer id와 message-flow correlation을 공유하는지 확인한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-B2 | `deferred` | commit ack 뒤 success reply를 확인하고 source cleanup evidence가 아직 없을 때 source를 중단한 뒤 target generation과 packet 처리가 유지되는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-B3 | `deferred` | adapter 미등록 시 빈 state transfer와 source·target callback 순서를 검사하고, commit 경계 marker가 같은 transfer id와 message-flow correlation을 공유하는지 확인한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-B4 | `deferred` | 명시적인 빈 state transfer 뒤 target state를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-C1 | `deferred` | admission 뒤 commit 전 source를 중단하고, target의 구조화된 `pending_admission_expired` evidence와 target membership·dispatch 부재를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-C2 | `deferred` | target commit 뒤 source 중단 후 location과 bound push를 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-C3 | `deferred` | callback failure 네 종류를 실행하고, joined callback 실패 뒤 실제 actor packet 요청이 실패하며 target handler evidence도 생기지 않는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-D1 | `deferred` | local·remote location은 joined 완료 전 기존 ref를 유지하고 완료 뒤 committed ref로 바뀌며, local 지연 중 packet이 target handler에 먼저 도달하지 않고 commit 뒤 target에서 처리되는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-D2 | `deferred` | commit 뒤 source cleanup queue가 stale owner release를 실행하기 전후에 target packet과 generation snapshot이 유지되는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-E1 | `deferred` | transfer 전후 같은 connector의 bound push 수신을 검사한다. 별도 session gateway 역할이 없다(`E2E-CP-56`). |
| ST-E2 | `deferred` | transfer-out adapter 실패로 commit 전 transfer를 거절하고, source의 기존 bound session이 follow-up notify를 받으며 target에는 `bound_push`·`joined` evidence가 없는지 검사한다. 역할별 배치 gap이 남아 있다(`E2E-CP-56`). |
| ST-F1 | `deferred` | target 처리 순서와 필수 `handoff_backlog`·`backlog_enqueued` marker를 검사하며, marker가 없으면 runner가 실패한다. marker 전달이 stderr 환경 변수 경로에 의존하는 gap은 남아 있다(`E2E-CP-57`). |
| ST-F2 | `deferred` | moving 중 B1/B2를 보낸 뒤 target의 구조화된 location commit evidence를 경계로 D1을 보내며, join caller가 완료를 읽기 전에 B1→B2→D1 순서를 검사한다. 필수 handoff marker 관측 gap은 남아 있다(`E2E-CP-50`, `E2E-CP-57`). |
| ST-F3 | `deferred` | moving 중 S1/S2를 보내고 target의 구조화된 location commit evidence 직후 기존 bound session으로 S3/S4를 보내 join caller가 완료를 읽기 전에 전체 순서를 검사한다. marker 관측 경로 gap은 남아 있다(`E2E-CP-57`). |
| ST-F4 | `deferred` | 같은 explicit old ref one-way send로 G1/G2를 보내고, G1의 구조화된 `straggler_forward`와 target 처리 뒤 `mapping_evicted`, G2의 구조화된 `stale_fail_fast`와 target 미처리를 검사한다. stderr marker 경로 gap은 남아 있다(`E2E-CP-57`). |
| ST-F5 | `deferred` | source 역할별 구조화 evidence로 다음 hop forwarding entry가 하나뿐인지 확인하고, `mapping_evicted`를 기다린 뒤 두 old ref가 stale로 실패하는지 검사한다. stderr marker 경로 gap은 남아 있다(`E2E-CP-57`). |
| ST-F6 | `deferred` | reply correlation과 timeout 결과는 검사하지만 handoff 과정의 request id와 flags 보존 evidence는 stderr 경로에 의존한다(`E2E-CP-57`). |

## 검증

- 2026-07-15: `./run_e2e.sh ST-F1`
  - 결과: 통과
  - 로그: `logs/20260715-090842-2606499`
  - 의미: 필수 handoff marker가 없으면 경고가 아니라 실행 실패가 되며, ST-F1은 두 marker를 모두 남겼다.
- 2026-07-15: `./run_e2e.sh ST-E2`
  - 결과: 통과
  - 로그: `logs/20260715-090612-2594420`
  - 의미: 성공 transfer와 새 session rebind가 없어도, 실패한 transfer 뒤 source binding 유지와 target route 비오염을 판별한다.

## 실행 범위

`run_e2e.sh all`은 ST-A1부터 ST-F6까지 스무 시나리오를 모두 선택한다. source process 중단이 필요한
ST-B2, ST-C1, ST-C2는 다른 시나리오와 분리해 실행하며, 그 사이에 actor-a를 다시 시작한다. 이 목록은
시나리오가 실행 대상으로 등록되어 있음을 뜻할 뿐, 위 표에 적은 gap의 완료 근거는 아니다.

Config 10을 완료하려면 먼저 역할별 process 배치를 공통 구성에 맞추고, 각 `E2E-CP-*` 항목의 실패
게이트를 만든 뒤 시나리오 단언을 보강해야 한다. public framework 계약에 없는 기능이 필요하면
내부 helper나 테스트 전용 API로 우회하지 않고 별도 계약 검토 대상으로 남긴다.
