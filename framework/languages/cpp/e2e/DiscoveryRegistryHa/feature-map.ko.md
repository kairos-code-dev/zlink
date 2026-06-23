# C++ Discovery/Registry HA E2E feature map

이 문서는 Config 6 공통 시나리오를 C++ framework 공개 API와 현재 E2E harness로 어디까지
검증하는지 정리한다.

## 구현한 시나리오

- `DR-A1`: standalone registry 1대, provider 2대, consumer 1대로 provider discovery와
  messaging baseline을 검증한다.
- `DR-D2`: registry를 별도 standalone 프로세스로 띄운 구성에서 discovery와 messaging이 동작하는지
  `DR-A1`과 같은 배포로 검증한다.

## C++에서 제외한 시나리오

- `DR-A2`, `DR-A3`, `DR-B1`, `DR-C2`: C++ framework의 in-process `registry_query_t`는
  `status`, `service_summary`, `member_peers`를 제공하지만, 원격 `registry_query_client_t`는 현재
  `topology()`만 노출한다. 공통 문서의 peer 합산 판정은 `MemberPeersAsync`와
  `ConnectedPeerRegistryCount`를 요구하므로, registry 내부 query를 별도 HTTP evidence로 노출하는
  전용 앱이 생긴 뒤 구현한다.
- `DR-C1`: 살아 있는 registry endpoint 지속성은 Config 5의 scale-in류 runner로 일부 관측하지만,
  죽은 registry endpoint 원격 query의 bounded failure를 C++ public query client로 고정하려면
  query timeout/오류 표면 보강이 필요하다.
- `DR-A4`, `DR-B2`, `DR-B3`, `DR-C3`, `DR-D1`, `DR-D3`, `DR-D4`: 충돌 광고, peer link flapping,
  전체 registry outage, embedded registry evidence, 혼합 cluster, in-process/remote topology 동등성은
  전용 registry/probe 앱과 query evidence가 필요하다.
