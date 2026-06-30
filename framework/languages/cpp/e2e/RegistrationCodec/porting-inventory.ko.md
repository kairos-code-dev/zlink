# C++ RegistrationCodec .NET 기준 포팅 inventory

이 문서는 `framework/languages/dotnet/e2e/RegistrationCodec`의 파일을 기준으로 C++
`RegistrationCodec` E2E의 대응 파일과 남은 gap을 기록한다. 현재 C++ 구현은 client scenario/support와
server configuration/handler/endpoint/support를 분리했고, invalid role과 JSON-only peer role도 별도
executable로 분리한다.

`.NET` 전용 attribute 등록은 C++ framework의 현재 public contract에 동일한 표면이 없으므로 새 public
API나 테스트 전용 adapter를 추가하지 않고 feature-map에서 제외한다. Protobuf/MessagePack은 C++ public
codec extension을 사용해 실제 E2E로 검증한다.

## 기준

- 공통 문서: `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`
- .NET 기준 구현: `framework/languages/dotnet/e2e/RegistrationCodec`
- C++ 대상: `framework/languages/cpp/e2e/RegistrationCodec`

## 파일 매핑

| .NET 기준 파일 | C++ 대응 파일 | 분류 | 상태 | 비고 |
|----------------|---------------|------|------|------|
| `.gitignore` | `.gitignore` | config | done | 실행 로그를 제외한다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | C++ public API로 검증한 항목과 C++ public contract gap을 분리한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | server/client, invalid role, JSON-only peer role을 각각 별도 executable로 실행한다. |
| `Shared/Messages.cs` | `Shared/registration_codec_contracts.hpp` | shared | done | JSON/Protobuf/MessagePack/custom serializer용 DTO와 evidence DTO가 대응한다. |
| `Shared/RegistrationCodec.Shared.csproj` | `Shared/registration_codec_contracts.hpp` | build | not-needed | C++ shared contract는 header로 포함된다. |
| `Client/Program.cs` | `Client/main.cpp` | client-entry | done | scenario dispatch와 framework client 구성을 수행한다. scenario 본문과 support helper는 별도 header로 분리했다. |
| `Client/RegistrationCodec.Client.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registration_codec_client` target이 대응한다. |
| `Client/Support/ClientOptions.cs` | `Client/Support/client_support.hpp`; `run_e2e.sh` | client-support | done | env parsing과 endpoint orchestration이 대응한다. |
| `Client/Support/CodecScenarioResult.cs` | not-needed | client-support | not-needed | C++ client는 typed reply를 직접 검사한다. |
| `Client/Support/EvidenceText.cs` | `Client/Support/client_support.hpp`; `Server/Infrastructure/scenario_state.hpp` | client-support | done | C++는 typed reply와 server evidence DTO를 직접 사용하고, HTTP evidence 조회 helper는 client support에 둔다. |
| `Client/Support/ProcessSupport.cs` | `run_e2e.sh` | runner-support | done | invalid startup process 실행은 shell runner가 담당한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/Support/client_support.hpp`; `run_e2e.sh` | client-support | done | C++ `ensure`와 shell failure checks가 대응한다. |
| `Client/Scenarios/AutoRegistrationScenario.cs` | `Client/Scenarios/auto_registration_scenario.hpp` | scenario | done | `RC-A1` request/send handler group 등록을 검증한다. |
| `Client/Scenarios/AttributeRegistrationScenario.cs` | not-implemented | scenario | gap | `RC-A2`는 `.NET` attribute 표면이다. C++ public API에는 대응 attribute discovery 계약이 없다. |
| `Client/Scenarios/ManualRegistrationScenario.cs` | `Client/Scenarios/manual_registration_scenario.hpp` | scenario | done | `RC-A3` route mesh 명시 handler 등록을 검증한다. |
| `Client/Scenarios/RcA4DiLifecycleScenario.cs` | `Client/Scenarios/rc_a4_di_lifecycle_scenario.hpp` | scenario | done | `RC-A4` scoped/singleton lifecycle을 검증한다. |
| `Client/Scenarios/RcA5FilterOrderingScenario.cs` | `Client/Scenarios/rc_a5_filter_ordering_scenario.hpp` | scenario | done | `RC-A5` filter before/after 순서를 검증한다. |
| `Client/Scenarios/InvalidRegistrationScenario.cs` | `run_e2e.sh`; `Server/Support/server_host.hpp` | scenario | done | `RC-A6` duplicate/wrong-group/unsupported-channel startup failure를 검증한다. |
| `Client/Scenarios/RcB1JsonCodecScenario.cs` | `Client/Scenarios/rc_b1_json_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp` | scenario | done | `RC-B1` JSON round-trip을 검증한다. |
| `Client/Scenarios/RcB2ProtobufCodecScenario.cs` | `Client/Scenarios/rc_b2_protobuf_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | scenario | done | C++ Protobuf codec extension request/send와 content-type evidence가 대응한다. |
| `Client/Scenarios/RcB3MessagePackCodecScenario.cs` | `Client/Scenarios/rc_b3_messagepack_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | scenario | done | C++ MessagePack codec extension request/send와 content-type evidence가 대응한다. |
| `Client/Scenarios/RcB4CodecCoexistenceScenario.cs` | `Client/Scenarios/rc_b4_codec_coexistence_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | scenario | done | JSON, Protobuf, MessagePack, custom serializer 공존을 검증한다. |
| `Client/Scenarios/CodecMismatchScenario.cs` | `Client/Scenarios/codec_mismatch_scenario.hpp`; `Server/Support/server_host.hpp`; `run_e2e.sh` | scenario | done | JSON-only peer에 Protobuf content-type request를 보내 C++ fallback 관측과 JSON recovery를 검증한다. |
| `Server/Main/Program.cs` | `Server/main.cpp` | server-entry | done | 정상 server entry가 대응한다. |
| `Server/Main/RegistrationCodecServerHostFactory.cs` | `Server/Support/server_host.hpp` | server-role | done | 정상 framework 구성이 대응한다. |
| `Server/Main/ServerOptions.cs` | `Server/Configuration/server_options.hpp`; `run_e2e.sh` | configuration | done | env 기반 endpoint/log option이 대응한다. |
| `Server/Main/DispatchFilters.cs` | `Server/Handlers/filter_order_handlers.hpp` | filter | done | first/second filter가 대응한다. |
| `Server/Main/Handlers/RegistrationHandlers.cs` | `Server/Handlers/registration_handlers.hpp` | handler | partial | auto/manual 성격의 handler가 대응한다. attribute handler는 gap이다. |
| `Server/Main/Handlers/DiEchoRequestHandler.cs` | `Server/Handlers/di_lifecycle_handlers.hpp` | handler | done | scoped/singleton lifecycle handler가 대응한다. |
| `Server/Main/Handlers/CodecHandlers.cs` | `Server/Handlers/codec_handlers.hpp` | handler | done | JSON/Protobuf/MessagePack/custom/mismatch handler가 대응한다. |
| `Server/Main/Infrastructure/EvidenceStore.cs` | `Server/Infrastructure/scenario_state.hpp` | infrastructure | done | scenario state/evidence snapshot이 대응한다. |
| `Server/Main/Infrastructure/Probes.cs` | `Server/Support/server_host.hpp`; `Client/Scenarios/`; `run_e2e.sh` | infrastructure | partial | C++는 typed reply와 startup failure로 probe를 직접 검증한다. |
| `Server/Main/Endpoints/OperationalEndpoints.cs` | `Server/Endpoints/operational_endpoints.hpp` | endpoint | done | `/health`와 `/evidence`가 대응한다. |
| `Server/Main/Endpoints/RegistrationScenarioEndpoints.cs` | not-needed | endpoint | not-needed | C++ client가 framework channel/route public API를 직접 호출한다. |
| `Server/Main/RegistrationCodec.Server.csproj` | `framework/languages/cpp/CMakeLists.txt` | build | done | `zlink_cpp_e2e_registration_codec_server` target이 대응한다. |
| `Server/InvalidDuplicate/*` | `Server/InvalidDuplicate/main.cpp`; `Server/Support/server_host.hpp`; `run_e2e.sh` | invalid-role | done | invalid role executable이 startup failure modes를 실행한다. |
| `Server/JsonOnlyPeer/*` | `Server/JsonOnlyPeer/main.cpp`; `Server/Support/server_host.hpp`; `run_e2e.sh` | codec-role | done | JSON-only peer executable이 binary codec을 등록하지 않는 peer로 실행된다. |
| `Server/CodecRequester/*` | `Client/Scenarios/codec_mismatch_scenario.hpp`; `Client/main.cpp`; `run_e2e.sh` | codec-role | done | `ZLINK_CPP_E2E_SCENARIO=b5` client가 codec mismatch requester 역할을 수행한다. |

