// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

/// <summary>
/// First stage of a send operation builder.
/// </summary>
public interface SendOperation
{
    /// <summary>
    /// Add the first message part to the operation.
    /// </summary>
    SendSubmitOperation Message(Message message);
}

/// <summary>
/// Submit stage of a send operation builder.
/// </summary>
public interface SendSubmitOperation
{
    /// <summary>
    /// Add another message part to the operation.
    /// </summary>
    SendSubmitOperation Message(Message message);

    /// <summary>
    /// Set send flags for the operation.
    /// </summary>
    SendSubmitOperation Flags(SendFlags flags);

    /// <summary>
    /// Submit the operation.
    /// </summary>
    bool Submit();
}

/// <summary>
/// First stage of a request operation builder.
/// </summary>
public interface RequestOperation
{
    /// <summary>
    /// Add the first request message part.
    /// </summary>
    RequestSubmitOperation Message(Message message);
}

/// <summary>
/// Submit stage of a request operation builder.
/// </summary>
public interface RequestSubmitOperation
{
    /// <summary>
    /// Add another request message part.
    /// </summary>
    RequestSubmitOperation Message(Message message);

    /// <summary>
    /// Set the request timeout.
    /// </summary>
    RequestSubmitOperation Timeout(TimeSpan timeout);

    /// <summary>
    /// Set send flags and switch to the callback submit shape.
    /// </summary>
    RequestCallbackSubmitOperation Flags(SendFlags flags);

    /// <summary>
    /// Submit the request and return the reply parts.
    /// </summary>
    /// <remarks>
    /// The caller owns the returned messages and must dispose them.
    /// </remarks>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);

    /// <summary>
    /// Submit the request and deliver the result to <paramref name="callback"/>.
    /// </summary>
    bool Submit(RequestCallback callback);
}

/// <summary>
/// Callback submit stage of a request operation builder.
/// </summary>
public interface RequestCallbackSubmitOperation
{
    /// <summary>
    /// Add another request message part.
    /// </summary>
    RequestCallbackSubmitOperation Message(Message message);

    /// <summary>
    /// Set the request timeout.
    /// </summary>
    RequestCallbackSubmitOperation Timeout(TimeSpan timeout);

    /// <summary>
    /// Set send flags for the request.
    /// </summary>
    RequestCallbackSubmitOperation Flags(SendFlags flags);

    /// <summary>
    /// Submit the request and deliver the result to <paramref name="callback"/>.
    /// </summary>
    bool Submit(RequestCallback callback);
}

/// <summary>
/// First stage of a reply operation builder.
/// </summary>
public interface ReplyOperation
{
    /// <summary>
    /// Add the first reply message part.
    /// </summary>
    ReplySubmitOperation Message(Message message);
}

/// <summary>
/// Submit stage of a reply operation builder.
/// </summary>
public interface ReplySubmitOperation
{
    /// <summary>
    /// Add another reply message part.
    /// </summary>
    ReplySubmitOperation Message(Message message);

    /// <summary>
    /// Set send flags for the reply.
    /// </summary>
    ReplySubmitOperation Flags(SendFlags flags);

    /// <summary>
    /// Submit the reply.
    /// </summary>
    void Submit();
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

/// <summary>
/// Receives request completion and reply payloads.
/// </summary>
/// <remarks>
/// When <paramref name="result"/> is <see cref="RequestResult.Ok"/>, the
/// callback owns the messages in <paramref name="parts"/> and must dispose them.
/// Non-success results provide an empty payload list.
/// </remarks>
public delegate void RequestCallback(RequestResult result,
    IReadOnlyList<Message> parts);
