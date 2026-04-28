[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Unity Stream Connector](./unity-stream-connector.ko.md) | [공통 Stream Connector](../../../streaming-client.ko.md)

# Draft -- ZLink Stream Connector For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 Unity에서 `ZLink STREAM` 서버에 접속하는
> stream connector를 어떤 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 [공통 stream connector 초안](../../../streaming-client.ko.md)을 `.NET`
표면으로 내린다. 핵심 목표는 서버 framework의 STREAM packet callback과 같은
`header + body` 메시지를 `.NET` client에서도 보내고 받을 수 있게 하는 것이다.
서버 framework 계약은 바꾸지 않는다. typed API와 fluent API는 client 쪽에서
`header, body`를 만들어 주는 helper 계층이다.

이 client는 게임 도메인을 포함하지 않는다. 사용자는 이 위에 채팅, 게임, 장비 제어,
알림 같은 자기 protocol을 얹는다.

## 2. 패키지 구성

권장 패키지 이름은 아래와 같다.

| 패키지 | 대상 | 역할 |
|--------|------|------|
| `Systems.Zlink.Stream.Connector` | 일반 C# / .NET | TCP, TLS, WS, WSS transport와 packet connector core |
| `Systems.Zlink.Stream.Connector.Unity` | Unity | Unity main thread callback dispatch와 Unity package metadata |
| `Systems.Zlink.Stream.Connector.Json` | 선택 | JSON packet helper |
| `Systems.Zlink.Stream.Connector.MessagePack` | 선택 | MessagePack packet helper |
| `Systems.Zlink.Stream.Connector.Protobuf` | 선택 | Protobuf packet helper |

Unity 패키지는 일반 C# 패키지 위에 얇게 얹는다. 별도 wire protocol을 만들면 안
된다.

`.NET` Stream Connector는 NuGet으로 별도 배포할 수 있어야 한다. NuGet package id와
`.NET` namespace는 `Systems.Zlink.Stream.Connector` 계열을 사용한다. 이 패키지는
ASP.NET Core adapter, SPOT, Stage wrapper 같은 서버 framework package에 의존하면
안 된다. 필요한 의존성은 transport, codec, compression처럼 connector 실행에 필요한
client-side runtime dependency로 제한한다.

서버 framework package가 Stream Connector를 참조해야 하는 것은 아니다. 서버는 기존
STREAM `header, body` callback 계약을 유지하고, connector package는 외부 client가 그
계약에 맞는 packet을 만들고 해석하도록 돕는다.

## 3. Transport

`.NET` core 패키지는 아래 네 transport를 모두 지원해야 한다.

```csharp
public enum ZlinkStreamTransport
{
    Tcp,
    Tls,
    WebSocket,
    WebSocketSecure
}
```

```csharp
public enum ZlinkStreamCodec : byte
{
    Raw = 0,
    Json = 1,
    MessagePack = 2,
    Protobuf = 3
}
```

```csharp
public enum ZlinkStreamCompression
{
    None,
    Lz4
}
```

URI scheme으로 transport를 추론할 수 있어야 한다.

| URI scheme | transport |
|------------|-----------|
| `tcp://` | `Tcp` |
| `tls://` | `Tls` |
| `ws://` | `WebSocket` |
| `wss://` | `WebSocketSecure` |

`Transport`가 명시되었고 `Endpoint` scheme과 충돌하면 configuration error로 본다.

## 4. Options 초안

```csharp
public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public ZlinkStreamTransport? Transport { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan SendTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan IdleTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan HeartbeatInterval { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan HeartbeatTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public int MaxSendFrameSize { get; init; } = 1024 * 1024;

    public int MaxSendMetadataSize { get; init; } = 1024;

    public bool SkipServerCertificateValidation { get; init; }

    public bool EnableSegmentedSend { get; init; } = true;

    public ZlinkStreamCodec DefaultCodec { get; init; } = ZlinkStreamCodec.Json;

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.None;

    public IZlinkStreamCodecRegistry? CodecRegistry { get; init; }

    public IZlinkStreamHeaderCodec? HeaderCodec { get; init; }

    public IZlinkStreamCompressionCodec? CompressionCodec { get; init; }

    public IZlinkStreamPacketNameResolver? NameResolver { get; init; }
}
```

`SkipServerCertificateValidation`은 테스트용 자체 서명 인증서 검증에만 사용한다.
운영 환경에서는 기본값 `false`를 유지해야 한다.

