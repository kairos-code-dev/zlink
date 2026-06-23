# Java RegistryMessaging E2E feature map

이 디렉터리는 Config 1의 Java framework 검증이다. 실행 시나리오는 public Spring starter,
`ZLinkClient`, `ZLinkRouteClient`, channel builders만 사용한다.

## 구현됨

- RM-A1 registry 자동 연결 + rid 자동 resolve
- RM-A2 수동 endpoint 연결
- RM-A4 같은 rid, 다른 endpoint failover
- RM-A6 cross-channel discovery
- RM-B1 scale-out
- RM-B2 scale-in / graceful drain
- RM-C1 request / send happy path
- RM-C2 targeted request by rid
- RM-C3 다중 provider 분산
- RM-C4 timeout과 late reply 비오염
- RM-C5 미등록 packet negative path
- RM-C8 메시지 크기 다양성 중 public typed client 왕복

## Java public API 미지원 또는 부분 지원

- RM-C6 dealer mesh peer request: 현재 Java `DealerMeshChannelBuilder`는 client endpoint 등록만
  공개하고 server bind API를 공개하지 않는다. 따라서 provider를 public API만으로 dealer mesh
  peer로 띄우는 E2E는 작성하지 않았다.
- RM-C7 weighted 분산: Java channel builders에는 server socket weight 설정 API가 공개되어 있지
  않다.
- RM-C8 상한 초과 거부: framework channel runtime의 max message size 적용이 public typed
  channel 경로에 배선되어 있지 않아 왕복만 검증한다.
