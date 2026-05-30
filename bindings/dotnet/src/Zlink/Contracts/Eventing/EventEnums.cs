// SPDX-License-Identifier: MPL-2.0

using System;

namespace Systems.Zlink;

/// <summary>
/// Connection lifecycle events that a monitor can be subscribed to. Combine as
/// flags to select which events are delivered.
/// </summary>
[Flags]
public enum SocketEvent
{
    /// <summary>
    /// A connection to a peer was established.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// An asynchronous connect is still in progress.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// A failed connect will be retried after a delay.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// The socket began listening on a bound endpoint.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Binding to an endpoint failed.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// An inbound connection was accepted.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Accepting an inbound connection failed.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// A connection was closed.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Closing a connection failed.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// A peer disconnected.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Monitoring of the socket has stopped.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// The connection handshake failed without further detail.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// The connection completed its handshake and is ready for traffic.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// The handshake failed due to a protocol error.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// The handshake failed authentication.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// A peer's load-balancing weight changed.
    /// </summary>
    PeerWeightChanged = 0x8000,
    /// <summary>
    /// Every event; subscribes the monitor to all of the above.
    /// </summary>
    All = 0xFFFF
}

/// <summary>
/// The kind of a delivered <see cref="MonitorEvent"/>; mirrors the lifecycle
/// events of <see cref="SocketEvent"/>.
/// </summary>
public enum MonitorEventType
{
    /// <summary>
    /// A connection to a peer was established.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// An asynchronous connect is still in progress.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// A failed connect will be retried after a delay.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// The socket began listening on a bound endpoint.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Binding to an endpoint failed.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// An inbound connection was accepted.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Accepting an inbound connection failed.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// A connection was closed.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Closing a connection failed.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// A peer disconnected.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Monitoring of the socket has stopped.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// The connection handshake failed without further detail.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// The connection completed its handshake and is ready for traffic.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// The handshake failed due to a protocol error.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// The handshake failed authentication.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// A peer's load-balancing weight changed.
    /// </summary>
    PeerWeightChanged = 0x8000
}

/// <summary>
/// Identifies what a monitored source is.
/// </summary>
public enum MonitorSourceKind
{
    /// <summary>
    /// A plain socket.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// The publish side of a spot.
    /// </summary>
    SpotPub = 3,
    /// <summary>
    /// The subscribe side of a spot.
    /// </summary>
    SpotSub = 4
}

/// <summary>
/// Identifies what kind of source a poll event came from.
/// </summary>
public enum PollSourceKind
{
    /// <summary>
    /// A socket.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// A raw file descriptor.
    /// </summary>
    Fd = 2,
    /// <summary>
    /// A timer.
    /// </summary>
    Timer = 3
}

/// <summary>
/// Readiness conditions a poll source can be watched for or report. Combine as
/// flags.
/// </summary>
public enum PollEventFlags
{
    /// <summary>
    /// No events.
    /// </summary>
    None = 0,
    /// <summary>
    /// Readable: a receive will not block.
    /// </summary>
    PollIn = 1,
    /// <summary>
    /// Writable: a send will not block.
    /// </summary>
    PollOut = 2,
    /// <summary>
    /// An error condition occurred on the source.
    /// </summary>
    PollErr = 4,
    /// <summary>
    /// Priority/out-of-band data is available.
    /// </summary>
    PollPri = 8,
    /// <summary>
    /// An asynchronous operation completed.
    /// </summary>
    PollCompletion = 32
}
