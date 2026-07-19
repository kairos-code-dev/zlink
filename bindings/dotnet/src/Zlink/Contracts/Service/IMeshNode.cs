// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     A MeshNode: RouteMesh membership, peer connections, node/channel/spot/actor
///     messaging, and the pull dispatch surface. Maps to
///     <c>zlink_mesh_node_*</c>.
/// </summary>
public interface IMeshNode : IDisposable, IAsyncDisposable
{
    /// <summary>Gets the node routing id (available after start).</summary>
    RoutingId RoutingId { get; }

    /// <summary>
    ///     Sets the node routing id. Required before <see cref="Start" /> along
    ///     with a bind endpoint and at least one channel. Maps to
    ///     <c>zlink_set_routing_id</c>.
    /// </summary>
    void SetRoutingId(RoutingId routingId);

    /// <summary>Sets the node bind endpoint. Apply before start.</summary>
    void SetBind(string endpoint);

    /// <summary>Starts the node.</summary>
    void Start();

    /// <summary>Shuts the node down, draining up to <paramref name="timeout" />.</summary>
    void Shutdown(TimeSpan timeout = default);

    /// <summary>Adds a named channel.</summary>
    void AddChannel(string channelName);

    /// <summary>Sets a channel's load-balancing weight.</summary>
    void SetChannelWeight(string channelName, uint weight);

    /// <summary>
    ///     Connects to a peer endpoint, optionally pinned to an expected routing
    ///     id. Returns the connection intent id.
    /// </summary>
    ulong ConnectPeer(string endpoint, RoutingId? expectedRid = null);

    /// <summary>Removes a peer connection intent.</summary>
    void RemovePeerConnection(ulong connectionIntentId);

    /// <summary>Disconnects an admitted peer.</summary>
    void DisconnectPeer(RoutingId peerRid, ulong lifecycleGeneration = 0);

    /// <summary>
    ///     Sends parts to a node, optionally attaching immutable outbound
    ///     application metadata. Parts are consumed on success.
    /// </summary>
    SubmitResult SendToNode(RoutingId targetRid, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to a node. Parts are consumed on success.</summary>
    SubmitResult RequestToNode(RoutingId targetRid, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Sends parts to a channel.</summary>
    SubmitResult SendToChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to a channel.</summary>
    SubmitResult RequestToChannel(string channelName,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Creates a channel-topic publisher.</summary>
    IMeshNodePublisher CreatePublisher();

    /// <summary>Reads the current node status.</summary>
    MeshNodeStatus Status();

    /// <summary>Lists the node's peers.</summary>
    MeshNodePeer[] Peers();

    /// <summary>
    ///     Opens the push monitor for RouteMesh lifecycle and messaging events.
    /// </summary>
    IMeshNodeMonitor OpenMonitor(
        MeshMonitorEventMask events = MeshMonitorEventMask.All);

    /// <summary>
    ///     Registers the ready handler invoked when readable traffic appears.
    /// </summary>
    void SetReadyHandler(MeshReadyHandler handler);

    /// <summary>
    ///     Drains the ready index into <paramref name="batch" />. Returns true when
    ///     more ready entries remain (residue) after this drain.
    /// </summary>
    bool DrainReady(MeshReadyDomains domains, MeshReadyBatch batch,
        RecvFlags flags = RecvFlags.None);

    /// <summary>Creates a user spot hosted by this node.</summary>
    ISpot CreateSpot();

    /// <summary>Gets the node's entry spot.</summary>
    ISpot EntrySpot();

    /// <summary>Looks up a spot by routing id, or null when absent.</summary>
    ISpot? SpotLookup(RoutingId spotRid);

    /// <summary>Gets an existing spot or creates one for the routing id.</summary>
    ISpot GetOrCreateSpot(RoutingId spotRid, out bool created);

    /// <summary>
    ///     Creates an actor hosted by this node, delivering optional creation parts
    ///     to the entry spot create lifecycle.
    /// </summary>
    ActorRef CreateActor(string actorId,
        IReadOnlyList<Message>? creationParts = null,
        TimeSpan timeout = default);

    /// <summary>Looks up a local actor, returning its location.</summary>
    bool ActorLookup(string actorId, out ActorLocation location);

    /// <summary>Starts a remote actor lookup; returns the operation id.</summary>
    MeshOperationId ActorLookupRemote(RoutingId targetNodeRid, string actorId,
        TimeSpan timeout = default);

    /// <summary>Starts an actor destroy; returns the operation id.</summary>
    MeshOperationId DestroyActor(ActorRef actor, TimeSpan timeout = default);

    /// <summary>Starts joining an actor to a spot; returns the operation id.</summary>
    MeshOperationId JoinSpot(ActorRef actor, RoutingId targetNodeRid,
        RoutingId targetSpotRid, ulong targetSpotGeneration,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default);

    /// <summary>Starts joining an actor to an entry spot; returns the operation id.</summary>
    MeshOperationId JoinEntrySpot(ActorRef actor, RoutingId targetNodeRid,
        IReadOnlyList<Message>? creationParts = null, TimeSpan timeout = default);

    /// <summary>Starts an actor leave; returns the operation id.</summary>
    MeshOperationId LeaveSpot(ActorRef actor, ulong expectedMembershipEpoch,
        TimeSpan timeout = default);

    /// <summary>Sends parts to an actor, optionally attaching actor metadata.</summary>
    SubmitResult SendToActor(ActorRef actor, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to an actor.</summary>
    SubmitResult RequestToActor(ActorRef actor, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Sends parts to an actor's bound STREAM session.</summary>
    SubmitResult SendBoundSession(ActorRef actor, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None);

    /// <summary>Closes an actor's bound STREAM session; returns the operation id.</summary>
    MeshOperationId CloseBoundSession(ActorRef actor,
        ulong expectedBindingGeneration, TimeSpan timeout = default);

    /// <summary>
    ///     Prepares an actor transfer fence, returning the framework-owned token
    ///     and the fenced reservation result. Maps to
    ///     <c>zlink_mesh_node_actor_transfer_prepare</c>.
    /// </summary>
    ActorTransferToken PrepareActorTransfer(ActorTransferPrepare prepare,
        out ActorTransferPrepareResult result, TimeSpan timeout = default);

    /// <summary>
    ///     Commits a prepared actor transfer to <paramref name="newMembershipEpoch" />.
    ///     Maps to <c>zlink_mesh_node_actor_transfer_commit</c>.
    /// </summary>
    void CommitActorTransfer(ActorTransferToken token, ulong newMembershipEpoch);

    /// <summary>
    ///     Activates a committed actor transfer. Maps to
    ///     <c>zlink_mesh_node_actor_transfer_activate</c>.
    /// </summary>
    void ActivateActorTransfer(ActorTransferToken token);

    /// <summary>
    ///     Aborts a prepared or committed actor transfer, releasing the fence.
    ///     Maps to <c>zlink_mesh_node_actor_transfer_abort</c>.
    /// </summary>
    void AbortActorTransfer(ActorTransferToken token);

    /// <summary>Creates a STREAM session service bound to a STREAM socket.</summary>
    IStreamSessionService CreateStreamSessionService(IStreamSocket stream);

    /// <summary>Closes the node and releases its resources.</summary>
    void Close();
}
