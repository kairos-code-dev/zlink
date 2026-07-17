# Codex 독립 문서 리뷰 — S3 iteration 27

## 발견 사항

[1차소스][high] `framework/doc/framework/java/guide/06-actor-session.ko.md:33` — 제거 대상인 `ZLinkSpotMeshBuilder`·`addSpotMesh`를 등록 예제로 사용한다 — 정식 인터페이스는 `framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:528-559,623`의 `ZLinkMeshNodeBuilder`·`addRouteMesh`이며 정식 예제도 `01-system-structure.ko.md:518-524`와 같다 — 네 곳의 `addSpotMesh` 사용을 `addRouteMesh` 기반 MeshNode 등록으로 바꾼다.

[1차소스][high] `framework/doc/framework/java/guide/06-actor-session.ko.md:61` — join 결과를 raw reply로 설명하고 존재하지 않는 `isJoined()`, `getSpot(Class)`, `await(replyType)`를 공개 표면처럼 제시한다 — `framework/doc/framework/spec/server/languages/java/02-handler-interfaces.ko.md:438-469`에서 상태는 `Optional<RoutingId> spotRid()`로 확인하며 `submit`은 `CompletionStage<ZLinkActorJoinResult<TReply>>`를 반환한다 — `Accepted`·`Rejected`를 분기하고 선언된 context 멤버만 사용하는 예제로 고친다.

[1차소스][medium] `framework/doc/framework/java/guide/06-actor-session.ko.md:66` — 모든 성공한 join 뒤 actor 객체를 더 사용하지 말라고 하여 local join과 remote transfer의 수명을 합친다 — `framework/doc/framework/spec/server/23-spot-actor.ko.md:47-58`은 같은 MeshNode join이 기존 Actor turn과 독립 control claim으로 완료된다고 규정하고, Java 계약도 `01-system-structure.ko.md:626-629`에서 기존 context의 `spotRid()` 갱신을 명시한다 — 객체 폐기 경고를 다른 MeshNode transfer로 한정하고 local join 동작을 분리한다.

[원칙][medium] `framework/doc/framework/java/guide/06-actor-session.ko.md:279` — 사용자 guide에 내부 dispatch 경로, actor-gateway 자동 배선과 Core SessionRelay 경로를 기술한다 — `AGENTS.md:16-23`은 guide에서 내부 구현을 제외하고 필요하면 internals를 링크하도록 규정한다 — 등록에 필요한 공개 호출과 사용자 관점 보장만 남기고 내부 배선은 internals로 옮긴다.

[1차소스][high] `framework/doc/framework/kotlin/guide/06-actor-session.ko.md:33` — Kotlin 예제가 제거 대상 `addSpotMesh`를 사용한다 — Kotlin 계약은 `framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md:548-561`에서 Java `ZLinkMeshNodeBuilder`를 소유자로 지정하고 `routeMesh` DSL을 선언하며, Java root 계약은 `java/02-handler-interfaces.ko.md:623`의 `addRouteMesh`다 — `routeMesh` 또는 `addRouteMesh` 기반 예제로 전환한다.

[1차소스][high] `framework/doc/framework/kotlin/guide/06-actor-session.ko.md:60` — join 결과를 raw reply로 설명하고 존재하지 않는 `isJoined()`·`getSpot(Class)`를 제시한다 — `framework/doc/framework/spec/server/languages/kotlin/02-handler-interfaces.ko.md:389-398`의 coroutine 표면은 모두 `ZLinkActorJoinResult<TReply>`를 반환하며, 상속한 context 선언은 `java/02-handler-interfaces.ko.md:438-446`뿐이다 — `awaitJoin` 결과의 `Accepted`·`Rejected`를 처리하고 `spotRid()`만 조회한다.

[1차소스][medium] `framework/doc/framework/kotlin/guide/06-actor-session.ko.md:64` — 모든 성공한 join 뒤 actor 객체가 폐기된다고 설명해 local join과 remote transfer를 구분하지 않는다 — `framework/doc/framework/spec/server/23-spot-actor.ko.md:47-58`은 같은 MeshNode join을 별도 계약으로 규정한다 — 폐기·접근 금지는 remote transfer에만 적용하고 local join 이후 context 상태를 별도로 설명한다.

[원칙][medium] `framework/doc/framework/kotlin/guide/06-actor-session.ko.md:287` — 사용자 guide가 내부 dispatch, gateway 자동 연결과 Core SessionRelay 배선을 노출한다 — `AGENTS.md:16-23`의 guide/internals 경계를 위반한다 — 공개 등록 절차와 관찰 가능한 동작만 남기고 내부 경로는 internals 문서로 분리한다.

[원칙][medium] `framework/doc/framework/spec/stream-connector/32-stream-connector.ko.md:527` — connector 상태와 무관하다고 스스로 규정한 범용 테스트 단언 세 개를 production connector 공개 계약에 추가한다 — connector 패키지 책임은 `framework/doc/framework/spec/stream-connector/README.ko.md:5-14`의 client·wire·session 계약이고, `doc/principal/software-design-principles.md:148-179`는 특정 사용처의 특수 로직을 그 상위 계층이 소유하도록 요구한다 — `ensure`·`expectFailure`·`expectTimeout`을 E2E test harness/testkit으로 옮기고 네 언어 connector exact 계약에서 제거한다.

[1차소스][medium] `framework/doc/framework/spec/90-implementation-gap.ko.md:691` — 현재 잔여 gap으로 “C++ connector 계약 문서가 없다”고 단정한다 — 실제 정식 문서가 `framework/doc/framework/spec/stream-connector/languages/cpp/03-stream-connector.ko.md:5-18`에 존재하고 package index도 `stream-connector/README.ko.md:16-23`에서 이를 열거한다 — 해당 행을 해결됨으로 정리하거나 현재 남은 구현 gap만 기술한다.

## 실행 증거

```text
model/session: Codex (GPT-5) / 019f6ef0-be4c-7c92-8c0c-81ca38a37ace
start-freeze: HEAD=169c458ed238228d7a23cea089c8c467c96b953c; scope-files.txt=06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d; scope-files.sha256=363f3945c8178b28e1b86ba87c69c4d3ae60ad740c551f1c788dca4e9b65f07f; per-file=205/205 OK
checks: FRAMEWORK DOC CONTRACTS CLEAN; actual pymdownx 10.21.2 render=205/205, local file/anchor errors=0
end-freeze: HEAD=169c458ed238228d7a23cea089c8c467c96b953c; both aggregate hashes unchanged; per-file=205/205 OK
```
