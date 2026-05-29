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
    /// Represents the Connected value.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// Represents the ConnectDelayed value.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// Represents the ConnectRetried value.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// Represents the Listening value.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Represents the BindFailed value.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// Represents the Accepted value.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Represents the AcceptFailed value.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// Represents the Closed value.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Represents the CloseFailed value.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// Represents the Disconnected value.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Represents the MonitorStopped value.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// Represents the HandshakeFailedNoDetail value.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// Represents the ConnectionReady value.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// Represents the HandshakeFailedProtocol value.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// Represents the HandshakeFailedAuth value.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// Represents the PeerWeightChanged value.
    /// </summary>
    PeerWeightChanged = 0x8000,
    /// <summary>
    /// Represents the All value.
    /// </summary>
    All = 0xFFFF
}

/// <summary>
/// Defines monitor event type values.
/// </summary>
public enum MonitorEventType
{
    /// <summary>
    /// Represents the Connected value.
    /// </summary>
    Connected = 0x0001,
    /// <summary>
    /// Represents the ConnectDelayed value.
    /// </summary>
    ConnectDelayed = 0x0002,
    /// <summary>
    /// Represents the ConnectRetried value.
    /// </summary>
    ConnectRetried = 0x0004,
    /// <summary>
    /// Represents the Listening value.
    /// </summary>
    Listening = 0x0008,
    /// <summary>
    /// Represents the BindFailed value.
    /// </summary>
    BindFailed = 0x0010,
    /// <summary>
    /// Represents the Accepted value.
    /// </summary>
    Accepted = 0x0020,
    /// <summary>
    /// Represents the AcceptFailed value.
    /// </summary>
    AcceptFailed = 0x0040,
    /// <summary>
    /// Represents the Closed value.
    /// </summary>
    Closed = 0x0080,
    /// <summary>
    /// Represents the CloseFailed value.
    /// </summary>
    CloseFailed = 0x0100,
    /// <summary>
    /// Represents the Disconnected value.
    /// </summary>
    Disconnected = 0x0200,
    /// <summary>
    /// Represents the MonitorStopped value.
    /// </summary>
    MonitorStopped = 0x0400,
    /// <summary>
    /// Represents the HandshakeFailedNoDetail value.
    /// </summary>
    HandshakeFailedNoDetail = 0x0800,
    /// <summary>
    /// Represents the ConnectionReady value.
    /// </summary>
    ConnectionReady = 0x1000,
    /// <summary>
    /// Represents the HandshakeFailedProtocol value.
    /// </summary>
    HandshakeFailedProtocol = 0x2000,
    /// <summary>
    /// Represents the HandshakeFailedAuth value.
    /// </summary>
    HandshakeFailedAuth = 0x4000,
    /// <summary>
    /// Represents the PeerWeightChanged value.
    /// </summary>
    PeerWeightChanged = 0x8000
}

/// <summary>
/// Defines monitor source kind values.
/// </summary>
public enum MonitorSourceKind
{
    /// <summary>
    /// Represents the Socket value.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// Represents the SpotPub value.
    /// </summary>
    SpotPub = 3,
    /// <summary>
    /// Represents the SpotSub value.
    /// </summary>
    SpotSub = 4
}

/// <summary>
/// Defines poll source kind values.
/// </summary>
public enum PollSourceKind
{
    /// <summary>
    /// Represents the Socket value.
    /// </summary>
    Socket = 1,
    /// <summary>
    /// Represents the Fd value.
    /// </summary>
    Fd = 2,
    /// <summary>
    /// Represents the Timer value.
    /// </summary>
    Timer = 3
}

/// <summary>
/// Defines poll event flags values.
/// </summary>
public enum PollEventFlags
{
    /// <summary>
    /// Represents the None value.
    /// </summary>
    None = 0,
    /// <summary>
    /// Represents the PollIn value.
    /// </summary>
    PollIn = 1,
    /// <summary>
    /// Represents the PollOut value.
    /// </summary>
    PollOut = 2,
    /// <summary>
    /// Represents the PollErr value.
    /// </summary>
    PollErr = 4,
    /// <summary>
    /// Represents the PollPri value.
    /// </summary>
    PollPri = 8,
    /// <summary>
    /// Represents the PollCompletion value.
    /// </summary>
    PollCompletion = 32
}
