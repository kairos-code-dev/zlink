[바인딩 가이드](../README.md) · [코어 가이드](../../01-overview.md)

# Node.js 바인딩 가이드 (`@zlink-systems/zlink`)

이 묶음은 **Node.js에서 zlink를 사용하는 방법**을 기능별로, 실제 샘플 코드 중심으로
설명합니다.

메시징 개념은 [코어 가이드](../../01-overview.md)를 참고하세요.

## 문서 구성

| 문서 | 내용 |
|------|------|
| [01 시작하기](./01-getting-started.md) | 설치, 5분 예제, 핵심 타입, 소유권 규칙 |
| [02 메시징](./02-messaging.md) | 소켓 패턴별 사용법 |
| [03 서비스](./03-services.md) | Registry · Discovery · SpotNode·Spot · Actor |
| [04 운영](./04-operations.md) | 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 |
| [05 레퍼런스](./05-reference.md) | 에러 처리 · 코덱 · C API↔Node 대응표 · 샘플 |

## 기능 지도

| 기능 | Node 진입점 | 한 줄 설명 | 가이드 |
|---|---|---|---|
| 컨텍스트 | `zlink.createContext()` | 런타임 진입점 | [01](./01-getting-started.md) |
| 메시지 | `Buffer` | 페이로드 (버퍼) | [01](./01-getting-started.md) |
| 수신 | `new zlink.Received()` | 수신 봉투 | [01](./01-getting-started.md) |
| 라우팅 ID | `new zlink.RoutingId(buf)` | 피어 식별 값 | [01](./01-getting-started.md) |
| PAIR | `zlink.createPairSocket(ctx)` | 1:1 배타적 연결 | [02](./02-messaging.md#pair) |
| DEALER/ROUTER | `createDealer/RouterSocket` | 요청/응답 | [02](./02-messaging.md#dealer--router) |
| PUB/SUB | `createPub/SubSocket` | 토픽 발행/구독 | [02](./02-messaging.md#pub--sub) |
| XPUB/XSUB | `createXPub/XSubSocket` | 구독 이벤트 | [02](./02-messaging.md#xpub--xsub) |
| STREAM | `createStreamSocket` | 원시 TCP | [02](./02-messaging.md#stream) |
| Registry | `zlink.createRegistry(ctx)` | 서비스 카탈로그 | [03](./03-services.md#registry) |
| Discovery | `zlink.createDiscovery(...)` | 서비스 발견 | [03](./03-services.md#discovery) |
| SpotNode/Spot | `createSpotNode` / `node.createSpot()` | 메시 노드 | [03](./03-services.md#spotnode--spot) |
| Actor | `node.createActor("id")` | 상태 엔티티 | [03](./03-services.md#actor) |
