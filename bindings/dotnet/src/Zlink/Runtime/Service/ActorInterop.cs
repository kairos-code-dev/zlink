// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

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

    internal static readonly NativeMethods.ZlinkActorJoinEntrySpotHandlerDelegate
        JoinEntrySpotHandler = OnActorJoinEntrySpot;

    internal static readonly IntPtr JoinEntrySpotHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(JoinEntrySpotHandler);

    internal static ActorReceived? RecvActor(SpotNode node, ActorRef actor,
        RecvFlags flags = RecvFlags.None)
    {
        var pending = node.ActorInbox.Take(actor);
        var nativeActor = ToNative(actor);
        List<Message> parts = pending?.Parts ?? new List<Message>();
        ActorRecvInfo? managedInfo = pending?.Info;
        var firstPart = parts.Count == 0;
        var transferred = false;

        try
        {
            while (true)
            {
                ZlinkMsg nativePart = default;
                var rc = NativeMethods.zlink_spot_node_actor_recv_part(node.Handle,
                    ref nativeActor,
                    out var info, ref nativePart,
                    out var hasMore,
                    (int)flags);
                if (rc != 0)
                {
                    var errno = NativeMethods.zlink_errno();
                    var error = ZlinkException.CreateRecvException(errno);
                    if ((flags & RecvFlags.DontWait) != 0
                        && error.Result == ZlinkRecvException.ErrorCode.NoData)
                    {
                        if (!firstPart && managedInfo != null)
                        {
                            node.ActorInbox.Store(actor, managedInfo, parts);
                            transferred = true;
                        }

                        return null;
                    }

                    throw error;
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

    internal static ActorReceived[] DrainActors(SpotNode node,
        IntPtr actor)
    {
        if (actor == IntPtr.Zero)
            return Array.Empty<ActorReceived>();
        var nativeActor = Marshal.PtrToStructure<ZlinkActorRef>(actor);
        var actorRef = FromNative(ref nativeActor);
        List<ActorReceived> messages = new();
        while (true)
        {
            ActorReceived? message;
            try
            {
                message = RecvActor(node, actorRef, RecvFlags.DontWait);
            }
            catch (ZlinkRecvException error)
                when (error.Result == ZlinkRecvException.ErrorCode.InvalidHandle)
            {
                // Actor dispatch notifications can outlive an actor that was
                // destroyed after the native event was queued. The stale
                // notification has no remaining message to drain.
                break;
            }
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
            var received = completed.Result;
            try
            {
                return received.TakePartsOwnership();
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
            parts = received.TakePartsOwnership();
            received.Dispose();
        }

        callback(result, parts);
    }

    internal static System.Threading.Timer? CreateTimeoutTimer(GCHandle handle,
        uint timeoutMs)
    {
        if (timeoutMs == 0)
            return null;
        return new System.Threading.Timer(static userdata => { RequestCallState.TimeoutFromUserData(userdata); },
            handle, (int)timeoutMs, Timeout.Infinite);
    }

    internal static void AttachPartsCallback(
        Func<Task<IReadOnlyList<Message>>> invoke,
        Action<RequestResult, IReadOnlyList<Message>> callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        AttachTaskCallback(invoke,
            parts => () => callback(RequestResult.Ok, parts),
            error => () => callback(MapRequestFailure(error),
                Array.Empty<Message>()),
            () => callback(RequestResult.Terminated, Array.Empty<Message>()));
    }

    internal static void AttachTaskCallback<T>(
        Func<Task<T>> invoke,
        Func<T, Action> onSuccess,
        Func<Exception, Action> onFault,
        Action onCanceled)
    {
        var context = SynchronizationContext.Current;
        _ = invoke().ContinueWith(task =>
        {
            Action delivery;
            if (task.IsFaulted)
            {
                var error = task.Exception?.GetBaseException()
                            ?? new ZlinkRequestException(
                                RequestResult.InternalError);
                delivery = onFault(error);
            }
            else if (task.IsCanceled)
            {
                delivery = onCanceled;
            }
            else
            {
                delivery = onSuccess(task.Result);
            }

            CallbackDelivery.Post(context, delivery);
        }, TaskScheduler.Default);
    }

    internal static RequestResult MapRequestFailure(Exception error)
    {
        return error is ZlinkRequestException requestError
            ? (RequestResult)requestError.Code
            : RequestResult.InternalError;
    }

    internal static void SubmitAndWait(IntPtr progressHandle,
        Func<IntPtr, int> submit)
    {
        SubmitAndWaitCore(submit,
            task => RequestProgressPump.AttachSpot(progressHandle, task));
    }

    internal static void SubmitAndWaitSocket(IntPtr progressHandle,
        Func<IntPtr, int> submit)
    {
        SubmitAndWaitCore(submit,
            task => RequestProgressPump.AttachSocket(progressHandle, task));
    }

    private static void SubmitAndWaitCore(
        Func<IntPtr, int> submit,
        Func<Task<Received>, Task<Received>> attachProgress)
    {
        var completion = new TaskCompletionSource<Received>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        GCHandle handle = default;
        var submitted = false;
        try
        {
            RequestCallState state = new(completion);
            handle = GCHandle.Alloc(state, GCHandleType.Normal);
            var rc = submit(GCHandle.ToIntPtr(handle));
            if (rc != 0)
            {
                handle.Free();
                handle = default;
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            submitted = true;
            var received = attachProgress(completion.Task)
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
}
