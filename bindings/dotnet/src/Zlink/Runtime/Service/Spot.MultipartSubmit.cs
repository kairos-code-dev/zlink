// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed partial class Spot
{
    private enum SpotMultipartSubmitKind
    {
        Publish,
        SendChannel
    }

    private unsafe void PublishCore(string topic, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        SubmitMultipartCore(SpotMultipartSubmitKind.Publish, topic, parts,
            flags, paramName, mapNoWaitResult: false);
    }

    private unsafe void SendChannelCore(string channelName, ReadOnlySpan<Message> parts,
        int flags, string paramName)
    {
        SubmitMultipartCore(SpotMultipartSubmitKind.SendChannel, channelName,
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
                SpotMultipartSubmitKind.SendChannel => SubmitSingleChannel(
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
            for (int i = 0; i < parts.Length; i++)
            {
                if (parts[i] == null)
                {
                    throw new ArgumentException(
                        "Parts must not contain null messages.", paramName);
                }
            }

            for (int i = 0; i < parts.Length; i++)
            {
                parts[i].MoveTo(ref nativeParts[i]);
                built++;
            }

            byte[]? publishTopicUtf8 = kind == SpotMultipartSubmitKind.Publish
                ? GetPublishTopicUtf8(subject)
                : null;
            for (int i = 0; i < built; i++)
            {
                NativeMethods.ZlinkPartFlag partFlag = i + 1 < built
                    ? NativeMethods.ZlinkPartFlag.More
                    : NativeMethods.ZlinkPartFlag.Final;
                int rc;
                if (kind == SpotMultipartSubmitKind.Publish)
                {
                    fixed (byte* topicPtr = publishTopicUtf8)
                    {
                        rc = NativeMethods.zlink_spot_publish_part_utf8(
                            _handle, topicPtr, ref nativeParts[i], flags,
                            partFlag);
                    }
                }
                else
                {
                    rc = NativeMethods.zlink_spot_send_channel_part(_handle,
                        subject, ref nativeParts[i], flags, partFlag);
                }
                submitted = i + 1;
                if (rc == 0)
                    continue;

                if (mapNoWaitResult)
                {
                    SendResult? sendResult = TryMapSendResultFromErrno();
                    if (sendResult != null)
                    {
                        for (int j = submitted; j < built; j++)
                            parts[j].RestoreFrom(ref nativeParts[j]);
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
            for (int i = submitted; i < built; i++)
                parts[i].RestoreFrom(ref nativeParts[i]);
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
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            part.MoveTo(ref nativePart);
            int rc = NativeMethods.zlink_spot_send_channel_part(_handle,
                channelName, ref nativePart, flags,
                NativeMethods.ZlinkPartFlag.Final);
            submitted = true;
            if (rc == 0)
                return SendResult.Sent;

            if (mapNoWaitResult)
            {
                SendResult? sendResult = TryMapSendResultFromErrno();
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
