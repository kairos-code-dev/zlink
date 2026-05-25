// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public sealed record ActorJoinResult(RequestResult Result, ActorRef Actor,
    RoutingId JoinedSpotRid, ulong JoinEpoch, uint Flags);

public sealed record ActorJoinEntrySpotResult(RequestResult Result,
    ActorRef Actor, RoutingId TargetNodeRid, ulong JoinEpoch, uint Flags);

public sealed record ActorLookupResult(RequestResult Result, ActorRef Actor,
    uint Flags);

public delegate void ActorJoinHandler(ActorJoinResult result,
    IReadOnlyList<Message> replyParts);

public delegate void ActorJoinEntrySpotHandler(
    ActorJoinEntrySpotResult result);

public delegate void ActorLookupHandler(ActorLookupResult result);

public delegate void ActorLifecycleHandler(ISpot spot,
    SpotActorLifecycleInfo info);

public delegate void ReplyHandler(RequestResult result,
    IReadOnlyList<Message> parts);

public interface ActorJoinOperation
{
    ActorJoinSubmitOperation Message(Message message);
}

public interface ActorJoinSubmitOperation
{
    ActorJoinSubmitOperation Message(Message message);
    ActorJoinSubmitOperation Timeout(TimeSpan timeout);
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)> SubmitAsync(
        CancellationToken ct = default);
    bool Submit(ActorJoinHandler callback);
}

public interface ActorJoinCallbackSubmitOperation
{
    ActorJoinCallbackSubmitOperation Message(Message message);
    ActorJoinCallbackSubmitOperation Timeout(TimeSpan timeout);
    ActorJoinCallbackSubmitOperation Flags(SendFlags flags);
    bool Submit(ActorJoinHandler callback);
}

public interface ActorJoinEntrySpotOperation
{
    ActorJoinEntrySpotOperation Timeout(TimeSpan timeout);
    Task<ActorJoinEntrySpotResult> SubmitAsync(CancellationToken ct = default);
    bool Submit(ActorJoinEntrySpotHandler callback);
}

public interface ActorJoinReplyOperation
{
    ActorJoinReplyOperation Message(Message message);
    void Submit();
}

public interface ActorLeaveOperation
{
    ActorLeaveOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorDestroyOperation
{
    ActorDestroyOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorLookupOperation
{
    ActorLookupOperation Timeout(TimeSpan timeout);
    Task<ActorLookupResult> SubmitAsync(CancellationToken ct = default);
    bool Submit(ActorLookupHandler callback);
}

public interface ActorBindOperation
{
    ActorBindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}

public interface ActorUnbindOperation
{
    ActorUnbindOperation Timeout(TimeSpan timeout);
    Task<IReadOnlyList<Message>> SubmitAsync(CancellationToken ct = default);
    bool Submit(ReplyHandler callback);
}
