# Framework Message Codec 경계 정리 계획

## 목적

framework는 codec extension을 통해 JSON, Protobuf, MessagePack, 사용자 정의 codec을 바꿔 쓸 수
있다. 그런데 일부 SPOT create, session callback, actor join API는 bindings 라이브러리의 raw `Message`를
업무 API로 직접 노출한다. 이 경로에서는 사용자가 codec별 `encode` / `decode` helper를 직접 호출해야
하므로, codec을 바꾸면 업무 코드도 같이 바뀐다.

이 계획의 목적은 raw `Message`를 framework 내부 경계로 밀어 넣고, 업무 API에서는 framework가
소유한 `ZLinkMessage` 또는 typed payload API를 사용하도록 정리하는 것이다. codec 선택과
payload 변환은 framework runtime이 등록된 codec registry를 기준으로 수행한다.

이 문서는 `framework-codec-extension-unification-plan.ko.md`의 후속 계획이다. codec extension
등록 계약은 기존 계획을 따르고, 이 문서는 그 codec registry를 SPOT create, session, actor join, sample,
문서 표면까지 일관되게 적용하는 방법을 다룬다.

이 계획의 전제는 선행 계획에서 정의한 codec registry 계약이 확정되어 있고, framework가 그 registry를
사용해 payload serializer를 찾을 수 있다는 점이다. 선행 registry 계약이 아직 머지되지 않은 언어가
있으면 이 계획을 먼저 적용하지 않고, 해당 언어의 registry 계약을 먼저 끝낸다.

기존 codec 통합 계획은 handler method, request method, reply type, payload DTO 같은 업무 API를
codec 변경 때문에 바꾸지 않는다는 원칙을 둔다. 이 문서는 그 원칙을 유지하되, 이미 raw bindings
`Message`를 업무 API로 노출한 예외 표면은 정리 대상으로 본다. 따라서 이 문서의 변경은 codec별
helper를 없애기 위한 API 정리이며, codec마다 다른 DTO나 helper를 추가하는 변경이 아니다.

codec extension과 custom codec의 등록 방식은 이 계획에서 바꾸지 않는다. 기존처럼 application startup의
options 또는 builder에서 codec extension을 추가하면 같은 registry를 통해 동작해야 한다. 바뀌는 것은
codec 등록 표면이 아니라, SPOT create, session dispatch, actor join 업무 코드가 raw `Message`나
codec별 helper를 직접 쓰지 않게 되는 부분이다.

이 문서를 구현 지시로 사용할 때는 “기존 방식”이라는 표현을 항상 아래처럼 나누어 판단한다.

- 유지할 기존 방식: codec extension을 options, builder, application startup에 등록하는 방식
- 제거할 기존 방식: handler, actor, session, SPOT create 코드에서 raw payload를 직접 만들거나 해석하는 방식

즉 custom codec을 추가하는 사용법은 기존과 같아야 한다. 사용자는 codec extension을 등록한 뒤 같은
DTO handler, 같은 actor join 코드, 같은 session callback 코드를 사용한다. `Message.from(...)`,
`JsonMessageExtensions`, raw `Buffer`, `zlink_msg_t` 같은 타입과 helper가 업무 코드에 남아 있다면
이 계획을 끝낸 것으로 보지 않는다.

## 현재 상태

이 절은 2026-06-23 checkout 기준으로 확인한 기준 상태다. 이후 구현 중 checkout이 바뀌면 먼저 이
상태를 다시 확인한다.

| 영역 | 확인한 상태 |
|------|-------------|
| .NET session | `IZLinkSession.OnDispatchAsync(...)`가 raw `Message` payload를 받는다. |
| .NET SPOT create | `IZLinkSpot.OnCreateAsync(...)`가 raw `Message` request를 받는다. |
| .NET actor join | `IZLinkActorContext.JoinSpot(...)` / `JoinEntrySpot(...)`이 raw `Message` request를 받는다. |
| Java session | raw `ZLinkSessionPacketHandler`와 typed `ZLinkTypedSessionPacketHandler`가 함께 있다. |
| Java actor join | `joinSpot(..., Object request)`와 typed `submit(Class<TReply>)`가 이미 있다. |
| Node actor join | `joinSpot(..., unknown request)`와 typed `submit<TReply>()`가 이미 있다. |
| framework message | C++, Java, Kotlin, Node.js, .NET 모두 아직 공통 `ZLinkMessage` 타입이 없다. |
| codec registry | 다섯 언어 모두 codec registry 또는 동등한 serializer 등록 인프라가 있다. |
| sample | 일부 sample이 `Message.from(...)`, JSON helper, codec별 helper를 업무 코드에서 직접 사용한다. |

## 문제 범위

아래 API는 사용자가 직접 raw `Message`를 다루게 만들기 때문에 codec 변경에 취약하다.

| 영역 | 현재 문제 | 개선 방향 |
|------|-----------|-----------|
| SPOT create | create hook이 raw request를 받거나 raw reply를 만든다. | create request/reply도 DTO 또는 `ZLinkMessage`로 처리하고 runtime이 codec registry를 사용한다. |
| session dispatch | session callback 또는 packet handler가 raw payload를 받는다. | framework `ZLinkMessage` 또는 typed session handler가 payload decode를 맡는다. |
| actor join request | actor가 `JoinSpot` / `JoinEntrySpot` 호출 시 raw request를 넘긴다. | request DTO 또는 `ZLinkMessage`를 넘기고 runtime이 codec registry로 encode한다. |
| actor join reply | join 결과가 raw reply로 돌아온다. | typed result 또는 `ZLinkMessage` reply가 codec registry로 decode된다. |
| join handler request | SPOT actor join handler가 raw request를 받는다. | handler가 DTO 또는 `ZLinkMessage` request를 받고 runtime이 decode한다. |
| join handler reply | SPOT의 actor join handler가 raw reply를 만든다. | handler가 DTO 또는 `ZLinkMessage` reply를 반환하고 runtime이 encode한다. |
| sample | sample이 codec별 helper나 raw `Message.from(...)`를 업무 코드에 둔다. | sample은 DTO와 framework API만 사용하고 codec 차이는 구성 단계에 둔다. |

