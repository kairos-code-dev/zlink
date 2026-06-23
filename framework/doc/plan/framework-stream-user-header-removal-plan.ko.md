# Framework Stream 사용자 header 제거 계획

## 목적

STREAM wire header는 framework와 connector runtime이 소유하는 내부 프로토콜이다. 이 header에는
packet name, message kind, codec, request sequence, compression flag, correlation id, metadata가
들어간다. 이 값들 중 일부는 request/reply matching과 actor relay에 필요하지만, 사용자가 직접 만들거나
수정할 공개 객체가 아니다.

현재 일부 framework 표면은 session callback과 actor relay에 `ZLinkStreamHeader` 계열 객체를 그대로
넘긴다. Node framework는 `ZlinkStreamHeader = unknown`과 `headerDecoder`까지 제공해서 application이
자체 header 모양을 만들 수 있다. 이 계획은 그런 사용자 header 표면을 제거하고, application이 필요한
부가 정보는 metadata와 작은 dispatch context로만 읽게 만드는 것이다.

호환성은 목표가 아니다. 기존 public header 인자와 custom header hook은 제거한다. 다만 내부 wire header
codec, byte layout, request sequence 보존, correlation echo, compression 처리는 유지한다.

## 원칙

1. 내부 STREAM header는 유지한다. 제거 대상은 사용자가 header 객체를 만들거나 session/relay API에
   직접 넘기는 public 표면이다.
2. application 부가 정보는 metadata로 보낸다. `trace-id`, `tenant-id`, `actor-id`, `locale` 같은 값은
   header subclass나 custom decoder가 아니라 metadata key-value로 표현한다.
3. packet 식별은 packet name으로만 노출한다. handler matching에는 `packetName` 또는 type metadata를
   사용하고, header flag나 request sequence를 읽게 하지 않는다.
4. request sequence, compression flag, correlation id는 runtime 내부 값이다. session code가 이 값을
   읽거나 바꾸지 않아도 reply와 actor relay가 같은 의미로 동작해야 한다.
5. relay API는 원본 header를 인자로 받지 않는다. runtime이 현재 inbound dispatch context를 보존해서
   bound actor route와 reply 경로에 필요한 내부 header를 만든다.
6. connector API는 `send/request(...).packetName(...).metadata(...).compress()` 모양을 유지한다.
   사용자가 header를 직접 encode/decode하거나 header 객체를 submit하지 않는다.
7. server/framework public API는 언어별로 하나의 payload 타입만 받는다. `_raw` 접미사가 붙은 API와
   같은 의미의 중복 overload는 제거한다. .NET, Java/Kotlin, Node.js는 `ZLinkMessage` 계열 타입을
   쓰고, C++는 코덱 교체와 가장 덜 묶이는 `zlink::message_t`를 쓴다.
   connector처럼 이미 encoded payload를 직접 다루는 client 계층은 이 규칙의 예외다. 이 계획에서
   제거하는 것은 server/framework session, reply, actor relay의 raw overload다.

## 범위

| 영역 | 포함 |
|------|------|
| C++ framework/server | session callback, `stream_t`, `session_actor_t` relay 표면, public header 노출 정리 |
| .NET framework/server | `IZLinkSession`, session packet handler, `IZLinkSessionActor`, session context relay/reply 경로 |
| Java framework/server | `ZLinkSession`, `ZLinkSessionPacketHandler`, `ZLinkSessionActor`, Spring/Kotlin adapter 표면 |
| Kotlin framework/server | suspend session base class와 Kotlin handler adapter의 header 인자 제거 |
| Node framework/server | `ZlinkStreamHeader = unknown`, `headerDecoder`, session/actor relay header 인자 제거 |
| Connector | C++, .NET, Java/Kotlin, Node connector의 사용자 header 생성/codec export 정리 |
| 문서와 sample | framework/connector guide, spec, sample, test fixture의 header 사용 예시 갱신 |

범위 밖:

- 내부 wire header byte layout 변경
- core STREAM protocol 변경
- metadata wire format 변경
- codec extension 등록 방식 변경
- HTTP client header 정책 변경

## 공통 To-be 모델

### 공개 inbound dispatch context

서버 session code는 원본 header 대신 작은 dispatch context를 받는다.

| 필드 | 공개 여부 | 설명 |
|------|-----------|------|
| `packetName` | 공개 | handler 선택과 actor 선택에 쓰는 packet 이름 |
| `metadata` | 공개 | application 부가 정보의 immutable snapshot |
| `messageKind` 또는 `canReply` | 선택 공개 | send/request 구분이 필요한 경우 request sequence 없이 boolean 또는 enum으로 제공 |
| `contentType` | 선택 공개 | codec registry가 이해하는 content type만 노출 |
| request sequence | 비공개 | reply matching을 위해 runtime 내부에 보존 |
| header flags | 비공개 | compression, metadata 존재 여부, correlation flag는 runtime 내부 값 |
| correlation id | 비공개 기본값 | tracing runtime이 쓰며 application metadata로 복제하지 않는다 |
| raw header bytes | 비공개 | framework/connector protocol owner만 접근 |

### Relay/reply 보존 방식

현재 `relay(payload)`는 사용자가 원본 header를 다시 넘기는 방식이다. 변경 후에는 runtime이
현재 dispatch context에 붙은 내부 header snapshot을 보존한다.

```text
inbound wire header -> internal dispatch state -> public dispatch context
                                      |
                                      +-> reply / actor relay internal header
```

사용자 코드는 `header`를 저장하거나 복사하지 않는다. session callback 밖에서 inbound relay를 호출하면
runtime은 명확한 오류를 낸다. session 밖에서 client에게 보내는 push는 기존 bound session send API를
사용한다.

## 인터페이스 변경 계획

### .NET

#### Connector

