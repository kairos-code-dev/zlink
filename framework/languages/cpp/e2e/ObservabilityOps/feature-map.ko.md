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
| OBS-B1 | `deferred` | server connection 계기는 확인하지만 C++ connector가 정식 `zlink.stream.reconnects` counter를 reader에 노출하고 자동 재접속 시도마다 증가시키는 증거가 없다. |
| OBS-B2 | `implemented` | 다수 room action 뒤 큐 depth·wait를 확인하고 actor 이동 1회와 transfer duration·pending sample 1회를 대조한다. |
| OBS-B3 | `implemented` | fanout 차분 1:2, drop 부재, 금지 label 부재를 확인하고 Redis 외부 지연으로 lease lateness를 만든다. |
| OBS-B4 | `implemented` | metrics-off 노드의 메시징 성공을 확인하고 단위 테스트가 reader 없는 10,000회 계측 뒤 내부 저장 구조 불변을 검증한다. |
| OBS-C1 | `deferred` | typed draining row 유지, 기존 route 요청 8/8, owner lease 갱신과 명시적 create 거절은 확인한다. 그러나 신규 ChannelName 선택 제외와 `zlink.drain.state`의 serving→draining 전이를 같은 실행에서 대조하지 않는다. |
| OBS-C2 | `deferred` | actor 이동 뒤 ping은 확인하지만 bound-session push 연속성, moving 직전 pending request 결과와 `zlink.drain.actors.handed_off` 계기를 함께 확인하지 않는다. |
| OBS-C3 | `deferred` | 정상 request 뒤 Spot 유지, drain 뒤 신규 turn 거부, accepted turn 완료와 actor·STREAM barrier 이후 local Spot close·row 제거, stale handle의 숨은 원격 생성 금지와 명시적 local `GetOrCreate` 뒤 새 generation을 확인해야 한다. 현재 runner는 제거 대상인 기존 분기 시나리오를 실행하므로 이 고정 drain 회귀를 검증하지 않는다. |
| OBS-C4 | `deferred` | 별도 `Session`과 `Play` 역할에서 강제 종료와 public `closeReason`은 확인한다. versioned `session-closing` 제어 프레임의 `reason=server_drain`, terminal `ForceStopped` 결과와 `zlink.drain.forced{kind=session}`을 한 실행에서 대조하지 않는다. |
| OBS-C5 | `deferred` | rolling drain의 정상 종료는 확인하지만, 두 번째 drain에서 이미 draining인 peer가 handoff 대상에서 제외되어 eligible target이 0이 되는 증거와 `ForceStopped(deadline_exceeded)` terminal 결과를 함께 확인하지 않는다. |

실행: `./run_e2e.sh [all|flow|metrics|fanout|drain|handoff|force|policy|offnode]`

`deferred` 행의 누락은 내부 계기나 테스트 전용 API로 대신하지 않는다. 표에 적은 public metric,
terminal result와 역할별 evidence가 같은 실행에서 확인된 뒤에만 완료로 바꾼다.
