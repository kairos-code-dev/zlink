# Config 11 — ObservabilityOps (C++) feature map

정본 시나리오: [config-11-observability-ops.ko.md](../../../../doc/framework/common/e2e/config-11-observability-ops.ko.md)

현재 runner는 `Session`, `Play`, `OrderWorkflow`를 별도 역할 process로 나누지 않고 `play-a`가 session
역할을 함께 맡는다. client scenario도 별도 파일이 아니라 shell과 인라인 Python에 들어 있다
(`E2E-CP-03`). 따라서 각 scenario에 실행 경로가 있더라도 공통 Config 11 완료 기준을 충족한 것으로
표시하지 않는다.

| 시나리오 | 상태 | 비고 |
|----------|------|------|
| OBS-A1 | `deferred` | flow id 집합의 교집합만 확인해 connector→actor relay→spot의 시간순 흐름을 판별하지 못한다(`E2E-CP-62`). |
| OBS-A2 | `deferred` | error flow id가 같은 id의 성공 라인과 함께 나타나는지 확인하지 않는다(`E2E-CP-62`). |
| OBS-A3 | `deferred` | tracing-off 상류의 id가 없어 하류 flow가 실제로 전파된 값인지 대조할 수 없다(`E2E-CP-62`). |
| OBS-A4 | `deferred` | subscriber 하나만 사용하고 timer flow를 action flow에 연결하지 않아 fan-out tree를 판별하지 못한다(`E2E-CP-62`). |
| OBS-B1 | `deferred` | server connection 계기는 확인하지만 connector의 `stream.reconnects` 계기는 공개 계약 검토가 남아 있다(`E2E-CP-11`). |
| OBS-B2 | `deferred` | room 부하를 만들지 않고 transfer 계기의 횟수와 구간을 계약값에 대조하지 않는다(`E2E-CP-63`). |
| OBS-B3 | `deferred` | fan-out 1:N 값을 대조하지 않으며 Redis lease 지연 주입도 없다(`E2E-CP-11`, `E2E-CP-63`). |
| OBS-B4 | `deferred` | meter가 꺼진 app collector의 빈 결과만 확인해 framework 내부 무적재를 판별하지 못한다(`E2E-CP-63`). |
| OBS-C1 | `deferred` | typed draining row 유지, 기존 route 요청 8/8, owner lease 갱신, drain state 전이와 명시적 create 거절은 검증한다. 역할별 process 배치 gap이 남아 있다(`E2E-CP-03`). |
| OBS-C2 | `deferred` | actor 이동 뒤 ping은 확인하지만 bound-session push 연속성과 pending request 결과를 확인하지 않는다(`E2E-CP-11`). |
| OBS-C3 | `deferred` | release-and-recreate 절반만 실행하고 row 해제 뒤 재생성을 기존 row 반환과 구분하지 못한다(`E2E-CP-63`). |
| OBS-C4 | `deferred` | 강제 종료 통지와 public `closeReason` 검사는 동작한다. 역할별 process 배치 gap이 남아 있다(`E2E-CP-03`). |
| OBS-C5 | `deferred` | rolling drain과 zero-target의 terminal state·counter 검사는 동작한다. 역할별 process 배치 gap이 남아 있다(`E2E-CP-03`). |

실행: `./run_e2e.sh [all|flow|metrics|fanout|drain|handoff|force|policy|offnode]`

각 gap은 해당 `E2E-CP-*` 항목에 실패 게이트와 수정 범위를 먼저 정한 뒤 닫는다. connector
`stream.reconnects`와 cross-language fanout wire처럼 공개 계약 검토가 필요한 항목은 내부 계기나
테스트 전용 API로 대신하지 않는다.
