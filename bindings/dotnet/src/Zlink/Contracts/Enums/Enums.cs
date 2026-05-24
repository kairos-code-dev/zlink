// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;

namespace Systems.Zlink;

public enum SocketType
{
    Any = 0,
    Pair = 0x1001,
    Pub = 0x1002,
    Sub = 0x1003,
    Dealer = 0x1004,
    Router = 0x1005,
    XPub = 0x1006,
    XSub = 0x1007,
    Stream = 0x1008
}

public enum AutoHwmProfile
{
    Compact = 0,
    LowLatency = 1,
    Balanced = 2,
    Throughput = 3
}

public enum RidDuplicatePolicy
{
    Reject = 0,
    Handover = 1
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

public enum RequestResult
{
    Ok = 0,
    TimedOut = 101,
    NotFound = 102,
    Terminated = 103,
    ProtocolError = 104,
    InternalError = 105,
    Rejected = 106,
    Conflict = 107,
    Busy = 108,
    NotConnected = 109,
    InvalidArgument = 110,
    InvalidState = 111,
    NotSupported = 112
}

public enum DealerMessageType
{
    Raw = 0,
    Request = 1,
    Reply = 2,
    ErrorReply = 3
}

public delegate void RequestCallback(RequestResult result,
    IReadOnlyList<Message> parts);

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

public enum MonitorSourceKind
{
    Socket = 1,
    SpotPub = 3,
    SpotSub = 4
}

public enum PollSourceKind
{
    Socket = 1,
    Fd = 2,
    Timer = 3
}

public enum PollEventFlags
{
    None = 0,
    PollIn = 1,
    PollOut = 2,
    PollErr = 4,
    PollPri = 8,
    PollCompletion = 32
}

public enum RegistryOption
{
    Id = 0x3801,
    HeartbeatIntervalMs = 0x3802,
    HeartbeatTimeoutMs = 0x3803,
    BroadcastIntervalMs = 0x3804
}

public enum SpotDispatchEvent
{
    SubscribeReadable = 1,
    RoutedReadable = 2,
    TimerReadable = 3,
    ChannelReplyReadable = 4,
    ActorReadable = 5,
    ActorJoinReadable = 6
}

public enum SpotDispatchSubjectKind
{
    Spot = 1,
    Timer = 2,
    ChannelDealer = 3,
    Actor = 4
}
