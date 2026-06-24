# Node RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Node framework E2E 상태를 정리한다. 현재
추적된 Node framework E2E runner/source가 없으므로 Node 전용 stdout marker로 구현 완료를 주장하지
않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `RM-A1`: registry discovery Node runner와 marker가 아직 없다.
- `RM-A2`: 수동 endpoint 연결 Node runner와 marker가 아직 없다.
- `RM-A4`: 같은 rid endpoint 교체 Node runner와 marker가 아직 없다.
- `RM-A6`: cross-channel discovery Node runner와 marker가 아직 없다.
- `RM-B1`: provider scale-out Node runner와 marker가 아직 없다.
- `RM-B2`: provider scale-in과 graceful drain Node runner와 marker가 아직 없다.
- `RM-C1`: request/send happy path Node runner와 marker가 아직 없다.
- `RM-C2`: target rid route request Node runner와 marker가 아직 없다.
- `RM-C3`: multi-endpoint 분산 Node runner와 marker가 아직 없다.
- `RM-C4`: timeout 뒤 late reply 비오염 Node runner와 marker가 아직 없다.
- `RM-C5`: 미등록 packet negative path Node runner와 marker가 아직 없다.
- `RM-C6`: dealer mesh peer request Node runner와 marker가 아직 없다.
- `RM-C7`: weighted 분산 Node runner와 marker가 아직 없다.
- `RM-C8`: payload size policy Node runner와 marker가 아직 없다.
- `RM-C9`: HWM/backpressure Node harness가 아직 없다.
