<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: 인터페이스 카탈로그](11-interface-catalog.ko.md) | [다음: Bingo Sample](samples/bingo-game-sample.ko.md)
<!-- framework-adapter-nav:end -->

# Java gRPC Alternative Guide

## 1. ZLink가 맞는 경우

- 서버 간 호출이 많고 endpoint 배선을 application에서 숨기고 싶다.
- request/send/pub-sub/room/stream을 같은 runtime 안에서 다루고 싶다.
- game room, stage, session actor처럼 동적 routing 단위가 필요하다.
- 외부 client STREAM과 server actor를 연결해야 한다.
- `.NET`, Java/Kotlin, Node 등 여러 언어가 같은 channel/packet으로 통신해야 한다.

## 2. gRPC가 더 단순한 경우

- 정적인 service-to-service API만 필요하다.
- load balancer와 service discovery를 이미 표준화했다.
- dynamic Spot, actor/session relay, external stream connector가 필요 없다.

## 3. 판단 기준

ZLink는 gRPC를 대체하는 범용 RPC 문법이 아니라, zlink core의 channel, Spot, STREAM
기능을 application framework 표면으로 올리는 계층이다. 단순 CRUD RPC만 필요하면 gRPC가
더 작고 익숙할 수 있다. 동적 room/session routing이 핵심이면 ZLink가 더 적합하다.

## 4. 정본 sample

실제 업무 흐름은 [공통 sample](../../common/sample/README.ko.md)이 정의한다. Java에서
등록하고 실행하는 방법은 다음 언어별 sample 문서에서 확인한다.

- [Bingo](samples/bingo-game-sample.ko.md)
- [TicTacToe](samples/tictactoe-game-sample.ko.md)
- [SupportChat](samples/supportchat-sample.ko.md)
- [DeliveryDispatch](samples/deliverydispatch-sample.ko.md)
- [ShoppingMall](samples/shoppingmall-sample.ko.md)
- [GameQuest](samples/gamequest-sample.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: 인터페이스 카탈로그](11-interface-catalog.ko.md) | [다음: Bingo Sample](samples/bingo-game-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
