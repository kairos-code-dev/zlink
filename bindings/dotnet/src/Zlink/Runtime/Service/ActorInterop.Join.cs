// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static partial class ActorInterop
{
    private delegate int ActorJoinSubmitter(ref ZlinkMsg firstPart,
        nuint partCount, IntPtr handler, IntPtr userData, int flags,
        uint timeoutMs);

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
        try
        {
            var tcs =
                SubmitJoinNative(node, actor, destNodeRid, destSpotRid, parts,
                    timeout, flags, CancellationToken.None);
            AttachTaskCallback(() => tcs.Task,
                env => () => callback(env.Result, env.Parts),
                error =>
                {
                    ActorJoinResult fail = new(MapRequestFailure(error), 0,
                        actor, default, 0, 0);
                    return () => callback(fail, Array.Empty<Message>());
                },
                () =>
                {
                    ActorJoinResult fail = new(RequestResult.Terminated, 0,
                        actor, default, 0, 0);
                    callback(fail, Array.Empty<Message>());
                });
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
        var nativeActor = ToNative(actor);
        var nativeNodeRid = destNodeRid.ToNative();
        var nativeSpotRid = destSpotRid.ToNative();
        return SubmitJoinNativeCore(parts, timeout, flags, ct,
            FullJoinHandlerPtr,
            (ref ZlinkMsg firstPart, nuint partCount, IntPtr handler,
                    IntPtr userData, int nativeFlags, uint timeoutMs) =>
                NativeMethods.zlink_spot_node_actor_join_spot(
                    node.Handle, ref nativeActor, ref nativeNodeRid,
                    ref nativeSpotRid, ref firstPart, partCount, handler,
                    userData, nativeFlags, timeoutMs),
            static () =>
            {
                ActorJoinResult fail = new(RequestResult.TimedOut, 0,
                    default, default, 0, 0);
                return new ActorJoinResultEnvelope(fail,
                    Array.Empty<Message>());
            });
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
        try
        {
            var tcs =
                SubmitJoinEntrySpotNative(node, actor, destNodeRid, parts,
                    timeout, flags, CancellationToken.None);
            AttachTaskCallback(() => tcs.Task,
                env => () => callback(env.Result, env.Parts),
                error =>
                {
                    ActorJoinEntrySpotResult fail =
                        new(MapRequestFailure(error), 0, default, destNodeRid,
                            default, 0, 0);
                    return () => callback(fail, Array.Empty<Message>());
                },
                () =>
                {
                    ActorJoinEntrySpotResult fail = new(
                        RequestResult.Terminated, 0, default, destNodeRid,
                        default, 0, 0);
                    callback(fail, Array.Empty<Message>());
                });
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
        var nativeActor = ToNative(actor);
        var nativeNodeRid = destNodeRid.ToNative();
        return SubmitJoinNativeCore(parts, timeout, flags, ct,
            JoinEntrySpotHandlerPtr,
            (ref ZlinkMsg firstPart, nuint partCount, IntPtr handler,
                    IntPtr userData, int nativeFlags, uint timeoutMs) =>
                NativeMethods.zlink_spot_node_actor_join_entry_spot(
                    node.Handle, ref nativeActor, ref nativeNodeRid,
                    ref firstPart, partCount, handler, userData, nativeFlags,
                    timeoutMs),
            static () =>
            {
                ActorJoinEntrySpotResult fail = new(RequestResult.TimedOut, 0,
                    default, default, default, 0, 0);
                return new ActorJoinEntrySpotResultEnvelope(fail,
                    Array.Empty<Message>());
            });
    }

    private static TaskCompletionSource<TEnvelope> SubmitJoinNativeCore<TEnvelope>(
        IReadOnlyList<Message> parts, TimeSpan timeout, SendFlags flags,
        CancellationToken ct, IntPtr handlerPtr,
        ActorJoinSubmitter submitter, Func<TEnvelope> timeoutResult)
    {
        if (parts == null || parts.Count == 0)
            throw new ZlinkConfigException(
                ZlinkConfigException.ErrorCode.InvalidArgument);
        var timeoutMs = NormalizeTimeout(timeout);
        var cloned = RequestReplySupport.CloneParts(parts);
        var nativeParts = new ZlinkMsg[cloned.Length];
        var built = 0;
        var completion = new TaskCompletionSource<TEnvelope>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            for (var i = 0; i < cloned.Length; i++)
            {
                cloned[i].Copy().MoveTo(ref nativeParts[i]);
                built++;
            }

            RequestCallState<TEnvelope> state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
                state.SetCancellationRegistration(ct.Register(
                    static h =>
                    {
                        RequestCallState<TEnvelope>.CancelFromUserData(h);
                    }, handle));
            if (timeoutMs > 0)
                state.SetTimeoutTimer(new System.Threading.Timer(h =>
                {
                    RequestCallState<TEnvelope>.FromUserData(h)
                        ?.TrySetResult(timeoutResult());
                }, handle, (int)timeoutMs, Timeout.Infinite));

            var rc = submitter(ref nativeParts[0], (nuint)nativeParts.Length,
                handlerPtr, GCHandle.ToIntPtr(handle), (int)flags, timeoutMs);
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
