[바인딩 가이드](../README.ko.md) · [코어 가이드](../../01-overview.ko.md)

# Python 바인딩 가이드 (`zlink`)

이 묶음은 **Python에서 zlink를 사용하는 방법**을 기능별로, 실제 샘플 코드 중심으로
설명합니다.

메시징 개념은 [코어 가이드](../../01-overview.ko.md)를 참고하세요.

## 문서 구성

| 문서 | 내용 |
|------|------|
| [01 시작하기](./01-getting-started.ko.md) | 설치, 5분 예제, 핵심 타입, 소유권 규칙 |
| [02 메시징](./02-messaging.ko.md) | 소켓 패턴별 사용법 |
| [03 서비스](./03-services.ko.md) | Registry · Discovery · SpotNode·Spot · Actor |
| [04 운영](./04-operations.ko.md) | 옵션 · TLS · 모니터링 · 폴러/타이머 · 스레딩 |
| [05 레퍼런스](./05-reference.ko.md) | 에러 처리 · 코덱 · C API↔Python 대응표 · 샘플 |

## 기능 지도

| 기능 | Python 진입점 | 한 줄 설명 | 가이드 |
|---|---|---|---|
| 컨텍스트 | `zlink.create_context()` | 런타임 진입점 | [01](./01-getting-started.ko.md) |
| 메시지 | `bytes` 리터럴 | 페이로드 (바이트) | [01](./01-getting-started.ko.md) |
| 수신 | `zlink.create_received()` | 수신 봉투 | [01](./01-getting-started.ko.md) |
| 라우팅 ID | `zlink.RoutingId(b"id")` | 피어 식별 값 | [01](./01-getting-started.ko.md) |
| PAIR | `zlink.create_pair_socket(ctx)` | 1:1 배타적 연결 | [02](./02-messaging.ko.md#pair) |
| DEALER/ROUTER | `create_dealer/router_socket` | 요청/응답 | [02](./02-messaging.ko.md#dealer--router) |
| PUB/SUB | `create_pub/sub_socket` | 토픽 발행/구독 | [02](./02-messaging.ko.md#pub--sub) |
| XPUB/XSUB | `create_xpub/xsub_socket` | 구독 이벤트 | [02](./02-messaging.ko.md#xpub--xsub) |
| STREAM | `create_stream_socket` | 원시 TCP | [02](./02-messaging.ko.md#stream) |
| Registry | `zlink.create_registry(ctx)` | 서비스 카탈로그 | [03](./03-services.ko.md#registry) |
| Discovery | `zlink.create_discovery(...)` | 서비스 발견 | [03](./03-services.ko.md#discovery) |
| SpotNode/Spot | `create_spot_node` / `node.create_spot()` | 메시 노드 | [03](./03-services.ko.md#spotnode--spot) |
| Actor | `node.actor("id")` | 상태 엔티티 | [03](./03-services.ko.md#actor) |
