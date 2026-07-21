// SPDX-License-Identifier: MPL-2.0

using System.Text;
using Systems.Zlink;
using Systems.Zlink.Runtime.Native;

namespace Zlink.Runtime.Service.InstanceSpots;

internal static class InstanceSpotDriverRuntime
{
    internal static SubmitResult SendToPlacement(
        ISpot source,
        InstanceSpotPlacement placement,
        IReadOnlyList<Message> parts,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        var spot = GetSpot(source);
        return WithPlacement(placement, native =>
            MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
                (nativeParts, partCount, nativeMetadata) =>
                    NativeMethods.zlink_spot_send_to_instance_placement(
                        spot.Handle, native, nativeMetadata, nativeParts,
                        partCount, (int)flags)));
    }

    internal static SubmitResult RequestToPlacement(
        ISpot source,
        InstanceSpotPlacement placement,
        IReadOnlyList<Message> parts,
        out MeshOperationId operationId,
        TimeSpan timeout,
        SendFlags flags,
        ReadOnlyMemory<byte> metadata)
    {
        var spot = GetSpot(source);
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId nativeOperationId = default;
        var result = WithPlacement(placement, native =>
            MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
                (nativeParts, partCount, nativeMetadata) =>
                    NativeMethods.zlink_spot_request_to_instance_placement(
                        spot.Handle, native, nativeMetadata, nativeParts,
                        partCount, out nativeOperationId, (int)flags,
                        timeoutMs)));
        operationId = new MeshOperationId(
            nativeOperationId.High,
            nativeOperationId.Low);
        return result;
    }

    private static Spot GetSpot(ISpot source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return source as Spot ?? throw new ArgumentException(
            "The source must be a Spot created by this bindings package.",
            nameof(source));
    }

    private static unsafe TResult WithPlacement<TResult>(
        InstanceSpotPlacement placement,
        NativePlacementCall<TResult> call)
    {
        if (placement.NodeRid.IsEmpty)
            throw new ArgumentException("NodeRid must not be empty.",
                nameof(placement));
        if (placement.NodeGeneration == 0)
            throw new ArgumentException("NodeGeneration must be greater than zero.",
                nameof(placement));
        if (placement.SpotRid.IsEmpty)
            throw new ArgumentException("SpotRid must not be empty.",
                nameof(placement));
        BoundaryValidation.ValidateFixedUtf8(
            placement.InstanceSpotType,
            nameof(placement.InstanceSpotType));
        BoundaryValidation.ValidateFixedUtf8(
            placement.MessageContractId,
            nameof(placement.MessageContractId));

        var instanceSpotType = Encoding.UTF8.GetBytes(placement.InstanceSpotType);
        var messageContractId = Encoding.UTF8.GetBytes(placement.MessageContractId);
        fixed (byte* instanceSpotTypePointer = instanceSpotType)
        fixed (byte* messageContractIdPointer = messageContractId)
        {
            var native = new ZlinkInstanceSpotPlacement
            {
                NodeRid = placement.NodeRid.ToNative(),
                NodeGeneration = placement.NodeGeneration,
                SpotRid = placement.SpotRid.ToNative(),
                InstanceSpotType = (IntPtr)instanceSpotTypePointer,
                InstanceSpotTypeSize = (nuint)instanceSpotType.Length,
                MessageContractId = (IntPtr)messageContractIdPointer,
                MessageContractIdSize = (nuint)messageContractId.Length
            };
            return call((IntPtr)(&native));
        }
    }

    private delegate TResult NativePlacementCall<TResult>(IntPtr placement);
}

