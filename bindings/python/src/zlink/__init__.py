# SPDX-License-Identifier: MPL-2.0

from .contracts import (
    version,
    strerror,
    has,
    proxy,
    proxy_steerable,
    sleep,
    multipart_close,
    Context,
    ContextOptions,
    CommonSocketOptions,
    DealerSocketOptions,
    StreamSocketOptions,
    SubSocketOptions,
    PubSocketOptions,
    RouterSocketOptions,
    PairSocket,
    DealerSocket,
    RouterSocket,
    StreamSocket,
    PubSocket,
    SubSocket,
    XPubSocket,
    XSubSocket,
    Message,
    Received,
    TopicMessage,
    RoutingId,
    SubscriptionEvent,
    AtomicCounter,
    Stopwatch,
    Thread,
    Timer,
    Poller,
    MonitorStatus,
    MonitorEvent,
    MonitorSocket,
    Registry,
    Discovery,
    MemberPeerEntry,
    RegistryStatus,
    RegistryServiceSummaryEntry,
    RegistryServiceSummaryFilter,
    RegistryTopologyEntry,
    RegistryTopologyFilter,
    RegistryQueryClient,
    SendOp,
    RequestOp,
    RequestCallbackOp,
    ReplyOp,
    SpotNode,
    Spot,
    Actor,
    ActorRef,
    ActorJoinRequest,
    ActorRoute,
    SpotRoute,
    ActorRecvInfo,
    ActorJoinInfo,
    remote_actor_ref,
    SpotDispatchInfo,
    SpotNodeActorEntry,
    SpotNodeSpotEntry,
    SpotNodeStatus,
    SpotNodePeerEntry,
    SpotNodePeerFilter,
    SpotNodeSubjectEntry,
    SpotNodeSubjectFilter,
    SpotNodeSocketEntry,
    SpotNodeSocketFilter,
    ZlinkError,
    SubmitError,
    RequestError,
    RecvError,
    HandlerError,
    CloseError,
    BindError,
    ConnectError,
    ConfigError,
    SocketType,
    ContextOption,
    AutoHwmProfile,
    AutoHwmRecalcReason,
    SocketOption,
    SendFlags,
    RecvFlags,
    SubmitResult,
    RequestResult,
    RecvResult,
    HandlerResult,
    CloseResult,
    BindResult,
    ConnectResult,
    ConfigResult,
    RouterOption,
    RidDuplicatePolicy,
    SubmitRetryMode,
    ErrorCode,
    ProtocolError,
    MonitorEventMask,
    DisconnectReason,
    PollEventFlag,
    AutoConnectType,
    ServiceRole,
    RegistrySocketRole,
    DiscoverySocketRole,
    SpotNodeSocketRole,
    SpotNodeMode,
    SpotNodeSocketOwner,
    SpotNodeOption,
    SpotNodeState,
    SpotPeerSource,
    SpotPeerKind,
    SpotPeerState,
    SpotKind,
    SpotSocketRole,
    SpotServiceAttachmentRole,
    SpotActorLifecycleEventKind,
    SpotDispatchEvent,
    SpotDispatchSubjectKind,
    RegistryState,
    TopologySource,
    TopologyState,
    ServiceKind,
    SubjectKind,
    SpotRole,
    PollSourceKind,
    PollEvent,
    PollEvents,
)
from .contracts.core.context import register_context_factory
from ._runtime.core.context import create_context
from .contracts.core.utilities import register_core_implementation
from ._runtime.core import zlink as core_runtime
from .contracts.eventing.poller import register_poller_factories
from ._runtime.eventing.poller import create_poll_events, create_poller
from .contracts.eventing.timer import register_timer_factories
from ._runtime.eventing.timer import create_timer, create_timer_from_spot
from .contracts.eventing.monitor import register_socket_monitor_factory
from ._runtime.eventing.monitor import open_socket_monitor as runtime_open_socket_monitor
from .contracts.messaging.message import register_messaging_factories
from ._runtime.messaging import message_materializer as messaging_runtime
from .contracts.sockets.socket_options import register_socket_option_factories
from ._runtime.options import option_mapping as socket_options_runtime
from .contracts.sockets.socket import register_socket_factories
from ._runtime.sockets import socket_base_impl as socket_runtime
from .contracts.service.discovery import register_discovery_factories
from ._runtime.service.discovery import discovery as discovery_runtime
from .contracts.service.spot import (
    register_spot_factories,
)
from ._runtime.service.spot import spot as spot_runtime


