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

/// <summary>
/// Defines request result values.
/// </summary>
public enum RequestResult
{
    /// <summary>
    /// Represents the Ok value.
    /// </summary>
    Ok = 0,
    /// <summary>
    /// Represents the TimedOut value.
    /// </summary>
    TimedOut = 101,
    /// <summary>
    /// Represents the NotFound value.
    /// </summary>
    NotFound = 102,
    /// <summary>
    /// Represents the Terminated value.
    /// </summary>
    Terminated = 103,
    /// <summary>
    /// Represents the ProtocolError value.
    /// </summary>
    ProtocolError = 104,
    /// <summary>
    /// Represents the InternalError value.
    /// </summary>
    InternalError = 105,
    /// <summary>
    /// Represents the Rejected value.
    /// </summary>
    Rejected = 106,
    /// <summary>
    /// Represents the Conflict value.
    /// </summary>
    Conflict = 107,
    /// <summary>
    /// Represents the Busy value.
    /// </summary>
    Busy = 108,
    /// <summary>
    /// Represents the NotConnected value.
    /// </summary>
    NotConnected = 109,
    /// <summary>
    /// Represents the InvalidArgument value.
    /// </summary>
    InvalidArgument = 110,
    /// <summary>
    /// Represents the InvalidState value.
    /// </summary>
    InvalidState = 111,
    /// <summary>
    /// Represents the NotSupported value.
    /// </summary>
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
