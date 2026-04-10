// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

internal static class RequestReplySupport
{
    private const int ErrnoEAgainWin = 35;
    private const int ErrnoEWouldBlockWin = 10035;

    internal static Message[] ToArray(IReadOnlyList<Message> parts)
    {
        Message[] array = new Message[parts.Count];
        for (int i = 0; i < parts.Count; i++)
            array[i] = parts[i];
        return array;
    }

    internal static Message CloneMessage(Message source)
    {
        return Message.FromBytes(source.AsReadOnlySpan());
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
        return new Received(source.RoutingId, parts, source.RequestSequence,
            source.HasRequestSequence);
    }

    internal static void DisposeParts(IEnumerable<Message> parts)
    {
        foreach (Message part in parts)
            part.Dispose();
    }

    internal static void AttachCallback(Func<Task<Received>> invoke,
        RequestReplyCallback onReply, RequestErrorCallback onError)
    {
        if (onReply == null)
            throw new ArgumentNullException(nameof(onReply));
        if (onError == null)
            throw new ArgumentNullException(nameof(onError));
        SynchronizationContext? context = SynchronizationContext.Current;
        _ = invoke().ContinueWith(task =>
        {
            if (task.IsFaulted)
            {
                ZlinkException error = task.Exception?.GetBaseException() as ZlinkException
                    ?? new ZlinkException((int)ErrorCode.Unknown,
                        task.Exception?.GetBaseException().Message ?? "request failed");
                CallbackDelivery.Post(context, () => onError(error));
                return;
            }
            if (task.IsCanceled)
            {
                CallbackDelivery.Post(context, () => onError(
                    new ZlinkException((int)ErrorCode.ETimedOut, "request canceled")));
                return;
            }
            CallbackDelivery.Post(context, () => onReply(task.Result));
        }, TaskScheduler.Default);
    }

    internal static unsafe void MovePartsToNative(IReadOnlyList<Message> parts,
        out Zlink.Native.ZlinkMsg[] nativeParts)
    {
        if (parts == null)
            throw new ArgumentNullException(nameof(parts));
        if (parts.Count == 0)
            throw new ArgumentException("parts must not be empty", nameof(parts));

        nativeParts = new Zlink.Native.ZlinkMsg[parts.Count];
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
        Zlink.Native.ZlinkMsg[] nativeParts, int built)
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

    internal static SendResult MapTrySendResult(ZlinkException error)
    {
        ErrorCode code = ZlinkException.MapErrorCode(error.Errno);
        return code switch
        {
            ErrorCode.EAgain => SendResult.Backpressured,
            _ when error.Errno == ErrnoEAgainWin ||
                error.Errno == ErrnoEWouldBlockWin => SendResult.Backpressured,
            _ => throw error
        };
    }
}
