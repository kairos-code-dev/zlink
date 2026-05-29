// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

/// <summary>
/// Describes actor join result data.
/// </summary>
/// <param name="Result">The result value.</param>
/// <param name="JoinResultCode">The join result code value.</param>
/// <param name="Actor">The actor value.</param>
/// <param name="JoinedSpotRid">The joined spot routing id value.</param>
/// <param name="JoinEpoch">The join epoch value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record ActorJoinResult(RequestResult Result, int JoinResultCode,
    ActorRef Actor, RoutingId JoinedSpotRid, ulong JoinEpoch, uint Flags);

/// <summary>
/// Describes actor join entry spot result data.
/// </summary>
/// <param name="Result">The result value.</param>
/// <param name="Actor">The actor value.</param>
/// <param name="TargetNodeRid">The target node routing id value.</param>
/// <param name="JoinEpoch">The join epoch value.</param>
/// <param name="Flags">The flags value.</param>
public sealed record ActorJoinEntrySpotResult(RequestResult Result,
    ActorRef Actor, RoutingId TargetNodeRid, ulong JoinEpoch, uint Flags);

/// <summary>
/// Represents the actor join handler callback.
/// </summary>
public delegate void ActorJoinHandler(ActorJoinResult result,
    IReadOnlyList<Message> replyParts);

/// <summary>
/// Represents the actor join entry spot handler callback.
/// </summary>
public delegate void ActorJoinEntrySpotHandler(
    ActorJoinEntrySpotResult result);

/// <summary>
/// Defines the actor join operation contract.
/// </summary>
public interface ActorJoinOperation
{
    /// <summary>
    /// Adds a message part to the operation.
    /// </summary>
    ActorJoinSubmitOperation Message(Message message);
}

/// <summary>
/// Defines the actor join submit operation contract.
/// </summary>
public interface ActorJoinSubmitOperation
{
    /// <summary>
    /// Adds a message part to the operation.
    /// </summary>
    ActorJoinSubmitOperation Message(Message message);
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorJoinSubmitOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Sets operation flags.
    /// </summary>
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> SubmitAsync(
        CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ActorJoinHandler callback);
}

/// <summary>
/// Defines the actor join callback submit operation contract.
/// </summary>
public interface ActorJoinCallbackSubmitOperation
{
    /// <summary>
    /// Adds a message part to the operation.
    /// </summary>
    ActorJoinCallbackSubmitOperation Message(Message message);
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorJoinCallbackSubmitOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Sets operation flags.
    /// </summary>
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ActorJoinHandler callback);
}

/// <summary>
/// Defines the actor join entry spot operation contract.
/// </summary>
public interface ActorJoinEntrySpotOperation
{
    /// <summary>
    /// Sets the operation timeout.
    /// </summary>
    ActorJoinEntrySpotOperation Timeout(TimeSpan timeout);
    /// <summary>
    /// Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<ActorJoinEntrySpotResult> SubmitAsync(CancellationToken ct = default);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    bool Submit(ActorJoinEntrySpotHandler callback);
}

/// <summary>
/// Defines the actor join reply operation contract.
/// </summary>
public interface ActorJoinReplyOperation
{
    /// <summary>
    /// Adds a message part to the operation.
    /// </summary>
    ActorJoinReplyOperation Message(Message message);
    /// <summary>
    /// Submits the operation.
    /// </summary>
    void Submit();
}
