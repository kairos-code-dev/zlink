// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

/// <summary>
/// The outcome of an actor lookup.
/// </summary>
/// <param name="Result">The outcome of the lookup.</param>
/// <param name="Actor">The actor that was found.</param>
/// <param name="Flags">Implementation-defined lookup flags.</param>
public sealed record ActorLookupResult(RequestResult Result, ActorRef Actor,
    uint Flags);

/// <summary>
/// Invoked with the result of an actor lookup.
/// </summary>
public delegate void ActorLookupHandler(ActorLookupResult result);

/// <summary>
/// Invoked with a request result and its reply parts; the callback owns the
/// parts and must dispose them.
/// </summary>
public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

/// <summary>
/// Builds an actor leave: optionally set a timeout, then submit.
/// </summary>
public interface ActorLeaveOperation
{
    /// <summary>
    /// Sets how long the operation waits before timing out.
    /// </summary>
    ActorLeaveOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Builds an actor destroy: optionally set a timeout, then submit.
/// </summary>
public interface ActorDestroyOperation
{
    /// <summary>
    /// Sets how long the operation waits before timing out.
    /// </summary>
    ActorDestroyOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Builds an actor lookup: optionally set a timeout, then submit.
/// </summary>
public interface ActorLookupOperation
{
    /// <summary>
    /// Sets how long the operation waits before timing out.
    /// </summary>
    ActorLookupOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ActorLookupHandler callback);
}

/// <summary>
/// Builds an actor bind: optionally set a timeout, then submit.
/// </summary>
public interface ActorBindOperation
{
    /// <summary>
    /// Sets how long the operation waits before timing out.
    /// </summary>
    ActorBindOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ReplyHandler callback);
}

/// <summary>
/// Builds an actor unbind: optionally set a timeout, then submit.
/// </summary>
public interface ActorUnbindOperation
{
    /// <summary>
    /// Sets how long the operation waits before timing out.
    /// </summary>
    ActorUnbindOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ReplyHandler callback);
}
