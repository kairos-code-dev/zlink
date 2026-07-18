// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Spot : ISpot
{
    private readonly MeshNode _owner;

    internal Spot(MeshNode owner, IntPtr handle)
    {
        _owner = owner;
        Handle = handle;
    }

    internal IntPtr Handle { get; private set; }

    public RoutingId RoutingId => Status().SpotRid;

    public SpotStatus Status()
    {
        EnsureNotDisposed();
        var native = new ZlinkSpotStatus
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkSpotStatus>(),
            Version = 1
        };
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_spot_status(Handle, ref native));
        return new SpotStatus(
            RoutingId.FromNative(ref native.SpotRid) ?? default,
            (SpotKind)native.SpotKind,
            native.LifecycleGeneration,
            native.PendingApplicationMessages,
            native.PendingInfrastructureMessages,
            native.PendingBytes,
            native.ActiveActorCount,
            native.Draining != 0,
            native.LastError,
            native.LastChangedMs);
    }

    public SubmitResult SendToChannel(string channelName,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        return MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_spot_send_to_channel(Handle, channelName,
                    meta, np, count, (int)flags));
    }

    public SubmitResult RequestToChannel(string channelName,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_spot_request_to_channel(Handle, channelName,
                    meta, np, count, out op, (int)flags, timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
    }

    public SubmitResult SendToSpot(RoutingId targetNodeRid,
        RoutingId targetSpotRid, ulong targetSpotGeneration,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeNode = targetNodeRid.ToNative();
        var nativeSpot = targetSpotRid.ToNative();
        return MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_spot_send_to_spot(Handle, ref nativeNode,
                    ref nativeSpot, targetSpotGeneration, meta, np, count,
                    (int)flags));
    }

    public SubmitResult RequestToSpot(RoutingId targetNodeRid,
        RoutingId targetSpotRid, ulong targetSpotGeneration,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeNode = targetNodeRid.ToNative();
        var nativeSpot = targetSpotRid.ToNative();
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_spot_request_to_spot(Handle, ref nativeNode,
                    ref nativeSpot, targetSpotGeneration, meta, np, count,
                    out op, (int)flags, timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
    }

    public MeshPublishDetail Publish(string channelName, string? topic,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        var detailPtr =
            Marshal.AllocHGlobal(Marshal.SizeOf<ZlinkMeshPublishDetail>());
        try
        {
            var seed = new ZlinkMeshPublishDetail
            {
                StructSize = (uint)Marshal.SizeOf<ZlinkMeshPublishDetail>(),
                Version = 1
            };
            Marshal.StructureToPtr(seed, detailPtr, false);
            var result = MeshSend.SubmitWithMetadata(parts, nameof(parts),
                metadata, (np, count, meta) =>
                    NativeMethods.zlink_spot_publish(Handle, channelName, topic,
                        meta, np, count, detailPtr, (int)flags));
            ZlinkException.ThrowSubmitIfError((int)result);
            var detail =
                Marshal.PtrToStructure<ZlinkMeshPublishDetail>(detailPtr);
            return new MeshPublishDetail(
                detail.SnapshotRemoteTargetCount,
                detail.AdmittedRemoteTargetCount,
                detail.DroppedRemoteTargetCount,
                detail.UnreachableRemoteTargetCount,
                detail.SnapshotLocalSpotCount,
                detail.AdmittedLocalSpotCount,
                detail.DroppedLocalSpotCount);
        }
        finally
        {
            Marshal.FreeHGlobal(detailPtr);
        }
    }

    public unsafe void SetNoDrop(bool noDrop)
    {
        EnsureNotDisposed();
        var value = noDrop ? 1 : 0;
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_spot_set_publish_option(Handle,
                MeshNodePublisher.OptNoDrop, (IntPtr)(&value), sizeof(int)));
    }

    public unsafe bool GetNoDrop()
    {
        EnsureNotDisposed();
        var value = 0;
        nuint length = sizeof(int);
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_spot_get_publish_option(Handle,
                MeshNodePublisher.OptNoDrop, (IntPtr)(&value), ref length));
        return value != 0;
    }

    public void SetSubscription(string channelName, string topicFilter,
        SpotSubscriptionKind kind = SpotSubscriptionKind.Exact)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_spot_set_subscription(Handle, channelName,
                topicFilter, (int)kind));
    }

    public void UnsetSubscription(string channelName, string topicFilter,
        SpotSubscriptionKind kind = SpotSubscriptionKind.Exact)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_spot_unset_subscription(Handle, channelName,
                topicFilter, (int)kind));
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (Handle == IntPtr.Zero)
            return;
        Destroy(true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Spot()
    {
        Destroy(false);
    }

    private void EnsureNotDisposed()
    {
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;
        _owner.UnregisterSpot(this);
        var handle = Handle;
        var rc = NativeMethods.zlink_spot_destroy(ref handle);
        if (rc == 0)
        {
            Handle = IntPtr.Zero;
        }
        else if (throwOnError)
        {
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        }
    }
}
