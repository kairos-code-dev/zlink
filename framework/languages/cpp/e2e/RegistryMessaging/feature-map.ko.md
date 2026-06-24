# C++ Registry Messaging E2E Feature Map

이 문서는 `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md`의
시나리오를 C++ framework E2E가 어디까지 검증하는지 정리한다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| `RM-A1` | 구현 | registry discovery로 `registry.messaging.api`를 resolve하고 request를 보낸다. |
| `RM-A2` | 구현 | `EnableClient(endpoint)` 수동 연결로 `api-a`에 직접 request를 보낸다. |
| `RM-A4` | 구현 | 같은 rid `api-a`를 다른 endpoint의 `api-a-v2`로 교체한 뒤 연속 request를 검증한다. |
| `RM-A6` | 구현 | 같은 registry 안에서 `api` channel과 `workflow` channel의 provider가 섞이지 않는지 검증한다. |
| `RM-B1` | 구현 | client 실행 중 `api-b` provider를 추가하고 두 provider로 분산되는지 검증한다. |
| `RM-B2` | 구현 | `api-b` provider 종료 뒤 request가 `api-a`로만 가는지 검증한다. |
| `RM-C1` | 구현 | request와 send happy path를 함께 검증한다. |
| `RM-C2` | 구현 | route mesh에서 target rid `api-b`로 request하고, 없는 rid는 실패하는지 검증한다. |
| `RM-C3` | 구현 | 수동 multi-endpoint client-server channel에서 두 provider가 모두 처리하는지 검증한다. |
| `RM-C4` | 구현 | timeout 뒤 정상 request가 late reply에 오염되지 않는지 검증한다. |
| `RM-C5` | 구현 | 미등록 packet request 실패와 send drop 이후 정상 request 복구를 검증한다. |
| `RM-C6` | 제거 | DealerMesh 채널 타입 제거로 시나리오가 제거됐다. 다중 provider 분산은 RM-C3에서 client-server multi-endpoint로 검증한다. |
| `RM-C7` | 구현 | `server_peer_weight`로 build-time server weight를 다르게 준 provider 두 개를 띄우고, registry-discovered client에서 high-weight provider가 더 많이 처리되는지 검증한다. |
| `RM-C8` | 구현 | public typed client로 소형, 대형, near-large payload 왕복을 검증하고, `server_max_message_size`를 낮춘 별도 provider에서 초과 payload 실패 뒤 정상 request가 복구되는지 검증한다. |
| `RM-C9` | 부분 구현 | C++ framework channel builder가 HWM 값을 live socket에 적용한다. 다만 framework E2E에서 포화를 결정적으로 유도하는 harness는 아직 없어 backpressure 계약 검증은 남아 있다. |
