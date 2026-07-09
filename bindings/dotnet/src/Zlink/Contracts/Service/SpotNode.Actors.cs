// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     Manages actors and actor routes owned by a SPOT node.
/// </summary>
public interface ISpotNodeActors
{
    /// <summary>
    ///     Creates an actor hosted by this node.
    /// </summary>
    IActor CreateActor(string actorId);

    /// <summary>
    ///     Creates an actor and delivers one request part to the entry spot create
    ///     lifecycle callback.
    /// </summary>
    IActor CreateActor(string actorId, Message request);

    /// <summary>
    ///     Creates an actor and delivers request parts to the entry spot create
    ///     lifecycle callback.
    /// </summary>
    IActor CreateActor(string actorId, IReadOnlyList<Message> requestParts);

    /// <summary>
    ///     Looks up an actor by actor id.
    /// </summary>
    ActorRef ActorLookup(string actorId);

    /// <summary>
    ///     Begins a send to the bound session of <paramref name="actor" />; parts
    ///     are consumed on a successful submit (see <see cref="SendOperation" />).
    /// </summary>
    SendOperation SendActorBoundSession(ActorRef actor);

    /// <summary>
    ///     Begins a send to a resolved actor reference. Completion acknowledges
    ///     handoff to the actor owner's mailbox.
    /// </summary>
    SendOperation SendToActor(ActorRef actor);

    /// <summary>
    ///     Begins a request to a resolved actor reference. The callback or task
    ///     receives the actor handler reply parts.
    /// </summary>
    RequestOperation RequestToActor(ActorRef actor);

    /// <summary>
    ///     Replies to a no-bind actor request described by <paramref name="info" />.
    ///     The reply parts are consumed on a successful submit.
    /// </summary>
    void ReplyActorNoBind(
        ActorRecvInfo info,
        IReadOnlyList<Message> parts,
        RequestResult result = RequestResult.Ok);

    /// <summary>
    ///     Forwards one stream session part to a remote actor while preserving the
    ///     original session owner and session routing ids.
    /// </summary>
    bool ForwardActorBoundSessionPart(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags = SendFlags.None);

    /// <summary>
    ///     Binds <paramref name="actor" /> to a session owned by a remote SPOT node.
    /// </summary>
    void BindRemoteActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid);

    /// <summary>
    ///     Closes the bound stream session for <paramref name="actor" />.
    /// </summary>
    void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default);

    /// <summary>
    ///     Gets a remote actor reference.
    /// </summary>
    ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId);

    /// <summary>
    ///     Starts an actor destroy operation.
    /// </summary>
    ActorDestroyOperation DestroyActor(ActorRef actor);

    /// <summary>
    ///     Starts a join operation.
    /// </summary>
    ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid);

    /// <summary>
    ///     Starts a join operation.
    /// </summary>
    ActorJoinEntrySpotOperation JoinActorEntrySpot(ActorRef actor,
        RoutingId destNodeRid, Message request);

    /// <summary>
    ///     Starts a leave operation.
    /// </summary>
    ActorLeaveOperation LeaveActor(ActorRef actor, RoutingId currentSpotRid);
}
