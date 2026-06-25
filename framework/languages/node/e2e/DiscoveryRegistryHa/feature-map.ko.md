# Node DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Node framework E2E 상태를 정리한다.

## 구현됨

- `DR-A1`: standalone registry 한 대에 provider 2개와 consumer를 붙이고, public topology query에서
  두 provider가 `Ready`로 보인 뒤 discovery 기반 request가 처리되는지 확인한다.
- `DR-A2`: registry 2대를 peer로 묶고 provider는 `reg-1`에만 광고한다. `reg-2`의 public
  `memberPeers(...)`가 provider를 합산하고, `reg-2`만 보는 consumer가 request에 성공하는지
  확인한다.
- `DR-A3`: registry 3대를 peer로 묶고 provider 2개를 서로 다른 registry에 광고한다. 모든 registry의
  public `memberPeers(...)`가 두 provider를 합산하고, 나머지 registry만 보는 consumer가 두 provider로
  request를 보낼 수 있는지 확인한다.
- `DR-D2`: registry만 별도 context로 띄운 standalone 배포에서 discovery와 messaging이 동작하는지
  확인한다.
- `DR-D4`: 같은 registry router endpoint를 in-process `ZLinkRegistryQuery`와 remote
  `ZLinkRegistryQueryClient`로 조회했을 때 동일한 topology snapshot을 반환하는지 확인한다.

## public API/harness 대기

- `DR-A4`: 같은 rid 충돌은 public `memberPeers(...)`로 합산 view를 고정해야 한다. 현재 Node runner
  시도에서는 같은 rid를 서로 다른 registry에 광고했을 때 peer 쪽 `memberPeers(...)`가 안정적인
  두-endpoint view를 제공하지 않아 완료 marker로 올리지 않는다.
- `DR-B1`: 늦게 뜨는 registry endpoint를 consumer가 기동 전부터 들고 있는 상태에서, 기존
  discovery/messaging과 late-start 후 messaging을 같은 consumer 재시작 없이 안정적으로 증명하는
  Node marker가 아직 없다.
- `DR-B2`: registry stop/recover Node harness가 아직 없다.
- `DR-B3`: registry flapping Node harness가 아직 없다.
- `DR-C1`: registry 1대 다운 중 지속 Node runner와 marker가 아직 없다.
- `DR-C2`: registry 장애 중 fallback Node harness가 아직 없다.
- `DR-C3`: 전체 registry 장애와 복구 Node harness가 아직 없다.
- `DR-D1`: registry와 service를 한 embedded application으로 띄우는 Node marker가 아직 없다. registry
  context와 service context를 같은 Node process에 따로 띄우는 검증은 embedded 배포 완료로 보지 않는다.
- `DR-D3`: embedded registry/service node와 standalone registry를 섞은 cluster marker가 아직 없다.