`MaxSendFrameSize`와 `MaxSendMetadataSize`는 connector가 보내는 packet에 적용하는
기본 보호 장치다. 수신 payload에 대한 도메인별 크기 제한은 connector 기본 계약에
넣지 않는다. 수신 제한이 필요한 애플리케이션은 handler나 상위 protocol에서 따로
검사한다.

## 5. Packet 모델

wire packet의 최저 레벨 모델은 `header + body`다.

```csharp
public sealed record ZlinkStreamPacket(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Body);
```

일반 사용자 API는 raw header를 매번 요구하지 않는다. body 객체의 CLR 타입 이름을
기본 packet 이름으로 쓰고, 호출자가 지정한 이름이 있으면 그 이름을 우선한다.
추가 metadata가 필요하면 작은 key-value metadata를 붙인다.

```csharp
public sealed record ZlinkStreamMessage(
    string Name,
    ZlinkStreamMetadata Metadata,
    object? Body);

public sealed record ZlinkStreamMessage<TBody>(
    string Name,
    ZlinkStreamMetadata Metadata,
    TBody Body);
```

`ZlinkStreamMessage`는 helper 모델이다. 실제 transport framing은 항상
`ReadOnlyMemory<byte> Header`와 `ReadOnlyMemory<byte> Body`를 기준으로 한다. helper는
`Name`, `Metadata`, codec 정보, request correlation 정보를 byte header로
인코딩한다. 서버는 이 결과를 기존 STREAM callback에서 `header, body`로 받는다.

STREAM frame의 앞쪽 `2B`는 connector helper header가 아니라 `header_size`다.
따라서 `.NET` helper가 만든 packet도 wire에서는 아래 순서를 따른다.

```text
+------------------+------------------+------------------+------------------+
| u16 header_size  | u32 body_size    | header bytes     | body bytes       |
+------------------+------------------+------------------+------------------+
```

helper header는 binary header다. `kind`와 `codec`은 문자열이 아니라 1바이트 enum
값으로 인코딩한다. packet name은 `u8 name_len + UTF-8 bytes`이며 최대 255 bytes다.

```text
+--------+---------+----------+----------+-----------+-----------+
| kind u8| codec u8| flags u8 | rid u64? | name u8+n | meta?     |
+--------+---------+----------+----------+-----------+-----------+
```

`.NET` enum 값은 공통 helper header 값과 맞춘다.

```csharp
public enum ZlinkStreamMessageKind : byte
{
    Send = 1,
    Request = 2,
    Response = 3,
    Error = 4
}

[Flags]
public enum ZlinkStreamHeaderFlags : byte
{
    None = 0,
    HasRid = 0x01,
    HasMetadata = 0x02,
    BodyCompressed = 0x04
}

public readonly record struct ZlinkStreamRequestId(ulong Value);

public sealed record ZlinkStreamHeader(
    ZlinkStreamMessageKind Kind,
    ZlinkStreamCodec Codec,
    ZlinkStreamHeaderFlags Flags,
    ZlinkStreamRequestId? RequestId,
    string Name,
    ZlinkStreamMetadata Metadata);

public interface IZlinkStreamHeaderCodec
{
    ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header);

    ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header);
}
```

`rid`는 request, response, error response에만 들어가는 `u64` correlation id다.
connector가 생성하는 `rid`는 같은 connector instance 안에서 동시에 pending 상태인
request 사이에 중복되면 안 된다. 값 `0`은 사용하지 않는다.
metadata는 `u16 meta_len + metadata bytes`로 붙인다. metadata가 header 크기 제한 안에
들어가야 하므로 trace id, locale, tenant id 같은 작은 값만 넣는다.
`MaxSendMetadataSize` 기본값은 1024 bytes다. `meta_len` wire 필드가 표현할 수 있는
최댓값은 65535 bytes지만, `.NET` connector는 `MaxSendMetadataSize`를 넘는 metadata를
보내면 error로 처리해야 한다.

helper header의 모든 multi-byte integer는 network byte order를 사용한다.
알 수 없는 kind, codec, flag bit는 decode error다. `HasRid`,
`HasMetadata` flag와 실제 필드 존재 여부가 맞지 않아도 decode error다.

metadata bytes는 아래 순서의 binary key-value 목록이다.

```text
+---------------+-------------+-------------+
| count u8      | entry...    | entry...    |
+---------------+-------------+-------------+

entry:
+-------------+-------------+-------------+-------------+
| key_len u8  | key bytes   | val_len u16 | value bytes |
+-------------+-------------+-------------+-------------+
```

