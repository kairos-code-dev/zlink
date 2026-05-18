// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class RequestReplySupport
{
    private const int ErrnoEAgainWin = 35;
    private const int ErrnoEWouldBlockWin = 10035;

    internal static Message CloneMessage(Message source)
    {
        return source.Copy();
    }

    internal static Message[] CloneParts(IReadOnlyList<Message> parts)
    {
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));
        Message[] cloned = new Message[parts.Count];
        for (int i = 0; i < parts.Count; i++)
            cloned[i] = CloneMessage(parts[i]);
        return cloned;
    }

    internal static IReadOnlyList<Message> TakeOwnedParts(Received source)
    {
        return source.TakePartsOwnership();
    }

    internal static void DisposeParts(IEnumerable<Message> parts)
    {
        foreach (Message part in parts)
            part.Dispose();
    }

    internal unsafe delegate int NativePartSubmitter(
        ref ZlinkMsg nativePart, NativeMethods.ZlinkPartFlag partFlag);

    internal static unsafe void CloneAndSubmitParts(IReadOnlyList<Message> parts,
        NativePartSubmitter submit)
    {
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        Message[] cloned = CloneParts(parts);
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

    internal static unsafe void SubmitClonedParts(IReadOnlyList<Message> parts,
        NativePartSubmitter submit)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));
        if (submit == null)
            throw new ArgumentNullException(nameof(submit));

        for (int i = 0; i < parts.Count; i++)
        {
            ZlinkMsg nativePart = default;
            parts[i].MoveTo(ref nativePart);
            bool submitted = false;
            try
            {
                int rc = submit(ref nativePart, i + 1 < parts.Count
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

    internal static void AttachResultCallback(Func<Task<Received>> invoke,
        Action<RequestResult, Received?> callback)
    {
        if (callback == null)
            throw new ArgumentNullException(nameof(callback));
        SynchronizationContext? context = SynchronizationContext.Current;
        _ = invoke().ContinueWith(task =>
        {
            if (task.IsFaulted)
            {
                Exception error = task.Exception?.GetBaseException()
                    ?? new ZlinkRequestException(RequestResult.ProtocolError);
                if (error is ZlinkRequestException requestError)
                {
                    DeliverCallback(context, () => callback(
                        (RequestResult)requestError.Code, null));
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
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    internal static SendResult MapSendNoWaitResult(ZlinkException error)
    {
        ErrorCode code = ZlinkException.MapErrorCode(error.InternalErrno);
        return code switch
        {
            ErrorCode.EAgain => SendResult.Backpressured,
            _ when error.InternalErrno == ErrnoEAgainWin ||
                error.InternalErrno == ErrnoEWouldBlockWin =>
                SendResult.Backpressured,
            _ => throw error
        };
    }
}
