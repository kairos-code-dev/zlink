# Framework unhandled dispatch 정책 초안

이 문서는 **구현 전 초안**이며 **현재 공개 계약이 아니다**.
정식 공개 계약은 구현 완료 뒤 `core/include/zlink.h`,
`core/include/zlink/*.h`, `doc/spec/core/`, 각 binding spec, framework spec 에
나누어 반영한다.

이 초안은 `.NET framework` 런타임에서 packet 또는 message 를 받았지만 등록된
application handler 가 없을 때 어떤 기본 동작을 해야 하는지 정리한다. 대상은
Session, Channel, RouteMeshChannel, Spot, EntrySpot, Actor dispatch 경로다.

## 1. 배경

현재 framework 의 여러 dispatch 경로는 handler 가 없을 때 서로 다른 방식으로
동작한다. 어떤 경로는 예외를 던지고, 어떤 경로는 reply error 를 만들고, 어떤
경로는 조용히 return 한다.

이 차이는 사용자에게 다음 문제를 만든다.

- request 는 실패를 알 수 있지만 send 나 publish 는 실패 여부가 보이지 않는다.
- handler 등록 누락과 의도한 no-op 을 구분하기 어렵다.
- sample session 코드가 framework 대신 fallback relay 정책을 직접 구현하게 된다.
- Spot, Actor, Channel, Session 마다 "handler 없음"의 의미가 달라진다.
- 운영 중에는 어느 session, channel, packet 이 버려졌는지 추적하기 어렵다.

따라서 framework 는 handler miss 를 각 dispatcher 의 우연한 구현 결과로 두지
않고, 하나의 정책으로 정의해야 한다.

## 2. 목표

1. 등록된 handler 가 없을 때 기본 동작을 message kind 별로 통일한다.
2. request 는 호출자에게 실패를 돌려줄 수 있으므로 error reply 를 기본으로 한다.
3. send 와 publish 는 reply 경로가 없으므로 관측 가능한 drop 을 기본으로 한다.
4. Session packet 은 actor 가 하나만 bind 되어 있으면 기본 relay 할 수 있다.
5. handler miss 는 표준 `ILogger<T>` 로 기록하고 zlink 전용 logger 등록 API 는
   추가하지 않는다.
6. message 흐름 추적은 logging, OpenTelemetry trace, metrics 로 나누어 제공한다.
7. 사용자가 엄격한 서버나 테스트 환경에서 정책을 바꿀 수 있도록 최소 옵션을 둔다.
8. sample 이 직접 fallback relay 와 handler miss 정책을 반복하지 않게 한다.

## 3. 비목표

- core C API 의 errno 계약을 이 초안에서 바로 변경하지 않는다.
- zlink 전용 logging provider 나 logger callback 등록 API 를 추가하지 않는다.
- payload 본문을 로그에 기록하지 않는다.
- OpenTelemetry exporter 를 framework 가 자동 등록하지 않는다.
- publish sender 에게 subscriber 별 handler miss 를 실패로 돌려주지 않는다.
- actor 가 여러 개 bind 된 session 에서 자동으로 target actor 를 추론하지 않는다.
- timer, monitoring event, lifecycle callback 의 no-op handler 의미를 packet
  handler miss 와 섞지 않는다.

## 4. 용어

| 용어 | 의미 |
|------|------|
| handler miss | runtime 이 받은 packet 또는 message 에 대응하는 application handler 를 찾지 못한 상태다. |
| unhandled dispatch | handler miss 뒤 framework 정책에 따라 reply, relay, drop, close, throw 중 하나를 수행하는 단계다. |
| request | caller 가 reply 를 기다리는 message 다. 실패를 error reply 로 전달할 수 있다. |
| send | reply 를 기다리지 않는 fire-and-forget message 다. 실패를 caller 에게 직접 돌려줄 수 없다. |
| publish | pub/sub fanout message 다. subscriber 하나의 handler miss 를 publisher 실패로 볼 수 없다. |
| bound actor | session 에 `BindActorHandleAsync(...)` 로 연결된 actor handle 이다. |
| observable drop | message 를 버리되, logger 또는 runtime event 로 원인을 남기는 동작이다. |
| message flow trace | 한 message 가 receive, decode, handler resolve, dispatch, relay, send, reply 같은 단계를 어떻게 통과했는지 남기는 진단 기록이다. |
| diagnostic field | 일반 운영 로그에는 너무 상세하지만 문제 분석 때 필요한 size, part count, ownership, ref count 같은 필드다. |

## 5. 기본 정책

기본 정책은 아래와 같다.