metadata key와 value는 UTF-8 문자열이다. `key_len`은 1 이상이어야 한다. 같은 key가
두 번 나오면 decode error다. `count`는 뒤따르는 entry 개수와 정확히 일치해야 한다.

helper header `flags` 값은 공통 스펙과 맞춘다.

| flag | value | 의미 |
|------|-------|------|
| has rid | `0x01` | `rid` 필드가 있다 |
| has metadata | `0x02` | `meta` 필드가 있다 |
| body compressed | `0x04` | body가 압축되어 있다 |

압축 알고리즘은 header에 packet마다 넣지 않는다. `.NET` connector의
`Compression` option으로 정한다. `body compressed` flag는 이 packet의 body가 해당
알고리즘으로 압축되어 있음을 나타낸다. 이 option은 client-to-server 자동 압축을
켜지 않는다. client에서 server로 보낼 때는 send/request builder에서 `.Compress()`를
명시한 경우에만 압축한다.

## 6. Metadata API 초안

사용자 API는 임의 header schema를 받지 않는다. connector helper header를 사용하고,
사용자가 추가할 수 있는 값은 작은 metadata key-value로 제한한다. 임의 header bytes를
직접 쓰려면 raw API를 사용한다.

```csharp
public sealed class ZlinkStreamMetadata
{
    public static ZlinkStreamMetadata Empty { get; }

    public int Count { get; }

    public IReadOnlyDictionary<string, string> Values { get; }

    public string? Get(string key);

    public bool TryGet(string key, out string value);

    public ZlinkStreamMetadata With(string key, string value);

    public ZlinkStreamMetadata WithMany(
        IEnumerable<KeyValuePair<string, string>> values);
}
```

metadata key와 value는 UTF-8 문자열로 인코딩한다. 구현은 key 중복, 빈 key, 너무 긴
metadata를 validation error로 처리해야 한다.

## 7. Packet Name API 초안

packet 이름은 helper header에 들어가므로 짧아야 한다. 기본 이름은 CLR 타입 이름을
사용하지만, 사용자는 attribute나 resolver로 이름을 바꿀 수 있어야 한다.

```csharp
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
public sealed class ZlinkStreamPacketNameAttribute : Attribute
{
    public ZlinkStreamPacketNameAttribute(string name);

    public string Name { get; }
}

public interface IZlinkStreamPacketNameResolver
{
    string Resolve(Type bodyType);
}
```

resolver가 돌려준 이름은 UTF-8 기준 255 bytes를 넘을 수 없다.

## 8. Connector API 초안

```csharp
public sealed class ZlinkStreamConnector : IAsyncDisposable
{
    public event Func<ZlinkStreamPacket, CancellationToken, ValueTask>? PacketReceived;

    public event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    public event Func<CancellationToken, ValueTask>? Disconnected;

    public bool IsConnected { get; }

    public ZlinkStreamConnectorOptions Options { get; }

    public static ValueTask<ZlinkStreamConnector> ConnectAsync(
        ZlinkStreamConnectorOptions options,
        CancellationToken cancellationToken = default);

    public ValueTask ConnectAsync(
        CancellationToken cancellationToken = default);

    public ValueTask CloseAsync(
        CancellationToken cancellationToken = default);

    public void SendRaw(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken = default);

    public void SendRaw(
        ZlinkStreamPacket packet,
        CancellationToken cancellationToken = default);

    public ValueTask<ZlinkStreamPacket> ReceiveRawAsync(
        CancellationToken cancellationToken = default);

    public ZlinkStreamSendBuilder<TBody> Send<TBody>(
        TBody body);

    public ZlinkStreamSendBuilder<TBody> Send<TBody>(
        string name,
        TBody body);

    public ZlinkStreamRequestBuilder<TBody> Request<TBody>(
        TBody body);

    public ZlinkStreamRequestBuilder<TBody> Request<TBody>(
        string name,
        TBody body);

    public IDisposable On<TBody>(
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler);

    public IDisposable On<TBody>(
        string name,
        Func<ZlinkStreamMessage<TBody>, CancellationToken, ValueTask> handler);
}
```

