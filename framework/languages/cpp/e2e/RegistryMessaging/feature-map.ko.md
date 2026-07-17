# C++ Location Messaging E2E Feature Map

이 문서는 `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md`의
시나리오를 C++ framework E2E가 어디까지 검증하는지 정리한다. C++ 디렉터리와 일부 namespace는
시나리오 ID 연속성을 위해 아직 `RegistryMessaging` 이름을 유지한다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| `RM-A1` | 10.0.0 전환 대상 | Redis location store로 MeshNode descriptor와 `registry.messaging.api` ChannelName membership을 공유하고, consumer가 선택된 ready member로 request를 보낸다. provider HTTP endpoint의 `/locations/mesh-nodes`가 store MeshNode descriptor 두 개를 검증한다. `E2E_START_ORDER`의 정방향·역방향·고정 seed shuffle 변형은 두 provider의 실제 시작 순서를 바꾼다. 임의 settle sleep이나 adapter request retry 없이 첫 결과를 그대로 검증해야 하며, 현재 source와 runner는 이 목표를 아직 충족하지 않는다. |
| `RM-A2` | 전환 필요 | 같은 MeshNode에서 `mesh.peer_connections().connect(...)`로 등록한 `api-a`와 location descriptor로 발견한 `api-b`를 함께 admission한다. descriptor가 추가되는 동안 진행 중인 request가 완료되고 두 peer의 ChannelName membership이 ready 상태를 유지하는지 10.0.0 package로 다시 검증한다. |
| `RM-A4` | 구현 | location store consumer를 교체 전부터 유지한다. 같은 rid `api-a`를 다른 endpoint의 `api-a-v2`로 교체한 뒤 runtime query에서 p2 row 하나만 남는지 확인하고, 같은 consumer로 보낸 연속 request 20개가 모두 v2 evidence에 기록되는지 검증한다. |
| `RM-A6` | 구현 | 같은 Redis location store 안에서 `api` channel과 `workflow` channel의 provider가 섞이지 않는지 검증한다. workflow adapter는 실패한 의미 request를 재시도하지 않고 framework 오류 종류를 보존한다. |
| `RM-B1` | 구현 | client 실행 중 `api-b` provider를 추가하고 두 provider로 분산되는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832`. |
| `RM-B2` | 구현 | 두 provider의 시작 순서를 `E2E_START_ORDER`로 바꿀 수 있다. store consumer에서 16개 request가 진행 중일 때 `api-b`를 종료하고, 각 완료를 정상 reply 또는 정해진 public error로 분류한다. MeshNode descriptor 제거 직후 재시도 없이 보낸 20개 request가 모두 `api-a`에서 처리되는지도 검증한다. |
| `RM-B3` | 전환 필요 | provider A handler-start evidence 뒤 A를 `SIGKILL`하고, 같은 consumer의 in-flight 결과를 유한한 public error로 분류하며 자동 재전송이 없음을 확인해야 한다. crash 전파 구간과 descriptor 제거 뒤의 신규 request는 생존 provider B로 계속 처리되는지 별도로 검증한다. 현재 runner에는 이 전체 경계의 증거가 없다. |
| `RM-C1` | 구현 | request와 send happy path를 함께 검증한다. |
| `RM-C2` | 전환 필요 | route mesh에서 target rid `api-b`로 request하는 경로는 구현했다. 없는 rid는 현재 `RouteNotConnected`로 분류하지만, 10.0.0 계약의 `RequestTargetNotFound`로 고치고 known-but-disconnected target의 `RouteNotConnected`와 대조해야 한다. |
| `RM-C3` | 전환 필요 | consumer MeshNode가 두 provider endpoint를 manual peer로 등록하고 같은 ChannelName request를 반복 제출해 두 ready member가 모두 선택되는지 검증한다. C++ HTTP array body binding 차이 때문에 batch endpoint 대신 단건 request endpoint를 반복 호출한다. |
| `RM-C4` | 구현 | store consumer HTTP role이 timeout request를 `TimeoutException`으로 분류하고, late reply가 후속 정상 request를 오염시키지 않는지 검증한다. |
| `RM-C5` | 구현 | store consumer HTTP role이 미등록 packet request를 `HandlerNotFound`로 분류하고, send drop 이후 정상 request 복구를 검증한다. |
| `RM-C7` | 구현 | `server_peer_weight`로 build-time server weight를 다르게 준 provider 두 개를 시작하고, location-store 자동 연결 client에서 high-weight provider가 더 많이 처리되는지 검증한다. 최신 전체 통과: `logs/20260708-131829-51832` (`api-a=59`, `api-b=41`). |
| `RM-C8` | 구현 | single consumer HTTP role이 `PayloadReq`/`PayloadRes`로 소형, 대형, near-large payload의 length와 SHA-256 왕복을 검증한다. 별도 max-size 실행에서는 초과 payload를 timeout과 다른 `RequestFailed`로 분류하고 이후 정상 request 복구를 검증한다. 두 실행 모두 의미 request를 readiness probe나 retry로 사용하지 않는다. |
| `RM-C9` | 전환 필요 | 현재 runner는 다량 `ProfileMsg` 제출과 backlog 해소 뒤 후속 request 복구만 검증한다. non-blocking submit의 즉시 backpressure 결과와 blocking submit의 bounded admission 결과를 public send call에서 직접 대조해야 한다. |
