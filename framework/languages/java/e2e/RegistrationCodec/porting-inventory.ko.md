# Java RegistrationCodec .NET 기준 포팅 inventory

기준 문서:

- `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`
- `framework/languages/dotnet/e2e/RegistrationCodec/feature-map.ko.md`
- `framework/doc/plan/framework-java-e2e-dotnet-porting-plan.ko.md`

이 문서는 `.NET` 기준 역할과 파일 책임에 맞춘 현재 Java 구현 매핑이다. 기존 Java 구현에서 보존한
handler, filter, DI, evidence 저장 책임은 server role로 옮겼고, Client는 HTTP driver로만 남긴다.

## 기존 Java 구현 보존 요약

- 현재 Java RegistrationCodec는 `Shared`, `Client`, `Server/Main`, `Server/InvalidDuplicate`,
  `Server/JsonOnlyPeer`, `Server/CodecRequester` Gradle subproject로 나뉜다.
- `Client`는 `ZLinkHttpClient`로 server HTTP endpoint를 호출하고 evidence를 조회한다. Client 안에서
  `@EnableZLinkFramework`, `ZLinkFrameworkConfigurer`, `ZLinkClient`를 사용하지 않는다.
- framework channel request/send와 codec registration 책임은 `Server/Main`과 `Server/CodecRequester`
  role 안에 있다. Client는 scenario 순서와 assertion만 담당한다.
- `run_e2e.sh`는 role별 `installDist` binary를 띄우고, single Client process가 RC-A1~RC-B5를 HTTP로
  구동한다.

## .NET 파일 매핑

