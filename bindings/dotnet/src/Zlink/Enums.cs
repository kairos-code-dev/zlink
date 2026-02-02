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
