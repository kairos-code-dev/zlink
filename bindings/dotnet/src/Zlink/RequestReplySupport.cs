// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

internal static class RequestReplySupport
{
    internal static Message[] ToArray(IReadOnlyList<Message> parts)
    {
        Message[] array = new Message[parts.Count];
        for (int i = 0; i < parts.Count; i++)
            array[i] = parts[i];
        return array;
    }

    internal static Message CloneMessage(Message source)
    {
        Message clone = Message.FromBytes(source.AsReadOnlySpan());
        (byte msgType, ulong correlationId) = source.GetRequestInfo();
        if (msgType == 1)
            clone.SetRequest(correlationId);
        else if (msgType == 2)
            clone.SetReply(correlationId);
        return clone;
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
        return new Received(source.RoutingId, parts);
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
}
