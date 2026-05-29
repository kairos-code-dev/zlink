// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    internal static Task<(ActorJoinResult Result, IReadOnlyList<Message> Parts)>
        JoinActorAsync(SpotNode node, ActorRef actor, RoutingId destNodeRid,
            RoutingId destSpotRid, IReadOnlyList<Message> parts,
            TimeSpan timeout, SendFlags flags, CancellationToken ct)
    {
        TaskCompletionSource<ActorJoinResultEnvelope> tcs =
            SubmitJoinNative(node, actor, destNodeRid, destSpotRid, parts,
                timeout, flags, ct);
        return tcs.Task.ContinueWith(t =>
        {
            ActorJoinResultEnvelope env = t.Result;
            return (env.Result, env.Parts);
        }, TaskScheduler.Default);
    }

    internal static bool JoinActorCallback(SpotNode node, ActorRef actor,
        RoutingId destNodeRid, RoutingId destSpotRid,
        IReadOnlyList<Message> parts, TimeSpan timeout, SendFlags flags,
        ActorJoinHandler callback)
    {
        SynchronizationContext? syncCtx = SynchronizationContext.Current;
        try
        {
            TaskCompletionSource<ActorJoinResultEnvelope> tcs =
                SubmitJoinNative(node, actor, destNodeRid, destSpotRid, parts,
                    timeout, flags, CancellationToken.None);
            _ = tcs.Task.ContinueWith(t =>
            {
                ActorJoinResultEnvelope env;
                if (t.IsFaulted)
                {
                    Exception err = t.Exception!.GetBaseException();
                    RequestResult rr = err is ZlinkRequestException re
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
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkActorRef nativeActor = ToNative(actor);
        ZlinkRoutingId nativeNodeRid = destNodeRid.ToNative();
        ZlinkRoutingId nativeSpotRid = destSpotRid.ToNative();
        Message[] cloned = RequestReplySupport.CloneParts(parts);
        ZlinkMsg[] nativeParts = new ZlinkMsg[cloned.Length];
        int built = 0;
        var completion = new TaskCompletionSource<ActorJoinResultEnvelope>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            for (int i = 0; i < cloned.Length; i++)
            {
                cloned[i].Copy().MoveTo(ref nativeParts[i]);
                built++;
            }

            ActorJoinCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.CancelReg = ct.Register(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinCallState s)
                    {
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                    }
                }, handle);
            }
            if (timeoutMs > 0)
            {
                state.TimeoutTimer = new System.Threading.Timer(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinCallState s)
                    {
                        ActorJoinResult fail = new(RequestResult.TimedOut, 0,
                            default, default, 0, 0);
                        if (s.Completion.TrySetResult(
                            new ActorJoinResultEnvelope(fail,
                                Array.Empty<Message>())))
                            s.Cleanup();
                    }
                }, handle, (int)timeoutMs, System.Threading.Timeout.Infinite);
            }

            int rc = NativeMethods.zlink_spot_node_actor_join_spot(
                node.Handle, ref nativeActor, ref nativeNodeRid,
                ref nativeSpotRid, ref nativeParts[0], (nuint)nativeParts.Length,
                FullJoinHandlerPtr, GCHandle.ToIntPtr(handle), (int)flags,
                timeoutMs);
            for (int i = 0; i < nativeParts.Length; i++)
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
            for (int i = 0; i < built; i++)
                NativeMethods.zlink_msg_close(ref nativeParts[i]);
            RequestReplySupport.DisposeParts(cloned);
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal static Task<ActorJoinEntrySpotResult> JoinActorEntrySpotAsync(
        SpotNode node, ActorRef actor, RoutingId destNodeRid,
        TimeSpan timeout, CancellationToken ct)
        => SubmitJoinEntrySpotNative(node, actor, destNodeRid, timeout, ct).Task;

    internal static bool JoinActorEntrySpotCallback(SpotNode node,
        ActorRef actor, RoutingId destNodeRid, TimeSpan timeout,
        ActorJoinEntrySpotHandler callback)
    {
        SynchronizationContext? syncCtx = SynchronizationContext.Current;
        try
        {
            Task<ActorJoinEntrySpotResult> task =
                JoinActorEntrySpotAsync(node, actor, destNodeRid, timeout,
                    CancellationToken.None);
            _ = task.ContinueWith(t =>
            {
                ActorJoinEntrySpotResult result;
                if (t.IsFaulted)
                {
                    Exception err = t.Exception!.GetBaseException();
                    RequestResult rr = err is ZlinkRequestException re
                        ? (RequestResult)re.Code
                        : RequestResult.InternalError;
                    result = new ActorJoinEntrySpotResult(rr, default,
                        destNodeRid, 0, 0);
                }
                else if (t.IsCanceled)
                {
                    result = new ActorJoinEntrySpotResult(
                        RequestResult.Terminated, default, destNodeRid, 0, 0);
                }
                else
                {
                    result = t.Result;
                }
                CallbackDelivery.Post(syncCtx, () => callback(result));
            }, TaskScheduler.Default);
            return true;
        }
        catch (ZlinkException)
        {
            throw;
        }
    }

    private static TaskCompletionSource<ActorJoinEntrySpotResult>
        SubmitJoinEntrySpotNative(SpotNode node, ActorRef actor,
            RoutingId destNodeRid, TimeSpan timeout, CancellationToken ct)
    {
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkActorRef nativeActor = ToNative(actor);
        ZlinkRoutingId nativeNodeRid = destNodeRid.ToNative();
        var completion = new TaskCompletionSource<ActorJoinEntrySpotResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            ActorJoinEntrySpotCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.CancelReg = ct.Register(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinEntrySpotCallState s)
                    {
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                    }
                }, handle);
            }
            if (timeoutMs > 0)
            {
                state.TimeoutTimer = new System.Threading.Timer(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorJoinEntrySpotCallState s)
                    {
                        ActorJoinEntrySpotResult fail = new(
                            RequestResult.TimedOut, default, default, 0, 0);
                        if (s.Completion.TrySetResult(fail))
                            s.Cleanup();
                    }
                }, handle, (int)timeoutMs, System.Threading.Timeout.Infinite);
            }

            int rc = NativeMethods.zlink_spot_node_actor_join_entry_spot(
                node.Handle, ref nativeActor, ref nativeNodeRid,
                JoinEntrySpotHandlerPtr, GCHandle.ToIntPtr(handle),
                timeoutMs);
            if (rc != 0)
            {
                if (handle.IsAllocated)
                    handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
            return completion;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }
}
