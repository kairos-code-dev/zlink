# .NET RuntimeMonitoring E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-7-monitoring.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| MON-A1 | 구현 | socket event observation marker가 있다. |
| MON-A2 | 구현 | registry event observation marker가 있다. |
| MON-A3 | 구현 | spot event observation marker가 있다. |
| MON-A4 | 미구현 | availability transition marker가 없다. |
| MON-A5 | 미구현 | remaining fixed event kinds marker가 없다. |
| MON-B1 | 미구현 | event kind filter marker가 없다. |
| MON-B2 | 미구현 | monitoring registration validation marker가 없다. |
| MON-C1 | 미구현 | event dispatch failure isolation marker가 없다. |
| MON-D1 | 미구현 | repeated failure/recovery observation marker가 없다. |
