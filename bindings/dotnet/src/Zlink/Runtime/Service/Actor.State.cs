// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public readonly partial struct ActorRef
{
    internal static ActorRef Unchecked(RoutingId nodeRid, string actorId)
    {
        return new ActorRef(nodeRid, actorId, 0);
    }

    internal static ActorRef Remote(RoutingId targetNodeRid, string actorId)
    {
        return Unchecked(targetNodeRid, actorId);
    }
}

public sealed partial class ActorJoinRequest
{
    internal ActorJoinRequest(ActorJoinInfo info, Message message)
        : this(info, [message], null)
    {
    }

    internal ActorJoinRequest(ActorJoinInfo info, IReadOnlyList<Message> parts)
        : this(info, parts, null)
    {
    }

    internal ActorJoinRequest(ActorJoinInfo info, IReadOnlyList<Message> parts,
        object? runtimeState)
    {
        Info = info;
        Parts = parts;
        Message = parts.Count > 0 ? parts[0] : new Message();
        RuntimeState = runtimeState;
    }

    internal object? RuntimeState { get; }
}