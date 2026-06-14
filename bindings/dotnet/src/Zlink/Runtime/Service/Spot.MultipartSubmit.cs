// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private enum SpotMultipartSubmitKind
    {
        Publish,
        SendToChannel
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        SubmitMultipartCore(SpotMultipartSubmitKind.Publish, topic, parts,
            flags, paramName, mapNoWaitResult: false);
    }

    private unsafe void SendToChannelCore(string channelName, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        SubmitMultipartCore(SpotMultipartSubmitKind.SendToChannel, channelName,
            parts, flags, paramName, mapNoWaitResult: false);
    }

    private unsafe SendResult PublishNoWaitCore(string topic,
        ReadOnlySpan<Message> parts, string paramName)
    {
        return SubmitMultipartCore(SpotMultipartSubmitKind.Publish, topic, parts,
            DontWaitFlag, paramName, mapNoWaitResult: true);
    }

    private unsafe SendResult SubmitMultipartCore(SpotMultipartSubmitKind kind,
        string subject, ReadOnlySpan<Message> parts, int flags, string paramName,
        bool mapNoWaitResult)
    {
        if (parts.Length == 1)
        {
            Message part = parts[0] ?? throw new ArgumentException(
                "Parts must not contain null messages.", paramName);
            return kind switch
            {
                SpotMultipartSubmitKind.Publish => mapNoWaitResult
                    ? PublishNoWaitSingleCore(subject, part)
                    : SubmitSinglePublish(subject, part, flags),
                SpotMultipartSubmitKind.SendToChannel => SubmitSingleChannel(
                    subject, part, flags, mapNoWaitResult),
                _ => throw new InvalidOperationException()
            };
        }

        ZlinkMsg[]? rented = null;
        Span<ZlinkMsg> nativeParts = parts.Length <= StackPublishPartLimit
            ? stackalloc ZlinkMsg[StackPublishPartLimit]
            : (rented = ArrayPool<ZlinkMsg>.Shared.Rent(parts.Length));
        nativeParts = nativeParts.Slice(0, parts.Length);

        int built = 0;
        int submitted = 0;
        try
        {
            NativeMessageParts.MoveToNative(parts, nativeParts, paramName,
                ref built);

            // Encode the subject (topic or channel name) to UTF-8 once and pin
            // it for the whole part loop, rather than re-marshalling the managed
            // string on every native call.
            byte[] subjectUtf8 = kind == SpotMultipartSubmitKind.Publish
                ? GetPublishTopicUtf8(subject)
                : GetChannelNameUtf8(subject);
            fixed (byte* subjectPtr = subjectUtf8)
            {
                for (int i = 0; i < built; i++)
                {
                    NativeMethods.ZlinkPartFlag partFlag = i + 1 < built
                        ? NativeMethods.ZlinkPartFlag.More
                        : NativeMethods.ZlinkPartFlag.Final;
                    int rc = kind == SpotMultipartSubmitKind.Publish
                        ? NativeMethods.zlink_spot_publish_part_utf8(_handle,
                            subjectPtr, ref nativeParts[i], flags, partFlag)
                        : NativeMethods.zlink_spot_send_channel_part_utf8(
                            _handle, subjectPtr, ref nativeParts[i], flags,
                            partFlag);
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

    private SendResult SubmitSinglePublish(string topic, Message part, int flags)
    {
        PublishSingleCore(topic, part, flags);
        return SendResult.Sent;
    }

    private unsafe SendResult SubmitSingleChannel(string channelName,
        Message part, int flags, bool mapNoWaitResult)
    {
        byte[] channelNameUtf8 = GetChannelNameUtf8(channelName);
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            part.MoveTo(ref nativePart);
            int rc;
            fixed (byte* channelPtr = channelNameUtf8)
            {
                rc = NativeMethods.zlink_spot_send_channel_part_utf8(_handle,
                    channelPtr, ref nativePart, flags,
                    NativeMethods.ZlinkPartFlag.Final);
            }
            submitted = true;
            if (rc == 0)
                return SendResult.Sent;

            if (mapNoWaitResult)
            {
                SendResult? sendResult = SendResultErrno.TryMapCurrent();
                if (sendResult != null)
                    return sendResult.Value;
            }
            throw ZlinkException.CreateSubmitException(
                NativeMethods.zlink_errno());
        }
        catch
        {
            if (!submitted)
                part.RestoreFrom(ref nativePart);
            throw;
        }
    }
}
