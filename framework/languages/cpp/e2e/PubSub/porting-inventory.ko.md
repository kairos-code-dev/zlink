# C++ PubSub .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/PubSub`의 파일을 기준으로 C++ `PubSub` E2E의 대응 파일과
남은 gap을 기록한다. C++ 구현은 `.NET` 기준처럼 Registry, Publisher, Subscriber 역할을 별도 실행
파일로 분리한다. 다만 push 수신 검증은 아직 subscriber HTTP evidence polling에 의존하므로 `.NET`
feature-map과 같은 검증 경로 gap을 남긴다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/PubSub`
- C++ 대상: `framework/languages/cpp/e2e/PubSub`

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 실행 로그를 제외한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | `.NET`과 같은 push 검증 gap을 부분 구현 상태로 명시한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | registry/publisher/subscriber/client process orchestration을 분리하고, `all` 또는 개별 PS scenario ID 실행을 지원한다. |
| `Shared/Messages.cs` | `Shared/pubsub_contracts.hpp` | shared | done | event/accepted evidence/ignored evidence/dispatch-error DTO가 대응된다. |
| `Shared/PubSub.Shared.csproj` | `Shared/pubsub_contracts.hpp` | build | not-needed | C++ shared contract는 header로 포함된다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | scenario dispatch만 담당하고 Publisher role HTTP endpoint를 호출한다. |
| `Client/PubSub.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_pubsub_client` target이 대응한다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp`; `run_e2e.sh` | client-support | done | env parsing, marker 대기, Publisher HTTP 호출 helper가 대응한다. |
| `Client/Support/Evidence.cs` | `run_e2e.sh` | client-support | partial | evidence polling/verification은 runner Python helper에 있다. push 검증 gap과 함께 남긴다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp`; `run_e2e.sh` | client-support | partial | client process assert와 runner evidence assert가 분리되어 있다. push 검증 gap과 함께 남긴다. |
| `Client/Support/ServerProcessLauncher.cs` | `run_e2e.sh` | runner-support | done | 프로세스 시작/정지/재시작은 shell runner가 담당한다. |
| `Client/Scenarios/FanoutBasicDeliveryScenario.cs` | `Client/Scenarios/fanout_basic_delivery_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-A1 발행 흐름은 분리됐다. push 수신 검증은 runner evidence polling에 남아 있다. |
| `Client/Scenarios/TopicFilterScenario.cs` | `Client/Scenarios/topic_filter_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-A2 발행 흐름은 분리됐다. push 수신 검증은 runner evidence polling에 남아 있다. |
| `Client/Scenarios/LateSubscriberScenario.cs` | `Client/Scenarios/late_subscriber_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-A3 발행 흐름은 분리됐다. late subscriber orchestration은 runner가 담당한다. |
| `Client/Scenarios/SubscriberReconnectScenario.cs` | `Client/Scenarios/subscriber_reconnect_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-A4 발행 흐름은 분리됐다. subscriber restart orchestration은 runner가 담당한다. |
| `Client/Scenarios/SlowSubscriberScenario.cs` | `Client/Scenarios/slow_subscriber_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-B1 발행 흐름은 분리됐다. push 수신 검증은 runner evidence polling에 남아 있다. |
| `Client/Scenarios/PublisherRestartScenario.cs` | `Client/Scenarios/publisher_restart_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-B2 발행 흐름은 분리됐고 Publisher role server를 재시작한다. push 수신 검증은 runner evidence polling에 남아 있다. |
| `Client/Scenarios/MissingMessageNameScenario.cs` | `Client/Scenarios/missing_message_name_scenario.hpp`; `run_e2e.sh` | scenario | partial | PS-C1 negative 발행 흐름은 분리됐다. error evidence 검증은 runner polling에 남아 있다. |
| `Server/Registry/Configuration/HostFactorySupport.cs` | `Server/Shared/server_support.hpp` | server-role | done | 공통 logging/codec/flow helper가 대응한다. |
| `Server/Registry/Configuration/RegistryOptions.cs` | `Server/Registry/Configuration/registry_options.hpp`; `run_e2e.sh` | configuration | done | registry endpoint/env parsing이 대응한다. |
| `Server/Registry/Configuration/ServerArgs.cs` | `run_e2e.sh` | configuration | done | C++ runner env가 서버 인자 역할을 담당한다. |
| `Server/Registry/EvidenceStore.cs` | not-needed | infrastructure | not-needed | 현재 PubSub C++ registry role은 evidence를 판정에 쓰지 않는다. 필요하면 별도 gap으로 승격한다. |
| `Server/Registry/OperationalEndpoints.cs` | `Server/Registry/main.cpp` | endpoint | done | health endpoint는 role entry에서 등록한다. |
| `Server/Registry/Program.cs` | `Server/Registry/main.cpp` | server-entry | done | registry 전용 executable이다. |
| `Server/Registry/RegistryHostFactory.cs` | `Server/Registry/main.cpp`; `Server/Shared/server_support.hpp` | server-role | done | registry framework 구성이 role entry에 있다. |
| `Server/Registry/PubSub.Registry.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_pubsub_registry` target이 대응한다. |
| `Server/Publisher/Configuration/HostFactorySupport.cs` | `Server/Shared/server_support.hpp` | server-role | done | 공통 logging/codec/flow helper가 대응한다. |
| `Server/Publisher/Configuration/PublisherOptions.cs` | `Server/Publisher/Configuration/publisher_options.hpp`; `run_e2e.sh` | configuration | done | publisher endpoint/log/registry/http options가 대응한다. |
| `Server/Publisher/Configuration/ServerArgs.cs` | `run_e2e.sh` | configuration | done | runner env orchestration이 인자 역할을 담당한다. |
| `Server/Publisher/Endpoints/OperationalEndpoints.cs` | `Server/Publisher/main.cpp` | endpoint | done | Publisher health endpoint는 role entry에서 등록한다. |
| `Server/Publisher/Endpoints/PublisherEndpoints.cs` | `Server/Publisher/Endpoints/publisher_endpoints.hpp` | endpoint | done | `/publish/event`와 `/publish/missing` endpoint가 Publisher role에서 framework publish를 실행한다. |
| `Server/Publisher/EvidenceDispatchErrorObserver.cs` | not-needed | handler | not-needed | 현재 C++ Publisher role evidence는 판정에 쓰지 않는다. subscriber dispatch error evidence로 PS-C1을 확인한다. |
| `Server/Publisher/EvidenceStore.cs` | not-needed | infrastructure | not-needed | 현재 C++ Publisher role evidence는 판정에 쓰지 않는다. |
| `Server/Publisher/Program.cs` | `Server/Publisher/main.cpp` | server-entry | done | publisher 전용 executable이다. |
| `Server/Publisher/PublisherHostFactory.cs` | `Server/Publisher/main.cpp`; `Server/Shared/server_support.hpp` | server-role | done | publisher framework 구성이 role entry에 있다. |
| `Server/Publisher/PubSub.Publisher.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_pubsub_publisher` target이 대응한다. |
| `Server/Subscriber/Configuration/HandlerDelayOptions.cs` | `Server/Subscriber/Configuration/subscriber_options.hpp`; `run_e2e.sh` | configuration | done | handler delay env parsing이 대응한다. |
| `Server/Subscriber/Configuration/HostFactorySupport.cs` | `Server/Shared/server_support.hpp` | server-role | done | 공통 logging/codec/flow helper가 대응한다. |
| `Server/Subscriber/Configuration/ServerArgs.cs` | `run_e2e.sh` | configuration | done | runner env orchestration이 인자 역할을 담당한다. |
| `Server/Subscriber/Configuration/SubscriberOptions.cs` | `Server/Subscriber/Configuration/subscriber_options.hpp`; `run_e2e.sh` | configuration | done | subscriber id/topic/http options가 대응한다. |
| `Server/Subscriber/EvidenceStore.cs` | `Server/Subscriber/Infrastructure/evidence_store.hpp` | infrastructure | done | subscriber accepted/ignored/error evidence store가 대응한다. |
| `Server/Subscriber/Handlers/EventNotifyHandler.cs` | `Server/Subscriber/Handlers/event_notify_handler.hpp` | handler | done | topic별 publish handler가 대응한다. |
| `Server/Subscriber/Handlers/EvidenceDispatchErrorObserver.cs` | `Server/Subscriber/Infrastructure/evidence_store.hpp`; `Server/Subscriber/main.cpp` | handler | done | dispatch observer가 evidence store에 error marker를 기록한다. |
| `Server/Subscriber/OperationalEndpoints.cs` | `Server/Subscriber/Endpoints/operational_endpoints.hpp` | endpoint | done | `/evidence` endpoint가 대응한다. |
| `Server/Subscriber/Program.cs` | `Server/Subscriber/main.cpp` | server-entry | done | subscriber 전용 executable이다. |
| `Server/Subscriber/SubscriberHostFactory.cs` | `Server/Subscriber/main.cpp`; `Server/Shared/server_support.hpp` | server-role | done | subscriber framework 구성이 role entry에 있다. |
| `Server/Subscriber/PubSub.Subscriber.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_pubsub_subscriber` target이 대응한다. |

## 공통 scenario ID 대응

| Scenario ID | C++ 대응 파일 | 상태 | 비고 |
|-------------|---------------|------|------|
| `PS-A1` | `Client/Scenarios/fanout_basic_delivery_scenario.hpp`; `run_e2e.sh` | partial | fanout 발행과 evidence 검증을 수행한다. push 검증 gap은 남아 있다. |
| `PS-A2` | `Client/Scenarios/topic_filter_scenario.hpp`; `run_e2e.sh` | partial | 관심 topic accepted evidence와 비관심 topic ignored evidence를 검증한다. push 검증 gap은 남아 있다. |
| `PS-A3` | `Client/Scenarios/late_subscriber_scenario.hpp`; `run_e2e.sh` | partial | late subscriber 합류 흐름을 검증한다. push 검증 gap은 남아 있다. |
| `PS-A4` | `Client/Scenarios/subscriber_reconnect_scenario.hpp`; `run_e2e.sh` | partial | subscriber reconnect 흐름을 검증한다. push 검증 gap은 남아 있다. |
| `PS-B1` | `Client/Scenarios/slow_subscriber_scenario.hpp`; `run_e2e.sh` | partial | slow subscriber 격리 흐름을 검증한다. push 검증 gap은 남아 있다. |
| `PS-B2` | `Client/Scenarios/publisher_restart_scenario.hpp`; `run_e2e.sh` | partial | Publisher role server restart 이후에만 발행한 값을 검증한다. push 검증 gap은 남아 있다. |
| `PS-C1` | `Client/Scenarios/missing_message_name_scenario.hpp`; `run_e2e.sh` | partial | missing message name error와 후속 정상 publish를 검증한다. push 검증 gap은 남아 있다. |

## 검증

- 2026-06-30: `./framework/languages/cpp/e2e/PubSub/run_e2e.sh all`
  - 결과: 통과
  - 로그: `logs/20260630-082052-3253217`
  - 의미: 현재 C++ PubSub의 PS-A1, PS-A2, PS-A3, PS-A4, PS-B1, PS-B2, PS-C1 흐름은 모두
    통과한다. 다만 공통 E2E README의 stream push 검증 경로는 아직 없으므로 각 scenario의
    완료 상태는 `.NET` 기준과 같이 `partial`로 유지한다.
