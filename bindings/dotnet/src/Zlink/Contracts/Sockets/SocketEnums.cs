// SPDX-License-Identifier: MPL-2.0

using System;
namespace Systems.Zlink;

/// <summary>
/// Defines socket type values.
/// </summary>
public enum SocketType
{
    /// <summary>
    /// Indicates the any socket type.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Indicates the pair socket type.
    /// </summary>
    Pair = 0x1001,
    /// <summary>
    /// Indicates the pub socket type.
    /// </summary>
    Pub = 0x1002,
    /// <summary>
    /// Indicates the sub socket type.
    /// </summary>
    Sub = 0x1003,
    /// <summary>
    /// Indicates the dealer socket type.
    /// </summary>
    Dealer = 0x1004,
    /// <summary>
    /// Indicates the router socket type.
    /// </summary>
    Router = 0x1005,
    /// <summary>
    /// Indicates the xpub socket type.
    /// </summary>
    XPub = 0x1006,
    /// <summary>
    /// Indicates the xsub socket type.
    /// </summary>
    XSub = 0x1007,
    /// <summary>
    /// Indicates the stream socket type.
    /// </summary>
    Stream = 0x1008
}

/// <summary>
/// Defines automatic high water mark profile values.
/// </summary>
public enum AutoHwmProfile
{
    /// <summary>
    /// Indicates the compact auto hwm profile.
    /// </summary>
    Compact = 0,
    /// <summary>
    /// Indicates the low latency auto hwm profile.
    /// </summary>
    LowLatency = 1,
    /// <summary>
    /// Indicates the balanced auto hwm profile.
    /// </summary>
    Balanced = 2,
    /// <summary>
    /// Indicates the throughput auto hwm profile.
    /// </summary>
    Throughput = 3
}

/// <summary>
/// Defines routing id duplicate policy values.
/// </summary>
public enum RidDuplicatePolicy
{
    /// <summary>
    /// Indicates the reject rid duplicate policy.
    /// </summary>
    Reject = 0,
    /// <summary>
    /// Indicates the handover rid duplicate policy.
    /// </summary>
    Handover = 1
}

/// <summary>
/// Defines submit retry mode values.
/// </summary>
public enum SubmitRetryMode
{
    /// <summary>
    /// Indicates the off submit retry mode.
    /// </summary>
    Off = 0,
    /// <summary>
    /// Indicates the local failure submit retry mode.
    /// </summary>
    LocalFailure = 1
}

/// <summary>
/// Defines send flags values.
/// </summary>
[Flags]
public enum SendFlags
{
    /// <summary>
    /// Indicates the none send flags.
    /// </summary>
    None = 0,
    /// <summary>
    /// Indicates the non-blocking send flags.
    /// </summary>
    DontWait = 1
}

/// <summary>
/// Defines recv flags values.
/// </summary>
[Flags]
public enum RecvFlags
{
    /// <summary>
    /// Indicates the none recv flags.
    /// </summary>
    None = 0,
    /// <summary>
    /// Indicates the non-blocking recv flags.
    /// </summary>
    DontWait = 1
}
