# SPDX-License-Identifier: MPL-2.0

from enum import IntEnum, IntFlag


class SocketType(IntEnum):
    PAIR = 0x1001
    PUB = 0x1002
    SUB = 0x1003
    DEALER = 0x1004
    ROUTER = 0x1005
    XPUB = 0x1006
    XSUB = 0x1007
    STREAM = 0x1008


class ContextOption(IntEnum):
    IO_THREADS = 1
    MAX_SOCKETS = 2
    SOCKET_LIMIT = 3
    THREAD_PRIORITY = 3
    THREAD_SCHED_POLICY = 4
    MAX_MSGSZ = 5
    MSG_T_SIZE = 6
    THREAD_AFFINITY_CPU_ADD = 7
    THREAD_AFFINITY_CPU_REMOVE = 8
    THREAD_NAME_PREFIX = 9
    CTX_OPT_BLOCKY = 10
    SPOT_WORKER_THREADS = 11
    AUTO_HWM_ENABLE = 12
    AUTO_HWM_TOTAL_MEMORY_BUDGET_MB = 13


class SocketOption(IntEnum):
    AFFINITY = 0x3001
    RATE = 0x3003
    RECOVERY_IVL = 0x3004
    SNDBUF = 0x3005
    RCVBUF = 0x3006
    FD = 0x3007
    EVENTS = 0x3008
    TYPE = 0x3009
    LINGER = 0x300A
    RECONNECT_IVL = 0x300B
    BACKLOG = 0x300C
    RECONNECT_IVL_MAX = 0x300D
    MAXMSGSIZE = 0x300E
    SNDHWM = 0x300F
    RCVHWM = 0x3010
    MULTICAST_HOPS = 0x3011
    RCVTIMEO = 0x3012
    SNDTIMEO = 0x3013
    LAST_ENDPOINT = 0x3014
    TCP_KEEPALIVE = 0x3015
    TCP_KEEPALIVE_CNT = 0x3016
    TCP_KEEPALIVE_IDLE = 0x3017
    TCP_KEEPALIVE_INTVL = 0x3018
    IMMEDIATE = 0x3019
    IPV6 = 0x301A
    CONFLATE = 0x301B
    TOS = 0x301C
    HANDSHAKE_IVL = 0x301D
    BLOCKY = 0x301E
    INVERT_MATCHING = 0x3020
    HEARTBEAT_IVL = 0x3021
    HEARTBEAT_TTL = 0x3022
    HEARTBEAT_TIMEOUT = 0x3023
    CONNECT_TIMEOUT = 0x3024
    TCP_MAXRT = 0x3025
    MULTICAST_MAXTPDU = 0x3026
    BINDTODEVICE = 0x3027
    TLS_CERT = 0x3028
    TLS_KEY = 0x3029
    TLS_CA = 0x302A
    TLS_VERIFY = 0x302B
    TLS_REQUIRE_CLIENT_CERT = 0x302C
    TLS_HOSTNAME = 0x302D
    TLS_TRUST_SYSTEM = 0x302E
    TLS_PASSWORD = 0x302F
    ZMP_METADATA = 0x3030
    DISCOVERY_METADATA_MAX_SIZE = 0x3032
    TCP_NODELAY = 0x3031

    ROUTING_ID = 5
    SUBSCRIBE = 6
    UNSUBSCRIBE = 7


class SendFlags(IntEnum):
    NONE = 0
    DONT_WAIT = 1


class RecvFlags(IntEnum):
    NONE = 0
    DONT_WAIT = 1


class SubmitResult(IntEnum):
    OK = 0
    BACKPRESSURED = 1
    NOT_CONNECTED = 2
    NOT_FOUND = 3
    TERMINATED = 4
    INVALID_HANDLE = 5
    INVALID_ARGUMENT = 6
    NOT_SUPPORTED = 7
    INVALID_STATE = 8
    THREAD_VIOLATION = 9
    OUT_OF_MEMORY = 10
    SEQ_EXHAUSTED = 11
    INTERNAL_ERROR = 12
    NOT_ADMITTED = 13


class RequestResult(IntEnum):
    OK = 0
    TIMED_OUT = 101
    NOT_FOUND = 102
    TERMINATED = 103
    PROTOCOL_ERROR = 104


class RecvResult(IntEnum):
    OK = 0
    NO_DATA = 201
    BUSY = 202
    TERMINATED = 203
    INVALID_HANDLE = 204
    NOT_SUPPORTED = 205


class HandlerResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 301
    BUSY = 302
    NOT_SUPPORTED = 303
    DEADLOCK = 304
    INVALID_HANDLE = 305


class CloseResult(IntEnum):
    OK = 0
    BUSY = 401
    SHUTDOWN = 402
    INVALID_HANDLE = 403


class BindResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 501
    ADDR_IN_USE = 502
    NOT_SUPPORTED = 503
    INVALID_HANDLE = 504


