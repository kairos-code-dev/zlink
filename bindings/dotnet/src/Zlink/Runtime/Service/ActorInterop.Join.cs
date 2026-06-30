// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)>
        JoinActorAsync(SpotNode node, ActorRef actor, RoutingId destNodeRid,
            RoutingId destSpotRid, IReadOnlyList<Message> parts,
            TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        var tcs =
            SubmitJoinNative(node, actor, destNodeRid, destSpotRid, parts,
                timeout, flags, ct);
        return tcs.Task.ContinueWith(t =>
        {
            var env = t.Result;
            return (env.Result, env.Parts);
        }, TaskScheduler.Default);
    }

    internal static bool JoinActorCallback(SpotNode node, ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, SendFlags flags,
        ActorJoinHandler callback)
    {
        var syncCtx = SynchronizationContext.Current;
        try
        {
            var tcs =
                SubmitJoinNative(node, actor, destNodeRid, destSpotRid, parts,
                    timeout, flags, CancellationToken.None);
            _ = tcs.Task.ContinueWith(t =>
            {
                ActorJoinResultEnvelope env;
                if (t.IsFaulted)
                {
                    var err = t.Exception!.GetBaseException();
                    var rr = err is ZlinkRequestException re
                        ? (RequestResult)re.Code
                        : RequestResult.InternalError;
                    ActorJoinResult fail = new(rr, 0, actor, default, 0, 0);
                    env = new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>());
                }
                else if (t.IsCanceled)
                {
                    ActorJoinResult fail = new(RequestResult.Terminated, 0,
                        actor, default, 0, 0);
                    env = new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>());
                }
                else
                {
                    env = t.Result;
                }

                CallbackDelivery.Post(syncCtx,
                    () => callback(env.Result, env.Parts));
            }, TaskScheduler.Default);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    private static TaskCompletionSource<ActorJoinResultEnvelope>
        SubmitJoinNative(SpotNode node, ActorRef actor, RoutingId destNodeRid,
            RoutingId destSpotRid, IReadOnlyList<Message> parts,
            TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        if (parts == null || parts.Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeActor = ToNative(actor);
        var nativeNodeRid = destNodeRid.ToNative();
        var nativeSpotRid = destSpotRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeParts = new ZlinkMsg[cloned.Length];
        var built = 0;
        var completion = new TaskCompletionSource<ActorJoinResultEnvelope>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            for (var i = 0; i < cloned.Length; i++)
            {
                cloned[i].Copy().MoveTo(ref nativeParts[i]);
                built++;
            }

            ActorJoinCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.CancelReg = ct.Register(static h =>
                {
                    var gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinCallState s)
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                }, handle);
            if (timeoutMs > 0)
                state.TimeoutTimer = new System.Threading.Timer(static h =>
                {
                    var gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinCallState s)
                    {
                        ActorJoinResult fail = new(RequestResult.TimedOut, 0,
                            default, default, 0, 0);
                        if (s.Completion.TrySetResult(
                                new ActorJoinResultEnvelope(fail,
                                    Array.Empty<Message>())))
                            s.Cleanup();
                    }
                }, handle, (int)timeoutMs, Timeout.Infinite);

            var rc = NativeMethods.zlink_spot_node_actor_join_spot(
                node.Handle, ref nativeActor, ref nativeNodeRid,
                ref nativeSpotRid, ref nativeParts[0], (nuint)nativeParts.Length,
                FullJoinHandlerPtr, GCHandle.ToIntPtr(handle), (int)flags,
                timeoutMs);
            for (var i = 0; i < nativeParts.Length; i++)
                nativeParts[i] = default;
            if (rc != 0)
            {
                if (handle.IsAllocated)
                    handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            RequestReplySupport.DisposeParts(cloned);
            return completion;
        }
        catch
        {
            for (var i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal static Task<(ActorJoinEntrySpotResult Result,
        IReadOnlyList<Message> Parts)> JoinActorEntrySpotAsync(
        SpotNode node, ActorRef actor, RoutingId destNodeRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, SendFlags flags,
        CancellationToken ct)
    {
        var tcs =
            SubmitJoinEntrySpotNative(node, actor, destNodeRid, parts,
                timeout, flags, ct);
        return tcs.Task.ContinueWith(t =>
        {
            var env = t.Result;
            return (env.Result, env.Parts);
        }, TaskScheduler.Default);
    }

    internal static bool JoinActorEntrySpotCallback(SpotNode node,
        ActorRef actor, RoutingId destNodeRid, IReadOnlyList<Message> parts,
        TimeSpan timeout, SendFlags flags, ActorJoinEntrySpotHandler callback)
    {
        var syncCtx = SynchronizationContext.Current;
        try
        {
            var tcs =
                SubmitJoinEntrySpotNative(node, actor, destNodeRid, parts,
                    timeout, flags, CancellationToken.None);
            _ = tcs.Task.ContinueWith(t =>
            {
                ActorJoinEntrySpotResultEnvelope env;
                if (t.IsFaulted)
                {
                    var err = t.Exception!.GetBaseException();
                    var rr = err is ZlinkRequestException re
                        ? (RequestResult)re.Code
                        : RequestResult.InternalError;
                    ActorJoinEntrySpotResult fail =
                        new(rr, 0, default, destNodeRid, default, 0, 0);
                    env = new ActorJoinEntrySpotResultEnvelope(fail,
                        Array.Empty<Message>());
                }
                else if (t.IsCanceled)
                {
                    ActorJoinEntrySpotResult fail = new(
                        RequestResult.Terminated, 0, default, destNodeRid,
                        default, 0, 0);
                    env = new ActorJoinEntrySpotResultEnvelope(fail,
                        Array.Empty<Message>());
                }
                else
                {
                    env = t.Result;
                }

                CallbackDelivery.Post(syncCtx,
                    () => callback(env.Result, env.Parts));
            }, TaskScheduler.Default);
            return true;
        }
        catch (ZlinkException error) when ((flags & SendFlags.DontWait) != 0
                                           && RequestReplySupport.MapSendNoWaitResult(error)
                                           == SendResult.Backpressured)
        {
            return false;
        }
    }

    private static TaskCompletionSource<ActorJoinEntrySpotResultEnvelope>
        SubmitJoinEntrySpotNative(SpotNode node, ActorRef actor,
            RoutingId destNodeRid, IReadOnlyList<Message> parts,
            TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        if (parts == null || parts.Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        var timeoutMs = NormalizeTimeout(timeout);
        var nativeActor = ToNative(actor);
        var nativeNodeRid = destNodeRid.ToNative();
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeParts = new ZlinkMsg[cloned.Length];
        var built = 0;
        var completion = new TaskCompletionSource<ActorJoinEntrySpotResultEnvelope>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            for (var i = 0; i < cloned.Length; i++)
            {
                cloned[i].Copy().MoveTo(ref nativeParts[i]);
                built++;
            }

            ActorJoinEntrySpotCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.CancelReg = ct.Register(static h =>
                {
                    var gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinEntrySpotCallState s)
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                }, handle);
            if (timeoutMs > 0)
                state.TimeoutTimer = new System.Threading.Timer(static h =>
                {
                    var gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinEntrySpotCallState s)
                    {
                        ActorJoinEntrySpotResult fail = new(
                            RequestResult.TimedOut, 0, default, default,
                            default, 0, 0);
                        if (s.Completion.TrySetResult(
                                new ActorJoinEntrySpotResultEnvelope(fail,
                                    Array.Empty<Message>())))
                            s.Cleanup();
                    }
                }, handle, (int)timeoutMs, Timeout.Infinite);

            var rc = NativeMethods.zlink_spot_node_actor_join_entry_spot(
                node.Handle, ref nativeActor, ref nativeNodeRid,
                ref nativeParts[0], (nuint)nativeParts.Length,
                JoinEntrySpotHandlerPtr, GCHandle.ToIntPtr(handle),
                (int)flags, timeoutMs);
            for (var i = 0; i < nativeParts.Length; i++)
                nativeParts[i] = default;
            if (rc != 0)
            {
                if (handle.IsAllocated)
                    handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            RequestReplySupport.DisposeParts(cloned);
            return completion;
        }
        catch
        {
            for (var i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }
}