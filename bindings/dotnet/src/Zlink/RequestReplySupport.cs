// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
namespace Zlink;

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

    internal static Received CloneReceived(Received source)
    {
        Message[] parts = new Message[source.Parts.Count];
        for (int i = 0; i < source.Parts.Count; i++)
            parts[i] = CloneMessage(source.Parts[i]);
        return Received.Create(source.RoutingId, parts, source.RequestSeq,
            source.SpotRid);
    }

    internal static void DisposeParts(IEnumerable<Message> parts)
    {
        foreach (Message part in parts)
            part.Dispose();
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
                    CallbackDelivery.Post(context, () => callback(
                        requestError.Result, null));
                    return;
                }

                CallbackDelivery.Post(context, () => callback(
                    RequestResult.ProtocolError, null));
                return;
            }
            if (task.IsCanceled)
            {
                CallbackDelivery.Post(context, () => callback(
                    RequestResult.Terminated, null));
                return;
            }
            CallbackDelivery.Post(context, () => callback(RequestResult.Ok,
                task.Result));
        }, TaskScheduler.Default);
    }

    internal static unsafe void MovePartsToNative(IReadOnlyList<Message> parts,
        out global::Zlink.Native.ZlinkMsg[] nativeParts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));

        nativeParts = new global::Zlink.Native.ZlinkMsg[parts.Count];
        int built = 0;
        try
        {
            for (int i = 0; i < parts.Count; i++)
            {
                Message part = parts[i]
                    ?? throw new ArgumentNullException(nameof(parts),
                        "parts must not contain null");
                part.MoveTo(ref nativeParts[i]);
                built++;
            }
        }
        catch
        {
            RestoreManagedParts(parts, nativeParts, built);
            throw;
        }
    }

    internal static void RestoreManagedParts(IReadOnlyList<Message> parts,
        global::Zlink.Native.ZlinkMsg[] nativeParts, int built)
    {
        for (int i = built - 1; i >= 0; i--)
        {
            try
            {
                parts[i].RestoreFrom(ref nativeParts[i]);
            }
            catch
            {
            }
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