class ConnectResult(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 601
    NOT_SUPPORTED = 602
    INVALID_HANDLE = 603


class ConfigResult(IntEnum):
    OK = 0
    INVALID_HANDLE = 701
    INVALID_ARGUMENT = 702
    NOT_SUPPORTED = 703


class SendResult(IntEnum):
    SENT = 0
    BACKPRESSURED = 1
    NOT_READY = 2


class RouterOption(IntEnum):
    MANDATORY = 0x3101
    HANDOVER = 0x3102
    PROBE = 0x3103
    CONNECT_ROUTING_ID = 0x3104
    REQUEST_TIMEOUT_MS = 0x3105
    WEIGHT = 0x3106


class ErrorCode(IntEnum):
    EFSM = 156384763
    ENOCOMPATPROTO = 156384764
    ETERM = 156384765
    EMTHREAD = 156384766


class ProtocolError(IntEnum):
    ZMP_MALFORMED_COMMAND_HELLO = 0x10000013


class MonitorEventMask(IntFlag):
    CONNECTED = 0x0001
    CONNECT_DELAYED = 0x0002
    CONNECT_RETRIED = 0x0004
    LISTENING = 0x0008
    BIND_FAILED = 0x0010
    ACCEPTED = 0x0020
    ACCEPT_FAILED = 0x0040
    CLOSED = 0x0080
    CLOSE_FAILED = 0x0100
    DISCONNECTED = 0x0200
    MONITOR_STOPPED = 0x0400
    HANDSHAKE_FAILED_NO_DETAIL = 0x0800
    CONNECTION_READY = 0x1000
    HANDSHAKE_FAILED_PROTOCOL = 0x2000
    HANDSHAKE_FAILED_AUTH = 0x4000
    PEER_WEIGHT_CHANGED = 0x8000
    ALL = 0xFFFF


# Legacy alias. Public surface should prefer MonitorEventMask for masks and
# zlink.MonitorEvent for the decoded monitor event object.
MonitorEvent = MonitorEventMask


class DisconnectReason(IntEnum):
    UNKNOWN = 0
    HANDSHAKE_FAILED = 3
    TRANSPORT_ERROR = 4
    CTX_TERM = 5


class PollEvent(IntFlag):
    POLLIN = 1
    POLLOUT = 2
    POLLERR = 4
    POLLPRI = 8


class ServiceType(IntEnum):
    SPOT = 0x3002
    SOCKET = 0x3003


class ServiceRole(IntEnum):
    INVALID = 0
    SPOT = 2
    ROUTER = 3
    DEALER = 4
    PUB = 5
    SUB = 6


class ServiceMonitorMask(IntFlag):
    ERROR = 1 << 4
    DISCOVERY_SERVICE_UP = 1 << 5
    DISCOVERY_SERVICE_DOWN = 1 << 6
    DISCOVERY_PROVIDERS_CHANGED = 1 << 7
    PEER_WEIGHT_CHANGED = 1 << 8
    CLOSED = 1 << 17
    ALL = (
        ERROR
        | DISCOVERY_SERVICE_UP
        | DISCOVERY_SERVICE_DOWN
        | DISCOVERY_PROVIDERS_CHANGED
        | PEER_WEIGHT_CHANGED
        | CLOSED
    )


class RegistrySocketRole(IntEnum):
    PUB = 1
    ROUTER = 2
    PEER_SUB = 3


class DiscoverySocketRole(IntEnum):
    SUB = 1


class DiscoveryDealerPeerMode(IntEnum):
    ROUTER = 1
    DEALER = 2


class SpotNodeSocketRole(IntEnum):
    NODE = 0
    PUB = 1
    SUB = 2
    DEALER = 3


class SpotNodeOption(IntEnum):
    PUB_MODE = 1
    PUB_QUEUE_HWM = 2
    PUB_QUEUE_FULL_POLICY = 3
    TOPIC_SEND_HWM = 0x3608
    TOPIC_RECV_HWM = 0x3609
    ROUTED_SEND_HWM = 0x360A
    ROUTED_RECV_HWM = 0x360B
    WEIGHT = 0x360C


class SpotNodePubMode(IntEnum):
    SYNC = 0
    ASYNC = 1


class SpotNodePubQueueFullPolicy(IntEnum):
    EAGAIN = 0
    DROP = 1


class SpotNodeState(IntEnum):
    IDLE = 1
    CONNECTING = 2
    PARTIAL_READY = 3
    READY = 4
    ERROR = 5


class SpotPeerSource(IntEnum):
    MANUAL = 1
    DISCOVERY = 2
    MIXED = 3


class SpotPeerState(IntEnum):
    CONFIGURED = 1
    CONNECTING = 2
    CONNECTED = 3


class SpotSocketRole(IntEnum):
    PUB = 1
    SUB = 2


class SpotServiceAttachmentRole(IntEnum):
    ROUTER = 1
    PUB = 2
    SUB = 3


class SpotDispatchEvent(IntEnum):
    SUBSCRIBE_READABLE = 1
    ROUTED_READABLE = 2
    TIMER_READABLE = 3
    CHANNEL_REPLY_READABLE = 4


class SpotDispatchSubjectKind(IntEnum):
    SPOT = 1
    TIMER = 2
    CHANNEL_DEALER = 3


class RegistryState(IntEnum):
    IDLE = 1
    ACTIVE = 2
    DEGRADED = 3
    ERROR = 4


class TopologySource(IntEnum):
    MANUAL = 1
    DISCOVERY = 2
    REGISTRY = 3


class TopologyState(IntEnum):
    DISCOVERED = 1
    CONNECTING = 2
    READY = 3
    LOST = 4
    ERROR = 5
    STOPPED = 6