| 현재 | 변경 후 |
|------|---------|
| `ZlinkStreamHeader`가 connector public model에 있음 | public export에서 제거하거나 internal model로 이동 |
| `ZlinkStreamHeaderFlags`, `ZlinkStreamRequestSeq`가 header 조립에 사용 가능 | 일반 사용자가 조립하지 않도록 internal로 이동. inbound observation에 필요한 값은 별도 snapshot 필드로 유지 |
| `ZlinkStreamHeaderCodec`는 internal | 유지. public export 금지 |
| `Send(...).PacketName(...).Metadata(...).Compress()` | 유지 |
| `Request(...).PacketName(...).Metadata(...).Compress()` | 유지 |
| `On(...)`, `WaitFor(...)`가 `ZlinkStreamMessage`를 제공 | 유지. message의 `Name`, `Metadata`, `Payload`가 public 수신 표면 |
| `ObserveInbound(...)`가 `ZlinkStreamInboundObservation` 제공 | 유지. 단 observation은 header 객체가 아니라 snapshot이다 |

#### Server framework

| 현재 | 변경 후 |
|------|---------|
| `IZLinkSession.OnDispatchAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, CancellationToken)` | `OnDispatchAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, CancellationToken)` |
| `IZLinkSessionPacketHandler<T>.HandleAsync(T context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...)` | `HandleAsync(T context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...)` |
| `IZLinkSessionPacketDispatcher.TryHandleAsync(context, header, payload, ...)` | `TryHandleAsync(context, dispatch, payload, ...)` |
| `IZLinkSessionActor.RelayAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...)` | `RelayAsync(ZLinkMessage payload, ...)`. 현재 session dispatch 안에서만 허용 |
| `IZLinkSessionActor.RelayRawAsync(...)`와 core `Message` overload | 제거. framework public 표면은 `ZLinkMessage`만 사용 |
| `ZLinkSessionContext.CurrentDispatchHeader` 내부 상태 | `CurrentDispatchState` 같은 internal 타입으로 변경. public 노출 금지 |
| `ZLinkSessionStreamTransport`가 reply header를 직접 구성 | 유지하되 internal dispatch state에서 request sequence, codec, correlation id를 읽음 |

새 public 타입 초안:

```csharp
public sealed class ZLinkSessionDispatchContext
{
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public bool CanReply { get; }
}
```

`CanReply`는 request sequence 값을 공개하지 않고 reply 가능 여부만 알려준다. `Metadata`는 immutable
snapshot이다.

### Java

#### Connector

| 현재 | 변경 후 |
|------|---------|
| connector user API가 `ZLinkStreamMessage` 중심 | 유지 |
| `ZLinkStreamWireProtocol` / header codec이 package 내부에서 header를 encode/decode | 유지. 이미 package-private이므로 public 제거 대상이 아니라 문서와 test helper 정리 대상 |
| `ZLinkStreamSendCall.packetName(...)`, `metadata(...)`, `compress()` | 유지 |
| `ZLinkStreamRequestCall.packetName(...)`, `metadata(...)`, `compress()` | 유지 |
| `ZLinkStreamInboundObservation` | 유지. header 객체가 아니라 snapshot으로 명시 |
| interop/test에서 header codec 직접 호출 | test helper 또는 internal test package로 이동 |

#### Server framework

| 현재 | 변경 후 |
|------|---------|
| `ZLinkSession.onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload)` | `onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload)` |
| `ZLinkSessionPacketHandler.handle(context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload)` | `handle(context, ZLinkSessionDispatchContext dispatch, ZLinkMessage payload)` |
| `ZLinkSessionPacketDispatcher.tryHandleAsync(context, header, payload)` | `tryHandleAsync(context, dispatch, payload)` |
| `ZLinkSessionActor.relay(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload)` | `relay(ZLinkMessage payload)` |
| `ZLinkStreamHeader` record in public `systems.zlink.framework.streams` | runtime/internal package로 이동하거나 public constructor 제거. handler 계약에서는 사용 금지 |
| `ZLinkStreamHeaderCodec` | internal runtime codec으로 유지 |

새 public 타입 초안:

```java
public record ZLinkSessionDispatchContext(
    String packetName,
    ZLinkMessageMetadata metadata,
    boolean canReply
) {}
```

`ZLinkMessageMetadata`가 아직 connector metadata와 분리되어 있지 않은 언어에서는 먼저 immutable wrapper를
추가한 뒤 session dispatch context에서 그 wrapper를 사용한다.

### Kotlin

Kotlin은 Java runtime 위의 adapter이므로 wire/runtime 변경은 Java 계획을 따른다. Kotlin에서 별도로
바꿀 표면은 suspend handler와 sample 코드다.

#### Connector

| 현재 | 변경 후 |
|------|---------|
| Java connector API를 Kotlin에서 그대로 사용 | 유지 |
| Kotlin sample이 `metadata(...)`, `packetName(...)` fluent call 사용 | 유지 |
| Kotlin code가 Java `ZLinkStreamHeader`를 직접 생성하는 경로 | 제거 |

#### Server framework

| 현재 | 변경 후 |
|------|---------|
| `onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage)` | `onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage)` |
| suspending session packet handler의 `dispatch: ZLinkSessionDispatchContext` 인자 | `dispatch: ZLinkSessionDispatchContext` |
| sample의 `requireActor(dispatch.packetName()).relay(payload)` | `requireActor(dispatch.packetName()).relay(payload)` |
| handler에서 `header.metadata()` 조회 | `dispatch.metadata()` 조회 |

Kotlin 문서는 Java 공통 계약을 링크하되, 예제 코드는 Kotlin suspend 표면의 새 signature를 직접 보여준다.

### Node.js

#### Connector

| 현재 | 변경 후 |
|------|---------|
| `ZlinkStreamHeader` interface export | public export에서 제거. runtime 내부 타입으로 이동 |
| `ZlinkStreamHeaderCodec` export | public export에서 제거. interop test는 internal test helper 사용 |
| `buildHeader(...)` runtime helper | internal 유지 |
| `send(...).packetName(...).metadata(...).compress()` | 유지 |
| `request(...).packetName(...).metadata(...).compress()` | 유지 |
| `on(...)`, `waitFor(...)`가 `ZlinkStreamMessage` 제공 | 유지 |
| `observeInbound(...)`가 `ZlinkStreamInboundObservation` 제공 | 유지. `requestSeq`는 observation snapshot에만 남기고 header 생성 API는 제공하지 않음 |

