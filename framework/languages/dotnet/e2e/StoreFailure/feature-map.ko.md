# .NET StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

이 문서는 `.NET` StoreFailure client가 실제로 실행하는 시나리오만 기록한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SF-A1 | 구현 | 정상 store에서 consumer descriptor/runtime snapshot에 `api-a`와 `api-b`가 모두 보이고, request가 성공하며, consumer와 두 provider의 runtime status가 healthy store와 owner lease 갱신을 보고한다. |
| SF-A2 | 구현 | watch/change-stamp surface가 없는 polling-only consumer가 watch disabled status를 보고하고, provider 추가와 제거를 polling만으로 descriptor/runtime snapshot에 반영한다. |
| SF-B1 | 구현 | store 장애 중에도 기존 연결 request가 계속 성공하고, consumer runtime status가 store unhealthy와 owner lease heartbeat 실패를 기록한 뒤 복구 후 healthy로 돌아온다. |
| SF-B2 | 구현 | store failure grace를 넘긴 장애 중에도 기존 연결 request가 성공하고, 장애가 status에 드러나며, store 복구 뒤 provider MeshNode descriptor가 다시 조회된다. |
| SF-D1 | 구현 | owner lease TTL보다 짧은 store 장애 동안 request가 계속 성공하고, 복구 뒤 runtime status가 healthy로 전환되며 provider MeshNode descriptor가 모두 조회 가능 상태로 유지된다. |
| SF-D3 | 구현 | 장애 전 healthy, 장애 중 unhealthy와 last error, 복구 후 healthy와 더 새로운 last refresh가 순서대로 status에 나타나고 watch/polling과 last error 필드를 관측한다. |
| SF-C2 | 구현 | provider 정상 종료가 lease TTL보다 빠르게 MeshNode descriptor를 제거하고, 이후 request가 종료된 `api-b`로 전달되지 않는다. |
| SF-C1 | 구현 | SIGKILL된 provider의 owner lease가 만료되면 `api-b` MeshNode descriptor가 store 조회와 runtime snapshot에서 제외되고, follow-up request가 정상 provider `api-a`에서만 처리된다. |
| SF-D2 | 구현 | lease TTL보다 긴 store 장애와 장애 중 provider crash 뒤에도 정상 provider가 복구 후 재등록되고, 비정상 종료된 provider는 disconnect grace 이후 선택되지 않으며 post-recovery request가 `api-a`로 처리된다. |
| SF-E1 | 구현 | consumer의 location store wrapper가 public descriptor query에 응답 지연을 주입한 동안 같은 consumer의 application request p99가 baseline 기반 budget 안에 머무르고, 지연 해제 뒤 follow-up request가 성공하는지 검증한다. |
