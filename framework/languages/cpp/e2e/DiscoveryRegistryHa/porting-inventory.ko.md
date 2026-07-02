# C++ DiscoveryRegistryHa .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/DiscoveryRegistryHa`의 source-only 파일을 기준으로
C++ `DiscoveryRegistryHa` E2E의 대응 파일과 검증 상태를 기록한다. `bin`, `obj`, `logs` 산출물은 기준
role로 세지 않는다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-6-discovery-registry-ha.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/DiscoveryRegistryHa`
- C++ 대상: `framework/languages/cpp/e2e/DiscoveryRegistryHa`
- 현재 상태: DR-A1~DR-D4는 C++ source role과 runner proof가 있다.

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | C++ 실행 로그를 제외한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | DR-A1~DR-D4 구현 상태와 최신 proof를 기록한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | `DR-A1`~`DR-D4`를 실제 프로세스로 띄운다. `all`은 모든 scenario를 실행한 뒤 passed marker로 끝난다. |
| `Shared/Messages.cs` | `Shared/discovery_registry_ha_contracts.hpp` | shared | done | channel 이름, profile request/reply, topology wait/snapshot, member endpoint wait, registry peer-count wait, evidence DTO가 있다. |
| `Shared/DiscoveryRegistryHa.Shared.csproj` | `Shared/discovery_registry_ha_contracts.hpp` | build | not-needed | C++ shared contract는 별도 프로젝트 파일 없이 header로 포함한다. |
| `Client/DiscoveryRegistryHa.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_client` target이 있다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | DR-A1~DR-D4 scenario 선택과 HTTP-only driver 진입점이 있다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp` | client-support | done | env parsing, HTTP typed GET/POST, marker/evidence 검증 helper가 있다. |
| `Client/Support/DiscoveryApiResult.cs` | `Client/Support/client_support.hpp` | client-support | done | topology/member/status wait response를 HTTP response body와 상태 코드로 검증한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp` | client-support | done | `ensure` helper가 C++ support header에 있다. |
| `Client/Scenarios/BasicDiscoveryScenario.cs` | `Client/Scenarios/dr_a1_basic_discovery_scenario.hpp` | scenario | done | DR-A1 단일 registry baseline을 HTTP driver로 직접 검증한다. |
| `Client/Scenarios/DrA2ClusterBridgeScenario.cs` | `Client/Scenarios/dr_a2_cluster_bridge_scenario.hpp` | scenario | done | DR-A2 two-registry bridge를 HTTP-only client driver로 검증한다. reg-2의 member endpoint wait, consumer request, provider evidence를 확인한다. |
| `Client/Scenarios/DrA3ClusterBridgeScenario.cs` | `Client/Scenarios/dr_a3_cluster_bridge_scenario.hpp` | scenario | done | DR-A3 3 registry peer 합산 view를 HTTP-only client driver로 검증한다. 세 registry의 connected peer count, A/B member endpoint, consumer request, provider evidence를 확인한다. |
| `Client/Scenarios/DrA4ThirdRegistryScenario.cs` | `Client/Scenarios/dr_a4_third_registry_scenario.hpp` | scenario | done | DR-A4 same-rid duplicate provider를 HTTP-only client driver로 검증한다. reg-2 consumer의 bounded request wait와 두 provider 중 하나의 evidence를 확인한다. |
| `Client/Scenarios/DrB1FailoverScenario.cs` | `Client/Scenarios/dr_b1_late_start_registry_scenario.hpp` | scenario | done | DR-B1 late-start registry 합류를 HTTP-only client driver로 검증한다. reg-2/reg-3의 member endpoint wait, 각 consumer request, provider evidence를 확인한다. |
| `Client/Scenarios/DrB2FailoverScenario.cs` | `Client/Scenarios/dr_b2_live_endpoint_continuity_scenario.hpp` | scenario | done | DR-B2 stopped registry + live endpoint 조합을 HTTP-only client driver로 검증한다. reg-2 중지 뒤 reg-1/reg-2 endpoint를 함께 가진 consumer request와 provider evidence를 확인한다. |
| `Client/Scenarios/DrB3RecoveryScenario.cs` | `Client/Scenarios/dr_b3_peer_flapping_scenario.hpp` | scenario | done | DR-B3 peer flapping/recovery를 HTTP-only client driver로 검증한다. |
| `Client/Scenarios/DrC1EmbeddedRegistryScenario.cs` | `Client/Scenarios/dr_c1_registry_down_scenario.hpp` | scenario | done | DR-C1 registry 1대 장애 중 live registry request/evidence와 dead registry bounded failure를 검증한다. |
| `Client/Scenarios/DrC2EmbeddedRegistryScenario.cs` | `Client/Scenarios/dr_c2_registry_recovery_scenario.hpp` | scenario | done | DR-C2 registry 복구 후 reg-2 member view와 messaging 복구를 검증한다. |
| `Client/Scenarios/DrC3EmbeddedRegistryScenario.cs` | `Client/Scenarios/dr_c3_all_registry_outage_scenario.hpp` | scenario | done | full registry outage 중 established channel request와 provider evidence를 검증한다. |
| `Client/Scenarios/DrD1DirectEndpointScenario.cs` | `Client/Scenarios/dr_d1_embedded_deployment_scenario.hpp` | scenario | done | DR-D1 embedded registry+provider scenario를 검증한다. |
| `Client/Scenarios/DrD2DirectEndpointScenario.cs` | `Client/Scenarios/dr_d2_standalone_deployment_scenario.hpp` | scenario | done | DR-D2 standalone registry deployment scenario를 검증한다. |
| `Client/Scenarios/DrD3DirectEndpointScenario.cs` | `Client/Scenarios/dr_d3_mixed_deployment_scenario.hpp` | scenario | done | DR-D3 embedded+standalone 혼합 cluster scenario를 검증한다. |
| `Client/Scenarios/DrD4DirectEndpointScenario.cs` | `Client/Scenarios/dr_d4_topology_query_scenario.hpp` | scenario | done | DR-D4 in-process/remote topology snapshot 비교 scenario를 검증한다. |
| `Server/Consumer/ConsumerOptions.cs` | `Server/Consumer/Configuration/consumer_options.hpp` | consumer-role | done | consumer HTTP endpoint와 registry router endpoint option parsing이 있다. |
| `Server/Consumer/ConsumerHostFactory.cs` | `Server/Consumer/main.cpp` | consumer-role | done | DR-A1~DR-D4 discovery consumer app 구성이 있다. |
| `Server/Consumer/Program.cs` | `Server/Consumer/main.cpp` | consumer-entry | done | consumer role executable 진입점이 있다. |
| `Server/Consumer/DiscoveryRegistryHa.Consumer.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_consumer` target이 있다. |
| `Server/Embedded/EmbeddedHandlers.cs` | `Server/Embedded/main.cpp`, `Server/Provider/Handlers/provider_handlers.hpp` | handler | done | embedded role은 기존 provider handler/evidence store를 재사용한다. |
| `Server/Embedded/EmbeddedHostFactory.cs` | `Server/Embedded/main.cpp` | embedded-role | done | registry+provider embedded app 구성이 있다. |
| `Server/Embedded/Program.cs` | `Server/Embedded/main.cpp` | embedded-entry | done | embedded role executable 진입점이 있다. |
| `Server/Embedded/Support/EmbeddedEvidenceStore.cs` | `Server/Provider/Infrastructure/provider_evidence_store.hpp` | infrastructure | done | embedded role은 provider evidence store를 재사용한다. |
| `Server/Embedded/Support/EmbeddedOptions.cs` | `Server/Embedded/Configuration/embedded_options.hpp` | embedded-role | done | embedded registry/provider endpoint option parsing이 있다. |
| `Server/Embedded/DiscoveryRegistryHa.Embedded.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_embedded` target이 있다. |
| `Server/Probe/ProbeHostFactory.cs` | `Server/Probe/main.cpp` | probe-role | done | registry query probe app 구성이 있다. |
| `Server/Probe/ProbeOptions.cs` | `Server/Probe/Configuration/probe_options.hpp` | probe-role | done | probe HTTP endpoint와 registry router endpoint option parsing이 있다. |
| `Server/Probe/Program.cs` | `Server/Probe/main.cpp` | probe-entry | done | probe role executable 진입점이 있다. |
| `Server/Probe/DiscoveryRegistryHa.Probe.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_probe` target이 있다. |
| `Server/Provider/ProviderHandlers.cs` | `Server/Provider/Handlers/provider_handlers.hpp` | handler | done | profile request handler와 evidence wait handler가 있다. |
| `Server/Provider/ProviderHostFactory.cs` | `Server/Provider/main.cpp` | provider-role | done | DR-A1~DR-D4 provider app 구성이 있다. DR-A4는 같은 `rid=api-a` provider 두 개를 서로 다른 endpoint로 띄우되 log file label은 분리한다. |
| `Server/Provider/Program.cs` | `Server/Provider/main.cpp` | provider-entry | done | provider role executable 진입점이 있다. |
| `Server/Provider/Support/ProviderEvidenceStore.cs` | `Server/Provider/Infrastructure/provider_evidence_store.hpp` | infrastructure | done | provider evidence store가 있다. |
| `Server/Provider/Support/ProviderOptions.cs` | `Server/Provider/Configuration/provider_options.hpp` | provider-role | done | provider rid, HTTP, channel, registry endpoint option parsing이 있다. |
| `Server/Provider/DiscoveryRegistryHa.Provider.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_provider` target이 있다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp` | registry-role | done | registry app 구성과 public remote member/status/topology query endpoint가 있다. |
| `Server/Registry/RegistryOptions.cs` | `Server/Registry/Configuration/registry_options.hpp` | registry-role | done | registry HTTP, pub, router, peer endpoint option parsing이 있다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | registry-entry | done | registry role executable 진입점이 있다. |
| `Server/Registry/DiscoveryRegistryHa.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_discovery_registry_ha_registry` target이 있다. |

