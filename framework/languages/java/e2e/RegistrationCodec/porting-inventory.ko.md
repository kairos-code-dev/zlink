# Java RegistrationCodec .NET 기준 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`
- `framework/languages/dotnet/e2e/RegistrationCodec/feature-map.ko.md`
- `framework/doc/plan/framework-java-e2e-dotnet-porting-plan.ko.md`

이 문서는 코드 이동 전에 작성한 매핑이다. 기존 Java 구현은 삭제 대상이 아니라 보존 입력이며,
`.NET` 기준 역할과 파일 책임에 맞게 옮기거나 나눈다.

## 기존 Java 구현 보존 요약

- 기존 Java RegistrationCodec는 단일 Gradle application과 `Program.java`의
  `ZLINK_JAVA_E2E_ROLE` 분기로 `server`, `invalid-server`, `client` 역할을 바꾼다.
- `ClientScenario.java` 하나가 RC-A1, RC-A2, RC-A3, RC-A4, RC-A5, RC-B1, RC-B2, RC-B3,
  RC-B4, RC-B5 실행과 evidence 조회, assertion을 함께 담당한다.
- `run_e2e.sh`는 invalid startup failure, main server/client, json-only mismatch server/client를
  한 binary로 실행한다.
- handler, filter, DI dependency, evidence server, codec registration 구현은 모두 public framework
  API 기반이므로 보존하되 `Shared`, `Client/Scenarios`, `Client/Support`, `Server/Main`,
  `Server/InvalidDuplicate`, `Server/JsonOnlyPeer`, `Server/CodecRequester` 책임으로 재분류한다.

