# C++ Location Messaging E2E Feature Map

이 문서는 `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`의
시나리오를 C++ framework E2E가 어디까지 검증하는지 정리한다. C++ 디렉터리와 일부 namespace는
시나리오 ID 연속성을 위해 아직 `RegistryMessaging` 이름을 유지한다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| `RM-A1` | 구현 | Redis location store로 `registry.messaging.api` peer row를 공유하고 endpoint 없는 client-server client가 자동 연결로 request를 보낸다. provider HTTP endpoint의 `/locations/peers`가 store peer row 두 개를 검증한다. `E2E_START_ORDER`의 정방향·역방향·고정 seed shuffle 변형은 두 provider의 실제 시작 순서를 바꾼다. |
| `RM-A2` | 구현 | `EnableClient(endpoint)` 수동 연결로 `api-a`에 직접 request를 보낸다. |
| `RM-A4` | 구현 | location store consumer를 교체 전부터 유지한다. 같은 rid `api-a`를 다른 endpoint의 `api-a-v2`로 교체한 뒤 runtime query에서 p2 row 하나만 남는지 확인하고, 같은 consumer로 보낸 연속 request 20개가 모두 v2 evidence에 기록되는지 검증한다. |
| `RM-A6` | 구현 | 같은 Redis location store 안에서 `api` channel과 `workflow` channel의 provider가 섞이지 않는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832`. |
| `RM-B1` | 구현 | client 실행 중 `api-b` provider를 추가하고 두 provider로 분산되는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832`. |
| `RM-B2` | 구현 | 두 provider의 시작 순서를 `E2E_START_ORDER`로 바꿀 수 있다. store consumer에서 16개 request가 진행 중일 때 `api-b`를 종료하고, 각 완료를 정상 reply 또는 정해진 public error로 분류한다. peer row 제거 직후 재시도 없이 보낸 20개 request가 모두 `api-a`에서 처리되는지도 검증한다. |
| `RM-C1` | 구현 | request와 send happy path를 함께 검증한다. |
| `RM-C2` | 구현 | route mesh에서 target rid `api-b`로 request하고, 없는 rid는 실패하는지 검증한다. |
| `RM-C3` | 구현 | direct consumer HTTP role이 수동 multi-endpoint client-server channel로 request를 보내고, 두 provider가 모두 처리하는지 검증한다. C++ HTTP array body binding 차이 때문에 `.NET`의 batch endpoint 대신 같은 consumer의 단건 request endpoint를 반복 호출한다. |
| `RM-C4` | 구현 | store consumer HTTP role이 timeout request와 정상 request를 보내고, late reply가 후속 request를 오염시키지 않는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832`. |
| `RM-C5` | 구현 | store consumer HTTP role이 미등록 packet request 실패와 send drop 이후 정상 request 복구를 검증한다. 최신 전체 통과: `logs/20260708-131829-51832`. |
| `RM-C7` | 구현 | `server_peer_weight`로 build-time server weight를 다르게 준 provider 두 개를 띄우고, location-store 자동 연결 client에서 high-weight provider가 더 많이 처리되는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832` (`api-a=59`, `api-b=41`). |
| `RM-C8` | 구현 | single consumer HTTP role이 `PayloadReq`/`PayloadRes`로 소형, 대형, near-large payload의 length와 SHA-256 왕복을 검증한다. `server_max_message_size`를 낮춘 별도 provider에서 초과 payload가 C++ public timeout error로 끝나는 것과 이후 정상 request 복구를 검증한다. |
| `RM-C9` | 구현 | backpressure consumer HTTP role이 느린 send handler에 다량 `ProfileMsg`를 제출하고, provider evidence와 backlog 해소 뒤 후속 request 복구를 검증한다. public send submit은 bounded-failure oracle을 노출하지 않는다. 최신 전체 통과: `logs/20260708-131829-51832`. |