## 공통 scenario ID 대응

| Scenario ID | C++ 대응 파일 | 상태 | 비고 |
|-------------|---------------|------|------|
| `DR-A1` | `Client/Scenarios/dr_a1_basic_discovery_scenario.hpp` | done | 단일 registry baseline scenario가 HTTP-only client driver로 통과한다. |
| `DR-A2` | `Client/Scenarios/dr_a2_cluster_bridge_scenario.hpp` | done | 2 registry peer 합산 view와 cross-registry messaging을 public member query와 provider evidence로 검증한다. |
| `DR-A3` | `Client/Scenarios/dr_a3_cluster_bridge_scenario.hpp` | done | 3 registry peer 합산 view, connected peer count, cross-registry messaging을 public status/member query와 provider evidence로 검증한다. |
| `DR-A4` | `Client/Scenarios/dr_a4_third_registry_scenario.hpp` | done | same-rid duplicate provider 광고 뒤 reg-2 consumer request가 bounded wait 안에 성공하고 provider evidence를 남기는지 검증한다. |
| `DR-B1` | `Client/Scenarios/dr_b1_late_start_registry_scenario.hpp` | done | late-start registry 합류 뒤 reg-2/reg-3 member endpoint wait와 consumer request/provider evidence를 검증한다. |
| `DR-B2` | `Client/Scenarios/dr_b2_live_endpoint_continuity_scenario.hpp` | done | stopped registry endpoint와 live registry endpoint를 함께 가진 consumer request가 살아 있는 provider endpoint로 완료되고 provider evidence를 남기는지 검증한다. |
| `DR-B3` | `Client/Scenarios/dr_b3_peer_flapping_scenario.hpp` | done | peer flapping/recovery scenario가 통과한다. |
| `DR-C1` | `Client/Scenarios/dr_c1_registry_down_scenario.hpp` | done | registry 1대 장애 scenario가 통과한다. |
| `DR-C2` | `Client/Scenarios/dr_c2_registry_recovery_scenario.hpp` | done | registry 복구 scenario가 통과한다. |
| `DR-C3` | `Client/Scenarios/dr_c3_all_registry_outage_scenario.hpp` | done | full registry outage 중 established channel request와 provider evidence를 검증한다. |
| `DR-D1` | `Client/Scenarios/dr_d1_embedded_deployment_scenario.hpp` | done | embedded deployment scenario가 통과한다. |
| `DR-D2` | `Client/Scenarios/dr_d2_standalone_deployment_scenario.hpp` | done | standalone deployment scenario가 통과한다. |
| `DR-D3` | `Client/Scenarios/dr_d3_mixed_deployment_scenario.hpp` | done | mixed deployment scenario가 통과한다. |
| `DR-D4` | `Client/Scenarios/dr_d4_topology_query_scenario.hpp` | done | topology query parity scenario가 통과한다. |