`PacketReceived`, typed `On<TBody>()`, `ReceiveRawAsync`는 같은 receive stream을
공유한다. 한 connector instance에서는 event/handler 기반 receive와
`ReceiveRawAsync`를 동시에 섞어 쓰지 않는다. event 또는 typed handler를 등록한 뒤
`ReceiveRawAsync`를 호출하거나, `ReceiveRawAsync`를 먼저 사용한 뒤 event 또는 typed
handler를 등록하면 configuration error로 처리한다.

raw API는 사용자가 직접 `header, body` bytes를 구성하는 경우에만 사용한다. typed
API는 connector helper header를 만들고, metadata, codec, compression, request
correlation을 처리한다.
raw API에는 request helper를 제공하지 않는다. 임의 header protocol의 correlation
규칙은 사용자 protocol 영역이므로 connector가 보장하지 않는다.
`SendRaw(...)`와 typed `Send(...).Exec()`는 응답을 기다리지 않는 fire-and-forget
submit API다. 실제 transport write는 connector 내부에서 비동기로 처리한다. 호출
시점에는 frame size, packet name, metadata size, connection 상태처럼 즉시 판단할 수
있는 validation 실패만 예외로 보고한다. write 중 발생한 오류는 `ErrorReceived`로
전달한다.

## 9. Send / Request Builder API 초안

fluent builder는 timeout, metadata, cancellation을 호출 단위로 조정할 수 있게 한다.
codec은 기본적으로 client option 또는 type registry에서 결정한다. 일반 request/send
호출에서는 codec 선택 메서드를 매번 호출하지 않는다.

```csharp
public sealed class ZlinkStreamSendBuilder<TBody>
{
    public ZlinkStreamSendBuilder<TBody> Name(string name);

    public ZlinkStreamSendBuilder<TBody> Metadata(string key, string value);

    public ZlinkStreamSendBuilder<TBody> Metadata(ZlinkStreamMetadata metadata);

    public ZlinkStreamSendBuilder<TBody> WithTimeout(TimeSpan timeout);

    public ZlinkStreamSendBuilder<TBody> Compress();

    public void Exec(CancellationToken cancellationToken = default);
}

public sealed class ZlinkStreamRequestBuilder<TBody>
{
    public ZlinkStreamRequestBuilder<TBody> Name(string name);

    public ZlinkStreamRequestBuilder<TBody> Metadata(string key, string value);

    public ZlinkStreamRequestBuilder<TBody> Metadata(ZlinkStreamMetadata metadata);

    public ZlinkStreamRequestBuilder<TBody> WithTimeout(TimeSpan timeout);

    public ZlinkStreamRequestBuilder<TBody> Compress();

    public ValueTask<TReply> ExecAsync<TReply>(
        CancellationToken cancellationToken = default);

    public void Exec(
        Action<ZlinkStreamResult> callback);

    public void Exec<TReply>(
        Action<ZlinkStreamResult<TReply>> callback);
}
```

builder instance는 1회 실행만 허용한다. 같은 builder에서 `ExecAsync`나 `Exec`를 두 번
호출하면 validation error로 처리한다.

```csharp
var reply = await client
    .Request(new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .Metadata("traceId", traceId)
    .ExecAsync<GetProfileReply>(cancellationToken);
```

packet 이름을 명시하지 않은 경우 기본 이름은 namespace를 제외한 body CLR 타입
이름이다. 다른 이름이 필요하면 `ZlinkStreamPacketNameAttribute` 또는
`IZlinkStreamPacketNameResolver`를 사용한다.

```csharp
client
    .Send(new ChatMessage { Text = "hello" })
    .Exec(cancellationToken);

client
    .Send("chat.message", new ChatMessage { Text = "hello" })
    .Metadata("traceId", traceId)
    .Exec(cancellationToken);
```

client-to-server compression은 명시 호출에서만 적용한다.

```csharp
client
    .Send(new UploadReplayChunk { Bytes = chunk })
    .Compress()
    .Exec(cancellationToken);
```

`.Compress()`는 body만 압축하고 helper header `flags`에 `body compressed`를 표시한다.
서버 framework는 기존처럼 `header, body`를 받는다. 서버 쪽 helper나 actor adapter는
helper header를 보고 필요하면 body를 압축 해제한다.

## 10. Result / Error API 초안

