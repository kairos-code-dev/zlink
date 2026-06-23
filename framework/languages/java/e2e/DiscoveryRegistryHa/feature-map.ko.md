# Java DiscoveryRegistryHa E2E feature map

이 디렉터리는 Config 6의 Java framework 검증이다. registry probe는 registry 프로세스 안에서
public `ZLinkRegistryQuery`를 호출하고, consumer는 public `ZLinkClient` discovery 경로만 사용한다.

## 구현됨

- DR-A1 단일 registry baseline: topology/member-peers에 provider 2개가 보이고 messaging이 성공한다.
- DR-A2 2 registry peer 합산: provider는 reg-1에만 광고하고, reg-2만 보는 consumer가 member-peers와
  messaging으로 provider를 확인한다.
- DR-A3 3 registry peer 합산: provider A는 reg-1, provider B는 reg-3에만 광고하고 세 registry probe가
  둘 다를 member로 보고 각 registry만 보는 consumer messaging이 성공한다.
- DR-C1 registry 1대 다운 중 지속: 살아 있는 registry에 직접 광고된 provider로 messaging이 계속되고,
  죽은 registry probe는 bounded failure로 끝난다.

## 후속 harness 필요

- DR-A4/DR-B3/DR-C3/DR-D3: 충돌·flapping·전체 장애·혼합 cluster는 더 강한 프로세스/link 제어가 필요하다.
- DR-B1/DR-B2/DR-C2/DR-D1/DR-D2/DR-D4: late-start, 정지/복구, 배포 모델, remote/in-process topology
  비교는 P1로 남겼다.