일반 channel handler, SPOT packet handler, actor packet handler, 일반 request/reply client 호출은 이미
typed payload를 기준으로 하는 경로다. 이 계획은 그 표면을 다시 설계하지 않는다. 범위는 raw bindings
`Message`가 새는 예외 표면인 SPOT create, session dispatch, actor join으로 제한한다.

raw `Message` 자체를 없애는 것은 목표가 아니다. 아래 경계에서는 raw payload가 여전히 필요하다.

- bindings와 backend socket을 직접 감싸는 runtime 경계
- stream frame writer와 frame reader
- actor relay와 bound session relay처럼 이미 만들어진 frame을 보존해야 하는 경계
- raw packet API처럼 사용자가 wire payload를 직접 다루겠다고 선택한 고급 표면
- 테스트에서 깨진 frame이나 codec 불일치를 의도적으로 주입하는 harness

반대로 SPOT create, session dispatch, actor join처럼 일반 사용자가 업무 DTO를 다루는 표면에서는
기존 raw 방식을 최종 상태로 남기지 않는다. 호환성 때문에 중간 단계에서 deprecated raw API를 둘 수는
있지만, 구현 완료 시점에는 sample, guide, contract test, 일반 public surface에서 raw 방식을 제거하는
방향으로 진행한다.

## 목표 공개 표면

### codec extension 사용법 유지

사용자는 기존처럼 options 또는 builder에서 codec extension을 등록한다. 이 plan을 적용한 뒤에도
아래 사용법은 같은 의미로 유지되어야 한다.

```csharp
builder.AddZLinkFramework(options =>
{
    options.Codecs.Use(ZLinkProtobufCodec.Default);
    options.Codecs.Use(ZLinkMessagePackCodec.Default);
    options.Codecs.Use(new MyCustomCodecExtension(...));
});
```

```java
ZLinkFramework.configure(options -> options
    .codecs(codecs -> codecs.use(ZLinkProtobufCodec.defaultCodec()))
    .codecs(codecs -> codecs.use(ZLinkMessagePackCodec.defaultCodec()))
    .codecs(codecs -> codecs.use(new MyCustomCodecExtension(...))));
```

```ts
zlinkFramework()
  .codecs((codecs) => codecs.use(zlinkProtobufCodec()))
  .codecs((codecs) => codecs.use(zlinkMessagePackCodec()))
  .codecs((codecs) => codecs.use(new MyCustomCodecExtension(...)));
```

```cpp
options.codecs()
  .use(zlink::framework::codecs::protobuf())
  .use(zlink::framework::codecs::message_pack())
  .use(my_custom_codec_extension{});
```

위 설정은 framework runtime, stream connector, HTTP client에서 선행 codec extension 계획이 정한 같은
registry 계약을 사용한다. 이 plan은 extension 등록 이름, options 위치, custom codec extension 작성
계약을 바꾸지 않는다. sample 차이도 dependency와 `Codecs.Use(...)` 또는 언어별 동등 호출에만 남아야
한다.

### 기존 방식과 변경 후 방식 구분

이 계획에서 “기존 방식”이라는 말은 두 가지를 반드시 구분해서 읽어야 한다.

첫째, codec extension을 등록하는 기존 방식은 유지한다. 사용자는 지금처럼 application startup,
builder, options에서 JSON, Protobuf, MessagePack, custom codec extension을 추가한다. 이 부분은
변경 대상이 아니다.

둘째, SPOT create, session dispatch, actor join의 업무 코드에서 bindings raw `Message`, raw `Buffer`,
`zlink_msg_t`, `Message.from(...)`, codec별 JSON helper를 직접 쓰는 기존 방식은 제거한다. 이 부분은
codec 변경 때마다 업무 코드가 같이 바뀌는 원인이므로 최종 public API와 sample에 남기지 않는다.

| 구분 | 변경 여부 | 기준 |
|------|-----------|------|
| codec extension 등록 | 유지 | application startup, builder, options에서 extension을 등록한다. |
| custom codec 작성 계약 | 유지 | 선행 codec extension 계획의 registry 계약을 그대로 따른다. |
| dependency 추가 방식 | 유지 | Protobuf, MessagePack, custom codec package 또는 module을 추가한다. |
| 업무 API의 raw payload 전달 | 제거 | SPOT create, session dispatch, actor join에서 raw `Message` / `Buffer` / `zlink_msg_t`를 넘기지 않는다. |
| 업무 코드의 codec별 helper 호출 | 제거 | handler, actor, sample에서 `Message.from(...)`, `JsonMessageExtensions`, 직접 serialize/deserialize를 쓰지 않는다. |
| wire-level raw API | 명시 raw surface만 유지 | relay, frame harness, backend boundary처럼 raw payload 보존이 목적일 때만 별도 raw 이름으로 남긴다. |

정리하면, codec을 추가하는 방법은 그대로 두고 payload를 다루는 업무 API만 바꾼다.

| 질문 | 답 |
|------|----|
| custom codec extension을 options에 등록하는 기존 코드는 바뀌는가? | 바뀌지 않는다. 기존 등록 코드는 계속 유효해야 한다. |
| custom codec extension 작성 계약이 바뀌는가? | 바뀌지 않는다. 선행 codec extension 계획의 registry 계약을 그대로 쓴다. |
| handler에서 `Message.from(...)`으로 직접 인코딩해도 되는가? | 안 된다. handler는 DTO 또는 `ZLinkMessage`를 사용한다. |
| session dispatch에서 raw `Message` / `Buffer`를 기본 payload로 받아도 되는가? | 안 된다. typed handler 또는 `ZLinkMessage`를 사용한다. |
| wire payload를 그대로 보존해야 하는 relay나 harness는 어떻게 하는가? | 명시 raw API로만 남긴다. 일반 업무 API와 sample의 기본 경로에서는 쓰지 않는다. |

유지되는 기존 방식은 아래처럼 codec extension을 등록하는 코드다. 이 코드는 변경 후에도 같은 의미로
동작해야 한다.

```csharp
builder.AddZLinkFramework(options =>
{
    options.Codecs.Use(new MyCustomCodecExtension(...));
});
```

```java
ZLinkFramework.configure(options -> options
    .codecs(codecs -> codecs.use(new MyCustomCodecExtension(...))));
```