```csharp
public sealed record ZlinkStreamError(
    ZlinkStreamErrorCode Code,
    string Message,
    Exception? Exception = null);

public enum ZlinkStreamErrorCode
{
    Disconnected,
    ConfigurationError,
    ValidationFailed,
    RequestTimeout,
    ConnectTimeout,
    FrameDecodeFailed,
    FrameTooLarge,
    SendFailed,
    CodecNotFound,
    CompressionFailed,
    TlsValidationFailed,
    DecompressionFailed,
    UserCallbackFailed
}

public sealed class ZlinkStreamException : Exception
{
    public ZlinkStreamError Error { get; }
}

public readonly struct ZlinkStreamResult
{
    public bool IsSuccess { get; }

    public ZlinkStreamError? Error { get; }
}

public readonly struct ZlinkStreamResult<T>
{
    public bool IsSuccess { get; }

    public T? Value { get; }

    public ZlinkStreamError? Error { get; }
}
```

async API는 실패 시 exception을 던진다. callback 기반 API는 `ErrorReceived`를
호출한다. 두 경로의 error code 의미는 같아야 한다.

## 11. Codec API 초안

core 패키지는 codec을 강제하지 않는다. codec package는 아래 인터페이스로 body
serialize와 deserialize를 제공한다.

```csharp
public interface IZlinkStreamBodyCodec
{
    ZlinkStreamCodec Codec { get; }

    bool CanSerialize(Type type);

    ReadOnlyMemory<byte> Serialize<T>(T value);

    object? Deserialize(Type type, ReadOnlyMemory<byte> body);

    T Deserialize<T>(ReadOnlyMemory<byte> body);
}

public interface IZlinkStreamCodecRegistry
{
    ZlinkStreamCodec DefaultCodec { get; }

    IZlinkStreamBodyCodec ResolveForSend(Type bodyType);

    IZlinkStreamBodyCodec ResolveForReceive(
        Type bodyType,
        ZlinkStreamCodec codec);
}
```

Protobuf는 `Google.Protobuf.IMessage` 구현 타입이면 Protobuf codec으로 구분할 수
있다. MessagePack은 attribute나 registry 등록으로 구분한다. 그 외 일반 POCO는
`DefaultCodec`을 따른다.

## 12. Compression Codec API 초안

compression package는 아래 인터페이스로 body 압축과 압축 해제를 제공한다.

```csharp
public interface IZlinkStreamCompressionCodec
{
    ZlinkStreamCompression Compression { get; }

    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> body);

    ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> body);
}
```

현재 계약에서 지원하는 압축 알고리즘은 LZ4 하나다. 서버와 클라이언트가 같은
알고리즘을 사용하도록 설정하는 책임은 사용자에게 있다. `ZlinkStreamCompression.None`은
body를 그대로 둔다. `BodyCompressed` flag가 있는데 `Compression`이 `None`이거나
`CompressionCodec`이 없으면 decode error로 처리한다.

## 13. Request Helper

request helper는 `header + body` 전송 위의 선택 기능이다. 일반 client도 많이 쓰는
흐름이므로 core 패키지에 포함한다.

요구 사항:

- async request
- callback request
- fluent request builder
- body 타입 이름 또는 명시 packet 이름
- optional metadata
- request timeout
- pending request map
- response correlation
- close 시 pending request 실패 처리
- timeout 시 pending request 제거

correlation id는 header helper 영역에 넣는다. 단, 사용자가 직접 byte header를
구성하는 경우를 막지 않는다.

request/response 규칙은 아래와 같다.

- `Send`는 `kind=Send`와 `rid` 없는 helper header를 만든다.
- `Request`는 `kind=Request`와 새 `rid`를 가진 helper header를 만들고 pending map에
  등록한다.
- `kind=Response` packet은 같은 `rid`의 pending request를 성공으로 완료한다.
- `kind=Error` packet에 `rid`가 있으면 같은 `rid`의 pending request를 실패로 완료한다.
- `kind=Error` packet에 `rid`가 없으면 connector error message로 `ErrorReceived`에
  전달한다.
- `Response`와 `Error`의 packet name은 원 request packet name과 같아야 한다.
- `Error`는 `codec=Json`을 사용한다.
- request timeout, close, disconnect 시 pending request는 실패 처리하고 map에서
  제거한다.

`Error` body는 codec과 무관하게 UTF-8 JSON object로 인코딩한다.

```json
{"code":"error_code","message":"message"}
```

애플리케이션 도메인 error를 정상 reply body로 다루고 싶으면 `Response` kind와 사용자
body schema를 사용한다.

## 14. Codec Extension

core 패키지는 codec을 강제하지 않는다. 아래 확장 패키지는 packet 이름, optional
metadata, body payload 생성과 parse만 돕는다.

