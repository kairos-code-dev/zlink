[스펙 목차](../../../README.ko.md)

[.NET 묶음](./README.ko.md) | [STREAM](./aspnet-core-stream.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [공통 Streaming Client](../../../streaming-client.ko.md)

# Draft -- ZLink Streaming Client For .NET

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `.NET`과 Unity에서 `ZLink STREAM` 서버에 접속하는
> client connector를 어떤 모양으로 노출할지 정리하기 위한 문서다.

## 1. 목적

이 문서는 [공통 streaming client 초안](../../../streaming-client.ko.md)을 `.NET`
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
| `Zlink.Streaming.Client` | 일반 C# / .NET | TCP, TLS, WS, WSS transport와 packet client core |
| `Zlink.Streaming.Client.Unity` | Unity | Unity main thread callback dispatch와 Unity package metadata |
| `Zlink.Streaming.Client.Json` | 선택 | JSON packet helper |
| `Zlink.Streaming.Client.MessagePack` | 선택 | MessagePack packet helper |
| `Zlink.Streaming.Client.Protobuf` | 선택 | Protobuf packet helper |

Unity 패키지는 일반 C# 패키지 위에 얇게 얹는다. 별도 wire protocol을 만들면 안
된다.

## 3. Transport

`.NET` core 패키지는 아래 네 transport를 모두 지원해야 한다.

```csharp
public enum ZlinkStreamingTransport
{
    Tcp,
    Tls,
    WebSocket,
    WebSocketSecure
}
```

```csharp
public enum ZlinkStreamingCodec : byte
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
public sealed class ZlinkStreamingClientOptions
{
    public required Uri Endpoint { get; init; }

    public ZlinkStreamingTransport? Transport { get; init; }

    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan SendTimeout { get; init; } = TimeSpan.FromSeconds(5);

    public TimeSpan RequestTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan IdleTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public TimeSpan HeartbeatInterval { get; init; } = TimeSpan.FromSeconds(10);

    public TimeSpan HeartbeatTimeout { get; init; } = TimeSpan.FromSeconds(30);

    public int MaxFrameSize { get; init; } = 1024 * 1024;

    public int MaxMetadataSize { get; init; } = 1024;

    public bool SkipServerCertificateValidation { get; init; }

    public bool EnableSegmentedSend { get; init; } = true;

    public ZlinkStreamingCodec DefaultCodec { get; init; } = ZlinkStreamingCodec.Json;

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.None;

    public IZlinkStreamingCodecRegistry? CodecRegistry { get; init; }
}
```

`SkipServerCertificateValidation`은 테스트용 자체 서명 인증서 검증에만 사용한다.
운영 환경에서는 기본값 `false`를 유지해야 한다.

## 5. Packet 모델

wire packet의 최저 레벨 모델은 `header + body`다.

```csharp
public sealed record ZlinkStreamingPacket(
    ReadOnlyMemory<byte> Header,
    ReadOnlyMemory<byte> Body);
```

일반 사용자 API는 raw header를 매번 요구하지 않는다. body 객체의 CLR 타입 이름을
기본 packet 이름으로 쓰고, 호출자가 지정한 이름이 있으면 그 이름을 우선한다.
추가 metadata가 필요하면 JSON object로 표현 가능한 header를 붙인다.

```csharp
public sealed record ZlinkStreamingMessage(
    string Name,
    object? Header,
    object? Body);
```

`ZlinkStreamingMessage`는 helper 모델이다. 실제 transport framing은 항상
`ReadOnlyMemory<byte> Header`와 `ReadOnlyMemory<byte> Body`를 기준으로 한다. helper는
`Name`, optional `Header`, codec 정보, request correlation 정보를 byte header로
인코딩한다. 서버는 이 결과를 기존 STREAM callback에서 `header, body`로 받는다.

STREAM frame의 앞쪽 `2B`는 connector helper header가 아니라 `header_size`다.
connector helper packet version은 `Header` byte 배열 내부의 첫 필드로 둔다.
따라서 `.NET` helper가 만든 packet도 wire에서는 아래 순서를 따른다.

```text
+------------------+------------------+------------------+------------------+
| u16 header_size  | u32 body_size    | header bytes     | body bytes       |
+------------------+------------------+------------------+------------------+
```

helper header v1은 binary header다. `kind`와 `codec`은 문자열이 아니라 1바이트 enum
값으로 인코딩한다. packet name은 `u8 name_len + UTF-8 bytes`이며 최대 255 bytes다.

```text
+--------+---------+----------+----------+-----------+-----------+
| ver u8 | kind u8 | codec u8 | flags u8 | rid u64?  | name u8+n |
+--------+---------+----------+----------+-----------+-----------+
| meta u16+bytes?                                           |
+-----------------------------------------------------------+
```

`.NET` enum 값은 공통 helper header v1 값과 맞춘다.

```csharp
public enum ZlinkStreamMessageKind : byte
{
    Send = 1,
    Request = 2,
    Response = 3,
    Error = 4
}
```

`rid`는 request, response, error response에만 들어가는 `u64` correlation id다.
metadata는 `u16 meta_len + metadata bytes`로 붙인다. metadata가 header 크기 제한 안에
들어가야 하므로 trace id, locale, tenant id 같은 작은 값만 넣는다. `MaxMetadataSize`
기본값은 1024 bytes다. `meta_len` wire 필드가 표현할 수 있는 최댓값은 65535 bytes지만,
`.NET` connector는 `MaxMetadataSize`를 넘는 metadata를 보내거나 받으면 error로
처리해야 한다.

helper header v1 `flags` 값은 공통 스펙과 맞춘다.

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

## 6. Client API 초안

```csharp
public sealed class ZlinkStreamingClient : IAsyncDisposable
{
    public event Func<ZlinkStreamingPacket, CancellationToken, ValueTask>? PacketReceived;

    public event Func<ZlinkStreamingError, CancellationToken, ValueTask>? ErrorReceived;

    public event Func<CancellationToken, ValueTask>? Disconnected;

    public bool IsConnected { get; }

    public static ValueTask<ZlinkStreamingClient> ConnectAsync(
        ZlinkStreamingClientOptions options,
        CancellationToken cancellationToken = default);

    public ValueTask ConnectAsync(
        CancellationToken cancellationToken = default);

    public ValueTask CloseAsync(
        CancellationToken cancellationToken = default);

    public ValueTask SendAsync(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken = default);

    public ValueTask SendAsync(
        ZlinkStreamingPacket packet,
        CancellationToken cancellationToken = default);

    public ZlinkStreamingSendBuilder<TBody> Send<TBody>(
        TBody body);

    public ZlinkStreamingSendBuilder<TBody> Send<TBody>(
        string name,
        TBody body);

    public ValueTask<ZlinkStreamingPacket> ReceiveAsync(
        CancellationToken cancellationToken = default);

    public ValueTask<ZlinkStreamingPacket> RequestAsync(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken = default);

    public void Request(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        Action<ZlinkStreamingPacket> callback);

    public ZlinkStreamingRequestBuilder<TBody> Request<TBody>(
        TBody body);

    public ZlinkStreamingRequestBuilder<TBody> Request<TBody>(
        string name,
        TBody body);
}
```

`PacketReceived`와 `ReceiveAsync`는 같은 receive stream을 공유하므로 같은 client에서
동시에 섞어 쓰는 것을 제한할 수 있다. 구현은 둘을 동시에 허용할지, configuration
error로 막을지 명확히 정해야 한다.

fluent builder는 timeout, header metadata, cancellation을 호출 단위로 조정할 수
있게 한다. codec은 기본적으로 client option 또는 type registry에서 결정한다.
일반 request/send 호출에서는 codec 선택 메서드를 매번 호출하지 않는다.

```csharp
var reply = await client
    .Request(new GetProfileRequest { AccountId = accountId })
    .WithTimeout(TimeSpan.FromMilliseconds(200))
    .Header(header)
    .ExecAsync<GetProfileReply>(cancellationToken);
```

`Header(header)`의 인자는 JSON object로 표현 가능한 값이어야 한다. 예를 들어
`JsonObject`, `JsonElement`, `IReadOnlyDictionary<string, object?>`, 익명 객체를
허용할 수 있다. 구현은 지원 타입을 명확히 문서화해야 한다.
`Header(header)`는 body payload를 대체하지 않는다. tracing id, locale, tenant id처럼
작은 routing 또는 metadata 값만 넣는 용도다.

packet 이름을 명시하지 않은 경우 기본 이름은 body CLR 타입 이름이다. 구현은 기본
이름 규칙이 namespace를 포함하는지, 짧은 이름만 쓰는지, attribute로 override할 수
있는지 정해야 한다.

```csharp
await client
    .Send(new ChatMessage { Text = "hello" })
    .ExecAsync(cancellationToken);

await client
    .Send("chat.message", new ChatMessage { Text = "hello" })
    .Header(new { TraceId = traceId })
    .ExecAsync(cancellationToken);
```

client-to-server compression은 명시 호출에서만 적용한다.

```csharp
await client
    .Send(new UploadReplayChunk { Bytes = chunk })
    .Compress()
    .ExecAsync(cancellationToken);
```

`.Compress()`는 body만 압축하고 helper header `flags`에 `body compressed`를 표시한다.
서버 framework는 기존처럼 `header, body`를 받는다. 서버 쪽 helper나 actor adapter는
helper header를 보고 필요하면 body를 압축 해제한다.

## 7. Error 모델

```csharp
public sealed record ZlinkStreamingError(
    ZlinkStreamingErrorCode Code,
    string Message,
    Exception? Exception = null);

public enum ZlinkStreamingErrorCode
{
    Disconnected,
    RequestTimeout,
    ConnectTimeout,
    FrameDecodeFailed,
    FrameTooLarge,
    SendFailed,
    TlsValidationFailed,
    DecompressionFailed,
    UserCallbackFailed
}
```

async API는 실패 시 exception을 던진다. callback 기반 API는 `ErrorReceived`를
호출한다. 두 경로의 error code 의미는 같아야 한다.

## 8. Request Helper

request helper는 `header + body` 전송 위의 선택 기능이다. 일반 client도 많이 쓰는
흐름이므로 core 패키지에 포함한다.

요구 사항:

- async request
- callback request
- fluent request builder
- body 타입 이름 또는 명시 packet 이름
- optional JSON object header metadata
- request timeout
- pending request map
- response correlation
- close 시 pending request 실패 처리
- timeout 시 pending request 제거

correlation id는 header helper 영역에 넣는다. 단, 사용자가 직접 byte header를
구성하는 경우를 막지 않는다.

## 9. Codec Extension

core 패키지는 codec을 강제하지 않는다. 아래 확장 패키지는 packet 이름, optional
header metadata, body payload 생성과 parse만 돕는다.

- JSON
- MessagePack
- Protobuf

예시:

```csharp
await client
    .Send(new ChatMessage("hello"))
    .ExecAsync(cancellationToken);

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

## 10. Compression

server-to-client 방향은 자동 압축 해제를 제공한다.

- 서버가 helper header `body compressed` flag를 켜고 body를 보내면 `.NET` connector는
  사용자 callback 전에 body를 압축 해제한다.
- `PacketReceived`, typed message handler, `ReceiveAsync`는 압축 해제된 body를 받는다.
- 압축 해제 실패는 `DecompressionFailed` error로 사용자에게 전달한다.

client-to-server 방향은 명시 호출일 때만 압축한다.

- 기본 `Send`와 `Request`는 압축하지 않는다.
- `.Compress()`를 호출한 send/request만 body를 압축한다.
- 압축된 packet에는 helper header `body compressed` flag를 켠다.
- raw `SendAsync(header, body)` API는 자동 압축을 적용하지 않는다.

압축은 body에만 적용한다. helper header는 압축하지 않는다.

## 11. Unity Adapter

`Zlink.Streaming.Client.Unity`는 일반 C# client를 감싼다.

필수 기능:

- Unity main thread callback dispatch
- `MonoBehaviour` 예제
- `Update()`에서 callback queue drain
- Unity package metadata
- play mode test

Unity adapter는 `ZlinkStreamingClient`의 packet 의미를 바꾸지 않는다.

### 11.1 Unity 패키지 구조

Unity 패키지는 Unity Package Manager에서 직접 참조할 수 있는 구조를 따른다.

권장 package 이름:

- package id: `dev.kairoscode.zlink.streaming.client`
- display name: `ZLink Streaming Client`

권장 폴더 구조:

```text
Zlink.Streaming.Client.Unity/
  package.json
  Runtime/
    Zlink.Streaming.Client.Unity.asmdef
    ZlinkStreamingClientBehaviour.cs
    ZlinkUnityCallbackDispatcher.cs
  Samples~/
    BasicStreamingClient/
      BasicStreamingClient.cs
```

Unity `Runtime/` 코드는 일반 C# core client를 재구현하지 않는다. TCP, TLS, WS, WSS
transport와 frame codec은 `Zlink.Streaming.Client` core를 사용한다.

### 11.2 Unity public surface 초안

Unity 사용자는 `MonoBehaviour` wrapper를 통해 연결과 callback을 다룰 수 있어야
한다.

```csharp
public sealed class ZlinkStreamingClientBehaviour : MonoBehaviour
{
    public string Endpoint { get; set; } = "tcp://127.0.0.1:18082";

    public bool ConnectOnStart { get; set; }

    public bool IsConnected { get; }

    public event Action<ZlinkStreamingPacket>? PacketReceived;

    public event Action<ZlinkStreamingError>? ErrorReceived;

    public event Action? Disconnected;

    public Task ConnectAsync(CancellationToken cancellationToken = default);

    public Task SendAsync(
        ReadOnlyMemory<byte> header,
        ReadOnlyMemory<byte> body,
        CancellationToken cancellationToken = default);

    public Task CloseAsync(CancellationToken cancellationToken = default);
}
```

Unity wrapper는 `Task`를 노출할 수 있지만, 사용자 callback은 Unity main thread에서
호출해야 한다. `PacketReceived` 안에서 `GameObject`, `Transform`, `UI` 같은 Unity
객체를 직접 다룰 수 있어야 하기 때문이다.

### 11.3 Callback dispatch

core client의 receive loop는 worker thread에서 실행될 수 있다. Unity adapter는
worker thread에서 사용자 callback을 직접 호출하지 않는다.

권장 동작:

1. core client가 packet/error/disconnect event를 받는다.
2. Unity adapter가 event를 thread-safe queue에 넣는다.
3. `Update()`에서 queue를 비운다.
4. 사용자 callback을 Unity main thread에서 호출한다.

callback queue에는 최대 pending 개수를 둘 수 있다. 초과 시 drop할지 disconnect할지
옵션으로 정한다.

```csharp
public sealed class ZlinkUnityDispatchOptions
{
    public int MaxPendingCallbacks { get; init; } = 4096;

    public ZlinkUnityCallbackOverflowPolicy OverflowPolicy { get; init; }
}

public enum ZlinkUnityCallbackOverflowPolicy
{
    Disconnect,
    DropNewest,
    DropOldest
}
```

기본값은 `Disconnect`가 적합하다. callback queue overflow는 사용자가 packet을
처리하지 못하고 있다는 뜻이므로 조용히 유실시키면 문제를 찾기 어렵다.

### 11.4 Unity lifecycle

Unity adapter는 아래 lifecycle을 지켜야 한다.

- `Start()`에서 `ConnectOnStart`가 켜져 있으면 연결을 시작한다.
- `OnDestroy()`에서 client를 닫는다.
- `OnApplicationPause(true)` 동작은 자동 disconnect로 고정하지 않는다.
- pause/resume 정책은 옵션으로 둔다.
- domain reload와 play mode 종료 중 dispose가 여러 번 호출되어도 안전해야 한다.

```csharp
public enum ZlinkUnityPausePolicy
{
    KeepConnection,
    CloseOnPause,
    CloseAndReconnectOnResume
}
```

모바일 환경에서는 pause 중 네트워크가 끊길 수 있으므로 `Disconnected` callback과
명시적 reconnect helper를 제공해야 한다. 자동 reconnect를 기본값으로 켜지는 않는다.

### 11.5 Unity transport 기준

Unity adapter는 아래 transport를 모두 사용할 수 있어야 한다.

- TCP
- TLS over TCP
- WebSocket
- WebSocket over TLS

다만 플랫폼마다 사용 가능한 API가 다르다. 예를 들어 WebGL 빌드는 raw TCP를 사용할
수 없으므로 `ws://` 또는 `wss://`만 허용될 수 있다. 이런 제한은 runtime error보다
configuration validation에서 먼저 알려야 한다.

### 11.6 Unity sample 기준

Unity sample은 게임 도메인을 넣지 않는다. 아래만 보여 준다.

1. endpoint 입력
2. connect 버튼
3. packet name과 body 입력
4. send 버튼
5. received packet log
6. disconnect 버튼

채팅방, room, actor 같은 샘플은 Unity adapter sample이 아니라 별도 application
sample에서 다룬다.

### 11.7 Unity 테스트 기준

필수 테스트:

- play mode에서 TCP echo
- play mode에서 WebSocket echo
- callback이 Unity main thread에서 호출되는지 검증
- `OnDestroy()`가 close를 호출하는지 검증
- callback queue overflow 정책 검증
- WebGL에서 TCP endpoint를 거부하는 validation 검증

TLS/WSS 테스트는 인증서 fixture가 준비된 CI 환경에서 실행한다.

## 12. 완료 기준

`.NET` 구현 완료 기준은 아래와 같다.

- `tcp://`, `tls://`, `ws://`, `wss://` endpoint에 연결할 수 있다.
- `SendAsync(header, body)`로 보낸 packet을 framework STREAM 서버가 받는다.
- 서버 `stream.Write(header, body)` packet을 client가 받는다.
- callback receive와 `ReceiveAsync`가 각각 동작한다.
- callback request와 `RequestAsync`가 각각 동작한다.
- request timeout, close 중 pending request 실패, disconnected send를 테스트한다.
- TLS 자체 서명 인증서 검증 옵션을 테스트한다.
- partial read, multi-packet read, large frame limit을 테스트한다.
- JSON, MessagePack, Protobuf extension이 core packet 계약을 바꾸지 않는지 테스트한다.
- server-to-client compressed body를 자동으로 압축 해제한다.
- client-to-server compression은 `.Compress()`를 호출한 packet에만 적용한다.
- Unity adapter가 main thread callback dispatch, lifecycle close, platform별
  transport validation을 보장한다.