| 조건 | 기본 동작 |
|------|-----------|
| handler 있음 | handler dispatch |
| Session packet handler 없음 + bound actor 1개 | actor 로 relay |
| Session packet handler 없음 + bound actor 0개 + request | error reply |
| Session packet handler 없음 + bound actor 0개 + send | `Warning` log 후 drop |
| Session packet handler 없음 + bound actor 여러 개 + request | ambiguous error reply |
| Session packet handler 없음 + bound actor 여러 개 + send | `Warning` log 후 drop |
| Channel request handler 없음 | error reply |
| Channel send handler 없음 | `Warning` log 후 drop |
| Channel publish handler 없음 | `Debug` log 후 drop |
| RouteMesh request handler 없음 | error reply |
| RouteMesh send handler 없음 | `Warning` log 후 drop |
| Spot route request handler 없음 | error reply |
| Spot route send handler 없음 | `Warning` log 후 drop |
| Spot subscription handler 없음 | `Debug` log 후 drop |
| Actor request handler 없음 | error reply |
| Actor send handler 없음 | `Warning` log 후 drop |
| EntrySpot actor join handler 없음 | rejected reply, 필요하면 `Debug` log |

request 의 error reply 는 기존 framework error envelope 또는 stream error reply 를
사용한다. send 와 publish 는 caller 가 기다리는 reply 경로가 없으므로 error reply 를
만들지 않는다.

## 6. 적용 대상

### 6.1 Stream Session packet

대상 public 표면은 다음과 같다.

- `IZLinkSession.OnDispatchAsync(...)`
- `IZLinkSessionPacketHandler<TSessionContext>`
- `IZLinkSessionPacketDispatcher<TSessionContext>`
- `IZLinkSessionContext.BoundActors`
- `IZLinkSessionContext.RelayToActorAsync(...)`

Session packet 의 기본 흐름은 아래와 같다.

1. framework 가 session 별 실행 줄에서 packet 을 받는다.
2. 등록된 `IZLinkSessionPacketHandler<TSessionContext>` 가 있으면 handler 에 보낸다.
3. handler 가 없고 bound actor 가 하나이면 그 actor 로 relay 한다.
4. handler 가 없고 bound actor 가 없으면 message kind 에 맞는 unhandled 정책을 적용한다.
5. handler 가 없고 bound actor 가 여러 개이면 framework 가 자동 target 을 고르지 않는다.

actor 가 여러 개 bind 된 경우에는 application 이 packet name, metadata, session state
등 자기 프로토콜에 맞는 selector 를 직접 구현해야 한다. framework 가 첫 번째 actor 를
고르면 오류를 숨길 가능성이 크다.

### 6.2 Channel message

대상 public 표면은 다음과 같다.

- `AddChannel(...)`
- `IZLinkRequestHandler<TRequest, TReply>`
- `IZLinkSendHandler<TMessage>`
- `IZLinkPublishHandler<TMessage>`

socket 관점의 대상은 다음과 같다.

- Router 로 수신한 request
- Router 로 수신한 send
- Sub 로 수신한 publish

Dealer 는 주로 client/outbound 역할이다. 다만 reply tracking 또는 internal routed
reply 처리에서 unexpected packet 을 받는 경우에는 logging 대상이 될 수 있다.

Channel request 는 handler 가 없으면 error reply 를 만든다. Channel send 는
`Warning` 으로 기록하고 drop 한다. Channel publish 는 subscriber 별 optional handler
성격이 강하므로 기본 `Debug` 로 기록하고 drop 한다.

### 6.3 RouteMeshChannel message

대상 public 표면은 다음과 같다.

- `AddRouteMeshChannel(...)`
- `IZLinkRouteSendHandler<TMessage>`
- `IZLinkRouteRequestHandler<TRequest, TReply>`

RouteMeshChannel 은 routed Router/Dealer 기반으로 request 와 send 를 처리한다.
publish handler 는 없다.

request handler 가 없으면 source routing id 와 request sequence 로 error reply 를
돌려준다. send handler 가 없으면 source routing id, channel id, packet name 을 로그에
남기고 drop 한다.

### 6.4 Spot route packet

대상 public 표면은 다음과 같다.

- `IZLinkSpotPacketHandler<TSpot, TMessage>`
- `IZLinkSpotRequestHandler<TSpot, TRequest, TReply>`
- spot client 의 send/request 계열 API

Spot route dispatcher 는 현재 handler 를 찾지 못하면 조용히 return 할 수 있다. 이
동작은 수정 대상이다.

request handler 가 없으면 request caller 에게 error reply 를 돌려준다. send handler 가
없으면 `Warning` log 후 drop 한다. Spot rid 와 packet name 은 로그에 남기되 payload 는
남기지 않는다.

### 6.5 Spot subscription packet

대상 public 표면은 다음과 같다.

- `IZLinkSpotSubscriptionHandler<TSpot, TEvent>`
- spot pub/sub subscribe 설정

subscription 은 보통 handler 등록에서 subscribe 대상이 만들어지므로 handler miss 가
정상적으로는 잘 생기지 않는다. 그래도 registry 와 runtime 상태가 어긋났거나 내부
mapping 이 깨진 경우를 대비해 `Debug` log 후 drop 을 둔다.

publish 는 fanout 이므로 publisher 에게 error reply 를 보낼 수 없다. handler miss 를
운영 신호로 보고 싶으면 사용자가 policy 에서 `Warning` 으로 올릴 수 있어야 한다.

