# SPDX-License-Identifier: MPL-2.0

from enum import IntEnum, IntFlag


class SocketType(IntEnum):
    PAIR = 0
    PUB = 1
    SUB = 2
    DEALER = 5
    ROUTER = 6
    XPUB = 9
    XSUB = 10
    STREAM = 11


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
    AFFINITY = 4
    ROUTING_ID = 5
    SUBSCRIBE = 6
    UNSUBSCRIBE = 7
    RATE = 8
    RECOVERY_IVL = 9
    SNDBUF = 11
    RCVBUF = 12
    RCVMORE = 13
    FD = 14
    EVENTS = 15
    TYPE = 16
    LINGER = 17
    RECONNECT_IVL = 18
    BACKLOG = 19
    RECONNECT_IVL_MAX = 21
    MAXMSGSIZE = 22
    SNDHWM = 23
    RCVHWM = 24
    MULTICAST_HOPS = 25
    RCVTIMEO = 27
    SNDTIMEO = 28
    LAST_ENDPOINT = 32
    ROUTER_MANDATORY = 33
    TCP_KEEPALIVE = 34
    TCP_KEEPALIVE_CNT = 35
    TCP_KEEPALIVE_IDLE = 36
    TCP_KEEPALIVE_INTVL = 37
    IMMEDIATE = 39
    XPUB_VERBOSE = 40
    IPV6 = 42
    PROBE_ROUTER = 51
    CONFLATE = 54
    ROUTER_HANDOVER = 56
    TOS = 57
    CONNECT_ROUTING_ID = 61
    HANDSHAKE_IVL = 66
    XPUB_NODROP = 69
    BLOCKY = 70
    XPUB_MANUAL = 71
    XPUB_WELCOME_MSG = 72
    INVERT_MATCHING = 74
    HEARTBEAT_IVL = 75
    HEARTBEAT_TTL = 76
    HEARTBEAT_TIMEOUT = 77
    XPUB_VERBOSER = 78
    CONNECT_TIMEOUT = 79
    TCP_MAXRT = 80
    MULTICAST_MAXTPDU = 84
    USE_FD = 89
    REQUEST_TIMEOUT = 90
    REQUEST_CORRELATE = 91
    BINDTODEVICE = 92
    TLS_CERT = 95
    TLS_KEY = 96
    TLS_CA = 97
    TLS_VERIFY = 98
    TLS_REQUIRE_CLIENT_CERT = 99
    TLS_HOSTNAME = 100
    TLS_TRUST_SYSTEM = 101
    TLS_PASSWORD = 102
    XPUB_MANUAL_LAST_VALUE = 98
    ONLY_FIRST_SUBSCRIBE = 108
    TOPICS_COUNT = 116
    ZMP_METADATA = 117


class SendFlag(IntFlag):
    NONE = 0
    DONTWAIT = 1
    SNDMORE = 2


class ReceiveFlag(IntFlag):
    NONE = 0
    DONTWAIT = 1


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
    LOCAL = 1
    REMOTE = 2
    HANDSHAKE_FAILED = 3
    TRANSPORT_ERROR = 4
    CTX_TERM = 5


class PollEvent(IntFlag):
    POLLIN = 1
    POLLOUT = 2
    POLLERR = 4
    POLLPRI = 8


class ServiceType(IntEnum):
    GATEWAY = 1
    SPOT = 2


class GatewayLbStrategy(IntEnum):
    ROUND_ROBIN = 0
    WEIGHTED = 1


class SpotTopicMode(IntEnum):
    QUEUE = 0
    RINGBUFFER = 1


class RegistrySocketRole(IntEnum):
    PUB = 1
    ROUTER = 2
    PEER_SUB = 3


class DiscoverySocketRole(IntEnum):
    SUB = 1


class GatewaySocketRole(IntEnum):
    ROUTER = 1


class ReceiverSocketRole(IntEnum):
    ROUTER = 1
    DEALER = 2


class SpotNodeSocketRole(IntEnum):
    PUB = 1
    SUB = 2
    DEALER = 3


class SpotSocketRole(IntEnum):
    PUB = 1
    SUB = 2
