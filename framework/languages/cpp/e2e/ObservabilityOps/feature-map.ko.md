# Config 11 — ObservabilityOps (C++) feature map

정본 시나리오: [config-11-observability-ops.ko.md](../../../../doc/framework/common/e2e/config-11-observability-ops.ko.md)

현재 runner는 `Session`, `Play`, `OrderWorkflow`를 별도 실행 진입점과 역할별 설정 파일로 시작하고,
standalone `Client`도 별도 실행 대상으로 사용한다. 열세 시나리오의 결과 단언은
`Client/Scenarios/obs_*_scenario.hpp`에 ID별로 분리했고, runner에는 process 수명주기와 drain 중간
상태 확인만 남겼다. 시나리오 결과의 관계 단언은 client가 담당하고, runner는 외부 장애와 역할 수명주기를
결정적으로 만든다.

| 시나리오 | 상태 | 비고 |
|----------|------|------|
| OBS-A1 | `implemented` | connector 발원 flow를 session 수신→route 송신→원격 spot 수신 순서로 대조한다. |
| OBS-A2 | `implemented` | 같은 flow의 수신·dispatch error·`phase=error` 순서를 대조한다. |
| OBS-A3 | `implemented` | tracing-off 노드 전후의 같은 flow를 대조하고 off 노드에는 flow 로그가 없음을 확인한다. |
| OBS-A4 | `implemented` | 한 publish flow가 두 subscriber에 전달되는지 확인하고 timer 발원 flow를 별도로 판별한다. |
| OBS-B1 | `deferred` | server connection 계기는 확인하지만 connector의 `stream.reconnects` 계기는 공개 계약 검토가 남아 있다(`E2E-CP-11`). |
| OBS-B2 | `implemented` | 다수 room action 뒤 큐 depth·wait를 확인하고 actor 이동 1회와 transfer duration·pending sample 1회를 대조한다. |
| OBS-B3 | `implemented` | fanout 차분 1:2, drop 부재, 금지 label 부재를 확인하고 Redis 외부 지연으로 lease lateness를 만든다. |
| OBS-B4 | `implemented` | metrics-off 노드의 메시징 성공을 확인하고 단위 테스트가 reader 없는 10,000회 계측 뒤 내부 저장 구조 불변을 검증한다. |
| OBS-C1 | `deferred` | typed draining row 유지, 기존 route 요청 8/8, owner lease 갱신, drain state 전이와 명시적 create 거절은 검증한다. |
| OBS-C2 | `deferred` | actor 이동 뒤 ping은 확인하지만 bound-session push 연속성과 pending request 결과를 확인하지 않는다(`E2E-CP-11`). |
| OBS-C3 | `implemented` | drain-natural room은 Draining 중 public close까지 유지되고, release-and-recreate workflow는 새 owner에서 append-only event를 재생한다. 두 policy의 room counter를 각각 확인한다. |
| OBS-C4 | `deferred` | 별도 `Session`과 `Play` 역할에서 강제 종료 통지와 public `closeReason` 검사는 동작한다. |
| OBS-C5 | `deferred` | 별도 역할 배치에서 rolling drain과 zero-target의 terminal state·counter 검사는 동작한다. |

실행: `./run_e2e.sh [all|flow|metrics|fanout|drain|handoff|force|policy|offnode]`

검증 로그는 flow `logs/20260716-092832-3306098`, fanout `logs/20260716-094302-3355921`,
metrics `logs/20260716-094229-3353674`, policy `logs/20260716-100036-3398019`, offnode
`logs/20260716-094745-3369410`이다. connector `stream.reconnects`와 bound-session push의 남은 차이는
내부 계기나 테스트 전용 API로 대신하지 않고 해당 gap에 유지한다.
