# .NET DiscoveryRegistryHa E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| DR-A1 | 구현 | single registry marker가 있다. |
| DR-A2 | 구현 | two-registry asymmetric advertisement marker가 있다. |
| DR-A3 | 구현 | three-registry asymmetric advertisement marker가 있다. |
| DR-A4 | 미구현 | same-rid conflict marker가 없다. |
| DR-B1 | 미구현 | late-start registry join marker가 없다. |
| DR-B2 | 미구현 | registry stop with live endpoint marker가 없다. |
| DR-B3 | 미구현 | peer link flapping marker가 없다. |
| DR-C1 | 구현 | one registry down discovery marker가 있다. |
| DR-C2 | 미구현 | registry recovery rejoin marker가 없다. |
| DR-C3 | 미구현 | all registry outage/recovery marker가 없다. |
| DR-D1 | 미구현 | embedded registry deployment marker가 없다. |
| DR-D2 | 미구현 | standalone deployment contrast marker가 없다. |
| DR-D3 | 미구현 | embedded + standalone mixed cluster marker가 없다. |
| DR-D4 | 미구현 | in-process vs remote topology query marker가 없다. |
