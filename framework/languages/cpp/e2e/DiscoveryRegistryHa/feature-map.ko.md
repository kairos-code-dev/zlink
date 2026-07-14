# C++ StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

이 문서는 C++ Config-6 E2E의 현재 구현 상태를 기록한다. 디렉터리 이름은 아직
`DiscoveryRegistryHa`이지만, 실행 표면과 CMake target은 Redis location store 기반
`StoreFailure`로 전환했다. registry/embedded/probe 레거시 role은 제거했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SF-A1 | 구현 | Redis location store가 정상일 때 provider 2개가 live peer row로 보이고, consumer request가 provider에 도달하며 consumer/provider runtime status가 healthy로 보인다. |
| SF-A2 | 구현 | C++ Redis store는 watch 없이 polling 경로로 동작한다. status의 `watch_enabled=false`와 provider shutdown 뒤 peer row 제거를 public `/query/*` endpoint로 검증한다. |
| SF-B1 | 구현 | Redis container process를 정지한 동안 기존 연결 request가 계속 성공하고, runtime status가 store unhealthy로 바뀐 뒤 빈 store 재기동 후 healthy로 회복된다. |
| SF-B2 | 구현 | Redis 정지 중 `api-b`를 새 channel endpoint에서 재기동한다. store failure grace를 넘길 때까지 기존 `api-a` 연결의 request만 성공하고, 빈 store 복구 뒤 새 endpoint row가 등록되어 `api-b`가 다시 요청을 처리한다. |
| SF-C1 | 구현 | provider `api-b`를 SIGABRT로 crash시키면 raw Redis row는 남지만 framework의 owner lease join이 lease 만료 뒤 live peer list에서 제외하고, 이후 request는 survivor `api-a`로만 간다. |
| SF-C2 | 구현 | provider `api-b`를 graceful shutdown하면 framework live-row reader가 lease와 row 정리를 반영해 lease TTL을 기다리지 않고 live peer list에서 제외하며, 이후 request는 `api-a`로만 간다. |
| SF-D1 | 구현 | 두 provider 연결을 실제 request로 준비하고 장애 전부터 복구 뒤까지 traffic을 유지한다. local row 재등록과 heartbeat 유예 뒤 status가 회복되며, 두 endpoint의 Connected/Disconnected count가 늘지 않는다. |
| SF-D2 | 구현 | 장애 전부터 지속 traffic을 흘리고 최대 성공 간격을 제한한다. Redis 정지 중 `api-b`가 crash된 뒤 `api-a` socket count는 유지되고 `api-b` Disconnected만 증가하며, owner lease join에서 dead row가 제외된다. |
| SF-D3 | 구현 | Redis process 정지·재기동 동안 runtime status가 healthy → unhealthy(last error 포함) → healthy 순서로 관측된다. |
| SF-E1 | 구현 | consumer process의 Redis location store 호출에 E2E 전용 delay wrapper로 1200ms 지연을 주입한다. 지연된 peer query가 실제로 느려지는 동안 같은 consumer process의 application request p99가 baseline budget 안에 남고, 지연 해제 뒤 request가 정상 복구되는지 검증한다. 최신 전체 통과: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all` (`logs/20260708-135342-166331`). |

표준 `/profile/request`는 내부 retry 없이 5초 제한의 framework request 한 번만 실행한다. 따라서 각 scenario의 request 성공은 늦은 재시도로 복구된 결과가 아니라 해당 시점 연결의 실제 결과다.

## 검증

- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-080111-2298217`(SF-D1), `logs/20260715-080125-2299402`(SF-D2)
  - 의미: consumer 내부 retry를 제거한 단일 request 경로에서도 지속 traffic과 복구 검증이 통과했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-075540-2274183`(SF-D1), `logs/20260715-075554-2275628`(SF-D2)
  - 의미: 장애 전부터 복구 뒤까지 실제 request traffic을 유지하고, socket monitoring evidence로
    survivor 연결 불변과 죽은 provider의 disconnect를 분리해 확인했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-074233-2224721`(SF-C1), `logs/20260715-074258-2226349`(SF-C2),
    `logs/20260715-074320-2228448`(SF-D2)
  - 의미: Redis store의 lease 필터를 제거한 뒤에도 framework의 공통 live-row reader가
    owner lease를 join해 stale peer 제외와 auto-connect 정리를 수행했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-072318-2155219`(SF-B2), 단독 검증은 `logs/20260715-072705-2167572`
  - 의미: store 장애 중 새 endpoint로 재기동한 provider가 grace 초과와 store 복구 전에는
    요청을 처리하지 않고, 복구 뒤 새 row를 통해 요청을 처리했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-071009-2105356`(SF-B1), `logs/20260715-071020-2106423`(SF-B2),
    `logs/20260715-071113-2109371`(SF-D1), `logs/20260715-071126-2110473`(SF-D2),
    `logs/20260715-071144-2111560`(SF-D3)
  - 의미: 고정 host port의 Redis를 실제 정지·재기동해 빈 store 복구 조건을 만들고,
    local row 재등록과 heartbeat 유예 뒤 전체 Config 6 scenario가 통과했다.