## .NET 파일 매핑

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | root | done | 기존 파일 보존. subproject build 산출물 제외가 필요하다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 기존 scenario 상태는 보존하되 포팅 후 실제 실행 로그와 구조 변경을 반영해야 한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | 역할별 installDist binary를 실행한다. 단일 app role switch는 제거했다. |
| `README.ko.md` 없음 | `README.ko.md` | docs | done | `.NET` RegistrationCodec에는 별도 README가 없지만 Java 완료 산출물로 역할, 실행법, gap을 기록한다. |
| `Shared/RegistrationCodec.Shared.csproj` | `Shared/build.gradle.kts` | build | done | 기존 root Gradle application에 섞여 있다. |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/registrationcodec/shared/Contracts.java` | shared | done | 기존 `Contracts.java`에 channel, packet DTO, evidence DTO가 있다. |
| `Client/RegistrationCodec.Client.csproj` | `Client/build.gradle.kts` | build | done | 기존 root Gradle application에 섞여 있다. |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Program.java` | client | done | 기존 `ClientApplication.java`와 `ClientScenario.java`가 실행과 framework 설정을 함께 담당한다. |
| `Client/Scenarios/AutoRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/AutoRegistrationScenario.java` | scenario | done | RC-A1. 기존 `ClientScenario.runRegistrationVariants()`에 구현되어 있다. |
| `Client/Scenarios/AttributeRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/AttributeRegistrationScenario.java` | scenario | done | RC-A2. 기존 `ClientScenario.runRegistrationVariants()`에 구현되어 있다. |
| `Client/Scenarios/ManualRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/ManualRegistrationScenario.java` | scenario | done | RC-A3. 기존 `ClientScenario.runRegistrationVariants()`에 구현되어 있다. |
| `Client/Scenarios/RcA4DiLifecycleScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcA4DiLifecycleScenario.java` | scenario | done | RC-A4. 기존 `ClientScenario.runRegistrationVariants()`에 구현되어 있다. |
| `Client/Scenarios/RcA5FilterOrderingScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcA5FilterOrderingScenario.java` | scenario | done | RC-A5. 기존 `ClientScenario.runRegistrationVariants()`에 구현되어 있다. |
| `Client/Scenarios/InvalidRegistrationScenario.cs` | `run_e2e.sh` | scenario | done | RC-A6. 기존 runner가 invalid server exit status와 startup error text를 확인한다. runner oracle로 확정했다. |
| `Client/Scenarios/RcB1JsonCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB1JsonCodecScenario.java` | scenario | done | RC-B1. 기존 `ClientScenario.runCodecVariants()`에 구현되어 있다. |
| `Client/Scenarios/RcB2ProtobufCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB2ProtobufCodecScenario.java` | scenario | done | RC-B2. 기존 `ClientScenario.runCodecVariants()`에 구현되어 있다. |
| `Client/Scenarios/RcB3MessagePackCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB3MessagePackCodecScenario.java` | scenario | done | RC-B3. 기존 `ClientScenario.runCodecVariants()`에 구현되어 있다. |
| `Client/Scenarios/RcB4CodecCoexistenceScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB4CodecCoexistenceScenario.java` | scenario | done | RC-B4. 기존 JSON, Protobuf, MessagePack을 같은 host에서 섞어 보내는 흐름에 구현되어 있다. |
| `Client/Scenarios/CodecMismatchScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/CodecMismatchScenario.java` | scenario | done | RC-B5. 기존 `ClientScenario.runCodecMismatch()`에 구현되어 있다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/ClientOptions.java` | support | done | 기존 `Env.java`와 runner env 해석에 흩어져 있다. |
| `Client/Support/CodecScenarioResult.cs` | 없음 | support | not-needed | Java scenario는 typed reply와 evidence assertion을 직접 사용하므로 별도 result DTO가 필요 없다. |
| `Client/Support/EvidenceText.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/Evidence.java` | support | done | 기존 `ClientScenario.snapshot()`과 evidence wait helper에 섞여 있다. |
| `Client/Support/ProcessSupport.cs` | `run_e2e.sh` | support | done | 기존 process lifecycle은 runner가 담당한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/ScenarioAssert.java` | support | done | 기존 `ClientScenario.ensure()`, wait helper, filter order helper에 섞여 있다. |
| `Server/Main/RegistrationCodec.Server.csproj` | `Server/Main/build.gradle.kts` | build | done | 기존 별도 main server project가 없다. |
| `Server/Main/Program.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Program.java` | server-role | done | 기존 `Program.java` role switch와 `ServerApplication.java`가 담당한다. |
| `Server/Main/RegistrationCodecServerHostFactory.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/RegistrationCodecServerApplication.java` | server-role | done | 기존 `ServerApplication.java` framework 설정을 역할 project로 옮긴다. |
| `Server/Main/ServerOptions.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Configuration/ServerOptions.java` | configuration | done | 기존 server endpoint, HTTP endpoint, codec mode, log dir env가 직접 조회된다. |
| `Server/Main/DispatchFilters.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/FirstOrderFilter.java`, `SecondOrderFilter.java`, `FilterOrderValues.java` | handler | done | 기존 root package의 filter classes를 main server handler/support로 옮긴다. |
| `Server/Main/Endpoints/OperationalEndpoints.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Endpoints/OperationalEndpoints.java` | endpoint | done | 기존 `EvidenceHttpServer`가 `/health`와 `/evidence`를 함께 제공한다. |
| `Server/Main/Endpoints/RegistrationScenarioEndpoints.cs` | 없음 | endpoint | not-needed | Java 기존 scenario는 client가 channel request/send를 직접 호출한다. HTTP scenario endpoint가 필요한지 필요하지 않아 runner와 channel client 검증으로 확정했다. |
| `Server/Main/Infrastructure/EvidenceStore.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Infrastructure/EvidenceStore.java` | infrastructure | done | 기존 `ScenarioState`가 evidence와 DI dispose count를 보관한다. |
| `Server/Main/Infrastructure/Probes.cs` | 없음 | infrastructure | not-needed | Java main server evidence store가 필요한 probe 상태를 직접 제공한다. |
| `Server/Main/Handlers/RegistrationHandlers.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/*Registration*`, `Auto*`, `Attr*`, `Manual*` | handler | done | 기존 `handlers/Auto*`, `AttrEchoHandler`, `Manual*`에 구현되어 있다. |
| `Server/Main/Handlers/CodecHandlers.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/*Codec*`, `Json*`, `Protobuf*`, `Msgpack*` | handler | done | 기존 `handlers/Json*`, `Protobuf*`, `Msgpack*`에 구현되어 있다. |
| `Server/Main/Handlers/DiEchoRequestHandler.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/DiLifecycleRequestHandler.java` | handler | done | 기존 `handlers/DiLifecycleRequestHandler.java`와 DI dependency classes에 구현되어 있다. |
| `Server/InvalidDuplicate/RegistrationCodec.InvalidDuplicate.csproj` | `Server/InvalidDuplicate/build.gradle.kts` | build | done | 기존 별도 invalid duplicate project가 없다. |
| `Server/InvalidDuplicate/Program.cs` | `Server/InvalidDuplicate/src/main/java/systems/zlink/e2e/registrationcodec/invalidduplicate/Program.java` | server-role | done | 기존 `Program.java` role switch와 `InvalidServerApplication.java`가 담당한다. |
| `Server/InvalidDuplicate/RegistrationCodecServerHostFactory.cs` | `Server/InvalidDuplicate/src/main/java/systems/zlink/e2e/registrationcodec/invalidduplicate/InvalidDuplicateApplication.java` | server-role | done | duplicate registration startup failure를 보존한다. |
| `Server/InvalidDuplicate/ServerOptions.cs` | `Server/InvalidDuplicate/src/main/java/systems/zlink/e2e/registrationcodec/invalidduplicate/Configuration/ServerOptions.java` | configuration | done | invalid endpoint env를 해석한다. |
| `Server/InvalidDuplicate/DispatchFilters.cs` | 없음 | handler | not-needed | invalid duplicate scenario는 duplicate packet registration만 필요하다. |
| `Server/InvalidDuplicate/OperationalEndpoints.cs` | 없음 | endpoint | not-needed | startup failure scenario라 HTTP endpoint가 뜨면 안 된다. |
| `Server/InvalidDuplicate/Infrastructure/EvidenceStore.cs` | `Server/InvalidDuplicate/src/main/java/systems/zlink/e2e/registrationcodec/invalidduplicate/Infrastructure/EvidenceStore.java` | infrastructure | done | handler bean 생성에 필요한 최소 state만 둔다. |
| `Server/InvalidDuplicate/Infrastructure/Probes.cs` | 없음 | infrastructure | not-needed | invalid duplicate scenario oracle은 process exit와 startup error text다. |
| `Server/InvalidDuplicate/Handlers/RegistrationHandlers.cs` | `Server/InvalidDuplicate/src/main/java/systems/zlink/e2e/registrationcodec/invalidduplicate/Handlers/ManualRequestHandler.java` | handler | done | duplicate registration에 필요한 request handler만 보존한다. |
| `Server/InvalidDuplicate/Handlers/CodecHandlers.cs` | 없음 | handler | not-needed | RC-A6 duplicate registration은 codec handler가 필요 없다. |
| `Server/InvalidDuplicate/Handlers/DiEchoRequestHandler.cs` | 없음 | handler | not-needed | RC-A6 duplicate registration은 DI handler가 필요 없다. |
| `Server/JsonOnlyPeer/RegistrationCodec.JsonOnlyPeer.csproj` | `Server/JsonOnlyPeer/build.gradle.kts` | build | done | 기존 main server에 `ZLINK_JAVA_E2E_CODEC_MODE=json-only`로 섞여 있다. |
| `Server/JsonOnlyPeer/Program.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/Program.java` | server-role | done | json-only peer role entrypoint를 분리한다. |
| `Server/JsonOnlyPeer/RegistrationCodecServerHostFactory.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/JsonOnlyPeerApplication.java` | server-role | done | JSON-only codec registry를 설정한다. |
| `Server/JsonOnlyPeer/ServerOptions.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/Configuration/ServerOptions.java` | configuration | done | endpoint, HTTP endpoint, log dir env를 해석한다. |
| `Server/JsonOnlyPeer/DispatchFilters.cs` | 없음 | handler | not-needed | RC-B5 json-only peer는 codec mismatch와 JSON recovery만 검증하므로 filter ordering 책임이 없다. |
| `Server/JsonOnlyPeer/OperationalEndpoints.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/Endpoints/OperationalEndpoints.java` | endpoint | done | mismatch server evidence/health endpoint를 제공한다. |
| `Server/JsonOnlyPeer/Infrastructure/EvidenceStore.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/Infrastructure/EvidenceStore.java` | infrastructure | done | current `ScenarioState` 역할을 분리한다. |
| `Server/JsonOnlyPeer/Infrastructure/Probes.cs` | 없음 | infrastructure | not-needed | Java json-only peer evidence store가 필요한 state를 직접 제공한다. |
| `Server/JsonOnlyPeer/Handlers/RegistrationHandlers.cs` | 없음 | handler | not-needed | RC-B5 json-only peer는 registration variants를 실행하지 않는다. |
| `Server/JsonOnlyPeer/Handlers/CodecHandlers.cs` | `Server/JsonOnlyPeer/src/main/java/systems/zlink/e2e/registrationcodec/jsononlypeer/Handlers/JsonRequestHandler.java`, `MsgpackRequestHandler.java` | handler | done | RC-B5 JSON baseline과 mismatch packet 처리를 보존한다. |
| `Server/JsonOnlyPeer/Handlers/DiEchoRequestHandler.cs` | 없음 | handler | not-needed | RC-B5 mismatch peer에는 DI scenario가 필요 없다. |
| `Server/CodecRequester/RegistrationCodec.CodecRequester.csproj` | `Server/CodecRequester/build.gradle.kts` | build | done | RC-B5 requester를 별도 Java application으로 분리했다. |
| `Server/CodecRequester/Program.cs` | `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/Program.java` | server-role | done | RC-B5 requester role entrypoint다. |
| `Server/CodecRequester/CodecRequesterHostFactory.cs` | `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/CodecRequesterApplication.java` | server-role | done | requester framework 설정과 scenario 실행을 담당한다. |
| `Server/CodecRequester/CodecRequesterOptions.cs` | `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/Configuration/CodecRequesterOptions.java` | configuration | done | requester endpoint, evidence endpoint, log dir env를 해석한다. |

