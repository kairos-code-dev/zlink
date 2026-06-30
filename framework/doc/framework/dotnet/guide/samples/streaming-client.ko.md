<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: ZLink Framework .NET STREAM Samples](stream-samples.ko.md) | [다음: TicTacToe Game Sample](tictactoe-game-sample.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../common/README.ko.md)

[.NET 묶음](../../README.ko.md) | [STREAM](../../spec/aspnet-core-stream.ko.md) | [STREAM 샘플](stream-samples.ko.md) | [Unity 가이드](../../../../../../core/doc/guide/unity-stream-connector.ko.md)

# ZLink Stream Connector For .NET

> 이 문서는 **릴리스 전 초안**이다.
> 즉 아직 배포된 공개 계약[^public-contract]이 아니며, `.NET`과 Unity에서 `ZLink STREAM`[^stream]
> 서버에 접속하는 stream connector[^stream-connector]를 어떤 모양으로 노출할지 정리해 둔
> 문서다. ZLink 를 어디에 쓸지 판단하는 케이스 스터디가 아니라, 외부 client 가
> STREAM 서버와 주고받을 packet 을 만드는 sample/API 설명 문서다.

## 1. 목적

이 절에서는 이 문서가 무엇을 다루는지, 그리고 어디까지가 client 표면의 책임인지 짧게 정리한다.

이 문서는 서버 framework 의 STREAM 모델에 접속하는 `.NET` client(Stream Connector) 를 정리한다. 핵심 목표는 한 가지다. 서버 framework 의 STREAM packet[^packet] 모델과
같은 메시지를, `.NET` client 에서도 똑같이 주고받을 수 있게 하는 것이다.

서버와 client 의 역할은 다음과 같이 나뉜다.

- 서버 framework 의 callback 이 받는 값은 `ZLinkSessionDispatchContext dispatch` 와
  `ZLinkMessage payload` 이다.
- connector 의 typed API 와 fluent API 는, client 쪽에서 wire 의 header / payload 를 만들어
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
| `Zlink.Framework.Codecs.MessagePack` | 선택 | MessagePack framework codec extension |
| `Zlink.Framework.Codecs.Protobuf` | 선택 | Protobuf framework codec extension |

`.NET` Stream Connector 는 NuGet[^nuget] 으로 별도 배포할 수 있어야 한다. NuGet 의
package id 와 `.NET` namespace 는 `Systems.Zlink.Stream.Connector` 계열을 그대로 사용한다.
JSON은 connector core의 기본 codec이다. MessagePack과 Protobuf는 connector 전용 패키지가
아니라 framework codec extension을 등록해서 framework, connector, HTTP client가 같은 codec
정책을 공유한다.

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

    public ZlinkStreamHeartbeatOptions Heartbeat { get; init; } = new();

    public ZlinkStreamReconnectOptions Reconnect { get; init; } = new();

    public int MaxSendPayloadSize { get; init; } = 64 * 1024;

    public int MaxReceivePayloadSize { get; init; } = 64 * 1024;

    public int MaxInboundObserverNotifications { get; init; } = 1024;

    public int MaxInboundObserverPayloadPreviewBytes { get; init; }

    public bool SkipServerCertificateValidation { get; init; }

    public ZlinkStreamDispatchMode DispatchMode { get; init; } =
        ZlinkStreamDispatchMode.Manual;

    public ZlinkStreamCompression Compression { get; init; } = ZlinkStreamCompression.Lz4;

    public IZlinkStreamCompressionCodec? CompressionCodec { get; init; }

    public IZlinkStreamPacketNameResolver NameResolver { get; init; }
}

public sealed class ZlinkStreamHeartbeatOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan Interval { get; init; } = TimeSpan.FromSeconds(1);

    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(5);
}

public sealed class ZlinkStreamReconnectOptions
{
    public bool Enabled { get; init; } = true;

    public TimeSpan InitialDelay { get; init; } = TimeSpan.FromMilliseconds(250);

    public TimeSpan MaxDelay { get; init; } = TimeSpan.FromSeconds(5);

    public double BackoffFactor { get; init; } = 2.0;

