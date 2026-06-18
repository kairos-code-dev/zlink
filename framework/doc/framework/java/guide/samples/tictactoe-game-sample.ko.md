<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: Bingo Game Sample](bingo-game-sample.ko.md) | [다음: SupportChat Sample](supportchat-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Java 묶음](../../README.ko.md) | [SPOT](../../spec/spring-boot-spot.ko.md) | [Actor/Session](../../spec/spring-boot-actor-session.ko.md) | [STREAM](../../spec/spring-boot-stream.ko.md)

# Smallest Realtime Game Sample: TicTacToe (Java/Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — TicTacToe](../../../common/sample/tictactoe/README.ko.md)다.
> 실행 코드는 `samples/java/TicTacToe`(Java), `samples/kotlin/TicTacToe`(Kotlin)에 있다.

## 1. 목적

별도 `Session` 서버 없이 `Api`와 `Play` 두 서버만으로 구성한 가장 작은 실시간 게임이다.
`Play`가 stream session을 함께 소유하고, 서버는 수동 endpoint로 직접 연결한다(Registry
자동 발견을 쓰지 않음). Bingo와 달리 분리 gateway·자동 discovery 흐름을 반복하지 않는다.

## 2. 샘플 범위

- `Api`: 인증과 game 생성 요청을 받는 client-server channel 서버.
- `Play`: stream 노드 + ActorGateway + room Spot(`TicTacToeGame`) + Entry Spot을 한
  서버에 둔다. `PlaySession`은 framework typed session packet dispatcher로 인증을
  처리하고, 이후 packet은 actor gateway가 bound actor로 relay 한다.
- 수동 endpoint 연결: `useManualConnections().connect(endpoint)` / `enableClient(endpoint)`.

## 3. 전체 흐름

1. client가 `Play` stream에 접속해 `AuthenticateReq`로 player actor에 bind 한다.
2. `Api` channel로 game 생성/매칭을 요청한다.
3. 두 player가 같은 room Spot에 join 하면 game이 시작된다.
4. `Mark` 제출 → board·turn 갱신 → `MoveNotify`/`GameEndedNotify` push.
5. client는 board, turn, winner 같은 의미 값을 검증한다.

## 4. 상태 모델

room은 빈 board에서 시작해 두 player가 번갈아 mark 한다. 승패 또는 draw가 정해지면
ended로 전이한다. `RoomId`는 명시적 식별자이며 room Spot routing id는 routing id 생성
API로 `RoomId` 문자열에서 만든다(임의 hex가 아님).

## 5. 메시지 계약

`Shared/contracts`에 `AuthenticateReq/Res`, `CreateGameReq/Res`, `MarkReq/Res`,
`MoveNotify`, `GameStartedNotify`, `GameEndedNotify`를 named type으로 둔다.

## 6. 등록 (builder API) · codec

```java
options.codecs().addMessagePack();
var play = options.addClientServerChannel(SampleNames.PlayChannel);
play.enableServer(settings.playChannelEndpoint());
var stream = options.addStreamNode(SampleNames.ClientStream);
stream.attachActorGateway(SampleNames.PlaySpot).bind(settings.playEndpoint())
      .registerSession(PlaySession.class)
      .addSessionPacketHandler(AuthenticatePlaySessionHandler.class);
```

Java/Kotlin TicTacToe 샘플은 MessagePack payload(`ZLinkStreamMessagePack`)를 쓴다. (공통
샘플 문서는 TicTacToe를 JSON으로 기술하며, 이 codec 표면 차이는 최종 정합 검토 대상이다.)

## 7. Client self-check

`TicTacToeClientScenario`는 game start·move·ended notify의 board·turn·winner를 의미 값으로
확인한다. push 대기는 stream connector의 public wait API(`ZLinkStreamMessagePack.codec()`
기반)를 직접 쓴다.

## 8. 완료 기준

- `Api`·`Play` 두 서버만으로 동작하고 별도 Session 서버가 없다.
- 수동 endpoint로 직접 연결한다(Registry/Discovery 미사용).
- `PlaySession`이 framework typed session dispatcher를 쓴다(수동 payload 역직렬화 없음).
- Java/Kotlin 두 샘플이 같은 역할·메시지·검증 순서를 따른다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: Bingo Game Sample](bingo-game-sample.ko.md) | [다음: SupportChat Sample](supportchat-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
