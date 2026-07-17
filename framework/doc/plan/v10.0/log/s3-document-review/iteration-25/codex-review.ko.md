# S3 문서 독립 리뷰 — iteration 25 (Codex)

## 실행 증거

- provider/model: OpenAI / GPT-5.6 Codex
- session ID: `019f6ead-9df8-7973-9245-7fcdf0778bee`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- `scope-files.sha256` SHA-256: `531e3ad5f8252ea95f8f77054080e399153134c10c1a2c73e76fd4c0ab2a7097`
- 파일별 hash: 시작·종료 205/205 일치
- 205개 파일 전체 검토, contract verifier CLEAN
- read-only 실행, 작성 파일 0개, 기존 dirty worktree 보존

## Findings

[원칙][high] framework/doc/framework/spec/90-implementation-gap.ko.md:90 — C++의 Spot 주소 메시징을 `O`로 표시했지만 `gaps/cpp.ko.md:274-275`의 `IMP-CP-03`은 미완료이며 C++ exact interface에도 금지된 node RID+spot RID overload가 남아 있다 — 요약표가 현재 gap ledger와 정식 interface를 동시에 부정한다 — C++ 칸을 `X`로 바꾸고 `IMP-CP-03`을 연결한다.

[원칙][high] framework/doc/framework/spec/gaps/dotnet.ko.md:886 — “현재 확인된 미완료 .NET spec gap은 없다”고 단정한다 — 같은 문서 `266-289`는 미완료 9건이라고 명시하고 `IMP-DN-24`~`31`을 열어 두며, 파일 전체에는 unchecked 행이 13개 남아 있다 — 현재 상태를 한 곳에서만 선언하고 과거 재리뷰 결론은 시점이 지난 기록임을 명시한다.

[1차소스][high] framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:1285 — `spot_common_context_t`가 node RID와 spot RID를 함께 받는 `send_to`/`request_to`를 정식 시그니처로 제공한다 — 공통 24 §4는 `SpotHandle`과 payload만 받도록 규정하고, 같은 문서 `1148-1154`도 해당 overload를 금지한다 — 옛 overload를 exact interface에서 제거하고 계약 결정 후 handle 기반 context outbound를 명시한다.

[1차소스][high] framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:117 — 수동 연결을 `channel + capability` 단위로 규정한다 — 공통 10 §1~4는 ChannelName들이 MeshNode의 ROUTER peer 연결 하나를 공유하고 manual 입력도 MeshNode peer intent라고 규정한다 — channel/capability별 연결 설명과 예제를 MeshNode `peerConnections()` 중심으로 교체한다.

[1차소스][high] framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:555 — `ZLinkActorFactory.create`를 동기 `ZLinkActor` 반환으로 선언한다 — 정확한 시그니처 소유 문서인 `02-handler-interfaces.ko.md:413-416`은 `CompletionStage<ZLinkActor>`를 반환한다 — 중복 선언을 제거하거나 정확한 비동기 시그니처로 일치시킨다.

[1차소스][high] framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md:406 — Kotlin public extension들이 선언·import되지 않은 `Message` 타입을 사용한다 — Kotlin/Java exact 문서 어디에도 이 타입 정의가 없고 Java 원형은 임의 typed payload를 받는 generic method다 — 각 함수를 `<TMessage>` generic으로 선언하거나 Java 원형에 맞는 명시적 타입을 사용한다.

[1차소스][high] framework/doc/framework/spec/server/languages/node/02-handler-interfaces.ko.md:2071 — 실제 Nest 구성 경로가 반환하는 `ZLinkNestStreamNodeBuilder`에 `enableActorDispatch(meshName)`이 없다 — 같은 exact 문서 `1740-1744`의 기본 builder와 §2.31은 이 메서드를 목표 계약으로 요구하지만 `zlinkFramework().addStreamNode()` 사용자는 호출할 수 없다 — Nest builder에도 같은 메서드를 선언하고 동일한 MeshName 검증 계약을 연결한다.

