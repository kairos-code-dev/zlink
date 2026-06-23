# Java Monitoring E2E feature map

이 디렉터리는 Config 7의 Java framework 검증이다. 각 host는 자기 source를 public
`ZLinkMonitoringOptionsCustomizer`로 등록하고, public `ZLinkRuntimeEventHandler`에서 evidence를 기록한다.

## 구현됨

- MON-A2 registry 이벤트 관찰: registry host의 `ops-registry` source에서
  `STATUS_CHANGED`/`TOPOLOGY_CHANGED`/`SERVICE_SUMMARY_CHANGED`를 관찰한다.
- MON-A1 socket 이벤트 관찰: service host의 `monitoring.api` source에서
  `CONNECTED` 또는 `CONNECTION_READY`를 관찰한다. native `ACCEPTED`/`LISTENING`은 framework public
  kind인 `CONNECTED`로 정규화한다.
- MON-A3 spot snapshot 이벤트 관찰: service host의 `monitoring.spot.node` source에서
  `STATUS_CHANGED`/`PEERS_CHANGED`/`SUBJECTS_CHANGED`를 관찰한다.
- MON-A3 timer failure 이벤트 관찰: service host의 failing timer가 `TIMER_HANDLER_FAILED`를 발행하고,
  timer handler 실패가 channel messaging을 멈추지 않는지 함께 확인한다.

## Java public monitoring gap

- MON-A5의 timer-stopped 관찰은 P1로 남겼다.
- MON-A4/MON-B1/MON-B2/MON-C1/MON-D1은 P1로 남겼다.
