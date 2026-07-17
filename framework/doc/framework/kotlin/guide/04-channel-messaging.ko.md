<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin Channel Messaging Guide

## 1. 언제 쓰나

서버 간 request/reply, one-way send, event fanout이 필요할 때 channel messaging을
쓴다. 호출자는 endpoint가 아니라 channel name만 안다.

## 2. Request/reply

호출 쪽은 `requestToChannel(meshName, channelName, request)` builder에
`submit(TReply::class.java).await()`를 이어 reply를 받는다. 업무 객체는 framework의 typed JSON
serializer로 직렬화된다. `request(meshName, channelName, message)` suspend 확장도 같은 의미다.

```kotlin
val reply: GetProfileReply = client
    .requestToChannel("application", "profile", GetProfileRequest(accountId))
    .submit(GetProfileReply::class.java)
    .await()
```

호출별 `timeout(...)`이 있으면 그 값을 쓰고, 없으면 channel builder의
`setDefaultRequestTimeout(...)`, 마지막으로 framework 전역
`setDefaultRequestTimeout(...)` 값을 사용한다. 전역 기본값은 30초다.

timeout이나 metadata가 필요하면 builder를 직접 쓴다.

```kotlin
import kotlinx.coroutines.future.await

val reply = client.requestToChannel("application", "profile", GetProfileRequest(accountId))
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

> **등록은 자동이 기본, 수동도 된다.** handler annotation과 package scan으로 자동 등록하는 방식이
> 기본이다. 등록 대상을 명시적으로 통제해야 하면 channel builder에서 요청·송신·구독 handler를 수동으로
> 등록한다. 정확한 호출 이름은 [공개 interface 문서](../../spec/server/languages/kotlin/02-handler-interfaces.ko.md)를 따른다. SPOT handler는 Spot/EntrySpot의
> `configure()` context에서 등록한다([05-spot](05-spot.ko.md)).

> 메서드 스타일도 지원하므로 한 클래스에 여러 packet handler를 둘 수 있다. annotation과 메서드
> signature는 공개 interface 문서의 예제를 따른다.

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
`ZLinkRouteClient`의 `request(meshName, target, request).submit(TReply::class.java).await()` 또는
`send(meshName, target, message).submit()`으로 호출한다. `ZLinkRouteClient`는 특정 mesh 하나에
묶인 client가 아니며, 호출할 때 MeshName과 target `RoutingId`를 함께 받는다.

```kotlin
val target = RoutingId.from("play-node-1")

val reply: AllocateRoomReply = routeClient
    .request("play", target, AllocateRoomRequest("alice"))
    .submit(AllocateRoomReply::class.java)
    .await()
```

같은 MeshName으로 반복 호출하면 application 코드에서 작은 wrapper를 만들어 Spring bean으로
등록해도 된다. 이 wrapper는 framework API가 아니라 application이 정한 이름이다. 그래서 업무
코드는 매번 MeshName을 반복하지 않고, wrapper 내부에서 어떤 mesh를 사용하는지만
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
    ): ZLinkRequestCall = routes.request("play", targetNodeRid, request) // "play"는 MeshName이다.
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
언어의 등록 표면은 [framework-api §9](../../spec/05-framework-api.ko.md#9-codec) 표를 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
