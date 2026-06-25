# Node RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `RM-A1`: embedded registry와 provider 2개, consumer를 public NestJS 모듈로 띄운다. 두 provider가
  topology에서 `Ready`로 보인 뒤, consumer가 endpoint 없이 discovery만으로 provider를 찾아 request를
  처리한다.
- `RM-A4`: 같은 rid `api-a`의 provider v1을 종료하고 다른 endpoint의 provider v2를 띄운 뒤,
  consumer 재시작 없이 v2로 request가 전환되는지 확인한다.
- `RM-A6`: 같은 registry에 `rm.cross.api`와 `rm.cross.workflow` provider를 함께 광고하고,
  각 channel consumer request가 자기 provider로만 resolve되는지 확인한다.
- `RM-A2`: public NestJS channel client/server가 수동 endpoint로 연결하고 send handler까지 도달한다.
- `RM-C5`: handler 없는 request는 error reply와 observer `HandlerMissing/ReplyError`로 실패하고,
  handler 없는 send는 observer `HandlerMissing/Drop`으로 기록되며 이후 정상 request가 오염되지 않는다.
- `RM-C1`: RM-A1의 registry-resolved 연결 위에서 request reply와 send handler evidence를 모두 확인한다.
- `RM-C3`: 수동 multi-endpoint client가 provider 2개에 request를 분산하고, 두 provider evidence 합이
  전체 request 수와 일치하는지 확인한다.
- `RM-C4`: RM-A1의 registry-resolved 연결 위에서 timeout 뒤 정상 request 두 번이 late reply에
  오염되지 않는지 확인한다.
- `RM-B1`: registry discovery 연결에서 provider B를 추가한 뒤 consumer 재시작 없이 A/B가 모두
  routing 대상이 되는지 확인한다.
- `RM-B2`: provider B를 정상 종료한 뒤 topology에서 빠진 것을 확인하고, 이후 request가 provider A로만
  처리되는지 확인한다.
- `RM-C2`: route mesh public client가 target rid `api-b`로 보낸 request는 `api-b`에만 도달하고,
  없는 rid request는 public error로 실패하는지 확인한다.

## public API/harness 대기

- `RM-C7`: weighted 분산 Node runner와 marker가 아직 없다.
- `RM-C8`: payload size policy Node runner와 marker가 아직 없다.
- `RM-C9`: HWM/backpressure Node harness가 아직 없다.
