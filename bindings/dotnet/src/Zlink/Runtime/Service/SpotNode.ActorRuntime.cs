// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class SpotNode
{
    public Actor CreateActor(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_actor_new(_handle,
            actorId, out ZlinkActorRef actor);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return new Actor(this, ActorInterop.FromNative(ref actor));
    }

    IActor ISpotNode.CreateActor(string actorId)
    {
        return CreateActor(actorId);
    }

    public ActorRef ActorLookup(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_spot_node_actor_lookup(_handle, actorId,
            out ZlinkActorRef actor);
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
                _handle,
                ref nativeActor,
                ref nativeSourceNode,
                ref nativeSourceSession,
                ref nativePart,
                (int)flags,
                hasMore ? NativeMethods.ZlinkPartFlag.More : NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc != 0)
            {
                throw ZlinkException.CreateSubmitException(NativeMethods.zlink_errno());
            }

            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
            && RequestReplySupport.MapSendNoWaitResult(error) == SendResult.Backpressured)
        {
            return false;
        }
        finally
        {
            if (!submitted)
            {
                NativeMethods.zlink_msg_close(ref nativePart);
            }
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
            _handle,
            ref nativeActor,
            ref nativeSourceNode,
            ref nativeSourceSession);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void CloseActorBoundSession(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_spot_node_actor_close_bound_session(
            _handle, ref nativeActor, ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }

    internal static ActorRef RemoteActorRef(RoutingId targetNodeRid, string actorId)
        => ActorRef.Remote(targetNodeRid, actorId);

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

    internal void DestroyActor(ActorRef actor, TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        int rc = NativeMethods.zlink_spot_node_actor_destroy(_handle,
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

        SynchronizationContext? context = SynchronizationContext.Current;
        var native = new NativeMethods.ZlinkSendReadyHandlerDelegate(
            OnNativeSendReady);
        int rc = NativeMethods.zlink_send_ready_handler(_handle, native,
            IntPtr.Zero);
        ZlinkException.ThrowHandlerIfError(rc);
        _sendReadyHandler = handler;
        _sendReadyHandlerContext = context;
        _sendReadyHandlerNative = native;
    }

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destSpotRid, Message message, TimeSpan timeout = default,
        SendFlags flags = SendFlags.None, CancellationToken ct = default)
        => JoinActor(actor, RoutingId, destSpotRid, message, timeout,
            flags, ct);

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout = default,
        CancellationToken ct = default)
        => JoinActor(actor, destNodeRid, destSpotRid, message, timeout,
            SendFlags.None, ct);

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

    internal Task<IReadOnlyList<Message>> JoinActor(ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid, Message message,
        TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        if (message == null)
            throw new ArgumentNullException(nameof(message));
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeNodeRid = destNodeRid.ToNative();
        ZlinkRoutingId nativeSpotRid = destSpotRid.ToNative();
        ZlinkMsg nativeMessage = default;
        message.Copy().MoveTo(ref nativeMessage);
        uint timeoutMs = ActorInterop.NormalizeTimeout(timeout);
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            state.SetCancellationRegistration(ct.CanBeCanceled
                ? ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle)
                : default);
            state.SetTimeoutTimer(ActorInterop.CreateTimeoutTimer(handle,
                timeoutMs));

            int rc = NativeMethods.zlink_spot_node_actor_join_spot(_handle,
                ref nativeActor, ref nativeNodeRid, ref nativeSpotRid,
                ref nativeMessage, 1, ActorInterop.JoinHandlerPtr,
                GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
            nativeMessage = default;
            if (rc != 0)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return ActorInterop.TakePartsAsync(
                RequestProgressPump.AttachSpot(_handle, completion.Task));
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

    public ActorLeaveOperation LeaveActor(ActorRef actor,
        RoutingId currentSpotRid)
    {
        EnsureNotDisposed();
        return new ActorLeaveOperationImpl(this, actor, currentSpotRid);
    }

    internal void LeaveActor(ActorRef actor, RoutingId destSpotRid,
        TimeSpan timeout = default)
    {
        EnsureNotDisposed();
        ZlinkActorRef nativeActor = ActorInterop.ToNative(actor);
        ZlinkRoutingId nativeSpotRid = destSpotRid.ToNative();
        int rc = NativeMethods.zlink_spot_node_actor_leave_spot(_handle,
            ref nativeActor, ref nativeSpotRid,
            ActorInterop.NoopReplyHandlerPtr, IntPtr.Zero,
            ActorInterop.NormalizeTimeout(timeout));
        if (rc != 0)
            throw ZlinkException.CreateRequestException(NativeMethods.zlink_errno());
    }
}