#### Server framework

| 현재 | 변경 후 |
|------|---------|
| `export type ZlinkStreamHeader = unknown` | 제거 |
| `ZLinkStreamSessionRuntimeOptions.headerDecoder` | 제거 |
| `ZLinkSession.onDispatch?(header, payload, signal)` | `onDispatch?(dispatch, payload, signal)` |
| `ZLinkSessionPacketHandler.handle(context, header, payload)` | `handle(context, dispatch, payload)` |
| `ZLinkSessionActor.relay(header, payload, signal)` | `relay(payload, signal)` |
| `tryGetStreamFrameHeader(header)` / `requireStreamFrameHeader(header)` public-facing dependency | runtime 내부 함수로 축소 |
| sample의 `header as ZlinkStreamHeader` cast | 제거 |

새 public 타입 초안:

```ts
export interface ZLinkSessionDispatchContext {
  readonly packetName: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly canReply: boolean;
}
```

Node는 custom header를 가장 많이 허용하던 언어이므로 첫 단계에서 `headerDecoder`를 없애고, 그 다음
session/relay signature를 바꾼다. `decodeStreamHeader`와 `encodeStreamHeader`는 runtime 내부에서만 사용한다.

### C++

#### Connector

| 현재 | 변경 후 |
|------|---------|
| public connector packet은 `packet_t{name, metadata, codec, compressed, payload}` | 유지 |
| `send_call_t::packet_name(...)`, `metadata(...)`, `codec(...)`, `compress()` | 유지 |
| `request_call_t::packet_name(...)`, `metadata(...)`, `codec(...)`, `compress()` | 유지 |
| header codec은 `runtime/protocol/header_codec.hpp` 아래 detail/runtime 경로 | 유지. public include나 guide에서 사용자 API로 소개하지 않음 |
| inbound observer는 `inbound_observation_t` snapshot 제공 | 유지 |

#### Server framework

| 현재 | 변경 후 |
|------|---------|
| public `stream_header_t` constructor와 getter가 session callback에 노출 | `stream_dispatch_context_t`로 대체 |
| `packet_stream_session_t::on_packet(stream, const stream_header_t&, const message_t&)` | `on_packet(stream, const stream_dispatch_context_t&, const zlink::message_t&)` |
| `on_raw_packet(...)` callback | 제거. C++ session callback은 별도 raw 이름 없이 `zlink::message_t`를 받음 |
| `stream_t::write_packet(const stream_header_t&, ...)` | public 제거. `stream.write_packet(payload).packet_name(...).metadata(...).compress()` 같은 call object로 대체 |
| `stream_t::reply_packet(const stream_header_t&, ...)` | `stream.reply_packet(payload)`로 변경. 현재 dispatch state에서 내부 request header를 사용 |
| `session_actor_t::relay(payload)` / `relay_raw(...)` | `relay(payload)`로 변경. 현재 dispatch state가 없으면 실패 |
| `stream_header_t` | runtime/internal 타입으로 이동. public contract header에서 제거하거나 detail namespace로 숨김 |

새 public 타입 초안:

```cpp
class stream_dispatch_context_t
{
  public:
    std::string_view packet_name () const noexcept;
    const stream_metadata_t &metadata () const noexcept;
    bool can_reply () const noexcept;
};
```

`stream_metadata_t`는 public value object로 유지한다. request sequence, flags, correlation id는
`stream_dispatch_context_t`에 넣지 않는다.

## 언어별 최종 public 인터페이스 상세

이 절은 구현자가 최종 표면을 한눈에 볼 수 있도록 interface 모양만 다시 정리한다. 실제 파일명이나
namespace는 언어별 기존 배치를 따른다. 아래 코드에 없는 header 생성자, header codec, request sequence
getter, flag getter는 public 계약에 남기지 않는다.

### .NET 최종 인터페이스

#### .NET connector

유지할 connector 표면:

```csharp
public interface IZlinkStreamConnector : IAsyncDisposable
{
    IZlinkStreamLifecycleCall Connect { get; }
    IZlinkStreamLifecycleCall Close { get; }
    IZlinkStreamLifecycleCall Dispatch { get; }

    IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload);
    IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload);

    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler);

    IZlinkStreamWaitCall WaitFor(string name);

    // 수신 frame을 바꾸거나 응답하지 못하는 관찰 전용 snapshot이다.
    IDisposable ObserveInbound(
        Func<ZlinkStreamInboundObservation, CancellationToken, ValueTask> observer);
}

public interface IZlinkStreamSendCall
{
    IZlinkStreamSendCall PacketName(string name);
    IZlinkStreamSendCall Metadata(string key, string value);
    IZlinkStreamSendCall Metadata(ZlinkStreamMetadata metadata);
    IZlinkStreamSendCall Compress();
    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZlinkStreamRequestCall
{
    IZlinkStreamRequestCall PacketName(string name);
    IZlinkStreamRequestCall Metadata(string key, string value);
    IZlinkStreamRequestCall Metadata(ZlinkStreamMetadata metadata);
    IZlinkStreamRequestCall Compress();
    IZlinkStreamRequestCall Timeout(TimeSpan timeout);
    ValueTask<ZlinkStreamEncodedPayload> Async(CancellationToken cancellationToken = default);
    void Submit(Action<ZlinkStreamResult> callback);
    void Submit(Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback);
}

public interface IZlinkStreamWaitCall
{
    IZlinkStreamWaitCall Timeout(TimeSpan timeout);
    IZlinkStreamWaitCall Where(Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool> predicate);
    ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> Async(
        CancellationToken cancellationToken = default);
}
```

제거할 connector 표면:

```csharp
// 제거: 사용자가 header를 만들거나 public API로 전달하지 않는다.
public sealed record ZlinkStreamHeader(...);
public enum ZlinkStreamHeaderFlags;
public readonly record struct ZlinkStreamRequestSeq(ulong Value);
```

`ZlinkStreamInboundObservation`에는 관찰에 필요한 `Kind`, `Name`, `Codec`, `Metadata`,
`PayloadLength`, `IsCompressed`, `ReceivedAt`, `PayloadPreview`만 둔다. `RequestSeq`가 꼭 필요하면
관찰 전용 nullable 값으로만 남기고, 사용자가 request/reply를 조립하는 값으로 설명하지 않는다.

