<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: STREAM 샘플](stream-samples.ko.md) | [다음: TicTacToe Game Sample](tictactoe-game-sample.ko.md)
<!-- framework-adapter-nav:end -->

[Java 묶음](../../README.ko.md) | [SPOT](../../spec/spring-boot-spot.ko.md) | [Actor/Session](../../spec/spring-boot-actor-session.ko.md) | [STREAM](../../spec/spring-boot-stream.ko.md)

# Matching Room Game Sample: Bingo (Java/Kotlin)

> 언어 중립 시나리오 정본은 [공통 샘플 — Bingo](../../../common/sample/bingo/README.ko.md)다.
> 이 문서는 같은 시나리오를 Java/Kotlin(Spring Boot) framework 표면으로 구체화한다.
> 실행 코드는 `samples/java/Bingo`(Java), `samples/kotlin/Bingo`(Kotlin coroutine)에 있다.

## 1. 목적

분리된 `Session`·`Api`·`Play`·`Registry` 서버로 구성한 실시간 매칭 게임이다. session
gateway, actor binding, Entry Spot, room Spot, server timer 기반 진행, bound session
push를 한 흐름으로 보여 준다. payload codec은 cross-language schema가 분명한 Protobuf다.

## 2. 샘플 범위

- `Session`: STREAM 노드로 client 접속을 받고 ActorGateway로 player actor에 bind 한다.
- `Api`: 인증·매칭 요청을 받는 client-server channel handler를 노출한다.
- `Play`: room Spot(`BingoRoomSpot`)과 Entry Spot(`BingoEntrySpot`), player actor,
  draw timer, domain event를 가진 상태 소유 서버다.
- `Registry`: embedded registry로 네 서버가 서로를 Discovery로 발견하게 한다.

## 3. 전체 흐름

1. client는 `Session` STREAM endpoint 하나만 연다.
2. 각 player가 `AuthenticatePlayerReq`로 인증하면 player actor가 생성·bind 된다.
3. `MatchBingoReq`로 매칭을 요청한다. 두 번째 참가 시 room이 running 상태가 되고
   양쪽 bound session에 game-start가 push 된다.
4. 각 player가 3 x 3 `SubmitBingoCardReq`로 카드를 제출한다.
5. room Spot의 draw timer가 숫자를 뽑아 `BingoNumberDrawnNotify`를 push 하고,
   승패가 정해지면 `BingoGameEndedNotify`를 push 한다. client는 draw 요청을 보내지 않는다.

## 4. 역할 분리 (Domain / Application / Adapters)

`Play` 서버는 순수 도메인 규칙(`domain/bingo`: `BingoCard`, `BingoRoomGame`)과 framework
adapter(`adapters/zlink`: actor·handler·spot·notification)를 분리한다. Entry Spot은 접속
actor를 가장 먼저 받아 room Spot으로 옮기기 전 진입 단계를 맡는다.

## 5. 상태 모델

room 상태는 `WaitingForPlayers → Running → Finished`로 전이한다. 첫 참가자는 waiting을
받고, 두 번째 참가자가 running을 만든다. 각 player는 자기 카드와 매칭 상태를 가진다.

## 6. 메시지 계약

요청/응답/알림 계약은 `Shared/contracts/Messages.java`(Kotlin은 동일 contracts)에 named
type으로 둔다: `AuthenticatePlayerReq/Res`, `MatchBingoReq/Res`, `SubmitBingoCardReq/Res`,
`BingoGameStartedNotify`, `BingoNumberDrawnNotify`, `BingoGameEndedNotify`, `PlayerJoinedNotify`.

## 7. 등록 (builder API)

```java
options.codecs().use(ZLinkProtobufCodec.defaultCodec());
var channel = options.addClientServerChannel(SampleNames.ApiChannel);
channel.enableServer(topology.apiChannelEndpoint());
channel.addHandlerGroup(SampleNames.ApiChannel);
var mesh = options.addSpotMesh(SampleNames.SpotMesh);
var node = mesh.addNode(SampleNames.PlayNode);
node.enableRouter(topology.spotEndpoint()).setRouterRoutingId(RoutingId.from(SampleNames.PlayNodeRoutingId));
node.configureEntrySpot().setRoutingId(RoutingId.from(SampleNames.EntrySpotRoutingId));
node.addEntrySpot(BingoEntrySpot.class);
```

Kotlin 샘플은 같은 builder 위에 `ZLinkCoroutine*` base와 suspend handler, `ZLinkKotlinStreamConnector`를
쓴다. 의미는 동일하고 표면만 coroutine이다.

## 8. Client self-check

`BingoClientScenario`는 성공 로그가 아니라 의미 값을 검증한다: 인증 token이 actor id와
일치하는지, 첫 참가자가 waiting·두 번째가 running인지, 자기 join notify는 받지 않는지,
draw/ended notify의 board·winner가 고정 시나리오와 맞는지. push 대기는 stream connector의
public wait API로 한다.

## 9. 완료 기준

- client는 `Session` STREAM 연결 하나만 연다.
- 네 서버가 Registry/Discovery로 자동 발견된다.
- Protobuf codec은 `ZLinkProtobufCodec.defaultCodec()` framework extension으로 등록한다.
- draw는 server timer가 주도하고 client는 draw 요청을 보내지 않는다.
- Java/Kotlin 두 샘플이 같은 역할·메시지·검증 순서를 따른다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: STREAM 샘플](stream-samples.ko.md) | [다음: TicTacToe Game Sample](tictactoe-game-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