internal sealed record InstanceSpotActivation : MeshRecordPayload,
    IInstanceSpotActivation
{
    private const int LeaderRole = 1;
    private const int FollowerRole = 2;
    private readonly object _sync = new();
    private ZlinkInstanceSpotActivationToken _token;
    private bool _claimedLeader;
    private bool _consumed;

    internal InstanceSpotActivation(
        InstanceSpotActivationData data,
        ZlinkInstanceSpotActivationToken token)
    {
        Data = data;
        _token = token;
    }

    public InstanceSpotActivationData Data { get; }

    public InstanceSpotClaimResult ClaimOwner(string locationOwnerId)
    {
        BoundaryValidation.ValidateFixedUtf8(
            locationOwnerId,
            nameof(locationOwnerId));
        var ownerBytes = Encoding.UTF8.GetBytes(locationOwnerId);

        lock (_sync)
        {
            EnsurePlacementToken();
            var rc = NativeMethods.zlink_instance_spot_activation_claim_owner(
                ref _token,
                ownerBytes,
                (nuint)ownerBytes.Length,
                out var result);
            ZlinkException.ThrowConfigIfError(rc);

            if (result.Role == FollowerRole)
            {
                _consumed = true;
                return new InstanceSpotClaimResult.Follower();
            }

            if (result.Role != LeaderRole
                || result.LeaderSpot == IntPtr.Zero
                || result.LeaderSpotGeneration == 0)
                throw new InvalidOperationException(
                    "Core returned an invalid Instance Spot leader claim.");

            _claimedLeader = true;
            return new InstanceSpotClaimResult.Leader(
                new InstanceSpotOwnerAdmission(result.LeaderSpot),
                result.LeaderSpotGeneration);
        }
    }

    public void MarkReady(TimeSpan ownerLease)
    {
        var leaseMs = EncodePositiveDuration(ownerLease, nameof(ownerLease));
        lock (_sync)
        {
            EnsureLeaderToken();
            var rc = NativeMethods.zlink_instance_spot_activation_mark_ready(
                ref _token,
                leaseMs);
            ZlinkException.ThrowConfigIfError(rc);
            _consumed = true;
        }
    }

    public void Redirect(ResolvedSpotRoute owner)
    {
        if (owner.NodeRid.IsEmpty)
            throw new ArgumentException("NodeRid must not be empty.",
                nameof(owner));
        if (owner.SpotRid.IsEmpty)
            throw new ArgumentException("SpotRid must not be empty.",
                nameof(owner));
        if (owner.SpotGeneration == 0)
            throw new ArgumentException(
                "SpotGeneration must be greater than zero.",
                nameof(owner));

        lock (_sync)
        {
            EnsurePlacementToken();
            var nodeRid = owner.NodeRid.ToNative();
            var spotRid = owner.SpotRid.ToNative();
            var rc = NativeMethods.zlink_instance_spot_activation_redirect(
                ref _token,
                ref nodeRid,
                ref spotRid,
                owner.SpotGeneration);
            ZlinkException.ThrowConfigIfError(rc);
            _consumed = true;
        }
    }

    public void Abort(ZLinkRequestResult result, int error)
    {
        if (!Enum.IsDefined(result))
            throw new ArgumentOutOfRangeException(nameof(result));

        lock (_sync)
        {
            EnsureUsableToken();
            var rc = NativeMethods.zlink_instance_spot_activation_abort(
                ref _token,
                (int)result,
                error);
            ZlinkException.ThrowConfigIfError(rc);
            _consumed = true;
        }
    }

    internal static unsafe InstanceSpotActivation FromNative(
        ref ZlinkInstanceSpotActivationData native)
    {
        var spotRid = RoutingId.FromNative(ref native.SpotRid) ?? default;
        string instanceSpotType;
        string messageContractId;
        fixed (byte* type = native.InstanceSpotType)
        fixed (byte* contract = native.MessageContractId)
        {
            instanceSpotType = NativeHelpers.ReadFixedString(type, 256);
            messageContractId = NativeHelpers.ReadFixedString(contract, 256);
        }

        return new InstanceSpotActivation(
            new InstanceSpotActivationData(
                spotRid,
                (InstanceSpotOperationKind)native.OperationKind,
                instanceSpotType,
                messageContractId),
            native.Token);
    }

    private void EnsurePlacementToken()
    {
        EnsureUsableToken();
        if (_claimedLeader)
            throw new InvalidOperationException(
                "The activation token has already been claimed by the leader.");
    }

    private void EnsureLeaderToken()
    {
        EnsureUsableToken();
        if (!_claimedLeader)
            throw new InvalidOperationException(
                "Only a leader claim can mark an activation ready.");
    }

    private void EnsureUsableToken()
    {
        if (_consumed)
            throw new InvalidOperationException(
                "The activation token has already been consumed.");
    }

    internal static uint EncodePositiveDuration(TimeSpan value, string name)
    {
        var milliseconds = value.TotalMilliseconds;
        if (double.IsNaN(milliseconds)
            || double.IsInfinity(milliseconds)
            || milliseconds <= 0
            || milliseconds > uint.MaxValue)
            throw new ArgumentOutOfRangeException(name);
        return (uint)Math.Ceiling(milliseconds);
    }
}

internal sealed class InstanceSpotOwnerAdmission : IInstanceSpotOwnerAdmission
{
    private readonly object _sync = new();
    private readonly IntPtr _spot;
    private bool _closing;

    internal InstanceSpotOwnerAdmission(IntPtr spot)
    {
        _spot = spot != IntPtr.Zero
            ? spot
            : throw new ArgumentException("Leader spot handle must not be null.",
                nameof(spot));
    }

    public void Renew(TimeSpan ownerLease)
    {
        var leaseMs = InstanceSpotActivation.EncodePositiveDuration(
            ownerLease,
            nameof(ownerLease));
        lock (_sync)
        {
            if (_closing)
                throw new InvalidOperationException(
                    "Owner admission is already closing.");
            ZlinkException.ThrowConfigIfError(
                NativeMethods.zlink_instance_spot_renew_owner_admission(
                    _spot,
                    leaseMs));
        }
    }

    public void BeginClose()
    {
        lock (_sync)
        {
            if (_closing)
                return;
            ZlinkException.ThrowConfigIfError(
                NativeMethods.zlink_instance_spot_begin_close(_spot));
            _closing = true;
        }
    }
}
