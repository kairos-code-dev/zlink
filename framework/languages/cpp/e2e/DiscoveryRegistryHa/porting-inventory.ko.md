# C++ StoreFailure .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/StoreFailure`의 source-only 파일을 기준으로
C++ Config-6 E2E의 대응 파일과 검증 상태를 기록한다. C++ 디렉터리 이름은 아직
`DiscoveryRegistryHa`이지만, 내부 구현과 runner는 StoreFailure 의미로 전환했다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/StoreFailure`
- C++ 대상: `framework/languages/cpp/e2e/DiscoveryRegistryHa`
- 현재 상태: SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3, SF-E1이 C++ runner proof를 가진다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `Shared/Messages.cs` | `Shared/store_failure_contracts.hpp` | shared | done | channel 이름, profile request/reply, evidence wait, runtime status, peer row DTO가 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | SF-A1~SF-D3 scenario 선택과 public HTTP probe driver가 있다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | client-support | done | env parsing, HTTP GET/POST, Redis process 정지·재기동과 readiness 확인, peer/status wait helper가 있다. |
| `Client/Support/SfProbe.cs` | `Client/Support/client_support.hpp`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | client-support | done | `/query/status`, `/query/peers`, `/profile/request`, `/health` 기반 probe를 제공한다. 표준 profile probe는 consumer 내부 retry 없이 framework request 한 번의 결과를 반환한다. |
| `Client/Support/StoreFailureProcessManager.cs` | `run_e2e.sh`, `Client/Support/client_support.hpp` | runner/client-support | done | runner가 provider/consumer와 고정 loopback host port의 Redis container를 시작한다. SF-B2에서는 Redis 정지를 확인한 뒤 `api-b`를 새 channel endpoint에서 재기동한다. client는 public HTTP와 Docker stop/restart로 장애와 복구를 제어한다. |
| `Client/Scenarios/*.cs` | `Client/main.cpp` | scenario | done | C++은 scenario 함수를 한 파일에 둔다. SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3, SF-E1을 구현했다. |
| `Client/Scenarios/SfE1StoreDelayNonBlockingScenario.cs` | `Client/main.cpp`, `Server/Consumer/Endpoints/consumer_endpoints.hpp`, `Server/Shared/location_store.hpp` | scenario | done | consumer store delay admin endpoint와 E2E 전용 delayable location store wrapper로 store read 지연을 주입하고, 지연 중 application request p99와 지연 해제 뒤 recovery request를 검증한다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/main.cpp` | provider-role | done | Redis location store, client-server channel server, runtime status endpoint, evidence endpoint, shutdown/crash endpoint를 구성한다. |
| `Server/Provider/ProviderEndpoints.cs` | `Server/Provider/Handlers/provider_handlers.hpp` | provider-role | done | `/query/status`, `/evidence`, `/evidence/wait`, `/shutdown`, `/admin/crash`를 제공한다. |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/Configuration/provider_options.hpp` | provider-role | done | provider rid, HTTP endpoint, channel endpoint, Redis endpoint/key prefix, log dir를 env로 읽는다. |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/Infrastructure/provider_evidence_store.hpp` | infrastructure | done | provider evidence를 process-local memory에 보관한다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp` | consumer-role | done | Redis location store와 client-server channel client를 구성하고 public query/request endpoint를 연다. runner cleanup은 consumer `/shutdown` endpoint로 정상 종료를 먼저 요청한다. |
| connection transition evidence | `Server/Consumer/Infrastructure/socket_evidence_store.hpp`, `Server/Consumer/Endpoints/consumer_endpoints.hpp` | consumer-evidence | done | client channel의 정식 socket monitoring event를 process-local로 기록하고 `/query/connections`에서 제공해 D1·D2가 survivor reconnect 부재와 dead peer disconnect를 확인한다. |
| `Server/Consumer/PollingOnlyLocationStore.cs` | C++ Redis store 기본 동작 | store-mode | done | C++ Redis extension은 watch surface 없이 change-stamp/polling 기반으로 동작하므로 SF-A2는 polling 경로를 직접 검증한다. |
| framework owner lease join | `framework/src/runtime/locations/live_location_reader.hpp` | runtime-read | done | Redis와 in-memory store는 raw row를 반환한다. framework reader가 store 기준 lease snapshot과 monotonic 경과 시간을 결합해 live peer·spot·actor·route row만 내부 소비자에게 제공한다. |
| `Server/Consumer/DelayableLocationStore.cs` | `Server/Shared/location_store.hpp` | store-mode | done | C++ E2E wrapper가 `location_store_t`와 `location_change_stamp_store_t` 호출을 inner Redis store로 위임하되, delay state가 설정된 동안 해당 store 호출을 늦춘다. 이 지연은 SF-E1 하네스가 store 응답 지연 중 같은 process의 무관 application request가 막히지 않는지 실측하기 위한 전용 구성이다. |
| `Server/Consumer/Support/ConsumerOptions.cs` | `Server/Consumer/Configuration/consumer_options.hpp` | consumer-role | done | consumer HTTP endpoint, Redis endpoint/key prefix, log dir를 env로 읽는다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | loopback Redis container를 띄우고 provider 2개와 consumer 1개를 실행한다. `all`은 parent run이 Redis container 하나를 준비하고 각 scenario child에 endpoint와 container 이름을 넘긴다. 의도된 provider crash scenario만 SIGABRT를 허용하고, cleanup 또는 일반 종료의 비정상 status는 실패로 드러낸다. |
| `*.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_store_failure_provider`, `zlink_cpp_e2e_store_failure_consumer`, `zlink_cpp_e2e_store_failure_client` target이 있다. |

## 제거된 레거시

- `Server/Registry`, `Server/Embedded`, `Server/Probe`는 Config-6 StoreFailure 계약에 맞지 않아 제거했다.
- `Client/Scenarios/dr_*`는 DiscoveryRegistryHa 전용 scenario라 제거하고 SF scenario를 `Client/main.cpp`로 옮겼다.
- CMake의 `zlink_cpp_e2e_discovery_registry_ha_*` registry/embedded/probe target은 제거했고 StoreFailure target으로 대체했다.

## 검증

- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-080111-2298217`(SF-D1), `logs/20260715-080125-2299402`(SF-D2)
  - 의미: 30초 retry와 100ms backoff를 제거한 뒤에도 전체 Config 6이 통과했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-075540-2274183`(SF-D1), `logs/20260715-075554-2275628`(SF-D2)
  - 의미: D1·D2가 outage 전부터 traffic을 실행하고 최대 성공 간격과 socket transition evidence를
    함께 검증했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-074233-2224721`(SF-C1), `logs/20260715-074258-2226349`(SF-C2),
    `logs/20260715-074320-2228448`(SF-D2)
  - 의미: Redis extension은 raw row를 반환하고 framework 공통 reader가 owner lease를 join하는
    책임 경계에서 crash, graceful removal, 장기 장애 복구 scenario가 통과했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-072318-2155219`(SF-B2), 단독 검증은 `logs/20260715-072705-2167572`
  - 의미: SF-B2가 장애 중 새 endpoint의 provider 재기동, grace 초과 전 구간의 기존 연결 유지,
    복구 전 신규 연결 억제와 복구 후 신규 연결을 실제 요청으로 검증했다.
- 2026-07-03: `cmake --build framework/languages/cpp/build-redis-vcpkg --target zlink_cpp_e2e_store_failure_provider zlink_cpp_e2e_store_failure_consumer zlink_cpp_e2e_store_failure_client -j2`
  - 결과: 통과
- 2026-07-03: `ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260703-212414-2415`, `logs/20260703-212420-3257`,
    `logs/20260703-212425-3891`, `logs/20260703-212433-4740`,
    `logs/20260703-212446-6015`, `logs/20260703-212508-7157`,
    `logs/20260703-212513-8240`, `logs/20260703-212523-9016`,
    `logs/20260703-212542-10036`
- 2026-07-07: `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh SF-E1`
  - 결과: 통과
  - 로그: `logs/20260707-190143-3182342`
  - 의미: C++ StoreFailure가 공통 config-6의 Track E store 응답 지연 중 non-blocking application request 검증을 포함한다.
- 2026-07-07: `CMAKE_BUILD_PARALLEL_LEVEL=1 nice -n 10 timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260707-190210-3183591`(SF-A1), `logs/20260707-190227-3185256`(SF-A2),
    `logs/20260707-190236-3186388`(SF-B1), `logs/20260707-190302-3188778`(SF-B2),
    `logs/20260707-190332-3191759`(SF-C1), `logs/20260707-190403-3194237`(SF-C2),
    `logs/20260707-190417-3195710`(SF-D1), `logs/20260707-190453-3199077`(SF-D2),
    `logs/20260707-190530-3201684`(SF-D3), `logs/20260707-190636-3206735`(SF-E1)
  - 의미: SF-A1~SF-E1 전체 sweep가 StoreFailure runner에서 통과했다.
- 2026-07-15: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260715-071009-2105356`(SF-B1), `logs/20260715-071020-2106423`(SF-B2),
    `logs/20260715-071113-2109371`(SF-D1), `logs/20260715-071126-2110473`(SF-D2),
    `logs/20260715-071144-2111560`(SF-D3)
  - 의미: Redis process stop/restart와 빈 store 복구 조건을 포함한 전체 Config 6 실행이 통과했다.
- 2026-07-08: `timeout 1200s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260708-135153-159069`(SF-A1), `logs/20260708-135202-159895`(SF-A2),
    `logs/20260708-135206-160402`(SF-B1), `logs/20260708-135216-161152`(SF-B2),
    `logs/20260708-135231-162310`(SF-C1), `logs/20260708-135254-163218`(SF-C2),
    `logs/20260708-135302-163973`(SF-D1), `logs/20260708-135314-164810`(SF-D2),
    `logs/20260708-135336-165762`(SF-D3), `logs/20260708-135342-166331`(SF-E1)
  - 의미: 전체 sweep가 cleanup status gate를 켠 runner에서 통과했다. `SF-C1`과 `SF-D2`의 provider SIGABRT는 scenario가 명시적으로 만든 failure injection으로만 허용한다. Redis 장애 주입 뒤 async Redis future가 무기한 남아 종료를 막지 않도록 C++ Redis location store operation은 제한 시간 안에 끝나지 않으면 실패를 반환한다.
