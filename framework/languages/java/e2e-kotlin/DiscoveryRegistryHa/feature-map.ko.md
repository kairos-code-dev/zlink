# Kotlin DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다. registry probe는 registry 프로세스 안에서
public `ZLinkRegistryQuery`를 호출하고, consumer는 public `ZLinkClient` discovery 경로만 사용한다.

## 구현됨

- `DR-A1`: 단일 registry baseline에서 provider 2개가 보이고 messaging이 성공하는지 검증한다.
- `DR-A2`: 2 registry peer 합산에서 reg-2만 보는 consumer가 reg-1 provider로 messaging하는지 검증한다.
- `DR-A3`: 3 registry peer 합산에서 서로 다른 registry에 붙은 provider 둘을 각 registry probe와 consumer messaging으로 확인한다.
- `DR-A4`: 기존 provider와 같은 rid를 다른 registry와 endpoint에 추가 광고한 뒤, 그 registry만 보는 consumer messaging이 bounded time 안에 성공하는지 확인한다.
- `DR-B1`: reg-1과 provider가 먼저 뜬 뒤 reg-2를 늦게 붙이고, reg-2를 보는 consumer가 기존 provider로 messaging하는지 확인한다.
- `DR-B2`: reg-2를 정상 종료했다가 같은 endpoint로 다시 띄운 뒤 reg-2 query와 messaging이 회복되는지 확인한다.
- `DR-B3`: reg-2를 두 번 stop/start한 뒤 중복/유실 없이 provider view와 messaging이 수렴하는지 확인한다.
- `DR-C1`: registry 1대 다운 중 살아 있는 registry에 직접 광고된 provider로 messaging이 계속되고, 죽은 registry probe는 bounded failure로 끝나는지 검증한다.
- `DR-C2`: 죽였던 registry를 같은 endpoint로 다시 띄운 뒤 peer 합산 view와 messaging이 복구되는지 확인한다.
- `DR-C3`: 모든 registry process를 잠시 내렸다가 같은 endpoint로 복구한 뒤 provider 재광고 view와 messaging이 다시 수렴하는지 확인한다.
- `DR-D1`: registry와 provider를 한 process에 함께 띄운 embedded 배포 모델에서 discovery와 messaging을 확인한다.
- `DR-D2`: registry를 별도 process로 띄운 standalone 배포 모델에서 discovery와 messaging을 확인한다.
- `DR-D3`: embedded registry+provider process와 standalone registry/provider process를 peer로 묶은 혼합 cluster에서 peer 합산 view와 messaging을 확인한다.
- `DR-D4`: 같은 registry router를 in-process probe와 remote query client로 조회해 topology view가 같은 provider 집합을 반환하는지 확인한다.

## public API/harness 대기

- 전체 registry가 내려간 동안 이미 resolve된 기존 channel connection의 지속 동작은 같은 client를 유지하는 장기 실행 harness가 필요하다. 현재 DR-C3는 복구 후 재광고와 messaging 수렴을 검증한다.
