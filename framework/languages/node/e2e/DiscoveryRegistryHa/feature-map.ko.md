# Node.js StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

| Scenario | 상태 | Node 구현 메모 |
|----------|------|----------------|
| SF-A1 | 구현 | Redis location store + provider 2개 + consumer baseline. public runtime query topology와 request/evidence를 검증한다. |
| SF-B1 | 구현 | Redis container를 정지한 동안 fail-static으로 기존 연결 request가 유지되고 runtime status가 unhealthy/lastError를 노출하는지 검증한다. |
| SF-C1 | 구현 | `api-b` SIGKILL 뒤 owner lease 만료만으로 stale peer row가 `/location/peers` 성공 결과에서 제외되고 후속 request가 `api-a`로만 가는지 검증한다. |
| SF-D1 | 구현 | Redis container를 짧게 `pause/unpause`하고 request window, runtime status recovery, peer list recovery를 검증한다. |
| SF-D2 | 구현 | Redis 장기 pause 중 `api-b`를 SIGKILL하고, 복구 뒤 `api-a` 재등록, `api-b` 제외, 후속 request의 `api-a` 단독 routing을 검증한다. |
| SF-A2 | 미구현 | P1 polling fallback 시나리오. P0 완료 뒤 필요 여부를 별도 판단한다. |
| SF-B2 | 미구현 | P1 store failure grace 초과 시나리오. P0 완료 뒤 필요 여부를 별도 판단한다. |
| SF-C2 | 미구현 | P1 graceful shutdown 대조 시나리오. P0 완료 뒤 필요 여부를 별도 판단한다. |
| SF-D3 | 미구현 | P1 runtime status 전이 관측 시나리오. P0 완료 뒤 필요 여부를 별도 판단한다. |

## 검증

- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-C1`
  - 로그: `logs/20260703-204014-50300`
  - 결과: `scenario SF-C1 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-D1`
  - 로그: `logs/20260703-204208-60437`
  - 결과: `scenario SF-D1 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-D2`
  - 로그: `logs/20260703-204434-71368`
  - 결과: `scenario SF-D2 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh`
  - 로그: `logs/20260703-205114-96377`
  - 결과: 현재 P0 sweep `SF-A1`·`SF-B1`·`SF-C1`·`SF-D1`·`SF-D2` 통과
