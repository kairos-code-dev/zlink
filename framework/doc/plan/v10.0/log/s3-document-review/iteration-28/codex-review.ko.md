# Codex 독립 문서 리뷰 — S3 iteration 28

[1차소스][high] `framework/doc/framework/spec/gaps/dotnet.ko.md:817` — 다섯 언어 gap 문서가 제거된 범용 assertion helper를 connector 공개 API로 계속 확정한다 — 공통 connector 계약 `32-stream-connector.ko.md:500-503`은 connector 상태와 무관한 assertion을 공개 계약에서 제외했지만 같은 주장이 `gaps/java.ko.md:1118`, `gaps/kotlin.ko.md:486`, `gaps/node.ko.md:941`, `gaps/cpp.ko.md:1137`과 각 TH 완료 행에도 남아 있다 — 다섯 gap에서 범용 assertion의 공개 API 확정·구현 지시를 제거하고 `Client/Support` 소유 검증 도구로 재분류한다.

[1차소스][high] `framework/doc/framework/spec/server/languages/java/01-system-structure.ko.md:442` — exact system 문서의 session 예제가 정식 Java interface를 구현할 수 없다 — `02-handler-interfaces.ko.md:302-315`는 네 lifecycle callback 모두 `CompletionStage<Void>`를 반환하도록 선언하지만 예제는 `void` 두 개만 구현하고 `onDispatch` 내부에서 `join()`한다 — 네 callback을 정확한 반환형으로 구현하고 비동기 체인을 직접 반환하도록 예제를 고친다.

[1차소스][high] `framework/doc/framework/java/guide/06-actor-session.ko.md:80` — actor/session guide가 존재하지 않는 Java 서명과 완료 체인을 사용한다 — transfer adapter는 exact interface `02-handler-interfaces.ko.md:274-280`과 달리 `CancellationToken`을 받고 동기 값을 반환하며, 232-244행은 `void` session callback, MeshName이 빠진 `getOrCreate`, 반환값이 `void`인 session reply `submit()`의 `thenCompose` 사용을 함께 제시한다 — `CompletionStage` 기반 exact 서명, 세 인자의 actor 조회, 실제 call 반환 계약에 맞춰 예제 전체를 다시 작성한다.

[1차소스][high] `framework/doc/framework/java/guide/07-stream.ko.md:52` — stream guide의 server와 connector 예제가 정식 Java API로 컴파일되지 않는다 — session callback은 exact 계약과 달리 `void`이고, 74-79행은 반환값이 `void`인 send/reply `submit()` 뒤에 completion 연산을 연결하며, 127-128행의 `thenCompose`도 `void`인 connector send `submit()`을 반환한다 — session에서는 `CompletionStage<Void>`를 반환하고 one-way `submit()`은 체인에서 분리하며 lifecycle completion만 기다리도록 수정한다.

[1차소스][high] `framework/doc/framework/kotlin/guide/06-actor-session.ko.md:78` — Kotlin actor/session guide가 exact extension 및 adapter 표면과 다른 호출을 연속해서 제시한다 — exact Kotlin adapter `02-handler-interfaces.ko.md:151-159`에는 `CancellationToken`이 없고 메시지 생성 함수는 `messageOf`이며, handler 등록은 `addHandler<T>()`뿐이다. guide의 `addActorPacket`, typed handler의 `packetName()`, MeshName이 빠진 `getOrCreate`도 선언돼 있지 않다 — token을 제거하고 `messageOf`, `addHandler<T>()`, `messageType()` 및 MeshName을 받는 exact actor API로 예제를 교체한다.

[1차소스][high] `framework/doc/framework/kotlin/guide/07-stream.ko.md:124` — guide가 exact connector interface에 없는 Kotlin compression option helper를 공개 사용법으로 고정한다 — `withLz4StreamCompression()`과 `withStreamCompression(codec)`은 exact Kotlin 공개 표면 `stream-connector/languages/java/03-stream-connector.ko.md:322-335,434-480`에 선언돼 있지 않으며 scope 전체에서 이 guide 외에는 등장하지 않는다 — 이미 선언된 options 생성 방식으로 예제를 바꾸거나 exact interface에 먼저 계약을 확정한다.

## 실행 증거

- model/session: `gpt-5.6-sol` / `019f6f1b-382c-7a31-beca-3f838cf06d60`
- full scope: 205/205 files, 47,253 lines
- renderer: Python-Markdown 3.10.2 + pymdown-extensions 10.21.2, 205/205, broken links 0
- verifier: `FRAMEWORK DOC CONTRACTS CLEAN`
- start/end HEAD, scope list, 205-file aggregate and per-file hashes all matched iteration 28.
