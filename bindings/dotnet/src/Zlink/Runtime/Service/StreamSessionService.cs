// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class StreamSessionService : IStreamSessionService
{
    private IntPtr _handle;

    internal StreamSessionService(MeshNode owner, StreamSocket stream)
    {
        owner.EnsureNotDisposed();
        _handle = NativeMethods.zlink_stream_session_service_new(owner.Handle,
            stream.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Start()
    {
        EnsureNotDisposed();
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_stream_session_service_start(_handle));
    }

    public void Shutdown(TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_stream_session_service_shutdown(_handle,
            MeshSend.EncodeTimeout(timeout));
        if (rc != 0)
            throw new ZlinkRequestException((RequestResult)rc);
    }

    public StreamSessionStatus Status()
    {
        EnsureNotDisposed();
        var native = new ZlinkStreamSessionStatus
        {
            StructSize = (uint)Marshal.SizeOf<ZlinkStreamSessionStatus>(),
            Version = 1
        };
        ZlinkException.ThrowConfigIfError(
            NativeMethods.zlink_stream_session_service_status(_handle,
                ref native));
        return new StreamSessionStatus(
            (StreamSessionState)native.State,
            native.LifecycleGeneration,
            native.SessionCount,
            native.BindingCount,
            native.PendingMessageCount,
            native.PendingByteCount,
            native.LastError);
    }

    public SubmitResult BindActor(RoutingId sessionRid, ActorRef actor,
        out MeshOperationId operationId, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeSession = sessionRid.ToNative();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_stream_session_bind_actor(_handle,
            ref nativeSession, ref nativeActor, out var op,
            MeshSend.EncodeTimeout(timeout));
        operationId = new MeshOperationId(op.High, op.Low);
        return (SubmitResult)rc;
    }

    public SubmitResult UnbindActor(RoutingId sessionRid, ActorRef actor,
        ulong expectedBindingGeneration, out MeshOperationId operationId,
        TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeSession = sessionRid.ToNative();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_stream_session_unbind_actor(_handle,
            ref nativeSession, ref nativeActor, expectedBindingGeneration,
            out var op, MeshSend.EncodeTimeout(timeout));
        operationId = new MeshOperationId(op.High, op.Low);
        return (SubmitResult)rc;
    }

    public StreamSessionBinding[] Bindings(RoutingId sessionRid)
    {
        EnsureNotDisposed();
        var nativeSession = sessionRid.ToNative();
        return NativeSnapshotReader.Read<ZlinkStreamSessionBinding,
            StreamSessionBinding>(
            (IntPtr entries, ref nuint count) =>
                NativeMethods.zlink_stream_session_bindings(_handle,
                    ref nativeSession, entries, ref count),
            (ref ZlinkStreamSessionBinding native) => new StreamSessionBinding(
                RoutingId.FromNative(ref native.SessionRid) ?? default,
                ActorInterop.FromNative(ref native.Actor),
                native.BindingGeneration,
                native.MembershipEpoch));
    }

    public SubmitResult SendToActor(RoutingId sessionRid, ActorRef actor,
        IReadOnlyList<Message> parts, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeSession = sessionRid.ToNative();
        var nativeActor = ActorInterop.ToNative(actor);
        return MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_stream_session_send_to_actor(_handle,
                    ref nativeSession, ref nativeActor, meta, np, count,
                    (int)flags));
    }

    public SubmitResult RequestToActor(RoutingId sessionRid, ActorRef actor,
        IReadOnlyList<Message> parts, out MeshOperationId operationId,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        ReadOnlyMemory<byte> metadata = default)
    {
        EnsureNotDisposed();
        var nativeSession = sessionRid.ToNative();
        var nativeActor = ActorInterop.ToNative(actor);
        var timeoutMs = MeshSend.EncodeTimeout(timeout);
        ZlinkMeshOperationId op = default;
        var result = MeshSend.SubmitWithMetadata(parts, nameof(parts), metadata,
            (np, count, meta) =>
                NativeMethods.zlink_stream_session_request_to_actor(_handle,
                    ref nativeSession, ref nativeActor, meta, np, count,
                    out op, (int)flags, timeoutMs));
        operationId = new MeshOperationId(op.High, op.Low);
        return result;
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
        var rc = NativeMethods.zlink_stream_session_service_destroy(ref handle);
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

    ~StreamSessionService()
    {
        if (_handle == IntPtr.Zero)
            return;
        var handle = _handle;
        NativeMethods.zlink_stream_session_service_destroy(ref handle);
        _handle = IntPtr.Zero;
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(StreamSessionService));
    }
}
