# 샘플 codec 메시지 표면 정렬 계획

> 상태: **초안**. 작성 2026-06-16.
> 목적: `cpp`, `java`, `kotlin`, `node`, `dotnet` framework 샘플에서 JSON,
> MessagePack, Protobuf codec 을 쓰더라도 업무 코드의 호출 표면은 동일하게 유지한다.

이 문서는 샘플 코드만 고치는 작업 계획이 아니다. 계획 작성 당시 샘플은 일부 경계에서
`ToJson`, `ToProto`, `ZLinkStreamProtobuf.decode`, `createProtobufMessage` 같은
codec 별 helper 를 직접 호출한다. 이 계획의 목표는 codec 선택을 framework 등록과
runtime 메시지 변환 계층으로 내리고, 샘플 업무 코드는 각 언어에 이미 공개된 표준
메시지 생성 표면과 typed submit 을 사용하게 정렬하는 것이다. `packetName` 계열 fluent
builder 는 타입이나 metadata 로 packet name 을 확정할 수 없을 때만 쓰는 override 로
둔다.

이 작업은 2026-06-16 재검토에서 breaking change 로 전환됐다. 기존 framework messaging
API가 domain object나 generated object를 직접 받는 표면은 유지하지 않는다. application이
직접 호출하는 `send/request/reply/join` 계열 API는 `Message`를 받는 표면으로 정렬하고,
object 직렬화와 packet name 확정은 `Message.from(...)`, `Message.From(...)`,
`message_t::from(...)` 또는 언어별 codec extension의 같은 의미 factory에서 끝낸다.

필요한 수정은 bindings codec extension, framework public API, runtime 연결, 샘플 사용
방식에 걸친다. 호환성 유지를 위한 object-typed overload, adapter, helper는 남기지 않는다.

## 1. 목표

샘플 업무 코드에서 codec 종류를 드러내지 않는다.

대표 목표 모양은 아래와 같다. 아래 예시는 언어에 이미 공개된 표준 표면이 있는 경우의
목표 모양이다. 같은 의미의 typed 호출이 이미 공개되어 있으면 그 표면을 유지하고, 이
작업 때문에 새 public overload 나 signature 를 추가하지 않는다.

### 1.1 Node.js

```ts
const response = await zlinkClient
  .requestToChannel(SampleNames.apiChannel, Message.from(request))
  .timeout(SampleTimings.requestTimeout)
  .submit<AuthenticatePlayerRes>();
```

`packetName(...)`은 `Message.from(request)`가 packet name 을 담을 수 없거나, runtime
type/metadata 에서 얻은 이름과 실제 packet name 이 다를 때만 override 로 사용한다.

### 1.2 .NET

```csharp
var response = await channels.RequestToChannel(
        SampleNames.ApiChannel,
        Message.From(request))
    .Async<AuthenticatePlayerRes>(cancellationToken);
```

`.NET`에 이미 `RequestToChannel(channel, request).Async<T>()` 같은 표준 typed 호출이
공개되어 있다면 그 표면은 유지한다. 단, 이 작업 때문에 새 overload 를 만들거나 기존
public signature 를 바꾸지 않는다. raw `Message` 경계에서 `request.FromJson<T>()`,
`request.FromProto<T>()`, `reply.ToJson()`, `reply.ToProto()`가 샘플 업무 코드에
직접 나오지 않아야 한다.

### 1.3 Java/Kotlin

```java
Messages.AuthenticatePlayerRes response = client
    .requestToChannel(SampleNames.ApiChannel, Message.from(request))
    .timeout(SampleTimings.RequestTimeout)
    .await(Messages.AuthenticatePlayerRes.class);
```

Java/Kotlin도 `requestToChannel(channel, Message.from(request)).await(Type)` 형태가
표준이다. 기존에 `requestToChannel(channel, request).await(Type)`처럼 object를 직접 받는
표면이 있으면 제거한다. 중요한 점은 sample session, Spot, actor join 코드에서
`ZLinkStreamJson.decode`, `ZLinkStreamProtobuf.decode`, `ObjectMapper.readValue`,
`json.writeValueAsBytes`가 직접 보이지 않게 하는 것이다.

### 1.4 C++

```cpp
auto response = co_await client
  .request (sample_names_t::api_channel,
            zlink::message_t::from (request))
  .async<authenticate_player_res_t> ();
```

C++도 `.request(channel, message_t::from(dto)).async<T>()` 형태가 표준이다. 기존
`.request(channel, dto).async<T>()`처럼 object를 직접 받는 표면이 있으면 제거한다. 단,
`to_stream_payload`, `from_stream_payload`, `json_to_protobuf_payload` 같은 helper 가
Spot, stream session, actor join 업무 코드에 직접 남으면 안 된다.

## 2. 비목표

- JSON, MessagePack, Protobuf 별로 별도 channel/client API 를 늘리지 않는다.
- 샘플마다 `createProtobufMessage`, `decodeJsonReply`, `decodeMessagePackReply` 같은
  helper 를 새로 만들지 않는다.
- messaging API 에 object payload 를 직접 받는 public overload 를 남기지 않는다.
- 호환성 유지를 위한 legacy overload 를 추가하지 않는다.
- Protobuf 샘플에서 POJO/record/data class 와 generated proto type 을 전송 계약으로
  동시에 쓰지 않는다.
- codec 구현을 core binding socket API 에 직접 섞지 않는다.
- 샘플을 통과시키기 위해 packet name 을 임의 규칙으로 숨기지 않는다. packet name 은
  `Message` metadata, generated type metadata, handler/request 등록부, 또는 명시적
  `packetName(...)` override 중 하나에서 명확히 드러나야 한다.

## 3. 공통 원칙

### 3.1 업무 코드는 payload 의미만 다룬다

handler 와 client scenario 는 request/reply 타입과 packet name 만 다룬다. codec 선택,
바이트 변환, reply decode 는 framework 등록 정보와 runtime 메시지 변환 계층의 책임이다.

### 3.2 표준 호출 인터페이스만 사용한다

이번 작업에서 샘플이 사용할 수 있는 표준 호출 인터페이스는 아래 범위로 제한한다.

- payload 생성: 이미 공개된 `Message.from(...)`, `Message.From(...)`, `message_t::from(...)`
  또는 언어별 codec extension의 같은 의미 factory
- packet name 지정: `packetName(...)`, `PacketName(...)`, `packet_name(...)` 또는
  이미 공개되어 있는 handler/request metadata
- 실행: `submit<T>()`, `Async<T>()`, `await(Type)`, `async<T>()`
- codec 등록: 기존 `AddJson/AddProtobuf/AddMessagePack`, `addJson/addProtobuf/addMessagePack`,
  `add_json/add_protobuf/add_messagepack` 계열

이 목록 밖의 public interface 를 추가하지 않는다. object-typed messaging overload 는
기존 표면이어도 제거한다. 구현 중 `Message` factory 가 부족하면 messaging API에 object
overload를 되살리지 말고, bindings codec extension 또는 `Message` factory 쪽으로 책임을
옮긴다.

### 3.3 기존 메시지 생성 표면은 codec 독립 진입점이다

각 언어의 `Message.from(...)`, `Message.From(...)`, `message_t::from(...)`는 이미 공개된
경우에만 codec 별 helper 의 대체 표면으로 사용한다. 이 factory 가 직접 객체를 받지
못하는 언어는 새 public overload 를 추가하지 않는다. 이미 공개된 표준 typed 호출이 있으면
그 내부가 동일한 codec registry 경로를 타게 만든다. 그런 표면도 없으면 raw bytes 를 만드는
helper 를 샘플에 추가하지 말고, 해당 언어의 표준 메시지 factory 범위를 별도 이슈로 분리한다.

이 factory 가 object payload 를 받는 언어에서는 호출 시점에 즉시 직렬화해야 한다.
`Message`는 원본 객체를 보관하지 않는다. C API 의 `msg_t`처럼 직렬화된 byte payload 와
runtime 이 전송에 필요한 metadata 만 가진다. packet name 도 이 시점에 runtime type,
generated type metadata, codec extension metadata, 또는 framework 등록부에서 확정할 수
있으면 `Message` metadata 로 함께 들어간다.

plain object 처럼 runtime type/metadata 가 없어서 packet name 을 확정할 수 없거나,
class/generated type 이름과 실제 packet name 이 다르면 호출부는 기존 fluent builder 의
`packetName(...)`, `PacketName(...)`, `packet_name(...)`을 override 로 사용한다. 반대로
type/metadata 에서 packet name 이 확정되는 샘플 호출부는 override 를 반복하지 않는다.

### 3.4 bindings codec extension 도 같은 의미를 제공한다

객체 payload 직렬화는 core `Message` 자체가 아니라 언어별 bindings codec extension 이
제공할 수 있다. 따라서 public surface 조사는 framework adapter 만 보지 않고 아래 7개
bindings 계층을 함께 확인한다.

- `bindings/cpp/codecs`
- `bindings/dotnet/codecs`
- `bindings/go/codec`
- `bindings/java/codec`
- `bindings/node/packages` 와 Node codec 관련 package
- `bindings/python/codecs`
- `bindings/rust/crates`

각 언어별 결정 규칙은 같다.

- 이미 공개된 Message factory 또는 codec extension factory 가 object payload 를 받으면,
  그 경로에서 즉시 직렬화하고 packet metadata 를 채운다.
- 이미 공개된 framework typed 호출이 있으면, 그 내부가 같은 codec registry/extension 을
  타게 만든다.
- core binding `Message`가 bytes-only 로 공개되어 있고 extension 표면도 없으면, 샘플을
  위해 임의 overload 를 추가하지 않는다. 필요한 public contract 는 별도 설계 이슈로 남긴다.
- 어떤 경우에도 샘플 업무 코드가 codec 별 helper 를 직접 호출해서 raw bytes 를 만들거나
  raw reply 를 다시 decode 하지 않는다.

### 3.5 raw `Message` 경계도 표준 인터페이스 안에서 처리한다

Spot create, Spot actor join, stream session packet handler 처럼 현재 raw `Message`를
받는 경계가 있다. 이 경계에서 샘플 코드가 직접 decode 하지 않도록 아래 중 하나로
정리한다.

- 기존 public callback/handler signature 는 유지한다.
- codec registry 기반 decode/encode 는 runtime 내부 또는 이미 존재하는 표준 handler
  dispatch 경로에 둔다.
- 샘플 업무 코드에는 `Json`, `Proto`, `MessagePack` 이름이 들어간 decode/encode 호출을
  남기지 않는다.
- 이를 위해 새 public callback/handler overload 나 `Message.decode<T>(...)` 같은 새 표면을
  추가하지 않는다.

### 3.6 codec 등록이 전송 계약의 단일 기준이다

등록부는 packet name 과 request/reply 타입, codec 종류를 함께 알고 있어야 한다.

예시:

```text
AuthenticatePlayerReq -> AuthenticatePlayerRes -> Protobuf
CreateGameReq         -> CreateGameRes         -> JSON
```

호출부는 이 등록 정보를 반복하지 않는다.

### 3.7 Protobuf 샘플은 generated type 을 전송 계약으로 사용한다

Bingo는 Protobuf 샘플이다. 전송 경계의 request/reply 타입은 `.proto`에서 나온 generated
type 이어야 한다. C++에서 generator 도입이 이번 단계에 포함될 수 있는지 먼저 결정한다.
도입한다면 generated type 을 전송 계약으로 사용하고, 도입하지 못한다면 C++ Bingo Protobuf
정리는 별도 하위 계획으로 분리한다. 어떤 경우에도 JSON으로 감싼 Protobuf 흉내나 수동
schema table 은 완료 상태로 인정하지 않는다.

## 4. 현재 조사 결과

### 4.1 Node.js

Node.js Bingo는 가장 크게 어긋나 있다.

수정 대상:

- `framework/languages/node/samples/Bingo.Ts/Shared/Contracts/protobuf-codec.ts`
- `framework/languages/node/samples/Bingo.Ts/Shared/Contracts/channel-codec.ts`
- `framework/languages/node/samples/Bingo.Ts/Shared/Contracts/messages.ts`
- `framework/languages/node/samples/Bingo.Ts/Shared/Contracts/bingo_messages.proto`
- `framework/languages/node/samples/Bingo.Ts/Client/main.ts`
- `framework/languages/node/samples/Bingo.Ts/Client/bingo-client-scenario.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Api/Handlers/authenticate-player-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Api/Handlers/match-bingo-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Application/RoomAllocation/bingo-room-allocator.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Handlers/allocate-bingo-room-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Handlers/ensure-player-actor-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Handlers/match-bingo-channel-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Handlers/submit-bingo-card-channel-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Handlers/bingo-notifications-handler.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Spots/bingo-entry-spot.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Play/Adapters/ZLink/Spots/bingo-room-spot.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Session/main.ts`
- `framework/languages/node/samples/Bingo.Ts/Server/Session/Sessions/Handlers/authenticate-session-handler.ts`
- `framework/languages/node/samples/TicTacToe.Ts/Server/Play/Adapters/ZLink/Sessions/play-session.ts`
- `framework/languages/node/samples/TicTacToe.Ts/Server/Play/Adapters/ZLink/Spots/play-entry-spot.ts`
- `framework/languages/node/samples/TicTacToe.Ts/Server/Play/Adapters/ZLink/Spots/tictactoe-game-spot.ts`

