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
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022102-2343678`
  - 통과 scenario: `DR-A1`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A2`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022108-2344016`
  - 통과 scenario: `DR-A2`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A3`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022113-2344464`
  - 통과 scenario: `DR-A3`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A4`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022120-2344980`
  - 통과 scenario: `DR-A4`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B1`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022134-2345758`
  - 통과 scenario: `DR-B1`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B3`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022140-2346239`
  - 통과 scenario: `DR-B3`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-C1`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022148-2346991`
  - 통과 scenario: `DR-C1`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-C2`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022151-2347457`
  - 통과 scenario: `DR-C2`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-C3`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022157-2347999`
  - 통과 scenario: `DR-C3`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D1`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022206-2348746`
  - 통과 scenario: `DR-D1`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D2`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022211-2349015`
  - 통과 scenario: `DR-D2`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D3`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022216-2349345`
  - 통과 scenario: `DR-D3`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D4`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-022220-2349869`
  - 통과 scenario: `DR-D4`
- `timeout 420s framework/languages/node/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B2`
  - 결과: `discovery-registry-ha e2e result=passed`
  - 최신 확인 로그 디렉터리: `logs/20260630-043800-2668198`
  - 통과 scenario: `DR-B2`
