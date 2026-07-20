# RouteMesh Python·Go·Rust 바인딩 공개 계약 초안

> 이 문서는 구현 전 초안이며 현재 공개 계약이 아니다. 구현과 contract test가 확정되기 전에는
> 정식 bindings spec이나 API reference의 근거로 사용하지 않는다.

## 1. 기준과 설계 결정

공개 동작의 기준은 `core/doc/spec/core/service/`와 `core/include/zlink/service/*.h`다. 다른 언어
바인딩은 이름과 언어 관례를 비교하는 자료일 뿐 계약의 출처가 아니다. 세 언어는 같은 Core 기능을
제공하되 Python은 엄격하게 타입화한 `Protocol`, Go는 concrete type과 return-based error, Rust는 safe
RAII type과 `Result`로 표현한다.

검토한 두 가지 형태 가운데 기존 operation builder를 유지하는 안보다 Core의 submit 결과와
`operation_id`를 직접 드러내는 얕은 메서드 안을 선택한다. builder는 multipart를 조립하기에는 편하지만
한 Core 작업을 여러 공개 단계로 나누고 오류·수명 지식을 호출자에게 전달한다. 선택한 안은 multipart를
`Sequence`, slice 또는 slice reference로 한 번에 받고, native 배열 변환과 operation 추적은 runtime이
소유한다. request 완료는 pull dispatch의 completion record로 받는다.

다음 이름은 제거하며 alias나 compatibility wrapper를 남기지 않는다.

- `SpotNode`, `create_spot_node`, `spot_node`
- `SpotRouteBridge`, `create_route_bridge`, `spot_route_bridge`
- dispatch worker 수·option과 worker callback 실행 표면
- service 메시징의 공개 `*_part`, `add_part`, `send_part` 형태

## 2. 공통 의미와 값 타입

| Core 개념 | Python | Go | Rust |
|---|---|---|---|
| node resource | `MeshNode` protocol | `MeshNode` struct | `MeshNode` struct |
| publisher | `MeshNodePublisher` protocol | `MeshNodePublisher` struct | `MeshNodePublisher` struct |
| ready storage | `ReadyBatch` protocol | `ReadyBatch` struct | `ReadyBatch` struct |
| claim | `Claim` protocol | `Claim` struct | `Claim` struct |
| receive storage | `ReceiveBatch` protocol | `ReceiveBatch` struct | `ReceiveBatch` struct |
| Spot resource | `Spot` protocol | `Spot` struct | `Spot` struct |
| Actor identity | `ActorRef` frozen value | `ActorRef` value | `ActorRef` value |
| session service | `StreamSessionService` protocol | `StreamSessionService` struct | `StreamSessionService` struct |
| metadata | immutable `bytes` | copied `[]byte` input | borrowed `&[u8]`, copied by submit |
| multipart | `Sequence[Message]` | `[]Message` | `&[Message]` |
| timeout | `int` milliseconds | `time.Duration` | `Duration` |
| operation id | `OperationId` value | `OperationID` value | `OperationId` value |

모든 snapshot 문자열과 metadata는 native buffer를 참조하지 않는 복사본이다. channel과 topic은 Core의
최대 길이와 UTF-8 규칙을 적용하고 metadata는 최대 1024 bytes다. 잘못된 길이, null handle, malformed
metadata는 native 진입 전에 같은 오류 domain으로 거부한다. 메시지 part는 submit 동안 읽기 전용으로
빌리며 성공과 실패 모두 호출자 소유를 유지한다.

## 3. Python exact interface

Python 최소 버전은 3.12다. 아래 이름은 `zlink.contracts.service`가 소유하고 `zlink`가 re-export한다.
모든 공개 인자와 반환값은 아래와 같이 명시하며 암시적 `Any`를 허용하지 않는다.

