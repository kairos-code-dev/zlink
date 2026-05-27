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

public enum ReceivedMessageType
{
    Raw = 0,
    Request = 1,
    Reply = 2,
    ErrorReply = 3
}

public delegate void RequestCallback(RequestResult result,
    IReadOnlyList<Message> parts);
