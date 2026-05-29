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
    internal const int DontWaitFlag = 1;
    internal static readonly NativeMethods.ZlinkReplyHandlerDelegate ReplyHandler =
        OnReply;
    internal static readonly IntPtr ReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(ReplyHandler);
    internal static readonly NativeMethods.ZlinkReplyHandlerDelegate NoopReplyHandler =
        OnNoopReply;
    internal static readonly IntPtr NoopReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(NoopReplyHandler);
    internal static readonly NativeMethods.ZlinkActorJoinHandlerDelegate JoinHandler =
        OnJoinReply;
    internal static readonly IntPtr JoinHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(JoinHandler);
    internal static readonly NativeMethods.ZlinkActorJoinEntrySpotHandlerDelegate
        JoinEntrySpotHandler = OnActorJoinEntrySpot;
    internal static readonly IntPtr JoinEntrySpotHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(JoinEntrySpotHandler);

    internal static Message CopyMessageFromPointer(IntPtr message)
    {
        if (message == IntPtr.Zero)
            return Message.From(ReadOnlySpan<byte>.Empty);
        nuint size = NativeMethods.zlink_msg_size(message);
        if (size == 0)
            return Message.From(ReadOnlySpan<byte>.Empty);
        IntPtr data = NativeMethods.zlink_msg_data(message);
        if (data == IntPtr.Zero)
            return Message.From(ReadOnlySpan<byte>.Empty);
        byte[] copy = new byte[(int)size];
        Marshal.Copy(data, copy, 0, copy.Length);
        return Message.From(copy);
    }

    internal static ActorReceived? RecvActor(IntPtr node, ActorRef actor,
        RecvFlags flags = RecvFlags.None)
    {
        ZlinkActorRef nativeActor = ToNative(actor);
        List<Message> parts = new();
        ActorRecvInfo? managedInfo = null;
        bool firstPart = true;
        bool transferred = false;

        try
        {
            while (true)
            {
                ZlinkMsg nativePart = default;
                int rc = NativeMethods.zlink_spot_node_actor_recv_part(node,
                    ref nativeActor,
                    out ZlinkActorRecvInfo info, ref nativePart,
                    out NativeMethods.ZlinkPartFlag hasMore,
                    (int)flags);
                if (rc != 0)
                {
                    int errno = NativeMethods.zlink_errno();
                    if (firstPart && (flags & RecvFlags.DontWait) != 0
                        && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                        return null;
                    throw ZlinkException.CreateRecvException(errno);
                }

                managedInfo ??= FromNative(ref info);
                parts.Add(Message.MoveFromNative(ref nativePart));
                firstPart = false;

                if (hasMore == NativeMethods.ZlinkPartFlag.Final)
                    break;
            }

            var result = new ActorReceived(managedInfo!, parts.ToArray());
            transferred = true;
            return result;
        }
        finally
        {
            if (!transferred)
                RequestReplySupport.DisposeParts(parts);
        }
    }

    internal static unsafe ActorReceived[] DrainActors(IntPtr node,
        IntPtr actor)
    {
        if (actor == IntPtr.Zero)
            return Array.Empty<ActorReceived>();
        ZlinkActorRef nativeActor = Marshal.PtrToStructure<ZlinkActorRef>(actor);
        ActorRef actorRef = FromNative(ref nativeActor);
        List<ActorReceived> messages = new();
        while (true)
        {
            ActorReceived? message = RecvActor(node, actorRef,
                RecvFlags.DontWait);
            if (message == null)
                break;
            messages.Add(message);
        }
        return messages.ToArray();
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
            RequestCallState.TimeoutFromUserData(userdata);
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
                    ? (RequestResult)requestError.Code
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

    internal static void SubmitAndWait(IntPtr progressHandle,
        Func<IntPtr, int> submit)
        => SubmitAndWaitCore(submit,
            task => RequestProgressPump.AttachSpot(progressHandle, task));

    internal static void SubmitAndWaitSocket(IntPtr progressHandle,
        Func<IntPtr, int> submit)
        => SubmitAndWaitCore(submit,
            task => RequestProgressPump.AttachSocket(progressHandle, task));

    private static void SubmitAndWaitCore(
        Func<IntPtr, int> submit,
        Func<Task<Received>, Task<Received>> attachProgress)
    {
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        bool submitted = false;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            int rc = submit(GCHandle.ToIntPtr(handle));
            if (rc != 0)
            {
                handle.Free();
                handle = default;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            submitted = true;
            Received received = attachProgress(completion.Task)
                .GetAwaiter()
                .GetResult();
            received.Dispose();
        }
        catch
        {
            if (!submitted && handle.IsAllocated)
                handle.Free();
            throw;
        }
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

    internal static void OnJoinReply(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        int result = resultPtr == IntPtr.Zero
            ? (int)RequestResult.InternalError
            : Marshal.PtrToStructure<ZlinkActorJoinResult>(resultPtr).Result;
        OnReply(result, parts, partCount, userData);
    }

    internal static void OnNoopReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        if (parts != IntPtr.Zero)
            NativeMethods.zlink_multipart_close(parts, partCount);
    }

    // --- Operation builder helpers ---

    internal static readonly NativeMethods.ZlinkActorLookupHandlerDelegate
        LookupHandler = OnLookupReply;

    internal static readonly IntPtr LookupHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(LookupHandler);

    internal sealed class ActorJoinCallState
    {
        public TaskCompletionSource<ActorJoinResultEnvelope> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorJoinCallState(
            TaskCompletionSource<ActorJoinResultEnvelope> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorLookupCallState
    {
        public TaskCompletionSource<ActorLookupResult> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorLookupCallState(
            TaskCompletionSource<ActorLookupResult> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal sealed class ActorJoinEntrySpotCallState
    {
        public TaskCompletionSource<ActorJoinEntrySpotResult> Completion { get; }
        public CancellationTokenRegistration? CancelReg { get; set; }
        public System.Threading.Timer? TimeoutTimer { get; set; }

        public ActorJoinEntrySpotCallState(
            TaskCompletionSource<ActorJoinEntrySpotResult> completion)
        {
            Completion = completion;
        }

        public void Cleanup()
        {
            CancelReg?.Dispose();
            TimeoutTimer?.Dispose();
        }
    }

    internal readonly struct ActorJoinResultEnvelope
    {
        public ActorJoinResult Result { get; }
        public IReadOnlyList<Message> Parts { get; }

        public ActorJoinResultEnvelope(ActorJoinResult result,
            IReadOnlyList<Message> parts)
        {
            Result = result;
            Parts = parts;
        }
    }

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

    internal static void OnActorJoinEntrySpot(IntPtr resultPtr,
        IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorJoinEntrySpotCallState state =
            (ActorJoinEntrySpotCallState)handle.Target!;
        try
        {
            if (resultPtr == IntPtr.Zero)
            {
                state.Completion.TrySetResult(new ActorJoinEntrySpotResult(
                    RequestResult.InternalError, default, default, 0, 0));
                return;
            }

            ZlinkActorJoinEntrySpotResult native = Marshal.PtrToStructure
                <ZlinkActorJoinEntrySpotResult>(resultPtr);
            ActorRef returnedActor =
                (RequestResult)native.Result == RequestResult.Ok
                    ? FromNative(ref native.Actor)
                    : default;
            RoutingId targetNodeRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.TargetNodeRid));
            ActorJoinEntrySpotResult result = new(
                (RequestResult)native.Result, returnedActor, targetNodeRid,
                native.JoinEpoch, native.Flags);
            state.Completion.TrySetResult(result);
        }
        finally
        {
            state.Cleanup();
            handle.Free();
        }
    }

    internal static void OnActorJoinFull(IntPtr resultPtr, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorJoinCallState state = (ActorJoinCallState)handle.Target!;
        try
        {
            if (resultPtr == IntPtr.Zero)
            {
                ActorJoinResult fail = new(RequestResult.InternalError, 0,
                    default, default, 0, 0);
                state.Completion.TrySetResult(
                    new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }
            ZlinkActorJoinResult native = Marshal.PtrToStructure
                <ZlinkActorJoinResult>(resultPtr);
            if ((RequestResult)native.Result != RequestResult.Ok)
            {
                ActorJoinResult fail = new((RequestResult)native.Result,
                    native.JoinResultCode, default, default, native.JoinEpoch,
                    native.Flags);
                state.Completion.TrySetResult(
                    new ActorJoinResultEnvelope(fail,
                        Array.Empty<Message>()));
                return;
            }

            ActorRef returnedActor = FromNative(ref native.Actor);
            RoutingId joinedSpot = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.JoinedSpotRid));
            ActorJoinResult result = new((RequestResult)native.Result,
                native.JoinResultCode, returnedActor, joinedSpot,
                native.JoinEpoch, native.Flags);
            Message[] replyParts = parts != IntPtr.Zero
                ? Message.FromNativeVector(parts, partCount)
                : Array.Empty<Message>();
            parts = IntPtr.Zero;
            partCount = 0;
            state.Completion.TrySetResult(
                new ActorJoinResultEnvelope(result, replyParts));
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            state.Cleanup();
            handle.Free();
        }
    }

    internal static readonly NativeMethods.ZlinkActorJoinHandlerDelegate
        FullJoinHandler = OnActorJoinFull;

    internal static readonly IntPtr FullJoinHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(FullJoinHandler);

    internal static Task<IReadOnlyList<Message>> LeaveActorAsync(SpotNode node,
        ActorRef actor, RoutingId currentSpotRid, TimeSpan timeout,
        CancellationToken ct)
    {
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkActorRef nativeActor = ToNative(actor);
        ZlinkRoutingId nativeSpotRid = currentSpotRid.ToNative();
        return SubmitReplyAsync(node.Handle, ct, (userData, handler) =>
            NativeMethods.zlink_spot_node_actor_leave_spot(node.Handle,
                ref nativeActor, ref nativeSpotRid, handler, userData,
                timeoutMs), attachSpot: true);
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
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkActorRef nativeActor = ToNative(actor);
        return SubmitReplyAsync(node.Handle, ct, (userData, handler) =>
            NativeMethods.zlink_spot_node_actor_destroy(node.Handle,
                ref nativeActor, handler, userData, timeoutMs),
            attachSpot: true);
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
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkRoutingId nativeSession = sessionRid.ToNative();
        ZlinkActorRef nativeActor = ToNative(actor);
        IntPtr handle = stream.Handle;
        return SubmitReplyAsync(handle, ct, (userData, handler) =>
            NativeMethods.zlink_stream_bind_actor(handle, ref nativeSession,
                ref nativeActor, handler, userData, timeoutMs),
            attachSpot: false);
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
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkRoutingId nativeSession = sessionRid.ToNative();
        IntPtr handle = stream.Handle;
        return SubmitReplyAsync(handle, ct, (userData, handler) =>
            NativeMethods.zlink_stream_unbind_actor(handle, ref nativeSession,
                actorId, handler, userData, timeoutMs), attachSpot: false);
    }

    internal static bool UnbindActorCallback(StreamSocket stream,
        RoutingId sessionRid, string actorId, TimeSpan timeout,
        ReplyHandler callback)
    {
        return SubmitReplyCallback(() => UnbindActorAsync(stream, sessionRid,
            actorId, timeout, CancellationToken.None), callback);
    }

    internal delegate int NativeSubmitFunc(IntPtr userData, IntPtr handler);

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
            {
                state.SetCancellationRegistration(ct.Register(static userdata =>
                {
                    RequestCallState.CancelFromUserData(userdata);
                }, handle));
            }
            int rc = submitter(GCHandle.ToIntPtr(handle), ReplyHandlerPtr);
            if (rc != 0)
            {
                handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
            Task<Received> attached = attachSpot
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
        SynchronizationContext? syncCtx = SynchronizationContext.Current;
        try
        {
            _ = invoker().ContinueWith(t =>
            {
                RequestResult result;
                IReadOnlyList<Message> parts;
                if (t.IsFaulted)
                {
                    Exception err = t.Exception!.GetBaseException();
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

    internal static Task<ActorLookupResult> RemoteActorGetRefAsync(
        SpotNode node, RoutingId targetNodeRid, string actorId,
        TimeSpan timeout, CancellationToken ct)
    {
        ValidateActorId(actorId, nameof(actorId));
        uint timeoutMs = NormalizeTimeout(timeout);
        ZlinkRoutingId nativeNodeRid = targetNodeRid.ToNative();
        TaskCompletionSource<ActorLookupResult> completion = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        try
        {
            ActorLookupCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            if (ct.CanBeCanceled)
            {
                state.CancelReg = ct.Register(static h =>
                {
                    GCHandle gh = (GCHandle)h!;
                    if (gh.Target is ActorLookupCallState s)
                    {
                        if (s.Completion.TrySetCanceled())
                            s.Cleanup();
                    }
                }, handle);
            }
            int rc = NativeMethods.zlink_remote_actor_get_ref(node.Handle,
                ref nativeNodeRid, actorId, LookupHandlerPtr,
                GCHandle.ToIntPtr(handle), timeoutMs);
            if (rc != 0)
            {
                handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
            return completion.Task;
        }
        catch
        {
            if (handle.IsAllocated)
                handle.Free();
            throw;
        }
    }

    internal static bool RemoteActorGetRefCallback(SpotNode node,
        RoutingId targetNodeRid, string actorId, TimeSpan timeout,
        ActorLookupHandler callback)
    {
        SynchronizationContext? syncCtx = SynchronizationContext.Current;
        try
        {
            _ = RemoteActorGetRefAsync(node, targetNodeRid, actorId, timeout,
                CancellationToken.None).ContinueWith(t =>
            {
                ActorLookupResult r;
                if (t.IsFaulted)
                {
                    Exception err = t.Exception!.GetBaseException();
                    RequestResult rr = err is ZlinkRequestException re
                        ? (RequestResult)re.Code
                        : RequestResult.InternalError;
                    r = new ActorLookupResult(rr, default, 0);
                }
                else if (t.IsCanceled)
                {
                    r = new ActorLookupResult(RequestResult.Terminated,
                        default, 0);
                }
                else
                {
                    r = t.Result;
                }
                CallbackDelivery.Post(syncCtx, () => callback(r));
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

    internal static void OnLookupReply(IntPtr resultPtr, IntPtr userData)
    {
        GCHandle handle = GCHandle.FromIntPtr(userData);
        ActorLookupCallState state = (ActorLookupCallState)handle.Target!;
        try
        {
            ActorLookupResult r;
            if (resultPtr == IntPtr.Zero)
            {
                r = new ActorLookupResult(RequestResult.InternalError,
                    default, 0);
            }
            else
            {
                ZlinkActorLookupResult native = Marshal.PtrToStructure
                    <ZlinkActorLookupResult>(resultPtr);
                RequestResult result = (RequestResult)native.Result;
                ActorRef actor = default;
                if (result == RequestResult.Ok)
                {
                    ActorRef? parsedActor = FromOptionalNative(ref native.Actor);
                    if (parsedActor is null)
                    {
                        result = RequestResult.InternalError;
                    }
                    else
                    {
                        actor = parsedActor.Value;
                    }
                }

                r = new ActorLookupResult(result, actor, native.Flags);
            }
            state.Completion.TrySetResult(r);
        }
        finally
        {
            state.Cleanup();
            handle.Free();
        }
    }
}
