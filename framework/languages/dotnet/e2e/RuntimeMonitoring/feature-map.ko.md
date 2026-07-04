# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| MON-A1 | 구현 | socket event observation marker가 있다. |
| MON-A2 | 구현 | `location-runtime` source가 `TopologyChanged`와 `ServiceSummaryChanged` marker를 남기고, topology/summary payload가 비어 있지 않음을 확인한다. |
| MON-A3 | 구현 | spot event observation marker가 있다. |
| MON-A4 | 구현 | drain/restore 중 trigger socket의 `PeerAdmissionChanged`, service admin drain marker, service의 `location-runtime` `TopologyChanged` marker를 함께 확인한다. |
| MON-A5 | 구현 | socket `HandshakeFailed` 또는 현재 backend의 `Internal`, `location-runtime` `StatusChanged`, spot `StatusChanged`, spot `TimerStoppedAfterUnhandledException` marker가 있다. |
| MON-B1 | 구현 | socket source를 `ConnectionReady`로 제한한 service marker가 있다. |
| MON-B2 | 구현 | 중복 source, 비양수 interval, missing spot/socket source startup validation marker가 있다. |
| MON-C1 | 구현 | monitoring handler 예외가 task failure로 보고되고 이후 messaging이 계속 동작하는 marker가 있다. |
| MON-D1 | 구현 | `svc-b` stop/restart 뒤 request가 재시작된 service에서 처리되고, observer의 `location-runtime` `TopologyChanged` marker가 remove/re-add 흐름을 포함해 3회 이상 관측된다. |
