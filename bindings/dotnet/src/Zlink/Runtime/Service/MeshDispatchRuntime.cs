// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

/// <summary>
///     A reusable buffer that receives drained ready-index records. Take a claim
///     for an entry, then pull its messages into a <see cref="MeshReceiveBatch" />.
/// </summary>
public sealed class MeshReadyBatch : IDisposable
{
    private static readonly int RecordSize =
        Marshal.SizeOf<ZlinkMeshReadyRecord>();

    private IntPtr _handle;

    /// <summary>Creates a ready batch sized for <paramref name="capacity" /> records.</summary>
    public MeshReadyBatch(int capacity = 64)
    {
        if (capacity <= 0)
            throw new ArgumentOutOfRangeException(nameof(capacity));
        _handle = NativeMethods.zlink_mesh_ready_batch_new((nuint)capacity);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal IntPtr Handle => _handle;

    /// <summary>Gets the number of ready records currently in the batch.</summary>
    public int Count => (int)NativeMethods.zlink_mesh_ready_batch_count(_handle);

    /// <summary>Gets the ready record at <paramref name="index" />.</summary>
    public MeshReadyRecord this[int index]
    {
        get
        {
            EnsureNotDisposed();
            if (index < 0 || index >= Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            var data = NativeMethods.zlink_mesh_ready_batch_data(_handle);
            var native = Marshal.PtrToStructure<ZlinkMeshReadyRecord>(
                IntPtr.Add(data, index * RecordSize));
            return new MeshReadyRecord(
                (MeshOwnerKind)native.OwnerKind,
                (MeshReadyDomains)native.Domain,
                RoutingId.FromNative(ref native.SpotRid) ?? default,
                ActorInterop.FromNative(ref native.Actor));
        }
    }

    /// <summary>Takes an exclusive claim on the ready entry at <paramref name="index" />.</summary>
    public MeshClaim TakeClaim(int index)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_mesh_ready_batch_take_claim(_handle,
            (nuint)index, out var claim);
        ZlinkException.ThrowConfigIfError(rc);
        return new MeshClaim(claim);
    }

    /// <summary>Clears the batch for reuse.</summary>
    public void Reset()
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_ready_batch_reset(_handle));
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        NativeMethods.zlink_mesh_ready_batch_destroy(ref handle);
        _handle = IntPtr.Zero;
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(MeshReadyBatch));
    }
}

/// <summary>
///     An exclusive claim on a ready owner. Pull its messages into a
///     <see cref="MeshReceiveBatch" />, then release the claim.
/// </summary>
public sealed class MeshClaim : IDisposable
{
    private ZlinkMeshClaim _claim;
    private bool _released;

    internal MeshClaim(ZlinkMeshClaim claim)
    {
        _claim = claim;
    }

    /// <summary>
    ///     Pulls the claimed owner's messages into <paramref name="batch" />.
    ///     Returns false when no data is currently available.
    /// </summary>
    public bool Receive(MeshReceiveBatch batch, RecvFlags flags = RecvFlags.None)
    {
        if (batch == null)
            throw new ArgumentNullException(nameof(batch));
        var rc = NativeMethods.zlink_mesh_claim_recv_batch(ref _claim,
            batch.Handle, out _, (int)flags);
        if (rc == (int)RecvResult.NoData)
            return false;
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return true;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_released)
            return;
        _released = true;
        NativeMethods.zlink_mesh_claim_release(ref _claim);
    }
}

/// <summary>
///     A reusable buffer that receives pulled messages for a claim. Read records
///     with the indexer and take ownership of a record's parts with
///     <see cref="RetainMessage" />.
/// </summary>
public sealed class MeshReceiveBatch : IDisposable
{
    private static readonly int RecordSize =
        Marshal.SizeOf<ZlinkMeshReceiveRecord>();

    private IntPtr _handle;

    /// <summary>Creates a receive batch with the given capacities.</summary>
    public MeshReceiveBatch(int messageCapacity = 64, int partCapacity = 256,
        int byteCapacity = 1 << 20)
    {
        _handle = NativeMethods.zlink_mesh_receive_batch_new(
            (nuint)messageCapacity, (nuint)partCapacity, (nuint)byteCapacity);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    internal IntPtr Handle => _handle;

    /// <summary>Gets the number of message records in the batch.</summary>
    public int Count => (int)NativeMethods.zlink_mesh_receive_batch_count(_handle);

    /// <summary>Gets the received record at <paramref name="index" />.</summary>
    public MeshReceiveRecord this[int index]
    {
        get
        {
            EnsureNotDisposed();
            if (index < 0 || index >= Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            var data = NativeMethods.zlink_mesh_receive_batch_data(_handle);
            var native = Marshal.PtrToStructure<ZlinkMeshReceiveRecord>(
                IntPtr.Add(data, index * RecordSize));
            return Convert(ref native);
        }
    }

    /// <summary>
    ///     Retains and takes ownership of the message parts for the record at
    ///     <paramref name="recordIndex" />. The caller must dispose the parts.
    /// </summary>
    public Message[] RetainMessage(int recordIndex)
    {
        EnsureNotDisposed();
        var record = this[recordIndex];
        if (record.PartCount == 0)
            return Array.Empty<Message>();

        var native = new ZlinkMsg[record.PartCount];
        var count = (nuint)record.PartCount;
        unsafe
        {
            fixed (ZlinkMsg* ptr = native)
            {
                var rc = NativeMethods.zlink_mesh_receive_batch_retain_message(
                    _handle, (nuint)recordIndex, (IntPtr)ptr, ref count);
                ZlinkException.ThrowConfigIfError(rc);
            }
        }

        var result = new Message[(int)count];
        for (var i = 0; i < result.Length; i++)
            result[i] = Message.MoveFromNative(ref native[i]);
        return result;
    }

    /// <summary>Clears the batch for reuse.</summary>
    public void Reset()
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_mesh_receive_batch_reset(_handle));
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        NativeMethods.zlink_mesh_receive_batch_destroy(ref handle);
        _handle = IntPtr.Zero;
    }

    private static MeshReceiveRecord Convert(ref ZlinkMeshReceiveRecord native)
    {
        string? channelName = null;
        if (native.ChannelName != IntPtr.Zero && native.ChannelNameSize > 0)
            channelName = Marshal.PtrToStringUTF8(native.ChannelName,
                (int)native.ChannelNameSize);

        string? topic = null;
        if (native.Topic != IntPtr.Zero && native.TopicSize > 0)
            topic = Marshal.PtrToStringUTF8(native.Topic, (int)native.TopicSize);

        byte[]? metadata = null;
        if (native.ApplicationMetadata != IntPtr.Zero
            && native.ApplicationMetadataSize > 0)
        {
            metadata = new byte[(int)native.ApplicationMetadataSize];
            Marshal.Copy(native.ApplicationMetadata, metadata, 0,
                metadata.Length);
        }

        return new MeshReceiveRecord(
            (MeshRecordKind)native.Kind,
            (MeshReadyDomains)native.Domain,
            RoutingId.FromNative(ref native.SourceNodeRid) ?? default,
            RoutingId.FromNative(ref native.SourceSpotRid) ?? default,
            ActorInterop.FromNative(ref native.SourceActor),
            new MeshOperationId(native.OperationId.High, native.OperationId.Low),
            (MeshOperationKind)native.OperationKind,
            channelName,
            topic,
            metadata,
            (int)native.PartOffset,
            (int)native.PartCount,
            native.TerminalResult,
            native.FailureErrno,
            native.ReplyToken);
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(MeshReceiveBatch));
    }
}
