// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The lifecycle state of a STREAM session service. Maps to
///     <c>zlink_stream_session_state_t</c>.
/// </summary>
public enum StreamSessionState
{
    /// <summary>Created but not started.</summary>
    Created = 1,

    /// <summary>Started.</summary>
    Started = 2,

    /// <summary>Draining.</summary>
    Draining = 3,

    /// <summary>Stopped.</summary>
    Stopped = 4,

    /// <summary>In an error state.</summary>
    Error = 5
}

/// <summary>
///     A session-to-actor binding. Maps to
///     <c>zlink_stream_session_binding_t</c>.
/// </summary>
/// <param name="SessionRid">The bound session routing id.</param>
/// <param name="Actor">The bound actor.</param>
/// <param name="BindingGeneration">The binding generation.</param>
/// <param name="MembershipEpoch">The actor membership epoch.</param>
public sealed record StreamSessionBinding(
    RoutingId SessionRid,
    ActorRef Actor,
    ulong BindingGeneration,
    ulong MembershipEpoch);

/// <summary>
///     A snapshot of a STREAM session service. Maps to
///     <c>zlink_stream_session_status_t</c>.
/// </summary>
/// <param name="State">The service lifecycle state.</param>
/// <param name="LifecycleGeneration">The lifecycle generation.</param>
/// <param name="SessionCount">The number of tracked sessions.</param>
/// <param name="BindingCount">The number of session-actor bindings.</param>
/// <param name="PendingMessageCount">Queued messages.</param>
/// <param name="PendingByteCount">Queued bytes.</param>
/// <param name="LastError">The last native error code, or 0.</param>
public sealed record StreamSessionStatus(
    StreamSessionState State,
    ulong LifecycleGeneration,
    ulong SessionCount,
    ulong BindingCount,
    ulong PendingMessageCount,
    ulong PendingByteCount,
    int LastError);

/// <summary>
///     The STREAM session service: binds STREAM sessions to actors and relays
///     to them. Maps to <c>zlink_stream_session_service_*</c>.
/// </summary>
public interface IStreamSessionService : IDisposable, IAsyncDisposable
{
    /// <summary>Starts the service.</summary>
    void Start();

    /// <summary>Shuts the service down, draining up to <paramref name="timeout" />.</summary>
    void Shutdown(TimeSpan timeout = default);

    /// <summary>Reads the current service status.</summary>
    StreamSessionStatus Status();

    /// <summary>Binds a session to an actor.</summary>
    SubmitResult BindActor(RoutingId sessionRid, ActorRef actor,
        out MeshOperationId operationId, TimeSpan timeout = default);

    /// <summary>Unbinds a session from an actor.</summary>
    SubmitResult UnbindActor(RoutingId sessionRid, ActorRef actor,
        ulong expectedBindingGeneration, out MeshOperationId operationId,
        TimeSpan timeout = default);

    /// <summary>Lists the bindings for a session.</summary>
    StreamSessionBinding[] Bindings(RoutingId sessionRid);

    /// <summary>Sends parts to a session-bound actor, optionally with metadata.</summary>
    SubmitResult SendToActor(RoutingId sessionRid, ActorRef actor,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Requests to a session-bound actor.</summary>
    SubmitResult RequestToActor(RoutingId sessionRid, ActorRef actor,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Closes the service and releases its resources.</summary>
    void Close();
}

/// <summary>
///     Publishes into a MeshNode channel topic plane. Maps to
///     <c>zlink_mesh_node_publisher_*</c>.
/// </summary>
public interface IMeshNodePublisher : IDisposable, IAsyncDisposable
{
    /// <summary>Publishes parts under a channel/topic.</summary>
    MeshPublishDetail Publish(string channelName, string? topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default);

    /// <summary>Closes the publisher.</summary>
    void Close();
}
