// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

/// <summary>
/// Describes actor lookup result data.
/// </summary>
/// <param name="Result">The result value.</param>
/// <param name="Actor">The actor value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record ActorLookupResult(RequestResult Result, ActorRef Actor,
    uint Flags);

/// <summary>
/// Represents the actor lookup handler callback.
/// </summary>
public delegate void ActorLookupHandler(ActorLookupResult result);

/// <summary>
/// Represents the reply handler callback.
/// </summary>
public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

/// <summary>
/// Defines the actor leave operation contract.
/// </summary>
public interface ActorLeaveOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorLeaveOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Defines the actor destroy operation contract.
/// </summary>
public interface ActorDestroyOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorDestroyOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Defines the actor lookup operation contract.
/// </summary>
public interface ActorLookupOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorLookupOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ActorLookupHandler callback);
}

/// <summary>
/// Defines the actor bind operation contract.
/// </summary>
public interface ActorBindOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorBindOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Defines the actor unbind operation contract.
/// </summary>
public interface ActorUnbindOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorUnbindOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ReplyHandler callback);
}
