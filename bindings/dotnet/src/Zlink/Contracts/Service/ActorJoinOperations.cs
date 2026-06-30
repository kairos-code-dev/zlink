// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The outcome of an actor join.
/// </summary>
/// <param name="Result">The outcome of the join request.</param>
/// <param name="JoinResultCode">The application-supplied join result code.</param>
/// <param name="Actor">The joined actor.</param>
/// <param name="JoinedSpotRid">The routing id of the spot that was joined.</param>
/// <param name="JoinEpoch">The join epoch (generation) assigned.</param>
/// <param name="Flags">Implementation-defined join flags.</param>
public sealed record ActorJoinResult(
    RequestResult Result,
    int JoinResultCode,
    ActorRef Actor,
    RoutingId JoinedSpotRid,
    ulong JoinEpoch,
    uint Flags);

/// <summary>
///     The outcome of an actor join routed through an entry spot.
/// </summary>
/// <param name="Result">The outcome of the join request.</param>
/// <param name="JoinResultCode">The application-supplied join result code.</param>
/// <param name="Actor">The joined actor.</param>
/// <param name="TargetNodeRid">The routing id of the node the actor was routed to.</param>
/// <param name="JoinedSpotRid">The routing id of the entry spot that was joined.</param>
/// <param name="JoinEpoch">The join epoch (generation) assigned.</param>
/// <param name="Flags">Implementation-defined join flags.</param>
public sealed record ActorJoinEntrySpotResult(
    RequestResult Result,
    int JoinResultCode,
    ActorRef Actor,
    RoutingId TargetNodeRid,
    RoutingId JoinedSpotRid,
    ulong JoinEpoch,
    uint Flags);

/// <summary>
///     Invoked with the result of an actor join and its reply parts; the callback
///     owns the parts and must dispose them.
/// </summary>
public delegate void ActorJoinHandler(ActorJoinResult result,
    IReadOnlyList<Message> replyParts);

/// <summary>
///     Invoked with the result of an actor join routed through an entry spot and
///     its reply parts; the callback owns the parts and must dispose them.
/// </summary>
public delegate void ActorJoinEntrySpotHandler(
    ActorJoinEntrySpotResult result, IReadOnlyList<Message> replyParts);

/// <summary>
///     Builds an actor join: add message parts, then submit and await a reply.
/// </summary>
public interface ActorJoinOperation
{
    /// <summary>
    ///     Adds a message part; consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    ActorJoinSubmitOperation Message(Message message);
}

/// <summary>
///     Accepts further parts, timeout, flags, and the terminal submit of an actor join.
/// </summary>
public interface ActorJoinSubmitOperation
{
    /// <summary>
    ///     Adds a message part; consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    ActorJoinSubmitOperation Message(Message message);

    /// <summary>
    ///     Sets how long the operation waits before timing out.
    /// </summary>
    ActorJoinSubmitOperation Timeout(TimeSpan timeout);

    /// <summary>
    ///     Sets the send flags applied at submit time.
    /// </summary>
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);

    /// <summary>
    ///     Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> Async(
        CancellationToken ct = default);

    /// <summary>
    ///     Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ActorJoinHandler callback);
}

/// <summary>
///     Callback-submission stage of an actor join (reached after setting flags).
/// </summary>
public interface ActorJoinCallbackSubmitOperation
{
    /// <summary>
    ///     Adds a message part; consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    ActorJoinCallbackSubmitOperation Message(Message message);

    /// <summary>
    ///     Sets how long the operation waits before timing out.
    /// </summary>
    ActorJoinCallbackSubmitOperation Timeout(TimeSpan timeout);

    /// <summary>
    ///     Sets the send flags applied at submit time.
    /// </summary>
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);

    /// <summary>
    ///     Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ActorJoinHandler callback);
}

/// <summary>
///     Builds an actor join routed through an entry spot: add parts, then submit.
/// </summary>
public interface ActorJoinEntrySpotOperation
{
    /// <summary>
    ///     Adds a message part; consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    ActorJoinEntrySpotOperation Message(Message message);

    /// <summary>
    ///     Sets how long the operation waits before timing out.
    /// </summary>
    ActorJoinEntrySpotOperation Timeout(TimeSpan timeout);

    /// <summary>
    ///     Sets the send flags applied at submit time.
    /// </summary>
    ActorJoinEntrySpotOperation Flags(SendFlags flags);

    /// <summary>
    ///     Submits the operation and returns the result asynchronously.
    /// </summary>
    Task<(ActorJoinEntrySpotResult Result, IReadOnlyList<Message> Parts)> Async(
        CancellationToken ct = default);

    /// <summary>
    ///     Submits the operation; the result is delivered to the callback.
    /// </summary>
    bool Submit(ActorJoinEntrySpotHandler callback);
}

/// <summary>
///     Builds a reply to an actor-join request: add parts, then submit.
/// </summary>
public interface ActorJoinReplyOperation
{
    /// <summary>
    ///     Adds a message part; consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    ActorJoinReplyOperation Message(Message message);

    /// <summary>
    ///     Submits the reply. Failures throw <see cref="ZlinkException" />.
    /// </summary>
    void Submit();
}