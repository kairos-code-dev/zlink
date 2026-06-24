# Node DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Node framework E2E 상태를 정리한다. 현재
추적된 Node DiscoveryRegistryHa E2E runner/source가 없으므로 Node 전용 stdout marker로 구현 완료를
주장하지 않는다.

## 구현됨

- 없음.

## public API/harness 대기

- `DR-A1`: 단일 registry baseline Node runner와 marker가 아직 없다.
- `DR-A2`: 2 registry peer 합산 Node runner와 marker가 아직 없다.
- `DR-A3`: 3 registry peer 합산 Node runner와 marker가 아직 없다.
- `DR-A4`: 같은 rid 충돌 Node harness가 아직 없다.
- `DR-B1`: late-start registry Node harness가 아직 없다.
- `DR-B2`: registry stop/recover Node harness가 아직 없다.
- `DR-B3`: registry flapping Node harness가 아직 없다.
- `DR-C1`: registry 1대 다운 중 지속 Node runner와 marker가 아직 없다.
- `DR-C2`: registry 장애 중 fallback Node harness가 아직 없다.
- `DR-C3`: 전체 registry 장애와 복구 Node harness가 아직 없다.
- `DR-D1`: remote registry 배포 Node harness가 아직 없다.
- `DR-D2`: in-process registry 배포 Node harness가 아직 없다.
- `DR-D3`: remote/in-process 혼합 cluster Node harness가 아직 없다.
- `DR-D4`: topology query와 messaging view 일관성 Node harness가 아직 없다.
