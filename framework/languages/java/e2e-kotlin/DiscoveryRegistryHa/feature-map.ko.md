# Kotlin DiscoveryRegistryHa E2E feature map

이 문서는 Config 6 StoreFailure 공통 시나리오 중 Kotlin E2E가 현재 검증하는 항목을 정리한다.
이전 Discovery/Registry HA(`DR-*`) 시나리오는 core native Discovery/Registry 위임 제거 대상이므로
제거했다. Kotlin 구현은 Java framework Redis location store extension을 Spring bean으로 등록하고,
consumer는 public `ZLinkClient`와 public `monitoringLocationRuntimeQuery()` 경로만 사용한다.

## 구현됨

- `SF-A1`: Redis location store에 provider peer row 2개가 보이고, consumer location runtime status가
  healthy이며, channel request가 location resolver를 통해 provider로 라우팅되는지 검증한다.
- `SF-A2`: watch 기능을 쓰지 않는 polling-only store wrapper에서 provider 추가/제거가 polling으로
  반영되고 request가 성공하는지 검증한다.
- `SF-B1`: Redis store outage 중 기존 static route로 요청이 계속 처리되고, unhealthy status와 owner
  lease failure가 public status endpoint에 드러나는지 검증한다.
- `SF-B2`: store failure grace를 넘는 outage 동안 요청이 처리되고, outage 상태가 status에 기록된 뒤
  Redis 복구 후 peer row와 request 경로가 회복되는지 검증한다.
- `SF-C1`: provider crash 뒤 owner lease TTL과 polling 주기 안에서 죽은 provider row가 제거되고
  survivor provider만 요청을 처리하는지 검증한다.
- `SF-C2`: graceful provider shutdown이 owner lease TTL보다 빨리 row를 제거하고 survivor request만
  남기는지 검증한다.
- `SF-D1`: 짧은 Redis outage 동안 request traffic이 멈추지 않고, 복구 뒤 peer row와 request가 다시
  healthy 상태로 수렴하는지 검증한다.
- `SF-D2`: 긴 Redis outage 중 provider crash가 겹쳐도 survivor request가 이어지고, 복구 뒤 죽은
  provider row 없이 survivor provider로만 후속 request가 가는지 검증한다.
- `SF-D3`: healthy, outage, recovered status transition에서 last refresh, owner lease renewal,
  watch/polling state, last error가 public status endpoint로 드러나는지 검증한다.

## 포팅 구조 상태

현재 Kotlin DiscoveryRegistryHa E2E는 `Shared`, `Client`, `Server/Provider`, `Server/Consumer`만 사용한다.
Provider와 Consumer는 `ZLinkRedisLocationStore`를 등록하고 `useDiscovery()`를 쓰지 않는다. Consumer HTTP
endpoint는 `/locations/status`와 `/locations/peers`를 노출해 client가 public monitoring query 결과를
확인한다.

legacy `Server/Registry`, `Server/Probe`, `Server/Embedded`와 기존 `DR-*` scenario 파일은 Gradle include와
source tree에서 제거했다.

## 검증 결과

- targeted build:
  `../../gradlew --project-cache-dir /tmp/zlink-kotlin-dr-gradle-cache --no-daemon --no-parallel --max-workers=1 :Client:installDist :Server:Provider:installDist :Server:Consumer:installDist --console=plain`
  통과.
- `logs/20260704-051543-91770`: `timeout 420s ./run_e2e.sh SF-A1` 통과.
- `logs/20260704-051605-92888`: `timeout 1200s ./run_e2e.sh all` 통과.
- full runner marker: `SF-A1`, `SF-A2`, `SF-B1`, `SF-B2`, `SF-C1`, `SF-C2`, `SF-D1`, `SF-D2`,
  `SF-D3` passed, `discovery-registry-ha kotlin e2e result=passed`.
