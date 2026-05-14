<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework .NET Session Actor Dispatch](session-actor-dispatch.ko.md) | [다음: ZLink Stream Connector For Unity](unity-stream-connector.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [Unity Stream Connector](./unity-stream-connector.ko.md) | [공통 Stream Connector](../../../streaming-client.ko.md)

# Draft -- ZLink Stream Connector For .NET

> 이 문서는 **구현 전 초안**이다.
> 즉 아직 공개된 계약[^public-contract]이 아니며, `.NET`과 Unity에서 `ZLink STREAM`[^stream]
> 서버에 접속하는 stream connector[^stream-connector]를 어떤 모양으로 노출할지 정리해 둔
> 문서다.

## 1. 목적

이 절에서는 이 문서가 무엇을 다루는지, 그리고 어디까지가 client 표면의 책임인지 짧게 정리한다.

이 문서는 [공통 stream connector 초안](../../../streaming-client.ko.md) 을 `.NET` 표면으로
내려 풀어 쓴 버전이다. 핵심 목표는 한 가지다. 서버 framework 의 STREAM packet[^packet] 모델과
같은 메시지를, `.NET` client 에서도 똑같이 주고받을 수 있게 하는 것이다.

서버와 client 의 역할은 다음과 같이 나뉜다.

- 서버 framework 의 callback 이 받는 값은 `IZLinkSessionPacket` 이다.
- connector 의 typed API 와 fluent API 는, client 쪽에서 wire 의 header / body 를 만들어
  주는 helper 계층이다.

이 client 는 게임 도메인 자체를 포함하지 않는다. 사용자가 그 위에 채팅, 게임, 장비 제어,
알림 같은 자신의 protocol 을 얹어 사용한다.

## 2. 패키지 구성

이 절에서는 connector 를 어떤 NuGet 패키지로 쪼개 배포하는지, 그리고 각 패키지가 어떤
의존성을 가져야 하는지를 정리한다.

권장하는 패키지 이름은 다음과 같다.

| 패키지 | 대상 | 역할 |
|--------|------|------|
| `Systems.Zlink.Stream.Connector` | 일반 C# / .NET | TCP[^tcp], TLS[^tls], WS[^ws], WSS[^wss] transport[^transport]와 packet connector core |
| `Systems.Zlink.Stream.Connector.Unity` | Unity | Unity main thread callback dispatch와 Unity package metadata |
| `Systems.Zlink.Stream.Connector.Json` | 선택 | JSON packet helper |
| `Systems.Zlink.Stream.Connector.MessagePack` | 선택 | MessagePack packet helper |
| `Systems.Zlink.Stream.Connector.Protobuf` | 선택 | Protobuf packet helper |
| `Systems.Zlink.Stream.Connector.Codecs` | 선택 | 타입 특성을 보고 codec[^codec]을 자동으로 골라 주는 convenience helper |

Unity 패키지는 일반 C# 패키지 위에 얇게 얹는 형태로만 둔다. 즉 별도의 wire protocol 을
새로 만들어서는 안 된다.

`.NET` Stream Connector 는 NuGet[^nuget] 으로 별도 배포할 수 있어야 한다. NuGet 의
package id 와 `.NET` namespace 는 `Systems.Zlink.Stream.Connector` 계열을 그대로 사용한다.

의존성 규칙은 다음과 같다.

- 이 패키지가 ASP.NET Core adapter, SPOT[^spot], Stage wrapper 같은 서버 framework 측
  package 에 의존해서는 안 된다.
- 의존성은 transport, codec, compression 처럼, connector 를 실행하는 데 꼭 필요한
  client-side runtime dependency 로만 한정한다.

반대 방향의 의존성도 마찬가지다. 서버 framework 측 package 가 Stream Connector 를 굳이
참조할 필요가 없다. 서버는 framework session packet 계약을 그대로 유지하면 된다.
connector package 는 외부 client 가 그 계약에 맞는 wire packet 을 만들고 해석할 수
있도록 돕는 역할만 맡는다.

## 3. Transport

이 절에서는 connector 가 지원해야 할 transport 종류와, URI scheme 으로부터 transport 를
추론하는 규칙을 정리한다.

`.NET` core 패키지는 다음 네 가지 transport 를 모두 지원해야 한다.

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

URI scheme 만 보고도 transport 를 추론할 수 있어야 한다.

| URI scheme | transport |
|------------|-----------|
| `tcp://` | `Tcp` |
| `tls://` | `Tls` |
| `ws://` | `WebSocket` |
| `wss://` | `WebSocketSecure` |

`Transport` 가 명시되어 있는데 그 값이 `Endpoint` 의 scheme 과 어긋난다면, 이는
configuration error 로 처리한다.

## 4. Options 초안

이 절에서는 connector 를 어떻게 구성하는지, 그리고 각 option 이 어떤 시점에 어떤 동작을
바꾸는지 정리한다.

```csharp
public sealed class ZlinkStreamConnectorOptions
{
    public required Uri Endpoint { get; init; }

    public ZlinkStreamTransport? Transport { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan IdleTimeout { get; init; } = TimeSpan.FromSeconds(60);

    public TimeSpan HeartbeatInterval { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan HeartbeatTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public int MaxSendFrameSize { get; init; } = 1024 * 1024;

    public int MaxSendMetadataSize { get; init; } = 1024;

    public bool SkipServerCertificateValidation { get; init; }

    public bool EnableSegmentedSend { get; init; } = true;

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.None;

    public IZLinkStreamHeaderCodec? HeaderCodec { get; init; }

    public IZlinkStreamCompressionCodec? CompressionCodec { get; init; }

    public IZlinkStreamPacketNameResolver? NameResolver { get; init; }
}
```

`SkipServerCertificateValidation` 은 테스트용 자체 서명 인증서를 검증할 때만 쓰는
옵션이다. 운영 환경에서는 기본값 `false` 를 반드시 그대로 두어야 한다.

`MaxSendFrameSize` 와 `MaxSendMetadataSize` 는 connector 가 내보내는 packet 에 대한
기본 보호 장치다. 즉 보내는 쪽 한도다.

반면 수신 payload 에 도메인별로 거는 크기 제한은 connector 의 기본 계약에 포함하지
않는다. 수신 크기 제한이 필요한 애플리케이션이라면, handler 또는 상위 protocol 에서
별도로 검사해야 한다.

`HeartbeatTimeout` 은 마지막으로 받은 heartbeat 응답을 기준으로 한 임계값이다. 이 시간이
지나도록 새 응답이 더 들어오지 않으면, 해당 연결을 죽은 것으로 간주한다. 기본값은
30 초다.

`IdleTimeout` 은 어느 방향으로든 트래픽이 전혀 없는 상태가 이어진 시간에 대한 임계값
이다. 이 시간을 넘기면 connector 가 연결을 닫는다. 기본값은 60 초다.

## 5. Packet 모델

이 절에서는 wire 위의 packet 한 단위가 어떻게 생겼는지, 그리고 helper 가 그 안의
header 와 body 를 어떻게 채워 넣는지를 정리한다.

wire packet[^wire] 의 가장 낮은 단위 모델은 `header + body` 형태다.

```csharp
public sealed record ZlinkStreamEncodedBody(
    ZlinkStreamCodec Codec,
    ReadOnlyMemory<byte> Body,
    Type? MessageType = null);
```

사용자 API 는 raw header bytes 를 직접 받지 않는다. 대신 다음 규칙을 따른다.

- 기본 packet 이름은 body 객체의 CLR 타입 이름을 사용한다.
- 호출자가 명시한 이름이 있다면 그쪽이 우선이다.
- 추가 metadata 가 필요하면 작은 key-value 쌍을 metadata 로 덧붙인다.

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

`ZlinkStreamMessage` 는 어디까지나 helper 모델이다. 실제 transport framing 은 항상
`ReadOnlyMemory<byte> Header` 와 `ReadOnlyMemory<byte> Body` 를 기준으로 이루어진다.

helper 는 다음 값을 모두 byte header 로 인코딩한다.

- `Name`
- `Metadata`
- codec 정보
- request correlation 정보

서버 framework 는 이렇게 만들어진 결과를 `IZLinkSessionPacket` 으로 감싸, session
callback 에 흘려보낸다.

STREAM frame 의 앞쪽 `2B` 는 connector helper header 가 아니라 `header_size` 다.
따라서 `.NET` helper 가 만들어 내는 packet 도 wire 에서는 다음 순서를 그대로 따른다.

```text
+------------------+------------------+------------------+------------------+
| u16 header_size  | u32 body_size    | header bytes     | body bytes       |
+------------------+------------------+------------------+------------------+
```

helper header 는 binary header 다. 구조는 다음과 같다.

- `kind` 와 `codec` 은 문자열이 아니라 1 바이트 enum 값으로 인코딩한다.
- packet name 은 `u8 name_len + UTF-8 bytes` 형식이고, 최대 길이는 255 bytes 다.

```text
+---------+----------+----------+------------------+-----------+-------+
| kind u8 | codec u8 | flags u8 | request_seq u64? | name u8+n | meta? |
+---------+----------+----------+------------------+-----------+-------+
```

`.NET` enum 값은 공통 helper header 값과 그대로 일치시킨다.

```csharp
public enum ZlinkStreamMessageKind : byte
{
    Send = 1,
    Request = 2,
    Response = 3,
    Error = 4
}

[Flags]
public enum ZLinkStreamHeaderFlags : byte
{
    None = 0,
    HasRequestSeq = 0x01,
    HasMetadata = 0x02,
    BodyCompressed = 0x04
}

public readonly record struct ZlinkStreamRequestSeq(ulong Value);

public sealed record ZLinkStreamHeader(
    ZlinkStreamMessageKind Kind,
    ZlinkStreamCodec Codec,
    ZLinkStreamHeaderFlags Flags,
    ZlinkStreamRequestSeq? RequestSeq,
    string Name,
    ZlinkStreamMetadata Metadata);

public interface IZLinkStreamHeaderCodec
{
    ReadOnlyMemory<byte> Encode(ZLinkStreamHeader header);

    ZLinkStreamHeader Decode(ReadOnlyMemory<byte> header);
}
```

`request_seq` 는 `u64` 형식의 correlation sequence 다. request, response, error response
에만 들어간다. 규칙은 다음과 같다.

- 같은 connector instance 안에서 동시에 pending 상태인 request 사이에는, connector 가
  만들어 내는 `request_seq` 값이 결코 중복되면 안 된다.
- 값 `0` 은 사용하지 않는다.

metadata 는 `u16 meta_len + metadata bytes` 형태로 붙는다. metadata 는 header 크기
제한 안에 모두 들어가야 하므로, trace id, locale, tenant id 처럼 작은 값만 넣는다.

크기 제한은 다음과 같이 두 단계로 본다.

- `meta_len` wire 필드가 표현할 수 있는 최댓값은 65535 bytes 다.
- 그러나 `MaxSendMetadataSize` 의 기본값은 1024 bytes 다.
- 따라서 `.NET` connector 는 `MaxSendMetadataSize` 를 넘어가는 metadata 를 보내려 하면,
  이를 error 로 처리해야 한다.

helper header 에 들어가는 모든 multi-byte integer 는 network byte order 로 표기한다.

decode 단계에서는 다음 경우를 모두 decode error 로 본다.

- 알 수 없는 kind, codec, flag bit 가 등장한 경우
- `HasRequestSeq`, `HasMetadata` flag 와 실제 필드의 존재 여부가 어긋나는 경우

metadata bytes 는 다음과 같이 순서대로 늘어선 binary key-value 목록이다.

```text
+---------------+-------------+-------------+
| count u8      | entry...    | entry...    |
+---------------+-------------+-------------+

entry:
+-------------+-------------+-------------+-------------+
| key_len u8  | key bytes   | val_len u16 | value bytes |
+-------------+-------------+-------------+-------------+
```

metadata 의 key 와 value 는 모두 UTF-8 문자열이다. 규칙은 다음과 같다.

- `key_len` 은 1 이상이어야 한다.
- 같은 key 가 두 번 등장하면 decode error 다.
- `count` 값은 뒤따라오는 entry 개수와 반드시 일치해야 한다.

helper header 의 `flags` 값은 공통 스펙과 동일하게 맞춘다.

| flag | value | 의미 |
|------|-------|------|
| has request seq | `0x01` | `request_seq` 필드가 있다 |
| has metadata | `0x02` | `meta` 필드가 있다 |
| body compressed | `0x04` | body가 압축된 상태다 |

압축 알고리즘은 packet 마다 header 에 따로 적지 않는다. 대신 `.NET` connector 의
`Compression` option 으로 한 번만 정한다. `body compressed` flag 는 단지 "이 packet
의 body 가 그 알고리즘으로 압축되어 있다" 는 표시일 뿐이다.

주의할 점이 하나 있다. 이 옵션이 client → server 자동 압축을 켜는 것은 아니다. client
에서 server 로 보낼 때는, send / request builder 에서 `.Compress()` 를 명시한 경우에만
압축이 적용된다.

## 6. Metadata API 초안

이 절에서는 사용자가 packet 에 추가로 실을 수 있는 값을 어떤 모양으로 노출하는지
정리한다.

사용자 API 는 임의의 header schema 를 그대로 받지 않는다. 즉 다음과 같은 제약을 둔다.

- connector 는 정해진 helper header 를 사용한다.
- 사용자가 추가로 실어 보낼 수 있는 값은, 작은 metadata key-value 쌍으로만 제한한다.
- 임의 header bytes 를 직접 다루는 API 는 connector 의 공개 표면에 두지 않는다.

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

metadata 의 key 와 value 는 UTF-8 문자열로 인코딩한다. 구현은 다음 경우를 모두
validation error 로 잡아내야 한다.

- key 중복
- 빈 key
- 지나치게 큰 metadata

## 7. Packet Name API 초안

이 절에서는 packet 의 이름을 어떻게 정하고, 사용자가 이름을 바꾸려면 어떤 표면을
거치는지 정리한다.

packet 이름은 helper header 에 그대로 실려 나간다. 따라서 짧아야 한다. 기본 이름은
CLR 타입 이름을 사용한다. 다만 사용자는 attribute 나 resolver 를 통해 이름을 다르게
지정할 수 있어야 한다.

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

resolver 가 돌려준 이름은 UTF-8 기준 255 bytes 를 넘으면 안 된다.

## 8. Connector API 초안

이 절에서는 연결을 열고 닫는 표면, 그리고 send / request / subscribe 의 진입점을 정리한다.

```csharp
public sealed class ZlinkStreamConnector : IAsyncDisposable
{
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

    public ZlinkStreamSendBuilder Send(
        ZlinkStreamEncodedBody body);

    public ZlinkStreamRequestBuilder Request(
        ZlinkStreamEncodedBody body);

    public IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedBody>, CancellationToken, ValueTask> handler);
}
```

connector 는 이 문서에서 정의한 helper header 만 decode 한다. 수신한 packet 은
`On(...)` 에 등록해 둔 이름별 handler 로 전달한다. helper header decode 가 실패하면,
오류만 `ErrorReceived` 로 전달한다. packet body 를 raw 상태 그대로 사용자에게
넘겨주지는 않는다.

`Send(...).Submit(...)` 은 응답을 기다리지 않는 submit API 다. 실제 transport write 는
connector 내부에서 비동기로 수행된다.

submit 시점의 동작은 두 갈래로 나뉜다.

- 호출 시점에는 즉시 판단할 수 있는 validation 실패만 예외로 던진다. 예를 들어 frame
  size, packet name, metadata size, 연결 상태가 이에 해당한다.
- write 도중에 발생한 오류는 `ErrorReceived` 로 전달한다.

stream connector 의 public option 에는 `SendTimeout` 을 두지 않는다. 이유는 다음과 같다.
connector send 는 request / reply 대기와 의미가 다른 fire-and-forget submit 이다.
여기에 timeout 옵션을 노출하면, request timeout 과 의미가 뒤섞이게 된다. connector
에서 timeout 이 필요한 공개 옵션은, request 의 reply 대기용인 `RequestTimeout` 뿐이다.

## 9. Send / Request Builder API 초안

이 절에서는 send / request 를 보낼 때 사용하는 fluent builder 의 모양과, codec 을 어떻게
연결하는지 정리한다.

fluent builder 는 timeout, metadata, cancellation 을 호출 단위로 조절할 수 있게 해 준다.
codec 은 `ZlinkStreamEncodedBody` 안에 들어 있는 값을 그대로 사용한다.

호출 흐름은 다음과 같다.

- 호출자는 `ToJson`, `ToMsgPack`, `ToProto` 같은 명시형 helper 를 통해, body bytes 와
  codec 값을 함께 만든다.
- 각 codec package 는 일반 CLR 객체를 곧장 넘길 수 있는 typed convenience builder 도
  함께 제공한다.
- 예를 들어 JSON package 의 namespace 를 사용하면, `Request(request)` 는 JSON 으로
  body 를 만들고 `Async<TReply>()` 는 JSON 으로 reply 를 읽는다.

```csharp
public sealed class ZlinkStreamSendBuilder
{
    public ZlinkStreamSendBuilder PacketName(string packetName);

    public ZlinkStreamSendBuilder Metadata(string key, string value);

    public ZlinkStreamSendBuilder Metadata(ZlinkStreamMetadata metadata);

    public ZlinkStreamSendBuilder Compress();

    public ValueTask Submit(CancellationToken cancellationToken = default);
}

public sealed class ZlinkStreamRequestBuilder
{
    public ZlinkStreamRequestBuilder PacketName(string packetName);

    public ZlinkStreamRequestBuilder Metadata(string key, string value);

    public ZlinkStreamRequestBuilder Metadata(ZlinkStreamMetadata metadata);

    public ZlinkStreamRequestBuilder Timeout(TimeSpan timeout);

    public ZlinkStreamRequestBuilder Compress();

    public ValueTask<ZlinkStreamEncodedBody> Submit(
        CancellationToken cancellationToken = default);

    public void Submit(
        Action<ZlinkStreamResult> callback);

    public void Submit(
        Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback);
}
```

builder instance 는 한 번만 실행할 수 있다. 같은 builder 에서 `Submit` 을 두 번
호출하면, 이는 validation error 로 처리한다.

```csharp
var reply = await client
    .Request(new GetProfileRequest { AccountId = accountId })
    .Timeout(TimeSpan.FromMilliseconds(200))
    .Metadata("traceId", traceId)
    .Submit<GetProfileReply>(cancellationToken);
```

packet 이름을 따로 명시하지 않으면, 기본 이름은 namespace 를 뺀 body 의 CLR 타입
이름이 된다. 다른 이름이 필요하다면, `ZlinkStreamPacketNameAttribute` 또는
`IZlinkStreamPacketNameResolver` 를 사용한다.

```csharp
client
    .Send(new ChatMessage { Text = "hello" })
    .Submit(cancellationToken);

client
    .Send(new ChatMessage { Text = "hello" })
    .PacketName("chat.message")
    .Metadata("traceId", traceId)
    .Submit(cancellationToken);
```

client 에서 server 로 보내는 방향의 압축은, 명시적으로 호출했을 때만 적용한다.

```csharp
client
    .Send(new UploadReplayChunk { Bytes = chunk })
    .Compress()
    .Submit(cancellationToken);
```

`.Compress()` 의 동작은 다음과 같다.

- body 만 압축한다.
- helper header 의 `flags` 에 `body compressed` 표시를 켠다.

서버 쪽 흐름은 다음과 같이 이어진다.

- 서버 framework 는 wire 의 header / body 를 `IZLinkSessionPacket` 으로 감싼다.
- 서버 쪽 helper 나 actor adapter 는 packet 의 metadata 를 보고, 필요한 경우 body 의
  압축을 해제한다.

## 10. Result / Error API 초안

이 절에서는 성공 / 실패를 어떤 모양으로 사용자에게 돌려주는지, 그리고 async 와 callback
표면이 error 를 어떻게 다르게 처리하는지 정리한다.

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
    UserCallbackFailed,
    // 서버가 kind=Error로 응답했고 request id가 없거나 부합하지 않을 때 발생하는
    // 일반 원격 오류 코드.
    RemoteError
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

두 표면의 error 전달 방식은 서로 다르다.

- async API 는 실패 시 exception 을 던진다.
- callback 기반 API 는 `ErrorReceived` 를 호출한다.

다만 두 경로에서 사용하는 error code 가 가지는 의미는 서로 같아야 한다.

## 11. Codec API 초안

이 절에서는 codec 을 어떻게 갈아 끼우는지, 그리고 자동 선택 helper 가 어떤 우선순위를
따르는지 정리한다.

core 패키지는 codec 을 강제로 정해 두지 않는다. codec package 는 두 가지 표면을
함께 제공한다.

- 명시형 extension method 로 `ZlinkStreamEncodedBody` 를 만들고 읽는 표면.
- typed convenience builder 로 일반 CLR 객체를 그대로 `Send`, `Request`, `On` 에
  넘기는 표면.

```csharp
namespace Systems.Zlink.Stream.Connector.Json;

public static class ZlinkStreamJsonExtensions
{
    ZlinkStreamEncodedBody ToJson<T>(
        this T value,
        JsonSerializerOptions? options = null);

    T FromJson<T>(
        this ZlinkStreamEncodedBody body,
        JsonSerializerOptions? options = null);

    ZlinkStreamJsonRequestBuilder Request<T>(
        this ZlinkStreamConnector connector,
        T body,
        JsonSerializerOptions? options = null);
}

namespace Systems.Zlink.Stream.Connector.MessagePack;

public static class ZlinkStreamMessagePackExtensions
{
    ZlinkStreamEncodedBody ToMsgPack<T>(
        this T value,
        MessagePackSerializerOptions? options = null);

    T FromMsgPack<T>(
        this ZlinkStreamEncodedBody body,
        MessagePackSerializerOptions? options = null);
}

namespace Systems.Zlink.Stream.Connector.Protobuf;

public static class ZlinkStreamProtobufExtensions
{
    ZlinkStreamEncodedBody ToProto<T>(this T value)
        where T : IMessage<T>;

    T FromProto<T>(this ZlinkStreamEncodedBody body)
        where T : IMessage<T>, new();
}
```

connector core 는 타입만 보고서 JSON, MessagePack, Protobuf 가운데 하나를 임의로
고르지 않는다. 어떤 codec 을 사용할지는, 호출자가 import 한 codec package 의 namespace
로 명시한다.

명시 방법은 두 가지다.

- `ToJson`, `ToMsgPack`, `ToProto` 를 직접 호출한다.
- 또는 해당 package 의 `Request<T>()`, `Send<T>()`, `On<T>()` convenience API 를
  활용한다.

`Systems.Zlink.Stream.Connector.Codecs` package 는, 다음 순서로 codec 을 자동 선택하는
편의 API 를 제공한다.

1. `Google.Protobuf.IMessage` 를 구현한 타입이면 Protobuf 를 사용한다.
2. `[MessagePackObject]` 가 붙은 타입이면 MessagePack 을 사용한다.
3. 그 외 일반 CLR 객체는 JSON 을 사용한다.

## 12. Compression Codec API 초안

이 절에서는 압축 코덱의 인터페이스와, 어느 시점에 어떤 오류를 던지는지 정리한다.

compression package 는 아래 인터페이스로 body 의 압축과 해제를 제공한다.

```csharp
public interface IZlinkStreamCompressionCodec
{
    ZlinkStreamCompression Compression { get; }

    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> body);

    ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> body);
}
```

현재 계약이 지원하는 압축 알고리즘은 LZ4 하나다. 서버와 클라이언트가 같은 알고리즘을
쓰도록 설정을 맞추는 책임은, 사용자에게 있다.

기본 동작은 다음과 같다.

- `ZlinkStreamCompression.None` 은 body 를 그대로 둔다.
- `BodyCompressed` flag 가 켜져 있는데도 `Compression` 이 `None` 이거나
  `CompressionCodec` 이 없다면, 이는 decode error 로 처리한다.

## 13. Request Helper

이 절에서는 request / reply 흐름에 필요한 기능과, request 와 response 의 짝짓기 규칙을
정리한다.

request helper 는 `header + body` 전송 위에 얹는 선택 기능이다. 다만 일반 client 에서도
충분히 자주 쓰는 흐름이라, core 패키지 안에 함께 포함한다.

요구 사항은 다음과 같다.

- async request
- callback request
- fluent request builder
- body 타입 이름 또는 명시적인 packet 이름
- optional metadata
- request timeout
- pending request map
- response correlation
- close 시 pending request 실패 처리
- timeout 시 pending request 제거

request sequence 는 header helper 영역에서 다룬다. 다만 사용자가 byte header 를 직접
구성하는 경로를 막지는 않는다.

request / response 규칙은 다음과 같다.

- `Send`는 `kind=Send`이고 `request_seq`가 없는 helper header를 만든다.
- `Request`는 `kind=Request`이고 새 `request_seq`를 가진 helper header를 만든 뒤,
  pending map에 등록한다.
- `kind=Response` packet은 같은 `request_seq`의 pending request를 성공으로 완료시킨다.
- `kind=Error` packet에 `request_seq`가 들어 있다면, 같은 `request_seq`의 pending request를
  실패로 완료시킨다.
- `kind=Error` packet에 `request_seq`가 없다면, connector의 error message로
  `ErrorReceived`에 전달한다.
- `Response`와 `Error`의 packet name은 원래 request의 packet name과 같아야 한다.
- `Error`는 `codec=Json`을 사용한다.
- request timeout, close, disconnect가 발생하면 pending request는 모두 실패 처리되고
  map에서 제거된다.

`Error` 의 body 는 codec 과 무관하게, UTF-8 JSON object 로 인코딩한다.

```json
{"code":"error_code","message":"message"}
```

애플리케이션 도메인의 error 를 정상 reply body 로 다루고 싶다면, `Response` kind 와
사용자 정의 body schema 를 사용한다.

## 14. Codec Extension

이 절에서는 codec 확장 패키지가 할 수 있는 일과, 절대 바꿔서는 안 되는 규칙을 정리한다.

core 패키지는 codec 을 강제하지 않는다. 다음 확장 패키지가 따로 있다. 이들은 packet
이름, optional metadata, body payload 를 만들고 parse 하는 일만 돕는다.

- JSON
- MessagePack
- Protobuf
- Auto Codecs

예시는 다음과 같다.

```csharp
using Systems.Zlink.Stream.Connector.Codecs;

client
    .Send(new ChatMessage("hello"))
    .Submit(cancellationToken);

var reply = await client
    .Request(new ChatRequest("hello"))
    .PacketName("chat.request")
    .Timeout(TimeSpan.FromSeconds(1))
    .Submit<ChatReply>(cancellationToken);
```

전체 흐름은 다음과 같다.

- codec extension 은 body bytes 와 함께 `ZlinkStreamCodec` 값을 만들어 준다.
- connector 는 그 codec 값을 helper header 에 그대로 적는다.
- core API 는 reply 나 handler 에 `ZlinkStreamEncodedBody` 를 그대로 흘려보낸다.
- codec package 의 typed convenience API 가 이를 다시, 지정된 reply / body 타입으로
  풀어 준다.

다만 codec extension 이 transport, timeout, request map, callback dispatch 의 규칙을
바꿔서는 안 된다.

## 15. Compression

이 절에서는 server → client 와 client → server 두 방향의 압축 동작을 따로 정리한다.

server → client 방향은 typed API 에서 자동 압축 해제를 제공한다.

- 서버가 helper header 에 `body compressed` flag 를 켜고 body 를 보낸다.
- 그러면 `.NET` connector 가, typed 사용자 callback 이 호출되기 전에 body 를 미리 압축
  해제해 둔다.
- typed message handler 와 request reply decode 는 모두, 압축이 해제된 body 를 받는다.
- 압축 해제에 실패하면, `DecompressionFailed` error 로 사용자에게 전달한다.

client → server 방향은 명시적으로 호출했을 때만 압축한다.

- 기본 `Send` 와 `Request` 는 압축하지 않는다.
- `.Compress()` 를 호출한 send / request 만 body 를 압축한다.
- 압축이 적용된 packet 에는, helper header 에 `body compressed` flag 를 켠다.

압축은 오직 body 에만 적용한다. helper header 는 절대 압축하지 않는다.

## 16. Unity Adapter

이 절에서는 Unity 패키지가 core 위에 얇게 얹는 부분과, 무엇을 그대로 두는지를 정리한다.

`Systems.Zlink.Stream.Connector.Unity` 는 별도의 Unity package 로 배포한다. 이 Unity
package 는 `Systems.Zlink.Stream.Connector` core 를 참조하고, 그 위에 다음을 얇게 얹는다.

- Unity main thread callback dispatch
- `MonoBehaviour` wrapper
- Unity lifecycle

Unity 측 상세 계약은 [unity-stream-connector.ko.md](./unity-stream-connector.ko.md) 를
기준으로 한다. Unity adapter 는 `ZlinkStreamConnector` 의 packet 의미나 helper header
의미를 임의로 바꾸지 않는다.

## 17. 완료 기준

이 절에서는 `.NET` 구현이 "끝났다" 고 말하려면, 어떤 시나리오가 모두 동작해야 하는지를
정리한다.

`.NET` 구현의 완료 기준은 다음과 같다.

- `tcp://`, `tls://`, `ws://`, `wss://` endpoint에 모두 연결할 수 있다.
- `Send(...).Submit(...)`로 보낸 packet을 framework STREAM 서버가 정상적으로 받는다.
- 서버가 helper header로 만든 packet을 client가 typed handler로 받는다.
- callback request와 `Request(...).Submit(...)`이 각각 정상 동작한다.
- request timeout, close 중 pending request 실패, disconnected 상태에서의 send 동작을
  테스트한다.
- TLS 자체 서명 인증서 검증 옵션을 테스트한다.
- partial read, multi-packet read, send frame limit 동작을 테스트한다.
- JSON, MessagePack, Protobuf extension이 core packet 계약을 바꾸지 않는지 테스트한다.
- server-to-client 방향에서 압축된 body를 typed API가 자동으로 해제한다.
- client-to-server 방향의 압축은 `.Compress()`를 호출한 packet에만 적용된다.
- Unity adapter가 main thread callback dispatch와 lifecycle close를 보장한다.

## 18. 구현 순서

이 절에서는 작업을 어떤 순서로 쌓아 가는지 정리한다. 단 단계마다 공개 계약을 따로
끊어 내려는 목적은 아니다.

구현은 다음 순서로 진행한다. 최종 공개 package 에는 이 문서의 계약을 한 번에 적용한다.

1. TCP transport와 STREAM frame의 encode/decode
2. helper header의 encode/decode, metadata의 encode/decode
3. typed `Send`, `Request`, `On`과 packet name resolver
4. request pending map, timeout, close/disconnect 실패 처리
5. JSON codec
6. MessagePack, Protobuf codec
7. LZ4 compression과 server-to-client 자동 압축 해제
8. TLS, WebSocket, WebSocket over TLS transport
9. Unity adapter package

## 19. 회귀 테스트

이 절에서는 어떤 항목을 어떤 단위로 회귀 테스트로 묶어 두는지, 그리고 API 수정 시
어떤 위치를 함께 맞춰야 하는지 정리한다.

Stream Connector 항목은 다음을 각각 분리해서 검증한다.

- transport frame
- typed request / reply
- metadata
- packet name
- codec
- compression
- error handling

Connector API 를 수정하는 경우에는, 아래 테스트 이름과 문서 설명을 반드시 함께 맞춰
두어야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamConnectorTests.TcpSendUsesHeaderBodyFrame` | TCP transport가 header/body frame 형식을 그대로 사용한다. |
| `StreamConnectorTests.TcpReceiveDispatchesMultipleHeaderPacketsInOrder` | 여러 header packet을 순서대로 callback에 전달한다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | typed request가 request sequence로 response를 정확히 짝짓는다. |
| `StreamConnectorTests.PacketNameAttributeIsUsedByDefault` | packet name attribute가 기본 packet 이름으로 사용된다. |
| `StreamConnectorTests.MetadataSendLimitIsEnforced` | metadata 크기 제한이 send 시점 이전에 적용된다. |
| `StreamConnectorTests.TypedCallbackDecompressesServerPacket` | 압축된 server packet을 typed callback에서 정상 복원한다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^stream]: `STREAM`은 외부 클라이언트와 서버 사이를 잇는, 연결 지향적인 양방향 메시지 통로를 가리키는 ZLink 추상이다.
[^stream-connector]: Stream Connector는 클라이언트(예: .NET app, Unity)에서 STREAM 서버에 접속해 packet을 주고받도록 돕는 클라이언트 측 라이브러리다.
[^packet]: packet은 stream 위에서 한 단위로 묶여 오가는 메시지로, header에 종류·metadata·correlation 정보가, body에 실제 payload가 담긴다.
[^tcp]: TCP는 신뢰성 있는 바이트 스트림 전송을 제공하는 기본 transport 프로토콜이다.
[^tls]: TLS는 TCP 위에 암호화와 인증을 더한 transport다. `tls://` scheme으로 표기한다.
[^ws]: WS(WebSocket)는 HTTP 핸드셰이크 위에 올라가는 양방향 메시지 프레이밍 프로토콜이다.
[^wss]: WSS는 WebSocket을 TLS 위에 올린 형태다. `wss://` scheme으로 표기한다.
[^transport]: transport는 실제 네트워크 위에서 바이트를 실어 나르는 계층을 뜻한다. 그 위에 framework의 packet/session 추상이 올라간다.
[^codec]: codec은 객체를 wire 위에서 주고받을 수 있는 byte 표현으로 직렬화하거나 다시 객체로 복원하는 컴포넌트다(JSON, MessagePack, Protobuf 등).
[^nuget]: NuGet은 `.NET`의 표준 패키지 매니저로, 라이브러리를 package id 단위로 배포·설치한다.
[^spot]: `SPOT`은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 ZLink의 추상이다.
[^wire]: wire는 네트워크 회선 위로 실제로 흘러가는 바이트 형태를 가리키는 표현이다. 같은 메시지라도 메모리상 표현과 wire 표현이 서로 다를 수 있다.