    public int? MaxAttempts { get; init; } = 3;
}
```

`SkipServerCertificateValidation` 은 테스트용 자체 서명 인증서를 검증할 때만 쓰는
옵션이다. 운영 환경에서는 기본값 `false` 를 반드시 그대로 두어야 한다.

`MaxSendPayloadSize` 는 connector 가 내보내는 payload bytes 에 대한 기본 보호 장치다.
기본값은 64KB다. 이 한도는 prefix 와 encoded header 를 뺀 payload 기준으로 적용한다.
64KB 를 넘는 payload 가 필요한 애플리케이션은 이 값을 의도적으로 키워야 한다.
`.Compress()` 를 호출한 경우에도 한도 검사는 압축 전 원본 payload 기준으로 한다.
한도를 넘으면 transport write 전에 `FrameTooLarge` 예외가 난다.

`MaxReceivePayloadSize` 는 connector 가 읽어 들이는 payload bytes 에 대한 기본 보호
장치다. 기본값은 64KB이며, TCP/TLS frame 을 할당하기 전과 WebSocket message 를 조립하는
동안 적용한다. LZ4 로 압축된 payload 를 해제할 때도 압축이 풀린 결과가 이 값을 넘으면
`FrameTooLarge` 예외로 처리한다. 이 값은 신뢰할 수 없는 피어가 아주 큰 frame 길이나 압축
결과 길이를 보내 connector 메모리를 소모시키지 못하게 막기 위한 한도다.

metadata encoded payload 는 1024 bytes 고정 한도를 사용한다. header 전체 길이는 STREAM
frame prefix 의 `u16 header_len` 으로 표현되므로 65535 bytes 를 넘을 수 없다. 하지만
metadata 는 route, trace id, locale 같은 작은 key-value 를 담는 용도이므로, 사용자가
한도를 키우는 option 은 제공하지 않는다. 큰 업무 데이터는 payload 로 보내야 한다.

수신 메시지는 `WaitFor`/`ReceivedCount` 용으로 bounded store 에 기록되며, 한도
(`MaxReceivedMessages`, 기본 1024)를 넘으면 가장 오래된 메시지를 제거한다. receive loop는
packet을 읽은 뒤 사용자 callback work item을 dispatch queue에 넣고 다음 read를 계속한다.
기본 `Manual` dispatch mode 에서는 사용자 callback이 느리면 `Dispatch.Async()`를 호출하는
application loop가 늦어질 뿐, network receive loop를 막지 않는다.

사용자 callback 실행은 별도의 dispatch queue로 분리한다. 기본값인
`DispatchMode = Manual`에서는 receive loop, reconnect loop, request callback task가
사용자 handler를 직접 호출하지 않고 queue에 넣는다. 사용자는 자신이 원하는 thread에서
`Dispatch.Async()`를 호출해 `On(...)` handler, lifecycle event, error event, request
callback을 실행한다. `PendingDispatchCount`는 아직 실행되지 않은 callback 수다.

`DispatchMode = Immediate`는 기존 방식처럼 내부 worker 흐름에서 callback을 바로 실행한다.
UI thread나 game loop가 있는 client에서는 `Manual`을 유지해야 한다.

send 경로의 segmented write 는 공개 option 으로 노출하지 않는다. transport 가 prefix,
header, payload 를 나누어 쓸 수 있으면 connector 가 내부에서 자동으로 사용한다. 지원하지
않는 transport 에서는 하나의 frame buffer 로 합쳐서 보낸다.

helper header 의 binary 형식은 connector 와 framework 가 공유하는 내부 프로토콜이다.
application 은 이 형식을 바꾸지 않는다. 이 경계를 고정해야 client 와 server 가 별도
negotiation 없이 같은 frame 을 해석할 수 있다.

`NameResolver` 는 payload 타입에서 packet 이름을 고르는 정책이다. 값을 지정하지 않으면
connector 가 제공하는 내부 기본 resolver 를 사용한다. 기본 resolver 는 namespace 를 제외한
CLR 타입 이름을 사용한다.

반면 수신 payload 에 도메인별로 거는 크기 제한은 connector 의 기본 계약에 포함하지
않는다. 수신 크기 제한이 필요한 애플리케이션이라면, handler 또는 상위 protocol 에서
별도로 검사해야 한다.

`Heartbeat`는 기본으로 켜져 있다. 끄고 싶으면 `Heartbeat.Enabled = false`로 설정한다.
기본값은 1초 interval과 5초 timeout이다.
timeout은 마지막 inbound frame 이후 새 frame이 들어오지 않은 시간을 기준으로 판정한다.
heartbeat ping, heartbeat pong, 사용자 packet 모두 inbound liveness 신호가 된다.

`Reconnect`도 기본으로 켜져 있다. 끄고 싶으면 `Reconnect.Enabled = false`로 설정한다.
기본값은 `InitialDelay = 250ms`, `MaxDelay = 5s`, `BackoffFactor = 2.0`,
`MaxAttempts = 3`이다.

## 5. Packet 모델

이 절에서는 connector helper 가 사용자가 넘긴 payload, packet name, metadata를 어떻게
packet 의미로 묶는지 정리한다. wire header는 connector runtime 내부 프로토콜이며, application이
직접 만들거나 transport에 넘기는 public 객체가 아니다.

```csharp
public sealed record ZlinkStreamEncodedPayload(
    ZlinkStreamCodec Codec,
    ReadOnlyMemory<byte> Payload,
    Type? MessageType = null);