```ts
zlinkFramework()
  .codecs((codecs) => codecs.use(new MyCustomCodecExtension(...)));
```

```cpp
options.codecs().use(my_custom_codec_extension{});
```

위 코드는 변경 후에도 그대로 유효해야 한다. custom codec을 추가하려면 application startup 또는
builder/options에서 extension을 등록한다. handler, actor, session, SPOT create 코드는 codec 종류를
직접 알 필요가 없어야 한다.

구현자는 아래 기준으로 코드를 판정한다.

| 코드 위치 | 기존 codec 등록 코드 허용 여부 | raw payload 코드 허용 여부 |
|-----------|-------------------------------|----------------------------|
| application startup / builder / options | 허용한다. 이 계획에서 바꾸지 않는다. | raw payload를 만들 필요가 없어야 한다. |
| framework runtime 내부 | 허용한다. registry 조회와 encode/decode를 담당한다. | 허용한다. 단 public 업무 API로 새면 안 된다. |
| SPOT create hook | codec 등록 코드를 두지 않는다. | 허용하지 않는다. request/reply는 DTO 또는 `ZLinkMessage`다. |
| session dispatch callback | codec 등록 코드를 두지 않는다. | 허용하지 않는다. typed payload 또는 `ZLinkMessage`를 받는다. |
| actor join caller / join handler | codec 등록 코드를 두지 않는다. | 허용하지 않는다. request/reply는 DTO 또는 `ZLinkMessage`다. |
| sample 업무 코드 | codec 종류별 helper를 두지 않는다. | 허용하지 않는다. 사용자가 따라 할 기본 예시이기 때문이다. |
| raw relay / frame harness | 보통 필요 없다. | 허용한다. 이름과 문서에서 raw 경계임을 드러낸다. |

제거되는 기존 방식은 아래처럼 업무 코드에서 raw payload를 직접 만들거나 해석하는 코드다. 이 코드는
“기존 codec 등록 방식”이 아니라 “기존 raw 업무 API 방식”이다. 최종 상태에서는 guide, sample, 일반
contract test에 남기지 않는다.

actor join에서 제거되는 방식:

```csharp
var request = Message.From(JsonSerializer.SerializeToUtf8Bytes(new JoinRoom("room-1")));
var result = await actor.Context.JoinSpot(roomRid, request).Async();
var reply = JsonSerializer.Deserialize<JoinedRoom>(result.Reply.Data);
```

```java
Message request = Message.from(jsonBytes);
ZLinkActorJoinResult result = actor.context().joinSpot(roomRid, request).submit().join();
JoinedRoom reply = JsonMessage.decode(result.reply(), JoinedRoom.class);
```

```ts
const request = Message.from(JSON.stringify({ roomId: "room-1" }));
const result = await actor.context.joinSpot(roomRid, request).submit();
const reply = JSON.parse(result.reply.toString());
```

session dispatch에서 제거되는 방식:

```csharp
public Task OnDispatchAsync(..., Message payload, ...)
{
    var packet = JsonSerializer.Deserialize<ChatPacket>(payload.Data);
    return HandleAsync(packet);
}
```

```java
session.onDispatch((metadata, message) -> {
    ChatPacket packet = JsonMessage.decode(message, ChatPacket.class);
    return handle(packet);
});
```

```ts
session.onDispatch((metadata, payload) => {
  const packet = JSON.parse(payload.toString()) as ChatPacket;
  return handle(packet);
});
```

SPOT create에서 제거되는 방식:

```csharp
public override Task<ZLinkSpotCreateResponse> OnCreateAsync(..., Message request, ...)
{
    var create = JsonSerializer.Deserialize<CreateRoom>(request.Data);
    var reply = Message.From(JsonSerializer.SerializeToUtf8Bytes(new RoomCreated(create.RoomId)));
    return Task.FromResult(ZLinkSpotCreateResponse.Accept(reply));
}
```

```java
spot.onCreate((request, context) -> {
    CreateRoom create = JsonMessage.decode(request, CreateRoom.class);
    return ZLinkSpotCreateResponse.accept(Message.from(JsonMessage.encode(new RoomCreated(create.roomId()))));
});
```

위 예시는 codec 선택과 payload 변환을 호출자 업무 코드로 밀어낸다. 이 방식은 JSON일 때만 자연스럽고,
Protobuf, MessagePack, custom codec으로 바꾸면 handler와 sample까지 같이 바뀐다. 따라서 최종 상태의
guide와 sample에는 이 방식을 남기지 않는다.

제거 대상은 예시와 같은 JSON 코드에만 한정하지 않는다. Protobuf, MessagePack, custom codec helper도
업무 코드에서 직접 호출하면 같은 문제다. 아래 형태는 모두 제거 대상이다.

- handler 안에서 `Message.from(...)`, `Buffer.from(...)`, `zlink_msg_t`를 직접 만드는 코드
- handler 안에서 `JsonSerializer`, `ObjectMapper`, `JSON.parse`, `JSON.stringify`로 wire payload를 직접
  변환하는 코드
- sample 업무 코드에서 Protobuf serializer, MessagePack serializer, custom serializer를 직접 찾아
  `encode` 또는 `decode` 하는 코드
- actor join reply를 raw message로 받은 뒤 sample이나 handler에서 직접 bytes를 읽는 코드
- session dispatch payload를 raw bytes로 받은 뒤 업무 코드에서 packet DTO로 바꾸는 코드

변경 후 업무 코드는 아래처럼 DTO 또는 `ZLinkMessage`만 다룬다. codec이 JSON인지 Protobuf인지
MessagePack인지 custom codec인지는 application startup의 extension 등록으로만 결정된다.

actor join:

```csharp
var result = await actor.Context
    .JoinSpot(roomRid, ZLinkMessage.From(new JoinRoom("room-1")))
    .Async();

var reply = result.Reply.Decode<JoinedRoom>();
```

```java
ZLinkActorJoinResult<JoinedRoom> result = actor.context()
    .joinSpot(roomRid, new JoinRoom("room-1"))
    .submit(JoinedRoom.class)
    .toCompletableFuture()
    .join();
```

```ts
const result = await actor.context
  .joinSpot(roomRid, { roomId: "room-1" })
  .submit<JoinedRoom>();
```

session dispatch:

```csharp
public Task OnDispatchAsync(..., ZLinkMessage payload, ...)
{
    var packet = payload.Decode<ChatPacket>();
    return HandleAsync(packet);
}
```

```java
session.onDispatch(ChatPacket.class, (metadata, packet) -> handle(packet));
```

```ts
session.onDispatch<ChatPacket>((metadata, packet) => handle(packet));
```

SPOT create:

```csharp
public override Task<ZLinkSpotCreateResponse> OnCreateAsync(..., ZLinkMessage request, ...)
{
    var create = request.Decode<CreateRoom>();
    return Task.FromResult(ZLinkSpotCreateResponse.Accept(new RoomCreated(create.RoomId)));
}
```

```java
spot.onCreate(CreateRoom.class, (create, context) ->
    ZLinkSpotCreateResponse.accept(new RoomCreated(create.roomId())));
```

언어별로 문법은 달라도 기준은 같다.

| 영역 | 제거되는 기존 방식 | 변경 후 방식 |
|------|--------------------|--------------|
| .NET actor join | `Message.From(...)`을 만든 뒤 `JoinSpot(..., Message)`에 넘긴다. | DTO 또는 `ZLinkMessage.From(dto)`를 넘기고 reply는 typed result나 `Decode<T>()`로 읽는다. |
| Java actor join | `Message.from(...)` 또는 JSON helper로 request/reply를 직접 변환한다. | `joinSpot(..., dto).submit(Reply.class)`를 기본으로 쓰고, 지연 decode가 필요할 때만 `ZLinkMessage`를 쓴다. |
| Kotlin actor join | Java raw `Message`나 sample helper를 감싸서 넘긴다. | Kotlin DTO 호출 또는 `messageOf(dto)` / `decode<T>()`를 쓴다. |
| Node.js actor join | `Buffer`나 binding message를 만들고 `JSON.parse(...)`로 reply를 읽는다. | plain object request와 typed `submit<T>()`를 기본으로 쓴다. |
| C++ actor join | `zlink_msg_t` 또는 raw buffer를 sample 업무 코드에서 직접 만든다. | DTO 또는 `zlink::framework::message`를 넘기고 runtime이 registry로 encode/decode한다. |
| session dispatch | callback이 raw payload를 받고 handler가 직접 parse한다. | typed session handler 또는 `ZLinkMessage` callback이 registry로 decode한다. |
| SPOT create | create hook이 raw request를 받고 raw reply를 만든다. | create request/reply를 DTO 또는 `ZLinkMessage`로 표현한다. |

변경 후에는 codec을 JSON에서 Protobuf, MessagePack, custom codec으로 바꿔도 위 업무 코드 모양이
바뀌지 않아야 한다. 바뀌는 곳은 dependency와 codec extension 등록 코드뿐이다. 이 원칙은 sample에도
동일하게 적용한다. sample에서 codec을 바꾸는 예시는 구성 코드만 바꾸고, handler와 actor 업무 코드는
그대로 두어야 한다.

### typed DTO와 ZLinkMessage 역할 분담

기본 업무 API는 typed DTO를 우선한다. 사용자가 request DTO, reply DTO, session packet DTO를 이미 알고
있다면 handler와 client는 DTO를 직접 주고받아야 한다. 이 경우 `ZLinkMessage`를 노출하지 않아도 된다.

`ZLinkMessage`는 아래 경우에만 사용한다.

- payload decode를 나중으로 미루고 싶은 경우
- payload를 decode하지 않고 다른 actor, session, raw relay 경계로 그대로 넘기는 경우
- reply type이 runtime에야 결정되는 경우
- content type 또는 stream codec id를 보고 application이 명시적으로 분기해야 하는 경우

따라서 Java와 Node.js처럼 이미 DTO request와 typed reply 표면이 있는 언어는 중복 overload를 늘리지
않는다. 그 언어들은 기존 typed 표면이 codec registry를 정확히 타도록 정리하고, `ZLinkMessage`는 위
예외 상황을 위한 보조 표면으로만 둔다.

### 공통 framework message

각 언어는 이름과 문법은 다를 수 있지만 같은 의미의 framework message 타입을 둔다.

| 언어 | 제안 이름 | 역할 |
|------|-----------|------|
| C++ | `zlink::framework::message` | DTO 또는 encoded payload를 들고 codec registry로 encode/decode한다. |
| Java | `ZLinkMessage` | DTO 또는 encoded payload를 들고 `ZLinkCodecRegistry`로 encode/decode한다. |
| Kotlin | `ZLinkMessage` + Kotlin extension | Java core 타입을 그대로 쓰되 `decode<T>()`, `messageOf(value)` 같은 Kotlin 문법을 제공한다. |
| Node.js | `ZLinkMessage<T = unknown>` | typed value 또는 encoded payload를 들고 runtime codec registry로 encode/decode한다. |
| .NET | `ZLinkMessage` | DTO 또는 encoded payload를 들고 `ZLinkCodecRegistryBuilder` 결과로 encode/decode한다. |

`ZLinkMessage`는 codec별 serializer를 public API로 드러내지 않는다. 사용자는 `ZLinkMessage.From(dto)`
또는 언어별 동등 factory를 호출하고, runtime은 보낼 때 등록된 codec registry로 wire payload를 만든다.
수신된 `ZLinkMessage`는 content type이나 stream codec id를 함께 보관하고, `Decode<T>()` 또는
언어별 동등 API에서 같은 registry로 DTO를 복원한다.

### codec 판별 메커니즘

`ZLinkMessage.Decode<T>()`가 어떤 codec을 쓸 수 있는지는 경로마다 다르다. 1단계에서 아래 표를 live
code로 다시 확인하고, 확인되지 않은 경로는 구현 전에 먼저 wire tag를 추가하거나 mismatch 테스트
범위를 조정한다.

