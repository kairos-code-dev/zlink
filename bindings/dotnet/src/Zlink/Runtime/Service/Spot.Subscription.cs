// SPDX-License-Identifier: MPL-2.0

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Text;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private unsafe TopicMessage SubscribeCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            if (!ReceiveSpotSubscribedParts(flags, topicBuffer,
                    out RoutingIdSnapshot routingId, out int topicLength,
                    out Message? singlePart, out MultipartMessageCollection? parts))
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            string topic = DecodeBuffer(topicBuffer, (nuint)topicLength);
            if (singlePart != null)
                return new TopicMessage(routingId, topic, singlePart);
            if (parts == null || parts.Count == 0)
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            return new TopicMessage(routingId, topic, parts);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe bool SubscribeInto(TopicMessage result, int flags)
    {
        byte[] topicBuffer = result.GetWritableTopicBuffer(TopicBufferSize);
        bool received = ReceiveSpotSubscribedParts(flags,
            topicBuffer, out RoutingIdSnapshot routingId, out int topicLength,
            out Message? singlePart, out MultipartMessageCollection? parts,
            allowNoData: (flags & 1) != 0);
        if (!received)
            return false;
        if (singlePart != null)
        {
            result.PopulateSinglePartFromWritableTopicBuffer(routingId,
                topicLength, singlePart);
            return true;
        }
        if (parts == null || parts.Count == 0)
            throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
        result.PopulateFromWritableTopicBuffer(routingId, topicLength, parts);
        return true;
    }

    private unsafe bool SubscribePartInto(Message result, Span<byte> topicBuffer,
        out int topicLength, out bool hasMore, int flags)
    {
        topicLength = 0;
        hasMore = false;
        ZlinkMsg part = default;
        int initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        bool initialized = true;
        try
        {
            fixed (byte* topicPtr = topicBuffer)
            {
                int rc = NativeMethods.zlink_spot_subscribe_part_buffer(
                    _handle, out _, topicPtr, (nuint)topicBuffer.Length,
                    out nuint nativeTopicLength, ref part,
                    out int nativeHasMore, flags);
                if (rc != 0)
                {
                    int errno = NativeMethods.zlink_errno();
                    if ((flags & DontWaitFlag) != 0
                        && ZlinkException.MapErrorCode(errno)
                            is ErrorCode.EAgain or ErrorCode.EBusy)
                    {
                        return false;
                    }
                    throw ZlinkException.CreateRecvException(errno);
                }

                topicLength = checked((int)nativeTopicLength);
                hasMore = nativeHasMore != 0;
                result.ReplaceNativeOwned(ref part);
                initialized = false;
                return true;
            }
        }
        finally
        {
            if (initialized)
                NativeMethods.zlink_msg_close(ref part);
        }
    }

    private unsafe SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_spot_recv_subscription_event(_handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, topic, subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe bool ReceiveSubscriptionEventInto(SubscriptionEvent result,
        int flags)
    {
        byte[] topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            int rc = NativeMethods.zlink_spot_recv_subscription_event(_handle,
                out IntPtr sourceRoutingId, out int subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out nuint topicLength, flags);
            if (rc != 0)
            {
                int errno = NativeMethods.zlink_errno();
                if ((flags & 1) != 0
                    && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                    return false;
                throw ZlinkException.CreateRecvException(errno);
            }

            byte[]? routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            RoutingId? routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            string topic = DecodeBuffer(topicBuffer, topicLength);
            result.Populate(routingId, topic, subscribedInt != 0);
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private unsafe SendResult PublishNoWaitSingleCore(string topic,
        Message message)
    {
        return PublishNoWaitSingleCore(GetPublishTopicUtf8(topic), message);
    }

    private unsafe SendResult PublishNoWaitSingleCore(byte[] topicUtf8,
        Message message)
    {
        ZlinkMsg nativePart = default;
        bool submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                int rc = NativeMethods.zlink_spot_publish_part_utf8(_handle,
                    topicPtr, ref nativePart, DontWaitFlag,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc == 0)
                    return SendResult.Sent;
            }

            SendResult? sendResult = SendResultErrno.TryMapCurrent();
            if (sendResult == null)
                throw ZlinkException.CreateSubmitException(
                    NativeMethods.zlink_errno());
            return sendResult.Value;
        }
        catch
        {
            if (!submitted)
                message.RestoreFrom(ref nativePart);
            throw;
        }
    }


}