### 6.6 Actor packet

대상 public 표면은 다음과 같다.

- `IZLinkActorSendHandler<TMessage>`
- `IZLinkActorRequestHandler<TRequest, TReply>`
- `IZLinkActorPacketHandler<TActor, TMessage>`
- `IZLinkActorRequestHandler<TActor, TRequest, TReply>`

Actor packet 은 Spot actor handler 를 먼저 찾고, 없으면 actor 자체 handler 로 fallback
할 수 있다. 두 단계 모두 handler 를 찾지 못하면 unhandled actor packet 이다.

request 는 error reply 를 만든다. send 는 `Warning` log 후 drop 한다. actor id,
actor type, packet name, source session 여부는 로그에 남길 수 있다.

### 6.7 EntrySpot actor packet

대상 public 표면은 다음과 같다.

- `IZLinkEntrySpotActorSendHandler<TEntrySpot, TActor, TMessage>`
- `IZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor, TRequest, TReply>`
- `IZLinkEntrySpotActorJoinedHandler<TEntrySpot, TActor>`
- `IZLinkEntrySpotActorLeftHandler<TEntrySpot, TActor>`

EntrySpot actor send/request 는 Actor packet 과 같은 정책을 사용한다. joined/left
handler 는 optional notification 이므로 handler miss 를 오류로 보지 않는다. 필요하면
`Debug` 로만 기록한다.

### 6.8 EntrySpot actor join

대상 public 표면은 다음과 같다.

- `IZLinkSpotActorJoinHandler<TSpot, TActor, TRequest, TReply>`

actor join 은 request 성격이지만 일반 packet handler miss 와 다르다. join handler 가
없거나 target actor 를 찾을 수 없으면 accepted=false 로 rejected reply 를 돌려준다.
이 동작은 유지한다.

보강할 점은 로그다. rejected 이유가 handler miss 인지, target actor 없음인지,
application handler reject 인지 구분할 수 있어야 한다.

## 7. 제외 대상

아래 대상은 이 초안의 unhandled packet 정책에서 제외한다.

| 대상 | 제외 이유 |
|------|-----------|
| `IZLinkSpotTimerHandler<TSpot>` | 외부 packet 이 아니라 등록된 timer 만 실행한다. handler 없음은 요청 실패가 아니다. |
| Spot actor joined/left notification | optional notification 이며 handler 없음은 정상 no-op 이다. |
| EntrySpot actor joined/left notification | optional notification 이며 handler 없음은 정상 no-op 이다. |
| `IZLinkRuntimeEventHandler<TEvent>` | monitoring event 이며 application packet dispatch 와 의미가 다르다. |
| lifecycle callback | connected, disconnected, closing 같은 runtime lifecycle 은 packet handler miss 가 아니다. |

## 8. Logging 정책

framework 는 `Microsoft.Extensions.Logging` 의 `ILogger<T>` 를 사용한다. zlink 전용
logger 등록 API 는 추가하지 않는다.

사용자는 일반적인 host 설정으로 로그를 제어한다.

```csharp
builder.Logging.AddConsole();
builder.Logging.SetMinimumLevel(LogLevel.Warning);
```

logging provider 가 없거나 사용자가 logging 을 등록하지 않은 경우 framework 는
`NullLogger<T>` 로 동작해야 한다. logging 때문에 framework DI 구성이 실패하면 안 된다.

framework 는 출력 포맷을 직접 고정하지 않는다. framework 책임은 category, event id,
event name, structured field 를 안정적으로 제공하는 것이다. console, JSON, file,
OpenTelemetry exporter 같은 실제 출력 형식은 사용자가 등록한 logging provider 가
결정한다.

예를 들어 framework 내부에서는 다음과 같은 구조화 로그를 남긴다.

```csharp
logger.LogWarning(
    "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason}",
    "handler-missing",
    "Session",
    "Send",
    "StartBingoReq",
    "Drop",
    "NoBoundActor");
```

JSON provider 를 쓰면 위 값들이 검색 가능한 field 로 남아야 한다. console provider 를
쓰면 사람이 읽는 한 줄 또는 여러 줄 로그로 출력된다. framework 가 자체 문자열 formatter
를 만들면 provider 생태계와 충돌하고 OpenTelemetry 연동도 어려워지므로 피한다.

### 8.1 로그 필드

unhandled dispatch 로그에는 가능한 한 아래 정보를 포함한다.

