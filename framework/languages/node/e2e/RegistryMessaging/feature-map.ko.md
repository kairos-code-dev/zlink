# Node RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RM-A2`: public NestJS channel client/server가 수동 endpoint로 연결하고 send handler까지 도달한다.

## public API/harness 대기

- `RM-A1`: registry discovery Node runner와 marker가 아직 없다.
- `RM-A4`: 같은 rid endpoint 교체 Node runner와 marker가 아직 없다.
- `RM-A6`: cross-channel discovery Node runner와 marker가 아직 없다.
- `RM-B1`: provider scale-out Node runner와 marker가 아직 없다.
- `RM-B2`: provider scale-in과 graceful drain Node runner와 marker가 아직 없다.
- `RM-C1`: registry-resolved 연결 위의 request/send Node runner와 marker가 아직 없다.
- `RM-C2`: target rid route request Node runner와 marker가 아직 없다.
- `RM-C3`: multi-endpoint 분산 Node runner와 marker가 아직 없다.
- `RM-C4`: timeout 뒤 late reply 비오염 Node runner와 marker가 아직 없다.
- `RM-C5`: 미등록 packet negative path Node runner와 marker가 아직 없다.
- `RM-C7`: weighted 분산 Node runner와 marker가 아직 없다.
- `RM-C8`: payload size policy Node runner와 marker가 아직 없다.
- `RM-C9`: HWM/backpressure Node harness가 아직 없다.