#### .NET server framework

최종 session 표면:

```csharp
public sealed class ZLinkSessionDispatchContext
{
    public string PacketName { get; }
    public ZLinkMessageMetadata Metadata { get; }
    public bool CanReply { get; }
}

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }

    ValueTask OnConnectedAsync(CancellationToken cancellationToken);
    ValueTask OnDisconnectedAsync(CancellationToken cancellationToken);
    ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionPacketHandler<in TSessionContext>
{
    string PacketName { get; }

    ValueTask HandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionPacketDispatcher<in TSessionContext>
{
    ValueTask<bool> TryHandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);
}
```

최종 actor relay 표면:

```csharp
public interface IZLinkSessionActor
{
    string ActorId { get; }
    ActorRef Ref { get; }

    // 현재 session dispatch의 내부 header snapshot을 runtime이 사용한다.
    ValueTask RelayAsync(
        ZLinkMessage payload,
        CancellationToken cancellationToken = default);

    ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default);
}
```

제거할 server 표면:

```csharp
ValueTask OnDispatchAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...);
ValueTask HandleAsync(..., ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...);
ValueTask RelayAsync(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, ...);
ValueTask RelayRawAsync(ZLinkSessionDispatchContext dispatch, Message payload, ...);
ValueTask RelayRawAsync(Message payload, ...);
```

### Java 최종 인터페이스

#### Java connector

유지할 connector 표면:

```java
public interface ZLinkStreamConnector {
    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall close();
    ZLinkStreamLifecycleCall dispatch();

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);
    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);

    ZLinkStreamWaitCall waitFor(String name);

    // 관찰 전용 snapshot이다. callback은 dispatch, reply, drop을 제어하지 못한다.
    AutoCloseable observeInbound(ZLinkStreamInboundObserver observer);
}

public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String packetName);
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(Map<String, String> metadata);
    ZLinkStreamSendCall compress();
    CompletionStage<Void> submit();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall packetName(String packetName);
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkStreamRequestCall compress();
    ZLinkStreamRequestCall timeout(Duration timeout);
    CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit();
}
```

connector에서 유지할 내부 표면:

```java
// package-private 유지: public connector 사용자가 STREAM header를 직접 생성하지 않는다.
final class ZLinkStreamWireProtocol { ... }
record Header(...) { ... }
```

Java connector는 현재도 public header 생성 API를 제공하지 않는다. 이 작업에서는 이 내부 protocol 타입을
public으로 올리지 않는다는 점을 contract test와 guide에서 고정한다.

#### Java server framework

최종 session 표면:

```java
public record ZLinkSessionDispatchContext(
    String packetName,
    ZLinkMessageMetadata metadata,
    boolean canReply
) {}

public interface ZLinkSession {
    ZLinkSessionContext context();

    void onConnected();
    void onDisconnected();
    void onError(ZLinkStreamError error);

    void onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload);
}

public interface ZLinkSessionPacketHandler<TSessionContext extends ZLinkSessionContext> {
    String packetName();

    void handle(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload);
}

public interface ZLinkSessionPacketDispatcher<TSessionContext extends ZLinkSessionContext> {
    CompletionStage<Boolean> tryHandleAsync(
        TSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload);
}
```

최종 actor relay 표면:

```java
public interface ZLinkSessionActor {
    String actorId();
    ZLinkActorRef ref();

    // 현재 session dispatch의 내부 header snapshot을 runtime이 사용한다.
    CompletionStage<Void> relay(ZLinkMessage payload);

    CompletionStage<Void> notifyDisconnected();
}
```

제거할 server 표면:

```java
void onDispatch(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload);
void handle(..., ZLinkSessionDispatchContext dispatch, ZLinkMessage payload);
CompletionStage<Void> relay(ZLinkSessionDispatchContext dispatch, ZLinkMessage payload);
```

### Kotlin 최종 인터페이스

Kotlin connector는 Java connector 최종 표면을 그대로 쓴다. Kotlin server adapter는 suspend 표면만
따로 명시한다.

최종 Kotlin session 표면:

```kotlin
abstract class ZLinkSuspendingSession : ZLinkSession {
    final override fun onDispatch(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage
    ) {
        // runtime adapter가 suspend 함수로 넘긴다.
    }

    protected open suspend fun onDispatchSuspending(
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage
    ) {
    }
}

interface ZLinkSuspendingSessionPacketHandler<TSessionContext : ZLinkSessionContext> {
    val packetName: String

    suspend fun handle(
        context: TSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        payload: ZLinkMessage
    )
}
```

최종 Kotlin sample 모양:

```kotlin
override suspend fun onDispatchSuspending(
    dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
) {
    val actor = requireActor(dispatch.packetName())
    actor.relay(payload).await() // 원본 request sequence는 runtime이 보존한다.
}
```

제거할 Kotlin 표면:

```kotlin
override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage)
suspend fun handle(context: TSessionContext, dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage)
actor.relay(payload)
```

### Node.js 최종 인터페이스

#### Node connector

유지할 connector 표면:

```ts
export interface ZlinkStreamConnector {
  connect(signal?: AbortSignal): Promise<void>;
  close(signal?: AbortSignal): Promise<void>;
  dispatch(signal?: AbortSignal): Promise<void>;

  send(payload: unknown, messageType?: Function): ZlinkStreamSendCall;
  request(payload: unknown, messageType?: Function): ZlinkStreamRequestCall;

  on<TPayload = ZlinkStreamEncodedPayload>(
    name: string,
    handler: (message: ZlinkStreamMessage<TPayload>, signal?: AbortSignal) => Promise<void>,
    messageType?: Function
  ): Disposable;

  waitFor<TPayload = ZlinkStreamEncodedPayload>(name: string): ZlinkStreamWaitCall<TPayload>;

  // 관찰 전용 snapshot이다. callback은 dispatch, reply, drop을 제어하지 못한다.
  observeInbound(
    observer: (observation: ZlinkStreamInboundObservation, signal?: AbortSignal) => Promise<void>
  ): Disposable;
}

export interface ZlinkStreamSendCall {
  packetName(name: string): this;
  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  compress(): this;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZlinkStreamRequestCall {
  packetName(name: string): this;
  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  compress(): this;
  timeout(ms: number): this;
  submit<TReply = ZlinkStreamEncodedPayload>(signal?: AbortSignal): Promise<TReply>;
  submit<TReply>(
    callback: (result: ZlinkStreamResult<TReply>) => void,
    signal?: AbortSignal
  ): void;
}

export interface ZlinkStreamWaitCall<TPayload = ZlinkStreamEncodedPayload> {
  where(predicate: (message: ZlinkStreamMessage<TPayload>) => boolean): this;
  timeout(ms: number): this;
  submit(signal?: AbortSignal): Promise<ZlinkStreamMessage<TPayload>>;
}
```

