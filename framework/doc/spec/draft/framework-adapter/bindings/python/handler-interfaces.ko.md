<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Python](README.ko.md) | [다음: ZLink Framework FastAPI Channel Messaging](fastapi-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Python 묶음](./README.ko.md) | [channel](./fastapi-channel-messaging.ko.md) | [SPOT](./fastapi-spot.ko.md) | [STREAM](./fastapi-stream.ko.md) | [Monitoring](./fastapi-monitoring.ko.md) | [Registry](./fastapi-registry.ko.md)

# Draft -- ZLink Framework Python Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Python`에서 `ZLink Framework`가 노출할 protocol,
> context, decorator를 한 곳에 모은 기준 문서다.

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../policy/README.ko.md)과
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
규칙을 그대로 따른다. 따라서 `Python` 문서에서는 아래를 기본으로 본다.

- 모든 public API를 `snake_case`로 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `send_to`, `request_to`, `send_channel`, `request_channel` 같은 action 이름을
  유지한다.
- send/publish는 기본 async submit으로 설명한다. backpressure는 별도 public
  no-wait 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

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


@dataclass(slots=True)
class ManualRoutedPeerEntry:
    target_rid: str
    endpoint: str


@dataclass(slots=True)
class ClientCapabilityOptions:
    manual_connections: Sequence[str] | None = None


@dataclass(slots=True)
class SubscriberCapabilityOptions:
    manual_connections: Sequence[str] | None = None


@dataclass(slots=True)
class ChannelOptions:
    server: dict[str, object] | None = None
    client: ClientCapabilityOptions | None = None
    publisher: dict[str, object] | None = None
    subscriber: SubscriberCapabilityOptions | None = None


@dataclass(slots=True)
class SpotRouterCapabilityOptions:
    manual_connections: Sequence[ManualRoutedPeerEntry] | None = None


@dataclass(slots=True)
class SpotPubSubCapabilityOptions:
    manual_connections: Sequence[str] | None = None


@dataclass(slots=True)
class SpotChannelClientCapabilityOptions:
    manual_connections: Sequence[str] | None = None


@dataclass(slots=True)
class SpotPublisherClientCapabilityOptions:
    manual_connections: Sequence[str] | None = None


@dataclass(slots=True)
class SpotFactoryEntry:
    spot_name: str
    spot_type: type


@dataclass(slots=True)
class SpotNodeOptions:
    bind: str | None = None
    router: SpotRouterCapabilityOptions | None = None
    pub_sub: SpotPubSubCapabilityOptions | None = None
    channel_clients: Mapping[str, SpotChannelClientCapabilityOptions] | None = None
    spot_publishers: Mapping[str, SpotPublisherClientCapabilityOptions] | None = None
    spot_factories: Sequence[SpotFactoryEntry] | None = None


class ZLinkFrameworkRegistration(Protocol):
    def add_channel(self, channel_name: str, options: ChannelOptions) -> None: ...
    def use_discovery(self, registries: Sequence[str]) -> None: ...
    def use_spot_discovery(
        self,
        channel_name: str,
        registries: Sequence[str],
    ) -> None: ...
    def add_spot_node(self, spot_node_name: str, options: SpotNodeOptions) -> None: ...


class ChannelClientConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class ChannelSubscriberConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class ZLinkChannelConnectionManager(Protocol):
    def get_client(self, channel_name: str) -> ChannelClientConnections: ...
    def get_subscriber(self, channel_name: str) -> ChannelSubscriberConnections: ...


@dataclass(slots=True)
class ZLinkStream:
    session_id: str
    routing_id: str | None = None
    local_addr: str | None = None
    remote_addr: str | None = None

    async def write(
        self,
        payload: Message,
        flags: SendFlags = SendFlags.NONE,
    ) -> None: ...

    async def write_packet(
        self,
        header: Message,
        payload: Message,
        flags: SendFlags = SendFlags.NONE,
    ) -> None: ...


