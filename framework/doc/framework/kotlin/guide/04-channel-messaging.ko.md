<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Channel Messaging Guide

## 1. 언제 쓰나

서버 간 request/reply, one-way send, event fanout이 필요할 때 channel messaging을
쓴다. 호출자는 endpoint가 아니라 channel name만 안다.

## 2. Request/reply

호출 쪽은 `requestToChannel(channel, request)` builder에 `submit(TReply::class.java).await()`를
이어 reply를 받는다. 업무 객체는 등록된 codec으로 직렬화된다. (payload가 이미 `Message`면
`request<TReply>(channel, message)` suspend 확장도 쓸 수 있다.)

```kotlin
val reply: GetProfileReply = client
    .requestToChannel("profile", GetProfileRequest(accountId))
    .submit(GetProfileReply::class.java)
    .await()
```

호출별 `timeout(...)`이 있으면 그 값을 쓰고, 없으면 channel builder의
`setDefaultRequestTimeout(...)`, 마지막으로 framework 전역
`setDefaultRequestTimeout(...)` 값을 사용한다. 전역 기본값은 30초다.

timeout이나 metadata가 필요하면 builder를 직접 쓴다.

```kotlin
import kotlinx.coroutines.future.await

val reply = client.requestToChannel("profile", GetProfileRequest(accountId))
    .timeout(Duration.ofSeconds(5))
    .submit(GetProfileReply::class.java)
    .await()
```

server는 `suspend` handler를 등록한다.

```kotlin
@ZLinkHandlerGroup("profile")
class GetProfileHandler : ZLinkSuspendingRequestHandler<GetProfileRequest, GetProfileReply> {
    override suspend fun handle(
        request: GetProfileRequest,
        context: ZLinkRequestContext,
    ): GetProfileReply = GetProfileReply(request.accountId)
}
```

> **등록은 자동이 기본, 수동도 된다.** handler에 `@ZLinkHandlerGroup`(또는 메서드에
> `@ZLinkRequest`/`@ZLinkSend`/`@ZLinkPublish`)을 달고 `addHandlersFromPackageOf(...)`
> package scan으로 **자동** 등록하는 것이 기본이고 편하다. 어떤 handler가 붙는지
> 명시적으로 통제하려면 channel builder에 `addRequestHandler(...)` / `addSendHandler(...)` /
> `addPublishHandler(...)`로 **수동** 등록한다. SPOT handler는 Spot/EntrySpot의
> `configure()` context에서 등록한다([05-spot](05-spot.ko.md)).

> 메서드 스타일도 된다. `@ZLinkHandlerGroup("play") class CreateGameHandler { @ZLinkRequest
> suspend fun create(request: CreateGameReq): CreateGameRes { ... } }`처럼 한 클래스에 여러
> packet handler를 둘 수 있다.

## 3. Fanout

```kotlin
fanoutClient.publish("profile", "profile.changed", ProfileChanged(accountId)).submit()
```

fanout은 reply를 기대하지 않는 event 전파다. 구독 쪽은
`ZLinkSuspendingPublishHandler<TEvent>`를 구현한다.

```kotlin
@ZLinkHandlerGroup("profile")
class ProfileChangedHandler : ZLinkSuspendingPublishHandler<ProfileChanged> {
    override suspend fun handle(message: ProfileChanged, context: ZLinkPublishContext) {
        cache.evict(message.accountId)   // suspend 작업 가능
    }
}
```

one-way send는 `client.sendToChannel("audit", AuditEvent(...)).submit()` 또는
`ZLinkSuspendingSendHandler<TMessage>` 수신 handler를 쓴다.

## 4. Route mesh

route mesh는 target node `RoutingId`를 application이 직접 알고 있을 때만 쓴다.
`ZLinkRouteClient`의 `requestTo(channel, target, request).submit(TReply::class.java).await()` /
`sendTo(channel, target, message).submit()`으로 호출한다. `ZLinkRouteClient`는 특정
channel 하나에 묶인 client가 아니며, 호출할 때 route channel 이름과 target `RoutingId`를
함께 받는다. route mesh channel이 여러 개 있어도 호출 인자의 channel 이름으로 어느 경로를
쓸지 분명하게 정한다.

```kotlin
val target = RoutingId.from("play-node-1")

val reply: AllocateRoomReply = routeClient
    .requestTo("play.route", target, AllocateRoomRequest("alice"))
    .submit(AllocateRoomReply::class.java)
    .await()
```

같은 route channel로 반복 호출하면 application 코드에서 작은 wrapper를 만들어 Spring bean으로
등록해도 된다. 이 wrapper는 framework API가 아니라 application이 정한 이름이다. 그래서 업무
코드는 매번 channel 문자열을 반복하지 않고, wrapper 내부에서 어떤 route channel로 나가는지만
한 곳에 둔다.

```kotlin
interface PlayRoutes {
    fun request(targetNodeRid: RoutingId, request: AllocateRoomRequest): ZLinkRequestCall
}

@Component
class DefaultPlayRoutes(
    private val routes: ZLinkRouteClient,
) : PlayRoutes {
    override fun request(
        targetNodeRid: RoutingId,
        request: AllocateRoomRequest,
    ): ZLinkRequestCall = routes.requestTo("play.route", targetNodeRid, request)
}
```

session actor relay는 route mesh를 흉내 내지 않고 SessionRelay를 사용한다([06-actor-session](06-actor-session.ko.md)).

## 5. 커스텀 codec (Avro 예시)

JSON은 framework 기본 codec이다. Protobuf나 MessagePack처럼 별도 포맷이 필요하면
framework codec extension package를 추가하고 `options.codecs().use(...)`로 등록한다.
직접 만든 포맷도 같은 extension 계약을 사용한다. extension은 `ZLinkMessageSerializer`를
content type으로 등록하고, serializer는 업무 객체 ↔ `Message`(byte payload) 변환만 맡는다.
packet name 결정과 dispatch 흐름은 framework가 그대로 처리한다.

```kotlin
class AvroOrderSerializer : ZLinkMessageSerializer {
    private val schema = Schema.Parser().parse(SCHEMA_JSON)

    override fun <T> serialize(value: T): Message {
        val out = ByteArrayOutputStream()
        val writer = GenericDatumWriter<T>(schema)
        writer.write(value, EncoderFactory.get().binaryEncoder(out, null))
        return Message.from(out.toByteArray())
    }

    override fun <T> deserialize(message: Message, type: Class<T>): T {
        val reader = GenericDatumReader<T>(schema)
        return type.cast(reader.read(null,
            DecoderFactory.get().binaryDecoder(message.toByteArray(), null)))
    }
}

options.codecs().use { codecs ->
    codecs.addSerializer("application/avro", AvroOrderSerializer())
}
```

등록 후 high-level 호출은 그대로 업무 객체를 주고받고 직렬화는 Avro로 처리된다. 다른
언어의 등록 표면은 [framework-api §2.2](../../spec/05-framework-api.ko.md) 표를 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