| 경로 | 판별 근거 | 적용 규칙 |
|------|-----------|-----------|
| channel / route / actor envelope | envelope header의 content type 문자열 | 수신 content type과 맞는 serializer가 없으면 decode error를 낸다. |
| actor join envelope | join request/reply envelope의 content type 문자열 | 모든 local, remote, native, entry spot fallback 경로가 같은 content type을 보존해야 한다. |
| session / stream packet | stream header의 codec id가 있으면 그 값을 쓴다. 없으면 mismatch를 감지할 수 없다. | codec id가 없는 언어 또는 경로는 먼저 codec id 보존을 추가하거나 mismatch 테스트 대상에서 제외한다. |
| SPOT create | 기존 raw 경로에는 self-describing tag가 없을 수 있다. | create request/reply를 envelope 또는 동등한 tagged payload로 바꾼 뒤 `ZLinkMessage`를 적용한다. |
| raw relay / raw packet API | 판별하지 않는다. | raw API는 codec registry 대상이 아니며 payload bytes를 그대로 보존한다. |

### session API

session의 기본 업무 API는 raw payload를 직접 받지 않는다.

```csharp
ValueTask OnDispatchAsync(
    ZlinkStreamHeader header,
    ZLinkMessage payload,
    CancellationToken cancellationToken);
```

typed session handler가 있는 언어는 그 표면을 우선한다.

```java
public final class AuthenticateHandler
        implements ZLinkTypedSessionPacketHandler<SessionContext, AuthenticateReq> {
    public CompletionStage<Void> handle(
            SessionContext context,
            ZLinkStreamHeader header,
            AuthenticateReq request) {
        return context.client().reply(new AuthenticateRes(request.userId())).submit();
    }
}
```

raw packet handler는 고급 API로 남기되, 이름과 문서에서 raw payload를 직접 다루는 경계임을 분명히
한다. 일반 sample과 guide는 raw handler를 기본 예시로 쓰지 않는다.

### SPOT create API

SPOT create도 actor join과 같은 codec 경계를 따른다. create request와 reply는 raw `Message`가 아니라
DTO 또는 `ZLinkMessage`로 표현한다.

```csharp
ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
    ZLinkMessage request,
    CancellationToken cancellationToken);
```

typed create hook이 있는 언어는 create request DTO와 reply DTO를 handler signature에 직접 드러내고,
runtime이 encode/decode를 맡는다. raw create hook은 raw API로 분리하고 일반 guide와 sample에서는
사용하지 않는다.

### actor join API

actor join의 request와 reply는 raw `Message`가 아니라 DTO 또는 `ZLinkMessage`를 사용한다.

```csharp
var result = await actor.Context
    .JoinSpot(roomRid, ZLinkMessage.From(new JoinRoom("room-1")))
    .Async();

var reply = result.Reply.Decode<JoinedRoom>();
```

typed overload가 자연스러운 언어는 typed result도 제공한다.

```java
ZLinkActorJoinResult<JoinedRoom> result = actor.context()
    .joinSpot(roomRid, new JoinRoom("room-1"))
    .submit(JoinedRoom.class)
    .toCompletableFuture()
    .join();
```

기존 raw join API는 중간 단계에서만 deprecated raw surface로 분리한다. 이 계획의 최종 상태에서는
일반 public 업무 API, sample, guide에서 raw join 방식을 제거한다. raw join이 필요하면 명시 raw API로만
남기고, 기본 join API는 DTO 또는 `ZLinkMessage` 경로를 기준으로 정리한다.

## 공통 구현 원칙

1. codec 선택은 framework runtime 내부에서만 수행한다.
2. `ZLinkMessage`는 bindings `Message`를 public 업무 API로 재노출하는 얇은 별칭이 아니어야 한다.
3. outbound message는 DTO와 declared type을 보관하고, 실제 encode는 runtime이 codec registry를
   가진 시점에 수행한다.
4. inbound message는 encoded bytes와 content type 또는 stream codec id를 보관하고, decode 시
   registry를 사용한다.
5. raw `Message`가 필요한 API는 이름이나 namespace로 raw 경계임을 드러낸다.
6. sample, guide, E2E scenario는 codec별 helper 호출을 업무 코드에 두지 않는다.
7. 같은 기능을 언어마다 다른 호출 방식으로 만들지 않는다. 문법 차이는 허용하지만 책임 경계는 같아야 한다.
8. 기존 raw 업무 API는 유지가 아니라 제거 방향으로 진행한다. 단, wire-level raw API는 별도 이름으로 남길 수 있다.
9. 이 계획은 raw 예외 표면을 제거하는 breaking change를 허용하는 단계다. 기존 raw 업무 API 호환성은
   최종 목표가 아니며, 필요한 경우 중간 이행 단계에서만 deprecated raw surface를 둔다.
10. codec extension 등록 방식은 변경하지 않는다. options 또는 builder에서 extension을 추가하는 기존
    사용법이 그대로 동작해야 한다.

## 언어별 적용 계획

### .NET

대상 경로:

- `framework/languages/dotnet/src/Zlink.Framework/Contracts/Streams/`
- `framework/languages/dotnet/src/Zlink.Framework/Contracts/Actors/`
- `framework/languages/dotnet/src/Zlink.Framework/Contracts/Spots/`
- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Streams/`
- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Actors/`
- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Spots/`
- `framework/languages/dotnet/src/Zlink.Framework/Runtime/Messaging/`

작업:

1. `Zlink.Framework.Contracts.Messaging.ZLinkMessage`를 추가한다.
2. `ZLinkMessage.From<T>(T value)`와 `Decode<T>()`를 제공한다.
3. runtime 내부 encode/decode는 기존 `ZLinkEnvelopeCodec`과 `ZLinkStreamPacketPayloadCodec`을
   사용하되, `bodyType == typeof(Message)` 특례가 업무 API로 새지 않게 한다.
4. `IZLinkSession.OnDispatchAsync`와 `IZLinkSessionPacketHandler`에 `ZLinkMessage` 기반 overload를
   추가한다.
5. 기존 raw `Message` callback은 deprecated raw surface로 분리한 뒤 최종 제거한다. 새 dispatcher는
   `ZLinkMessage` callback을 기준으로 호출한다.
6. `IZLinkActorContext.JoinSpot` / `JoinEntrySpot`에 `ZLinkMessage` overload와 typed overload를
   추가한다.
7. `ZLinkActorJoinResult`에는 `ZLinkMessage Reply`를 제공하고, 기존 raw reply는 중간 이행용 raw surface로
   분리한 뒤 일반 업무 API에서 제거한다.
8. SPOT actor join handler와 join reply envelope가 `ZLinkMessage` 또는 DTO reply를 codec registry로
   encode하도록 바꾼다.
9. `IZLinkSpot.OnCreateAsync`와 `ZLinkSpotCreateResponse`도 `ZLinkMessage` 또는 DTO request/reply로
   정리한다.
10. `JsonMessageExtensions` 같은 codec별 helper는 raw/compat 문서로 내리고, guide 예시에서는 제거한다.

완료 기준:

- session callback에서 Protobuf 또는 MessagePack payload를 `payload.Decode<T>()`로 읽을 수 있다.
- actor join request와 reply가 custom codec으로 왕복한다.
- SPOT create request와 reply가 custom codec으로 왕복한다.
- raw relay API는 기존처럼 frame을 그대로 보존한다.
- 일반 public 업무 API에는 raw `Message`가 남지 않는다. raw가 필요한 곳은 명시 raw API로만 남는다.

### Java

대상 경로:

- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/streams/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/actors/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/spots/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/streams/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/actors/`
- `framework/languages/java/zlink-framework-core/src/main/java/systems/zlink/framework/runtime/messaging/`