| 필드 | 설명 |
|------|------|
| `kind` | request, send, publish 중 하나 |
| `surface` | Session, Channel, RouteMeshChannel, Spot, Actor, EntrySpot 중 하나 |
| `packetName` | application packet name |
| `channelName` | Channel 또는 RouteMeshChannel 이름. 없으면 기록하지 않는다 |
| `sessionId` | Session packet 이면 session id |
| `actorId` | Actor 관련 packet 이면 actor id |
| `actorType` | Actor type 을 알 수 있으면 기록 |
| `spotRid` | Spot packet 이면 target spot rid |
| `boundActorCount` | Session fallback 에서 actor 개수 |
| `action` | reply-error, drop, close-session, throw, rejected 중 하나 |
| `reason` | no-handler, no-bound-actor, ambiguous-bound-actor, no-target-actor 등 |
| `messageId` | framework 가 부여하거나 upstream metadata 에서 가져온 message 식별자 |
| `correlationId` | request/reply 또는 외부 trace 와 묶기 위한 correlation id |
| `sizeBytes` | 기록 가능한 경우 전체 message byte 크기 |
| `partCount` | multipart message 의 part 개수 |
| `payloadSizeBytes` | payload body 크기. payload 본문은 기록하지 않는다 |

payload bytes, payload JSON, access token, user secret 은 로그에 남기지 않는다.

### 8.2 기본 로그 레벨

| 상황 | 기본 레벨 |
|------|-----------|
| request handler 없음 + error reply | `Warning` |
| send handler 없음 + drop | `Warning` |
| publish handler 없음 + drop | `Debug` |
| optional notification handler 없음 | 로그 없음 또는 `Debug` |
| actor join handler 없음 + rejected | `Debug` |
| close-session 정책 실행 | `Warning` |
| throw 정책 실행 | `Warning` 후 throw |

publish 는 정상적인 subscriber 차이일 수 있으므로 기본 `Warning` 으로 두지 않는다.
운영 환경에서 publish miss 를 반드시 보고 싶으면 사용자가 옵션으로 레벨을 올릴 수
있어야 한다.

## 9. Message flow diagnostics

handler miss 로그는 message flow diagnostics 의 일부다. message 유실, timeout,
복사, relay 위치 문제를 찾으려면 "문제가 생긴 순간"만으로는 부족하고 message 가 어떤
단계를 통과했는지 볼 수 있어야 한다.

framework 는 diagnostics 를 세 층으로 나눈다.

| 층 | 목적 | 기본 상태 |
|----|------|-----------|
| 운영 로그 | handler 없음, drop, timeout, route 없음, reply error 를 사람이 확인한다 | `Warning` 이상 중심 |
| OpenTelemetry trace | message 가 hop 별로 어디를 지나갔는지 추적한다 | instrumentation 은 존재하되 exporter 는 사용자가 등록 |
| metrics | receive, dispatch, drop, timeout, size 분포를 숫자로 본다 | 낮은 비용의 counter/histogram 중심 |

### 9.1 flow event 단계

flow event 이름은 최대한 작고 안정적으로 유지한다.

| event | 의미 |
|-------|------|
| `received` | transport 나 runtime 이 message 를 받았다 |
| `decoded` | header 와 routing metadata 를 decode 했다 |
| `handler-resolved` | application handler 를 찾았다 |
| `handler-missing` | application handler 를 찾지 못했다 |
| `enqueued` | session, actor, spot 실행 queue 에 work item 을 넣었다 |
| `dispatched` | handler 실행을 시작했다 |
| `relay-selected` | session 또는 actor relay target 을 골랐다 |
| `sent` | outbound send 를 완료했다 |
| `reply-sent` | request reply 또는 error reply 를 보냈다 |
| `dropped` | 정책에 따라 message 를 버렸다 |
| `timed-out` | request 나 relay 가 timeout 으로 끝났다 |
| `failed` | 처리 중 예외나 runtime 오류가 발생했다 |

기본 운영 로그에서는 모든 event 를 출력하지 않는다. 상세 흐름은 trace 와 diagnostic
옵션을 켰을 때만 남긴다.

### 9.2 출력 형식

framework 는 message flow 를 직접 파일 포맷으로 쓰지 않는다. 대신 같은 event shape 를
`ILogger<T>`, `ActivitySource`, `Meter` 에 맞게 나누어 기록한다.

권장 JSON field shape 는 아래와 같다. 이 모양은 logger provider 가 JSON 을 출력할 때
검색 기준으로 삼을 수 있도록 문서화하는 예시다.

```json
{
  "event": "handler-missing",
  "surface": "Session",
  "kind": "Send",
  "packetName": "StartBingoReq",
  "sessionId": "s-123",
  "boundActorCount": 0,
  "action": "Drop",
  "reason": "NoBoundActor",
  "messageId": "01J...",
  "correlationId": "01J...",
  "sizeBytes": 128,
  "partCount": 2
}
```

timestamp, log level, category, span id, trace id 의 실제 출력 이름은 logging provider
또는 OpenTelemetry exporter 가 정한다.

### 9.3 OpenTelemetry trace

진짜 tracing 은 OpenTelemetry `ActivitySource` 로 연결한다. framework 는 exporter 를
등록하지 않고 source 만 제공한다.

```csharp
internal static class ZLinkTelemetry
{
    public const string ActivitySourceName = "Zlink.Framework";
    public const string MeterName = "Zlink.Framework";

    public static readonly ActivitySource ActivitySource = new(ActivitySourceName);
    public static readonly Meter Meter = new(MeterName);
}
```

