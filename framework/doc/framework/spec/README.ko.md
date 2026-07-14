# ZLink Framework 스펙

이 디렉토리는 ZLink가 배포하는 **세 패키지의 공개 계약**을 한 자리에서 관리한다.

**두 축으로 정리한다.**

- **폴더 = 패키지.** 어느 산출물이 배포하는 계약인가.
- **숫자 prefix = 주제와 읽는 순서.** 인용 키이기도 하다(`[04 §1.1]`, `[12 §3]`, `[32 §5.2]`).

```text
spec/
├── 00~05, 90            기반 계약 — 세 패키지가 모두 따른다
├── server/              framework(서버) 패키지
│   └── languages/       언어별 public API
├── http-client/         HTTP client 패키지
│   └── languages/
└── stream-connector/    Stream connector 패키지
    └── languages/
```

**패키지 폴더 밑에 그 패키지의 언어별 인터페이스가 있다.** 어떤 패키지의 public 표면을 확인하려면
그 폴더만 열면 된다. 서버 계약과 client 계약이 한 파일에 섞이지 않는다.

**guide·internals·perf는 여기 없다.** 이 트리는 **계약**만 소유한다. 사용 안내는
`framework/doc/framework/<lang>/guide/`, connector 안내는 `framework/doc/stream-connector/<lang>/`,
HTTP client 안내는 `framework/doc/http-client/<lang>/`에 있다.

## 기반 계약 — 세 패키지 공통

이 문서들은 **어느 한 패키지의 것이 아니다.** `00`의 계약 관리 절차는 셋 다 따르고, `03`의 reply
상관관계(sequence 단독)는 channel과 STREAM에 똑같이 적용되며, `04`의 terminator는 server handler와
HTTP client가 함께 쓴다. `05`의 오류 kind는 framework와 **HTTP client**가 공유한다
([12 §6](http-client/12-http-client.ko.md)) — **connector는 자기 오류 집합을 따로 갖는다**
([32 §7](stream-connector/32-stream-connector.ko.md)).

**"공통"이 "모든 문장이 세 패키지에 해당한다"는 뜻은 아니다.** 각 문서 안에는 server 전용 서술이
섞여 있다. 기준은 **계약의 소유권**이다 — 그 축을 어느 한 패키지 폴더에 가두면 다른 패키지가
그 계약을 참조할 근거를 잃는다.

| 문서 | 범위 |
|------|------|
| [00 공개 계약 관리](00-public-contract-governance.ko.md) | 정식 계약과 draft, 변경 통제, 검증 규칙. **먼저 읽는다** |
| [01 개요](01-overview.ko.md) | framework가 무엇을 해결하는가 |
| [02 상호작용 모델](02-interaction-model.ko.md) | request·send·publish의 공용 의미 |
| [03 메시지 모델](03-message-model.ko.md) | header + payload의 multipart wire 구성, reply 상관관계 |
| [04 비동기 실행과 coroutine 정책](04-async-execution-policy.ko.md) | 세 terminator(`submit`/`async`/`yield`), CPU·I/O worker, 취소 |
| [05 framework API](05-framework-api.ko.md) | framework 역할과 등록 표면, 공용 오류 모델 |
| [90 언어별 구현 차이](90-implementation-gap.ko.md) | 정식 spec과 현재 5개 구현의 차이 |

## [server/](server/) — framework(서버) 패키지

| 번호대 | 문서 |
|---|---|
| `1x` | [10 channel topology](server/10-channel-topology.ko.md) · [11 channel 메시징](server/11-channel-messaging.ko.md) |
| `2x` | [20 SPOT 메시징](server/20-spot-messaging.ko.md) · [21 SpotNode](server/21-spot-node.ko.md) · [22 Actor 모델](server/22-actor-model.ko.md) · [23 Spot Actor Join/Transfer](server/23-spot-actor.ko.md) · [24 Spot 주소 메시징](server/24-spot-address-messaging.ko.md) · [25 Stage Wrapper](server/25-stage-wrapper-on-spot.ko.md) |
| `3x` | [30 STREAM 서버 세션](server/30-stream-session.ko.md) · [31 Session Actor Dispatch](server/31-session-actor-dispatch.ko.md) |
| `4x` | [40 location runtime](server/40-location-runtime.ko.md) · [41 Redis location store](server/41-location-store-redis.ko.md) |
| `5x` | [50 런타임 모니터링](server/50-runtime-monitoring.ko.md) · [51 런타임 메트릭](server/51-runtime-metrics.ko.md) · [52 메시지 흐름 추적](server/52-message-flow-tracing.ko.md) · [53 흐름 상관관계](server/53-flow-correlation.ko.md) · [54 Graceful Drain & Handoff](server/54-graceful-drain-handoff.ko.md) |