```

사용자 API 는 raw header bytes 를 직접 받지 않는다. 대신 다음 규칙을 따른다.

- 기본 packet 이름은 payload 객체의 CLR 타입 이름을 사용한다.
- 호출자가 명시한 이름이 있다면 그쪽이 우선이다.
- 추가 metadata 가 필요하면 작은 key-value 쌍을 metadata 로 덧붙인다.

```csharp
public sealed record ZlinkStreamMessage(
    string Name,
    ZlinkStreamMetadata Metadata,
    object? Payload);

public sealed record ZlinkStreamMessage<TPayload>(
    string Name,
    ZlinkStreamMetadata Metadata,
    TPayload Payload);
```

`ZlinkStreamMessage` 는 수신 handler와 wait API가 보는 helper 모델이다. connector runtime은
다음 값을 내부 wire header로 인코딩하지만, public API에는 header 객체를 노출하지 않는다.

- `Name`
- `Metadata`
- codec 정보
- request correlation 정보

서버 framework 는 이렇게 만들어진 header 와 payload 를 session callback 에
흘려보낸다.

STREAM frame 의 앞쪽 `2B` 는 connector helper header 가 아니라 `header_size` 다.
따라서 `.NET` helper 가 만들어 내는 packet 도 wire 에서는 다음 순서를 그대로 따른다.

```text
+----------------+----------------+----------------+----------------+
| u16 header_len | u32 payload_sz | header bytes   | payload bytes  |
+----------------+----------------+----------------+----------------+
```

내부 wire header 는 binary header 다. 구조를 이해해야 할 때는 다음 정도만 참고한다.
application code는 이 값을 만들거나 수정하지 않는다.

- `kind` 와 `codec` 은 문자열이 아니라 1 바이트 enum 값으로 인코딩한다.
- packet name 은 `u8 name_len + UTF-8 bytes` 형식이고, 최대 길이는 255 bytes 다.

```text
+---------+----------+----------+------------------+-----------+-------+-------+
| kind u8 | codec u8 | flags u8 | request_seq u64? | name u8+n | meta? | corr? |
+---------+----------+----------+------------------+-----------+-------+-------+
```

STREAM header 값은 connector runtime 내부에서 만든다. client 예제는 header 객체를 직접
만들지 않고, request call에 packet name과 metadata를 설정한다. `request_seq` 는 runtime 이
관리하는 `u64` 형식의 correlation sequence 다. request, response, error response에만
들어간다. 규칙은 다음과 같다.

- 같은 connector instance 안에서 동시에 pending 상태인 request 사이에는, connector 가
  만들어 내는 `request_seq` 값이 결코 중복되면 안 된다.
- 값 `0` 은 사용하지 않는다.

metadata 는 `u16 meta_len + metadata bytes` 형태로 붙는다. metadata 는 header 크기
제한 안에 모두 들어가야 하므로, trace id, locale, tenant id 처럼 작은 값만 넣는다.

크기 제한은 다음과 같이 두 단계로 본다.

- `meta_len` wire 필드가 표현할 수 있는 최댓값은 65535 bytes 다.
- `.NET` connector 는 metadata encoded payload 가 1024 bytes 를 넘으면 보내기 전에
  validation error 로 처리한다.
- 1024 bytes 한도는 public option 으로 조절하지 않는다.

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
| payload compressed | `0x04` | payload가 압축된 상태다 |
| has correlation id | `0x08` | correlation id 필드가 있다 |

`Control` kind는 connector 내부 control frame이다.
현재 `.NET` connector는 `$zlink.heartbeat.ping`과 `$zlink.heartbeat.pong`을 예약한다.
control frame은 `Raw` codec, `None` flags, null request sequence, empty metadata, empty
payload를 사용한다. 응용 packet name은 `$zlink.` prefix를 사용할 수 없다.

압축 알고리즘은 packet 마다 header 에 따로 적지 않는다. 대신 `.NET` connector 의
`Compression` option 으로 한 번만 정한다. `payload compressed` flag 는 단지 "이 packet
의 payload 가 그 알고리즘으로 압축되어 있다" 는 표시일 뿐이다.

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
    string Resolve(Type payloadType);
}
```

resolver 가 돌려준 이름은 UTF-8 기준 255 bytes 를 넘으면 안 된다.

## 8. 연결 생명주기

Connector는 아래 상태를 공개한다.