## 검증

- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B3`
  - 결과: 통과
  - 로그: `logs/20260702-085923-98682`
  - 의미: restarted reg-2와 survivor reg-1 모두 provider member를 관측하고 consumer request/provider
    evidence를 완료했다.
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-C1`
  - 결과: 통과
  - 로그: `logs/20260702-085945-99479`
  - 의미: reg-2 중지 중 reg-1 기준 request/evidence가 성공하고 reg-2 `/health` bounded failure를 확인했다.
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-C2`
  - 결과: 통과
  - 로그: `logs/20260702-090002-465`
  - 의미: reg-2 재시작 뒤 member view와 reg-2 consumer request/provider evidence가 복구됐다.
- 2026-07-02: `cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging && timeout 120s framework/languages/cpp/build/test_cpp_framework_channel_messaging`
  - 결과: 통과
  - 의미: framework channel request가 registry 조회로 확인한 discovery endpoint를 보존하고, registry
    outage 중에는 보존한 endpoint로 request/reply를 완료하며, provider 중지 뒤 stale endpoint는 timeout으로
    빠르게 실패하는 regression을 검증했다.
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh dr-c3`
  - 결과: 통과
  - 로그: `logs/20260702-092213-28860`
  - 의미: before phase와 full registry outage during phase 모두 consumer request와 provider evidence를
    완료했다.
