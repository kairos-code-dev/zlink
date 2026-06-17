<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](./03-concepts.ko.md) | [다음: Spot](./05-spot.ko.md)
<!-- framework-adapter-nav:end -->

# Java Channel Messaging Guide

## 1. 언제 쓰나

서버 간 request/reply, one-way send, event fanout이 필요할 때 channel messaging을
쓴다. 호출자는 endpoint가 아니라 channel name만 안다.

## 2. Request/reply

```java
client.requestToChannel("profile", new GetProfileRequest(accountId))
    .timeout(Duration.ofMillis(200))
    .submit(GetProfileReply.class);
```

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

> **등록은 자동이 기본, 수동도 된다.** handler에 annotation(`@ZLinkRequest` 등)을 달고
> `addHandlersFromPackageOf(...)` package scan으로 **자동** 등록하는 것이 기본이고 편하다.
> 어떤 handler가 붙는지 명시적으로 통제하려면 channel builder에 `addRequestHandler(...)` /
> `addSendHandler(...)` / `addPublishHandler(...)`로 **수동** 등록한다. SPOT handler는 Spot/
> EntrySpot의 `configure()` context에서 등록한다([06-spot](./05-spot.ko.md)).

## 3. Fanout

```java
fanoutClient.publish("profile", "profile.changed", new ProfileChanged(accountId))
    .submit();
```

fanout은 reply를 기대하지 않는 event 전파다.

## 4. Route mesh

route mesh는 target node `RoutingId`를 application이 직접 알고 있을 때만 쓴다.
session actor relay는 route mesh를 흉내 내지 않고 ActorGateway를 사용한다.

## 5. 커스텀 codec (Avro 예시)

기본 codec(`addJson`/`addProtobuf`/`addMessagePack`) 외의 포맷이 필요하면
`ZLinkMessageSerializer`를 구현해 content type으로 등록한다. serializer는 업무 객체 ↔
`Message`(byte payload) 변환만 맡고, packet name 결정·codec 선택은 framework가 그대로
처리한다. framework당 custom serializer는 하나만 둔다(둘 이상이면 구성 오류).

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

options.codecs().addSerializer("application/avro", new AvroOrderSerializer());
```

등록 후 high-level 호출은 그대로 업무 객체를 주고받고 직렬화는 Avro로 처리된다. 다른
언어의 등록 표면은 [framework-api §2.2](../../../../doc/spec/framework-api.ko.md) 표를 본다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Concepts](./03-concepts.ko.md) | [다음: Spot](./05-spot.ko.md)
<!-- framework-adapter-nav:bottom:end -->
