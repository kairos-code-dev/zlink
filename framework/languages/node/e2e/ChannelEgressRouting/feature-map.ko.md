# Config 12 Node E2E feature map

공통 Config 12의 exact scenario를 selector에 등록했지만, Node role server와
process evidence가 아직 없다. `run_e2e.sh`는 이 상태를 `BLOCKED`로 반환하며
성공으로 집계하지 않는다.

| ID | 상태 | 후속 조건 |
|---|---|---|
| CH-E2E-01 | blocked | RouteMesh 양방향 request role과 evidence 구현 |
| CH-E2E-02 | blocked | 다른 topology nested request role 구현 |
| CH-E2E-03 | blocked | Spot callback·timer downstream request role 구현 |
| CH-E2E-04A | blocked | ClientServer weight process evidence 구현 |
| CH-E2E-04B | blocked | draining server admission evidence 구현 |
| CH-E2E-04C | blocked | server restart readiness evidence 구현 |
| CH-E2E-05 | blocked | one-way send process evidence 구현 |
| CH-E2E-06 | blocked | duplicate egress startup failure 구현 |
| CH-E2E-07A | blocked | missing channel NotFound process evidence 구현 |
| CH-E2E-07B | blocked | local Server role remote selection 구현 |
| CH-E2E-07C | blocked | known target Unavailable process evidence 구현 |
| CH-E2E-08 | blocked | port 0 advertised endpoint evidence 구현 |
| CH-E2E-09 | blocked | client/server role split evidence 구현 |
| CH-E2E-10 | blocked | clientserver recovery evidence 구현 |
| CH-E2E-11 | blocked | ChannelName-only route selection 구현 |
| CH-E2E-12 | blocked | handler-originated one-way evidence 구현 |

`blocked`는 구현 완료나 PASS가 아니다. ND-E2E-IMP-001과 ND-E2E-IMP-002의
후속 작업 입력으로 유지한다.
