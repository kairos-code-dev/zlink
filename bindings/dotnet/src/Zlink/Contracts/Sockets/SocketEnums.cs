// SPDX-License-Identifier: MPL-2.0

using System;
namespace Systems.Zlink;

/// <summary>
/// Defines socket type values.
/// </summary>
public enum SocketType
{
    /// <summary>
    /// Represents the Any value.
    /// </summary>
    Any = 0,
    /// <summary>
    /// Represents the Pair value.
    /// </summary>
    Pair = 0x1001,
    /// <summary>
    /// Represents the Pub value.
    /// </summary>
    Pub = 0x1002,
    /// <summary>
    /// Represents the Sub value.
    /// </summary>
    Sub = 0x1003,
    /// <summary>
    /// Represents the Dealer value.
    /// </summary>
    Dealer = 0x1004,
    /// <summary>
    /// Represents the Router value.
    /// </summary>
    Router = 0x1005,
    /// <summary>
    /// Represents the XPub value.
    /// </summary>
    XPub = 0x1006,
    /// <summary>
    /// Represents the XSub value.
    /// </summary>
    XSub = 0x1007,
    /// <summary>
    /// Represents the Stream value.
    /// </summary>
    Stream = 0x1008
}

/// <summary>
/// Defines automatic high water mark profile values.
/// </summary>
public enum AutoHwmProfile
{
    /// <summary>
    /// Represents the Compact value.
    /// </summary>
    Compact = 0,
    /// <summary>
    /// Represents the LowLatency value.
    /// </summary>
    LowLatency = 1,
    /// <summary>
    /// Represents the Balanced value.
    /// </summary>
    Balanced = 2,
    /// <summary>
    /// Represents the Throughput value.
    /// </summary>
    Throughput = 3
}

/// <summary>
/// Defines routing id duplicate policy values.
/// </summary>
public enum RidDuplicatePolicy
{
    /// <summary>
    /// Represents the Reject value.
    /// </summary>
    Reject = 0,
    /// <summary>
    /// Represents the Handover value.
    /// </summary>
    Handover = 1
}

/// <summary>
/// Defines submit retry mode values.
/// </summary>
public enum SubmitRetryMode
{
    /// <summary>
    /// Represents the Off value.
    /// </summary>
    Off = 0,
    /// <summary>
    /// Represents the LocalFailure value.
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
    /// Represents the None value.
    /// </summary>
    None = 0,
    /// <summary>
    /// Represents the DontWait value.
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
    /// Represents the None value.
    /// </summary>
    None = 0,
    /// <summary>
    /// Represents the DontWait value.
    /// </summary>
    DontWait = 1
}
