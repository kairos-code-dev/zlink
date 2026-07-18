// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

// Slim ActorRef marshalling + STREAM session bind/unbind async-reply plumbing.
// Built entirely on the surviving request/reply infrastructure
// (RequestCallState / RequestProgressPump / RequestReplySupport / Received) so
// that the raw STREAM socket keeps its session-to-actor bind feature.
internal static class ActorInterop
{
    private static readonly NativeMethods.ZlinkReplyHandlerDelegate NativeReplyHandler =
        OnReply;

    private static readonly IntPtr ReplyHandlerPtr =
        Marshal.GetFunctionPointerForDelegate(NativeReplyHandler);

    internal static void ValidateActorId(string actorId, string paramName)
    {
        ActorContractValidation.ValidateActorId(actorId, paramName);
    }

    internal static uint NormalizeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }

    internal static unsafe ActorRef FromNative(ref ZlinkActorRef native)
    {
        fixed (byte* actorId = native.ActorId)
        {
            var nodeRid = RoutingId.From(
                NativeHelpers.ReadRoutingId(ref native.NodeRid));
            return new ActorRef(nodeRid,
                NativeHelpers.ReadFixedString(actorId, 256),
                native.Generation);
        }
    }

    internal static unsafe ZlinkActorRef ToNative(ActorRef actor)
    {
        ZlinkActorRef native = default;
        if (!actor.NodeRid.IsEmpty)
            native.NodeRid = actor.NodeRid.ToNative();
        var actorId = native.ActorId;
        NativeHelpers.WriteFixedString(actor.ActorId, actorId, 256);
        native.Generation = actor.Generation;
        return native;
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
                ref nativeActor, handler, userData, timeoutMs));
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
                actorId, handler, userData, timeoutMs));
    }

    internal static bool UnbindActorCallback(StreamSocket stream,
        RoutingId sessionRid, string actorId, TimeSpan timeout,
        ReplyHandler callback)
    {
        return SubmitReplyCallback(() => UnbindActorAsync(stream, sessionRid,
            actorId, timeout, CancellationToken.None), callback);
    }

    private static void OnReply(int result, IntPtr parts, nuint partCount,
        IntPtr userData)
    {
        RequestReplySupport.CompleteReceivedReply(result, parts, partCount,
            userData);
    }

    private delegate int NativeSubmitFunc(IntPtr userData, IntPtr handler);

    private static Task<IReadOnlyList<Message>> SubmitReplyAsync(
        IntPtr socketHandle, CancellationToken ct, NativeSubmitFunc submitter)
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
                    ct.Register(
                        static userdata =>
                        {
                            RequestCallState.CancelFromUserData(userdata);
                        },
                        handle));
            var rc = submitter(GCHandle.ToIntPtr(handle), ReplyHandlerPtr);
            if (rc != 0)
            {
                handle.Free();
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }

            var attached =
                RequestProgressPump.AttachSocket(socketHandle, completion.Task);
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
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        try
        {
            var context = SynchronizationContext.Current;
            _ = invoker().ContinueWith(task =>
            {
                Action delivery;
                if (task.IsFaulted)
                {
                    var error = task.Exception?.GetBaseException()
                                ?? new ZlinkRequestException(
                                    RequestResult.InternalError);
                    var result = error is ZlinkRequestException requestError
                        ? (RequestResult)requestError.Code
                        : RequestResult.InternalError;
                    delivery = () => callback(result, Array.Empty<Message>());
                }
                else if (task.IsCanceled)
                {
                    delivery = () => callback(RequestResult.Terminated,
                        Array.Empty<Message>());
                }
                else
                {
                    var parts = task.Result;
                    delivery = () => callback(RequestResult.Ok, parts);
                }

                CallbackDelivery.Post(context, delivery);
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

    private static Task<IReadOnlyList<Message>> TakePartsAsync(
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
}

internal sealed class ActorBindOperationImpl : ActorBindOperation
{
    private readonly ActorRef _actor;
    private readonly RoutingId _sessionRid;
    private readonly StreamSocket _stream;
    private OperationSubmissionGuard _submission;
    private TimeSpan _timeout;

    internal ActorBindOperationImpl(StreamSocket stream, RoutingId sessionRid,
        ActorRef actor)
    {
        _stream = stream;
        _sessionRid = sessionRid;
        _actor = actor;
    }

    public ActorBindOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> Async(CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmittedAfterValidation();
        return ActorInterop.BindActorAsync(_stream, _sessionRid, _actor,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmittedAfterValidation();
        return ActorInterop.BindActorCallback(_stream, _sessionRid, _actor,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}

internal sealed class ActorUnbindOperationImpl : ActorUnbindOperation
{
    private readonly string _actorId;
    private readonly RoutingId _sessionRid;
    private readonly StreamSocket _stream;
    private OperationSubmissionGuard _submission;
    private TimeSpan _timeout;

    internal ActorUnbindOperationImpl(StreamSocket stream, RoutingId sessionRid,
        string actorId)
    {
        _stream = stream;
        _sessionRid = sessionRid;
        _actorId = actorId;
    }

    public ActorUnbindOperation Timeout(TimeSpan timeout)
    {
        EnsureNotSubmitted();
        _timeout = timeout;
        return this;
    }

    public Task<IReadOnlyList<Message>> Async(CancellationToken ct = default)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmittedAfterValidation();
        return ActorInterop.UnbindActorAsync(_stream, _sessionRid, _actorId,
            _timeout, ct);
    }

    public bool Submit(ReplyHandler callback)
    {
        EnsureNotSubmitted();
        _submission.MarkSubmittedAfterValidation();
        return ActorInterop.UnbindActorCallback(_stream, _sessionRid, _actorId,
            _timeout, callback);
    }

    private void EnsureNotSubmitted()
    {
        _submission.EnsureNotSubmitted();
    }
}