```csharp
public enum ZlinkStreamConnectionState
{
    Created,
    Connecting,
    Connected,
    Reconnecting,
    Disconnected,
    Closed
}
```

`IsConnected`는 `Connected` 상태에서만 `true`다.
`Close.Async()`와 `DisposeAsync()` 뒤 상태는 `Closed`이며, 같은 connector 객체를 다시
연결하지 않는다.

`Connect.Async()`는 상태별로 아래처럼 동작한다.

| 현재 상태 | 동작 |
|-----------|------|
| `Created` | 초기 연결을 시작한다 |
| `Disconnected` | 수동 재연결을 시작한다 |
| `Connecting` | 이미 진행 중인 연결 시도를 기다린다 |
| `Connected` | 성공으로 즉시 반환한다 |
| `Reconnecting` | 자동 reconnect loop 결과를 기다린다 |
| `Closed` | `ObjectDisposedException`으로 실패한다 |

자동 reconnect는 기본으로 켜져 있으며, `Reconnect.Enabled = false`이면 꺼진다.
reconnect 중 submit은 내부 queue에 저장하지 않고 `Disconnected` 오류로 실패한다.
연결이 끊기면 pending request는 모두 실패하며, reconnect 뒤 자동 재전송하지 않는다.

Heartbeat가 켜져 있으면 connector는 `Heartbeat.Interval`마다 control ping을 보낸다.
`Heartbeat.Timeout` 동안 inbound frame이 없으면 transport를 끊긴 것으로 처리하고
reconnect 정책을 적용한다. `Heartbeat.Enabled = false`여도 inbound heartbeat ping에는 pong으로
응답한다.

## 9. Connector API 초안

이 절에서는 연결을 열고 닫는 표면, 그리고 send / request / subscribe 의 진입점을 정리한다.

```csharp
public interface IZlinkStreamConnector : IAsyncDisposable
{
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    event Func<CancellationToken, ValueTask>? Disconnected;

    event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;

    bool IsConnected { get; }

    ZlinkStreamConnectionState State { get; }

    ZlinkStreamConnectorOptions Options { get; }

    int PendingDispatchCount { get; }

    IZlinkStreamLifecycleCall Connect { get; }

    IZlinkStreamLifecycleCall Close { get; }

    IZlinkStreamLifecycleCall Dispatch { get; }

    IZlinkStreamSendCall Send(
        ZlinkStreamEncodedPayload payload);

    IZlinkStreamRequestCall Request(
        ZlinkStreamEncodedPayload payload);

    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler);

    IDisposable ObserveInbound(
        Func<ZlinkStreamInboundObservation, CancellationToken, ValueTask> observer);

    int ReceivedCount(string name);

    IZlinkStreamWaitCall WaitFor(string name);
}

public sealed record ZlinkStreamConnectionStateChanged(
    ZlinkStreamConnectionState Previous,
    ZlinkStreamConnectionState Current,
    ZlinkStreamError? Error = null);

public static class ZlinkStreamConnectorFactory
{
    public static IZlinkStreamConnector Create(
        ZlinkStreamConnectorOptions options);
}
```

`Create()`는 객체만 만들고 네트워크 연결을 열지 않는다.
호출자는 handler와 event callback을 등록한 뒤 `Connect.Async()`를 호출한다.

connector 는 이 문서에서 정의한 helper header 만 decode 한다. 수신한 packet 은
`On(...)` 에 등록해 둔 이름별 handler 로 전달한다. helper header decode 가 실패하면,
오류만 `ErrorReceived` 로 전달한다. packet payload 를 raw 상태 그대로 사용자에게
넘겨주지는 않는다.

`Send(...).Submit(...)` 은 응답을 기다리지 않는 submit API 다.

submit 시점의 동작은 두 갈래로 나뉜다.

- send API 는 validation 실패와 transport write 실패를 framework 내부 전송 상태로 처리한다.
- send 중 transport 오류가 발생하면 같은 오류를 `ErrorReceived` 로도 알리고,
  connector lifecycle은 연결 끊김 처리로 들어간다.
- callback 기반 request API 는 실패를 callback result로 전달한다.

stream connector 의 public option 에는 `SendTimeout` 을 두지 않는다. 이유는 다음과 같다.
connector send 는 request / reply 대기와 의미가 다른 fire-and-forget submit 이다.
여기에 timeout 옵션을 노출하면, request timeout 과 의미가 뒤섞이게 된다. connector
에서 timeout 이 필요한 공개 옵션은, request 의 reply 대기용인 `RequestTimeout` 뿐이다.