```python
ReadyHandler = Callable[[ReadyDomain], ReadyDomain]

@runtime_checkable
class MeshNode(Protocol):
    def set_bind(self, endpoint: str) -> None: ...
    def set_routing_id(self, routing_id: RoutingId) -> None: ...
    def add_channel_name(self, channel_name: str) -> None: ...
    def set_channel_weight(self, channel_name: str, weight: int) -> None: ...
    def start(self) -> None: ...
    def shutdown(self, timeout_ms: int = 0) -> RequestResult: ...
    def close(self) -> CloseResult: ...
    def connect_peer(self, endpoint: str,
                     expected_rid: RoutingId | None = None) -> int: ...
    def remove_peer_connection(self, connection_intent_id: int) -> None: ...
    def disconnect_peer(self, peer_rid: RoutingId,
                        lifecycle_generation: int) -> None: ...
    def send_to_node(self, target_rid: RoutingId, parts: Sequence[Message], *,
                     metadata: bytes = b"", flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_node(self, target_rid: RoutingId, parts: Sequence[Message], *,
                        metadata: bytes = b"", flags: SendFlags = SendFlags.NONE,
                        timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def send_to_channel(self, channel_name: str, parts: Sequence[Message], *,
                        metadata: bytes = b"", flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_channel(self, channel_name: str, parts: Sequence[Message], *,
                           metadata: bytes = b"", flags: SendFlags = SendFlags.NONE,
                           timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def create_publisher(self) -> MeshNodePublisher: ...
    def set_ready_handler(self, handler: ReadyHandler | None) -> None: ...
    def drain_ready(self, domains: ReadyDomain, batch: ReadyBatch, *,
                    flags: RecvFlags = RecvFlags.NONE) -> DrainResult: ...
    def status(self) -> MeshNodeStatus: ...
    def peers(self) -> tuple[MeshPeerEntry, ...]: ...
    def peer_channels(self, peer_rid: RoutingId,
                      lifecycle_generation: int) -> tuple[PeerChannel, ...]: ...
    def set_router_hwm_profile(self, profile: AutoHwmProfile) -> None: ...
    def router_hwm_profile(self) -> AutoHwmProfile: ...
    def set_router_hwm(self, value: int) -> None: ...
    def router_hwm(self) -> int: ...
    def set_mailbox_message_budget(self, value: int) -> None: ...
    def mailbox_message_budget(self) -> int: ...
    def set_mailbox_byte_budget(self, value: int) -> None: ...
    def mailbox_byte_budget(self) -> int: ...
    def create_spot(self) -> Spot: ...
    def entry_spot(self) -> Spot: ...
    def spot_lookup(self, spot_rid: RoutingId) -> Spot | None: ...
    def get_or_create_spot(self, spot_rid: RoutingId) -> tuple[Spot, bool]: ...
    def create_actor(self, actor_id: str, creation_parts: Sequence[Message] = (), *,
                     flags: SendFlags = SendFlags.NONE,
                     timeout_ms: int = 0) -> tuple[RequestResult, ActorRef | None]: ...
    def actor_lookup(self, actor_id: str) -> ActorLocation | None: ...
    def actor_lookup_remote(self, target_node_rid: RoutingId, actor_id: str, *,
                            timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def actor_destroy(self, actor: ActorRef, *, timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def send_to_actor(self, actor: ActorRef, parts: Sequence[Message], *,
                      metadata: bytes = b"", flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_actor(self, actor: ActorRef, parts: Sequence[Message], *,
                         metadata: bytes = b"", flags: SendFlags = SendFlags.NONE,
                         timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def __enter__(self) -> Self: ...
    def __exit__(self, exc_type: type[BaseException] | None,
                 exc: BaseException | None, traceback: TracebackType | None) -> None: ...

def create_mesh_node(context: Context, *, mesh_name: str = "",
                     trust_profile: str = "") -> MeshNode: ...
def create_ready_batch(record_capacity: int) -> ReadyBatch: ...
def create_receive_batch(message_capacity: int, part_capacity: int,
                         byte_capacity: int) -> ReceiveBatch: ...
def create_stream_session_service(node: MeshNode,
                                  stream: StreamSocket) -> StreamSessionService: ...
```

나머지 Python 공개 리소스의 exact interface는 다음과 같다.

