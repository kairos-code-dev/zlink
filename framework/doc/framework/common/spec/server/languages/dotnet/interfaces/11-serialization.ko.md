# .NET typed serialization 공개 인터페이스

[.NET exact interface 목차](README.ko.md)

## 1. Typed JSON 기본 경로

Handler와 client의 generic payload는 기본 JSON serializer로 encode/decode한다. JSON용 message-specific
codec registration 함수는 public contract에 없다. `ZLinkMessage`를 받는 overload는 lifecycle state나
명시적인 encoded payload가 필요한 경계에만 사용한다.

Packet name은 message type descriptor에서 결정한다. 선언적 packet metadata가 있으면 그 이름을 사용하고,
없으면 nominal type 이름을 사용한다. Codec, payload instance와 handler가 [packet name](../../../../01-glossary.ko.md#packet-name) 결정을 반복하지
않는다.

Custom serializer는 content type 단위 extension으로 등록한다. Message type마다 codec을 등록하는 API는
제공하지 않는다.

```csharp
public interface IZLinkCodecExtension
{
    void Register(IZLinkCodecRegistrar codecs);
}

public interface IZLinkCodecRegistryBuilder
{
    void Use(IZLinkCodecExtension extension);
}

public interface IZLinkCodecRegistrar
{
    void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer);
    void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize);
    void AddStreamCodec(
        string contentType,
        ZlinkStreamCodec codec);
}

public interface IZLinkMessageSerializer
{
    ZLinkEncodedPayload Serialize(object value, Type type);
    object? Deserialize(ZLinkEncodedPayload payload, Type type);
}

public readonly struct ZLinkEncodedPayload : IEquatable<ZLinkEncodedPayload>
{
    public ReadOnlyMemory<byte> Bytes { get; }
    public static ZLinkEncodedPayload From(byte[] bytes);
    public static ZLinkEncodedPayload From(ReadOnlyMemory<byte> bytes);
    public static ZLinkEncodedPayload From(ReadOnlySpan<byte> bytes);
    public byte[] ToArray();
    public bool Equals(ZLinkEncodedPayload other);
    public override bool Equals(object? obj);
    public override int GetHashCode();
    public static bool operator ==(
        ZLinkEncodedPayload left,
        ZLinkEncodedPayload right);
    public static bool operator !=(
        ZLinkEncodedPayload left,
        ZLinkEncodedPayload right);
}
```

Framework codec 확장 패키지는 같은 codec instance를 Framework message serializer와 stream connector payload
codec에 등록한다. `Default`는 별도 상태를 갖지 않는 공유 instance다. Application은 message type마다 codec을
등록하지 않고, 사용할 content type의 extension을 한 번 등록한다.

```csharp
public sealed class ZLinkMessagePackCodec :
    IZLinkCodecExtension,
    IZlinkStreamPayloadCodec
{
    public static ZLinkMessagePackCodec Default { get; }

    public void Register(IZLinkCodecRegistrar codecs);
    public ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload);
    public TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload);
}

public sealed class ZLinkProtobufCodec :
    IZLinkCodecExtension,
    IZlinkStreamPayloadCodec
{
    public static ZLinkProtobufCodec Default { get; }

    public void Register(IZLinkCodecRegistrar codecs);
    public ZlinkStreamEncodedPayload Encode<TPayload>(TPayload payload);
    public TPayload Decode<TPayload>(ZlinkStreamEncodedPayload payload);
}
```

Handler filter의 exact extension 경계는 다음과 같다.

```csharp
public sealed class ZLinkHandlerInvocation
{
    public string OwnerKind { get; }
    public string? ChannelName { get; }
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
}

public delegate ValueTask ZLinkHandlerFilterNext();

public interface IZLinkHandlerFilter
{
    ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken);
}
```

## 2. Global object reference JSON

`ActorRef`와 `SpotRef`의 typed JSON contract는 다음 property 이름과 JSON type으로 고정한다. 모든 property는
required이고 중복 property, `null`, unknown property와 범위를 벗어난 generation을 거부한다. Property 이름은
case-sensitive다. `objectGeneration`은 unsigned 63-bit 범위를 잃지 않도록 decimal JSON string으로 encode한다.
`actorId`와 `spotId`는 global logical ID이고 `meshName`과 `nodeRid`는 조회 시점의 location snapshot이다.

```json
{
  "actorId": "player-42",
  "objectGeneration": "17",
  "meshName": "game",
  "nodeRid": "game-node-0123456789abcdef0123456789abcdef"
}
```

```json
{
  "spotId": "room-42",
  "objectGeneration": "9",
  "meshName": "game",
  "nodeRid": "game-node-0123456789abcdef0123456789abcdef"
}
```

`objectGeneration`은 `"1"`..`"9223372036854775807"`의 leading-zero 없는 decimal string이다. 숫자 token,
부호, 소수점과 exponent는 허용하지 않는다. ID와 route string은 각 public type의 validation을 그대로 적용하며
deserialization에서 normalization하지 않는다.
