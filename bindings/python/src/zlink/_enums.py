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

    # Legacy compatibility aliases. Canonical surface uses dedicated helpers.
    ROUTING_ID = 5
    SUBSCRIBE = 6
    UNSUBSCRIBE = 7


class SendFlag(IntFlag):
    NONE = 0
    DONTWAIT = 1
    SNDMORE = 2


class ReceiveFlag(IntFlag):
    NONE = 0
    DONTWAIT = 1


class StreamDispatchMode(IntFlag):
    NONE = 0
    LEN32BE = 0x0001


class ErrorCode(IntEnum):
    EFSM = 156384763
    ENOCOMPATPROTO = 156384764
    ETERM = 156384765
    EMTHREAD = 156384766


class ProtocolError(IntEnum):
    ZMP_MALFORMED_COMMAND_HELLO = 0x10000013


class MonitorEvent(IntFlag):
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
    ALL = 0xFFFF


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


class RegistrySocketRole(IntEnum):
    PUB = 1
    ROUTER = 2
    PEER_SUB = 3


class DiscoverySocketRole(IntEnum):
    SUB = 1


class SpotNodeSocketRole(IntEnum):
    NODE = 0
    PUB = 1
    SUB = 2
    DEALER = 3


class SpotNodeOption(IntEnum):
    PUB_MODE = 1
    PUB_QUEUE_HWM = 2
    PUB_QUEUE_FULL_POLICY = 3


class SpotNodePubMode(IntEnum):
    SYNC = 0
    ASYNC = 1


class SpotNodePubQueueFullPolicy(IntEnum):
    EAGAIN = 0
    DROP = 1


class SpotSocketRole(IntEnum):
    PUB = 1
    SUB = 2
