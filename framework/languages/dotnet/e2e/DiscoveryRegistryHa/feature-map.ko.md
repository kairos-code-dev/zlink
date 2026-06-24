# .NET DiscoveryRegistryHa E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| DR-A1 | 구현 | single registry marker가 있다. |
| DR-A2 | 구현 | two-registry asymmetric advertisement marker가 있다. |
| DR-A3 | 구현 | three-registry asymmetric advertisement marker가 있다. |
| DR-A4 | 구현 | same-rid duplicate provider 광고 후 request가 stale 대기 없이 성공하는 marker가 있다. |
| DR-B1 | 구현 | late-start reg-2/reg-3 join 후 member query와 messaging marker가 있다. |
| DR-B2 | 구현 | consumer가 살아 있는 registry endpoint를 포함한 상태에서 messaging 지속 marker가 있다. |
| DR-B3 | 구현 | reg-2 반복 stop/restart flapping 후 member 합산과 survivor registry request marker가 있다. |
| DR-C1 | 구현 | one registry down discovery marker가 있다. |
| DR-C2 | 구현 | reg-2 shutdown 후 재기동, member query, messaging recovery marker가 있다. |
| DR-C3 | 구현 | 모든 live registry 종료 중 established channel 유지, reg-2 복구와 provider 재광고 후 request marker가 있다. |
| DR-D1 | 구현 | embedded registry+provider 단일 프로세스 배포 request marker가 있다. |
| DR-D2 | 구현 | standalone registry deployment messaging marker가 있다. |
| DR-D3 | 구현 | embedded registry/provider와 standalone reg-1 peer 혼합 cluster member 합산 및 request marker가 있다. |
| DR-D4 | 구현 | 같은 router endpoint에 대한 HTTP in-process query와 remote query client snapshot 비교 marker가 있다. |