- JSON
- MessagePack
- Protobuf

예시:

```csharp
client
    .Send(new ChatMessage("hello"))
    .Exec(cancellationToken);

var reply = await client
    .Request("chat.request", new ChatRequest("hello"))
    .WithTimeout(TimeSpan.FromSeconds(1))
    .ExecAsync<ChatReply>(cancellationToken);
```

codec 선택은 아래 순서로 결정한다.

1. body 타입 또는 reply 타입에 등록된 codec
2. client option의 `DefaultCodec`
3. 구현이 정한 기본값

타입만으로 JSON, MessagePack, Protobuf를 항상 안전하게 구분할 수는 없다. 구현은
attribute 또는 `CodecRegistry`처럼 타입과 codec을 연결하는 명시적 등록 방식을
제공해야 한다. 일반 fluent API의 기본 사용 예시는 타입 등록 정보와 기본 codec을
따른다. codec extension은 transport, timeout, request map, callback dispatch 규칙을
바꾸면 안 된다.

## 15. Compression

server-to-client 방향은 typed API에서 자동 압축 해제를 제공한다.

- 서버가 helper header `body compressed` flag를 켜고 body를 보내면 `.NET` connector는
  typed 사용자 callback 전에 body를 압축 해제한다.
- typed message handler와 request reply decode는 압축 해제된 body를 받는다.
- `PacketReceived`와 `ReceiveRawAsync`는 raw STREAM packet API이므로 helper header와
  body를 transport에서 받은 형태로 노출한다.
- 압축 해제 실패는 `DecompressionFailed` error로 사용자에게 전달한다.

client-to-server 방향은 명시 호출일 때만 압축한다.

- 기본 `Send`와 `Request`는 압축하지 않는다.
- `.Compress()`를 호출한 send/request만 body를 압축한다.
- 압축된 packet에는 helper header `body compressed` flag를 켠다.
- raw `SendRaw(header, body)` API는 자동 압축을 적용하지 않는다.

압축은 body에만 적용한다. helper header는 압축하지 않는다.

## 16. Unity Adapter

`Systems.Zlink.Stream.Connector.Unity`는 별도 Unity package로 배포한다. Unity package는
`Systems.Zlink.Stream.Connector` core를 참조하고, Unity main thread callback dispatch,
`MonoBehaviour` wrapper, Unity lifecycle만 추가한다.

Unity 상세 계약은 [unity-stream-connector.ko.md](./unity-stream-connector.ko.md)를
기준으로 한다. Unity adapter는 `ZlinkStreamConnector`의 packet 의미와 helper header
의미를 바꾸지 않는다.

## 17. 완료 기준

`.NET` 구현 완료 기준은 아래와 같다.

- `tcp://`, `tls://`, `ws://`, `wss://` endpoint에 연결할 수 있다.
- `SendRaw(header, body)`로 보낸 packet을 framework STREAM 서버가 받는다.
- 서버 `stream.Write(header, body)` packet을 client가 받는다.
- callback receive와 `ReceiveRawAsync`가 각각 동작한다.
- callback request와 `Request(...).ExecAsync<TReply>()`가 각각 동작한다.
- request timeout, close 중 pending request 실패, disconnected send를 테스트한다.
- TLS 자체 서명 인증서 검증 옵션을 테스트한다.
- partial read, multi-packet read, send frame limit을 테스트한다.
- JSON, MessagePack, Protobuf extension이 core packet 계약을 바꾸지 않는지 테스트한다.
- server-to-client compressed body를 typed API에서 자동으로 압축 해제한다.
- client-to-server compression은 `.Compress()`를 호출한 packet에만 적용한다.
- Unity adapter가 main thread callback dispatch와 lifecycle close를 보장한다.

## 18. 구현 순서

구현은 아래 순서로 진행한다. 단계별로 공개 계약을 나누기 위한 순서가 아니다.
최종 공개 package에는 이 문서의 계약을 한 번에 적용한다.

1. TCP transport와 STREAM frame encode/decode
2. helper header encode/decode와 metadata encode/decode
3. raw `SendRaw`, `ReceiveRawAsync`
4. typed `Send`, `Request`, `On`과 packet name resolver
5. request pending map, timeout, close/disconnect 실패 처리
6. JSON codec
7. MessagePack, Protobuf codec
8. LZ4 compression과 server-to-client 자동 압축 해제
9. TLS, WebSocket, WebSocket over TLS transport
10. Unity adapter package