사용자는 host 에서 exporter 를 선택한다.

```csharp
builder.Services.AddOpenTelemetry()
    .WithTracing(tracing =>
    {
        tracing
            .AddSource("Zlink.Framework")
            .AddOtlpExporter();
    })
    .WithMetrics(metrics =>
    {
        metrics
            .AddMeter("Zlink.Framework")
            .AddOtlpExporter();
    });
```

권장 span 이름은 아래와 같다.

| span 이름 | kind | 의미 |
|-----------|------|------|
| `zlink.session.dispatch` | Consumer | session 이 client packet 을 받아 처리 |
| `zlink.channel.dispatch` | Consumer | channel 이 request/send/publish 를 받아 처리 |
| `zlink.route.dispatch` | Consumer | route mesh channel 이 routed packet 을 받아 처리 |
| `zlink.spot.dispatch` | Consumer | Spot route/subscription packet 처리 |
| `zlink.actor.dispatch` | Consumer | Actor packet handler 처리 |
| `zlink.actor.relay` | Producer | session 에서 actor 로 relay |
| `zlink.message.send` | Producer | outbound message send |
| `zlink.message.request` | Client | outbound request |

권장 tag 는 아래와 같다.

| tag | 설명 |
|-----|------|
| `zlink.surface` | Session, Channel, RouteMeshChannel, Spot, Actor, EntrySpot |
| `zlink.kind` | request, send, publish, response, error |
| `zlink.packet.name` | packet name |
| `zlink.message.id` | message 식별자 |
| `zlink.correlation.id` | correlation id |
| `zlink.channel.name` | channel 이름 |
| `zlink.session.id` | session id |
| `zlink.actor.id` | actor id |
| `zlink.spot.rid` | spot routing id |
| `zlink.source.rid` | source routing id |
| `zlink.target.rid` | target routing id |
| `zlink.request.seq` | request sequence |
| `zlink.action` | dispatch, relay, drop, reply-error 등 |
| `zlink.reason` | no-handler, timeout, route-missing 등 |
| `zlink.message.size_bytes` | 전체 message 크기 |
| `zlink.message.part_count` | multipart part 개수 |
| `zlink.message.payload_size_bytes` | payload 크기 |

trace tag 는 cardinality 를 조심해야 한다. `messageId`, `sessionId`, `actorId` 처럼 값
종류가 많은 field 는 사용자가 trace 를 켠 상태에서만 의미 있게 수집해야 한다.

### 9.4 Metrics

metrics 는 "어느 구간에서 수가 줄었는지"를 먼저 찾기 위한 것이다. 개별 message 를
따라가는 trace 와 목적이 다르다.

권장 metric 은 아래와 같다.

| metric | 종류 | 설명 |
|--------|------|------|
| `zlink.messages.received` | counter | 수신한 message 수 |
| `zlink.messages.dispatched` | counter | handler 또는 relay 로 전달한 message 수 |
| `zlink.messages.dropped` | counter | 정책에 따라 drop 한 message 수 |
| `zlink.messages.handler_missing` | counter | handler miss 수 |
| `zlink.messages.reply_error` | counter | error reply 수 |
| `zlink.messages.timed_out` | counter | timeout 수 |
| `zlink.message.size_bytes` | histogram | message size 분포 |
| `zlink.message.payload_size_bytes` | histogram | payload size 분포 |
| `zlink.message.part_count` | histogram | multipart part count 분포 |
| `zlink.dispatch.duration_ms` | histogram | dispatch 처리 시간 |
| `zlink.relay.duration_ms` | histogram | relay 처리 시간 |

metric tag 는 낮은 cardinality 중심이어야 한다.

권장 tag:

- `surface`
- `kind`
- `action`
- `reason`

기본 metric tag 에 `messageId`, `sessionId`, `actorId` 는 넣지 않는다. cardinality 가 너무
높아 metric backend 비용과 성능 문제를 만들 수 있기 때문이다.

### 9.5 Message size, part count, ref count

message size 와 part count 는 기본 진단 field 로 취급한다. payload 본문 없이도 큰 message
병목, multipart 손상, 특정 hop 의 크기 증가를 확인할 수 있기 때문이다.

기본 기록 대상:

- 전체 message size
- part count
- header size
- payload size

ref count 와 ownership 은 더 조심해야 한다. 이 값은 binding 과 native runtime 구현
세부에 가까우며, 모든 언어에서 같은 의미를 보장하기 어렵다. 따라서 diagnostic/verbose
모드에서만 optional field 로 기록한다.