제거할 connector export:

```ts
export interface ZlinkStreamHeader { ... }
export class ZlinkStreamHeaderCodec { ... }
export function buildHeader(...): ZlinkStreamHeader;
```

#### Node server framework

최종 session 표면:

```ts
export interface ZLinkSessionDispatchContext {
  readonly packetName: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly canReply: boolean;
}

export interface ZLinkSession {
  readonly context: ZLinkSessionContext;

  onConnected?(context: ZLinkSessionContext): Promise<void>;
  onDisconnected?(context: ZLinkSessionContext): Promise<void>;
  onError?(context: ZLinkSessionContext, error: ZLinkStreamError): Promise<void>;

  onDispatch?(
    dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkSessionPacketHandler<TSessionContext> {
  readonly packetName: string;

  handle(
    context: TSessionContext,
    dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkSessionPacketDispatcher<TSessionContext> {
  dispatch(
    context: TSessionContext,
    dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void>;
}
```

최종 actor relay 표면:

```ts
export interface ZLinkSessionActor {
  readonly actorId: string;
  readonly ref: ActorRef;

  // 현재 session dispatch의 내부 header snapshot을 runtime이 사용한다.
  relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<void>;

  notifyDisconnected(signal?: AbortSignal): Promise<void>;
}
```

제거할 server export:

```ts
export type ZlinkStreamHeader = unknown;

interface ZLinkStreamSessionRuntimeOptions {
  headerDecoder?: (header: Message) => ZlinkStreamHeader;
}

onDispatch?(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void>;
relay(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void>;
```

### C++ 최종 인터페이스

#### C++ connector

유지할 connector 표면:

```cpp
struct metadata_t
{
    std::map<std::string, std::string> values;
    metadata_t &with (std::string key, std::string value);
};

struct packet_t
{
    std::string name;
    metadata_t metadata;
    codec_t codec = codec_t::raw;
    bool compressed = false;
    zlink::message_t payload;
};

class connector_t
{
  public:
    result_t<void> connect ();
    result_t<void> close ();
    result_t<void> dispatch ();

    send_call_t send (packet_t packet);
    request_call_t request (packet_t packet);
    result_t<packet_t> wait_for (std::string packet_name);

    inbound_observer_registration_t
    observe_inbound (std::function<void (const inbound_observation_t &)> observer);
};

class send_call_t
{
  public:
    send_call_t &packet_name (std::string name);
    send_call_t &metadata (std::string key, std::string value);
    send_call_t &metadata (metadata_t metadata);
    send_call_t &codec (codec_t codec);
    send_call_t &compress ();
    result_t<void> submit ();
};

class request_call_t
{
  public:
    request_call_t &packet_name (std::string name);
    request_call_t &metadata (std::string key, std::string value);
    request_call_t &metadata (metadata_t metadata);
    request_call_t &codec (codec_t codec);
    request_call_t &compress ();
    request_call_t &timeout (std::chrono::milliseconds timeout);
    result_t<zlink::message_t> submit ();
};
```

제거할 connector public 표면:

```cpp
// public aggregate include나 guide에서 사용자 API로 노출하지 않는다.
detail::stream_header_t;
detail::header_codec_t;
```

#### C++ server framework

최종 session 표면:

```cpp
class stream_dispatch_context_t
{
  public:
    std::string_view packet_name () const noexcept;
    const stream_metadata_t &metadata () const noexcept;
    bool can_reply () const noexcept;
};

class stream_t
{
  public:
    std::string session_id () const;
    task_t<void> close ();

    stream_write_call_t write_packet (zlink::message_t payload);

    // 현재 dispatch state의 request header를 runtime이 사용한다.
    stream_write_call_t reply_packet (zlink::message_t payload);
};

class stream_write_call_t
{
  public:
    stream_write_call_t &packet_name (std::string name);
    stream_write_call_t &metadata (std::string key, std::string value);
    stream_write_call_t &metadata (stream_metadata_t metadata);
    stream_write_call_t &compress ();
    task_t<void> async ();
};

class packet_stream_session_t
{
  public:
    virtual task_t<void> on_connected (stream_t &stream) = 0;
    virtual task_t<void> on_disconnected (stream_t &stream) = 0;
    virtual task_t<void> on_error (stream_t &stream, const stream_error_t &error) = 0;

    virtual task_t<void> on_packet (
        stream_t &stream,
        const stream_dispatch_context_t &dispatch,
        const zlink::message_t &payload);

};
```

최종 actor relay 표면:

```cpp
class session_actor_t
{
  public:
    std::string_view actor_id () const noexcept;
    const actor_ref_t &ref () const noexcept;

    // 현재 session dispatch의 내부 header snapshot을 runtime이 사용한다.
    relay_call_t relay (const zlink::message_t &payload);
    relay_request_call_t relay_request (const zlink::message_t &payload);

    relay_call_t notify_disconnected ();
};
```

제거할 server 표면:

```cpp
stream_header_t;
stream_write_call_t::submit_fn_t;
stream_write_call_t (stream_header_t header, ...);
stream_t::write_packet (const stream_header_t &header, ...);
stream_t::reply_packet (const stream_header_t &request_header, ...);
packet_stream_session_t::on_packet (stream_t &, const stream_header_t &, ...);
packet_stream_session_t::on_raw_packet (...);
session_actor_t::relay (const stream_header_t &header, ...);
session_actor_t::relay_raw (const stream_header_t &header, ...);
session_actor_t::relay_raw (...);
session_actor_t::relay_request_raw (...);
```

