# .NET StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

이 문서는 `.NET` StoreFailure client가 실제로 실행하는 시나리오만 기록한다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SF-A1 | 구현 | 정상 store에서 consumer descriptor/runtime snapshot에 `api-a`와 `api-b`가 모두 보이고, request가 성공하며, consumer와 두 provider의 runtime status가 healthy store와 owner lease 갱신을 보고한다. |
| SF-A2 | 구현 | watch/change-stamp surface가 없는 polling-only consumer가 watch disabled status를 보고하고, provider 추가와 제거를 polling만으로 descriptor/runtime snapshot에 반영한다. |
| SF-B1 | 구현 | store 장애 중에도 기존 연결 request가 계속 성공하고, consumer runtime status가 store unhealthy와 owner lease heartbeat 실패를 기록한 뒤 복구 후 healthy로 돌아온다. |
| SF-B2 | core 대기 | store 복구 창에서 기존 transport가 zombie 상태로 남아 request가 timeout으로 끝난다. core 연결 종료 감지 수정 뒤 다시 실행한다. |
| SF-D1 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| SF-D3 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| SF-C2 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| SF-C1 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| SF-D2 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
| SF-E1 | 재검 대기 | 최신 전체 실행이 SF-B2에서 중단되어 현재 source로 통과를 확인하지 못했다. |
