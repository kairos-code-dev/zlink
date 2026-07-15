# Java RuntimeMonitoring E2E feature map

이 문서는 Config 7 Runtime Monitoring 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 각 host는 자기 source를 public
`ZLinkMonitoringOptionsCustomizer`로 등록하고, public `ZLinkRuntimeEventHandler`에서 evidence를
기록한다. Client는 HTTP driver이고, framework channel traffic과 malformed connection trigger는
`Server/Trigger` role이 맡는다.

마지막 검증:

- 명령: `nice -n 10 timeout 420s ./run_e2e.sh all`
- 결과: passed
- 로그: `framework/languages/java/e2e/RuntimeMonitoring/logs/20260707-221130-3621759/`

## 구현됨

- `MON-A1` (차단): service host의 bare `monitoring.api` source를 사용한다. 정식 source인
  `monitoring.api.client`로 바꾼 집중 실행은 startup에서 "source is not configured"로 실패했으며,
  연결 해제도 유발하지 않아 현재 계약의 연결·해제 identity를 검증하지 못한다.
- `MON-A2`: service host의 `ops-locations` source에서 `STATUS_CHANGED`,
  `TOPOLOGY_CHANGED`, `SERVICE_SUMMARY_CHANGED`를 관찰한다.
- `MON-A3`: service host의 `monitoring.spot.mesh` source에서 `STATUS_CHANGED`,
  `PEERS_CHANGED`, `SUBJECTS_CHANGED`를 관찰하고, failing timer가 `TIMER_HANDLER_FAILED`를
  발행해도 channel messaging이 멈추지 않는지 확인한다.
- `MON-A4` (미구현): 현재 시나리오는 socket weight를 0/100으로 바꾸는 한 전이만 검증한다. 별도
  observer host가 정상 replacement, `SIGKILL` failover, weight 변경을 구분해 관찰하는 갱신 계약은
  구현하지 않았다.
- `MON-A5`: handshake 전용 public channel에 잘못된 TCP 연결을 보내 socket 전이를 관찰하고,
  location runtime/spot `STATUS_CHANGED`와 stop-on-unhandled timer의
  `TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION`을 함께 확인한다. 현재 Java native backend는 raw malformed
  연결을 `HANDSHAKE_FAILED`가 아니라 연결/해제 marker로 보고한다.
- `MON-B1`: service host의 socket source를 `CONNECTION_READY` kind로 필터링하고, evidence에
  필터에 포함한 kind만 기록되는지 확인한다.
- `MON-B2`: monitoring 등록 검증에서 비양수 polling interval은 구성 시점에 실패하고, 미존재
  socket/spot source는 host 시작 시점에 명확한 오류로 실패하는지 확인한다.
- `MON-C1`: monitoring event handler가 예외를 던져도 dispatcher가 예외를 격리하고, 그 뒤 channel
  messaging이 계속 성공하는지 확인한다.
- `MON-D1`: `svc-b`를 HTTP admin endpoint로 종료한 뒤 runner가 사용하는 `FilteredService` binary로
  재기동한다. 재기동된 service가 request를 처리하고, 살아 있는 observer service의 location runtime이
  down/up을 포함한 `TOPOLOGY_CHANGED` 전이를 계속 기록하는지 확인한다.

## public API/harness 대기

socket source registry가 capability가 붙은 정식 이름을 받아들이도록 Java runtime을 먼저 고쳐야 한다.
그 뒤 MON-A1과 MON-A4를 별도 observer topology로 다시 구현한다.
