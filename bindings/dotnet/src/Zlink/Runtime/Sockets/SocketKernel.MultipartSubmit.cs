// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Systems.Zlink.Native;

namespace Systems.Zlink.Sockets.Internal;

internal sealed partial class SocketKernel
{
    private enum MultipartSubmitKind
    {
        Send,
        Publish,
        RoutedSend
    }

    private unsafe void SendCore(ReadOnlySpan<Message> parts, int flags,
        string paramName)
    {
        ZlinkRoutingId unused = default;
        SubmitMultipartCore(MultipartSubmitKind.Send, null, ref unused, parts,
            flags, paramName, mapNoWaitResult: false);
    }

    private unsafe SendResult SendNoWaitResultCore(ReadOnlySpan<Message> parts,
        string paramName)
    {
        ZlinkRoutingId unused = default;
        return SubmitMultipartCore(MultipartSubmitKind.Send, null, ref unused,
            parts, DontWaitFlag, paramName, mapNoWaitResult: true);
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        ZlinkRoutingId unused = default;
        SubmitMultipartCore(MultipartSubmitKind.Publish, topic, ref unused,
            parts, flags, paramName, mapNoWaitResult: false);
    }

    private unsafe SendResult PublishNoWaitCore(string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        ZlinkRoutingId unused = default;
        return SubmitMultipartCore(MultipartSubmitKind.Publish, topic,
            ref unused, parts, DontWaitFlag, paramName, mapNoWaitResult: true);
    }

    private unsafe void SendCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, int flags, string paramName)
    {
        SubmitMultipartCore(MultipartSubmitKind.RoutedSend, null, ref routingId,
            parts, flags, paramName, mapNoWaitResult: false);
    }

    private unsafe SendResult SendNoWaitResultCore(ref ZlinkRoutingId routingId,
        ReadOnlySpan<Message> parts, string paramName)
    {
        return SubmitMultipartCore(MultipartSubmitKind.RoutedSend, null,
            ref routingId, parts, DontWaitFlag, paramName,
            mapNoWaitResult: true);
    }

    private unsafe SendResult SubmitMultipartCore(MultipartSubmitKind kind,
        string? topic, ref ZlinkRoutingId routingId, ReadOnlySpan<Message> parts,
        int flags, string paramName, bool mapNoWaitResult)
    {
        if (parts.Length == 1)
        {
            Message part = parts[0] ?? throw new ArgumentException(
                "Parts must not contain null messages.", paramName);
            return kind switch
            {
                MultipartSubmitKind.Send => mapNoWaitResult
                    ? SendSingleResultCore(part, flags)
                    : SubmitSingle(part, flags),
                MultipartSubmitKind.Publish => mapNoWaitResult
                    ? PublishNoWaitSingleCore(topic!, part)
                    : SubmitSinglePublish(topic!, part, flags),
                MultipartSubmitKind.RoutedSend => mapNoWaitResult
                    ? SendSingleResultCore(ref routingId, part, flags)
                    : SubmitSingle(ref routingId, part, flags),
                _ => throw new InvalidOperationException()
            };
        }

        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackSendPartLimit
            ? stackalloc ZlinkMsg[StackSendPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            NativeMessageParts.MoveToNative(parts, nativeParts, paramName,
                ref built);
            byte[]? publishTopicUtf8 = kind == MultipartSubmitKind.Publish
                ? GetPublishTopicUtf8(topic!)
                : null;
            for (int i = 0; i < built; i++)
            {
                NativeMethods.ZlinkPartFlag partFlag = i + 1 < built
                    ? NativeMethods.ZlinkPartFlag.More
                    : NativeMethods.ZlinkPartFlag.Final;
                int rc;
                if (kind == MultipartSubmitKind.Publish)
                {
                    fixed (byte* topicPtr = publishTopicUtf8)
                    {
                        rc = NativeMethods.zlink_publish_part_utf8(Handle,
                            topicPtr, ref nativeParts[i], flags, partFlag);
                    }
                }
                else
                {
                    rc = kind switch
                    {
                        MultipartSubmitKind.Send => NativeMethods.zlink_send_part(
                            Handle, ref nativeParts[i], flags, partFlag),
                        MultipartSubmitKind.RoutedSend =>
                            NativeMethods.zlink_send_part_rid(Handle,
                                ref routingId, ref nativeParts[i], flags, partFlag),
                        _ => throw new InvalidOperationException()
                    };
                }
                submitted = i + 1;
                if (rc == 0)
                    continue;

                if (mapNoWaitResult)
                {
                    SendResult? sendResult = SendResultErrno.TryMapCurrent();
                    if (sendResult != null)
                    {
                        NativeMessageParts.RestoreManaged(parts, nativeParts,
                            submitted, built - submitted);
                        return sendResult.Value;
                    }
                }
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            }
            return SendResult.Sent;
        }
        catch
        {
            NativeMessageParts.RestoreManaged(parts, nativeParts, submitted,
                built - submitted);
            throw;
        }
        finally
        {
            if (rented != null)
                ArrayPool<ZlinkMsg>.Shared.Return(rented);
        }
    }

    private SendResult SubmitSingle(Message part, int flags)
    {
        SendSingleCore(part, flags);
        return SendResult.Sent;
    }

    private SendResult SubmitSingle(ref ZlinkRoutingId routingId, Message part,
        int flags)
    {
        SendSingleCore(ref routingId, part, flags);
        return SendResult.Sent;
    }

    private SendResult SubmitSinglePublish(string topic, Message part, int flags)
    {
        PublishSingleCore(topic, part, flags);
        return SendResult.Sent;
    }
}