작업:

1. `systems.zlink.framework.messaging.ZLinkMessage`를 추가한다.
2. `ZLinkMessage.of(Object value, Class<?> declaredType)`와 `decode(Class<T> type)`를 제공한다.
3. typed session handler가 이미 있으면 그 경로가 codec registry를 항상 타는지 점검하고, raw
   `Message` handler보다 우선하도록 정리한다.
4. `ZLinkSession.onDispatch(...)`의 raw payload 경로는 raw surface로 분리한 뒤 일반 업무 API에서 제거한다.
5. `ZLinkActorContext.joinSpot(...)`과 `joinEntrySpot(...)`은 이미 `Object request`로 DTO를 받고
   typed `submit(Class<T>)` 모양을 갖고 있으므로, 중복 overload를 늘리기보다 runtime codec 경계와 raw
   fallback 제거를 우선한다.
6. `ZLinkActorJoinResult`의 typed reply가 모든 join 경로에서 codec registry를 통해 만들어지는지 검증한다.
7. actor join runtime에서 request payload와 reply payload가 `ZLinkMessageSerializer`와 content type을
   보존하도록 envelope를 정리한다.
8. `ZLinkSpot.onCreate(...)`와 `ZLinkSpotCreateResponse`도 DTO 또는 `ZLinkMessage` request/reply로
   정리한다.
9. Spring starter가 actor/session/spot create handler를 등록할 때 새 typed surface를 우선 인식하는지 확인한다.

완료 기준:

- `ZLinkTypedSessionPacketHandler`가 JSON, Protobuf, MessagePack, custom codec을 모두 같은 호출
  모양으로 처리한다.
- actor join request/reply가 `Message.from(...)` 없이 DTO로 작성된다.
- SPOT create request/reply가 `Message.from(...)` 없이 DTO로 작성된다.
- 기존 raw session test는 raw API 테스트로만 남고, 일반 contract test는 `ZLinkMessage` 또는 typed
  handler를 기준으로 바뀐다.

### Kotlin

대상 경로:

- `framework/languages/java/zlink-framework-kotlin/src/main/kotlin/systems/zlink/framework/kotlin/`
- Java sample 아래 Kotlin sample 디렉토리

작업:

1. Java `ZLinkMessage`를 그대로 사용한다.
2. Kotlin extension으로 `messageOf(value)`, `inline fun <reified T> ZLinkMessage.decode(): T`를
   제공한다.
3. suspending SPOT create hook, session handler, actor join extension이 raw `Message` 대신 `ZLinkMessage` 또는 typed
   payload를 받도록 overload를 추가한다.
4. 기존 suspending raw hooks는 raw surface로 분리하고, 일반 sample에서는 사용하지 않는다.
5. Kotlin DSL에서 `joinSpot<TReply>(rid, request)`처럼 reply type을 자연스럽게 지정할 수 있게 한다.

완료 기준:

- Kotlin sample이 `Message.from(...)` 또는 codec별 decode helper 없이 SPOT create, actor join,
  session dispatch를 작성한다.
- Java core와 Kotlin wrapper의 codec 선택 결과가 같은 contract test로 확인된다.
- coroutine cancellation과 decode failure가 기존 error 처리 규칙을 유지한다.

### Node.js

대상 경로:

- `framework/languages/node/packages/framework/src/contracts/Streams/`
- `framework/languages/node/packages/framework/src/contracts/Actors/`
- `framework/languages/node/packages/framework/src/contracts/Spots/`
- `framework/languages/node/packages/framework/src/runtime/messaging/`
- `framework/languages/node/packages/framework/src/runtime/actors/`
- `framework/languages/node/packages/framework/src/runtime/streams/`
- `framework/languages/node/packages/nestjs/`

작업:

1. `ZLinkMessage<T = unknown>`를 contracts 또는 messaging package에 추가한다.
2. `ZLinkMessage.from(value)`와 `decode<T>()`를 제공한다. TypeScript의 runtime type 한계를 고려해,
   codec extension이 필요로 하는 schema 또는 constructor를 함께 받을 수 있게 한다.
3. session interface와 packet handler에서 raw `Buffer` 또는 binding message 대신 `ZLinkMessage` 또는
   typed payload를 받는 overload를 추가한다.
4. actor context의 `joinSpot` / `joinEntrySpot`은 이미 `unknown` request와 typed `submit<TReply>` 모양을
   갖고 있으므로, 중복 overload를 늘리기보다 runtime codec 경계와 raw fallback 제거를 우선한다.
5. SPOT create hook과 create response도 `ZLinkMessage` 또는 typed payload로 정리한다.
6. NestJS adapter가 controller/provider method를 분석할 때 새 typed surface를 우선 사용하도록 한다.
7. raw packet API는 `raw` 이름을 포함한 별도 표면으로만 유지한다.

완료 기준:

- TypeScript sample에서 codec별 `encode` / `decode` helper 호출이 사라진다.
- Protobuf와 MessagePack package가 framework message 경로에서 같은 contract를 통과한다.
- NestJS sample도 일반 framework sample과 같은 DTO 호출 모양을 유지한다.
- 일반 framework 업무 API에는 raw `Buffer` 또는 binding message가 남지 않는다.

### C++

대상 경로:

- `framework/languages/cpp/include/`
- `framework/languages/cpp/src/`
- `framework/languages/cpp/extensions/framework-codec-protobuf/`
- `framework/languages/cpp/extensions/framework-codec-messagepack/`
- `framework/languages/cpp/samples/`
- `framework/languages/cpp/tests/`

작업:

1. `zlink::framework::message`를 추가한다.
2. `message::from<T>(value)`와 `message.decode<T>()` 또는 `decode<T>(message)`를 제공한다.
3. codec registry가 template 기반 serializer 선택과 content type 또는 stream codec id를 함께
   보존하도록 한다.
4. C++은 compile-time template serializer 선택과 runtime registry 선택이 충돌할 수 있으므로, template
   선택을 registry 뒤로 숨기는 방식을 먼저 설계한다. 후보는 type-erased serializer entry, concept 기반
   adapter, `std::type_index` registry, explicit typed call wrapper다.
5. SPOT create, session callback, actor join API에서 low-level `zlink_msg_t` 또는 binding message wrapper를
   직접 받는 업무 표면을 줄인다.
6. typed handler template이 있는 경우 그 경로를 표준으로 삼고, raw packet handler는 명시 raw surface로
   둔다.
7. Protobuf와 MessagePack extension의 C++ trait 또는 adapter는 framework message 내부에서만 쓰이게
   한다. sample 업무 코드가 trait를 직접 호출하지 않게 한다.

완료 기준:

- C++ sample의 actor join과 session handler가 DTO 중심으로 작성된다.
- C++ sample의 SPOT create도 DTO 중심으로 작성된다.
- raw frame relay와 manual wire test는 기존처럼 raw payload를 다룰 수 있다.
- codec extension 추가 시 sample 호출부가 아니라 구성 코드만 바뀐다.

## 단계별 진행

### 1단계: 공통 계약 고정

- 언어별 `ZLinkMessage` 이름과 최소 API를 확정한다.
- 기존 `ZLinkMessageSerializer`, `ZLinkMessageMetadata`, `ZLinkMessageFlowTracer` 같은 이름과 혼동되지
  않는지 확인하고, 필요하면 더 구체적인 이름을 선택한다.
- typed DTO와 `ZLinkMessage`의 역할 분담을 언어별 public contract에 반영한다.
- 경로별 codec 판별 메커니즘 표를 live code로 검증하고, self-describing tag가 없는 경로의 처리 방식을
  먼저 확정한다.
- raw surface 이름과 deprecation 범위를 정한다.
- 기존 codec extension 계획의 registry 계약과 충돌하는 부분이 없는지 확인한다.
- 문서에 “업무 API는 DTO 또는 framework message, runtime 경계는 raw message”라는 기준을 먼저 적는다.
- codec extension 등록 예시가 기존 options/builder 사용법을 유지하는지 확인한다.

### 2단계: session dispatch 적용

- session runtime이 inbound frame을 `ZLinkMessage`로 감싸도록 바꾼다.
- typed session packet handler가 등록된 codec registry로 decode하는지 확인한다.
- raw session handler는 raw surface로 분리한다.
- session reply와 send는 이미 typed encode 경로를 타는지 확인하고, `ZLinkMessage.From(...)`도 받을 수
  있게 한다.

### 3단계: SPOT create 적용

- SPOT create request를 `ZLinkMessage` 또는 typed DTO로 받도록 바꾼다.
- SPOT create reply도 DTO 또는 `ZLinkMessage`로 encode되도록 한다.
- create reject reply와 empty reply가 codec registry 규칙을 유지하는지 확인한다.
- 기존 raw create hook은 raw surface로 분리한 뒤 일반 업무 API에서 제거한다.

### 4단계: actor join 적용

- actor context에 DTO 또는 `ZLinkMessage` 기반 join overload를 추가한다.
- join request envelope가 content type과 payload bytes를 보존하도록 바꾼다.
- join handler가 DTO 또는 `ZLinkMessage` request를 받을 수 있게 한다.
- join reply가 DTO 또는 `ZLinkMessage`로 encode되어 caller에서 decode되도록 한다.
- local join, remote routed join, native join, entry spot join fallback 경로를 모두 같은 규칙으로 맞춘다.

### 5단계: raw API 격리

- raw relay, raw stream write, backend adapter, protocol frame test를 제외한 업무 코드에서 raw
  `Message` 사용을 제거한다.
- public API에 raw `Message`가 남아야 하는 곳은 이름, 주석, guide에서 raw wire payload 경계임을
  명시한다.
- raw API를 일반 guide 첫 예시로 쓰지 않는다.

### 6단계: sample 반영

- 모든 framework sample의 SPOT create, actor join, session dispatch, session packet handler에서 codec별
  helper와 raw `Message.from(...)` 사용을 제거한다.
- sample DTO는 codec별 wrapper로 나누지 않는다.
- codec 차이는 dependency와 extension 등록 코드에만 나타나게 한다.
- JSON sample, Protobuf sample, MessagePack sample이 같은 업무 코드 흐름을 유지하는지 확인한다.

### 7단계: 문서 반영

- `framework/doc/framework/<lang>/` 문서에서 SPOT create, session dispatch, actor join 예시를 새 표면으로 바꾼다.
- `framework/doc/stream-connector/<lang>/` 문서에서 raw packet API와 typed payload API를 분리해
  설명한다.
- `framework/doc/http-client/<lang>/` 문서와 codec guide가 같은 extension 등록 용어를 쓰는지 확인한다.
- `framework/doc/framework/common/e2e/config-4-registration-codec.ko.md`의 codec 검증 설명을 새
  message 경계와 맞춘다.
- 언어별 문서는 `framework/languages/<lang>/doc/` 아래에 새로 만들지 않는다. 필요한 문서는
  `framework/doc/` 아래 대응 위치에서 수정한다.

### 8단계: 커밋, 푸시, 반복 리뷰

