# .NET DiscoveryRegistryHa E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| DR-A1 | 구현 | single registry marker가 있다. |
| DR-A2 | 구현 | two-registry asymmetric advertisement marker가 있다. |
| DR-A3 | 구현 | three-registry asymmetric advertisement marker가 있다. |
| DR-A4 | 미구현 | same-rid conflict marker가 없다. |
| DR-B1 | 구현 | late-start reg-2/reg-3 join 후 member query와 messaging marker가 있다. |
| DR-B2 | 구현 | consumer가 살아 있는 registry endpoint를 포함한 상태에서 messaging 지속 marker가 있다. |
| DR-B3 | 미구현 | peer link flapping marker가 없다. |
| DR-C1 | 구현 | one registry down discovery marker가 있다. |
| DR-C2 | 구현 | reg-2 shutdown 후 재기동, member query, messaging recovery marker가 있다. |
| DR-C3 | 미구현 | all registry outage/recovery marker가 없다. |
| DR-D1 | 미구현 | embedded registry deployment marker가 없다. |
| DR-D2 | 구현 | standalone registry deployment messaging marker가 있다. |
| DR-D3 | 미구현 | embedded + standalone mixed cluster marker가 없다. |
| DR-D4 | 구현 | 같은 router endpoint에 대한 HTTP in-process query와 remote query client snapshot 비교 marker가 있다. |
