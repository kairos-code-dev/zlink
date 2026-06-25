# Kotlin RegistryMessaging E2E feature map

이 문서는 Config 1 Registry Messaging 공통 시나리오 중 Kotlin 전용 E2E 상태를 정리한다. Kotlin은
Java runtime 위의 wrapper를 공유하지만, runner와 scenario code는 Kotlin public framework API로
작성해 Kotlin 호출 표면에서 검증한다.

## 구현됨

- `RM-A1`: Kotlin client가 registry discovery로 provider를 찾아 request/reply를 수행한다.
- `RM-A2`: Kotlin client가 수동 endpoint로 `api-a`에 연결한다.
- `RM-A4`: 같은 rid provider 교체 뒤 Kotlin client가 새 instance로 request를 보낸다.
- `RM-A6`: Kotlin client가 API channel과 workflow channel discovery를 함께 확인한다.
- `RM-B1`: provider scale-out 뒤 Kotlin client가 두 provider 모두로 routing되는지 확인한다.
- `RM-B2`: provider scale-in 뒤 제거된 provider로 더 이상 routing되지 않는지 확인한다.
- `RM-C1`: Kotlin client request/send happy path를 확인한다.
- `RM-C2`: Kotlin route mesh client가 target rid request와 missing rid 실패를 확인한다.
- `RM-C3`: Kotlin client가 multi-endpoint 수동 연결에서 두 provider 모두를 사용한다.
- `RM-C4`: timeout 뒤 late reply가 후속 request를 오염시키지 않는지 확인한다.
- `RM-C5`: 미등록 packet request 실패와 send drop 뒤 정상 request 회복을 확인한다.
- `RM-C7`: server socket weight 설정 뒤 Kotlin client가 높은 weight provider 선호를 확인한다.
- `RM-C8`: 작은 payload와 큰 payload 왕복을 확인한다.
- `RM-C9`: 많은 slow request로 timeout/backpressure를 관찰하고 후속 request 회복을 확인한다.
