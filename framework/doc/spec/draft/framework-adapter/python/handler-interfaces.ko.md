[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [channel](./fastapi-channel-messaging.ko.md) | [SPOT](./fastapi-spot.ko.md) | [STREAM](./fastapi-stream.ko.md) | [Registry](./fastapi-registry.ko.md)

# Draft -- ZLink Framework Python Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python`에서 `ZLink Framework`가 노출할 protocol,
> context, decorator를 한 곳에 모은 기준 문서다.

## 1. 기본 타입

```python
@dataclass(slots=True)
class ZLinkSendOptions:
    packet_name: str | None = None


@dataclass(slots=True)
class ZLinkRequestOptions:
    packet_name: str | None = None
    timeout: float | None = None


@dataclass(slots=True)
class ZLinkHandlerContext:
    channel_name: str | None
    packet_name: str | None
    content_type: str | None
    correlation_id: str | None
    deadline: datetime | None
```

## 2. Client

```python
class ZLinkClient(Protocol):
    async def send(
        self,
        channel_name: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...

    async def request(
        self,
        channel_name: str,
        request: object,
        options: ZLinkRequestOptions | None = None,
    ) -> object: ...
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packet_name`
2. payload 타입 decorator
3. payload 클래스 이름

## 3. Decorator

```python
def zlink_packet(packet_name: str) -> Callable[[type], type]: ...
def zlink_request(packet_name: str | None = None) -> Callable[..., Any]: ...
def zlink_send(packet_name: str | None = None) -> Callable[..., Any]: ...
def zlink_event(packet_name: str | None = None) -> Callable[..., Any]: ...
```

## 4. Handler

```python
class ZLinkRequestHandler(Protocol[TRequest, TReply]):
    async def handle(
        self,
        request: TRequest,
        context: ZLinkRequestContext,
    ) -> TReply: ...


class ZLinkSendHandler(Protocol[TMessage]):
    async def handle(
        self,
        message: TMessage,
        context: ZLinkSendContext,
    ) -> None: ...
```

## 5. 중요한 규칙

- 같은 outbound channel은 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