- 2026-07-02: `timeout 420s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260702-092354-30903`, `logs/20260702-092402-31420`, `logs/20260702-092409-31920`,
    `logs/20260702-092418-32708`, `logs/20260702-092426-33265`, `logs/20260702-092440-34127`,
    `logs/20260702-092454-34756`, `logs/20260702-092513-35596`, `logs/20260702-092527-36224`,
    `logs/20260702-092546-36949`, `logs/20260702-092610-37998`, `logs/20260702-092618-38420`,
    `logs/20260702-092625-38937`, `logs/20260702-092633-39492`
  - 의미: DR-A1~DR-D4가 모두 passed marker를 남기고
    `discovery-registry-ha c++ e2e result=passed`로 끝났다.
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D1`
  - 결과: 통과
  - 로그: `logs/20260702-091123-11905`
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D2`
  - 결과: 통과
  - 로그: `logs/20260702-090024-1357`
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D3`
  - 결과: 통과
  - 로그: `logs/20260702-091135-12373`
- 2026-07-02: `timeout 180s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-D4`
  - 결과: 통과
  - 로그: `logs/20260702-091147-13042`
- 2026-07-02: `timeout 420s framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-091202-13581`, `logs/20260702-091210-14084`, `logs/20260702-091217-14606`,
    `logs/20260702-091226-15378`, `logs/20260702-091234-15953`, `logs/20260702-091248-16779`,
    `logs/20260702-091302-17380`, `logs/20260702-091321-18215`, `logs/20260702-091335-18832`,
    `logs/20260702-091354-19600`, `logs/20260702-091401-20020`, `logs/20260702-091409-20509`,
    `logs/20260702-091417-21047`
  - 의미: DR-A1~DR-B3, DR-C1, DR-C2, DR-D1~DR-D4가 모두 passed marker를 남긴 뒤, DR-C3 runtime
    gap을 숨기지 않기 위해 `discovery-registry-ha c++ e2e result=partial`과 exit 2로 끝난다.
- 2026-07-02: source-only inventory를 추가했다.
  - 결과: partial
  - 의미: `.NET` source role과 scenario 파일은 C++ gap으로 명시되었고, 아직 runner proof는 없다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A1`
  - 결과: 통과
  - 로그: `logs/20260702-050447-75075`
  - 의미: registry, provider 2개, consumer, HTTP-only client driver가 실제 프로세스로 실행됐고
    `scenario DiscoveryRegistryHa.A1 passed`, `discovery-registry-ha client scenario=DR-A1 result=passed`,
    `discovery-registry-ha c++ scenario=DR-A1 result=passed` marker가 남았다. `consumer-a1-flow.log`와
    provider flow log에는 `discovery.registry.ha.profile` channel request/reply evidence가 남았다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-050506-76898`
  - 의미: 이 시점의 `all`은 당시 구현된 DR-A1을 실행한 뒤
    `discovery-registry-ha c++ e2e result=partial`을 출력하고 exit 2로 끝났다. 나머지 DR scenario
    gap을 통과로 숨기지 않는다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A1`
  - 결과: 통과
  - 로그: `logs/20260702-052949-93630`
  - 의미: DR-A1이 최신 public member query 추가 뒤에도 실제 프로세스 runner로 통과했다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A2`
  - 결과: 통과
  - 로그: `logs/20260702-052958-94893`
  - 의미: reg-1/reg-2 peer 구성에서 reg-2의 `/registry/members/wait`가 reg-1 provider endpoint를
    관측했고, consumer request와 provider evidence marker가 함께 통과했다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-053013-97738`, `logs/20260702-053021-99559`
  - 의미: 이 시점의 `all`은 당시 구현된 DR-A1과 DR-A2를 실행한 뒤
    `discovery-registry-ha c++ e2e result=partial`을 출력하고 exit 2로 끝난다. 나머지 DR scenario
    gap을 통과로 숨기지 않는다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A3`
  - 결과: 통과
  - 로그: `logs/20260702-055413-14835`
  - 의미: reg-1/reg-2/reg-3 peer 구성에서 세 registry 모두 connected peer count 2와 api-a/api-b member
    endpoint를 관측했고, 각 registry만 보는 consumer request와 provider evidence marker가 함께 통과했다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-055429-16407`, `logs/20260702-055437-17388`, `logs/20260702-055444-18329`
  - 의미: 이 시점의 `all`은 당시 구현된 DR-A1, DR-A2, DR-A3를 실행한 뒤
    `discovery-registry-ha c++ e2e result=partial`을 출력하고 exit 2로 끝난다. 나머지 DR scenario gap을
    통과로 숨기지 않는다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-A4`
  - 결과: 통과
  - 로그: `logs/20260702-060253-40055`
  - 의미: 같은 `rid=api-a` provider를 reg-1/reg-2에 서로 다른 endpoint로 광고한 뒤, reg-2 consumer의
    `/profile/request/wait` request가 bounded wait 안에 성공했고 provider evidence marker가 남았다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-060306-41059`, `logs/20260702-060313-42479`, `logs/20260702-060321-43640`,
    `logs/20260702-060330-44627`
  - 의미: `all`은 현재 구현된 DR-A1, DR-A2, DR-A3, DR-A4를 실행한 뒤
    `discovery-registry-ha c++ e2e result=partial`을 출력하고 exit 2로 끝난다. 나머지 DR scenario gap을
    통과로 숨기지 않는다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B1`
  - 결과: 통과
  - 로그: `logs/20260702-060821-63399`
  - 의미: reg-1을 먼저 띄운 뒤 phase1 provider를 종료하고 reg-2/reg-3 late-start 합류를 실행했다.
    client는 reg-2/reg-3의 `/registry/members/wait`로 api-a/api-b endpoint 관측을 확인했고, 두 late
    registry consumer request가 provider HTTP evidence wait와 함께 통과했다. `consumer-reg2-flow.log`,
    `consumer-reg3-flow.log`, `api-a-flow.log`에 request/reply flow marker가 남았다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-060909-65794`, `logs/20260702-060917-66333`, `logs/20260702-060924-67003`,
    `logs/20260702-060933-68513`, `logs/20260702-060942-69119`
  - 의미: `all`은 현재 구현된 DR-A1, DR-A2, DR-A3, DR-A4, DR-B1을 실행한 뒤
    `discovery-registry-ha c++ e2e result=partial`과 `Only DR-A1, DR-A2, DR-A3, DR-A4, and DR-B1 are
    implemented in the current C++ port.`를 출력하고 exit 2로 끝난다. 나머지 DR scenario gap을 통과로
    숨기지 않는다.
