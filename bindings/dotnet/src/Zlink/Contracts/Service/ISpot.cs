// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The kind of a spot. Maps to <c>zlink_spot_kind_t</c>.
/// </summary>
public enum SpotKind
{
    /// <summary>Unset.</summary>
    Invalid = 0,

    /// <summary>The node's entry spot.</summary>
    Entry = 1,

    /// <summary>A user-created spot.</summary>
    User = 2,

    /// <summary>A Framework-managed, address-activated spot.</summary>
    Instance = 3
}

/// <summary>
///     The activation state of a spot. Non-instance spots report
///     <see cref="Invalid" />.
/// </summary>
public enum SpotActivationState
{
    /// <summary>The spot does not use the Instance Spot activation barrier.</summary>
    Invalid = 0,

    /// <summary>The owner is being selected and initialized.</summary>
    Activating = 1,

    /// <summary>The owner accepts application messages and timer ticks.</summary>
    Ready = 2,

    /// <summary>The owner has stopped accepting new application work.</summary>
    Closing = 3
}

/// <summary>
///     How a spot subscription topic filter is matched. Maps to
///     <c>zlink_spot_subscription_kind_t</c>.
/// </summary>
public enum SpotSubscriptionKind
{
    /// <summary>Exact topic match.</summary>
    Exact = 1,

    /// <summary>Topic prefix match.</summary>
    Prefix = 2
}

/// <summary>
///     Detail of a publish fan-out. Maps to
///     <c>zlink_mesh_publish_detail_t</c>.
/// </summary>
/// <param name="SnapshotRemoteTargets">Remote targets in the snapshot.</param>
/// <param name="AdmittedRemoteTargets">Remote targets that accepted.</param>
/// <param name="DroppedRemoteTargets">Remote targets that dropped.</param>
/// <param name="UnreachableRemoteTargets">Remote targets unreachable.</param>
/// <param name="SnapshotLocalSpots">Local spots in the snapshot.</param>
/// <param name="AdmittedLocalSpots">Local spots that accepted.</param>
/// <param name="DroppedLocalSpots">Local spots that dropped.</param>
public sealed record MeshPublishDetail(
    uint SnapshotRemoteTargets,
    uint AdmittedRemoteTargets,
    uint DroppedRemoteTargets,
    uint UnreachableRemoteTargets,
    uint SnapshotLocalSpots,
    uint AdmittedLocalSpots,
    uint DroppedLocalSpots);

/// <summary>
///     The terminal result and target detail from one Logical Multicast
///     operation. Core provides the detail for both successful and
///     back-pressured partial admission.
/// </summary>
public sealed record MeshPublishResult(
    SubmitResult Result,
    MeshPublishDetail Detail);

/// <summary>
///     A snapshot of a spot's status. Maps to <c>zlink_spot_status_t</c>.
/// </summary>
/// <param name="SpotRid">The spot routing id.</param>
/// <param name="SpotKind">The kind of spot.</param>
/// <param name="LifecycleGeneration">The spot lifecycle generation.</param>
/// <param name="PendingApplicationMessages">Queued application messages.</param>
/// <param name="PendingInfrastructureMessages">Queued infrastructure messages.</param>
/// <param name="PendingBytes">Queued bytes.</param>
/// <param name="ActiveActorCount">The number of active actors.</param>
/// <param name="Draining">Whether the spot is draining.</param>
/// <param name="LastError">The last native error code, or 0.</param>
/// <param name="LastChangedMs">When the spot last changed, in milliseconds.</param>
/// <param name="ActivationState">The Instance Spot activation state.</param>
public sealed record SpotStatus(
    RoutingId SpotRid,
    SpotKind SpotKind,
    ulong LifecycleGeneration,
    ulong PendingApplicationMessages,
    ulong PendingInfrastructureMessages,
    ulong PendingBytes,
    uint ActiveActorCount,
    bool Draining,
    int LastError,
    ulong LastChangedMs,
    SpotActivationState ActivationState);

/// <summary>
///     A spot: a logical destination that can send, request, publish, subscribe,
///     and host actors on a MeshNode.
/// </summary>
public interface ISpot : IZlinkSocket, IDisposable, IAsyncDisposable
{
    /// <summary>Gets the routing id that identifies this spot.</summary>
    RoutingId RoutingId { get; }

    /// <summary>Reads the current spot status.</summary>
    SpotStatus Status();

    /// <summary>
    ///     Sends parts to a channel, optionally attaching immutable outbound
    ///     application metadata. Parts are consumed on success.
    /// </summary>
    SubmitResult SendToChannel(string channelName, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to a channel. Parts are consumed on success.</summary>
    SubmitResult RequestToChannel(string channelName,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Sends parts to a spot on another node.</summary>
    SubmitResult SendToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
        ulong targetSpotGeneration, IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to a spot on another node.</summary>
    SubmitResult RequestToSpot(RoutingId targetNodeRid, RoutingId targetSpotRid,
        ulong targetSpotGeneration, IReadOnlyList<Message> parts,
        out MeshOperationId operationId, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>
    ///     Publishes parts under a channel/topic (logical multicast), optionally
    ///     attaching immutable outbound application metadata.
    /// </summary>
    MeshPublishResult Publish(string channelName, string? topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Adds a subscription for a channel topic filter.</summary>
    void SetSubscription(string channelName, string topicFilter,
        SpotSubscriptionKind kind = SpotSubscriptionKind.Exact);

    /// <summary>Removes a subscription for a channel topic filter.</summary>
    void UnsetSubscription(string channelName, string topicFilter,
        SpotSubscriptionKind kind = SpotSubscriptionKind.Exact);

    /// <summary>Closes the spot and releases its resources.</summary>
    void Close();
}