| 필드 | 기본 기록 | diagnostic 기록 | 설명 |
|------|-----------|-----------------|------|
| `zlink.message.size_bytes` | 예 | 예 | 전체 message 크기 |
| `zlink.message.part_count` | 예 | 예 | multipart part 수 |
| `zlink.message.header_size_bytes` | 예 | 예 | header 크기 |
| `zlink.message.payload_size_bytes` | 예 | 예 | payload 크기 |
| `zlink.message.ref_count` | 아니오 | 가능 | native message ref count. 지원되는 runtime 에서만 기록 |
| `zlink.message.ownership` | 아니오 | 가능 | borrowed, owned, cloned, moved 같은 ownership 상태 |
| `zlink.message.native_handle_id` | 아니오 | 가능 | raw pointer 가 아닌 process-local diagnostic id |

raw pointer, unmanaged address, payload bytes 는 기록하지 않는다. native handle 을
식별해야 하면 raw address 대신 process 안에서만 의미가 있는 diagnostic id 를 만들어
사용한다.

### 9.6 Diagnostics 옵션

초안의 옵션 형태는 아래 정도면 충분하다.

```csharp
public enum ZLinkMessageFlowLogMode
{
    Off,
    ErrorsOnly,
    KeyTransitions,
    Verbose,
    Diagnostic
}

public interface IZLinkDiagnosticsOptions
{
    ZLinkMessageFlowLogMode MessageFlow { get; set; }

    double SampleRate { get; set; }

    bool IncludeMessageSizes { get; set; }

    bool IncludeNativeDiagnostics { get; set; }
}
```

권장 기본값:

| 옵션 | 기본값 |
|------|--------|
| `MessageFlow` | `ErrorsOnly` 또는 `Off` 중 구현 비용을 보고 결정 |
| `SampleRate` | `1.0` for errors, 낮은 값 for verbose flow |
| `IncludeMessageSizes` | `true` |
| `IncludeNativeDiagnostics` | `false` |

`IncludeNativeDiagnostics` 가 `false` 이면 ref count, ownership, native diagnostic id 를
기록하지 않는다.

## 10. 정책 옵션

옵션은 복잡한 rule engine 이 아니라 message kind 별 기본 action 을 바꾸는 정도로
제한한다.

```csharp
options.ConfigureDispatch(dispatch =>
{
    dispatch.Unhandled.Request = ZLinkUnhandledDispatchAction.ReplyError;
    dispatch.Unhandled.Send = ZLinkUnhandledDispatchAction.LogAndDrop;
    dispatch.Unhandled.Publish = ZLinkUnhandledDispatchAction.LogAndDrop;
    dispatch.Unhandled.PublishLogLevel = LogLevel.Debug;
});
```

초안의 public 형태는 아래 정도면 충분하다.

```csharp
public enum ZLinkUnhandledDispatchAction
{
    ReplyError,
    LogAndDrop,
    Drop,
    CloseSession,
    Throw
}

public interface IZLinkUnhandledDispatchOptions
{
    ZLinkUnhandledDispatchAction Request { get; set; }

    ZLinkUnhandledDispatchAction Send { get; set; }

    ZLinkUnhandledDispatchAction Publish { get; set; }

    LogLevel SendLogLevel { get; set; }

    LogLevel PublishLogLevel { get; set; }
}
```

제약은 다음과 같다.

- `ReplyError` 는 request 에만 유효하다.
- send 또는 publish 에 `ReplyError` 를 설정하면 configuration validation 에서 실패한다.
- `CloseSession` 은 Session surface 에서만 직접적인 의미가 있다. 다른 surface 에서는
  지원하지 않거나 `Throw` 로 명확히 바꿔야 한다.
- `Drop` 은 로그 없이 버리는 동작이므로 기본값으로 두지 않는다.
- surface 별 세부 정책이 필요해지기 전까지는 public 옵션을 늘리지 않는다.

## 11. Error reply 의미

request handler miss 는 application handler exception 과 구분되어야 한다.

권장 error kind 는 다음과 같다.

| 상황 | error kind |
|------|------------|
| handler 없음 | `HandlerNotFound` 또는 기존 error kind 에 대응되는 `InvalidOperationException` |
| session actor 없음 | `ActorRouteNotFound` 또는 session unhandled 전용 error |
| session actor 여러 개 | `AmbiguousActorBinding` 또는 `InvalidOperationException` |
| actor request handler 없음 | `ActorDispatchHandlerNotFound` |
| route request handler 없음 | `RouteHandlerNotFound` |

현재 framework error kind 에 위 이름이 없으면 처음 구현에서는 message 가 명확한
`InvalidOperationException` 으로 error reply 를 만들 수 있다. 다만 장기적으로는
`ZLinkFrameworkErrorKind` 를 확장해 caller 가 원인을 분기할 수 있게 하는 편이 낫다.

## 12. 구현 방향

### 12.1 공통 helper

각 dispatcher 가 같은 문자열과 조건문을 반복하지 않도록 내부 helper 를 둔다.

```csharp
internal sealed class ZLinkUnhandledDispatchPolicy
{
    public ValueTask HandleRequestAsync(...);

    public ValueTask HandleSendAsync(...);

    public ValueTask HandlePublishAsync(...);
}
```

helper 는 다음 책임만 가진다.