- 변경은 언어별 runtime, tests, samples, docs처럼 검증 가능한 단위로 나누어 커밋한다.
- 각 커밋 전에는 staged diff를 확인하고, 해당 범위의 테스트 또는 정적 검사를 실행한다.
- 모든 단계가 끝나면 최종 통합 테스트를 실행하고 원격 브랜치에 푸시한다.
- 푸시 뒤 Codex로 리뷰를 반복한다. 리뷰는 이 문서의 항목별 체크리스트를 기준으로 수행한다.
- 리뷰에서 나온 누락 항목, raw API 잔존, sample 미반영, 문서 불일치, 테스트 공백은 새 커밋으로 고친다.
- Codex 리뷰에서 high/medium 이슈가 없고, 이 문서의 완료 판정 항목이 모두 충족될 때까지 반복한다.

## 회귀 테스트 계획

### 공통 contract test

각 언어에 아래 contract test를 추가한다.

| 테스트 | 검증 내용 |
|--------|-----------|
| SPOT create JSON round-trip | create hook이 `ZLinkMessage` 또는 typed payload로 JSON DTO를 받는다. |
| SPOT create binary codec round-trip | Protobuf 또는 MessagePack create request/reply가 raw helper 없이 왕복한다. |
| SPOT create custom codec round-trip | custom codec extension만 등록하고 같은 create hook이 동작한다. |
| session JSON round-trip | session handler가 `ZLinkMessage` 또는 typed payload로 JSON DTO를 받는다. |
| session binary codec round-trip | Protobuf 또는 MessagePack payload가 raw helper 없이 decode된다. |
| session custom codec round-trip | custom codec extension만 등록하고 같은 session handler가 동작한다. |
| actor join JSON round-trip | join request와 reply가 DTO 또는 `ZLinkMessage`로 왕복한다. |
| actor join binary codec round-trip | Protobuf 또는 MessagePack join request/reply가 같은 API로 왕복한다. |
| actor join custom codec round-trip | custom codec join request/reply가 raw helper 없이 왕복한다. |
| codec mismatch | 수신 content type이나 stream codec id에 맞는 extension이 없으면 정해진 decode error가 난다. |
| raw relay preservation | raw relay API는 payload bytes와 header를 바꾸지 않는다. |
| raw API static check | 일반 업무 API와 sample에서 기존 raw API가 제거되고, raw harness만 명시 raw surface를 쓰는지 grep 또는 API compatibility check로 확인한다. |

### 언어별 검증

| 언어 | 테스트 위치 | 실행 범위 |
|------|-------------|-----------|
| .NET | `framework/languages/dotnet/tests/` | contract, unit, integration, codec extension tests |
| Java | `framework/languages/java/zlink-framework-core/src/test`, `integrationTest`, `zlink-framework-testkit` | core unit, fake backend, integration, codec packages |
| Kotlin | `framework/languages/java/zlink-framework-kotlin/src/test`와 Kotlin sample test | Kotlin extension, suspending handlers, sample compile |
| Node.js | `framework/languages/node/test/contract`, package unit tests | framework runtime, NestJS adapter, codec packages |
| C++ | `framework/languages/cpp/tests/` | framework unit, codec extension, sample layout contract |

### E2E scenario

- E2E 적용 단계에서는 E2E scenario 문서를 수정하지 않는다. 이 단계는 E2E harness, sample app,
  test code처럼 실행 코드 수정만 수행한다. scenario 문서 변경이 필요하면 7단계 문서 반영에서 별도
  commit으로 처리한다.
- Config 2 Spot 서비스 시나리오에서 SPOT create, actor join, bound session relay가 새 message 경계를 지나도
  정상 동작하는지 확인한다.
- Config 4 등록·codec 시나리오에서 JSON, Protobuf, MessagePack, custom codec이 같은 SPOT create,
  session, actor join API로 왕복하는지 확인한다.
- cross-node actor join, entry spot join fallback, remote routed join에서 reply decode가 같은 규칙을
  따르는지 확인한다.
- codec registry가 서로 다른 peer 사이에서는 명확한 decode error 또는 문서화된 fallback만 허용한다.

## 문서 완료 기준

- framework guide는 SPOT create, session dispatch, actor join에서 raw `Message` 예시를 기본 경로로 보여주지 않는다.
- codec guide는 “codec을 바꾸려면 extension을 등록한다”는 설명만 하고, handler나 sample에 codec별
  helper를 넣으라고 안내하지 않는다.
- codec extension과 custom codec 등록 예시는 기존 options/builder 사용법을 그대로 보여준다.
- stream connector guide는 typed API와 raw API의 목적을 분리한다.
- sample guide와 실제 sample 코드가 같은 호출 모양을 사용한다.
- 한국어 문서는 쉬운 설명을 우선하고, 사용자가 몰라도 되는 runtime 구현 상세를 guide에 넣지 않는다.
  구현 상세가 필요하면 internals 문서로 연결한다.

## sample 완료 기준

각 sample은 아래 기준을 만족해야 한다.

- 업무 DTO는 codec별로 나뉘지 않는다.
- handler, SPOT create, actor join, session dispatch 코드에서 JSON/Protobuf/MessagePack helper를 직접 호출하지
  않는다.
- codec 선택은 application startup의 codec extension 등록에만 나타난다.
- raw payload 사용은 wire-level sample이나 raw API 시연 sample로만 제한한다.
- sample self-check가 JSON 기본값과 선택 codec 등록 양쪽에서 통과한다.

## 완료 판정

이 계획은 아래 조건을 모두 만족할 때 완료로 본다.

1. C++, Java, Kotlin, Node.js, .NET에서 SPOT create, session dispatch, actor join의 기본 업무 API가 raw
   binding message를 요구하지 않는다.
2. 모든 언어에서 `ZLinkMessage` 또는 typed payload가 등록된 codec registry를 통해 encode/decode된다.
3. raw API는 명시 raw surface로 남고, 일반 guide와 sample의 기본 경로에서 빠진다.
4. JSON, Protobuf, MessagePack, custom codec 회귀 테스트가 SPOT create, session, actor join에서 통과한다.
5. codec extension과 custom codec은 기존 options/builder 등록 방식으로 계속 동작한다.
6. framework 문서와 sample이 새 표면으로 갱신되어, codec 변경이 dependency와 extension 등록에만
   드러난다.
7. 변경이 적절한 단위로 커밋되어 있고, 최종 브랜치가 원격에 푸시되어 있다.
8. Codex 반복 리뷰에서 high/medium 이슈가 남지 않았고, 이 문서의 모든 항목이 적용되었음을 확인했다.
