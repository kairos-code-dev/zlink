// SPDX-License-Identifier: MPL-2.0

using System;

namespace Zlink;

internal enum SocketType
{
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

internal enum ContextOption
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
    ThreadNamePrefix = 9,
    Blocky = 10
}

internal enum SocketOption
{
    RoutingId = 0x7F000001,
    Subscribe = 0x7F000002,
    Unsubscribe = 0x7F000003,
    Affinity = 0x3001,
    Rate = 0x3003,
    RecoveryIvl = 0x3004,
    SndBuf = 0x3005,
    RcvBuf = 0x3006,
    RcvMore = 0x7F000004,
    Fd = 0x3007,
    Events = 0x3008,
    Type = 0x3009,
    Linger = 0x300A,
    ReconnectIvl = 0x300B,
    Backlog = 0x300C,
    ReconnectIvlMax = 0x300D,
    MaxMsgSize = 0x300E,
    SndHwm = 0x300F,
    RcvHwm = 0x3010,
    MulticastHops = 0x3011,
    RcvTimeo = 0x3012,
    SndTimeo = 0x3013,
    LastEndpoint = 0x3014,
    TcpKeepalive = 0x3015,
    TcpKeepaliveCnt = 0x3016,
    TcpKeepaliveIdle = 0x3017,
    TcpKeepaliveIntvl = 0x3018,
    Immediate = 0x3019,
    Ipv6 = 0x301A,
    Conflate = 0x301B,
    Tos = 0x301C,
    HandshakeIvl = 0x301D,
    Blocky = 0x301E,
    InvertMatching = 0x3020,
    HeartbeatIvl = 0x3021,
    HeartbeatTtl = 0x3022,
    HeartbeatTimeout = 0x3023,
    ConnectTimeout = 0x3024,
    TcpMaxRt = 0x3025,
    MulticastMaxTpdu = 0x3026,
    BindToDevice = 0x3027,
    TlsCert = 0x3028,
    TlsKey = 0x3029,
    TlsCa = 0x302A,
    TlsVerify = 0x302B,
    TlsRequireClientCert = 0x302C,
    TlsHostname = 0x302D,
    TlsTrustSystem = 0x302E,
    TlsPassword = 0x302F,
    ZmpMetadata = 0x3030,
    TcpNoDelay = 0x3031,
    RidDuplicatePolicy = 0x3033,
    AutoHwmMsgUnitBytes = 0x3034,
    RouterMandatory = 0x3101,
    ProbeRouter = 0x3103,
    ConnectRoutingId = 0x3104,
    XPubVerbose = 0x3301,
    XPubVerboser = 0x3302,
    XPubManual = 0x3303,
    XPubManualLastValue = 0x3304,
    XPubNoDrop = 0x3305,
    XPubWelcomeMsg = 0x3306,
    TopicsCount = 0x3307,
    StreamNotify = 0x3501,
    SubTopicsCount = 0x3400,
    UseFd = 0x7F000005,
    OnlyFirstSubscribe = 0x7F000006
}

internal enum SendResult
{
    Sent = 0,
    Backpressured = 1,
    NotReady = 2
}

[Flags]
public enum SendFlags
{
    None = 0,
    DontWait = 1
}

[Flags]
public enum RecvFlags
{
    None = 0,
    DontWait = 1
}

public enum SubmitResult
{
    Ok = 0,
    Backpressured = 1,
    NotConnected = 2,
    NotFound = 3,
    NotAdmitted = 13,
    Terminated = 4,
    InvalidHandle = 5,
    InvalidArgument = 6,
    NotSupported = 7,
    InvalidState = 8,
    ThreadViolation = 9,
    OutOfMemory = 10,
    SeqExhausted = 11,
    InternalError = 12
}

public enum RequestResult
{
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104
}

public enum RecvResult
{
    Ok = 0,
    NoData = 201,
    Busy = 202,
    Terminated = 203,
    InvalidHandle = 204,
    NotSupported = 205
}

public enum HandlerResult
{
    Ok = 0,
    InvalidArgument = 301,
    Busy = 302,
    NotSupported = 303,
    Deadlock = 304,
    InvalidHandle = 305
}