```python
@runtime_checkable
class ReadyBatch(Protocol):
    def reset(self) -> None: ...
    def records(self) -> tuple[ReadyRecord, ...]: ...
    def take_claim(self, index: int) -> Claim: ...
    def close(self) -> CloseResult: ...

@runtime_checkable
class Claim(Protocol):
    def recv_batch(self, batch: ReceiveBatch, *,
                   flags: RecvFlags = RecvFlags.NONE) -> ClaimRecvResult: ...
    def close(self) -> CloseResult: ...

@runtime_checkable
class ReceiveBatch(Protocol):
    def reset(self) -> None: ...
    def records(self) -> tuple[ReceiveRecord, ...]: ...
    def parts(self) -> tuple[MessageView, ...]: ...
    def retain_message(self, record_index: int) -> tuple[Message, ...]: ...
    def close(self) -> CloseResult: ...

@runtime_checkable
class MeshNodePublisher(Protocol):
    def publish(self, channel_name: str, topic: str, parts: Sequence[Message], *,
                metadata: bytes = b"", flags: SendFlags = SendFlags.NONE
                ) -> tuple[SubmitResult, PublishDetail]: ...
    def close(self) -> CloseResult: ...

@runtime_checkable
class Spot(Protocol):
    def status(self) -> SpotStatus: ...
    def send_to_channel(self, channel_name: str, parts: Sequence[Message], *,
                        metadata: bytes = b"", flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_channel(self, channel_name: str, parts: Sequence[Message], *,
                           metadata: bytes = b"", flags: SendFlags = SendFlags.NONE,
                           timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def send_to_spot(self, target_node_rid: RoutingId, target_spot_rid: RoutingId,
                     target_spot_generation: int, parts: Sequence[Message], *,
                     metadata: bytes = b"", flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_spot(self, target_node_rid: RoutingId, target_spot_rid: RoutingId,
                        target_spot_generation: int, parts: Sequence[Message], *,
                        metadata: bytes = b"", flags: SendFlags = SendFlags.NONE,
                        timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def publish(self, channel_name: str, topic: str, parts: Sequence[Message], *,
                metadata: bytes = b"", flags: SendFlags = SendFlags.NONE
                ) -> tuple[SubmitResult, PublishDetail]: ...
    def set_subscription(self, channel_name: str, topic_filter: str,
                         kind: SpotSubscriptionKind) -> None: ...
    def unset_subscription(self, channel_name: str, topic_filter: str,
                           kind: SpotSubscriptionKind) -> None: ...
    def close(self) -> CloseResult: ...

@runtime_checkable
class StreamSessionService(Protocol):
    def start(self) -> None: ...
    def shutdown(self, timeout_ms: int = 0) -> RequestResult: ...
    def status(self) -> StreamSessionStatus: ...
    def bind_actor(self, session_rid: RoutingId, actor: ActorRef, *,
                   timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def unbind_actor(self, session_rid: RoutingId, actor: ActorRef,
                     expected_binding_generation: int, *,
                     timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def bindings(self, session_rid: RoutingId) -> tuple[StreamSessionBinding, ...]: ...
    def send_to_actor(self, session_rid: RoutingId, actor: ActorRef,
                      parts: Sequence[Message], *, metadata: bytes = b"",
                      flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
    def request_to_actor(self, session_rid: RoutingId, actor: ActorRef,
                         parts: Sequence[Message], *, metadata: bytes = b"",
                         flags: SendFlags = SendFlags.NONE,
                         timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
    def close(self) -> CloseResult: ...

def reply(token: ReplyToken, parts: Sequence[Message], *,
          flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
def actor_join_reply(token: ReplyToken, result: ActorJoinResult,
                     parts: Sequence[Message] = (), *,
                     flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
```

`MeshNode`에는 다음 Actor·transfer·bound-session 메서드도 정확히 포함한다.

```python
def actor_join_spot(self, actor: ActorRef, target_node_rid: RoutingId,
                    target_spot_rid: RoutingId, target_spot_generation: int,
                    creation_parts: Sequence[Message] = (), *, timeout_ms: int = 0
                    ) -> tuple[SubmitResult, OperationId]: ...
def actor_join_entry_spot(self, actor: ActorRef, target_node_rid: RoutingId,
                          creation_parts: Sequence[Message] = (), *, timeout_ms: int = 0
                          ) -> tuple[SubmitResult, OperationId]: ...
def actor_leave_spot(self, actor: ActorRef, expected_membership_epoch: int, *,
                     timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
def actor_send_to_actor(self, source: ActorRef, target: ActorRef,
                        parts: Sequence[Message], *, metadata: bytes = b"",
                        flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
def actor_request_to_actor(self, source: ActorRef, target: ActorRef,
                           parts: Sequence[Message], *, metadata: bytes = b"",
                           flags: SendFlags = SendFlags.NONE,
                           timeout_ms: int = 0) -> tuple[SubmitResult, OperationId]: ...
def actor_transfer_prepare(self, prepare: ActorTransferPrepare, *, timeout_ms: int = 0
                           ) -> tuple[RequestResult, ActorTransferToken | None,
                                      ActorTransferPrepareResult | None]: ...
def actor_transfer_commit(self, token: ActorTransferToken,
                          new_membership_epoch: int) -> None: ...
def actor_transfer_activate(self, token: ActorTransferToken) -> None: ...
def actor_transfer_abort(self, token: ActorTransferToken) -> None: ...
def actor_send_bound_session(self, actor: ActorRef, parts: Sequence[Message], *,
                             flags: SendFlags = SendFlags.NONE) -> SubmitResult: ...
def actor_close_bound_session(self, actor: ActorRef,
                              expected_binding_generation: int, *, timeout_ms: int = 0
                              ) -> tuple[SubmitResult, OperationId]: ...
```