class ZLinkStreamSessionError(StrEnum):
    INTERNAL = 'internal'
    TRANSPORT_ERROR = 'transport_error'
    HANDSHAKE_FAILED = 'handshake_failed'


@dataclass(slots=True)
class ZLinkStreamError:
    error: ZLinkStreamSessionError
    internal_errno: int

    def get_error_code(self) -> ErrorCode: ...
    def get_error_message(self) -> str: ...


class ZLinkPacketStreamSession(Protocol):
    async def on_connected(self, stream: ZLinkStream) -> None: ...
    async def on_disconnected(self, stream: ZLinkStream) -> None: ...
    async def on_error(
        self,
        stream: ZLinkStream,
        error: ZLinkStreamError,
    ) -> None: ...
    async def on_packet(
        self,
        stream: ZLinkStream,
        header: Message,
        payload: Message,
    ) -> None: ...


class ZLinkRawStreamSession(Protocol):
    async def on_connected(self, stream: ZLinkStream) -> None: ...
    async def on_disconnected(self, stream: ZLinkStream) -> None: ...
    async def on_error(
        self,
        stream: ZLinkStream,
        error: ZLinkStreamError,
    ) -> None: ...
    async def on_raw(
        self,
        stream: ZLinkStream,
        payload: Message,
    ) -> None: ...


class ZLinkMonitoringOptions(Protocol):
    def add_socket_events(
        self,
        source_name: str,
        events: SocketEvent = SocketEvent.ALL,
    ) -> None: ...

    def add_discovery_events(
        self,
        source_name: str,
        events: Sequence[ServiceMonitorEventMask],
    ) -> None: ...

    def add_registry_events(
        self,
        source_name: str,
        interval: float,
    ) -> None: ...

    def add_spot_events(
        self,
        source_name: str,
        interval: float,
    ) -> None: ...


@dataclass(slots=True)
class ZLinkRuntimeEvent:
    source_name: str
    timestamp: datetime


class ZLinkRuntimeEventHandler(Protocol[T_contra]):
    async def handle(self, event: T_contra) -> None: ...


class ZLinkSocketEventKind(StrEnum):
    CONNECTED = 'connected'
    CONNECTION_READY = 'connection_ready'
    DISCONNECTED = 'disconnected'
    HANDSHAKE_FAILED = 'handshake_failed'
    PEER_ADMISSION_CHANGED = 'peer_admission_changed'
    CLOSED = 'closed'
    INTERNAL = 'internal'


@dataclass(slots=True)
class ZLinkSocketEvent(ZLinkRuntimeEvent):
    event: ZLinkSocketEventKind
    value: int
    routing_id: str | None = None
    local_addr: str | None = None
    remote_addr: str | None = None


class ZLinkDiscoveryEventKind(StrEnum):
    SERVICE_UP = 'service_up'
    SERVICE_DOWN = 'service_down'
    PROVIDERS_CHANGED = 'providers_changed'
    PEER_ADMISSION_CHANGED = 'peer_admission_changed'
    ERROR = 'error'
    CLOSED = 'closed'
    INTERNAL = 'internal'


@dataclass(slots=True)
class ZLinkDiscoveryEvent(ZLinkRuntimeEvent):
    event: ZLinkDiscoveryEventKind
    status: int
    error_code: int
    service_name: str
    endpoint: str
    routing_id: str | None = None


class ZLinkRegistryEventKind(StrEnum):
    STATUS_CHANGED = 'status_changed'
    TOPOLOGY_CHANGED = 'topology_changed'
    SERVICE_SUMMARY_CHANGED = 'service_summary_changed'


@dataclass(slots=True)
class ZLinkRegistryEvent(ZLinkRuntimeEvent):
    event: ZLinkRegistryEventKind


class ZLinkSpotEventKind(StrEnum):
    STATUS_CHANGED = 'status_changed'
    PEERS_CHANGED = 'peers_changed'
    SUBJECTS_CHANGED = 'subjects_changed'


