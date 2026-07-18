// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

internal enum SendResult
{
    Sent = 0,
    Backpressured = 1,
    NotReady = 2
}

/// <summary>
///     The outcome of a MeshNode/spot/actor submit. Maps to
///     <c>zlink_submit_result_t</c>.
/// </summary>
public enum SubmitResult
{
    /// <summary>The submit succeeded.</summary>
    Ok = 0,

    /// <summary>The target mailbox is back-pressured.</summary>
    Backpressured = 1,

    /// <summary>The target is not connected.</summary>
    NotConnected = 2,

    /// <summary>The target was not found.</summary>
    NotFound = 3,

    /// <summary>The target peer is not admitted into the mesh.</summary>
    NotAdmitted = 13,

    /// <summary>The context or node was terminated.</summary>
    Terminated = 4,

    /// <summary>The handle was null or invalid.</summary>
    InvalidHandle = 5,

    /// <summary>An argument was invalid.</summary>
    InvalidArgument = 6,

    /// <summary>The operation is not supported.</summary>
    NotSupported = 7,

    /// <summary>The lifecycle state rejected the submit.</summary>
    InvalidState = 8,

    /// <summary>The call violated the owning thread affinity.</summary>
    ThreadViolation = 9,

    /// <summary>Out of memory.</summary>
    OutOfMemory = 10,

    /// <summary>The operation sequence space is exhausted.</summary>
    SeqExhausted = 11,

    /// <summary>An internal error occurred.</summary>
    InternalError = 12
}

internal enum RecvResult
{
    Ok = 0,
    NoData = 201,
    Busy = 202,
    Terminated = 203,
    InvalidHandle = 204,
    NotSupported = 205,
    InternalError = 206
}

internal enum HandlerResult
{
    Ok = 0,
    InvalidArgument = 301,
    Busy = 302,
    NotSupported = 303,
    Deadlock = 304,
    InvalidHandle = 305,
    InternalError = 306
}