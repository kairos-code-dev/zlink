[바인딩 가이드](../README.ko.md) · [코어 가이드](../../01-overview.ko.md)

# Rust 바인딩 가이드 (`zlink`)

이 묶음은 **Rust에서 zlink를 사용하는 방법**을 기능별로, 실제 샘플 코드 중심으로
설명합니다.

메시징 개념은 [코어 가이드](../../01-overview.ko.md)를 참고하세요.

## 문서 구성

| 문서 | 내용 |
|------|------|
| [01 시작하기](./01-getting-started.ko.md) | Cargo 추가, 5분 예제, 핵심 타입, 소유권 규칙 |
| [02 메시징](./02-messaging.ko.md) | 소켓 패턴별 사용법 |
| [03 서비스](./03-services.ko.md) | Registry · Discovery · SpotNode·Spot · Actor |
| [04 운영](./04-operations.ko.md) | 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 |
| [05 레퍼런스](./05-reference.ko.md) | 에러 처리 · 코덱 · C API↔Rust 대응표 · 샘플 |

## 기능 지도

| 기능 | Rust 타입 / 진입점 | 한 줄 설명 | 가이드 |
|---|---|---|---|
| 컨텍스트 | `Context::new()` | 런타임 진입점 | [01](./01-getting-started.ko.md) |
| 메시지 | `Message::try_from(b"data")` | 페이로드 프레임 | [01](./01-getting-started.ko.md) |
| 수신 | `Received::empty()` | 수신 봉투 | [01](./01-getting-started.ko.md) |
| 라우팅 ID | `RoutingId::from(b"id")` | 피어 식별 값 | [01](./01-getting-started.ko.md) |
| PAIR | `ctx.pair_socket()` | 1:1 배타적 연결 | [02](./02-messaging.ko.md#pair) |
| DEALER/ROUTER | `ctx.dealer/router_socket()` | 요청/응답 | [02](./02-messaging.ko.md#dealer--router) |
| PUB/SUB | `ctx.pub/sub_socket()` | 토픽 발행/구독 | [02](./02-messaging.ko.md#pub--sub) |
| XPUB/XSUB | `ctx.xpub/xsub_socket()` | 구독 이벤트 | [02](./02-messaging.ko.md#xpub--xsub) |
| STREAM | `ctx.stream_socket()` | 원시 TCP | [02](./02-messaging.ko.md#stream) |
| Registry | `Registry::new(&ctx)` | 서비스 카탈로그 | [03](./03-services.ko.md#registry) |
| Discovery | `Discovery::new(&ctx, ...)` | 서비스 발견 | [03](./03-services.ko.md#discovery) |
| SpotNode/Spot | `SpotNode::new(&ctx)` | 메시 노드 | [03](./03-services.ko.md#spotnode--spot) |
| Actor | `node.create_actor("id")` | 멀티파트 액터 | [03](./03-services.ko.md#actor) |