`ObserveInbound(...)`는 수신 frame을 읽기 전용으로 기록하거나 측정하기 위한 등록 API다.
등록은 `Connect.Async()` 전에만 허용한다. observation에는 message kind, packet name,
codec, request sequence, metadata, payload byte length, 압축 여부, 수신 시간이 들어간다.
payload 본문은 기본으로 제공하지 않으며, preview option을 켠 경우에만 제한된 byte copy를
제공한다. observer callback은 receive 경로에서 직접 실행하지 않으므로, 느린 로그 출력이나
metrics 전송이 request 완료와 handler dispatch를 막으면 안 된다. callback 실패는
`ObserverFailed`, queue overflow는 `ObserverDropped` 오류로 보고하지만 원래 frame 처리는
계속된다.

## 10. Send / Request Builder API 초안

이 절에서는 send / request 를 보낼 때 사용하는 fluent builder 의 모양과, codec 을 어떻게
연결하는지 정리한다.

fluent builder 는 timeout, metadata, cancellation 을 호출 단위로 조절할 수 있게 해 준다.
codec 은 `ZlinkStreamEncodedPayload` 안에 들어 있는 값을 그대로 사용한다.

호출 흐름은 다음과 같다.

- 호출자는 `ToJson` helper 로 JSON payload bytes 와 codec 값을 함께 만든다. MessagePack·
  Protobuf 는 connector option 의 `PayloadCodec`(`ZLinkMessagePackCodec.Default` /
  `ZLinkProtobufCodec.Default`)에 codec 을 넣어 처리한다.
- 각 codec package 는 일반 CLR 객체를 곧장 넘길 수 있는 typed convenience builder 도
  함께 제공한다.
- 예를 들어 JSON package 의 namespace 를 사용하면, `Request(request)` 는 JSON 으로
  payload 를 만들고 `Async<TReply>()` 는 JSON 으로 reply 를 읽는다.

```csharp
public interface IZlinkStreamSendCall
{
    IZlinkStreamSendCall PacketName(string packetName);

    IZlinkStreamSendCall Metadata(string key, string value);

    IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamSendCall Compress();

    void Submit(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamRequestCall
{
    IZlinkStreamRequestCall PacketName(string packetName);

    IZlinkStreamRequestCall Metadata(string key, string value);

    IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata);

    IZlinkStreamRequestCall Timeout(TimeSpan timeout);

    IZlinkStreamRequestCall Compress();

    ValueTask<ZlinkStreamEncodedPayload> Async(
        CancellationToken cancellationToken = default);

    void Submit(
        Action<ZlinkStreamResult> callback);

    void Submit(
        Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback);
}
```

builder instance 는 한 번만 실행할 수 있다. 같은 builder 에서 `Submit` 을 두 번
호출하면, 이는 validation error 로 처리한다.

```csharp
var reply = await client
    .Request(new GetProfileRequest { AccountId = accountId })
    .Metadata("traceId", traceId)
    .Async<GetProfileReply>(cancellationToken);
```

packet 이름을 따로 명시하지 않으면, 기본 이름은 namespace 를 뺀 payload 의 CLR 타입
이름이 된다. 다른 이름이 필요하다면, `ZlinkStreamPacketNameAttribute` 또는
`IZlinkStreamPacketNameResolver` 를 사용한다.

```csharp
client
    .Send(new ChatMessage { Text = "hello" })
    .Async(cancellationToken);

client
    .Send(new ChatMessage { Text = "hello" })
    .Metadata("traceId", traceId)
    .Async(cancellationToken);
```

client 에서 server 로 보내는 방향의 압축은, 명시적으로 호출했을 때만 적용한다.

```csharp
client
    .Send(new UploadReplayChunk { Bytes = chunk })
    .Compress()
    .Async(cancellationToken);
```

`.Compress()` 의 동작은 다음과 같다.

- payload 만 압축한다.
- helper header 의 `flags` 에 `payload compressed` 표시를 켠다.

서버 쪽 흐름은 다음과 같이 이어진다.

- 서버 framework 는 wire 의 header / payload 를 decode 해서 session callback 에 넘긴다.
- 서버 쪽 helper 나 actor adapter 는 header 의 metadata 를 보고, 필요한 경우 payload 의
  압축을 해제한다.

## 11. Result / Error API 초안

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
    CompressionFailed,
    TlsValidationFailed,
    DecompressionFailed,
    UserCallbackFailed,
    ObserverFailed,
    ObserverDropped,
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
- callback 기반 request API 는 실패를 `ZlinkStreamResult`/`ZlinkStreamResult<T>` 로 그
  callback 에 넘긴다. `ErrorReceived` 이벤트는 request id 없는 remote error 같은
  stream-level error 에 쓴다.