## 4. Go exact interface

아래 export는 `zlink.systems/zlink/contracts`에 둔다. 실패 가능한 설정과 조회는 `error`, Core가 결과
domain을 공개한 submit·request·close는 typed result와 `error`를 함께 반환한다.

```go
func NewMeshNode(ctx *Context, options MeshNodeOptions) (*MeshNode, error)
func NewReadyBatch(recordCapacity int) (*ReadyBatch, error)
func NewReceiveBatch(messageCapacity, partCapacity, byteCapacity int) (*ReceiveBatch, error)
func NewStreamSessionService(node *MeshNode, stream *StreamSocket) (*StreamSessionService, error)

func (n *MeshNode) SetBind(endpoint string) error
func (n *MeshNode) SetRoutingID(rid RoutingID) error
func (n *MeshNode) AddChannelName(name string) error
func (n *MeshNode) SetChannelWeight(name string, weight uint32) error
func (n *MeshNode) Start() error
func (n *MeshNode) Shutdown(timeout time.Duration) (RequestResult, error)
func (n *MeshNode) Close() (CloseResult, error)
func (n *MeshNode) ConnectPeer(endpoint string, expected *RoutingID) (uint64, error)
func (n *MeshNode) RemovePeerConnection(intentID uint64) error
func (n *MeshNode) DisconnectPeer(rid RoutingID, generation uint64) error
func (n *MeshNode) SendToNode(rid RoutingID, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (n *MeshNode) RequestToNode(rid RoutingID, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) SendToChannel(channel string, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (n *MeshNode) RequestToChannel(channel string, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) NewPublisher() (*MeshNodePublisher, error)
func (n *MeshNode) SetReadyHandler(handler func(ReadyDomain) ReadyDomain) error
func (n *MeshNode) DrainReady(domains ReadyDomain, batch *ReadyBatch, flags RecvFlags) (DrainResult, error)
func (n *MeshNode) Status() (MeshNodeStatus, error)
func (n *MeshNode) Peers() ([]MeshPeerEntry, error)
func (n *MeshNode) PeerChannels(rid RoutingID, generation uint64) ([]PeerChannel, error)
func (n *MeshNode) SetRouterHWMProfile(profile AutoHwmProfile) error
func (n *MeshNode) RouterHWMProfile() (AutoHwmProfile, error)
func (n *MeshNode) SetRouterHWM(value int32) error
func (n *MeshNode) RouterHWM() (int32, error)
func (n *MeshNode) SetMailboxMessageBudget(value uint64) error
func (n *MeshNode) MailboxMessageBudget() (uint64, error)
func (n *MeshNode) SetMailboxByteBudget(value uint64) error
func (n *MeshNode) MailboxByteBudget() (uint64, error)
func (n *MeshNode) NewSpot() (*Spot, error)
func (n *MeshNode) EntrySpot() (*Spot, error)
func (n *MeshNode) LookupSpot(rid RoutingID) (*Spot, bool, error)
func (n *MeshNode) GetOrCreateSpot(rid RoutingID) (*Spot, bool, error)
func (n *MeshNode) NewActor(id string, creation []Message, flags SendFlags, timeout time.Duration) (RequestResult, ActorRef, error)
func (n *MeshNode) LookupActor(id string) (ActorLocation, bool, error)
func (n *MeshNode) LookupRemoteActor(target RoutingID, id string, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) DestroyActor(actor ActorRef, timeout time.Duration) (SubmitResult, OperationID, error)
```

Go의 나머지 exact signature는 다음과 같다.