현재 문제:

- 2026-06-16 재확인: Node Bingo 샘플 업무 코드에서 `createProtobufMessage(...)`,
  `readProtobufMessage(...)`, `decodeBingoChannelReply(...)`, `bingoChannelHandlerOptions()`,
  `submit<Buffer>().then(decode...)` 패턴은 제거됐다.
- Spot create/join 호출부는 현재 `Message.from(bingoPayload(...))` 표준 메시지 생성 표면을
  사용한다. `Message.from(...)`은 object payload 의 `toBytes()`를 호출해 즉시 bytes 로
  직렬화한다.
- raw Spot create/join 수신부는 아직 `request.value<T>()`에 의존한다. 이는 현재 public
  lifecycle signature 가 raw `Message`라 request/reply type metadata 를 registry 에서 받을
  표면이 없기 때문에 남은 중간 상태다. 최종 typed lifecycle metadata 설계 없이 완료로 보지
  않는다.
- `protobuf-codec.ts`는 현재 `protobufjs.loadSync(bingo_messages.proto)`와 `lookupType(...)`을
  사용한다. 수동 varint encoder/schema table 방식은 남아 있지 않다. 다만 static generated
  TypeScript type 이 아니라 reflection 기반 plain object 변환과 packet/type 추론 map 을
  사용하므로, generated proto type 전송 계약 완료로 보지는 않는다.
- TicTacToe Spot/session 경계에서 `JSON.parse`, `JSON.stringify`, `BindingMessage.from(Buffer...)`가 직접 보인다.
- Node core binding 의 `Message.from(...)`는 현재 `BufferLike | ObjectMessagePayload | Message`
  를 받는다. object payload 는 `toBytes()`, `serializeBinary()`, `data()` 순서로 즉시 bytes 로
  직렬화되고, 그런 메서드가 없으면 JSON 문자열로 직렬화된다. 이 표면은 새로 늘리지 않고
  기존 공개 factory 로 사용한다.
- Node framework 의 `DefaultZLinkCodecRegistryBuilder`와 registration serializer map 은
  channel envelope encode/decode 경로에 연결되어 있다. `requestToChannel(..., dto).submit<T>()`
  는 serializer 가 등록되어 있으면 request 와 reply 를 같은 registry 경로로 변환한다.
- 2026-06-16 현재 `RegistrationCodecRegistryBuilder.addProtobuf()`와
  `DefaultZLinkCodecRegistryBuilder.addProtobuf()`는 기본 serializer 를 등록한다. 그러나 이
  serializer 는 packet 별 `.proto` generated type 을 고르는 serializer 가 아니라, 임의
  JavaScript 값을 자체 Protobuf wire 모양으로 감싸는 generic serializer 이다. 따라서
  `.codecs().addProtobuf()` 호출만 남았다는 사실은 sample-specific `addSerializer(...)`
  반복 등록을 제거했다는 증거는 되지만, Bingo가 generated `.proto` 전송 계약을 완성했다는
  증거는 아니다.
- 2026-06-16 현재 Node bindings 의 `@zlink-systems/zlink-codec-protobuf.encode(value, type)`와
  stream connector 의 `createZlinkStreamProtobufCodec(type)`는 모두 protobufjs generated/static
  type 또는 reflection `Type`을 인자로 받아야 한다. 반면 framework/NestJS 의 기존 public
  `addProtobuf()`는 인자가 없고, packet name 별 request/reply protobuf type map 을 넘길 표면도
  없다. 따라서 `addProtobuf()` 안에서 임의로 serializer 를 만들면 어떤 packet 을 어떤
  generated type 으로 encode/decode 할지 알 수 없다. 이 문제는 helper 이름을 숨기는 방식이
  아니라 packet registration 과 generated type metadata 를 연결하는 설계로 풀어야 한다.
- 2026-06-16 현재 Node framework/NestJS handler registration 은 packet name 만 보관한다.
  TypeScript generic type 은 JavaScript runtime 에 남지 않고, `@zlinkRequestHandler(...)`
  decorator 도 request/reply runtime type 을 등록하지 않는다. 따라서 packet name 과
  request/reply type 등록을 codec 선택의 단일 기준으로 삼으려면 registration metadata 를
  보강하는 설계가 먼저 필요하다. 이 작업에서 새 public overload 나 signature 를 임의로
  추가하지 않는다.
- 2026-06-16 현재 Node Bingo에서 `addSerializer(...)`를 샘플 모듈에 반복 등록하는 방식은
  제거된 상태다. 이 형태는 contract gate 와 최종 표준에 맞지 않으므로 다시 도입하지 않는다.
  남은 문제는 `addProtobuf()` 또는 packet registration 이 generated Protobuf type metadata 를
  어떻게 찾을지이다.
- NestJS handler decorator 에는 `decodePayload` / `encodeResult` 옵션이 있지만, Bingo에서
  이를 handler 별 helper 로 반복하는 방식은 샘플 업무 코드에 codec helper 를 남기는
  문제를 해결하지 못한다.
- `ZLinkSpot.onCreate(request: Message)`, `ZLinkSpot.onActorJoin(actor, request: Message)`,
  `ZLinkActorContext.joinSpot(..., request?: Message)`, `ZLinkSessionPacketHandler.handle(...,
  payload: Message)`는 현재 공개 signature 가 raw `Message`이다. 이 signature 를 바꾸지
  않는 조건에서는 lifecycle handler 자체를 typed request/reply handler 로 바꾸는 작업을
  임의로 구현하지 않는다. 기존 actor packet handler registry 로 옮길 수 있는 업무 packet 은
  옮기고, Spot create/join 과 stream session packet 의 typed lifecycle 표면은 별도 설계
  이슈로 분리한다.
- 2026-06-16 현재 Node actor join public surface 도 raw `Message` 기반이다.
  `ZLinkActorContext.joinSpot(spotRid, request?: Message)`와
  `ZLinkActorJoinSpotCall.submit(): Promise<ZLinkActorJoinResult>`만 공개되어 있고,
  `joinSpot(..., object).submit<TReply>()` 표면은 없다. 따라서 Bingo `BingoEntrySpot.matchActor`
  는 `Message.from(bingoRoomJoinReq(...))`를 사용하지만, join reply 는 아직 raw
  `ZLinkActorJoinResult.reply`와 `request.value<T>()` 기반 중간 상태다. Java/Kotlin처럼 typed
  actor join call 로 정리하려면 내부 registration metadata 설계가 필요하다.
- 2026-06-16 현재 TicTacToe의 channel request 중 request runtime type 에서 packet name 을
  확정할 수 있는 호출은 fluent `packetName(...)` 없이 동작하도록 정리했다. 예를 들어
  play session 의 API 인증 요청은 `AuthenticatePlayerReq` runtime type 을 사용하고, HTTP
  create-game relay 는 runtime type 이름이 `CreateGame`인 request object 를 사용한다.
- 2026-06-16 현재 Node Bingo의 channel request 호출부는 `submit<Buffer>().then(decode...)`
  패턴 없이 typed reply 를 직접 반환한다. `MatchBingoReq`, `SubmitBingoCardReq`,
  `BingoNotificationsReq`는 session relay 가 붙이는 `actorId` / `displayName`을 Protobuf
  전송 계약에 포함하도록 정리했다. 이 필드를 proto 에 넣지 않으면 serializer 가 plain object 를
  `MatchBingoReq` 등으로 직렬화할 때 actor identity 를 버린다.
- 2026-06-16 현재 Node Bingo timer 는 `@zlinkSpotTimerHandler({ spot, name, periodMs,
  options })` metadata 로 자동 등록한다. Spot 업무 코드에서 `context.addTimer(...)`로 직접
  등록하지 않는다.
- 2026-06-16 현재 Node Spot create/join public surface 는 raw `Message`만 받는다. 따라서
  `BingoRoomAllocator`, `BingoEntrySpot`, `BingoRoomSpot`은 `Message.from(bingoPayload(...))`와
  `request.value<T>()`로 raw lifecycle 을 통과한다. helper 이름은 제거됐지만, generated type
  metadata 와 codec registry 가 lifecycle 경계의 단일 기준이 된 상태는 아니므로 typed Spot
  lifecycle metadata 설계 이슈로 분리한다.
- timer handler 등록도 샘플의 공개 사용 표면이다. Bingo처럼 특정 Spot에 고정된 draw timer는
  handler metadata 로 자동 등록하는 방식을 우선한다. 다만 언어별 timer annotation/attribute 가
  `name`과 `period`만 담고 overrun 정책이나 예외 후 정지 정책을 담지 못하면, public surface 를
  새로 늘리지 않는 조건에서 기존 수동 `addTimer(...)`와 완전히 같은 의미로 바꿀 수 없다.
  이 경우에는 샘플에서 조용히 옵션을 잃지 말고 timer metadata 확장 여부를 별도 설계 이슈로
  분리한다.

수정 방향:

- `createProtobufMessage`, `createProtobufReplyMessage`, `decodeBingoChannelReply`,
  `bingoChannelHandlerOptions`는 샘플 업무 코드에서 제거했다. `toBingoProto` /
  `fromBingoProto`는 현재 `Shared/Contracts/protobuf-codec.ts`와 raw session bridge 에만 남아
  있으며, generated Protobuf type 과 typed stream/session metadata 설계 전까지 완료로 보지
  않는다.
- Node framework runtime 에 packet name 기반 codec registry 를 실제 request/reply 경로에 연결한다.
- Node core `Message.from(object)` 표면은 현재 source/dist 에 이미 존재한다. 이번 작업에서 새
  overload/signature 를 추가하지 않고, 샘플은 이 기존 factory 와 existing typed call 만 쓴다.
- framework typed 호출이 object payload 를 받으면 runtime 내부에서 즉시 직렬화한다.
  `Message`에는 byte payload 와 packet metadata 만 남긴다.
- `.codecs().addJson()`, `.codecs().addMessagePack()`, `.codecs().addProtobuf()`는 codec 이름만
  기록하는 no-op 이면 안 된다. 기존 public method 이름은 유지하되, 내부에서 해당 codec 이
  실제 channel/stream/Spot 변환 경로에 연결되도록 만든다. codec 이 message type 을 알아야
  하는 경우에는 packet name 과 request/reply type 등록부를 사용한다. 현재 Node의
  `addProtobuf()` 기본 serializer 는 generic serializer 이므로 Bingo generated Protobuf
  계약을 완료한 것으로 보지 않는다. 최종 표준에서는 샘플 업무 코드나 샘플 모듈마다
  codec helper 를 반복시키지 않는다.
- class/generated type metadata 에서 packet name 을 알 수 있는 샘플 호출부는 fluent
  `packetName(...)`을 반복하지 않는다.
- Node의 `packetName(...)`은 plain object 처럼 packet name 을 알 수 없거나 실제 packet name
  이 type metadata 와 다른 경우의 override 로만 쓴다.
- `submit<Buffer>()`는 샘플 업무 코드에서 제거한다. raw bytes 를 다루는 테스트나 codec
  구현이 아닌 이상, channel/stream request 호출부는 `submit<ReplyType>()`로 끝나야 한다.
- stream connector 에서도 `client.request(request).submit<T>()`는 connector codec 을 쓰되,
  session server 샘플은 frame/header 를 직접 decode 하지 않도록 framework stream hosting 경로로 이동한다.
- Bingo Protobuf는 `.proto` generated type 을 사용한다. generated output 위치를
  `Shared/Contracts/generated/` 같은 한 곳으로 정하고 import 를 그쪽으로 통일한다.
- 2026-06-16 현재 Node framework workspace 에는 `protobufjs` CLI 가 설치되어 있지 않다.
  런타임 로더는 `bindings/node/node_modules/protobufjs` fallback 으로 reflection runtime 을
  사용한다. 따라서 static generated TS 산출물을 만들려면 generator dependency, 생성 위치,
  build 포함 방식을 별도 하위 계획으로 정해야 한다. 그 전에는 reflection 기반 Protobuf
  encode/decode 가 동작하더라도 generated proto type 전송 계약 완료로 보지 않는다.
- TicTacToe JSON은 POJO/interface DTO를 유지하되, JSON encode/decode helper 를 Spot/session
  업무 코드에서 제거하고 framework runtime 내부 변환 경로로 이동한다.

필요한 framework/runtime 수정 후보:

- `framework/languages/node/packages/framework/src/contracts/Channels/Calls.ts`
- `framework/languages/node/packages/framework/src/runtime/channels/index.ts`
- `framework/languages/node/packages/framework/src/runtime/codecs/index.ts`
- `framework/languages/node/packages/framework/src/runtime/spots/index.ts`
- `framework/languages/node/packages/framework/src/runtime/streams/index.ts`
- `framework/languages/node/packages/nestjs/src` 아래 handler decorator/metadata 처리
- `framework/languages/node/packages/stream-connector-protobuf/src/index.ts`
- `framework/languages/node/packages/stream-connector-json/src/index.ts`
- `framework/languages/node/packages/stream-connector-msgpack/src/index.ts`

### 4.2 .NET

`.NET`은 channel/connector 호출 표면은 비교적 정리되어 있지만 raw `Message` 경계가
codec helper 를 직접 호출한다.

수정 대상:

- `framework/languages/dotnet/samples/Bingo/Server/Session/Sessions/Handlers/AuthenticateSessionHandler.cs`
- `framework/languages/dotnet/samples/Bingo/Server/Api/Handlers/MatchBingoHandler.cs`
- `framework/languages/dotnet/samples/Bingo/Server/Play/Application/RoomAllocation/BingoRoomAllocator.cs`
- `framework/languages/dotnet/samples/Bingo/Server/Play/Adapters/ZLink/Spots/BingoRoom.cs`
- `framework/languages/dotnet/samples/Bingo/Server/Play/Adapters/ZLink/Spots/Handlers/MatchBingoActorHandler.cs`
- `framework/languages/dotnet/samples/TicTacToe/Server/Play/Adapters/ZLink/Sessions/PlaySession.cs`
- `framework/languages/dotnet/samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/TicTacToeGame.cs`
- `framework/languages/dotnet/samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/Handlers/PlayActorJoinGameHandler.cs`
- JSON 샘플 raw Spot 경계:
  - `framework/languages/dotnet/samples/DeliveryDispatch/Server/Tracking/DeliveryTrackingSpot.cs`
  - `framework/languages/dotnet/samples/DeliveryDispatch/Server/Tracking/Handlers.cs`
  - `framework/languages/dotnet/samples/GameQuest/Server/QuestMission/Adapters/ZLink/Spots/PlayerQuestSpot.cs`
  - `framework/languages/dotnet/samples/GameQuest/Server/QuestMission/Application/QuestEventProcessor.cs`
  - `framework/languages/dotnet/samples/ShoppingMall/Server/OrderWorkflow/Adapters/ZLink/Handlers/OrderWorkflowRouteHandlers.cs`
  - `framework/languages/dotnet/samples/SupportChat/Server/Support/Application/ConversationAssignment/SupportConversationAllocator.cs`
  - `framework/languages/dotnet/samples/SupportChat/Server/Support/Adapters/ZLink/Spots/ConversationSpot.cs`
  - `framework/languages/dotnet/samples/SupportChat/Server/Support/Adapters/ZLink/Spots/Handlers/OpenConversationActorHandler.cs`

현재 문제:

- Bingo raw 경계에서 `payload.FromProto<T>()`, `reply.ToProto()`가 직접 나온다.
- TicTacToe와 다른 JSON 샘플 raw 경계에서 `payload.FromJson<T>()`, `reply.ToJson()` 또는
  `Encode()`가 직접 나온다.
- `RequestToChannel(channel, dto).Async<T>()` 표면은 유지한다. 2026-06-16 현재
  `Zlink.Framework` 내부 channel envelope codec 은 `options.Codecs.AddProtobuf()` /
  `AddJson()` 등록을 보고 typed request/reply body 를 직렬화하도록 연결했다. 이 변경은
  public signature 를 바꾸지 않는다.
- 2026-06-16 현재 .NET Bingo/TicTacToe의 channel request handler 와 channel client 호출부는
  대부분 typed request/reply 표면을 사용한다. 이 경로에서는 codec extension using 이
  필요하지 않으므로 typed handler 에 남아 있던 불필요한 `Systems.Zlink.Codecs.Protobuf`
  using 은 제거한다.
- 2026-06-16 현재 .NET Bingo Shared 는 generated Protobuf 계약을 사용한다.
  `Bingo.Shared.csproj`가 `Google.Protobuf`, `Grpc.Tools`,
  `<Protobuf Include="Contracts\bingo_messages.proto" GrpcServices="None" />`를 등록하고,
  생성 산출물은 `IMessage<T>` 기반 partial class 로 만들어진다. 따라서 .NET Bingo는
  POJO/record 와 generated proto type 을 전송 계약으로 중복 사용하는 문제가 아니다. 남은
  문제는 Spot create/join 과 session handler 가 raw `Message`라서 샘플 업무 코드에
  `.ToProto()` / `.FromProto()`가 직접 남는 점이다.
- raw `Message` 경계와 spot create/join 경계는 아직 동일한 추상화를 쓰지 못한다.
- `IZLinkSpot.OnCreateAsync(Message request, ...)`, `IZLinkSpot<TActor>.OnActorJoinAsync(...,
  Message request, ...)`, `IZLinkSpotManager.CreateAsync<TSpot>(Message request, ...)`,
  `IZLinkActorContext.JoinSpot(..., Message request)`, `IZLinkSessionPacketHandler.HandleAsync(...,
  Message payload, ...)`는 현재 공개 signature 가 raw `Message`이다. 새 public overload 를
  만들지 않는 조건에서 샘플 업무 코드의 모든 lifecycle decode/encode 를 없애려면 기존
  runtime registry 가 request/reply type metadata 를 별도로 알아야 한다. 그 metadata 가
  없는 경계는 임의 구현하지 않고 설계 이슈로 분리한다.
- 특히 `IZLinkActorContext.JoinSpot(RoutingId, Message)`와 `IZLinkSpotManager.CreateAsync<TSpot>(Message)`
  는 object payload 를 받는 공개 overload 가 없다. 따라서 `new BingoRoomJoinReq(...).ToProto()`,
  `new TicTacToeGameJoinReq(...).ToJson()`, room create settings 의 `.ToProto()`를 단순히
  기존 typed 호출로 바꿀 수 없다. 이 항목은 public signature 를 바꾸지 않는 조건에서는
  runtime registration metadata 로 해결할 별도 설계 이슈다.
- `IZLinkActorJoinSpotCall.Async()`도 `ZLinkActorJoinResult`를 반환하고 reply 는 raw
  `Message Reply`이다. Java/Kotlin처럼 `submit(ReplyType)` 또는 `await(ReplyType)`에 해당하는
  public surface 가 없으므로, actor join reply decode 를 샘플에서 없애려면 typed join metadata
  설계가 먼저 필요하다.
- `IZLinkSessionPacketHandler<TContext>.HandleAsync(..., Message payload, ...)`도 request type 을
  generic 으로 드러내지 않는다. Bingo session auth 의 `payload.FromProto<AuthenticateReq>()`와
  TicTacToe session auth 의 `payload.FromJson<AuthenticateReq>()`는 기존 public surface 만으로는
  제거할 수 없으며, typed stream packet handler metadata 설계가 먼저 필요하다.
- .NET session dispatcher 는 `PacketName`으로 handler 를 찾은 뒤 raw `Message payload`를 그대로
  `HandleAsync`에 넘긴다. handler type 에 request/reply generic 이 없으므로 packet name 만으로
  codec registry 의 target type 을 고를 수 없다.

수정 방향:

- `Message.From(request)` 또는 기존 typed 호출이 내부적으로 codec registry 를 타도록
  정리한다. .NET channel send/request/publish 와 Spot outbound typed 호출은 이 경로로
  연결한다.
- Spot create/join 은 기존 public signature 를 유지한 상태에서 runtime 내부 decode/encode
  경로가 codec registry 를 타도록 정리한다.
- raw `Message`를 받는 lower-level API 는 유지한다. 샘플을 위해 새 typed public API 를
  추가하지 않는다.
- 기존 actor packet handler 처럼 request/reply type 이 이미 handler generic 으로 드러나는
  경로는 runtime 내부 codec registry 로 옮긴다. 반대로 Spot create/join 과 session packet
  handler 처럼 public contract 가 raw `Message`만 제공하는 경계는, 어떤 등록 metadata 로
  request/reply type 을 줄지 먼저 설계해야 한다.
- `.ToJson`, `.FromJson`, `.ToProto`, `.FromProto`는 codec 패키지의 저수준 helper 로 남기되
  샘플 업무 코드에서는 사용하지 않는다.
- Bingo는 generated Protobuf type 을 전송 계약으로 유지한다.
- TicTacToe와 기타 JSON 샘플은 DTO는 유지하되 encode/decode 호출은 framework runtime 내부
  변환 경로로 이동한다.
- Bingo draw timer 는 가능하면 `[ZLinkSpotTimerHandler("bingo-draw", ...)]`로 자동 등록한다.
  그러나 현재 attribute 는 `name`과 `periodMilliseconds`만 제공하고 `ZLinkTimerOptions`를
  담지 않는다. 기존 `Context.AddTimer<BingoRoomDrawTimerHandler>(..., new ZLinkTimerOptions
  { OverrunPolicy = DelayNextTick, StopOnUnhandledException = true })`와 같은 정책을 보존해야
  한다면, public surface 를 바꾸지 않는 이번 작업에서 attribute 만으로 표현할 수 없다. 현재
  Bingo 샘플은 handler attribute 로 자동 등록하고, handler 가 draw 준비 전 tick 을 no-op 으로
  처리한다. timer options metadata 는 별도 설계 이슈로 남긴다.

필요한 framework/runtime 수정 후보:

- `framework/languages/dotnet/src/Zlink.Framework/` 아래 channel, stream, spot runtime
- `framework/languages/dotnet/src/Systems.Zlink.Stream.Connector.Json/`
- `framework/languages/dotnet/src/Systems.Zlink.Stream.Connector.Protobuf/`
- `bindings/dotnet/codecs/Zlink.Codecs.Json/`
- `bindings/dotnet/codecs/Zlink.Codecs.Protobuf/`

### 4.3 Java

Java는 channel request handler 표면은 대체로 정상이다. 문제는 stream session 과
Spot create/join 경계다.

수정 대상:

- `framework/languages/java/samples/java/Bingo/Server/Session/src/main/java/systems/zlink/samples/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.java`
- `framework/languages/java/samples/java/Bingo/Server/Play/src/main/java/systems/zlink/samples/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.java`
- `framework/languages/java/samples/java/Bingo/Server/Play/src/main/java/systems/zlink/samples/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomSpotCreatedHandler.java`
- `framework/languages/java/samples/java/Bingo/Server/Play/src/main/java/systems/zlink/samples/bingo/server/play/adapters/zlink/handlers/BingoRoomDirectory.java`
- `framework/languages/java/samples/java/TicTacToe/Server/src/main/java/systems/zlink/samples/tictactoe/server/play/adapters/zlink/sessions/handlers/AuthenticatePlaySessionHandler.java`
- `framework/languages/java/samples/java/TicTacToe/Server/src/main/java/systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.java`
- `framework/languages/java/samples/java/TicTacToe/Server/src/main/java/systems/zlink/samples/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorJoinGameHandler.java`

현재 문제:

- stream session auth 에서 `ZLinkStreamProtobuf.decode(...)` 또는 `ZLinkStreamJson.decode(...)`를
  직접 호출한다.
- Spot join/create 에서 `ObjectMapper.readValue(request.toByteArray(), ...)`와
  `Message.from(json.writeValueAsBytes(reply))`가 직접 나온다.
- Java Bingo Shared 계약은 아직 generated Protobuf type 으로 전환되지 않았다.
  `Shared/src/main/proto/bingo_messages.proto`가 없고,
  `Shared/src/main/java/.../Messages.java` hand-written record 파일이 전송 계약으로 남아 있다.
  `Shared/build.gradle.kts`도 `com.google.protobuf` Gradle plugin 을 등록하지 않는다.
- 2026-06-16 현재 `ZLinkStreamProtobuf`는 generated `MessageLite` 값이면 real Protobuf bytes 를
  쓰도록 정리되어 있다. 값이 `MessageLite`이면 `toByteArray()`로 encode 하고, target type 이
  `MessageLite`이면 generated `parser()`로 decode 한다. 그러나 Java/Kotlin Bingo sample type 은
  아직 `MessageLite`가 아니므로 이 경로를 타지 않는다. 현재는 JSON fallback 으로 직렬화된다.
- Kotlin Bingo Shared 도 아직 generated Protobuf type 으로 전환되지 않았다.
  `Shared/src/main/kotlin/.../Messages.kt`의 `data class`들이 전송 계약으로 남아 있다.
- `ZLinkSpot.onCreate(Message request)`, `ZLinkSpot.onActorJoin(actor, Message request, ...)`,
  `ZLinkSessionPacketHandler.handle(..., Message payload)`는 현재 공개 signature 가 raw
  `Message`이다. Kotlin wrapper 도 같은 Java core surface 를 따른다. 새 typed lifecycle
  signature 를 추가하지 않는 조건에서는 이 경계에 필요한 request/reply type metadata 를
  기존 registration 안에서 제공할 수 있는지 먼저 설계해야 한다.
