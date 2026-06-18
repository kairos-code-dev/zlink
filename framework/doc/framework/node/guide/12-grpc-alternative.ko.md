# gRPC 대안 — ZLink을 어디에 쓰나

이 장은 Node/NestJS 서비스에서 ZLink framework 를 gRPC/HTTP 대신 언제 쓰는지 판단
기준을 정리한다. ZLink 는 범용 RPC 문법 대체가 아니라 zlink core 의 channel, Spot,
STREAM 기능을 NestJS 표면으로 올리는 계층이다.

## 1. ZLink가 맞는 경우

- 서버 간 호출이 많고 endpoint 배선을 application 에서 숨기고 싶다.
- request/send/pub-sub/room/stream 을 같은 runtime 안에서 다루고 싶다.
- game room, stage, session actor 처럼 동적 routing 단위가 필요하다.
- 외부 client STREAM 과 server actor 를 연결해야 한다.
- `.NET`, Java/Kotlin, C++, Node 등 여러 언어가 같은 channel/packet 으로 통신해야 한다.

## 2. gRPC가 더 단순한 경우

- 정적인 service-to-service API 만 필요하다.
- load balancer 와 service discovery 를 이미 표준화했다.
- 동적 Spot, actor/session relay, external stream connector 가 필요 없다.

## 3. 판단 기준

단순 CRUD RPC 만 필요하면 gRPC 가 더 작고 익숙할 수 있다. 동적 room/session routing 과
실시간 상태 서버가 핵심이면 ZLink 가 더 적합하다. 도메인 일관성·영속성은 그대로
application 책임으로 남고, ZLink 는 서비스 간 통신 배선을 줄인다.

## 4. 케이스 스터디 — 도메인별 개별 문서

쉬운 기본형 → 기능이 모두 필요한 강한 사례 → 경계가 분명한 사례 순으로 읽으면 된다.

| 케이스 | 무엇을 보나 | ZLink 핵심 기능 |
|--------|-------------|-----------------|
| [13 전자상거래 체크아웃](case-studies/13-case-ecommerce-checkout.ko.md) | channel messaging 기본형(request/send/pub-sub) | channel + pub/sub |
| [14 내부 마이크로서비스 mesh + 운영](case-studies/14-case-microservice-mesh.ko.md) | service discovery 와 운영·topology | channel  + Registry + monitoring |
| [15 실시간 멀티플레이 게임](case-studies/15-case-realtime-game.ko.md) | STREAM+SPOT+actor 가 모두 필요한 강한 사례 | STREAM + SPOT + actor + session dispatch |
| [16 라이드헤일링 디스패치](case-studies/16-case-ride-hailing.ko.md) | zone 상태와 위치 fan-out | STREAM + pub/sub + zone SPOT |
| [17 채팅·메시징](case-studies/17-case-chat-messaging.ko.md) | room membership 과 presence | STREAM + room SPOT + boundSession |
| [17-1 마켓플레이스 채팅](case-studies/17-1-case-marketplace-chat.ko.md) | 거래·문의 conversation | STREAM + conversation actor/SPOT |
| [17-2 라이브 커머스 채팅](case-studies/17-2-case-live-commerce-chat.ko.md) | live chat, slow mode, moderation | STREAM + stream SPOT |
| [17-3 게임 채팅](case-studies/17-3-case-game-chat.ko.md) | party/guild/match chat scope | STREAM + player actor + room |
| [18 트레이딩 시스템](case-studies/18-case-trading-system.ko.md) | SPOT 모델은 맞지만 HFT 핫패스는 제외되는 경계 사례 | STREAM + symbol SPOT + pub/sub |

각 케이스는 "도메인의 진짜 난제 → 기존 스택 → ZLink 스택 → 코드 비교 → 아키텍처·
메시지 흐름 비교 → 줄어드는 것/그대로 남는 것" 순으로 구성된다. 사용법 정식은
04~10 챕터가 다룬다.

cross-language wire 계약 smoke 기준은 [internals/cross-language-smoke](../internals/cross-language-smoke.ko.md)에 있다.

## 회귀 테스트

가이드 장 존재와 README 링크는 `test/contract/documentation-regression.test.js` 에서 확인한다.