| .NET 기준 파일 | Java 대응 파일 | 분류 | 상태 | 비고 |
|----------------|----------------|------|------|------|
| `.gitignore` | `.gitignore` | root | done | 기존 파일 보존. subproject build 산출물 제외가 필요하다. |
| `feature-map.ko.md` | `feature-map.ko.md` | docs | done | 기존 scenario 상태는 보존하되 포팅 후 실제 실행 로그와 구조 변경을 반영해야 한다. |
| `run_e2e.sh` | `run_e2e.sh` | runner | done | 역할별 installDist binary를 실행한다. 단일 app role switch는 제거했다. |
| `README.ko.md` 없음 | `README.ko.md` | docs | done | `.NET` RegistrationCodec에는 별도 README가 없지만 Java 완료 산출물로 역할, 실행법, gap을 기록한다. |
| `Shared/RegistrationCodec.Shared.csproj` | `Shared/build.gradle.kts` | build | done | 기존 root Gradle application에 섞여 있다. |
| `Shared/Messages.cs` | `Shared/src/main/java/systems/zlink/e2e/registrationcodec/shared/Contracts.java` | shared | done | 기존 `Contracts.java`에 channel, packet DTO, evidence DTO가 있다. |
| `Client/RegistrationCodec.Client.csproj` | `Client/build.gradle.kts` | build | done | HTTP client driver project다. framework runtime dependency를 두지 않는다. |
| `Client/Program.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Program.java` | client | done | `ZLinkHttpClient`로 `Server/Main`과 `Server/CodecRequester` endpoint를 호출한다. |
| `Client/Scenarios/AutoRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/AutoRegistrationScenario.java` | scenario | done | RC-A1. `/registration/auto`를 호출하고 evidence를 확인한다. |
| `Client/Scenarios/AttributeRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/AttributeRegistrationScenario.java` | scenario | done | RC-A2. `/registration/attribute`를 호출하고 evidence를 확인한다. |
| `Client/Scenarios/ManualRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/ManualRegistrationScenario.java` | scenario | done | RC-A3. `/registration/manual`을 호출하고 evidence를 확인한다. |
| `Client/Scenarios/RcA4DiLifecycleScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcA4DiLifecycleScenario.java` | scenario | done | RC-A4. `/registration/di-filter-order`를 호출하고 DI lifecycle evidence를 확인한다. |
| `Client/Scenarios/RcA5FilterOrderingScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcA5FilterOrderingScenario.java` | scenario | done | RC-A5. `/registration/filter-order`를 호출하고 filter order evidence를 확인한다. |
| `Client/Scenarios/InvalidRegistrationScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/InvalidRegistrationScenario.java` | scenario | done | RC-A6. Client scenario가 invalid server process를 시작하고 exit status와 startup error text를 확인한다. |
| `Client/Scenarios/RcB1JsonCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB1JsonCodecScenario.java` | scenario | done | RC-B1. `/codec/roundtrip`의 JSON reply와 evidence를 확인한다. |
| `Client/Scenarios/RcB2ProtobufCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB2ProtobufCodecScenario.java` | scenario | done | RC-B2. `/codec/roundtrip`의 Protobuf reply와 evidence를 확인한다. |
| `Client/Scenarios/RcB3MessagePackCodecScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB3MessagePackCodecScenario.java` | scenario | done | RC-B3. `/codec/roundtrip`의 MessagePack reply와 evidence를 확인한다. |
| `Client/Scenarios/RcB4CodecCoexistenceScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/RcB4CodecCoexistenceScenario.java` | scenario | done | RC-B4. 한 host에서 세 codec이 함께 동작하는 `/codec/roundtrip`을 호출한다. |
| `Client/Scenarios/CodecMismatchScenario.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Scenarios/CodecMismatchScenario.java` | scenario | done | RC-B5. `Server/CodecRequester` HTTP endpoint로 mismatch와 JSON recovery를 검증한다. |
| `Client/Support/ClientOptions.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/ClientOptions.java` | support | done | server HTTP endpoint, codec requester HTTP endpoint, invalid server endpoint, build dir, log dir를 env에서 읽는다. |
| `Client/Support/CodecScenarioResult.cs` | `Shared/.../Contracts.CodecScenarioRes` | support | done | Java는 shared DTO로 HTTP result를 decode한다. |
| `Client/Support/EvidenceText.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/Evidence.java` | support | done | HTTP로 server evidence를 조회한다. |
| `Client/Support/ProcessSupport.cs` | `Client/.../Scenarios/InvalidRegistrationScenario.java`, `run_e2e.sh` | support | done | RC-A6 invalid server lifecycle은 Client scenario가 담당하고, runner는 static server roles를 시작한다. |
| `Client/Support/ScenarioAssert.cs` | `Client/src/main/java/systems/zlink/e2e/registrationcodec/client/Support/ScenarioAssert.java` | support | done | 기존 `ClientScenario.ensure()`, wait helper, filter order helper에 섞여 있다. |
| `Server/Main/RegistrationCodec.Server.csproj` | `Server/Main/build.gradle.kts` | build | done | 기존 별도 main server project가 없다. |
| `Server/Main/Program.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Program.java` | server-role | done | 기존 `Program.java` role switch와 `ServerApplication.java`가 담당한다. |
| `Server/Main/RegistrationCodecServerHostFactory.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/RegistrationCodecServerApplication.java` | server-role | done | 기존 `ServerApplication.java` framework 설정을 역할 project로 옮긴다. |
| `Server/Main/ServerOptions.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Configuration/ServerOptions.java` | configuration | done | 기존 server endpoint, HTTP endpoint, codec mode, log dir env가 직접 조회된다. |
| `Server/Main/DispatchFilters.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/FirstOrderFilter.java`, `SecondOrderFilter.java`, `FilterOrderValues.java` | handler | done | 기존 root package의 filter classes를 main server handler/support로 옮긴다. |
| `Server/Main/Endpoints/OperationalEndpoints.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Endpoints/OperationalEndpoints.java` | endpoint | done | `/health`, `/evidence`, `/registration/*`, `/codec/roundtrip`을 제공한다. |
| `Server/Main/Endpoints/RegistrationScenarioEndpoints.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Endpoints/OperationalEndpoints.java` | endpoint | done | Java는 같은 HTTP endpoint class 안에서 scenario endpoint를 제공한다. |
| `Server/Main/Infrastructure/EvidenceStore.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Infrastructure/EvidenceStore.java` | infrastructure | done | 기존 `ScenarioState`가 evidence와 DI dispose count를 보관한다. |
| `Server/Main/Infrastructure/Probes.cs` | 없음 | infrastructure | not-needed | Java main server evidence store가 필요한 probe 상태를 직접 제공한다. |
| `Server/Main/Handlers/RegistrationHandlers.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/*Registration*`, `Auto*`, `Attr*`, `Manual*` | handler | done | 기존 `handlers/Auto*`, `AttrEchoHandler`, `Manual*`에 구현되어 있다. |
| `Server/Main/Handlers/CodecHandlers.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/*Codec*`, `Json*`, `Protobuf*`, `Msgpack*` | handler | done | 기존 `handlers/Json*`, `Protobuf*`, `Msgpack*`에 구현되어 있다. |
| `Server/Main/Handlers/DiEchoRequestHandler.cs` | `Server/Main/src/main/java/systems/zlink/e2e/registrationcodec/main/Handlers/DiLifecycleReqHandler.java` | handler | done | 기존 `handlers/DiLifecycleReqHandler.java`와 DI dependency classes에 구현되어 있다. |
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
| `Server/CodecRequester/CodecRequesterHostFactory.cs` | `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/CodecRequesterApplication.java`, `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/Endpoints/CodecRequesterEndpoints.java` | server-role | done | requester framework 설정과 `/codec/protobuf/request`, `/codec/json/request` HTTP endpoint를 담당한다. |
| `Server/CodecRequester/CodecRequesterOptions.cs` | `Server/CodecRequester/src/main/java/systems/zlink/e2e/registrationcodec/codecrequester/Configuration/CodecRequesterOptions.java` | configuration | done | requester endpoint, evidence endpoint, log dir env를 해석한다. |

