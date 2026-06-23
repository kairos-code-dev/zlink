# Java Monitoring E2E feature map

이 디렉터리는 Config 7의 Java framework 검증이다. 각 host는 자기 source를 public
`ZLinkMonitoringOptionsCustomizer`로 등록하고, public `ZLinkRuntimeEventHandler`에서 evidence를 기록한다.

## 구현됨

- MON-A2 registry 이벤트 관찰: registry host의 `ops-registry` source에서
  `STATUS_CHANGED`/`TOPOLOGY_CHANGED`/`SERVICE_SUMMARY_CHANGED`를 관찰한다.
- MON-A3 spot snapshot 이벤트 관찰: service host의 `monitoring.spot.node` source에서
  `STATUS_CHANGED`/`PEERS_CHANGED`/`SUBJECTS_CHANGED`를 관찰한다.

## Java public monitoring gap

- MON-A1 socket 이벤트 관찰은 현재 native 실행에서 `monitoring.api` source를 등록해도 service host에
  socket event evidence가 들어오지 않아 완료로 주장하지 않는다. channel messaging과 message-flow trace는
  정상이라, 남은 문제는 socket monitor event 발행/수신 경로로 분리된다.
- MON-A3의 timer failure 관찰과 MON-A5의 timer-stopped 관찰은 현재 Java monitoring runtime에 발행 경로가
  없다. enum은 존재하지만 `ZLinkMonitoringRuntime.pollSpot`은 status/peers/subjects snapshot diff만
  발행한다.
- MON-A4/MON-B1/MON-B2/MON-C1/MON-D1은 P1로 남겼다.
