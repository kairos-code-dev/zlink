# Java DiscoveryRegistryHa E2E feature map

이 디렉터리는 공통 E2E Config 6 `Store Failure Recovery`를 Java framework location store
계약으로 다시 맞추는 중이다. 기존 Java 이름은 `DiscoveryRegistryHa`이지만, 새 검증 기준은
`framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`다.

## 실행 구조 상태

- `implemented`: 현재 Gradle 실행 그래프는 `Shared`, `Client`, `Server/Provider`,
  `Server/Consumer`만 포함한다. 제거된 public registry 계약을 쓰던 registry, embedded, probe
  role과 오래된 DR 시나리오 dispatch는 소스에서 제거했다.
- `implemented`: provider와 consumer는 Redis location store extension을 같은 endpoint와 key
  prefix로 등록한다.
- `implemented`: consumer는 public `ZLinkLocationRuntimeQuery`를 HTTP endpoint로 노출한다.
- `implemented`: config-6 전체 중 `SF-A1` baseline, `SF-A2` polling fallback,
  `SF-B1` store outage fail-static, `SF-B2` grace exceeded, `SF-C1` provider crash lease
  expiry, `SF-C2` graceful shutdown removal, `SF-D1` short outage recovery, `SF-D2` long outage
  recovery, `SF-D3` status transition이 Java location store 경로로 검증됐다.

## 구현됨

- `SF-A1`: Redis location store + provider 2개 + consumer 자동 연결 baseline을 검증한다.
  consumer의 public runtime query에서 provider peer row 2개가 보이고, store status가 healthy이며,
  consumer request가 provider 중 하나에서 처리되는지 확인한다.
- `SF-A2`: change-stamp surface를 숨긴 polling-only consumer가 `watchEnabled=false` 상태에서
  provider 추가(`api-c`)와 제거를 polling interval 안에 peer list와 request path로 반영하는지 확인한다.
- `SF-B1`: runner가 전용 Redis 컨테이너를 pause한 동안 기존 consumer 연결의 request가 계속
  성공하고, public runtime status가 store unhealthy와 owner lease heartbeat failure를 보여준 뒤
  unpause 후 healthy로 복구되는지 확인한다.
- `SF-B2`: Redis outage를 store failure grace보다 길게 유지하면서 기존 연결 request가 계속
  성공하는지 확인하고, outage status와 recovery 후 provider row 복구를 public endpoint로 검증한다.
- `SF-C1`: `api-b`를 SIGKILL해 row 제거 없이 종료시킨 뒤, owner lease 만료 후 public peer list에서
  `api-b`가 제외되고 consumer request가 survivor인 `api-a`로만 빠르게 처리되는지 확인한다.
- `SF-C2`: provider HTTP `/shutdown`으로 `api-b`를 정상 종료시킨 뒤, owner lease TTL을 기다리지
  않고 public peer list에서 `api-b`가 사라지고 request가 `api-a`로만 가는지 확인한다.
- `SF-D1`: 전용 Redis 컨테이너를 owner lease TTL보다 짧게 pause/unpause하면서 background client가
  계속 request를 보내고, 모든 request 성공과 복구 후 healthy status 및 provider row 2개를 확인한다.
- `SF-D2`: 전용 Redis 컨테이너를 owner lease TTL보다 길게 pause하고 그 동안 `api-b`를 SIGKILL한다.
  복구 뒤 살아 있는 `api-a`가 재등록되어 request를 계속 처리하고, 재등록하지 못한 `api-b`만 peer
  list에서 빠지는지 확인한다.
- `SF-D3`: 전용 Redis 컨테이너를 pause/unpause하면서 public runtime status가 healthy 상태에서
  last refresh와 owner lease 갱신 시간을 노출하고, outage 중에는 store/owner lease unhealthy와
  last error를 보이며, 복구 뒤 last refresh 시간이 전진하는지 확인한다.

## 남은 항목

- 없음: Config 6의 Java Redis location store scenario는 `all` runner로 검증됐다.

## 검증

- `../../gradlew --project-cache-dir /tmp/zlink-dr-gradle-cache --no-daemon compileJava --console=plain`
  - 결과: `BUILD SUCCESSFUL`
- `timeout 240s ./run_e2e.sh SF-A1`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-205335-11858/`
- `timeout 300s ./run_e2e.sh SF-A2`
  - 결과: `scenario SF-A2 passed providers=[api-a]`, `scenario SF-A2 passed providers=[api-b]`,
    `scenario SF-A2 passed providers=[api-a]`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-211659-69614/`
- `timeout 360s ./run_e2e.sh SF-B1`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-B1 passed providers=[api-a, api-b]`,
    `scenario SF-B1-RECOVERED passed providers=[api-b]`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-212347-99803/`
- `timeout 420s ./run_e2e.sh SF-B2`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-B2 passed providers=[api-b, api-a]`,
    `scenario SF-B2-RECOVERED passed providers=[api-b]`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-213206-24636/`
- `timeout 360s ./run_e2e.sh SF-D1`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-D1 passed providers=[api-b, api-a]`,
    `scenario SF-D1-RECOVERED passed providers=[api-a]`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-212806-15766/`
- `timeout 300s ./run_e2e.sh SF-C1`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-C1 passed providers=[api-a]`,
    `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-210726-45574/`
- `timeout 300s ./run_e2e.sh SF-C2`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-C2 passed providers=[api-a]`,
    `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-211120-55351/`
- `timeout 360s ./run_e2e.sh SF-D3`
  - 결과: `scenario SF-D3-HEALTHY passed providers=[api-a]`,
    `scenario SF-D3-OUTAGE passed providers=[]`, `scenario SF-D3-RECOVERED passed providers=[api-b]`,
    `scenario SF-D3 passed`, `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-213847-41499/`
- `timeout 420s ./run_e2e.sh SF-D2`
  - 결과: `scenario SF-A1 passed providers=[api-a]`, `scenario SF-D2 passed providers=[api-a]`,
    `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-214417-56996/`
- `timeout 900s ./run_e2e.sh all`
  - 결과: `SF-A1`/`SF-A2`/`SF-B1`/`SF-B2`/`SF-C1`/`SF-C2`/`SF-D1`/`SF-D2`/`SF-D3` 통과,
    `discovery-registry-ha e2e result=passed`
  - 로그: `logs/20260703-214733-67106/`
- cleanup 후 재검증
  - `../../gradlew --project-cache-dir /tmp/zlink-dr-cleanup-gradle-cache --no-daemon :Client:compileJava :Server:Provider:compileJava :Server:Consumer:compileJava --console=plain`
    - 결과: `BUILD SUCCESSFUL`
  - `timeout 900s ./run_e2e.sh all`
    - 결과: `SF-A1`/`SF-A2`/`SF-B1`/`SF-B2`/`SF-C1`/`SF-C2`/`SF-D1`/`SF-D2`/`SF-D3` 통과,
      `discovery-registry-ha e2e result=passed`
    - 로그: `logs/20260704-033113-35152/`