@dataclass(slots=True)
class ZLinkSpotEvent(ZLinkRuntimeEvent):
    event: ZLinkSpotEventKind
```

현재 초안에서는 capability 값을 `True`와 object로 섞지 않고, 항상 object로 두는
편을 기준으로 본다. 즉 `client=ClientCapabilityOptions()`는 client capability만
켠다는 뜻이고, `client=ClientCapabilityOptions(manual_connections=[...])`는 같은
capability의 manual 연결까지 같이 준다는 뜻이다.

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


class ZLinkSpotClient(Protocol):
    async def send_channel(
        self,
        channel_name: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...

    async def request_channel(
        self,
        channel_name: str,
        request: object,
        options: ZLinkRequestOptions | None = None,
    ) -> object: ...

    async def send_to(
        self,
        target_rid: str,
        spot_rid: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...

    async def request_to(
        self,
        target_rid: str,
        spot_rid: str,
        request: object,
        options: ZLinkRequestOptions | None = None,
    ) -> object: ...

    async def publish(
        self,
        topic: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...


class ZLinkSpotPublisherClient(Protocol):
    async def publish(
        self,
        channel_name: str,
        topic: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...


class ZLinkEventPublisher(Protocol):
    async def publish(
        self,
        channel_name: str,
        topic: str,
        message: object,
        options: ZLinkSendOptions | None = None,
    ) -> None: ...


@dataclass(slots=True)
class ZLinkSpotCreateResult:
    spot_rid: str
    spot_name: str
    created: bool


@dataclass(slots=True)
class ZLinkSpotInfo:
    spot_rid: str
    spot_name: str


class ZLinkSpotManager(Protocol):
    async def create(
        self,
        spot_name: str,
        spot_rid: str | None = None,
    ) -> ZLinkSpotCreateResult: ...
    async def get(self, spot_rid: str) -> ZLinkSpotInfo | None: ...
    async def list(self) -> Sequence[ZLinkSpotInfo]: ...
    async def remove(self, spot_rid: str) -> bool: ...


class ZLinkSpot(Protocol):
    spot_rid: str

    async def add_timer(
        self,
        name: str,
        period: float,
        handler_type: type,
    ) -> ZLinkTimer: ...


class SpotRouterConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class SpotPubSubConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class SpotChannelClientConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class SpotPublisherClientConnections(Protocol):
    def connect(self, endpoint: str) -> None: ...
    def disconnect(self, endpoint: str) -> None: ...
    def list_connections(self) -> Sequence[str]: ...


class ZLinkSpotConnectionManager(Protocol):
    def get_router(self, spot_node_name: str) -> SpotRouterConnections: ...
    def get_pub_sub(self, spot_node_name: str) -> SpotPubSubConnections: ...
    def get_channel_client(
        self,
        spot_node_name: str,
        channel_name: str,
    ) -> SpotChannelClientConnections: ...
    def get_spot_publisher_client(
        self,
        spot_node_name: str,
        channel_name: str,
    ) -> SpotPublisherClientConnections: ...


class ZLinkTimer(Protocol):
    @property
    def is_disposed(self) -> bool: ...

    async def cancel(self) -> None: ...
```

일반 channel client manual 연결은 endpoint 집합만 다루고, `SPOT` router manual
연결도 같은 방식으로 endpoint 집합만 등록한다. 이 초안에서는 `connect(...)`
호출 시 remote router id를 따로 받지 않는다. `ZLinkSpotManager`는 등록된
`spot_name`으로 factory를 고르고, `get(...)`와 `list(...)`는 runtime이 들고 있는
`spot_rid -> spot_name` 매핑을 다시 보는 용도다.

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

- 같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.
- 수동 연결은 `channel + capability` 단위로 관리한다.
- manual capability는 startup 등록뿐 아니라 런타임 `connect`, `disconnect`,
  `list_connections`도 지원해야 한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