`stream_write_call_t`가 내부 submit callback을 보관해야 한다면 public header에는 pimpl이나 internal
factory만 둔다. public constructor와 public callback typedef가 `stream_header_t`를 드러내면 사용자가
직접 header를 다시 조립할 수 있으므로 제거 대상이다.

## Bound session 유지 표면

Bound session은 session 밖에서 이미 묶인 client에게 push를 보내는 API다. 이 경로는 inbound request
header를 재사용하지 않으므로 제거 대상이 아니다. 다만 public 인자는 packet name, metadata, 언어별 단일
payload 타입 중심으로 유지하고, 같은 의미의 overload나 `_raw` 접미사 API는 추가하지 않는다.

### .NET bound session

```csharp
public interface IZLinkBoundSession
{
    IZLinkBoundSessionSendCall Send<TMessage>(TMessage message);

    ValueTask DisconnectAsync(CancellationToken cancellationToken = default);
}

public interface IZLinkBoundSessionSendCall
{
    IZLinkBoundSessionSendCall PacketName(string packetName);
    IZLinkBoundSessionSendCall Metadata(string key, string value);
    ValueTask Async(CancellationToken cancellationToken = default);
}
```

### Java bound session

```java
public interface ZLinkBoundSession {
    ZLinkBoundSessionSendCall send(Object message);
    CompletionStage<Void> disconnect();
}

public interface ZLinkBoundSessionSendCall {
    ZLinkBoundSessionSendCall packetName(String packetName);
    ZLinkBoundSessionSendCall metadata(String key, String value);
    CompletionStage<Void> submit();
}
```

### Node.js bound session

```ts
export interface ZLinkBoundSession {
  send(message: unknown): ZLinkBoundSessionSendCall;
  disconnect(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkBoundSessionSendCall {
  packetName(packetName: string): this;
  metadata(key: string, value: string): this;
  submit(signal?: AbortSignal): Promise<void>;
}
```

### C++ bound session

```cpp
class bound_session_t
{
  public:
    send_call_t send (const zlink::message_t &message);
    send_call_t disconnect ();
};
```

C++ bound session의 기존 `send_raw(const zlink::message_t &)`는 제거하고, 별도 raw 이름 없이
`send(const zlink::message_t &)`가 encoded payload를 보낸다. typed template `send<TMessage>(...)`를
계속 둘 경우에는 `zlink::message_t` overload와 같은 call object 규칙을 따라야 한다.

Bound session 회귀 테스트는 두 가지를 확인한다. 첫째, header 객체 없이 packet name과 metadata가 client로
전달되어야 한다. 둘째, `_raw` API나 같은 의미의 overload가 bound session public 계약에 새로 생기지
않아야 한다.

## 서버 구현 순서

1. 각 언어 runtime에 internal dispatch state를 만든다.
   - 내부 header 전체와 payload ownership 정보를 가진다.
   - public dispatch context는 이 state에서 packet name과 metadata만 읽는다.
2. session callback signature를 바꾼다.
   - 호환 overload는 두지 않는다.
   - contract tests가 old signature 부재를 검증한다.
3. session packet dispatcher와 handler interface를 바꾼다.
   - handler lookup은 `dispatch.packetName` 기준으로 유지한다.
4. reply API가 current dispatch state를 사용하게 한다.
   - request sequence와 correlation id echo는 internal state에서 처리한다.
   - request가 아닌 packet에서 reply하면 기존과 같은 오류를 낸다.
5. actor relay API에서 header 인자를 제거한다.
   - relay는 current dispatch state가 있는 동안만 가능하다.
   - 다중 actor 선택은 metadata의 `actor-id` 같은 application key를 읽어서 actor를 찾는다.
6. framework 내부 SPOT/actor gateway 경로는 internal header를 계속 사용한다.
   - internal packet envelope에는 request sequence, codec, metadata, correlation id를 보존한다.
7. bound session push 경로가 header 제거와 무관하게 유지되는지 확인한다.
   - packet name과 metadata는 계속 public 입력값으로 받는다.
   - `_raw` API와 같은 의미의 overload는 새로 만들지 않는다.
8. samples와 doc fixtures에서 header 변수, cast, constructor 사용을 제거한다.

## Connector 구현 순서

1. public package export에서 header type과 header codec을 제거한다.
   - .NET: `ZlinkStreamHeader` public model 제거 또는 internal 이동.
   - Java: public 사용 문서 제거, test helper 분리.
   - Node: `ZlinkStreamHeader`, `ZlinkStreamHeaderCodec` export 제거.
   - C++: runtime header codec은 detail/runtime include로 유지하고 public aggregate include에서 노출하지 않는다.
2. send/request builder가 내부 header를 조립하는 유일한 경로인지 확인한다.
   - `packetName`, `metadata`, `codec`, `compress`만 public 조립 표면으로 둔다.
3. 수신 경로는 header를 decode한 뒤 `ZlinkStreamMessage`/`packet_t`/observation snapshot으로 변환한다.
4. inbound observer는 계속 snapshot만 제공한다.
   - observation은 logging/tracing용이고 reply나 dispatch를 바꾸지 못한다.
5. interop tests가 header codec을 직접 써야 하면 public API가 아니라 test-only helper를 사용한다.

## 문서 변경 계획

문서는 모두 `framework/doc/` 아래에서 수정한다. 언어별 소스 디렉토리의 `doc/` 아래에는 새 문서를
추가하지 않는다.

### 공통 framework 문서