- action 결정
- 로그 출력
- drop, throw, close-session 실행
- request error reply 에 필요한 exception 생성

실제 reply writer 는 surface 마다 다르므로 helper 가 직접 모든 transport 를 알면 안 된다.
request 는 dispatcher 가 가진 reply writer callback 을 넘기는 형태가 낫다.

### 12.2 Session fallback 을 framework 로 이동

sample session 이 아래 패턴을 직접 쓰지 않게 한다.

```csharp
if (!await handlers.TryHandleAsync(...))
{
    var actor = Context.BoundActors.Single();
    await Context.RelayToActorAsync(actor, header, payload, cancellationToken);
}
```

framework 는 session packet dispatcher 또는 session runtime 에 다음 동작을 제공한다.

1. session packet handler 를 먼저 시도한다.
2. 없으면 `BoundActors` 를 확인한다.
3. actor 가 하나이면 relay 한다.
4. actor 가 없거나 여러 개이면 unhandled policy 를 실행한다.

application 이 직접 target actor 를 고르고 싶으면 지금처럼 `OnDispatchAsync(...)` 를
override 하거나 명시 helper 를 호출할 수 있어야 한다.

### 12.3 Channel dispatcher

`ZLinkChannelPacketDispatcher` 는 handler registry 조회 실패를 잡아 request 와 send,
publish 정책을 나눠야 한다.

- request: error reply
- send: log/drop
- publish: log/drop

현재 registry 가 handler 없음에서 어떤 예외를 던지는지 확인하고, dispatcher 에서
명확히 `TryGet...` 형태로 바꾸는 것이 좋다. handler miss 를 예외 흐름으로만 처리하면
정책 적용 지점이 흐려진다.

### 12.4 RouteMeshChannel dispatcher

`ZLinkRoutePacketDispatcher` 는 route handler registry 조회 실패를 잡아야 한다.

- internal packet 은 먼저 처리한다.
- internal packet 도 아니고 application handler 도 없으면 unhandled policy 를 적용한다.
- request 는 source routing id 가 있어야 error reply 를 보낼 수 있다. source routing id
  가 없으면 로그 후 drop 또는 throw 정책으로 처리한다.

### 12.5 Spot route dispatcher

`ZLinkSpotRouteDispatcher` 는 `packets.TryResolve(...)` 실패 시 조용히 return 하지 않는다.

- request 여부를 header 에서 알 수 있으면 request 는 error reply 를 만든다.
- send 는 log/drop 한다.
- response 나 internal kind 는 별도 처리하거나 log/drop 한다.

### 12.6 Actor dispatcher

`ZLinkSpotActorPacketDispatcher` 와 `ZLinkActorPacketDispatcher` 는 handler fallback 을
모두 시도한 뒤에도 없을 때 정책을 적용한다.

request 에서 handler 가 없으면 지금처럼 exception 을 던지는 것만으로 끝내지 말고, 그
exception 이 caller 에게 error reply 로 전달되는지 확인해야 한다. send 는 조용히 return
하지 않고 log/drop 한다.

### 12.7 EntrySpot actor join

`ZLinkSpotActorJoinDispatcher` 는 rejected reply 를 유지하되 이유별 로그를 추가한다.

- join handler 없음
- target actor 없음
- handler exception
- application reject

handler exception 은 rejected reply 로 숨길지, error reply 에 대응되는 실패로 볼지
별도 확인이 필요하다. 첫 구현에서는 기존 동작을 깨지 않도록 rejected reply 와 `Warning`
로그가 낫다.

### 12.8 Telemetry 구현

telemetry 구현은 hot path 비용을 줄여야 한다.

- log level 과 diagnostics mode 를 먼저 확인한 뒤 field 객체를 만든다.
- payload body 는 decode 하지 않고 size 만 계산한다.
- OpenTelemetry `ActivitySource` 는 listener 가 없으면 span 생성 비용이 낮아야 한다.
- metrics tag 는 low-cardinality field 만 사용한다.
- ref count 와 native diagnostic id 는 diagnostic mode 에서만 조회한다.

## 13. 테스트 계획

### 13.1 Unit tests

| 테스트 | 기대 결과 |
|--------|-----------|
| session request handler 없음 + actor 없음 | error reply |
| session send handler 없음 + actor 없음 | warning log, drop |
| session publish 성격 packet handler 없음 | debug log, drop |
| session handler 없음 + actor 1개 | actor relay |
| session handler 없음 + actor 여러 개 | ambiguous 처리 |
| channel request handler 없음 | error reply |
| channel send handler 없음 | warning log, drop |
| channel publish handler 없음 | debug log, drop |
| route request handler 없음 | error reply |
| route send handler 없음 | warning log, drop |
| spot route request handler 없음 | error reply |
| spot route send handler 없음 | warning log, drop |
| actor request handler 없음 | error reply |
| actor send handler 없음 | warning log, drop |
| logging provider 없음 | dispatch 가 실패하지 않음 |
| send/publish 에 ReplyError 설정 | configuration validation 실패 |
| message size 기록 | payload 본문 없이 size/part count 만 기록 |
| native diagnostics off | ref count/native handle id 가 로그와 trace 에 남지 않음 |
| native diagnostics on | 지원 runtime 에서 ref count/ownership field 기록 |