다만 두 경로에서 사용하는 error code 가 가지는 의미는 서로 같아야 한다.

## 12. Codec Extension

이 절에서는 codec 을 어떻게 갈아 끼우는지 정리한다.

JSON은 framework와 connector의 기본 codec이다. Protobuf와 MessagePack은 connector 전용
패키지가 아니라 framework codec extension package가 제공한다. 같은 extension을 framework
codec registry, HTTP client, stream connector에 등록하면 세 표면이 같은 content type과
stream codec 매핑을 공유한다.

```csharp
using Zlink.Framework.Codecs.MessagePack;

builder.Codecs(codecs => codecs.Use(ZLinkMessagePackCodec.Default));

var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri("tcp://127.0.0.1:9000"),
    PayloadCodec = ZLinkMessagePackCodec.Default,
});
```

custom codec도 같은 규칙을 따른다. 사용자가 만든 package는 `IZLinkCodecExtension`으로
framework serializer와 stream codec 매핑을 등록하고, connector가 사용할 payload codec 구현도
같이 제공한다.

## 13. Compression

이 절에서는 압축 설정과, 어느 시점에 어떤 오류를 던지는지 정리한다.

connector public option 은 `ZlinkStreamCompression` 과 `CompressionCodec` 을 노출한다.
기본 활성 codec은 LZ4다. 기본값은 모든 payload 를 자동 압축한다는 뜻이 아니라,
send/request call 에서 `.Compress()` 를 호출한 frame만 압축한다는 뜻이다.

```csharp
public enum ZlinkStreamCompression
{
    None = 0,
    Lz4 = 1
}
```

서버와 클라이언트가 같은 compression codec 을 쓰도록 설정을 맞추는 책임은 사용자에게
있다. built-in LZ4 대신 custom codec 을 쓰려면 connector option 과 framework
builder 양쪽에 같은 구현을 설정한다.

```csharp
var codec = new MyStreamCompressionCodec();

frameworkOptions.ConfigureStreamCompression()
    .Use(codec); // 이 framework runtime 의 활성 compression codec

var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri("tcp://127.0.0.1:9000"),
    Compression = ZlinkStreamCompression.Lz4,
    CompressionCodec = codec, // server 와 같은 codec 으로 compressed frame 을 복원한다.
});
```

기본 동작은 다음과 같다.

- `ZlinkStreamCompression.Lz4` 는 framework 가 제공하는 LZ4 codec 을 사용한다.
- `.Compress()` 를 호출하지 않은 frame 은 payload 를 그대로 보낸다.
- `ZlinkStreamCompression.None` 은 compressed frame 을 보내거나 받지 않는다는 뜻이다.
- `PayloadCompressed` flag 가 켜져 있는데도 `Compression` 이 `None` 이라면, 이는 decode
  error 로 처리한다. 오류 메시지는 compression codec 이 설정되지 않았다는 뜻을 드러낸다.
- 압축이 풀린 payload 가 `MaxReceivePayloadSize` 를 넘으면 `FrameTooLarge` 로 처리한다.

## 14. Request Helper

이 절에서는 request / reply 흐름에 필요한 기능과, request 와 response 의 짝짓기 규칙을
정리한다.

request helper 는 send와 같은 connector packet call 위에 request/reply matching을 더한 기능이다.
일반 client 에서도 충분히 자주 쓰는 흐름이라, core 패키지 안에 함께 포함한다.

요구 사항은 다음과 같다.

- async request
- callback request
- fluent request builder
- payload 타입 이름 또는 명시적인 packet 이름
- optional metadata
- request timeout
- pending request map
- response correlation
- close 시 pending request 실패 처리
- timeout 시 pending request 제거

request sequence 는 connector runtime이 내부 pending request map과 함께 관리한다. 사용자는
byte header를 직접 구성하지 않고 `Request(...).PacketName(...).Metadata(...)` call을 제출한다.

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

`Error` 의 payload 는 codec 과 무관하게, UTF-8 JSON object 로 인코딩한다.

```json
{"code":"error_code","message":"message"}
```

애플리케이션 도메인의 error 를 정상 reply payload 로 다루고 싶다면, `Response` kind 와
사용자 정의 payload schema 를 사용한다.

## 15. Codec Extension

이 절에서는 codec 확장 패키지가 할 수 있는 일과, 절대 바꿔서는 안 되는 규칙을 정리한다.

JSON은 기본 codec이다. MessagePack과 Protobuf는 framework codec extension package로 제공한다.
이 extension은 packet payload를 encode/decode하고 framework registry에 content type과 stream
codec 매핑을 등록한다.

예시는 다음과 같다.

