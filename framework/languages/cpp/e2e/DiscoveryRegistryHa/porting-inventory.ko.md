# C++ StoreFailure .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/StoreFailure`의 source-only 파일을 기준으로
C++ Config-6 E2E의 대응 파일과 검증 상태를 기록한다. C++ 디렉터리 이름은 아직
`DiscoveryRegistryHa`이지만, 내부 구현과 runner는 StoreFailure 의미로 전환했다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-6-store-failure-recovery.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/StoreFailure`
- C++ 대상: `framework/languages/cpp/e2e/DiscoveryRegistryHa`
- 현재 상태: SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3가 C++ runner proof를 가진다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `Shared/Messages.cs` | `Shared/store_failure_contracts.hpp` | shared | done | channel 이름, profile request/reply, evidence wait, runtime status, peer row DTO가 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | SF-A1~SF-D3 scenario 선택과 public HTTP probe driver가 있다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | client-support | done | env parsing, HTTP GET/POST, Redis pause/unpause, peer/status wait helper가 있다. |
| `Client/Support/SfProbe.cs` | `Client/Support/client_support.hpp` | client-support | done | `/query/status`, `/query/peers`, `/profile/request`, `/health` 기반 probe를 제공한다. |
| `Client/Support/StoreFailureProcessManager.cs` | `run_e2e.sh`, `Client/Support/client_support.hpp` | runner/client-support | done | runner가 provider/consumer/Redis container를 시작하고, client가 public HTTP와 Docker pause/unpause로 장애를 만든다. |
| `Client/Scenarios/*.cs` | `Client/main.cpp` | scenario | done | C++은 scenario 함수를 한 파일에 둔다. SF-A1, SF-A2, SF-B1, SF-B2, SF-C1, SF-C2, SF-D1, SF-D2, SF-D3를 구현했다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/main.cpp` | provider-role | done | Redis location store, client-server channel server, runtime status endpoint, evidence endpoint, shutdown/crash endpoint를 구성한다. |
| `Server/Provider/ProviderEndpoints.cs` | `Server/Provider/Handlers/provider_handlers.hpp` | provider-role | done | `/query/status`, `/evidence`, `/evidence/wait`, `/shutdown`, `/admin/crash`를 제공한다. |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/Configuration/provider_options.hpp` | provider-role | done | provider rid, HTTP endpoint, channel endpoint, Redis endpoint/key prefix, log dir를 env로 읽는다. |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/Infrastructure/provider_evidence_store.hpp` | infrastructure | done | provider evidence를 process-local memory에 보관한다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp` | consumer-role | done | Redis location store와 client-server channel client를 구성하고 public query/request endpoint를 연다. |
| `Server/Consumer/PollingOnlyLocationStore.cs` | C++ Redis store 기본 동작 | store-mode | done | C++ Redis extension은 watch surface 없이 change-stamp/polling 기반으로 동작하므로 SF-A2는 polling 경로를 직접 검증한다. |
| `Server/Consumer/Support/ConsumerOptions.cs` | `Server/Consumer/Configuration/consumer_options.hpp` | consumer-role | done | consumer HTTP endpoint, Redis endpoint/key prefix, log dir를 env로 읽는다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | loopback Redis container를 띄우고 provider 2개와 consumer 1개를 실행한다. `all`은 9개 scenario를 순서대로 실행한다. |
| `*.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_store_failure_provider`, `zlink_cpp_e2e_store_failure_consumer`, `zlink_cpp_e2e_store_failure_client` target이 있다. |

## 제거된 레거시

- `Server/Registry`, `Server/Embedded`, `Server/Probe`는 Config-6 StoreFailure 계약에 맞지 않아 제거했다.
- `Client/Scenarios/dr_*`는 DiscoveryRegistryHa 전용 scenario라 제거하고 SF scenario를 `Client/main.cpp`로 옮겼다.
- CMake의 `zlink_cpp_e2e_discovery_registry_ha_*` registry/embedded/probe target은 제거했고 StoreFailure target으로 대체했다.

## 검증

- 2026-07-03: `cmake --build framework/languages/cpp/build-redis-vcpkg --target zlink_cpp_e2e_store_failure_provider zlink_cpp_e2e_store_failure_consumer zlink_cpp_e2e_store_failure_client -j2`
  - 결과: 통과
- 2026-07-03: `ZLINK_CPP_E2E_BUILD_DIR=/home/hep7/project/kairos/zlink/framework/languages/cpp/build-redis-vcpkg timeout 900s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260703-212414-2415`, `logs/20260703-212420-3257`,
    `logs/20260703-212425-3891`, `logs/20260703-212433-4740`,
    `logs/20260703-212446-6015`, `logs/20260703-212508-7157`,
    `logs/20260703-212513-8240`, `logs/20260703-212523-9016`,
    `logs/20260703-212542-10036`