```go
func (n *MeshNode) SendToActor(actor ActorRef, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (n *MeshNode) RequestToActor(actor ActorRef, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) ActorSendToActor(source, target ActorRef, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (n *MeshNode) ActorRequestToActor(source, target ActorRef, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) JoinActorSpot(actor ActorRef, targetNode, targetSpot RoutingID, targetGeneration uint64, creation []Message, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) JoinActorEntrySpot(actor ActorRef, targetNode RoutingID, creation []Message, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) LeaveActorSpot(actor ActorRef, expectedEpoch uint64, timeout time.Duration) (SubmitResult, OperationID, error)
func (n *MeshNode) PrepareActorTransfer(prepare ActorTransferPrepare, timeout time.Duration) (RequestResult, ActorTransferToken, ActorTransferPrepareResult, error)
func (n *MeshNode) CommitActorTransfer(token *ActorTransferToken, newEpoch uint64) error
func (n *MeshNode) ActivateActorTransfer(token *ActorTransferToken) error
func (n *MeshNode) AbortActorTransfer(token *ActorTransferToken) error
func (n *MeshNode) ActorSendBoundSession(actor ActorRef, parts []Message, flags SendFlags) (SubmitResult, error)
func (n *MeshNode) ActorCloseBoundSession(actor ActorRef, expectedGeneration uint64, timeout time.Duration) (SubmitResult, OperationID, error)
func (p *MeshNodePublisher) Publish(channel, topic string, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, PublishDetail, error)
func (b *ReadyBatch) Reset() error
func (b *ReadyBatch) Records() []ReadyRecord
func (b *ReadyBatch) TakeClaim(index int) (*Claim, error)
func (b *ReadyBatch) Close() (CloseResult, error)
func (c *Claim) RecvBatch(batch *ReceiveBatch, flags RecvFlags) (ClaimRecvResult, error)
func (c *Claim) Close() (CloseResult, error)
func (b *ReceiveBatch) Reset() error
func (b *ReceiveBatch) Records() []ReceiveRecord
func (b *ReceiveBatch) Parts() []MessageView
func (b *ReceiveBatch) RetainMessage(index int) ([]Message, error)
func (b *ReceiveBatch) Close() (CloseResult, error)
func Reply(token *ReplyToken, parts []Message, flags SendFlags) (SubmitResult, error)
func ActorJoinReply(token *ReplyToken, result ActorJoinResult, parts []Message, flags SendFlags) (SubmitResult, error)
```

```go
func (s *Spot) Status() (SpotStatus, error)
func (s *Spot) SendToChannel(channel string, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (s *Spot) RequestToChannel(channel string, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (s *Spot) SendToSpot(targetNode, targetSpot RoutingID, targetGeneration uint64, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (s *Spot) RequestToSpot(targetNode, targetSpot RoutingID, targetGeneration uint64, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (s *Spot) Publish(channel, topic string, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, PublishDetail, error)
func (s *Spot) SetSubscription(channel, filter string, kind SpotSubscriptionKind) error
func (s *Spot) UnsetSubscription(channel, filter string, kind SpotSubscriptionKind) error
func (s *Spot) Close() (CloseResult, error)
func (s *StreamSessionService) Start() error
func (s *StreamSessionService) Shutdown(timeout time.Duration) (RequestResult, error)
func (s *StreamSessionService) Status() (StreamSessionStatus, error)
func (s *StreamSessionService) BindActor(session RoutingID, actor ActorRef, timeout time.Duration) (SubmitResult, OperationID, error)
func (s *StreamSessionService) UnbindActor(session RoutingID, actor ActorRef, expectedGeneration uint64, timeout time.Duration) (SubmitResult, OperationID, error)
func (s *StreamSessionService) Bindings(session RoutingID) ([]StreamSessionBinding, error)
func (s *StreamSessionService) SendToActor(session RoutingID, actor ActorRef, parts []Message, metadata []byte, flags SendFlags) (SubmitResult, error)
func (s *StreamSessionService) RequestToActor(session RoutingID, actor ActorRef, parts []Message, metadata []byte, flags SendFlags, timeout time.Duration) (SubmitResult, OperationID, error)
func (s *StreamSessionService) Close() (CloseResult, error)
```

## 5. Rust exact interface

아래 item은 `contracts/service/`가 소유하고 crate root가 re-export한다. raw FFI type은 공개하지 않는다.