**STREAM은 `30`/`31`(서버)과 `32`(client connector)로 갈린다.** `32`는 별도 패키지이므로
[stream-connector/](stream-connector/)가 소유한다.

**언어별 public API:** [server/languages/](server/languages/README.ko.md) —
[cpp](server/languages/cpp/README.ko.md) ·
[dotnet](server/languages/dotnet/README.ko.md) ·
[java](server/languages/java/README.ko.md) ·
[kotlin](server/languages/kotlin/README.ko.md) ·
[node](server/languages/node/README.ko.md)

## [http-client/](http-client/README.ko.md) — HTTP client 패키지

별도 패키지로 배포하지만 **계약은 framework가 소유한다.** framework application이 외부 API와
레거시 API를 zlink 스타일로 부르는 전용 client다.

| 문서 | 범위 |
|------|------|
| [12 HTTP client](http-client/12-http-client.ko.md) | **framework-facing 계약** — 정체성, fluent builder, terminator, turn seam, 등록, codec, 오류 |
| [01](http-client/01-scope-and-architecture.ko.md)~[11](http-client/11-regression-tests.ko.md) | 상세 계약 — builder, 응답, 실행 모델, redirect·retry·cookie, 인증·TLS·proxy, 압축, 오류, 회귀 |

**언어별 public API:**
[cpp](http-client/languages/cpp/cpp-http-client.ko.md) ·
[dotnet](http-client/languages/dotnet/dotnet-http-client.ko.md) ·
[java](http-client/languages/java/java-http-client.ko.md) ·
[kotlin](http-client/languages/kotlin/kotlin-http-client.ko.md) ·
[node](http-client/languages/node/node-http-client.ko.md)
([대조표](http-client/language-interfaces.ko.md)는 비규범이다)

## [stream-connector/](stream-connector/README.ko.md) — Stream connector 패키지

브라우저·게임엔진에서 도는 **client** 산출물이다. framework host와 배포 단위·실행 환경이 다르다.

| 문서 | 범위 |
|------|------|
| [32 Stream Connector](stream-connector/32-stream-connector.ko.md) | **framework-facing 계약** — 대상 실행 환경, transport, wire, 생명주기, 배포 산출물 |

**언어별 public API:**
[dotnet](stream-connector/languages/dotnet/03-stream-connector.ko.md) ·
[java](stream-connector/languages/java/03-stream-connector.ko.md) ·
[typescript](stream-connector/languages/typescript/README.ko.md)

## 규칙

**공통 스펙은 특정 언어의 타입이나 문법을 계약으로 강제하지 않는다.** 이해를 돕는 언어별 예시가
있더라도 그 예시는 **비규범 설명**이다. 정확한 public 타입과 시그니처는 각 패키지의
`languages/` 문서만 소유한다.

**Node.js와 TypeScript를 나눈 이유:** stream connector는 브라우저에서 도는 client이고 Node.js
framework host와 배포 단위·실행 환경이 다르다. Node.js framework 계약은
[server/languages/node](server/languages/node/README.ko.md)가, browser connector 계약은
[stream-connector/languages/typescript](stream-connector/languages/typescript/README.ko.md)가
소유한다([00 §4](00-public-contract-governance.ko.md)).

**C++은 예외**로, framework 자체를 구현하므로 기능별 스펙을 유지한다.

언어별 정식 스펙은 각 언어가 제공해야 하는 **목표 public contract**를 고정한다.
**현재 구현이 정식 스펙과 다르면 정식 스펙을 코드에 맞춰 축소하지 않는다.**
[언어별 구현 차이](90-implementation-gap.ko.md)에 기록한 뒤 **구현이 스펙을 따르게 한다.**

"(제안)" 표시 문서는 아직 [공개 계약 관리](00-public-contract-governance.ko.md)의 승격 절차를
거치지 않은 제안 스펙이다.