## 공통 시나리오 매핑

| 시나리오 | 기존 Java 위치 | 목표 Java 위치 | 상태 | 비고 |
|----------|----------------|----------------|------|------|
| RC-A1 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/AutoRegistrationScenario.java` | done | HTTP driver가 server endpoint를 호출하고 자동 등록 request/send evidence를 확인한다. |
| RC-A2 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/AttributeRegistrationScenario.java` | done | HTTP driver가 server endpoint를 호출하고 annotation 등록 request/send evidence를 확인한다. |
| RC-A3 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/ManualRegistrationScenario.java` | done | HTTP driver가 server endpoint를 호출하고 명시 등록 request/send evidence를 확인한다. |
| RC-A4 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/RcA4DiLifecycleScenario.java` | done | HTTP driver가 DI lifecycle server endpoint 결과와 evidence를 확인한다. |
| RC-A5 | `ClientScenario.runRegistrationVariants()` | `Client/.../Scenarios/RcA5FilterOrderingScenario.java` | done | HTTP driver가 filter-order server endpoint 결과와 evidence를 확인한다. |
| RC-A6 | `Client/.../Scenarios/InvalidRegistrationScenario.java` | `Client/.../Scenarios/InvalidRegistrationScenario.java` | done | startup failure oracle을 Client scenario로 옮겼다. |
| RC-B1 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB1JsonCodecScenario.java` | done | HTTP driver가 JSON round-trip server endpoint와 evidence를 확인한다. |
| RC-B2 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB2ProtobufCodecScenario.java` | done | HTTP driver가 Protobuf round-trip server endpoint와 evidence를 확인한다. |
| RC-B3 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB3MessagePackCodecScenario.java` | done | HTTP driver가 MessagePack round-trip server endpoint와 evidence를 확인한다. |
| RC-B4 | `ClientScenario.runCodecVariants()` | `Client/.../Scenarios/RcB4CodecCoexistenceScenario.java` | done | HTTP driver가 한 host의 세 codec 공존 endpoint를 확인한다. |
| RC-B5 | `ClientScenario.runCodecMismatch()` | `Client/.../Scenarios/CodecMismatchScenario.java` | done | HTTP driver가 codec requester endpoint로 json-only peer mismatch와 recovery를 확인한다. |

## 보존·이동·삭제 판단

| 기존 Java 파일 | 판단 | 목표 위치 | 근거 |
|----------------|------|-----------|------|
| `Contracts.java` | move | `Shared/.../shared/Contracts.java` | client/server 공용 DTO와 evidence type이다. |
| `ClientScenario.java` | split | `Client/.../Scenarios/*`, `Client/.../Support/*` | scenario ID, assertion, HTTP evidence helper가 섞여 있다. |
| `ClientApplication.java` | remove | 없음 | Client는 framework application이 아니라 HTTP driver이므로 제거했다. |
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
