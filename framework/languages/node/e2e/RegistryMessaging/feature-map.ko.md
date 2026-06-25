# Node RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RM-A1`: embedded registry와 provider 2개, consumer를 public NestJS 모듈로 띄운다. 두 provider가
  topology에서 `Ready`로 보인 뒤, consumer가 endpoint 없이 discovery만으로 provider를 찾아 request를
  처리한다.
- `RM-A2`: public NestJS channel client/server가 수동 endpoint로 연결하고 send handler까지 도달한다.
- `RM-C5`: handler 없는 request는 error reply와 observer `HandlerMissing/ReplyError`로 실패하고,
  handler 없는 send는 observer `HandlerMissing/Drop`으로 기록되며 이후 정상 request가 오염되지 않는다.
- `RM-C1`: RM-A1의 registry-resolved 연결 위에서 request reply와 send handler evidence를 모두 확인한다.

## public API/harness 대기

- `RM-A4`: 같은 rid endpoint 교체 Node runner와 marker가 아직 없다.
- `RM-A6`: cross-channel discovery Node runner와 marker가 아직 없다.
- `RM-B1`: provider scale-out Node runner와 marker가 아직 없다.
- `RM-B2`: provider scale-in과 graceful drain Node runner와 marker가 아직 없다.
- `RM-C2`: target rid route request Node runner와 marker가 아직 없다.
- `RM-C3`: multi-endpoint 분산 Node runner와 marker가 아직 없다.
- `RM-C4`: registry-resolved 연결 위에서 timeout 뒤 late reply 비오염을 검증하는 Node marker가 아직 없다.
- `RM-C7`: weighted 분산 Node runner와 marker가 아직 없다.
- `RM-C8`: payload size policy Node runner와 marker가 아직 없다.
- `RM-C9`: HWM/backpressure Node harness가 아직 없다.