```csharp
using Zlink.Framework.Codecs.Protobuf;

builder.Codecs(codecs => codecs.Use(ZLinkProtobufCodec.Default));

connector
    .Send(new ChatMessage("hello"))
    .Async(cancellationToken);

var reply = await connector
    .Request(new ChatRequest("hello"))
    .Async<ChatReply>(cancellationToken);
```

전체 흐름은 다음과 같다.

- codec extension 은 payload bytes 와 함께 `ZlinkStreamCodec` 값을 만들어 준다.
- connector 는 그 codec 값을 helper header 에 그대로 적는다.
- core API 는 reply 나 handler 에 `ZlinkStreamEncodedPayload` 를 그대로 흘려보낸다.
- 등록된 extension 이 이를 다시, 지정된 reply / payload 타입으로 풀어 준다.

다만 codec extension 이 transport, timeout, request map, callback dispatch 의 규칙을
바꿔서는 안 된다.

## 16. Compression

이 절에서는 server → client 와 client → server 두 방향의 압축 동작을 따로 정리한다.

server → client 방향은 typed API 에서 자동 압축 해제를 제공한다.

- 서버가 helper header 에 `payload compressed` flag 를 켜고 payload 를 보낸다.
- 그러면 `.NET` connector 가, typed 사용자 callback 이 호출되기 전에 payload 를 미리 압축
  해제해 둔다.
- typed message handler 와 request reply decode 는 모두, 압축이 해제된 payload 를 받는다.
- 압축 해제 결과가 `MaxReceivePayloadSize` 를 넘으면, 사용자 callback 이 호출되기 전에
  `FrameTooLarge` error 로 사용자에게 전달한다.
- 압축 해제에 실패하면, `DecompressionFailed` error 로 사용자에게 전달한다.

client → server 방향은 명시적으로 호출했을 때만 압축한다.

- 기본 `Send` 와 `Request` 는 압축하지 않는다.
- `.Compress()` 를 호출한 send / request 만 payload 를 압축한다.
- 압축이 적용된 packet 에는, helper header 에 `payload compressed` flag 를 켠다.

압축은 오직 payload 에만 적용한다. helper header 는 절대 압축하지 않는다.

## 17. Unity 사용

이 절에서는 Unity에서 별도 connector package 없이 기본 connector를 사용하는 기준만 정리한다.

Unity용 별도 connector package는 두지 않는다. Unity도 `Systems.Zlink.Stream.Connector`
core를 그대로 사용하고, `MonoBehaviour.Update()`에서 `Dispatch.Async()`를 호출한다. 그러면
수신 handler와 lifecycle event가 Unity main thread에서 실행된다.

비동기 실행과 coroutine adapter의 의미는
[framework 공통 정책](../../../common/spec/async-execution-policy.ko.md)을
따른다. Unity에서도 connector의 public API는 일반 `.NET`과 같은 `Task` / `ValueTask`
기반 비동기 표면이다. `StartCoroutine(...)` 중심의 프로젝트에서는 application helper가
awaitable 호출을 감싸는 방식으로 맞춘다.

Unity 사용 예제와 lifecycle 처리 방식은
[Unity Stream Connector 가이드](../../../../../../core/doc/guide/unity-stream-connector.ko.md)
에서 다룬다. 이 가이드는 사용법 문서이며, 별도의 wire protocol이나 별도 public API 계약을
정의하지 않는다.

## 18. 완료 기준

이 절에서는 `.NET` 구현이 "끝났다" 고 말하려면, 어떤 시나리오가 모두 동작해야 하는지를
정리한다.

`.NET` 구현의 완료 기준은 다음과 같다.

- `tcp://`, `tls://`, `ws://`, `wss://` endpoint에 모두 연결할 수 있다.
- `Send(...).Submit(...)`로 보낸 packet을 framework STREAM 서버가 정상적으로 받는다.
- 서버가 helper header로 만든 packet을 client가 typed handler로 받는다.
- callback request와 `Request(...).Async<TReply>(...)`이 각각 정상 동작한다.
- request timeout, close 중 pending request 실패, disconnected 상태에서의 send 동작을
  테스트한다.
- TLS 자체 서명 인증서 검증 옵션을 테스트한다.
- partial read, multi-packet read, send payload limit 동작을 테스트한다.
- receive payload limit 이 TCP/TLS frame 할당 전, WebSocket message 조립 중, LZ4 압축
  해제 전에 적용되는지 테스트한다.
