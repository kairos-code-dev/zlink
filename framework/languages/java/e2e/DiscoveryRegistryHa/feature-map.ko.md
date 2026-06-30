# Java DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Java framework E2E가 현재 검증하는
항목과, public API 또는 harness 제어가 더 필요한 항목을 구분한다. registry probe는 registry
프로세스 또는 별도 probe 프로세스에서 public registry query API를 호출하고, consumer는 public
`ZLinkClient` discovery 경로만 사용한다.

마지막 검증:

- `../../gradlew --project-cache-dir "$HOME/.cache/zlink/java-e2e/DiscoveryRegistryHa-gradle-cache" --no-daemon installDist --stacktrace`
  - 결과: 성공
- `./run_e2e.sh`
  - 로그: `logs/20260629-164645-539461`
  - 결과: `DR-A1`, `DR-A2`, `DR-A3`, `DR-A4`, `DR-B1`, `DR-D1`, `DR-D2`, `DR-D3`, `DR-D4` 통과 후
    `DR-B2`, `DR-B3`를 gap marker로 기록했다. `DR-C1`은 살아 있는 registry의 member query가
    `api-a`를 bounded timeout 안에 보지 못해 `java-discovery-survivor-member-timeout` gap으로
    남겼다. `DR-C2`는 recovered reg-2 member view가 provider를 다시 보지 못해
    `java-discovery-recovered-registry-member-timeout` gap으로 남겼다. `DR-C3`는 registry 전체
    outage 전/중 같은 consumer process로 messaging이 유지되는지 확인하고, 복구 후 재광고와
    messaging 수렴 검증까지 통과했다.

## 구현됨

- `DR-A1`: 단일 registry baseline에서 provider 2개가 보이고 messaging이 성공하는지 검증한다.
- `DR-A2`: 2 registry peer 합산에서 reg-2만 보는 consumer가 reg-1 provider로 messaging하는지
  검증한다.
- `DR-A3`: 3 registry peer 합산에서 서로 다른 registry에 붙은 provider 둘을 각 registry probe와
  consumer messaging으로 확인한다.
- `DR-A4`: 기존 provider와 같은 rid를 다른 registry와 endpoint에 추가 광고한 뒤, 그 registry만
  보는 consumer messaging이 bounded time 안에 성공하는지 확인한다.
- `DR-B1`: reg-1과 provider가 먼저 뜬 뒤 reg-2를 늦게 붙이고, reg-2를 보는 consumer가 기존
  provider로 messaging하는지 확인한다.
- `DR-C3`: 모든 registry process를 잠시 내린 동안 기존 consumer process의 messaging이 유지되는지
  확인하고, 같은 endpoint로 registry와 provider를 복구한 뒤 재광고 view와 messaging이 다시
  수렴하는지 확인한다.
- `DR-D1`: registry와 provider를 한 process에 함께 띄운 embedded 배포 모델에서 discovery와
  messaging을 확인한다.
- `DR-D2`: registry를 별도 process로 띄운 standalone 배포 모델에서 discovery와 messaging을 확인한다.
- `DR-D3`: embedded registry+provider process와 standalone registry/provider process를 peer로 묶은
  혼합 cluster에서 peer 합산 view와 messaging을 확인한다.
- `DR-D4`: 같은 registry router를 registry process의 in-process query와 별도 probe process의 remote
  query client로 조회해 topology view가 같은 provider 집합을 반환하는지 확인한다.

## public API/harness 대기

- `DR-B2`: provider가 reg-1/reg-2에 광고된 뒤 reg-2가 내려가면, consumer가 살아 있는 reg-1만
  configured한 상태에서도 request가 provider에 도달하지 못하고 timeout난다. `logs/repro-b2-20260629-164320-530103`
  재현에서 consumer flow는 send만 반복되고 provider flow는 남지 않았다. 살아 있는 endpoint 기준
  지속은 Java discovery runtime 쪽 동작 정리가 필요하므로 현재 runner는
  `java-discovery-dead-registry-timeout` gap marker를 남긴다.
- `DR-B3`: reg-2 stop/start flapping 후 recovered registry의 member view가 provider를 다시 보지
  못해 `java-discovery-peer-flap-member-timeout` gap marker를 남긴다. peer flap 후 수렴 보장은
  Java registry/discovery runtime 쪽 동작 확인이 필요하다.
- `DR-C1`: reg-2를 강제 종료한 뒤 살아 있는 reg-1의 `memberPeers`가 reg-1에 직접 광고된
  `api-a`도 bounded timeout 안에 보지 못해 `java-discovery-survivor-member-timeout` gap marker를
  남긴다. 공통 시나리오가 살아 있는 registry의 `MemberPeersAsync` 지속을 요구하므로, messaging만
  통과시키는 완화 검증으로 처리하지 않는다.
- `DR-C2`: 죽였던 reg-2를 같은 endpoint로 다시 띄운 뒤 recovered registry의 member view가
  provider를 다시 보지 못해 `java-discovery-recovered-registry-member-timeout` gap marker를
  남긴다. 복구 후 peer broadcast 재구독과 합산 view 재구성은 Java registry/discovery runtime 쪽
  동작 확인이 필요하다.