public enum CloseResult
{
    Ok = 0,
    Busy = 401,
    Shutdown = 402,
    InvalidHandle = 403
}

public enum BindResult
{
    Ok = 0,
    InvalidArgument = 501,
    AddrInUse = 502,
    NotSupported = 503,
    InvalidHandle = 504
}

public enum ConnectResult
{
    Ok = 0,
    InvalidArgument = 601,
    NotSupported = 602,
    InvalidHandle = 603
}

public enum ConfigResult
{
    Ok = 0,
    InvalidHandle = 701,
    InvalidArgument = 702,
    NotSupported = 703
}

internal enum ErrorCode
{
    None = 0,
    Unknown = -1,

    EBusy = 16,
    EIntr = 4,
    EAgain = 11,
    EBadf = 9,
    ENomem = 12,
    EAccess = 13,
    EFault = 14,
    EInval = 22,
    ENotSock = 88,
    EMsgSize = 90,
    EProtoNoSupport = 93,
    ENotSup = 95,
    EAfNoSupport = 97,
    EAddrInUse = 98,
    EAddrNotAvail = 99,
    ENetDown = 100,
    ENetUnreach = 101,
    ENetReset = 102,
    EConnAborted = 103,
    EConnReset = 104,
    ENoBufs = 105,
    ENotConn = 107,
    ETimedOut = 110,
    EConnRefused = 111,
    EHostUnreach = 113,
    EInProgress = 115,
    EShutdown = 108,

    Efsm = 156384763,
    EnoCompatProto = 156384764,
    Eterm = 156384765,
    EmThread = 156384766
}

internal enum ProtocolError
{
    ZmpMalformedCommandHello = 0x10000013
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
    PeerWeightChanged = 0x8000,
    All = 0xFFFF
}

public enum MonitorEventType
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
    PeerWeightChanged = 0x8000
}

internal enum DisconnectReason
{
    Unknown = 0,
    HandshakeFailed = 3,
    TransportError = 4,
    CtxTerm = 5
}

internal enum MonitorSourceKind
{
    Socket = 1,
    SpotPub = 3,
    SpotSub = 4
}

public enum SourceKind
{
    Socket = 1,
    SpotPub = 3,
    SpotSub = 4
}

[Flags]
internal enum MonitorState
{
    None = 0,
    Ready = 1 << 0,
    BoundReady = 1 << 1,
    Closed = 1 << 3
}

[Flags]
internal enum MonitorSnapshotDetail
{
    None = 0,
    SendPendingMessages = 1 << 1,
    ReceivePendingMessages = 1 << 2
}

public enum PollEvents
{
    None = 0,
    PollIn = 1,
    PollOut = 2,
    PollErr = 4,
    PollPri = 8
}

internal enum RegistrySocketRole
{
    Pub = 1,
    Router = 2,
    PeerSub = 3
}

internal enum SpotNodeSocketRole
{
    Node = 0,
    Pub = 1,
    Sub = 2,
    Dealer = 3
}

internal enum SpotNodeOption
{
    PubMode = 1,
    PubQueueHwm = 2,
    PubQueueFullPolicy = 3,
    TopicSendHwm = 0x3608,
    TopicRecvHwm = 0x3609,
    RoutedSendHwm = 0x360A,
    RoutedRecvHwm = 0x360B
}

internal enum MessageType : byte
{
    Data = 0,
    Request = 1,
    Reply = 2
}

internal enum SpotNodePubMode
{
    Sync = 0,
    Async = 1
}

internal enum SpotNodePubQueueFullPolicy
{
    Eagain = 0,
    Drop = 1
}

internal enum SpotSocketRole
{
    Pub = 1,
    Sub = 2
}

public enum SpotDispatchEvent
{
    SubscribeReadable = 1,
    RoutedReadable = 2,
    TimerReadable = 3,
    ChannelReplyReadable = 4
}

public enum SpotDispatchSubjectKind
{
    Spot = 1,
    Timer = 2,
    ChannelDealer = 3
}
