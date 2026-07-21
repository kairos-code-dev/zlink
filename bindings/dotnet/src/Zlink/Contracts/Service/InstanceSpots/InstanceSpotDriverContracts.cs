// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink;

namespace Zlink.Runtime.Service.InstanceSpots;

/// <summary>The operation held behind an Instance Spot activation barrier.</summary>
public enum InstanceSpotOperationKind
{
    /// <summary>A one-way send.</summary>
    Send = 1,

    /// <summary>A request/reply operation.</summary>
    Request = 2
}

/// <summary>
///     Identifies the eligible MeshNode and logical Instance Spot for a cold
///     placement operation.
/// </summary>
public readonly record struct InstanceSpotPlacement(
    RoutingId NodeRid,
    ulong NodeGeneration,
    RoutingId SpotRid,
    string InstanceSpotType,
    string MessageContractId);

/// <summary>
///     Describes the activation record delivered by Core. Authority and
///     generation values remain owned by the location store and activation
///     object.
/// </summary>
public sealed record InstanceSpotActivationData(
    RoutingId SpotRid,
    InstanceSpotOperationKind OperationKind,
    string InstanceSpotType,
    string MessageContractId);

/// <summary>The result of claiming one local activation token.</summary>
public abstract record InstanceSpotClaimResult
{
    private InstanceSpotClaimResult()
    {
    }

    /// <summary>The caller must create and initialize the local owner.</summary>
    public sealed record Leader(
        IInstanceSpotOwnerAdmission Admission,
        ulong SpotGeneration) : InstanceSpotClaimResult;

    /// <summary>Another token already leads the same local activation.</summary>
    public sealed record Follower : InstanceSpotClaimResult;
}

/// <summary>An exact existing Spot route used after location resolution.</summary>
public readonly record struct ResolvedSpotRoute(
    RoutingId NodeRid,
    RoutingId SpotRid,
    ulong SpotGeneration);

/// <summary>Request terminal values accepted by activation abort.</summary>
public enum ZLinkRequestResult
{
    /// <summary>The request deadline elapsed.</summary>
    TimedOut = 101,
    /// <summary>The target was not found.</summary>
    NotFound = 102,
    /// <summary>The context terminated the request.</summary>
    Terminated = 103,
    /// <summary>The request/reply protocol failed.</summary>
    ProtocolError = 104,
    /// <summary>An internal error terminated the request.</summary>
    InternalError = 105,
    /// <summary>The activation was rejected.</summary>
    Rejected = 106,
    /// <summary>The requested identity conflicts with an existing owner.</summary>
    Conflict = 107,
    /// <summary>The owner is closing or otherwise busy.</summary>
    Busy = 108,
    /// <summary>The selected node is not connected.</summary>
    NotConnected = 109,
    /// <summary>An activation argument was invalid.</summary>
    InvalidArgument = 110,
    /// <summary>The activation state does not permit the operation.</summary>
    InvalidState = 111,
    /// <summary>The target does not support the operation.</summary>
    NotSupported = 112,
    /// <summary>The bounded activation budget was exhausted.</summary>
    Backpressured = 113
}

/// <summary>
///     Owns one opaque Core activation token and permits exactly one valid
///     transition sequence.
/// </summary>
public interface IInstanceSpotActivation
{
    /// <summary>Gets the message-free activation metadata.</summary>
    InstanceSpotActivationData Data { get; }

    /// <summary>Claims local ownership using the store-issued owner id.</summary>
    InstanceSpotClaimResult ClaimOwner(string locationOwnerId);

    /// <summary>Opens the Core barrier after the location is committed Ready.</summary>
    void MarkReady(TimeSpan ownerLease);

    /// <summary>Forwards a losing placement to the resolved exact owner.</summary>
    void Redirect(ResolvedSpotRoute owner);

    /// <summary>Terminates the pending activation with a request failure.</summary>
    void Abort(ZLinkRequestResult result, int error);
}

/// <summary>
///     Controls admission for the exact local owner returned by a leader claim.
/// </summary>
public interface IInstanceSpotOwnerAdmission
{
    /// <summary>Extends the local monotonic admission deadline.</summary>
    void Renew(TimeSpan ownerLease);

    /// <summary>Stops new application and timer admission before Store closing.</summary>
    void BeginClose();
}

/// <summary>
///     Framework-only facade over cold placement sends and requests. It keeps
///     the ordinary <see cref="ISpot" /> interface free of placement details.
/// </summary>
public static class InstanceSpotDriver
{
    /// <summary>
    ///     Submits a cold one-way placement from the source Entry Spot.
    /// </summary>
    public static SubmitResult SendToPlacement(
        ISpot source,
        InstanceSpotPlacement placement,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        return InstanceSpotDriverRuntime.SendToPlacement(
            source, placement, parts, flags, metadata);
    }

    /// <summary>
    ///     Submits a cold request placement from the source Entry Spot.
    /// </summary>
    public static SubmitResult RequestToPlacement(
        ISpot source,
        InstanceSpotPlacement placement,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        return InstanceSpotDriverRuntime.RequestToPlacement(
            source, placement, parts, out operationId, timeout, flags,
            metadata);
    }
}
