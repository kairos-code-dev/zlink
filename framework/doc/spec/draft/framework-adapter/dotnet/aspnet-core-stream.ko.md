[스펙 목차](../../../README.ko.md)

# Draft -- ZLink Framework ASP.NET Core STREAM Integration

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `ASP.NET Core`에서 `STREAM`을 어떤 handler 모델로
> 올릴지 방향을 정리한다.
>
> 아직 닫지 않은 serializer, write, lifecycle 항목은
> [stream-open-items.ko.md](./stream-open-items.ko.md)를 참고한다.

## 1. 목표

`STREAM`은 일반 request-response와 성격이 다르다.
연결 수명, peer 식별, packet framing, raw payload 처리 같은 요소가 더 중요할 수
있다. 이 문서의 목표는 `.NET` framework 표면에서 `STREAM`을 아래 두 방식으로
정리하는 것이다.

- packet handler
- raw handler

현재 초안에서는 application이 직접 `recv` loop를 돌리는 방식은 지원 대상으로 보지
않는다. framework가 수신 dispatch를 맡고, 사용자는 handler를 구현하는 쪽을
기본으로 본다.

## 2. 기본 방향

`STREAM`은 일반 service messaging handler와 같은 감각으로 억지로 맞추지 않는다.
특히 아래 원칙을 둔다.

- raw payload chunk를 직접 다루고 재조립까지 application이 맡고 싶으면 raw handler를 쓴다.
- C API가 잘라 준 `header/body` packet 단위를 처리하고 싶으면 packet handler를 쓴다.
- typed packet handler는 raw `header` 청크만 사용자 정의 타입으로 변환해 쓰는 상위 표면이다.
- `playhouse`처럼 header는 고정 메타데이터이고, body는 `header.MsgId`를 보고 각 packet 타입으로 decode하는 방식을 자연스러운 기본 모델로 본다.
- 이 decode helper는 `playhouse/extensions`처럼 transport 본체에 넣기보다,
  `Message` 위에 얹는 serializer extension 계층으로 두는 쪽을 기본으로 본다.
- recv loop는 application 표면에 직접 올리지 않는다.
- connection/session handler는 현재 초안 범위에서 우선순위를 낮춘다.

즉 현재 방향은 "packet handler + raw handler" 두 축으로 먼저 닫고, 나중에 필요하면
더 높은 수준의 session lifecycle 표면을 검토하는 쪽이다.

## 3. 인터페이스 초안

전체 인터페이스 기준은 [handler-interfaces.ko.md](./handler-interfaces.ko.md)를
참고한다. `STREAM` 쪽 핵심 초안은 아래 두 가지다.

```csharp
public interface IZLinkStreamPacketHandler
{
    ValueTask HandleAsync(
        Message header,
        Message body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamPacketHandler<in THeader>
{
    ValueTask HandleAsync(
        THeader header,
        Message body,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkStreamRawHandler
{
    ValueTask HandleAsync(
        Message payload,
        ZLinkStreamContext context,
        CancellationToken cancellationToken);
}
```

여기서 기대하는 점은 아래와 같다.

- raw handler는 socket에서 들어오는 payload chunk를 그대로 받는다.
- raw handler는 필요하면 application이 직접 packet 재조립을 맡는다.
- packet handler는 C API가 이미 header/body로 나눈 framed packet을 받는다.
- typed packet handler는 raw `Message header`를 사용자 정의 `THeader`로 변환해 쓴다.
- body는 보통 고정 타입 하나로 바로 올리지 않고, header 안의 `msgId` 같은 값을 보고
  각 packet 타입으로 decode한다.
- 이 decode 과정은 가능하면 `Message.AsReadOnlySpan()`이나 그 위에 얹는 helper를
  사용해서 추가 복사를 피하는 쪽을 기본으로 본다.
- 두 방식 모두 `ZLinkStreamContext`에서 peer, connection metadata를 읽는다.
- 둘 다 framework dispatch 경로 위에 올라가고, application은 직접 recv loop를
  만들지 않는다.

## 4. 등록 모델 초안

현재 `.NET` 쪽에서는 `STREAM`도 명시적 등록이 더 자연스럽다.
예를 들면 아래와 같은 등록 표면을 생각할 수 있다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.AddStreamNode("client.stream", stream =>
    {
        stream.Bind("tcp://0.0.0.0:9100");
        stream.AddPacket<ClientPacketHandler>();
        stream.AddRaw<ClientRawHandler>();
    });
});
```

이 등록 모델에서 중요한 점은 아래와 같다.

- packet handler와 raw handler를 분리해서 붙인다.
- recv callback이나 recv loop를 application이 직접 노출받지 않는다.
- 어떤 handler가 packet path인지 raw path인지 등록 시점에 분명히 보인다.

## 5. serializer 계층

`playhouse/extensions`를 보면 protobuf/json/messagepack 지원을 transport 본체에
직접 섞지 않고, `Parse<T>()`, `OfProto(...)` 같은 확장 메서드 계층으로 얹는다.
`STREAM`도 같은 감각이 자연스럽다.

즉 framework 기본 표면은 아래 정도로 유지한다.

- `IZLinkStreamPacketHandler`
- `IZLinkStreamRawHandler`
- `Message`

그리고 객체 변환은 별도 확장 패키지나 serializer provider가 맡는다.

예를 들면:

```csharp
ClientInput input = body.ParseProto<ClientInput>();
ChatRequest request = body.ParseJson<ChatRequest>();
```

이 구조의 장점은 아래와 같다.

- protobuf/json/messagepack을 transport 인터페이스에 고정하지 않는다.
- serializer를 추가 패키지로 분리하기 쉽다.
- `Message.AsReadOnlySpan()` 기반 helper를 써서 불필요한 복사를 줄이기 쉽다.
- `playhouse/extensions`와 비슷한 사용 경험을 만들 수 있다.

## 6. recv 방식은 왜 기본에서 빼는가

recv 방식은 low-level binding에서는 유효할 수 있다.
하지만 framework 표면까지 그대로 올리면 아래 문제가 생긴다.

- framework가 dispatch, DI, filter, logging을 일관되게 묶기 어렵다.
- application이 직접 loop와 cancellation, backpressure를 떠안게 된다.
- packet handler와 raw handler를 함께 설명하기가 어려워진다.

따라서 현재 초안은 recv 기반 사용을 막자는 뜻이 아니라, **framework의 기본
application 표면으로는 올리지 않는다**는 뜻이다.

## 7. 아직 확정하지 않는 것

- `AddStreamNode(...)` 같은 등록 이름을 그대로 갈지
- packet handler 등록을 attribute까지 지원할지
- raw handler와 packet handler를 한 stream node에 같이 붙일지, 하나만 허용할지
- typed packet handler에서 header 변환을 codec으로 할지 mapper로 할지
- body decode helper를 framework가 어디까지 제공할지
- `Message` 위에 protobuf/json zero-copy helper를 둘지
- protobuf/json/messagepack serializer를 확장 패키지로 나눌지
- connection open/close hook을 별도 인터페이스로 둘지
- codec 없는 raw stream과 framed packet stream을 같은 표면으로 설명할지