- 2026-07-02: `timeout 420s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B2`
  - 결과: 실패, 수동 종료
  - 로그: `logs/20260702-061247-81702`, `logs/20260702-061803-992`
  - 의미: reg-2를 중지한 뒤 reg-1/reg-2 endpoint를 함께 가진 consumer가 `/profile/request`를 받았고
    `consumer-reg1-reg2-flow.log`에는 channel request sent가 남았다. 하지만 `api-a-flow.log`와
    `api-b-flow.log`에는 provider received/replied marker가 없고 client가 완료되지 않았다. 이는 DR-B2를
    sample retry로 숨길 수 없는 framework discovery failover gap이다.
- 2026-07-02: `./framework/languages/cpp/build/test_cpp_framework_channel_messaging`
  - 결과: 통과, exit 0
  - 의미: DR-B2 조사 중 시도했던 불완전한 channel outbound 변경을 되돌린 뒤 기존 channel messaging
    regression target이 다시 통과한다.
- 2026-07-02: `./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-062140-16892`, `logs/20260702-062148-18141`, `logs/20260702-062157-18820`,
    `logs/20260702-062205-20008`, `logs/20260702-062214-21018`
  - 의미: `all`은 현재 구현된 DR-A1, DR-A2, DR-A3, DR-A4, DR-B1을 실행한 뒤 partial로 끝난다. DR-B2와
    이후 scenario는 gap으로 남아 있으며, runner가 이를 통과로 숨기지 않는다.