- 2026-07-03: `ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260703-212414-2415`, `logs/20260703-212420-3257`,
    `logs/20260703-212425-3891`, `logs/20260703-212433-4740`,
    `logs/20260703-212446-6015`, `logs/20260703-212508-7157`,
    `logs/20260703-212513-8240`, `logs/20260703-212523-9016`,
    `logs/20260703-212542-10036`
  - 의미: SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3가 모두 passed marker를 남기고 `store-failure c++ e2e result=passed`로 끝났다.
- 2026-07-07: `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh SF-E1`
  - 결과: 통과
  - 로그: `logs/20260707-190143-3182342`
  - 의미: store 응답 지연 중에도 같은 consumer process의 무관 application request가 baseline budget 안에서 처리되고, 지연 해제 뒤 request path가 복구된다.
- 2026-07-07: `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260707-190210-3183591`(SF-A1), `logs/20260707-190227-3185256`(SF-A2),
    `logs/20260707-190236-3186388`(SF-B1), `logs/20260707-190302-3188778`(SF-B2),
    `logs/20260707-190332-3191759`(SF-C1), `logs/20260707-190403-3194237`(SF-C2),
    `logs/20260707-190417-3195710`(SF-D1), `logs/20260707-190453-3199077`(SF-D2),
    `logs/20260707-190530-3201684`(SF-D3), `logs/20260707-190636-3206735`(SF-E1)
  - 의미: 공통 config-6의 SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3, SF-E1이 모두 C++ runner에서 passed marker를 남기고 `store-failure c++ e2e result=passed`로 끝났다.
- 2026-07-08: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260708-135153-159069`(SF-A1), `logs/20260708-135202-159895`(SF-A2),
    `logs/20260708-135206-160402`(SF-B1), `logs/20260708-135216-161152`(SF-B2),
    `logs/20260708-135231-162310`(SF-C1), `logs/20260708-135254-163218`(SF-C2),
    `logs/20260708-135302-163973`(SF-D1), `logs/20260708-135314-164810`(SF-D2),
    `logs/20260708-135336-165762`(SF-D3), `logs/20260708-135342-166331`(SF-E1)
  - 의미: runner가 Redis container를 parent run에서 한 번만 띄우고 각 scenario에 endpoint와 container 이름을 넘긴다. `SF-C1`과 `SF-D2`의 provider SIGABRT는 scenario가 `/admin/crash`로 만든 failure injection으로만 허용하고, cleanup 또는 일반 provider/consumer 종료의 비정상 status는 실패로 드러낸다. Redis outage 이후 async Redis future가 무기한 남지 않도록 C++ Redis location store operation은 제한 시간 안에 끝나지 않으면 실패를 반환한다.
