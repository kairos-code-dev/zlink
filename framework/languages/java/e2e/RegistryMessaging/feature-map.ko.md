# Java RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Java framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. 실행 코드는 public Spring starter,
`ZLinkClient`, `ZLinkRouteClient`, channel builder만 사용한다.

## 구현됨

- `RM-A1`: registry discovery로 provider를 resolve하고 request를 보낸다.
- `RM-A2`: 수동 endpoint 연결로 provider에 직접 request를 보낸다.
- `RM-A4`: 같은 rid를 다른 endpoint로 교체한 뒤 follow-up request를 검증한다.
- `RM-A6`: 같은 registry 안에서 서로 다른 channel의 provider가 섞이지 않는지 검증한다.
- `RM-B1`: 실행 중 provider를 추가하고 두 provider로 분산되는지 검증한다.
- `RM-B2`: provider 하나를 정상 종료한 뒤 남은 provider로 request가 계속 성공하는지 검증한다.
- `RM-C1`: request와 send happy path를 함께 검증한다.
- `RM-C2`: route mesh에서 target rid request와 없는 rid 실패를 검증한다.
- `RM-C3`: 수동 multi-endpoint client/server channel에서 두 provider가 모두 처리하는지 검증한다.
- `RM-C4`: timeout 뒤 정상 request가 late reply에 오염되지 않는지 검증한다.
- `RM-C5`: 미등록 packet request 실패와 send drop 이후 정상 request 복구를 검증한다.
- `RM-C7`: provider 시작 시 public runtime socket option으로 weight 75/25를 설정하고, manual
  multi-endpoint client 요청이 높은 weight provider 쪽으로 더 많이 분산되는지 검증한다.
- `RM-C8`: public typed client로 소형, 대형, near-large payload 왕복을 검증한다. max size 초과
  거부는 framework channel runtime의 max message size 적용이 public typed channel 경로에 배선된
  뒤 같은 scenario의 추가 marker로 확장한다.
- `RM-C9`: 느린 handler에 다량 request를 짧은 timeout으로 동시에 제출해 bounded timeout/public
  error를 관측하고, 적체 뒤 정상 request가 다시 성공하는지 검증한다.
