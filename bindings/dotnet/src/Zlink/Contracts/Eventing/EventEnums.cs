// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Defines socket event values.
/// </summary>
[Flags]
public enum SocketEvent
{
    /// <summary>
    /// Indicates the connected socket event.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// Indicates the connect delayed socket event.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// Indicates the connect retried socket event.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// Indicates the listening socket event.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Indicates the bind failed socket event.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// Indicates the accepted socket event.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Indicates the accept failed socket event.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// Indicates the closed socket event.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Indicates the close failed socket event.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// Indicates the disconnected socket event.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Indicates the monitor stopped socket event.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// Indicates the handshake failed no detail socket event.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// Indicates the connection ready socket event.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// Indicates the handshake failed protocol socket event.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// Indicates the handshake failed auth socket event.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// Indicates the peer weight changed socket event.
    /// </summary>
    PeerWeightChanged = 0x8000,
    /// <summary>
    /// Indicates the all socket event.
    /// </summary>
    All = 0xFFFF
}

/// <summary>
/// Defines monitor event type values.
/// </summary>
public enum MonitorEventType
{
    /// <summary>
    /// Indicates the connected monitor event type.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// Indicates the connect delayed monitor event type.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// Indicates the connect retried monitor event type.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// Indicates the listening monitor event type.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Indicates the bind failed monitor event type.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// Indicates the accepted monitor event type.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Indicates the accept failed monitor event type.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// Indicates the closed monitor event type.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Indicates the close failed monitor event type.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// Indicates the disconnected monitor event type.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Indicates the monitor stopped monitor event type.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// Indicates the handshake failed no detail monitor event type.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// Indicates the connection ready monitor event type.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// Indicates the handshake failed protocol monitor event type.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// Indicates the handshake failed auth monitor event type.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// Indicates the peer weight changed monitor event type.
    /// </summary>
    PeerWeightChanged = 0x8000
}

/// <summary>
/// Defines monitor source kind values.
/// </summary>
public enum MonitorSourceKind
{
    /// <summary>
    /// Indicates the socket monitor source kind.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// Indicates the spot pub monitor source kind.
    /// </summary>
    SpotPub = 3,
    /// <summary>
    /// Indicates the spot sub monitor source kind.
    /// </summary>
    SpotSub = 4
}

/// <summary>
/// Defines poll source kind values.
/// </summary>
public enum PollSourceKind
{
    /// <summary>
    /// Indicates the socket poll source kind.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// Indicates the file descriptor poll source kind.
    /// </summary>
    Fd = 2,
    /// <summary>
    /// Indicates the timer poll source kind.
    /// </summary>
    Timer = 3
}

/// <summary>
/// Defines poll event flags values.
/// </summary>
public enum PollEventFlags
{
    /// <summary>
    /// Indicates the none poll event flags.
    /// </summary>
    None = 0,
    /// <summary>
    /// Indicates the poll in poll event flags.
    /// </summary>
    PollIn = 1,
    /// <summary>
    /// Indicates the poll out poll event flags.
    /// </summary>
    PollOut = 2,
    /// <summary>
    /// Indicates the poll err poll event flags.
    /// </summary>
    PollErr = 4,
    /// <summary>
    /// Indicates the poll pri poll event flags.
    /// </summary>
    PollPri = 8,
    /// <summary>
    /// Indicates the poll completion poll event flags.
    /// </summary>
    PollCompletion = 32
}