### 13.2 E2E tests

| 경로 | 검증 |
|------|------|
| Stream session → ActorGateway | handler 없는 authenticated session packet 이 bound actor 로 relay 된다 |
| Stream session request unbound | client 가 error reply 를 받는다 |
| Channel request | handler 없는 request 에 error reply 가 온다 |
| Channel publish | handler 없는 publish 가 connection 을 끊지 않는다 |
| Spot request | handler 없는 spot request 가 timeout 이 아니라 error reply 로 끝난다 |
| Actor request | handler 없는 actor request 가 timeout 이 아니라 error reply 로 끝난다 |
| OpenTelemetry tracing | `ActivitySource` listener 가 session dispatch span 을 받는다 |
| metrics | received, dispatched, dropped counter 가 증가한다 |

### 13.3 Sample regression

sample 에서 아래 패턴이 다시 생기지 않도록 회귀 테스트를 둔다.

- sample 별 session fallback relay helper
- sample 별 actor handle 상태 저장소
- handler miss 에서 조용한 `return`
- `payload.Move()` 를 session handler 호출마다 반복하는 코드

## 14. 문서 반영 계획

구현 뒤 아래 문서에 나누어 반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/languages/dotnet/doc/spec/handler-interfaces.ko.md` | handler miss 기본 정책과 logging 방식 |
| `framework/languages/dotnet/doc/spec/session-actor-dispatch.ko.md` | session fallback relay, actor 없음/여러 개 정책 |
| `framework/languages/dotnet/doc/spec/aspnet-core-stream.ko.md` | session packet dispatch 정책 |
| `framework/languages/dotnet/doc/spec/aspnet-core-spot.ko.md` | Spot, EntrySpot handler miss 정책 |
| `framework/languages/dotnet/doc/guide/06-actor-session.ko.md` | sample 에서 직접 fallback relay 를 작성하지 않는 사용법 |
| `framework/languages/dotnet/doc/guide/07-stream.ko.md` | request/send/publish 별 unhandled 동작 |
| `framework/languages/dotnet/doc/internals/di-capability-exposure-policy.ko.md` | `ILogger<T>` 사용과 별도 logger API 비추가 이유 |
| 새 diagnostics guide 또는 monitoring 문서 | OpenTelemetry source/meter 이름, 권장 tag, message size/ref count 정책 |

정식 spec 문서에는 구현 완료 전까지 이 초안의 세부 계약을 섞어 쓰지 않는다. 필요한 경우
짧은 링크만 둔다.

## 15. POSD 검토

이 정책은 다음 설계 문제를 줄이기 위한 것이다.

- **얕은 session sample 제거**: sample 이 handler miss 와 actor relay 를 직접 구현하지
  않게 한다.
- **정보 은닉**: handler registry miss, actor count, request reply writer 차이를
  application handler 에 노출하지 않는다.
- **복잡성을 아래로 이동**: 사용자는 request/send/publish 의 기본 실패 의미를 외우지
  않아도 된다.
- **오류를 정의로 제거**: handler 없음이 경로마다 timeout, silent drop, exception 으로
  갈라지지 않고 정의된 결과로 끝난다.
- **특수 코드와 범용 코드 분리**: sample 은 자기 protocol handler 만 담고, framework 는
  공통 fallback 정책을 소유한다.
- **정보 은닉**: ref count, native handle 같은 내부 진단 정보는 diagnostic mode 로
  제한해 일반 application 표면으로 새지 않게 한다.

## 16. 미결정 사항

1. `ZLinkFrameworkErrorKind` 에 handler miss 전용 kind 를 추가할지, 처음에는
   `InvalidOperationException` 기반 error reply 로 갈지 결정해야 한다.
2. publish handler miss 의 기본 로그 레벨을 `Debug` 로 둘지 `Information` 으로 둘지
   운영 관측 요구를 보고 정해야 한다.
3. `CloseSession` action 을 Session surface 전용으로 제한할지, Channel/Spot 에서는
   configuration validation 으로 막을지 결정해야 한다.
4. Spot route request 에서 handler 를 찾기 전에 request/send kind 를 항상 판별할 수
   있는지 현재 wire header 기준으로 확인해야 한다.
5. handler miss 를 runtime monitoring event 로도 발행할지, logger 만으로 시작할지
   결정해야 한다.
6. message flow 기본값을 `Off` 로 둘지 `ErrorsOnly` 로 둘지 결정해야 한다.
7. OpenTelemetry semantic convention 을 zlink 전용 tag 로만 갈지, messaging semantic
   convention 과 일부 맞출지 결정해야 한다.
8. core/native ref count 를 .NET framework 에서 public diagnostic field 로 읽을 수 있는
   API 가 필요한지 확인해야 한다.