def _bootstrap_runtime():
    register_context_factory(create_context)
    register_core_implementation(
        core_runtime,
        stopwatch_factory=core_runtime.create_stopwatch,
        thread_factory=core_runtime.create_thread,
        atomic_counter_factory=core_runtime.create_atomic_counter,
    )
    register_poller_factories(
        poller_factory=create_poller,
        poll_events_factory=create_poll_events,
    )
    register_timer_factories(
        timer_factory=create_timer,
        spot_timer_factory=create_timer_from_spot,
    )
    register_socket_monitor_factory(runtime_open_socket_monitor)
    register_messaging_factories(
        message_factory=messaging_runtime.create_message,
        message_from_factory=messaging_runtime.message_from,
        message_allocate_factory=messaging_runtime.message_allocate,
        message_wrap_buffer_factory=messaging_runtime.wrap_message_buffer,
        received_message_factory=messaging_runtime.create_received_message,
        received_message_from_owner_factory=messaging_runtime.received_message_from_owner,
        received_multipart_factory=messaging_runtime.create_received_multipart,
        received_factory=messaging_runtime.create_received,
        topic_message_factory=messaging_runtime.create_topic_message,
        subscription_event_factory=messaging_runtime.create_subscription_event,
    )
    register_socket_option_factories(
        common_socket_options_factory=socket_options_runtime.create_common_socket_options,
        dealer_socket_options_factory=socket_options_runtime.create_dealer_socket_options,
        stream_socket_options_factory=socket_options_runtime.create_stream_socket_options,
        sub_socket_options_factory=socket_options_runtime.create_sub_socket_options,
        pub_socket_options_factory=socket_options_runtime.create_pub_socket_options,
        router_socket_options_factory=socket_options_runtime.create_router_socket_options,
    )
    register_socket_factories(
        pair_socket_factory=socket_runtime.create_pair_socket,
        dealer_socket_factory=socket_runtime.create_dealer_socket,
        router_socket_factory=socket_runtime.create_router_socket,
        stream_socket_factory=socket_runtime.create_stream_socket,
        pub_socket_factory=socket_runtime.create_pub_socket,
        sub_socket_factory=socket_runtime.create_sub_socket,
        xpub_socket_factory=socket_runtime.create_xpub_socket,
        xsub_socket_factory=socket_runtime.create_xsub_socket,
    )
    register_discovery_factories(
        registry_factory=discovery_runtime.create_registry,
        discovery_factory=discovery_runtime.create_discovery,
        registry_query_client_factory=discovery_runtime.create_registry_query_client,
    )
    register_spot_factories(
        spot_node_factory=spot_runtime.create_spot_node,
        spot_factory=spot_runtime.create_spot,
    )


_bootstrap_runtime()

create_message = messaging_runtime.create_message
allocate_message = messaging_runtime.message_allocate
create_message_from = messaging_runtime.message_from
create_received = messaging_runtime.create_received
create_topic_message = messaging_runtime.create_topic_message
create_subscription_event = messaging_runtime.create_subscription_event
create_stopwatch = core_runtime.create_stopwatch
create_thread = core_runtime.create_thread
create_atomic_counter = core_runtime.create_atomic_counter

create_pair_socket = socket_runtime.create_pair_socket
create_dealer_socket = socket_runtime.create_dealer_socket
create_router_socket = socket_runtime.create_router_socket
create_stream_socket = socket_runtime.create_stream_socket
create_pub_socket = socket_runtime.create_pub_socket
create_sub_socket = socket_runtime.create_sub_socket
create_xpub_socket = socket_runtime.create_xpub_socket
create_xsub_socket = socket_runtime.create_xsub_socket
create_common_socket_options = socket_options_runtime.create_common_socket_options
create_dealer_socket_options = socket_options_runtime.create_dealer_socket_options
create_stream_socket_options = socket_options_runtime.create_stream_socket_options
create_sub_socket_options = socket_options_runtime.create_sub_socket_options
create_pub_socket_options = socket_options_runtime.create_pub_socket_options
create_router_socket_options = socket_options_runtime.create_router_socket_options

create_registry = discovery_runtime.create_registry
create_discovery = discovery_runtime.create_discovery
create_registry_query_client = discovery_runtime.create_registry_query_client
create_spot_node = spot_runtime.create_spot_node
create_spot = spot_runtime.create_spot

