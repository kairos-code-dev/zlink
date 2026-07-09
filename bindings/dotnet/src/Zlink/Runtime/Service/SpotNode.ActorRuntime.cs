// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode
{
    IActor ISpotNodeActors.CreateActor(string actorId)
    {
        return CreateActor(actorId);
    }

    IActor ISpotNodeActors.CreateActor(string actorId, Message request)
    {
        return CreateActor(actorId, request);
    }

    IActor ISpotNodeActors.CreateActor(string actorId,
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

    public SendOperation SendToActor(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorSendToActorOperation(this, actor);
    }

    public RequestOperation RequestToActor(ActorRef actor)
    {
        EnsureNotDisposed();
        return new ActorRequestToActorOperation(this, actor);
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
            if (rc != 0)
                throw ZlinkException.CreateSubmitException((SubmitResult)rc);

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
            throw ZlinkException.CreateRequestException((RequestResult)rc);
    }

    internal Task<IReadOnlyList<Message>> RequestToActorAsync(
        ActorRef actor,
        IReadOnlyList<Message> parts,
        TimeSpan timeout = default,
        SendFlags flags = SendFlags.None,
        CancellationToken ct = default)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        EnsureNotDisposed();
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeActor = ActorInterop.ToNative(actor);
        var timeoutMs = RequestReplySupport.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        var nativeParts = cloned.Length <= NativeMessageParts.StackPartLimit
            ? stackalloc ZlinkMsg[NativeMessageParts.StackPartLimit]
            : new ZlinkMsg[cloned.Length];
        nativeParts = nativeParts.Slice(0, cloned.Length);
        var built = 0;
        var submitted = false;
        try
        {
            NativeMessageParts.MoveToNative(cloned, nativeParts,
                nameof(parts), ref built);
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            state.SetCancellationRegistration(ct.CanBeCanceled
                ? ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle)
                : default);
            unsafe
            {
                fixed (ZlinkMsg* nativePtr = nativeParts)
                {
                    var rc = NativeMethods.zlink_spot_node_request_to_actor(
                        Handle,
                        ref nativeActor,
                        nativePtr,
                        (nuint)cloned.Length,
                        ActorInterop.ReplyHandlerPtr,
                        GCHandle.ToIntPtr(handle),
                        (int)flags,
                        timeoutMs);
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            (SubmitResult)rc);
                }
            }
            submitted = true;
            return ActorInterop.TakePartsAsync(completion.Task);
        }
        catch
        {
            if (!submitted)
                NativeMessageParts.RestoreManaged(cloned, nativeParts, 0, built);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal bool SendToActor(
        ActorRef actor,
        IReadOnlyList<Message> parts,
        SendFlags flags = SendFlags.None)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        EnsureNotDisposed();
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeParts = cloned.Length <= NativeMessageParts.StackPartLimit
            ? stackalloc ZlinkMsg[NativeMessageParts.StackPartLimit]
            : new ZlinkMsg[cloned.Length];
        nativeParts = nativeParts.Slice(0, cloned.Length);
        var built = 0;
        var submitted = false;
        try
        {
            NativeMessageParts.MoveToNative(cloned, nativeParts,
                nameof(parts), ref built);
            var nativeActor = ActorInterop.ToNative(actor);
            int rc;
            unsafe
            {
                fixed (ZlinkMsg* nativePtr = nativeParts)
                {
                    rc = NativeMethods.zlink_spot_node_send_to_actor(
                        Handle,
                        ref nativeActor,
                        nativePtr,
                        (nuint)cloned.Length,
                        ActorInterop.NoopReplyHandlerPtr,
                        IntPtr.Zero,
                        (int)flags,
                        0);
                }
            }
            submitted = true;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException((SubmitResult)rc);
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
                NativeMessageParts.RestoreManaged(cloned, nativeParts, 0, built);
        }
    }

    public void ReplyActorNoBind(
        ActorRecvInfo info,
        IReadOnlyList<Message> parts,
        RequestResult result = RequestResult.Ok)
    {
        RequestReplySupport.EnsureParts(parts, nameof(parts));
        EnsureNotDisposed();
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeActor = ActorInterop.ToNative(info.Actor);
        var nativeInfo = new ZlinkActorRecvInfo
        {
            Actor = nativeActor,
            SourceNodeRid = info.SourceNodeRid.ToNative(),
            SourceSessionRid = info.SourceSessionRid.ToNative(),
            RequestId = info.RequestId,
            Flags = info.Flags
        };
        var nativeParts = cloned.Length <= NativeMessageParts.StackPartLimit
            ? stackalloc ZlinkMsg[NativeMessageParts.StackPartLimit]
            : new ZlinkMsg[cloned.Length];
        nativeParts = nativeParts.Slice(0, cloned.Length);
        var built = 0;
        var submitted = false;
        try
        {
            NativeMessageParts.MoveToNative(cloned, nativeParts,
                nameof(parts), ref built);
            unsafe
            {
                fixed (ZlinkMsg* nativePtr = nativeParts)
                {
                    var rc = NativeMethods.zlink_spot_node_actor_reply_no_bind(
                        Handle,
                        ref nativeInfo,
                        nativePtr,
                        (nuint)cloned.Length,
                        (int)result);
                    submitted = true;
                    if (rc != 0)
                        throw ZlinkException.CreateSubmitException(
                            (SubmitResult)rc);
                }
            }
        }
        finally
        {
            if (!submitted)
                NativeMessageParts.RestoreManaged(cloned, nativeParts, 0, built);
        }
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
            throw ZlinkException.CreateConfigException((ConfigResult)rc);
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
        var nativeParts = parts.Length <= NativeMessageParts.StackPartLimit
            ? stackalloc ZlinkMsg[NativeMessageParts.StackPartLimit]
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
                        (ConfigResult)rc);
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

}
