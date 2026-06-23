# C++ Registry Messaging E2E Feature Map

이 문서는 `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`의
시나리오를 C++ framework E2E가 어디까지 검증하는지 정리한다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| RM-A1 | 구현 | registry discovery로 `registry.messaging.api`를 resolve하고 request를 보낸다. |
| RM-A2 | 구현 | `EnableClient(endpoint)` 수동 연결로 `api-a`에 직접 request를 보낸다. |
| RM-A4 | 구현 | 같은 rid `api-a`를 다른 endpoint의 `api-a-v2`로 교체한 뒤 연속 request를 검증한다. |
| RM-A6 | 구현 | 같은 registry 안에서 `api` channel과 `workflow` channel의 provider가 섞이지 않는지 검증한다. |
| RM-B1 | 구현 | client 실행 중 `api-b` provider를 추가하고 두 provider로 분산되는지 검증한다. |
| RM-B2 | 구현 | `api-b` provider 종료 뒤 request가 `api-a`로만 가는지 검증한다. |
| RM-C1 | 구현 | request와 send happy path를 함께 검증한다. |
| RM-C2 | 구현 | route mesh에서 target rid `api-b`로 request하고, 없는 rid는 실패하는지 검증한다. |
| RM-C3 | 구현 | 수동 multi-endpoint client-server channel에서 두 provider가 모두 처리하는지 검증한다. |
| RM-C4 | 구현 | timeout 뒤 정상 request가 late reply에 오염되지 않는지 검증한다. |
| RM-C5 | 구현 | 미등록 packet request 실패와 send drop 이후 정상 request 복구를 검증한다. |
| RM-C6 | 구현 | dealer mesh peer 두 개에 request가 분산되는지 검증한다. |
| RM-C7 | 미지원 | C++ framework의 public channel builder가 server socket weight 설정을 노출하지 않는다. bindings socket option을 우회 호출하지 않는다. |
| RM-C8 | 부분 구현 | public typed client로 소형, 대형, near-large payload 왕복을 검증한다. max size 초과 거부는 framework channel runtime에 max message size 적용이 배선된 뒤 추가한다. |
| RM-C9 | 미지원 | C++ framework public channel API에는 HWM 포화와 backpressure를 안정적으로 유도하는 테스트용 제어 API가 없다. |