```rust
impl MeshNode {
    pub fn new(ctx: &Context, options: MeshNodeOptions) -> Result<Self, ZlinkError>;
    pub fn set_bind(&self, endpoint: &str) -> Result<(), ZlinkError>;
    pub fn set_routing_id(&self, rid: &RoutingId) -> Result<(), ZlinkError>;
    pub fn add_channel_name(&self, name: &str) -> Result<(), ZlinkError>;
    pub fn set_channel_weight(&self, name: &str, weight: u32) -> Result<(), ZlinkError>;
    pub fn start(&self) -> Result<(), ZlinkError>;
    pub fn shutdown(&self, timeout: Duration) -> Result<RequestResult, ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
    pub fn connect_peer(&self, endpoint: &str, expected: Option<&RoutingId>) -> Result<u64, ZlinkError>;
    pub fn remove_peer_connection(&self, intent_id: u64) -> Result<(), ZlinkError>;
    pub fn disconnect_peer(&self, rid: &RoutingId, generation: u64) -> Result<(), ZlinkError>;
    pub fn send_to_node(&self, rid: &RoutingId, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_node(&self, rid: &RoutingId, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn send_to_channel(&self, channel: &str, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_channel(&self, channel: &str, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn publisher(&self) -> Result<MeshNodePublisher, ZlinkError>;
    pub fn set_ready_handler(&self, handler: Option<ReadyHandler>) -> Result<(), ZlinkError>;
    pub fn drain_ready(&self, domains: ReadyDomain, batch: &mut ReadyBatch, flags: RecvFlags) -> Result<DrainResult, ZlinkError>;
    pub fn status(&self) -> Result<MeshNodeStatus, ZlinkError>;
    pub fn peers(&self) -> Result<Vec<MeshPeerEntry>, ZlinkError>;
    pub fn peer_channels(&self, rid: &RoutingId, generation: u64) -> Result<Vec<PeerChannel>, ZlinkError>;
    pub fn set_router_hwm_profile(&self, profile: AutoHwmProfile) -> Result<(), ZlinkError>;
    pub fn router_hwm_profile(&self) -> Result<AutoHwmProfile, ZlinkError>;
    pub fn set_router_hwm(&self, value: i32) -> Result<(), ZlinkError>;
    pub fn router_hwm(&self) -> Result<i32, ZlinkError>;
    pub fn set_mailbox_message_budget(&self, value: u64) -> Result<(), ZlinkError>;
    pub fn mailbox_message_budget(&self) -> Result<u64, ZlinkError>;
    pub fn set_mailbox_byte_budget(&self, value: u64) -> Result<(), ZlinkError>;
    pub fn mailbox_byte_budget(&self) -> Result<u64, ZlinkError>;
    pub fn new_spot(&self) -> Result<Spot, ZlinkError>;
    pub fn entry_spot(&self) -> Result<Spot, ZlinkError>;
    pub fn lookup_spot(&self, rid: &RoutingId) -> Result<Option<Spot>, ZlinkError>;
    pub fn get_or_create_spot(&self, rid: &RoutingId) -> Result<(Spot, bool), ZlinkError>;
    pub fn new_actor(&self, id: &str, creation: &[Message], flags: SendFlags, timeout: Duration) -> Result<(RequestResult, Option<ActorRef>), ZlinkError>;
    pub fn lookup_actor(&self, id: &str) -> Result<Option<ActorLocation>, ZlinkError>;
    pub fn lookup_remote_actor(&self, target: &RoutingId, id: &str, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn destroy_actor(&self, actor: &ActorRef, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
}
```

나머지 Rust exact signature는 다음과 같다.