## 공통 scenario ID 대응

| Scenario ID | C++ 대응 파일 | 상태 | 비고 |
|-------------|---------------|------|------|
| `RC-A1` | `Client/Scenarios/auto_registration_scenario.hpp`; `Server/Handlers/registration_handlers.hpp` | done | handler group 기반 request/send 등록을 검증한다. |
| `RC-A2` | `feature-map.ko.md` | gap | `.NET` attribute 표면이며 C++ public API에는 대응 계약이 없다. |
| `RC-A3` | `Client/Scenarios/manual_registration_scenario.hpp`; `Server/Handlers/registration_handlers.hpp`; `Server/Support/server_host.hpp` | done | route mesh 명시 handler 등록을 검증한다. |
| `RC-A4` | `Client/Scenarios/rc_a4_di_lifecycle_scenario.hpp`; `Server/Handlers/di_lifecycle_handlers.hpp` | done | scoped dependency 교체, singleton 유지, scoped dispose count를 검증한다. |
| `RC-A5` | `Client/Scenarios/rc_a5_filter_ordering_scenario.hpp`; `Server/Handlers/filter_order_handlers.hpp` | done | filter ordering을 검증한다. |
| `RC-A6` | `run_e2e.sh`; `Server/Support/server_host.hpp` | done | invalid startup failure를 검증한다. |
| `RC-B1` | `Client/Scenarios/rc_b1_json_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp` | done | JSON round-trip을 검증한다. |
| `RC-B2` | `Client/Scenarios/rc_b2_protobuf_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | done | Protobuf codec extension request/send와 content-type evidence를 검증한다. |
| `RC-B3` | `Client/Scenarios/rc_b3_messagepack_codec_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | done | MessagePack codec extension request/send와 content-type evidence를 검증한다. |
| `RC-B4` | `Client/Scenarios/rc_b4_codec_coexistence_scenario.hpp`; `Server/Handlers/codec_handlers.hpp`; `Server/Support/server_host.hpp` | done | JSON/Protobuf/MessagePack/custom serializer 공존을 검증한다. |
| `RC-B5` | `Client/Scenarios/codec_mismatch_scenario.hpp`; `Server/Support/server_host.hpp`; `run_e2e.sh` | done | JSON-only peer fallback 관측과 JSON recovery를 검증한다. |

## 검증

- 2026-06-30: `cmake --build framework/languages/cpp/build --target zlink_cpp_e2e_registration_codec_server zlink_cpp_e2e_registration_codec_invalid_duplicate zlink_cpp_e2e_registration_codec_json_only_peer zlink_cpp_e2e_registration_codec_client`
  - 결과: 통과
- 2026-06-30: `./framework/languages/cpp/e2e/RegistrationCodec/run_e2e.sh`
  - 결과: 통과
  - 로그: `logs/20260630-082445-3267605`
  - 의미: 정상 server, JSON-only peer executable, invalid role executable, client가 같은 gate에서 검증된다.