__all__ = [
    "version",
    "strerror",
    "has",
    "proxy",
    "proxy_steerable",
    "sleep",
    "multipart_close",
    "create_context",
    "Context",
    "ContextOptions",
    "CommonSocketOptions",
    "create_common_socket_options",
    "DealerSocketOptions",
    "create_dealer_socket_options",
    "StreamSocketOptions",
    "create_stream_socket_options",
    "SubSocketOptions",
    "create_sub_socket_options",
    "PubSocketOptions",
    "create_pub_socket_options",
    "RouterSocketOptions",
    "create_router_socket_options",
    "PairSocket",
    "create_pair_socket",
    "DealerSocket",
    "create_dealer_socket",
    "RouterSocket",
    "create_router_socket",
    "StreamSocket",
    "create_stream_socket",
    "PubSocket",
    "create_pub_socket",
    "SubSocket",
    "create_sub_socket",
    "XPubSocket",
    "create_xpub_socket",
    "XSubSocket",
    "create_xsub_socket",
    "Message",
    "create_message",
    "allocate_message",
    "create_message_from",
    "Received",
    "create_received",
    "TopicMessage",
    "create_topic_message",
    "RoutingId",
    "SubscriptionEvent",
    "create_subscription_event",
    "AtomicCounter",
    "create_atomic_counter",
    "Stopwatch",
    "create_stopwatch",
    "Thread",
    "create_thread",
    "Timer",
    "create_timer",
    "create_timer_from_spot",
    "Poller",
    "create_poller",
    "MonitorStatus",
    "MonitorEvent",
    "MonitorSocket",
    "Registry",
    "create_registry",
    "Discovery",
    "create_discovery",
    "MemberPeerEntry",
    "RegistryStatus",
    "RegistryServiceSummaryEntry",
    "RegistryServiceSummaryFilter",
    "RegistryTopologyEntry",
    "RegistryTopologyFilter",
    "RegistryQueryClient",
    "create_registry_query_client",
    "SendOp",
    "RequestOp",
    "RequestCallbackOp",
    "ReplyOp",
    "SpotNode",
    "create_spot_node",
    "Spot",
    "create_spot",
    "Actor",
    "ActorRef",
    "ActorJoinRequest",
    "ActorRoute",
    "SpotRoute",
    "ActorRecvInfo",
    "ActorJoinInfo",
    "remote_actor_ref",
    "SpotDispatchInfo",
    "SpotNodeActorEntry",
    "SpotNodeSpotEntry",
    "SpotNodeStatus",
    "SpotNodePeerEntry",
    "SpotNodePeerFilter",
    "SpotNodeSubjectEntry",
    "SpotNodeSubjectFilter",
    "SpotNodeSocketEntry",
    "SpotNodeSocketFilter",
    "ZlinkError",
    "SubmitError",
    "RequestError",
    "RecvError",
    "HandlerError",
    "CloseError",
    "BindError",
    "ConnectError",
    "ConfigError",
    "SocketType",
    "ContextOption",
    "AutoHwmProfile",
    "AutoHwmRecalcReason",
    "SocketOption",
    "SendFlags",
    "RecvFlags",
    "SubmitResult",
    "RequestResult",
    "RecvResult",
    "HandlerResult",
    "CloseResult",
    "BindResult",
    "ConnectResult",
    "ConfigResult",
    "RouterOption",
    "RidDuplicatePolicy",
    "SubmitRetryMode",
    "ErrorCode",
    "ProtocolError",
    "MonitorEventMask",
    "DisconnectReason",
    "PollEventFlag",
    "AutoConnectType",
    "ServiceRole",
    "RegistrySocketRole",
    "DiscoverySocketRole",
    "SpotNodeSocketRole",
    "SpotNodeMode",
    "SpotNodeSocketOwner",
    "SpotNodeOption",
    "SpotNodeState",
    "SpotPeerSource",
    "SpotPeerKind",
    "SpotPeerState",
    "SpotKind",
    "SpotSocketRole",
    "SpotServiceAttachmentRole",
    "SpotActorLifecycleEventKind",
    "SpotDispatchEvent",
    "SpotDispatchSubjectKind",
    "RegistryState",
    "TopologySource",
    "TopologyState",
    "ServiceKind",
    "SubjectKind",
    "SpotRole",
    "PollSourceKind",
    "PollEvent",
    "PollEvents",
    "create_poll_events",
]
