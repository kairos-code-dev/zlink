# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| MON-A1 | 구현 | socket event observation marker가 있다. |
| MON-A2 | 구현 | registry event observation marker가 있다. |
| MON-A3 | 구현 | spot event observation marker가 있다. |
| MON-A4 | 구현 | drain/restore 중 client socket의 peer admission 전이와 registry topology marker가 있다. |
| MON-A5 | 구현 | registry/spot status, timer stopped, malformed connection socket marker가 있다. 현재 backend는 raw malformed connection을 `Internal`로 관측한다. |
| MON-B1 | 구현 | socket source를 `ConnectionReady`로 제한한 service marker가 있다. |
| MON-B2 | 구현 | 중복 source, 비양수 interval, missing spot/socket source startup validation marker가 있다. |
| MON-C1 | 구현 | monitoring handler 예외가 task failure로 보고되고 이후 messaging이 계속 동작하는 marker가 있다. |
| MON-D1 | 구현 | service stop/restart 반복 중 registry topology와 재시작 후 request marker가 있다. |