[1차소스][high] framework/doc/framework/dotnet/guide/09-stream.ko.md:281 — Spot RID를 `SendToNode`의 target node RID로 넣고 `"spot.route"`를 MeshName 인자처럼 전달한다 — .NET exact interface는 `SendToNode(meshName, targetNodeRid, ...)`와 `SendToSpot(SpotHandle, ...)`을 분리하며 Spot owner node RID를 공개하지 않는다 — metadata의 Spot RID를 `ResolveAsync`로 `SpotHandle`로 해석한 뒤 `SendToSpot`을 호출하도록 고친다.

[원칙][medium] framework/doc/framework/spec/server/languages/cpp/02-framework-interfaces.ko.md:1025 — 예제가 `handler_registry_t.group(...).add(...)`를 호출하지만 같은 문서 `958-1014`의 정식 `handler_registry_t`에는 `group`이나 `add`가 선언되어 있지 않다 — exact interface만으로 예제를 타입 검사할 수 없고 `group_builder_t`는 전방 선언만 존재한다 — group builder와 반환 타입·등록 메서드의 정확한 선언을 추가하거나 예제를 이미 선언된 API로 바꾼다.

[1차소스][medium] framework/doc/framework/spec/server/languages/cpp/60-http-hosting.ko.md:137 — HTTP handler 예제가 `_client.request(channel, request)` 두 인자 overload를 호출한다 — C++ exact `request_client_t::request`는 `meshName`, `channelName`, request, options를 받는다 — 예제에 RouteMesh 이름을 첫 인자로 추가한다.

[1차소스][medium] framework/doc/framework/spec/stream-connector/languages/typescript/03-stream-connector.ko.md:348 — `where(p => p.status === …)`라고 안내한다 — 정식 predicate 인자는 payload가 아니라 `ZlinkStreamMessage<T>`이고 상태는 `message.payload.status`에 있다 — 예제를 `where(message => message.payload.status === …)`로 수정한다.

[1차소스][medium] framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:1462 — `ZLinkSpot<MatchActor>` 예제가 `onDisconnectActor(MatchActor)`를 override한다 — 같은 문서 `954-960`, `978-980`의 lifecycle 계약은 `ZLinkActorMembership`을 받으므로 이 코드는 override되지 않는다 — 매개변수를 `ZLinkActorMembership`으로 바꾸고 필요한 actor 정보는 membership에서 읽는다.

[1차소스][medium] framework/doc/framework/java/guide/04-channel-messaging.ko.md:15 — 일반 `ZLinkClient` 예제가 MeshName 없이 `requestToChannel(channel, request)`를 호출한다 — Java exact `ZLinkClient`는 `requestToChannel(meshName, channelName, request)`만 제공한다 — RouteMesh 이름을 포함한 세 인자 예제로 고친다.

[1차소스][medium] framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md:417 — `ZLinkFanoutClient.publishToTopic`의 반환형을 `ZLinkPublishCall`로 선언한다 — Java exact 원형은 classic fanout 전용 `ZLinkFanoutPublishCall`을 반환하며 Logical Multicast용 `ZLinkPublishCall`과 완료 의미가 다르다 — 반환형을 `ZLinkFanoutPublishCall`로 맞춘다.

[1차소스][medium] framework/doc/framework/kotlin/guide/04-channel-messaging.ko.md:20 — Kotlin 예제가 MeshName 없이 `requestToChannel(channel, request)`를 호출한다 — Kotlin exact extension과 상속한 Java `ZLinkClient` 모두 일반 호출에는 MeshName을 요구한다 — MeshName을 첫 인자로 추가하고 14~16행의 설명도 같은 호출 모양으로 고친다.

[1차소스][medium] framework/doc/framework/node/guide/04-channel-messaging.ko.md:52 — fanout 예제가 topic과 event만 넘긴다 — Node exact `ZLinkFanoutClient.publish`는 `channelName, topic, event` 세 인자를 요구한다 — fanout channel 이름을 첫 인자로 추가한다.

[원칙][low] framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:776 — `RegistryMonitor` 예제는 implements 절, method 선언과 여는 중괄호 없이 `@Override`와 닫는 중괄호만 둔다 — Java 구문으로 성립하지 않아 monitoring handler 사용법을 재현할 수 없다 — 지원하는 runtime event handler interface와 완전한 `handle(...)` 선언을 포함한 컴파일 가능한 예제로 교체한다.
