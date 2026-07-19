// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class MeshNodeMonitor : IMeshNodeMonitor
{
    private IntPtr _handle;

    internal MeshNodeMonitor(MeshNode owner, MeshMonitorEventMask events)
    {
        var options = new ZlinkMeshMonitorOpenOptions
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkMeshMonitorOpenOptions>(),
            Version = 1,
            Events = (ulong)events
        };
        _handle = NativeMethods.zlink_mesh_node_monitor_open(owner.Handle,
            ref options);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public unsafe MeshMonitorEvent? Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        ZlinkMeshMonitorEvent native = new()
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkMeshMonitorEvent>(),
            Version = 1
        };
        var rc = NativeMethods.zlink_mesh_node_monitor_recv(_handle, ref native,
            (int)flags);
        if (rc == (int)RecvResult.NoData
            && (flags & RecvFlags.DontWait) != 0)
            return null;
        if (rc != (int)RecvResult.Ok)
            throw ZlinkException.CreateRecvException((RecvResult)rc);

        var peerRid = RoutingId.FromNative(ref native.PeerRid) ?? default;
        var spotRid = RoutingId.FromNative(ref native.SpotRid) ?? default;
        var actor = ActorInterop.FromNative(ref native.Actor);
        string channel;
        byte* bytes = native.ChannelName;
        channel = NativeHelpers.ReadFixedString(bytes, 256);
        return new MeshMonitorEvent(
            (MeshMonitorEventKind)native.Kind,
            native.TimestampMs,
            native.MeshLifecycleGeneration,
            native.MeshDescriptorRevision,
            (MeshNodeState)native.MeshState,
            peerRid,
            native.PeerLifecycleGeneration,
            native.PeerDescriptorRevision,
            (MeshOwnerKind)native.OwnerKind,
            spotRid,
            actor,
            channel,
            new MeshOperationId(native.OperationIdHigh, native.OperationIdLow),
            native.SnapshotRemoteTargetCount,
            native.AdmittedRemoteTargetCount,
            native.DroppedRemoteTargetCount,
            native.UnreachableRemoteTargetCount,
            native.SnapshotLocalSpotCount,
            native.AdmittedLocalSpotCount,
            native.DroppedLocalSpotCount,
            native.ResultCode,
            native.FailureErrno);
    }

    public MeshMonitorStatus Status()
    {
        EnsureNotDisposed();
        ZlinkMeshMonitorStatus native = new()
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkMeshMonitorStatus>(),
            Version = 1
        };
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_node_monitor_status(_handle, ref native));
        return new MeshMonitorStatus(
            (MeshNodeState)native.State,
            native.PeerAdmitted,
            native.PeerRejected,
            native.SubmittedMessages,
            native.CompletedOperations,
            native.BackpressuredSubmits,
            native.MulticastMessages,
            native.MulticastDroppedTargets,
            native.ActiveClaims,
            native.PendingApplicationMessages,
            native.PendingInfrastructureMessages,
            native.PendingBytes);
    }

    public void Close() => Dispose();

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        var rc = NativeMethods.zlink_mesh_node_monitor_close(ref handle);
        if (rc != 0)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~MeshNodeMonitor()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        NativeMethods.zlink_mesh_node_monitor_close(ref handle);
        _handle = IntPtr.Zero;
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(MeshNodeMonitor));
    }
}
