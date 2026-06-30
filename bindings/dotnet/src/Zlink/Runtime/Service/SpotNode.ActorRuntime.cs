// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode
{
    private const int StackActorCreatePartLimit = 8;

    IActor ISpotNode.CreateActor(string actorId)
    {
        return CreateActor(actorId);
    }

    IActor ISpotNode.CreateActor(string actorId, Message request)
    {
        return CreateActor(actorId, request);
    }

    IActor ISpotNode.CreateActor(string actorId,
        IReadOnlyList<Message> requestParts)
    {
        return CreateActor(actorId, requestParts);
    }

    public ActorRef ActorLookup(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_actor_lookup(Handle, actorId,
            out var actor);
        ZlinkException.ThrowConfigIfError(rc);
        return ActorInterop.FromNative(ref actor);
    }

    public SendOperation SendActorBoundSession(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorSendBoundSessionOperation(this, actor);
    }

    public bool ForwardActorBoundSessionPart(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags = SendFlags.None)
    {
        ArgumentNullException.ThrowIfNull(message);
        EnsureNotDisposed();
        using var cloned = message.Copy();
        ZlinkMsg nativePart = default;
        cloned.MoveTo(ref nativePart);
        var submitted = false;
        try
        {
            var nativeActor = ActorInterop.ToNative(actor);
            var nativeSourceNode = sourceNodeRid.ToNative();
            var nativeSourceSession = sourceSessionRid.ToNative();
            var rc = NativeMethods.zlink_spot_node_actor_forward_bound_session_part(
                Handle,
                ref nativeActor,
                ref nativeSourceNode,
                ref nativeSourceSession,
                ref nativePart,
                (int)flags,
                hasMore ? NativeMethods.ZlinkPartFlag.More : NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc != 0) throw ZlinkException.CreateSubmitException(NativeMethods.zlink_errno());

            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error) ==
                                           SendResult.Backpressured)
        {
            return false;
        }
        finally
        {
            if (!submitted) NativeMethods.zlink_msg_close(ref nativePart);
        }
    }

    public void BindRemoteActorBoundSession(
        ActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var nativeSourceNode = sourceNodeRid.ToNative();
        var nativeSourceSession = sourceSessionRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_actor_bind_remote_session(
            Handle,
            ref nativeActor,
            ref nativeSourceNode,
            ref nativeSourceSession);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_spot_node_actor_close_bound_session(
            Handle, ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    public ActorLookupOperation RemoteActorGetRef(RoutingId targetNodeRid,
        string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        return new ActorLookupOperationImpl(this, targetNodeRid, actorId);
    }

    public ActorDestroyOperation DestroyActor(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorDestroyOperationImpl(this, actor);
    }

    public ActorJoinOperation JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid)
    {
        EnsureNotDisposed();
        return new ActorJoinOperationImpl(this, actor, destNodeRid,
            destSpotRid);
    }

    public ActorJoinEntrySpotOperation JoinActorEntrySpot(ActorRef actor,
        RoutingId destNodeRid, Message request)
    {
        EnsureNotDisposed();
        return new ActorJoinEntrySpotOperationImpl(this, actor, destNodeRid,
            request);
    }

    public ActorLeaveOperation LeaveActor(ActorRef actor,
        RoutingId currentSpotRid)
    {
        EnsureNotDisposed();
        return new ActorLeaveOperationImpl(this, actor, currentSpotRid);
    }

    public Actor CreateActor(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_spot_node_actor_new(Handle,
            actorId, out var actor);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return new Actor(this, ActorInterop.FromNative(ref actor));
    }

    public Actor CreateActor(string actorId, Message request)
    {
        if (request == null)
            throw new ArgumentNullException(nameof(request));
        return CreateActor(actorId, new[] { request });
    }

    public unsafe Actor CreateActor(string actorId,
        IReadOnlyList<Message> requestParts)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        RequestReplySupport.EnsureParts(requestParts, nameof(requestParts));
        EnsureNotDisposed();

        var parts = requestParts as Message[] ?? new List<Message>(
            requestParts).ToArray();
        var nativeParts = parts.Length <= StackActorCreatePartLimit
            ? stackalloc ZlinkMsg[StackActorCreatePartLimit]
            : new ZlinkMsg[parts.Length];
        nativeParts = nativeParts.Slice(0, parts.Length);
        var built = 0;
        var submitted = false;
        try
        {
            NativeMessageParts.MoveToNative(parts, nativeParts,
                nameof(requestParts), ref built);
            fixed (ZlinkMsg* nativePtr = nativeParts)
            {
                var rc = NativeMethods.zlink_spot_node_actor_new_with_request(
                    Handle, actorId, nativePtr, (nuint)parts.Length,
                    out var actor);
                if (rc != 0)
                    throw ZlinkException.CreateConfigException(
                        NativeMethods.zlink_errno());
                submitted = true;
                return new Actor(this, ActorInterop.FromNative(ref actor));
            }
        }
        finally
        {
            if (!submitted)
                NativeMessageParts.RestoreManaged(parts, nativeParts, 0, built);
        }
    }

    internal static ActorRef RemoteActorRef(RoutingId targetNodeRid, string actorId)
    {
        return ActorRef.Remote(targetNodeRid, actorId);
    }

    internal void DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var rc = NativeMethods.zlink_spot_node_actor_destroy(Handle,
            ref nativeActor, ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    internal void DestroyRemoteActor(ActorRef actor, TimeSpan timeout = default)
    {
        DestroyActor(actor, timeout);
    }

    internal void OnSendReady(Action handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        var context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        var rc = NativeMethods.zlink_send_ready_handler(Handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destSpotRid, Message message, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, CancellationToken ct = default)
    {
        return JoinActor(actor, RoutingId, destSpotRid, message, timeout,
            flags, ct);
    }

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default,
        CancellationToken ct = default)
    {
        return JoinActor(actor, destNodeRid, destSpotRid, message, timeout,
            SendFlags.None, ct);
    }

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var nativeNodeRid = destNodeRid.ToNative();
        var nativeSpotRid = destSpotRid.ToNative();
        ZlinkMsg nativeMessage = default;
        message.Copy().MoveTo(ref nativeMessage);
        var timeoutMs = ActorInterop.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            state.SetCancellationRegistration(ct.CanBeCanceled
                ? ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle)
                : default);
            state.SetTimeoutTimer(ActorInterop.CreateTimeoutTimer(handle,
                timeoutMs));

            var rc = NativeMethods.zlink_spot_node_actor_join_spot(Handle,
                ref nativeActor, ref nativeNodeRid, ref nativeSpotRid,
                ref nativeMessage, 1, ActorInterop.JoinHandlerPtr,
                GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
            nativeMessage = default;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return ActorInterop.TakePartsAsync(
                RequestProgressPump.AttachSpot(Handle, completion.Task));
        }
        catch
        {
            NativeMethods.zlink_msg_close(ref nativeMessage);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal bool JoinActor(ActorRef actor, RoutingId destSpotRid, Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        TimeSpan? timeout = null, SendFlags flags = SendFlags.None)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActor(actor, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags, CancellationToken.None),
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

    internal bool JoinActor(ActorRef actor, RoutingId destNodeRid,
        RoutingId destSpotRid, Message message,
        RequestCallback callback,
        SendFlags flags = SendFlags.None, TimeSpan? timeout = null)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            ActorInterop.AttachPartsCallback(
                () => JoinActor(actor, destNodeRid, destSpotRid, message,
                    timeout ?? TimeSpan.Zero, flags, CancellationToken.None),
                (result, parts) => callback(result, parts));
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal void LeaveActor(ActorRef actor, RoutingId destSpotRid,
        TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        var nativeActor = ActorInterop.ToNative(actor);
        var nativeSpotRid = destSpotRid.ToNative();
        var rc = NativeMethods.zlink_spot_node_actor_leave_spot(Handle,
            ref nativeActor, ref nativeSpotRid,
            ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }
}