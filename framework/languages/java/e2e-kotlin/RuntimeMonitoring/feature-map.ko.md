# Kotlin RuntimeMonitoring E2E feature map

이 문서는 Config 7 Runtime Monitoring 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 각 host는 자기 source를 public
`ZLinkMonitoringOptionsCustomizer`로 등록하고, public `ZLinkRuntimeEventHandler`에서 evidence를
기록한다.

현재 `MON-A1`, `MON-A2`, `MON-A3`, `MON-A4`, `MON-A5`, `MON-B1`, `MON-B2`, `MON-C1`, `MON-D1` client scenario는
`Client/src/main/kotlin/.../client/scenarios/` 아래 Kotlin 파일로 분리되어 있고,
Client는 framework runtime에 참여하지 않는 plain JVM HTTP/evidence driver다. framework request가 필요한
흐름은 Trigger HTTP endpoint가 public framework client 또는 transient framework lifecycle로 수행한다.
`logs/20260704-043031-38623` full runner에서 `Client` module binary로 통과했다. Service,
FilteredService, ThrowingService, Trigger role은 각각 `Server/Service`,
`Server/FilteredService`, `Server/ThrowingService`, `Server/Trigger` module binary로 실행한다.
registry role은 Redis location store 전환 뒤 제거했다.
`MON-A4`는 Service admin endpoint가 public runtime socket weight를 0으로 바꾸고, Trigger socket
monitoring evidence의 `PEER_ADMISSION_CHANGED`, Service admin evidence, location `TOPOLOGY_CHANGED`
evidence를 함께 확인한다.
`MON-B2` client scenario는 Trigger HTTP endpoint를 호출해 중복 socket source, 비양수 polling interval,
미존재 socket source, 미존재 spot source 등록 실패를 확인한다. message/evidence contract는 `Shared`
Gradle module로 분리했다.

## 구현됨

- `MON-A1`: Client가 Trigger의 disconnect request endpoint를 호출하고, Trigger가 transient public framework client로 Service에 request를 보낸 뒤 service host의 `monitoring.api` socket source에서 `CONNECTED` 또는 `CONNECTION_READY`를 관찰한다.
- `MON-A2`: service host의 `ops-locations` source에서 `STATUS_CHANGED`, `TOPOLOGY_CHANGED`, `SERVICE_SUMMARY_CHANGED`를 관찰한다.
- `MON-A3`: service host의 `monitoring.spot.mesh` source에서 `STATUS_CHANGED`, `PEERS_CHANGED`, `SUBJECTS_CHANGED`를 관찰하고, failing timer가 `TIMER_HANDLER_FAILED`를 발행해도 channel messaging이 멈추지 않는지 확인한다.
- `MON-A4`: service host의 public runtime socket weight를 0으로 바꿨다가 복구하고, trigger host의 client socket source에서 `PEER_ADMISSION_CHANGED`를 관찰한다. Service admin evidence와 location topology evidence도 함께 확인한다.
- `MON-A5`: handshake 전용 public channel에 잘못된 TCP 연결을 보내 socket 전이를 관찰하고, location/spot `STATUS_CHANGED`와 stop-on-unhandled timer의 `TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION`을 함께 확인한다. 현재 Java/Kotlin native backend는 raw malformed 연결을 `HANDSHAKE_FAILED`가 아니라 연결/해제 marker로 보고한다.
- `MON-B1`: Client가 Trigger의 service-b request endpoint를 호출하고, filtered service host의 socket source를 `CONNECTION_READY` kind로 필터링하며, evidence에 필터에 포함한 kind만 기록되는지 확인한다.
- `MON-B2`: client가 Trigger HTTP endpoint를 호출해 monitoring 등록 검증을 실행한다. 중복 socket source와 비양수 polling interval은 구성 시점에 실패하고, 미존재 socket/spot source는 host 시작 시점에 명확한 오류로 실패하는지 확인한다.
- `MON-C1`: Client가 Trigger의 throwing-service request endpoint를 호출한다. throwing service host의 monitoring event handler가 예외를 던져도 dispatcher가 예외를 격리하고, 그 뒤 channel messaging이 계속 성공하는지 확인한다.
- `MON-D1`: service-b 역할의 FilteredService를 HTTP endpoint로 종료하고 같은 binary와 endpoint로 재시작한 뒤, Trigger가 service-b endpoint만 가진 transient framework client로 request를 보내 restarted service가 응답하는지 확인한다. location `TOPOLOGY_CHANGED` evidence와 restarted service socket evidence도 함께 확인한다.
