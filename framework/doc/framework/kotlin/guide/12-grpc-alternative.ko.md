<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: 인터페이스 카탈로그](11-interface-catalog.ko.md) | [다음: 케이스 — 전자상거래 체크아웃](case-studies/13-case-ecommerce-checkout.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin gRPC Alternative Guide

## 1. ZLink가 맞는 경우

- 서버 간 호출이 많고 endpoint 배선을 application에서 숨기고 싶다.
- request/send/pub-sub/room/stream을 같은 runtime 안에서 다루고 싶다.
- game room, stage, session actor처럼 동적 routing 단위가 필요하다.
- 외부 client STREAM과 server actor를 연결해야 한다.
- `.NET`, Java/Kotlin, Node 등 여러 언어가 같은 channel/packet으로 통신해야 한다.
- 모든 호출을 `suspend` 함수로 합성하고 싶다(Spring WebFlux/coroutine 스택과 자연스럽게 결합).

## 2. gRPC가 더 단순한 경우

- 정적인 service-to-service API만 필요하다.
- load balancer와 service discovery를 이미 표준화했다.
- dynamic Spot, actor/session relay, external stream connector가 필요 없다.

## 3. 판단 기준

ZLink는 gRPC를 대체하는 범용 RPC 문법이 아니라, zlink core의 channel, Spot, STREAM
기능을 application framework 표면으로 올리는 계층이다. 단순 CRUD RPC만 필요하면 gRPC가
더 작고 익숙할 수 있다. 동적 room/session routing이 핵심이면 ZLink가 더 적합하다.

## 4. 케이스 스터디 — 도메인별 개별 문서

아래 케이스는 실행 가능한 전체 예제가 아니라 **도입 판단과 아키텍처 매핑**을 위한
산문이다. 각 케이스는 "도메인의 진짜 난제 → 기존 스택 → ZLink 스택 → 코드 비교 →
아키텍처·운영" 순서로 읽는다. 사용법 정식은 04~09 챕터가, 도입 판단은 이 문서와 각
케이스 스터디가 다룬다.

| 케이스 | 무엇을 보나 | ZLink 핵심 기능 |
|--------|-------------|-----------------|
| [13 전자상거래 체크아웃](case-studies/13-case-ecommerce-checkout.ko.md) | channel messaging 기본형(request/send/pub-sub) | channel + pub/sub |
| [14 내부 마이크로서비스 mesh + 운영](case-studies/14-case-microservice-mesh.ko.md) | service discovery 와 운영·topology | channel + Registry + monitoring |
| [15 실시간 멀티플레이 게임](case-studies/15-case-realtime-game.ko.md) | STREAM+SPOT+actor 가 모두 필요한 강한 사례 | STREAM + SPOT + actor + session dispatch |
| [16 라이드헤일링 디스패치](case-studies/16-case-ride-hailing.ko.md) | zone 상태와 위치 fan-out | STREAM + pub/sub + zone SPOT |
| [17 채팅·메시징](case-studies/17-case-chat-messaging.ko.md) | room membership 과 presence | STREAM + room SPOT + bound session |
| [17-1 마켓플레이스 채팅](case-studies/17-1-case-marketplace-chat.ko.md) | 거래·문의 conversation | STREAM + conversation actor/SPOT |
| [17-2 라이브 커머스 채팅](case-studies/17-2-case-live-commerce-chat.ko.md) | live chat, slow mode, moderation | STREAM + stream SPOT |
| [17-3 게임 채팅](case-studies/17-3-case-game-chat.ko.md) | party/guild/match chat scope | STREAM + player actor + room |
| [18 트레이딩 시스템](case-studies/18-case-trading-system.ko.md) | SPOT 모델은 맞지만 HFT 핫패스는 제외되는 경계 사례 | STREAM + symbol SPOT + pub/sub |

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: 인터페이스 카탈로그](11-interface-catalog.ko.md) | [다음: 케이스 — 전자상거래 체크아웃](case-studies/13-case-ecommerce-checkout.ko.md)
<!-- framework-adapter-nav:bottom:end -->
