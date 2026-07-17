<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Java Channel Messaging Guide

## 1. 언제 쓰나

서버 간 request/reply, one-way send, event fanout이 필요할 때 channel messaging을
쓴다. 호출자는 endpoint가 아니라 channel name만 안다.

## 2. Request/reply

```java
client.requestToChannel("application", "profile", new GetProfileRequest(accountId))
    .submit(GetProfileReply.class);
```

호출별 `timeout(...)`이 있으면 그 값을 쓰고, 없으면 channel builder의
`setDefaultRequestTimeout(...)`, 마지막으로 framework 전역
`setDefaultRequestTimeout(...)` 값을 사용한다. 전역 기본값은 30초다.

server는 handler를 등록한다.

```java
@Component
public final class GetProfileHandler
    implements ZLinkRequestHandler<GetProfileRequest, GetProfileReply> {
    @Override
    public GetProfileReply handle(
        GetProfileRequest request,
        ZLinkRequestContext context) {
        return new GetProfileReply(request.accountId());
    }
}
```

> **등록은 자동이 기본, 수동도 된다.** handler annotation과 package scan으로 자동 등록하는 방식이
> 기본이다. 등록 대상을 명시적으로 통제해야 하면 channel builder에서 요청·송신·구독 handler를 수동으로
> 등록한다. 정확한 호출 이름은 [공개 interface 문서](../../spec/server/languages/java/02-handler-interfaces.ko.md)를 따른다. SPOT handler는 Spot/
> EntrySpot의 `configure()` context에서 등록한다([05-spot](05-spot.ko.md)).

## 3. Fanout

```java
fanoutClient.publish("profile", "profile.changed", new ProfileChanged(accountId))
    .submit();
```

fanout은 reply를 기대하지 않는 event 전파다.

## 4. Route mesh

route mesh는 target node `RoutingId`를 application이 직접 알고 있을 때만 쓴다.
`ZLinkRouteClient`는 특정 channel 하나에 묶인 client가 아니며, 호출할 때 route
MeshName과 target `RoutingId`를 함께 받는다. MeshName이 여러 개 있어도 호출 인자로
어느 mesh를 사용할지 분명하게 정한다.

```java
RoutingId target = RoutingId.from("play-node-1");

AllocateRoomReply reply = routeClient
    .requestToNode("play", target, new AllocateRoomRequest("alice"))
    .submit(AllocateRoomReply.class)
    .toCompletableFuture()
    .join();
```

같은 MeshName으로 반복 호출하면 application 코드에서 작은 wrapper를 만들어 Spring bean으로
등록해도 된다. 이 wrapper는 framework API가 아니라 application이 정한 이름이다. 그래서 업무
코드는 매번 MeshName을 반복하지 않고, wrapper 내부에서 어떤 mesh를 사용하는지만
한 곳에 둔다.

```java
public interface PlayRoutes {
    ZLinkRequestCall request(RoutingId targetNodeRid, AllocateRoomRequest request);
}

@Component
public final class DefaultPlayRoutes implements PlayRoutes {
    private final ZLinkRouteClient routes;

    public DefaultPlayRoutes(ZLinkRouteClient routes) {
        this.routes = routes;
    }

    @Override
    public ZLinkRequestCall request(RoutingId targetNodeRid, AllocateRoomRequest request) {
        // "play"는 route channel이 아니라 target node가 참여한 MeshName이다.
        return routes.requestToNode("play", targetNodeRid, request);
    }
}
```

session actor relay는 route mesh를 흉내 내지 않고 SessionRelay를 사용한다.

## 5. 커스텀 codec (Avro 예시)

JSON은 기본 codec이므로 별도 등록하지 않는다. Protobuf나 MessagePack처럼 별도 포맷이 필요하면
framework codec extension package를 추가하고 `options.codecs().use(...)`로 등록한다.
직접 만든 포맷도 같은 extension 계약을 사용한다. extension은 `ZLinkMessageSerializer`를
content type으로 등록하고, serializer는 업무 객체 ↔ `Message`(byte payload) 변환만 맡는다.
packet name 결정과 dispatch 흐름은 framework가 그대로 처리한다.

```java
public final class AvroOrderSerializer implements ZLinkMessageSerializer {
    private final Schema schema = new Schema.Parser().parse(SCHEMA_JSON);

    @Override
    public <T> Message serialize(T value) {
        var out = new ByteArrayOutputStream();
        var writer = new GenericDatumWriter<>(schema);
        writer.write(value, EncoderFactory.get().binaryEncoder(out, null));
        return Message.from(out.toByteArray());
    }

    @Override
    public <T> T deserialize(Message message, Class<T> type) {
        var reader = new GenericDatumReader<>(schema);
        return type.cast(reader.read(null,
            DecoderFactory.get().binaryDecoder(message.toByteArray(), null)));
    }
}

options.codecs().use(codecs -> {
    codecs.addSerializer("application/avro", new AvroOrderSerializer());
});
```

등록 후 high-level 호출은 그대로 업무 객체를 주고받고 직렬화는 Avro로 처리된다. 다른
언어의 등록 표면은 [framework-api §9](../../spec/05-framework-api.ko.md#9-codec) 표를 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](03-concepts.ko.md) | [다음: Spot](05-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
