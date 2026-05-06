// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public delegate ActorAdmissionResult ActorAdmissionHandler(string actorId,
    Message message);

public readonly struct ActorRef : IEquatable<ActorRef>
{
    public ActorRef(RoutingId nodeRid, string actorId, ulong generation)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        NodeRid = nodeRid;
        ActorId = actorId;
        Generation = generation;
    }

    public RoutingId NodeRid { get; }
    public string ActorId { get; }
    public ulong Generation { get; }
    public bool IsUnchecked => Generation == 0;

    public static ActorRef Unchecked(RoutingId nodeRid, string actorId)
        => new(nodeRid, actorId, 0);

    public static ActorRef Remote(RoutingId targetNodeRid, string actorId)
    {
        ZlinkRoutingId nodeRid = NativeHelpers.WriteRoutingId(
            targetNodeRid.ToByteArray());
        int rc = NativeMethods.zlink_remote_actor_get_ref(ref nodeRid, actorId,
            out ZlinkActorRef actor);
        ZlinkException.ThrowConfigIfError(rc);
        return ActorInterop.FromNative(ref actor);
    }

    public bool Equals(ActorRef other)
        => Generation == other.Generation
            && NodeRid.Equals(other.NodeRid)
            && string.Equals(ActorId, other.ActorId, StringComparison.Ordinal);

    public override bool Equals(object? obj)
        => obj is ActorRef other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(NodeRid, ActorId,
        Generation);

    public static bool operator ==(ActorRef left, ActorRef right)
        => left.Equals(right);

    public static bool operator !=(ActorRef left, ActorRef right)
        => !left.Equals(right);
}

public sealed record ActorCreateResult(ActorCreateStatus Status,
    ActorRef Actor);

public sealed record ActorRoute(ActorRef Actor, bool Joined,
    RoutingId? JoinedSpotRid);

public sealed record ActorRecvInfo(ActorRef Actor, RoutingId? SourceNodeRid,
    RoutingId? SourceSessionRid, uint Flags);

public sealed record ActorJoinInfo(ActorRef SourceActor, ActorRef TargetActor,
    RoutingId? SourceNodeRid, RoutingId? SourceSpotRid,
    RoutingId? TargetNodeRid, RoutingId? TargetSpotRid, ulong JoinEpoch,
    uint Flags)
{
    internal ZlinkActorJoinInfo NativeInfo { get; init; }
}

public sealed record ActorPart(ActorRecvInfo Info, Message Message,
    bool More);

public sealed record ActorJoinRequest(ActorJoinInfo Info, Message Message);

public sealed record SpotNodeSpotEntry(RoutingId? SpotRid,
    bool DispatchHandlerAttached, uint JoinedActorCount,
    uint PendingActorJoinCount, bool RouteSynced, ulong LastChangedMs);

public sealed record SpotNodeActorEntry(ActorRef Actor, bool Joined,
    RoutingId? JoinedSpotRid, bool RouteSynced, uint PendingMessageCount,
    ulong LastChangedMs);

public sealed class Actor : IDisposable, IAsyncDisposable
{
    private readonly SpotNode _node;
    private readonly ActorRef _ref;
    private bool _disposed;

    internal Actor(SpotNode node, ActorRef actorRef)
    {
        _node = node ?? throw new ArgumentNullException(nameof(node));
        _ref = actorRef;
    }

    public ActorRef Ref { get { EnsureNotDisposed(); return _ref; } }

