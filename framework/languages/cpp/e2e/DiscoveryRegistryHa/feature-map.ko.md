# C++ StoreFailure E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`

이 문서는 C++ Config-6 E2E의 현재 구현 상태를 기록한다. 디렉터리 이름은 아직
`DiscoveryRegistryHa`이지만, 실행 표면과 CMake target은 Redis location store 기반
`StoreFailure`로 전환했다. registry/embedded/probe 레거시 role은 제거했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| SF-A1 | 구현 | Redis location store가 정상일 때 provider 2개가 live peer row로 보이고, consumer request가 provider에 도달하며 consumer/provider runtime status가 healthy로 보인다. |
| SF-A2 | 구현 | C++ Redis store는 watch 없이 polling 경로로 동작한다. status의 `watch_enabled=false`와 provider shutdown 뒤 peer row 제거를 public `/query/*` endpoint로 검증한다. |
| SF-B1 | 구현 | Redis container pause 중 기존 연결 request가 계속 성공하고, runtime status가 store unhealthy로 바뀐 뒤 unpause 후 healthy로 회복된다. |
| SF-B2 | 구현 | store failure grace를 넘긴 Redis outage 중에도 기존 연결 request는 계속 성공하고, unpause 후 provider rows와 status가 회복된다. |
| SF-C1 | 구현 | provider `api-b`를 SIGABRT로 crash시키면 stale row가 owner lease 만료 뒤 live peer list에서 제외되고 이후 request는 survivor `api-a`로만 간다. |
| SF-C2 | 구현 | provider `api-b`를 graceful shutdown하면 lease TTL을 기다리지 않고 live peer list에서 제거되고 이후 request는 `api-a`로만 간다. |
| SF-D1 | 구현 | lease TTL보다 짧은 Redis outage 뒤 status가 healthy로 회복되고 request가 계속 성공한다. |
| SF-D2 | 구현 | Redis outage 중 provider `api-b`가 crash된 뒤 recovery 시 survivor `api-a`는 다시 live row로 보이고 dead `api-b`는 제외된다. |
| SF-D3 | 구현 | runtime status가 healthy → unhealthy(last error 포함) → healthy 순서로 관측된다. |

## 검증

- 2026-07-03: `ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260703-212414-2415`, `logs/20260703-212420-3257`,
    `logs/20260703-212425-3891`, `logs/20260703-212433-4740`,
    `logs/20260703-212446-6015`, `logs/20260703-212508-7157`,
    `logs/20260703-212513-8240`, `logs/20260703-212523-9016`,
    `logs/20260703-212542-10036`
  - 의미: SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3가 모두 passed marker를 남기고 `store-failure c++ e2e result=passed`로 끝났다.