- 2026-07-02: `cmake --build framework/languages/cpp/build --target test_cpp_framework_channel_messaging && timeout 20s ./framework/languages/cpp/build/test_cpp_framework_channel_messaging`
  - 결과: 통과, exit 0
  - 의미: framework channel client가 live registry endpoint와 dead registry endpoint를 함께 가진 상태에서도
    public request timeout 안에서 살아 있는 registry/provider로 요청을 완료하고, provider stop 뒤 stale
    discovery endpoint 요청은 timeout으로 빠르게 실패한다.
- 2026-07-02: `timeout 90s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh DR-B2`
  - 결과: 통과, exit 0
  - 로그: `logs/20260702-063352-72918`
  - 의미: reg-2를 중지한 뒤 reg-1/reg-2 endpoint를 함께 가진 consumer가 `/profile/request`를 처리했고,
    client가 provider evidence까지 확인했다. `scenario DR-B2 passed`,
    `discovery-registry-ha client scenario=DR-B2 result=passed`,
    `discovery-registry-ha c++ scenario=DR-B2 result=passed` marker가 남았다.
- 2026-07-02: `timeout 180s ./framework/languages/cpp/e2e/DiscoveryRegistryHa/run_e2e.sh all`
  - 결과: partial exit 2
  - 로그: `logs/20260702-063532-79106`, `logs/20260702-063540-80554`, `logs/20260702-063550-83743`,
    `logs/20260702-063600-85219`, `logs/20260702-063609-86494`, `logs/20260702-063624-87673`
  - 의미: `all`은 현재 구현된 DR-A1, DR-A2, DR-A3, DR-A4, DR-B1, DR-B2를 모두 실행하고 각 scenario
    passed marker를 남긴 뒤, 이후 scenario gap을 숨기지 않기 위해
    `discovery-registry-ha c++ e2e result=partial`과 exit 2로 끝난다.