| 문서 | 변경 |
|------|------|
| `framework/doc/framework/common/spec/message-model.ko.md` | STREAM header는 내부 protocol이고 application 부가 정보는 metadata라는 원칙을 명시 |
| `framework/doc/framework/common/spec/framework-api.ko.md` | session callback 공통 모양을 dispatch context + payload로 갱신 |
| `framework/doc/framework/common/spec/session-actor-dispatch.ko.md` | typed actor handler에 header 전체를 노출하지 않는다는 기존 원칙을 session callback까지 확장 |
| `framework/doc/framework/common/e2e/config-2-spot-service.ko.md` | E2E 설명에서 session relay가 header를 전달한다고 쓰인 부분을 dispatch context와 metadata 기준으로 갱신 |
| `framework/doc/README.ko.md` | 이 계획 문서를 구현 계획 목록에 유지 |

### .NET framework 문서

| 문서 | 변경 |
|------|------|
| `framework/doc/framework/dotnet/spec/handler-interfaces.ko.md` | `OnDispatchAsync(header, payload)`와 handler `header` 인자를 dispatch context로 변경 |
| `framework/doc/framework/dotnet/spec/aspnet-core-stream.ko.md` | stream callback과 relay 계약을 dispatch context + `ZLinkMessage`로 변경 |
| `framework/doc/framework/dotnet/spec/session-actor-dispatch.ko.md` | `RelayAsync(payload)`와 `RelayRawAsync(...)` 예제를 제거하고 `RelayAsync(payload)`만 설명 |
| `framework/doc/framework/dotnet/guide/06-actor-session.ko.md` | actor 선택에 필요한 값은 `dispatch.Metadata`에서 읽도록 예제 수정 |
| `framework/doc/framework/dotnet/guide/07-stream.ko.md` | STREAM header 생성/전달 설명을 제거하고 metadata와 packet name 중심으로 설명 |
| `framework/doc/framework/dotnet/guide/samples/` | stream sample과 spot sample의 header 인자, `RelayRawAsync(...)`, `RelayAsync(payload)` 예제를 제거 |
| `framework/doc/framework/dotnet/guide/case-studies/` | session relay 예제를 `RelayAsync(payload)`와 metadata 기반 actor 선택으로 변경 |
| `framework/doc/framework/dotnet/guide/11-interface-catalog.ko.md` | public interface catalog에서 header 인자와 raw relay 항목 제거 |

### Java/Kotlin framework 문서

| 문서 | 변경 |
|------|------|
| `framework/doc/framework/java/spec/handler-interfaces.ko.md` | `ZLinkStreamHeader` handler signature를 `ZLinkSessionDispatchContext`로 변경 |
| `framework/doc/framework/java/spec/stream-connector.ko.md` | Java connector의 wire protocol 타입은 package 내부 구현이고 public header 생성 API가 아님을 명시 |
| `framework/doc/framework/java/spec/spring-boot-stream.ko.md` | Spring stream adapter 예제를 dispatch context + `ZLinkMessage`로 갱신 |
| `framework/doc/framework/java/spec/spring-boot-actor-session.ko.md` | `relay(payload)` 예제를 `relay(payload)`로 변경 |
| `framework/doc/framework/java/guide/` | stream, actor session, sample, case-study 예제 전체에서 `ZLinkStreamHeader` callback과 `relay(payload)` 제거 |
| `framework/doc/framework/kotlin/guide/` | suspend session/handler signature, sample, case-study relay 예제를 새 dispatch context 기준으로 갱신 |

### Node.js framework 문서

| 문서 | 변경 |
|------|------|
| `framework/doc/framework/node/spec/handler-interfaces.ko.md` | `ZlinkStreamHeader = unknown`, `headerDecoder`, header 기반 relay 계약 제거 |
| `framework/doc/framework/node/spec/nestjs-actor.ko.md` | session lifecycle context 인자를 유지한 채 `onDispatch(dispatch, payload)` 예제로 변경 |
| `framework/doc/framework/node/spec/nestjs-stream.ko.md` | stream callback과 actor relay 계약을 dispatch context + `ZLinkMessage`로 변경 |
| `framework/doc/framework/node/spec/session-actor-dispatch.ko.md` | session actor dispatch 내부 설명에서 public header 인자를 제거하고 internal dispatch state로 설명 |
| `framework/doc/framework/node/spec/stream-connector.ko.md` | connector public export에서 header codec을 제공하지 않는다는 기준 명시 |
| `framework/doc/framework/node/guide/` | `ZlinkStreamHeader = unknown`, `headerDecoder`, header cast, case-study relay 예제를 제거 |
| `framework/doc/framework/node/guide/` | packet name과 metadata를 읽는 예제에는 해당 호출 옆 주석으로 역할을 설명 |

### C++ framework와 stream connector 문서

| 문서 | 변경 |
|------|------|
| `framework/doc/framework/cpp/spec/cpp-stream.ko.md` | `stream_dispatch_context_t`, `zlink::message_t`, `reply_packet(payload)`, raw API 제거 기준으로 갱신 |
| `framework/doc/framework/cpp/spec/cpp-framework-interfaces.ko.md` | interface catalog에서 `stream_header_t` public session API와 raw relay/write 항목 제거 |
| `framework/doc/framework/cpp/spec/actor-gateway-session-relay.ko.md` | actor relay 예제를 `relay(payload)`와 internal dispatch state 기준으로 변경 |
| `framework/doc/framework/cpp/guide/09-actor-session.ko.md` | `relay(payload)` 시퀀스와 예제를 `relay(payload)`로 변경 |
| `framework/doc/framework/cpp/guide/10-stream.ko.md` | stream callback과 reply 예제를 dispatch context + `zlink::message_t`로 변경 |
| `framework/doc/framework/cpp/guide/13-interface-catalog.ko.md` | `stream_header_t`, `relay_raw`, `reply_packet_raw`, `write_packet_raw` 항목 제거 |
| `framework/doc/framework/cpp/internals/stream-samples.ko.md` | `stream_header_t`, `on_raw_packet`, `relay_raw`, `reply_packet_raw` 예제를 제거 |
| `framework/doc/framework/cpp/internals/cpp-framework-posd-refactoring-log.ko.md` | C++ public API가 별도 raw 이름 없이 `zlink::message_t` 하나로 수렴한다는 설계 결정을 기록 |
| `framework/doc/framework/cpp/internals/stream-open-items.ko.md` | 남은 stream 과제가 public header가 아니라 internal header 보존 기준임을 명시 |
| `framework/doc/framework/cpp/internals/cpp-framework-policy.ko.md` | 사용자 정의 header와 raw stream session 설명을 제거하거나 internal-only 설명으로 낮춤 |
| `framework/doc/stream-connector/cpp/guide/03-connector-options.ko.md` | header option처럼 읽히는 설명을 제거하고 packet name, metadata, codec option만 설명 |
| `framework/doc/stream-connector/cpp/guide/04-sending.ko.md` | 사용자 header 생성/codec 사용법을 제거하고 metadata와 packet name 중심 예제로 변경 |
| `framework/doc/plan/framework-message-codec-boundary-plan.ko.md` | 이전 C++ raw 분리 계획을 새 단일 payload 계획과 맞춤 |

