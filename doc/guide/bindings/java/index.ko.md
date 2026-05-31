[바인딩 가이드](../README.ko.md) · [코어 가이드](../../01-overview.ko.md)

# Java 바인딩 가이드 (`systems.zlink`)

이 묶음은 **Java에서 zlink를 사용하는 방법**을 기능별로, 실제 샘플 코드 중심으로
설명합니다. Java 사용자가 가장 먼저 보는 진입 문서입니다.

메시징 **개념의 깊은 설명**(왜 DEALER인지, PUB/SUB 시맨틱, 라우팅 ID 정책 등)은
언어 중립적으로 [코어 가이드](../../01-overview.ko.md)가 소유합니다. 이 가이드는
각 기능을 **Java API로 어떻게 쓰는지**에 집중하고, 개념이 필요한 지점마다 코어
챕터로 링크합니다.

## 이 가이드 읽는 법

- **바로 쓰려는 사람** → [01 시작하기](./01-getting-started.ko.md)에서 의존성을 추가하고
  5분 예제를 띄운 뒤, 필요한 기능 문서로 이동하세요.
- **메시징이 처음인 사람** → 코어 가이드 [개요](../../01-overview.ko.md)와
  [소켓 패턴](../../03-0-socket-patterns.ko.md)을 먼저 본 뒤 여기로 돌아오세요.
- **API 멤버를 찾는 사람** → [API 레퍼런스 생성](./05-reference.ko.md#api-레퍼런스-생성).
- **메인테이너** → 구현 계약 [`doc/spec/bindings/java`](../../../spec/bindings/java/README.ko.md).

## 문서 구성

| 문서 | 내용 |
|------|------|
| [01 시작하기](./01-getting-started.ko.md) | 의존성 추가, 5분 예제, 핵심 타입(Context·Message·Received·RoutingId), 소유권 규칙 |
| [02 메시징](./02-messaging.ko.md) | 소켓 패턴별 사용법 — PAIR / DEALER·ROUTER / PUB·SUB / XPUB·XSUB / STREAM / Proxy |
| [03 서비스](./03-services.ko.md) | Registry · Discovery · SpotNode·Spot · Actor |
| [04 운영](./04-operations.ko.md) | 소켓 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 · 네이티브 라이브러리 |
| [05 레퍼런스](./05-reference.ko.md) | 에러 처리 · 코덱 · C API↔Java 대응표 · API 레퍼런스 · 샘플 |

## 기능 지도

이 바인딩이 제공하는 기능 전체입니다.

| 기능 | Java 타입 / 진입점 | 한 줄 설명 | 가이드 | 개념(core) |
|---|---|---|---|---|
| 컨텍스트 | `Zlink.createContext()` → `Context` | 런타임 진입점, I/O 스레드 소유 | [01](./01-getting-started.ko.md) | [02](../../02-core-api.ko.md) |
| 메시지 | `Message` | 페이로드 프레임(단일/멀티파트) | [01](./01-getting-started.ko.md) | [09](../../09-message-api.ko.md) |
| 수신 버퍼 | `Received` | 재사용 가능한 수신 봉투 | [01](./01-getting-started.ko.md) | [09](../../09-message-api.ko.md) |
| 라우팅 ID | `RoutingId` | 피어/스팟 식별 값 | [01](./01-getting-started.ko.md) | [08](../../08-routing-id.ko.md) |
| PAIR | `ctx.createPairSocket()` | 1:1 배타적 연결 | [02](./02-messaging.ko.md#pair) | [03-1](../../03-1-pair.ko.md) |
| DEALER/ROUTER | `ctx.createDealerSocket()` / `createRouterSocket()` | 비동기 요청/응답·라우팅 | [02](./02-messaging.ko.md#dealer--router) | [03-3](../../03-3-dealer.ko.md) |
| PUB/SUB | `ctx.createPubSocket()` / `createSubSocket()` | 토픽 발행/구독 | [02](./02-messaging.ko.md#pub--sub) | [03-2](../../03-2-pubsub.ko.md) |
| XPUB/XSUB | `ctx.createXPubSocket()` / `createXSubSocket()` | 구독 이벤트 가시화 | [02](./02-messaging.ko.md#xpub--xsub) | [03-2](../../03-2-pubsub.ko.md) |
| STREAM | `ctx.createStreamSocket()` | 원시 TCP·패킷 프레이밍 | [02](./02-messaging.ko.md#stream) | [03-5](../../03-5-stream.ko.md) |
| 프록시 | `Zlink.proxy(...)` | 프론트/백엔드 중계 | [02](./02-messaging.ko.md#프록시-proxy) | [03-6](../../03-6-proxy.ko.md) |
| Registry | `ctx.createRegistry()` | 클러스터 서비스 카탈로그 | [03](./03-services.ko.md#registry) | [07-4r](../../07-4-registry.ko.md) |
| Discovery | `ctx.createDiscovery(...)` | 서비스 발견·라우트 해석 | [03](./03-services.ko.md#discovery) | [07-1](../../07-1-discovery.ko.md) |
| SpotNode/Spot | `ctx.createSpotNode()` / `node.createSpot()` | 메시 노드와 메시징 엔드포인트 | [03](./03-services.ko.md#spotnode--spot) | [07-3](../../07-3-spot.ko.md) |
| Actor | `node.createActor("id")` | 상태 보유 엔티티(세션·플레이어 등) | [03](./03-services.ko.md#actor) | [07-4](../../07-4-actor.ko.md) |
| 소켓 옵션 | `socket.options()` | HWM·타임아웃·하트비트 등 | [04](./04-operations.ko.md#소켓-옵션) | [12](../../12-socket-options.ko.md) |
| TLS 보안 | `socket.setTlsServer/setTlsClient` | 전송 암호화 | [04](./04-operations.ko.md#tls-보안) | [05](../../05-tls-security.ko.md) |
| 모니터링 | `socket.monitorOpen(...)` | 연결 수명 이벤트 | [04](./04-operations.ko.md#모니터링) | [06](../../06-monitoring.ko.md) |
| 폴러/타이머 | `Zlink.createPoller()` / `createTimer()` | 다중 소켓 폴링·타이머 | [04](./04-operations.ko.md#폴러--타이머) | [02](../../02-core-api.ko.md) |
| 스레딩 | — | 컨텍스트 공유, 소켓 단일 스레드 | [04](./04-operations.ko.md#스레딩) | [11](../../11-thread-safety.ko.md) |
| 직렬화(코덱) | `zlink-codec-json` 등 | JSON/MessagePack/Protobuf | [05](./05-reference.ko.md#코덱) | — |
| 에러 처리 | `ZlinkException` 계층 | 작업별 타입 예외 | [05](./05-reference.ko.md#에러-처리) | — |

> 전송 방식(`tcp`/`ipc`/`inproc`/`ws`/`tls`)은 모든 소켓에 공통이며
> [트랜스포트](../../04-transports.ko.md), 성능 튜닝은 [성능](../../10-performance.ko.md)을
> 참고하세요.
