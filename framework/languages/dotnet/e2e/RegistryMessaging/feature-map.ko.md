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
| RM-C7 | 구현 | build-time weight 75/25 provider를 직접 연결하고 high-weight provider가 더 많이 처리하는 marker가 있다. |
| RM-C8 | 구현 | 1B, 4KiB, 256KiB, 1MiB payload 왕복 hash/length marker가 있다. MaxMessageSize 초과 거부는 framework channel runtime 미배선으로 별도 한계다. |
| RM-C9 | public API/harness 대기 | framework channel runtime이 HWM 옵션을 live socket에 적용하지 않아 public E2E에서 포화 backpressure를 직접 만들 수 없다. |