## 공통 시나리오 매핑

| 시나리오 | 기존 Java 위치 | 목표 Java 위치 | 상태 | 비고 |
|----------|----------------|----------------|------|------|
| RC-A1 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/AutoRegistrationScenario.java` | done | 자동 등록 request/send 검증을 보존한다. |
| RC-A2 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/AttributeRegistrationScenario.java` | done | Java annotation 등록 request/send 검증을 보존한다. |
| RC-A3 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/ManualRegistrationScenario.java` | done | 명시 등록 request/send 검증을 보존한다. |
| RC-A4 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/RcA4DiLifecycleScenario.java` | done | scoped id, singleton id, dispose count evidence 검증을 보존한다. |
| RC-A5 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/RcA5FilterOrderingScenario.java` | done | filter before/after ordering 검증을 보존한다. |
| RC-A6 | `run_e2e.sh` invalid-server phase | `run_e2e.sh` 또는 `Client/.../Scenarios/InvalidRegistrationScenario.java` | done | startup failure oracle을 보존한다. |
| RC-B1 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB1JsonCodecScenario.java` | done | JSON request/send round-trip을 보존한다. |
| RC-B2 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB2ProtobufCodecScenario.java` | done | Protobuf request/send round-trip을 보존한다. |
| RC-B3 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB3MessagePackCodecScenario.java` | done | MessagePack request/send round-trip을 보존한다. |
| RC-B4 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB4CodecCoexistenceScenario.java` | done | 한 host에서 세 codec 공존 검증을 보존한다. |
| RC-B5 | `ClientScenario.runCodecMismatch()` | `Client/.../Scenarios/CodecMismatchScenario.java` | done | json-only peer mismatch와 JSON recovery 검증을 보존한다. |

## 보존·이동·삭제 판단

| 기존 Java 파일 | 판단 | 목표 위치 | 근거 |
|----------------|------|-----------|------|
| `Contracts.java` | move | `Shared/.../shared/Contracts.java` | client/server 공용 DTO와 evidence type이다. |
| `ClientScenario.java` | split | `Client/.../Scenarios/*`, `Client/.../Support/*` | scenario ID, assertion, HTTP evidence helper가 섞여 있다. |
| `ClientApplication.java` | move/split | `Client/.../Program.java`, `Client/.../Support/*` | client framework 설정과 scenario 실행을 분리한다. |
| `ServerApplication.java` | split | `Server/Main/...`, `Server/JsonOnlyPeer/...` | main server와 json-only peer가 codec mode env로 섞여 있다. |
| `InvalidServerApplication.java` | move | `Server/InvalidDuplicate/.../InvalidDuplicateApplication.java` | RC-A6 duplicate registration startup failure를 보존한다. |
| `ScenarioState.java` | move | 각 server role `Infrastructure/EvidenceStore.java` | evidence와 DI dispose count 보관 책임이다. |
| `EvidenceHttpServer.java` | move | 각 server role `Endpoints/OperationalEndpoints.java` | `/health`와 `/evidence` endpoint 구현이다. |
| `FirstOrderFilter.java`, `SecondOrderFilter.java`, `FilterOrderValues.java` | move | `Server/Main/.../Handlers` 또는 `Support` | RC-A5 filter ordering을 보존한다. |
| `DiScopedDependency.java`, `DiSingletonDependency.java` | move | `Server/Main/.../Infrastructure` | RC-A4 DI lifecycle 검증에 필요하다. |
| `handlers/*.java` | move/split | `Server/Main/.../Handlers`, `Server/JsonOnlyPeer/.../Handlers`, `Server/InvalidDuplicate/.../Handlers` | public handler 구현을 역할별로 재분류한다. |
| `Env.java` | split | 각 role `Configuration/*Options.java`, `Client/.../Support/ClientOptions.java` | 전역 env accessor를 역할별 option parsing으로 줄인다. |
| `Program.java` | delete after split | 역할별 `Program.java` | 단일 app role switch는 완료 구조가 아니다. |
| `build.gradle.kts` | rewrite | root `build.gradle.kts`와 role별 `build.gradle.kts` | multi-project로 나누어야 한다. |
| `settings.gradle.kts` | rewrite | `settings.gradle.kts` | `:Shared`, `:Client`, `:Server:<Role>`를 포함해야 한다. |
| `run_e2e.sh` | rewrite | `run_e2e.sh` | 역할별 binary를 build/start하고 health/evidence를 확인해야 한다. |
| `feature-map.ko.md` | rewrite | `feature-map.ko.md` | 포팅 후 실행 결과와 실제 gap을 반영해야 한다. |
