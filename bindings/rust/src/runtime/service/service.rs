use std::ffi::{CStr, CString, c_char, c_void};
use std::mem::MaybeUninit;
use std::ptr;
use std::sync::mpsc;
use std::time::Duration;

mod service_converters;
mod service_helpers;
mod service_ops;
mod spot_receive;

pub(crate) use service_helpers::fixed_cstring_or_panic;
pub(super) use service_helpers::*;

use service_ops::{
    NativeReplyOp, NativeRequestOp, NativeSendOp, ReplyOpKind, RequestOpKind, SendOpKind,
    wrap_reply_op, wrap_request_op, wrap_send_op,
};
pub(crate) use service_ops::{
    actor_bind_op_new, actor_unbind_op_new, dealer_request_op, router_reply_op,
    router_reply_to_spot_op, router_request_op, router_request_to_spot_op, router_send_to_spot_op,
    socket_publish_op, socket_send_op, socket_send_to_op, spot_reply_to_router_op,
    spot_reply_to_spot_op, spot_send_to_spot_op, stream_bound_actor_send_op,
};

use crate::actor_models::{
    ActorJoinEntrySpotResult, ActorJoinInfo, ActorJoinRequest, ActorJoinResult, ActorLookupResult,
    ActorRecvInfo, ActorRef, ActorRoute, DiscoveryRoute, SpotActorLifecycleEvent,
    SpotActorLifecycleEventKind, SpotActorLifecycleInfo, SpotNodeActorEntry, SpotRoute,
};
use crate::actor_received::ActorReceived;
use crate::actor_resource::Actor;
use crate::core_context::AutoHwmProfile;
use crate::discovery_resource::{Discovery, RouteKind};
use crate::domain::Received;
use crate::error::{
    BindError, CloseError, ConfigError, ConnectError, HandlerError, RecvError, RecvResult,
    RequestError, SubmitError, ZlinkError,
};
use crate::ffi;
use crate::flags::{RecvFlags, SendFlags};
use crate::message::{Message, RoutingId};
use crate::messaging_subscription_event::SubscriptionEvent;
use crate::monitor_contracts::MonitorStatus;
use crate::native_errors::{
    check_bind_rc, check_close_rc, check_config_rc, check_connect_rc, check_handler_rc, check_rc,
    check_recv_rc, check_submit_rc, config_validation_error, last_errno, request_error_from_result,
    request_error_from_submit, submit_not_supported_error, submit_validation_error,
};
use crate::registry_models::{
    MemberPeerEntry, RegistryServiceSummaryEntry, RegistryServiceSummaryFilter, RegistryStatus,
    RegistryTopologyEntry, RegistryTopologyFilter,
};
use crate::registry_query_client_resource::RegistryQueryClient;
use crate::registry_resource::Registry;
use crate::request_progress::RequestProgressGuard;
use crate::runtime_bridge::{
    ActorRuntime, DiscoveryRuntime, RegistryQueryClientRuntime, RegistryRuntime, ReplyOpRuntime,
    RequestOpRuntime, SendOpRuntime, SpotNodeRuntime, SpotRuntime,
};
use crate::socket::{
    CallbackBox, prepare_send_parts, send_ready_trampoline, submit_part_sequence, take_parts,
};
use crate::spot_models::{
    AutoConnectType, RegistryState, ServiceKind, ServiceRole, SocketType, SpotDispatchEvent,
    SpotDispatchInfo, SpotKind, SpotNodeMode, SpotNodeOptions, SpotNodePeerEntry,
    SpotNodePeerFilter, SpotNodeSocketEntry, SpotNodeSocketFilter, SpotNodeSocketOwner,
    SpotNodeSpotEntry, SpotNodeState, SpotNodeStatus, SpotNodeSubjectEntry, SpotNodeSubjectFilter,
    SpotPeerKind, SpotPeerSource, SpotPeerState, SpotRole, SubjectKind, TopologySource,
    TopologyState,
};
use crate::spot_node_resource::SpotNode;
use crate::spot_operations::{
    ActorBindOp, ActorDestroyOp, ActorJoinEntrySpotOp, ActorJoinOp, ActorJoinReplyOp, ActorLeaveOp,
    ActorLookupOp, ActorUnbindOp, CallbackReady, Empty, Ready, ReplyOp, RequestOp, SendOp,
};
use crate::spot_resource::Spot;
use crate::topic_message_contract::TopicMessage;
use spot_receive::{recv_spot_routed_parts, recv_spot_subscribed_parts};

mod actor_model_runtime;
mod discovery_runtime;
pub(crate) use discovery_runtime::{
    discovery_actor_route_sync_enabled, discovery_bind_route, discovery_close,
    discovery_connect_registry, discovery_get_value, discovery_member_peers, discovery_new,
    discovery_resolve_actor, discovery_resolve_route, discovery_resolve_spot,
    discovery_set_actor_route_sync_enabled, discovery_set_spot_owner_sync_enabled,
    discovery_set_tls_client, discovery_set_value, discovery_spot_owner_sync_enabled,
    discovery_unbind_route,
};
mod registry_runtime;
use actor_model_runtime::*;
pub(crate) use registry_runtime::*;
mod spot_node_runtime;
pub(crate) use spot_node_runtime::*;
mod actor_ops_runtime;
use actor_ops_runtime::{
    NativeActorJoinEntrySpotOp, NativeActorJoinOp, NativeActorJoinReplyOp, NativeActorLookupOp,
    NativeActorReplyOp, NativeActorReplyOpKind, wrap_actor_join_entry_spot_op, wrap_actor_join_op,
    wrap_actor_join_reply_op, wrap_actor_lookup_op, wrap_actor_reply_op,
};
mod actor_runtime;
mod spot_runtime;
pub(crate) use spot_runtime::*;
mod registry_query_runtime;
pub(crate) use registry_query_runtime::*;
