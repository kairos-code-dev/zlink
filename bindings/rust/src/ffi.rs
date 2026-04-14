//! Raw FFI bindings to the zlink C API.
//! This module is crate-internal and not exposed in the public surface.

#![allow(non_camel_case_types, non_upper_case_globals, dead_code)]

use std::ffi::{c_char, c_int, c_long, c_ulong, c_void};

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_msg_t {
    #[cfg(target_pointer_width = "64")]
    _data: [u64; 8],
    #[cfg(target_pointer_width = "32")]
    _data: [u32; 16],
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct zlink_routing_id_t {
    pub size: u8,
    pub data: [u8; 255],
}

pub type zlink_free_fn = unsafe extern "C" fn(data: *mut c_void, hint: *mut c_void);

pub const ZLINK_MSG_METADATA_KEY_USER_MIN: u16 = 0x0100;
pub const ZLINK_MSG_METADATA_VALUE_MAX: usize = 65535;

// ---------------------------------------------------------------------------
// Context options
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_ctx_option_t {
    ZLINK_IO_THREADS = 1,
    ZLINK_MAX_SOCKETS = 2,
    ZLINK_SOCKET_LIMIT = 3,
    ZLINK_THREAD_SCHED_POLICY = 4,
    ZLINK_MAX_MSGSZ = 5,
    ZLINK_MSG_T_SIZE = 6,
    ZLINK_THREAD_AFFINITY_CPU_ADD = 7,
    ZLINK_THREAD_AFFINITY_CPU_REMOVE = 8,
    ZLINK_THREAD_NAME_PREFIX = 9,
    ZLINK_CTX_OPT_BLOCKY = 10,
}

// ---------------------------------------------------------------------------
// Send flags / result
// ---------------------------------------------------------------------------

pub type zlink_send_flags_t = u32;

pub const ZLINK_DONTWAIT: zlink_send_flags_t = 0x0001;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_send_result_t {
    ZLINK_SEND_RESULT_SENT = 0,
    ZLINK_SEND_RESULT_BACKPRESSURED = 1,
    ZLINK_SEND_RESULT_NOT_READY = 2,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_request_result_t {
    ZLINK_REQUEST_RESULT_OK = 0,
    ZLINK_REQUEST_RESULT_TIMED_OUT = 101,
    ZLINK_REQUEST_RESULT_NOT_FOUND = 102,
    ZLINK_REQUEST_RESULT_TERMINATED = 103,
    ZLINK_REQUEST_RESULT_PROTOCOL_ERROR = 104,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_config_result_t {
    ZLINK_CONFIG_OK = 0,
    ZLINK_CONFIG_INVALID_HANDLE = 701,
    ZLINK_CONFIG_INVALID_ARGUMENT = 702,
    ZLINK_CONFIG_NOT_SUPPORTED = 703,
}

// ---------------------------------------------------------------------------
// Socket types
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_socket_type_t {
    ZLINK_SOCKET_PAIR = 0x1001,
    ZLINK_SOCKET_PUB = 0x1002,
    ZLINK_SOCKET_SUB = 0x1003,
    ZLINK_SOCKET_DEALER = 0x1004,
    ZLINK_SOCKET_ROUTER = 0x1005,
    ZLINK_SOCKET_XPUB = 0x1006,
    ZLINK_SOCKET_XSUB = 0x1007,
    ZLINK_SOCKET_STREAM = 0x1008,
}

// ---------------------------------------------------------------------------
// Socket options
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_option_t {
    ZLINK_OPT_AFFINITY = 0x3001,
    ZLINK_OPT_RATE = 0x3003,
    ZLINK_OPT_RECOVERY_IVL = 0x3004,
    ZLINK_OPT_SNDBUF = 0x3005,
    ZLINK_OPT_RCVBUF = 0x3006,
    ZLINK_OPT_MAXMSGSIZE = 0x300E,
    ZLINK_OPT_SNDHWM = 0x300F,
    ZLINK_OPT_RCVHWM = 0x3010,
    ZLINK_OPT_LINGER = 0x300A,
    ZLINK_OPT_RECONNECT_IVL = 0x300B,
    ZLINK_OPT_BACKLOG = 0x300C,
    ZLINK_OPT_RECONNECT_IVL_MAX = 0x300D,
    ZLINK_OPT_MULTICAST_HOPS = 0x3011,
    ZLINK_OPT_RCVTIMEO = 0x3012,
    ZLINK_OPT_SNDTIMEO = 0x3013,
    ZLINK_OPT_CONNECT_TIMEOUT = 0x3024,
    ZLINK_OPT_HANDSHAKE_IVL = 0x301D,
    ZLINK_OPT_TCP_KEEPALIVE = 0x3015,
    ZLINK_OPT_TCP_KEEPALIVE_CNT = 0x3016,
    ZLINK_OPT_TCP_KEEPALIVE_IDLE = 0x3017,
    ZLINK_OPT_TCP_KEEPALIVE_INTVL = 0x3018,
    ZLINK_OPT_TCP_MAXRT = 0x3025,
    ZLINK_OPT_TCP_NODELAY = 0x3031,
    ZLINK_OPT_HEARTBEAT_IVL = 0x3021,
    ZLINK_OPT_HEARTBEAT_TTL = 0x3022,
    ZLINK_OPT_HEARTBEAT_TIMEOUT = 0x3023,
    ZLINK_OPT_IPV6 = 0x301A,
    ZLINK_OPT_TOS = 0x301C,
    ZLINK_OPT_MULTICAST_MAXTPDU = 0x3026,
    ZLINK_OPT_BINDTODEVICE = 0x3027,
    ZLINK_OPT_TLS_CERT = 0x3028,
    ZLINK_OPT_TLS_KEY = 0x3029,
    ZLINK_OPT_TLS_CA = 0x302A,
    ZLINK_OPT_TLS_VERIFY = 0x302B,
    ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT = 0x302C,
    ZLINK_OPT_TLS_HOSTNAME = 0x302D,
    ZLINK_OPT_TLS_TRUST_SYSTEM = 0x302E,
    ZLINK_OPT_TLS_PASSWORD = 0x302F,
    ZLINK_OPT_IMMEDIATE = 0x3019,
    ZLINK_OPT_CONFLATE = 0x301B,
    ZLINK_OPT_BLOCKY = 0x301E,
    ZLINK_OPT_INVERT_MATCHING = 0x3020,
    ZLINK_OPT_FD = 0x3007,
    ZLINK_OPT_EVENTS = 0x3008,
    ZLINK_OPT_TYPE = 0x3009,
    ZLINK_OPT_LAST_ENDPOINT = 0x3014,
    ZLINK_OPT_ZMP_METADATA = 0x3030,
    ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE = 0x3032,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_router_option_t {
    ZLINK_ROUTER_OPT_MANDATORY = 0x3101,
    ZLINK_ROUTER_OPT_HANDOVER = 0x3102,
    ZLINK_ROUTER_OPT_PROBE = 0x3103,
    ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID = 0x3104,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_dealer_option_t {
    ZLINK_DEALER_OPT_PROBE = 0x3201,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_pub_option_t {
    ZLINK_PUB_OPT_VERBOSE = 0x3301,
    ZLINK_PUB_OPT_VERBOSER = 0x3302,
    ZLINK_PUB_OPT_MANUAL = 0x3303,
    ZLINK_PUB_OPT_MANUAL_LAST_VALUE = 0x3304,
    ZLINK_PUB_OPT_NODROP = 0x3305,
    ZLINK_PUB_OPT_WELCOME_MSG = 0x3306,
    ZLINK_PUB_OPT_TOPICS_COUNT = 0x3307,
    ZLINK_PUB_OPT_APPROVE_SUBSCRIBE = 0x3308,
    ZLINK_PUB_OPT_REJECT_SUBSCRIBE = 0x3309,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_sub_option_t {
    ZLINK_SUB_OPT_TOPICS_COUNT = 0x3400,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_stream_option_t {
    ZLINK_STREAM_OPT_NOTIFY = 0x3501,
}

// ---------------------------------------------------------------------------
// Monitor types
// ---------------------------------------------------------------------------

pub type zlink_socket_monitor_event_mask_t = u32;

pub const ZLINK_SOCKET_MONITOR_EVENT_CONNECTED: u32 = 0x0001;
pub const ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED: u32 = 0x0002;
pub const ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED: u32 = 0x0004;
pub const ZLINK_SOCKET_MONITOR_EVENT_LISTENING: u32 = 0x0008;
pub const ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED: u32 = 0x0010;
pub const ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED: u32 = 0x0020;
pub const ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED: u32 = 0x0040;
pub const ZLINK_SOCKET_MONITOR_EVENT_CLOSED: u32 = 0x0080;
pub const ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED: u32 = 0x0100;
pub const ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED: u32 = 0x0200;
pub const ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED: u32 = 0x0400;
pub const ZLINK_SOCKET_MONITOR_EVENT_ALL: u32 = 0x7FFF;
pub const ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL: u32 = 0x0800;
pub const ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY: u32 = 0x1000;
pub const ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL: u32 = 0x2000;
pub const ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH: u32 = 0x4000;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_disconnect_reason_t {
    ZLINK_DISCONNECT_REASON_UNKNOWN = 0,
    ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED = 3,
    ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR = 4,
    ZLINK_DISCONNECT_REASON_CTX_TERM = 5,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_protocol_error_t {
    ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO = 0x10000013,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_monitor_event_t {
    pub event: u64,
    pub value: u64,
    pub routing_id: zlink_routing_id_t,
    pub local_addr: [c_char; 256],
    pub remote_addr: [c_char; 256],
}

pub type zlink_socket_monitor_event_t = zlink_monitor_event_t;

pub type zlink_monitor_handler_fn =
    unsafe extern "C" fn(event: *const zlink_monitor_event_t, userdata: *mut c_void);
pub type zlink_socket_monitor_handler_fn = zlink_monitor_handler_fn;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_socket_monitor_open_options_t {
    pub events: zlink_socket_monitor_event_mask_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_monitor_source_kind_t {
    ZLINK_MONITOR_SOURCE_SOCKET = 1,
    ZLINK_MONITOR_SOURCE_SPOT_PUB = 3,
    ZLINK_MONITOR_SOURCE_SPOT_SUB = 4,
}

pub type zlink_monitor_state_mask_t = u32;
pub type zlink_monitor_snapshot_detail_mask_t = u32;

pub const ZLINK_MONITOR_STATE_READY: u32 = 1 << 0;
pub const ZLINK_MONITOR_STATE_BOUND_READY: u32 = 1 << 1;
pub const ZLINK_MONITOR_STATE_CLOSED: u32 = 1 << 3;

pub const ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS: u32 = 1 << 1;
pub const ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS: u32 = 1 << 2;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_monitor_snapshot_t {
    pub source_kind: zlink_monitor_source_kind_t,
    pub state_flags: zlink_monitor_state_mask_t,
    pub detail_flags: zlink_monitor_snapshot_detail_mask_t,
    pub snd_pending_msgs: u64,
    pub rcv_pending_msgs: u64,
}

// ---------------------------------------------------------------------------
// Callback function pointer types
// ---------------------------------------------------------------------------

pub type zlink_socket_msg_handler_fn = unsafe extern "C" fn(
    source_rid: *const zlink_routing_id_t,
    parts: *mut zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
);

pub type zlink_subscribe_handler_fn = unsafe extern "C" fn(
    source_rid: *const zlink_routing_id_t,
    topic: *const c_char,
    topic_len: usize,
    parts: *mut zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
);

pub type zlink_send_ready_handler_fn =
    unsafe extern "C" fn(subject: *mut c_void, userdata: *mut c_void);

pub type zlink_reply_handler_fn = unsafe extern "C" fn(
    result: zlink_request_result_t,
    parts: *mut zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
);

pub type zlink_router_handler_fn = unsafe extern "C" fn(
    source_node_rid: *const zlink_routing_id_t,
    source_spot_rid: *const zlink_routing_id_t,
    request_seq: u64,
    parts: *mut zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
);

pub type zlink_spot_handler_fn = unsafe extern "C" fn(
    source_rid: *const zlink_routing_id_t,
    spot_rid: *const zlink_routing_id_t,
    request_seq: u64,
    parts: *mut zlink_msg_t,
    part_count: usize,
    userdata: *mut c_void,
);

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_spot_dispatch_event_t {
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE = 2,
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE = 3,
}

pub type zlink_spot_dispatch_event_handler_fn = unsafe extern "C" fn(
    spot: *mut c_void,
    event: zlink_spot_dispatch_event_t,
    userdata: *mut c_void,
);

// ---------------------------------------------------------------------------
// Service discovery types
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_service_type_t {
    ZLINK_SERVICE_TYPE_SPOT = 0x3002,
    ZLINK_SERVICE_TYPE_SOCKET = 0x3003,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_service_role_t {
    ZLINK_SERVICE_ROLE_INVALID = 0,
    ZLINK_SERVICE_ROLE_SPOT = 2,
    ZLINK_SERVICE_ROLE_ROUTER = 3,
    ZLINK_SERVICE_ROLE_DEALER = 4,
    ZLINK_SERVICE_ROLE_PUB = 5,
    ZLINK_SERVICE_ROLE_SUB = 6,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_service_kind_t {
    ZLINK_SERVICE_KIND_DISCOVERY = 1,
    ZLINK_SERVICE_KIND_SPOT_SUB = 3,
    ZLINK_SERVICE_KIND_SPOT_PUB = 4,
    ZLINK_SERVICE_KIND_SOCKET = 5,
}

pub type zlink_discovery_monitor_event_mask_t = u32;
pub type zlink_spot_monitor_event_mask_t = u32;
pub type zlink_service_event_detail_mask_t = u32;
pub type zlink_service_monitor_event_mask_t = u32;

pub const ZLINK_DISCOVERY_MONITOR_EVENT_ERROR: u32 = 1 << 4;
pub const ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_UP: u32 = 1 << 5;
pub const ZLINK_DISCOVERY_MONITOR_EVENT_SERVICE_DOWN: u32 = 1 << 6;
pub const ZLINK_DISCOVERY_MONITOR_EVENT_PROVIDERS_CHANGED: u32 = 1 << 7;
pub const ZLINK_DISCOVERY_MONITOR_EVENT_CLOSED: u32 = 1 << 17;

pub const ZLINK_SERVICE_EVENT_DETAIL_SERVICE_NAME: u32 = 0x0001;
pub const ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT: u32 = 0x0002;
pub const ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_RID: u32 = 0x0004;
pub const ZLINK_SERVICE_EVENT_DETAIL_PEER_RID: u32 = 0x0008;
pub const ZLINK_SERVICE_EVENT_DETAIL_SUBJECT: u32 = 0x0010;
pub const ZLINK_SERVICE_EVENT_DETAIL_SUBJECT_KIND: u32 = 0x0020;

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_service_event_subject_kind_t {
    ZLINK_SERVICE_EVENT_SUBJECT_NONE = 0,
    ZLINK_SERVICE_EVENT_SUBJECT_TOPIC = 1,
    ZLINK_SERVICE_EVENT_SUBJECT_PATTERN = 2,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_service_event_t {
    pub service_kind: zlink_service_kind_t,
    pub event_type: u32,
    pub status: i32,
    pub error_code: i32,
    pub value: u32,
    pub detail_flags: zlink_service_event_detail_mask_t,
    pub service_name: [c_char; 256],
    pub endpoint: [c_char; 256],
    pub routing_id: zlink_routing_id_t,
    pub subject: [c_char; 256],
    pub subject_kind: u32,
}

pub type zlink_service_monitor_event_t = zlink_service_event_t;

pub type zlink_service_monitor_handler_fn =
    unsafe extern "C" fn(event: *const zlink_service_event_t, userdata: *mut c_void);

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_service_monitor_open_options_t {
    pub events: zlink_service_monitor_event_mask_t,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_monitor_target_kind_t {
    ZLINK_MONITOR_TARGET_SOCKET = 1,
    ZLINK_MONITOR_TARGET_DISCOVERY = 2,
    ZLINK_MONITOR_TARGET_SPOT = 4,
    ZLINK_MONITOR_TARGET_SPOT_NODE = 5,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_spot_role_t {
    ZLINK_SPOT_ROLE_PUB = 1,
    ZLINK_SPOT_ROLE_SUB = 2,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_spot_node_state_t {
    ZLINK_SPOT_NODE_STATE_IDLE = 1,
    ZLINK_SPOT_NODE_STATE_CONNECTING = 2,
    ZLINK_SPOT_NODE_STATE_PARTIAL_READY = 3,
    ZLINK_SPOT_NODE_STATE_READY = 4,
    ZLINK_SPOT_NODE_STATE_ERROR = 5,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_spot_node_status_t {
    pub service_name: [c_char; 256],
    pub local_endpoint: [c_char; 256],
    pub node_routing_id: zlink_routing_id_t,
    pub state: zlink_spot_node_state_t,
    pub configured_peer_count: u32,
    pub active_peer_count: u32,
    pub connected_peer_count: u32,
    pub subject_count: u32,
    pub ready_subject_count: u32,
    pub last_error: i32,
    pub last_changed_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_spot_peer_source_t {
    ZLINK_SPOT_PEER_SOURCE_MANUAL = 1,
    ZLINK_SPOT_PEER_SOURCE_DISCOVERY = 2,
    ZLINK_SPOT_PEER_SOURCE_MIXED = 3,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_spot_peer_state_t {
    ZLINK_SPOT_PEER_STATE_CONFIGURED = 1,
    ZLINK_SPOT_PEER_STATE_CONNECTING = 2,
    ZLINK_SPOT_PEER_STATE_CONNECTED = 3,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_spot_node_peer_entry_t {
    pub service_name: [c_char; 256],
    pub local_endpoint: [c_char; 256],
    pub peer_endpoint: [c_char; 256],
    pub source: zlink_spot_peer_source_t,
    pub state: zlink_spot_peer_state_t,
    pub connected_since_ms: u64,
    pub last_changed_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_spot_node_peer_filter_t {
    pub peer_endpoint: [c_char; 256],
    pub source: zlink_spot_peer_source_t,
    pub state: zlink_spot_peer_state_t,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_spot_node_subject_entry_t {
    pub role: zlink_spot_role_t,
    pub subject: [c_char; 256],
    pub subject_kind: u32,
    pub ready_peer_count: u32,
    pub active_peer_count: u32,
    pub last_changed_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_spot_node_subject_filter_t {
    pub role: zlink_spot_role_t,
    pub subject: [c_char; 256],
    pub subject_kind: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_registry_state_t {
    ZLINK_REGISTRY_STATE_IDLE = 1,
    ZLINK_REGISTRY_STATE_ACTIVE = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR = 4,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_registry_status_t {
    pub registry_id: u32,
    pub bind_endpoint: [c_char; 256],
    pub state: zlink_registry_state_t,
    pub topology_entry_count: u32,
    pub peer_registry_count: u32,
    pub connected_peer_registry_count: u32,
    pub list_seq: u64,
    pub last_error: i32,
    pub last_changed_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_registry_service_summary_entry_t {
    pub service_kind: zlink_service_kind_t,
    pub service_role: zlink_service_role_t,
    pub service_name: [c_char; 256],
    pub total_count: u32,
    pub connecting_count: u32,
    pub ready_count: u32,
    pub error_count: u32,
    pub stopped_count: u32,
    pub last_reported_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_registry_service_summary_filter_t {
    pub service_kind: zlink_service_kind_t,
    pub service_role: zlink_service_role_t,
    pub service_name: [c_char; 256],
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_member_peer_entry_t {
    pub service_type: zlink_service_type_t,
    pub service_role: u16,
    pub service_name: [c_char; 256],
    pub endpoint: [c_char; 256],
    pub routing_id: zlink_routing_id_t,
    pub value: i64,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_topology_source_t {
    ZLINK_TOPOLOGY_SOURCE_MANUAL = 1,
    ZLINK_TOPOLOGY_SOURCE_DISCOVERY = 2,
    ZLINK_TOPOLOGY_SOURCE_REGISTRY = 3,
}

#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum zlink_topology_state_t {
    ZLINK_TOPOLOGY_STATE_DISCOVERED = 1,
    ZLINK_TOPOLOGY_STATE_CONNECTING = 2,
    ZLINK_TOPOLOGY_STATE_READY = 3,
    ZLINK_TOPOLOGY_STATE_LOST = 4,
    ZLINK_TOPOLOGY_STATE_ERROR = 5,
    ZLINK_TOPOLOGY_STATE_STOPPED = 6,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_registry_topology_entry_t {
    pub routing_id: zlink_routing_id_t,
    pub service_kind: zlink_service_kind_t,
    pub service_role: zlink_service_role_t,
    pub service_name: [c_char; 256],
    pub endpoint: [c_char; 256],
    pub source: zlink_topology_source_t,
    pub state: zlink_topology_state_t,
    pub desired_count: u32,
    pub ready_count: u32,
    pub error_code: u32,
    pub last_reported_ms: u64,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_registry_topology_filter_t {
    pub service_kind: zlink_service_kind_t,
    pub service_role: zlink_service_role_t,
    pub service_name: [c_char; 256],
    pub routing_id: zlink_routing_id_t,
    pub state: zlink_topology_state_t,
    pub source: zlink_topology_source_t,
}

// ---------------------------------------------------------------------------
// Poller types
// ---------------------------------------------------------------------------

#[cfg(all(target_os = "windows", target_pointer_width = "64"))]
pub type zlink_fd_t = u64;
#[cfg(all(target_os = "windows", target_pointer_width = "32"))]
pub type zlink_fd_t = c_uint;
#[cfg(not(target_os = "windows"))]
pub type zlink_fd_t = c_int;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_pollitem_t {
    pub socket: *mut c_void,
    pub fd: zlink_fd_t,
    pub events: i16,
    pub revents: i16,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct zlink_poller_event_t {
    pub socket: *mut c_void,
    pub fd: zlink_fd_t,
    pub user_data: *mut c_void,
    pub events: i16,
}

pub const ZLINK_POLLIN: i16 = 1;
pub const ZLINK_POLLOUT: i16 = 2;
pub const ZLINK_POLLERR: i16 = 4;

// ---------------------------------------------------------------------------
// Extern "C" functions
// ---------------------------------------------------------------------------

unsafe extern "C" {
    // -- Error / version ---------------------------------------------------
    pub fn zlink_errno() -> c_int;
    pub fn zlink_strerror(errnum: c_int) -> *const c_char;
    pub fn zlink_version(major: *mut c_int, minor: *mut c_int, patch: *mut c_int);

    // -- Context -----------------------------------------------------------
    pub fn zlink_ctx_new() -> *mut c_void;
    pub fn zlink_ctx_term(ctx: *mut c_void) -> c_int;
    pub fn zlink_ctx_shutdown(ctx: *mut c_void) -> c_int;
    pub fn zlink_ctx_set(ctx: *mut c_void, option: zlink_ctx_option_t, optval: c_int) -> c_int;
    pub fn zlink_ctx_get(ctx: *mut c_void, option: zlink_ctx_option_t) -> c_int;

    // -- Message -----------------------------------------------------------
    pub fn zlink_msg_init(msg: *mut zlink_msg_t) -> c_int;
    pub fn zlink_msg_init_size(msg: *mut zlink_msg_t, size: usize) -> c_int;
    pub fn zlink_msg_init_data(
        msg: *mut zlink_msg_t,
        data: *mut c_void,
        size: usize,
        ffn: Option<zlink_free_fn>,
        hint: *mut c_void,
    ) -> c_int;
    pub fn zlink_msg_close(msg: *mut zlink_msg_t) -> c_int;
    pub fn zlink_msg_move(dest: *mut zlink_msg_t, src: *mut zlink_msg_t) -> c_int;
    pub fn zlink_msg_copy(dest: *mut zlink_msg_t, src: *mut zlink_msg_t) -> c_int;
    pub fn zlink_msg_data(msg: *mut zlink_msg_t) -> *mut c_void;
    pub fn zlink_msg_size(msg: *const zlink_msg_t) -> usize;
    pub fn zlink_msg_refcnt(msg: *const zlink_msg_t) -> c_int;
    pub fn zlink_msg_gets(msg: *const zlink_msg_t, property: *const c_char) -> *const c_char;

    // -- Socket create / close ---------------------------------------------
    pub fn zlink_socket(ctx: *mut c_void, typ: zlink_socket_type_t) -> *mut c_void;
    pub fn zlink_close(socket: *mut c_void) -> c_int;

    // -- Callback handlers -------------------------------------------------
    pub fn zlink_recv_handler(
        socket: *mut c_void,
        handler: zlink_socket_msg_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_subscribe_handler(
        socket: *mut c_void,
        handler: zlink_subscribe_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_send_ready_handler(
        socket: *mut c_void,
        handler: zlink_send_ready_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;

    // -- Socket options ----------------------------------------------------
    pub fn zlink_set_option(
        handle: *mut c_void,
        option: zlink_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_get_option(
        handle: *mut c_void,
        option: zlink_option_t,
        optval: *mut c_void,
        optvallen: *mut usize,
    ) -> c_int;
    pub fn zlink_set_routing_id(handle: *mut c_void, data: *const c_void, size: usize) -> c_int;
    pub fn zlink_get_routing_id(handle: *mut c_void, out: *mut zlink_routing_id_t) -> c_int;
    pub fn zlink_set_tls_server(
        handle: *mut c_void,
        cert: *const c_char,
        key: *const c_char,
        require_client_cert: c_int,
    ) -> c_int;
    pub fn zlink_set_tls_client(
        handle: *mut c_void,
        ca_cert: *const c_char,
        hostname: *const c_char,
        trust_system: c_int,
    ) -> c_int;
    pub fn zlink_set_router_option(
        handle: *mut c_void,
        option: zlink_router_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_get_router_option(
        handle: *mut c_void,
        option: zlink_router_option_t,
        optval: *mut c_void,
        optvallen: *mut usize,
    ) -> c_int;
    pub fn zlink_set_dealer_option(
        handle: *mut c_void,
        option: zlink_dealer_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_set_pub_option(
        handle: *mut c_void,
        option: zlink_pub_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_get_pub_option(
        handle: *mut c_void,
        option: zlink_pub_option_t,
        optval: *mut c_void,
        optvallen: *mut usize,
    ) -> c_int;
    pub fn zlink_set_sub_option(
        handle: *mut c_void,
        option: zlink_sub_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_get_sub_option(
        handle: *mut c_void,
        option: zlink_sub_option_t,
        optval: *mut c_void,
        optvallen: *mut usize,
    ) -> c_int;
    pub fn zlink_set_stream_option(
        handle: *mut c_void,
        option: zlink_stream_option_t,
        optval: *const c_void,
        optvallen: usize,
    ) -> c_int;
    pub fn zlink_get_stream_option(
        handle: *mut c_void,
        option: zlink_stream_option_t,
        optval: *mut c_void,
        optvallen: *mut usize,
    ) -> c_int;

    // -- Bind / connect ----------------------------------------------------
    pub fn zlink_bind(socket: *mut c_void, addr: *const c_char) -> c_int;
    pub fn zlink_connect(socket: *mut c_void, addr: *const c_char) -> c_int;
    pub fn zlink_unbind(socket: *mut c_void, addr: *const c_char) -> c_int;
    pub fn zlink_disconnect(socket: *mut c_void, addr: *const c_char) -> c_int;
    pub fn zlink_socket_attach_discovery(socket: *mut c_void, discovery: *mut c_void) -> c_int;

    // -- Send / recv -------------------------------------------------------
    pub fn zlink_send(
        socket: *mut c_void,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_send_rid(
        socket: *mut c_void,
        target_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_dealer_request(
        dealer: *mut c_void,
        parts: *mut zlink_msg_t,
        part_count: usize,
        handler: zlink_reply_handler_fn,
        userdata: *mut c_void,
        flags: zlink_send_flags_t,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_router_request(
        router: *mut c_void,
        peer_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        handler: zlink_reply_handler_fn,
        userdata: *mut c_void,
        flags: zlink_send_flags_t,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_router_reply(
        router: *mut c_void,
        peer_rid: *const zlink_routing_id_t,
        request_seq: u64,
        parts: *mut zlink_msg_t,
        part_count: usize,
    ) -> c_int;
    pub fn zlink_router_handler(
        router: *mut c_void,
        handler: zlink_router_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_router_recv(
        router: *mut c_void,
        source_node_rid_out: *mut *const zlink_routing_id_t,
        source_spot_rid_out: *mut *const zlink_routing_id_t,
        request_seq_out: *mut u64,
        parts_out: *mut *mut zlink_msg_t,
        part_count_out: *mut usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_router_request_spot(
        router: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        handler: zlink_reply_handler_fn,
        userdata: *mut c_void,
        flags: zlink_send_flags_t,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_router_reply_spot(
        router: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        request_seq: u64,
        parts: *mut zlink_msg_t,
        part_count: usize,
    ) -> c_int;
    pub fn zlink_router_send_spot(
        router: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_recv(
        socket: *mut c_void,
        source_rid_out: *mut zlink_routing_id_t,
        parts_out: *mut *mut zlink_msg_t,
        part_count_out: *mut usize,
        flags: zlink_send_flags_t,
    ) -> c_int;

    // -- Publish / subscribe -----------------------------------------------
    pub fn zlink_publish(
        subject: *mut c_void,
        topic_id: *const c_char,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_set_subscription(handle: *mut c_void, filter: *const c_char) -> c_int;
    pub fn zlink_unset_subscription(handle: *mut c_void, filter: *const c_char) -> c_int;
    pub fn zlink_subscription_at(
        handle: *mut c_void,
        index: usize,
        filter_out: *mut c_char,
        filter_len_inout: *mut usize,
        is_pattern_out: *mut c_int,
    ) -> c_int;
    pub fn zlink_subscribe(
        subject: *mut c_void,
        source_rid_out: *mut zlink_routing_id_t,
        parts_out: *mut *mut zlink_msg_t,
        part_count_out: *mut usize,
        topic_id_out: *mut c_char,
        topic_id_len_out: *mut usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_subscription_event(
        subject: *mut c_void,
        source_rid_out: *mut zlink_routing_id_t,
        subscribed_out: *mut c_int,
        topic_id_out: *mut c_char,
        topic_id_len_out: *mut usize,
        flags: zlink_send_flags_t,
    ) -> c_int;

    // -- Multipart close ---------------------------------------------------
    pub fn zlink_multipart_close(parts: *mut zlink_msg_t, part_count: usize);

    // -- Socket monitor ----------------------------------------------------
    pub fn zlink_socket_monitor_open(
        socket: *mut c_void,
        options: *const zlink_socket_monitor_open_options_t,
    ) -> *mut c_void;
    pub fn zlink_socket_monitor_handler(
        monitor: *mut c_void,
        handler: zlink_socket_monitor_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_socket_monitor_recv(
        monitor: *mut c_void,
        out: *mut zlink_socket_monitor_event_t,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_monitor_snapshot(
        monitor: *mut c_void,
        out: *mut zlink_monitor_snapshot_t,
    ) -> c_int;
    pub fn zlink_monitor_close(monitor_p: *mut *mut c_void) -> c_int;
    pub fn zlink_monitor_ignore_handler(event: *const zlink_monitor_event_t, userdata: *mut c_void);

    // -- Service monitor ---------------------------------------------------
    pub fn zlink_service_monitor_open(
        target: *mut c_void,
        options: *const zlink_service_monitor_open_options_t,
    ) -> *mut c_void;
    pub fn zlink_service_monitor_handler(
        monitor: *mut c_void,
        handler: zlink_service_monitor_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_service_monitor_recv(
        monitor: *mut c_void,
        out: *mut zlink_service_monitor_event_t,
        flags: zlink_send_flags_t,
    ) -> c_int;

    // -- Registry ----------------------------------------------------------
    pub fn zlink_registry_new(ctx: *mut c_void) -> *mut c_void;
    pub fn zlink_registry_bind(
        registry: *mut c_void,
        pub_endpoint: *const c_char,
        router_endpoint: *const c_char,
    ) -> c_int;
    pub fn zlink_registry_set_id(registry: *mut c_void, id: u32) -> c_int;
    pub fn zlink_registry_add_peer(
        registry: *mut c_void,
        peer_pub_endpoint: *const c_char,
    ) -> c_int;
    pub fn zlink_registry_set_heartbeat(
        registry: *mut c_void,
        interval_ms: u32,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_registry_set_broadcast_interval(registry: *mut c_void, interval_ms: u32) -> c_int;
    pub fn zlink_registry_destroy(registry_p: *mut *mut c_void) -> c_int;

    // -- Discovery ---------------------------------------------------------
    pub fn zlink_discovery_new(
        ctx: *mut c_void,
        service_type: zlink_service_type_t,
        service_name: *const c_char,
    ) -> *mut c_void;
    pub fn zlink_discovery_connect_registry(
        discovery: *mut c_void,
        registry_endpoint: *const c_char,
    ) -> c_int;
    pub fn zlink_discovery_set_value(discovery: *mut c_void, value: i64) -> c_int;
    pub fn zlink_discovery_get_value(discovery: *mut c_void, value_out: *mut i64) -> c_int;
    pub fn zlink_discovery_set_metadata(
        discovery: *mut c_void,
        data: *const c_void,
        size: usize,
    ) -> c_int;
    pub fn zlink_discovery_get_metadata(
        discovery: *mut c_void,
        metadata_out: *mut zlink_msg_t,
    ) -> c_int;
    pub fn zlink_discovery_resolve_spot(
        discovery: *mut c_void,
        spot_rid: *const zlink_routing_id_t,
        owner_node_rid_out: *mut zlink_routing_id_t,
    ) -> c_int;
    pub fn zlink_discovery_set_dealer_peer_mode(discovery: *mut c_void, mode: u32) -> c_int;
    pub fn zlink_discovery_destroy(discovery_p: *mut *mut c_void) -> c_int;

    // -- Spot --------------------------------------------------------------
    pub fn zlink_spot_new(node: *mut c_void) -> *mut c_void;
    pub fn zlink_spot_destroy(spot_p: *mut *mut c_void) -> c_int;
    pub fn zlink_spot_node_new(ctx: *mut c_void) -> *mut c_void;
    pub fn zlink_spot_node_destroy(node_p: *mut *mut c_void) -> c_int;
    pub fn zlink_spot_node_bind(node: *mut c_void, endpoint: *const c_char) -> c_int;
    pub fn zlink_spot_node_connect_peer(node: *mut c_void, peer_endpoint: *const c_char) -> c_int;
    pub fn zlink_spot_node_disconnect_peer(
        node: *mut c_void,
        peer_endpoint: *const c_char,
    ) -> c_int;
    pub fn zlink_spot_node_attach_discovery(node: *mut c_void, discovery: *mut c_void) -> c_int;
    pub fn zlink_spot_send_spot(
        spot: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_spot_request_spot(
        spot: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        handler: zlink_reply_handler_fn,
        userdata: *mut c_void,
        flags: zlink_send_flags_t,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_spot_reply_spot(
        spot: *mut c_void,
        dest_node_rid: *const zlink_routing_id_t,
        dest_spot_rid: *const zlink_routing_id_t,
        request_seq: u64,
        parts: *mut zlink_msg_t,
        part_count: usize,
    ) -> c_int;
    pub fn zlink_spot_send_router(
        spot: *mut c_void,
        peer_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        flags: zlink_send_flags_t,
    ) -> c_int;
    pub fn zlink_spot_request_router(
        spot: *mut c_void,
        peer_rid: *const zlink_routing_id_t,
        parts: *mut zlink_msg_t,
        part_count: usize,
        handler: zlink_reply_handler_fn,
        userdata: *mut c_void,
        flags: zlink_send_flags_t,
        timeout_ms: u32,
    ) -> c_int;
    pub fn zlink_spot_reply_router(
        spot: *mut c_void,
        peer_rid: *const zlink_routing_id_t,
        request_seq: u64,
        parts: *mut zlink_msg_t,
        part_count: usize,
    ) -> c_int;
    pub fn zlink_spot_handler(
        spot: *mut c_void,
        handler: zlink_spot_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_spot_dispatch_event_handler(
        spot: *mut c_void,
        handler: zlink_spot_dispatch_event_handler_fn,
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_spot_recv(
        spot: *mut c_void,
        source_rid_out: *mut *const zlink_routing_id_t,
        spot_rid_out: *mut *const zlink_routing_id_t,
        request_seq_out: *mut u64,
        parts_out: *mut *mut zlink_msg_t,
        part_count_out: *mut usize,
        flags: zlink_send_flags_t,
    ) -> c_int;

    // -- Spot / registry snapshot -------------------------------------------
    pub fn zlink_spot_node_status_snapshot(
        node: *mut c_void,
        out: *mut zlink_spot_node_status_t,
    ) -> c_int;
    pub fn zlink_spot_node_peers_snapshot(
        node: *mut c_void,
        entries: *mut zlink_spot_node_peer_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_spot_node_peers_query(
        node: *mut c_void,
        filter: *const zlink_spot_node_peer_filter_t,
        entries: *mut zlink_spot_node_peer_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_spot_node_subjects_snapshot(
        node: *mut c_void,
        filter: *const zlink_spot_node_subject_filter_t,
        entries: *mut zlink_spot_node_subject_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_status_snapshot(
        registry: *mut c_void,
        out: *mut zlink_registry_status_t,
    ) -> c_int;
    pub fn zlink_registry_service_summary_snapshot(
        registry: *mut c_void,
        filter: *const zlink_registry_service_summary_filter_t,
        entries: *mut zlink_registry_service_summary_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_member_peers(
        registry: *mut c_void,
        service_type: zlink_service_type_t,
        service_name: *const c_char,
        entries: *mut zlink_member_peer_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_member_peer_metadata(
        registry: *mut c_void,
        service_type: zlink_service_type_t,
        service_name: *const c_char,
        service_role: u16,
        endpoint: *const c_char,
        metadata_out: *mut zlink_msg_t,
    ) -> c_int;
    pub fn zlink_discovery_member_peers(
        discovery: *mut c_void,
        entries: *mut zlink_member_peer_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_discovery_member_peer_metadata(
        discovery: *mut c_void,
        service_role: u16,
        endpoint: *const c_char,
        metadata_out: *mut zlink_msg_t,
    ) -> c_int;
    pub fn zlink_registry_topology_snapshot(
        registry: *mut c_void,
        entries: *mut zlink_registry_topology_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_topology_query(
        registry: *mut c_void,
        filter: *const zlink_registry_topology_filter_t,
        entries: *mut zlink_registry_topology_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_query_client_new(ctx: *mut c_void) -> *mut c_void;
    pub fn zlink_registry_query_client_connect(
        client: *mut c_void,
        endpoint: *const c_char,
    ) -> c_int;
    pub fn zlink_registry_query_snapshot(
        client: *mut c_void,
        filter: *const zlink_registry_topology_filter_t,
        entries: *mut zlink_registry_topology_entry_t,
        count: *mut usize,
    ) -> c_int;
    pub fn zlink_registry_query_destroy(client_p: *mut *mut c_void) -> c_int;

    // -- Poller ------------------------------------------------------------
    pub fn zlink_poll(items: *mut zlink_pollitem_t, nitems: c_int, timeout: c_long) -> c_int;
    pub fn zlink_poller_new() -> *mut c_void;
    pub fn zlink_poller_destroy(poller_p: *mut *mut c_void) -> c_int;
    pub fn zlink_poller_size(poller: *mut c_void, error_out: *mut c_int) -> c_int;
    pub fn zlink_poller_add(
        poller: *mut c_void,
        socket: *mut c_void,
        user_data: *mut c_void,
        events: i16,
    ) -> c_int;
    pub fn zlink_poller_modify(poller: *mut c_void, socket: *mut c_void, events: i16) -> c_int;
    pub fn zlink_poller_remove(poller: *mut c_void, socket: *mut c_void) -> c_int;
    pub fn zlink_poller_add_fd(
        poller: *mut c_void,
        fd: zlink_fd_t,
        user_data: *mut c_void,
        events: i16,
    ) -> c_int;
    pub fn zlink_poller_modify_fd(poller: *mut c_void, fd: zlink_fd_t, events: i16) -> c_int;
    pub fn zlink_poller_remove_fd(poller: *mut c_void, fd: zlink_fd_t) -> c_int;
    pub fn zlink_poller_wait(
        poller: *mut c_void,
        event: *mut zlink_poller_event_t,
        timeout: c_long,
    ) -> c_int;
    pub fn zlink_poller_wait_all(
        poller: *mut c_void,
        events: *mut zlink_poller_event_t,
        n_events: c_int,
        timeout: c_long,
    ) -> c_int;
    pub fn zlink_timer_new() -> *mut c_void;
    pub fn zlink_timer_destroy(timer_p: *mut *mut c_void) -> c_int;
    pub fn zlink_timer_start(timer: *mut c_void, interval_ns: u64, repeat_count: u64) -> c_int;
    pub fn zlink_timer_stop(timer: *mut c_void) -> c_int;
    pub fn zlink_timer_recv(timer: *mut c_void, out: *mut u64, flags: c_int) -> c_int;
    pub fn zlink_timer_handler(
        timer: *mut c_void,
        handler: unsafe extern "C" fn(timer: *mut c_void, count: u64, userdata: *mut c_void),
        userdata: *mut c_void,
    ) -> c_int;
    pub fn zlink_stopwatch_start() -> *mut c_void;
    pub fn zlink_stopwatch_intermediate(watch: *mut c_void) -> c_ulong;
    pub fn zlink_stopwatch_stop(watch: *mut c_void) -> c_ulong;

    // -- Proxy -------------------------------------------------------------
    pub fn zlink_proxy(frontend: *mut c_void, backend: *mut c_void, capture: *mut c_void) -> c_int;
    pub fn zlink_proxy_steerable(
        frontend: *mut c_void,
        backend: *mut c_void,
        capture: *mut c_void,
        control: *mut c_void,
    ) -> c_int;

    // -- Capability --------------------------------------------------------
    pub fn zlink_has(capability: *const c_char) -> c_int;
    pub fn zlink_sleep(seconds: c_int);
}