```rust
impl MeshNode {
    pub fn send_to_actor(&self, actor: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_actor(&self, actor: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn actor_send_to_actor(&self, source: &ActorRef, target: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn actor_request_to_actor(&self, source: &ActorRef, target: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn actor_join_spot(&self, actor: &ActorRef, target_node: &RoutingId, target_spot: &RoutingId, target_generation: u64, creation: &[Message], timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn actor_join_entry_spot(&self, actor: &ActorRef, target_node: &RoutingId, creation: &[Message], timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn actor_leave_spot(&self, actor: &ActorRef, expected_epoch: u64, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn actor_transfer_prepare(&self, prepare: &ActorTransferPrepare, timeout: Duration) -> Result<(RequestResult, Option<ActorTransferToken>, Option<ActorTransferPrepareResult>), ZlinkError>;
    pub fn actor_transfer_commit(&self, token: &mut ActorTransferToken, new_epoch: u64) -> Result<(), ZlinkError>;
    pub fn actor_transfer_activate(&self, token: &mut ActorTransferToken) -> Result<(), ZlinkError>;
    pub fn actor_transfer_abort(&self, token: &mut ActorTransferToken) -> Result<(), ZlinkError>;
    pub fn actor_send_bound_session(&self, actor: &ActorRef, parts: &[Message], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn actor_close_bound_session(&self, actor: &ActorRef, expected_generation: u64, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
}
impl Spot {
    pub fn status(&self) -> Result<SpotStatus, ZlinkError>;
    pub fn send_to_channel(&self, channel: &str, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_channel(&self, channel: &str, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn send_to_spot(&self, target_node: &RoutingId, target_spot: &RoutingId, target_generation: u64, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_spot(&self, target_node: &RoutingId, target_spot: &RoutingId, target_generation: u64, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn publish(&self, channel: &str, topic: &str, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<(SubmitResult, PublishDetail), ZlinkError>;
    pub fn set_subscription(&self, channel: &str, filter: &str, kind: SpotSubscriptionKind) -> Result<(), ZlinkError>;
    pub fn unset_subscription(&self, channel: &str, filter: &str, kind: SpotSubscriptionKind) -> Result<(), ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
}
impl StreamSessionService {
    pub fn new(node: &MeshNode, stream: &StreamSocket) -> Result<Self, ZlinkError>;
    pub fn start(&self) -> Result<(), ZlinkError>;
    pub fn shutdown(&self, timeout: Duration) -> Result<RequestResult, ZlinkError>;
    pub fn status(&self) -> Result<StreamSessionStatus, ZlinkError>;
    pub fn bind_actor(&self, session: &RoutingId, actor: &ActorRef, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn unbind_actor(&self, session: &RoutingId, actor: &ActorRef, expected_generation: u64, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn bindings(&self, session: &RoutingId) -> Result<Vec<StreamSessionBinding>, ZlinkError>;
    pub fn send_to_actor(&self, session: &RoutingId, actor: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
    pub fn request_to_actor(&self, session: &RoutingId, actor: &ActorRef, parts: &[Message], metadata: &[u8], flags: SendFlags, timeout: Duration) -> Result<(SubmitResult, OperationId), ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
}
impl ReadyBatch {
    pub fn new(record_capacity: usize) -> Result<Self, ZlinkError>;
    pub fn reset(&mut self) -> Result<(), ZlinkError>;
    pub fn records(&self) -> &[ReadyRecord];
    pub fn take_claim(&mut self, index: usize) -> Result<Claim, ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
}
impl Claim {
    pub fn recv_batch(&mut self, batch: &mut ReceiveBatch, flags: RecvFlags) -> Result<ClaimRecvResult, ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
}
impl ReceiveBatch {
    pub fn new(message_capacity: usize, part_capacity: usize, byte_capacity: usize) -> Result<Self, ZlinkError>;
    pub fn reset(&mut self) -> Result<(), ZlinkError>;
    pub fn records(&self) -> &[ReceiveRecord];
    pub fn parts(&self) -> &[MessageView];
    pub fn retain_message(&self, index: usize) -> Result<Vec<Message>, ZlinkError>;
    pub fn close(&mut self) -> Result<CloseResult, ZlinkError>;
}
pub fn reply(token: &mut ReplyToken, parts: &[Message], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
pub fn actor_join_reply(token: &mut ReplyToken, result: ActorJoinResult, parts: &[Message], flags: SendFlags) -> Result<SubmitResult, ZlinkError>;
```

각 리소스는 concrete RAII type이며 명시적 `close(&mut self)`와 idempotent `Drop`을 제공한다. claim은
`take_claim`에서 이동되며 `Clone`이 아니다. reply token도 `Clone`이 아니다. 다만
`reply(token: &mut ReplyToken, ...)`와 `actor_join_reply(token: &mut ReplyToken, ...)`는 token을 빌리고,
`SubmitResult::Ok`일 때만 내부 상태를 consumed로 바꾼다. backpressure 등 성공하지 않은 submit에서는
claim을 release하기 전 같은 token으로 재시도할 수 있다. Core가 thread-safe를 보장하지 않은 handle에는
`Send`나 `Sync`를 구현하지 않는다.

## 6. Snapshot과 operation 값

각 언어의 concrete immutable value는 아래 필드를 빠짐없이 제공한다. Python과 Rust는 snake_case, Go는
PascalCase로 표기하며 `struct_size`, ABI `version`, native pointer와 길이 필드는 공개 값에서 제외한다.