    public Task<IReadOnlyList<Message>> JoinAsync(Spot spot, Message message,
        TimeSpan timeout = default, SendFlags flags = SendFlags.None,
        CancellationToken ct = default)
    {
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        uint timeoutMs = ActorInterop.NormalizeTimeout(timeout);
        ZlinkMsg nativePart = default;
        message.Copy().MoveTo(ref nativePart);
        GCHandle handle = default;
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            state.SetCancellationRegistration(
                ct.CanBeCanceled
                    ? ct.Register(static userdata =>
                    {
                        RequestCallState callbackState =
                            (RequestCallState)((GCHandle)userdata!).Target!;
                        callbackState.TrySetCanceled(CancellationToken.None);
                    }, handle)
                    : default);
            state.SetTimeoutTimer(ActorInterop.CreateTimeoutTimer(handle,
                timeoutMs));

            ZlinkActorRef nativeActor = ActorInterop.ToNative(_ref);
            ZlinkRoutingId destNodeRid = NativeHelpers.WriteRoutingId(
                _node.RoutingId.ToByteArray());
            ZlinkRoutingId destSpotRid = NativeHelpers.WriteRoutingId(
                spot.RoutingId.ToByteArray());
            int rc = NativeMethods.zlink_spot_node_actor_join_spot(_node.Handle,
                ref nativeActor, ref destNodeRid, ref destSpotRid,
                ref nativePart, ActorInterop.ReplyHandlerPtr,
                GCHandle.ToIntPtr(handle),
                (int)flags, timeoutMs);
            nativePart = default;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return ActorInterop.TakePartsAsync(
                RequestProgressPump.AttachSpot(spot.Handle, completion.Task));
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref nativePart);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    public bool Join(Spot spot, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null, SendFlags flags = SendFlags.None)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinAsync(spot, message, timeout ?? TimeSpan.Zero, flags),
                callback);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
    }

    public void Leave(Spot spot)
    {
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(_ref);
        ZlinkRoutingId currentSpotRid = NativeHelpers.WriteRoutingId(
            spot.RoutingId.ToByteArray());
        int rc = NativeMethods.zlink_spot_node_actor_leave_spot(_node.Handle,
            ref nativeActor, ref currentSpotRid, 0);
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public ActorPart? RecvPart(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        return ActorInterop.RecvActorPart(_node.Handle, _ref, flags);
    }

    public bool SendBoundSession(Message message,
        SendFlags flags = SendFlags.None)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.Copy().MoveTo(ref nativePart);
            ZlinkActorRef nativeActor = ActorInterop.ToNative(_ref);
            int rc = NativeMethods.zlink_spot_node_actor_send_bound_session_msg(
                _node.Handle, ref nativeActor, ref nativePart, (int)flags);
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error)
                == SendResult.Backpressured)
        {
            return false;
        }
        finally
        {
            if (!submitted)
                NativeMethods.zlink_msg_close(ref nativePart);
        }
    }

    public void CloseBoundSession(TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(_ref);
        int rc = NativeMethods.zlink_spot_node_actor_close_bound_session(
            _node.Handle, ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public void Close(TimeSpan timeout = default)
    {
        Destroy(throwOnError: true, ActorInterop.NormalizeTimeout(timeout));
    }

    public void Dispose()
    {
        Destroy(throwOnError: true, timeoutMs: 0);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Actor()
    {
        Destroy(throwOnError: false, timeoutMs: 0);
    }

    private void EnsureNotDisposed()
    {
        if (_disposed)
            throw new ObjectDisposedException(nameof(Actor));
    }

    private void Destroy(bool throwOnError, uint timeoutMs)
    {
        if (_disposed)
            return;
        ZlinkActorRef nativeActor = ActorInterop.ToNative(_ref);
        int rc = NativeMethods.zlink_spot_node_actor_destroy(_node.Handle,
            ref nativeActor, timeoutMs);
        if (rc == 0)
        {
            _disposed = true;
            return;
        }
        if (throwOnError)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }
}

internal static class ActorInterop
{
    internal const int DontWaitFlag = 1;
    internal static readonly NativeMethods.ZlinkReplyHandlerDelegate ReplyHandler =
        OnReply;
    internal static readonly IntPtr ReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(ReplyHandler);

    internal static void ValidateActorId(string actorId, string paramName)
    {
        if (actorId == null)
            throw new ArgumentNullException(paramName);
        if (actorId.IndexOf('\0') >= 0)
            throw new ArgumentException("Actor id must not contain NUL.",
                paramName);
        BoundaryValidation.ValidateFixedUtf8(actorId, paramName);
    }

    internal static unsafe ActorRef FromNative(ref ZlinkActorRef native)
    {
        fixed (byte* actorId = native.ActorId)
        {
            RoutingId nodeRid = RoutingId.FromBytes(
                NativeHelpers.ReadRoutingId(ref native.NodeRid));
            return new ActorRef(nodeRid,
                NativeHelpers.ReadFixedString(actorId, NativeConstants.ActorIdMax),
                native.Generation);
        }
    }

    internal static unsafe ZlinkActorRef ToNative(ActorRef actor)
    {
        ZlinkActorRef native = default;
        native.NodeRid = NativeHelpers.WriteRoutingId(actor.NodeRid.ToByteArray());
        byte* actorId = native.ActorId;
        NativeHelpers.WriteFixedString(actor.ActorId, actorId,
            NativeConstants.ActorIdMax);
        native.Generation = actor.Generation;
        return native;
    }

    internal static ActorRecvInfo FromNative(ref ZlinkActorRecvInfo native)
    {
        return new ActorRecvInfo(
            FromNative(ref native.Actor),
            RoutingId.FromNative(ref native.SourceNodeRid),
            RoutingId.FromNative(ref native.SourceSessionRid),
            native.Flags);
    }

    internal static ActorJoinInfo FromNative(ref ZlinkActorJoinInfo native)
    {
        return new ActorJoinInfo(
            FromNative(ref native.SourceActor),
            FromNative(ref native.TargetActor),
            RoutingId.FromNative(ref native.SourceNodeRid),
            RoutingId.FromNative(ref native.SourceSpotRid),
            RoutingId.FromNative(ref native.TargetNodeRid),
            RoutingId.FromNative(ref native.TargetSpotRid),
            native.JoinEpoch,
            native.Flags)
        {
            NativeInfo = native
        };
    }

    internal static ActorRoute FromNative(ref ZlinkActorRoute native)
    {
        return new ActorRoute(FromNative(ref native.Actor), native.Joined != 0,
            RoutingId.FromNative(ref native.JoinedSpotRid));
    }

    internal static uint NormalizeTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
            return 0;
        double millis = timeout.TotalMilliseconds;
        if (millis <= 1)
            return 1;
        if (millis >= uint.MaxValue)
            return uint.MaxValue;
        return (uint)millis;
    }

    internal static Message CopyMessageFromPointer(IntPtr message)
    {
        if (message == IntPtr.Zero)
            return Message.FromBytes(ReadOnlySpan<byte>.Empty);
        nuint size = NativeMethods.zlink_msg_size(message);
        if (size == 0)
            return Message.FromBytes(ReadOnlySpan<byte>.Empty);
        IntPtr data = NativeMethods.zlink_msg_data(message);
        if (data == IntPtr.Zero)
            return Message.FromBytes(ReadOnlySpan<byte>.Empty);
        byte[] copy = new byte[(int)size];
        Marshal.Copy(data, copy, 0, copy.Length);
        return Message.FromOwnedBytes(copy);
    }

    internal static ActorPart? RecvActorPart(IntPtr node, ActorRef actor,
        RecvFlags flags = RecvFlags.None)
    {
        ZlinkActorRef nativeActor = ToNative(actor);
        ZlinkMsg nativePart = default;
        int rc = NativeMethods.zlink_spot_node_actor_recv_part(node,
            ref nativeActor,
            out ZlinkActorRecvInfo info, ref nativePart,
            out NativeMethods.ZlinkPartFlag hasMore, (int)flags);
        if (rc != 0)
        {
            int errno = NativeMethods.zlink_errno();
            if ((flags & RecvFlags.DontWait) != 0
                && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                return null;
            throw ZlinkException.CreateRecvException(errno);
        }

        Message message = Message.MoveFromNative(ref nativePart);
        ActorRecvInfo managedInfo = FromNative(ref info);
        return new ActorPart(managedInfo, message,
            hasMore == NativeMethods.ZlinkPartFlag.More);
    }

    internal static unsafe ActorPart[] DrainActorParts(IntPtr node,
        IntPtr actor)
    {
        if (actor == IntPtr.Zero)
            return Array.Empty<ActorPart>();
        ZlinkActorRef nativeActor = Marshal.PtrToStructure<ZlinkActorRef>(actor);
        ActorRef actorRef = FromNative(ref nativeActor);
        List<ActorPart> parts = new();
        while (true)
        {
            ActorPart? part = RecvActorPart(node, actorRef,
                RecvFlags.DontWait);
            if (part == null)
                break;
            parts.Add(part);
        }
        return parts.ToArray();
    }

    internal static Task<IReadOnlyList<Message>> TakePartsAsync(
        Task<Received> task)
    {
        return task.ContinueWith(completed =>
        {
            if (completed.IsFaulted)
                throw completed.Exception!.GetBaseException();
            if (completed.IsCanceled)
                throw new TaskCanceledException(completed);
            Received received = completed.Result;
            try
            {
                return RequestReplySupport.TakeOwnedParts(received);
            }
            finally
            {
                received.Dispose();
            }
        }, TaskScheduler.Default);
    }

    internal static void DeliverParts(RequestResult result, Received? received,
        Action<RequestResult, IReadOnlyList<Message>> callback)
    {
        IReadOnlyList<Message> parts = Array.Empty<Message>();
        if (received != null)
        {
            parts = RequestReplySupport.TakeOwnedParts(received);
            received.Dispose();
        }
        callback(result, parts);
    }

    internal static System.Threading.Timer? CreateTimeoutTimer(GCHandle handle,
        uint timeoutMs)
    {
        if (timeoutMs == 0)
            return null;
        return new System.Threading.Timer(static userdata =>
        {
            RequestCallState callbackState =
                (RequestCallState)((GCHandle)userdata!).Target!;
            callbackState.TrySetException(
                new ZlinkRequestException(RequestResult.TimedOut));
        }, handle, (int)timeoutMs, Timeout.Infinite);
    }

    internal static void AttachPartsCallback(
        Func<Task<IReadOnlyList<Message>>> invoke,
        Action<RequestResult, IReadOnlyList<Message>> callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        SynchronizationContext? context = SynchronizationContext.Current;
        _ = invoke().ContinueWith(task =>
        {
            if (task.IsFaulted)
            {
                Exception error = task.Exception?.GetBaseException()
                    ?? new ZlinkRequestException(RequestResult.InternalError);
                RequestResult result = error is ZlinkRequestException requestError
                    ? requestError.Result
                    : RequestResult.InternalError;
                CallbackDelivery.Post(context, () => callback(result,
                    Array.Empty<Message>()));
                return;
            }
            if (task.IsCanceled)
            {
                CallbackDelivery.Post(context, () => callback(
                    RequestResult.Terminated, Array.Empty<Message>()));
                return;
            }
            CallbackDelivery.Post(context, () => callback(RequestResult.Ok,
                task.Result));
        }, TaskScheduler.Default);
    }

    internal static void OnReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        RequestCallState state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            Message[] replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            Received received = Received.Create((RoutingId?)null, replyParts);
            if (!state.TrySetResult(received))
                RequestReplySupport.DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }
}
