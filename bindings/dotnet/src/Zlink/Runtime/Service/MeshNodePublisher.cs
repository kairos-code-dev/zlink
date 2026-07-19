// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class MeshNodePublisher : IMeshNodePublisher
{
    private IntPtr _handle;

    internal MeshNodePublisher(MeshNode owner)
    {
        owner.EnsureNotDisposed();
        _handle = NativeMethods.zlink_mesh_node_publisher_new(owner.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
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
                    NativeMethods.zlink_mesh_node_publisher_publish(_handle,
                        channelName, topic, meta, np, count, detailPtr,
                        (int)flags));
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

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        var rc = NativeMethods.zlink_mesh_node_publisher_destroy(ref handle);
        if (rc == 0)
            _handle = IntPtr.Zero;
        else
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~MeshNodePublisher()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        NativeMethods.zlink_mesh_node_publisher_destroy(ref handle);
        _handle = IntPtr.Zero;
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(MeshNodePublisher));
    }
}
