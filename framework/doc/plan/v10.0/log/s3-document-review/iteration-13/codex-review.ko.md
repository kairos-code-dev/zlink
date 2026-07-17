# S3 iteration 13 — Codex 독립 문서 리뷰

## 동결 검증

- 시작·종료 파일 hash: 202/202 일치
- 문서 집합 SHA-256: `6ffed18e000bdb2df033499ec16eb4544e47cc183dfc18d84b29220898888e31`
- 파일 목록 SHA-256: `dfabc81f2c0ec7403683bc54ab3f350d9cf6565aac60a3baf9579192c513642f`
- 기준 commit: `169c458ed238228d7a23cea089c8c467c96b953c`
- 파일 수정: 없음

## Finding

[1차소스][high] framework/doc/framework/spec/server/23-spot-actor.ko.md:35 — source `OnLeaveActor`가 location CAS보다 먼저 현재 membership을 정리하므로 CAS가 stale로 실패해도 source callback side effect가 이미 발생하며, stale generation·epoch가 membership을 변경하지 않는다는 같은 문서의 검증 요구와 양립하지 않는다 — Core 계약은 accepted join reply를 유일한 commit point로 규정하고 reject 시 source membership을 유지하며(`core/doc/spec/core/service/04-actor.ko.md:227-242`), Framework 문서도 stale 실패의 무변경을 요구한다(`23-spot-actor.ko.md:95`) — CAS 성공 뒤 source leave를 실행하거나, 기존 순서를 유지해야 한다면 source prepare와 rollback·callback 보상 의미를 정식 계약으로 정의한다.

[1차소스][high] framework/doc/framework/spec/server/40-location-runtime.ko.md:71 — Actor location row에 Spot lifecycle generation이 없어 같은 RID로 다시 만들어진 Spot과 이전 membership을 구분할 수 없다 — Core의 `zlink_actor_location_t`는 `spot_generation`을 필수로 보존하고 같은 RID의 새 Spot을 이전 location이 가리키지 않아야 한다고 규정한다(`core/doc/spec/core/service/04-actor.ko.md:49-56,128-132`); Redis canonical row와 네 언어 exact record도 모두 이 필드를 누락했다(`41-location-store-redis.ko.md:106-107`) — 공통 Actor location, 모든 언어 exact record, Redis field 순서와 fixture에 Spot lifecycle generation을 추가하고 resolver가 이를 검증하도록 고정한다.

[1차소스][high] framework/doc/framework/spec/server/41-location-store-redis.ko.md:57 — transfer key만 `meshName`과 `actorId`를 raw `:` 구분자로 이어 붙여 서로 다른 actor가 같은 authority key를 만들 수 있다 — MeshName과 Actor ID는 NUL만 금지한 임의 UTF-8이므로 `:`가 허용되고(`core/doc/spec/core/service/01-mesh-node.ko.md:138-140`, `core/doc/spec/core/service/04-actor.ko.md:128`), `(a:b,c)`와 `(a,b:c)`는 같은 `P:transfer:a:b:c:{transferId}` 및 active index를 만든다; 같은 문서는 일반 row key에서 이 충돌을 막으려고 length-prefix를 요구한다(`41-location-store-redis.ko.md:43-46`) — 모든 transfer key의 가변 필드를 UTF-8 byte 길이 기반 length-prefix로 encode하고 colon·비ASCII 충돌 fixture를 추가한다.

[1차소스][high] framework/languages/java/e2e-kotlin/ToActorMessaging/feature-map.ko.md:9 — Kotlin `TA-A3`와 `TA-A4`가 공통 P0 시나리오 대신 각각 actor 생성 전 fail-fast와 처음부터 unbound인 row를 검증하며 전체 E2E 통과를 주장한다 — 공통 `TA-A3`는 live actor의 bind 전 send/request 성공 뒤 실제 session bind와 push를 요구하고(`config-9-to-actor-messaging.ko.md:89-97`), `TA-A4`는 먼저 bind한 session을 unbind/disconnect한 뒤 actor 생존을 검증한다(`:99-113`); Kotlin 역할 목록에도 session gateway가 없다(`feature-map.ko.md:15-17`) — 실제 stream gateway를 포함해 두 lifecycle을 그대로 검증하거나 두 행을 미구현·차단으로 표시한다.