- JSON, MessagePack, Protobuf extension이 core packet 계약을 바꾸지 않는지 테스트한다.
- server-to-client 방향에서 압축된 payload를 typed API가 자동으로 해제한다.
- client-to-server 방향의 압축은 `.Compress()`를 호출한 packet에만 적용된다.
- manual dispatch에서 callback이 `Dispatch.Async()` 호출 thread에서 실행되는지 테스트한다.
- immediate dispatch에서 별도 pump 호출 없이 callback이 실행되는지 테스트한다.

## 19. 구현 순서

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
9. Unity 사용 가이드

## 20. 회귀 테스트

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
- inbound observer

Connector API 를 수정하는 경우에는, 아래 테스트 이름과 문서 설명을 반드시 함께 맞춰
두어야 한다.

| 테스트 케이스 | 확인 기준 |
|---------------|-----------|
| `StreamConnectorTests.TcpSendUsesHeaderPayloadFrame` | TCP transport가 header/payload frame 형식을 그대로 사용한다. |
| `StreamConnectorTests.TcpReceiveDispatchesMultipleHeaderPacketsInOrder` | 여러 header packet을 순서대로 callback에 전달한다. |
| `StreamConnectorTests.TcpTypedRequestCorrelatesResponse` | typed request가 request sequence로 response를 정확히 짝짓는다. |
| `StreamConnectorTests.PacketNameAttributeIsUsedByDefault` | packet name attribute가 기본 packet 이름으로 사용된다. |
| `StreamConnectorTests.MetadataSendLimitIsEnforced` | metadata 크기 제한이 send 시점 이전에 적용된다. |
| `StreamConnectorTests.SendPayloadLimitIsEnforcedBeforeTransportWrite` | send payload 크기 제한이 transport write 전에 적용된다. |
| `StreamConnectorTests.RequestPayloadLimitIsEnforcedBeforeTransportWrite` | request payload 크기 제한이 pending request 전송 전에 적용된다. |
| `StreamConnectorTests.TypedCallbackDecompressesServerPacket` | 압축된 server packet을 typed callback에서 정상 복원한다. |
| `StreamConnectorTests.ManualDispatchRunsHandlerOnDispatchCaller` | 기본 manual dispatch에서 callback이 `Dispatch.Async()` 호출 thread에서 실행된다. |
| `StreamConnectorTests.ImmediateDispatchRunsHandlerWithoutManualDispatch` | immediate dispatch에서는 별도 pump 없이 callback이 실행된다. |

[^public-contract]: public contract는 외부 사용자에게 공개되어 변경 시 호환성을 책임져야 하는 API 표면을 뜻한다.
[^stream]: `STREAM`은 외부 클라이언트와 서버 사이를 잇는, 연결 지향적인 양방향 메시지 통로를 가리키는 ZLink 추상이다.
[^stream-connector]: Stream Connector는 클라이언트(예: .NET app, Unity)에서 STREAM 서버에 접속해 packet을 주고받도록 돕는 클라이언트 측 라이브러리다.
[^packet]: packet은 stream 위에서 한 단위로 묶여 오가는 메시지로, header에 종류·metadata·correlation 정보가, payload에 실제 payload가 담긴다.
[^tcp]: TCP는 신뢰성 있는 바이트 스트림 전송을 제공하는 기본 transport 프로토콜이다.
[^tls]: TLS는 TCP 위에 암호화와 인증을 더한 transport다. `tls://` scheme으로 표기한다.
[^ws]: WS(WebSocket)는 HTTP 핸드셰이크 위에 올라가는 양방향 메시지 프레이밍 프로토콜이다.
[^wss]: WSS는 WebSocket을 TLS 위에 올린 형태다. `wss://` scheme으로 표기한다.
[^transport]: transport는 실제 네트워크 위에서 바이트를 실어 나르는 계층을 뜻한다. 그 위에 framework의 packet/session 추상이 올라간다.
[^codec]: codec은 객체를 wire 위에서 주고받을 수 있는 byte 표현으로 직렬화하거나 다시 객체로 복원하는 컴포넌트다(JSON, MessagePack, Protobuf 등).
[^nuget]: NuGet은 `.NET`의 표준 패키지 매니저로, 라이브러리를 package id 단위로 배포·설치한다.
[^spot]: `SPOT`은 동적으로 생성·소멸되는 논리적 노드(예: room, stage 등) 단위로 메시지를 라우팅하는 ZLink의 추상이다.
[^wire]: wire는 네트워크 회선 위로 실제로 흘러가는 바이트 형태를 가리키는 표현이다. 같은 메시지라도 메모리상 표현과 wire 표현이 서로 다를 수 있다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: ZLink Framework .NET STREAM Samples](stream-samples.ko.md) | [다음: TicTacToe Game Sample](tictactoe-game-sample.ko.md)
<!-- framework-adapter-nav:bottom:end -->
