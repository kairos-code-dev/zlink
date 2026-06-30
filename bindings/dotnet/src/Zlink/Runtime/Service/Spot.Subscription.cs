// SPDX-License-Identifier: MPL-2.0

using System.Buffers;
using Systems.Zlink.Runtime.Native;
using Systems.Zlink.Runtime.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot
{
    private TopicMessage SubscribeCore(int flags)
    {
        var topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            if (!ReceiveSpotSubscribedParts(flags, topicBuffer,
                    out var routingId, out var topicLength,
                    out var singlePart, out var parts))
                throw ZlinkException.CreateRecvException((int)ErrorCode.EAgain);
            var topic = DecodeBuffer(topicBuffer, (nuint)topicLength);
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

    private bool SubscribeInto(TopicMessage result, int flags)
    {
        var topicBuffer = result.GetWritableTopicBuffer(TopicBufferSize);
        var received = ReceiveSpotSubscribedParts(flags,
            topicBuffer, out var routingId, out var topicLength,
            out var singlePart, out var parts,
            (flags & 1) != 0);
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
        var initRc = NativeMethods.zlink_msg_init(ref part);
        if (initRc != 0)
            throw ZlinkException.CreateRecvException(
                NativeMethods.zlink_errno());

        var initialized = true;
        try
        {
            fixed (byte* topicPtr = topicBuffer)
            {
                var rc = NativeMethods.zlink_spot_subscribe_part_buffer(
                    Handle, out _, topicPtr, (nuint)topicBuffer.Length,
                    out var nativeTopicLength, ref part,
                    out var nativeHasMore, flags);
                if (rc != 0)
                {
                    var errno = NativeMethods.zlink_errno();
                    if ((flags & DontWaitFlag) != 0
                        && ZlinkException.MapErrorCode(errno)
                            is ErrorCode.EAgain or ErrorCode.EBusy)
                        return false;
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

    private SubscriptionEvent ReceiveSubscriptionEventCore(int flags)
    {
        var topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            var rc = NativeMethods.zlink_spot_recv_subscription_event(Handle,
                out var sourceRoutingId, out var subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out var topicLength, flags);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(
                    NativeMethods.zlink_errno());

            var routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            var routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            var topic = DecodeBuffer(topicBuffer, topicLength);
            return new SubscriptionEvent(routingId, topic, subscribedInt != 0);
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private bool ReceiveSubscriptionEventInto(SubscriptionEvent result,
        int flags)
    {
        var topicBuffer = ArrayPool<byte>.Shared.Rent(TopicBufferSize);
        try
        {
            var rc = NativeMethods.zlink_spot_recv_subscription_event(Handle,
                out var sourceRoutingId, out var subscribedInt, topicBuffer,
                (nuint)topicBuffer.Length, out var topicLength, flags);
            if (rc != 0)
            {
                var errno = NativeMethods.zlink_errno();
                if ((flags & 1) != 0
                    && ZlinkException.MapErrorCode(errno) == ErrorCode.EAgain)
                    return false;
                throw ZlinkException.CreateRecvException(errno);
            }

            var routingIdBytes = CopyRoutingIdBytes(sourceRoutingId);
            var routingId = routingIdBytes == null
                ? null
                : RoutingId.FromOwnedOptionalBytes(routingIdBytes);
            var topic = DecodeBuffer(topicBuffer, topicLength);
            result.Populate(routingId, topic, subscribedInt != 0);
            return true;
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(topicBuffer);
        }
    }

    private SendResult PublishNoWaitSingleCore(string topic,
        Message message)
    {
        return PublishNoWaitSingleCore(GetPublishTopicUtf8(topic), message);
    }

    private unsafe SendResult PublishNoWaitSingleCore(byte[] topicUtf8,
        Message message)
    {
        // HOT PATH: public spot.Publish(topic).Message(message)
        // .Flags(DontWait).Submit() reaches this method for every single-part
        // publish. Keep the path as one native submit after Message.MoveTo; do
        // not add benchmark-only caches that depend on repeated topic or payload
        // values.
        ZlinkMsg nativePart = default;
        var submitted = false;
        try
        {
            message.MoveTo(ref nativePart);
            fixed (byte* topicPtr = topicUtf8)
            {
                var rc = NativeMethods.zlink_spot_publish_part_utf8(Handle,
                    topicPtr, ref nativePart, DontWaitFlag,
                    NativeMethods.ZlinkPartFlag.Final);
                submitted = true;
                if (rc == 0)
                    return SendResult.Sent;
            }

            var sendResult = SendResultErrno.TryMapCurrent();
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