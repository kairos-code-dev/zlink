# Node.js DiscoveryRegistryHa E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

| Scenario | 상태 | 근거 |
|----------|------|------|
| DR-A1 | 구현 | 단일 registry에서 provider 2개 topology와 consumer discovery request, provider evidence를 검증한다. |
| DR-A2 | 구현 | 2 registry peer 합산에서 reg-2 `MemberPeers`가 reg-1 provider를 보고, reg-2만 보는 consumer request가 성공하는지 검증한다. |
| DR-A3 | 구현 | 3 registry peer 합산에서 모든 registry가 connected peer 2개와 reg-1/reg-3 provider `MemberPeers`를 보고, 각 registry만 보는 consumer request가 성공하는지 검증한다. |
| DR-A4 | 구현 | 같은 rid `api-a`를 다른 endpoint로 reg-1/reg-2에 광고한 뒤, reg-2 consumer request와 어느 provider든 marker evidence가 남는지 검증한다. |
| DR-B1 | 구현 | reg-1 provider를 유지한 채 reg-2/reg-3를 늦게 기동하고, 늦게 뜬 registry의 connected peer 상태, peer 합산 view, consumer request/provider evidence를 검증한다. |
| DR-B2 | 구현 | live+stopped registry endpoint를 함께 둔 consumer 실행 경로를 `run_e2e.sh DR-B2`로 추가했다. Node runtime은 등록된 channel client를 시작 시점에 준비하므로, consumer가 살아 있는 registry endpoint로 provider를 발견한 뒤 stopped endpoint가 있어도 request/evidence marker를 검증한다. |
| DR-B3 | 구현 | reg-2를 짧게 정지/재기동하는 flapping harness 뒤에 reg-2와 survivor reg-1의 connected peer 상태, peer 합산 view, consumer request/provider evidence를 검증한다. |
| DR-C1 | 구현 | reg-2를 강제 종료한 뒤 live reg-1의 member view, consumer request/provider evidence, dead reg-2 HTTP query bounded failure를 검증한다. |
| DR-C2 | 구현 | reg-2 강제 종료 후 같은 endpoint로 재기동하고, recovered reg-2의 connected peer 상태, peer 합산 view, consumer request/provider evidence를 검증한다. |
| DR-C3 | 구현 | 전체 registry를 중지한 동안 기존 consumer request가 유지되는지 확인하고, registry 재기동과 api-c 재광고 뒤 모든 registry topology와 recovered reg-2 consumer request/provider evidence를 검증한다. |
| DR-D1 | 구현 | embedded registry+provider 단일 프로세스와 embedded registry consumer request/provider evidence를 검증한다. |
| DR-D2 | 구현 | standalone registry + provider + consumer 배포에서 member view, consumer request, provider evidence를 검증한다. |
| DR-D3 | 구현 | embedded registry+provider와 standalone registry/provider를 peer cluster로 묶고, embedded registry의 peer 합산 view와 consumer request/provider evidence를 검증한다. |
| DR-D4 | 구현 | standalone registry의 in-process topology query와 remote query client probe의 topology snapshot이 같은지 검증한다. |

검증:

- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh`
  - 결과: full sweep aggregate `discovery-registry-ha e2e result=passed`
  - 최신 aggregate 로그 디렉터리: `logs/20260702-065342-62284`
  - 이전 aggregate 로그 디렉터리: `logs/20260702-050632-82209`
  - child 로그 디렉터리: `logs/20260702-050632-82215`(`DR-A1`), `logs/20260702-050637-82730`(`DR-A2`),
    `logs/20260702-050643-83236`(`DR-A3`), `logs/20260702-050650-83983`(`DR-A4`),
    `logs/20260702-050705-84912`(`DR-B1`), `logs/20260702-050712-85448`(`DR-B2`),
    `logs/20260702-050720-86021`(`DR-B3`), `logs/20260702-050727-86602`(`DR-C1`),
    `logs/20260702-050732-87169`(`DR-C2`), `logs/20260702-050738-87556`(`DR-C3`),
    `logs/20260702-050746-88176`(`DR-D1`), `logs/20260702-050751-88535`(`DR-D2`),
    `logs/20260702-050757-89039`(`DR-D3`), `logs/20260702-050803-90124`(`DR-D4`)
