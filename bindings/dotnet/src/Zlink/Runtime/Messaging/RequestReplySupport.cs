// SPDX-License-Identifier: MPL-2.0

using System.Buffers;
using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RequestReplySupport
{
    private const int ErrnoEAgainWin = 35;
    private const int ErrnoEWouldBlockWin = 10035;
    private const int StackPartLimit = 8;

    internal static Message CloneMessage(Message source)
    {
        return source.Copy();
    }

    internal static Message[] CloneParts(IReadOnlyList<Message> parts)
    {
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));
        var cloned = new Message[parts.Count];
        for (var i = 0; i < parts.Count; i++)
            cloned[i] = CloneMessage(parts[i]);
        return cloned;
    }

    internal static void EnsureParts(IReadOnlyList<Message> parts,
        string paramName)
    {
        if (parts == null)
            throw new ArgumentNullException(paramName);
        if (parts.Count == 0)
            throw new ArgumentException("Parts must not be empty.", paramName);
    }

    internal static uint NormalizeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }

    internal static uint NormalizeRequestTimeout(TimeSpan timeout,
        TimeSpan defaultTimeout)
    {
        var effective = timeout == TimeSpan.Zero
            ? defaultTimeout
            : timeout;
        return BoundaryValidation.EncodeTimeoutMilliseconds(effective,
            nameof(timeout));
    }

    internal static IReadOnlyList<Message> TakeOwnedParts(Received source)
    {
        return source.TakePartsOwnership();
    }

    internal static void DisposeParts(IEnumerable<Message> parts)
    {
        foreach (var part in parts)
            part.Dispose();
    }

    internal static void CloneAndSubmitParts(IReadOnlyList<Message> parts,
        NativePartSubmitter submit)
    {
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        var cloned = CloneParts(parts);
        try
        {
            SubmitClonedParts(cloned, submit);
        }
        catch
        {
            DisposeParts(cloned);
            throw;
        }
    }

    internal static void SubmitClonedParts(IReadOnlyList<Message> parts,
        NativePartSubmitter submit)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        for (var i = 0; i < parts.Count; i++)
        {
            ZlinkMsg nativePart = default;
            parts[i].MoveTo(ref nativePart);
            var submitted = false;
            try
            {
                var rc = submit(ref nativePart, i + 1 < parts.Count
                    ? NativeMethods.ZlinkPartFlag.More
                    : NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc != 0)
                    throw ZlinkException.CreateSubmitException(
                        NativeMethods.zlink_errno());
            }
            finally
            {
                if (!submitted)
                    NativeMethods.zlink_msg_close(ref nativePart);
            }
        }
    }

    internal static void SubmitOwnedSinglePart(Message part,
        NativePartSubmitter submit)
    {
        if (part == null)
            throw new ArgumentNullException(nameof(part));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        ZlinkMsg nativePart = default;
        var shouldRestore = false;
        try
        {
            part.MoveTo(ref nativePart);
            shouldRestore = true;
            var rc = submit(ref nativePart, NativeMethods.ZlinkPartFlag.Final);
            if (rc == 0)
            {
                shouldRestore = false;
                return;
            }

            var errno = NativeMethods.zlink_errno();
            part.RestoreFrom(ref nativePart);
            shouldRestore = false;
            throw ZlinkException.CreateSubmitException(errno);
        }
        catch
        {
            if (shouldRestore)
                part.RestoreFrom(ref nativePart);
            throw;
        }
    }

    internal static void SubmitOwnedParts(IReadOnlyList<Message> parts,
        NativePartSubmitter submit)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        if (parts.Count == 1)
        {
            SubmitOwnedSinglePart(parts[0], submit);
            return;
        }

        Message[]? copiedParts = null;
        var sourceParts = PartsAsSpan(parts, ref copiedParts);
        ZlinkMsg[]? rentedNative = null;
        var nativeParts = sourceParts.Length <= StackPartLimit
            ? stackalloc ZlinkMsg[StackPartLimit]
            : rentedNative = ArrayPool<ZlinkMsg>.Shared.Rent(sourceParts.Length);
        nativeParts = nativeParts[..sourceParts.Length];

        var built = 0;
        var submitted = 0;
        try
        {
            NativeMessageParts.MoveToNative(sourceParts, nativeParts,
                nameof(parts), ref built);
            for (var i = 0; i < built; i++)
            {
                var rc = submit(ref nativeParts[i], i + 1 < built
                    ? NativeMethods.ZlinkPartFlag.More
                    : NativeMethods.ZlinkPartFlag.Final);
                submitted = i + 1;
                if (rc == 0)
                    continue;

                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
        }
        catch
        {
            NativeMessageParts.RestoreManaged(sourceParts, nativeParts,
                submitted, built - submitted);
            throw;
        }
        finally
        {
            if (rentedNative != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rentedNative);
        }
    }

    private static ReadOnlySpan<Message> PartsAsSpan(IReadOnlyList<Message> parts,
        ref Message[]? copiedParts)
    {
        if (parts is Message[] array)
            return array;

        copiedParts = new Message[parts.Count];
        for (var i = 0; i < copiedParts.Length; i++)
            copiedParts[i] = parts[i];
        return copiedParts;
    }

    internal static void AttachResultCallback(Func<Task<Received>> invoke,
        Action<RequestResult, Received?> callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        var context = SynchronizationContext.Current;
        _ = invoke().ContinueWith(task =>
            {
                if (task.IsFaulted)
                {
                    var error = task.Exception?.GetBaseException()
                                ?? new ZlinkRequestException(RequestResult.ProtocolError);
                    if (error is ZlinkRequestException requestError)
                    {
                        DeliverCallback(context, () => callback(
                            (RequestResult)requestError.Code, null));
                        return;
                    }

                    if (error is ZlinkSubmitException submitError)
                    {
                        DeliverCallback(context, () => callback(
                            MapSubmitFailureResult(submitError), null));
                        return;
                    }

                    DeliverCallback(context, () => callback(
                        RequestResult.ProtocolError, null));
                    return;
                }

                if (task.IsCanceled)
                {
                    DeliverCallback(context, () => callback(
                        RequestResult.Terminated, null));
                    return;
                }

                DeliverCallback(context, () => callback(RequestResult.Ok,
                    task.Result));
            }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    internal static void CompleteReceivedReply(int result, IntPtr parts,
        nuint partCount, IntPtr userData)
    {
        var handle = GCHandle.FromIntPtr(userData);
        var state = (RequestCallState)handle.Target!;
        try
        {
            if (result != 0)
            {
                state.TrySetException(new ZlinkRequestException(
                    (RequestResult)result));
                return;
            }

            var replyParts = Message.FromNativeVector(parts, partCount);
            parts = IntPtr.Zero;
            partCount = 0;
            var received = Received.Create(null, replyParts);
            if (!state.TrySetResult(received))
                DisposeParts(replyParts);
        }
        finally
        {
            if (parts != IntPtr.Zero)
                NativeMethods.zlink_multipart_close(parts, partCount);
            handle.Free();
        }
    }

    private static void DeliverCallback(SynchronizationContext? context,
        Action action)
    {
        if (context != null)
        {
            CallbackDelivery.Post(context, action);
            return;
        }

        try
        {
            action();
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
        }
    }

    internal static SendResult MapSendNoWaitResult(ZlinkException error)
    {
        var code = ZlinkException.MapErrorCode(error.NativeErrno);
        return code switch
        {
            ErrorCode.EAgain => SendResult.Backpressured,
            _ when error.NativeErrno == ErrnoEAgainWin ||
                   error.NativeErrno == ErrnoEWouldBlockWin =>
                SendResult.Backpressured,
            _ => throw error
        };
    }

    private static RequestResult MapSubmitFailureResult(ZlinkSubmitException error)
    {
        return error.Result switch
        {
            ZlinkSubmitException.ErrorCode.NotConnected => RequestResult.NotConnected,
            ZlinkSubmitException.ErrorCode.NotFound => RequestResult.NotFound,
            ZlinkSubmitException.ErrorCode.NotAdmitted
                or ZlinkSubmitException.ErrorCode.InvalidState => RequestResult.Rejected,
            ZlinkSubmitException.ErrorCode.Backpressured => RequestResult.Busy,
            ZlinkSubmitException.ErrorCode.InvalidArgument => RequestResult.InvalidArgument,
            ZlinkSubmitException.ErrorCode.NotSupported => RequestResult.NotSupported,
            ZlinkSubmitException.ErrorCode.Terminated => RequestResult.Terminated,
            _ => RequestResult.InternalError
        };
    }

    internal delegate int NativePartSubmitter(
        ref ZlinkMsg nativePart, NativeMethods.ZlinkPartFlag partFlag);
}
