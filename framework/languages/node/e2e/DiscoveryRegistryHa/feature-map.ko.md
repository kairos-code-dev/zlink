# Node.js StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

| Scenario | 상태 | Node 구현 메모 |
|----------|------|----------------|
| SF-A1 | 구현 | Redis location store + provider 2개 + consumer baseline. public runtime query topology와 request/evidence를 검증한다. |
| SF-B1 | 구현 | Redis container를 정지한 동안 fail-static으로 기존 연결 request가 유지되고 runtime status가 unhealthy/lastError를 노출하는지 검증한다. |
| SF-C1 | 구현 | `api-b` SIGKILL 뒤 owner lease 만료만으로 stale peer row가 `/location/peers` 성공 결과에서 제외되고 후속 request가 `api-a`로만 가는지 검증한다. |
| SF-D1 | 구현 | Redis container를 짧게 제거한 뒤 같은 endpoint에 빈 container로 재기동하고 request 전 구간 성공, runtime status recovery, peer list recovery를 검증한다. |
| SF-D2 | 구현 | Redis 장기 중단 중 `api-b`를 SIGKILL하고 빈 store 재기동 뒤 `api-a` 재등록, `api-b` 제외, request 전 구간 성공과 `api-a` 단독 routing을 검증한다. |
| SF-A2 | 구현 | Redis location store의 `watchEnabled=false` 상태에서 provider 추가와 정상 제거가 polling interval 안에 peer list와 request routing에 반영되는지 검증한다. |
| SF-B2 | 구현 | store 중단 뒤 `storeFailureGraceMs` 6000ms를 넘겨도 기존 ready 연결 request가 계속 성공하는지 검증한다. store 복구 전 새 provider row 등록은 만들지 않는다. |
| SF-C2 | 구현 | `api-b` 정상 종료 뒤 lease TTL을 기다리지 않고 peer list에서 빠지고 후속 request가 `api-a`로만 가는지 검증한다. |
| SF-D3 | 구현 | Redis pause/unpause 사이클 동안 consumer public runtime query의 healthy → unhealthy/lastError → healthy/lastRefresh 전이를 검증한다. |
| SF-E1 | 구현 | Redis `CLIENT PAUSE`로 store 응답 지연을 주입하고, 같은 consumer process가 pending store read를 가진 동안 기존 channel profile request를 낮은 지연으로 계속 처리하는지 검증한다. |

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
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-D3`
  - 로그: `logs/20260707-113447-1458059`
  - 결과: `scenario SF-D3 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-C2`
  - 로그: `logs/20260707-113607-1461661`
  - 결과: `scenario SF-C2 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-B2`
  - 로그: `logs/20260707-113841-1472835`
  - 결과: `scenario SF-B2 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-A2`
  - 로그: `logs/20260707-114016-1479743`
  - 결과: `scenario SF-A2 passed`, `store-failure-recovery scenario result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh`
  - 로그: `logs/20260707-114042-1480828` 외 scenario별 하위 실행 로그
  - 결과: `SF-A1`·`SF-A2`·`SF-B1`·`SF-B2`·`SF-C1`·`SF-C2`·`SF-D1`·`SF-D2`·`SF-D3` 통과, `store-failure-recovery e2e result=passed`
- `./e2e/DiscoveryRegistryHa/run_e2e.sh SF-E1`
  - 로그: 최신 실행 후 기록
  - 결과: Redis 응답 지연 중 unrelated profile request 비블로킹 검증
