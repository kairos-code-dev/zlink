# Kotlin DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 Discovery/Registry HA 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목과,
`.NET` 기준 포팅 구조에서 아직 남은 항목을 구분한다. registry probe는 registry 프로세스 안에서
public `ZLinkRegistryQuery`를 호출하고, consumer는 public `ZLinkClient` discovery 경로만 사용한다.
현재 구현은 role Gradle project로 process를 나눴고 client scenario dispatcher, scenario ID별 실행
파일, shared message 타입, registry/provider/consumer/probe/embedded role support는 Kotlin으로
옮겼다. 파일별 재분류 상태는 `porting-inventory.ko.md`에 기록한다.

## 구현됨

- `DR-A1`: 단일 registry baseline에서 provider 2개가 보이고 messaging이 성공하는지 검증한다.
- `DR-A2`: 2 registry peer 합산에서 reg-2만 보는 consumer가 reg-1 provider로 messaging하는지 검증한다.
- `DR-A3`: 3 registry peer 합산에서 서로 다른 registry에 붙은 provider 둘을 각 registry probe와 consumer messaging으로 확인한다.
- `DR-A4`: 기존 provider와 같은 rid를 다른 registry와 endpoint에 추가 광고한 뒤, 그 registry만 보는 consumer messaging이 bounded time 안에 성공하는지 확인한다.
- `DR-B1`: reg-1과 provider가 먼저 뜬 뒤 reg-2를 늦게 붙이고, reg-2를 보는 consumer가 기존 provider로 messaging하는지 확인한다.
- `DR-B2`: reg-2를 정상 종료했다가 같은 endpoint로 다시 띄운 뒤 reg-2 query와 messaging이 회복되는지 확인한다.
- `DR-B3`: reg-2를 두 번 stop/start한 뒤 중복/유실 없이 provider view와 messaging이 수렴하는지 확인한다.
- `DR-C1`: registry 1대 다운 중 살아 있는 registry에 직접 광고된 provider로 messaging이 계속되고, 죽은 registry probe는 bounded failure로 끝나는지 검증한다.
- `DR-C2`: 죽였던 registry를 같은 endpoint로 다시 띄운 뒤 peer 합산 view와 messaging이 복구되는지 확인한다.
- `DR-C3`: 모든 registry process를 잠시 내렸다가 같은 endpoint로 복구한 뒤 provider 재광고 view와 messaging이 다시 수렴하는지 확인한다.
- `DR-D1`: registry와 provider를 한 process에 함께 띄운 embedded 배포 모델에서 discovery와 messaging을 확인한다.
- `DR-D2`: registry를 별도 process로 띄운 standalone 배포 모델에서 discovery와 messaging을 확인한다.
- `DR-D3`: embedded registry+provider process와 standalone registry/provider process를 peer로 묶은 혼합 cluster에서 peer 합산 view와 messaging을 확인한다.
- `DR-D4`: 같은 registry router를 in-process probe와 remote query client로 조회해 topology view가 같은 provider 집합을 반환하는지 확인한다.

## 포팅 구조 상태

현재 Kotlin DiscoveryRegistryHa E2E는 실행 가능한 동작 기준선이며, Gradle project와 process 역할은
`Client`, `Server/Registry`, `Server/Provider`, `Server/Consumer`, `Server/Probe`, `Server/Embedded`로 나뉘었다. 각 role process는
환경 변수 대신 CLI option으로 scenario, endpoint, log dir 입력을 받는다. client scenario/support
분리와 registry/provider/consumer/probe/embedded role support는 Kotlin file로 옮겼다. DR-C3는 같은
Consumer process를 유지한 채 registry outage 전/중/후 messaging을 확인한다.

## 검증 결과

- `2026-06-29`: `timeout 720s framework/languages/java/e2e-kotlin/DiscoveryRegistryHa/run_e2e.sh`
  실행 결과 `logs/20260629-180145-820678`에서 full runner가 통과했다.
- 통과 marker: `scenario DR-A1 passed`, `scenario DR-A2 passed`, `scenario DR-A3 passed`,
  `scenario DR-A4 passed`, `scenario DR-B1 passed`, `scenario DR-B2 passed`,
  `scenario DR-B3 passed`, `scenario DR-C1 passed`, `scenario DR-C2 passed`,
  `scenario DR-C3 passed`, `scenario DR-D1 passed`, `scenario DR-D2 passed`,
  `scenario DR-D3 passed`, `scenario DR-D4 passed`.
- 각 client mode stdout에는 `discovery-registry-ha kotlin e2e result=passed`가 남아 있다.
- `DR-C3`는 같은 Consumer process를 유지한 채 outage 전/중/후 client mode를 실행했고,
  `consumer-DR-C3-flow.log`에는 같은 consumer label의 `SENT`와 `REPLY_RECEIVED` 흐름이 이어진다.
- `DR-D4`는 `probe-DR-D4.stdout.log` remote Probe process와 `client-DR-D4.stdout.log`의
  `scenario DR-D4 passed providers=[api-a, api-b]`로 같은 provider 집합 비교를 확인했다.
- 이 결과는 현재 구현의 process 분리, CLI option parser 전환, client scenario/support Kotlin file
  분리, shared message와 registry/provider/consumer/probe/embedded role support Kotlin 전환 증거다.
