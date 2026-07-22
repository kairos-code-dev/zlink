# ZLink Framework 11.0.0 스펙

이 디렉토리는 ZLink가 배포하는 server framework, HTTP client와 Stream connector의 공개 계약을
관리한다. 폴더는 package를 구분하고 숫자 prefix는 주제와 읽는 순서를 나타낸다.

```text
spec/
├── 00~05               Common contracts
├── server/              Server framework contracts
│   └── languages/       Language-specific public APIs
├── http-client/         HTTP client contracts
│   └── languages/
└── stream-connector/    Stream connector contracts
    └── languages/
```

이 트리는 public contract만 소유한다. 사용 목적과 예제는 언어별 guide가, socket 배선과 thread model은
internals 문서가 소유한다.

## 기반 계약

| 문서 | 범위 |
|---|---|
| [00 공개 계약 관리](00-public-contract-governance.ko.md) | 계약 소유권, 언어별 표현과 검증 |
| [01 개요](01-overview.ko.md) | RouteMesh, 메시징 기능과 owner |
| [02 상호작용 모델](02-interaction-model.ko.md) | node·channel·Spot·Actor·fanout·STREAM의 완료 의미 |
| [03 메시지 모델](03-message-model.ko.md) | typed payload, metadata와 reply correlation |
| [04 비동기 실행 정책](04-async-execution-policy.ko.md) | submit, handler turn, timer와 cancellation |
| [05 Framework API](05-framework-api.ko.md) | 언어 중립 API family, 등록과 오류 kind |

## Server framework

| 번호대 | 문서 |
|---|---|
| `1x` | [10 Channel topology](server/10-channel-topology.ko.md) · [11 Channel 메시징](server/11-channel-messaging.ko.md) · [12 ClientServer Channel](server/12-client-server-channel.ko.md) · [13 Network listener identity](server/13-network-listener-identity.ko.md) |
| `2x` | [20 Spot 메시징](server/20-spot-messaging.ko.md) · [21 MeshNode](server/21-mesh-node.ko.md) · [22 Actor 모델](server/22-actor-model.ko.md) · [23 Spot Actor](server/23-spot-actor.ko.md) · [24 Spot 주소 메시징](server/24-spot-address-messaging.ko.md) · [25 Stage wrapper](server/25-stage-wrapper-on-spot.ko.md) |
| `3x` | [30 STREAM 서버 세션](server/30-stream-session.ko.md) · [31 Session Actor dispatch](server/31-session-actor-dispatch.ko.md) |
| `4x` | [40 Location runtime](server/40-location-runtime.ko.md) · [41 Redis Location Store](server/41-location-store-redis.ko.md) · [42 Redis Transfer Store](server/42-transfer-store-redis.ko.md) |
| `5x` | [50 Runtime monitoring](server/50-runtime-monitoring.ko.md) · [51 Runtime metrics](server/51-runtime-metrics.ko.md) · [52 Message flow tracing](server/52-message-flow-tracing.ko.md) · [53 Flow correlation](server/53-flow-correlation.ko.md) · [54 Host retirement와 shutdown](server/54-graceful-drain-handoff.ko.md) · [55 Transport liveness](server/55-transport-liveness.ko.md) |

언어별 정확한 server public API는 [server/languages](server/languages/README.ko.md)가 소유한다.

- [C++](server/languages/cpp/README.ko.md)
- [.NET](server/languages/dotnet/README.ko.md)
- [Java](server/languages/java/README.ko.md)
- [Kotlin](server/languages/kotlin/README.ko.md)
- [Node.js](server/languages/node/README.ko.md)

RouteMesh·MeshNode의 .NET 시그니처는
[.NET RouteMesh·MeshNode 인터페이스](server/languages/dotnet/interfaces/03-configuration-topology.ko.md)가 소유한다.

## HTTP client

| 문서 | 범위 |
|---|---|
| [12 HTTP client](http-client/12-http-client.ko.md) | framework-facing typed HTTP client 계약 |
| [상세 계약](http-client/README.ko.md) | builder, 응답, 실행, 인증, TLS, proxy, codec과 오류 |

언어별 public API는 package의 [languages](http-client/languages/) 아래에서 관리한다.

## Stream connector

[32 Stream connector](stream-connector/32-stream-connector.ko.md)는 client 실행 환경, transport, wire,
lifecycle과 배포 산출물의 계약을 정의한다. 언어별 public API는 package의
[languages](stream-connector/languages/) 아래에서 관리한다.

## 문서 경계

- 공통 스펙은 특정 언어의 타입이나 문법을 계약으로 강제하지 않는다.
- 언어별 스펙은 정확한 public 타입, 시그니처와 비동기 표현을 고정한다.
- server, HTTP client와 Stream connector 계약은 각 package 폴더에서 관리한다.
- sample과 E2E는 정식 스펙을 검증하며 public interface의 출처가 되지 않는다.
- 11.0 구현은 이 정식 spec과 언어별 internals의 목표 구조를 기준으로 진행한다. 구현 누락과 진행 상태는
  [implementation gap](90-implementation-gap.ko.md)과 실행 ledger에만 기록한다.
