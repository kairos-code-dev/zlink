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


}