[1차소스][high] framework/languages/node/e2e/SpotActorTransfer/feature-map.ko.md:14 — Node는 `ST-B3`를 “adapter 미등록 실패”로 구현 완료 처리해 공통 계약과 정반대 동작을 PASS로 인정한다 — 공통 P0 시나리오는 adapter가 없어도 framework 기본 빈 state transfer가 성공해야 한다고 고정한다(`config-10-spot-actor-transfer.ko.md:158-169`); Kotlin map도 같은 잘못된 목표 문구를 반복한다(`framework/languages/java/e2e-kotlin/SpotActorTransfer/feature-map.ko.md:14`) — 두 map을 기본 빈 state 성공 의미로 정정하고 Node runner가 source·target 기본 callback 및 성공 commit 순서를 실제로 단언하게 한다.

[1차소스][medium] framework/doc/framework/spec/server/30-stream-session.ko.md:87 — server spec은 framework·HTTP client·connector가 codec registry를 공유한다고 하지만 각 package의 정식 계약은 서로 다른 소유 모델을 정의한다 — HTTP client는 registry instance와 host별 등록이 별도라고 명시하고(`http-client/12-http-client.ko.md:141-145`), connector는 instance별 typed codec option 하나를 소유하며(`stream-connector/32-stream-connector.ko.md:293-301`), 공통 API 표도 server root와 connector 표면을 분리한다(`05-framework-api.ko.md:188-200`) — 공유 대상은 codec 번호와 typed payload 계약임을 명시하고 registry instance는 server·HTTP host·connector별로 분리하며, 잘못 연결된 HTTP `[11 §6]` 링크도 실제 codec owner로 고친다.

[1차소스][medium] framework/doc/framework/spec/server/51-runtime-metrics.ko.md:86 — transfer metric을 “terminal completion”까지 측정하면서 성공 outcome을 `committed`로 고정해 transfer의 정식 terminal 상태와 충돌한다 — 공통 API는 deadline terminal을 `Activated|Aborted`로 정의하고 commit 뒤에는 activation recovery를 계속한다고 규정하며(`05-framework-api.ko.md:56-60`), Redis transfer도 `activate`를 terminal 전이로 정의한다(`41-location-store-redis.ko.md:148-160`); Config 11은 다시 out→commit ack만 duration으로 측정한다(`config-11-observability-ops.ko.md:146-155`) — 성공 outcome을 `activated`로 정렬하고 activation 또는 성공 reply까지 측정하거나, commit metric을 별도의 phase metric으로 분리한다.

[1차소스][medium] framework/doc/framework/common/e2e/config-4-registration-codec.ko.md:160 — `RC-B5`가 codec 불일치 결과를 JSON fallback과 public decode error 중 아무 쪽이나 허용해 언어별로 다른 wire 의미가 모두 통과할 수 있다 — 실제 언어 map들은 Protobuf→JSON-only peer를 실패로 검증하고 C++는 `payload_decode_failed`까지 고정하지만(`framework/languages/cpp/e2e/RegistrationCodec/feature-map.ko.md:22`), 공통 시나리오는 선택지를 닫지 않는다 — explicit non-JSON content-type에 대응 codec이 없을 때의 정확한 public error kind를 하나로 고정하고, JSON fallback은 outbound 미지원 타입 같은 별도 조건으로 분리한다.

[원칙][medium] framework/doc/framework/kotlin/guide/07-stream.ko.md:74 — 사용자 guide가 `close()`를 “현재 구현은 no-op”이라고 설명해 정식 사용 의미와 현재 문서 집합의 구현 상태 모두를 잘못 안내한다 — Kotlin은 Java의 `ZLinkSessionContext` 계약을 그대로 사용하고 Java guide는 `close()`가 서버 연결을 종료한다고 설명하며(`framework/doc/framework/java/guide/07-stream.ko.md:88-95`), Java gap 문서도 no-op 결함을 이미 수정했다고 기록한다(`framework/doc/framework/spec/gaps/java.ko.md:382-386`) — 구현 진행 설명을 guide에서 제거하고 인증 실패·protocol 위반 시 연결을 종료하는 공개 동작으로 정렬한다.
