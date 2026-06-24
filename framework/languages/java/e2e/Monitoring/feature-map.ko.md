# Java Monitoring E2E feature map

이 문서는 Config 7 Runtime Monitoring 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 각 host는 자기 source를 public
`ZLinkMonitoringOptionsCustomizer`로 등록하고, public `ZLinkRuntimeEventHandler`에서 evidence를
기록한다.

## 구현됨

- `MON-A1`: service host의 `monitoring.api` socket source에서 `CONNECTED` 또는
  `CONNECTION_READY`를 관찰한다.
- `MON-A2`: registry host의 `ops-registry` source에서 `STATUS_CHANGED`,
  `TOPOLOGY_CHANGED`, `SERVICE_SUMMARY_CHANGED`를 관찰한다.
- `MON-A3`: service host의 `monitoring.spot.mesh` source에서 `STATUS_CHANGED`,
  `PEERS_CHANGED`, `SUBJECTS_CHANGED`를 관찰하고, failing timer가 `TIMER_HANDLER_FAILED`를
  발행해도 channel messaging이 멈추지 않는지 확인한다.
- `MON-B1`: service host의 socket source를 `CONNECTION_READY` kind로 필터링하고, evidence에
  필터에 포함한 kind만 기록되는지 확인한다.
- `MON-B2`: monitoring 등록 검증에서 비양수 polling interval은 구성 시점에 실패하고, 미존재
  socket/spot source는 host 시작 시점에 명확한 오류로 실패하는지 확인한다.

## public API/harness 대기

- `MON-A4`: failover/drain 전이를 socket/registry monitoring event로 묶어 보는 runner가 아직 없다.
- `MON-A5`: handshake failure, status transition, timer stopped kind를 안정적으로 유발하는 trigger가
  아직 없다.
- `MON-C1`: event handler 실패 격리와 runtime error sink 보고를 단언하는 scenario가 아직 없다.
- `MON-D1`: 장애/복구 반복 중 monitoring event 연속성을 보는 장시간 harness가 아직 없다.
