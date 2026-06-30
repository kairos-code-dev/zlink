// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static Task<IReadOnlyList<Message>> LeaveActorAsync(SpotNode node,
        ActorRef actor, RoutingId currentSpotRid, TimeSpan timeout,
        CancellationToken ct)
    {
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeActor = ToNative(actor);
        var nativeSpotRid = currentSpotRid.ToNative();
        return SubmitReplyAsync(node.Handle, ct, (userData, handler) =>
            NativeMethods.zlink_spot_node_actor_leave_spot(node.Handle,
                ref nativeActor, ref nativeSpotRid, handler, userData,
                timeoutMs), true);
    }

    internal static bool LeaveActorCallback(SpotNode node, ActorRef actor,
        RoutingId currentSpotRid, TimeSpan timeout, ReplyHandler callback)
    {
        return SubmitReplyCallback(() => LeaveActorAsync(node, actor,
            currentSpotRid, timeout, CancellationToken.None), callback);
    }

    internal static Task<IReadOnlyList<Message>> DestroyActorAsync(SpotNode node,
        ActorRef actor, TimeSpan timeout, CancellationToken ct)
    {
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeActor = ToNative(actor);
        return SubmitReplyAsync(node.Handle, ct, (userData, handler) =>
                NativeMethods.zlink_spot_node_actor_destroy(node.Handle,
                    ref nativeActor, handler, userData, timeoutMs),
            true);
    }

    internal static bool DestroyActorCallback(SpotNode node, ActorRef actor,
        TimeSpan timeout, ReplyHandler callback)
    {
        return SubmitReplyCallback(() => DestroyActorAsync(node, actor,
            timeout, CancellationToken.None), callback);
    }

    internal static Task<IReadOnlyList<Message>> BindActorAsync(
        StreamSocket stream, RoutingId sessionRid, ActorRef actor,
        TimeSpan timeout, CancellationToken ct)
    {
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeSession = sessionRid.ToNative();
        var nativeActor = ToNative(actor);
        var handle = stream.Handle;
        return SubmitReplyAsync(handle, ct, (userData, handler) =>
                NativeMethods.zlink_stream_bind_actor(handle, ref nativeSession,
                    ref nativeActor, handler, userData, timeoutMs),
            false);
    }

    internal static bool BindActorCallback(StreamSocket stream,
        RoutingId sessionRid, ActorRef actor, TimeSpan timeout,
        ReplyHandler callback)
    {
        return SubmitReplyCallback(() => BindActorAsync(stream, sessionRid,
            actor, timeout, CancellationToken.None), callback);
    }

    internal static Task<IReadOnlyList<Message>> UnbindActorAsync(
        StreamSocket stream, RoutingId sessionRid, string actorId,
        TimeSpan timeout, CancellationToken ct)
    {
        ValidateActorId(actorId, nameof(actorId));
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeSession = sessionRid.ToNative();
        var handle = stream.Handle;
        return SubmitReplyAsync(handle, ct, (userData, handler) =>
            NativeMethods.zlink_stream_unbind_actor(handle, ref nativeSession,
                actorId, handler, userData, timeoutMs), false);
    }

    internal static bool UnbindActorCallback(StreamSocket stream,
        RoutingId sessionRid, string actorId, TimeSpan timeout,
        ReplyHandler callback)
    {
        return SubmitReplyCallback(() => UnbindActorAsync(stream, sessionRid,
            actorId, timeout, CancellationToken.None), callback);
    }

    private static Task<IReadOnlyList<Message>> SubmitReplyAsync(
        IntPtr progressHandle, CancellationToken ct,
        NativeSubmitFunc submitter, bool attachSpot)
    {
        TaskCompletionSource<Received> completion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(
                    ct.Register(static userdata => { RequestCallState.CancelFromUserData(userdata); }, handle));
            var rc = submitter(GCHandle.ToIntPtr(handle), ReplyHandlerPtr);
            if (rc != 0)
            {
                handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            var attached = attachSpot
                ? RequestProgressPump.AttachSpot(progressHandle, completion.Task)
                : RequestProgressPump.AttachSocket(progressHandle,
                    completion.Task);
            return TakePartsAsync(attached);
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    private static bool SubmitReplyCallback(
        Func<Task<IReadOnlyList<Message>>> invoker, ReplyHandler callback)
    {
        var syncCtx = SynchronizationContext.Current;
        try
        {
            _ = invoker().ContinueWith(t =>
            {
                RequestResult result;
                IReadOnlyList<Message> parts;
                if (t.IsFaulted)
                {
                    var err = t.Exception!.GetBaseException();
                    result = err is ZlinkRequestException rex
                        ? (RequestResult)rex.Code
                        : RequestResult.InternalError;
                    parts = Array.Empty<Message>();
                }
                else if (t.IsCanceled)
                {
                    result = RequestResult.Terminated;
                    parts = Array.Empty<Message>();
                }
                else
                {
                    result = RequestResult.Ok;
                    parts = t.Result;
                }

                CallbackDelivery.Post(syncCtx, () => callback(result, parts));
            }, TaskScheduler.Default);
            return true;
        }
        catch (ZlinkException error) when (
            RequestReplySupport.MapSendNoWaitResult(error)
            == SendResult.Backpressured)
        {
            return false;
        }
    }

    internal delegate int NativeSubmitFunc(IntPtr userData, IntPtr handler);
}