### 문서 검증

문서 수정 뒤에는 아래 검색으로 남은 예제를 확인한다.

```bash
rg -n "headerDecoder|ZlinkStreamHeader|ZLinkStreamHeader|stream_header_t|relay\\(header|RelayRaw|relay_raw|on_raw_packet|reply_packet_raw|write_packet_raw" framework/doc --glob '!plan/framework-stream-user-header-removal-plan.ko.md'
```

남아 있는 항목은 내부 protocol 설명인지 public 사용 예제인지 구분한다. public 사용 예제라면 제거하고,
내부 protocol 설명이라면 사용자가 직접 만들거나 전달하는 값이 아니라는 문장을 함께 둔다.

## 회귀 테스트 계획

### 공통

| 검증 | 기대 결과 |
|------|-----------|
| old signature compile check | header 인자를 받는 session/handler/relay API가 컴파일 계약에 남지 않음 |
| metadata propagation | connector에서 보낸 metadata가 session dispatch context에 보존됨 |
| packet name dispatch | packet name으로 handler가 선택됨 |
| reply matching | 사용자가 request sequence를 보지 않아도 reply가 원 요청으로 돌아감 |
| correlation tracing | 내부 correlation id가 reply와 tracing에 유지됨 |
| compression | compressed inbound payload가 기존처럼 decode됨 |
| actor relay | `relay(payload)`가 내부 dispatch state를 사용해 bound actor로 전달됨 |
| dispatch 밖 relay | current dispatch state 없이 relay하면 명확한 오류 |
| multi actor bind | metadata `actor-id`로 actor를 선택하고 header 객체 없이 relay됨 |
| bound session push | header 객체 없이 packet name, metadata, 언어별 단일 payload 타입만으로 client push가 동작함 |
| raw overload absence | `_raw` 접미사 API와 같은 의미의 overload가 server/framework public 계약에 남지 않음 |
| connector interop | 기존 wire header byte layout과 cross-language interop가 유지됨 |

### .NET

- `Zlink.Framework.ContractTests`: `IZLinkSession`, `IZLinkSessionPacketHandler`, `IZLinkSessionActor` old method 부재와 new method 존재 확인
- `Zlink.Framework.UnitTests`: session dispatch context metadata/packet name, current dispatch reply, actor relay state 검증
- `Zlink.Framework.ContractTests`: `RelayRawAsync`와 중복 overload 부재, bound session send 계약 유지 확인
- `Systems.Zlink.Stream.Connector.Tests`: public header model/export 제거, send/request metadata 보존, inbound observation 유지
- sample smoke: TicTacToe, Bingo, SupportChat, DeliveryDispatch session relay 경로

### Java/Kotlin

- `zlink-framework-core` handler contract test: old `ZLinkStreamHeader` signature 제거
- `zlink-framework-core` stream/session integration: dispatch context metadata, reply, actor relay
- `zlink-framework-core` contract test: `relayRaw` 계열 부재와 bound session send 계약 유지 확인
- `zlink-framework-kotlin` tests: suspend handler signature와 Kotlin sample relay compile 검증
- `zlink-stream-connector` tests: public connector message/metadata API 유지, wire protocol interop 유지
- Java/Kotlin sample runner: header import가 남지 않는지 grep gate 추가

### Node.js

- TypeScript contract: `ZlinkStreamHeader` export 제거, `headerDecoder` 옵션 제거
- stream runtime tests: dispatch context creation, metadata map immutability, reply, actor relay
- TypeScript contract: `relayRaw` 계열 부재와 bound session send 계약 유지 확인
- stream connector tests: `ZlinkStreamHeaderCodec` public import 제거, send/request/observe behavior 유지
- sample tests: `header as ZlinkStreamHeader` cast와 custom header guard 제거

### C++

- `test_cpp_framework_contract_headers`: public framework headers에서 `stream_header_t`가 session API로 노출되지 않는지 검증
- framework runtime tests: `stream_dispatch_context_t`, `stream.reply_packet(...)`, `session_actor_t::relay(...)`
- contract header tests: `stream_write_call_t::submit_fn_t`, `_raw` 접미사 API, framework `message_t` overload 부재 확인
- connector tests: `packet_t`, metadata, inbound observation behavior 유지
- layout contract: runtime protocol headers가 public aggregate include로 새지 않는지 검증
- sample build/smoke: session code가 `stream_header_t`를 include하지 않는지 확인

## 완료 기준

1. 사용자가 직접 STREAM header 객체를 생성해서 send/request/session relay에 넣는 public API가 없다.
2. Node framework에서 `ZlinkStreamHeader = unknown`과 `headerDecoder`가 사라진다.
3. 모든 server session callback은 dispatch context + payload 모양을 사용한다.
4. 모든 actor relay public API는 header 인자를 받지 않는다.
5. server/framework public API에 `_raw` 접미사 API와 같은 의미의 overload가 남지 않는다.
6. bound session public API는 header 없이 packet name, metadata, 언어별 단일 payload 타입으로 push를 보낼 수 있다.
7. metadata, packet name, reply matching, compression, correlation tracing, actor relay interop 회귀 테스트가 통과한다.
8. guide/spec/sample에 `relay(payload)`, `onDispatch(dispatch, payload)`, custom header decoder 예제가 남지 않는다.
9. 내부 header codec과 cross-language wire layout은 기존과 같은 byte-level 계약을 유지한다.
