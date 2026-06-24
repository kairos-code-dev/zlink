# Java DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Java framework E2E가 현재 검증하는
항목과, public API 또는 harness 제어가 더 필요한 항목을 구분한다. registry probe는 registry
프로세스 안에서 public `ZLinkRegistryQuery`를 호출하고, consumer는 public `ZLinkClient` discovery
경로만 사용한다.

## 구현됨

- `DR-A1`: 단일 registry baseline에서 provider 2개가 보이고 messaging이 성공하는지 검증한다.
- `DR-A2`: 2 registry peer 합산에서 reg-2만 보는 consumer가 reg-1 provider로 messaging하는지
  검증한다.
- `DR-A3`: 3 registry peer 합산에서 서로 다른 registry에 붙은 provider 둘을 각 registry probe와
  consumer messaging으로 확인한다.
- `DR-C1`: registry 1대 다운 중 살아 있는 registry에 직접 광고된 provider로 messaging이 계속되고,
  죽은 registry probe는 bounded failure로 끝나는지 검증한다.

## public API/harness 대기

- `DR-A4`: 같은 rid 충돌과 tie-break를 안정적으로 고정하는 conflict harness가 필요하다.
- `DR-B1`: late-start registry가 기존 topology를 따라잡는지 보는 단계가 아직 없다.
- `DR-B2`: registry stop/recover 뒤 재합류를 검증하는 restart 단계가 아직 없다.
- `DR-B3`: registry flapping 중 중복/유실 없이 수렴하는지 보는 반복 harness가 필요하다.
- `DR-C2`: registry 장애 중 consumer fallback 순서와 실패 범위를 분리하는 runner가 아직 없다.
- `DR-C3`: 전체 registry 장애와 복구를 검증하는 restart orchestration이 필요하다.
- `DR-D1`: remote registry 배포 모델을 별도 process/topology로 고정하는 harness가 아직 없다.
- `DR-D2`: in-process registry 배포 모델을 별도 baseline으로 고정하는 harness가 아직 없다.
- `DR-D3`: remote와 in-process 혼합 cluster를 검증하는 배포 harness가 아직 없다.
- `DR-D4`: topology query와 messaging view의 장시간 일관성을 보는 harness가 아직 없다.