| 공개 값 | 필드 |
|---|---|
| `MeshNodeOptions` | `mesh_name`, `trust_profile` |
| `PublishDetail` | `snapshot_remote_target_count`, `admitted_remote_target_count`, `dropped_remote_target_count`, `unreachable_remote_target_count`, `snapshot_local_spot_count`, `admitted_local_spot_count`, `dropped_local_spot_count` |
| `MeshNodeStatus` | `state`, `routing_id`, `mesh_name`, `local_endpoint`, `lifecycle_generation`, `descriptor_revision`, `channel_count`, `configured_peer_count`, `admitted_peer_count`, `draining_peer_count`, `pending_application_messages`, `pending_infrastructure_messages`, `pending_bytes`, `multicast_submitted`, `multicast_dropped_targets`, `last_error`, `last_changed_ms` |
| `MeshPeerEntry` | `connection_intent_id`, `source`, `state`, `routing_id`, `lifecycle_generation`, `descriptor_revision`, `endpoint`, `channel_count`, `last_error`, `last_changed_ms` |
| `PeerChannel` | `name`, `weight` |
| `SpotStatus` | `spot_rid`, `spot_kind`, `lifecycle_generation`, `pending_application_messages`, `pending_infrastructure_messages`, `pending_bytes`, `active_actor_count`, `draining`, `last_error`, `last_changed_ms` |
| `ReadyRecord` | `owner_kind`, `domain`, `spot_rid`, `actor` |
| `ReceiveRequirements` | `message_count`, `part_count`, `byte_count` |
| `ReceiveRecord` | `kind`, `domain`, `source_node_rid`, `source_spot_rid`, `source_actor`, `operation_id`, `operation_kind`, `reply_token`, `channel_name`, `topic`, `application_metadata`, `kind_data`, `part_offset`, `part_count`, `terminal_result`, `failure_errno` |
| `SendReadyData` | `destination_kind`, `target_node_rid`, `target_spot_rid`, `target_actor`, `channel_name` |
| `ActorRef` | `node_rid`, `actor_id`, `generation` |
| `ActorLocation` | `actor`, `spot_rid`, `spot_generation`, `membership_epoch` |
| `ActorControlRecord` | `kind`, `previous_actor`, `current_actor`, `previous_spot_rid`, `current_spot_rid`, `previous_spot_generation`, `current_spot_generation`, `previous_membership_epoch`, `current_membership_epoch`, `result_code` |
| `ActorJoinCompletion` | `join_result`, `actor`, `location` |
| `ActorTransferPrepare` | `role`, `transfer_id`, `actor`, `expected_membership_epoch`, `peer_node_rid`, `final_sequence`, `reserve_message_count`, `reserve_byte_count` |
| `ActorTransferPrepareResult` | `role`, `transfer_id`, `actor`, `final_sequence`, `reserve_message_count`, `reserve_byte_count` |
| `ActorTransferControl` | `phase`, `role`, `transfer_id`, `actor`, `membership_epoch`, `final_sequence`, `result_code`, `failure_errno` |
| `StreamSessionBinding` | `session_rid`, `actor`, `binding_generation`, `membership_epoch` |
| `StreamSessionStatus` | `state`, `lifecycle_generation`, `session_count`, `binding_count`, `pending_message_count`, `pending_byte_count`, `last_error` |

`ClaimRecvResult`는 `recv_result`와 `required: ReceiveRequirements`, `DrainResult`는 `recv_result`와
`has_residue`를 갖는다. `OperationId`와 `ActorTransferId`는 high/low 64-bit 값이다. `ReplyToken`, `Claim`과
`ActorTransferToken`의 opaque word는 공개 field가 아니며 생성자도 제공하지 않는다.

## 7. 오류와 ownership

- Python은 설정·검증·native failure를 기존 typed exception으로 변환하고 Core 결과 domain은 enum 반환을
  보존한다. type annotation이 runtime 검증을 대신하지 않는다.
- Go는 programmer input과 native 오류를 typed `error`로, 정상적인 Core 결과를 result 값으로 반환한다.
- Rust는 `ZlinkError`에 errno와 Core result 문맥을 보존한다. 정상적인 `busy`, `timeout`, `would_block`을
  panic으로 바꾸지 않는다.
- node는 publisher, Spot, session service와 미처리 callback보다 오래 유지되어야 한다. child가 남은
  `close`의 `busy`는 handle을 보존해 재시도할 수 있게 한다.
- ready record의 claim은 `take_claim`에 성공한 순간 batch에서 호출자로 이동한다. reset과 close는 이동된
  claim을 해제하지 않는다.
- receive batch record와 parts는 다음 reset 또는 close까지만 유효하다. retain은 독립 Message를 만든다.
- reply token은 성공한 submit에서만 정확히 한 번 소비된다. 성공 뒤 중복 사용은 native에 전달하지 않고
  거부하며, backpressure 등 성공하지 않은 submit 뒤에는 claim을 release하기 전 재시도할 수 있다.
- callback은 Core callback thread에서 사용자 작업을 오래 실행하지 않는다. callback 경계 밖으로 panic이나
  Python 예외가 전파되지 않으며 Go callback은 런타임이 관리하는 전달 경계로 넘긴다.

## 8. 구현 전 red contract test

각 언어는 새 `MeshNode` import 또는 compile fixture, 제거 이름의 실패 fixture, struct layout fixture,
claim 이동·batch reset·retain·reply token 재사용·close busy와 callback 예외 회귀를 먼저 추가한다. package
consumer는 private runtime, repository 상대 경로와 `core/build` fallback 없이 같은 fixture를 실행한다.
이 red test와 본 초안의 review가 끝난 뒤 Python 구현을 시작한다.
