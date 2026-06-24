# C++ Discovery/Registry HA E2E feature map

이 문서는 Config 6 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 구현한 시나리오

- `DR-A1`: standalone registry 1대, provider 1대, consumer 1대로 provider discovery와
  messaging baseline을 검증한다.
- `DR-A2`: registry 2대를 peer로 묶고 provider는 `reg-1`에만 광고한다. `reg-2`의 colocated
  registry evidence가 `member_peers`를 보고하고, `reg-2`만 보는 consumer가 messaging에 성공하는지
  검증한다.
- `DR-A3`: registry 3대를 peer로 묶고 provider A는 `reg-1`, provider B는 `reg-3`에만 광고한다.
  세 registry의 colocated evidence가 두 provider를 모두 보고하고, 각 registry만 보는 consumer가
  두 provider에 모두 request를 성공시키는지 검증한다.
- `DR-B1`: 미리 peer endpoint를 선언한 뒤 `reg-2`를 늦게 시작해, late-start registry가
  기존 provider를 `member_peers`로 합산하고 그 registry만 보는 consumer가 messaging에
  성공하는지 검증한다.
- `DR-B2`: provider가 살아 있는 registry에도 직접 광고된 상태에서 registry 한 대를 정지하고,
  살아 있는 registry endpoint로 discovery와 messaging이 계속 성공하는지 검증한다.
- `DR-C1`: registry 2대 중 하나를 종료한 뒤 살아 있는 registry endpoint로 discovery와 messaging이
  계속 성공하고, 죽은 registry의 evidence endpoint가 bounded timeout 안에 실패하는지 검증한다.
- `DR-C2`: 정지했던 registry를 같은 peer 설정으로 다시 시작한 뒤 peer broadcast를 다시 구독해
  provider를 `member_peers`로 합산하고, 복구된 registry만 보는 consumer가 messaging에
  성공하는지 검증한다.
- `DR-D2`: registry를 별도 standalone 프로세스로 띄운 구성에서 discovery와 messaging이 동작하는지
  `DR-A1`과 같은 배포로 검증한다.

## C++에서 제외한 시나리오

- `DR-A4`, `DR-B3`, `DR-C3`, `DR-D1`, `DR-D3`, `DR-D4`: 충돌 광고, peer link flapping,
  전체 registry outage, embedded registry,
  혼합 cluster, in-process/remote topology 동등성은 별도 장기/혼합 배포 harness가 필요하다.
