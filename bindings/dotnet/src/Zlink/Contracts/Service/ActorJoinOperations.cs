// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Systems.Zlink;

public sealed record ActorJoinResult(RequestResult Result, int JoinResultCode,
    ActorRef Actor, RoutingId JoinedSpotRid, ulong JoinEpoch, uint Flags);

public sealed record ActorJoinEntrySpotResult(RequestResult Result,
    ActorRef Actor, RoutingId TargetNodeRid, ulong JoinEpoch, uint Flags);

public delegate void ActorJoinHandler(ActorJoinResult result,
    IReadOnlyList<Message> replyParts);

public delegate void ActorJoinEntrySpotHandler(
    ActorJoinEntrySpotResult result);

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
