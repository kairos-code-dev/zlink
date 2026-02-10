// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

public enum SocketType
{
    Pair = 0,
    Pub = 1,
    Sub = 2,
    Dealer = 5,
    Router = 6,
    XPub = 9,
    XSub = 10,
    Stream = 11
}

public enum ContextOption
{
    IoThreads = 1,
    MaxSockets = 2,
    SocketLimit = 3,
    ThreadPriority = 3,
    ThreadSchedPolicy = 4,
    MaxMsgSz = 5,
    MsgTSize = 6,
    ThreadAffinityCpuAdd = 7,
    ThreadAffinityCpuRemove = 8,
    ThreadNamePrefix = 9
}

public enum SocketOption
{
    Affinity = 4,
    RoutingId = 5,
    Subscribe = 6,
    Unsubscribe = 7,
    Rate = 8,
    RecoveryIvl = 9,
    SndBuf = 11,
    RcvBuf = 12,
    RcvMore = 13,
    Fd = 14,
    Events = 15,
    Type = 16,
    Linger = 17,
    ReconnectIvl = 18,
    Backlog = 19,
    ReconnectIvlMax = 21,
    MaxMsgSize = 22,
    SndHwm = 23,
    RcvHwm = 24,
    MulticastHops = 25,
    RcvTimeo = 27,
    SndTimeo = 28,
    LastEndpoint = 32,
    RouterMandatory = 33,
    TcpKeepalive = 34,
    TcpKeepaliveCnt = 35,
    TcpKeepaliveIdle = 36,
    TcpKeepaliveIntvl = 37,
    Immediate = 39,
    XPubVerbose = 40,
    Ipv6 = 42,
    ProbeRouter = 51,
    Conflate = 54,
    RouterHandover = 56,
    Tos = 57,
    ConnectRoutingId = 61,
    HandshakeIvl = 66,
    XPubNoDrop = 69,
    Blocky = 70,
    XPubManual = 71,
    XPubWelcomeMsg = 72,
    InvertMatching = 74,
    HeartbeatIvl = 75,
    HeartbeatTtl = 76,
    HeartbeatTimeout = 77,
    XPubVerboser = 78,
    ConnectTimeout = 79,
    TcpMaxRt = 80,
    MulticastMaxTpdu = 84,
    UseFd = 89,
    RequestTimeout = 90,
    RequestCorrelate = 91,
    BindToDevice = 92,
    TlsCert = 95,
    TlsKey = 96,
    TlsCa = 97,
    TlsVerify = 98,
    TlsRequireClientCert = 99,
    TlsHostname = 100,
    TlsTrustSystem = 101,
    TlsPassword = 102,
    XPubManualLastValue = 98,
    OnlyFirstSubscribe = 108,
    TopicsCount = 116,
    ZmpMetadata = 117
}

[Flags]
public enum SendFlags
{
    None = 0,
    DontWait = 1,
    SendMore = 2
}

[Flags]
public enum ReceiveFlags
{
    None = 0,
    DontWait = 1
}

[Flags]
public enum SocketEvent
{
    Connected = 0x0001,
    ConnectDelayed = 0x0002,
    ConnectRetried = 0x0004,
    Listening = 0x0008,
    BindFailed = 0x0010,
    Accepted = 0x0020,
    AcceptFailed = 0x0040,
    Closed = 0x0080,
    CloseFailed = 0x0100,
    Disconnected = 0x0200,
    MonitorStopped = 0x0400,
    HandshakeFailedNoDetail = 0x0800,
    ConnectionReady = 0x1000,
    HandshakeFailedProtocol = 0x2000,
    HandshakeFailedAuth = 0x4000,
    All = 0xFFFF
}

public enum DisconnectReason
{
    Unknown = 0,
    Local = 1,
    Remote = 2,
    HandshakeFailed = 3,
    TransportError = 4,
    CtxTerm = 5
}

[Flags]
public enum PollEvents
{
    None = 0,
    PollIn = 1,
    PollOut = 2,
    PollErr = 4,
    PollPri = 8
}

public enum SpotTopicMode
{
    Queue = 0,
    RingBuffer = 1
}

public enum GatewayLoadBalancing
{
    RoundRobin = 0,
    Weighted = 1
}

public enum RegistrySocketRole
{
    Pub = 1,
    Router = 2,
    PeerSub = 3
}

public enum DiscoverySocketRole
{
    Sub = 1
}

public enum GatewaySocketRole
{
    Router = 1
}

public enum ReceiverSocketRole
{
    Router = 1,
    Dealer = 2
}

public enum SpotNodeSocketRole
{
    Pub = 1,
    Sub = 2,
    Dealer = 3
}

public enum SpotSocketRole
{
    Pub = 1,
    Sub = 2
}
