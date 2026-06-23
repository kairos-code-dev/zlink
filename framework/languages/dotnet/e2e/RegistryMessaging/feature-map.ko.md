# .NET RegistryMessaging E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| RM-A1 | 구현 | registry resolve 기반 request marker가 있다. |
| RM-A2 | 구현 | 수동 endpoint request marker가 있다. |
| RM-A4 | 구현 | 같은 rid failover marker가 있다. |
| RM-A6 | 구현 | profile/workflow channel을 같은 registry에 광고하고, 각 channel request가 자기 provider evidence에만 남는지 검증한다. |
| RM-B1 | 구현 | scale-out marker가 있다. |
| RM-B2 | 구현 | scale-in / graceful drain marker가 있다. |
| RM-C1 | 구현 | request / send happy path marker가 있다. |
| RM-C2 | 구현 | targeted request by rid marker가 있다. |
| RM-C3 | 구현 | 다중 provider 분산 marker가 있다. |
| RM-C4 | 구현 | timeout 뒤 late reply 비오염 marker가 있다. |
| RM-C5 | 구현 | 미등록 packet 처리 marker가 있다. |
| RM-C6 | 구현 | dealer mesh peer request marker가 있다. |
| RM-C7 | 미구현 | weighted 분산 marker가 없다. |
| RM-C8 | 미구현 | 메시지 크기 다양성 marker가 없다. |
| RM-C9 | 미구현 | backpressure / HWM 포화 marker가 없다. |