- 반면 `ZLinkActorContext.joinSpot(RoutingId, Object)`와
  `ZLinkActorJoinSpotCall.await(Class<TReply>)`는 이미 공개되어 있고, runtime 내부에서
  serializer 를 사용한다. Java/Kotlin의 actor join 호출부가 별도 `Message.from(...)`이나
  JSON helper 를 만들 필요는 없다. 이 경로는 샘플에서 우선 정리한다.
- 2026-06-16 현재 Java/Kotlin TicTacToe의 `PlayActorJoinGameHandler`는 이미 이 표준 표면을
  사용한다. `joinSpot(RoutingId, new TicTacToeGameJoinReq(...)).await(TicTacToeGameJoinRes.class)`
  또는 Kotlin의 동등한 `submit(TicTacToeGameJoinRes::class.java)` 형태이며, 호출부에
  `Message.from(json.writeValueAsBytes(...))`가 없다.
- 2026-06-16 현재 Java/Kotlin Bingo의 `MatchBingoActorHandler`도 같은 표준 표면을 사용한다.
  다만 전송 값은 generated builder 가 아니라 hand-written record/data class 이다. 따라서
  호출 표면 일부는 정리되어 있어도 generated Protobuf 전송 계약 완료로 보지 않는다.
- 2026-06-16 현재 `ZLinkSpotManager.create/getOrCreate`는 `Message request`만 받는다. 따라서
  Spot create settings 를 object request 로 직접 넘기는 표준 surface 는 없다. Java/Kotlin
  Bingo는 현재 `ObjectMapper` / `Message.from(json.writeValueAsBytes(...))` 경로로 settings
  payload 를 만든다. 이 항목은 아직 raw `Message` 표면이므로 typed Spot create metadata 설계
  이슈로 남긴다.
- 2026-06-16 현재 `ZLinkSpot.onCreate`, `ZLinkSpot.onActorJoin`, `ZLinkSessionPacketHandler.handle`
  모두 raw `Message` payload 를 받는다. Java/Kotlin Bingo raw lifecycle 경계에는 아직
  `ZLinkStreamProtobuf.decode`, `ObjectMapper.readValue`,
  `Message.from(json.writeValueAsBytes(...))`가 남아 있다. TicTacToe 쪽 raw JSON lifecycle
  경계도 typed metadata 설계 후 제거한다.
- Java session dispatcher 도 `packetName()`으로 handler 를 찾은 뒤 raw `Message payload`를
  `handle(context, header, payload)`에 넘긴다. handler interface 에 request/reply generic 이
  없으므로 session auth decode 는 typed stream packet metadata 설계 전에는 제거할 수 없다.

수정 방향:

- Java framework 의 기존 session packet handler public signature 는 유지한다. 일반 해법은 packet
  name 과 request/reply type 을 기존 registration metadata 와 codec registry 내부에서 연결하는
  것이다.
- Java framework 의 기존 Spot create/join public signature 는 유지한다.
  raw `Message` decode/encode 는 샘플이 아니라 runtime 내부 표준 경로에서 처리한다.
- request/reply type 을 generic handler 에서 이미 알 수 있는 channel, actor packet, Spot
  request handler 경로를 우선 codec registry 로 연결한다. raw lifecycle 경계는 metadata
  공급 방식이 정해지기 전에는 샘플 helper 를 다른 이름의 helper 로 숨기지 않는다.
- Java/Kotlin actor join 호출부는 이미 object request 와 typed reply 를 받는 표준 표면이므로
  `joinSpot(..., dto).await(Reply.class)` 또는 Kotlin wrapper 의 동등한 표면을 사용한다.
  이 호출부에서 `Message.from(json.writeValueAsBytes(...))`를 직접 만들면 실패로 본다.
- `ZLinkStreamProtobuf.decode`, `ZLinkStreamJson.decode`, `ObjectMapper.readValue`,
  `json.writeValueAsBytes` 직접 호출을 샘플에서 제거한다. 단, 현재 public signature 가 raw
  `Message`인 TicTacToe session/Spot lifecycle 경계는 typed metadata 설계 전까지 별도 미완료
  항목으로 둔다.
- Bingo Java Shared 는 아직 `.proto` generated class 를 전송 계약으로 쓰지 않는다.
  `Messages.java` record 계약과 raw lifecycle `ObjectMapper`/`ZLinkStreamProtobuf.decode`
  경로가 남아 있다. 남은 Java 쪽 작업은 generated Protobuf 계약 도입과 raw
  Spot/session lifecycle 처리를 runtime metadata 기반 표준 경로로 일반화하는 것이다.
- 2026-06-16 현재 Java framework channel/actor/Spot 공용 serializer 는
  `ZLinkProtobufMessageSerializer`를 추가해 generated `MessageLite` 값이면 real Protobuf bytes 를
  쓴다. 그러나 Java/Kotlin Bingo sample type 은 아직 generated `MessageLite`가 아니므로 이
  경로를 타지 않는다. fallback 은 아직 generated type 으로 전환되지 않은 DTO 호환 경로다.
- TicTacToe Java Shared 는 JSON DTO를 유지하되 framework codec registry 가 encode/decode 한다.
- 2026-06-16 현재 Java Bingo draw timer 는 `@ZLinkSpotTimer(name = "bingo-draw",
  periodMillis = SampleTimings.DrawPeriodMillis)`로 자동 등록한다. `BingoRoomSpot`의
  `context.addTimer(...)`, `ZLinkTimer` 필드, timer cancel 코드는 제거했다. 기존 수동 등록은
  `new ZLinkTimerOptions()` 기본값만 사용했기 때문에 scanned timer 와 의미가 같다. `DrawPeriodMillis`는
  annotation 에서 쓸 수 있는 compile-time constant 로 두고, 기존 `DrawPeriod` Duration 은 이
  값을 사용한다.

필요한 framework/runtime 수정 후보:

- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/channels/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/streams/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/`
- `framework/languages/java/zlink-stream-connector-json/src/main/java/...`
- `framework/languages/java/zlink-stream-connector-protobuf/src/main/java/...`
- `framework/languages/java/zlink-stream-connector-msgpack/src/main/java/...`
- `framework/languages/java/zlink-framework-testkit/src/contractTest/java/...`

### 4.4 Kotlin

Kotlin은 Java framework 위의 언어 표면이므로 Java와 같은 문제를 가진다.

수정 대상:

- `framework/languages/java/samples/kotlin/Bingo/Server/Session/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/session/sessions/handlers/AuthenticateSessionHandler.kt`
- `framework/languages/java/samples/kotlin/Bingo/Server/Play/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/BingoRoomSpot.kt`
- `framework/languages/java/samples/kotlin/Bingo/Server/Play/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/spots/handlers/BingoRoomSpotCreatedHandler.kt`
- `framework/languages/java/samples/kotlin/Bingo/Server/Play/src/main/kotlin/systems/zlink/samples/kotlin/bingo/server/play/adapters/zlink/handlers/BingoRoomDirectory.kt`
- `framework/languages/java/samples/kotlin/TicTacToe/Server/src/main/kotlin/systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/sessions/handlers/AuthenticatePlaySessionHandler.kt`
- `framework/languages/java/samples/kotlin/TicTacToe/Server/src/main/kotlin/systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/TicTacToeGame.kt`
- `framework/languages/java/samples/kotlin/TicTacToe/Server/src/main/kotlin/systems/zlink/samples/kotlin/tictactoe/server/play/adapters/zlink/spots/handlers/PlayActorJoinGameHandler.kt`

현재 문제:

- Kotlin Bingo의 Protobuf 전송 계약은 아직 generated type 으로 전환되지 않았다.
  `Messages.kt`의 `data class` 계약과 raw lifecycle 경계의 `ZLinkStreamProtobuf.decode`,
  `ObjectMapper.readValue`, `json.writeValueAsBytes` 호출이 남아 있다.
- Kotlin TicTacToe session handler 에 `ZLinkStreamJson.decode(...)`가 직접 나온다.
- Kotlin TicTacToe Spot actor join 경계에 `json.readValue(request.toByteArray(), ...)`와
  `Message.from(json.writeValueAsBytes(...))`가 직접 나온다.
- 2026-06-16 현재 Kotlin Bingo Shared 는 Java generated `Messages` class 를 재사용하지 않는다.
  Kotlin sample 의 request/reply/notify 전송 타입은 `Messages.kt`의 hand-written `data class`
  이다.
- Java core runtime 의 channel/actor/Spot serializer 와 `ZLinkStreamProtobuf`는 generated
  `MessageLite` 값이면 real Protobuf bytes 를 쓴다. Kotlin Bingo는 아직 generated type 이
  아니므로 이 경로를 타지 않는다.

수정 방향:

- Java core 의 기존 session/spot 표준 경로 정리를 Kotlin coroutine wrapper 가 그대로
  사용하게 한다. Kotlin 전용 새 public API 를 추가하지 않는다.
- Kotlin Bingo 샘플은 아직 `ZLinkCoroutineSessionPacketHandler`와 `ZLinkCoroutineSpot`에서
  `ZLinkStreamProtobuf.decode`, `ObjectMapper.readValue`, `json.writeValueAsBytes`를 직접
  호출한다. 이를 generated Protobuf type 과 runtime metadata 기반 표준 경로로 옮겨야 한다.
- Kotlin TicTacToe 샘플은 JSON DTO를 전송 계약으로 유지한다. actor join 호출부는 이미
  `joinSpot(..., TicTacToeGameJoinReq).submit(TicTacToeGameJoinRes::class.java)` 표준 표면을
  사용하므로, 이 호출부에 codec helper 를 되살리지 않는다. 남은 session/Spot 수신 경계는
  Java core 의 typed lifecycle metadata 설계 후 제거한다.
- Bingo Kotlin Shared 는 Java와 같은 `.proto` generated type 을 사용하거나 Kotlin friendly
  wrapper 를 두더라도 전송 계약은 generated type 으로 고정한다.
- Kotlin wrapper 를 두더라도 wrapper 가 전송 계약이 되면 안 된다. wrapper 는 domain 편의
  변환용으로만 허용하고, stream/channel/actor/Spot 경계의 request/reply/notify type 은
  generated type 이어야 한다.
- Kotlin sample 전환은 Java generated class 를 Kotlin source 에서 직접 쓰는 방식을 우선 검토한다.
  별도 Kotlin data class 를 request/reply type 으로 되살리면 Protobuf schema 와 병렬 계약이
  생기므로 실패로 본다.
- TicTacToe Kotlin Shared 는 JSON DTO를 유지하되 runtime 내부 변환 경로가 decode/encode 한다.
- 2026-06-16 현재 Kotlin Bingo draw timer 도 Java core 의 `@ZLinkSpotTimer` scanning 경로를
  따른다. `BingoRoomSpot`의 `context.addTimer(...)`, nullable `ZLinkTimer`, timer cancel 코드는
  제거했다. Kotlin 전용 새 public API 는 추가하지 않았다. 기존 수동 등록은 기본
  `ZLinkTimerOptions()`만 사용했기 때문에 scanned timer 와 의미가 같다.

필요한 framework/runtime 수정 후보:

- Java 항목 전체
- `framework/languages/java/zlink-framework-kotlin/src/main/kotlin/...`
- Kotlin sample release gate 또는 contract test

### 4.5 C++

C++은 channel client typed 호출은 비교적 정상이다. 문제는 shared contract helper 와
Spot/stream raw `message_t` 경계다.

수정 대상:

- `framework/languages/cpp/samples/Bingo/Shared/Contracts/messages.hpp`
- `framework/languages/cpp/samples/Bingo/Shared/Contracts/bingo_messages.proto`
- `framework/languages/cpp/samples/Bingo/Server/Session/Sessions/Handlers/authenticate_session_handler.hpp`
- `framework/languages/cpp/samples/Bingo/Server/Session/Sessions/bingo_session.hpp`
- `framework/languages/cpp/samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_entry_spot.hpp`
- `framework/languages/cpp/samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_room_spot.hpp`
- `framework/languages/cpp/samples/Bingo/Server/Play/Adapters/ZLink/Handlers/allocate_bingo_room_handler.hpp`
- `framework/languages/cpp/samples/Bingo/Server/Play/Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp`
- `framework/languages/cpp/samples/TicTacToe/Shared/Contracts/messages.hpp`
- `framework/languages/cpp/samples/TicTacToe/Server/Play/Adapters/ZLink/Sessions/Handlers/authenticate_play_session_handler.hpp`
- `framework/languages/cpp/samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_entry_spot.hpp`
- `framework/languages/cpp/samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_spot.hpp`

현재 문제:

- Bingo `messages.hpp`에 `json_to_protobuf_payload`, `json_from_protobuf_payload`,
  `to_stream_payload`, `from_stream_payload`가 있다.
- TicTacToe `messages.hpp`에도 JSON 전용 `to_stream_payload`, `from_stream_payload`가 있다.
- Spot join/create 경계에서 이 helper 를 직접 호출한다.
- Bingo Protobuf가 `.proto`와 1:1 generated type 이 아니라 JSON을 Protobuf 비슷한 payload 로
  감싸는 형태다.
- 2026-06-16 현재 C++ channel typed 호출은 `channel_client_t::request(channel, dto).async<T>()`
  표면이 있고, serializer registry 는 `std::type_index(typeid(TRequest))`를 기준으로
  request/reply 변환 경로를 탄다. 이 경로는 표준 typed 호출로 유지한다.
- 2026-06-16 현재 `serializer_registry_t::add_json<T>()`,
  `add_message_pack<T>()`, `add_protobuf<T>()`는 codec extension 또는 ADL
  `to_stream_payload/from_stream_payload`를 사용한다. 즉 channel 경로에는 typed registry 가
  있지만, raw Spot/session 경계에서 같은 type metadata 를 제공하는 public surface 는 없다.
- 2026-06-16 현재 `actor_context_t::join_spot(spot_rid_t, const zlink::message_t&)`,
  stream `reply_packet(..., const zlink::message_t&)`, packet stream session `on_packet(...,
  const zlink::message_t&)`, Spot create/join response 는 raw `message_t` 중심이다. 이 경계의
  `to_stream_payload/from_stream_payload` 호출을 샘플에서 완전히 없애려면 typed lifecycle/stream
  metadata 설계가 먼저 필요하다.
- C++ Bingo의 현재 Protobuf 구현은 generated source 가 아니다. `json_to_protobuf_payload`와
  `json_from_protobuf_payload`가 존재하는 한, 이 상태를 Bingo Protobuf 정리 완료로 인정하지 않는다.
- 2026-06-16 재확인: 로컬 환경에는 `/usr/bin/protoc`가 있고 버전은 `libprotoc 3.21.12`다.
  그러나 `framework/languages/cpp/CMakeLists.txt`와 C++ cmake 파일에는
  `find_package(Protobuf)`, `protobuf_generate`, `Protobuf::...`, `protoc` 실행 규칙이 없다.
  `framework/languages/cpp/samples/Bingo/Shared/Contracts/bingo_messages.proto`는 있지만
  generated `bingo_messages.pb.h` / `bingo_messages.pb.cc` 산출물도 없다. 따라서 C++ Bingo
  generated Protobuf 전환은 도구 설치 문제가 아니라 CMake 생성 규칙과 전송 타입 전환을 함께
  해야 하는 하위 작업이다.

수정 방향:

- C++ framework codec registry 에서 이미 공개된 typed request 표면이 packet name/type
  registry 를 통해 encode 되도록 책임을 모은다. `message_t::from(dto)`가 이미 공개된
  표면이 아니면 새 overload 를 만들지 않는다.
- raw `message_t` Spot/session public signature 는 유지한다. codec registry 기반 변환은
  기존 runtime 내부 경로로 내린다.
- `messages.hpp`에서는 전송 DTO와 packet name 상수만 유지한다. codec helper 는 제거하거나
  framework codec implementation 으로 이동한다.
- Bingo `.proto`를 실제 generated source 로 쓰는 빌드 내부 경로를 추가한다. generator 도입이
  이번 단계에서 과하면, C++ Bingo Protobuf 정리는 별도 하위 계획으로 분리한다. JSON 문자열을
  Protobuf payload 로 넣는 현재 방식이나 수동 binary encoding 은 완료 상태로 인정하지 않는다.
- TicTacToe JSON helper 는 샘플 계약에서 제거하고 JSON codec registry 로 이동한다.
- C++에서 `message_t::from(dto)`가 공개 표면으로 이미 존재하지 않으면 새 overload 를 추가하지
  않는다. channel 은 기존 `.request(channel, dto).async<T>()`를 사용하고, raw Spot/session
  경계는 typed metadata 설계 없이 helper 이름만 바꾸어 숨기지 않는다.

필요한 framework/runtime 수정 후보:

- `framework/languages/cpp/connector/core/include/zlink/stream_connector/codecs/`
- `framework/languages/cpp/connector/core/include/zlink/stream_connector/contracts/codec_registry.hpp`
- `framework/languages/cpp/connector/core/src/runtime/connector_runtime.cpp`
- `framework/languages/cpp/samples/*/Shared/Contracts/messages.hpp`
- `framework/languages/cpp/CMakeLists.txt`
- C++ sample parity/contract tests

## 5. 공통 구현 단계

### 5.1 현재 public surface 확정

먼저 문서와 테스트가 기대하는 표면을 고정한다.

수정 대상:

- `framework/doc/spec/sample/README.ko.md`
- `framework/doc/spec/sample/bingo/README.ko.md`
- `framework/doc/spec/sample/tictactoe/README.ko.md`
- `framework/languages/node/doc/spec/handler-interfaces.ko.md`
- `framework/languages/node/doc/spec/nestjs-channel-messaging.ko.md`
- `framework/languages/node/doc/spec/nestjs-stream.ko.md`
- `framework/languages/dotnet/doc/spec/aspnet-core-channel-messaging.ko.md`
- `framework/languages/cpp/doc/spec/cpp-framework-interfaces.ko.md`
- `framework/languages/java/samples/README.md`
- `framework/languages/node/samples/README.ko.md`
- `framework/languages/cpp/samples/README.ko.md`
- `framework/languages/dotnet/samples/README.md`

문서에 명시할 규칙:

- Bingo는 client-server stream, server-server channel, actor, Spot payload 모두 Protobuf.
- TicTacToe는 client-server stream, server-server channel, actor, Spot payload 모두 JSON.
- codec 선택은 등록부와 runtime이 처리한다.
- 샘플 업무 코드는 codec 별 helper 를 호출하지 않는다.
- 호출 표면은 이미 공개된 `Message.from(...)`류 factory 또는 기존 표준 typed 호출과
  typed submit 이다.
- `Message.from(...)`, `Message.From(...)`, `message_t::from(...)`가 object payload 를
  받는 언어에서는 즉시 직렬화하고, `Message`에는 byte payload 와 packet metadata 만
  남긴다. 원본 객체를 나중에 decode 하기 위해 보관하지 않는다.
- `packetName(...)`, `PacketName(...)`, `packet_name(...)`은 packet name 을 type/metadata
  에서 알 수 없거나 실제 packet name 이 다를 때의 override 로 설명한다.

bindings codec extension 조사 대상:

- C++: `bindings/cpp/codecs`
- .NET: `bindings/dotnet/codecs`
- Go: `bindings/go/codec`
- Java: `bindings/java/codec`
- Node.js: `bindings/node/packages` 와 codec 관련 package
- Python: `bindings/python/codecs`
- Rust: `bindings/rust/crates`

이 7개 bindings 에서 이미 제공하는 object serialization 표면과 framework adapter 의
typed 호출 표면을 먼저 확인한다. framework 샘플 대상은 현재 `cpp`, `dotnet`, `java`,
`kotlin`, `node`이지만, 메시지 생성 의미는 bindings extension 전체와 어긋나면 안 된다.

현재 확인된 bindings codec extension 표면:

| 언어 | 현재 object 직렬화 표면 | 판단 |
|------|-------------------------|------|
| C++ | `message_t::from_json`, `message_t::from_messagepack`, `message_t::from_protobuf`, `zlink::codec::*::encode/to_message` | codec extension 이 즉시 직렬화한다. framework typed 호출은 이 extension 계층을 타야 한다. |
| .NET | `value.ToJson()`, `value.ToMsgPack()`, `value.ToProto()` | extension method 가 즉시 직렬화한다. 샘플 업무 코드에서는 직접 호출하지 않고 runtime 내부에서 사용한다. |
| Go | `json.Encode`, `messagepack.Encode`, `proto.Encode` | codec package 가 즉시 직렬화한다. framework adapter 가 도입될 때 같은 의미를 따른다. |
| Java | `JsonCodec.toMessage`, `MessagePackCodec.toMessage`, `ProtobufCodec.toMessage` | codec package 가 즉시 직렬화한다. Spring/framework typed 호출 내부로 내려야 한다. |
| Node.js | `@zlink-systems/zlink-codec-json.encode`, `zlink-codec-messagepack.encode`, `zlink-codec-protobuf.encode` | codec package 가 즉시 직렬화한다. `Message.from(object)`를 core binding 에 임의로 넣지 않고 framework registry/extension 연결을 먼저 본다. |
| Python | `zlink_codec_json.encode`, `zlink_codec_messagepack.encode`, `zlink_codec_protobuf.encode` | codec package 가 즉시 직렬화한다. framework adapter 가 도입될 때 같은 의미를 따른다. |
| Rust | `zlink_codec_json::encode`, `zlink_codec_messagepack::encode`, `zlink_codec_protobuf::encode` | codec crate 가 즉시 직렬화한다. framework adapter 가 도입될 때 같은 의미를 따른다. |

따라서 이번 샘플 정리의 구현 방향은 core binding `Message`를 일괄 object factory 로
바꾸는 것이 아니다. 이미 존재하는 bindings codec extension 또는 framework typed 호출
내부에서 같은 즉시 직렬화 의미를 제공하고, 샘플 업무 코드에서는 그 codec helper 이름을
직접 드러내지 않는다.

### 5.2 framework runtime 의 codec registry 연결

각 언어별로 channel, stream, Spot, actor relay가 같은 registry 를 타도록 정리한다.

Node.js channel 경로는 1차로 다음 방향으로 연결한다.

- `ZLinkFrameworkOptions.codecs(...)`가 기존 `ZLinkCodecRegistryBuilder`에 serializer 를
  등록한다.
- `ZLinkFrameworkRegistration`은 등록된 serializer map 을 보관한다.
- channel envelope encode/decode 는 serializer 가 하나 등록된 경우 그 serializer 를
  기본 payload 변환기로 사용한다.
- NestJS handler 는 channel dispatcher 에서 decode 된 object payload 를 그대로 받는다.
- serializer 가 없으면 기존 JSON/Buffer 동작을 유지한다.

이 연결은 channel request/send/publish 와 request reply 에만 해당한다. Spot create,
Spot actor join, stream session packet handler 는 아직 raw `Message`/frame 경계가
남아 있으므로 같은 registry 경로를 별도로 연결해야 한다. 이 경계를 샘플 helper 로
덮어 두면 codec helper 나 object payload cache 같은 중간 수단이 업무 코드로 다시 새어
나오기 때문에 완료로 보지 않는다.

필수 확인 항목:

- channel request payload encode
- channel request reply decode
- channel send payload encode
- fanout publish payload encode
- stream client request/send payload encode
- stream server session request decode
- stream server session reply encode
- Spot create request decode
- Spot actor join request decode
- Spot actor join reply encode
- actor request payload decode
- actor request reply encode
- notification publish payload encode

### 5.3 샘플 정리 순서

1. 7개 bindings codec extension 과 framework typed 호출의 현재 public surface 를 먼저
   확정한다.
   - 이미 object payload 를 받는 Message factory/extension 이 있는지 확인한다.
   - 없으면 새 public overload 를 추가하지 않고, 기존 typed 호출 내부 연결로 해결 가능한지
     확인한다.
   - 기존 표면으로 불가능한 언어는 별도 설계 이슈로 분리한다.
2. Node Bingo를 먼저 정리한다.
   - 현재 가장 많은 위반이 있고, 잘못된 Protobuf 구현이 여기에 집중되어 있다.
3. .NET Bingo/TicTacToe raw Spot 경계를 정리한다.
   - framework reference surface 로 삼기 좋다.
4. Java/Kotlin Bingo/TicTacToe를 같은 표준 호출 인터페이스로 정리한다.
5. C++ Bingo/TicTacToe helper 를 framework codec 쪽으로 내린다.
6. 다른 .NET JSON 샘플의 raw Spot 경계를 정리한다.
   - DeliveryDispatch, GameQuest, ShoppingMall, SupportChat.
7. 문서와 contract tests를 마지막에 실제 코드 표면과 맞춘다.

### 5.4 다음 구현 단위

현재 public surface 를 확인한 결과, 다음 작업은 한 번에 모든 raw 경계를 제거하는 방식으로
진행하지 않는다. 표준 표면이 이미 있는 경로와 새 설계가 필요한 경로를 나누어 처리한다.

1. **Node channel/registry 마무리**
   - `requestToChannel(..., dto).submit<T>()`가 `addJson()` / `addProtobuf()` 등록만으로
     request 와 reply 를 typed 값으로 돌려주는지 contract test 를 추가한다.
   - 샘플 모듈의 sample-specific serializer 반복 등록은 금지 상태로 유지한다.
   - Spot create/join 과 session bridge 의 raw helper 는 typed lifecycle 설계 전까지
     허용 범위에만 가둔다.
2. **.NET channel/spot outbound typed 경로 고정**
   - channel typed 호출과 Spot outbound typed 호출이 codec registry 를 타는 regression test 를
     추가한다.
   - raw Spot create/join/session handler helper 는 현재 public signature 로 제거할 수 없으므로
     typed lifecycle metadata 설계 이슈로 남긴다.
   - Bingo draw timer 는 attribute 옵션 계약이 정해지기 전에는 수동 등록의 timer policy 를
     잃지 않는다.
3. **Java/Kotlin generated Protobuf 전환 정리**
   - Java/Kotlin Bingo Shared 는 아직 hand-written record/data class 계약이다.
   - 전송 경계의 request/reply/notify type 은 generated class 로 바꾼다. domain 편의 모델이
     필요하면 전송 계약이 아닌 내부 모델로 제한한다.
   - Kotlin `data class`가 전송 계약으로 다시 생기면 Java/Kotlin 전체 완료로 보지 않는다.
4. **C++ generated Protobuf 전환 설계**
   - CMake/protoc 도입 범위와 generated source 위치를 먼저 정한다.
   - generator 도입 전에는 `json_to_protobuf_payload` 방식이 동작해도 완료로 인정하지 않는다.
   - raw `message_t` Spot/session 경계는 typed metadata 설계 전까지 helper 이름만 바꾸어 숨기지 않는다.
5. **최종 grep gate 강화**
   - 중간 gate 의 raw lifecycle 허용 목록을 제거하거나, 설계 이슈로 남은 public raw 경계를
     명시적으로 제외한다.
   - 예외는 codec package 내부, codec unit test, 문서의 나쁜 예시, HTTP body 처리, config
     JSON 처리로만 제한한다.

## 6. 회귀 테스트 계획

### 6.0 현재 완료 판정

2026-06-16 현재 이 작업은 완료 상태가 아니다. 지금까지 통과한 contract gate 는
금지 helper 가 더 넓은 샘플 업무 코드로 퍼지지 않게 막는 중간 안전장치다. 아래 항목은
최종 완료 전 반드시 별도 구현 또는 설계 결정이 필요하다.

- Node Bingo는 channel request 호출부의 `submit<Buffer>().then(decode...)` 패턴과
  Spot create/join 경계의 `createProtobufMessage` / `readProtobufMessage` 호출을 제거했다.
  현재 Spot create/join 은 `Message.from(bingoPayload(...))`와 `request.value<T>()` 중간
  상태이고, session bridge 에는 `fromBingoProto` / `toBingoProto`가 남아 있다. 현재 public
  signature 가 raw `Message`/frame 이므로 helper 이름만 바꾸어 숨기지 않는다.
- .NET Bingo/TicTacToe는 channel typed 호출과 Bingo generated Protobuf 계약은 정리되어
  있지만, Spot create/join/session handler 경계에는 `.ToProto()` / `.FromProto<T>()` /
  `.ToJson()` / `.FromJson<T>()`가 남아 있다. `IZLinkSpotManager.CreateAsync`,
  `IZLinkActorContext.JoinSpot`, `IZLinkSessionPacketHandler.HandleAsync`의 현재 public
  signature 만으로는 request/reply type metadata 를 알 수 없다.
- Java/Kotlin Bingo는 아직 `.proto` generated `Messages` class 를 전송 계약으로 사용하지
  않는다. Java는 `Messages.java` record, Kotlin은 `Messages.kt` data class 를 전송 계약으로
  사용한다.
- Java/Kotlin framework channel/actor/Spot serializer 와 `ZLinkStreamProtobuf`는 generated
  `MessageLite` 값이면 real Protobuf bytes 를 쓸 수 있게 됐다. 그러나 Java/Kotlin Bingo sample
  타입은 아직 generated 타입이 아니므로 이 경로를 타지 않는다.
- Java/Kotlin Bingo와 TicTacToe raw session/Spot lifecycle 경계에는 아직
  `ZLinkStreamProtobuf.decode`, `ZLinkStreamJson.decode`, `ObjectMapper.readValue`,
  `Message.from(json.writeValueAsBytes(...))`가 남아 있다.
  이미 공개된 actor join typed 호출부는 유지하되, 남은 raw lifecycle 수신 경계는 별도
  metadata 설계가 필요하다.
- C++ Bingo는 generated Protobuf type 을 사용하지 않고, `json_to_protobuf_payload`와
  수동 varint helper 를 통해 JSON 문자열을 payload 로 감싼다. 이 상태는 Protobuf 샘플로
  완료 처리하지 않는다.
- C++/TicTacToe와 C++/Bingo raw Spot/session 경계에는 `to_stream_payload` /
  `from_stream_payload`가 남아 있다. 현재 actor join, stream reply, Spot create/join
  public surface 가 raw `message_t` 중심이므로 typed lifecycle metadata 설계가 필요하다.
- .NET Bingo draw timer 는 기존 `[ZLinkSpotTimerHandler(name, periodMilliseconds)]` attribute
  로 자동 등록하도록 전환했다. 다만 attribute 는 timer options 를 담지 못하므로,
  `DelayNextTick`과 `StopOnUnhandledException` 같은 policy 를 metadata 로 표현하는 문제는
  별도 설계 이슈로 남는다.

따라서 “helper confinement gate 통과”와 “샘플 업무 코드에서 codec helper 제거 완료”를
동일하게 보지 않는다. 완료 선언은 아래 공통 grep gate 가 예외 경계까지 함께 해소되고,
Bingo generated Protobuf 계약이 언어별로 실제 빌드 산출물과 샘플 type 사용에서 확인된 뒤에만
가능하다.

### 6.1 공통 grep gate

샘플 업무 코드에서 아래 패턴이 남아 있으면 실패로 본다.

```text
createProtobufMessage
createProtobufReplyMessage
decodeBingoChannelReply
bingoChannelHandlerOptions
submit<Buffer>
bingoPayloadBase64
toBingoProto
fromBingoProto
ZLinkStreamProtobuf.decode
ZLinkStreamJson.decode
ZLinkStreamMessagePack.decode
ObjectMapper.readValue(request.toByteArray
readValue(request.toByteArray
json.writeValueAsBytes
writeValueAsBytes
decode(request,
.FromJson<
.ToJson(
.FromProto<
.ToProto(
json_to_protobuf_payload
json_from_protobuf_payload
to_stream_payload
from_stream_payload
```

예외:

- codec package 내부 구현
- codec unit tests
- 문서에서 나쁜 예시로 명시한 코드
- HTTP API body 처리를 위한 `ReadFromJsonAsync`, `ObjectMapper` 사용
- config 파일 JSON read/write

grep gate 는 단순 문자열 검사만으로 끝내지 않는다. 각 언어의 sample release gate 나
contract test 에서 sample 업무 코드 경로를 대상으로 실행하고, wrapper 함수 이름 때문에
직접 decode/encode 가 숨어 있지 않은지 코드 리뷰로 한 번 더 확인한다.

2026-06-16 현재 focused grep 결과:

- Node Bingo:
  - `createProtobufMessage`, `readProtobufMessage`, `decodeBingoChannelReply`,
    `bingoChannelHandlerOptions`, `submit<Buffer>().then(decode...)`는 현재
    `framework/languages/node/samples/Bingo.Ts` 아래에 남아 있지 않다.
  - `bingoMessage`와 `readBingoMessage` helper export 는 제거했다. 이 둘은 현재 참조가 없고,
    다시 샘플 업무 코드가 Protobuf helper 를 직접 호출하게 만들 여지만 남긴다.
  - `bingoPayloadBase64`는 `BingoNotificationsHandler`에 남아 있다. notification batch 가
    `payloadBase64`를 전송 계약으로 들고 있기 때문에, 이 호출은 단순 import 정리로 제거할
    수 없다. generated type metadata 로 notification payload encode 를 runtime 에 맡기는
    설계 전까지 완료로 보지 않는다.
  - `toBingoProto`는 `Server/Session/main.ts` raw stream bridge 와
    `Shared/Contracts/protobuf-codec.ts` 내부에 남아 있다.
  - `fromBingoProto`는 `Server/Session/main.ts` raw stream bridge 에 남아 있다.
  - `protobuf-codec.ts`는 아직 `protobufjs.loadSync(...)`, `lookupType(...)`,
    `fromObject(...)`, `toObject(...)` reflection 경로를 쓴다. 이 상태는 generated Protobuf
    type 전송 계약 완료가 아니다.
- .NET Bingo/TicTacToe:
  - Bingo Spot/session/room allocation 경계에 `.ToProto()` / `.FromProto<T>()`가 남아 있다.
  - TicTacToe Spot/actor join 경계에 `.ToJson()` / `.FromJson<T>()`가 남아 있다.
  - 기존 public surface 에 typed Spot create/join/session packet metadata 가 없으므로 설계
    이슈와 연결한다.
- Java/Kotlin Bingo/TicTacToe:
  - Java Bingo와 Kotlin Bingo는 아직 generated Protobuf type 을 전송 계약으로 사용하지 않는다.
    Java는 hand-written `Messages.java` record 계약, Kotlin은 hand-written `Messages.kt`
    data class 계약을 사용한다.
  - Java/Kotlin Bingo raw session/Spot lifecycle 경계에는 아직 `ZLinkStreamProtobuf.decode`,
    `ObjectMapper.readValue`, `Message.from(json.writeValueAsBytes(...))` 계열이 남아 있다.
    generated type 도입과 raw lifecycle 수신 경계의 codec helper 제거는 완료되지 않았다.
  - Java/Kotlin TicTacToe raw session/Spot lifecycle 경계에는 아직 `ZLinkStreamJson.decode`,
    `ObjectMapper.readValue`, `Message.from(json.writeValueAsBytes(...))`가 남아 있다.
  - actor join 호출부 중 이미 typed `joinSpot(..., dto).await(Type)`가 있는 경로는 유지하고,
    남은 TicTacToe raw lifecycle 수신 경계는 별도 metadata 설계 후 제거한다.
- C++ Bingo/TicTacToe:
  - Spot/session 경계와 shared contracts 에 `to_stream_payload`, `from_stream_payload`,
    `json_to_protobuf_payload`, `json_from_protobuf_payload`가 남아 있다.
  - C++ Bingo generated Protobuf 전환 전에는 완료로 보지 않는다.

2026-06-16 contract gate 조정:

- Java `SampleReleaseGateContractTest`가 TicTacToe session auth handler 에
  `ZLinkStreamJson.decode`가 반드시 있어야 한다고 검사하던 조건을 제거했다.
- 이 변경은 helper 제거가 완료됐다는 뜻이 아니다. 현재 Java/Kotlin session handler public
  surface 는 raw `Message`이므로 decode 호출은 아직 남을 수 있다. 다만 contract test 가 금지
  예정 패턴을 표준 사용 방식처럼 강제하지 않도록 바로잡은 것이다.
- Node `sample-regression.test.js`가 Bingo session bridge 의 `fromBingoProto` /
  `toBingoProto`와 `BingoPayloadEnvelope`를 반드시 요구하던 조건을 제거했다. 이 이름들은 현재
  raw session 경계와 reflection 기반 Protobuf 구현의 중간 상태일 뿐, 최종 표준 표면이 아니다.
  대신 `writeVarint`, `readVarint`, `schemaTable`, `manualSchema`, `wireType` 같은 수동 Protobuf
  구현 흔적을 금지 패턴으로 추가했다.
- Node `sample-regression.test.js`는 이제 `createProtobufMessage`, `readProtobufMessage`,
  `decodeBingoChannelReply`, `bingoChannelHandlerOptions`, `submit<Buffer>().then(decode...)`
  패턴이 Bingo 샘플에 다시 생기면 실패한다. `fromBingoProto` / `toBingoProto`는 현재 raw
  `Server/Session/main.ts` bridge 와 `Shared/Contracts` codec 내부에만 남기는 중간 안전장치다.

### 6.2 Node 검증

- `cd framework/languages/node && npm run build`
- `cd framework/languages/node && npm run typecheck`
- `cd framework/languages/node && node --test test/contract/sample-regression.test.js`
- `cd framework/languages/node && ./samples/run_samples.sh`

2026-06-16 현재 확인:

- `npm run typecheck`: 통과.
- `npm run build`: 통과.
- `node --test test/contract/sample-regression.test.js`: 통과.
- `./samples/Bingo.Ts/run_sample.sh`: 통과.
- gate 조정 후 `node --test test/contract/sample-regression.test.js`: 통과.
- raw lifecycle/session helper 허용 범위 gate 추가 후
  `node --test test/contract/sample-regression.test.js`: 통과.
- 2026-06-16 재확인:
  `cd framework/languages/node && npm run build`: 통과.
- 2026-06-16 재확인:
  `cd framework/languages/node && npm run typecheck`: 통과.
- 2026-06-16 재확인:
  `cd framework/languages/node && node --test test/contract/sample-regression.test.js`: 통과.
- `node --test test/contract/channel-client.test.js`: 통과.
  이 gate 에 `ZLinkFrameworkRuntimeHost uses channel serializer registry for typed request replies`
  를 추가했다. 이 테스트는 기존 typed `requestToChannel(...).submit<T>()` 경로가 serializer
  registry 를 타고 handler 에 decoded object 를 넘기며, client 에 typed reply 를 바로 돌려주는지
  확인한다. 단, 이 테스트는 `addSerializer(...)`로 등록한 serializer 경로를 고정하는
  중간 회귀 테스트다. `addProtobuf()`만으로 generated Protobuf serializer 를 자동 연결하는
  최종 상태는 아직 별도 구현 대상이다.

추가할 테스트:

- Node에 이미 공개된 표준 메시지 factory, bindings codec extension, 또는 typed 호출이
  packet name registry 로 Protobuf encode 되는 테스트.
- Node에 이미 공개된 표준 메시지 factory, bindings codec extension, 또는 typed 호출이
  JSON registry 로 encode 되는 테스트.
- `Message.from(request)` 경로가 object payload 를 즉시 직렬화하고 원본 객체를 보관하지
  않는 테스트.
- packet name 을 type/generated metadata 에서 알 수 있으면 fluent `packetName(...)`
  override 없이 request 가 전송되는 테스트.
- `submit<T>()`가 Buffer를 반환하지 않고 typed reply 를 반환하는 테스트.
- 샘플 source 에 `submit<Buffer>()`와 `.then(decode...)` 형태가 남지 않는 contract test.
- `@zlinkRequestHandler(group, packetName)`만으로 codec registry decode/encode 가 적용되는 테스트.

### 6.3 .NET 검증

- `dotnet test bindings/dotnet/codecs/Zlink.Codecs.Tests/Zlink.Codecs.Tests.csproj`
- `dotnet build framework/languages/dotnet/Zlink.Framework.sln`
- `framework/languages/dotnet/samples/Bingo/run_sample.sh`
- `framework/languages/dotnet/samples/TicTacToe/run_sample.sh`
- 필요 시 `framework/languages/dotnet/samples/run_samples.sh`

2026-06-16 현재 확인:

- `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj
  --no-build --filter "FullyQualifiedName=Zlink.Framework.UnitTests.Samples.RegressionTests.Bingo_Uses_Protobuf_And_TicTacToe_Uses_Json_Sample_Payloads"`:
  통과.
- 위 regression gate 는 Bingo Shared project 의 `Google.Protobuf`, `Grpc.Tools`,
  `<Protobuf Include="Contracts\bingo_messages.proto" GrpcServices="None" />` 등록과
  Shared/Contracts 아래 hand-written request/reply record 부재를 함께 확인한다. 따라서 .NET
  Bingo의 generated Protobuf 계약 회귀를 잡는다.
- 같은 regression gate 는 `.ToJson()`, `.FromJson<T>()`, `.ToProto()`, `.FromProto<T>()`가
  현재 raw Spot/session lifecycle 경계 밖으로 퍼지면 실패한다. HTTP API body 처리의
  `ReadFromJsonAsync`는 이 gate 대상이 아니다. 이 gate 는 helper 제거 완료가 아니라, 기존
  public surface 로 제거할 수 없는 raw lifecycle 범위를 고정하는 중간 안전장치다.
- 2026-06-16 재확인: .NET Bingo/TicTacToe의 `RequestToChannel(...).Async<T>()`와 client
  stream request 는 typed reply 를 직접 반환한다. `.ToProto()` / `.FromProto<T>()` /
  `.ToJson()` / `.FromJson<T>()`는 `IZLinkSessionPacketHandler.HandleAsync`, Spot
  `OnActorJoinAsync`, `IZLinkActorContext.JoinSpot(Message)`,
  `IZLinkSpotManager.CreateAsync(Message)`처럼 현재 public signature 가 raw `Message`인 경계에만
  남아 있다. 이 범위는 새 public overload 없이 제거하지 않는다.
- raw helper confinement gate 추가 후
  `dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj
  --filter "FullyQualifiedName=Zlink.Framework.UnitTests.Samples.RegressionTests.Bingo_Uses_Protobuf_And_TicTacToe_Uses_Json_Sample_Payloads"`:
  통과.
- 2026-06-16 재확인:
  `dotnet test bindings/dotnet/codecs/Zlink.Codecs.Tests/Zlink.Codecs.Tests.csproj`:
  통과. 결과는 총 5개 테스트 통과다.
- 2026-06-16 재확인:
  `dotnet build framework/languages/dotnet/Zlink.Framework.sln`:
  통과. 새 오류는 없고 기존 샘플/test warning 만 남았다.
- 2026-06-16 재확인:
  `framework/languages/dotnet/samples/Bingo/run_sample.sh`:
  통과. 출력은 `topology=ready`, `bingo=completed`를 포함한다.
- 2026-06-16 추가 확인:
  .NET Bingo draw timer 는 `BingoRoomDrawTimerHandler`의
  `[ZLinkSpotTimerHandler("bingo-draw", 200)]` attribute 로 자동 등록된다.
  `BingoRoom` 내부의 `Context.AddTimer<BingoRoomDrawTimerHandler>(...)` 직접 호출은 제거했고,
  수동 timer 시작용 `ShouldStartDrawTimer` 내부 플래그도 제거했다. 변경 뒤
  `dotnet build framework/languages/dotnet/samples/Bingo/Bingo.sln --no-incremental`과
  `framework/languages/dotnet/samples/Bingo/run_sample.sh`가 통과했다.
- 2026-06-16 추가 확인:
  .NET Bingo bound session notification send 는 packet name 이 notify type 이름과 같으므로,
  `BingoNotificationPublisher`의 반복 `.PacketName(SampleNames.*NotifyPacket)` override 를
  제거했다. `ZLinkBoundSessionSendCall<TMessage>`는 기본값으로 message type name 을 packet
  name 으로 사용한다. 변경 뒤 `framework/languages/dotnet/samples/Bingo/run_sample.sh`가
  `topology=ready`, `bingo=completed`로 통과했다.
- 2026-06-16 추가 확인:
  `dotnet test bindings/dotnet/codecs/Zlink.Codecs.Tests/Zlink.Codecs.Tests.csproj`,
  `dotnet build framework/languages/dotnet/Zlink.Framework.sln --no-incremental`,
  `framework/languages/dotnet/samples/TicTacToe/run_sample.sh`를 순차 실행했고 모두 통과했다.
  병렬 실행하면 같은 obj/bin 산출물을 동시에 만져 `ref/Systems.Zlink.dll` 누락이나
  `CreateAppHost` apphost 오류가 날 수 있으므로 이 검증 묶음은 순차 실행한다.
- 2026-06-16 재확인:
  `framework/languages/dotnet/samples/TicTacToe/run_sample.sh`:
  통과. 출력은 `tictactoe=completed`를 포함한다.

추가할 테스트:

- `.NET`에 이미 공개된 표준 메시지 factory 또는 기존 typed 호출이 Protobuf registry 를
  타는 테스트.
- `.NET`에 이미 공개된 표준 메시지 factory 또는 기존 typed 호출이 JSON registry 를 타는 테스트.
- Spot create/join 기존 public signature 가 샘플 codec helper 없이 동작하는 테스트.
- 샘플 source grep contract.

### 6.4 Java/Kotlin 검증

- `cd framework/languages/java && ./gradlew test`
- `cd framework/languages/java/samples && ./run_samples.sh`

2026-06-16 현재 확인:

- `cd framework/languages/java && ./gradlew :zlink-framework-core:test --tests systems.zlink.framework.runtime.messaging.ZLinkProtobufMessageSerializerTest`:
  통과. 이 테스트는 generated `MessageLite` payload 가 real Protobuf bytes 로 serialize/deserialize
  되고, 전환 전 record/data class 는 JSON fallback 을 유지하는지 확인한다.
- `cd framework/languages/java && ./gradlew :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.ConnectorCodecContractTest`:
  통과. 이 테스트는 stream connector Protobuf helper 가 generated `MessageLite` payload 를 real
  Protobuf bytes 로 encode/decode 하는지 확인한다.
- 2026-06-16 현재 `SampleReleaseGateContractTest`에
  `codecHelpersStayConfinedToRawLifecycleBoundaries` gate 를 추가했다. 이 gate 는
  `ZLinkStreamProtobuf.decode`, `ZLinkStreamJson.decode`, `json.readValue(request.toByteArray...)`,
  `json.writeValueAsBytes(...)`가 현재 raw session/Spot lifecycle 경계 밖으로 퍼지면 실패한다.
  HTTP API body 처리와 config mapper 는 이 패턴에 포함하지 않는다. 이 gate 는 helper 제거 완료가
  아니라, 기존 public surface 로 제거할 수 없는 범위를 고정하는 중간 안전장치다.
- 2026-06-16 재확인:
  `cd framework/languages/java && ./gradlew test`:
  통과. framework core, Kotlin wrapper, Spring starter, stream connector, connector codec
  테스트가 통과했다.
- Java Bingo generated Protobuf build 확인:
  현재 확인 결과 `framework/languages/java/samples/java/Bingo/Shared/src/main/proto`가 없고,
  `Shared/build.gradle.kts`에 Protobuf generator 설정이 없다. 따라서 Java Bingo generated
  Protobuf build 는 아직 수행할 수 없으며, 완료 증거가 없다.
- Kotlin/Java adjacent sample build 확인:
  `cd framework/languages/java/samples && ./gradlew -Pzlink.useLocalBindings=true --no-daemon :kotlin:Bingo:Server:Play:build :kotlin:Bingo:Server:Session:build :kotlin:TicTacToe:Server:build :java:TicTacToe:Server:build`:
  Kotlin Bingo Server Play/Session 과 Java/Kotlin TicTacToe compile 단계는 통과했지만,
  `:kotlin:TicTacToe:Server:distTar`에서 `zlink-java-6.0.4.jar`를 TAR에 중복 추가하려는 문제로
  실패했다. 이 결과는 builder 표면 수정의 compile 확인으로만 사용하고, sample packaging 완료
  증거로는 사용하지 않는다.
- Kotlin Bingo generated Protobuf build 확인:
  현재 확인 결과 `framework/languages/java/samples/kotlin/Bingo/Shared/src/main/kotlin/.../Messages.kt`
  data class 계약이 남아 있고 Java generated `Messages` type 을 재사용하지 않는다. 따라서 Kotlin
  Bingo generated Protobuf build 는 아직 완료 증거가 없다.
- Java/Kotlin Bingo 금지 helper grep 확인:
  `rg -n 'ZLinkStreamProtobuf\.decode|ZLinkStreamJson\.decode|ObjectMapper|readValue|writeValueAsBytes|createProtobufMessage|decodeBingoChannelReply|submit<Buffer>' framework/languages/java/samples/kotlin/Bingo framework/languages/java/samples/java/Bingo -S`:
  현재 `ZLinkStreamProtobuf.decode`, `ObjectMapper`, `readValue`, `writeValueAsBytes` 결과가
  남는다. 이 grep 은 아직 실패 상태다.
- Java sample runner 재확인:
  `cd framework/languages/java/samples && ./run_samples.sh`:
  실패. 실패한 gate 는 `SampleReleaseGateContractTest.requiredSamplesExposeExecutableEntryPoints()`이고
  정확한 메시지는 `missing standalone.settings.gradle.kts for kotlin/SupportChat`이다.
  현재 `framework/languages/java/samples/kotlin/SupportChat`는 entrypoint 파일이 없는 스텁이므로,
  이 실패는 Bingo/TicTacToe codec/generated Protobuf 전환 실패가 아니라 별도 샘플 구조 drift 로
  분리한다.
- Java codec helper confinement gate 재확인:
  `cd framework/languages/java && ./gradlew :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest.codecHelpersStayConfinedToRawLifecycleBoundaries`:
  통과. 이 결과는 남은 helper 호출이 현재 문서화한 raw lifecycle 경계 밖으로 퍼지지 않았다는
  중간 안전장치다.

추가할 테스트:

- `SampleReleaseGateContractTest`에 최종 표준 인터페이스 밖 codec helper 직접 호출을 실패로 보는
  검출 규칙 추가.
- Java 기존 session packet handler 가 codec registry 를 타는 contract test.
- Kotlin coroutine session packet handler 가 codec registry 를 타는 contract test.
- Spot create/join 기존 public signature 가 샘플 codec helper 없이 동작하는 contract test.
- Protobuf generated type 사용 여부 gate.

### 6.5 C++ 검증

- `cmake --build framework/languages/cpp/build`
- `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)' --output-on-failure`
- 필요 시 `framework/languages/cpp/samples/run_samples.sh`

2026-06-16 현재 확인:

- `cmake --build framework/languages/cpp/build`: 통과.
- `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)' --output-on-failure`: 통과.
- 단, 이 결과는 현재 sample smoke 와 parity target 이 통과한다는 뜻이다. C++ Bingo가 generated
  Protobuf type 을 전송 계약으로 사용한다는 증거는 아니다. `to_stream_payload`,
  `from_stream_payload`, `json_to_protobuf_payload`, `json_from_protobuf_payload`는 아직 남아
  있으므로 codec 메시지 표면 정리 완료로 보지 않는다.
- 2026-06-16 현재 `test_cpp_framework_sample_parity`에
  `CodecHelpersStayConfinedToRawLifecycleBoundaries` gate 를 추가했다. 이 gate 는
  `to_stream_payload`, `from_stream_payload`, `json_to_protobuf_payload`,
  `json_from_protobuf_payload`, 수동 Protobuf varint helper 가 Shared contracts 와 현재 raw
  Spot/session lifecycle 경계 밖으로 퍼지면 실패한다. 이 gate 는 helper 제거 완료가 아니라,
  기존 public surface 로 제거할 수 없는 범위를 고정하는 중간 안전장치다.
- `cmake --build framework/languages/cpp/build --target test_cpp_framework_sample_parity`: 통과.
- `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_sample_parity' --output-on-failure`:
  통과.
- 2026-06-16 재확인:
  `cmake --build framework/languages/cpp/build`: 통과.
- 2026-06-16 재확인:
  `ctest --test-dir framework/languages/cpp/build -R 'test_cpp_framework_sample_parity|sample_smoke_sample_cpp_framework_(bingo|tictactoe)' --output-on-failure`:
  통과. 7개 테스트가 모두 통과했다.
- 2026-06-16 재확인:
  C++ Protobuf generator 환경은 `protoc 3.21.12`로 확인했다. 하지만 C++ CMake에는 Protobuf
  생성 규칙과 generated source link 규칙이 없고, Bingo sample tree 에는 generated
  `bingo_messages.pb.h` / `bingo_messages.pb.cc`가 없다. 이 상태는 smoke 통과와 별개로
  generated Protobuf 계약 완료가 아니다.

추가할 테스트:

- sample parity test 에 최종 표준 인터페이스 밖 `to_stream_payload/from_stream_payload` 직접 사용을
  실패로 보는 gate 추가.
- Protobuf 샘플이 JSON 문자열 wrapper 를 사용하지 않는지 최종 grep gate 추가.
- Spot/session 기존 public signature 가 샘플 codec helper 없이 동작하는 contract test.

## 7. 완료 조건

- Bingo는 모든 언어에서 Protobuf payload 를 사용한다.
- TicTacToe는 모든 언어에서 JSON payload 를 사용한다.
- MessagePack도 이미 공개된 표준 메시지 factory 또는 typed submit 표면으로 추가할 수
  있어야 한다.
- 7개 bindings codec extension 과 framework typed 호출의 현재 public surface 를 확인하고,
  각 언어가 object payload 즉시 직렬화와 packet metadata 를 어떤 기존 표면으로 제공하는지
  문서에 남긴다.
- 샘플 업무 코드에는 codec 별 helper 호출이 없다.
- 샘플 업무 코드의 request 호출은 raw `Buffer`를 반환받지 않는다. `submit<TReply>()`가
  최종 reply 값을 바로 반환해야 한다.
- packet name 과 request/reply type 등록이 codec 선택의 단일 기준이다. packet name 을
  type/generated metadata 에서 확정할 수 있으면 샘플 호출부는 fluent override 를 반복하지
  않는다.
- `.proto`와 POJO/record/data class 가 같은 전송 계약으로 중복되지 않는다.
- 언어별 sample runner 와 contract test 가 통과한다.
- Bingo timer handler 는 각 언어의 기존 public metadata 표면으로 자동 등록하는 방향을 우선한다.
  다만 기존 timer annotation/attribute 가 timer options 나 동적 period 를 표현하지 못하는
  언어에서는 의미를 잃는 전환을 하지 않고 별도 설계 이슈로 분리한다.
- 구현 완료 후 Codex 에이전트를 별도로 실행해 이 문서의 수정 대상, 금지 패턴, public API
  변경 금지 조건, 언어별 검증 명령이 실제 결과와 맞는지 리뷰한다.
- Codex 에이전트 리뷰에서 나온 blocking finding 은 완료 상태로 인정하기 전에 수정하거나,
  별도 이슈로 분리해야 하는 이유와 후속 계획을 문서에 남긴다.

## 8. 별도 설계 이슈

아래 항목은 단순 샘플 수정으로 끝내면 안 된다. 기존 public signature 를 바꾸지 않는 조건에서
runtime 이 어떤 metadata 로 request/reply type 과 codec 을 알 수 있는지 먼저 정해야 한다.

### 8.1 Node.js Spot create/join typed lifecycle

현재 공개 표면:

- `ZLinkSpot.onCreate(request: Message, ...)`
- `ZLinkSpot.onActorJoin(actor, request: Message, ...)`
- `ZLinkActorContext.joinSpot(spotRid, request?: Message)`
- `ZLinkActorJoinSpotCall.submit(): Promise<ZLinkActorJoinResult>`

문제:

- `submit<TReply>()`가 없고 reply 는 `Message`로만 돌아온다.
- request 도 `Message`만 받으므로 object payload 를 framework serializer 로 넘길 공개 경로가 없다.
- 이 상태에서 샘플 업무 코드의 `request.value<T>()`, `JSON.parse`, `JSON.stringify`,
  stream bridge codec helper 를 제거하려고 하면 새 helper 를 숨기거나 public API 를 늘리게 된다.

후속 방향:

- 기존 public signature 는 유지한다.
- Spot lifecycle 별 request/reply type metadata 를 registration 내부에 어떻게 줄지 설계한다.
- 그 전에는 Node Spot create/join typed lifecycle 전환을 완료로 표시하지 않는다.

### 8.2 Node.js stream session typed packet handler

현재 공개 표면:

- `ZLinkSessionPacketHandler.handle(context, header, payload: Message)`

문제:

- handler generic 에 request/reply type 이 없다.
- `header.name`은 packet name 만 제공하고, payload type 과 reply type 은 registration 에 없다.
- 샘플 session code 에서 `fromBingoProto`, `toBingoProto`, `JSON.parse`를 없애려면 typed session
  packet dispatch metadata 가 필요하다.

후속 방향:

- stream packet handler registration 에 packet name, request type, optional reply type 을
  내부 metadata 로 보관하는 방식을 설계한다.
- 새 public callback overload 를 만들지 않는다.

### 8.3 .NET Spot/session raw `Message` lifecycle

현재 공개 표면:

- `IZLinkSpot.OnCreateAsync(Message request, ...)`
- `IZLinkSpot<TActor>.OnActorJoinAsync(TActor actor, Message request, ...)`
- `IZLinkActorContext.JoinSpot(RoutingId spotRid, Message request)`
- `IZLinkSessionPacketHandler<T>.HandleAsync(..., Message payload, ...)`

문제:

- actor join call 은 typed reply 를 반환하지 않고 `Message Reply`를 반환한다.
- session packet handler 는 request type metadata 를 노출하지 않는다.
- 샘플 업무 코드에서 `.FromJson<T>()`, `.ToJson()`, `.FromProto<T>()`, `.ToProto()`를 제거하려면
  runtime 이 lifecycle 별 request/reply type 을 알아야 한다.

후속 방향:

- 기존 actor packet handler 와 channel handler 처럼 generic type 이 이미 드러나는 경로를 먼저
  registry 로 연결한다.
- Spot create/join, session packet handler 는 registration metadata 설계를 먼저 한다.

### 8.4 Java/Kotlin raw Spot/session lifecycle

현재 공개 표면:

- `ZLinkSpot.onCreate(Message request)`
- `ZLinkSpot.onActorJoin(ZLinkActor actor, Message request, ...)`
- `ZLinkSessionPacketHandler.handle(..., Message payload)`

문제:

- actor `joinSpot(RoutingId, Object).await(Reply.class)`는 이미 serializer 를 사용하지만,
  수신 쪽 Spot lifecycle 은 raw `Message`만 받는다.
- session packet handler 도 raw `Message`만 받으므로 stream helper decode 가 샘플에 남는다.

후속 방향:

- Java/Kotlin actor join 호출부는 기존 typed call 로 정리한다.
- Spot/session 수신 경계는 registration metadata 설계를 먼저 한다.

### 8.5 C++ generated Protobuf 도입

현재 문제:

- Bingo C++은 `.proto` 파일이 있지만 전송 계약은 generated type 이 아니라 `messages.hpp`의
  JSON DTO와 수동 `json_to_protobuf_payload` wrapper 에 의존한다.
- 이 상태를 Protobuf payload 완료로 인정하지 않는다.
- 2026-06-16 현재 작업 환경에는 `protoc 3.21.12`, `libprotobuf`, C++ Protobuf headers 가
  있다. 따라서 도구 부재 때문에 불가능한 상태는 아니다.
- 현재 `framework/languages/cpp/CMakeLists.txt`의 sample target 은
  `samples/Bingo/Shared/Contracts/bingo_messages.proto`를 generated source 로 빌드하지 않는다.
  sample target 도 generated include/source 를 링크하지 않는다.
- C++ Protobuf codec extension 은 `zlink::message_t::from_protobuf(value)`와
  `parse_protobuf<T>()`를 제공하므로, generated type 을 도입하면 framework serializer registry 로
  연결할 수 있는 기반은 있다.

후속 방향:

- CMake 에 Protobuf generator 를 넣을 수 있는지 먼저 결정한다.
- generator 도입 자체는 가능하지만, hand-written `messages.hpp` struct 를 generated message 로
  치환하면 client, channel handler, Spot, stream session, notification code 전반의 전송 계약이
  바뀐다. 이 변경은 단순 helper 제거가 아니라 C++ Bingo 계약 전환 작업이다.
- 이번 구현에서 이 전환을 끝까지 수행하지 못하면 C++ Bingo Protobuf 전환을 별도 하위 계획으로
  분리하고, 현재 JSON wrapper 를 완료로 표시하지 않는다.

### 8.6 Bingo timer metadata options

현재 문제:

- Node는 `@zlinkSpotTimerHandler({ spot, name, periodMs, options })`가 있어 Bingo timer 를
  handler metadata 로 자동 등록할 수 있다.
- .NET은 `[ZLinkSpotTimerHandler(name, periodMilliseconds)]`가 있지만
  `ZLinkTimerOptions`를 attribute 에 담지 못한다. 2026-06-16 현재 Bingo는 수동
  `Context.AddTimer<BingoRoomDrawTimerHandler>(...)` 등록을 제거하고 attribute 자동 등록을
  사용한다. handler 는 준비 전 tick 을 no-op 으로 처리한다.
- Java/Kotlin은 `@ZLinkSpotTimer(name, periodMillis)`가 있지만 `ZLinkTimerOptions`와 동적
  period 를 담지 못한다. 2026-06-16 현재 Java/Kotlin Bingo는 기본 timer options 와 고정
  `SampleTimings.DrawPeriodMillis`만 필요하므로 handler annotation 으로 자동 등록한다.
- Java runtime 의 scanned timer 는 Spot handler registration 시점에 등록되고, 수동 timer 는
  `onInitialize`에서 등록된다. Bingo tick 이 준비 전 no-op 이더라도 등록 시점 차이는 문서화된
  의미 차이다.

후속 방향:

- timer options 가 필요 없는 샘플은 기존 annotation/attribute 를 사용한다.
- 2026-06-16 검증: Java/Kotlin Bingo Play 샘플은 이 규칙에 따라 수동 timer 등록을 제거했고,
  `./gradlew :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest`
  및 `./gradlew -Pzlink.useLocalBindings=true --no-daemon :java:Bingo:Server:Play:build
  :kotlin:Bingo:Server:Play:build`를 통과했다.
- Bingo처럼 options 나 설정 기반 period 를 유지해야 하는 샘플은 기존 public surface 만으로
  자동 등록 전환이 의미 보존인지 먼저 검증한다. .NET Bingo는 샘플 완료 흐름을
  `run_sample.sh`로 검증했지만, timer options 를 attribute metadata 로 표현하는 계약은 아직
  없다.
- 필요한 경우 timer metadata 에 options/default policy 를 연결하는 설계를 별도 draft 로
  작성한다. 단, 이번 codec 표면 정리 작업을 위해 새 public attribute parameter 나 overload 를
  즉흥적으로 추가하지 않는다.

## 9. 위험과 순서상 주의점

- Node는 현재 잘못된 Protobuf 수동 구현이 깊게 들어가 있으므로, 단순 grep 치환으로는
  정리되지 않는다. 먼저 framework codec registry 경로를 제대로 연결해야 한다.
- Java/Kotlin은 같은 core runtime 을 공유하므로 기존 public API 를 바꾸지 않고 Java
  runtime 내부 표준 경로를 먼저 안정화한 뒤 Kotlin wrapper 와 샘플을 옮겨야 한다.
- .NET은 기존 typed channel overload 가 많으므로 raw Spot/session 경계를 우선 공략한다.
- C++은 generated Protobuf 도입 범위를 먼저 결정해야 한다. generator 도입이 큰 작업이면
  plan 을 한 번 더 쪼개되, 현재 JSON 문자열 wrapper 는 완료 상태로 인정하지 않는다.
- 현재 워킹트리에 관련 없는 변경이 많으므로, 구현 단계에서는 언어별로 좁게 stage 하고
  검증해야 한다.
