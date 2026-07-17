# S3 문서 독립 리뷰 — iteration 23 (Codex)

## 실행 증거

- provider/model: OpenAI / GPT-5 Codex
- session ID: `019f6e75-c8d0-7a00-a2eb-e09594e54131`
- 시작·종료 HEAD: `169c458ed238228d7a23cea089c8c467c96b953c`
- `scope-files.txt` SHA-256: `06f3695c8571e2c253c8f7af522dbe7d9fd54596c94eb5f724743c392087470d`
- `scope-files.sha256` SHA-256: `5379db55546bf16e9bee1d77ca12f7282c3c06c4892efa29c3fb7c2b966564c6`
- 파일별 hash: 205/205 일치
- contract verifier: CLEAN
- read-only 실행, 작성 파일 0개, 기존 dirty worktree 보존

## Findings

[원칙][medium] framework/doc/framework/cpp/guide/07-channel-messaging.ko.md:273 — 사용자 guide가 ROUTER endpoint와 동일 socket 송수신 배선을 설명한다 — AGENTS.md는 내부 socket 배선을 internals로 분리하도록 규정한다 — 공개 설정의 효과만 남기고 ROUTER 배선과 다이어그램은 C++ internals로 옮겨 링크한다.

[1차소스][medium] framework/doc/framework/spec/http-client/12-http-client.ko.md:158 — timeout을 §05 오류 kind가 아닌 boundary 상태로 단정한다 — 09-error-model §9.1~9.2는 .NET·Java·Kotlin·Node에서 기존 `RequestFailed` kind와 원인 또는 retriable 표식을 함께 사용하도록 규정한다 — “전용 timeout kind는 없지만 managed language에서는 RequestFailed로 보고한다”로 고친다.

[1차소스][high] framework/doc/framework/spec/stream-connector/32-stream-connector.ko.md:224 — 압축 수신 한도를 압축된 payload만 기준으로 규정한다 — .NET guide와 C++·.NET·Java·TypeScript 구현은 같은 수신 한도를 압축 해제 결과에도 적용하므로 언어별 수락 결과가 정식 계약과 어긋난다 — wire payload와 압축 해제 결과의 두 한도 검사를 정식 계약에 명시하고 오류·종료 의미를 고정한다.

[1차소스][high] framework/doc/framework/spec/stream-connector/languages/dotnet/03-stream-connector.ko.md:267 — `FrameTooLarge`가 연결을 종료하고 `TransportError`와 reconnect를 유발한다는 의미가 .NET 문서에만 있다 — 공통 §6.3은 언어 문서가 종료 사유의 타입 이름과 노출 방식만 소유한다고 제한하며, 공통 §9에는 오류별 terminal 여부·종료 사유·reconnect 표가 없다 — 공통 spec에 오류별 lifecycle 행렬을 추가하고 모든 exact projection을 그 행렬에 맞춘다.

[1차소스][high] framework/doc/framework/spec/stream-connector/languages/typescript/03-stream-connector.ko.md:61 — “정확한 public 표면”이 불완전하다 — `RequiredZlinkStreamConnectorOptions`, `ZlinkStreamExpectNoneCall`, `ZlinkStreamSequenceCall` 등을 선언 없이 사용하고, line 140의 options에는 `meterProvider`만 있지만 실제 public options에는 필수 `endpoint`와 codec·transport·timeout·queue·reconnect 등 다수 필드가 있다 — 전체 공개 선언을 수록하거나 고정된 API snapshot을 정본으로 연결하고 누락 검사를 추가한다.

[1차소스][high] framework/doc/framework/spec/90-implementation-gap.ko.md:144 — connector wire 계약이 모두 해소됐다고 단정한다 — 같은 문서 line 99·124와 gaps/java의 미완료 §12.1은 Java 수신 queue overflow가 여전히 반대 동작임을 기록하며, §10.5가 바로 그 overflow 계약이다 — blanket 완료 문장을 제거하고 Java/Kotlin의 미완료 상태와 대상 구현을 명시한다.

[1차소스][high] framework/languages/java/e2e/SpotService/feature-map.ko.md:103 — `SM-D10`을 구현됨으로 분류하면서 오래된 push를 제거하고 최신 push를 유지한다고 검증한다 — 공통 connector §10.1은 기존 메시지를 유지하고 새 메시지를 버리도록 규정하며 gaps/java §12.1도 현재 Java 동작을 미충족으로 기록한다 — `SM-D10`을 미충족으로 표시하고 기존 메시지 유지·신규 drop·`ReceivedMessageDropped`를 검증하도록 바꾼다.

[1차소스][high] framework/languages/java/e2e-kotlin/SpotService/feature-map.ko.md:116 — Kotlin `SM-D10`도 오래된 push 제거와 최신 push 유지를 완료 증거로 사용한다 — Kotlin이 공유하는 Java connector의 이 동작은 공통 §10.1과 반대이며 중앙 gap에서 미완료다 — Java §12.1을 상속하는 blocked 상태로 기록하고 정식 queue admission 의미를 검증한다.

[1차소스][high] framework/languages/java/e2e-kotlin/ObservabilityOps/feature-map.ko.md:9 — Kotlin 역할 host 없이 공유 Java runtime으로 실행한 시나리오 대부분을 PASS로 표시한다 — 같은 문서 line 23과 gaps/java의 E2E-KT-06은 Kotlin 고유 metric·drain·flow 결함을 관측할 수 없다고 기록하며, 공통 E2E 규칙은 해당 언어 framework의 공개 API로 역할을 실행하도록 요구한다 — Kotlin host가 실제 역할을 수행하기 전까지 관련 행을 전환 대상으로 표시한다.

[1차소스][medium] framework/languages/node/e2e/ToActorMessaging/feature-map.ko.md:11 — 없는 actor의 send와 request가 모두 `actorRouteNotFound`를 검증한다고 적었다 — 공통 Config 9 TA-B1은 request만 해당 오류로 분류하고 one-way send는 local submit과 handler·location evidence 부재로 검증하며, 현재 Node scenario 코드도 그 방식으로 동작한다 — request 오류와 send local acceptance·negative evidence를 분리해 기술한다.
