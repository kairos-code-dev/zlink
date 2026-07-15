# C++ SpotActorTransfer E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-10-spot-actor-transfer.ko.md`

이 문서는 C++ Config 10 E2E의 현재 구현 상태를 공통 시나리오별로 기록한다. runner는 actor node
두 개, session gateway 두 개, transfer controller 한 개와 consumer를 서로 다른 process로 시작한다.
actor 생성·이동·조회는 controller가 맡고, session 연결과 actor 처리는 각각 gateway와 actor node가
맡는다. `run_e2e.sh all`은 공통 문서의 스무 시나리오를 모두 실행한다.

| 시나리오 | 상태 | 현재 검증 |
|----------|------|----------------------|
| ST-A1 | `implemented` | local admission부터 성공 응답까지의 순서를 client가 검사한다. |
| ST-A2 | `implemented` | local admission 거절과 joined side effect 부재를 검사한다. |
| ST-A3 | `implemented` | joined gate가 유지되는 동안 actor packet이 완료되지 않는지 검사한다. |
| ST-B1 | `implemented` | stateful remote transfer와 target state를 검사하고, source·target 역할 evidence의 `commit_request`, `location_committed`, `commit_ack`, `source_cleanup`이 같은 transfer id와 message-flow correlation을 공유하는지 확인한다. |
| ST-B2 | `implemented` | commit ack 뒤 success reply를 확인하고 source cleanup evidence가 아직 없을 때 source를 중단한 뒤 target generation과 packet 처리가 유지되는지 검사한다. |
| ST-B3 | `implemented` | adapter 미등록 시 빈 state transfer와 source·target callback 순서를 검사하고, commit 경계 marker가 같은 transfer id와 message-flow correlation을 공유하는지 확인한다. |
| ST-B4 | `implemented` | 명시적인 빈 state transfer 뒤 target state를 검사한다. |
| ST-C1 | `implemented` | admission 뒤 commit 전 source를 중단하고, target의 구조화된 `pending_admission_expired` evidence와 target membership·dispatch 부재를 검사한다. |
| ST-C2 | `implemented` | target commit 뒤 source 중단 후 location과 bound push를 검사한다. |
| ST-C3 | `implemented` | callback failure 네 종류를 실행하고, joined callback 실패 뒤 실제 actor packet 요청이 실패하며 target handler evidence도 생기지 않는지 검사한다. |
| ST-D1 | `implemented` | local·remote location은 joined 완료 전 기존 ref를 유지하고 완료 뒤 committed ref로 바뀌며, local 지연 중 packet이 target handler에 먼저 도달하지 않고 commit 뒤 target에서 처리되는지 검사한다. |
| ST-D2 | `implemented` | commit 뒤 source cleanup queue가 stale owner release를 실행하기 전후에 target packet과 generation snapshot이 유지되는지 검사한다. |
| ST-E1 | `implemented` | transfer 전후 같은 connector의 bound push 수신을 검사한다. |
| ST-E2 | `implemented` | transfer-out adapter 실패로 commit 전 transfer를 거절하고, source의 기존 bound session이 follow-up notify를 받으며 target에는 `bound_push`·`joined` evidence가 없는지 검사한다. |
| ST-F1 | `implemented` | source `handoff_backlog`와 target `backlog_enqueued`를 같은 transfer correlation으로 검사한다. target은 prepare 뒤 받은 전체 backlog를 queue에 넣은 다음 location을 공개하며 P1→P2→P3 처리 순서도 확인한다. |
| ST-F2 | `implemented` | moving 중 B1/B2를 보내고 target의 `backlog_enqueued`가 `location_committed`보다 앞서는지 검사한다. location 공개 직후 D1을 보내 join caller가 완료를 읽기 전에 B1→B2→D1 순서가 유지되는지도 확인한다. |
| ST-F3 | `implemented` | moving 중 S1/S2를 보내고 target의 구조화된 location commit evidence 직후 기존 bound session으로 S3/S4를 보내 join caller가 완료를 읽기 전에 전체 순서를 검사한다. |
| ST-F4 | `implemented` | 같은 explicit old ref one-way send로 G1/G2를 보내고, G1의 구조화된 `straggler_forward`와 target 처리 뒤 `mapping_evicted`, G2의 구조화된 `stale_fail_fast`와 target 미처리를 검사한다. |
| ST-F5 | `implemented` | actor-a→actor-b→actor-a의 다른 Spot으로 연속 이동하고, source 역할별 구조화 evidence로 다음 hop forwarding entry가 하나뿐인지 확인한다. `mapping_evicted` 뒤 두 old ref가 즉시 실패하는지도 검사한다. |
| ST-F6 | `implemented` | source와 target의 구조화 evidence에서 같은 request id와 request flag가 보존되는지 비교한다. 같은 request id의 재시도는 backlog와 target handler에 한 번만 남고, 긴 timeout은 원래 caller reply로, 짧은 timeout은 일반 timeout과 late reply로 끝나는지 검사한다. |

## 검증

- 2026-07-15: `./run_e2e.sh ST-F1`
  - 결과: 통과
  - 로그: `logs/20260715-090842-2606499`
  - 의미: 필수 handoff marker가 없으면 경고가 아니라 실행 실패가 되며, ST-F1은 두 marker를 모두 남겼다.
- 2026-07-15: `./run_e2e.sh ST-E2`
  - 결과: 통과
  - 로그: `logs/20260715-090612-2594420`
  - 의미: 성공 transfer와 새 session rebind가 없어도, 실패한 transfer 뒤 source binding 유지와 target route 비오염을 판별한다.
- 2026-07-15: `./run_e2e.sh all`
  - 결과: 스무 시나리오 통과
  - 로그: `logs/20260715-111050-3056956`
  - 의미: 분리된 역할 배치에서 일반·실패·순서·session·request handoff 절차가 모두 통과했다.
- 2026-07-15: `./run_e2e.sh ST-F6`
  - 결과: 통과
  - 로그: `logs/20260715-110604-3043381`
  - 의미: request id와 request flag가 source backlog에서 target replay까지 유지되고, 중복 dispatch 없이 reply와 timeout이 원래 caller 계약으로 끝났다.
- 2026-07-15: admission burst 성능 비교
  - 결과: median throughput 94,926/s에서 95,955/s, p99 2,685.49µs에서 2,419.20µs
  - 의미: session route snapshot 리팩토링 뒤 dispatch 성능 회귀가 없었다.

## 실행 범위

`run_e2e.sh all`은 ST-A1부터 ST-F6까지 스무 시나리오를 모두 선택한다. source process 중단이 필요한
ST-B2, ST-C1, ST-C2는 다른 시나리오와 분리해 실행하며, 그 사이에 actor-a를 다시 시작한다. 모든
시나리오는 client가 역할별 evidence와 응답을 직접 판정하고, runner는 process 수명과 실행 순서만
관리한다.
